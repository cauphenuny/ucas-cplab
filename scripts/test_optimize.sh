#!/usr/bin/env bash
set -euo pipefail

script_dir="$(dirname "$(realpath "$0")")"

export common="--silent --exec"
export refer=""
export test="--optimize"

"$script_dir/autotest.sh" "$@"