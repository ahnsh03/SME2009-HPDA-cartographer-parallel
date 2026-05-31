# 02. 환경 구성 및 실험 워크플로

## 1. 네트워크·계정

| 항목 | 값 |
|------|-----|
| Gateway | `rcv@112.171.196.32:22` |
| Jetson | `student_19@192.168.0.104` |
| ROS Master | `http://192.168.0.106:11311` |
| Docker 컨테이너 | `student_19` |
| catkin_ws (컨테이너) | `/root/catkin_ws` |

### SSH ProxyJump (`~/.ssh/config`)

```
Host rcv-gateway
    HostName 112.171.196.32
    User rcv
    Port 22
    IdentityFile ~/.ssh/id_ed25519_jetson

Host jetson-nano-19
    HostName 192.168.0.104
    User student_19
    ProxyJump rcv-gateway
    IdentityFile ~/.ssh/id_ed25519_jetson
```

**연결 확인:**

```bash
ssh rcv-gateway "echo gateway OK"
ssh jetson-nano-19 "echo jetson OK"
```

### SSH 키 등록 (최초 1회)

```bash
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_jetson -C "pa01-jetson"
ssh-copy-id -i ~/.ssh/id_ed25519_jetson.pub rcv@112.171.196.32
ssh-copy-id -i ~/.ssh/id_ed25519_jetson jetson-nano-19
```

**주의:** `-i` 뒤는 `id_ed25519_jetson` 또는 `.pub`만. `id_ed25519_jetson.rcv-gateway` 같은 이름은 **없음**.

---

## 2. 접속·Docker 진입

### Phase 1: Jetson 호스트

```bash
ssh jetson-nano-19
# 또는: ssh -p 22 rcv@112.171.196.32 → ssh student_19@192.168.0.104
```

### Phase 2: 컨테이너

```bash
docker start student_19
docker exec -it student_19 /bin/bash
```

---

## 3. Docker `~/.bashrc` (필수)

`cat > ~/.bashrc` 실수로 비운 경우:

```bash
cp /etc/skel/.bashrc ~/.bashrc
```

맨 아래 추가:

```bash
export ROS_MASTER_URI=http://192.168.0.106:11311
export ROS_IP=192.168.0.104
source /opt/ros/melodic/setup.bash
source /root/catkin_ws/devel/setup.bash
```

```bash
source ~/.bashrc
echo $ROS_MASTER_URI   # http://192.168.0.106:11311
echo $ROS_IP           # 192.168.0.104
```

**104 vs 106:** Master는 106, 실행 노드는 104 — **설계상 정상**. `ROS_IP` 없으면 `score_all`이 호출되지 않는 사례 다수 (→ `04_TROUBLESHOOTING.md`).

---

## 4. 표준 실험 파이프라인 (매 RUN 반복)

### Step 0 — 접속

```bash
# 노트북
ssh jetson-nano-19
docker exec -it student_19 bash
```

### Step 1 — 환경·부하 기록 (측정 전)

```bash
source ~/.bashrc
{
  date
  echo "ROS_MASTER_URI=$ROS_MASTER_URI"
  echo "ROS_IP=$ROS_IP"
  uptime
  free -h | head -2
} | tee ~/pa01_${RUN}_env.txt
```

**권장:** `uptime` load average **2.0 미만**일 때 측정.

### Step 2 — 빌드

```bash
cd /root/catkin_ws
catkin_make -DPA01_OPT_LEVEL=N    # CPU: 0~6, GPU: 7
# GPU 추가:
# catkin_make -DPA01_OPT_LEVEL=7 -DPA01_USE_GPU=ON

source devel/setup.bash   # ★ 필수 — 안 하면 예전 .so 사용
```

**빌드 검증 (선택):**

```bash
grep PA01_OPT_LEVEL /root/catkin_ws/build/CMakeCache.txt
grep PA01_OPT_LEVEL \
  /root/catkin_ws/build/cartographer_parallel/cartographer_parallel/CMakeFiles/assignment_cpu_lib.dir/flags.make
strings /root/catkin_ws/devel/lib/libassignment_cpu_lib.so | grep -E 'opt[0-9]|baseline|GOMP|ScoreCandidates'
```

### Step 3 — 실행 + 전체 로그

```bash
export RUN=baseline          # 예: opt2_loop_interchange, opt6_best, opt7_gpu
export ROS_IP=192.168.0.104  # student_19

roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
  2>&1 | tee ~/pa01_${RUN}_run.log
```

