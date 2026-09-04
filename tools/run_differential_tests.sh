#!/usr/bin/env bash
set -e

UNFISH_BIN="./bin/unfish"

if [ ! -f "$UNFISH_BIN" ]; then
    echo "Binary $UNFISH_BIN not found. Please run 'make' first."
    exit 1
fi

TESTS_DIR="./tests/conformance"
PASSED=0
FAILED=0

TMP_INTERP_OUT=$(mktemp)
TMP_INTERP_ERR=$(mktemp)
TMP_VM_OUT=$(mktemp)
TMP_VM_ERR=$(mktemp)
TMP_NATIVE_OUT=$(mktemp)
TMP_NATIVE_ERR=$(mktemp)
TMP_NATIVE_BIN=$(mktemp)

cleanup() {
    rm -f "$TMP_INTERP_OUT" "$TMP_INTERP_ERR" "$TMP_VM_OUT" "$TMP_VM_ERR" "$TMP_NATIVE_OUT" "$TMP_NATIVE_ERR" "$TMP_NATIVE_BIN"
}
trap cleanup EXIT

echo "Running 3-way differential conformance tests (AST Interpreter vs Bytecode VM vs Native C99)..."
echo "--------------------------------------------------------------------------------------------"

for test_file in "$TESTS_DIR"/*.unfish; do
    [ -f "$test_file" ] || continue
    test_name=$(basename "$test_file")
    case "$test_name" in helper_*) continue ;; esac

    flags=""
    if grep -q "^# flags:" "$test_file"; then
        flags=$(grep "^# flags:" "$test_file" | head -n1 | sed 's/^# flags:[ ]*//')
    fi

    # 1. Run AST interpreter
    set +e
    "$UNFISH_BIN" run $flags "$test_file" > "$TMP_INTERP_OUT" 2> "$TMP_INTERP_ERR"
    interp_exit=$?

    # 2. Run Bytecode VM
    "$UNFISH_BIN" run --vm $flags "$test_file" > "$TMP_VM_OUT" 2> "$TMP_VM_ERR"
    vm_exit=$?
    set -e

    diff_failed=0

    # 3. Compare exit codes between Interp and VM
    if [ "$interp_exit" -ne "$vm_exit" ]; then
        echo "FAIL: $test_name (Exit code divergence: Interpreter=$interp_exit, VM=$vm_exit)"
        diff_failed=1
    fi

    # 4. Compare stdout between Interp and VM
    if ! diff -u "$TMP_INTERP_OUT" "$TMP_VM_OUT" > /dev/null 2>&1; then
        echo "FAIL: $test_name (Stdout divergence between Interpreter and VM):"
        diff -u "$TMP_INTERP_OUT" "$TMP_VM_OUT" || true
        diff_failed=1
    fi

    # 5. For positive tests (exit 0), compare Native C99 compiler
    is_native_tested=0
    if [ "$interp_exit" -eq 0 ] && [ "$vm_exit" -eq 0 ]; then
        set +e
        "$UNFISH_BIN" build "$test_file" -o "$TMP_NATIVE_BIN" > /dev/null 2>&1
        build_exit=$?
        if [ "$build_exit" -ne 0 ]; then
            echo "FAIL: $test_name (Native C99 build failed with code $build_exit)"
            diff_failed=1
        else
            "$TMP_NATIVE_BIN" > "$TMP_NATIVE_OUT" 2> "$TMP_NATIVE_ERR"
            native_exit=$?
            if [ "$native_exit" -ne 0 ]; then
                echo "FAIL: $test_name (Native binary exited with code $native_exit)"
                diff_failed=1
            elif ! diff -u "$TMP_INTERP_OUT" "$TMP_NATIVE_OUT" > /dev/null 2>&1; then
                echo "FAIL: $test_name (Stdout divergence between Interpreter and Native C99):"
                diff -u "$TMP_INTERP_OUT" "$TMP_NATIVE_OUT" || true
                diff_failed=1
            else
                is_native_tested=1
            fi
        fi
        rm -f "$TMP_NATIVE_BIN"
        set -e
    fi

    if [ "$diff_failed" -eq 0 ]; then
        if [ "$is_native_tested" -eq 1 ]; then
            echo "PASS (Parity 100% Interp == VM == Native): $test_name (exit=$interp_exit)"
        else
            echo "PASS (Parity 100% Interp == VM): $test_name (exit=$interp_exit)"
        fi
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
done

echo "--------------------------------------------------------------------------------------------"
echo "Differential Test Results: $PASSED passed (identical), $FAILED diverged."

if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
