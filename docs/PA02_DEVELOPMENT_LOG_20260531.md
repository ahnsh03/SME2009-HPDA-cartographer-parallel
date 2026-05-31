# PA02 개발·트러블슈팅 로그 (2026-05-31)

PA02 Phase 0~3 완료 이후, CPU/GPU 검토·Phase 3 hybrid 결정·upstream 구조 실험까지 **오늘 진행한 작업과 이슈**를 시간순으로 정리한다.

> Jetson: Docker `student_19`, catkin `/root/catkin_ws`  
> bag KPI: `[match]` cumulative (ms)  
> 최종 채택: **PA02_OPT_LEVEL=3** + PA01 L9 GPU T=256

---

## 1. 오늘 작업 타임라인

| 순서 | 작업 | 산출 / 커밋 |
|------|------|-------------|
| 1 | PA02 CPU/GPU 역할 검토, score_all path 분석 | `docs/PA02_OPTIMIZATION_REVIEW.md`, `scripts/pa02_analyze_score_paths.py` |
| 2 | Jetson 추가 실험 (make_cand OMP, Branch OMP, L2 vs L3) | `scripts/pa02_review_experiments.sh`, `data/bench/pa02_review_20260531_150414/` |
| 3 | Phase 3 hybrid 결정 sweep (L3+GPU vs CPU-only vs T=64) | `scripts/pa02_phase3_hybrid_sweep.sh`, `data/bench/pa02_phase3_hybrid_20260531_151643/` |
| 4 | PA01/PA02 통합 타임라인 문서 | `docs/PA01_PA02_OPTIMIZATION_COMPLETE.md` |
| 5 | upstream Cartographer 참고 구조 실험 L4~L6 | `scripts/pa02_structural_sweep.sh`, `ref/cartographer/` |
| 6 | ShrinkToFit 정확도 회귀 → revert, L4/L5 재정의 | `docs/PA02_STRUCTURAL_EXPERIMENTS.md` |
| 7 | FAQ·분석 정리 | `docs/PA02_FAQ_AND_ANALYSIS.md` (본 문서와 쌍) |

---

## 2. Phase 0~3 누적 결과 (기준선)

| Level | 변경 | bag match (ms) | best_score |
|------:|------|---------------:|-----------:|
| L0 | baseline | 90,482 | 0.783 |
| L1 | make_cand reserve | ~90,701 | 0.783 |
| L2 | Branch buffer reuse | 89,926 | 0.783 |
| **L3** | **Score scan-bucket** | **87,866** | **0.783** |

**L0→L3: −2.9%**. `score_all`(PA01) ~34,786 ms 고정, matcher_scope ~53 s가 PA02 타깃.

---

## 3. 오늘 실험 A — CPU/GPU 검토 및 파라미터 정당화

### 3.1 score_all path 분해

L0/L1/L3 bag 로그 공통 패턴:

| path | elapsed 비중 | 대표 n |
|------|-------------:|--------|
| **cuda** | **~81%** | 256 |
| n4 | ~18% | 4 |
| interchange | <1% | 2 |

→ GPU는 PA01 hybrid가 담당. PA02 Phase 1~3은 CPU orchestration.

### 3.2 make_cand OMP sweep

hot path 16×16=256 후보 → `PA02_MAKE_CAND_OMP_MIN=512`면 OMP **미발동**.

| omp_min | avg_ms (bag-hot 16×16) |
|--------:|-----------------------:|
| 999999 (serial) | **0.0020** |
| 256 | 0.0042 (×2.1 slower) |
| 512 | 0.0020 |

**채택: 512**

### 3.3 Branch sibling OMP (CPU-only microbench)

| branch_omp_min | match avg_ms |
|---------------:|-------------:|
| 999999 | 28.41 |
| 128 | **22.47** (−22%) |

GPU bag build: `#ifndef PA01_USE_GPU`로 OMP **compile-time off** → `PA02_BRANCH_OMP_MIN=999999`.

### 3.4 Score L2 vs L3 (GPU L9 환경)

| build | L2 | L3 | Δ |
|-------|---:|---:|--:|
| GPU L9 T=256 | 47.61 ms | **44.33 ms** | **−6.9%** |

