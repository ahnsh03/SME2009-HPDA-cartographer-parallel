# PA02 프로파일링·최적화 전략

> PA01 워크플로(`docs/PA01_REPORT_PREP/02_ENVIRONMENT_AND_WORKFLOW.md`)를 그대로 따르되,  
> **모듈 KPI** = bag 1회 동안 `[match]` **cumulative (ms)**.  
> `score_all`은 PA01에서 고정(L9 + `PA01_GPU_THRESHOLD=256`)하고, PA02는 **matcher 오케스트레이션**을 측정·최적화한다.

---

## 0. 패키지 구조와 SLAM에서의 위치

### 0.1 파일·호출 관계

```
fast_correlative_node.cpp          ROS: LaserScan → Match() → pose publish
        │
        ▼
FastMatcher::MatchWithWindow()     [match] KPI — B&B 전체
        │
        ├── MakeScans / MakeBounds / MakeGridStack
        ├── MakeLowCands() ──► make_cand()          [make_cand] [MakeLowCands]
        ├── Score()      ──► score_all()            [Score]     [score_all] (PA01)
        └── Branch()     ──► Score() 재귀          [Branch]
```

| 구성요소 | SLAM 역할 |
|----------|-----------|
| **MakeScans** | 초기 pose 주변 **yaw** 후보(회전된 스캔) 생성 |
| **MakeBounds** | 스캔별 **(x,y) 탐색 창** (linear/global window) |
| **MakeGridStack** | occupancy **다해상도 피라미드** (max-pool, win=2^level) |
| **make_cand** | bounds를 `step` 간격 격자로 훑어 **후보 (cx,cy)** 생성 |
| **score_all** | 후보×스캔 포인트 **correlative score** (PA01 커널) |
| **Score** | 스캔별 후보 필터 → `score_all` → **sort** (B&B 순서용) |
| **Branch** | 점수순 후보 + **가지치기** + 4-way split + 재귀 정밀화 |

알고리즘: Cartographer **Fast Correlative Scan Matcher**의 **Branch-and-Bound** (다해상도 + 정렬된 상한으로 가지치기).

### 0.2 PA01 / PA02 역할 분리

| | PA01 | PA02 |
|---|------|------|
| 대상 | `score_all` 커널 | `make_cand`, `Score`, `Branch` (+ 래퍼) |
| 빌드 | `PA01_OPT_LEVEL=9`, GPU threshold **256** (bag sweep 확정) | `PA02_OPT_LEVEL`, `PA02_PROFILE=ON` |
| 고정 검증 | — | 동일 bag에서 `[score_all]` cumulative **~33.8k ms** 유지 (regression) |

---

## 1. 실험으로 병목 확인하기 (코드 추측 금지)

최적화 대상은 **“루프가 무거워 보인다”**가 아니라, **동일 bag·동일 빌드의 프로파일 로그**에서 아래 절차로 확정한다.

### 1.1 원칙

1. **KPI는 bag cumulative** — 벽시계 96 s가 아님 (`docs/PA01_ADDITIONAL_EXPERIMENTS_20260531.md`와 동일).
2. **태그별 `elapsed` 합**과 **마지막 줄 `cumulative`**를 함께 본다 (호출 수·평균 해석용).
3. **중첩 타이머**를 인지한다 — `Branch` ⊃ `Score` ⊃ `score_all`. 태그 cumulative를 단순 합산하면 이중 계상된다.
4. **선택(CPU/GPU/하이브리드)**은 microbench **+** bag 둘 다에서 우승한 쪽만 채택 (PA01 threshold sweep 교훈).
5. (선택) **Ablation** — 함수 일부를 no-op으로 바꿔 match delta를 직접 측정하면 보고서 설득력이 가장 크다 (§1.5).

### 1.2 Phase 0 필수 실험: bag 프로파일 1회

