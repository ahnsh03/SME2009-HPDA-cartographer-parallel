# PA02 프로파일링·최적화 전략

> PA01 워크플로(`docs/PA01_REPORT_PREP/02_ENVIRONMENT_AND_WORKFLOW.md`)를 그대로 따르되,  
> **모듈 KPI** = `[match]` cumulative + `[score_all]` cumulative 분해.

---

## 1. 최적화 순서 (Phase)

| Phase | 목표 | 함수 | 이유 |
|-------|------|------|------|
| **0 (지금)** | baseline 프로파일 | make_cand, Score, Branch, match | bag vs microbench 비중 확인, 임계값 근거 확보 |
| **1** | CPU | `make_cand` | 구현 단순, side effect 적음, OpenMP/reserve 검증 쉬움 |
| **2** | CPU | `Branch` | 가지치기·할당·재귀 — score_all 가속 후 2차 병목 |
| **3** | CPU+GPU | `Score` | grid 1회 H2D, 스캔 batch — **CPU/GPU 분담 임계는 실험으로 결정** |

**score_all**은 PA01 L7(또는 L9+threshold=256)을 **고정**하고 Phase 0~3에서 건드리지 않음.

---

## 2. 로그 태그 (PA01과 동일 패턴)

| 태그 | 위치 | cumulative 의미 |
|------|------|-------------------|
| `[make_cand]` | `score_all.cpp` | 후보 격자 생성 1회 |
| `[MakeLowCands]` | `fast_matcher.cpp` | 스캔별 make_cand + Cand 조립 |
| `[Score]` | `fast_matcher.cpp` | 스캔별 score_all + sort **전체** |
| `[Branch]` | `fast_matcher.cpp` | 재귀 B&B 1회 (내부 Score 포함) |
| `[match]` | `fast_matcher.cpp` | `MatchWithWindow` 1회 (전체) |
| `[score_all]` | PA01 | 기존과 동일 |

### Phase 0 분석 공식

```
Score 오케스트레이션 ≈ [Score] cumulative − (해당 Score 호출들의 score_all elapsed 합)
Branch 순수 오버헤드 ≈ [Branch] cumulative − (Branch 내부 Score 중 score_all 제외)  ← depth별 grep 필요
모듈 score_all 외 ≈ [match] cumulative − Σ score_all (같은 bag run)
```

**주의 (PA01 교훈):** micro-bench crossover ≠ bag hot path.  
Score GPU batch 임계·OpenMP 임계는 **bag sweep + microbench `--baglike`** 둘 다 필요.

---

## 3. 빌드·실행

### CMake (Jetson Docker)

```bash
cd /root/catkin_ws
catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON \
  -DPA02_PROFILE=ON -DPA02_OPT_LEVEL=0
source devel/setup.bash
export ROS_IP=192.168.0.104
/root/pa02_bag_profile.sh pa02_l0_profile
```

### PC에서 일괄 deploy (SSH 설정된 경우)

```bash
chmod +x scripts/pa02_deploy_profile.sh scripts/pa02_bag_profile.sh
./scripts/pa02_deploy_profile.sh pa02_l0_profile
# → data/pa02/pa02_l0_profile_summary.txt
```

### micro-bench (ROS 없음)

```bash
cd benchmark
make pa02_microbench YAML=../cartographer_parallel/cartographer_parallel/maps/0501.yaml
make pa02_sweep
```

---

## 4. 로그 수집 (PA01과 동일)

```bash
grep -oE '\[(make_cand|Score|Branch|match|score_all)\][^[:cntrl:]]*' run.log > all_clean.log
tail -1 run.log  # 각 tag별 summary는 pa02_bag_profile.sh가 생성
```

---

## 5. CPU/GPU 분담 실험 계획 (Phase 3 전)

| 실험 | microbench | bag | 목적 |
|------|------------|-----|------|
| A | `make_cand --sweep` | make_cand cumulative / match 수 | OpenMP threshold (span_x×span_y) |
| B | `match --baglike` | `[Score]` vs `[score_all]` 비율 | batch vs per-scan GPU |
| C | — | Branch depth별 call 수 | sibling OpenMP threshold (cand_in) |

임계 후보는 **데이터로 확정** 후 `PA02_*_THRESHOLD` CMake 캐시로 노출 (PA01 `PA01_GPU_THRESHOLD`와 동일 패턴).

---

## 6. PA02_OPT_LEVEL 로드맵 (예정)

| Level | 내용 |
|-------|------|
| 0 | 프로파일만 (최적화 없음) |
| 1 | make_cand CPU (reserve + OpenMP) |
| 2 | Branch CPU (prune + reserve + sibling OMP) |
| 3 | Score pipeline (grid cache + optional batch GPU) |

---

## 7. 역할 분담: 사용자 vs 에이전트

| 작업 | 에이전트 (로컬 repo) | 사용자 (Jetson) |
|------|---------------------|-----------------|
| `fast_matcher.cpp`, `make_cand`, `pa02_timing.h` 수정 | ✅ | — |
| CMake, 스크립트, microbench, docs | ✅ | — |
| `catkin_make` + bag 실행 | — (SSH 가능 시 deploy 스크립트) | ✅ **필수** |
| `ROS_IP`, Docker, load average 확인 | — | ✅ |
| 로그 PC 수집 | deploy 스크립트 또는 ssh cat | ✅ |
| Phase 1~3 최적화 구현 | ✅ (데이터 확인 후) | Jetson 재측정 |

**과제 제출 데이터는 반드시 Jetson Nano bag 결과.**  
에이전트는 코드·스크립트까지 준비하고, **첫 baseline run은 사용자 Jetson에서 1회** 필요.

---

## 8. 관련 파일

| 파일 | 용도 |
|------|------|
| `include/cartographer_parallel/pa02_timing.h` | chrono 로그 |
| `scripts/pa02_bag_profile.sh` | bag + tag별 clean/summary |
| `scripts/pa02_deploy_profile.sh` | PC→Jetson deploy+run+fetch |
| `benchmark/pa02_microbench.cpp` | make_cand / match 격리 벤치 |
| `data/pa02/` | PC 수집 데이터 |
