# thug2-web

How fast is a browser today? This project runs *Tony Hawk's Underground 2* (PC, 2004) in the browser as native
WebAssembly + WebGPU code and benchmarks it against the original game on the same machine.

> **Not affiliated with or endorsed by Activision, Neversoft or any other rights holder.** No game files are here or
> anywhere this project publishes: you play it with your own copy of the game, installed from your own discs, on your
> own machine. See [NOTICE.md](NOTICE.md) and [LICENSE](LICENSE).

## Method

No emulation. An emulator runs the original x86 machine code through an interpreter or JIT, so the browser would be
judged on the emulator's overhead, not on its own speed. Instead the game's own C++ source is compiled straight to
WebAssembly with full optimisation, and its Direct3D 9 renderer is backed by WebGPU. The browser gets the best code we
can give it.

The source comes from the community decompilation of THUG2 PC,
[CanuckAussieKev/thug2-decomp](https://github.com/CanuckAussieKev/thug2-decomp) (`upstream/`, git submodule). On
2026-10-30 it reached 8,529 of 8,529 game functions rewritten and started a standalone build target (the game built
without the original exe). It does not link standalone yet.

## Comparison

Three builds, one machine, one fixed run (same level, same recorded input or camera path, same resolution, frame
limiter off):

| Build | Code | Graphics | What it shows |
|---|---|---|---|
| Original | `THUG2.exe` (MSVC 2003, x86) | Direct3D 9 | The baseline |
| Native | the decomp's standalone build, same compiler flags as the web build where possible | Direct3D 9 | Same source, native |
| Web | the decomp compiled with Emscripten (wasm32, SIMD, -O3) | WebGPU | The browser |

Native vs Web isolates the browser's cost on identical source. Original vs Native shows what the decomp itself changed.
Measured: frame time (median, p95, p99), CPU time per frame, load time, memory.

## Layout

| Path | Contents |
|---|---|
| `upstream/` | The decomp, as a submodule: the fork `Loosemond/thug2-decomp`, branch `web` - the community repo with the web port's changes on top, on their way upstream. None of the decomp's code is in this repository |
| `port/` | The web platform layer: Win32/D3D9 shim headers, D3D9-on-WebGPU, WebAudio, file system, main loop |
| `web/` | The HTML shell and a local server (COOP/COEP headers for threads) |
| `bench/` | (to come) The fixed run, frame-time capture, the report |
| `docs/` | Feasibility notes, decisions, results |
| `game/` | Your game, installed from your discs by `port/tools/install_game.py` (gitignored, never published) |

## Milestones

1. **Set up.** Game files, Emscripten, plan. *(2026-10-30)*
2. **Compiles.** Every upstream source compiles with `em++` against the shim headers. Inline asm and MSVC-only
   constructs replaced. *(2026-10-30)*
3. **Links.** No call into the original exe remains: `GameFunction<>` calls go to the rewritten functions,
   `GameVariable<>` globals are defined, exe tables and vtables are in source. Upstream is doing this now for its own
   standalone build; follow it and contribute. *(2026-10-30)*
4. **Boots.** Game data read from `game/`, the main loop driven per frame, D3D9 calls translated to WebGPU, sound
   through WebAudio. Bink movies skipped. *(2026-10-30: the main menu and the Training level draw through WebGPU in
   Chrome. 2026-10-31: sound - effects, music and voice streams.)*
5. **Plays.** A level loads and skates at full speed in Chrome, Safari and Firefox. *(2026-10-30: the Training level
   skates in Chrome - `?args=-level%20TR`; speed not measured yet, Safari and Firefox not tried.)*
6. **Benchmark.** The three builds on the same machine, published as a report.

## Your game

The game is not in this repository, and the build doesn't include any of it. You need the PC release of *Tony Hawk's
Underground 2* on its three CDs (US English: the port reads the retail `THUG2.exe`'s data at its addresses, so another
build of the exe may not start). The installer puts it in `game/THUG2/Game`:

```sh
brew install msitools                                       # (Linux: apt install msitools libarchive-tools)
python3 port/tools/install_game.py /Volumes/THUG2_1 /Volumes/THUG2_2 /Volumes/THUG2_3
```

Give it the discs at once: mounted discs, folders copied off them, or `.iso` images of them, in any order. Disc 3 only
has movies, which the web build doesn't play, so it can be left out. A copy installed on Windows works too (the folder
with `Game/THUG2.exe`). Nothing is cracked or patched: the exe's code is SafeDisc-encrypted and never used, its
encrypted data isn't decrypted (the port rebuilds it from the decomp's source, `port/src/hook/exe_data.cpp`), and the
game's files are read as they are.