```bash
# Jetson
cd /root/catkin_ws
catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON \
  -DPA02_PROFILE=ON -DPA02_OPT_LEVEL=0
source devel/setup.bash
export ROS_IP=192.168.0.104
export ROS_MASTER_URI=http://192.168.0.106:11311
scripts/pa02_bag_profile.sh pa02_l0_profile
```

산출물 (`data/pa02/`):

| 파일 | 용도 |
|------|------|
| `*_summary.txt` | 태그별 마지막 cumulative (한눈 요약) |
| `*_<tag>_clean.log` | 호출별 `elapsed`, `depth`, `n_cand` 등 **분해 분석** |
| `*_env.txt` | `PA01_GPU_THRESHOLD=256`, dispatch 매크로 증빙 |

### 1.3 분석 A — 모듈 KPI 분해 (score_all vs matcher)

**질문:** bag 한 번에서 시간의 몇 %가 PA01 커널이고, 몇 %가 PA02 영역인가?

```
match_total     = [match] 마지막 cumulative
score_all_total = [score_all] 마지막 cumulative
matcher_scope   = match_total − score_all_total    # PA02가 줄일 수 있는 상한(이론상)
```

**L0 실측** (`pa02_l0_profile`, 2026-05-31, threshold=256):

| 항목 | ms | match 대비 |
|------|-----|-----------|
| `[match]` | **90,482** | 100% |
| `[score_all]` | **33,814** | **37.4%** (PA01 고정) |
| **matcher_scope** (match − score_all) | **56,667** | **62.6%** |

→ **실험 결론:** 과제 모듈 KPI의 과반은 `score_all` 바깥 **matcher 오케스트레이션**이다. PA02 최적화는 이 **~56.7 s** 구간을 겨냥한다 (코드 구조 추측이 아님).

### 1.4 분석 B — Score: 커널 vs 오케스트레이션

코드상 `score_all()` 호출은 **`FastMatcher::Score()` 안에서만** 일어난다. 따라서:

```
Σ Score.elapsed  ≈  [Score] cumulative
Σ score_all.elapsed  ≈  [score_all] cumulative   (동일 bag, 동일 run)

Score_orchestration = Σ Score.elapsed − Σ score_all.elapsed
```

**포함되는 것 (오케스트레이션):** 스캔별 후보 필터, `vector` 할당/복사, `std::sort`, `score_all` 호출 전후.

**L0 실측:**

| 항목 | ms | 비고 |
|------|-----|------|
| `[Score]` cumulative | 59,869 | Branch 내부 호출 포함 |
| `[score_all]` (동일 run) | 33,814 | Score 안에서 소비 |
| **Score_orchestration** | **~25,955** | **Score의 43.4%** |

→ **실험 결론:** `Score`는 “커널만 도는 함수”가 아니다. **약 26 s**가 sort·필터·호출 구조 오버헤드 → **Phase 3 (`Score` 파이프라인)** 근거.

#### B-1. Score 부하의 입력 크기별 분해 (hot path)

clean log에서 `n_cand=`별 `elapsed` 합:

| n_cand | calls | elapsed 합 (ms) | avg (ms/call) | 해석 |
|--------|------:|----------------:|--------------:|------|
| **3840** | 2,110 | **41,445** | 19.6 | coarse 초기 scoring (`MakeLowCands` 직후, match당 ≈1회) |
| **4** | 74,147 | **17,836** | 0.24 | B&B leaf 근처 **소량 후보·고빈도** |
| 2 | 1,788 | 390 | 0.22 | 경계 케이스 |

→ **실험 결론:** Score 최적화는 **대량 1회(3840)** 와 **소량 다회(4)** 두 regime이 있다. microbench·batch 정책은 **둘 다** sweep해야 한다 (한쪽만 보면 PA01 microbench≠bag 재현).

### 1.5 분석 C — Branch: depth별 부하

`[Branch]` 로그의 `depth=`별 `elapsed` 합 (호출당 wall time, 내부 Score 포함):

