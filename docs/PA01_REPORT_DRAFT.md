# PA01 보고서 (표지 포함 6페이지 이내)

> Jetson Nano `student_19`, Docker `student_19`, `data/pa01_*_clean.log`  
> Fig.1~3: `docs/figures/` (PDF 본문 삽입)

---

# 제1장. 서론 및 문제 정의

## 1.1 Google Cartographer LiDAR SLAM과 스캔 매칭 연산부 개요

본 과제는 Google Cartographer 기반 2D LiDAR SLAM 패키지 `cartographer_parallel`을 대상으로 한다. Fast Correlative Scan Matcher는 수신 스캔과 기구축 격자 지도(probability grid)의 일치도를 후보 pose마다 평가한다. 상위 모듈 `fast_matcher.cpp`의 `Score`·`Branch` 재귀 안에서 **`score_all()`이 반복 호출**되며, 본 보고서는 이 함수 **하나**에 대해 CPU·GPU 고속화와 Jetson Nano 상 정량 비교를 수행한다. `fast_matcher`의 분기·정렬 로직은 변경하지 않았다.

## 1.2 `score_all()` 수학적 구조와 베이스라인 코드의 병목 해부

### 수식과 복잡도

후보 \(i=0,\ldots,n-1\), 스캔 점 \(j=0,\ldots,p-1\)에 대해

\[
\text{score}[i] = \frac{1}{255 \cdot p} \sum_{j} \mathbf{1}_{\text{in}}(x_{ij}, y_{ij}) \cdot \text{grid}[y_{ij} \cdot w + x_{ij}]
\]

\(x_{ij}=px_j+cx_i\), \(y_{ij}=py_j+cy_i\)이며 연산량은 \(\Theta(n \times p)\)이다. 본 bag에서 \(p \approx 1081\), 지도는 \(467 \times 314\) (약 147KB), 후보 수 \(n\)은 SLAM 단계에 따라 **4, 256** 등으로 달라진다.

### 베이스라인(level 0) 루프 구조

```cpp
for (int i = 0; i < n; ++i) {           // 외측: 후보
  int sum = 0;
  for (int j = 0; j < p; ++j) {       // 내측: 스캔 점
    const int x = px[j] + cx[i];
    const int y = py[j] + cy[i];
    if (x >= 0 && x < w && y >= 0 && y < h)
      sum += grid[y * w + x];
  }
  (*score)[i] = (float)sum / (255.0f * (float)p);
}
```

### 병목이 발생하는 코드 위치와 이유

| # | 코드 위치 | 현상 | Jetson(A57)에서의 의미 |
|---|-----------|------|------------------------|
| **B1** | `grid[y*w+x]` (내측 루프) | **비순차·랜덤** 주소 접근 | LiDAR 히트마다 행이 달라 캐시 미스·메모리 대역폭이 지배. **핵심 병목.** |
| **B2** | 루프 순서 **i 바깥 · j 안쪽** | 후보가 바뀔 때마다 스캔 \(p\)개를 다시 읽음 | \(n=256\)이면 스캔 배열을 사실상 \(256\)번 재순회 → 불필요한 트래픽 |
| **B3** | `sum / (255*p)` | 후보 \(i\)마다 float 나눗셈 | \(p\)는 호출 내 불변(LICM 후보)이나 원본은 매번 나눗셈 → **부차** |
| **B4** | `if (x>=0 && …)` 4조건 | in-bounds마다 분기 패턴 상이 | 분기 예측 실패 시 스톨 → **부차** |

연산 횟수(FLOPs)만 보면 단순해 보이지만, 실제로는 **B1의 랜덤 grid 읽기**와 **B2로 인한 스캔 데이터 재사용 실패**가 성능을 막는다. \(n \times p\)가 커질수록(특히 **n=256**) 두 효과가 곱해진다.

### 상위 모듈과의 관계

