#!/usr/bin/env bash
# Start the web harness server. Does NOT launch a browser -- open the URL
# yourself in Chrome >=137 (needs WebGL2 + SharedArrayBuffer + WebAssembly JSPI).
#
# The server sends COOP/COEP headers (for SharedArrayBuffer) and supports HTTP
# Range (the game streams res.pak via async fetch). web/deadcells.{js,wasm} must
# be built (scripts/40_build_wasm.sh) and res.pak must live in $DC_GAME_DIR.
source "$(dirname "$0")/config.sh"
PORT="${1:-8088}"

[ -f "$REPO/web/deadcells.js" ] || { echo "missing web/deadcells.js -- run scripts/40_build_wasm.sh"; exit 1; }
[ -f "$GAME_DIR/res.pak" ]      || { echo "missing res.pak in $GAME_DIR (set DC_GAME_DIR)"; exit 1; }

echo "------------------------------------------------------------"
echo "Open this in Chrome >=137 (WebGL2 + SharedArrayBuffer + JSPI):"
echo "    http://127.0.0.1:$PORT/"
echo
echo "NOTE: this script intentionally does NOT open a browser for you."
echo "Browser console output is mirrored to web/browser.log"
echo "Press Ctrl+C to stop the server."
echo "------------------------------------------------------------"
exec python3 "$REPO/web/serve.py" "$PORT" "$GAME_DIR"
