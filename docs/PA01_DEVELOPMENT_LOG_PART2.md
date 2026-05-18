# PA01 개발·트러블슈팅 기록 (Part 2)

> 이전 대화에서 정리한 내용(환경 설정, ROS_IP, 베이스라인 확보, `score_all` 병목 분석) **이후**의 작업입니다.  
> 보고서에 앞 문서 뒤에 이어붙이면 됩니다.

---

## 1. CPU 단계별 최적화 설계 (로컬 PC)

### 작업 내용

- 강의 정리(1~7주차: LICM·캐시 지역성, 9~11주차: 분기/메모리)를 반영해 `score_all.cpp`를 **한 파일 + `PA01_OPT_LEVEL` 0~5** 로 분기.
- `CMakeLists.txt`에 `target_compile_definitions(... PA01_OPT_LEVEL=...)` 추가.
- Jetson에서는 **코드 복붙 없이** `catkin_make -DPA01_OPT_LEVEL=N` 만 바꿔 측정.

| Level | 태그 | 기법 |
|-------|------|------|
| 0 | `baseline` | 원본 이중 루프 |
| 1 | `opt1_licm` | `1/(255·p)` 루프 밖 (LICM) |
| 2 | `opt2_loop_interchange` | j(스캔) 바깥 / i(후보) 안쪽 + `sums[]` 누적 |
| 3 | `opt3_prefetch` | opt2 + `__builtin_prefetch` |
| 4 | `opt4_branchless` | in-bounds 마스크 |
| 5 | `opt5_all_cpu` | 2+3+4 통합 |

상세 계획: `docs/CPU_OPTIMIZATION_PLAN.md`

### 초기 구현 버그 (로컬에서 수정)

- **루프 교환(level 2+)** 첫 버전: 후보마다 `score[i]`를 덮어써 **합산이 깨짐** → `std::vector<int> sums(n)` 누적 후 마지막에 `* inv_denom` 으로 수정.

---

## 2. Jetson 측정 워크플로 (확정)

### 매 단계 공통

```bash
cd ~/catkin_ws
catkin_make -DPA01_OPT_LEVEL=N    # N = 0 .. 5
source devel/setup.bash

export RUN=opt1_licm              # 단계별 이름 (baseline, opt2_loop_interchange, ...)
export ROS_IP=192.168.0.104       # student_19

roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
  2>&1 | tee ~/pa01_${RUN}_run.log

grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log | tail -1 > ~/pa01_${RUN}_summary.txt
grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log > ~/pa01_${RUN}_clean.log
```

### PC로 가져오기

```bash
cd ~/SME2009_HPDA/PA01/data
ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_${RUN}_summary.txt" \
  > pa01_${RUN}_summary.txt
ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_${RUN}_clean.log" \
  > pa01_${RUN}_clean.log
```

### 로그만으로 단계 비교할 때

- **필수:** `pa01_*_summary.txt` 마지막 줄의 `avg=... ms/call`, `opt=... level=N`
- **선택:** `clean.log` 전체(수만 줄) — 분포 확인용, speedup 표에는 summary만으로 충분

---

## 3. 트러블슈팅 (Part 2)

### 3.1 `[score_all]` 로그가 안 보이거나 `tee`/`grep` 결과가 비어 있음

| 증상 | 원인 | 해결 |
|------|------|------|
| 터미널에 `[score_all]` 없음 | `ROS_IP` 미설정, 다른 노드/마스터로 실행 | `export ROS_IP=192.168.0.104` (`~/.bashrc`에 고정) |
| `tee` 파일에 로그 없음 | stderr만 출력, 경로 오류 | `2>&1 \| tee ...` 사용 |
| `[RUNNING]`과 한 줄로 합쳐짐 | rosbag `\r` 덮어쓰기 | `grep -oE '\[score_all\].*'` 로 줄 단위 추출 |

---

### 3.2 `PA01_OPT_LEVEL=1` 빌드했는데 로그는 `opt=baseline level=0`

**증상 (PC에서 확인):**

- Jetson: `CMakeCache.txt` → `PA01_OPT_LEVEL:STRING=1` ✅  
- Jetson: `flags.make` → `-DPA01_OPT_LEVEL=1` ✅  
- Jetson: `strings libassignment_cpu_lib.so` → `opt1_licm` ✅  
- 그런데 `pa01_opt1_licm_summary.txt` / `clean.log` → 전부 `opt=baseline level=0`

**원인:** 빌드는 level 1인데, **가져온 로그는 예전 level 0 run** 이거나, **`source devel/setup.bash` 없이** roslaunch로 예전 `.so` 사용.

**해결:**

```bash
catkin_make -DPA01_OPT_LEVEL=1
source devel/setup.bash   # 필수

export RUN=opt1_licm
roslaunch ... 2>&1 | tee ~/pa01_${RUN}_run.log

# PC로 보내기 전 Jetson에서 확인
tail -1 ~/pa01_opt1_licm_summary.txt
# 기대: opt=opt1_licm level=1
```

**교훈:** 빌드 확인(CMake/strings)과 **실행 로그의 `opt=` / `level=`** 는 별개. summary 한 줄로 run 검증 필수.

---

