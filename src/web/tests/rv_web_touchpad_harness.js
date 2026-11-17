// Test harness for the web touchpad scripts: loads them into a node:vm context, as the glued pre-js would.
// To load more parts (S3), add names to PARTS in CMakeLists.txt order (RV_WEB_PRE_JS_PARTS) and stub the
// globals they read (Module, document, window, matchMedia) in load() before running them.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const WEB_DIR = path.join(__dirname, '..');

// Runs one script from src/web in a shared context; top-level var declarations become context globals.
function run(context, file) { vm.runInContext(fs.readFileSync(path.join(WEB_DIR, file), 'utf8'), context, {filename : file}); }

function load() {
    const context = vm.createContext({console});
    run(context, 'rv_web_touchpad_draw.js');
    return context;
}

// A 2D context that records every fillRect with the fillStyle and globalAlpha in force at that call.
function recordingContext() {
    const ctx = {
        fillStyle : '#000000',
        globalAlpha : 1,
        calls : [],
        fillRect(x, y, w, h) { ctx.calls.push({x, y, w, h, fillStyle : ctx.fillStyle, globalAlpha : ctx.globalAlpha}); },
        setTransform() {},
        clearRect() {},
    };
    return ctx;
}

module.exports = {
    load,
    run,
    recordingContext
};
