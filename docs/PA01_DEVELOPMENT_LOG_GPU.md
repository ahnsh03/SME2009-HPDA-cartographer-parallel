# PA01 GPU (opt7) 개발·트러블슈팅 기록

> CPU 단계(0~6)는 `PA01_DEVELOPMENT_LOG_PART2.md`, 측정 요약은 `PA01_CPU_VERIFICATION.md` 참고.  
> 본 문서는 **level 7 GPU 하이브리드** 구현·빌드·실패 run·수정·**최종 opt7 데이터 검증**까지 정리한다.

---

## 1. 배경·목표

- **과제(PA01)**: `score_all()` 단일 함수에 대해 CPU 고속화 + **GPU 고속화** + 비교 분석 (Jetson Nano 필수).
- 교수님 코멘트: 현 워크로드에서는 **CPU(특히 OpenMP)가 GPU보다 유리할 수 있음**. 다만 GPU 구현·분석 자체가 학습 목적이므로 **진행**.
- **전략**: GPU-only가 아니라 **하이브리드(level 7)** — CPU opt6와 동일한 dispatch 철학.
  - `n=4` → CPU `ScoreN4` (호출 수 많음, 커널 런치 비용 큼)
  - `n≥64` → CUDA (실질적으로 `n=256`이 병목)
  - 그 외 → CPU `ScoreInterchange`

`fast_matcher.cpp`는 과제 범위상 수정하지 않음. 모든 최적화는 `score_all` + CMake.

---

## 2. 구현 개요

### 2.1 추가·변경 파일

| 파일 | 역할 |
|------|------|
| `include/cartographer_parallel/score_all_cuda.h` | CUDA host API (`ScoreCandidates`, `IsAvailable`) |
| `src/score_all_cuda.cu` | 커널, device 버퍼, grid 캐시, H2D/D2H |
| `src/score_all.cpp` | `PA01_OPT_LEVEL==7` 분기, `opt6` 네임스페이스 재사용 + CUDA dispatch |
| `CMakeLists.txt` | `-DPA01_USE_GPU=ON` 시 `.cu` 빌드, `libcudart` 링크, SM 5.3 (Nano) |

### 2.2 빌드·실행

```bash
cd ~/catkin_ws
catkin_make -DPA01_OPT_LEVEL=7 -DPA01_USE_GPU=ON
source devel/setup.bash
```

### 2.3 CUDA 커널 (수학 = CPU와 동일)

- 후보 `i`당 스레드 1개, 내부에서 스캔 `j=0..p-1` 누적.
- 맵 밖 좌표는 기여 0. 최종 `score[i] = sum * (1/(255*p))`.
- **grid**: 동일 host 포인터·`w×h`이면 device 재업로드 생략.
- **px/py/cx/cy/score**: 호출마다 `cudaMemcpyAsync` + `cudaStreamSynchronize` (로그 `elapsed`에 전송·동기화 포함).

### 2.4 로그 태그

```
[score_all] LOADED opt=opt7_gpu_hybrid level=7 cuda=1 (hybrid: n=4 CPU, n>=64 GPU)
...
| path=n4 | cuda=1     # n=4, CPU
| path=cuda | cuda=1   # n>=64, GPU 성공
| path=interchange | cuda=1   # GPU 실패 시 폴백 또는 중간 n
```

---

## 3. 트러블슈팅 (시간순)

### 3.1 Jetson CUDA 환경 확인 (사전)

**목적**: 별도 CUDA 설치 없이 빌드 가능한지 확인.

```bash
nvcc --version
ls /usr/local/cuda/bin/nvcc
ls /usr/local/cuda-10.2/targets/aarch64-linux/lib/libcudart.so*
ldconfig -p | grep cudart
python3 -c "import ctypes; l=ctypes.CDLL('libcudart.so'); c=ctypes.c_int(); l.cudaGetDeviceCount(ctypes.byref(c)); print('devices', c.value)"
```

**결과**

