# 06. GPU 레벨 고속화 (level 7, CUDA 하이브리드)

## 1. 전략 (GPU-only가 아닌 하이브리드)

과제는 “GPU만”이 아니라 **GPU 고속화 + 어디에 쓸지 판단**이 핵심.

| n | 경로 | 이유 |
|---|------|------|
| n = 4 | CPU `ScoreN4` | 호출 ~9만 회, 연산 작음 → **커널 런치·전송 > 이득** |
| n ≥ 64 (실질 256) | CUDA `path=cuda` | 병목 구간, 후보 병렬 |
| 그 외 | CPU `ScoreInterchange` | 중간 n |

- **level 6** = CPU 최종 (보고서 1) CPU 고속화 **대표값**)
- **level 7** = CPU + GPU 융합 (보고서 2) GPU 고속화)

**교수님 코멘트 대응:** 전체 cumulative만 보면 CPU가 이길 수 있으나, **n=256 + path=cuda** 구간에서 GPU 이득을 분리 서술.

---

## 2. 구현 파일

| 파일 | 역할 |
|------|------|
| `include/cartographer_parallel/score_all_cuda.h` | `ScoreCandidates`, `IsAvailable` |
| `src/score_all_cuda.cu` | 커널, device 버퍼, grid 캐시 |
| `src/score_all.cpp` | `PA01_OPT_LEVEL==7` dispatch |
| `CMakeLists.txt` | `PA01_USE_GPU=ON`, SM 5.3 (Nano), `libcudart` |

**수학 (CPU/opt6와 동일):**

- 후보 `i` = 스레드 1개, 내부 `j=0..p-1` 누적
- 맵 밖 → 0
- `score[i] = sum * (1/(255*p))`
- **elapsed:** H2D/D2H + `cudaStreamSynchronize` 포함 (함수 전체 시간)

---

## 3. 빌드·실행 명령

### 환경 확인 (컨테이너)

```bash
nvcc --version
# CUDA 10.2 예

ls /usr/local/cuda/bin/nvcc
ls /usr/local/cuda-10.2/targets/aarch64-linux/lib/libcudart.so*

python3 -c "import ctypes; l=ctypes.CDLL('libcudart.so'); c=ctypes.c_int(); l.cudaGetDeviceCount(ctypes.byref(c)); print('devices', c.value)"
# devices 1
```

→ **추가 apt CUDA 설치 없이** 빌드 가능한 경우가 많음. (`libomp-dev`는 level 7 **필수 아님**.)

### 빌드·실행

```bash
cd /root/catkin_ws
catkin_make -DPA01_OPT_LEVEL=7 -DPA01_USE_GPU=ON
source devel/setup.bash

export RUN=opt7_gpu
export ROS_IP=192.168.0.104

roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
  2>&1 | tee ~/pa01_${RUN}_run.log

grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log | tail -1 > ~/pa01_${RUN}_summary.txt
grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log > ~/pa01_${RUN}_clean.log
```

### 동작 검증

```bash
grep 'LOADED' ~/pa01_opt7_gpu_run.log | head -1
# opt=opt7_gpu_hybrid level=7 cuda=1

grep 'path=cuda' ~/pa01_opt7_gpu_run.log | head -3
grep 'score_all_cuda' ~/pa01_opt7_gpu_run.log | head
# cudaMemcpy 에러 없어야 함

strings devel/lib/libassignment_cpu_lib.so | grep ScoreCandidates
```

### 로그 태그 예

```
[score_all] LOADED opt=opt7_gpu_hybrid level=7 cuda=1 (hybrid: n=4 CPU, n>=64 GPU)
...
| path=n4 | cuda=1
| path=cuda | cuda=1
| path=interchange | cuda=1
```

---

## 4. 전송·캐시 (PA01 범위, `score_all` 내부)

