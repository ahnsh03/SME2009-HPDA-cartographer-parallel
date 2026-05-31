# SME2009 HPDA — Cartographer Parallel (PA01/PA02)

인하대 SME2009 고성능 데이터/코드 분석 과제: Google Cartographer `fast_correlative_scan_matcher` 병렬화.

- **PA01:** `score_all.cpp` CPU (`PA01_OPT_LEVEL` 0~6, **6=CPU 최종**) + GPU 하이브리드 (**7**)
- **PA02:** `fast_correlative_scan_matcher` 전체 (예정)

## ROS 패키지

`cartographer_parallel/` — Jetson Nano Docker `student_19`, catkin 워크스페이스 `~/catkin_ws/src/` 에 배치.

```bash
cd ~/catkin_ws
catkin_make -DPA01_OPT_LEVEL=6   # CPU 최종 (OpenMP, libomp-dev)
catkin_make -DPA01_OPT_LEVEL=7 -DPA01_USE_GPU=ON   # GPU 하이브리드 (Jetson)
source devel/setup.bash
export ROS_IP=<jetson_ip>
roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19"
```

## 문서

- `docs/CPU_OPTIMIZATION_PLAN.md` — 단계별 CPU 기법·측정 방법
- `docs/PA01_DEVELOPMENT_LOG_PART2.md` — 환경/트러블슈팅·측정 이력 (Part 2)
- `docs/PA01_CPU_VERIFICATION.md` — opt0~6 log/summary 검증
- `docs/PA01_GPU.md` — level 7 빌드·측정·비교
- `docs/PA01_DEVELOPMENT_LOG_GPU.md` — opt7 구현·트러블슈팅·데이터 검증

## 측정 데이터

`data/pa01_*_summary.txt`, `data/pa01_*_clean.log` — grep 추출 로그 (Git 포함, ~42MB).

**OpenMP:** 기본 코드베이스는 **미사용**. `PA01_OPT_LEVEL=6` 빌드 시 CMake가 OpenMP를 찾으면 `n≥64`에서 후보 병렬화.

## Jetson

- Master: `192.168.0.106:11311`
- SSH: `jetson-nano-19` (ProxyJump `rcv-gateway`)

### Git clone (catkin `src/` 배치)

Repo 루트 안에 이미 `cartographer_parallel/` 폴더가 있으므로, **clone 대상 폴더 이름을 `cartographer_parallel`로 하면 경로가 3중**이 됩니다. 아래처럼 **repo만 clone한 뒤 inner 폴더만 `src/`로 옮깁니다.**

```bash
cd ~/catkin_ws/src
rm -rf cartographer_parallel cartographer_parallel.bak _hpda_repo
git clone --depth 1 https://github.com/ahnsh03/SME2009-HPDA-cartographer-parallel.git _hpda_repo
mv _hpda_repo/cartographer_parallel cartographer_parallel
rm -rf _hpda_repo

# 기대 경로: src/cartographer_parallel/cartographer_parallel/package.xml
ls cartographer_parallel/cartographer_parallel/package.xml

cd ~/catkin_ws
rm -rf build devel   # 경로 변경 후 1회 클린 빌드 권장
catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON -DPA02_PROFILE=ON -DPA02_OPT_LEVEL=0
source devel/setup.bash
```

이후 코드 갱신:

```bash
cd ~/catkin_ws/src/cartographer_parallel/cartographer_parallel
# inner가 git root가 아님 → repo 루트에서 pull:
cd ~/catkin_ws/src && rm -rf _hpda_repo && git clone --depth 1 ... _hpda_repo && \
  rsync -a _hpda_repo/cartographer_parallel/ cartographer_parallel/ && rm -rf _hpda_repo
```

또는 `src/cartographer_parallel` 전체를 repo root로 symlink/clone 유지하려면 `_hpda_repo` 이름으로 clone하고 `scripts/` 등은 repo 루트에서 별도 sync.

**간단 pull (repo root를 `src/hpda`로 clone해 두는 경우):**

```bash
cd ~/catkin_ws/src/hpda && git pull
cd ~/catkin_ws && catkin_make ...
```

catkin은 `src/` 아래 `package.xml`을 재귀 탐색하므로 clone **폴더 이름**은 자유롭지만, **ROS 패키지 depth**는 `.../cartographer_parallel/cartographer_parallel/package.xml` (2단)이 README·launch와 일치합니다.
