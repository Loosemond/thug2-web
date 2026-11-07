#!/usr/bin/env python3
"""The local server for the web build: the page and build output (build/web), and the game folder at /game/ - on this
machine only (127.0.0.1) unless --lan is given. Sends the cross-origin isolation headers wasm threads need.

    python3 web/serve.py [--port 8642] [--lan] [--https]

A phone needs --lan --https: its browser gives WebGPU and threads only to a secure origin (http://localhost is one,
http://<this machine> isn't). --https uses certs/ (port/tools/make_certs.sh) and also serves, over plain HTTP on the
next port, a page that hands out certs/ca.crt for the phone to install and trust.

The page sends what it logs to POST /log; it lands in build/web/page.log (the phone's console, readable here).
"""

import argparse
import datetime
import functools
import http.server
import os
import pathlib
import ssl
import struct
import threading

ROOT = pathlib.Path(__file__).resolve().parents[1]
GAME = ROOT / "game" / "THUG2" / "Game"
CERTS = ROOT / "certs"
PAGE_LOG = ROOT / "build" / "web" / "page.log"
ENTRIES = "/game/Data/streams/pcm/entries/"


@functools.cache
def wad_entries():
    """pcm.wad's entries by checksum (8 hex digits): offset and size, from pcm.dat."""
    data = (GAME / "Data" / "streams" / "pcm" / "pcm.dat").read_bytes()
    (count,) = struct.unpack_from("<I", data)
    return {f"{checksum:08x}": (offset, size) for checksum, offset, size in struct.iter_unpack("<3I", data[4:4 + count * 12])}


class Handler(http.server.SimpleHTTPRequestHandler):
    # (keep-alive: the game fetches its files one request each, and over TLS a connection apiece would be slow)
    protocol_version = "HTTP/1.1"
    timeout = 120
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm",
                      ".js": "text/javascript", ".mjs": "text/javascript", ".crt": "application/x-x509-ca-cert"}
    log_lock = threading.Lock()

    def translate_path(self, path):
        clean = path.split("?", 1)[0].split("#", 1)[0]
        if clean == "/game/manifest.txt":  # (made by the build: port/tools/make_manifest.py)
            return str(ROOT / "build" / "web" / "manifest.txt")
        if clean.startswith("/game/"):
            return str(GAME / http.server.urllib.parse.unquote(clean[len("/game/"):]))
        if clean in ("/", "/index.html"):
            return str(ROOT / "web" / "index.html")
        if clean == "/thug2-ca.crt":
            return str(CERTS / "ca.crt")
        if clean.startswith("/web/"):
            return str(ROOT / clean.lstrip("/"))
        return str(ROOT / "build" / "web" / clean.lstrip("/"))

    # (pcm.wad's entries, each a file of its own: the page fetches a voice line or a stream alone, not the 104 MiB wad)
    def send_head(self):
        clean = self.path.split("?", 1)[0]
        if not clean.startswith(ENTRIES):
            return super().send_head()
        entry = wad_entries().get(clean[len(ENTRIES):].removesuffix(".bik"))
        if not entry:
            self.send_error(404)
            return None
        offset, size = entry
        file = open(GAME / "Data" / "streams" / "pcm" / "pcm.wad", "rb")
        file.seek(offset)
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(size))
        self.end_headers()
        return Slice(file, size)

    def do_POST(self):
        if self.path != "/log":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length") or 0)
        if length > 1 << 20:
            self.send_error(413)
            return
        text = self.rfile.read(length).decode("utf-8", "replace")
        stamp = datetime.datetime.now().strftime("%H:%M:%S")
        lines = "".join(f"{stamp} {self.client_address[0]} {line}\n" for line in text.splitlines())
        with self.log_lock, PAGE_LOG.open("a") as log:
            log.write(lines)
        self.send_response(204)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # (the page and the build never from a cache: a browser restoring its tabs takes cached files without asking,
        # which ran a day-old build; the game's own files don't change, so those are only checked)
        game = self.path.startswith("/game/") and not self.path.startswith("/game/manifest.txt")
        self.send_header("Cache-Control", "no-cache" if game else "no-store")
        super().end_headers()

    def log_request(self, code="-", size="-"):
        if str(code) not in ("200", "204", "304"):
            super().log_request(code, size)


