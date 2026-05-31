=== PA02 Phase 3 hybrid sweep ===
stamp=20260531_151643
out=/root/catkin_ws/src/hpda/data/bench/pa02_phase3_hybrid_20260531_151643
warmup=3 iters=15 bag=true

--- microbench (GPU build, full Match) ---
  micro legacy_gpu: L2 T=256 avg_ms=57.3569 score=0.1265
  micro hybrid_prod: L3 T=256 avg_ms=46.7379 score=0.1265
  micro cpu_score: L3 T=999999 avg_ms=36.5654 score=0.1265
  micro gpu_aggr: L3 T=64 avg_ms=35.5947 score=0.1265
  micro legacy_cpu: L2 T=999999 avg_ms=37.2256 score=0.1265

--- microbench CSV ---
variant,pa02_level,gpu_threshold,avg_ms,last_score,iters,warmup
legacy_gpu,L2,256,57.3569,0.1265,15,3
hybrid_prod,L3,256,46.7379,0.1265,15,3
cpu_score,L3,999999,36.5654,0.1265,15,3
gpu_aggr,L3,64,35.5947,0.1265,15,3
legacy_cpu,L2,999999,37.2256,0.1265,15,3

--- bag KPI (requires ROS master) ---
  (skip bag hybrid_prod — using existing pa02_l3_profile)
  bag cpu_score: building L3 T=999999 tag=pa02_hybrid_cpu_score ...
  bag cpu_score: match=91749.655 score_all=47325.259 Score=66875.154 best=0.783
  bag gpu_aggr: building L3 T=64 tag=pa02_hybrid_gpu_aggr ...
  bag gpu_aggr: match=87713.448 score_all=35151.975 Score=57342.184 best=0.783
  (skip bag legacy_gpu — using existing pa02_l2_profile)

--- bag CSV ---
variant,pa02_level,gpu_threshold,match_ms,score_all_ms,score_ms,best_score
hybrid_prod,L3,256,87865.755,34785.592,57074.667,0.783
cpu_score,L3,999999,91749.655,47325.259,66875.154,0.783
gpu_aggr,L3,64,87713.448,35151.975,57342.184,0.783
legacy_gpu,L2,256,89925.609,35043.199,60434.129,0.783

=== done: /root/catkin_ws/src/hpda/data/bench/pa02_phase3_hybrid_20260531_151643 ===