`FastMatcher::Score`는 스캔별로 후보 \(cx, cy\)를 모아 `score_all`을 한 번 호출한다. `Branch`는 후보를 \(2 \times 2\)로 쪼개며 깊이를 내려가 **n=4(초기)** → **n=256(분기 후)** 같은 패턴을 만든다. 따라서 “한 번의 `score_all`이 빨라졌는가”와 “같은 bag에서 `score_all`이 몇 번 불렸는가”를 **로그로 분리**해 읽어야 한다(제5장).

## 1.3 설계 방향: CPU ablation → CPU 최종(L6) → GPU 하이브리드(L7)

본 과제는 한 번에 모든 최적화를 넣지 않고, **병목을 나눠 실험한 뒤** 최종적으로 **후보 수 n에 따라 CPU·GPU를 나누는 하이브리드**로 마무리하였다.

| 단계 | `PA01_OPT_LEVEL` | 목표 |
|------|------------------|------|
| ① | 0 | 베이스라인 측정, n별 호출·시간 비중 파악 |
| ② | 1~5 | B2~B4를 **각각 분리** 적용·측정 (ablation) |
| ③ | 6 | 유효 기법 통합 + **n별 dispatch** + OpenMP(\(n \ge 64\)) |
| ④ | 7 | L6의 작은 n 경로 유지 + **\(n \ge 64\)만 CUDA** |

Jetson처럼 CPU·GPU가 메모리 대역폭을 공유하는 환경에서는 **모든 n을 GPU로 보내는 것이 항상 이득이 아니다.** 커널 런치, Host↔Device 복사, 스트림 동기화 같은 **고정 비용**이 있기 때문이다. 반면 후보가 많을 때는 grid 읽기가 병렬로 흩어져 GPU가 유리해진다. 아래 표가 Level 7의 **분기 원칙**이다.

| n | 연산량 \(n \times p\) | Level 6 (CPU 최종) | Level 7 | 왜 이렇게 나누었는가 |
|---|----------------------|-------------------|---------|---------------------|
| **4** | 4,324 | `ScoreN4` (~0.10 ms) | **동일 CPU** (`path=n4`) | 호출은 **70%**지만 연산은 작다. GPU를 쓰면 런치·전송·sync가 연산보다 커져 **이득이 없을 것**으로 보고 CPU 전용 커널을 유지했다. |
| 8~63 | 중간 | `ScoreInterchange` | CPU | OpenMP·CUDA 이득 대비 분기·전송 부담이 상대적으로 큼 |
| **≥64 (실측 256)** | 276,736 | OpenMP **~2.0 ms** | CUDA **~0.91 ms** | 연산·랜덤 메모리 접근이 커서 **GPU가 CPU(OpenMP)보다 빠르다** (로그로 확인) |

정리하면, **“작은 n은 CPU가 GPU보다 빠르다”고 단정한 실험은 n=4에 CUDA를 돌리지 않아 직접 대조하지 않았다.** 대신 **연산 규모 대비 GPU 고정비**와 **L7에서 n=256만 GPU가 L6보다 2.2× 빠른 수치**를 근거로, **작은 n은 CPU, 큰 n만 GPU**라는 하이브리드를 선택하였다.

### 개발 및 측정

로컬 PC에서 `score_all.cpp`를 수정한 뒤 Jetson Docker에서 `catkin_make -DPA01_OPT_LEVEL=N`으로 빌드하고, 동일 bag·동일 launch로 재생한다. 로그는 `grep -oE '\[score_all\].*'`로 수집하였다. 반복 실험을 위해 SSH 키와 ProxyJump(`jetson-nano-19`)로 관문·Jetson에 접속하였다.

---

# 제2장. Jetson Nano 환경 및 베이스라인 정량 평가

## 2.1 하드웨어·Docker 환경

- **SoC:** Jetson Nano — 4× Cortex-A57, Maxwell 128-core GPU, 4GB LPDDR4 (**CPU·GPU가 메모리 대역폭을 공유**)
- **실행:** Docker `student_19` (ROS Melodic, CUDA 10.2)
- **측정:** `std::chrono::steady_clock`로 `score_all` 진입부터 반환까지. Level 7은 H2D/D2H·`cudaStreamSynchronize`까지 **elapsed에 포함**

