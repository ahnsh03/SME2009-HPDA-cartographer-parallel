# PA01 추가 실험 기록 (2026-05-31)

PA02로 넘어가기 전, micro-bench 결론과 bag 실측 불일치·opt8/9 설계·GPU dispatch threshold를 **데이터로 확정**하기 위해 진행한 실험 정리.

---

## 1. 배경과 목적

### 1.1 당시 남아 있던 질문

| # | 질문 | 오늘 실험으로 답한 내용 |
|---|------|------------------------|
| 1 | opt8/9(우수 보고서 반영)가 opt7 CPU·GPU 커널을 **느리게** 만들었나? | **아니오.** L6≈L8≈L9 CPU 동급. 차이는 **dispatch(threshold)만** |
| 2 | micro-bench crossover **n≥2048**인데 bag에서 L7이 더 빠른 이유? | bag n=256 hot path + OpenMP per-call 비용 + GPU buffer 재사용. **측정 환경·정책 차이** |
| 3 | bag 환경에서 GPU를 **몇 n부터** 쓰는 게 맞나? | **threshold bag sweep**으로 실측 (§4) |
| 4 | OpenMP를 bag에서 벤치처럼 warm 유지할 수 있나? | `parallel for` 구조상 **불가**에 가깝고, bag CPU slowness는 OpenMP만의 문제 아님 (§3.3) |

### 1.2 측정 지표 (변경 없음)

- **KPI**: bag 1회 동안 `[score_all]` **cumulative** (ms) — 벽시계 96 s 아님
- **보조**: n=4 / n=256 구간 avg ms, `path=` (n4 / omp_cand / cuda)
- **입력**: map 467×314, p≈1081, bag `scan.bag`, `ns:=student_19`

---

## 2. 코드·실험 인프라 (오늘 추가)

| 항목 | 파일 | 내용 |
|------|------|------|
| GPU threshold 빌드 옵션 | `CMakeLists.txt` | `PA01_GPU_THRESHOLD` (0=레벨 기본: L7→64, L9→2048) |
| dispatch | `score_all.cpp` | `PA01_GPU_DISPATCH_THRESHOLD` 매크로 |
| bag threshold sweep | `scripts/pa01_opt9_threshold_sweep.sh` | threshold별 catkin_make + bag + CSV |
| bag 종료 | `launch/cartographer_parallel_with_bag.launch` | `rosbag play`에 `required="true"` (bag 후 launch 종료) |
| bag-like micro-bench | `benchmark/pa01_microbench.cpp` | `--baglike` (호출 1회씩 평균) |

---

## 3. 실험 A — opt6~9 bag 재실행 및 커널 regression 검증

### 3.1 고정 bag run (level별 1회)

| Level | tag | calls | cumulative (ms) | avg (ms/call) | n=256 path / avg |
|-------|-----|------:|------------------:|--------------:|------------------|
| L6 | opt6_best | 85,260 | 52,132 | 0.611 | omp ~1.99 ms |
| **L7** | opt7_gpu_hybrid | **134,942** | **41,123** | **0.305** | **cuda ~0.86 ms** |
| L8 | opt8_cpu_slam | 84,692 | 51,851 | 0.612 | omp ~1.94 ms |
| L9 (threshold=2048) | opt9_hybrid_bench | 84,802 | 51,579 | 0.608 | omp ~1.91 ms |

**데이터**: `data/pa01_opt{6,7,8,9}_*_summary.txt`, `*_clean.log`

### 3.2 커널 regression 결론

L6~L9는 **동일 `ScoreN4` / `ScoreOmpCandidates` / CUDA 커널** 공유. 레벨별 차이는 **compile-time dispatch 상수**뿐.

- **L8 CPU ≈ L6 CPU** (n=256 omp ~1.94 vs ~1.99 ms) → opt8 `kOmpMinCandidates=8`은 bag n 분포(2,4,256)에서 **hot path 무영향**
- **L9 (2048) ≈ L8** → n=256에서 GPU 미사용 (`path=cuda` 0회)
- **L7 cumulative 최소** → n=256 **GPU** (~0.86 ms) vs L8/L9 **OpenMP** (~1.9 ms)

> calls 수가 L7(≈135k) vs L8/L9(≈85k)로 다름 → SLAM 타이밍 비결정성. **cumulative·n=256 avg·path**를 함께 해석.

### 3.3 micro-bench vs bag CPU 차이 (OpenMP “warmup” 오해 정리)

