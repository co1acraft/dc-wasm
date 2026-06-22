#!/usr/bin/env bash
# Build the native transpiler + analysis tools (link against the freshly-built libhl).
source "$(dirname "$0")/config.sh"

cc="gcc -I $HL/src -O1 -w"
# Add -DDC_GC_ROOTS to the bc2c build below to emit shadow-stack rooting for GC-managed
# pointer registers (so HashLink's conservative GC can run on wasm). Off by default --
# see README "Known issues" for the GC status. e.g.:  GC_ROOTS=1 scripts/10_build_tools.sh
[ -n "${GC_ROOTS:-}" ] && cc="$cc -DDC_GC_ROOTS"
$cc -o "$BUILD/bc2c"    "$REPO/src/bc2c.c"    "$HL/src/code.c" -L"$HL" -Wl,-rpath,"$HL" -lhl -lm
$cc -o "$BUILD/inspect" "$REPO/src/inspect.c" "$HL/src/code.c" -L"$HL" -Wl,-rpath,"$HL" -lhl -lm
$cc -o "$BUILD/disasm"  "$REPO/src/disasm.c"  "$HL/src/code.c" -L"$HL" -Wl,-rpath,"$HL" -lhl -lm
echo "built: $BUILD/{bc2c,inspect,disasm}"
echo "try:   LD_LIBRARY_PATH=$HL $BUILD/inspect \"$GAME_DIR/hlboot.dat\""