| 데이터 | 크기(대략) | 전략 |
|--------|------------|------|
| grid | ~147KB (467×314) | device 캐시, 동일 포인터·w×h면 **재업로드 생략** |
| px, py | ~4KB×2 | 호출마다 memcpy |
| cx, cy | n×8 | 호출마다 memcpy |
| score | n×4 | D2H |

**한계:** 호출마다 작은 버퍼 전송 + sync — **n=4는 CPU 유지**가 타당.

---

## 5. 트러블슈팅 (GPU)

### 5.1 빌드: `score_all_cuda has not been declared`

**증상**

```
score_all.cpp:545: error: 'score_all_cuda' has not been declared
```

**원인:** 헤더를 `namespace cartographer_parallel {` **안**에서 include → `cartographer_parallel::cartographer_parallel::score_all_cuda`.

**수정:** include를 **파일 최상단**, namespace **이전**.

---

### 5.2 실행: 1차 버그 run

| 항목 | 버그 run | 수정 후 |
|------|----------|---------|
| path=cuda | **0** | **37,866** |
| `[score_all_cuda]` | `cudaMemcpy py: invalid argument` | **0건** |
| calls | ~43,018 | **133,496** |
| cumulative | ~81,189 ms | **42,864 ms** |
| n=256 | interchange ~6.7 ms | cuda **~0.91 ms** (1회차 ~96 ms 워밍업 제외) |

**원인:** `Grow()`에서 `d_px`/`d_py`가 `cap_p` 공유 → `d_py` nullptr.

**수정:** `cap_px`, `cap_py`, `cap_cx`, `cap_cy`, `cap_score` 분리.

---

## 6. 최종 opt7 실측 (`data/pa01_opt7_gpu_*`)

### summary 마지막 줄

```
[score_all] opt=opt7_gpu_hybrid level=7 | call=133496 | elapsed=0.082 ms (82 us) | n=4 p=1081 | map=467x314 | work_units(n*p)=4324 | us_per_candidate=20.500 | cumulative=42864.140 ms / 133496 calls (avg=0.321 ms/call) | path=n4 | cuda=1
```

| 항목 | 값 |
|------|-----|
| 총 calls | 133,496 |
| cumulative | **42,864 ms** |
| avg | 0.321 ms/call |
| path=cuda | 37,866회 |
| path=n4 | 93,084회 |
| path=interchange | 2,525회 |

### 구간별 평균 elapsed (clean.log 집계)

| path | n | 호출 수 | 평균 elapsed | 비고 |
|------|---|---------|--------------|------|
| cuda | 256 | 37,693 | **~0.91 ms** | 1회 **~96 ms** = grid 최초 H2D |
| n4 | 4 | 92,696 | **~0.088 ms** | CPU |
| interchange | 기타 | 2,510 | ~0.063 ms | CPU |

---

## 7. GPU vs CPU OpenMP (n=256만, 개념)

| | opt6 omp_cand | opt7 cuda |
|--|---------------|-----------|
| 병렬 | 4 CPU 코어 | GPU 다수 스레드 |
| n=256 avg | ~2.0 ms | ~0.91 ms (안정 구간) |
| 전체 cumulative | 52,132 ms | 42,864 ms |

**첫 cuda 호출 ~96 ms:** device grid 업로드·컨텍스트 — 보고서에 **워밍업 1회 제외** 명시 가능.

---

## 8. 보고서 GPU 섹션 체크리스트

- [ ] 커널: 후보당 1 스레드, bounds, `inv_denom`
- [ ] 하이브리드 이유 (n=4 CPU, n≥64 GPU)
- [ ] grid device 캐시·전송 비용
- [ ] 빌드/실행 실패 2건 → 수정 (짧게)
- [ ] **1차 실패 vs 2차 성공** run 구분
- [ ] `path=cuda` 구간 수치 **필수**
- [ ] SM 5.3, Jetson Maxwell, CUDA 10.2 환경

상세: `docs/PA01_DEVELOPMENT_LOG_GPU.md`, `docs/PA01_GPU.md`.
