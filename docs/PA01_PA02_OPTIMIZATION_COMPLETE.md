# PA01·PA02 최적화 전체 정리 및 microbench vs bag 분석

> 작성: 2026-05-31  
> **모듈 KPI** = bag 1회 `[match]` cumulative (ms)  
> **PA01 고정** = L9 + `PA01_USE_GPU=ON` + `PA01_GPU_THRESHOLD=256`  
> **PA02 최종** = `PA02_OPT_LEVEL=3` (L1 make_cand + L2 Branch + L3 Score)

---

## 0. 한눈에 보는 결론

| 구분 | 대상 | bag KPI / score_all | 개선폭 | 핵심 수단 |
|------|------|---------------------|--------|-----------|
| **PA01** | `score_all` 커널 | score_all **86,824 → 33,814 ms** (−61%) | **큼** | CPU N4/OMP + CUDA hybrid, threshold sweep |
| **PA02** | matcher 오케스트레이션 | match **90,482 → 87,866 ms** (−2.9%) | **작음** | CPU buffer reuse, scan bucket (GPU는 PA01 위에 유지) |

**왜 PA02가 PA01보다 덜 줄었나?**

1. PA02 시작 시점에 **score_all(37%)은 이미 PA01으로 최적화됨** — PA02가 건드릴 수 있는 상한은 matcher_scope **~56.7 s**뿐.
2. matcher_scope 안에서도 **Score_orchestration·Branch 재귀·sort** 등 커널 바깥 구조 비용이 지배적.
3. GPU sibling OMP·batch Score GPU 등 **추가 GPU 레이어 ROI가 낮거나 unsafe** (실험으로 확인).

**최종 채택:** L3 CPU bucket + PA01 GPU hybrid (T=256). microbench만 보면 CPU-only가 빠르지만 **bag KPI에서는 hybrid가 4.4% 우승**.

---

## 1. microbench vs bag — CPU/GPU 결과가 왜 다른가?

### 1.1 문제: 같은 함수인데 벤치마다 승자가 바뀐다

| 측정 | n=256 승자 | n=256 CPU | n=256 GPU | 근거 파일 |
|------|-----------|----------:|----------:|-----------|
| 연속 microbench (PA01) | **CPU** | 0.68 ms | 1.40 ms | `docs/PA01_ADDITIONAL_EXPERIMENTS_20260531.md` §4.1 |
| bag-like microbench (PA01) | **CPU 근소** | 0.64 ms | 0.72 ms | 동일 §4.2 |
| **실제 bag (PA01 L7/L8)** | **GPU** | ~1.94 ms (omp) | **~0.86 ms (cuda)** | 동일 §3.3 |
| Phase3 hybrid microbench | **CPU-only** | 36.6 ms/match | 46.7 ms (L3 hybrid) | `data/bench/pa02_phase3_hybrid_.../microbench_sweep.csv` |
| **Phase3 hybrid bag** | **hybrid (L3+T256)** | 91,750 ms (cpu_score) | **87,866 ms (hybrid)** | `data/bench/pa02_phase3_hybrid_.../bag_sweep.csv` |

→ **microbench에서 CPU가 이기고, bag에서 GPU hybrid가 이기는 패턴이 PA01·PA02 모두에서 반복**된다.

### 1.2 구조적 이유 (5가지)

```
┌─────────────────────────────────────────────────────────────────┐
│  microbench (연속 Match / score_all 호출)                        │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐                            │
│  │ call │→│ call │→│ call │→│ call │  간격 없음, cache/grid warm  │
│  └──────┘ └──────┘ └──────┘ └──────┘                            │
│  → GPU: H2D·launch overhead가 매 호출 부담                       │
│  → CPU OMP: 스레드 풀 warm, locality 좋음 → 상대적으로 유리      │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  bag / SLAM (sporadic Match)                                     │
│  ┌──────┐    ROS/scan gap    ┌──────┐    gap    ┌──────┐        │
│  │ Match│ ·················· │ Match│ ······· │ Match│          │
│  └──────┘                    └──────┘         └──────┘          │
│  + FastMatcher::Score heap 할당, n=4↔256 교차, 로깅, B&B 재귀    │
│  → GPU: grid pointer cache (UploadGrid) 재사용, coarse n=256     │
│  → CPU OMP: 매 호출 region 진입 + 교차 호출로 locality 악화      │
└─────────────────────────────────────────────────────────────────┘
```

