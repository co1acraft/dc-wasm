#!/usr/bin/env bash
# Shared configuration sourced by the other scripts.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENDOR="$REPO/vendor"
BUILD="$REPO/build"
EMSDK="$VENDOR/emsdk"
HL="$VENDOR/hashlink"
SDL112="$VENDOR/sdl112"           # SDL2-era sdl.c/gl.c (matches Dead Cells bytecode)
HAXE_DIR="$VENDOR/haxe"

# Path to your OWN copy of the game files (hlboot.dat + res.pak). NOT in this repo.
GAME_DIR="${DC_GAME_DIR:-/home/ubuntu/Documents/deadcells/depots/588653/23762174}"

# Pin versions for reproducibility.
HL_COMMIT="${HL_COMMIT:-459601b}"  # hashlink master (Haxe 4.x era) used for the runtime + patches
SDL_TAG="1.12"                     # SDL2-era libs/sdl sources matching the game

# libhl runtime std sources (compiled into the wasm runtime)
STD="array buffer bytes cast date error debug file fun maps math obj random regexp socket string sys types ucs2 thread process track"

emcc_env() { source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1; }
pcre_srcs() { ls "$HL"/include/pcre/pcre2_*.c | grep -vE "pcre2_jit"; }
mkdir -p "$VENDOR" "$BUILD"
