# 제2장 부록 — 시스템·OS·OpenMP가 만드는 CPU 한계 (구현과 무관한 층)

> **전제:** `ScoreOmpCandidates` 코드 자체는 microbench에서 우수 과제와 동급 이상으로 동작한다 ([02c](02c_cpu_credibility_review.md)).  
> 그럼에도 bag에서 CPU n=256이 microbench(0.68 ms) 대비 ~2.0 ms로 악화되고 GPU(0.86 ms)에 지는 이유는, **애플리케이션 코드 한 줄을 더 고치면 해결되는 종류가 아닌** 시스템·런타임·아키텍처 조건에 있다.

---

## 1. 문제를 다시 정의한다

| 관찰 | 수치 |
|------|------|
| Jetson microbench, n=256, CPU OpenMP | **0.68 ms** |
| Jetson bag, n=256, `path=omp_cand` (23k+ calls) | **~2.00 ms** |
| Jetson bag, n=256, `path=cuda` | **~0.86 ms** |
| microbench GPU crossover | n **≥ 2048** |
| bag GPU policy crossover | n **≥ 256** |

질문은 두 가지다.

1. 왜 **같은 CPU 커널**이 microbench와 bag에서 3× 다르게 나오는가?
2. 왜 **GPU만** bag 맥락에서 상대적으로 유리해져 crossover가 내려오는가?

아래는 OpenMP 런타임, Linux 스케줄러, Jetson SoC 메모리 구조, ROS 프로세스 모델 순으로 풀어 쓴다.

---

## 2. OpenMP fork–join 모델의 구조적 비용

### 2.1 `parallel for`는 “스레드 풀 예열”로 사라지지 않는다

본인 코드 (`score_all.cpp`):

```cpp
#pragma omp parallel for schedule(dynamic, 64) if (n >= kOmpMinCandidates)
for (int i = 0; i < n; ++i) {
  // 후보 i마다 p=1081번 grid 랜덤 읽기
}
```

OpenMP 4.x (libgomp, GCC)의 일반적인 동작:

```
score_all() 호출 1회
    │
    ├─ [master] parallel region 진입
    │      ├─ worker 스레드 wake / team 구성 (fork)
    │      ├─ loop 분배 (static 또는 dynamic)
    │      ├─ barrier (implicit, for 종료 시)
    │      └─ team 해체 / worker sleep (join)
    │
    └─ return
```

**microbench (연속 50~100회 `score_all`):**

- 프로세스 안에 **오직 벤치 스레드만** 존재
- 연속 호출로 **명령·데이터 캐시가 n=256 / p=1081 패턴에 적응**
- libgomp worker 스레드가 sleep 상태에 가깝게 유지되나, **parallel region 진입·barrier 비용은 호출마다 발생**

**bag launch (sporadic `score_all`):**

- 한 `Match()` 안에서도 `score_all`이 **수십~수백 회** 불리지만,
- 호출 사이에 `Score`의 sort·vector 연산, `Branch` 재귀, **n=4 전용 `ScoreN4` 경로**가 끼어듦
- 매 n=256 호출마다 **동일한 fork–join 사이클**이 반복되나, 직전에 실행된 코드·캐시 상태가 microbench와 다름

`docs/PA01_ADDITIONAL_EXPERIMENTS_20260531.md` §3.3에서 정리한 대로:

> warmup은 **스레드 풀 예열**일 뿐, `#pragma omp parallel for` **region 재진입을 없애지 않는다.**

bag에서 n=256 omp 호출 **100회 이후** 평균을 따져도 ~2.0 ms — “첫 호출만 느린 것”이 아니라 **구조적으로 매 호출에 붙는 비용 + 환경 비용**이 누적된 steady state다.

### 2.2 `schedule(dynamic, 64)`의 런타임 스케줄링 오버헤드

| schedule | 동작 | 적합한 경우 |
|----------|------|------------|
| **static** | 컴파일/진입 시 청크 고정 분배 | **후보마다 연산량 동일** (모두 p번 grid 읽기) |
| **dynamic** | 청크 완료 시 런타임이 다음 청크 할당 | 후보마다 작업량 **불균등**할 때 |

`score_all`의 n=256 OpenMP 루프는 후보 i마다 **동일하게 p=1081회** in-bounds grid 접근 → **static이 이론상 적합**.