class Slice:
    """The next `left` bytes of a file, read as a file."""

    def __init__(self, file, left):
        self.file, self.left = file, left

    def read(self, size=-1):
        size = self.left if size is None or size < 0 else min(size, self.left)
        data = self.file.read(size) if size > 0 else b""
        self.left -= len(data)
        return data

    def close(self):
        self.file.close()


class SetupHandler(http.server.BaseHTTPRequestHandler):
    """Plain HTTP, next to the HTTPS server: the certificate authority to install, and how."""

    https_port = 8642

    def do_GET(self):
        host = (self.headers.get("Host") or "localhost").rsplit(":", 1)[0]
        if self.path == "/thug2-ca.crt":
            body = (CERTS / "ca.crt").read_bytes()
            kind = "application/x-x509-ca-cert"
        else:
            game = f"https://{host}:{self.https_port}/"
            body = SETUP_PAGE.format(game=game, level=game + "?args=-level%20TR").encode()
            kind = "text/html; charset=utf-8"
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


SETUP_PAGE = """<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>THUG2 Web setup</title>
<style>body{{font:17px/1.5 -apple-system,system-ui,sans-serif;margin:24px;max-width:36em;background:#0e1013;color:#d8dde4}}
a{{color:#e8b44c}} li{{margin:.6em 0}} code{{font-size:15px}}</style></head><body>
<h1>THUG2 Web on this phone</h1>
<p>Once per phone: the game's server uses a certificate of its own, which the phone has to trust.</p>
<ol>
<li><a href="/thug2-ca.crt">Download the certificate</a> and tap <b>Allow</b>.</li>
<li>Settings &rarr; General &rarr; VPN &amp; Device Management &rarr; <b>THUG2 Web local CA</b> &rarr; Install.</li>
<li>Settings &rarr; General &rarr; About &rarr; Certificate Trust Settings &rarr; turn on <b>THUG2 Web local CA</b>.</li>
<li>Open the game: <a href="{game}">{game}</a><br>or straight into the Training level:
<a href="{level}">{level}</a></li>
</ol>
<p>Hold the phone sideways. Share &rarr; Add to Home Screen opens it full screen.</p>
</body></html>"""


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    context = None

    # (the TLS handshake on the request's thread, not the one accepting connections)
    def get_request(self):
        sock, address = self.socket.accept()
        if self.context:
            sock = self.context.wrap_socket(sock, server_side=True, do_handshake_on_connect=False)
        return sock, address

    def finish_request(self, request, client_address):
        if self.context:
            try:
                request.settimeout(30)
                request.do_handshake()
                request.settimeout(None)
            except (ssl.SSLError, OSError):
                return
        super().finish_request(request, client_address)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8642)
    parser.add_argument("--lan", action="store_true", help="listen on every interface (the LAN), not just this machine")
    parser.add_argument("--https", action="store_true", help="HTTPS with certs/ (port/tools/make_certs.sh)")
    args = parser.parse_args()
    host = "0.0.0.0" if args.lan else "127.0.0.1"
    server = Server((host, args.port), functools.partial(Handler, directory=str(ROOT)))
    scheme = "http"
    if args.https:
        if not (CERTS / "server.crt").exists():
            parser.error("no certs/server.crt: run port/tools/make_certs.sh first")
        server.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        server.context.load_cert_chain(CERTS / "server.crt", CERTS / "server.key")
        scheme = "https"
        SetupHandler.https_port = args.port
        setup = Server((host, args.port + 1), SetupHandler)
        threading.Thread(target=setup.serve_forever, daemon=True).start()
        print(f"setup (the certificate for a phone): http://<this machine>:{args.port + 1}/")
    print(f"{scheme}://{'localhost' if not args.lan else '<this machine>'}:{args.port}/  (game folder: {GAME})")
    server.serve_forever()


if __name__ == "__main__":
    main()
