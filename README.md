# SME2009 HPDA — Cartographer Parallel (PA01 / PA02)

인하대 SME2009 **고성능 데이터/코드 분석** 과제: Google Cartographer `fast_correlative_scan_matcher` 병렬화.

| 과제 | 대상 파일 | 최종 설정 |
|------|-----------|-----------|
| **PA01** | `score_all.cpp`, `score_all_cuda.cu` | `PA01_OPT_LEVEL=9`, GPU hybrid, `PA01_GPU_THRESHOLD=256` |
| **PA02** | `fast_matcher.cpp` (matcher 오케스트레이션) | **`PA02_OPT_LEVEL=3`** + PA01 고정 |

> **채점·재현용:** 아래 [PA02 재현 (Jetson)](#pa02-재현-jetson) 절을 따라 **동일 bag KPI**를 확인할 수 있습니다.

---

## 1. 저장소 구조

```
hpda/                          ← git clone 루트 (폴더 이름 `hpda` 권장)
├── README.md                  ← 이 파일
├── cartographer_parallel/
│   └── cartographer_parallel/ ← ROS 패키지 (package.xml 위치)
│       ├── src/fast_matcher.cpp   ← PA02
│       ├── src/score_all.cpp      ← PA01
│       ├── src/score_all_cuda.cu  ← PA01 GPU
│       ├── launch/cartographer_parallel_with_bag.launch
│       ├── maps/0501.yaml, bags/scan.bag
│       └── CMakeLists.txt
├── scripts/pa02_bag_profile.sh  ← bag 1회 + 로그 추출
├── benchmark/                   ← microbench (ROS 불필요)
└── data/pa02/                   ← 참고 측정 로그 (Git)
```

**주의:** clone 폴더 이름을 `cartographer_parallel`로 하면  
`src/cartographer_parallel/cartographer_parallel/cartographer_parallel/…` **3중 경로**가 되어 catkin 오류가 납니다.  
**반드시 `hpda` 등 다른 이름으로 clone** 하세요.

---

## 2. 환경

| 항목 | 값 |
|------|-----|
| 하드웨어 | Jetson Nano (Docker `student_19`) |
| ROS | Melodic, catkin `/root/catkin_ws` |
| ROS Master | `http://192.168.0.106:11311` (공용) |
| ROS_IP | Jetson IP (예: `192.168.0.104`) — **본인 Jetson IP로 설정** |
| CUDA | Jetson Nano sm_53, `PA01_USE_GPU=ON` |
| map / bag | `467×314`, `maps/0501.yaml`, `bags/scan.bag` |
| launch namespace | `ns:=student_19` (본인 student 번호에 맞게 변경) |

---

## 3. 최초 설치 (Jetson Docker)

```bash
# Docker 컨테이너 안 (student_19)
cd /root/catkin_ws/src
rm -rf hpda cartographer_parallel   # 예전 수동 복사본 제거 (duplicate package 방지)

git clone https://github.com/ahnsh03/SME2009-HPDA-cartographer-parallel.git hpda
# SSH clone 사용 시: git clone git@github.com:ahnsh03/SME2009-HPDA-cartographer-parallel.git hpda

ls hpda/cartographer_parallel/cartographer_parallel/package.xml   # 경로 확인

cd /root/catkin_ws
rm -rf build devel
catkin_make \
  -DPA01_OPT_LEVEL=9 \
  -DPA01_USE_GPU=ON \
  -DPA02_OPT_LEVEL=3 \
  -DPA02_PROFILE=ON \
  -DPA02_MAKE_CAND_OMP_MIN=512 \
  -DPA02_BRANCH_OMP_MIN=999999 \
  -DPA01_GPU_THRESHOLD=256

source devel/setup.bash
```

코드 갱신:

```bash
cd /root/catkin_ws/src/hpda && git pull
cd /root/catkin_ws && catkin_make ...   # 위와 동일 CMake 플래그
source devel/setup.bash
```

---

## 4. PA02 재현 (Jetson)

### 4.1 PA02가 하는 일

PA02는 **`FastMatcher`** (make_cand → Branch-and-Bound → Score) 의 **CPU 오케스트레이션**을 최적화합니다.  
GPU `score_all` 커널은 **PA01(L9 hybrid)** 에서 처리하며, PA02는 그 위 레이어입니다.

| `PA02_OPT_LEVEL` | 내용 | 채택 |
|------------------|------|:----:|
| 0 | baseline | 비교용 |
| 1 | make_cand `vector::reserve` | ✓ (L3에 포함) |
| 2 | Branch buffer reuse, empty skip | ✓ |
| **3** | **Score scan-bucket + buffer reuse** | **✓ 최종** |
| 4~5 | exact reserve, sort-skip (실험만, bag KPI 무변화) | ✗ |

### 4.2 최종 빌드 (제출·채점용)

```bash
cd /root/catkin_ws
catkin_make \
  -DPA01_OPT_LEVEL=9 \
  -DPA01_USE_GPU=ON \
  -DPA02_OPT_LEVEL=3 \
  -DPA02_PROFILE=ON \
  -DPA02_MAKE_CAND_OMP_MIN=512 \
  -DPA02_BRANCH_OMP_MIN=999999 \
  -DPA01_GPU_THRESHOLD=256
source devel/setup.bash
```

빌드 확인:

```bash
grep PA02_OPT_LEVEL build/CMakeCache.txt          # STRING=3
grep PA01_GPU_DISPATCH_THRESHOLD build/CMakeCache.txt  # 256
strings devel/lib/libassignment_cpu_lib.so | grep ScoreCandidates   # GPU 심볼
```

### 4.3 bag 1회 실행 + 로그 추출

```bash
export ROS_MASTER_URI=http://192.168.0.106:11311
export ROS_IP=<본인_Jetson_IP>

cd /root/catkin_ws/src/hpda
chmod +x scripts/pa02_bag_profile.sh
./scripts/pa02_bag_profile.sh pa02_l3_profile
```

출력 위치: `data/pa02/pa02_l3_profile_*`

| 파일 | 용도 |
|------|------|
| `*_summary.txt` | 태그별 cumulative 한 줄 요약 |
| `*_match_clean.log` | `[match]` 전체 |
| `*_env.txt` | CMake·ROS 환경 기록 |
| `*_bottleneck.txt` | 병목 분해 (analyze 스크립트) |

### 4.4 KPI (채점·비교 지표)

**모듈 KPI = bag 1회 동안 `[match]` cumulative (ms)**  
(벽시계 roslaunch 시간 ≠ KPI)

참고 run (`data/pa02/pa02_l3_profile_summary.txt`, 2026-05-31 Jetson):

| metric | cumulative (ms) | 비고 |
|--------|----------------:|------|
| **`[match]`** | **87,866** | **PA02 KPI** |
| `[score_all]` | 34,786 | PA01 고정 |
| `best_score` | 0.783 | 마지막 match 줄 |
| `coarse_n` | 3840 | 탐색 공간 (정상) |
| `ok` | 1 | match 성공 |

동일 Jetson·동일 bag에서 **±3%** 정도는 SLAM 타이밍 비결정성으로 흔합니다.  
`best_score=0.783`, `coarse_n=3840` 유지 여부로 **정확도 회귀**를 함께 확인하세요.

KPI 추출 한 줄:

```bash
grep '\[match\]' data/pa02/pa02_l3_profile_match_clean.log | tail -1
# cumulative=87865.755 ms ... best_score=0.783 coarse_n=3840 ok=1
```

### 4.5 baseline(L0) vs 최종(L3) 비교 (선택)

```bash
# L0
catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON -DPA02_OPT_LEVEL=0 \
  -DPA02_PROFILE=ON -DPA01_GPU_THRESHOLD=256
source devel/setup.bash
./scripts/pa02_bag_profile.sh pa02_l0_profile

# L3 (최종) — §4.2 빌드 후
./scripts/pa02_bag_profile.sh pa02_l3_profile
```

| Level | match cumulative (ms) | Δ |
|-------|----------------------:|--:|
| L0 | 90,482 | — |
| **L3** | **87,866** | **−2.9%** |

---

## 5. CMake 옵션 요약

| 옵션 | 최종값 | 설명 |
|------|--------|------|
| `PA01_OPT_LEVEL` | **9** | PA01 hybrid bench 빌드 |
| `PA01_USE_GPU` | **ON** | CUDA `score_all` |
| `PA01_GPU_THRESHOLD` | **256** | n≥256 → GPU (bag sweep 확정) |
| `PA02_OPT_LEVEL` | **3** | make_cand + Branch + Score bucket |
| `PA02_PROFILE` | **ON** | `[make_cand]` `[Score]` `[Branch]` `[match]` 로그 |
| `PA02_MAKE_CAND_OMP_MIN` | **512** | hot path 16×16=256 → OMP off |
| `PA02_BRANCH_OMP_MIN` | **999999** | GPU 빌드에서 Branch OMP off (CUDA unsafe) |
| `PA01_CUDA_BLOCK_SIZE` | 128 (기본) | 실험용; bag KPI는 128 유지 |
| `PA01_CUDA_USE_PINNED` | OFF (기본) | 실험용; 미채택 |

---

## 6. 트러블슈팅

| 증상 | 해결 |
|------|------|
| `duplicate package cartographer_parallel` | `src/` 아래 수동 복사본 삭제, **`hpda` 하나만** 유지 |
| `git pull` 충돌 (untracked data) | `git status` 확인 후 untracked log/data 정리 또는 `git stash` |
| roslaunch 후 종료 안 됨 | launch에 `rosbag play required="true"` 포함됨 — bag 끝나면 종료 |
| `[match]` 로그 없음 | `PA02_PROFILE=ON` 으로 재빌드 |
| GPU 미사용 (`path=omp` @ n=256) | `-DPA01_USE_GPU=ON -DPA01_OPT_LEVEL=9 -DPA01_GPU_THRESHOLD=256` 확인 |
| `ROS_MASTER_URI=localhost` | `export ROS_MASTER_URI=http://192.168.0.106:11311` |
| namespace 불일치 | launch `ns:=student_XX` 를 **본인 student 번호**로 |

---

## 7. PA01 (요약)

PA01은 `score_all` 커널 최적화입니다. PA02 채점 시 **PA01은 L9+GPU T=256으로 고정**되어 있습니다.

```bash
catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON -DPA01_GPU_THRESHOLD=256
```

상세: `docs/PA01_GPU.md`, `docs/PA01_ADDITIONAL_EXPERIMENTS_20260531.md`

---

## 8. 문서·데이터 인덱스

| 문서 | 내용 |
|------|------|
| `docs/PA01_PA02_OPTIMIZATION_COMPLETE.md` | PA01+PA02 전체 타임라인·최종 빌드 |
| `docs/PA02_OPTIMIZATION_REVIEW.md` | CPU/GPU 역할, hybrid 결정 |
| `docs/PA02_FAQ_AND_ANALYSIS.md` | best_score, ROI, concurrent CUDA FAQ |
| `docs/pa02_report/` | 보고서 초안 (그림 포함) |
| `data/pa02/pa02_l{0,1,2,3}_profile_summary.txt` | 레벨별 참고 KPI |
| `data/bench/pa02_phase3_hybrid_*` | hybrid vs CPU-only bag sweep |

---

## 9. PC에서 microbench (ROS 불필요, 참고용)

```bash
cd benchmark
make pa02_microbench_gpu PA02_LEVEL=3 PA01_GPU_THRESHOLD=256
./pa02_microbench_gpu --yaml ../cartographer_parallel/cartographer_parallel/maps/0501.yaml \
  --mode match --warmup 2 --iters 10
```

> microbench 승자 ≠ bag KPI 승자. **채점·재현은 §4 bag run 기준.**

---

## 10. 연락·repo

- GitHub: `https://github.com/ahnsh03/SME2009-HPDA-cartographer-parallel`
- Jetson SSH (예): `ssh jetson-nano-19` (ProxyJump `rcv-gateway`)
