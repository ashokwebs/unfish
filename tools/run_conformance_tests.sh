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

TMP_OUT=$(mktemp)
TMP_ERR=$(mktemp)

cleanup() {
    rm -f "$TMP_OUT" "$TMP_ERR"
}
trap cleanup EXIT

echo "Running language conformance test suite..."
echo "----------------------------------------------------"

for test_file in "$TESTS_DIR"/*.unfish; do
    [ -f "$test_file" ] || continue
    test_name=$(basename "$test_file")
    case "$test_name" in helper_*) continue ;; esac

    expected_exit=0
    if grep -q "^# expect-exit:" "$test_file"; then
        expected_exit=$(grep "^# expect-exit:" "$test_file" | head -n1 | awk '{print $3}')
    fi

    # Run unfish
    set +e
    "$UNFISH_BIN" run "$test_file" > "$TMP_OUT" 2> "$TMP_ERR"
    actual_exit=$?
    set -e

    test_failed=0

    # 1. Verify exit code
    if [ "$actual_exit" -ne "$expected_exit" ]; then
        echo "FAIL: $test_name (exit code: expected $expected_exit, got $actual_exit)"
        test_failed=1
    fi

    # 2. If positive test, verify stdout matches all '# expect: ...'
    if [ "$expected_exit" -eq 0 ]; then
        expected_output=""
        while IFS= read -r line; do
            if [[ "$line" =~ ^#\ expect:\ (.*) ]]; then
                if [ -z "$expected_output" ]; then
                    expected_output="${BASH_REMATCH[1]}"
                else
                    expected_output="${expected_output}"$'\n'"${BASH_REMATCH[1]}"
                fi
            fi
        done < "$test_file"

        actual_output=$(cat "$TMP_OUT")
        if [ "$expected_output" != "$actual_output" ]; then
            echo "FAIL: $test_name (stdout mismatch)"
            echo "  Expected:"
            echo "$expected_output" | sed 's/^/    /'
            echo "  Got:"
            echo "$actual_output" | sed 's/^/    /'
            test_failed=1
        fi
    fi

    # 3. If negative test, verify stderr contains '# expect-error: ...'
    if [ "$expected_exit" -ne 0 ]; then
        while IFS= read -r line; do
            if [[ "$line" =~ ^#\ expect-error:\ (.*) ]]; then
                expected_err="${BASH_REMATCH[1]}"
                if ! grep -q "$expected_err" "$TMP_ERR"; then
                    echo "FAIL: $test_name (stderr missing expected error: '$expected_err')"
                    echo "  Actual stderr:"
                    cat "$TMP_ERR" | sed 's/^/    /'
                    test_failed=1
                fi
            fi
        done < "$test_file"
    fi

    if [ "$test_failed" -eq 0 ]; then
        echo "PASS: $test_name"
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
done

echo "----------------------------------------------------"
echo "Conformance Suite Results: $PASSED passed, $FAILED failed."

if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
exit 0
