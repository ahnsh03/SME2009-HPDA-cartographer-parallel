#!/usr/bin/env python3
"""Summarize score_all dispatch paths (n4 / cuda / omp) from PA02 bag clean logs.

Usage:
  python3 scripts/pa02_analyze_score_paths.py pa02_l3_profile --data-dir data/pa02
  python3 scripts/pa02_analyze_score_paths.py --all --data-dir data/pa02
"""
import argparse
import glob
import os
import re
import sys


def analyze_file(path):
    path_re = re.compile(r"path=([^\s|]+)")
    n_re = re.compile(r"\bn=([0-9]+)\b")
    elapsed_re = re.compile(r"elapsed=([0-9.]+) ms")
    cuda_re = re.compile(r"cuda=([01])")

    by_path = {}
    by_n = {}
    total_calls = 0
    total_elapsed = 0.0

    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for line in fh:
            pm = path_re.search(line)
            if not pm:
                continue
            path_tag = pm.group(1)
            elapsed = float(elapsed_re.search(line).group(1)) if elapsed_re.search(line) else 0.0
            n_val = int(n_re.search(line).group(1)) if n_re.search(line) else -1
            cuda = cuda_re.search(line).group(1) if cuda_re.search(line) else "?"

            bucket = by_path.setdefault(path_tag, {"calls": 0, "elapsed_ms": 0.0, "cuda": cuda})
            bucket["calls"] += 1
            bucket["elapsed_ms"] += elapsed

            if n_val >= 0:
                nb = by_n.setdefault(n_val, {"calls": 0, "elapsed_ms": 0.0, "paths": {}})
                nb["calls"] += 1
                nb["elapsed_ms"] += elapsed
                nb["paths"][path_tag] = nb["paths"].get(path_tag, 0) + 1

            total_calls += 1
            total_elapsed += elapsed

    return {
        "file": path,
        "total_calls": total_calls,
        "total_elapsed_ms": total_elapsed,
        "by_path": by_path,
        "by_n": by_n,
    }


def format_report(run, stats):
    lines = [
        "# score_all path breakdown — %s" % run,
        "",
        "| path | calls | elapsed sum (ms) | % elapsed |",
        "|------|------:|-----------------:|----------:|",
    ]
    for path_tag in sorted(stats["by_path"].keys()):
        b = stats["by_path"][path_tag]
        pct = 100.0 * b["elapsed_ms"] / stats["total_elapsed_ms"] if stats["total_elapsed_ms"] else 0.0
        lines.append("| %s | %s | %.1f | %.1f%% |" % (path_tag, b["calls"], b["elapsed_ms"], pct))
    lines.append("")
    lines.append("**Hot n values (bag-like):**")
    lines.append("")
    lines.append("| n | calls | elapsed sum (ms) | dominant path |")
    lines.append("|--:|------:|-----------------:|---------------|")
    for n_val in sorted(stats["by_n"].keys()):
        if n_val not in (2, 4, 256, 3840):
            continue
        b = stats["by_n"][n_val]
        dom = max(b["paths"].items(), key=lambda kv: kv[1])[0] if b["paths"] else "?"
        lines.append("| %s | %s | %.1f | %s |" % (n_val, b["calls"], b["elapsed_ms"], dom))
    lines.append("")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run", nargs="?", default=None)
    ap.add_argument("--data-dir", default="data/pa02")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    runs = []
    if args.all:
        for p in sorted(glob.glob(os.path.join(args.data_dir, "*_score_all_clean.log"))):
            base = os.path.basename(p)
            runs.append(base.replace("_score_all_clean.log", ""))
    elif args.run:
        runs = [args.run]
    else:
        ap.error("provide RUN or --all")

    reports = []
    for run in runs:
        path = os.path.join(args.data_dir, "%s_score_all_clean.log" % run)
        if not os.path.isfile(path):
            print("skip (missing): %s" % path, file=sys.stderr)
            continue
        stats = analyze_file(path)
        reports.append(format_report(run, stats))

    if not reports:
        sys.exit(1)

    text = "\n\n".join(reports) + "\n"
    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text)
        print("wrote %s" % args.out)
    else:
        print(text)


if __name__ == "__main__":
    main()
