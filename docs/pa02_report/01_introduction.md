# 제1장. 서론 및 문제 정의

## 1.1 PA01 이후 남은 과제

PA01에서는 `score_all()` 커널 하나에 대해 CPU ablation(L0~L6)과 GPU hybrid(L7~L9)를 구현하였다. bag 기준 `[score_all]` cumulative는 **86,824 ms → 33,814 ms(−61%)** 까지 줄였고, PA01 보고서에서 높은 평가를 받았다.

PA02의 범위는 `fast_matcher.cpp`에 있는 **matcher 오케스트레이션**이다. `score_all`은 이미 최적화되었으므로 PA02에서 건드리지 않고, 그 위에서 반복 호출되는 구조를 개선한다.

```
fast_correlative_node.cpp     ROS: LaserScan → Match() → pose publish
        │
        ▼
FastMatcher::MatchWithWindow()   ← [match] KPI (B&B 전체)
        │
        ├── MakeScans / MakeBounds / MakeGridStack
        ├── MakeLowCands() ──► make_cand()          [make_cand]
        ├── Score()      ──► score_all()            [Score] → [score_all] (PA01)
        └── Branch()     ──► Score() 재귀          [Branch]
```

### 알고리즘 개요

Fast Correlative Scan Matcher는 Cartographer의 **Branch-and-Bound(B&B)** 기반 2D scan matching이다.

1. 초기 pose 주변에서 **yaw 후보**(회전 스캔)를 생성한다 (`MakeScans`).
2. 스캔별 **(x,y) 탐색 창**을 정한다 (`MakeBounds`).
3. 다해상도 occupancy grid 피라미드를 만든다 (`MakeGridStack`).
4. `make_cand`로 후보 pose 격자 `(cx, cy)`를 생성한다.
5. `Score`가 스캔별로 `score_all`을 호출해 correlative score를 계산하고 정렬한다.
6. `Branch`가 점수순 가지치기 후 4-way split으로 재귀 정밀화한다.

PA02는 4~6단계의 **호출 구조·메모리 할당·정렬 오버헤드**를 줄이는 것이 목표이다.

---

## 1.2 PA01 / PA02 역할 분리

| | PA01 | PA02 |
|---|------|------|
| **대상 함수** | `score_all()` | `make_cand`, `Score`, `Branch` |
| **빌드 플래그** | `PA01_OPT_LEVEL=9`, `PA01_GPU_THRESHOLD=256` | `PA02_OPT_LEVEL=0~3` |
| **고정 조건** | — | PA01 L9 hybrid 유지 |
| **KPI** | `[score_all]` cumulative | `[match]` cumulative |
| **회귀 검증** | max_diff vs baseline | `best_score`, `coarse_n`, `[score_all]` ±몇 % |

PA02 최적화 후에도 `[score_all]` cumulative가 L0 대비 **±3% 이내**인지 매 Phase마다 확인하였다. 커널을 건드리지 않았음을 실측으로 보장하기 위함이다.

---

## 1.3 최적화 대상 3함수 — 실측으로 선정

코드 구조만 보고 “어느 루프가 무거워 보인다”고 타깃을 정하지 않았다. Phase 0 bag 프로파일(L0)에서 **태그별 cumulative와 stratum 분해**로 다음을 확정하였다.

| 함수 | SLAM 역할 | L0 measured | match 대비 | Phase |
|------|-----------|-------------|-----------|-------|
| `make_cand` | 후보 격자 (cx,cy) 생성 | 1,216 ms | 1.3% | 1 |
| `Branch` | B&B 재귀·가지치기 (Score 포함) | 54,743 ms | 60.5% | 2 |
| `Score` | score_all 호출 + sort 오케스트레이션 | 59,869 ms | 66.2% | 3 |

**Phase 순서가 impact 순서가 아닌 이유:** `make_cand`는 절대 비중이 작지만 side effect가 없고 microbench로 검증 파이프라인을 먼저 고정하기에 적합하다. Branch·Score는 B&B 내부에서 중첩 타이머로 얽혀 있어, make_cand 검증 후 순차 적용한다.

---

## 1.4 KPI 및 정확도 지표

### 모듈 KPI

```
KPI = bag 1회 동안 [match] 마지막 줄의 cumulative (ms)
```

- 벽시계 시간(약 96 s)이 아니다. ROS launch·bag replay·네트워크 지연을 포함하지 않는 **순수 matcher 연산 누적**이다.
- PA01에서도 동일 원칙(`[score_all]` cumulative)을 사용했으며, PA02에서 `[match]`로 범위를 확장했다.

### 정확도 회귀 검증

| 지표 | 의미 | PA02 기준 |
|------|------|----------|
| `best_score` | B&B가 찾은 최고 correlative score (0~1) | 0.783 유지 |
| `coarse_n` | coarse 단계 후보 수 | 3840 유지 |
| `ok` | `best_score > min_score` | 1 유지 |

이는 전역 SLAM RMSE가 아니라, **동일 bag·동일 map에서 matcher 출력이 baseline과 동일한가**를 보는 sanity check이다. 과제 범위(`fast_correlative_scan_matcher`)에서 적절한 검증이다.

---

## 1.5 설계 방향 요약

1. **Phase 0:** L0 bag 프로파일 → 병목 분해 → 3함수 로드맵 확정
2. **Phase 1~3:** `PA02_OPT_LEVEL` 누적 ablation, 매 단계 bag KPI + regression 확인
3. **PA01 GPU hybrid 유지:** score_all의 coarse(n=256)는 이미 CUDA path (~81%)
4. **upstream 참고:** Cartographer 원본 read-only diff → 이식 실험 → 실측 기각/채택
5. **측정:** microbench는 후보 선별, **bag cumulative가 최종 채택** (제2장 상세)

---

## 1.6 보고서 구성과 PA01의 연속성

PA01 보고서(`docs/PA01_REPORT_DRAFT.md`)에서 확립한 다음 원칙을 PA02에서도 유지한다.

- 병목을 코드 위치(B1~B4)와 연결해 설명
- Level별 ablation 표와 로그 수치 병기
- “avg만으로 속도를 말하지 않는다” — cumulative와 stratum 함께 제시
- 실패·기각 실험도 데이터와 함께 기록

PA02에서 새로 추가되는 것은 **matcher 전체 KPI**, **중첩 타이머 해석**, **upstream 이식 실험**, **microbench vs bag 방법론**이다.
