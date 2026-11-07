#!/usr/bin/env python3
"""Installs Tony Hawk's Underground 2 (PC) from your own discs into game/THUG2/Game, where web/serve.py serves it.

    python3 port/tools/install_game.py SOURCE [SOURCE ...] [--to game/THUG2/Game] [--force]

Each SOURCE is one of:
  - a mounted disc (macOS: /Volumes/THUG2_1), or a folder with a disc's files copied off it
  - a disc image (.iso), read with bsdtar
  - a copy of the game installed on Windows (the folder with Game/THUG2.exe, or that Game folder), copied as it is

The retail release is three CDs: a Windows Installer package (Tony Hawk's Underground 2.msi, disc 1) whose cabinets
are on discs 1 and 2, and files kept uncompressed on the discs (Setup/Data/Game: THUG2.exe on disc 1, the movies on
discs 2 and 3). Give the discs at once. The cabinets are unpacked with msiextract (msitools: `brew install msitools`,
`apt install msitools`), the uncompressed files are copied beside them. Disc 3 only has movies, which the web build
doesn't play: it can be left out.

Nothing is changed in the game's files, and nothing leaves this machine.
"""

import argparse
import hashlib
import os
import pathlib
import shutil
import stat
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_TO = ROOT / "game" / "THUG2" / "Game"
# The THUG2.exe the port is made for: the retail CD's (US English, linked 2004-09-14). The port reads its data
# sections at fixed addresses (port/src/hook/exe_image.cpp), so another build of the exe may not start.
KNOWN_EXE = {"c34ea46e041d08d7d85565a262473c29b90ed8a4d5b740d6cc04d4fe48d52347"}
# What the web build can't start without.
REQUIRED = ["THUG2.exe", "Data/pre", "Data/scripts", "Data/levels", "Data/streams/pcm/pcm.wad",
            "Data/streams/pcm/pcm.dat"]


def fail(message):
    print(f"install_game: {message}", file=sys.stderr)
    sys.exit(1)


def child(folder, name):
    """folder/name, matching the name without regard to case (a disc's names may be in either), or None."""
    if not folder.is_dir():
        return None
    exact = folder / name
    if exact.exists():
        return exact
    lower = name.lower()
    return next((entry for entry in folder.iterdir() if entry.name.lower() == lower), None)


def path_in(folder, relative):
    for part in pathlib.PurePosixPath(relative).parts:
        if folder is None:
            return None
        folder = child(folder, part)
    return folder


def files_under(folder):
    for top, folders, files in os.walk(folder):
        folders.sort()
        for name in sorted(files):
            yield pathlib.Path(top) / name


def copy_tree(source, to):
    """Copies the files under source into to, over what's there (contents only: a disc's files are read-only)."""
    count = 0
    for path in files_under(source):
        target = to / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists():
            target.unlink()
        shutil.copyfile(path, target)
        count += 1
    return count


def move_tree(source, to):
    count = 0
    for path in files_under(source):
        target = to / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        os.replace(path, target)
        count += 1
    return count


def remove(folder):
    def writable(function, path, _):
        os.chmod(path, stat.S_IRWXU)
        function(path)
    shutil.rmtree(folder, onexc=writable) if sys.version_info >= (3, 12) else shutil.rmtree(folder, onerror=writable)


def installed_game(source):
    """The Game folder of an installed copy, if source is one (or is that folder)."""
    for folder in (source, child(source, "Game")):
        if folder and child(folder, "THUG2.exe") and child(folder, "Data") and not child(folder, "Setup"):
            return folder
    return None


def disc_roots(source):
    """The folders under source (itself, or up to two levels down) that hold a disc's installer files."""
    roots = []
    for folder in [source, *sorted(p for p in source.glob("*") if p.is_dir()),
                   *sorted(p for p in source.glob("*/*") if p.is_dir())]:
        if folder.name.lower() == "directx":
            continue
        names = [entry.name.lower() for entry in folder.iterdir()] if folder.is_dir() else []
        if any(n.endswith((".cab", ".msi")) for n in names) or path_in(folder, "Setup/Data/Game"):
            roots.append(folder)
    return roots


