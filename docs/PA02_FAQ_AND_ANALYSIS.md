# PA02 FAQ 및 기술 분석 (2026-05-31)

과제 진행 중 나온 질문(best_score, GPU hybrid, ROI, concurrent CUDA)과 코드·실측 근거를 정리한다.

> bag KPI = `[match]` cumulative (ms)  
> PA01 고정 = L9 + `PA01_USE_GPU=ON` + `PA01_GPU_THRESHOLD=256`  
> PA02 채택 = `PA02_OPT_LEVEL=3`

---

## 1. `best_score=0.783`은 무엇인가?

### 1.1 정의

`best_score`는 **한 번의 scan matching(`Match()`)** 에서 Branch-and-Bound가 찾은 **최고 pose의 correlative score**이다.

```cpp
const Cand best = Branch(grids, scans, bounds, coarse, max_depth, opt_.min_score);
out->ok = best.score > opt_.min_score;  // min_score 기본 0.05
// LogMatch(..., best.score, ...) → 로그 best_score=0.783
```

| 필드 | 의미 |
|------|------|
| `best_score` | LiDAR 스캔과 occupancy grid 정합 점수 (0~1, **높을수록 좋음**) |
| `ok=1` | `best_score > min_score` → match 성공 |
| `coarse_n` | coarse 단계 후보 수 (3840 = 정상 탐색 공간) |

score 수식은 PA01 `score_all`과 동일: 후보 pose마다 grid hit 합을 정규화.

### 1.2 로그에서 0.783의 위치

summary의 `best_score=0.783`은 **bag run 마지막 `[match]` 줄**에 찍힌 값이다.  
모든 match 호출의 최댓값이 아니다. 실제 bag 한 run 안에서는 호출마다 0.78~0.85 등 다양한 score가 나온다.

### 1.3 SLAM 성능 검증인가?

**부분적으로만 해당** — matcher 정확도 **회귀(regression) sanity check**이지, 전역 SLAM 평가는 아니다.

| 확인한 것 | 확인하지 않은 것 |
|-----------|------------------|
| 최적화 후 match 성공(`ok=1`) 유지 | 전체 bag 궤적 RMSE / loop closure |
| `best_score`·`coarse_n`이 baseline과 유사 | 최종 occupancy map 품질 |
| 탐색 공간 축소 여부 (`coarse_n`) | SLAM end-to-end 벽시계 |

과제 범위(`fast_correlative_scan_matcher`)에서는 **“같은 bag·같은 map에서 matcher 출력이 baseline과 동일한가”** 가 적절한 검증이다.

### 1.4 실험별 해석

| run | summary best_score | coarse_n | 판단 |
|-----|-------------------:|---------:|------|
| L0~L3 (채택) | 0.783 | 3840 | baseline과 동일 |
| L4/L5 (reserve/tweak) | 0.783 | 3840 | 정확도 유지, KPI 무변화 |
| L4 **ShrinkToFit** (기각) | **0.748** | **3600** | 탐색 공간 축소 → **정확도 악화** |

ShrinkToFit 기각 근거: 속도만 빨라져도 `best_score` 하락 + `coarse_n` 감소는 **matcher 품질 회귀**로 간주.

---

## 2. Phase 3에서 GPU를 “괜찮다”고 했는데 왜 PA02에 GPU 코드를 추가하지 않았나?

### 2.1 오해 정리: GPU는 **끈 것이 아님**

| 계층 | 역할 | GPU |
|------|------|-----|
| **PA01** | `score_all` 커널 (n≥256 → CUDA) | **ON**, threshold=256 |
| **PA02 L3** | Score bucket, Branch buffer reuse | CPU 오케스트레이션만 |

bag `score_all` path 분해: **n=256 coarse 호출의 ~81%가 `cuda` path**.  
PA02는 그 위의 필터·버킷·할당 구조만 CPU로 최적화했다.

### 2.2 Phase 3 hybrid sweep 결과

데이터: `data/bench/pa02_phase3_hybrid_20260531_151643/bag_sweep.csv`

| variant | PA02 | GPU threshold | match (ms) | Δ vs hybrid |
|---------|------|---------------|----------:|------------:|
| **hybrid_prod** | **L3** | **256** | **87,866** | — |
| gpu_aggr | L3 | 64 | 87,713 | −0.2% |
| legacy_gpu | L2 | 256 | 89,926 | +2.3% |
| cpu_score | L3 | 999999 (GPU OFF) | 91,750 | **+4.4%** |

