#!/bin/bash
# PA01 CUDA tuning experiment: block size + pinned H2D staging (W14/W15 concepts).
# Run on Jetson Docker (student_19) or any machine with GPU + nvcc sm_53.
#
# Usage:
#   ./scripts/pa01_cuda_experiment_sweep.sh
#   ./scripts/pa01_cuda_experiment_sweep.sh --bag   # also run short PA02 L3 bag for top variants
#
# Output: data/bench/pa01_cuda_experiment_YYYYMMDD_HHMMSS/
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH_DIR="${ROOT}/benchmark"
MAP="${MAP:-${ROOT}/cartographer_parallel/cartographer_parallel/maps/0501.pgm}"
OUT_DIR="${OUT_DIR:-${ROOT}/data/bench/pa01_cuda_experiment_$(date +%Y%m%d_%H%M%S)}"
RUN_BAG=0
if [[ "${1:-}" == "--bag" ]]; then
  RUN_BAG=1
fi

mkdir -p "$OUT_DIR"
CSV="${OUT_DIR}/microbench_baglike.csv"
echo "variant,block_size,pinned,cuda_tag,n,cpu_ms,gpu_ms,max_diff,winner,verify" > "$CSV"

cd "$BENCH_DIR"

variants=(
  "baseline:128:0"
  "block256:256:0"
  "block512:512:0"
  "pinned128:128:1"
  "pinned256:256:1"
)

for spec in "${variants[@]}"; do
  name="${spec%%:*}"
  rest="${spec#*:}"
  blk="${rest%%:*}"
  pin="${rest##*:}"

  echo ""
  echo "======== ${name} block=${blk} pinned=${pin} ========"
  make -s clean microbench7 \
    PA01_CUDA_BLOCK_SIZE="${blk}" \
    PA01_CUDA_USE_PINNED="${pin}" \
    MAP="${MAP}"

  verify_out="$(./microbench7 --map "${MAP}" --verify --warmup 5 --iters 30 2>&1)" || true
  echo "$verify_out" | tail -5
  if echo "$verify_out" | grep -q 'verify PASS'; then
    verify="PASS"
  else
    verify="FAIL"
  fi

  sweep_out="$(./microbench7 --map "${MAP}" --baglike --sweep --sweep-n-max 256 \
    --warmup 15 --iters 100 --gap-ms 0 2>&1)"
  line4="$(echo "$sweep_out" | awk -F, '$1==4 {print; exit}')"
  line256="$(echo "$sweep_out" | awk -F, '$1==256 {print; exit}')"
  echo "n=4   ${line4}"
  echo "n=256 ${line256}"

  for n in 4 256; do
    line="$(echo "$sweep_out" | awk -F, -v n="$n" '$1==n {print; exit}')"
    if [[ -z "$line" ]]; then
      continue
    fi
    cpu_ms="$(echo "$line" | cut -d, -f2)"
    gpu_ms="$(echo "$line" | cut -d, -f3)"
    diff="$(echo "$line" | cut -d, -f5)"
    win="$(echo "$line" | cut -d, -f6)"
    echo "${name},${blk},${pin},block${blk}$([ "$pin" = 1 ] && echo _pinned),${n},${cpu_ms},${gpu_ms},${diff},${win},${verify}" >> "$CSV"
  done

  cp -f microbench7 "${OUT_DIR}/microbench7_${name}" 2>/dev/null || true
done

echo ""
echo "=== microbench results: ${CSV} ==="
column -t -s, "$CSV" 2>/dev/null || cat "$CSV"

# Recommend best n=256 gpu_ms
best_line="$(awk -F, 'NR>1 && $5==256 && $7+0>0 {print $7,$1,$2,$3}' "$CSV" | sort -n | head -1)"
if [[ -n "$best_line" ]]; then
  echo ""
  echo "Best n=256 baglike gpu_ms: ${best_line}"
fi

if [[ "$RUN_BAG" -eq 1 && -d /root/catkin_ws ]]; then
  echo ""
  echo "======== bag runs (PA02 L3, score_all cumulative KPI) ========"
  BAG_CSV="${OUT_DIR}/bag_score_all.csv"
  echo "variant,block_size,pinned,score_all_cumulative_ms,calls,n256_cuda_avg_ms" > "$BAG_CSV"
  best_name="$(echo "$best_line" | awk '{print $2}')"
  best_blk="$(echo "$best_line" | awk '{print $3}')"
  best_pin="$(echo "$best_line" | awk '{print $4}')"
  bag_variants=("baseline:128:0")
  if [[ -n "$best_name" && "$best_name" != "baseline" ]]; then
    bag_variants+=("${best_name}:${best_blk}:${best_pin}")
  fi
  for bag_spec in "${bag_variants[@]}"; do
    bname="${bag_spec%%:*}"
    brest="${bag_spec#*:}"
    bblk="${brest%%:*}"
    bpin="${brest##*:}"
    echo "--- bag ${bname} block=${bblk} pinned=${bpin} ---"
    cd /root/catkin_ws
    source /opt/ros/melodic/setup.bash
    PA01_FLAGS=(-DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON -DPA02_OPT_LEVEL=3 -DPA01_GPU_THRESHOLD=256
                -DPA01_CUDA_BLOCK_SIZE="${bblk}")
    if [[ "$bpin" == "1" ]]; then
      PA01_FLAGS+=(-DPA01_CUDA_USE_PINNED=ON)
    fi
    catkin_make "${PA01_FLAGS[@]}"
    source devel/setup.bash
    TAG="pa01_cuda_${bname}"
    timeout 200 roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
      2>&1 | tee "${OUT_DIR}/${TAG}_run.log" || true
    grep -oE '\[score_all\][^[:cntrl:]]*' "${OUT_DIR}/${TAG}_run.log" > "${OUT_DIR}/${TAG}_clean.log" || true
    summary="${OUT_DIR}/${TAG}_summary.txt"
    grep -oE '\[score_all\][^[:cntrl:]]*' "${OUT_DIR}/${TAG}_run.log" | tail -1 > "$summary" || true
    python3 - "$summary" "${OUT_DIR}/${TAG}_clean.log" "$bname" "$bblk" "$bpin" "$BAG_CSV" <<'PY'
import re, sys, os
summary_path, clean_path, name, blk, pin, csv_path = sys.argv[1:7]
summary = open(summary_path).read() if os.path.isfile(summary_path) else ""
m = re.search(r'call=(\d+).*cumulative=([\d.]+) ms', summary)
calls = cumulative = 0
if m:
    calls = int(m.group(1))
    cumulative = float(m.group(2))
n256_t, n256_c = 0.0, 0
pat = re.compile(r'elapsed=([\d.]+) ms.*\bn=256\b.*path=cuda')
if os.path.isfile(clean_path):
    for line in open(clean_path):
        mm = pat.search(line)
        if mm:
            n256_t += float(mm.group(1))
            n256_c += 1
n256_avg = (n256_t / n256_c) if n256_c else 0.0
with open(csv_path, 'a') as out:
    out.write(f"{name},{blk},{pin},{cumulative:.3f},{calls},{n256_avg:.3f}\n")
print(f"  {name} score_all cumulative={cumulative:.1f} ms n256_cuda_avg={n256_avg:.3f} ms")
PY
  done
  echo "=== bag CSV: ${BAG_CSV} ==="
  column -t -s, "$BAG_CSV" 2>/dev/null || cat "$BAG_CSV"
elif [[ "$RUN_BAG" -eq 1 ]]; then
  echo "WARN: --bag requested but /root/catkin_ws not found; microbench only."
fi

echo ""
echo "Done. Output dir: ${OUT_DIR}"
