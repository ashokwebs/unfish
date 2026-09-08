#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-bin/unfish}"

if [ ! -f "$BIN" ]; then
    echo "Error: Binary $BIN not found. Run 'make' first."
    exit 1
fi

echo "========================================================================================================"
echo "                         Unfish Multi-Tier Performance Benchmark Suite                          "
echo "========================================================================================================"
printf "%-22s | %-10s | %-10s | %-11s | %-10s | %-10s\n" "Benchmark" "AST Interp" "Stack VM" "Register VM" "Reg Speedup" "Native C99"
echo "-----------------------+------------+------------+-------------+-------------+------------"

BENCH_DIR="tests/benchmarks"
TMP_BIN="/tmp/uf_bench_native_$$"

cleanup() {
    rm -f "$TMP_BIN" "$TMP_BIN.c"
}
trap cleanup EXIT

get_time_ms() {
    if date +%s%N 2>/dev/null | grep -vq N; then
        echo $(( $(date +%s%N) / 1000000 ))
    elif command -v python3 >/dev/null 2>&1; then
        python3 -c 'import time; print(int(time.time() * 1000))'
    elif command -v perl >/dev/null 2>&1; then
        perl -MTime::HiRes=time -e 'printf("%.0f\n", time()*1000)'
    else
        echo $(( $(date +%s) * 1000 ))
    fi
}

for bench in "$BENCH_DIR"/*.unfish; do
    if [ ! -f "$bench" ]; then continue; fi
    name=$(basename "$bench" .unfish)

    # 1. AST Interpreter
    s_ast=$(get_time_ms)
    "$BIN" run "$bench" > /dev/null 2>&1 || true
    e_ast=$(get_time_ms)
    d_ast_ms=$(( e_ast - s_ast ))
    sec_ast=$(awk "BEGIN {printf \"%.3f\", $d_ast_ms / 1000}")

    # 2. Bytecode Stack VM
    s_vm=$(get_time_ms)
    "$BIN" run --vm "$bench" > /dev/null 2>&1 || true
    e_vm=$(get_time_ms)
    d_vm_ms=$(( e_vm - s_vm ))
    sec_vm=$(awk "BEGIN {printf \"%.3f\", $d_vm_ms / 1000}")

    # 3. Register VM
    s_reg=$(get_time_ms)
    "$BIN" run --regvm "$bench" > /dev/null 2>&1 || true
    e_reg=$(get_time_ms)
    d_reg_ms=$(( e_reg - s_reg ))
    sec_reg=$(awk "BEGIN {printf \"%.3f\", $d_reg_ms / 1000}")

    reg_speedup=$(awk "BEGIN { if ($d_reg_ms > 0) printf \"%.1fx\", $d_ast_ms / $d_reg_ms; else printf \">100x\"; }")

    # 4. Native C99 (if compiles)
    sec_nat="-"
    if "$BIN" build -o "$TMP_BIN" "$bench" > /dev/null 2>&1; then
        s_nat=$(get_time_ms)
        "$TMP_BIN" > /dev/null 2>&1 || true
        e_nat=$(get_time_ms)
        d_nat_ms=$(( e_nat - s_nat ))
        sec_nat=$(awk "BEGIN {printf \"%.3f\", $d_nat_ms / 1000}")
    fi

    printf "%-22s | %8ss | %8ss | %9ss | %11s | %8ss\n" "$name" "$sec_ast" "$sec_vm" "$sec_reg" "$reg_speedup" "$sec_nat"
done

echo "========================================================================================================"
