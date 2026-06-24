# dc-wasm

Porting **Dead Cells** (a [HashLink](https://hashlink.haxe.org/) / [Heaps.io](https://heaps.io/)
game) to **WebAssembly**, so it can run in a browser.

The heart of this project is **`bc2c`**, a from-scratch HashLink-bytecode → C
transpiler. It turns the game's `hlboot.dat` into portable C, which is then
compiled to wasm with Emscripten and linked against the HashLink runtime, the
SDL2/WebGL2 backend, and a small browser I/O shim.

> ### ⚠️ Legal / you must bring your own game files
> This repository contains **only original code** (the transpiler, build
> scripts, runtime patches, and web harness). It does **not** contain and will
> **not** download Dead Cells. Dead Cells is a proprietary game,
> © Motion Twin / Evil Empire. You must **own a legitimate copy** and point the
> build at your own `hlboot.dat` + `res.pak` via `DC_GAME_DIR`. Do not
> redistribute the game's files or any artifacts derived from them. See
> [`LICENSE`](./LICENSE).

---

## Why a transpiler?

HashLink ships two execution modes: a **JIT** (`jit.c`) and an **HL/C** path
(bytecode compiled to C). The JIT emits native x86/ARM machine code and cannot
target wasm — `jit.c` does not even compile under `emcc`, and there is no
bytecode *interpreter* to fall back on. The HL/C path is the supported route to
"weird" targets.

The official HL/C generator lives inside the Haxe compiler (`hl2c.ml`) and runs
*before* bytecode exists — it needs the original `.hx` sources, which we don't
have for a shipped game. So we generate the equivalent C **directly from the
shipped `hlboot.dat`**. `bc2c` reuses HashLink's own bytecode reader
(`src/code.c`) and emits C that mirrors `hl2c.ml` opcode-for-opcode.

Validated end-to-end on toy programs: `Haxe → HL/C → emcc → wasm → node`
produces byte-identical output to the native HashLink run.

```
              bring your own
              hlboot.dat ──┐
                           ▼
  src/bc2c.c ─────────►  build/dc/*.c   (≈113 MB C, ≈5.2 M lines, 28 units)
 (uses hashlink code.c)     │
                            ├─ gcc ───────────►  native sanity binary
                            │                    (reads res.pak from disk)
                            └─ emcc + libhl + SDL2/WebGL2 + shims
                                     │
                          ┌──────────┴───────────┐
                          ▼                       ▼
                   deadcells-node.js          web/deadcells.js
                   (NODEFS; reaches            (browser main thread:
                    SDL boundary)               WebGL2 + JSPI, fetch-
                                                streamed res.pak)
```

---

## Prerequisites

- Linux, `gcc`, `python3`, `git`, `curl`, ~20 GB free disk
- A C-capable `node` (v18+; v22 tested) for the node build
- **Your own** Dead Cells files in one directory:
  - `hlboot.dat` (~17 MB, bytecode v4, hl_ver 1.12.0)
  - `res.pak` (~2 GB)

Everything else (Emscripten, HashLink, SDL2-era sources, Haxe) is fetched into
`vendor/` by the setup script.

```bash
export DC_GAME_DIR=/path/to/your/deadcells   # contains hlboot.dat + res.pak
```

---

## Quick start

```bash
# 0. fetch + build toolchains (Emscripten, patched HashLink, SDL sources, libhl)
scripts/00_setup.sh

# 1. build the transpiler + analysis tools
scripts/10_build_tools.sh

# 2. transpile YOUR hlboot.dat -> build/dc/*.c
scripts/20_transpile.sh

# 3. (optional) native sanity build — compiles the generated C and runs it
#    natively against real on-disk res.pak, up to the SDL boundary
scripts/30_build_native.sh

# 4. build the wasm targets (slow: the dc_f* chunks compile at -O1)
scripts/40_build_wasm.sh

# 5a. run the node build (reaches the SDL init boundary; no rendering in node)
DC_GAME_DIR=$DC_GAME_DIR node scripts/run_node.js

# 5b. serve the browser build, then open the printed URL yourself
scripts/run_web.sh 8088
```

`run_web.sh` deliberately **does not launch a browser**. Open
`http://127.0.0.1:8088/` in **Chrome ≥137** — it needs WebGL2, SharedArrayBuffer
(COOP/COEP, served automatically) and **WebAssembly JSPI**; Firefox doesn't
support JSPI yet, so it won't run there. Console output is mirrored to
`web/browser.log`.

---

## Status

**Works**
- `bc2c` transpiles the full game: all **91** opcodes Dead Cells uses,
  **37,448** functions, **25,079** types, **503** natives — ≈113 MB of C across
  28 compilation units in ≈2.6 s.
- Generated C compiles cleanly (0 errors) both natively (≈85 MB binary) and to
  wasm (≈74 MB web module, with libpng/zlib/vorbis codecs + JSPI + wasm SjLj).
- Full engine boot runs: HashLink runtime init, thread pool, **`res.pak`
  loads** (2 GB, SHA-256 stamp verified) → prints `done.`, Steam shim returns OK.
- **The browser build renders.** Verified headless Chromium + SwiftShader: WebGL2
  context, all shaders compiled, `res.pak` streamed via async fetch, assets decoded
  (real `fmt` lib), and the game draws its intro (the Evil Empire splash) — full
  1280×720 frames, animating, **zero GL errors**.
- **Audio works.** HashLink's `openal` binding is compiled against Emscripten's
  OpenAL (`-lopenal` → Web Audio), and OGG/Vorbis decode is enabled (real libvorbis
  port). Verified: the game opens a **running** `AudioContext` and decodes its OGG
  audio with no errors. (EFX effects/filters are absent in Emscripten's OpenAL and
  stay stubbed; core positional audio is real.)
- **The GC works.** HashLink's conservative GC can't see pointers held in wasm
  locals; the fix is two-part: `bc2c` (with `GC_ROOTS=1`, default) forces
  GC-managed pointer *registers* onto the linear-memory shadow stack once per
  function (precise — unlike Binaryen `--spill-pointers`, which 9×-bloats the
  module and produces a 94 MB function over V8's limit), and the libhl/native
  objects are compiled `-O0` so their pointer locals also live on the shadow
  stack. Memory is now **bounded (~230 MB, no OOM, no leak)**.

### Rendering: why it must run on the browser main thread (JSPI)

The blank-canvas bug turned out **not** to be a GL-semantics problem but a
**threading** one. SDL2-emscripten always creates the WebGL context on the
browser **main thread** (its EGL proxies `getContext` there, and OffscreenCanvas
fails: `getContext` on a transferred canvas throws `InvalidStateError`). When the
game ran its render loop on a worker (`PROXY_TO_PTHREAD` + `OFFSCREEN_FRAMEBUFFER`),
per-command GL proxying never reached that context, so `glBufferData` silently
allocated **0-byte** buffers (no error) and every `glDrawElements` failed with
`Insufficient buffer size`. A main-thread WebGL self-test allocated correctly,
which pinned the cause.

The fix is to render on the **main thread**, where the context lives:
- Dead Cells uses a **blocking** native render loop, so we yield to the page each
  frame with `emscripten_sleep(16)` at `SDL_GL_SwapWindow`, suspended/resumed via
  **JSPI** (`-sJSPI`; Chrome ≥137). Asyncify was rejected — it inflates a generated
  function past V8's 7.65 MB function-size limit (the same wall `--spill-pointers`
  hit).
- JSPI can only suspend pure-wasm stacks, but HL's `try/catch` (`setjmp/longjmp`)
  lowers to JS `invoke_*` trampolines by default → `SuspendError`. Building with
  **`-sSUPPORT_LONGJMP=wasm`** keeps the whole stack in wasm.
- Synchronous XHR with `responseType` is forbidden on the main thread, so the
  game's file reads are turned into **async `fetch`** (`EM_ASYNC_JS`, suspended by
  JSPI) — see `file.c`'s browser path.

