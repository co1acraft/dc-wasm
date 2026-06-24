#!/usr/bin/env python3
# Static server for the Dead Cells wasm harness.
#  * sends COOP/COEP headers  -> required for SharedArrayBuffer (wasm threads)
#  * supports HTTP Range       -> the game streams res.pak via fetch Range requests
#  * serves ./ (harness/wasm/js) at /, and the game dir at /files/
#  * POST /log appends to browser.log (handy for headless capture)
#
# Usage:  python3 serve.py [port] [game_dir]
#   game_dir defaults to $DC_GAME_DIR or the depot path. res.pak must live there.
import http.server, os, re, sys

WEBROOT  = os.path.dirname(os.path.abspath(__file__))
GAMEDIR  = (sys.argv[2] if len(sys.argv) > 2 else
            os.environ.get("DC_GAME_DIR",
            "/home/ubuntu/Documents/deadcells/depots/588653/23762174"))
LOG = os.path.join(WEBROOT, "browser.log")

class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _coop(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "cross-origin")
    def do_POST(self):
        if self.path == "/log":
            n = int(self.headers.get("Content-Length", 0))
            data = self.rfile.read(n).decode("utf-8", "replace")
            with open(LOG, "a") as f: f.write(data + "\n")
            print("[browser]", data, flush=True)
            self.send_response(204); self._coop(); self.end_headers()
        else:
            self.send_response(404); self.end_headers()
    def _resolve(self):
        if self.path.startswith("/files/"):
            return os.path.join(GAMEDIR, self.path[len("/files/"):].split("?")[0])
        p = self.path.split("?")[0]
        if p == "/": p = "/deadcells.html"
        return os.path.join(WEBROOT, p.lstrip("/"))
    def _ctype(self, path):
        return ("text/javascript" if path.endswith(".js") else
                "application/wasm"  if path.endswith(".wasm") else
                "text/html"         if path.endswith(".html") else "application/octet-stream")
    def do_HEAD(self): self._serve(True)
    def do_GET(self):  self._serve(False)
    def _serve(self, head):
        path = self._resolve()
        if not os.path.isfile(path):
            self.send_response(404); self._coop(); self.end_headers(); return
        size = os.path.getsize(path); start, end, partial = 0, size - 1, False
        rng = self.headers.get("Range")
        if rng:
            m = re.match(r"bytes=(\d+)-(\d*)", rng)
            if m:
                start = int(m.group(1)); end = int(m.group(2)) if m.group(2) else size - 1
                end = min(end, size - 1); partial = True
        length = end - start + 1
        self.send_response(206 if partial else 200)
        self.send_header("Content-Type", self._ctype(path))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(length))
        if partial: self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self._coop(); self.end_headers()
        if head: return
        try:
            with open(path, "rb") as f:
                f.seek(start); rem = length
                while rem > 0:
                    c = f.read(min(1 << 20, rem))
                    if not c: break
                    self.wfile.write(c); rem -= len(c)
        except (BrokenPipeError, ConnectionResetError): pass

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8088
    print(f"serving {WEBROOT} (+ /files -> {GAMEDIR}) on http://127.0.0.1:{port}", flush=True)
    http.server.ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()
