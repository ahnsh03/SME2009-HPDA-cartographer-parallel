# SME2009 HPDA — Cartographer Parallel (PA01/PA02)

인하대 SME2009 고성능 데이터/코드 분석 과제: Google Cartographer `fast_correlative_scan_matcher` 병렬화.

- **PA01:** `score_all.cpp` CPU 최적화 (`PA01_OPT_LEVEL` 0~6, **6=최종 CPU**)
- **PA02:** CUDA assignment (예정)

## ROS 패키지

`cartographer_parallel/` — Jetson Nano Docker `student_19`, catkin 워크스페이스 `~/catkin_ws/src/` 에 배치.

```bash
cd ~/catkin_ws
catkin_make -DPA01_OPT_LEVEL=6   # 0=baseline .. 6=final CPU
source devel/setup.bash
export ROS_IP=<jetson_ip>
roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19"
```

## 문서

- `docs/CPU_OPTIMIZATION_PLAN.md` — 단계별 CPU 기법·측정 방법
- `docs/PA01_CPU_ANALYSIS.md` — level 0~5 검증·통찰·level 6 설계
- `docs/PA01_DEVELOPMENT_LOG_PART2.md` — 환경/트러블슈팅·측정 이력 (Part 2)
- `scripts/collect_pa01_logs.sh` — Jetson roslaunch + grep 수집

## 측정 데이터

`data/pa01_*_summary.txt` — 마지막 줄 (`avg ms/call`, `opt=`, `level=`).  
`data/pa01_*_clean.log` — `[score_all]` 줄만 추출 (grep용, Git 포함).

PC로 가져오기:

```bash
RUN=opt6_final_cpu
ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_${RUN}_summary.txt" \
  > data/pa01_${RUN}_summary.txt
ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_${RUN}_clean.log" \
  > data/pa01_${RUN}_clean.log
```

## Jetson

- Master: `192.168.0.106:11311`
- SSH: `jetson-nano-19` (ProxyJump `rcv-gateway`)
