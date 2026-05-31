#!/usr/bin/env python3
"""PA02 profile log analyzer: experimental bottleneck decomposition and target selection.

Reads pa02_bag_profile.sh outputs (*_<tag>_clean.log, *_summary.txt) and writes
<RUN>_bottleneck.txt with data-driven justification for the three PA02 targets:
  make_cand, FastMatcher::Score, FastMatcher::Branch

Usage:
  python3 scripts/pa02_analyze_profile.py [RUN] [--data-dir DIR]
  python3 scripts/pa02_analyze_profile.py pa02_l0_profile --data-dir data/pa02
"""
import argparse
import os
import re
import sys

TAGS = ["match", "score_all", "Score", "Branch", "make_cand", "MakeLowCands"]
PA02_TARGETS = ["make_cand", "Score", "Branch"]


class TagStats(object):
    def __init__(self):
        self.count = 0
        self.elapsed_sum_ms = 0.0
        self.last_cumulative_ms = 0.0
        self.last_call = 0
        self.strata = {}


def parse_log_line(line):
    out = {}
    em = re.search(r"elapsed=([0-9.]+) ms", line)
    if em:
        out["elapsed_ms"] = em.group(1)
    cm = re.search(r"cumulative=([0-9.]+) ms", line)
    if cm:
        out["cumulative_ms"] = cm.group(1)
    call_m = re.search(r"call=([0-9]+)", line)
    if call_m:
        out["call"] = call_m.group(1)
    for key in ("depth", "n_cand", "grid_span", "n_scans", "cand_in", "child_gen"):
        m = re.search(r"%s=([^\s|]+)" % key, line)
        if m:
            out[key] = m.group(1)
    return out


def load_tag(data_dir, run, tag):
    path = os.path.join(data_dir, "%s_%s_clean.log" % (run, tag))
    stats = TagStats()
    if not os.path.isfile(path):
        return stats

    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            p = parse_log_line(line)
            if "elapsed_ms" not in p:
                continue
            elapsed = float(p["elapsed_ms"])
            stats.count += 1
            stats.elapsed_sum_ms += elapsed
            if "cumulative_ms" in p:
                stats.last_cumulative_ms = float(p["cumulative_ms"])
            if "call" in p:
                stats.last_call = int(p["call"])

            key = None
            if tag == "Branch" and "depth" in p:
                key = "depth=%s" % p["depth"]
            elif tag == "Score" and "n_cand" in p:
                key = "n_cand=%s" % p["n_cand"]
            elif tag == "make_cand" and "grid_span" in p:
                key = "grid_span=%s" % p["grid_span"]
            elif tag == "Branch" and "cand_in" in p:
                key = "cand_in=%s" % p["cand_in"]

            if key:
                bucket = stats.strata.setdefault(key, {"count": 0.0, "elapsed_sum_ms": 0.0})
                bucket["count"] += 1
                bucket["elapsed_sum_ms"] += elapsed

    return stats


def pct(part, whole):
    # type: (float, float) -> float
    return 100.0 * part / whole if whole > 0 else 0.0


def fmt_ms(v):
    return "{:,.1f}".format(v)


def top_strata(strata, n=5):
    # type: (Dict[str, Dict[str, float]], int) -> List[Tuple[str, Dict[str, float]]]
    return sorted(strata.items(), key=lambda kv: -kv[1]["elapsed_sum_ms"])[:n]


