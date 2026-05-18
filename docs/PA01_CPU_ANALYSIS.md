# PA01 CPU 최적화 검증·분석 (level 0~5)

## 1. 로그·summary 정합성 검증

| Run | clean.log 줄 수 | 첫/끝 `opt=` | summary `level` | 판정 |
|-----|-----------------|--------------|-----------------|------|
| baseline | 27754 | baseline / 0 | 0 | OK |
| opt1_licm | 27873 | opt1_licm / 1 | 1 | OK |
| opt2_loop_interchange | 41261 | opt2 / 2 | 2 | OK |
| opt3_prefetch | 40998 | opt3 / 3 | 3 | OK |
| opt4_branchless | 31631 | opt4 / 4 | 4 | OK |
| opt5_all_cpu | 31672 | opt5 / 5 | 5 | OK |

**결론:** 0~5 단계 모두 **의도한 바이너리로 실행**되었고, grep·summary 추출 파이프라인도 정상이다.

> `call=` 수가 run마다 다름(27k~41k): bag 1회 기준으로도 로그 한 줄 = `score_all` 1회 호출이며, 호출 횟수 차이는 run 환경(노드 중복·bag 재생 등) 영향 가능. **단계 간 비교는 `avg ms/call`과 워크로드별 부분 평균을 함께 본다.**

---

## 2. Summary 비교 (마지막 줄)

| Level | opt | calls | cumulative (s) | **avg ms/call** | vs baseline |
|-------|-----|-------|------------------|-----------------|-------------|
| 0 | baseline | 27754 | 86.82 | **3.128** | 1.00× |
| 1 | opt1_licm | 27873 | 86.97 | **3.120** | 1.00× |
| 2 | opt2_loop_interchange | 41261 | 82.48 | **1.999** | **1.56×** |
| 3 | opt3_prefetch | 40998 | 82.51 | **2.013** | 1.55× |
| 4 | opt4_branchless | 31631 | 85.70 | **2.709** | 1.15× |
| 5 | opt5_all_cpu | 31672 | 85.54 | **2.701** | 1.16× |

- **LICM(1):** 거의 변화 없음 (예상대로).
- **루프 교환(2):** 가장 큰 이득. cumulative도 baseline보다 약 **4.3s** 적음.
- **프리페치(3):** 2보다 약간 느림 → Jetson에서 제거.
- **분기 마스크(4)·전체 합(5):** 2보다 크게 느림 → 제거.

---

## 3. 워크로드별 분해 (clean.log, `elapsed` per call)

| Level | n=4 avg | n=256 avg | n=256 비중(호출) |
|-------|---------|-----------|------------------|
| 0 baseline | 0.182 ms | **11.09 ms** | ~27% |
| 1 opt1 | 0.181 ms | 11.07 ms | ~27% |
| 2 opt2 | **0.122 ms** | **7.04 ms** | ~27% |
| 3 opt3 | 0.140 ms | 7.09 ms | ~27% |
| 4 opt4 | 0.162 ms | 9.68 ms | ~27% |
| 5 opt5 | 0.175 ms | 9.64 ms | ~27% |

**통찰**

1. 병목의 상당 부분은 **`n=256` (coarse search)** 호출. opt2는 여기서 **~37%** 단축.
2. **`n=4` (fine)** 도 opt2에서 **0.182→0.122 ms (~33%)** — 루프 교환·`px/py` 재사용이 양쪽에 유효.
3. 프리페치·분기 트릭은 n=256에서 오히려 악화 → **level 6에는 넣지 않음**.

---

## 4. level 6 설계 (`opt6_final_cpu`)

**베이스:** level 2 (LICM + j-i 루프 교환 + `sums[]` 누적)

**추가 (마이크로 최적화만)**

| 기법 | 이유 |
|------|------|
| `thread_local` `sums` 버퍼 재사용 | 매 호출 `vector` 할당 제거 |
| `row = grid + y*w; row[x]` | 인덱스 계산·주소 생성 감소 |
| `n==4` / `n==2` 전용 루프 (수동 unroll) | 실측에서 지배적 소규모 n |
| 프리페치·분기 마스크 **미사용** | 3~5에서 악화 확인 |

**로깅 (grep 호환)**

- 접두사 유지: `[score_all]`
- 추가 필드: `heavy=0|1` (n≥64), `peak_ms=` (run 누적 최대 1회 elapsed)

---

## 5. level 6 Jetson 측정 (TODO)

```bash
cd ~/catkin_ws
catkin_make -DPA01_OPT_LEVEL=6
source devel/setup.bash
export RUN=opt6_final_cpu ROS_IP=192.168.0.104

roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
  2>&1 | tee ~/pa01_${RUN}_run.log

grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log | tail -1 > ~/pa01_${RUN}_summary.txt
grep -oE '\[score_all\].*' ~/pa01_${RUN}_run.log > ~/pa01_${RUN}_clean.log
```

**성공 기준:** `opt=opt6_final_cpu level=6`, `avg ms/call` ≤ opt2 (목표 ~1.95 ms 이하).

---

## 6. CPU 단계 마무리 후

- PA01 보고: 본 문서 + opt6 summary
- PA02: CUDA `score_all` (동일 repo, `BUILD_CUDA_TASK` / `assignment_cuda.cu`)
