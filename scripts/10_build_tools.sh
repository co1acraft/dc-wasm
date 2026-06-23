#!/usr/bin/env bash
# Build the native transpiler + analysis tools (link against the freshly-built libhl).
source "$(dirname "$0")/config.sh"

cc="gcc -I $HL/src -O1 -w"
# -DDC_GC_ROOTS makes bc2c emit shadow-stack rooting for GC-managed pointer registers so
# HashLink's conservative GC works on wasm (paired with -O0-compiled libhl/native in
# 40_build_wasm.sh). ON by default; set GC_ROOTS=0 to disable. See README "Known issues".
[ "${GC_ROOTS:-1}" = "0" ] || cc="$cc -DDC_GC_ROOTS"
$cc -o "$BUILD/bc2c"    "$REPO/src/bc2c.c"    "$HL/src/code.c" -L"$HL" -Wl,-rpath,"$HL" -lhl -lm
$cc -o "$BUILD/inspect" "$REPO/src/inspect.c" "$HL/src/code.c" -L"$HL" -Wl,-rpath,"$HL" -lhl -lm
$cc -o "$BUILD/disasm"  "$REPO/src/disasm.c"  "$HL/src/code.c" -L"$HL" -Wl,-rpath,"$HL" -lhl -lm
echo "built: $BUILD/{bc2c,inspect,disasm}"
echo "try:   LD_LIBRARY_PATH=$HL $BUILD/inspect \"$GAME_DIR/hlboot.dat\""