### 3.3 `CMakeCache.txt`를 `build/cartographer_parallel/` 에서 못 찾음

**증상:**

```bash
cd ~/catkin_ws/build/cartographer_parallel
grep PA01_OPT_LEVEL CMakeCache.txt
# grep: No such file or directory
```

**원인:** catkin이 패키지마다 **한 단계 더 중첩** 빌드 (`build/cartographer_parallel/cartographer_parallel/`).

**해결:**

```bash
# 워크스페이스 루트 캐시 (일부 환경)
grep PA01_OPT_LEVEL ~/catkin_ws/build/CMakeCache.txt

# 패키지 빌드 디렉터리
grep PA01_OPT_LEVEL \
  ~/catkin_ws/build/cartographer_parallel/cartographer_parallel/CMakeCache.txt

# 컴파일 플래그 (가장 확실)
grep PA01_OPT_LEVEL \
  ~/catkin_ws/build/cartographer_parallel/cartographer_parallel/CMakeFiles/assignment_cpu_lib.dir/flags.make
```

---

### 3.4 `roslaunch` 시 `RUN` 변수 비어 있음

**증상:** `tee ~/pa01_${RUN}_run.log` → `pa01__run.log` 같은 이름.

**해결:** launch 전에 반드시 `export RUN=opt1_licm` (등) 설정.

---

### 3.5 단계별 `call=` 횟수가 run마다 다름

**관측 (summary 기준):**

| Level | call | avg ms/call | cumulative ms |
|-------|------|-------------|-----------------|
| 0 baseline | 27754 | 3.128 | 86824 |
| 1 opt1_licm | 27873 | 3.120 | 86974 |
| 2 opt2 | 41261 | 1.999 | 82476 |
| 3 opt3 | 40998 | 2.013 | 82511 |
| 4 opt4 | 31631 | 2.709 | 85703 |
| 5 opt5 | 31672 | 2.701 | 85542 |

- bag 길이(96.4s)는 동일해도 **호출당 로그를 전부 grep** 하면 `call` 수가 달라질 수 있음 (이전 run 잔여, grep 범위, 노드 재시작 등).
- **단계 간 비교:** 같은 bag 1회 완주 + summary의 **`opt=` / `level=` 일치** 확인 후 **`avg ms/call`** 및 **`cumulative`** 로 비교.
- opt2에서 **avg가 크게 감소**(3.12 → 2.00) — 루프 교환 효과로 해석 가능. opt3~5는 Jetson/호출 수 차이로 avg만 보면 opt2보다 느려 보일 수 있어, 분석 시 **cumulative / call** 함께 기록 권장.

---

## 4. 최종 측정 결과 스냅샷 (summary, PC `data/`)

모든 summary에서 **`opt=` / `level=` 이 빌드 단계와 일치**함을 확인 완료.

```
level=0  avg=3.128 ms/call  (baseline)
level=1  avg=3.120 ms/call  (opt1_licm, ~0.3%)
level=2  avg=1.999 ms/call  (opt2_loop_interchange, ~36% vs baseline avg)
level=3  avg=2.013 ms/call
level=4  avg=2.709 ms/call
level=5  avg=2.701 ms/call
```

※ 정밀 speedup 표는 `data/pa01_*_summary.txt` 기준으로 별도 분석 예정.

---

## 5. 수정·건드린 파일 (Part 2)

| 파일 | 변경 |
|------|------|
| `cartographer_parallel/.../src/score_all.cpp` | PA01_OPT_LEVEL 0~5, `opt=` 로그 |
| `cartographer_parallel/.../CMakeLists.txt` | `PA01_OPT_LEVEL` cache + compile definition |
| `docs/CPU_OPTIMIZATION_PLAN.md` | 단계별 측정 가이드 |
| `docs/PA01_DEVELOPMENT_LOG_PART2.md` | 본 문서 |

**건드리지 않음 (과제 범위):** `fast_matcher.cpp` 로직, launch ns/topic (student_19 확인 후 유지).

---

## 6. 이후 작업 (GitHub · PA02)

- 코드: [SME2009-HPDA-cartographer-parallel](https://github.com/ahnsh03/SME2009-HPDA-cartographer-parallel.git)
- PA01: `data/` summary 기반 speedup 분석
- PA02: CUDA `score_all` / assignment (동일 저장소에서 브랜치 또는 디렉터리로 관리 예정)

---

## 7. 빠른 참조 — 잘 되는 명령어 체크리스트

```bash
# 1) 빌드
cd ~/catkin_ws && catkin_make -DPA01_OPT_LEVEL=N && source devel/setup.bash

# 2) 빌드 검증
grep PA01_OPT_LEVEL ~/catkin_ws/build/CMakeCache.txt
grep CXX_DEFINES .../assignment_cpu_lib.dir/flags.make
strings ~/catkin_ws/devel/lib/libassignment_cpu_lib.so | grep -E 'opt[0-9]|baseline'

# 3) 실행 검증 (로그)
export RUN=... ROS_IP=192.168.0.104
roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" 2>&1 | tee ~/pa01_${RUN}_run.log
grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log | tail -1

# 4) PC 수집
ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_${RUN}_summary.txt" > pa01_${RUN}_summary.txt
```