## 2.2 컴파일 분기 프레임워크 (`PA01_OPT_LEVEL`)

`score_all.cpp` 한 파일에 `#if PA01_OPT_LEVEL == N`으로 level 0~7을 분기하고, CMake `target_compile_definitions`로 Jetson 빌드 시 레벨만 바꾼다. 동일 bag·동일 launch에서 **코드 변경 효과만** 비교할 수 있다.

| Level | 태그 | 코드상 핵심 변경 |
|-------|------|------------------|
| 0 | baseline | §1.2 원본 루프 |
| 1 | opt1_licm | B3: `inv_denom` LICM |
| 2 | opt2_loop_interchange | B2: j-i 루프 교환 + `sums[]` |
| 3~5 | prefetch, 분기마스크, 통합 | ablation |
| 6 | opt6_best | dispatch + OpenMP + **ScoreN4** |
| 7 | opt7_gpu_hybrid | level 6 + **n≥64 CUDA** |

## 2.3 베이스라인 측정 및 연산-빈도 불균형

**summary (level 0):** 27,754 calls, cumulative **86,824 ms**, avg 3.128 ms/call.

**clean.log를 n별로 집계하면 다음과 같다.**

| n | 호출 비율 | elapsed 합 비율 | 평균 elapsed | work_units (n×p) |
|---|-----------|-----------------|--------------|------------------|
| 4 | **70.8%** | **4.1%** | 0.182 ms | 4,324 |
| 256 | **27.0%** | **95.8%** | **11.091 ms** | 276,736 |

SLAM 매처는 **싼 호출(n=4)을 매우 자주**, **비싼 호출(n=256)을 가끔** 수행한다. 따라서 **호출당 평균(avg)만 보면** n=4가 많아 전체가 빨라 보인 것처럼 착각할 수 있다. 본 과제의 최적화 KPI는 **n=256의 평균·합**과 **bag 전체 cumulative(ms)**이다. n=4와 256 모두 **elapsed/(n×p) ≈ 0.04 µs/단위**로 스케일이 맞아, 병목이 “루프 횟수”와 “메모리 접근 패턴”의 결합임을 뒷받침한다.

---

# 제3장. CPU 레벨 고속화 — 코드 변경과 로그 검증

각 level은 §1.2의 병목 B1~B4 중 무엇을 겨냈는지 명시하고, ablation 단계에서는 **n=256 구간** 로그로 채택·기각을 판정한다. Level 6에서 **n별로 서로 다른 CPU 커널**을 쓰는 이유는, §2.3의 **호출 빈도와 시간 비용이 n마다 다르기** 때문이다.

## 3.1 [Level 1] LICM — B3 나눗셈 제거

\(p\)는 한 번의 `score_all` 호출 안에서 변하지 않으므로, 분모를 루프 밖에서 한 번만 계산한다.

```cpp
const float inv_denom = 1.0f / (255.0f * p);
(*score)[i] = (float)sum * inv_denom;
```

| 지표 | L0 | L1 |
|------|-----|-----|
| n=256 평균 elapsed | 11.091 ms | 11.069 ms |
| cumulative | 86,824 ms | 86,974 ms |

**판정:** B3만으로는 B1(랜덤 grid)에 묻혀 **거의 차이가 없다.** 다만 이후 Level 6·7·CUDA에서도 동일한 `inv_denom`을 쓰므로 **공통 전제**로 유지한다.

## 3.2 [Level 2~3] 루프 교환 — B2 스캔 재사용

베이스라인은 후보 \(i\)를 바깥에 두어, 후보가 바뀔 때마다 `px`, `py` 전체를 다시 읽는다. 루프 교환은 **스캔 점 \(j\)를 바깥**에 두고, 한 점의 `px[j]`, `py[j]`를 읽은 뒤 모든 후보에 대해 grid를 누적한다.