The game is served from this machine only (`web/serve.py` listens on 127.0.0.1; `--lan` adds your own network, for a
phone). Don't put the game's files on a public server.

## Running

```sh
git clone --recursive https://github.com/Loosemond/thug2-web.git && cd thug2-web
source <your emsdk>/emsdk_env.sh                                       # Emscripten (https://emscripten.org)
port/tools/fetch_deps.sh                                             # once: the pinned third-party sources (build/deps)
ninja -C build/web thug2 thug2-node                                  # (first: emcmake cmake -S port -B build/web -G Ninja -DCMAKE_BUILD_TYPE=Release)
python3 port/tools/make_manifest.py game/THUG2/Game build/web/manifest.txt   # the page's list of the game's files
python3 web/serve.py                                                  # http://localhost:8642/
```

- The page: `http://localhost:8642/` (the main menu), `http://localhost:8642/?args=-level%20TR` (free skate in the
  Training level - `-level <name>`, upstream's `game/front/start_level.h`). Needs WebGPU (Chrome).
- On a phone (iPhone: Safari's WebKit; WebGPU and threads need HTTPS): `sh port/tools/make_certs.sh` once, then
  `python3 web/serve.py --lan --https`. On the phone, `http://<this Mac>:8643/` hands out the certificate and says how
  to trust it; the game is at `https://<this Mac>:8642/`. Hold it sideways: touch controls (a d-pad, Ollie / Grab /
  Flip / Grind, spins, Nollie / Switch, View, Pause) and Bluetooth pads drive one SDL game controller
  (`port/web/page_input.cpp`). What the page logs lands in `build/web/page.log`.
- Keys: W/A/S/D move; the tricks on the numpad (8 grind, 4 flip, 6 grab, 2 ollie, 1/3 spins, 7/9 nollie/switch)
  or, for a laptop, the right hand's home row (J flip, K or Space ollie, L grab, I grind, H/; spins, U/O
  nollie/switch); the arrows are the d-pad; Enter pauses, F changes the view (upstream's `sdl_input.cpp`).
- Sound: DirectSound's buffers and Bink's music and voice streams (a Bink audio decoder of ours,
  `port/src/library/bink_audio.cpp`, checked against FFmpeg's) are mixed on a thread (`port/src/dsound/mixer.cpp`)
  into a ring an AudioWorklet plays; it starts on the first key or touch. `?mute` turns it off. Headless,
  `THUG2_SOUND_RAW=<file>` saves the mix (raw stereo floats, 44.1 kHz). The game's files are fetched whole when
  first read; a song or a voice line comes in the background instead, when the game opens it
  (`port/web/downloads.cpp`), so the game never waits on one while it plays. The stats line counts `gaps` (a
  stream ran dry) and `silence` (the speakers had nothing).
- `?nobc` decodes the DXT textures as a phone without BC texture support must; `?touch` shows the touch controls on a
  desktop. `PHONE=1 TAPS="#stick:up:3000 #a" node port/tools/drive_page.mjs <url> 30 shot.png` drives it as a phone.
- Indirect calls: after each link `port/tools/route_indirect_calls.mjs` puts a lookup (`thug2_code_slot`) in front of
  every `call_indirect`, so the original's code addresses work as function pointers without a 6.6-million-slot
  function table per thread (Safari spent 320 MB on each).
- Headless under Node (no drawing; logs frames a second and draws a frame):
  `THUG2_GAME=$PWD/game/THUG2/Game node build/web/thug2-node.js -level TR`. `THUG2_KEYS="600:Down 700:Return"`
  presses keys at those frames (SDL key names).

## Legal

- **No game files.** Nothing from the game - executables, levels, models, textures, sounds, music, movies - is in this
  repository or its history, and no build is published: everyone builds it and plays it with their own copy.
- **The decomp** (`upstream/`) reconstructs the game's code; its copyright belongs to the game's owners and no licence
  is granted for it (`upstream/LICENSE`, `upstream/NOTICE.md`). The same goes for the tables of the original exe that
  `port/src/hook/exe_data.cpp` and `member_tables.inc` rebuild from it. This project's own work is MIT (`LICENSE`).
- **No copy protection is circumvented**: nothing in the exe is decrypted or patched, and its protected code never runs.
- **Trademarks**: *Tony Hawk's Underground 2* and its names are Activision's, used here only to say what the software
  works with.
- **Rights holders**: open an issue, and it will be dealt with promptly.