- `nvcc` 10.2, `libcudart.so.10.2` 존재, `devices 1`.
- **추가 apt 설치 없이** 빌드 가능한 환경으로 판단 (`libomp-dev`는 level 6용, level 7 필수 아님).

---

### 3.2 빌드 오류: `'score_all_cuda' has not been declared`

**증상** (`catkin_make -DPA01_OPT_LEVEL=7 -DPA01_USE_GPU=ON`):

```
score_all.cpp:545:7: error: 'score_all_cuda' has not been declared
      score_all_cuda::ScoreCandidates(...)
```

**확인**

```bash
grep -n 'score_all_cuda' ~/catkin_ws/src/.../score_all.cpp | head
# 425:#include "cartographer_parallel/score_all_cuda.h"  (당시 namespace 안쪽)
ls .../score_all_cuda.h
ls .../score_all_cuda.cu
grep PA01_OPT .../flags.make
# PA01_OPT_LEVEL=7, PA01_USE_GPU=1 정상
```

**원인**

- `score_all_cuda.h`가 이미 열려 있는 `namespace cartographer_parallel {` **안에서** include됨.
- 헤더가 다시 `namespace cartographer_parallel { namespace score_all_cuda {` 를 열어  
  실제 선언이 `cartographer_parallel::cartographer_parallel::score_all_cuda` 로 중첩됨.
- `opt6::Dispatch` 안의 `score_all_cuda::` 는 바깥 `cartographer_parallel::score_all_cuda` 를 찾지 못함.

**수정**

- `#include "cartographer_parallel/score_all_cuda.h"` 를 **파일 최상단**, `namespace cartographer_parallel {` **이전**으로 이동 (24~29행 부근).

**수정 후**

- `catkin_make` 성공, `strings devel/lib/libassignment_cpu_lib.so | grep ScoreCandidates` 에 CUDA 심볼 확인.

---

### 3.3 실행: CUDA가 안 도는 것처럼 보임 (1차 측정, 버그 run)

**증상**

```bash
grep 'LOADED' ~/pa01_opt7_gpu_run.log | head -1
# LOADED ... cuda=1  → 빌드는 level 7

grep 'path=cuda' ~/pa01_opt7_gpu_run.log | head -3
# (출력 없음)

grep 'path=n4' ~/pa01_opt7_gpu_run.log | head -3
# path=n4 만 보임
```

**clean.log 분석** (`pa01_opt7_gpu` 구버전, ~43k calls):

- `n=256` 호출도 전부 `path=interchange` (CPU 폴백).
- 첫 호출 `elapsed≈124 ms` 후 `~6.7 ms` — OpenMP opt6의 interchange와 유사 (GPU 미사용 패턴).

**런타임 에러 확인**

```bash
grep -E 'score_all_cuda|WARNING: no CUDA' ~/pa01_opt7_gpu_run.log | head -20
```

**결과** (반복):

```
[score_all_cuda] cudaMemcpy py: invalid argument
```

- `ScoreCandidates()` 가 memcpy 단계에서 실패 → `Dispatch` 가 `false` 반환 → **항상 CPU `interchange` 폴백**.
- 사용자 관찰: “쿠다로 안 돌아간 것 같다” → **정확함**.

**추가 확인** (GPU 자체는 살아 있음):

```bash
python3 -c "import ctypes; ... cudaGetDeviceCount ... print('devices', c.value)"
# devices 1
```

→ 드라이버/디바이스 문제가 아니라 **우리 코드 버그**.

**원인** (`score_all_cuda.cu`의 `Grow()`):

```cpp
Grow(&b.d_px, &b.cap_p, need_p);  // d_px 할당, cap_p 갱신
Grow(&b.d_py, &b.cap_p, need_p);  // cap_p 이미 충분 → d_py 할당 생략 → nullptr
cudaMemcpy(b.d_py, py, ...);      // invalid argument
```

- `d_px`/`d_py` 가 **`cap_p` 공유**, `d_cx`/`d_cy`/`d_score` 가 **`cap_n` 공유** — 두 번째 버퍼가 malloc 없이 통과.

**수정**

