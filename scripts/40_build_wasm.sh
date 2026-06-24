#!/usr/bin/env bash
# Build the wasm targets. Compiles all objects once (the dc_f* chunks at -O1 are
# the slow part, several minutes), then links two variants:
#   build/deadcells-node.js  -> runs in node (NODEFS), reaches the SDL boundary
#   web/deadcells.js         -> runs in a browser, main thread (SDL2/WebGL2 + JSPI + async fetch)
source "$(dirname "$0")/config.sh"
emcc_env
DC="$BUILD/dc"; WOBJ="$BUILD/wobj"; mkdir -p "$WOBJ"
[ -f "$DC/dc.h" ] || { echo "run 20_transpile.sh first"; exit 1; }

PT="-pthread"
# HL's try/catch uses setjmp/longjmp. Emscripten's default lowers these to JS
# invoke_* trampolines, whose JS stack frames block JSPI from suspending the wasm
# stack (SuspendError). Lower longjmp to native wasm exception-handling instead so
# the whole render/IO call stack is pure wasm and JSPI can suspend it.
SJLJ="-sSUPPORT_LONGJMP=wasm"
INC="-I $DC -I $HL/src"
PCRE_DEFS="-D PCRE2_CODE_UNIT_WIDTH=16 -D HAVE_CONFIG_H -D PCRE2_STATIC"
SDLINC="-I $HL/src -I $HL/include -I $SDL112"
J="$(nproc)"

ccobj(){ # ccobj <src> <out> <extra flags...>
  local src="$1" out="$2"; shift 2
  [ -f "$out" ] && [ "$out" -nt "$src" ] && return 0
  emcc -c $PT $SJLJ -O1 -w "$@" "$src" -o "$out"
}
# -O0 variant for libhl runtime + native objects: LLVM keeps C locals in addressable
# memory (shadow stack) so HashLink's conservative GC can scan their pointers.
ccobj0(){ local src="$1" out="$2"; shift 2; [ -f "$out" ] && [ "$out" -nt "$src" ] && return 0; emcc -c $PT $SJLJ -O0 -w "$@" "$src" -o "$out"; }
wait_slots(){ while [ "$(jobs -r | wc -l)" -ge "$J" ]; do wait -n; done; }

echo "== compile game chunks + units (-O1; slow) =="
for c in "$DC"/dc_f*.c "$DC"/dc_types.c "$DC"/dc_main.c "$DC"/dc_stubs.c; do
  ccobj "$c" "$WOBJ/$(basename "$c").wo" $INC & wait_slots
done
ccobj0 "$REPO/runtime/ssl_impl.c" "$WOBJ/ssl_impl.wo" -I "$HL/src" & wait_slots
ccobj0 "$HL/libs/ui/ui_stub.c"    "$WOBJ/ui_stub.wo"  -I "$HL/src" & wait_slots
ccobj0 "$SDL112/gl.c"             "$WOBJ/sdlgl.wo"  -sUSE_SDL=2 -sFULL_ES3 $SDLINC & wait_slots
ccobj0 "$SDL112/sdl.c"            "$WOBJ/sdlmain.wo" -sUSE_SDL=2 $SDLINC & wait_slots
# browser variant: yields at SDL_GL_SwapWindow (DC_BROWSER) so the blocking native
# render loop returns control to the page each frame under JSPI.
ccobj0 "$SDL112/sdl.c"            "$WOBJ/sdlmain_browser.wo" -DDC_BROWSER -sUSE_SDL=2 $SDLINC & wait_slots
# fmt lib (image/zlib/vorbis decoding) against Emscripten codec ports
PORTS="-sUSE_LIBPNG=1 -sUSE_ZLIB=1 -sUSE_VORBIS=1 -sUSE_OGG=1"
ccobj0 "$HL/libs/fmt/fmt.c"       "$WOBJ/fmt.wo" $PORTS -I "$HL/src" -I "$HL/include/minimp3" & wait_slots
ccobj0 "$HL/libs/fmt/sha1.c"      "$WOBJ/fmt_sha1.wo" -I "$HL/src" & wait_slots
# OpenAL bindings -> Emscripten's libopenal.js (Web Audio). AL headers come from the
# Emscripten sysroot; -lopenal at link time provides the implementation. EFX is absent
# in Emscripten so those natives stay stubbed (see bc2c_emit.h openal handling).
ccobj0 "$HL/libs/openal/openal.c" "$WOBJ/openal.wo" -I "$HL/src" & wait_slots