| depth | calls | elapsed 합 (ms) | 해석 |
|-------|------:|----------------:|------|
| 0 | 11,592 | ~0.3 | leaf 반환만 (거의 0) |
| 1 | 20,653 | 5,258 | fine 단계 |
| 2 | 43,690 | 14,673 | |
| **3** | **2,113** | **34,679** | **coarse B&B 진입** (match 수와 유사) |

→ **실험 결론:** Branch 비용은 **depth=3 (coarse)** 에 집중. “재귀가 많아 보인다”가 아니라 **로그 stratum**으로 확인.

**Branch 순수 오버헤드** (child 생성·재귀·가지치기만)는 `Branch` ⊃ `Score` 중첩 때문에 한 줄로 안 나뉜다. 보고서용으로는:

- **상한:** `[Branch]` cumulative 54,743 ms (Score+score_all 포함)
- **정밀 분리(선택):** Phase 0b **ablation** (§1.7) 또는 호출 스택 ID 추가 계측

### 1.6 분석 D — make_cand / MakeLowCands

| 태그 | cumulative (ms) | match 대비 |
|------|----------------:|-----------|
| `[make_cand]` | 1,216 | **1.3%** |
| `[MakeLowCands]` | 6,365 | 7.0% |
| MakeLowCands − make_cand (래퍼 추정) | ~5,150 | 5.7% |

→ **실험 결론:** `make_cand` 단독은 KPI에서 **작다**. 그럼에도 Phase 1 대상인 이유는 §3.2 (검증·실험 인프라) 참고. “병목”이라기보다 **측정 가능한 첫 타깃**.

### 1.7 분석 E — (선택) Ablation으로 인과 확인

태그 합만으로 부족할 때, **동일 bag**에서 코드 경로를 잠깐 바꿔 **match delta**를 잰다.

| Ablation | 기대 match 변화 | 확인하는 것 |
|----------|-----------------|-------------|
| `make_cand` → 빈 vector 반환 금지, step만 유지 등 | 소폭 | make_cand 기여 상한 |
| `Score`에서 `sort` 생략 | 중~대 | sort·순서 의존 비용 |
| `Branch` depth=0 즉시 반환 | 대폭 | B&B 전체 기여 |

Ablation은 **L0 baseline 대비 1회**씩만 돌려도 보고서에 “인과 실험”으로 쓸 수 있다. `PA02_OPT_LEVEL` 또는 compile-time `PA02_ABLATION_*` 플래그로 관리 (구현은 Phase 0b).

### 1.8 분석 F — microbench (격리·정책 sweep)

bag는 **전체 SLAM 피드백**이 섞이므로, 함수 단위 가설은 microbench로 **먼저** 좁히고 bag로 **확정**한다.

```bash
cd benchmark
make pa02_microbench YAML=../cartographer_parallel/cartographer_parallel/maps/0501.yaml
# (스크립트 추가 예정) make_cand span sweep, match --baglike
```

| 함수 | microbench 역할 | bag에서 검증할 것 |
|------|-----------------|-------------------|
| make_cand | span×span, OpenMP threshold | `[make_cand]` cumulative ↓, match 소폭 ↓ |
| Score | per-scan vs batch, grid 1×H2D | `Score_orchestration` ↓, `[score_all]` 불변 |
| Branch | cand_in 분포 재현 | depth=3 `[Branch]` elapsed ↓ |

**PA01 교훈:** 연속 microbench crossover(n≥2048) ≠ bag hot path(n=4,256). **bag-like** 모드 필수 (`docs/PA01_ADDITIONAL_EXPERIMENTS_20260531.md` §3.3).

### 1.9 로그 분석 스크립트

`scripts/pa02_analyze_profile.py` — bag 프로파일 후 자동 실행 (`pa02_bag_profile.sh`):

```bash
python3 scripts/pa02_analyze_profile.py pa02_l0_profile --data-dir data/pa02
# → data/pa02/pa02_l0_profile_bottleneck.txt
```

수동 스니펫은 스크립트와 동일 로직; 스크립트가 §A~G 보고서를 생성한다.

### 1.10 중첩 타이머 해석 (실수 방지)

