#!/usr/bin/env bash
set -euo pipefail

DIR="test/samples_codegen_functional"
CACTC=${CACTC:-"build/compiler"}
common=${common:-"--silent --exec"}
refer=${refer:-""}
test=${test:-"--lowering"}

echo -e "Running autotest in $DIR\nwith\n    CACTC='$CACTC'\n    common='$common'\n    refer='$refer'\n    test='$test'\n    extra='$@'\n"

pass=0
fail=0

for cact in "$DIR"/*.cact; do
    base="${cact%.cact}"
    name="$(basename "$cact" .cact)"

    if [ -f "$base.in" ]; then
        if diff <($CACTC $common $cact $refer $@ < $base.in) \
                <($CACTC $common $cact $test  $@ < $base.in) > /dev/null 2>&1; then
            echo "PASS: $name (with input)"
            pass=$((pass+1))
        else
            echo "FAIL: $name (with input)"
            diff <($CACTC $common $cact $refer $@ < $base.in) \
                 <($CACTC $common $cact $test  $@ < $base.in)
            fail=$((fail+1))
        fi
    else
        if diff <($CACTC $common $cact $refer $@) \
                <($CACTC $common $cact $test  $@) > /dev/null 2>&1; then
            echo "PASS: $name"
            pass=$((pass+1))
        else
            echo "FAIL: $name"
            diff <($CACTC $common $cact $refer $@) \
                 <($CACTC $common $cact $test  $@)
            fail=$((fail+1))
        fi
    fi
done

echo "=============================="
echo "Total: $((pass + fail)), Passed: $pass, Failed: $fail"

if [ "$fail" -ne 0 ]; then
    exit 1
fi