**채택: L3 CPU bucket + PA01 GPU hybrid (T=256)** — GPU를 끄지 않고, PA01 레이어를 유지.

### 2.3 “적용하지 않은” 것들

| 미적용 항목 | 이유 |
|-------------|------|
| CPU-only (T=999999) | microbench 36.6 ms “승”이나 bag +4.4% 느림 |
| GPU threshold 64 | match ±0.2%, score_all 소폭 증가 → T=256 유지 |
| PA02 전용 batch GPU Score | §3 참고 (ROI 낮음) |
| Branch sibling OMP (GPU build) | §4 참고 (concurrent CUDA unsafe) |

---

## 3. “PA02 batch GPU Score — ROI 낮아 미구현”의 의미

### 3.1 ROI = Return on Investment

**구현·검증에 드는 시간(비용) 대비 bag KPI에서 기대되는 이득(수익)이 작다**는 뜻이다.  
“GPU가 쓸모없다”가 아니라 **“추가 GPU 레이어를 새로 만들 가치가 없다”**는 판단이다.

### 3.2 L3 bag 병목 분해

데이터: `data/pa02/pa02_l3_profile_bottleneck.txt`

| 구간 | ms | match 대비 |
|------|---:|-----------:|
| `[match]` 전체 | 87,866 | 100% |
| `[score_all]` (PA01, 이미 GPU) | 34,786 | 39.6% |
| **Score_orchestration** | **~22,235** | Score의 39% |
| Branch (depth=3 집중) | ~32,448 (depth=3 sum) | — |

`Score_orchestration` = `[Score]` cumulative − Σ `score_all.elapsed`  
→ bucket 분류, cx/cy 채우기, `std::sort`, Branch 재귀·함수 오버헤드 등 **CPU 작업**.

### 3.3 batch GPU가 줄일 수 있는 것 vs 못 줄이는 것

| 구간 | 현재 | batch GPU 기대 효과 |
|------|------|---------------------|
| coarse n=256 score_all | **이미 scan당 CUDA** (~81% cuda path) | launch/H2D 추가 절감만 → Phase 3 T=64도 ±0.2% |
| leaf n=4 (75,507 calls) | CPU `n4` path (threshold 미만) | 별도 multi-scan 커널 필요, 구현 복잡 |
| Score_orchestration ~22 s | CPU loop/sort | **GPU 커널 추가로 직접 감소 불가** |

L3 bucket이 orchestration을 ~26 s → ~22 s로 이미 줄였다.  
남은 22 s에 batch GPU를 얹어도 **match KPI 수백 ms~1 s 미만** 기대 → 과제 시간 대비 ROI 낮음.

### 3.4 microbench vs bag (Phase 3 재확인)

| variant | microbench avg (ms) | bag match (ms) |
|---------|--------------------:|---------------:|
| cpu_score | **36.6** (가장 빠름) | 91,750 (가장 느림) |
| hybrid_prod | 46.7 | **87,866** (가장 빠름) |

연속 microbench는 GPU launch/H2D overhead 때문에 CPU가 유리해 보이지만,  
sporadic bag + grid pointer cache 환경에서는 **hybrid(GPU)가 KPI 우승** (PA01 threshold sweep과 동일 패턴).

---

## 4. “concurrent CUDA unsafe”의 의미와 대안

### 4.1 문제: 전역 CUDA singleton

`score_all_cuda.cu`:

```cpp
DeviceBuffers& Buffers() {
  static DeviceBuffers buf;  // d_grid, d_cx, d_cy, d_score, stream — 1세트
  return buf;
}
```

Branch sibling OMP (`fast_matcher.cpp`, GPU 빌드에서는 **컴파일 제외**):

```cpp
#if defined(PA01_HAS_OPENMP) && !defined(PA01_USE_GPU)
#pragma omp parallel
  for (...) {
    Score(...);   // → score_all → ScoreCandidates() → 같은 Buffers()
    Branch(...);
  }
#endif
```

여러 OMP 스레드가 동시에 `ScoreCandidates()`를 호출하면:

