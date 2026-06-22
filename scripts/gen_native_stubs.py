#!/usr/bin/env python3
# Emit no-op stubs for the sdl_* natives (so the *native* sanity build links
# without system SDL2). The wasm builds use the real sdl.c/gl.c instead.
# Reads build/dc/dc.h, writes the stubs to stdout.
import re, sys
hdr = sys.argv[1]
for line in open(hdr):
    m = re.match(r'\s*HL_API\s+(.+?)\s+(sdl_\w+)\s*\((.*)\)\s*;', line)
    if not m:
        continue
    ret, name, args = m.group(1).strip(), m.group(2), m.group(3).strip()
    params = "void" if args in ("", "void") else \
             ",".join(f"{a.strip()} p{i}" for i, a in enumerate(args.split(",")))
    if ret == "void":
        print(f"{ret} {name}({params}) {{ }}")
    else:
        print(f"{ret} {name}({params}) {{ return ({ret})0; }}")
