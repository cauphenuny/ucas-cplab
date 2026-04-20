#!/usr/bin/env python3
import os
import re
from update_readme import update_readme


def extract_brief(filepath):
    """Extract @brief from the beginning of a file."""
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()[:20]  # Read first 20 lines
            for line in lines:
                # Match /// @brief or /* @brief or // @brief
                match = re.search(
                    r'(?:///*\s*@brief\s*(.+)|/\*\s*@brief\s*(.+)\s*\*/)', line.strip())
                if match:
                    brief = match.group(1) or match.group(2)
                    return brief.strip()
    except Exception as e:
        pass
    return None


def collect_briefs(root_dir):
    """Collect briefs for all files under root_dir."""
    briefs = {}
    for dirpath, dirnames, filenames in os.walk(root_dir):
        for filename in filenames:
            filepath = os.path.join(dirpath, filename)
            brief = extract_brief(filepath)
            if brief:
                # Store relative path as key
                rel_path = os.path.relpath(filepath, root_dir)
                briefs[rel_path] = brief
    return briefs


def generate_tree_output(root_dir_abs, briefs, root_dir_rel):
    """Generate tree-like output with briefs."""
    def get_items(path):
        items = []
        try:
            for name in sorted(os.listdir(path)):
                if name.startswith('.'):
                    continue  # Skip hidden files
                full_path = os.path.join(path, name)
                rel_path = os.path.relpath(full_path, root_dir_abs)
                items.append((name, rel_path, os.path.isdir(full_path)))
        except OSError:
            pass
        return items

    def build_tree(path, prefix="", is_last=True):
        lines = []
        items = get_items(path)
        for i, (name, rel_path, is_dir) in enumerate(items):
            last = (i == len(items) - 1)
            if is_dir:
                lines.append(
                    "{}{}{}/".format(prefix, '└── ' if last else '├── ', name))
                sub_prefix = prefix + ("    " if last else "│   ")
                lines.extend(build_tree(os.path.join(
                    path, name), sub_prefix, last))
            else:
                brief = briefs.get(rel_path, "")
                suffix = ":\t{}".format(brief) if brief else ""
                lines.append(
                    "{}{}{}{}".format(prefix, '└── ' if last else '├── ', name, suffix))
        return lines

    lines = ["{}/".format(root_dir_rel)]
    lines.extend(build_tree(root_dir_abs, "", True))
    # Count dirs and files

    def count_items(path):
        dirs = 0
        files = 0
        for root, dirs_list, files_list in os.walk(path):
            dirs += len([d for d in dirs_list if not d.startswith('.')])
            files += len([f for f in files_list if not f.startswith('.')])
        return dirs, files

    dirs, files = count_items(root_dir_abs)
    lines.append("")
    lines.append("{} directories, {} files".format(dirs, files))
    return "\n".join(lines)


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir_abs = os.path.abspath(os.path.join(script_dir, '..', 'src'))
    root_dir_rel = 'src'
    readme_path = os.path.join(script_dir, '..', 'README.md')

    if not os.path.exists(root_dir_abs):
        print("Directory {} does not exist.".format(root_dir_abs))
        return

    # Collect briefs
    briefs = collect_briefs(root_dir_abs)

    # Generate tree output
    modified_output = generate_tree_output(root_dir_abs, briefs, root_dir_rel)
    # Print to console
    print(modified_output)

    # Update README.md
    update_readme(readme_path, modified_output)


if __name__ == '__main__':
    main()