| 충돌 | 결과 |
|------|------|
| 같은 `d_cx`/`d_score`에 동시 H2D | 입력·출력 덮어씀 |
| 같은 `cudaStream_t`에 kernel interleave | 잘못된 score 계산 |
| D2H 타이밍 꼬임 | `cand[].score` corruption |

→ **data race**로 score 유실·오염 가능. “덮어쓰기”보다 정확한 표현은 **공유 GPU 버퍼에 대한 동시 접근**.

참고: `thread_local` child buffer는 스레드별로 안전하지만, **CUDA Buffers()는 thread_local이 아님**.

### 4.2 안전장치 추가 가능 여부

| 방법 | 안전성 | bag 기대 |
|------|--------|----------|
| mutex로 `ScoreCandidates` 직렬화 | 안전 | OMP 병렬 이득 상쇄 → **느려질 가능성** |
| thread별 `DeviceBuffers` + stream | 안전 | Jetson VRAM/SM 제한, concurrent kernel 이득 미미 |
| GPU worker 1스레드 + 작업 큐 | 안전 | 설계 복잡, latency 증가 |
| multi-scan batch 커널 | 안전(설계 시) | launch overhead 절감; T=64 실측 ±0.2% |
| Branch OMP + CPU score_all | — | cpu_score bag **+4.4%** |

CPU-only Branch OMP microbench: sibling OMP **−22%** (28.4→22.2 ms).  
GPU bag production에서는 concurrent CUDA 수정 비용 대비 bag 이득 불확실 → **compile-time off** (`PA02_BRANCH_OMP_MIN=999999`).

### 4.3 CMake 주석 (근거)

`CMakeLists.txt`:

```cmake
# Branch sibling OMP when cand_in >= N (L2+, CPU-only builds). 999999=off (required for GPU score_all).
set(PA02_BRANCH_OMP_MIN "999999" ...)
```

---

## 5. 최종 production 빌드

```bash
catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON -DPA02_OPT_LEVEL=3 \
  -DPA02_MAKE_CAND_OMP_MIN=512 -DPA02_BRANCH_OMP_MIN=999999 \
  -DPA01_GPU_THRESHOLD=256
```

| 파라미터 | 값 | 근거 |
|----------|-----|------|
| `PA01_GPU_THRESHOLD` | 256 | bag sweep 최소 (37,367 ms) |
| `PA02_OPT_LEVEL` | 3 | match 90,482→87,866 ms (−2.9%) |
| `PA02_MAKE_CAND_OMP_MIN` | 512 | hot path n=256 → OMP 미발동 |
| `PA02_BRANCH_OMP_MIN` | 999999 | GPU build concurrent CUDA unsafe |

---

## 6. 관련 문서·데이터

| 파일 | 내용 |
|------|------|
| `docs/PA02_OPTIMIZATION_REVIEW.md` | CPU/GPU 역할, Phase 3 hybrid |
| `docs/PA01_PA02_OPTIMIZATION_COMPLETE.md` | PA01+PA02 전체 타임라인 |
| `docs/PA02_STRUCTURAL_EXPERIMENTS.md` | upstream 구조 실험, ShrinkToFit 기각 |
| `data/bench/pa02_phase3_hybrid_20260531_151643/` | hybrid vs cpu_score bag |
| `data/pa02/pa02_l3_profile_bottleneck.txt` | Score orchestration 분해 |

---

## 7. 보고서용 한 문단 (복붙용)

PA02 최적화는 PA01 GPU hybrid(`score_all` n≥256 CUDA, bag path ~81%)를 유지한 채 matcher CPU orchestration(make_cand reserve, Branch buffer reuse, Score scan-bucket)만 개선하였다. Phase 3 hybrid sweep에서 L3+T=256이 bag match 87,866 ms로 CPU-only(91,750 ms) 대비 4.4% 우수하여 채택하였으며, microbench만으로 CPU/GPU 정책을 결정하지 않았다. `best_score`는 per-scan matcher 정확도 회귀 검증에 사용하였고, upstream ShrinkToFit 실험에서 best_score 0.783→0.748·coarse_n 3840→3600 악화로 기각하였다. Branch sibling OMP는 CUDA 전역 버퍼 singleton과의 concurrent access로 unsafe하여 GPU build에서 compile-time off하였으며, batch GPU Score·per-thread CUDA buffer는 구현 ROI 대비 bag KPI 추가 이득이 미미하여 미구현하였다.
