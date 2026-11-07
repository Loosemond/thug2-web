#!/usr/bin/env bash
# Downloads the pinned third-party sources into build/deps (gitignored).
#   port/tools/fetch_deps.sh
set -euo pipefail
# (Emscripten: em++ on the PATH already, or $EMSDK's emsdk_env.sh)
command -v em++ >/dev/null || source "${EMSDK:?source your emsdk_env.sh first, or set EMSDK}/emsdk_env.sh" >/dev/null 2>&1
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
D="$ROOT/build/deps"; mkdir -p "$D"
get() { # get <dir> <url> [tar paths...]
    local dir=$1 url=$2; shift 2
    [[ -e "$D/$dir/.fetched" ]] && return
    rm -rf "$D/$dir"; mkdir -p "$D/$dir"
    curl -fsSL "$url" | tar xz -C "$D/$dir" --strip-components=1 "$@"
    echo "$url" > "$D/$dir/.fetched"; echo "fetched $dir"
}
# The versions upstream's CMakeLists.txt pins
get imgui https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.tar.gz
get stb https://github.com/nothings/stb/archive/1ee679ca2ef753a528db5ba6801e1067b40481b8.tar.gz
get lzham https://github.com/richgel999/lzham_codec/archive/d379b1f9121e2197881c61cfc4713c78848bdfe7.tar.gz
# Win32 types and COM for non-Windows builds (zlib), and the Direct3D 9 headers it uses (mingw-w64 / Wine, LGPL 2.1)
get dxvk https://github.com/doitsujin/dxvk/archive/d5df6b768d116452ee737ce845df8b586f403ea9.tar.gz \
    dxvk-d5df6b768d116452ee737ce845df8b586f403ea9/include/native dxvk-d5df6b768d116452ee737ce845df8b586f403ea9/LICENSE
get directx https://github.com/Joshua-Ashton/mingw-directx-headers/archive/9df86f2341616ef1888ae59919feaa6d4fad693d.tar.gz

# The level editor's fonts (SIL OFL 1.1; upstream's CMake pins the same archives and hashes), compressed into C arrays
# with ImGui's binary_to_compressed_c as upstream's build does: build/deps/fonts/<symbol>.inc
font() { # font <zip url> <sha256> <path in zip> <symbol>
    local url=$1 sha=$2 path=$3 symbol=$4 zip="$D/fonts/$(basename "$1")"
    [[ -e "$D/fonts/$symbol.inc" ]] && return
    mkdir -p "$D/fonts"
    [[ -e "$zip" ]] || curl -fsSL -o "$zip" "$url"
    echo "$sha  $zip" | shasum -a 256 -c --quiet
    # (built with Emscripten and run on Node: no host C++ toolchain needed)
    [[ -e "$D/fonts/binary_to_compressed_c.js" ]] || em++ -O2 -w -sNODERAWFS -o "$D/fonts/binary_to_compressed_c.js" \
        "$D/imgui/misc/fonts/binary_to_compressed_c.cpp"
    unzip -p "$zip" "$path" > "$D/fonts/$symbol.ttf"
    node "$D/fonts/binary_to_compressed_c.js" -u32 "$D/fonts/$symbol.ttf" "$symbol" > "$D/fonts/$symbol.inc"
    echo "made fonts/$symbol.inc"
}
INTER=https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
INTER_SHA=9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e
MONO=https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip
MONO_SHA=6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf
font $INTER $INTER_SHA extras/ttf/Inter-Regular.ttf Inter_Regular
font $INTER $INTER_SHA extras/ttf/Inter-SemiBold.ttf Inter_SemiBold
font $MONO $MONO_SHA fonts/ttf/JetBrainsMono-Regular.ttf JetBrainsMono_Regular
