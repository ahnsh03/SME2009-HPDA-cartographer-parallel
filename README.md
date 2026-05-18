# SME2009 HPDA — Cartographer Parallel (PA01/PA02)

인하대 SME2009 고성능 데이터/코드 분석 과제: Google Cartographer `fast_correlative_scan_matcher` 병렬화.

- **PA01:** `score_all.cpp` CPU 최적화 (`PA01_OPT_LEVEL` 0~6, **6=최종**)
- **PA02:** CUDA assignment (예정)

## ROS 패키지

`cartographer_parallel/` — Jetson Nano Docker `student_19`, catkin 워크스페이스 `~/catkin_ws/src/` 에 배치.

```bash
cd ~/catkin_ws
catkin_make -DPA01_OPT_LEVEL=6   # 0=baseline .. 6=opt6_best (OpenMP)
source devel/setup.bash
export ROS_IP=<jetson_ip>
roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19"
```

## 문서

- `docs/CPU_OPTIMIZATION_PLAN.md` — 단계별 CPU 기법·측정 방법
- `docs/PA01_DEVELOPMENT_LOG_PART2.md` — 환경/트러블슈팅·측정 이력 (Part 2)
- `docs/PA01_CPU_VERIFICATION.md` — opt0~5 log/summary 검증·level6 설계 근거

## 측정 데이터

`data/pa01_*_summary.txt`, `data/pa01_*_clean.log` — grep 추출 로그 (Git 포함, ~42MB).

**OpenMP:** 기본 코드베이스는 **미사용**. `PA01_OPT_LEVEL=6` 빌드 시 CMake가 OpenMP를 찾으면 `n≥64`에서 후보 병렬화.

## Jetson

- Master: `192.168.0.106:11311`
- SSH: `jetson-nano-19` (ProxyJump `rcv-gateway`)