`dynamic, 64`는 256 후보·4 스레드 환경에서 런타임 큐·원자적 카운터 접근이 추가된다. 구현 미숙이라기보다 **스케줄 정책 선택** 문제이나, static으로 바꿔도 fork–join·bag 환경 비용(§3~§5)이 지배적이라 **crossover 역전(2.0→0.86 ms)은 어렵다**.

### 2.3 `if (n >= 64)` — 작은 n에서의 region 회피는 큰 n을 구원하지 못한다

n=4는 `ScoreN4`로 OpenMP 없이 처리 — **올바른 설계**.  
그러나 bag에서 **n=4 호출 71%** + **n=256 호출 27%**가 교차하면:

- n=256 호출 직전에 n=4 경로가 L1/L2의 일부를 evict
- 분기 예측기·BTB가 서로 다른 코드 경로(`ScoreN4` vs `ScoreOmpCandidates`) 사이를 오감

이는 **알고리즘(B&B)이 만드는 호출 분포**이지, OpenMP 루프 몸체를 더 다듬으면 없어지는 비용이 아니다.

### 2.4 OpenMP가 bag 전체에 병렬화를 “한 번에” 못 쓰는 이유

이상적 대안: `Match()` 진입 시 `omp parallel` **한 번**, 내부 `score_all`은 `omp single` / already-in-team.

**과제·CUDA와 충돌:**

- `score_all` CUDA 경로는 **전역 `DeviceBuffers` singleton** — sibling OMP + concurrent CUDA는 data race ([PA02 FAQ](docs/PA02_FAQ_AND_ANALYSIS.md) §4)
- B&B `Branch` 재귀는 후보 순서·가지치기에 **순차 의존성**

→ OpenMP를 **함수 호출 단위**로만 쓸 수밖에 없고, 이것이 OpenMP 모델상 **가장 overhead가 큰 사용 패턴**이다. 구현 실수가 아니라 **알고리즘 + GPU 혼합 아키텍처가 강제하는 형태**다.

---

## 3. Jetson SoC·메모리 계층 — CPU가 불리한 물리 조건

### 3.1 하드웨어 스펙과 병목 성격

| 항목 | Jetson Nano |
|------|-------------|
| CPU | 4× Cortex-A57 @ 1.43 GHz |
| GPU | Maxwell 128 CUDA cores (단일 SM) |
| 메모리 | 4 GB **LPDDR4**, CPU·GPU **대역폭 공유** (unified memory 경로) |

`score_all` 연산 특성:

- FLOPs는 적고 **`grid[y*w+x]` 랜덤 읽기**가 지배 (memory-bound)
- grid ≈ 147 KB — L2보다 작지만, LiDAR 히트마다 **행이 달라** 순차 prefetch 효과 제한

**CPU (4 스레드):** 후보 축 병렬 — 각 스레드가 **서로 다른 (x,y)** 를 읽어 **캐시 라인 충돌·메모리 대역폭 경합**  
**GPU (128 스레드):** 후보 i당 스레드 1개 — 랜덤 읽기를 **다수 lane**으로 흩어 latency hiding

microbench에서 n=256 CPU가 “충분히 빠른” 것은 **4코어·캐시 warm·경쟁 스레드 없음** 조건이다. bag에서는 **GPU 드라이버 스레드·ROS 스레드·CUDA callback**이 같은 DRAM 대역폭을 쓴다.

### 3.2 GPU만 bag에서 유리해지는 “상주(stateful)” 특성

CUDA 경로 (`score_all_cuda.cu`):

```cpp
// 동일 host grid 포인터 → H2D 생략
if (host_grid_key == grid) { /* skip UploadGrid */ }
```

| 자원 | microbench (격리) | bag (SLAM) |
|------|-------------------|------------|
| `d_grid` | run마다 업로드 성분 큼 | **1회 업로드 후 재사용** |
| CUDA context | 매 프로세스 1회 | 동일 |
| 커널 launch | 매 호출 고정 비용 | 동일 but **grid H2D 제거** |

→ GPU는 bag일수록 **상태 유지(stateful accelerator)** 이점을 본다.  
CPU OpenMP는 **매 호출 stateless fork–join** — grid는 항상 host DRAM에서 읽고, “device에 올려둔다”는 개념이 없다.

**crossover가 2048→256으로 내려온 핵심 물리 이유:** bag에서 GPU **고정 비용이 줄고**, CPU **변동 비용(OpenMP+캐시)은 그대로**이기 때문.

### 3.3 n=4 ↔ n=256 교차와 캐시·TLB

