# PA02 최적화 검토: CPU/GPU 역할·파라미터 근거·추가 실험 (2026-05-31)

> bag KPI 기준: `[match]` cumulative (ms). PA01 고정: L9 + GPU threshold **256**.

---

## 1. CPU와 GPU를 둘 다 썼는가?

**예 — 하지만 역할이 분리되어 있다.**

| 계층 | 담당 | CPU | GPU |
|------|------|-----|-----|
| **PA01 `score_all`** | correlative score 커널 | n=4 전용 `ScoreN4`, n<256 소량 | **n≥256 → CUDA** (`path=cuda`) |
| **PA02 Phase 1** | `make_cand` | reserve + (OMP off at hot path) | 없음 |
| **PA02 Phase 2** | `Branch` | buffer reuse, empty skip | 없음 (sibling OMP는 GPU 빌드에서 **컴파일 차단**) |
| **PA02 Phase 3** | `Score` 오케스트레이션 | scan bucket, buffer reuse | 없음 (기존 PA01 hybrid 호출) |

### bag 실측: score_all dispatch path (`scripts/pa02_analyze_score_paths.py`)

L0/L1/L3 bag 로그에서 동일 패턴:

| path | elapsed 비중 | 대표 n | 의미 |
|------|-------------:|--------|------|
| **cuda** | **~81%** | **256** | coarse scoring (3840÷15 scans = 256) |
| **n4** | ~18% | 4 | B&B leaf 고빈도 |
| interchange | <1% | 2 | 경계 케이스 |

→ **GPU는 PA01 hybrid가 담당**하고, PA02는 그 위의 **CPU 오케스트레이션**을 줄였다. Phase 3에서 별도 batch GPU를 넣지 않은 것은, 병목이 커널(이미 GPU)이 아니라 **필터·할당·호출 구조**였기 때문이다 (`Score_orchestration` ~22–26 s).

CUDA 쪽 grid H2D는 `score_all_cuda.cu`의 `UploadGrid()` pointer cache로 **이미 1회 업로드**된다. Phase 3 추가 GPU 실험(“매 호출 H2D”)은 해당되지 않음.

---

## 2. 최적화 방법이 적절했는가?

### Phase 1 — make_cand (CPU)

| 항목 | 판단 |
|------|------|
| 대상 | match 1.3% — 절대량은 작지만 실험 파이프라인 검증용 1순위 |
| 방법 | `vector::reserve` + OpenMP optional |
| 적절성 | **적절** — side effect 없음, `[score_all]` regression 없음 |

### Phase 2 — Branch (CPU)

| 항목 | 판단 |
|------|------|
| 대상 | Branch cumulative ~45–55 s, depth=3 stratum 집중 |
| 방법 | `thread_local` child buffer, empty quadrant skip, MakeLowCands reserve |
| sibling OMP | CPU microbench에서 **−22%** (28.4→22.2 ms) but **GPU bag 빌드에서는 `#ifndef PA01_USE_GPU`로 비활성** — concurrent CUDA `score_all` unsafe |
| 적절성 | **적절** — GPU bag에서 OMP off는 코드·실험 모두 정당 |

### Phase 3 — Score (CPU orchestration)

| 항목 | 판단 |
|------|------|
| 대상 | Score_orchestration ~25 s (Score의 ~40%) |
| 방법 | 1-pass scan bucket, `thread_local` cx/cy/scores/buckets, empty scan skip |
| 미구현 | batch GPU / per-scan H2D 제거 — **이미 PA01 CUDA cache + orchestration 병목이라 우선순위 낮음** |
| 적절성 | **적절** — L3 bag: Score −5.6%, match −2.1%, score_all ±0.7%, best_score 동일 |

---

## 3. 실험적으로 선택된 수치 — 근거 표