Phase 3 CPU bucket 이득이 CPU-only artifact가 아님을 확인.

---

## 4. 오늘 실험 B — Phase 3 hybrid 결정

**질문:** L3 CPU bucket + GPU kernel hybrid vs CPU-only vs GPU threshold 64?

실행: `./scripts/pa02_phase3_hybrid_sweep.sh --bag`

| variant | match (ms) | score_all (ms) | best_score |
|---------|----------:|---------------:|-----------:|
| **hybrid_prod (L3, T=256)** | **87,866** | **34,786** | 0.783 |
| gpu_aggr (L3, T=64) | 87,713 | 35,152 | 0.783 |
| legacy_gpu (L2, T=256) | 89,926 | 35,043 | 0.783 |
| cpu_score (L3, T=999999) | 91,750 | 47,325 | 0.783 |

**결정:** hybrid_prod 채택. CPU-only는 microbench 36.6 ms “승”이나 bag +4.4%.

---

## 5. 오늘 실험 C — upstream 구조적 개선

upstream: `ref/cartographer/.../fast_correlative_scan_matcher_2d.{h,cc}` (read-only diff)

### 5.1 E1 ShrinkToFit (MakeBounds) — **기각**

| Level | match (ms) | coarse_n | best_score |
|-------|----------:|---------:|-----------:|
| L3 | 88,276 | 3840 | **0.783** |
| L4 (+ShrinkToFit) | 91,326 | **3600** | **0.748** |

- 탐색 공간 축소 → 최적 pose 후보 제거
- 과제 `score_all`은 OOB 포인트 0 처리 → upstream과 scoring semantics 불일치
- microbench L6 34.2 ms “승” vs bag L3 승 → **microbench≠bag 재확인**

**조치:** ShrinkToFit 코드 revert (`a2b2ca1`)

### 5.2 E2 exact reserve (MakeLowCands) — 미채택

데이터: `data/bench/pa02_structural_20260531_161217/bag_l3_l5.csv`

| Level | match (ms) | Δ vs L3 | best_score |
|-------|----------:|--------:|-----------:|
| L3 | 88,145 | — | 0.783 |
| L4 | 88,461 | +0.4% | 0.783 |
| L5 | 88,673 | +0.6% | 0.783 |

bag KPI ±0.6% 노이즈, microbench −6.4%는 bag에 미반영 → **L3 유지**.

### 5.3 E3 Score tweak (sort-skip, presize) — 미채택

L5: match +0.6% vs L3, Branch −1.2% but match KPI 악화 방향 → 미채택.

### 5.4 OPT level 재번호 (ShrinkToFit revert 후)

| Level | 내용 | production |
|------:|------|:----------:|
| L3 | Phase 1~3 (make_cand + Branch + Score bucket) | **채택** |
| L4 | MakeLowCands exact reserve | 코드 존재, 미채택 |
| L5 | Score sort-skip + presize | 코드 존재, 미채택 |
| ~~L4 shrink~~ | ~~ShrinkToFit~~ | **제거** |

---

## 6. 트러블슈팅

### 6.1 Jetson `git pull` 충돌

**증상:** untracked local data/log 파일 때문에 pull 실패.

**해결:** Jetson에서 불필요 untracked 파일 정리 후 `git pull` (`git clean` / `rm`).

### 6.2 `pa02_review_experiments.sh` self-copy 버그

**증상:** 스크립트가 자기 자신을 잘못 복사하는 버그.

**해결:** 경로 수정 (`aadb182`).

### 6.3 `pa02_structural_sweep.sh` 이슈

| 이슈 | 처리 |
|------|------|
| self-copy 버그 (review와 유사) | 스크립트 수정 |
| Jetson `column` 명령 없음 | non-fatal, CSV는 수동/PC에서 확인 |

### 6.4 ShrinkToFit 정확도 회귀

**증상:** L4 bag match 악화, `best_score` 0.783→0.748, `coarse_n` 3840→3600.

**원인:** local bounds shrink가 valid-but-partial-overlap 후보까지 제거.

