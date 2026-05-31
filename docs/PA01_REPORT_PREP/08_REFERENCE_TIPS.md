# 08. 참고 정보·팁 (스크랩 통합)

보고서 본문에는 넣지 않아도 되는 **환경·도구·개념** 정리.

---

## 1. 정확한 벤치마크 (공유 Jetson)

4명이 동일 보드·GPU/CPU 대역폭 공유 → 다른 학생 부하 시 수치 흔들림.

### 호스트에서 확인

```bash
w
top -bn1 | head -20
free -h
tegrastats    # GR3D_FREQ = GPU %, Ctrl+C 종료
```

### 해석 예 (여유)

```
load average: 0.30        # 4코어 기준 4 근처가 포화
top: 95.9% id (idle)
tegrastats: CPU @102MHz, GR3D_FREQ 0% (대부분)
```

### 측정 팁

1. `uptime` load **< 2.0** 권장
2. 동일 조건 **3회** roslaunch → avg·표준편차
3. 보고서에 **측정 시각, load, calls** 기록
4. 다른 학생 bag 재생 중이면 “공유 환경” 명시

### Docker·ROS

```bash
docker ps
rosnode list | grep student
rostopic list | grep scan
# /student_05/scan, /student_19/scan 동시 → Master 공유
```

---

## 2. 리눅스·경로

| 개념 | 설명 |
|------|------|
| `/` | 파일시스템 루트 |
| `root` 계정 | 사용자 이름 (루트 디렉터리와 다름) |
| `~/catkin_ws` | `/root/catkin_ws` (컨테이너 root 계정) |
| 호스트 vs 컨테이너 | **서로 다른 파일시스템** — 코드는 컨테이너 안에서 빌드 |

---

## 3. SSH·파일 전송

### ProxyJump 없이 (2단)

```bash
ssh -p 22 rcv@112.171.196.32
ssh student_19@192.168.0.104
```

### scp 예 (summary만)

```bash
scp -o ProxyJump=rcv@112.171.196.32 \
  student_19@192.168.0.104:/root/pa01_baseline_summary.txt \
  ./data/
```

### docker exec 경유 (권장)

```bash
ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_baseline_summary.txt" > data/pa01_baseline_summary.txt
```

---

## 4. Git vs cat+scp (Jetson)

| | cat + scp | git pull/push |
|--|-----------|---------------|
| 코드 반영 | 빠름, 한 파일 | pull + catkin_make |
| 로그 | 대용량 scp | repo 비대화 |
| 실수 | 덮어쓰기 | 커밋·되돌리기 |

**추천 혼합**

- 코드: 로컬 개발 → GitHub → Jetson `git pull`
- 로그: **summary/clean만** `PA01/data/`, `_run.log`는 Git 제외

```bash
# 컨테이너에서
git --version
ssh -T git@github.com
```

저장소 예: `SME2009-HPDA-cartographer-parallel`

---

## 5. 강의 개념 ↔ PA01 매핑

| 주차/개념 | PA01 적용 |
|-----------|-----------|
| LICM | opt1 `inv_denom` |
| 캐시/지역성 | opt2 루프 교환 |
| 프리페치 | opt3 (Jetson에서 비효과) |
| 분기/워프 | opt4 (비효과) |
| OpenMP | opt6 n≥64 |
| CUDA/Shared mem | opt7, grid 캐시·타일링 논의 |
| Amdahl | n=4는 CPU 유지 |

---

## 6. PA02 미리보기 (PA01 보고서에 넣을 때 주의)

PA01에서 **하지 않은 것** (PA02 후보):

- `Branch`, `make_cand` 최적화
- `fast_matcher` 전체 CUDA화
- 여러 함수 선정·프로파일링 장문

PA01에서 **한 것**:

- `score_all` CUDA + grid device 캐시
- n별 CPU/GPU dispatch

---

## 7. 명령 치트시트 (전체)

```bash
# === 노트북 ===
ssh jetson-nano-19
mkdir -p ~/SME2009_HPDA/PA01/data

# === Docker ===
docker start student_19 && docker exec -it student_19 bash
source ~/.bashrc
cd /root/catkin_ws

# 빌드
catkin_make -DPA01_OPT_LEVEL=0    # baseline
catkin_make -DPA01_OPT_LEVEL=6    # CPU 최종
catkin_make -DPA01_OPT_LEVEL=7 -DPA01_USE_GPU=ON

source devel/setup.bash

# 실행
export RUN=baseline ROS_IP=192.168.0.104
roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
  2>&1 | tee ~/pa01_${RUN}_run.log

# 추출
grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log > ~/pa01_${RUN}_clean.log
grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log | tail -1 > ~/pa01_${RUN}_summary.txt

# PC
ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_${RUN}_summary.txt" \
  > ~/SME2009_HPDA/PA01/data/pa01_${RUN}_summary.txt
```

---

## 8. scrap.md에만 있던 가이드 (요약)

- **모바일 핫스팟** — 연구실 방화벽 우회
- **Docker 생성 예** — `docker run -it --runtime nvidia --network host -v /home/dataset/:/data --name student_19 ...`
- **PA02 로드맵** — 모듈 전체, 함수 2개+, CPU-GPU 협업 설계
- **Cartographer scan matching** — 위치 후보 탐색 맥락 설명

원문: `docs/scrap.md` (분할 후에도 보관).