| 환경 | n=256 CPU | n=256 GPU |
|------|-----------|-----------|
| 연속 micro-bench (`080435`) | **0.68 ms** | 1.40 ms → CPU 승 |
| bag-like micro-bench (`112002`) | 0.64 ms | 0.72 ms → CPU 근소 승 |
| **실제 bag (L8 omp / L7 cuda)** | **~1.94 ms** | **~0.86 ms** → **GPU 승** |

- micro-bench **연속 루프도** 호출마다 `#pragma omp parallel for` region 진입. warmup은 **스레드 풀 예열**일 뿐 region 재진입을 없애지 않음.
- bag CPU가 더 느린 추가 요인: n=4↔n=256 **교차 호출**, `FastMatcher::Score` heap 할당, 로깅·메모리 locality.
- **대안**: OpenMP 튜닝보다 bag hot path에서 **GPU dispatch(L7)** 이 KPI상 확실.

---

## 4. 실험 B — micro-bench (격리 vs bag-like)

### 4.1 연속 n 스윕 (기존, `080435`)

- **crossover**: n **≥ 2048**에서 GPU 유리
- n=256: CPU 0.68 ms, GPU 1.40 ms (winner: cpu)
- **데이터**: `data/bench/pa01_bench_20260531_080435_sweep.csv`

### 4.2 bag-like n 스윕 (신규, Jetson `112002`)

- **방법**: `--baglike`, warmup=10, iters=100, **호출 1회씩** 평균 (OpenMP 진입 비용 포함)
- **crossover**: n **≥ 512**
- n=256: CPU 0.64 ms, GPU 0.72 ms (여전히 CPU 근소) — **실제 bag(0.86 vs 1.94)과 불일치**
- **데이터**: `data/bench/pa01_baglike_20260531_112002_sweep.csv`

### 4.3 해석

| 벤치 모드 | crossover | bag KPI와의 관계 |
|-----------|-----------|------------------|
| 연속 micro-bench | 2048 | 격리 커널·교수님 “공정 비교” 근거 |
| bag-like micro-bench | 512 | bag에 가깝지만 ROS/실후보 미포함 |
| **opt9 threshold bag sweep** | **≤256 (실측)** | **SLAM cumulative 직접 측정 → 최종 선택 근거** |

---

## 5. 실험 C — opt9 GPU threshold bag sweep (핵심)

### 5.1 방법

```bash
cd ~/catkin_ws/src/scripts
./pa01_opt9_threshold_sweep.sh 64 256 2048
```

- 빌드: `catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON -DPA01_GPU_THRESHOLD={T}`
- bag 1회/threshold → `~/pa01_threshold_sweep/sweep.csv`
- launch `required="true"`로 bag 종료 후 자동 shutdown

### 5.2 결과 (2026-05-31, Jetson `inha-desktop` / student_19)

| GPU threshold | calls | cumulative (ms) | avg (ms/call) | n=256 avg (ms) | n=256 cuda | n=256 omp |
|--------------:|------:|----------------:|--------------:|---------------:|-----------:|----------:|
| **64** | 135,770 | 40,705 | 0.300 | 0.842 | 38,070 | 0 |
| **256** | 136,093 | **37,367** | **0.275** | **0.754** | 38,089 | 0 |
| **2048** | 86,181 | 51,560 | 0.598 | 1.894 | **0** | 23,642 |

**데이터**

| 파일 | 경로 |
|------|------|
| CSV | `data/bench/pa01_opt9_threshold_sweep.csv` |
| 전체 로그 | `data/bench/threshold_sweep/thresh_{64,256,2048}_*.log` |
| summary | `data/bench/threshold_sweep/thresh_*_summary.txt` |

### 5.3 분석

1. **threshold ≤ 256**: n=256 전부 `path=cuda`, cumulative **37~41 s** (L7 41,123 ms와 동급).
2. **threshold = 2048**: n=256 전부 `path=omp_cand`, cumulative **51,560 ms** (L8/L9 기존 ~51.5 s와 동일).
3. **2048 vs 256**: n=256만 **~2.5×** (1.89 vs 0.75 ms) → cumulative **~38%** 차.
4. **64 vs 256**: 둘 다 GPU @ n=256. 이번 run에서는 **256이 cumulative 최소** (calls·SLAM 비결정성 포함). 추가 run으로 64≈256 확인 가능.
5. **`REQUIRED process ... has finished cleanly`**: 오류 아님. bag 정상 종료 + launch shutdown.

### 5.4 L7과의 대조