```
[match]  ████████████████████████████████████████  90.5 s
          ├─ MakeLowCands/make_cand  ~6.4 s
          ├─ Score (모든 호출)       ~59.9 s  ← score_all + orchestration
          │    └─ score_all          ~33.8 s  (PA01)
          └─ Branch wall time        ~54.7 s  ← 내부 Score 중복 포함

※ Branch(54.7) + 초기 Score 일부가 겹침 → 막대 합 ≠ match
```

**보고서에 쓸 문장 예:**  
“`[match]` 90.5 s 중 `[score_all]` 33.8 s(37.4%)를 제외한 **56.7 s**를 matcher 오케스트레이션으로 측정하였고, 그 안에서 `[Score]` 오케스트레이션 **26.0 s**, `[Branch]` depth=3 stratum **34.7 s**(elapsed 합)을 로그 분해로 확인하였다.”

---

## 2. 최적화 대상 3함수 — 실험 근거 요약

| 함수 | 실험으로 확인한 사실 | 최적화 Phase | CPU/GPU 방향 |
|------|----------------------|--------------|--------------|
| **make_cand** | match의 **1.3%**; 호출 31k+; 격리·OpenMP sweep 용이 | **1** | **CPU** (데이터 나오기 전 기본 가설) |
| **Branch** | `[Branch]` **54.7 s**; depth=3에 **34.7 s** 집중 | **2** | **CPU** (가지치기·할당·sibling OMP; GPU는 ablation 후) |
| **Score** | Score 내 **43%**가 score_all 외부; n_cand=3840/4 **이중 regime** | **3** | **실험 결정** (grid 1×H2D, batch, CPU sort) |

**score_all**은 PA01 L9+threshold=256 **고정**. Phase 1~3 후에도 `[score_all]` cumulative가 L0 대비 **±몇 % 이내**인지 매 Phase마다 확인 (regression).

### 2.1 왜 “3개”인가 (과제 + 데이터)

1. **과제 범위:** `fast_correlative_scan_matcher` 모듈 내 **서로 다른 단계** (후보 생성 / 점수 오케스트레이션 / B&B).
2. **데이터:** §1.3~1.6에서 matcher_scope **56.7 s**가 실측됨 → 세 단계가 그 구간을 구성.
3. **PA01 분리:** 커널(`score_all`)은 이미 최적화됨; PA02는 **호출 구조**가 병목.

---

## 3. 최적화 순서 (Phase) — 실험 후 적용

| Phase | 목표 | 함수 | 선행 실험 | 적용 후 검증 |
|-------|------|------|-----------|--------------|
| **0** | baseline + **병목 분해** | 전 태그 | §1.2~1.9 | `*_summary.txt`, `*_bottleneck.txt` |
| **0b** | (선택) ablation | — | §1.7 | match delta 표 |
| **1** | CPU | `make_cand` | microbench span sweep | `[make_cand]`↓, `[score_all]` 동일 |
| **2** | CPU | `Branch` | depth / cand_in 분포 | depth=3 `[Branch]`↓ |
| **3** | CPU+GPU | `Score` | §5 실험 B + bag | `Score_orchestration`↓, `[score_all]` 동일 |

Phase 1을 먼저 하는 이유는 **절대 시간이 작아서**가 아니라, **가장 단순한 함수로 “실험→적용→bag 검증” 루틴을 고정**하기 위함이다.

---

## 4. 로그 태그

| 태그 | 위치 | cumulative 의미 |
|------|------|-------------------|
| `[make_cand]` | `score_all.cpp` | 후보 격자 1회 |
| `[MakeLowCands]` | `fast_matcher.cpp` | 스캔별 make_cand + Cand 조립 |
| `[Score]` | `fast_matcher.cpp` | 스캔별 score_all + sort **전체** |
| `[Branch]` | `fast_matcher.cpp` | B&B 1회 (**내부 Score 포함**) |
| `[match]` | `fast_matcher.cpp` | `MatchWithWindow` 1회 |
| `[score_all]` | PA01 | 커널 (고정) |

