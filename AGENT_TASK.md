# Agent task: find & fix the runtime errors in the Dead Cells wasm browser build

> ## ⏱ STATUS UPDATE — the original GL-init crash is FIXED
> The `getParameter` crash below was the first of many layers. The browser build
> now runs the **entire engine init**: WebGL2 context created, **all 1291 shaders
> compiled**, PNG/assets decoded (real `fmt` lib), audio initialized. Fixes landed
> in `runtime/patches/` + `scripts/40_build_wasm.sh`:
> 1. **`-sOFFSCREEN_FRAMEBUFFER=1 -sMAX_WEBGL_VERSION=2`** — the real cause wasn't
>    "no OffscreenCanvas": SDL2's EGL `eglCreateContext` *proxies to the main
>    thread*, so OffscreenCanvas (canvas on a worker) can't work. Keep canvas on
>    main, proxy GL.
> 2. **`gl.c` in `HL_GLES` mode** — direct GLES3 calls (fixes a libGL symbol clash).
> 3. **per-subsystem `SDL_Init`** — `SDL_INIT_EVERYTHING` aborts (HAPTIC unsupported).
> 4. **`PROFILE_ES`** — was requesting desktop GL 3.2 Core; EGL rejected it.
> 5. **`-sASSERTIONS=0`** — an over-strict `proxying_legacy` assert blocked the swap.
> 6. **`file.c` writes + `save/` dirs** — saves were routed through read-only XHR.
> 7. **`gc.c` tolerate unregistered threads** (SDL/loader threads).
> 8. **real `fmt` lib** vs libpng/zlib/vorbis ports — PNG/asset decoding.
> 9. **shader GLES-3 rewrite** in `gl_shader_source`: `#version 300 es`, rename
>    reserved word `sample`, `layout(location=N)` for MRT outputs.
>
> **The GC now works** and the game reaches the **render loop**. Fix:
> - `bc2c` (`GC_ROOTS=1`, default) forces GC pointer registers onto the shadow
>   stack once per function (precise; `--spill-pointers` was unusable — 423 MB,
>   94 MB function over V8's limit).
> - libhl/native objects compiled `-O0` so their pointer locals are on the shadow
>   stack too. `-O1` under-roots (collects a live Shader); `-O0` is precise enough
>   here because the GC only over-retained via the OGG/vorbis decode leak, which is
>   now stubbed (OpenAL is stubbed, so its buffers never drained). Memory is
>   bounded (~230 MB, no OOM).
> - Plus: `win_get_size` via `emscripten_get_canvas_element_size` (SDL races to 0
>   under threads → 0-size render targets), and keep ELEMENT buffer bound.
>
> **Remaining blocker = nothing renders (blank).** `glDrawElements` fails GL
> validation (`Insufficient buffer size`): DC's GlDriver runs in **desktop-GL**
> mode (no VAOs, cached/global element binding, likely client-side index arrays),
> which WebGL2 (GLES core) rejects. DC's GLES detection fails because Emscripten's
> `glGetString(GL_VERSION)` = `"OpenGL ES 3.0 (WebGL…)"` (DC's parser wants a
> leading number). **Next task:** make DC take the GLES render path, or emulate
> desktop-GL draw state in `gl.c`. The original brief below is historical.

---


This is a handoff brief for a coding agent. The project (`dc-wasm`) is a WebAssembly
port of **Dead Cells** (a HashLink/Heaps game). It boots fully in the browser but
**crashes at WebGL initialization**. Your job is to reproduce that crash, diagnose
it, fix it, and then keep going through whatever errors surface next until the game
renders.

> Read `README.md` first for the architecture. This file adds the debugging workflow
> and the current known failure.

---

## TL;DR

- The browser build gets all the way through engine init + asset loading, then dies
  with `TypeError: Cannot read properties of undefined (reading 'getParameter')`
  inside `_emscripten_glGetString`, **on a worker pthread** (see `web/browser.log`).
- **Leading hypothesis:** the web build links with `-s PROXY_TO_PTHREAD` (the game
  runs on a worker) but **without any OffscreenCanvas support**, so the worker has no
  WebGL context. The first GL call (`glGetString`) dereferences an undefined context.
- Everything needed to reproduce is already on this machine (build outputs,
  toolchains, game files, and `chromium`). You should **not** need to re-run the
  heavy one-time setup.
- Fix → re-link → re-run headless → re-read `web/browser.log`. Iterate.

---

## Environment (already provisioned — do NOT redo)

| Thing | Location / value | Notes |
|---|---|---|
| Repo | `/home/ubuntu/Documents/deadcells/dc-wasm` | git repo, 2 commits |
| Toolchains | `vendor/emsdk`, `vendor/hashlink`, `vendor/haxe`, `vendor/sdl112` | `00_setup.sh` already ran; `emcc` present |
| Game files | `/home/ubuntu/Documents/deadcells/depots/588653/23762174/` | `hlboot.dat` (17 MB) + `res.pak` (2.08 GB) |
| Build outputs | `web/deadcells.{js,wasm}`, `build/deadcells-node.{js,wasm}`, `build/deadcells_native`, `build/dc/*.c` (29 files) | already built |
| Browser | `/usr/bin/chromium` | use headless + swiftshader for software WebGL2 |
| node | v22 | for the node build |

```bash
export DC_GAME_DIR=/home/ubuntu/Documents/deadcells/depots/588653/23762174
```

- **Do not run `scripts/00_setup.sh`** unless a toolchain is actually broken — it
  clones/builds Emscripten + HashLink and needs ~20 GB and network.
- `scripts/40_build_wasm.sh` **caches object files** by mtime (`ccobj`), so after you
  edit only link flags it just re-links (fast). Editing a `.c`/`.h` source recompiles
  just that unit.

---

## How it runs (the parts that matter here)

- `web/deadcells.html` is the harness. It sets a global `Module` (the web build is
  **not** MODULARIZE'd), wires `<canvas id="canvas">`, and mirrors `print`/`printErr`/
  `window.onerror` to the server via `POST /log`.
- `web/serve.py` (via `scripts/run_web.sh PORT`) serves the harness with **COOP/COEP**
  (required for SharedArrayBuffer/threads) and **HTTP Range** (the game streams the
  2 GB `res.pak` over XHR Range from `/files/...`). It appends every browser log line
  to `web/browser.log` and echoes `[browser] ...` to stdout.
- The web wasm is linked in `scripts/40_build_wasm.sh`, the `== link web build ==`
  block (≈ lines 55–63): `-s PROXY_TO_PTHREAD -s PTHREAD_POOL_SIZE=40 -sUSE_SDL=2
  -sFULL_ES3 -sGL_ENABLE_GET_PROC_ADDRESS ... -s ASSERTIONS=1 -s INVOKE_RUN=1`.
- HashLink's GC is **patched off** (`runtime/patches/gc.c.patch` (`gc_flags=64`))
  as a temporary workaround; open fds are pinned as GC roots. Expect memory growth on
  long runs.

---

## The current known error (from `web/browser.log`)

```
[out] done.                              <- res.pak loaded, steam shim ok
[err] [native] steam_get_current_game_language
[err] worker: onmessage() captured an uncaught exception:
      TypeError: Cannot read properties of undefined (reading 'getParameter')
[err] TypeError: Cannot read properties of undefined (reading 'getParameter')
    at _emscripten_glGetString (http://localhost:8088/deadcells.js:9956:32)
    at deadcells.wasm:wasm-function[38715]   (SDL/GL init path)
    ... invoke_ii / invoke_vi / invoke_iiii ...
[err] Pthread 0x... sent an error! ... Uncaught TypeError: ... 'getParameter'
```

Interpretation: boot, thread pool, `res.pak` load, and the Steam shim all succeed.
Execution reaches SDL2/WebGL init and calls `glGetString` (likely `GL_VERSION` /
`GL_EXTENSIONS`). `_emscripten_glGetString` reads `GLctx.getParameter`, but
`GLctx`/`GL.currentContext` is **undefined on this worker thread** → the port never
created (or never bound) a GL context on the thread that runs the render loop.

---

## Why the hypothesis (PROXY_TO_PTHREAD without OffscreenCanvas)

With `-s PROXY_TO_PTHREAD`, `main()` and the render loop run on a Web Worker, not the
browser main thread. A WebGL context lives on the thread that owns the canvas. To use
GL from the worker you must transfer the canvas to it via **OffscreenCanvas**, which
in Emscripten requires linking with **`-sOFFSCREENCANVAS_SUPPORT=1`** and naming the
canvas(es) to transfer, e.g. **`-sOFFSCREENCANVASES_TO_PTHREAD='#canvas'`**. The web
link command currently has neither, so the worker's `GL.currentContext` stays
`undefined` and the first `glGetString` throws.

(For reference, a sibling project, `stswasm`, hit the same class of bug and fixed it
by enabling `OFFSCREENCANVAS_SUPPORT` and transferring the `.canvas` element to the
render pthread.)

**Treat this as a hypothesis to confirm, not a certainty** — verify by reproducing
and inspecting before/after.

---

## Reproduce loop (do this first to confirm the baseline)

```bash
cd /home/ubuntu/Documents/deadcells/dc-wasm
export DC_GAME_DIR=/home/ubuntu/Documents/deadcells/depots/588653/23762174

# 1. start the server (background)
: > web/browser.log                      # truncate so you read only this run
scripts/run_web.sh 8088 &                # serves http://127.0.0.1:8088/
SERVER=$!
sleep 1

# 2. drive it headless with software WebGL2, let it run, then kill
PROF=$(mktemp -d)
timeout 60 chromium --headless=new --no-sandbox --disable-dev-shm-usage \
  --use-gl=angle --use-angle=swiftshader-webgl --enable-unsafe-swiftshader \
  --window-size=1280,720 --no-first-run --no-default-browser-check \
  --user-data-dir="$PROF" http://127.0.0.1:8088/ >/dev/null 2>&1 &
CHROME=$!
sleep 45                                 # boot + 2GB res.pak stream takes a while
kill $CHROME 2>/dev/null; kill $SERVER 2>/dev/null

# 3. read what the page reported
cat web/browser.log
```

You should see the same `getParameter` crash. If `res.pak` streaming makes 45 s too
short, increase the sleeps. (Optionally, for screenshots/richer capture, drive
chromium over the DevTools Protocol on `--remote-debugging-port=9222` instead of a
plain navigate; the harness's `POST /log` capture is usually enough.)

---

## Fix: things to try, in order

1. **Enable OffscreenCanvas for the render pthread (primary).** In
   `scripts/40_build_wasm.sh`, add to the **web** link only (not node):
   ```
   -sOFFSCREENCANVAS_SUPPORT=1 -sOFFSCREENCANVASES_TO_PTHREAD='#canvas'
   ```
   Re-run `scripts/40_build_wasm.sh` (re-links only), then re-run the reproduce loop.
   - Confirm the canvas id matches the harness (`<canvas id="canvas">` →
     selector `#canvas`).
   - Chrome supports `transferControlToOffscreen`; keep testing in chromium.
2. **If GL must stay on the main thread instead**, consider routing GL to the browser
   main thread (`-sOFFSCREEN_FRAMEBUFFER=1` + proxying) or not proxying main
   (`PROXY_TO_PTHREAD` removed) — but the engine likely needs a non-blocking main
   thread, so option 1 is preferred. Weigh against how `sdl112/sdl.c` creates the
   window/context.
3. **Add GL diagnostics** while iterating: `-sGL_ASSERTIONS=1 -sGL_DEBUG=1` on the web
   link to get clearer messages than "undefined".
4. Inspect the generated glue around the failure: open `web/deadcells.js` near
   `_emscripten_glGetString` and the `GL.currentContext` / `GL.makeContextCurrent`
   logic to see on which thread the context is created vs. used. Cross-check the
   SDL window/context creation in `vendor/sdl112/sdl.c` and `vendor/sdl112/gl.c`.

After any source change to the patched HashLink or SDL files, remember the build
caches objects by mtime — touching the file is enough to force its recompile.

---

## Re-verify & what "progress" looks like

- **Success for step 1:** `web/browser.log` no longer shows the `getParameter` crash
  and execution proceeds past GL init (new log lines, a created GL context, or a
  rendered frame). Grab a screenshot via CDP `Page.captureScreenshot` to confirm
  pixels if you want hard proof.
- **Expect follow-on errors** once GL is alive, e.g.:
  - **GC is disabled** (`runtime/patches/gc.c.patch`), so long sessions may leak/OOM
    (`INITIAL_MEMORY=1.5 GB`, `MAXIMUM_MEMORY=4 GB`). The real fix (precise rooting /
    spilling wasm locals to the shadow stack so the conservative scan sees them) is
    the project's main open task — see README "Known issues".
  - Input (SDL events), audio, shader/format issues, or threading races.
- Triage each new error the same way: read the stack in `browser.log`, map the
  `deadcells.js` symbol, decide whether it's in the runtime patches
  (`runtime/`), the SDL layer (`vendor/sdl112/`), the link flags (`40_build_wasm.sh`),
  or the transpiler output (`src/bc2c*`).

---

## Guardrails

- **Never commit game files or anything derived from them** (`hlboot.dat`, `res.pak`,
  the generated `build/dc/*.c`, the `.wasm`). `.gitignore` already excludes them —
  keep it that way. Dead Cells is © Motion Twin / Evil Empire.
- **Do not re-run `00_setup.sh`** unless a toolchain is genuinely missing/broken.
- `build/dc/*.c` is ~113 MB / ~5.2 M lines — **don't read it wholesale.** Use the
  tools: `build/inspect`, `build/disasm` (see below).
- Keep all real fixes in `src/`, `runtime/`, `scripts/`, `web/` and regenerate via the
  pipeline — don't hand-edit generated output in `build/`.
- This file (`AGENT_TASK.md`) is untracked; delete it or move it out before committing
  if you don't want it in history.

---

## Reference

**Pipeline / commands**
| Step | Command | When |
|---|---|---|
| Setup (one-time) | `scripts/00_setup.sh` | already done — skip |
| Build tools | `scripts/10_build_tools.sh` | already done |
| Transpile | `scripts/20_transpile.sh` | already done (rerun only if `hlboot.dat` changes) |
| Native sanity | `scripts/30_build_native.sh` | optional; reaches SDL boundary natively |
| Build wasm | `scripts/40_build_wasm.sh` | **re-run after link-flag/source changes** |
| Run node | `DC_GAME_DIR=… node scripts/run_node.js` | stops at SDL init (no canvas) — expected |
| Run web | `scripts/run_web.sh 8088` | serve; drive with headless chromium |

**Key files**
- `scripts/40_build_wasm.sh` — emcc compile + link flags (the GL/threads fix goes here).
- `scripts/config.sh` — paths/versions; `GAME_DIR` default (line 14), `EMSDK` etc.
- `runtime/patches/*.patch` — HashLink/SDL patches: `file.c` (XHR/NODEFS I/O + fd
  pinning) and `gc.c` (collection disabled, lines ~184–193).
- `web/deadcells.html` — harness (`Module`, canvas `#canvas`, `POST /log`).
- `web/serve.py` — COOP/COEP + Range static server; appends `web/browser.log`.
- `vendor/sdl112/sdl.c`, `vendor/sdl112/gl.c` — SDL2-era window/GL context creation.
- `web/browser.log` — captured browser console (the current error lives here).

**Analysis tools** (run with `LD_LIBRARY_PATH=vendor/hashlink`)
- `build/inspect <hlboot.dat>` — counts funcs/types/opcodes/natives.
- `build/disasm <hlboot.dat>` — mini bytecode disassembler (map `f<index>` symbols).

**Env vars**: `DC_GAME_DIR` (game dir), `HL_COMMIT` (HashLink pin), port arg to
`run_web.sh`.
