#!/usr/bin/env python3
"""The game folder's file list for the browser build: one "size<TAB>path" line per file (paths relative to the
folder, '/' separated). The page's file system makes each file from it and fetches its bytes only when read.

    make_manifest.py game/THUG2/Game build/web/manifest.txt
"""

import os
import sys


def main():
    root, out = sys.argv[1], sys.argv[2]
    lines = []
    for folder, folders, files in os.walk(root):
        folders.sort()
        for name in sorted(files):
            path = os.path.join(folder, name)
            relative = os.path.relpath(path, root).replace(os.sep, "/")
            lines.append(f"{os.path.getsize(path)}\t{relative}")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{len(lines)} files")


if __name__ == "__main__":
    main()