echo "== compile libhl runtime (gc patched, std minus file, pcre) =="
ccobj0 "$HL/src/gc.c" "$WOBJ/gc.wo" -I "$HL/src" -I "$HL/include/pcre" $PCRE_DEFS & wait_slots
for s in $STD; do [ "$s" = file ] && continue; ccobj0 "$HL/src/std/$s.c" "$WOBJ/std_$s.wo" -I "$HL/src" -I "$HL/include/pcre" $PCRE_DEFS & wait_slots; done
for p in $(pcre_srcs); do ccobj0 "$p" "$WOBJ/$(basename "$p").wo" -I "$HL/src" -I "$HL/include/pcre" $PCRE_DEFS & wait_slots; done
# file.c: two variants (node = NODEFS readSync; browser = async fetch via JSPI)
emcc -c $PT $SJLJ -O0 -w           -I "$HL/src" -I "$HL/include/pcre" $PCRE_DEFS "$HL/src/std/file.c" -o "$WOBJ/file_node.wo"
emcc -c $PT $SJLJ -O0 -w -DDC_BROWSER -I "$HL/src" -I "$HL/include/pcre" $PCRE_DEFS "$HL/src/std/file.c" -o "$WOBJ/file_browser.wo"
wait

RT_COMMON=$(ls "$WOBJ"/gc.wo "$WOBJ"/std_*.wo "$WOBJ"/pcre2_*.wo)
# GAME excludes the SDL main object; each link adds its own (node vs browser-yield).
GAME=$(ls "$WOBJ"/dc_*.wo "$WOBJ"/ssl_impl.wo "$WOBJ"/ui_stub.wo "$WOBJ"/sdlgl.wo "$WOBJ"/fmt.wo "$WOBJ"/fmt_sha1.wo "$WOBJ"/openal.wo)

echo "== link node build =="
emcc -O1 -w $PT $SJLJ -sUSE_SDL=2 -sFULL_ES3 -sGL_ENABLE_GET_PROC_ADDRESS $PORTS \
  $GAME "$WOBJ/sdlmain.wo" "$WOBJ/file_node.wo" $RT_COMMON \
  -s PTHREAD_POOL_SIZE=40 -s PTHREAD_POOL_SIZE_STRICT=0 \
  -s INITIAL_MEMORY=1610612736 -s ALLOW_MEMORY_GROWTH=1 -s MAXIMUM_MEMORY=4294967296 \
  -s EXIT_RUNTIME=0 -s ERROR_ON_UNDEFINED_SYMBOLS=0 -s FORCE_FILESYSTEM=1 -lnodefs.js -lopenal \
  -s MODULARIZE=1 -s EXPORT_NAME=createDC -s INVOKE_RUN=0 -s "EXPORTED_RUNTIME_METHODS=callMain,FS,NODEFS" \
  -o "$BUILD/deadcells-node.js"
echo "  -> $BUILD/deadcells-node.js"

echo "== link web build (main-thread render + JSPI) =="
# SDL2-emscripten always creates the WebGL context on the BROWSER MAIN THREAD (its
# EGL proxies getContext there), and per-command GL proxying from a worker doesn't
# reach it -> buffers silently fail to allocate. So we render on the main thread.
# The native loop blocks, so JSPI suspends it each frame (emscripten_sleep at swap)
# and turns the game's synchronous file reads into async fetch (EM_ASYNC_JS).
emcc -O1 -w $PT $SJLJ -sUSE_SDL=2 -sFULL_ES3 -sGL_ENABLE_GET_PROC_ADDRESS $PORTS \
  $GAME "$WOBJ/sdlmain_browser.wo" "$WOBJ/file_browser.wo" $RT_COMMON \
  -s PTHREAD_POOL_SIZE=40 -s PTHREAD_POOL_SIZE_STRICT=0 \
  -s JSPI -s MAX_WEBGL_VERSION=2 \
  -s INITIAL_MEMORY=1610612736 -s ALLOW_MEMORY_GROWTH=1 -s MAXIMUM_MEMORY=4294967296 \
  -s EXIT_RUNTIME=0 -s ERROR_ON_UNDEFINED_SYMBOLS=0 -s FORCE_FILESYSTEM=1 -s ASSERTIONS=0 \
  -lopenal \
  -s GL_ASSERTIONS=1 \
  -s INVOKE_RUN=1 \
  -o "$REPO/web/deadcells.js"
echo "  -> $REPO/web/deadcells.js  (+ .wasm)"
echo "done."