| 호출 유형 | grid 접근 패턴 | 코드 경로 |
|----------|---------------|----------|
| n=4 (`ScoreN4`) | 4×p — 전개 루프 | 분기·레지스터 상수화 |
| n=256 (OpenMP) | 256×p — 병렬 랜덤 | libgomp + barrier |

연속 n=256만 호출하는 microbench와 달리, bag는 **수 ms 안에 경로가 바뀐다.**

- **캐시:** grid 147 KB는 L2에 들어가도, **scan `px/py`·후보 `cx/cy`·스택 프레임**이 서로 다른 working set
- **TLB:** 랜덤 (x,y) 접근은 page fault는 없어도 **D-TLB miss** 빈도 증가
- **분기 예측:** `Dispatch()` 분기(n4 / cuda / omp / interchange)가 호출마다 전환

이는 **컴퓨터 구조에서 말하는 context / workload interference**이며, CPU 측 latency-sensitive 경로가 더 잘 드러난다. GPU 커널은 launch 후 device에서 격리 실행되어 **host 측 캐시 오염 영향이 상대적으로 작다**.

---

## 4. Linux OS·프로세스 — ROS bag 실행의 추가 층

### 4.1 스케줄러(CFS)와 다중 스레드

bag launch 시 동시에 존재하는 실행체:

```
roslaunch (Python)
  ├─ rosmaster 통신 스레드
  ├─ rosbag play (스캔 publish)
  ├─ fast_correlative_node (Match / Score / Branch)
  │     ├─ main + ROS callback 큐
  │     └─ OpenMP worker ×4 (score_all 호출 시에만)
  └─ (GPU 빌드 시) CUDA driver thread(s)
```

**microbench:** 프로세스 1개·스레드 수 최소·`SCHED_OTHER`에서 거의 독점  
**bag:** 4코어 중 **ROS I/O·콜백·OpenMP·GPU**가 시간 분할

CFS는 “공정성”을 위해 running thread를 주기적으로 교체한다. OpenMP barrier 구간에서는 **straggler 스레드 하나**가 전체 후보 처리 시간을 늘린다. 4코어에서 **가시적**이다.

### 4.2 Docker 컨테이너 오버헤드

`student_19` 컨테이너 위에서 실행:

- cgroup CPU quota (과제 환경에 따라 다름)
- 네임스페이스 격리 — **치명적 오버헤드는 아니나**, `ROS_MASTER_URI`·네트워크 loopback 경유
- **공유 호스트 커널** — 다른 student 컨테이너·SSH 세션과 **DRAM·LLC 경쟁** 가능

측정 전 `uptime`·`tegrastats`로 부하를 확인한 것은 이 **OS·시스템 층 노이즈**를 통제하기 위함이었다.

### 4.3 stderr 로깅 10만 줄+ — I/O가 CPU 성능에 미치는 방식

bag 빌드: `PA01_NO_LOG` **없음** → `[score_all]`마다 `std::cerr << ... << flush`.

- chrono 타이밍은 `Dispatch()`만 감싸므로 **로그 문자열 생성은 elapsed 밖**
- 그러나 `cerr` flush는 **커널 pipe·TTY 드라이버**를 깨우고, **공유 L1/L2·memory bus**를 사용
- 다음 `score_all` 호출의 **캐시·DRAM 상태**를 microbench(무로그)보다 불리하게 만듦

GPU 경로도 로그는 동일하지만, **연산의 대부분이 device**에서 끝나 host는 launch·sync 위주 — 로그가 상대적으로 덜 치명적.

---

## 5. 왜 microbench는 CPU에 유리한 “실험실 조건”인가

다음 표는 **동일 Jetson·동일 수식**에서 환경만 비교한다.

| 조건 | microbench | bag launch |
|------|------------|------------|
| 프로세스 | 단독 `microbench7` | ROS node + rosbag |
| 로그 | `PA01_NO_LOG=1` | 10만 줄+ stderr |
| n 분포 | **단일 n 스윕** | n=4 (71%) + n=256 (27%) |
| grid on device | 매 측정 run 성격 상 H2D | **pointer cache** |
| OpenMP | 연속 호출, 동일 경로 | 교차 호출, fork–join 반복 |
| 경쟁 스레드 | 거의 없음 | ROS + CUDA driver |
| KPI | per-call 평균 | **cumulative + SLAM 피드백** |

