#!/usr/bin/env bash
set -euo pipefail

DIR="test/samples_codegen_functional"
CC="build/compiler"
pass=0
fail=0

for cact in "$DIR"/*.cact; do
    base="${cact%.cact}"
    name="$(basename "$cact" .cact)"

    if [ -f "$base.in" ]; then
        if diff <("$CC" "$cact" --silent --lowering --exec "$@" < "$base.in") \
              <("$CC" "$cact" --silent --asm-exec "$@" < "$base.in") > /dev/null 2>&1; then
            echo "PASS: $name (with input)"
            ((pass++))
        else
            echo "FAIL: $name (with input)"
            diff <("$CC" "$cact" --silent --lowering --exec "$@" < "$base.in") \
                  <("$CC" "$cact" --silent --asm-exec "$@" < "$base.in")
            ((fail++))
        fi
    else
        if diff <("$CC" "$cact" --silent --lowering --exec "$@") \
              <("$CC" "$cact" --silent --asm-exec "$@") > /dev/null 2>&1; then
            echo "PASS: $name"
            ((pass++))
        else
            echo "FAIL: $name"
            diff <("$CC" "$cact" --silent --lowering --exec "$@") \
                  <("$CC" "$cact" --silent --asm-exec "$@")
            ((fail++))
        fi
    fi
done

echo "=============================="
echo "Total: $((pass + fail)), Passed: $pass, Failed: $fail"
