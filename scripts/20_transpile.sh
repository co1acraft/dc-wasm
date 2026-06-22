#!/usr/bin/env bash
# Transpile hlboot.dat -> C (multi-file) in build/dc/.
source "$(dirname "$0")/config.sh"
IN="${1:-$GAME_DIR/hlboot.dat}"
OUT="$BUILD/dc"
mkdir -p "$OUT"
LD_LIBRARY_PATH="$HL" "$BUILD/bc2c" "$IN" "$OUT"
echo "generated C in $OUT/ :"
ls "$OUT" | sed 's/^/  /'
