#!/usr/bin/env bash
# One-time setup: Emscripten, HashLink (patched), SDL2-era sources, native libhl,
# and (optional) Haxe for the test programs.
source "$(dirname "$0")/config.sh"

echo "== Emscripten =="
if [ ! -d "$EMSDK" ]; then
  git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK"
fi
( cd "$EMSDK" && ./emsdk install "$EMSDK_VERSION" && ./emsdk activate "$EMSDK_VERSION" )

echo "== HashLink (commit $HL_COMMIT) =="
if [ ! -d "$HL/.git" ]; then
  git clone https://github.com/HaxeFoundation/hashlink.git "$HL"
fi
( cd "$HL" && git checkout -q "$HL_COMMIT" )

echo "== Patch HashLink (gc.c, file.c, fmt.c, openal.c) -- see runtime/patches/ =="
git -C "$HL" checkout -- src/gc.c src/std/file.c libs/fmt/fmt.c libs/openal/openal.c 2>/dev/null || true
for pair in src/gc.c:gc.c src/std/file.c:file.c libs/fmt/fmt.c:fmt.c libs/openal/openal.c:openal.c; do
  rel="${pair%%:*}"; pat="${pair##*:}"
  sed -i 's/\r$//' "$HL/$rel"                       # normalize line endings so patches apply
  patch -s "$HL/$rel" < "$REPO/runtime/patches/$pat.patch"
done

echo "== SDL2-era sdl.c/gl.c/GLImports.h (tag $SDL_TAG) + GLES3/web patches =="
mkdir -p "$SDL112"
for f in sdl.c gl.c GLImports.h; do
  curl -fsSL -o "$SDL112/$f" \
    "https://raw.githubusercontent.com/HaxeFoundation/hashlink/$SDL_TAG/libs/sdl/$f"
done
for pat in sdl.c gl.c; do
  sed -i 's/\r$//' "$SDL112/$pat"
  patch -s "$SDL112/$pat" < "$REPO/runtime/patches/$pat.patch"
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
