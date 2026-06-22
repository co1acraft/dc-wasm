#!/usr/bin/env bash
# Native sanity build: prove the transpiled C compiles+links+runs (uses real
# native file I/O, so it reads res.pak from disk). Reaches the game's startup
# (pak load -> steam -> SDL init). sdl_* are no-op stubs here (no system SDL).
source "$(dirname "$0")/config.sh"
DC="$BUILD/dc"
[ -f "$DC/dc.h" ] || { echo "run 20_transpile.sh first"; exit 1; }

python3 "$REPO/scripts/gen_native_stubs.py" "$DC/dc.h" > "$BUILD/sdl_native_stubs.c"

inc="-I $DC -I $HL/src"
cc="gcc $inc -O1 -w"
echo "compiling generated C (this can take a few minutes)..."
objs=()
for c in "$DC"/dc_types.c "$DC"/dc_main.c "$DC"/dc_stubs.c "$DC"/dc_f*.c \
         "$REPO/runtime/ssl_impl.c" "$HL/libs/ui/ui_stub.c" "$BUILD/sdl_native_stubs.c"; do
  o="$BUILD/$(basename "$c").o"; objs+=("$o")
  [ -f "$o" ] && [ "$o" -nt "$c" ] && continue   # cached
  $cc -c "$c" -o "$o" &
  # limit parallelism
  while [ "$(jobs -r | wc -l)" -ge "$(nproc)" ]; do wait -n; done
done
wait
gcc -O1 -w -o "$BUILD/deadcells_native" "${objs[@]}" "$HL/libhl.so" -Wl,-rpath,"$HL" -lm
echo "built $BUILD/deadcells_native"
echo "run:  ( cd \"$GAME_DIR\" && LD_LIBRARY_PATH=\"$HL\" \"$BUILD/deadcells_native\" )"
