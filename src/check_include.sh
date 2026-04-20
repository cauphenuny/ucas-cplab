#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:-src}"

if ! command -v rg >/dev/null 2>&1; then
    echo "error: 'rg' (ripgrep) is required for include checks." >&2
    exit 2
fi

header_for_symbol() {
    case "$1" in
        # Containers
        array) echo "array" ;;
        deque) echo "deque" ;;
        forward_list) echo "forward_list" ;;
        list) echo "list" ;;
        map|multimap) echo "map" ;;
        multiset|set) echo "set" ;;
        priority_queue|queue) echo "queue" ;;
        stack) echo "stack" ;;
        unordered_map|unordered_multimap) echo "unordered_map" ;;
        unordered_multiset|unordered_set) echo "unordered_set" ;;
        vector) echo "vector" ;;

        # Utility/value wrappers
        optional|nullopt|nullopt_t) echo "optional" ;;
        variant|monostate|holds_alternative) echo "variant" ;;
        any) echo "any" ;;
        tuple) echo "tuple" ;;
        pair) echo "utility" ;;

        # Memory/resource
        unique_ptr|shared_ptr|weak_ptr|make_unique|make_shared) echo "memory" ;;

        # Common support types
        string) echo "string" ;;
        string_view) echo "string_view" ;;
        function|reference_wrapper|ref|cref) echo "functional" ;;
        filesystem) echo "filesystem" ;;

        *) echo "" ;;
    esac
}

missing_files=0
missing_headers=0

while IFS= read -r file; do
    symbols="$(rg -o 'std::[A-Za-z_][A-Za-z0-9_]*' "$file" | sed 's/^std:://' | sort -u || true)"

    [[ -z "$symbols" ]] && continue

    missing_pairs=()

    while IFS= read -r sym; do
        [[ -z "$sym" ]] && continue
        header="$(header_for_symbol "$sym")"
        [[ -z "$header" ]] && continue

        if ! rg -q "^[[:space:]]*#include[[:space:]]*<${header}>" "$file"; then
            missing_pairs+=("${header}:${sym}")
        fi
    done <<EOF
${symbols}
EOF

    if (( ${#missing_pairs[@]} > 0 )); then
        ((missing_files += 1))
        echo "[missing-include] $file"

        grouped="$(printf '%s\n' "${missing_pairs[@]}" | sort -u | awk -F: '
            {
                if ($1 != hdr) {
                    if (NR > 1) printf ")\n";
                    hdr = $1;
                    printf "  - <%s> (for std::%s", $1, $2;
                } else {
                    printf ",%s", $2;
                }
            }
            END {
                if (NR > 0) printf ")\n";
            }
        ')"
        printf '%s\n' "$grouped"

        header_count=$(printf '%s\n' "${missing_pairs[@]}" | cut -d: -f1 | sort -u | wc -l | tr -d ' ')
        ((missing_headers += header_count))
    fi
done < <(rg --files "$ROOT_DIR" -g '*.h' -g '*.hpp' -g '*.c' -g '*.cc' -g '*.cpp')

if (( missing_files > 0 )); then
    echo
    echo "Include check failed: ${missing_files} file(s), ${missing_headers} missing include(s)."
    exit 1
fi

echo "Include check passed: no missing mapped standard includes under ${ROOT_DIR}."
