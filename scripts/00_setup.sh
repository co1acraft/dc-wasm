#!/usr/bin/env bash
# One-time setup: Emscripten, HashLink (patched), SDL2-era sources, native libhl,
# and (optional) Haxe for the test programs.
source "$(dirname "$0")/config.sh"

echo "== Emscripten =="
if [ ! -d "$EMSDK" ]; then
  git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK"
fi
( cd "$EMSDK" && ./emsdk install latest && ./emsdk activate latest )

echo "== HashLink (commit $HL_COMMIT) =="
if [ ! -d "$HL/.git" ]; then
  git clone https://github.com/HaxeFoundation/hashlink.git "$HL"
fi
( cd "$HL" && git checkout -q "$HL_COMMIT" 2>/dev/null || echo "  (using current checkout)" )

echo "== Apply patches (file.c explicit-offset IO + fd pinning; gc.c disable) =="
python3 "$REPO/runtime/patch_hashlink.py" "$HL"

echo "== SDL2-era sdl.c/gl.c/GLImports.h (tag $SDL_TAG) =="
mkdir -p "$SDL112"
for f in sdl.c gl.c GLImports.h; do
  [ -f "$SDL112/$f" ] || curl -fsSL -o "$SDL112/$f" \
    "https://raw.githubusercontent.com/HaxeFoundation/hashlink/$SDL_TAG/libs/sdl/$f"
done

echo "== Build native libhl + hl (for the transpiler/tools and a native sanity run) =="
( cd "$HL" && make hl )   # builds libhl.so + hl (pcre is bundled; no system deps)

echo "== Haxe (optional, for test/ programs) =="
if [ ! -d "$HAXE_DIR" ]; then
  curl -fsSL -o "$VENDOR/haxe.tar.gz" \
    https://github.com/HaxeFoundation/haxe/releases/download/4.3.6/haxe-4.3.6-linux64.tar.gz || true
  if [ -f "$VENDOR/haxe.tar.gz" ]; then
    mkdir -p "$HAXE_DIR" && tar xzf "$VENDOR/haxe.tar.gz" -C "$HAXE_DIR" --strip-components=1 || true
  fi
fi

echo "== setup done =="
