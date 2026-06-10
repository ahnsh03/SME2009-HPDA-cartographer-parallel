# PA01 CUDA 개선 실험 결과 (W14/W15 개념 적용)

> Jetson Docker `student_19`, map 467×314, p=1081  
> 실험일: 2026-06-10  
> 동기: CuPy(W14) pinned memory·transfer once, Numba(W15) block size·occupancy 튜닝

---

## 1. 구현한 개선 (CMake 옵션)

| 옵션 | 기본값 | W14/W15 대응 |
|------|--------|--------------|
| `PA01_CUDA_BLOCK_SIZE` | **128** | W15 launch config (threads/block) |
| `PA01_CUDA_USE_PINNED` | **OFF** | W14 pinned memory + W15 pinned H2D |

코드: `score_all_cuda.cu` — pinned staging은 H2D 전 pageable→pinned `memcpy` 후 `cudaMemcpyAsync`.

재현:

```bash
# microbench sweep (Jetson)
./scripts/pa01_cuda_experiment_sweep.sh

# catkin production
catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON \
  -DPA01_CUDA_BLOCK_SIZE=256 -DPA01_CUDA_USE_PINNED=OFF ...
```

---

## 2. Microbench (bag-like, n=256, sporadic per-call)

데이터: `data/bench/pa01_cuda_experiment_20260610_005915/microbench_baglike.csv`

| variant | block | pinned | gpu_ms (n=256) | Δ vs baseline | verify |
|---------|------:|--------|---------------:|--------------:|:------:|
| **baseline** | 128 | 0 | **0.7703** | — | PASS |
| **block256** | 256 | 0 | **0.6276** | **−18.5%** | PASS |
| block512 | 512 | 0 | 0.6387 | −17.1% | PASS |
| pinned128 | 128 | 1 | 0.8116 | +5.4% | PASS |
| pinned256 | 256 | 1 | 0.8135 | +5.6% | PASS |

→ microbench만 보면 **block256/512가 크게 유리**, pinned는 **역효과** (추가 host memcpy).

---

## 3. Bag 실측 (PA02 L3, score_all cumulative KPI)

데이터: `data/bench/pa01_cuda_experiment_20260610_bag/bag_score_all.csv`

| variant | score_all cumulative (ms) | calls | n=256 cuda avg (ms) | Δ cumulative |
|---------|--------------------------:|------:|--------------------:|---------------:|
| **baseline (block128)** | **35,456.6** | 112,513 | **0.844** | — |
| block256 | 35,539.3 | 111,498 | 0.850 | **+0.23%** (악화) |

→ **microbench −18% ≠ bag 개선.** Phase 3 hybrid sweep과 동일 패턴 재현.

---

## 4. 해석

### 4.1 Block size 256

- n=256, block=128 → 2 blocks/SM; block=256 → 1 block — Jetson Nano에서 occupancy·launch 패턴 변화.
- **격리 microbench**에서는 GPU kernel+sync 구간만 측정 → block256 유리.
- **실제 bag**에서는 ROS 타이밍, SLAM 호출 간격, grid cache hit, n=4↔n=256 교차, `[score_all]` 로깅 등 **전체 cumulative** 기준 차이 소멸(±0.2% 노이즈).

### 4.2 Pinned staging

- W14: pinned → faster async DMA.
- 본 워크load: px/py/cx/cy **~10 KB/호출** — pinned 이득 < **pageable→pinned memcpy 비용**.
- microbench n=256: baseline 0.77 ms vs pinned128 0.81 ms (**+5%**).

### 4.3 채택 권고

| 항목 | 권고 | 이유 |
|------|:----:|------|
| `PA01_CUDA_BLOCK_SIZE=128` | **유지** | bag KPI 최소 (35,457 ms) |
| `PA01_CUDA_USE_PINNED` | **OFF 유지** | microbench·bag 모두 악화 |
| block256 | **미채택** | microbench만 빠름, bag +0.2% |

**production 빌드 변경 없음** — 기존 L9 + T=256 + block128.

---

## 5. 보고서용 한 문단

W14 pinned memory·W15 block size 튜닝을 `PA01_CUDA_BLOCK_SIZE`·`PA01_CUDA_USE_PINNED` CMake 옵션으로 구현하고 Jetson에서 검증하였다. bag-like microbench(n=256)에서 block256은 GPU 0.63 ms로 baseline 0.77 ms 대비 18% 빠르였으나, PA02 L3 bag에서 score_all cumulative는 baseline 35,457 ms vs block256 35,539 ms(+0.2%)로 KPI 개선이 없었다. pinned staging은 추가 host memcpy로 microbench에서 5% 느려져 기각하였다. CuPy/Numba 라이브러리 자체는 C++ SLAM 노드에 부적합하나, 강의 개념 중 **shared memory(px/py)는 이미 적용**, **transfer-once는 grid cache로 적용**, **pinned·block sweep은 bag KPI 기준 미채택**으로 정리한다.

---

## 6. 관련 파일

| 파일 | 용도 |
|------|------|
| `scripts/pa01_cuda_experiment_sweep.sh` | variant sweep |
| `cartographer_parallel/.../score_all_cuda.cu` | block + pinned 구현 |
| `data/bench/pa01_cuda_experiment_20260610_005915/` | microbench CSV |
| `data/bench/pa01_cuda_experiment_20260610_bag/` | bag CSV |
