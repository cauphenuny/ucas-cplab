#!/usr/bin/env bash
set -euo pipefail

script_dir="$(dirname "$(realpath "$0")")"

export common="--silent --exec"
export refer=""
export test="--lowering"

"$script_dir/autotest.sh" "$@"