```cpp
for (int j = 0; j < p; ++j) {
  const int px_j = px[j], py_j = py[j];
  for (int i = 0; i < n; ++i) {
    if (in_bounds) sums[i] += grid[y*w+x];
  }
}
```

| 지표 | L0 | L2 | L3 |
|------|-----|-----|-----|
| n=256 평균 | 11.09 ms | **7.04 ms** | 7.09 ms |
| µs / work_unit @ n=256 | 40.33 | **25.60** | 25.48 |

**판정:** **B2 해소가 CPU ablation에서 유일하게 큰 이득**이다(baseline 대비 n=256 **1.58×**). Level 3의 `__builtin_prefetch`는 LiDAR 좌표가 행 연속이 아니어 Jetson A57에서 **무의미하거나 악화**되었고, 이후 단계에서 제외하였다.

## 3.3 [Level 4~5] 분기 마스크 — B4, CPU에서는 기각

in-bounds를 비트 마스크 `(x>=0)&(x<w)&…`로 묶었다. n=256 elapsed **합**이 opt2보다 커졌다(81,794 ms). CPU에서는 분기 예측이 이미 잘 동작하는 경우가 많아 **명령 수만 늘었다.** Level 7 CUDA 커널은 워프 관점에서 `unsigned` 비교 분기를 사용한다.

## 3.4 [Level 6] dispatch + OpenMP + n=4 전용 커널 — CPU 최종

Level 2의 루프 교환은 **모든 n에 공통으로 도움이 되지만**, SLAM bag에서는 **n=4 호출이 70%**를 차지한다. 후보가 항상 4개인 경우에는 **i 루프를 아예 없애고** 전용 커널 `ScoreN4`로 분기하는 편이 낫다. 반면 **n=256**에서는 후보 축으로 **OpenMP**를 쓰는 편이 B1(랜덤 grid 읽기)을 코어에 나누는 데 유리하다.

### `opt6::Dispatch` 구조

| n | 함수 | 겨냥 병목 | 방식 |
|---|------|-----------|------|
| **4** | `ScoreN4` | B2, B4 | **j 바깥 루프** + 후보 4개 **수동 전개** + `cx[i]`, `cy[i]`를 j 루프 **밖에서 상수화** + `inv_denom`(LICM) + `unsigned` 경계 검사. OpenMP·CUDA 없음 |
| **≥64** | `ScoreOmpCandidates` | B1, B2 | `#pragma omp parallel for`로 **후보 i** 병렬 — 후보당 j 루프 |
| 그 외 | `ScoreInterchange` | B2 | Level 2와 동일한 j-i 교환 + 스택 `sums[256]` |

`ScoreN4`는 §3.2의 `ScoreInterchange`와 **원리는 같다**(스캔을 한 번씩만 읽음). 차이는 후보 수가 4로 고정되어 **루프 전개·상수화**로 분기·인덱싱 비용을 줄인 **도메인 특화** 경로라는 점이다. L2 대비 n=4 평균은 약 **0.12 ms → 0.098 ms(~20%)** 이다.

```cpp
#pragma omp parallel for schedule(static) if (n >= 64)
for (int i = 0; i < n; ++i) {
  // 후보 i마다 j 루프 — B1 읽기를 코어에 분산
}
```

### 로그 (Level 6)

| n | path | 평균 elapsed | elapsed 합 비중 |
|---|------|--------------|-----------------|
| 256 | omp_cand | **1.996 ms** | 88.5% |
| 4 | n4 | **0.098 ms** | 10.9% |

- summary: **85,260 calls**, cumulative **52,132 ms** (L0 대비 **1.67×**)
- n=256: 11.09 ms → **2.00 ms** (**5.6×**)

