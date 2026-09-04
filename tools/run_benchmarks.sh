#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-bin/unfish}"

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run 'make' first."
    exit 1
fi

echo "=========================================================================================="
echo "                   Unfish Multi-Tier Performance Benchmark Suite                          "
echo "=========================================================================================="
printf "%-24s | %-12s | %-12s | %-10s | %-12s\n" "Benchmark" "AST Interp" "Bytecode VM" "VM Speedup" "Native C99"
echo "-------------------------+--------------+--------------+------------+-------------"

BENCH_DIR="tests/benchmarks"
TMP_BIN="/tmp/uf_bench_native_$$"

cleanup() {
    rm -f "$TMP_BIN" "$TMP_BIN.c"
}
trap cleanup EXIT

for bench in "$BENCH_DIR"/*.unfish; do
    if [ ! -f "$bench" ]; then continue; fi
    name=$(basename "$bench" .unfish)

    # 1. AST Interpreter
    s_ast=$(date +%s%N)
    "$BIN" run "$bench" > /dev/null 2>&1 || true
    e_ast=$(date +%s%N)
    d_ast_ms=$(( (e_ast - s_ast) / 1000000 ))
    sec_ast=$(awk "BEGIN {printf \"%.3f\", $d_ast_ms / 1000}")

    # 2. Bytecode VM
    s_vm=$(date +%s%N)
    "$BIN" run --vm "$bench" > /dev/null 2>&1 || true
    e_vm=$(date +%s%N)
    d_vm_ms=$(( (e_vm - s_vm) / 1000000 ))
    sec_vm=$(awk "BEGIN {printf \"%.3f\", $d_vm_ms / 1000}")

    vm_speedup=$(awk "BEGIN { if ($d_vm_ms > 0) printf \"%.1fx\", $d_ast_ms / $d_vm_ms; else printf \">100x\"; }")

    # 3. Native C99 (if compiles)
    sec_nat="-"
    if "$BIN" build -o "$TMP_BIN" "$bench" > /dev/null 2>&1; then
        s_nat=$(date +%s%N)
        "$TMP_BIN" > /dev/null 2>&1 || true
        e_nat=$(date +%s%N)
        d_nat_ms=$(( (e_nat - s_nat) / 1000000 ))
        sec_nat=$(awk "BEGIN {printf \"%.3f\", $d_nat_ms / 1000}")
    fi

    printf "%-24s | %10ss | %10ss | %10s | %10ss\n" "$name" "$sec_ast" "$sec_vm" "$vm_speedup" "$sec_nat"
done

echo "=========================================================================================="