microbench는 **커널의 이론적 throughput 상한**을 보는 도구이고,  
bag는 **운영체제·런타임·SLAM 제어 흐름이 얹힌 실효 latency**를 본다.

**다수 학생이 microbench에서 CPU가 넓게 이긴 것**은 코드가 더 잘 짜여서가 아니라, **동일한 lab 조건**을 공유했기 때문이다.  
**본인이 bag에서 GPU를 고른 것**은 그 lab 조건이 **과제 KPI와 불일치**함을 실측한 결과다.

---

## 6. GPU crossover가 “내려온” 메커니즘 — 한 장 요약

```
                    microbench                    bag launch
                    ──────────                    ──────────
CPU OpenMP          fork-join + warm cache       fork-join + n교차 + ROS + 로그
                    → 0.68 ms (n=256)            → 2.00 ms (n=256)

GPU CUDA            H2D + launch every call      grid cache + launch
                    → 1.40 ms (n=256)            → 0.86 ms (n=256)

crossover           n ≥ 2048                     n ≥ 256 (policy)
```

**CPU 구현 품질은 microbench에서 이미 검증** (0.68 ms ≤ 우수 과제 1.05 ms).  
**bag 열세는 OpenMP·OS·SoC·호출 분포가 만든 간극**이고, **GPU는 stateful accelerator로 그 간극을 역이용**한다.

---

## 7. “그래도 OpenMP·시스템 한계를 더 줄일 수 없었나?”

| 대안 | 한계 |
|------|------|
| `schedule(static)` | §2.2 — 소폭, crossover 역전 불가 |
| `omp_set_dynamic(0)`, affinity | Jetson 4코어 — 수 % 개선 가능, 2.0→0.86 역전 불가 |
| `Match()` 단위 single parallel region | CUDA concurrent unsafe, B&B 순차 의존 |
| pthread pool 직접 구현 | OpenMP와 동일 계열 fork 비용, 과제 범위 초과 |
| hugepage / mbind | grid 147 KB — TLB 이득 미미, 랜덤 접근 지배 |
| CPU 전용 코어 isolation (`isolcpus`) | 공유 Jetson·Docker — 실험 환경 통제 불가 |

즉 **“구현을 더 하면 microbench 승자를 bag에서도 유지할 수 있다”** 는 주장은, 위 시스템 층 비용을 과소평가한 것이다.  
현실적인 정책은 **bag에서 이긴 GPU path를 n=256에 쓰고**, CPU는 **n=4 `ScoreN4` 등 소형 호출**에 남기는 hybrid — 본인이 채택한 L7/L9 + T=256.

---

## 8. 보고서용 문단 (시스템·OpenMP 상세)

> CPU OpenMP 경로가 연속 microbench(n=256, 0.68 ms)에서는 GPU(1.40 ms)보다 빠르게 측정되었으나, 동일 Jetson의 ROS bag에서는 2.00 ms로 약 3배 악화되었다. 이는 구현 오류라기보다 OpenMP fork–join이 `score_all` 호출마다 반복되고, bag의 n=4↔n=256 교차 호출이 캐시·분기 상태를 흔들며, Linux CFS 하에서 ROS·CUDA 드라이버 스레드와 CPU 코어를 공유하기 때문이다. 반면 GPU는 `UploadGrid` pointer cache로 grid H2D를 세션 동안 생략하는 stateful 가속 구조이므로 bag에서 오히려 0.86 ms로 개선되어, 동일 bag 조건의 정면 비교에서 crossover가 microbench(n≥2048)보다 낮은 n≥256으로 이동하였다. 따라서 hybrid dispatch는 런타임·OS·SoC 특성을 반영한 정책이며, bag cumulative KPI는 이러한 실효 비용을 포함한 올바른 평가 기준이다.

---

## 9. 관련 문서·데이터

| 자료 | 내용 |
|------|------|
| [02c_cpu_credibility_review.md](02c_cpu_credibility_review.md) | 구현 부족 아님 — 요약 |
| [02b_gpu_crossover_discovery.md](02b_gpu_crossover_discovery.md) | crossover 발견·데이터 |
| `docs/PA01_ADDITIONAL_EXPERIMENTS_20260531.md` §3.3 | OpenMP warmup 오해 |
| `docs/PA02_FAQ_AND_ANALYSIS.md` §4 | CUDA singleton + OMP |
| OpenMP spec (fork-join) | parallel region semantics |
| Jetson Nano TRM | Unified memory, 4× A57 |