def build_target_evidence(tag, stats, match_ms, score_all_ms, score_orch_ms):
    # type: (str, TagStats, float, float, float) -> Dict[str, object]
    cum = stats.last_cumulative_ms or stats.elapsed_sum_ms
    evidence: Dict[str, object] = {
        "tag": tag,
        "cumulative_ms": cum,
        "calls": stats.last_call or stats.count,
        "pct_of_match": pct(cum, match_ms),
        "elapsed_sum_ms": stats.elapsed_sum_ms,
    }

    if tag == "make_cand":
        evidence["role"] = "후보 격자 (cx,cy) 생성 — B&B coarse 탐색 공간"
        evidence["selection_reason"] = (
            "match의 {:.1f}%로 절대 비중은 작으나, "
            "호출 {:,}회·격리 microbench 가능·side effect 적음 → Phase 1 검증 타깃".format(
                pct(cum, match_ms), stats.count
            )
        )
        evidence["top_strata"] = top_strata(stats.strata, 3)
    elif tag == "Score":
        evidence["role"] = "스캔별 score_all 호출 + sort — B&B 점수 평가 오케스트레이션"
        evidence["score_orchestration_ms"] = score_orch_ms
        evidence["pct_of_score"] = pct(score_orch_ms, cum)
        evidence["selection_reason"] = (
            "Score_orchestration={} ms "
            "(Score cumulative의 {:.1f}%, match의 {:.1f}%). "
            "n_cand stratum에서 coarse(3840) vs leaf(4) 이중 regime → Phase 3 실험 대상".format(
                fmt_ms(score_orch_ms), pct(score_orch_ms, cum), pct(score_orch_ms, match_ms)
            )
        )
        evidence["top_strata"] = top_strata(stats.strata, 5)
    elif tag == "Branch":
        evidence["role"] = "Branch-and-Bound 재귀·가지치기·child split (내부 Score 포함)"
        evidence["selection_reason"] = (
            "Branch cumulative={} ms (match의 {:.1f}%). "
            "depth stratum으로 coarse(depth=3) 집중 확인 → Phase 2 대상".format(
                fmt_ms(cum), pct(cum, match_ms)
            )
        )
        evidence["top_strata"] = top_strata(stats.strata, 5)
    return evidence


