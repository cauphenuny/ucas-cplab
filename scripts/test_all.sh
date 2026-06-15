#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR=${BUILD_DIR:-build}

ctest --test-dir ${BUILD_DIR}
script_dir="$(dirname "$(realpath "$0")")"

export CACTC=${BUILD_DIR}/compiler
$script_dir/test_isel.sh "$@"
$script_dir/test_isel.sh "$@" --optimize
$script_dir/test_lowering.sh "$@"
$script_dir/test_lowering.sh "$@" --optimize

$BUILD_DIR/test_optimize --enum
$BUILD_DIR/test_optimize --enum --exit-ssa --regalloc