| 파라미터 | 채택값 | 실험 | 근거 |
|----------|--------|------|------|
| `PA01_GPU_THRESHOLD` | **256** | `data/bench/pa01_opt9_threshold_sweep.csv` (bag) | T=64: 40.7 s > T=256: **37.4 s**; T=2048: GPU coarse(256) 미사용 → 51.6 s |
| `PA02_MAKE_CAND_OMP_MIN` | **512** | Jetson sweep (`pa02_review_*_make_cand_omp_sweep.csv`) | hot path n_out=**256** → OMP **미발동**; omp_min=256일 때 0.0042 ms > serial 0.0020 ms |
| `PA02_BRANCH_OMP_MIN` | **999999** (off) | CPU sweep + compile guard | CPU-only: omp=128 → 22.2 ms (best); **GPU build: OMP 코드 자체 제외** |
| Phase 3 Score | L3 bucket | bag + microbench | 아래 §4 |

---

## 4. 추가 실험 결과 (2026-05-31 Jetson)

실행: `./scripts/pa02_review_experiments.sh`  
산출: `data/bench/pa02_review_20260531_150414/`

### 4.1 make_cand OMP (bag-hot 16×16, n=256)

| omp_min | avg_ms |
|--------:|-------:|
| 999999 (serial) | **0.0020** |
| 256 | 0.0042 (×2.1 slower) |
| 512 | 0.0020 (OMP off: 256<512) |

→ **512 선택 정당**: 실제 bag에서 OMP가 켜지지 않고, 켜지는 큰 grid에서도 serial과 동률.

### 4.2 Branch sibling OMP (CPU L6 microbench)

| branch_omp_min | match avg_ms | score |
|---------------:|-------------:|------:|
| 999999 | 28.41 | 0.1265 |
| 128 | **22.47** | 0.1265 |
| 256 | 22.15 | 0.1265 |

→ CPU-only 환경에서는 OMP 유리. **production GPU bag는 compile-time off** → 999999는 “의도적 off”이지 “실험 없이 임의”가 아님.

### 4.3 Score L2 vs L3 (microbench + GPU L9)

| build | L2 avg_ms | L3 avg_ms | Δ | score |
|-------|----------:|----------:|--:|------:|
| CPU L6 | 28.49 | **26.55** | −6.8% | 0.1265 |
| **GPU L9 T=256** | 47.61 | **44.33** | **−6.9%** | 0.1265 |

→ Phase 3 이득이 **CPU-only artifact가 아님** — production과 동일 hybrid에서도 재현.

### 4.4 bag KPI 누적 (L0→L3)

| Level | match (ms) | Score (ms) | Score_orch (est.) | score_all (ms) |
|-------|----------:|-----------:|------------------:|---------------:|
| L0 | 90,482 | 59,869 | ~26,055 | 33,814 |
| L2 | 89,926 | 60,434 | ~25,391 | 35,043 |
| **L3** | **87,866** | **57,075** | **~22,235** | **34,786** |

---

## 5. 결론 (보고서용)

1. **CPU+GPU hybrid는 PA01 `score_all`에서 이미 동작** — bag 시간의 ~81%가 CUDA path (n=256 coarse per scan).
2. **PA02 Phase 1–3은 CPU 오케스트레이션** — GPU 커널/ threshold를 건드리지 않아 `[score_all]` regression이 없음.
3. **모든 CMake threshold는 sweep/bag로 정당화** — 256(GPU), 512(make_cand OMP off), 999999(Branch OMP off on GPU).
4. **Phase 3 CPU bucket은 적절** — orchestration −12% (vs L2), match −2.1%, 정확도 동일.
5. **미적용 Branch OMP** — CPU에서만 이득; GPU bag에서는 concurrent CUDA 위험 + compile guard.

---

## 6. 관련 파일

| 파일 | 용도 |
|------|------|
| `scripts/pa02_review_experiments.sh` | 추가 실험 일괄 실행 |
| `scripts/pa02_analyze_score_paths.py` | bag score_all path 분해 |
| `data/bench/pa02_score_paths_bag.txt` | L0/L1/L3 path breakdown |
| `data/bench/pa02_review_20260531_150414/` | OMP / L2 vs L3 sweep CSV |
| `docs/PA02_PROFILING_STRATEGY.md` | Phase 0 병목 분석 |