- 용량을 버퍼별로 분리: `cap_px`, `cap_py`, `cap_cx`, `cap_cy`, `cap_score`.
- `Grow()`: `*ptr != nullptr && *cap >= need` 일 때만 재할당 생략.

**수정 후 재측정 절차**

```bash
# score_all_cuda.cu 반영 후
cd ~/catkin_ws && catkin_make -DPA01_OPT_LEVEL=7 -DPA01_USE_GPU=ON
source devel/setup.bash
roslaunch ... 2>&1 | tee ~/pa01_opt7_gpu_run.log
grep 'path=cuda' ~/pa01_opt7_gpu_run.log | head -3   # 이제 n=256 에서 보여야 함
grep 'score_all_cuda' ~/pa01_opt7_gpu_run.log | head  # 에러 없어야 함
```

---

## 4. 최종 opt7 데이터 검증 (수정 후)

**파일**: `data/pa01_opt7_gpu_summary.txt`, `data/pa01_opt7_gpu_clean.log`

### 4.1 LOADED·경로 분포

| 항목 | 결과 |
|------|------|
| LOADED | `opt=opt7_gpu_hybrid level=7 cuda=1` |
| `[score_all_cuda]` 에러 | **0건** (clean.log 전체) |
| `path=cuda` | **37,866** 회 |
| `path=n4` | **93,084** 회 |
| `path=interchange` | **2,525** 회 (중간 n 등) |
| 총 `call` (summary) | **133,496** |

→ **GPU 경로 정상 동작**. 1차 버그 run(~43k calls, cuda 0회)과 구분할 것.

### 4.2 summary 마지막 줄

```
cumulative=42864.140 ms / 133496 calls (avg=0.321 ms/call) | path=n4
```

- 마지막 한 줄이 `path=n4`인 것은 **마지막 호출이 n=4**이기 때문이며, 전 run이 n4만 돈 것은 아님.

### 4.3 구간별 평균 elapsed (clean.log 집계)

| path | n | 호출 수 | 평균 elapsed | 비고 |
|------|---|---------|--------------|------|
| cuda | 256 | 37,693 | **~0.91 ms** | 1회차 **~96 ms** (초기 grid H2D·컨텍스트) 제외 시 ~2 ms대 |
| n4 | 4 | 92,696 | **~0.088 ms** | CPU 전용 |
| interchange | 기타 | 2,510 | **~0.063 ms** | 소형 n |

**1차 실패 run과 대비**

| | 버그 run | 수정 후 |
|--|----------|---------|
| path=cuda | 0 | 37,866 |
| n=256 | interchange ~6.7 ms | cuda ~2 ms (안정), 1회차 96 ms |
| cumulative | ~81,189 ms / 43,018 calls | **42,864 ms / 133,496 calls** |

---

## 5. 보고서·다음 단계

- **개발·트러블슈팅**: 본 문서 + `docs/PA01_GPU.md`(빌드 명령).
- **성능 비교 표**: `PA01_CPU_VERIFICATION.md`의 level 6 대비 — cumulative, `n=256` cuda 구간, `n=4` 구간을 **별도 행**으로 작성 (다음 단계).
- **교수님 코멘트 대응**: “GPU가 전체에서는 이기지 못해도” → 전송·n=4 비중·첫 호출 워밍업을 근거로 서술.

---

## 6. 참고 명령 모음

```bash
# 빌드
catkin_make -DPA01_OPT_LEVEL=7 -DPA01_USE_GPU=ON
strings devel/lib/libassignment_cpu_lib.so | grep -E 'ScoreCandidates|cuda'

# 로그 추출
RUN=opt7_gpu
grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log | tail -1 > ~/pa01_${RUN}_summary.txt
grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log > ~/pa01_${RUN}_clean.log

# GPU 동작 여부
grep 'path=cuda' ~/pa01_${RUN}_run.log | head -3
grep 'score_all_cuda' ~/pa01_${RUN}_run.log | head
grep -c 'path=cuda' ~/pa01_${RUN}_clean.log
```
