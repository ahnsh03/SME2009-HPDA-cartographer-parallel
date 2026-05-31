# PA02 구조적 개선 실험 (upstream Cartographer 참고)

> Jetson bag KPI = `[match]` cumulative (ms)  
> PA01 고정: L9 + GPU T=256  
> upstream 참고: `ref/cartographer/.../fast_correlative_scan_matcher_2d.{h,cc}`

---

## 1. 실험 설계

upstream clone 후 **read-only diff**로 후보를 선정하고, **PA02_OPT_LEVEL** 누적 방식으로 Jetson bag + microbench를 순차 측정했다.

| 실험 | upstream 근거 | 구현 | 채택 |
|------|---------------|------|------|
| **E1 ShrinkToFit** | `SearchParameters::ShrinkToFit` | L4 local bounds에 map clamp | **기각** (정확도 회귀) |
| **E2 exact reserve** | `GenerateLowestResolutionCandidates` | L4 후보 수 선계산 `reserve` | bag 이득 없음 → **미채택** |
| **E3 Score tweak** | (자체) sort-skip n≤1, scores presize | L5 | bag 이득 없음 → **미채택** |

**최종 채택:** **PA02_OPT_LEVEL=3** (Phase 1~3: make_cand + Branch + Score bucket)

---

## 2. 실험 E1 — MakeBounds ShrinkToFit (실패)

### 가설
Cartographer처럼 local window `[-lin,+lin]`과 **per-scan map in-bounds** 교집합을 쓰면 invalid 후보를 줄여 matcher_scope가 감소한다.

### 구현
`MakeBounds` local path에 upstream `ShrinkToFit` 동일 로직 적용 (L4, 1차 sweep).

### 결과 (Jetson `pa02_structural_20260531_155906`)

| Level | match (ms) | coarse_n | **best_score** | score_all (ms) |
|-------|----------:|---------:|---------------:|---------------:|
| L3 (baseline) | 88,276 | **3840** | **0.783** | 35,518 |
| L4 (+ShrinkToFit) | 91,326 | **3600** | **0.748** | 44,296 |
| L5 | 91,853 | 3600 | 0.748 | 45,319 |
| L6 | 92,567 | 3600 | 0.777 | 49,112 |

### 분석
- **coarse_n 3840→3600**: 탐색 공간 축소 (grid_span 16×16 → 16×15 등)
- **best_score 0.783→0.748**: 최적 pose 후보가 탐색 공간 밖으로 제거됨
- 우리 `score_all`은 **일부 OOB 포인트를 0으로 처리**해 partial overlap 후보가 높은 점수를 낼 수 있음. upstream은 **모든 포인트 in-map** 후보만 생성 → 동일 ShrinkToFit을 그대로 적용하면 **과제 코드와 scoring semantics 불일치**

### 결론
**정확도 회귀 → 기각.** microbench만 보면 L4~L6이 빨라 보였으나 bag KPI·best_score 모두 악화 (microbench≠bag 재확인).

---

## 3. 실험 E2 — MakeLowCands exact reserve

### 가설
upstream `GenerateLowestResolutionCandidates`처럼 scan×bounds로 **정확한 후보 개수**를 `reserve`하면 vector growth/realloc 제거.

### 구현 (ShrinkToFit 제거 후 L4)

```cpp
// bounds별 (nx*ny) 합산 → out.reserve(total)
```

### 결과 (Jetson `pa02_structural_20260531_161217`)

| Level | match (ms) | Δ vs L3 | best_score | score_all (ms) |
|-------|----------:|--------:|-----------:|---------------:|
| L3 | 88,145 | — | 0.783 | 35,805 |
| **L4** | 88,461 | **+0.4%** | 0.783 | 36,032 |
| L5 | 88,673 | +0.6% | 0.783 | 36,395 |

microbench (동일 run): L3 46.8 → L4 45.4 → L5 43.8 ms/match (−6.4%)

### 분석
- bag: match **±0.6%** — SLAM 비결정성·노이즈 범위, **유의미한 KPI 개선 없음**
- make_cand cumulative L3 816 ms vs L4 807 ms (−1%) — match 1% 미만 구간
- **정확도 유지** (best_score=0.783, coarse_n=3840)