**정리:** Level 6까지는 **모든 n을 CPU로 처리**한다. 큰 n에서 OpenMP로 얻은 **~2 ms**가 Level 7 GPU 비교의 **CPU 기준선**이 된다. “CPU가 n=256에서 GPU보다 빠르다”는 뜻이 아니라, **GPU 도입 전 CPU가 도달한 한계**이다.

---

# 제4장. GPU 레벨 CUDA 및 하이브리드 통합 (Level 7)

## 4.1 커널 설계와 Host 전송

CUDA는 OpenMP와 같이 **후보 \(i\) 한 개를 스레드 하나**에 맡긴다. 스레드 내부에서 \(j=0..p-1\)로 grid를 누적하며, 수학은 CPU와 동일하고 `inv_denom`은 상수로 전달한다.

```cpp
__global__ void ScoreCandidatesKernel(...) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  for (int j = 0; j < p; ++j) {
    // grid[y*w+x], unsigned bounds
  }
  score_out[i] = sum * inv_denom;
}
```

- **병렬 단위:** 후보 \(i\) — B1의 랜덤 읽기를 다수 CUDA 스레드로 분산
- **Host:** `DeviceBuffers`로 **동일 grid 포인터**면 H2D 생략; `px`/`py`/`cx`/`cy`/`score`는 `cudaMemcpyAsync` 후 sync (로그 `elapsed`에 포함)
- **n=4:** CUDA를 **호출하지 않고** Level 6과 동일한 `ScoreN4` 유지

**구현 현황:** 강의의 **shared memory tiling은 아직 적용하지 않았다.** 커널은 global memory 기반이며, 147KB 지도를 device에 캐시해 **반복 H2D를 줄인 것**이 주된 전송 최적화이다.

### Level 7 `Dispatch`

`n == 4` → `ScoreN4` (CPU). `n >= 64` → `score_all_cuda::ScoreCandidates`. 그 외·CUDA 실패 시 → `ScoreInterchange` (CPU). 즉 Level 7은 **Level 6의 작은 n·중간 n CPU 경로를 그대로 두고**, **큰 n만 GPU로 바꾼다.**

## 4.2 로그로 본 하이브리드 — 같은 n에서 CPU vs GPU

| n | Level 6 (CPU) | Level 7 | 해석 |
|---|---------------|---------|------|
| **4** | n4 **0.098 ms** | n4 **0.088 ms** (CPU) | **GPU 미사용.** 호출 92,785회. 연산이 작아 GPU 고정비를 피한 설계가 로그와 일치 |
| **256** | omp **1.996 ms** | cuda **0.911 ms** | **GPU가 CPU(OpenMP)보다 2.2× 빠름.** elapsed 합의 대부분이 이 구간 |

- cumulative **42,864 ms** (L6 대비 **1.22×**, L0 대비 **2.03×**)
- n=256 cuda: 중앙값 **0.83 ms**; **최초 1회 ~96 ms**는 grid H2D(워밍업)

Level 7 summary의 avg **0.32 ms/call**은 n=4 호출이 **약 70%**이기 때문에 낮아 보인다. 실제 병목 구간은 **n=256의 cuda 경로**이므로, 보고서에는 **n=256 평균·path 태그·cumulative**를 함께 쓴다.

## 4.3 하이브리드 정당성 (한 줄 요약)

전체 파이프라인을 GPU-only로 두면, **호출은 많지만 연산이 작은 n=4**에서 런치·전송 손해가 누적될 수 있다. 반대로 **n=256**에서는 GPU가 CPU 최종보다 빠르다. Level 7은 **“큰 n만 GPU, 작은 n은 CPU에서 이미 최적화한 커널”**이라는 정책이며, 로그의 `path=cuda` / `path=n4`가 이를 보여 준다.

---

# 제5장. 종합 성능 비교 및 시스템 레벨 피드백

## 5.1 Level 0 · 6 · 7 정량 비교

### Bag-level cumulative