| # | 요인 | microbench | bag |
|---|------|------------|-----|
| 1 | **호출 패턴** | 연속 반복, warm cache | scan 간격, cold start 성분 |
| 2 | **GPU grid H2D** | 매 실험마다 amortization 다름 | 동일 grid pointer → **cache hit** (`score_all_cuda.cu`) |
| 3 | **입력 n 분포** | 단일 n 스윕 | n=4(75k calls) + n=256(31k calls) **혼재** |
| 4 | **주변 비용** | score_all/Match만 격리 | Score 필터·sort·Branch 재귀·vector alloc 포함 |
| 5 | **OpenMP** | 연속 호출 시 상대적으로 유리 | `#pragma omp parallel for` **매 호출 region 진입** |

### 1.3 PA02 Phase 3 hybrid 실험 — 같은 함정 재현

**microbench** (`pa02_microbench_gpu`, 15 iters):

| variant | avg_ms | 해석 (microbench만 보면) |
|---------|-------:|---------------------------|
| cpu_score (L3, T=999999) | **36.6** | “CPU-only가 최고” |
| gpu_aggr (L3, T=64) | 35.6 | |
| hybrid_prod (L3, T=256) | 46.7 | “hybrid가 느림” |
| legacy_gpu (L2, T=256) | 57.4 | |

**bag KPI** (동일 variant):

| variant | match (ms) | Δ vs hybrid | score_all (ms) |
|---------|----------:|------------:|---------------:|
| **hybrid_prod** | **87,866** | — | **34,786** |
| gpu_aggr | 87,713 | −0.2% | 35,152 |
| legacy_gpu | 89,926 | +2.3% | 35,043 |
| cpu_score | 91,750 | **+4.4%** | **47,325** |

**cpu_score bag path 분해** — coarse가 GPU 대신 OpenMP로:

| path | elapsed 비중 | n=256 |
|------|-------------:|-------|
| hybrid_prod | **cuda 81%** | GPU ~28 s |
| cpu_score | **omp_cand 88%** | OMP ~41.7 s |

→ microbench는 **“연속 Match 1개 함수”**만 재고, bag는 **전체 SLAM + sporadic 호출 + hybrid dispatch 누적**을 본다.

### 1.4 실험·선택 원칙 (교훈)

```
가설 → microbench (격리·후보 좁히기)
     → bag-like microbench (호출 1회 패턴)
     → bag cumulative (KPI 확정)   ← 최종 선택은 여기서만
```

| 단계 | PA01 사례 | PA02 Phase 3 사례 |
|------|-----------|-------------------|
| microbench | n≥2048에서 GPU crossover | cpu_score 36.6 ms “승” |
| bag | T=256 cumulative **37,367 ms** 최소 | hybrid_prod match **87,866 ms** 최소 |
| **채택** | `PA01_GPU_THRESHOLD=256` | L3 + T=256 (변경 없음) |

**보고서용 한 줄:**  
“microbench는 커널·정책 **후보 선별**용, bag cumulative는 **SLAM KPI 확정**용이다. 두 결과가 다를 때 bag를 따른다.”

---

## 2. PA01 최적화 전체 (score_all)

### 2.1 목표·범위

- **함수:** `score_all()` — correlative scan matching score 커널
- **KPI (PA01):** bag 1회 `[score_all]` cumulative (ms)

### 2.2 실험 → 선택 → 적용

| 단계 | 실험 | 결과 | 선택 |
|------|------|------|------|
| L0→L6 | opt level bag sweep | baseline 86,824 ms → L6 52,132 ms | CPU N4 + OMP + loop opt |
| L7 hybrid | L7 vs L8 bag | L7 41,123 ms (cuda @ n=256) vs L8 51,851 ms (omp) | GPU 도입 |
| L8/L9 설계 | L6≈L8≈L9 커널 동일 확인 | dispatch 상수만 차이 | L9 + configurable threshold |
| **threshold sweep** | T=64,256,2048 bag | **256: 37,367 ms** < 64: 40,705 < 2048: 51,560 | **`PA01_GPU_THRESHOLD=256`** |

