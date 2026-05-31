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

Repo 루트 안에 `cartographer_parallel/` 폴더가 **한 번 더** 있으므로, clone 폴더 이름을 `cartographer_parallel`로 하면 `src/cartographer_parallel/cartographer_parallel/cartographer_parallel/…`처럼 **3중**이 됩니다.

**권장:** repo 전체를 다른 이름(예: `hpda`)으로 clone → `git pull` 유지.

```bash
cd ~/catkin_ws/src
rm -rf hpda cartographer_parallel cartographer_parallel.bak
git clone --depth 1 https://github.com/ahnsh03/SME2009-HPDA-cartographer-parallel.git hpda

ls hpda/cartographer_parallel/cartographer_parallel/package.xml

cd ~/catkin_ws
rm -rf build devel
catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON -DPA02_PROFILE=ON -DPA02_OPT_LEVEL=0
source devel/setup.bash
```

코드 갱신:

```bash
cd ~/catkin_ws/src/hpda && git pull
cd ~/catkin_ws && catkin_make ...
```

catkin은 `src/` 아래 `package.xml`을 재귀 탐색합니다. 예전 수동 복사본(`src/cartographer_parallel/…`)과 **동시에 두지 마세요** — duplicate package 오류 원인.