**해결:** ShrinkToFit revert, 실험 E1 **기각** 문서화, L4/L5를 reserve/tweak로 재정의.

### 6.5 `pa02_microbench_gpu` PA02 매크로

**증상:** GPU microbench 빌드에서 PA02 매크로 누락.

**해결:** CMake/target compile definitions 수정 (`c54bb6f`).

### 6.6 ROS_MASTER_URI 불일치 (로그)

**증상:** 일부 run 로그에 `ROS_MASTER_URI=http://localhost:11311` vs `192.168.0.106:11311`.

**영향:** bag replay는 정상 완료. KPI 비교 시 동일 Jetson 세션 내 상대 비교로 해석.

### 6.7 structural sweep 1차 vs 2차 run

| run dir | 내용 |
|---------|------|
| `pa02_structural_20260531_155906` | L3~L6 **ShrinkToFit 포함** (실패 기록) |
| `pa02_structural_20260531_161217` | ShrinkToFit revert 후 L3~L5 (E2/E3) |

2차 run이 production 코드 상태와 일치.

---

## 7. Git 커밋 이력 (2026-05-31, PA02 관련)

```
87657c3 Add PA02 structural experiment results and analysis (ShrinkToFit rejected)
a2b2ca1 Fix PA02 L4-L5: drop ShrinkToFit after accuracy regression in bag
f72b259 PA02 L4-L6: upstream-inspired structural opts + sweep script
6f0dd6f Add PA01/PA02 optimization complete doc and reorganize pa01 data
cc461e3 Add Phase 3 hybrid vs CPU-only vs GPU-heavy bag experiment results
9de52e2 Add Phase 3 CPU/GPU hybrid decision sweep script
f442e0a Document PA02 CPU/GPU review and add Jetson validation experiments
c54bb6f Fix PA02 macros in pa02_microbench_gpu build
aadb182 Fix self-copy bug in pa02_review_experiments.sh
7e0fe0a Add PA02 review experiments and score_all path analysis
35b3cb3 Add PA02 L3 bag profile data (Score scan-bucket pipeline)
3f17db3 PA02 Phase 3: Score scan-bucket pipeline with buffer reuse
```

---

## 8. 최종 상태

### 채택

```bash
catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON -DPA02_OPT_LEVEL=3 \
  -DPA02_MAKE_CAND_OMP_MIN=512 -DPA02_BRANCH_OMP_MIN=999999 \
  -DPA01_GPU_THRESHOLD=256
```

### 미채택 (의도적)

| 항목 | 이유 |
|------|------|
| ShrinkToFit (E1) | best_score 회귀 |
| exact reserve (E2), Score tweak (E3) | bag KPI 무변화 |
| CPU-only score (T=999999) | bag +4.4% |
| Branch sibling OMP (GPU build) | concurrent CUDA unsafe |
| batch GPU Score | ROI 낮음 (§PA02_FAQ_AND_ANALYSIS.md) |

### 남은 작업 (과제)

- 6p PDF 보고서 (`docs/PA01_PA02_OPTIMIZATION_COMPLETE.md`, `docs/PA02_FAQ_AND_ANALYSIS.md` 참고)
- 선택: Jetson confirmatory bag run (L3, ROS_MASTER_URI 통일)

---

## 9. 관련 파일

| 경로 | 용도 |
|------|------|
| `docs/PA02_FAQ_AND_ANALYSIS.md` | best_score, GPU, ROI, CUDA FAQ |
| `docs/PA02_OPTIMIZATION_REVIEW.md` | CPU/GPU 검토 상세 |
| `docs/PA02_STRUCTURAL_EXPERIMENTS.md` | upstream 구조 실험 |
| `docs/PA01_PA02_OPTIMIZATION_COMPLETE.md` | 전체 타임라인 |
| `scripts/pa02_phase3_hybrid_sweep.sh` | hybrid 결정 sweep |
| `scripts/pa02_structural_sweep.sh` | 구조 실험 sweep |
| `data/bench/pa02_phase3_hybrid_20260531_151643/` | hybrid bag CSV |
| `data/bench/pa02_structural_20260531_161217/` | E2/E3 bag CSV |
