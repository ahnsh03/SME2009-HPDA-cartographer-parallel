# PA01 `score_all` CPU 최적화 전략 (Jetson Nano)

## 베이스라인 (확정)

| 항목 | 값 |
|------|-----|
| `avg ms/call` | **3.131** |
| `calls` | 27,900 (bag 1회) |
| `n` (대표) | 4 |
| `p` (스캔 포인트) | ~1081 |
| 지도 | 467×314 |

로그 태그에 `invoke=`가 있으면 **이전 진단용 빌드**입니다. 현재 로컬 `score_all.cpp`는 `opt=` / `level=`만 출력합니다.

---

## 알고리즘 구조

```text
for each candidate i (n):
  sum = 0
  for each scan point j (p):
    if in_map(x,y): sum += grid[y*w + x]
  score[i] = sum / (255 * p)
```

- 연산량: **O(n × p)** (이론상); 실제로는 in-bounds 비율에 따라 감소
- 병목: **grid 랜덤 접근** (후보마다 x,y 변경), **px/py 반복 읽기** (후보 루프마다)

---

## 강의 개념 매핑 (1~7주차 + 9~11주차)

| 순서 | 기법 | 강의 근거 | `score_all` 적용 |
|------|------|----------|----------------|
| 1 | **LICM** — `1/(255·p)` 루프 밖 | W1 루프 불변, 나눗셈→곱셈 | `PA01_OPT_LEVEL=1` |
| 2 | **루프 교환** — j(스캔) 바깥, i(후보) 안쪽 | 캐시: px/py 재사용 | `PA01_OPT_LEVEL=2` |
| 3 | **프리페치** | 메모리 계층, latency hiding | `PA01_OPT_LEVEL=3` |
| 4 | **분기 최소화** — in/out 분리 | SIMT 워프 다이버전스 (9주차) | `PA01_OPT_LEVEL=4` |
| 5 | **전체 조합** | 위 1~4 통합 | `PA01_OPT_LEVEL=5` |
| 6 | **최종 (opt6_best)** | interchange + OpenMP(n≥64) | `PA01_OPT_LEVEL=6` |
| (다음) | CUDA | GPU assignment | PA02 |

**의도적으로 하지 않는 것 (이번 PA01 CPU 단계)**

- `restrict`만으로는 이득 제한적 → 포인터 const화는 opt2에 포함
- OpenMP — Jetson 단일 코어 실습에서는 효과 불확실
- Shared memory — 후보/스캔이 호출마다 다름; CUDA에서 타일링 (9주차 shared memory)

---

## Jetson 빌드 방법

`score_all.cpp`만 교체한 뒤:

```bash
cd /root/catkin_ws
catkin_make
source devel/setup.bash
```

**중요:** `fast_matcher`를 다시 빌드해야 `libassignment_cpu_lib.so`가 갱신됩니다. `cartographer_parallel` 패키지 전체가 링크되므로 `catkin_make` 한 번이면 됩니다.

### 단계별 컴파일 (CMake 권장)

```bash
cd /root/catkin_ws
catkin_make -DPA01_OPT_LEVEL=0   # baseline
catkin_make -DPA01_OPT_LEVEL=1   # LICM
# ... 2, 3, 4, 5
source devel/setup.bash
```

소스 상단 `#ifndef PA01_OPT_LEVEL`은 CMake가 넘긴 값을 우선합니다.

---

## 측정 절차 (매 단계 동일)

```bash
source ~/.bashrc
export ROS_IP=192.168.0.104
cd /root/catkin_ws && source devel/setup.bash

RUN=baseline   # opt0, opt1, ... opt5 로 변경

roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
  2>&1 | tee ~/pa01_${RUN}_run.log

grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log | tail -1 > ~/pa01_${RUN}_summary.txt
```

PC로 가져오기:

```bash
cd ~/SME2009_HPDA/PA01/data
ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_${RUN}_summary.txt" > pa01_${RUN}_summary.txt
```

### 기록 표 (보고서용)

| level | opt 태그 | avg ms/call | cumulative ms | 비고 |
|-------|----------|-------------|---------------|------|
| 0 | baseline | 3.131 | 87362 | 기준 |
| 1 | opt1_licm | | | 나눗셈 제거 |
| 2 | opt2_loop_interchange | | | px/py 재사용 |
| 3 | opt3_prefetch | | | |
| 4 | opt4_branchless | | | 분기 분리 |
| 5 | opt5_all_cpu | | | 전체 |

`speedup = baseline_avg / opt_avg`

---

## 결과 해석 가이드

- **opt1:** 0.1~2% 개선도 정상 (나눗셈 1회 절약)
- **opt2:** 수 % 개선 기대 (p가 크면 px/py 트래픽 감소)
- **opt3:** Jetson에서 효과 가변 (프리페치 한계)
- **opt4:** in-bounds 비율 높으면 유의미 (SLAM은 대부분 in-bounds)
- **n=256** 호출 시 elapsed 큼 → branch & cache miss; opt4/5에서도 개선 가능

**주의:** `score` 값은 **비트 동일**해야 함. 분기 분리(opt4)는 out-of-bounds를 0으로 처리하므로 in-bounds만 있는 워크로드와 동일해야 합니다.

---

## 다음 단계 (PA02 / CUDA)

- `score_all` 커널: grid를 `__shared__`에 타일링, candidate당 스레드 1:1
- PCIe 전송 최소화는 이번 CPU 단계와 별개 (호출 횟수 많음, 데이터 작음)

---

## 참고: 로그 필드

```
[score_all] opt=opt1_licm level=1 | call=... | elapsed=... | n=... p=... | cumulative=... | avg=... ms/call
```

`invoke=` 없음 — 현재 코드 기준.