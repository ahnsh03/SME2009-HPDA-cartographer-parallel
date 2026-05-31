# 07. 비교 분석 (보고서용 표·서술)

PDF 6페이지 제한 → **표 2~3개 + 그래프 1개** + 짧은 분석 권장.

---

## 1. 보고서 3단 구조 매핑

| 과제 항목 | 대표 구현 | Jetson 데이터 파일 |
|-----------|-----------|-------------------|
| 1) CPU 고속화 | level **6** `opt6_best` | `data/pa01_opt6_best_summary.txt` |
| (과정) CPU ablation | level 0~5 | `data/pa01_opt*_summary.txt` |
| 2) GPU 고속화 | level **7** `opt7_gpu_hybrid` | `data/pa01_opt7_gpu_summary.txt` |
| 3) 비교 분석 | 0 vs 6 vs 7 | 본 문서 표 |

**baseline:** level **0** — GPU/CPU 공통 기준.

---

## 2. 핵심 비교 표 (보고서 메인)

### 2.1 세 버전 summary (동일 bag, Jetson)

| 구분 | Level | calls | cumulative (ms) | avg (ms/call) | cumulative vs baseline |
|------|-------|-------|-----------------|---------------|------------------------|
| **원본 (baseline)** | 0 | 27,754 | **86,824** | 3.128 | 1.00× |
| **CPU 최종** | 6 | 85,260 | **52,132** | 0.611 | **1.67× 빠름** |
| **GPU 하이브리드** | 7 | 133,496 | **42,864** | 0.321 | **2.03× 빠름** |

**원문 마지막 줄:**

```
# baseline
... cumulative=86824.499 ms / 27754 calls (avg=3.128 ms/call)

# CPU opt6
... cumulative=52132.304 ms / 85260 calls (avg=0.611 ms/call) | path=n4 | openmp=1

# GPU opt7
... cumulative=42864.140 ms / 133496 calls (avg=0.321 ms/call) | path=n4 | cuda=1
```

### 2.2 왜 avg만 쓰면 안 되는가 (보고서 필수 문단)

- 최적화 후 `score_all`이 빨라지면 SLAM이 **더 많은 매칭**을 수행 → **calls 증가**
- 예: baseline 27,754 → opt7 **133,496** calls
- 따라서 **총 부하 = cumulative(ms)** 가 “bag 동안 score_all이 차지한 CPU/GPU 시간”에 가깝고, **avg는 보조 지표**.

---

## 3. 병목 구간 비교 (n=256, 보고서 서브표)

전체의 ~95%가 **n=256** 구간에서 발생 (baseline clean.log 합산).

| 구분 | n=256 처리 | n=256 호출당 (대략) | 비고 |
|------|------------|---------------------|------|
| baseline | CPU 이중 루프 | ~11 ms (로그 상한) | |
| opt2 | CPU 루프 교환 | ~7 ms → 합계 5%↓ | ablation |
| opt6 | OpenMP `omp_cand` | **~2.0 ms** | 4코어 |
| opt7 | CUDA `path=cuda` | **~0.91 ms** | 1회 ~96 ms 워밍업 제외 |

**n=4 구간 (CPU 유지):**

| | opt6 | opt7 |
|--|------|------|
| 경로 | `path=n4` | `path=n4` (CPU) |
| avg | ~0.098 ms | ~0.088 ms |

→ GPU는 **n=4를 CPU에 맡긴 설계**가 데이터와 일치.

---

## 4. CPU ablation 축약 표 (본문 또는 부록)

| Level | 기법 | avg (ms/call) | 판정 |
|-------|------|---------------|------|
| 0 | baseline | 3.128 | 기준 |
| 1 | LICM | 3.120 | 기각 (~0%) |
| 2 | 루프 교환 | **1.999** | **채택** |
| 3 | +prefetch | 2.013 | 기각 |
| 4 | +branchless | 2.709 | 기각 |
| 5 | 전부 합침 | 2.701 | 기각 |
| 6 | OpenMP+dispatch | **0.611** | **CPU 최종** |

---

## 5. 하드웨어 관점 분석 포인트 (서술 템플릿)

### CPU (0 → 6)

1. **LICM (opt1):** 나눗셈 1회 제거 — 루프 대비 미미.
2. **루프 교환 (opt2):** `px[j], py[j]`를 스캔마다 1회, `grid` 접근 패턴 개선 — **Jetson에서 가장 큰 단일 CPU 기법**.
3. **prefetch/branchless (opt3~5):** 랜덤 `grid` 지배 → 오히려 악화 — **실험으로 기각**.
4. **OpenMP (opt6):** n=256에서 후보 병렬 — 4코어 A57, **n=256 avg ~3.5×**.
5. **dispatch:** n=4 전용 커널로 분기·할당 오버헤드 감소.

### GPU (6 → 7)

1. **병렬 단위:** 후보 `i` = GPU 스레드 (OpenMP와 동일 논리).
2. **이득:** n=256 구간 ms/call 감소, cumulative **52,132 → 42,864 ms**.
3. **한계:** n=4 다수 호출은 CPU; 호출마다 H2D/D2H; **첫 grid 업로드 ~96 ms**.
4. **하이브리드:** “GPU가 항상 이기지 않음” — **전체 avg**만 보면 misleading, **구간 분리**로 설명.

### Jetson Nano 제약 (한 단락)

- Maxwell GPU, 제한적 SM, unified memory 대역폭
- CPU 4코어 vs GPU 많은 스레드 — **n×p 랜덤 읽기**에 유리
- Docker CUDA 10.2, SM 5.3

---

## 6. 그래프 제안 (6페이지 내)

1. **막대 그래프:** cumulative (ms) — baseline / CPU6 / GPU7 (3막대)
2. **선택:** n=256 평균 elapsed — 동일 3종
3. **부록:** level 0~6 avg (ablation)

---

## 7. 정확도·공정성 체크리스트

- [ ] 동일 Jetson, 동일 launch, `ROS_IP=104`
- [ ] 측정 구간: chrono가 이중 루프(±CUDA 전송)만 포함
- [ ] 점수 수식 동일 (경계 밖 0)
- [ ] GPU 표 제목: **“하이브리드 level 7”** (GPU-only 아님)
- [ ] opt7 **버그 run**과 **성공 run** 구분
- [ ] load average / 측정 일시 (선택, `*_env.txt`)

---

## 8. 보고서 목차 예시 (6페이지)

1. **서론** — SLAM, `score_all` 병목, 목표 (0.5p)
2. **환경·측정** — Jetson, ROS, chrono, cumulative 정의 (0.5p)
3. **CPU 고속화** — ablation + level 6 + 표 (1.5p)
4. **GPU 고속화** — 하이브리드, 커널, 트러블슈팅 1줄 (1.5p)
5. **비교·분석** — 표 + n=256 + 하드웨어 해석 (1.5p)
6. **결론** — 시행착오 요약, 한계, PA02 (0.5p)

시행착오: `04_TROUBLESHOOTING.md`에서 ROS_IP, opt 레벨 불일치, CUDA memcpy만 본문에.

---

## 9. speedup 계산식

```
speedup_avg_vs_baseline = 3.128 / <avg_ms_per_call>
speedup_cumulative    = 86824 / <cumulative_ms>   # calls 다를 때 권장
```

**보고서 권장:** cumulative speedup **1.67× (CPU)**, **2.03× (GPU)** + n=256 구간 **~3.5× (CPU)**, **~2× (GPU vs CPU6 on n=256)**.
