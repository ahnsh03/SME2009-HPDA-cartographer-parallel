# CPU bag 성능 검토 — 구현 부족 vs 구조적 한계

> **질문:** bag에서 CPU가 microbench보다 나빠진 이유가 OpenMP 구현이 부족해서인가?  
> 그렇다면 “bag cumulative KPI” 주장의 신뢰도가 떨어지지 않는가?

**결론:** bag에서의 CPU 열세는 **구현 미숙이 주원인이 아니다.** 동일 bag·동일 ROS 환경에서 **CPU(L6) vs GPU(L7)를 비교하면 GPU가 ~2.3× 빠르다**는 사실은 유효하며, KPI로 bag cumulative를 쓰는 것은 오히려 **공정한 비교**이다.

---

## 1. “CPU 코드가 부족하다”고 보기 어려운 근거

### 1.1 격리 환경에서는 CPU 구현이 충분히 빠르다

| 측정 | n=256 CPU | 비고 |
|------|----------:|------|
| Jetson 연속 microbench | **0.68 ms** | `pa01_bench_20260531_080435_sweep.csv` |
| Jetson bag-like microbench | **0.64 ms** | 호출 1회씩 (`112002`) |
| 우수 PA01 (이승빈) microbench | 1.05 ms (n=330) | 동일 OpenMP 패턴 |

동일 Jetson·동일 `score_all`에서 **본인 CPU microbench는 우수 과제보다 빠르거나 동급**이다. OpenMP·루프 교환·`ScoreN4`·cx/cy hoisting이 “미완성”이라기보다 **ablation으로 검증된 CPU 최종(L6)** 이다.

### 1.2 ablation으로 잘못된 기법은 기각했다

| Level | 기법 | n=256 판정 |
|-------|------|-----------|
| L1 | LICM | 기각 (미미) |
| L2 | **루프 교환** | **채택** (11→7 ms) |
| L3 | prefetch | 기각 |
| L4~5 | branchless | 기각 |
| L6 | OpenMP + `ScoreN4` dispatch | **채택** |

“최적화를 안 해서”가 아니라, **Jetson에서 먹히는 것만 남긴 상태**다.

### 1.3 bag에서도 CPU 최종(L6)은 baseline 대비 크게 개선됨

`pa01_opt6_best` bag, n=256 `path=omp_cand` **23,325회** 평균:

| | n=256 avg (bag) |
|--|----------------:|
| L0 baseline | ~11.1 ms |
| L2 loop interchange | ~7.0 ms |
| **L6 OpenMP** | **~2.0 ms** |

CPU 경로만 놓고도 bag 안에서 **5× 이상** 개선되었다. “bag에서 CPU가 느리다”는 **절대 속도** 이야기이지, **최적화가 안 먹혔다**는 뜻이 아니다.

---

## 2. microbench 0.68 ms vs bag 2.0 ms — 3× 차이의 원인

**같은 Jetson, 같은 L6 `ScoreOmpCandidates` 코드**인데 환경만 다르다.

| 요인 | microbench | bag launch | CPU에 미치는 영향 |
|------|------------|------------|-------------------|
| 빌드 | `-DPA01_NO_LOG=1` (`benchmark/Makefile`) | 로그 **ON** (`[score_all]` 10만 줄+) | timed 밖이지만 stderr·캐시 간섭 |
| 프로세스 | 단독 실행 파일 | ROS node + rosbag + 다중 스레드 | CPU contention |
| 호출 패턴 | **n=256만** 연속 수백 회 | **n=4(71%) ↔ n=256(27%)** 교차 | locality·분기 예측 악화 |
| 주변 비용 | `score_all`만 | `Score` vector alloc, B&B, chrono | bag 실측에 포함 (GPU도 동일 환경) |
| GPU grid | 매 run H2D 성분 | **pointer cache** → GPU만 bag에서 유리 |

핵심: **CPU는 bag 환경에서 microbench 대비 ~3× 악화**, **GPU는 bag에서 microbench 대비 ~1.6× 개선** (1.40→0.86 ms).  
→ crossover가 “내려온” 것은 CPU가 특별히 못해서가 아니라, **GPU만 bag 맥락에서 상대적으로 이득**을 보기 때문이다.

> **OpenMP fork–join, Linux CFS, Jetson unified memory, 캐시 interference** 등 시스템·런타임 층의 상세 설명:  
> [02d_system_os_openmp_limits.md](02d_system_os_openmp_limits.md)