---

## 5. CPU/GPU·하이브리드 — 실험 설계 (Phase 3 전 확정)

**절차 (함수마다 동일):**

1. 가설 2~3개 (예: Score = “grid 매 호출 H2D” vs “1회 H2D + batch”)  
2. microbench / bag-like로 후보 정렬  
3. bag 1회로 KPI·regression 확인  
4. 승자만 `PA02_*_THRESHOLD` CMake 캐시에 기록 (`PA01_GPU_THRESHOLD`와 동일 패턴)

| 실험 | microbench | bag | 결정 질문 |
|------|------------|-----|-----------|
| **A** | make_cand span sweep | `[make_cand]`, `[match]` | OpenMP threshold |
| **B** | match `--baglike` | `Score_orchestration`, `n_cand` stratum | per-scan vs batch GPU |
| **C** | — | Branch `depth`, `cand_in` | sibling OpenMP threshold |

**보고서 표 (채울 칸):**

| 후보 | microbench | bag `[match]` | bag `Score_orch` | `[score_all]` | 선택 |
|------|------------|---------------|------------------|---------------|------|
| CPU only | | | | | |
| GPU batch | | | | | |
| hybrid @ T= | | | | | |

---

## 6. 빌드·실행

### CMake (Jetson Docker)

```bash
cd /root/catkin_ws
catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON \
  -DPA02_PROFILE=ON -DPA02_OPT_LEVEL=0
source devel/setup.bash
export ROS_IP=192.168.0.104
export ROS_MASTER_URI=http://192.168.0.106:11311
scripts/pa02_bag_profile.sh pa02_l0_profile
# 분해 분석 (§1.9) → data/pa02/pa02_l0_profile_bottleneck.txt 권장
```

### PC deploy (SSH)

```bash
./scripts/pa02_deploy_profile.sh pa02_l0_profile
```

---

## 7. PA02_OPT_LEVEL 로드맵

| Level | 내용 |
|-------|------|
| 0 | 프로파일만; §1 병목 분석 |
| 1 | make_cand CPU (reserve + OpenMP) |
| 2 | Branch CPU (prune + reserve + sibling OMP) |
| 3 | Score pipeline (grid cache + optional batch GPU) |

---

## 8. 역할 분담

| 작업 | 로컬 repo | Jetson |
|------|-----------|--------|
| 코드·문서·분석 스크립트 | ✅ | — |
| `catkin_make` + bag + 분석 | — | ✅ **필수** |
| 제출용 수치 | — | bag cumulative |

---

## 9. 관련 파일

| 파일 | 용도 |
|------|------|
| `include/cartographer_parallel/pa02_timing.h` | chrono 로그 |
| `scripts/pa02_bag_profile.sh` | bag + clean/summary |
| `scripts/pa02_deploy_profile.sh` | PC→Jetson |
| `scripts/pa02_analyze_profile.py` | §1.9 병목 분해 + 3함수 선정 (`*_bottleneck.txt`) |
| `benchmark/pa02_microbench.cpp` | 격리 벤치 |
| `data/pa02/pa02_l0_profile_*` | L0 baseline |
| `docs/PA01_ADDITIONAL_EXPERIMENTS_20260531.md` | threshold·microbench≠bag |

---

## 10. L0 baseline 스냅샷 (2026-05-31)

`pa02_l0_profile_summary.txt` (opt9, GPU threshold=256):

| 태그 | cumulative (ms) | calls |
|------|----------------:|------:|
| match | 90,482 | 2,117 |
| score_all | 33,814 | 107,913 |
| Score | 59,869 | 78,275 |
| Branch | 54,743 | 78,275 |
| MakeLowCands | 6,365 | 2,117 |
| make_cand | 1,216 | 31,755 |

**분해 (§1.3~1.6, 동일 run clean log):** matcher_scope **56,667 ms**; Score_orchestration **25,955 ms**; Branch depth=3 elapsed 합 **34,679 ms**.
