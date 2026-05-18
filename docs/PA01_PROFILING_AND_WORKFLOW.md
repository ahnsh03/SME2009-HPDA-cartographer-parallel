# PA01: bag 측정 vs 단독 최적화 · 프로파일링 · 메모리/연산

## 1. `score_all.cpp` + bag vs 단독 cpp 벤치

| | **ROS bag 경로 (현재)** | **단독 cpp / micro-benchmark** |
|--|-------------------------|--------------------------------|
| 측정 대상 | `score_all` + `fast_matcher` + ROS + rosbag I/O | `score_all` 함수만 |
| 장점 | 과제·실기와 동일, 최종 SLAM 파이프라인 반영 | 병목 함수만 격리, 반복 수천 회로 분산 축소 |
| 단점 | 호출 횟수·`n` 분포가 run마다 변동, 노이즈 큼 | 실제와 입력 분포가 다르면 과대/과소 추정 |
| 권장 | **제출·최종 수치** | **알고리즘 튜닝·프로파일링** |

**실무 흐름:** micro-bench로 interchange/OpenMP 후보를 고른 뒤 → bag 1회로 `cumulative`/`avg` 검증.

---

## 2. 프로파일링으로 병목 찾기

### A. 현재 방식 (`[score_all]` 로그)

- `elapsed`, `n`, `p`, `path`, `openmp` — **함수 전체** 시간만.
- `n=256` vs `n=4` grep 합산으로 **어느 케이스가 지배적인지**는 가능 (PA01_CPU_VERIFICATION.md).

### B. Jetson / Linux (권장)

```bash
# 1) perf (CPU 샘플링)
sudo perf record -g -p $(pgrep -f fast_correlative_node) -- sleep 30
sudo perf report

# 2) callgrind (함수별, Valgrind 계열 — 느림)
valgrind --tool=callgrind --callgrind-out-file=score_all.cg \
  ./your_microbench  # 단독 벤치에 적합

# 3) gprof (컴파일 시 -pg)
# CMake: -DCMAKE_CXX_FLAGS="-pg -O2"
```

### C. micro-benchmark 스켈레톤 (선택)

`fast_matcher` 없이 고정 `grid, px, py, cx, cy`로 `score_all`만 1000회 호출 → opt2 vs opt6 직접 비교.

### D. 이번 PA01에서 이미 알려진 병목

1. **`grid[y*w+x]` 랜덤 읽기** (캐시 미스)
2. **`n=256` 호출**이 전체 시간 ~95%
3. opt6 v1: **`openmp=0`** → OpenMP 경로 미사용, opt2 대비 불리한 구현 요소(힙 할당 등)

---

## 3. 연산 최적화 vs 메모리 최적화

| 구분 | 연산(속도) | 메모리 |
|------|------------|--------|
| 목표 | 사이클·명령 수 감소 | 할당/대역폭/캐시 미스 감소 |
| 예 (본 과제) | LICM `inv_denom`, 루프 교환 | 스택 `sums[256]` (heap 제거) |
| 예 | OpenMP 후보 병렬 | 행 포인터 `row[x]` (인덱스 재계산 감소) |
| 예 (미채택) | prefetch | grid 전체 복사 (map 작아서 불필요) |
| GPU 단계 | 커널 병렬 | shared memory 타일링 |

**적용 원칙:** map 467×314는 L2에 들어갈 만큼 작음 → **연산·접근 순서**가 우선. `n×p`가 크면 **호출마다 `vector` 할당** 같은 숨은 메모리 비용이 속도를 깎음 → opt6 v2에서 스택 버퍼.

---

## 4. opt6 Jetson 재측정 체크

```bash
sudo apt-get install -y libomp-dev   # OpenMP
cd ~/catkin_ws && catkin_make -DPA01_OPT_LEVEL=6
strings devel/lib/libassignment_cpu_lib.so | grep -E 'GOMP|omp'
source devel/setup.bash
# LOADED 줄에 openmp=1, n=256 줄에 path=omp_cand 기대
```

`openmp=0`이면 v2의 interchange만 동작 → opt2와 유사 이상이어야 함.
