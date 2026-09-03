#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-bin/unfish}"

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run 'make' first."
    exit 1
fi

echo "================================================================="
echo "               Unfish Performance Benchmark Suite                "
echo "================================================================="
printf "%-30s | %-12s | %-10s\n" "Benchmark" "Elapsed Time" "Status"
echo "-------------------------------+--------------+------------"

BENCH_DIR="tests/benchmarks"

for bench in "$BENCH_DIR"/*.unfish; do
    if [ ! -f "$bench" ]; then continue; fi
    name=$(basename "$bench" .unfish)
    
    start_ns=$(date +%s%N)
    if out=$("$BIN" run "$bench" 2>&1); then
        end_ns=$(date +%s%N)
        duration_ms=$(( (end_ns - start_ns) / 1000000 ))
        sec=$(awk "BEGIN {printf \"%.3f\", $duration_ms / 1000}")
        printf "%-30s | %10ss | \033[32mPASS\033[0m\n" "$name" "$sec"
    else
        printf "%-30s | %12s | \033[31mFAIL\033[0m\n" "$name" "-"
        echo "$out"
    fi
done

echo "================================================================="