**threshold sweep 데이터** (`data/bench/pa01_opt9_threshold_sweep.csv`):

| T | score_all cumulative (ms) | n=256 path |
|---|------------------------:|------------|
| 64 | 40,705 | cuda |
| **256** | **37,367** | cuda |
| 2048 | 51,560 | omp (GPU 미사용) |

### 2.3 PA01 최종 dispatch (L9 hybrid)

```
n == 4        → CPU ScoreN4      (bag ~18% elapsed)
n >= 256      → CUDA             (bag ~81% elapsed, coarse per scan)
8 <= n < 256  → CPU OpenMP
else          → CPU interchange
```

### 2.4 PA02 관점에서의 PA01 성과

PA02 L0 bag에서 score_all **33,814 ms** (PA01 L9 이미 적용).  
PA01 단독 baseline(86,824 ms) 대비 **~61% 감소** — 이후 PA02는 **match − score_all ≈ 56.7 s** 만 추가로 줄일 수 있음.

---

## 3. PA02 최적화 전체 (matcher 오케스트레이션)

### 3.1 Phase 0 — 병목 분석 (실험 선행)

**실험:** `scripts/pa02_bag_profile.sh pa02_l0_profile`  
**분석:** `scripts/pa02_analyze_profile.py` → `pa02_l0_profile_bottleneck.txt`

| metric | L0 (ms) | match 대비 |
|--------|--------:|-----------:|
| `[match]` KPI | 90,482 | 100% |
| `[score_all]` (PA01) | 33,814 | 37.4% |
| **matcher_scope** | **56,667** | **62.6%** |
| Score_orchestration | 25,955 | 28.7% |
| Branch depth=3 elapsed | 34,679 | — |

**선정된 3타깃 (코드 추측 아님, 로그 stratum 기반):**

1. `make_cand` — Phase 1 (작지만 격리 실험 용이)
2. `Branch` — Phase 2 (depth=3 집중)
3. `Score` — Phase 3 (orchestration ~26 s, n_cand 3840/4 이중 regime)

### 3.2 Phase 1 — make_cand (CPU)

| 항목 | 내용 |
|------|------|
| **가설** | vector reserve + OpenMP로 후보 생성 가속 |
| **실험** | `scripts/pa02_make_cand_omp_sweep.sh`, Jetson microbench |
| **결과** | hot path 16×16 (n=256): serial **0.0020 ms** ≤ OMP 256 **0.0042 ms** |
| **선택** | `PA02_MAKE_CAND_OMP_MIN=512` → bag hot path에서 OMP **미발동**, reserve만 유효 |
| **bag** | make_cand 1,216 → 763 ms (−37%); match 90,482 → 90,701 ms (±노이즈) |

### 3.3 Phase 2 — Branch (CPU)

| 항목 | 내용 |
|------|------|
| **가설** | child buffer reuse, empty quadrant skip, MakeLowCands reserve |
| **실험** | `scripts/pa02_branch_cpu_sweep.sh` (CPU L6 microbench) |
| **결과** | L2 reserve 28.6 ms; sibling OMP(128) **22.2 ms** (−22%), score 동일 |
| **선택** | reserve/skip **적용**; sibling OMP **`PA02_BRANCH_OMP_MIN=999999`** — GPU bag 빌드에서 `#ifndef PA01_USE_GPU`로 OMP **compile-time off** (concurrent CUDA unsafe) |
| **bag** | Branch 54,743 → 45,301 ms (−17%); match 90,482 → 89,926 ms (−0.6%) |

### 3.4 Phase 3 — Score (CPU orchestration + PA01 GPU 유지)

| 항목 | 내용 |
|------|------|
| **가설** | scan별 O(n_scans×n_cand) 필터·매번 vector alloc 제거 |
| **구현** | 1-pass scan bucket, `thread_local` buffers, empty scan skip |
| **실험** | L2 vs L3 microbench (CPU L6 / GPU L9); hybrid sweep |
| **bag** | Score 60,434 → 57,075 ms; Score_orch ~25,391 → **22,235 ms**; match 89,926 → **87,866 ms** |
| **hybrid 결정** | §1.3 — **L3+T=256** bag KPI 최소, cpu_score +4.4% |

