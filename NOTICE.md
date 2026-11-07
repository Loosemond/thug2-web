# Notice

**This project is not affiliated with, endorsed by, sponsored by or approved by Activision Publishing, Inc., Neversoft
Entertainment, or any other owner of the games, trademarks or content it relates to.**

Tony Hawk's Underground 2 is a trademark or registered trademark of Activision Publishing, Inc. and/or its respective
owners. All other trademarks belong to their owners. They are named here only to describe what the software works
with.

## Purpose

A non-commercial project for research, interoperability, preservation and education: it measures how fast a browser
runs a real 2004 PC game compiled to WebAssembly, and keeps that game playable on today's machines. Nothing here is
sold.

## What is and isn't here

- **No game files.** This repository contains no executables, levels, models, textures, sounds, music, movies or other
  assets from the game, and the project publishes no build of it. To use it you need your own legally obtained copy of
  Tony Hawk's Underground 2 for PC; `port/tools/install_game.py` installs it from your discs onto your machine, and
  `web/serve.py` serves it to your own browser.
- **No circumvention.** The game's executable is read only for its unprotected data. Its SafeDisc-encrypted code and
  data are never decrypted, patched or run; what the port needs of the encrypted data is rebuilt from the
  decompilation's source.
- **The reconstructed game code** comes from the community decompilation in `upstream/` (its own `NOTICE.md` and
  `LICENSE`): its copyright belongs to the game's owners and no licence is granted for it. The same applies to the
  tables of the original executable that this project rebuilds from it (see `LICENSE`).
- **Original work** (the web platform layer, the page, the server, the tools, the documentation) is MIT licensed (see
  `LICENSE`).

## Rights holders

If you hold rights to any of this material and want it changed or removed, please open an issue on the repository or
contact the maintainer through GitHub, and it will be dealt with promptly.
