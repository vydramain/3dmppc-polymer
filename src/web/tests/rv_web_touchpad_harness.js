// Test harness for the web touchpad scripts: loads them into a node:vm context, as the glued pre-js would.
// load() runs only the draw script. scene() runs all parts in CMakeLists.txt order (RV_WEB_PRE_JS_PARTS, less
// rv_web_pre.js) against stubbed Module, document, window, matchMedia, timers and Date, and gives fake pointer events.
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
        // A clear starts a redraw: calls then hold the last redraw only.
        clearRect() { ctx.calls.length = 0; },
    };
    return ctx;
}

const PARTS = [ 'rv_web_touchpad_draw.js', 'rv_web_touchpad_layout.js', 'rv_web_touchpad.js', 'rv_web_touchpad_input.js' ];
// SDL_GamepadAxis numbers the page sends.
const AXIS = {
    LEFTX : 0,
    LEFTY : 1,
    RIGHTX : 2,
    RIGHTY : 3,
    LEFT_TRIGGER : 4,
    RIGHT_TRIGGER : 5
};

// Landscape scene as on a phone: buffer 1280x592, a 640x360 frame centred, frame_w 320, so scale 2 and a 640x296 pad
// surface. groups: a bit mask of rvTouchLayout.GROUP names, default sticks|face|triggers|menu.
function scene(groupNames = [ 'sticks', 'face', 'triggers', 'menu' ]) {
    const clock = {t : 1000, timers : [], next : 1};
    const handlers = {};
    const addEventListener = (type, fn) => { (handlers[type] = handlers[type] || []).push(fn); };
    const ctx = recordingContext();
    ctx.imageSmoothingEnabled = true;
    const canvas = {
        width : 0,
        height : 0,
        hidden : true,
        get clientWidth() { return canvas.width; },
        get clientHeight() { return canvas.height; },
        // The key is label<Id>: any label text, so the page does not warn.
        dataset : new Proxy({}, {get : (_, key) => String(key).slice(5).toUpperCase()}),
        getBoundingClientRect : () => ({left : 0, top : 0}),
        setPointerCapture() {},
        getContext : () => ctx,
    };
    const calls = {axes : [], buttons : [], trackpad : []};
    const Module = {
        canvas : {},
        _rv_pctouchpad_button : (b, down) => calls.buttons.push({button : b, down : !!down}),
        _rv_pctouchpad_axis : (a, v) => calls.axes.push({axis : a, value : v}),
        _rv_pctouchpad_trackpad : (pad, down, x, y) => calls.trackpad.push({pad, down : !!down, x, y}),
    };
    const context = vm.createContext({
        console,
        Module,
        document : {getElementById : id => id === 'touchpad' ? canvas : null, addEventListener},
        window : {addEventListener, screen : {}},
        matchMedia : () => ({matches : true}),
        Date : {now : () => clock.t},
        setTimeout : (fn, ms) => {
            clock.timers.push({id : clock.next, due : clock.t + ms, fn});
            return clock.next++;
        },
        clearTimeout : id => { clock.timers = clock.timers.filter(t => t.id !== id); },
    });
    PARTS.forEach(f => run(context, f));
    Module.rvTouchpadLayout(320, 116, 640, 360, 1280, 592, 320);
    Module.rvTouchpadShow(groupNames.reduce((m, n) => m | context.rvTouchLayout.GROUP[n], 0));

    const s = {
        context,
        canvas,
        ctx,
        calls,
        scale : context.rvTouchpad.controls().scale,
        controls : () => context.rvTouchpad.controls().elements,
        el : id => s.controls().find(e => e.id === id),
        // Fake pointer event at pad pixel (x, y); client pixels are pad pixels times the scale (canvas is 1:1 to the buffer).
        pointer(type, pointerId, x, y) {
            const e = {pointerId, clientX : x * s.scale, clientY : y * s.scale, target : canvas};
            (handlers[type] || []).forEach(fn => fn(e));
        },
        down : (id, x, y) => s.pointer('pointerdown', id, x, y),
        move : (id, x, y) => s.pointer('pointermove', id, x, y),
        up : (id, x, y) => s.pointer('pointerup', id, x, y),
        cancel : (id, x, y) => s.pointer('pointercancel', id, x, y),
        // Last value sent for an axis, or undefined.
        axis(a) {
            const list = calls.axes.filter(c => c.axis === a);
            return list.length ? list[list.length - 1].value : undefined;
        },
        // Sends for one button in order: 1 down, 0 up.
        buttons : b => calls.buttons.filter(c => c.button === b).map(c => c.down ? 1 : 0),
        // Moves the clock and runs the timers that came due.
        advance(ms) {
            clock.t += ms;
            for (;;) {
                const due = clock.timers.filter(t => t.due <= clock.t).sort((a, b) => a.due - b.due)[0];
                if (!due) {
                    return;
                }
                clock.timers = clock.timers.filter(t => t !== due);
                due.fn();
            }
        },
    };
    return s;
}

module.exports = {
    AXIS,
    scene,
    load,
    run,
    recordingContext
};
