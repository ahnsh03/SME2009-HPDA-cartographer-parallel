# 04. 트러블슈팅 (증상별·시간순)

보고서에는 **핵심 2~3건**만 본문에, 나머지는 부록·한 줄 요약으로 압축 가능.

---

## 요약 표 (보고서 복붙용)

| # | 증상 | 원인 | 해결 |
|---|------|------|------|
| 1 | `[score_all]` 로그 전혀 없음 | `ROS_IP` 미설정 | `export ROS_IP=192.168.0.104` in `~/.bashrc` |
| 2 | 빌드 level=N인데 로그 `level=0` | 예전 run 로그 scp / `source devel/setup.bash` 누락 | 재실행 후 summary에서 `opt=` 확인 |
| 3 | `tee`/`grep` 빈 파일 | stderr만, ROS_IP, 경로 | `2>&1 \| tee`, ROS_IP, `grep -oE` |
| 4 | `[RUNNING]`과 한 줄 합침 | rosbag `\r` | `grep -oE '\[score_all\].*'` |
| 5 | `~/.ros/log`에 score_all 없음 | cerr → ROS log 미기록 | tee 로그가 정본 |
| 6 | `~/.bashrc` 0바이트 | `cat > ~/.bashrc` 실수 | `cp /etc/skel/.bashrc` + ROS 줄 |
| 7 | SSH 매번 비밀번호 | 키 미등록 | `id_ed25519_jetson` + ssh-copy-id |
| 8 | `openmp=0` (level 6) | `libomp-dev` 없이 빌드 | apt install + **재 catkin_make** |
| 9 | CUDA 빌드 `not declared` | 헤더를 namespace 안 include | include를 파일 최상단으로 |
| 10 | `cudaMemcpy py: invalid argument` | `Grow()` cap 공유로 `d_py` nullptr | cap_px/py/cx/cy/score 분리 |
| 11 | `RUN` 비어 `pa01__run.log` | `export RUN` 누락 | launch 전 `export RUN=...` |
| 12 | CMakeCache 경로 없음 | catkin 중첩 경로 | `build/cartographer_parallel/cartographer_parallel/` |

---

## A. 환경·ROS (베이스라인 확보 전)

### A.1 [증상] `roslaunch`만 돌고 `[score_all]` 없음

**관찰**

- `[RUNNING] Bag Time...`만 출력
- `tee` 후 `grep score_all` → 빈 결과
- `grep "Loaded map"` 도 없음 (초기)

**실시한 진단**

```bash
ldd devel/lib/cartographer_parallel/fast_correlative_node | grep assignment
nm -D devel/lib/libfast_matcher_lib.so | grep score_all
rostopic hz /student_19/scan
rosnode info /student_19/fast_correlative_node
```

**중간 결론:** 라이브러리 로드는 되나 `score_all()` 본문 미실행 → **앞단 파이프라인** 또는 **네트워크 설정**.

→ **A.2 ROS_IP** 가 실제 원인.

---

### A.2 [증상] 빌드 OK, 런타임 매칭·score_all 미동작

**확인**

```bash
echo $ROS_MASTER_URI   # http://192.168.0.106:11311  OK
echo $ROS_IP           # (비어 있음)  NG
```

**메커니즘 (보고서용):**

- ROS1 Master(106)에 노드 등록
- Jetson(104)은 **자신의 IP**를 알려야 TCP·콜백 안정
- `rostopic echo`는 되는데 **같은 프로세스 매칭이 안 되는** 경우 재현

**해결**

```bash
export ROS_MASTER_URI=http://192.168.0.106:11311
export ROS_IP=192.168.0.104
source /opt/ros/melodic/setup.bash
source /root/catkin_ws/devel/setup.bash
source ~/.bashrc
```

**결과:** `[score_all] call=...` 연속 출력, 베이스라인 확보.

**보고서 블록 (문제–원인–해결):**

> **문제:** chrono 추가·빌드 후에도 성능 로그 없음.  
> **조사:** 링크·토픽·스캔 정상, `score_all` 미호출.  
> **원인:** `ROS_IP` 미설정으로 분산 ROS에서 매칭 파이프라인 미동작.  
> **해결:** `ROS_IP=192.168.0.104` 후 bag 완주 시 ~27,800 calls, avg ~3.13 ms/call.

---

### A.3 [부수] `~/.bashrc` 비움

```bash
# 증상
ls -la ~/.bashrc   # 0 bytes

# 복구
cp /etc/skel/.bashrc ~/.bashrc
# ROS 4줄 재추가 후 source ~/.bashrc
```

**교훈:** `cat >` = 덮어쓰기. 추가는 `>>` 또는 `nano`.

---

## B. 로그·측정

### B.1 [증상] `grep`이 ROS log에서 실패

```bash
grep score_all ~/.ros/log/latest/*/fast_correlative_node*.log
# No such file or directory
```

| 원인 | 설명 |
|------|------|
| `latest` 없음 | Melodic Docker |
| glob 미매칭 | 경로 불일치 |
| 미기록 | `[score_all]` → **stderr/tee** |

**해결:** `~/pa01_*_run.log` 에서 `grep -oE '\[score_all\]'`.

---

### B.2 [증상] opt1 빌드했는데 로그는 baseline level=0

**Jetson 빌드 확인 (정상 예):**

| 확인 | 결과 |
|------|------|
| `CMakeCache.txt` `PA01_OPT_LEVEL=1` | OK |
| `flags.make` `-DPA01_OPT_LEVEL=1` | OK |
| `strings libassignment_cpu_lib.so` → `opt1_licm` | OK |

**PC 로그 (문제):**

- `pa01_opt1_licm_clean.log` 전부 `opt=baseline level=0`
- summary가 baseline과 동일