Getting here also took a series of Emscripten/WebGL2 fixes (see `runtime/patches/`
+ `scripts/40_build_wasm.sh`): `gl.c` in `HL_GLES` mode + GLES-3 shader rewriting;
per-subsystem `SDL_Init` (HAPTIC unsupported); `PROFILE_ES`; MEMFS save files; the
GC rooting above; reading the canvas size via `emscripten_get_canvas_element_size`;
and a `preserveDrawingBuffer` override in the harness so screenshots capture the
frame.

### Audio

HashLink's `openal` library (`openal_*` natives) is compiled (`openal.c`, patched
to skip `<AL/alext.h>` so it builds against Emscripten's core-only OpenAL) and
linked with `-lopenal`, which Emscripten maps to a **Web Audio** backend. OGG
decode (`fmt.c`'s `ogg_open`) is re-enabled against the already-linked
`-sUSE_VORBIS=1 -sUSE_OGG=1` ports. The old multi-GB OOM from leaked Vorbis
decoders is gone now that OpenAL actually drains queued buffers. Emscripten's
OpenAL has **no EFX** (effects/filters/aux-effect-slots) and no `al(c)GetProcAddress`,
so the 33 EFX natives remain no-op stubs (the game gates them behind
`alcIsExtensionPresent("ALC_EXT_EFX")`, which returns false on the web).

**Known issues / WIP**
- **Audio needs a user gesture.** Browsers start an `AudioContext` suspended until
  the first click/keypress (autoplay policy); click the page to enable sound. (The
  headless test forces `--autoplay-policy=no-user-gesture-required`.)
- **No frame pacing / input wiring audited** beyond boot + intro rendering;
  gameplay past the splash/menu is not yet verified.
- **Rendering is browser-only.** node has no canvas/WebGL, so the node build
  stops at SDL init (`screen is not defined`) — expected.

---

## Repository layout

```
src/
  bc2c.c          HL bytecode -> C transpiler (entry)
  bc2c_emit.h     opcode emitter (mirrors hl2c.ml)
  bc2c_refl.h     reflection/type tables emission
  bc2c_gen.h      multi-file output + init-function splitting
  inspect.c       bytecode inspector (counts funcs/types/opcodes/natives)
  disasm.c        mini disassembler
runtime/
  patches/        unified diffs applied to vendored sources by 00_setup.sh:
                    gc.c  (disable collection; tolerate unregistered threads)
                    file.c(explicit-offset I/O: async-fetch reads (web) / NODEFS readSync (node), MEMFS writes, fd pinning)
                    fmt.c (build under Emscripten; stub turbojpeg; OGG/Vorbis decode enabled)
                    sdl.c (per-subsystem SDL_Init; GLES profile; main-thread render yield for JSPI)
                    gl.c  (HL_GLES mode; rewrite shaders to GLSL ES 3.00)
                    openal.c (skip <AL/alext.h> so it builds against Emscripten core OpenAL -> Web Audio)
  ssl_impl.c         real SHA-256/SHA-1 ssl_dgst_make (res.pak stamp check)
web/
  deadcells.html  canvas harness + console->server log bridge
  serve.py        COOP/COEP + HTTP Range static server (streams res.pak)
scripts/
  config.sh           shared paths/versions
  00_setup.sh         fetch + build toolchains
  10_build_tools.sh   build bc2c/inspect/disasm
  20_transpile.sh     hlboot.dat -> build/dc/*.c
  30_build_native.sh  native sanity build (+ gen_native_stubs.py for sdl_*)
  40_build_wasm.sh    build node + web wasm targets
  run_node.js         run the node build (NODEFS mount)
  run_web.sh          serve the browser build (no auto-launch)
test/
  Min.hx, Test.hx     toy programs used to validate the pipeline
```

`vendor/` and `build/` are generated and git-ignored.

---

## Notes on the transpiler

- **Index-based naming.** Types, functions, and globals are emitted as `T<i>`,
  `f<findex>`, `g<i>` to exactly match HashLink's internal indices — no name
  mangling guesswork, and it keeps 5 M lines navigable.
- **Multi-file output.** Functions are sharded into `dc_f<N>.c` (1500 each)
  alongside `dc_types.c`, `dc_main.c`, and `dc_stubs.c` so the C compiler can
  chew through it in parallel.
- **Generated init functions are split into batches.** A single function that
  registers 25 k types or all global hashes blows past compiler/V8 limits, so
  type/root/hash registration is chunked.
- **wasm local limit.** V8 caps a function at ~50 k locals; building at `-O1`
  (so clang coalesces locals) instead of `-O0` keeps the giant generated
  functions under the limit.
- **String handling.** Non-ASCII string constants are emitted as UTF-16 byte
  arrays rather than C string literals; `String` equality (`OJEq` on two
  `String` objects) calls the `__compare` proto method instead of comparing
  pointers.

## Credits

Built on [HashLink](https://github.com/HaxeFoundation/hashlink) and
[Haxe](https://haxe.org/) (HaxeFoundation), [SDL](https://www.libsdl.org/), and
[Emscripten](https://emscripten.org/). Dead Cells © Motion Twin / Evil Empire.