- bag **Done** 까지 대기 (`Ctrl+C`로 중단하지 않음)
- 터미널에 `[score_all]` + `[RUNNING] Bag Time...` 혼재 → 정상

### Step 4 — 로그 정리 (컨테이너)

`[RUNNING]`과 `\r` 때문에 한 줄로 합쳐짐 → **`grep -oE` 필수**:

```bash
grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log > ~/pa01_${RUN}_clean.log
grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log | tail -1 > ~/pa01_${RUN}_summary.txt

# PC로 보내기 전 검증
head -1 ~/pa01_${RUN}_clean.log
tail -1 ~/pa01_${RUN}_summary.txt
# 기대: opt=... level=N 일치
```

### Step 5 — PC `PA01/data/`로 수집

```bash
mkdir -p ~/SME2009_HPDA/PA01/data
cd ~/SME2009_HPDA/PA01/data

ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_${RUN}_summary.txt" \
  > pa01_${RUN}_summary.txt
ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_${RUN}_clean.log" \
  > pa01_${RUN}_clean.log
ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_${RUN}_env.txt" \
  > pa01_${RUN}_env.txt
```

### Step 6 — 클립보드 (선택)

```bash
ssh jetson-nano-19 "docker exec student_19 grep -oE '\[score_all\].*' /root/pa01_${RUN}_run.log | tail -1" \
  | xclip -selection clipboard
```

---

## 5. 코드 반영 방법

### 방법 A: `vi`

```bash
cd /root/catkin_ws/src/cartographer_parallel/cartographer_parallel/src/
vi score_all.cpp
cd /root/catkin_ws && catkin_make && source devel/setup.bash
```

### 방법 B: `cat >` + 붙여넣기 + Ctrl+D

```bash
cat > /root/catkin_ws/src/cartographer_parallel/cartographer_parallel/src/score_all.cpp
# 붙여넣기 후 Ctrl+D
```

**주의:** `cat > ~/.bashrc` 는 **덮어쓰기** — `cat >>` 또는 `nano` 사용.

---

## 6. 실험별 PC 저장 파일

| PC 파일 | 내용 |
|---------|------|
| `pa01_<RUN>_env.txt` | 날짜, ROS_IP, uptime |
| `pa01_<RUN>_summary.txt` | 마지막 1줄 (avg, cumulative, calls) |
| `pa01_<RUN>_clean.log` | 모든 `[score_all]` 줄 |
| `pa01_<RUN>_run.log` | 전체 tee (선택, 용량 큼) |

`<RUN>` 예: `baseline`, `opt1_licm`, `opt2_loop_interchange`, `opt6_best`, `opt7_gpu`

---

## 7. 측정 전 부하 확인 (공유 Jetson)

**호스트 터미널** (`student_19@192.168.0.104`)에서:

```bash
w                    # 다른 student SSH 여부
top -bn1 | head -20  # CPU 점유
free -h
tegrastats           # GR3D_FREQ = GPU % (0%면 CUDA 부하 없음)
```

**컨테이너 안:**

```bash
docker ps
rosnode list | grep student
rostopic list | grep scan
```

**해석 예 (여유 상태):**

- load average ~0.30 (4코어 기준 여유)
- `top`: `id`(idle) ~96%
- `tegrastats`: `GR3D_FREQ 0%` 대부분, CPU @102MHz 유휴

---

## 8. OpenMP / CUDA 패키지 (레벨별)

| Level | 추가 설치 |
|-------|-----------|
| 0~5 | 없음 (단일 스레드 코드 경로) |
| 6 | `libomp-dev` 후 **재 catkin_make** |
| 7 | 보통 **CUDA 이미 포함** — `nvcc --version` 확인 |

```bash
# level 6 OpenMP 확인
apt-get install -y libomp-dev   # Jetson에서 1회
catkin_make -DPA01_OPT_LEVEL=6
grep PA01_HAS_OPENMP .../assignment_cpu_lib.dir/flags.make
# 기대: -DPA01_HAS_OPENMP=1 -fopenmp

# level 7 CUDA 확인
nvcc --version
ls /usr/local/cuda/bin/nvcc
python3 -c "import ctypes; l=ctypes.CDLL('libcudart.so'); c=ctypes.c_int(); l.cudaGetDeviceCount(ctypes.byref(c)); print('devices', c.value)"
```
