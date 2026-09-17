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
TMP_REGVM_OUT=$(mktemp)
TMP_REGVM_ERR=$(mktemp)
TMP_NATIVE_OUT=$(mktemp)
TMP_NATIVE_ERR=$(mktemp)
TMP_WASM_OUT=$(mktemp)
TMP_WASM_ERR=$(mktemp)
TMP_NATIVE_BIN=""

HAS_WASM=0
if which node >/dev/null 2>&1 && which clang >/dev/null 2>&1; then
    HAS_WASM=1
fi

cleanup() {
    rm -f "$TMP_INTERP_OUT" "$TMP_INTERP_ERR" "$TMP_VM_OUT" "$TMP_VM_ERR" "$TMP_REGVM_OUT" "$TMP_REGVM_ERR" "$TMP_NATIVE_OUT" "$TMP_NATIVE_ERR" "$TMP_WASM_OUT" "$TMP_WASM_ERR"
    [ -n "$TMP_NATIVE_BIN" ] && rm -f "$TMP_NATIVE_BIN"
}
trap cleanup EXIT

if [ "$HAS_WASM" -eq 1 ]; then
    echo "Running 5-way differential conformance tests (AST Interpreter vs Stack VM vs Register VM vs Native C99 vs WebAssembly)..."
else
    echo "Running 4-way differential conformance tests (AST Interpreter vs Stack VM vs Register VM vs Native C99)..."
fi
echo "----------------------------------------------------------------------------------------------------------------------"


for test_file in "$TESTS_DIR"/*.unfish; do
    [ -f "$test_file" ] || continue
    test_name=$(basename "$test_file")
    case "$test_name" in helper_*) continue ;; esac

    flags=""
    if grep -q "^# flags:" "$test_file"; then
        flags=$(grep "^# flags:" "$test_file" | head -n1 | tr -d '\r' | sed 's/^# flags:[ ]*//')
    fi

    # 1. Run AST interpreter
    set +e
    "$UNFISH_BIN" run $flags "$test_file" > "$TMP_INTERP_OUT" 2> "$TMP_INTERP_ERR"
    interp_exit=$?

    # 2. Run Bytecode Stack VM
    "$UNFISH_BIN" run --vm $flags "$test_file" > "$TMP_VM_OUT" 2> "$TMP_VM_ERR"
    vm_exit=$?

    # 3. Run Register VM
    "$UNFISH_BIN" run --regvm $flags "$test_file" > "$TMP_REGVM_OUT" 2> "$TMP_REGVM_ERR"
    regvm_exit=$?
    set -e

    diff_failed=0

    # Compare exit codes between Interp, VM, and RegVM
    if [ "$interp_exit" -ne "$vm_exit" ]; then
        echo "FAIL: $test_name (Exit code divergence: Interpreter=$interp_exit, Stack VM=$vm_exit)"
        diff_failed=1
    fi
    if [ "$interp_exit" -ne "$regvm_exit" ]; then
        echo "FAIL: $test_name (Exit code divergence: Interpreter=$interp_exit, RegVM=$regvm_exit)"
        diff_failed=1
    fi

    # Compare stdout between Interp, VM, and RegVM
    if ! diff -u "$TMP_INTERP_OUT" "$TMP_VM_OUT" > /dev/null 2>&1; then
        echo "FAIL: $test_name (Stdout divergence between Interpreter and Stack VM):"
        diff -u "$TMP_INTERP_OUT" "$TMP_VM_OUT" || true
        diff_failed=1
    fi
    if ! diff -u "$TMP_INTERP_OUT" "$TMP_REGVM_OUT" > /dev/null 2>&1; then
        echo "FAIL: $test_name (Stdout divergence between Interpreter and Register VM):"
        diff -u "$TMP_INTERP_OUT" "$TMP_REGVM_OUT" || true
        diff_failed=1
    fi

    # 4. For positive tests (exit 0), compare Native C99 compiler
    is_native_tested=0
    if [ "$interp_exit" -eq 0 ] && [ "$vm_exit" -eq 0 ] && [ "$regvm_exit" -eq 0 ]; then
        set +e
        TMP_NATIVE_BIN=$(mktemp)
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
        # 5. For positive tests, also compare WebAssembly (WASM) compiler and runner
        is_wasm_tested=0
        if [ "$HAS_WASM" -eq 1 ] && [ "$diff_failed" -eq 0 ]; then
            set +e
            "$UNFISH_BIN" run --wasm "$test_file" > "$TMP_WASM_OUT" 2> "$TMP_WASM_ERR"
            wasm_exit=$?
            if [ "$wasm_exit" -ne 0 ]; then
                echo "FAIL: $test_name (WebAssembly execution failed with code $wasm_exit)"
                diff_failed=1
            elif ! diff -u "$TMP_INTERP_OUT" "$TMP_WASM_OUT" > /dev/null 2>&1; then
                echo "FAIL: $test_name (Stdout divergence between Interpreter and WebAssembly):"
                diff -u "$TMP_INTERP_OUT" "$TMP_WASM_OUT" || true
                diff_failed=1
            else
                is_wasm_tested=1
            fi
            set -e
        fi
    fi

    if [ "$diff_failed" -eq 0 ]; then
        if [ "$is_native_tested" -eq 1 ] && [ "$is_wasm_tested" -eq 1 ]; then
            echo "PASS (Parity 100% Interp == VM == RegVM == Native == WASM): $test_name (exit=$interp_exit)"
        elif [ "$is_native_tested" -eq 1 ]; then
            echo "PASS (Parity 100% Interp == VM == RegVM == Native): $test_name (exit=$interp_exit)"
        else
            echo "PASS (Parity 100% Interp == VM == RegVM): $test_name (exit=$interp_exit)"
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
