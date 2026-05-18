#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
MAP="${1:-../cartographer_parallel/cartographer_parallel/maps/0501.pgm}"

make -s compare MAP="$MAP" 2>&1 | grep -E '^(PA01|n=|===|verify|max_diff)' || true