| 버전 | Level | calls | cumulative (ms) | vs L0 |
|------|-------|-------|-----------------|-------|
| 원본 | 0 | 27,754 | 86,824 | 1.00× |
| CPU 최종 | 6 | 85,260 | 52,132 | **1.67×** |
| GPU 하이브리드 | 7 | 133,496 | 42,864 | **2.03×** |

### n=256 구간만 (병목 정면 비교)

| 버전 | n=256 평균 elapsed | vs L0 @ n=256 |
|------|-------------------|---------------|
| L0 | 11.091 ms | 1.00× |
| L2 (교환만) | 7.039 ms | 1.58× |
| L6 (OpenMP) | 1.996 ms | 5.56× |
| L7 (CUDA) | 0.911 ms | **12.2×** |

### CPU ablation (n=256 elapsed 합, ms)

| Level | 기법 | n=256 합 | 판정 |
|-------|------|----------|------|
| 0 | baseline | 82,952 | — |
| 1 | LICM | 83,193 | 기각 |
| 2 | **루프 교환** | **78,735** | **채택** |
| 3~5 | prefetch 등 | ≥78,165 | 기각 |

## 5.2 피드백 루프: 가속 → `score_all` 호출 수 증가

`score_all`만 빠르게 하면 상위 `Branch`/`Score`가 같은 bag에서 **더 깊이** 진행되어 호출 수가 늘 수 있다.

| Level | calls | cumulative | cum/call (avg) |
|-------|-------|------------|----------------|
| 0 | 27,754 | 86,824 ms | 3.13 ms |
| 7 | 133,496 | 42,864 ms | 0.32 ms |

calls는 **약 4.8×** 늘었지만 cumulative는 **약 2.0×** 줄었다. 따라서 **avg만으로 “몇 배 빨라졌다”고 쓰면 안 되고**, cumulative와 n=256 구간을 병기해야 한다.

## 5.3 µs/work_unit — 기법이 연산량에 맞게 동작하는지

| Level | n=256 µs/(n×p) | 해석 |
|-------|----------------|------|
| L0 | 40.33 | 기준 |
| L2 | 25.60 | 루프 교환 — n에 무관한 공통 이득 |
| L6 | 7.25 | 후보 축 OpenMP |
| L7 cuda | 3.30 | GPU가 grid 읽기 병렬화 |

## 5.4 한계 및 향후 연구

- run마다 **calls 수가 달라** 동일 호출 수 가정 비교는 불가 → 3회 median·micro-benchmark 권장
- Level 7 **grid 첫 H2D ~96 ms** — 워밍업을 분리해 보고하는 것이 좋음
- Jetson bag에서 CPU vs GPU **점수 벡터 동일성**은 미검증 (PC bench에서는 max_diff=0)
- CUDA **shared memory tiling** 미구현 — PA02에서 검토

---

# 제6장. 결론

1. 베이스라인 병목은 **(B1) 랜덤 `grid` 읽기**와 **(B2) 후보 바깥·스캔 안쪽 루프**이며, **n=256**이 bag elapsed의 **약 96%**를 차지한다.  
2. **CPU(Level 6):** 루프 교환(L2)으로 공통 이득을 낸 뒤, **n=4는 `ScoreN4`(j 외부·전개·LICM)**, **n≥64는 OpenMP**로 n=256을 **11.1→2.0 ms**, cumulative **1.67×**까지 개선하였다.  
3. **GPU(Level 7):** **n≥64만 CUDA**로 바꾸었고, n=256에서 **0.91 ms로 CPU(2.0 ms)보다 2.2× 빠르다.** **n=4는 GPU를 쓰지 않고** CPU `ScoreN4`를 유지하였다 — 연산 규모 대비 GPU 고정비가 크다는 **임베디드 하이브리드** 설계이다.  
4. 함수 가속은 상위 매처의 **호출 빈도 증가**라는 2차 효과가 있으므로, **cumulative·n=256·`path` 태그**로 평가해야 한다.  
5. 향후 CUDA **타일링**, `Score()` 단위 **grid 상주**, Jetson **점수 동일성** 검증이 필요하다.
