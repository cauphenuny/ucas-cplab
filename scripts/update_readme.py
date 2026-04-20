#!/usr/bin/env python3
import os


def update_readme(readme_path, new_content, start_marker='<!--source_tree-->', end_marker='<!--/source_tree-->'):
    """Update the README.md with new content between markers."""
    try:
        with open(readme_path, 'r', encoding='utf-8') as f:
            content = f.read()

        start_idx = content.find(start_marker)
        if start_idx == -1:
            print("Start marker '{}' not found in {}".format(
                start_marker, readme_path))
            return

        end_idx = content.find(end_marker, start_idx)
        if end_idx == -1:
            print("End marker '{}' not found in {}".format(
                end_marker, readme_path))
            return

        # Include the markers in the replacement
        before = content[:start_idx + len(start_marker)]
        after = content[end_idx:]

        updated_content = before + '\n```\n' + new_content + '\n```\n' + after

        with open(readme_path, 'w', encoding='utf-8') as f:
            f.write(updated_content)

        print("Updated {} with new content.".format(readme_path))
    except Exception as e:
        print("Error updating README: {}".format(e))
