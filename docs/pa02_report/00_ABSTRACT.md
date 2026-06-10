# Abstract

본 보고서는 Google Cartographer 기반 2D Fast Correlative Scan Matcher의 **오케스트레이션 계층**(`make_cand`, `Score`, `Branch`)을 Jetson Nano 실환경에서 최적화한 PA02 과제의 결과를 정리한다. PA01에서 `score_all` 커널을 L9 GPU hybrid로 고정(−61%)한 뒤, bag 프로파일링으로 matcher_scope **56.7 s(62.6%)** 가 잔여 병목임을 실측하였다.

최적화는 Phase 1(make_cand reserve) → Phase 2(Branch buffer reuse) → Phase 3(Score scan-bucket) 순으로 누적 적용하였으며, 모듈 KPI `[match]` cumulative는 **90,482 ms → 87,866 ms(−2.9%)** 로 개선되었다. `best_score=0.783`, `coarse_n=3840`은 전 단계에서 유지되어 matcher 정확도 회귀가 없음을 확인하였다.

본 연구의 측정 방법론적 핵심은, 격리 microbench가 아닌 **`roslaunch` + bag replay 환경에서 태그별 cumulative 시간**을 KPI로 삼은 것이다. Phase 3 hybrid 실험에서 CPU-only variant는 microbench에서 가장 빠르지만(36.6 ms), bag에서는 hybrid(L3+GPU T=256)가 **4.4% 우수**(91,750 ms vs 87,866 ms)하여, SLAM 전체 맥락에서의 성능 평가가 함수 단위 벤치보다 신뢰할 수 있음을 재확인하였다.

upstream Cartographer의 `ShrinkToFit` 이식 실험은 `best_score` 0.783→0.748 정확도 회귀로 기각하였다. 최종 production 빌드는 `PA02_OPT_LEVEL=3`, `PA01_GPU_THRESHOLD=256`, `PA02_BRANCH_OMP_MIN=999999`(GPU concurrent CUDA 안전)이다.