def render_report(run, data_dir, stats, env_path):
    match_ms = stats["match"].last_cumulative_ms
    score_all_ms = stats["score_all"].last_cumulative_ms
    score_ms = stats["Score"].last_cumulative_ms
    branch_ms = stats["Branch"].last_cumulative_ms
    make_cand_ms = stats["make_cand"].last_cumulative_ms
    makelow_ms = stats["MakeLowCands"].last_cumulative_ms

    score_orch_ms = stats["Score"].elapsed_sum_ms - stats["score_all"].elapsed_sum_ms
    matcher_scope_ms = match_ms - score_all_ms
    makelow_wrap_ms = makelow_ms - make_cand_ms

    lines = []
    lines.append("# PA02 bottleneck analysis - %s" % run)
    lines.append("# data_dir: %s" % data_dir)
    lines.append("")

    if env_path and os.path.isfile(env_path):
        lines.append("## Build / run env (excerpt)")
        with open(env_path, "r", encoding="utf-8", errors="replace") as ef:
            for raw in ef:
                raw = raw.rstrip("\n")
                if any(k in raw for k in ("PA01_", "PA02_", "ROS_", "RUN=", "date")):
                    lines.append(raw)
        lines.append("")

    lines.append("## A. Module KPI decomposition")
    lines.append("")
    lines.append("| metric | ms | % of match |")
    lines.append("|--------|---:|-----------:|")
    rows_a = [
        ("[match] cumulative", match_ms, 100.0),
        ("[score_all] cumulative (PA01 fixed)", score_all_ms, pct(score_all_ms, match_ms)),
        ("matcher_scope (match − score_all)", matcher_scope_ms, pct(matcher_scope_ms, match_ms)),
    ]
    for name, ms, p in rows_a:
        lines.append("| %s | %s | %.1f%% |" % (name, fmt_ms(ms), p))
    lines.append("")
    lines.append(
        "**Conclusion:** PA02 optimizes matcher_scope **%s ms** "
        "(%.1f%% of match); score_all held fixed at **%s ms**."
        % (fmt_ms(matcher_scope_ms), pct(matcher_scope_ms, match_ms), fmt_ms(score_all_ms))
    )
    lines.append("")

    lines.append("## B. Score: kernel vs orchestration")
    lines.append("")
    lines.append("| metric | ms | note |")
    lines.append("|--------|---:|------|")
    lines.append("| [Score] cumulative | %s | includes Branch-nested calls |" % fmt_ms(score_ms))
    lines.append("| Σ score_all.elapsed | %s | all from Score() |" % fmt_ms(stats["score_all"].elapsed_sum_ms))
    lines.append(
        "| **Score_orchestration** | **%s** | **%.1f%% of Score** |"
        % (fmt_ms(score_orch_ms), pct(score_orch_ms, score_ms))
    )
    lines.append("")
    lines.append("### Score by n_cand (elapsed sum)")
    lines.append("")
    lines.append("| n_cand | calls | elapsed sum (ms) | avg (ms/call) |")
    lines.append("|--------|------:|-----------------:|--------------:|")
    for key, b in top_strata(stats["Score"].strata, 10):
        n = int(b["count"])
        s = b["elapsed_sum_ms"]
        avg = s / n if n else 0.0
        lines.append("| %s | %s | %s | %.3f |" % (key.replace("n_cand=", ""), "{:,}".format(n), fmt_ms(s), avg))
    lines.append("")

    lines.append("## C. Branch by depth (elapsed sum, includes nested Score)")
    lines.append("")
    lines.append("| depth | calls | elapsed sum (ms) | avg (ms/call) |")
    lines.append("|-------|------:|-----------------:|--------------:|")
    for key, b in sorted(stats["Branch"].strata.items(), key=lambda kv: int(kv[0].split("=")[1])):
        n = int(b["count"])
        s = b["elapsed_sum_ms"]
        avg = s / n if n else 0.0
        lines.append("| %s | %s | %s | %.3f |" % (key.replace("depth=", ""), "{:,}".format(n), fmt_ms(s), avg))
    lines.append("")

    lines.append("## D. make_cand / MakeLowCands")
    lines.append("")
    lines.append("| tag | cumulative (ms) | % of match | calls |")
    lines.append("|-----|----------------:|-----------:|------:|")
    for tag, ms in [("make_cand", make_cand_ms), ("MakeLowCands", makelow_ms)]:
        c = stats[tag].last_call or stats[tag].count
        lines.append("| [%s] | %s | %.1f%% | %s |" % (tag, fmt_ms(ms), pct(ms, match_ms), "{:,}".format(c)))
    lines.append("| MakeLowCands wrapper (est.) | %s | %.1f%% | — |" % (fmt_ms(makelow_wrap_ms), pct(makelow_wrap_ms, match_ms)))
    lines.append("")
    if stats["make_cand"].strata:
        lines.append("### make_cand by grid_span (top 5)")
        lines.append("")
        lines.append("| grid_span | calls | elapsed sum (ms) |")
        lines.append("|-----------|------:|-----------------:|")
        for key, b in top_strata(stats["make_cand"].strata, 5):
            lines.append("| %s | %s | %s |" % (key.replace("grid_span=", ""), "{:,}".format(int(b["count"])), fmt_ms(b["elapsed_sum_ms"])))
        lines.append("")

    lines.append("## E. PA02 target selection (experiment-based)")
    lines.append("")
    lines.append(
        "Three targets are **not** chosen by reading loops; they map to distinct "
        "matcher stages with measured cost in this bag run."
    )
    lines.append("")

    excluded = [
        ("score_all", score_all_ms, "PA01 scope — fixed L9 + GPU threshold=256"),
        ("MakeScans/MakeBounds/ToOut", None, "not separately tagged; remainder after tagged costs"),
    ]
    lines.append("### Excluded from PA02 optimization")
    lines.append("")
    for name, ms, reason in excluded:
        extra = " (%s ms)" % fmt_ms(ms) if ms is not None else ""
        lines.append("- **%s**%s: %s" % (name, extra, reason))
    lines.append("")

    lines.append("### Selected targets (ranked by experimental relevance)")
    lines.append("")

    target_rows = []
    for i, tag in enumerate(PA02_TARGETS, start=1):
        ev = build_target_evidence(tag, stats[tag], match_ms, score_all_ms, score_orch_ms)
        phase = { "make_cand": 1, "Score": 3, "Branch": 2 }[tag]
        target_rows.append((tag, phase, ev))

    # Rank by measurable PA02 impact proxy
    impact_rank = sorted(
        target_rows,
        key=lambda t: (
            t[2].get("score_orchestration_ms", 0)
            if t[0] == "Score"
            else t[2]["cumulative_ms"]
        ),
        reverse=True,
    )

    lines.append("| rank | target | Phase | cumulative (ms) | % match | experimental basis |")
    lines.append("|-----:|--------|------:|----------------:|--------:|--------------------|")
    for rank, (tag, phase, ev) in enumerate(impact_rank, start=1):
        basis = ev["selection_reason"][:80] + ("…" if len(ev["selection_reason"]) > 80 else "")
        lines.append(
            "| %d | `%s` | %d | %s | %.1f%% | %s |"
            % (rank, tag, phase, fmt_ms(ev["cumulative_ms"]), ev["pct_of_match"], basis)
        )
    lines.append("")

    for tag, phase, ev in target_rows:
        lines.append("### Target: `%s` (Phase %d)" % (tag, phase))
        lines.append("")
        lines.append("- **Role:** %s" % ev["role"])
        lines.append(
            "- **Measured:** cumulative=%s ms, calls=%s, %.1f%% of match"
            % (fmt_ms(ev["cumulative_ms"]), "{:,}".format(ev["calls"]), ev["pct_of_match"])
        )
        lines.append("- **Selection:** %s" % ev["selection_reason"])
        if ev.get("top_strata"):
            lines.append("- **Hot strata:**")
            for key, b in ev["top_strata"]:
                n = int(b["count"])
                s = b["elapsed_sum_ms"]
                lines.append("  - %s: %s calls, %s ms elapsed sum" % (key, "{:,}".format(n), fmt_ms(s)))
        lines.append("")

    lines.append("## F. Implementation order (not impact order)")
    lines.append("")
    lines.append("| Phase | target | why this order |")
    lines.append("|------:|--------|----------------|")
    lines.append(
        "| 1 | make_cand | smallest side effects; validates experiment pipeline |"
    )
    lines.append(
        "| 2 | Branch | depth=3 stratum largest B&B cost; after Phase 1 regression check |"
    )
    lines.append(
        "| 3 | Score | Score_orchestration + dual n_cand regime; CPU/GPU choice needs sweep |"
    )
    lines.append("")

    lines.append("## G. Regression check (after each Phase)")
    lines.append("")
    lines.append("- `[score_all]` cumulative must stay near **%s ms** (±few %%)" % fmt_ms(score_all_ms))
    lines.append("- `[match]` cumulative is module KPI (L0 baseline **%s ms**)" % fmt_ms(match_ms))
    lines.append("")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="PA02 profile bottleneck analyzer")
    parser.add_argument("run", nargs="?", default="pa02_l0_profile", help="RUN tag prefix")
    parser.add_argument(
        "--data-dir",
        default="data/pa02",
        help="Directory containing <RUN>_<tag>_clean.log files",
    )
    parser.add_argument(
        "-o", "--output",
        default="",
        help="Output path (default: <data-dir>/<RUN>_bottleneck.txt)",
    )
    args = parser.parse_args()

    data_dir = args.data_dir
    run = args.run
    out_path = args.output if args.output else os.path.join(data_dir, "%s_bottleneck.txt" % run)

    missing = [
        t for t in TAGS
        if not os.path.isfile(os.path.join(data_dir, "%s_%s_clean.log" % (run, t)))
    ]
    if missing:
        print("ERROR: missing clean logs for tags: %s" % ", ".join(missing), file=sys.stderr)
        print("  expected under: %s/%s_<tag>_clean.log" % (data_dir, run), file=sys.stderr)
        return 1

    stats = dict((tag, load_tag(data_dir, run, tag)) for tag in TAGS)
    if stats["match"].last_cumulative_ms <= 0:
        print("ERROR: [match] cumulative is zero — empty or corrupt logs?", file=sys.stderr)
        return 1

    env_path = os.path.join(data_dir, "%s_env.txt" % run)
    report = render_report(run, data_dir, stats, env_path if os.path.isfile(env_path) else None)

    out_dir = os.path.dirname(out_path)
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    with open(out_path, "w", encoding="utf-8") as out_f:
        out_f.write(report)

    sys.stdout.write(report.encode(sys.stdout.encoding or "utf-8", errors="replace").decode(sys.stdout.encoding or "utf-8", errors="replace"))
    sys.stdout.write("\n=== wrote %s ===\n" % out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