**원인 (가능성 순)**

1. opt1 빌드 **전** run 로그를 opt1 이름으로 scp
2. `catkin_make` 후 **`source devel/setup.bash` 누락**
3. `export RUN` 비어 잘못된 파일만 갱신

**해결 절차**

```bash
catkin_make -DPA01_OPT_LEVEL=1
source devel/setup.bash
export RUN=opt1_licm
roslaunch ... 2>&1 | tee ~/pa01_${RUN}_run.log
tail -1 ~/pa01_opt1_licm_summary.txt   # opt=opt1_licm level=1 확인 후 scp
```

**교훈:** CMake/strings ≠ 실행 로그. **summary 한 줄**으로 run 검증 필수.

---

### B.3 [증상] `CMakeCache.txt` 없음

```bash
cd ~/catkin_ws/build/cartographer_parallel
grep PA01_OPT_LEVEL CMakeCache.txt
# No such file
```

**실제 경로:**

```bash
grep PA01_OPT_LEVEL ~/catkin_ws/build/CMakeCache.txt
grep PA01_OPT_LEVEL \
  ~/catkin_ws/build/cartographer_parallel/cartographer_parallel/CMakeCache.txt
grep CXX_DEFINES \
  ~/catkin_ws/build/cartographer_parallel/cartographer_parallel/CMakeFiles/assignment_cpu_lib.dir/flags.make
```

---

## C. CPU (level 6)

### C.1 `openmp=0`, opt6가 opt2보다 느림

**증상:** `LOADED ... openmp=0`, `path=interchange`만, n=256도 OpenMP 미사용.

**원인:** 빌드 시 `libomp-dev` 없음 → `PA01_HAS_OPENMP` 미정의.

**해결**

```bash
apt-get install -y libomp-dev
cd ~/catkin_ws
rm -rf build/cartographer_parallel devel/lib/libassignment_cpu_lib.so  # 필요 시
catkin_make -DPA01_OPT_LEVEL=6
source devel/setup.bash
```

**검증**

```bash
grep PA01_HAS_OPENMP .../flags.make    # -DPA01_HAS_OPENMP=1 -fopenmp
strings devel/lib/libassignment_cpu_lib.so | grep GOMP
# 실행: openmp=1, n=256 → path=omp_cand
```

**결과:** avg 2.187 → **0.611**, cumulative 82,807 → **52,132** ms.

**참고:** level 0~5는 OpenMP 코드 없음 → **libomp 설치 전 데이터도 유효**.

---

### C.2 level 2+ 루프 교환 버그 (로컬 수정)

- 잘못된 버전: 매 `j`마다 `score[i]` 덮어쓰기 → 합산 깨짐
- 수정: `std::vector<int> sums(n)` 누적 후 `* inv_denom`

---

## D. GPU (level 7)

### D.1 빌드: `'score_all_cuda' has not been declared`

```
score_all.cpp:545: error: 'score_all_cuda' has not been declared
```

**원인:** `score_all_cuda.h`를 `namespace cartographer_parallel {` **안**에서 include → 네임스페이스 이중 중첩.

**수정:** `#include "cartographer_parallel/score_all_cuda.h"` 를 **파일 최상단**, namespace **이전**.

---

### D.2 실행: CUDA 미동작 (1차 버그 run)

**증상**

```bash
grep 'path=cuda' ~/pa01_opt7_gpu_run.log | head
# (없음)
grep 'score_all_cuda' ~/pa01_opt7_gpu_run.log | head
# cudaMemcpy py: invalid argument (반복)
```

- `LOADED ... cuda=1` → 빌드는 level 7
- n=256도 `path=interchange` (CPU 폴백)
- `cudaGetDeviceCount` → devices **1** (GPU 살아 있음)

**원인 (`Grow()`):**

```cpp
Grow(&b.d_px, &b.cap_p, need_p);
Grow(&b.d_py, &b.cap_p, need_p);  // cap_p 이미 충분 → d_py 미할당
cudaMemcpy(b.d_py, py, ...);       // invalid argument
```

**수정:** `cap_px`, `cap_py`, `cap_cx`, `cap_cy`, `cap_score` 분리.

**검증 (수정 후)**

```bash
grep -c 'path=cuda' ~/pa01_opt7_gpu_clean.log    # 37866
grep 'score_all_cuda' ~/pa01_opt7_gpu_run.log | head   # 에러 0
```

**보고서:** 1차 실패 run(~43k calls, cuda 0) vs 2차 성공 run(**133,496** calls) **구분** 기재.

---

## E. SSH·기타

### E.1 ssh-copy-id 키 이름 오류

- 잘못: `id_ed25519_jetson.rcv-gateway`
- 올바름: `~/.ssh/id_ed25519_jetson` 또는 `.pub`

### E.2 `cd catkin_ws` 실패

- `/` 루트와 `root` **계정** 혼동
- 컨테이너에서는 `cd ~/catkin_ws` 또는 `/root/catkin_ws`

---

## F. 분석 시 주의 (버그 아님)

### F.1 run마다 `call=` 수 다름

| Level | calls (예) |
|-------|------------|
| 0 | 27754 |
| 2 | 41261 |
| 6 | 85260 |
| 7 | 133496 |

**이유:** `score_all`이 빨라지면 같은 bag에서 **매칭·호출 횟수 증가** 가능.

**대응:** **cumulative(ms)** 와 **n=256 / path=cuda 구간 평균** 병기. avg만으로 GPU/CPU 우열 판단 금지.

### F.2 summary 마지막 줄 `path=n4`

마지막 **한 번** 호출이 n=4였을 뿐, 전 run이 n4만이 아님.