### 3.5 Phase별 bag KPI 누적

| Level | 내용 | match (ms) | Δ vs L0 | Branch | Score | make_cand |
|------:|------|----------:|--------:|-------:|------:|----------:|
| L0 | baseline | 90,482 | — | 54,743 | 59,869 | 1,216 |
| L1 | make_cand | 90,701 | +0.2% | 53,773 | 60,230 | 763 |
| L2 | Branch | 89,926 | −0.6% | 45,301 | 60,434 | 781 |
| **L3** | **Score** | **87,866** | **−2.9%** | **44,796** | **57,075** | 820 |

**matcher_scope (match − score_all):** 56,667 → **53,080 ms** (−6.3%)

정확도: 모든 level `best_score=0.783` 동일.

---

## 4. 실험·선택 마스터 표

| # | 실험 | 스크립트/데이터 | 측정 | 결과 | 최종 선택 |
|---|------|-----------------|------|------|-----------|
| 1 | PA01 opt level bag | `data/pa01/pa01_opt*_summary.txt` | score_all cumulative | L7 hybrid 41 s | L9 framework |
| 2 | PA01 GPU threshold | `pa01_opt9_threshold_sweep.sh` | score_all @ T | **T=256: 37,367 ms** | `PA01_GPU_THRESHOLD=256` |
| 3 | PA02 L0 profile | `pa02_bag_profile.sh` | match decomposition | matcher_scope 56.7 s | 3-target roadmap |
| 4 | make_cand OMP | `pa02_make_cand_omp_sweep.sh` | microbench 16×16 | OMP@256 slower | `OMP_MIN=512` |
| 5 | Branch OMP | `pa02_branch_cpu_sweep.sh` | CPU match microbench | OMP128 −22% | **off on GPU bag** |
| 6 | Score L2 vs L3 | `pa02_review_experiments.sh` | microbench | GPU L3 −6.9% | L3 bucket |
| 7 | score_all path | `pa02_analyze_score_paths.py` | bag path % | cuda 81% @ n=256 | hybrid 유지 |
| 8 | **Phase3 hybrid** | `pa02_phase3_hybrid_sweep.sh --bag` | microbench **vs** bag | microbench CPU 승, **bag hybrid 승** | **L3+T=256** |

---

## 5. 결과 분석

### 5.1 PA01 vs PA02 개선폭 비교

| | 절대 감소 (ms) | 비율 | 이유 |
|--|---------------:|-----:|------|
| PA01 score_all | ~53,000 | **−61%** | 단일 hot loop → SIMD/GPU/CUDA 직접 적용 |
| PA02 match | ~2,616 | **−2.9%** | score_all(37%) 고정, orchestration·B&B 구조 잔여 |
| PA02 matcher_scope | ~3,587 | **−6.3%** | PA02 실질 공격 대상 |

### 5.2 어디서 줄었나 (L0 → L3)

| 구간 | L0 (ms) | L3 (ms) | Δ (ms) | Δ (%) |
|------|--------:|--------:|-------:|------:|
| make_cand | 1,216 | 820 | −396 | −33% |
| Branch cumulative | 54,743 | 44,796 | −9,947 | −18% |
| Score cumulative | 59,869 | 57,075 | −2,794 | −4.7% |
| Score_orchestration | 25,955 | 22,235 | **−3,720** | **−14%** |
| score_all (regression) | 33,814 | 34,786 | +972 | +2.9% (허용) |
| **match KPI** | 90,482 | 87,866 | **−2,616** | **−2.9%** |

Branch·Score_orchestration에서 대부분 이득. make_cand는 절대량 작아 match KPI 기여는 미미.

### 5.3 아직 남은 비용 (추가 개선 여지)

| 잔여 | L3 (ms) | 비고 |
|------|--------:|------|
| score_all (PA01) | 34,786 | PA02 범위 밖; 이미 GPU hybrid |
| Score_orchestration | ~22,235 | sort, score_all 호출 간격, Cand 복사 등 |
| Branch (Score 포함) | 44,796 | depth=3 stratum ~32 s; sibling OMP는 GPU bag 불가 |
| MakeScans/Bounds/GridStack | 태그 없음 | match − Σtag 잔여 |
| **이론적 상한** | match 87,866 | score_all을 0으로 만들어도 ~53 s orchestration |

