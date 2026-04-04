#!/usr/bin/env python3
"""
Usage: $0 /path/to/compiler [--ir]

Finds pairs of `*.cact` (or `*.ir` if --ir is specified) and `*.out` files in the current directory,
runs the compiler (or interpreter) with appropriate flags, captures stdout, and compares
it to the corresponding `.out` file after removing the first line of
the `.out` file.

Exits with non-zero code if any mismatch occurs.
"""
import sys
import subprocess
import argparse
from pathlib import Path
import difflib


def compare_outputs(source_path: Path, out_path: Path, executable: str, is_ir: bool) -> int:
    # run executable
    if is_ir:
        cmd = [executable, str(source_path), "--silent"]
    else:
        cmd = [executable, str(source_path), "--exec", "--silent"]
    in_path = source_path.with_suffix('.in')
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
        print("Passed: ", source_path)
        return 0

    # print unified diff
    diff = difflib.unified_diff(expected, actual, fromfile=str(
        out_path) + " (expected)", tofile=str(source_path) + " (actual)", lineterm="")
    print("Mismatch for:", source_path)
    for line in diff:
        print(line)
    return 2


def main():
    parser = argparse.ArgumentParser(description="Test functional interpretation")
    parser.add_argument("executable", help="Path to compiler or interpreter")
    parser.add_argument("--ir", action="store_true", help="Test IR files instead of CACT files")
    args = parser.parse_args()

    cwd = Path('.')
    status = 0

    if args.ir:
        files = sorted(cwd.glob('*.ir'))
    else:
        files = sorted(cwd.glob('*.cact'))

    for source in files:
        out = source.with_suffix('.out')
        if not out.exists():
            print(f"Skipping {source}: no corresponding {out.name}")
            continue
        rc = compare_outputs(source, out, args.executable, args.ir)
        if rc != 0:
            status = rc

    return status


if __name__ == '__main__':
    raise SystemExit(main())
