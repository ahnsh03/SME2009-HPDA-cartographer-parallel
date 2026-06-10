# 제4장. Phase 1~2 CPU 최적화

> PA01 고정: L9 + `PA01_GPU_THRESHOLD=256`  
> 빌드: `PA02_OPT_LEVEL` 누적 (1 → 2)

---

## 4.1 Phase 1 — make_cand (L1)

### 4.1.1 대상과 가설

`make_cand(min_x, max_x, min_y, max_y, step, cx, cy)`는 탐색 창을 `step` 간격 격자로 훑어 후보 offset `(cx, cy)`를 `vector`에 추가한다. L0에서 match의 **1.3%**이지만 호출 **31,755회**, hot path `16×16=256` 후보.

**가설:** `vector::reserve`로 realloc을 제거하고, 큰 grid에서만 OpenMP를 켜면 `[make_cand]` cumulative가 줄어든다.

### 4.1.2 구현 (`score_all.cpp`, `PA02_OPT_LEVEL >= 1`)

```cpp
const int nx = (max_x - min_x) / step + 1;
const int ny = (max_y - min_y) / step + 1;
const int n_out = nx * ny;
cx->reserve(cx->size() + n_out);
cy->reserve(cy->size() + n_out);
// ... nested loop with optional OpenMP if n_out >= PA02_MAKE_CAND_OMP_MIN
```

### 4.1.3 OpenMP threshold sweep

bag hot path는 `n_out=256`. `PA02_MAKE_CAND_OMP_MIN`을 sweep한 결과:

| omp_min | avg_ms (16×16) | 판단 |
|--------:|---------------:|------|
| 999999 (serial) | **0.0020** | 기준 |
| 256 | 0.0042 (×2.1) | OMP 오버헤드 |
| **512** | **0.0020** | hot path에서 OMP **미발동** |

**채택:** `PA02_MAKE_CAND_OMP_MIN=512` — 실제 bag에서 OMP가 켜지지 않고, 큰 grid에서도 serial과 동률.

### 4.1.4 bag 결과

| Level | `[make_cand]` (ms) | `[match]` (ms) | `best_score` |
|------:|-------------------:|---------------:|-------------:|
| L0 | 1,216 | 90,482 | 0.783 |
| L1 | ~820 (L3 run 기준) | ~90,701 | 0.783 |

match KPI 영향은 **1% 미만**이나, `[score_all]` regression 없음 → Phase 2로 진행.

---

## 4.2 Phase 2 — Branch (L2)

### 4.2.1 대상과 가설

`Branch()`는 B&B 재귀의 핵심이다. L0에서 `[Branch]` cumulative **54,743 ms**, depth=3 stratum **34,679 ms**.

**가설:**
1. child `vector<Cand>` 매 재귀마다 heap 할당 → `thread_local` buffer reuse
2. bounds 밖 quadrant는 child 생성 생략 (empty skip)
3. `MakeLowCands`에 추정 `reserve( bounds.size() * 256 )`

### 4.2.2 구현 (`fast_matcher.cpp`, `PA02_OPT_LEVEL >= 2`)

**thread_local child buffer:**

```cpp
static thread_local std::vector<Cand> child;
auto fill_children = [&](const Cand& c, int* gen) {
  child.clear();
  child.reserve(4);
  for (const int dx : {0, half}) {
    if (c.x + dx > bounds[c.scan].max_x) continue;
    for (const int dy : {0, half}) {
      if (c.y + dy > bounds[c.scan].max_y) continue;
      // push child ...
    }
  }
};
```

**sibling OpenMP (CPU-only builds):**

```cpp
#if defined(PA01_HAS_OPENMP) && !defined(PA01_USE_GPU)
#pragma omp parallel for schedule(dynamic, 4)
  for (int i = 0; i < n_cand; ++i) { ... Score(...); Branch(...); }
#endif
```

### 4.2.3 Branch sibling OMP — CPU에서만 유효

CPU L6 microbench:

| branch_omp_min | match avg_ms | score |
|---------------:|-------------:|------:|
| 999999 (off) | 28.41 | 0.1265 |
| 128 | **22.47** | 0.1265 |

**−22%** — CPU-only 환경에서는 유리하다.

### 4.2.4 GPU build에서 OMP compile-time off

**문제:** `score_all_cuda.cu`의 `DeviceBuffers`는 **전역 singleton**이다.

```cpp
DeviceBuffers& Buffers() {
  static DeviceBuffers buf;  // d_grid, d_cx, d_score, stream — 1세트
  return buf;
}
```

여러 OMP 스레드가 동시에 `ScoreCandidates()`를 호출하면 같은 GPU 버퍼에 **data race**가 발생한다 (H2D 덮어쓰기, stream interleave, score corruption).

**대안 평가:**

| 방법 | 안전성 | bag 기대 |
|------|--------|----------|
| mutex 직렬화 | 안전 | OMP 이득 상쇄 |
| thread별 DeviceBuffers | 안전 | VRAM/SM 제한, 이득 미미 |
| compile-time off | 안전 | **채택** |

**채택:** `PA02_BRANCH_OMP_MIN=999999` + `#ifndef PA01_USE_GPU` compile guard.

`CMakeLists.txt`:

```cmake
# Branch sibling OMP (L2+, CPU-only). 999999=off (required for GPU score_all).
set(PA02_BRANCH_OMP_MIN "999999" ...)
```

### 4.2.5 bag 결과

| Level | `[match]` (ms) | `[Branch]` depth=3 sum (ms) | `best_score` |
|------:|---------------:|----------------------------:|-------------:|
| L0 | 90,482 | 34,679 | 0.783 |
| L2 | 89,926 | — | 0.783 |
| L3 | 87,866 | 32,448 | 0.783 |

L0→L2: match **−0.6%**. Branch buffer reuse로 재귀 할당 비용 감소. 정확도 유지.

---

## 4.3 Phase 1~2 누적 그림

![Fig. 6](figures/fig06_phase_progression.png)

| Level | 내용 | match (ms) | Δ vs L0 |
|------:|------|----------:|--------:|
| L0 | baseline | 90,482 | — |
| L1 | make_cand reserve | ~90,701 | +0.2% (노이즈) |
| L2 | Branch buffer reuse | 89,926 | −0.6% |
| L3 | Score bucket (제5장) | **87,866** | **−2.9%** |

Phase 1~2만으로는 match KPI **~1%** 수준. 본질적 이득은 Phase 3 Score_orchestration에서 발생한다.

---

## 4.4 Phase 1~2 회귀 검증

| 검증 항목 | L0 | L2 | 판정 |
|-----------|-----|-----|------|
| `[score_all]` cumulative | 33,814 ms | ~35,043 ms | ±3% 이내 ✓ |
| `best_score` | 0.783 | 0.783 | 동일 ✓ |
| `coarse_n` | 3840 | 3840 | 동일 ✓ |
| score_all path (cuda %) | ~81% | ~81% | GPU hybrid 유지 ✓ |

PA02 Phase 1~2는 **CPU 오케스트레이션만** 변경했으며 PA01 커널·dispatch를 건드리지 않았다.
