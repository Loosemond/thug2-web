#!/usr/bin/env bash
# Compile every upstream game source with em++ (-fsyntax-only) against port/include and rank the errors.
#   port/tools/compile_survey.sh [-j N] [filter-regex]
# Writes build/survey/<file>.log per source and build/survey/summary.txt.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
JOBS=10; [[ "${1:-}" == "-j" ]] && { JOBS=$2; shift 2; }
FILTER="${1:-.}"
# (Emscripten: em++ on the PATH already, or $EMSDK's emsdk_env.sh)
command -v em++ >/dev/null || source "${EMSDK:?source your emsdk_env.sh first, or set EMSDK}/emsdk_env.sh" >/dev/null 2>&1
OUT="$ROOT/build/survey"; rm -rf "$OUT"; mkdir -p "$OUT"
DEPS="$ROOT/build/deps"
CXXFLAGS=(-std=c++20 -fsyntax-only -fms-extensions -fdeclspec -fshort-wchar -ferror-limit=50
  -Wno-ignored-attributes -Wno-microsoft-cast -Wno-unknown-pragmas -Wno-everything
  -include thug2web/msvc_crt.h -DTHUG2_STANDALONE -DTHUG2_WEB -DWIN32_LEAN_AND_MEAN -DNOMINMAX
  -I"$ROOT/port/include" -I"$DEPS/dxvk/include/native/windows" -I"$DEPS/directx"
  -I"$ROOT/upstream/src" -I"$ROOT/build/generated"
  -I"$DEPS/fonts" -I"$DEPS/imgui" -I"$DEPS/stb" -I"$DEPS/lzham/include" -sUSE_SDL=2)
cd "$ROOT/upstream"
find src/game -name '*.cpp' | grep -E "$FILTER" | sort > "$OUT/files.txt"
export OUT; export -f 2>/dev/null
xargs -P "$JOBS" -I{} sh -c 'em++ "$@" "{}" > "$OUT/$(echo {} | tr / _).log" 2>&1; echo $? > "$OUT/$(echo {} | tr / _).rc"' _ "${CXXFLAGS[@]}" < "$OUT/files.txt"
total=$(wc -l < "$OUT/files.txt"); ok=$(grep -l '^0$' "$OUT"/*.rc | wc -l)
{
  echo "files: $total  compile clean: $ok  failing: $((total-ok))"
  echo; echo "== top error messages (normalised)"
  cat "$OUT"/*.log | grep -E ': (fatal )?error: ' | sed -E 's/^.*error: //; s/'"'"'[^'"'"']*'"'"'/X/g' | sort | uniq -c | sort -rn | head -40
  echo; echo "== top undeclared/unknown names"
  cat "$OUT"/*.log | grep -oE "(unknown type name|use of undeclared identifier|no member named) '[^']+'" | sed -E "s/^.*'([^']+)'/\1/" | sort | uniq -c | sort -rn | head -80
} > "$OUT/summary.txt"
cat "$OUT/summary.txt" | head -140
