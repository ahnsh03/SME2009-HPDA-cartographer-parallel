# score_all micro-benchmark (PC)

ROS/bag 없이 `score_all`만 반복 측정합니다. SLAM 점수는 baseline(level 0)과 **max_diff=0** 으로 검증했습니다.

```bash
cd benchmark
make compare
```

예시 (PC, map 0501.pgm, 500 iters):

| Level | n=4 (ms) | n=256 (ms) |
|-------|----------|------------|
| 0 baseline | ~0.005 | ~0.49 |
| 2 interchange | ~0.005 | ~0.37 |
| 6 (OpenMP) | **~0.003** | **~0.16** |
| 6 (no OpenMP) | **~0.003** | **~0.25** |

Jetson: `libomp-dev` 설치 후 level 6 빌드 시 `openmp=1`, `n=256`에서 `path=omp_cand` 기대.
