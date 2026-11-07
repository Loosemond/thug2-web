# Feasibility, 2026-10-30

## Game files

- The retail PC release, US English: three CDs with an InstallShield MSI and external cabs (discs 1 and 2) and files
  kept uncompressed (`Setup/Data/Game`: `THUG2.exe` on disc 1, the movies on discs 2 and 3).
- Unpacked with `msiextract` (brew `msitools`) plus the uncompressed files into `game/THUG2/Game`
  (`port/tools/install_game.py`): 2.2 GB, 10,687 files (`Data/pre`, `levels`, `models`, `streams`, `movies/bik`,
  ...).
- `THUG2.exe`: 3,926,726 bytes, SHA-256 `c34ea46e041d08d7d85565a262473c29b90ed8a4d5b740d6cc04d4fe48d52347`, linked
  2004-09-14. It has the `stxt774` and `stxt371` sections of **SafeDisc**. SafeDisc's driver (`secdrv.sys`) is blocked
  on Windows 10 and 11, and it checks the physical disc, so this exe will not start from an extracted copy.
  Upstream's development builds use a different "reference build" exe and do not support this retail one.

## Upstream

`CanuckAussieKev/thug2-decomp` at `0b48eb9` (2026-10-30): 8,529 of 8,529 game functions rewritten (1,625.9 KB of
code). Libraries (D3DX9, GameSpy, the MSVC runtime) are to be linked, not rewritten. 667 `.cpp` files under
`src/game`, about 558k lines with headers. C++20, MSVC, Win32 only. SDL2 already does the window and input
(PartyMod's work), so Emscripten's SDL2 port covers that part.

Still between the source and a standalone game (counts in `src/game`):

| What | Count | Notes |
|---|---:|---|
| `GameFunction<>(addr)` calls into the original exe | 1,775 in 344 files | Upstream's `tools/game_function_map.ts` (added today) maps them to the rewritten functions |
| `GameVariable<>(addr)` globals in the exe's `.data` | 2,579 in 461 files | Need definitions, and their initial values from the exe |
| `REPLACE(addr, fn)` lines | ~9,000 | Expand to nothing in a standalone build |
| `__declspec(naked)` register-ABI stubs | 105 in 39 files | Only exist so the original exe can call our code; not needed standalone |
| MSVC `__asm` blocks | 186 in 78 files | Mostly x87 `fsin`/`fcos`/`fptan`/`fpatan`; replace with `<cmath>` |

## Renderer

Direct3D 9, mostly the fixed-function pipeline with texture stage states, 8 `ps.1.1` pixel shaders, a few vertex
shaders. About 40 distinct device calls (`SetRenderState` 90 sites, `SetTextureStageState` 56, `SetSamplerState` 33,
`SetTransform` 22, `DrawPrimitiveUP` 17, `DrawIndexedPrimitive` 15, ...). D3DX: only `D3DXMatrixMultiply`. A D3D9
subset on WebGPU (state-keyed generated WGSL for the fixed-function stages, the 8 pixel shaders by hand) is a bounded
job.

## Why wasm32 fits

The original is 32-bit. wasm32 also has 4-byte pointers, little-endian, and `long` = 4 bytes, so offset-based struct
access and on-disk formats with pointer-sized fields keep their layout. Things that differ and need care:
`wchar_t` (use `-fshort-wchar`), `long double`, bitfield layout (MSVC vs Itanium), and virtual destructors (MSVC uses
one vtable slot, Itanium two) wherever code indexes vtables by hand.

## Native builds need a 32-bit target

The source is tied to the original's 32-bit layout (counts in `src/game`):

| What | Count |
|---|---:|
| Fields read at fixed byte offsets (`Field<T>(p, 0x2C)`, `At<T>(...)`, `p + 0x14`) | ~2,000 |
| `static_assert(offsetof(...) == N)` / `static_assert(sizeof(...) == N)` | 361 / 272 |
| Pointers cast to `uint32_t` | ~270 |
| Data files with 4-byte pointers relocated in place (`Relocate*`) | the script and asset loaders |

wasm32 matches all of it. macOS runs only 64-bit code (no 32-bit processes, and Apple Silicon has no 32-bit mode),
so a native Mac build needs the code made 64-bit clean first: weeks of work, and it diverges from upstream. A native
32-bit build works today on x86 Linux (`-m32`) or Windows.

## The exe's data at its own addresses

The retail `THUG2.exe`'s data matches upstream's reference build address for address: `c_dfDIJoystick2` at
0x00667204 = {24, 16, 1, 272, 164}, `c_dfDIKeyboard` 0x00666FFC, `c_dfDIMouse2` 0x00666DF4, `IID_IDirectSound3DListener`
at 0x00666B44, pi/360 at 0x00648800. Only `.text` is encrypted (SafeDisc, 8.0 bits/byte), and we never run it.

Wasm memory is a flat space from 0, so the build can keep the low 8 MB to itself (`-sGLOBAL_BASE=0x800000`) and load
the user's exe's `.rdata` and `.data` (plus `.data`'s zeroed tail) at their original addresses, 0x645000-0x7DD000, at
start-up. Then `GameVariable<T>(address)` and every raw table/string address work unchanged, with the original's
initial values. What stays is code addresses: `GameFunction<>` calls, and code pointers inside the data (vtables,
script function tables), both mapped through the `REPLACE(address, function)` lines. A native 32-bit build can do the
same with `mmap` at those addresses.

## Graphics backends

D3D9 is a state machine like OpenGL, so the D3D9 layer gets a small backend interface: WebGPU first, WebGL2 second,
sharing the fixed-function shader generator. Natively the same two backends run on Dawn (Chrome's WebGPU) and ANGLE
(Chrome's WebGL), so native vs browser compares the same graphics code too.

## x87 maths

Upstream keeps the original's x87 code (`fsin`, `fpatan`, 80-bit registers) in `__asm` blocks so results match the
original bit for bit, which online play with original clients needs. Wasm has only IEEE 32/64-bit floats, so the port
uses `double` throughout: single player is unaffected, results differ from the original in the last bits. The native
control build uses the same portable code, so both measure identical source.

- `mth/x87_math.h`: portable `#else` branch by hand.
- `port/tools/x87_to_cpp.py`: runs each pure-x87 block on a symbolic FPU stack and writes the C++ twin under
  `#else` (66 blocks).
- `port/tools/guard_naked.py`: the 81 `__declspec(naked)` register stubs only serve the original exe; compiled out
  elsewhere.
- About 20 blocks are left by hand: x87 values carried across blocks, and calls into the original exe
  (`call 0x00495130`, the C runtime's `_CIacos`), which belong with the link work.

## First compile survey

`port/tools/compile_survey.sh`: every `src/game/*.cpp` through `em++ -fsyntax-only -std=c++20 -fms-extensions` with
empty Windows headers. 6 minutes on an Apple M4 laptop with 8 jobs.

- 667 files, **82 compile clean**, 585 fail.
- Top errors: undeclared identifier 939, unknown type 664 (Win32/D3D9/DirectInput types: `DWORD` 122, `HRESULT` 74,
  `IDirect3DDevice9` 73, `_stricmp` 56, `HANDLE` 50, ...), redefinition 545 (all one cause: `src/hook/hook.h`'s
  `CalleePop` specialisations per calling convention collapse into one on wasm), MS inline asm 536, `naked` not
  supported 105.

The failures are shim headers and three mechanical patterns, not design problems.