def media_table(msi):
    """The package's Media rows: (disc prompt, cabinet name), from msiinfo."""
    out = subprocess.run(["msiinfo", "export", str(msi), "Media"], check=True, capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines()[3:]:  # (column names, column types, the table's name and key)
        fields = line.split("\t")
        if len(fields) >= 4:
            rows.append((fields[2], fields[3]))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("sources", nargs="+", type=pathlib.Path, metavar="SOURCE",
                        help="a mounted disc or a folder copied off one, a .iso image, or an installed copy")
    parser.add_argument("--to", type=pathlib.Path, default=DEFAULT_TO, help=f"default: {DEFAULT_TO.relative_to(ROOT)}")
    parser.add_argument("--force", action="store_true", help="install over a game that's already there")
    args = parser.parse_args()

    to = args.to.resolve()
    if child(to, "THUG2.exe") and not args.force:
        fail(f"{to} already has the game (--force installs over it)")
    for source in args.sources:
        if not source.exists():
            fail(f"{source}: not found")
    to.parent.mkdir(parents=True, exist_ok=True)
    # (the work folder next to the game: unpacking moves files into place instead of copying them)
    work = pathlib.Path(tempfile.mkdtemp(prefix=".install-", dir=to.parent))
    try:
        install(args.sources, to, work)
    finally:
        remove(work)
    check(to)


def install(sources, to, work):
    roots, copies = [], []
    for number, source in enumerate(sources, 1):
        source = source.resolve()
        if source.is_file() and source.suffix.lower() == ".iso":
            if not shutil.which("bsdtar"):
                fail("reading a .iso needs bsdtar (macOS has it; apt install libarchive-tools) - or mount it and "
                     "give the mounted folder")
            disc = work / f"disc{number}"
            disc.mkdir()
            print(f"reading {source.name}")
            subprocess.run(["bsdtar", "-xf", str(source), "-C", str(disc)], check=True)
            source = disc
        elif not source.is_dir():
            fail(f"{source}: not a folder or a .iso")
        game = installed_game(source)
        if game:
            copies.append(game)
            continue
        found = disc_roots(source)
        if not found:
            fail(f"{source}: no THUG2 disc or installed game in it")
        roots += found

    for game in copies:
        print(f"copying {game}")
        copy_tree(game, to)
    if not roots:
        return

    msis = [p for root in roots for p in root.iterdir()
            if p.suffix.lower() == ".msi" and not p.name.lower().startswith("isscript")]
    if not msis:
        fail("no Tony Hawk's Underground 2.msi in the sources: it's on disc 1")
    for tool, package in (("msiextract", "msitools"), ("msiinfo", "msitools")):
        if not shutil.which(tool):
            fail(f"unpacking the discs needs {tool} (brew install {package} / apt install {package})")

    # The package and every cabinet it names, side by side, as msiextract wants them.
    staged = work / "package"
    staged.mkdir()
    msi = staged / "thug2.msi"
    msi.symlink_to(msis[0])
    missing = {}
    for prompt, cabinet in media_table(msis[0]):
        if not cabinet or cabinet.startswith("#"):  # (uncompressed files, or a cabinet inside the package)
            continue
        found = next((p for root in roots if (p := child(root, cabinet)) and p.is_file()), None)
        if found:
            (staged / cabinet).symlink_to(found)
        else:
            missing.setdefault(prompt, []).append(cabinet)
    if missing:
        fail("missing " + "; ".join(f"{', '.join(cabs)} (on {prompt})" for prompt, cabs in missing.items()) +
             ": give that disc too")

    print("unpacking the cabinets (a few minutes)")
    out = work / "out"
    subprocess.run(["msiextract", "-C", str(out), str(msi)], check=True, stdout=subprocess.DEVNULL)
    games = [pathlib.Path(top) for top, folders, _ in os.walk(out)
             if pathlib.Path(top).name.lower() == "game" and any(f.lower() == "data" for f in folders)]
    if len(games) != 1:
        fail(f"the package unpacked without one Game folder ({len(games)} found)")
    print(f"  {move_tree(games[0], to)} files")

    for root in roots:
        loose = path_in(root, "Setup/Data/Game")
        if loose:
            print(f"copying the uncompressed files from {root}")
            print(f"  {copy_tree(loose, to)} files")


def check(to):
    gone = [name for name in REQUIRED if path_in(to, name) is None]
    if gone:
        fail(f"{to} is missing {', '.join(gone)}: a disc left out? (THUG2.exe is on disc 1)")
    exe = path_in(to, "THUG2.exe")
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    if digest not in KNOWN_EXE:
        print(f"warning: this THUG2.exe (SHA-256 {digest}) isn't the US retail CD's the port is made for; another "
              "build's data may sit at other addresses, and the game may not start", file=sys.stderr)
    if path_in(to, "Data/movies/bik") is None:
        print("(no movies: the web build doesn't play them)")
    files = list(files_under(to))
    size = sum(p.stat().st_size for p in files)
    print(f"installed: {len(files)} files, {size / 1e9:.1f} GB in {to}")

    manifest = ROOT / "build" / "web" / "manifest.txt"
    if to == DEFAULT_TO and manifest.parent.is_dir():
        subprocess.run([sys.executable, str(ROOT / "port" / "tools" / "make_manifest.py"), str(to), str(manifest)],
                       check=True)
        print("next: python3 web/serve.py, then http://localhost:8642/")
    elif to == DEFAULT_TO:
        print("next: the build (README: Running), then python3 web/serve.py")


if __name__ == "__main__":
    main()
