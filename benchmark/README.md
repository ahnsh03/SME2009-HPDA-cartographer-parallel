# score_all micro-benchmark (PA01)

ROS/bag 없이 **동일 입력**으로 CPU(L6 경로)와 GPU를 검증·측정합니다.  
하이브리드 분기 기준(`n>=64` GPU)을 **n 스윕으로 정량 확인**할 때 사용합니다.

## 빠른 실행

```bash
cd benchmark
chmod +x run_microbench.sh
./run_microbench.sh
```

또는:

```bash
make microbench7    # Jetson / CUDA PC
make verify         # CPU vs GPU max_diff
make sweep7         # n 스윕 + crossover 추천
```

CPU만 (CUDA 없는 PC):

```bash
make microbench6
make sweep6
```

## 무엇을 재는가

| 항목 | 설명 |
|------|------|
| **입력** | map 0501.pgm (또는 synthetic), **p=1081**, 합성 cx/cy |
| **CPU** | `ScoreN4`(n=4), OpenMP(n≥64), else interchange — **L6와 동일** |
| **GPU** | `score_all_cuda::ScoreCandidates` — **동일 grid/px/py/cx/cy** |
| **verify** | CPU vs GPU `max_diff` (기대: 0) |
| **timing** | warmup 후 반복 평균 ms/call (bag cumulative 아님) |

## n 스윕 출력

```text
n,cpu_ms,gpu_ms,cpu_over_gpu,max_diff
...
# crossover (first n with cpu/gpu>=1): n=...
# recommend: use GPU for n>=..., CPU for n<...
```

- **crossover**: GPU가 CPU와 같거나 빨라지는 **첫 n** (이 벤치·환경 기준)
- **현재 코드**: `kLargeCandThreshold = 64` (`score_all.cpp`, `score_all_cuda.cu`)
- SLAM bag에서는 **n=4(호출 多)** / **n=256(시간 多)** → 스윕 결과와 bag 로그를 **함께** 해석

Jetson catkin_ws (repo 루트 = `src/cartographer_parallel/`):

```text
~/catkin_ws/src/cartographer_parallel/
├── README.md
├── benchmark/              ← 여기 (새 폴더)
│   ├── pa01_microbench.cpp
│   ├── Makefile
│   └── run_microbench.sh
└── cartographer_parallel/  ← 기존 패키지 (건드리지 않음)
    ├── include/cartographer_parallel/score_all_bench.h  ← 추가
    ├── src/score_all.cpp                                ← 수정
    ├── src/score_all_cuda.cu
    └── maps/0501.pgm
```

PC repo (`PA01/benchmark/`)와 동일하게 **패키지와 형제(sibling)** 로 두면 Makefile이 경로를 자동 인식합니다.

## 레거시

`make legacy` → 예전 `bench0/bench2/bench6` (level별 `score_all()` 직접 호출).

## bag 실험과의 관계

| | micro-bench | bag `[score_all]` 로그 |
|--|-------------|------------------------|
| 목적 | 동일 n·p에서 CPU/GPU **공정 비교**, crossover | SLAM **실제 호출 분포**, cumulative |
| 호출 수 | 고정 반복 | 최적화 후 **증가 가능** |
| 권장 | threshold·정확성 근거 | 최종 보고서 KPI |

보고서에는 **둘 다** 쓰는 것이 가장 설득력 있습니다.
