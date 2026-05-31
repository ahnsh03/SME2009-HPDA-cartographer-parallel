# PA01 GPU (`score_all` level 7)

상세 개발·트러블슈팅(빌드 오류, `cudaMemcpy` 버그, 1차/2차 run 비교): **`docs/PA01_DEVELOPMENT_LOG_GPU.md`**

## 설계

| n | 경로 | 이유 |
|---|------|------|
| 4 | CPU `ScoreN4` | 커널 런치·전송 비용 > 이득 |
| ≥64 | CUDA `ScoreCandidatesKernel` | 후보당 스레드 1개 (OpenMP와 동일 병렬 단위) |
| 그 외 | CPU `ScoreInterchange` | 중간 크기 |

- **grid device 캐시**: 동일 `grid.data()` 포인터·크기면 H2D 생략 (`Score()` 내 스캔 반복 시 유리)
- **비동기 스트림**: px/py/cx/cy/score는 호출마다 H2D/D2H, 커널 후 `cudaStreamSynchronize` (로그 `elapsed`에 전송 포함)

## Jetson 빌드·실행

```bash
cd ~/catkin_ws
catkin_make -DPA01_OPT_LEVEL=7 -DPA01_USE_GPU=ON
source devel/setup.bash

# CUDA 심볼 확인
strings devel/lib/libassignment_cpu_lib.so | grep -E 'ScoreCandidates|cuda'

export ROS_IP=<jetson_ip>
RUN=opt7_gpu
roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
  2>&1 | tee ~/pa01_${RUN}_run.log

grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log | tail -1 > ~/pa01_${RUN}_summary.txt
grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log > ~/pa01_${RUN}_clean.log
```

**기대 로그**

- `LOADED ... opt=opt7_gpu_hybrid level=7 cuda=1`
- `n=256` 줄: `path=cuda | cuda=1`
- `n=4` 줄: `path=n4`

## CPU 최종(6)과 비교

동일 bag, summary의 **cumulative(ms)** 와 `n=256` / `n=4` grep 평균을 표로 정리.

GPU가 전체 cumulative에서 CPU(6)보다 느려도 정상일 수 있음(전송·작은 n 비중). 보고서에는 **구간별 + 이유**를 명시.

## PC micro-bench (CUDA 있을 때)

```bash
cd PA01/benchmark
# Makefile에 bench7 추가 후
make bench7 MAP=../cartographer_parallel/cartographer_parallel/maps/0501.pgm
```
