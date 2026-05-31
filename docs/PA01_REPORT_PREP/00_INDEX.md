# PA01 보고서 작성용 문서 인덱스

`docs/scrap.md`에 붙여넣었던 가이드·AI 답변·명령·측정 결과를 **보고서 작성 순서**에 맞게 분할·통합한 문서 모음입니다.

## 보고서 필수 항목 (과제 요약)

| 항목 | 내용 |
|------|------|
| 실행 환경 | **Jetson Nano** (`student_19`, Docker `student_19`) |
| 대상 함수 | `score_all()` 단일 함수 |
| 1) CPU 고속화 | 메모리·루프·OpenMP 등 (level 0~6) |
| 2) GPU 고속화 | CUDA 하이브리드 (level 7) |
| 3) 비교 분석 | baseline vs CPU 최종 vs GPU, **cumulative**·구간별(n=4/n=256) |
| 제출 | PDF **최대 6페이지**, 핵심 결과 위주 |

## 문서 목록

| 파일 | 용도 |
|------|------|
| [01_ASSIGNMENT_AND_PROBLEM.md](01_ASSIGNMENT_AND_PROBLEM.md) | 과제 정의, `score_all` 병목, 문제 정의·접근 |
| [02_ENVIRONMENT_AND_WORKFLOW.md](02_ENVIRONMENT_AND_WORKFLOW.md) | SSH·Docker·ROS·반복 실험 파이프라인 |
| [03_BASELINE_AND_MEASUREMENT.md](03_BASELINE_AND_MEASUREMENT.md) | 베이스라인 확보, chrono 측정, 로그 수집 |
| [04_TROUBLESHOOTING.md](04_TROUBLESHOOTING.md) | 증상별 원인·해결 (시간순·카테고리) |
| [05_CPU_OPTIMIZATION.md](05_CPU_OPTIMIZATION.md) | level 0~6 단계별 기법·명령·결과 |
| [06_GPU_CUDA_OPTIMIZATION.md](06_GPU_CUDA_OPTIMIZATION.md) | level 7 CUDA·하이브리드·트러블슈팅 |
| [07_COMPARISON_FOR_REPORT.md](07_COMPARISON_FOR_REPORT.md) | 보고서용 표·그래프·서술 포인트 |
| [08_REFERENCE_TIPS.md](08_REFERENCE_TIPS.md) | 벤치마크 팁, 리눅스/SSH, 모니터링, PA02 경계 |
| [../PA02_PROFILING_STRATEGY.md](../PA02_PROFILING_STRATEGY.md) | PA02 프로파일 로그·최적화 순서·Jetson 워크플로 |
| **[PA01_ADDITIONAL_EXPERIMENTS_20260531.md](../PA01_ADDITIONAL_EXPERIMENTS_20260531.md)** | **2026-05-31 추가 실험** (opt7~9, bag-like bench, threshold sweep, 최종 선택) |

## 기존 상세 문서 (참고)

| 경로 | 내용 |
|------|------|
| `docs/CPU_OPTIMIZATION_PLAN.md` | CPU level별 계획 |
| `docs/PA01_CPU_VERIFICATION.md` | 0~6 검증·n=4/n=256 분해 |
| `docs/PA01_DEVELOPMENT_LOG_PART2.md` | Part2 개발 로그 |
| `docs/PA01_DEVELOPMENT_LOG_GPU.md` | GPU 개발 로그 |
| `docs/PA01_GPU.md` | GPU 빌드 명령 |
| `docs/PA01_PROFILING_AND_WORKFLOW.md` | 프로파일링·워크플로 |
| `data/pa01_*_summary.txt` | Jetson 실측 마지막 줄 |
| `data/pa01_*_clean.log` | 전체 `[score_all]` 줄 |

## 권장 보고서 서술 순서

1. **문제 정의** → `01`
2. **환경·측정 방법** → `02`, `03`
3. **시행착오(요약)** → `04` (핵심 2~3건만 본문, 나머지 부록 느낌)
4. **CPU 최적화** → `05` + `07` 표
5. **GPU 최적화** → `06` + `07` 표
6. **비교·결론** → `07`

## 원본 스크랩

- `docs/scrap.md` — 분할 전 원본 (2684줄). 중복 내용은 본 디렉터리가 **정본**으로 사용.