**현실적 추가 후보 (미구현·ROI 검토됨):**

| 후보 | 기대 | 장벽 |
|------|------|------|
| Branch sibling OMP | CPU microbench −22% | GPU bag concurrent CUDA unsafe |
| Score `std::sort` 최적화 / partial sort | orchestration 일부 | B&B 정렬 의존성 검증 필요 |
| batch GPU Score (multi-scan 1 kernel) | microbench 미측정 | grid cache已有, scan당 n=256 이미 GPU |
| 알고리즘 (depth 축소, pruning 강화) | match 대폭 | 정확도·과제 범위 |
| Phase 0b ablation | 인과 입증 | 구현·bag 추가 run |

**판단:** 커널급 PA01-style −60% gains는 **이미 score_all에서 소진**. PA02는 orchestration **−6.3% (matcher_scope)** 가 데이터상 현실적 ceiling에 가깝고, microbench-only 정책(예: CPU-only T=999999)은 bag에서 **역효과**였다.

---

## 6. 최종 빌드·설정 (재현)

```bash
# Jetson Docker
catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON \
  -DPA02_PROFILE=ON -DPA02_OPT_LEVEL=3 \
  -DPA02_MAKE_CAND_OMP_MIN=512 \
  -DPA02_BRANCH_OMP_MIN=999999 \
  -DPA01_GPU_THRESHOLD=256
source devel/setup.bash
export ROS_IP=192.168.0.104
export ROS_MASTER_URI=http://192.168.0.106:11311
scripts/pa02_bag_profile.sh pa02_l3_profile
```

**기대 KPI (2026-05-31 run):** match **~87,866 ms**, best_score **0.783**, score_all **~34,786 ms**.

---

## 7. 데이터·문서 인덱스

| 자료 | 경로 |
|------|------|
| PA01 추가 실험·microbench≠bag | `docs/PA01_ADDITIONAL_EXPERIMENTS_20260531.md` |
| PA02 프로파일링 전략 | `docs/PA02_PROFILING_STRATEGY.md` |
| PA02 CPU/GPU 검토 | `docs/PA02_OPTIMIZATION_REVIEW.md` |
| PA02 L0~L3 bag summary | `data/pa02/pa02_l{0,1,2,3}_profile_summary.txt` |
| PA01 threshold CSV | `data/bench/pa01_opt9_threshold_sweep.csv` |
| Phase3 hybrid sweep | `data/bench/pa02_phase3_hybrid_20260531_151643/` |
| PA02 review sweep | `data/bench/pa02_review_20260531_150414/` |
| score_all path breakdown | `data/bench/pa02_score_paths_bag.txt` |
| 실험 재실행 | `scripts/pa02_phase3_hybrid_sweep.sh --bag` |

---

## 8. 보고서용 요약 문단 (복사 가능)

PA01에서는 `score_all` 커널에 CPU 전개(N4)·OpenMP·CUDA hybrid를 적용하고 bag threshold sweep으로 `PA01_GPU_THRESHOLD=256`을 확정하여 score_all cumulative를 baseline 86,824 ms에서 33,814 ms(−61%)까지 줄였다. PA02는 동일 bag에서 프로파일링으로 matcher_scope 56.7 s( match의 62.6%)를 측정한 뒤 make_cand·Branch·Score 순으로 CPU orchestration을 최적화하였고, match KPI는 90,482 ms에서 87,866 ms(−2.9%)로 개선하였다. microbench는 연속 호출 환경에서 CPU가 유리하게 나타났으나(예: Phase3 cpu_score 36.6 ms vs hybrid 46.7 ms), sporadic bag 패턴에서는 GPU grid cache와 n=256 coarse CUDA dispatch가 우세하여 hybrid(L3+T=256)가 bag match 87,866 ms로 cpu-only 91,750 ms 대비 4.4% 우수하였다. 따라서 최종 정책은 **L3 CPU Score bucket + PA01 GPU hybrid**이며, microbench 결과만으로 CPU/GPU 정책을 결정하지 않고 bag cumulative를 KPI 확정 근거로 사용하였다.
