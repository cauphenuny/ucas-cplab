#!/usr/bin/env python3
"""
Usage: $0 /path/to/compiler

Finds pairs of `*.cact` and `*.out` files in the current directory,
runs the compiler with `--exec --silent`, captures stdout, and compares
it to the corresponding `.out` file after removing the first line of
the `.out` file.

Exits with non-zero code if any mismatch occurs.
"""
import sys
import subprocess
from pathlib import Path
import difflib


def compare_outputs(cact_path: Path, out_path: Path, compiler: str) -> int:
    # run compiler
    cmd = [compiler, str(cact_path), "--exec", "--silent"]
    in_path = cact_path.with_suffix('.in')
    if in_path.exists():
        with in_path.open('r') as infile:
            proc = subprocess.run(cmd, stdin=infile, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True)
    else:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True)
    actual = proc.stdout.splitlines()

    # read expected and drop first line
    with out_path.open() as f:
        expected = f.read().splitlines()
    expected = expected[1:]

    if actual == expected:
        print(f"Passed: {cact_path}")
        return 0

    # print unified diff
    diff = difflib.unified_diff(expected, actual, fromfile=str(
        out_path) + " (expected)", tofile=str(cact_path) + " (actual)", lineterm="")
    print("Mismatch for:", cact_path)
    for line in diff:
        print(line)
    return 2


def main(argv):
    if len(argv) != 2:
        print("Usage: run_and_compare.py /path/to/compiler")
        return 1

    compiler = argv[1]
    cwd = Path('.')
    status = 0

    # find all .cact files and match .out
    for cact in sorted(cwd.glob('*.cact')):
        out = cact.with_suffix('.out')
        if not out.exists():
            print(f"Skipping {cact}: no corresponding {out.name}")
            continue
        rc = compare_outputs(cact, out, compiler)
        if rc != 0:
            status = rc

    return status


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
