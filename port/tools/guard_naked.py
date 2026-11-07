#!/usr/bin/env python3
"""Wrap every __declspec(naked) function in upstream/src/game in `#if defined(_M_IX86)` ... `#endif`.

The naked stubs adapt the original exe's register calling conventions to our functions. Only REPLACE lines (empty in a
standalone build) and header declarations refer to them, so a build for any other architecture leaves them out.
Idempotent: already guarded functions are skipped.

    python3 -I port/tools/guard_naked.py [--check]
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
GAME = ROOT / "upstream" / "src" / "game"
GUARD = "#if defined(_M_IX86) // NOT ORIGINAL: x86 register stub, only the original exe calls it"
NAKED = re.compile(r"^[ \t]*(?:static[ \t]+|inline[ \t]+)*__declspec\(naked\)[^\n]*$", re.M)


def function_end(text: str, start: int) -> int:
    """The index just past the closing brace of the function whose header starts at `start`."""
    i = text.index("{", start)
    depth = 0
    while i < len(text):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        elif c == "/" and text.startswith("//", i):
            i = text.index("\n", i)
            continue
        i += 1
    raise ValueError("unbalanced braces")


def guard(text: str) -> tuple[str, int]:
    out, pos, count = [], 0, 0
    for m in NAKED.finditer(text):
        if m.start() < pos:
            continue
        before = text[:m.start()].rstrip("\n").rsplit("\n", 1)[-1]
        if before.startswith("#if defined(_M_IX86)"):
            continue
        if before.rstrip().endswith("\\"):  # inside a macro definition: guard the #define by hand
            print(f"skipped (in a macro): {text.count(chr(10), 0, m.start()) + 1}")
            continue
        end = function_end(text, m.start())
        out.append(text[pos:m.start()])
        out.append(GUARD + "\n" + text[m.start():end] + "\n#endif")
        pos = end
        count += 1
    out.append(text[pos:])
    return "".join(out), count


def main() -> int:
    check = "--check" in sys.argv
    total = 0
    for path in sorted(GAME.rglob("*.cpp")) + sorted(GAME.rglob("*.h")):
        text = path.read_text(encoding="utf-8")
        new, count = guard(text)
        if count:
            total += count
            print(f"{path.relative_to(ROOT)}: {count}")
            if not check:
                path.write_text(new, encoding="utf-8")
    print(f"{total} naked functions {'to guard' if check else 'guarded'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