### 결론
**안전하지만 bag KPI 채택 이득 없음** — 코드 단순화 목적 외에는 L3 유지.

---

## 4. 실험 E3 — Score sort-skip + buffer presize (L5)

### 가입
- `cand->size() <= 1`일 때 `std::sort` 생략
- L3 bucket path에서 `scores.resize(nb)` 사전 할당

### 결과
위表 L5: match 88,673 ms (+0.6% vs L3), Branch 43,914 ms (−1.2%), Score_orch ~22,024 ms.

### 분석
- Branch −1% 정도는 있으나 match KPI는 **악화 방향** (노이즈)
- leaf n=4 호출이 대부분이나 sort cost는 orchestration의 일부에 불과

### 결론
**미채택** — L3 대비 bag 이득 없음.

---

## 5. microbench vs bag (구조 실험에서 재확인)

### ShrinkToFit run (155906)

| Level | microbench (ms) | bag match (ms) | best_score |
|-------|----------------:|---------------:|-----------:|
| L3 | 48.0 | 88,276 | 0.783 |
| L4 shrink | 43.3 | 91,326 | **0.748** |
| L6 | 34.2 | 92,567 | 0.777 |

→ microbench **승자(L6)** ≠ bag **승자(L3)**

### exact reserve run (161217)

| Level | microbench (ms) | bag match (ms) |
|-------|----------------:|---------------:|
| L3 | 46.8 | 88,145 |
| L5 | 43.8 | 88,673 |

→ microbench 개선(−6%)이 bag KPI에 **반영되지 않음**

---

## 6. PA02 전체 누적 (L0 → L3, 기존 + 본 실험)

| Level | 내용 | match (ms) | best_score | 비고 |
|------:|------|----------:|-----------:|------|
| L0 | baseline | 90,482 | 0.783 | |
| L3 | Phase 1~3 | **87,866** | 0.783 | **채택** (pa02_l3_profile) |
| L3′ | 동일 세션 재측정 | 88,145 | 0.783 | ±0.3% 노이즈 |
| L4 shrink | **기각** | 91,326 | **0.748** | 정확도 회귀 |
| L4 reserve | 미채택 | 88,461 | 0.783 | KPI 무변화 |
| L5 tweak | 미채택 | 88,673 | 0.783 | KPI 무변화 |

**L0→L3: −2.9%** (90,482→87,866 ms). 구조 실험 추가 후에도 **L3가 최적**.

---

## 7. upstream 대비 — 왜 더 못 줄였나

| upstream 구조 | 우리 L3 | 추가 이식 시 |
|---------------|---------|-------------|
| `ScoreCandidates` inline CPU loop | scan별 `score_all` + **GPU hybrid** | inline 이식 → GPU 상실 |
| `ShrinkToFit` | local ±lin 고정 | **정확도 회귀** (E1) |
| `PrecomputationGrid` probability | uchar max-pool | grid 빌드 1회, bag 영향 미미 |
| batch candidate reserve | L2 추정 reserve | exact reserve bag 무변화 (E2) |

**score_all(PA01)이 이미 −61%** — matcher_scope ~53 s 중 orchestration·B&B 구조가 잔여 한계.

---

## 8. 재현

```bash
# Jetson Docker
cd /root/catkin_ws/src/hpda
./scripts/pa02_structural_sweep.sh
```

산출:
- `data/bench/pa02_structural_20260531_161217/` — E2/E3 (L3~L5)
- `data/bench/pa02_structural_20260531_155906/` — E1 ShrinkToFit (L3~L6, **실패 기록**)

upstream 참고:
```bash
# PC (catkin 밖)
git clone --depth 1 https://github.com/cartographer-project/cartographer.git ref/cartographer
```

---

## 9. 보고서용 결론 (6p PDF)

1. upstream **ShrinkToFit** 실험 → **best_score 회귀**로 기각 (데이터: E1 표)
2. **exact reserve·Score tweak** → microbench만 개선, **bag KPI 무변화**
3. **최종 PA02_OPT_LEVEL=3** — make_cand + Branch + Score bucket + PA01 GPU hybrid
4. 추가 구조 변경 ROI 낮음; 보고서에 **실패 실험(E1)** 포함 시 설득력 ↑