### 공정한 A/B는 “bag 안에서” 한다

| 경로 | bag n=256 avg (동일 ROS run) |
|------|------------------------------:|
| L6 `omp_cand` | **~2.00 ms** |
| L7 `cuda` | **~0.86 ms** |
| L8 `omp_cand` (opt8) | **~1.94 ms** |

**microbench CPU 0.68 vs GPU 1.40**은 GPU에게 불리한 시험 조건(H2D·launch 매번)이다.  
**bag CPU 2.0 vs GPU 0.86**은 같은 SLAM 파이프라인에서의 정면 비교다.  
→ KPI를 bag cumulative로 잡는 것은 “CPU 구현 핑계”가 아니라 **동일 조건에서의 정책 선택**이다.

---

## 3. 남아 있는 구현 여지 (있지만 crossover를 뒤집지는 못함)

솔직히 말할 수 있는 **미세 튜닝**은 있다. 다만 bag CPU 2.0→1.6 ms 수준이지, GPU 0.86 ms를 역전시키기는 어렵다.

| 항목 | 현재 | 가능 개선 | 기대 |
|------|------|----------|------|
| OpenMP schedule | `dynamic, 64` | `static` (우수 과제와 동일) | fork/sched 오버헤드 소폭 감소 |
| 로그 | bag에서 매 호출 `cerr` | `PA01_NO_LOG` (측정 전용) | I/O 간섭 감소 |
| OpenMP 범위 | `score_all` 호출마다 parallel region | `Match` 단위 parallel once | 구조 변경, 과제 범위 초과 |

`schedule(static)`으로 20–30% 개선을 가정해도 bag n=256 CPU **~1.4 ms** vs GPU **~0.86 ms** — **여전히 GPU 승**.

---

## 4. KPI 신뢰도 — 왜 오히려 bag가 맞는가

### 잘못된 비판

> “CPU 코드가 bag에서만 느려지니, bag KPI를 고집하는 것은 CPU 구현 부족을 숨기려는 것이다.”

### 반박

1. **CPU 구현 부족이 아니라 측정 환경 차이** — microbench는 CPU에 유리한 lab 조건, bag는 SLAM 실조건.
2. **GPU hybrid 채택은 bag A/B로 결정** — CPU를 “좋게 보이게” 한 선택이 아님. T=2048(CPU @ n=256)이면 cumulative **+38%**.
3. **CPU도 bag에서 충분히 최적화됨** — L0 11 ms → L6 2 ms (동일 bag).
4. **신뢰도를 높이는 비교** — 같은 launch에서 `path=omp_cand` vs `path=cuda`를 보는 것이, lab microbench 승자를 그대로 쓰는 것보다 **교수·동료가 납득할 근거**가 강하다.

### 보고서용 한 문단

> 연속 microbench에서 CPU는 n=256에 0.68 ms로 GPU(1.40 ms)보다 빠르게 측정되었으나, 이는 `PA01_NO_LOG`·단독 프로세스·n 고정 연속 호출이라는 유리한 조건 때문이다. 동일 Jetson·동일 L6 빌드의 bag 로그에서 n=256 `omp_cand` 23,325회 평균은 2.00 ms이며, 이는 baseline(11.1 ms) 대비 5× 이상 개선된 **CPU 최종 구현의 bag 실측**이다. 같은 bag 환경의 GPU(L7) n=256 cuda 평균 0.86 ms와 비교하면 GPU가 약 2.3× 빠르므로, GPU dispatch threshold는 microbench crossover(n≥2048)가 아니라 **bag 정면 비교**로 확정하였다. 따라서 bag cumulative KPI는 CPU 구현 미숙을 감추는 선택이 아니라, SLAM 실조건에서 검증된 hybrid 정책을 반영한 것이다.

---

## 5. 관련 데이터

| 파일 | 내용 |
|------|------|
| `data/pa01/pa01_opt6_best_clean.log` | bag L6 n=256 omp 평균 ~2.0 ms |
| `data/pa01/pa01_opt7_gpu_hybrid_clean.log` | bag L7 n=256 cuda 평균 ~0.86 ms |
| `data/bench/pa01_bench_20260531_080435_sweep.csv` | microbench crossover n≥2048 |
| `benchmark/Makefile` | microbench `-DPA01_NO_LOG=1` |
| `docs/PA01_CPU_VERIFICATION.md` | L6 ablation·확정 근거 |
