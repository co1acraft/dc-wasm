#!/usr/bin/env node
// Run the node wasm build (build/deadcells-node.js). Mounts your game directory
// (res.pak lives there) into the wasm FS via NODEFS, then calls main().
//
// This build runs the full engine + asset pipeline (res.pak loads, "done.")
// and reaches the SDL init boundary. node has no WebGL/canvas, so SDL init
// fails there ("screen is not defined" / "Failed to init SDL") -- that is the
// expected stopping point. Use the *web* build for actual rendering.
//
// Usage:  DC_GAME_DIR=/path/to/game node scripts/run_node.js
const path = require("path");
const createDC = require(path.join(__dirname, "..", "build", "deadcells-node.js"));
const GAME = process.env.DC_GAME_DIR ||
  "/home/ubuntu/Documents/deadcells/depots/588653/23762174";

createDC({ arguments: [], print: console.log, printErr: console.error })
  .then((M) => {
    M.FS.mkdir("/game");
    M.FS.mount(M.NODEFS, { root: GAME }, "/game");
    M.FS.chdir("/game");
    try { M.callMain([]); }
    catch (e) { console.error("[exit]", e && e.message ? e.message : e); }
  })
  .catch((e) => { console.error("init failed:", e); process.exit(1); });
