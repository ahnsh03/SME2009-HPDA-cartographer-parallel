# PA02 보고서 — 목차 및 작성 가이드

> **과목:** SME2009 고성능 데이터/코드 분석  
> **대상:** `cartographer_parallel` Fast Correlative Scan Matcher 오케스트레이션  
> **환경:** Jetson Nano `student_19`, Docker, ROS Melodic, CUDA 10.2  
> **KPI:** bag 1회 `[match]` cumulative (ms)  
> **최종 빌드:** `PA02_OPT_LEVEL=3` + PA01 L9 GPU `T=256`

---

## 문서 구성

| 장 | 파일 | 페이지 예산 | 핵심 메시지 |
|----|------|------------|------------|
| Abstract | [00_ABSTRACT.md](00_ABSTRACT.md) | 0.3p | bag KPI −2.9%, hybrid 유지, 측정 철학 |
| 제1장 | [01_introduction.md](01_introduction.md) | 1.0p | PA01 이후 남은 병목, 3함수, KPI 정의 |
| 제2장 | [02_environment_workflow.md](02_environment_workflow.md) | 1.0p | SSH·Git 워크플로, **bag 누적 측정 철학** |
| 제2장 부록 | [02b_gpu_crossover_discovery.md](02b_gpu_crossover_discovery.md) | 0.8p | **GPU crossover 발견·원인**, microbenchmark 용어 |
| 제2장 부록 | [02c_cpu_credibility_review.md](02c_cpu_credibility_review.md) | 0.5p | CPU bag 열세 = 구현 부족? **신뢰도 검토** |
| 제2장 부록 | [02d_system_os_openmp_limits.md](02d_system_os_openmp_limits.md) | 0.8p | **시스템·OS·OpenMP 구조적 한계** 상세 |
| 제3장 | [03_phase0_profiling.md](03_phase0_profiling.md) | 1.0p | L0 병목 분해, 타깃 선정 근거 |
| 제4장 | [04_phase1_2_optimization.md](04_phase1_2_optimization.md) | 1.0p | make_cand, Branch CPU 최적화 |
| 제5장 | [05_phase3_hybrid.md](05_phase3_hybrid.md) | 1.0p | Score bucket, hybrid sweep, microbench≠bag |
| 제6장 | [06_upstream_experiments.md](06_upstream_experiments.md) | 0.8p | Cartographer 참고, ShrinkToFit 기각 |
| 제7장 | [07_conclusion.md](07_conclusion.md) | 0.7p | 종합, 한계, PA01+PA02 전체 그림 |

---

## 시각화 자료 (`figures/`)

| 그림 | 파일 | 사용 장 |
|------|------|--------|
| Fig. 1 | `fig01_l0_match_decomposition.png` | 제3장 — L0 시간 분해 |
| Fig. 2 | `fig02_l0_vs_l3_cumulative.png` | 제7장 — L0 vs L3 비교 |
| Fig. 3 | `fig03_hybrid_sweep_bag.png` | 제5장 — hybrid 결정 |
| Fig. 4 | `fig04_microbench_vs_bag.png` | 제2·5장 — **측정 철학 핵심** |
| Fig. 5 | `fig05_shrinktofit_failure.png` | 제6장 — upstream 실패 실험 |
| Fig. 6 | `fig06_phase_progression.png` | 제4·7장 — Phase 누적 |
| Fig. 7 | `fig07_feedback_calls.png` | 제2·5장 — 호출 수 피드백 |
| Fig. 8 | `fig08_crossover_microbench_vs_bag.png` | 제2장 부록 — **crossover 2048→256** |
| Fig. 9 | `fig09_crossover_causes.png` | 제2장 부록 — crossover 원인 |

---

## PDF 병합 순서

1. 표지 (학번·이름·과목·날짜)
2. `00_ABSTRACT.md`
3. 제1장 → 제7장 순서
4. 부록 (선택): `scripts/pa02_bag_profile.sh` 요약, CMake 플래그 표

---

## 관련 원본 데이터

| 경로 | 내용 |
|------|------|
| `data/pa02/pa02_l0_profile_*` | L0 baseline |
| `data/pa02/pa02_l3_profile_*` | L3 최종 |
| `data/bench/pa02_phase3_hybrid_20260531_151643/` | hybrid sweep |
| `data/bench/pa02_structural_20260531_155906/` | ShrinkToFit 실패 |
| `data/bench/pa02_structural_20260531_161217/` | reserve/tweak 실험 |

## 관련 개발 문서

- `docs/PA02_PROFILING_STRATEGY.md` — Phase 0 전략
- `docs/PA02_FAQ_AND_ANALYSIS.md` — 기술 FAQ
- `docs/PA01_PA02_OPTIMIZATION_COMPLETE.md` — PA01+PA02 통합 타임라인
- `docs/PA01_REPORT_DRAFT.md` — PA01 보고서 (연속성 유지)
