#!/usr/bin/env python3
import os
import sys
import subprocess
from update_readme import update_readme


def run_help(executable_path):
    """Run --help on the executable and return the output."""
    try:
        result = subprocess.run(
            [executable_path, '--help'], capture_output=True, text=True, timeout=10)
        output = result.stdout.strip()
        # Replace the full path in the first line with basename
        lines = output.split('\n')
        if lines:
            first_line = lines[0]
            # Assume first line starts with the executable path
            if executable_path in first_line:
                lines[0] = first_line.replace(executable_path, os.path.basename(executable_path))
        return '\n'.join(lines)
    except Exception as e:
        print("Error running {} --help: {}".format(executable_path, e))
        return ""


def main():
    if len(sys.argv) != 3:
        print("Usage: {} <compiler_path> <interpreter_path>".format(
            sys.argv[0]))
        sys.exit(1)

    compiler_path = sys.argv[1]
    interpreter_path = sys.argv[2]

    compiler_name = os.path.basename(compiler_path)
    interpreter_name = os.path.basename(interpreter_path)

    compiler_help = run_help(compiler_path)
    interpreter_help = run_help(interpreter_path)

    # Generate usage content
    usage_content = """{}

{}""".format(compiler_help, interpreter_help)

    # Update README
    script_dir = os.path.dirname(os.path.abspath(__file__))
    readme_path = os.path.join(script_dir, '..', 'README.md')
    update_readme(readme_path, usage_content, '<!--usage-->', '<!--/usage-->')


if __name__ == '__main__':
    main()
