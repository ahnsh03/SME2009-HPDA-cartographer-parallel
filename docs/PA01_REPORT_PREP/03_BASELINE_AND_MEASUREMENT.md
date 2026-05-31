# 03. 베이스라인 확보 및 측정 방법

## 1. 베이스라인의 의미

- **코드:** `PA01_OPT_LEVEL=0` — 원본과 동일한 이중 루프·나눗셈
- **로그 태그:** `opt=baseline level=0`
- **역할:** CPU/GPU 모든 speedup의 **기준점**

---

## 2. 측정 코드 요약 (`score_all` 내부)

| 항목 | 구현 |
|------|------|
| 타이머 | `std::chrono::steady_clock` |
| 구간 | 이중 for **직전 ~ 직후** (점수 계산과 동일 경로) |
| 출력 | `std::cerr` + `flush` |
| 누적 | `static call_count`, `cumulative_us` → **avg ms/call** |
| GPU | level 7에서 H2D/D2H·`cudaStreamSynchronize` 포함 여부를 보고서에 명시 |

**주의:** `cumulative`는 **score_all 함수 CPU 시간 합**이지, bag wall time(~96.4s)이나 SLAM 전체 시간이 아님.

### 로그 한 줄 예시 (필드 설명)

```
[score_all] opt=baseline level=0 | call=27754 | elapsed=0.197 ms (197 us) | n=4 p=1081 | map=467x314 | work_units(n*p)=4324 | us_per_candidate=49.250 | cumulative=86824.499 ms / 27754 calls (avg=3.128 ms/call)
```

| 필드 | 예 | 의미 |
|------|-----|------|
| `call` | 27754 | bag 1회 동안 호출 누적 |
| `n`, `p` | 4, 1081 | 후보 수, 스캔 점 수 |
| `elapsed` | 0.197 ms | **이번** 호출만 |
| `cumulative` | 86824 ms | 지금까지 합 |
| `avg` | 3.128 ms/call | cumulative/call |

---

## 3. 베이스라인 확보 과정 (요약)

1. `score_all.cpp`에 chrono·로그 추가 후 `catkin_make`
2. `roslaunch ... ns:=student_19` → 처음에는 **`[score_all]` 없음** (환경 문제)
3. `ROS_IP=192.168.0.104` 설정 후 재실행 → **성공**
4. `tee` + `grep -oE`로 `pa01_baseline_*` 저장

상세 트러블슈팅: `04_TROUBLESHOOTING.md` § ROS_IP, § 로그 없음.

---

## 4. Jetson 베이스라인 실측 결과

### summary 마지막 줄 (`data/pa01_baseline_summary.txt`)

```
[score_all] opt=baseline level=0 | call=27754 | elapsed=0.197 ms (197 us) | n=4 p=1081 | map=467x314 | work_units(n*p)=4324 | us_per_candidate=49.250 | cumulative=86824.499 ms / 27754 calls (avg=3.128 ms/call)
```

| 지표 | 값 |
|------|-----|
| 총 호출 | **27,754** |
| 누적 시간 | **86,824 ms (~86.8 s)** |
| 평균 | **3.128 ms/call** |
| 지도 | 467×314 |
| 대표 1회 | n=4, p=1081, elapsed≈0.2 ms |

### 초기 성공 로그 (다른 run, scrap 기록)

```
[score_all] call=27800 | cumulative=87034.014 ms / 27800 calls (avg=3.131 ms/call)
Done. (bag 96.4초 재생 완료)
```

→ 호출 수는 run마다 **수십~수만 차이** 날 수 있음. **동일 조건 3회 평균** 권장하나, 이후 CPU/GPU는 **cumulative·구간별** 비교가 더 안정적.

### 이상치 패턴 (병목 설명용)

| 패턴 | 의미 |
|------|------|
| n=4, elapsed≈0.17~0.2 ms | 대부분 호출 |
| n=256, elapsed≈7~11 ms | branch 단계 등 후보 급증 |
| work_units 4324 vs 276736 | n×p 비례 |

---

## 5. 로그 수집 명령 (정본)

### tee + grep (권장)

```bash
cd /root/catkin_ws && source devel/setup.bash && source ~/.bashrc

roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
  2>&1 | tee ~/pa01_baseline_run.log

grep -oE '\[score_all\].*' ~/pa01_baseline_run.log > ~/pa01_baseline_score_all_clean.log
grep -oE '\[score_all\].*' ~/pa01_baseline_run.log | tail -1 > ~/pa01_baseline_summary.txt
```

### 대안

```bash
# 마지막 줄만
grep -oE '\[score_all\].*' ~/pa01_baseline_run.log | tail -1 > ~/pa01_baseline_summary.txt

# script 세션 전체
script ~/pa01_session.log
# roslaunch ...
exit
grep score_all ~/pa01_session.log
```

### ROS 로그에서 찾기 (보통 실패)

```bash
ls -lt /root/.ros/log/ | head -5
grep -r score_all /root/.ros/log/<run-id>/
```

`[score_all]`은 **stderr → tee** 가 정본. `~/.ros/log/latest` 는 Melodic Docker에 없을 수 있음.

### grep 패턴 (검증됨)

```bash
grep -oE '\[score_all\][^[:cntrl:]]*' ~/pa01_baseline_run.log
grep -oE '\[score_all\].*' ~/pa01_baseline_run.log
```

---

## 6. 진단 명령 (베이스라인 전 “로그가 안 나올 때”)

```bash
strings devel/lib/libassignment_cpu_lib.so | grep timing
ldd devel/lib/cartographer_parallel/fast_correlative_node | grep assignment
nm -D devel/lib/libfast_matcher_lib.so | grep score_all   # U score_all
rostopic hz /student_19/scan
rosnode info /student_19/fast_correlative_node
rostopic echo /student_19/scan -n1

echo $ROS_MASTER_URI
echo $ROS_IP
```

**중간 진단:** `LOADED`만 있고 `make_cand`/`score_all` 본문 미호출 → **앞단 파이프라인** 또는 **ROS_IP** 문제.

---

## 7. 보고서에 쓸 베이스라인 문장 (템플릿)

> Jetson Nano (`student_19`, Docker)에서 동일 bag(`cartographer_parallel_with_bag.launch`, `ns:=student_19`)을 재생한 결과, 원본 `score_all`(level 0)은 **27,754회 호출**, 함수 내부 누적 시간 **86.8 s**, 호출당 평균 **3.13 ms**였다. 측정은 `std::chrono::steady_clock`으로 이중 루프 구간만 포함하였다.

---

## 8. 다음 단계

- CPU: `05_CPU_OPTIMIZATION.md` — level 1~6, 동일 로그 형식
- 비교: `07_COMPARISON_FOR_REPORT.md` — baseline vs opt6 vs opt7
