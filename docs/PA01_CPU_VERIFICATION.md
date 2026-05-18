# PA01 CPU opt 0~5 검증·분석 (log + summary)

## 1. 빌드·실행 검증 (summary + clean.log)

| Level | `opt=` 태그 | 로그 줄 수 | 태그 일치 |
|-------|-------------|-----------|-----------|
| 0 | baseline | 27754 | OK |
| 1 | opt1_licm | 27873 | OK |
| 2 | opt2_loop_interchange | 41261 | OK |
| 3 | opt3_prefetch | 40998 | OK |
| 4 | opt4_branchless | 31631 | OK |
| 5 | opt5_all_cpu | 31672 | OK |

**결론:** 0~5 코드는 **의도한 최적화 단계별로 컴파일·실행됨** (`opt=` / `level=` 일치).

---

## 2. summary 지표 (마지막 줄)

| Level | calls | cumulative (ms) | avg (ms/call) | vs baseline avg |
|-------|-------|-----------------|---------------|-------------------|
| 0 baseline | 27754 | 86824.5 | **3.128** | 1.00× |
| 1 LICM | 27873 | 86974.1 | 3.120 | 1.00× (~0.3%) |
| 2 loop interchange | 41261 | 82475.6 | **1.999** | **1.56×** |
| 3 prefetch | 40998 | 82510.5 | 2.013 | 1.55× |
| 4 branchless | 31631 | 85703.5 | 2.709 | 1.15× |
| 5 all_cpu | 31672 | 85542.3 | 2.701 | 1.16× |

※ `calls`가 run마다 다름 → bag 동일해도 **매처가 더 많이 score_all을 호출**한 run(특히 opt2/3)이 있음.  
**총 cumulative(ms)** 와 **n별 합산**으로 교차 검증 권장.

---

## 3. clean.log 심층: `n=4` vs `n=256` (elapsed 합산, ms)

| Run | n=4 횟수 | n=4 합계 ms | n=256 횟수 | n=256 합계 ms | n=256 비중 |
|-----|----------|-------------|------------|---------------|------------|
| baseline | 19601 | 3575 | 7479 | **82952** | ~96% |
| opt1 | 19632 | 3558 | 7516 | 83193 | ~96% |
| opt2 | 29094 | 3536 | 11185 | **78734** | ~96% |
| opt3 | 28922 | 4060 | 11027 | 78165 | ~95% |
| opt4 | 22378 | 3618 | 8452 | 81794 | ~96% |
| opt5 | 22423 | 3922 | 8443 | 81372 | ~95% |

### 통찰

1. **병목은 `n=256` 호출** (전체 score_all 시간의 ~95% 이상).
2. **opt1 (LICM):** n=4·n=256 거의 동일 → 예상대로 미미.
3. **opt2 (루프 교환):** n=256 합계 **82952 → 78734 ms (~5.1%↓)** — **유일하게 큰 이득**.
4. **opt3 (prefetch):** n=256은 약간 개선, n=4는 오히려 증가 → 프리페치는 Jetson에서 **불안정/해로울 수 있음**.
5. **opt4/5:** n=256 합계가 opt2보다 큼 + 호출 수 적음 → **분기 마스크·프리페치 조합은 채택하지 않음**.

### 점수 정확성

- 모든 level은 **동일 수식** `sum(grid) / (255*p)` (경계 밖 0).  
- 구현만 다르고 **in-bounds에서 동일 산술** → SLAM 점수는 동일해야 함 (별도 float diff 검증은 opt6에서 샘플 로그 가능).

---

## 4. opt6 v1 측정 (참고 — best 아님)

| | opt2 | opt6 v1 |
|--|------|---------|
| summary avg | **1.999** ms/call | 2.543 ms/call |
| cumulative | 82476 ms | 84717 ms |
| `openmp` | — | **0** (미링크) |
| `path` | — | 전부 `interchange` |

**원인:** Jetson에서 OpenMP 미활성 + 힙 `vector` sums + opt2와 다른 경계 처리.  
**v2 수정:** opt2 동일 interchange, 스택 sums, 행 포인터, CMake `-fopenmp` 폴백.

---

## 5. Level 6 설계 근거 (`PA01_OPT_LEVEL=6`)

| 채택 | 기각 (0~5 실험) |
|------|-----------------|
| LICM `inv_denom` | prefetch (opt3) |
| 루프 교환 (`n` 작을 때) | branchless mask만 (opt4) |
| `n≥64` → **후보(i) 병렬 OpenMP** | opt5 전체 조합 |
| unsigned bounds 검사 | — |

- **OpenMP:** `n`이 작으면(4) `if(n>=64)`로 **직렬 interchange** — 1081× parallel region 생성 방지.  
- **`n=256`:** 후보별로 `j` 루프를 담당 → 4코어 Jetson에서 유리.

---

## 5. 측정·grep 워크플로 (level 6)

```bash
catkin_make -DPA01_OPT_LEVEL=6
source devel/setup.bash
export RUN=opt6_best ROS_IP=192.168.0.104

roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
  2>&1 | tee ~/pa01_${RUN}_run.log

grep -oE '\[score_all\][^\\n]*' ~/pa01_${RUN}_run.log > ~/pa01_${RUN}_clean.log
grep -oE '\[score_all\][^\\n]*' ~/pa01_${RUN}_run.log | tail -1 > ~/pa01_${RUN}_summary.txt
```

PC:

```bash
ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_opt6_best_summary.txt" \
  > ~/SME2009_HPDA/PA01/data/pa01_opt6_best_summary.txt
```

기대 summary: `opt=opt6_best level=6 | path=interchange|omp_cand | openmp=0|1`