| | L7 (n≥64 GPU) | opt9 @ threshold=256 | opt9 @ threshold=2048 |
|--|---------------|----------------------|------------------------|
| cumulative | 41,123 ms | **37,367 ms** | 51,560 ms |
| n=256 | cuda ~0.86 ms | cuda ~0.75 ms | omp ~1.89 ms |

→ **동일 CUDA 커널**, threshold만 맞으면 L7급 KPI. micro-bench 기본 2048은 **bag hot path에 부적합**이 실험으로 확정.

---

## 6. 최종 선택 (PA01 마감 기준)

### 6.1 레벨·역할

| Level | GPU threshold | bag SLAM KPI | 보고서 역할 |
|-------|---------------|--------------|-------------|
| **L6** | — | CPU baseline | CPU 최적화 정점 |
| **L7** | **64** (고정) | **최적급** (~41k ms) | **production / 제출 KPI** |
| **L8** | 없음 | CPU only (~52k ms) | CPU-only SLAM 비교 |
| **L9** | **2048** (micro-bench) | ~52k ms (GPU 미사용) | 격리 벤치 crossover **정당화** |
| **L9** | **256** (bag sweep) | **~37k ms** | bag sweep **최적점** (동일 코드, `-DPA01_GPU_THRESHOLD=256`) |

### 6.2 보고서에 쓸 이중 근거

1. **Micro-bench (연속)**: 동일 n·p 공정 비교 → crossover **2048** → opt9 기본값 설계 근거.
2. **Bag threshold sweep**: SLAM cumulative → **threshold ≤ 256** (실측 best **256**) → **실제 로봇 bag KPI** 근거.
3. **불일치 설명**: micro-bench는 OpenMP 연속 측정·격리 환경; bag는 n=256 hot path + sporadic 호출 + GPU cache → **같은 n에서 winner 반대**.

### 6.3 PA02 이전 체크리스트

- [x] opt7/8/9 bag 로그 확보 및 regression 여부 확인
- [x] micro-bench vs bag 불일치 원인 문서화
- [x] bag-like micro-bench (`112002`)
- [x] opt9 GPU threshold bag sweep (64 / 256 / 2048)
- [x] sweep 데이터 PC 반영 (`data/bench/threshold_sweep/`)
- [ ] (선택) 보고서 본문 `07_COMPARISON_FOR_REPORT.md`에 본 문서 표 인용
- [ ] (선택) 제출 KPI는 **L7** 또는 **L9+threshold=256** 중 하나로 명시

---

## 7. PC로 데이터 가져오기 (SSH cat 방식)

`inha-desktop` 호스트명은 PC에서 resolve 안 됨. **`jetson-nano-19`** 사용.

```bash
REPO=~/SME2009_HPDA/PA01/data/bench/threshold_sweep
mkdir -p "$REPO"
REMOTE=/root/pa01_threshold_sweep

for f in sweep.csv thresh_64_summary.txt thresh_256_summary.txt thresh_2048_summary.txt; do
  ssh jetson-nano-19 "docker exec student_19 cat ${REMOTE}/${f}" > "${REPO}/${f}"
done

# clean/run.log (~30MB) 필요 시
ssh jetson-nano-19 "docker exec student_19 cat ${REMOTE}/thresh_256_clean.log" \
  > "${REPO}/thresh_256_clean.log"
```

---

## 8. 관련 파일 인덱스

```
data/
├── pa01_opt6_best_summary.txt
├── pa01_opt7_gpu_hybrid_summary.txt / clean.log
├── pa01_opt8_cpu_slam_summary.txt / clean.log
├── pa01_opt9_hybrid_bench_summary.txt / clean.log
└── bench/
    ├── pa01_bench_20260531_080435_*     # 연속 micro-bench
    ├── pa01_baglike_20260531_112002_*   # bag-like micro-bench
    ├── pa01_opt9_threshold_sweep.csv
    └── threshold_sweep/                 # sweep 전체 로그

scripts/pa01_opt9_threshold_sweep.sh
benchmark/pa01_microbench.cpp            # --baglike
cartographer_parallel/.../CMakeLists.txt # PA01_GPU_THRESHOLD
```

---

## 9. 한 줄 요약

> **opt8/9는 커널을 느리게 하지 않았고**, bag에서 L7이 빠른 이유는 **n=256 GPU dispatch**이다. micro-bench crossover 2048은 **격리 벤치용**이며, **bag KPI 최적 GPU threshold는 ≤256(실측 256)** 이다. PA02 전 PA01은 **L7(production) + threshold sweep(정책 근거) + dual benchmark narrative**로 마감한다.
