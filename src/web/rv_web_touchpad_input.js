// Touch input for the on-screen gamepad: pointer events on the pad canvas become console calls.
// Plain script for --pre-js, after rv_web_touchpad.js. Also sends device motion; the iOS permission is in rv_web_shell.html.
void (function() {
    // Id of the pad canvas in rv_web_shell.html.
    var CANVAS_ID = 'touchpad';
    // SDL_GamepadButton and SDL_GamepadAxis from SDL3/SDL_gamepad.h of the SDL 3.4.18 headers the web kit builds against.
    // Values follow the enum order (SOUTH = 0, LEFTX = 0); the console receives them unchanged.
    var BUTTON = {
        SOUTH : 0,
        EAST : 1,
        WEST : 2,
        NORTH : 3,
        BACK : 4,
        START : 6,
        LEFT_STICK : 7,
        RIGHT_STICK : 8,
        LEFT_SHOULDER : 9,
        RIGHT_SHOULDER : 10,
        DPAD_UP : 11,
        DPAD_DOWN : 12,
        DPAD_LEFT : 13,
        DPAD_RIGHT : 14,
        RIGHT_PADDLE1 : 16,
        LEFT_PADDLE1 : 17,
        RIGHT_PADDLE2 : 18,
        LEFT_PADDLE2 : 19,
        TOUCHPAD : 20,
        MISC2 : 21,
        MISC3 : 22,
        MISC4 : 23
    };
    var AXIS = {LEFTX : 0, LEFTY : 1, RIGHTX : 2, RIGHTY : 3, LEFT_TRIGGER : 4, RIGHT_TRIGGER : 5};
    // Plate id -> SDL button, by layout group. L4 and R4 are the upper paddles, PADDLE1: RV_ISOURCE_REAR_BTTN_LEFT_UPPER
    // and RV_ISOURCE_REAR_BTTN_RIGHT_UPPER in rv_pcgamepads_sdl3.cpp button_map.
    var FACE = {A : BUTTON.SOUTH, B : BUTTON.EAST, X : BUTTON.WEST, Y : BUTTON.NORTH};
    var DPAD = {DN : BUTTON.DPAD_UP, DS : BUTTON.DPAD_DOWN, DW : BUTTON.DPAD_LEFT, DE : BUTTON.DPAD_RIGHT};
    var BUMPER = {L1 : BUTTON.LEFT_SHOULDER, R1 : BUTTON.RIGHT_SHOULDER};
    var CENTER = {VIEW : BUTTON.BACK, MENU : BUTTON.START};
    var REAR = {L4 : BUTTON.LEFT_PADDLE1, R4 : BUTTON.RIGHT_PADDLE1, L5 : BUTTON.LEFT_PADDLE2, R5 : BUTTON.RIGHT_PADDLE2};
    var PLATE = Object.assign({}, FACE, DPAD, BUMPER, CENTER, REAR);
    // Triggers are axes: a touch pulls fully (1), a lift releases (0).
    var TRIGGER = {L2 : AXIS.LEFT_TRIGGER, R2 : AXIS.RIGHT_TRIGGER};
    // Stick and trackpad ids: axes or pad index, the button a short tap sends, and the stick's touch, held while a
    // finger is on the stick. Project convention (rv_pcgamepads_sdl3.cpp, stick touch rule): the console reads MISC3
    // and MISC4 as the stick touch of the pads it lists; SDL_gamepad.h gives them only as extra buttons.
    var STICK = {
        LS : {x : AXIS.LEFTX, y : AXIS.LEFTY, click : BUTTON.LEFT_STICK, touch : BUTTON.MISC3},
        RS : {x : AXIS.RIGHTX, y : AXIS.RIGHTY, click : BUTTON.RIGHT_STICK, touch : BUTTON.MISC4}
    };
    var PAD = {LP : {pad : 0, click : BUTTON.TOUCHPAD}, RP : {pad : 1, click : BUTTON.MISC2}};
    // A touch that moves less than TAP_SLOP pad pixels and lifts within TAP_MS is a tap.
    var TAP_SLOP = 4, TAP_MS = 300;
    // A click holds its button for one console frame at the default 60 FPS (RV_PCONSOLE_DEFAULT_TARGET_FPS in
    // rv_pconsole.hpp). Assumption: 50 ms is longer than a frame when target_fps is above 20. The code does not check
    // that bound: rv_pconsole_run.cpp maps only target_fps 0 to the default.
    var CLICK_HOLD_MS = 50;
    var FUNCS = [ '_rv_pctouchpad_button', '_rv_pctouchpad_axis', '_rv_pctouchpad_trackpad' ];
    // Device motion. W3C DeviceMotionEvent: x right, y top, z toward the user, the same axes as SDL's phone frame at
    // natural orientation (SDL_sensor.h). The gyro goes out as SDL_sensor.h names its axes: x pitch (W3C beta),
    // y yaw (gamma), z roll (alpha). Browser rates are degrees per second; SDL takes radians per second.
    var DEG_TO_RAD = Math.PI / 180;
    // Screen angles come in degrees; the frame table has one row per quarter turn of a full turn.
    var DEGREES_PER_QUARTER_TURN = 90;
    var QUARTER_TURNS = 4;
    var AX = {X : 0, Y : 1, Z : 2};
    // Quarter turns of the screen, counter-clockwise: screen.orientation.angle / 90, modulo 4. Each row names the device
    // axis and sign of the pad's x and y; z stays. Row 1: the device top points left, so the pad's x is -device y.
    var SCREEN_FRAME = [
        {x : [ 1, AX.X ], y : [ 1, AX.Y ]}, {x : [ -1, AX.Y ], y : [ 1, AX.X ]}, {x : [ -1, AX.X ], y : [ -1, AX.Y ]},
        {x : [ 1, AX.Y ], y : [ -1, AX.X ]}
    ];

    // pointerId -> touch state. holders: element id -> pointer ids on it, the first one drives stick and trackpad.
    // clicks: button -> pending release timer. sinkChecked and sinkOk cache the console check.
    var active = {}, holders = {}, clicks = {}, sinkChecked = false, sinkOk = false;

    // A native build or an old kit has no input functions: one warning, then the pad sends nothing.
    function sink() {
        if (!sinkChecked) {
            sinkChecked = true;
            sinkOk = FUNCS.every(function(name) { return typeof Module[name] === 'function'; });
            if (!sinkOk) {
                console.warn('touchpad: console input functions are missing; the on-screen gamepad sends nothing');
            }
        }
        return sinkOk;
    }

    function button(b, down) { Module._rv_pctouchpad_button(b, down ? 1 : 0); }
    function axis(a, v) { Module._rv_pctouchpad_axis(a, v); }
    function trackpad(pad, down, x, y) { Module._rv_pctouchpad_trackpad(pad, down ? 1 : 0, x, y); }
    // Tap click: the button is down until CLICK_HOLD_MS after the last tap; a tap inside the hold restarts the timer.
    function click(b) {
        if (clicks[b] === undefined) {
            button(b, true);
        } else {
            clearTimeout(clicks[b]);
        }
        clicks[b] = setTimeout(function() {
            delete clicks[b];
            button(b, false);
        }, CLICK_HOLD_MS);
    }
    function clamp01(v) { return Math.min(1, Math.max(0, v)); }

    // Plate: touch presses, lift releases; the picture follows through rvTouchpad.press.
    function plateSet(id, down) {
        if (TRIGGER[id] !== undefined) {
            axis(TRIGGER[id], down ? 1 : 0);
        } else {
            button(PLATE[id], down);
        }
        rvTouchpad.press(id, down);
    }

    // Stick: offset from the element centre in half-sides, clipped to the unit circle; +y is up.
    function stickMove(st, p) {
        var s = STICK[st.el.id];
        var dx = (p.px - st.el.x - st.el.w / 2) / (st.el.w / 2);
        var dy = (st.el.y + st.el.h / 2 - p.py) / (st.el.h / 2);
        var k = Math.max(1, Math.hypot(dx, dy));
        axis(s.x, dx / k);
        axis(s.y, dy / k);
    }
    function stickDown(st, p) {
        stickMove(st, p);
        button(STICK[st.el.id].touch, true);
    }
    function stickUp(st, p, tap) {
        var s = STICK[st.el.id];
        axis(s.x, 0);
        axis(s.y, 0);
        button(s.touch, false);
        if (tap) {
            click(s.click);
        }
    }

    // Trackpad: touch position in 0..1 from the pad's top-left corner, clamped to its square.
    function padTouch(st, p, down) {
        var s = PAD[st.el.id];
        trackpad(s.pad, down, clamp01((p.px - st.el.x) / st.el.w), clamp01((p.py - st.el.y) / st.el.h));
    }
    function padDown(st, p) { padTouch(st, p, true); }
    function padUp(st, p, tap) {
        padTouch(st, p, false);
        if (tap) {
            click(PAD[st.el.id].click);
        }
    }

    // Per element kind: down starts output (first holder only), move follows the driver, up ends it
    // (last holder only), take hands the output to the next holder when the driver lifts.
    var KIND = {
        btn : {
            down : function(st) { plateSet(st.el.id, true); },
            move : function() {},
            up : function(st) { plateSet(st.el.id, false); },
            take : function() {}
        },
        stick : {down : stickDown, move : stickMove, up : stickUp, take : stickMove},
        pad : {down : padDown, move : padDown, up : padUp, take : padDown}
    };

    // First control under pad pixel (px, py). Well and empty ground are no control.
    function hitAt(elements, px, py) {
        for (var i = 0; i < elements.length; i++) {
            var el = elements[i];
            if (el.kind !== 'well' && px >= el.x && px < el.x + el.w && py >= el.y && py < el.y + el.h) {
                return el;
            }
        }
        return null;
    }

    function elementOf(elements, id) {
        var found = elements.filter(function(el) { return el.id === id; });
        return found[0] || null;
    }

    // Client -> canvas buffer (buffer over CSS size) -> pad pixels (over the scale).
    function toPad(cv, e, scale) {
        var r = cv.getBoundingClientRect();
        var px = (e.clientX - r.left) * cv.width / cv.clientWidth / scale;
        var py = (e.clientY - r.top) * cv.height / cv.clientHeight / scale;
        return {px : px, py : py};
    }

    // A hidden or unsized canvas has no pad geometry: nothing is sent while it is not shown.
    function live(cv) { return !cv.hidden && cv.clientWidth > 0 && cv.clientHeight > 0; }

    function holdersOf(id) {
        if (holders[id] === undefined) {
            holders[id] = [];
        }
        return holders[id];
    }

    // Touch starts on a control. The first pointer on it drives the output; later ones only join the holders.
    function onDown(e) {
        // A pointer id still active lost its up or cancel: end that touch first.
        finish(e.pointerId, true);
        var cv = document.getElementById(CANVAS_ID);
        if (!cv || e.target !== cv || !live(cv) || !sink()) {
            return;
        }
        var model = rvTouchpad.controls();
        var p = toPad(cv, e, model.scale);
        p.el = hitAt(model.elements, p.px, p.py);
        if (!p.el) {
            return;
        }
        cv.setPointerCapture(e.pointerId);
        var st = {
            pid : e.pointerId,
            el : p.el,
            sx : p.px,
            sy : p.py,
            px : p.px,
            py : p.py,
            cx : e.clientX,
            cy : e.clientY,
            t0 : Date.now(),
            moved : false
        };
        active[e.pointerId] = st;
        var h = holdersOf(p.el.id);
        h.push(e.pointerId);
        if (h.length === 1) {
            KIND[p.el.kind].down(st, p);
        }
    }

    // A layout that dropped the control cancels its touches; a hidden canvas sends nothing.
    function onMove(e) {
        var st = active[e.pointerId];
        var cv = document.getElementById(CANVAS_ID);
        if (!st || !cv) {
            return;
        }
        var model = rvTouchpad.controls();
        var el = elementOf(model.elements, st.el.id);
        if (!el) {
            dropHolders(st.el.id);
            return;
        }
        st.el = el;
        if (!live(cv)) {
            st.moved = true;
            return;
        }
        var p = toPad(cv, e, model.scale);
        st.px = p.px;
        st.py = p.py;
        st.cx = e.clientX;
        st.cy = e.clientY;
        if (Math.hypot(p.px - st.sx, p.py - st.sy) > TAP_SLOP) {
            st.moved = true;
        }
        if (holdersOf(el.id)[0] === e.pointerId) {
            KIND[el.kind].move(st, p);
        }
    }

    // Element gone from the layout: all its holders end with one release, no handover at the old geometry.
    function dropHolders(id) {
        var list = holdersOf(id).splice(0);
        if (list.length === 0) {
            return;
        }
        var first = active[list[0]];
        list.forEach(function(pid) { delete active[pid]; });
        KIND[first.el.kind].up(first, {px : first.px, py : first.py}, false);
    }

    // The driver lifted: the next holder takes the output. Its client point is re-projected at the current scale
    // and element rectangle; without a live canvas there is no geometry, so the element is dropped instead.
    function handOver(pid) {
        var next = active[pid];
        var cv = document.getElementById(CANVAS_ID);
        var model = rvTouchpad.controls();
        var el = elementOf(model.elements, next.el.id);
        if (el === null || !cv || !live(cv)) {
            dropHolders(next.el.id);
            return;
        }
        var p = toPad(cv, {clientX : next.cx, clientY : next.cy}, model.scale);
        next.el = el;
        next.px = p.px;
        next.py = p.py;
        KIND[el.kind].take(next, p);
    }

    // Lift or cancel, from the last known position. The last holder of a stick or trackpad releases without reading
    // the DOM (a lift within TAP_MS may tap). A plate release also redraws the pad through rvTouchpad.press, which
    // looks up the canvas. Otherwise handOver gives the output to the next holder, unless the element left the layout
    // or the canvas is gone, hidden or unsized: then the remaining holders are released instead.
    function finish(pid, cancel) {
        var st = active[pid];
        if (!st) {
            return;
        }
        delete active[pid];
        var h = holdersOf(st.el.id);
        var driver = h[0] === pid;
        h.splice(h.indexOf(pid), 1);
        if (h.length > 0) {
            if (driver) {
                handOver(h[0]);
            }
            return;
        }
        var tap = !cancel && !st.moved && Date.now() - st.t0 <= TAP_MS;
        KIND[st.el.kind].up(st, {px : st.px, py : st.py}, tap);
    }

    // Screen angle in degrees from screen.orientation, else the iOS window.orientation (there -90 is a clockwise turn).
    // The angle is rounded to whole quarter turns, then wrapped into the SCREEN_FRAME rows.
    function quarterTurn() {
        var so = window.screen && window.screen.orientation;
        var angle = so && typeof so.angle === 'number' ? so.angle : (window.orientation || 0);
        var turns = Math.round(angle / DEGREES_PER_QUARTER_TURN);
        return ((turns % QUARTER_TURNS) + QUARTER_TURNS) % QUARTER_TURNS;
    }

    // Device vector [x, y, z] -> pad vector for the screen's quarter turn.
    function toScreenFrame(v, quarter) {
        var f = SCREEN_FRAME[quarter];
        return [ f.x[0] * v[f.x[1]], f.y[0] * v[f.y[1]], v[AX.Z] ];
    }

    // A sample field must be a finite number: browsers send null for a missing sensor; NaN and Infinity are no data.
    function complete(values) {
        return values.every(function(v) { return typeof v === 'number' && isFinite(v); });
    }

    // A -1 from rv_pctouchpad_motion refuses for three reasons: no pad is attached, the pad has no gyro group, or a
    // sample is not finite (the page filters those out). The refusal holds until reload.
    var motionRefused = false;
    // One warning per load: the device gives no motion data. Each such sample is skipped.
    var motionDataWarned = false;
    // Motion is off for reasons that are not a fault of the sample: the permission is refused, the pad is not shown,
    // or the console has no motion function. Each case is silent; the page sends nothing until it changes.
    function motionOpen(cv) {
        var hasFunction = typeof Module._rv_pctouchpad_motion === 'function';
        return !motionRefused && Module.rvMotionAllowed !== false && hasFunction && !!cv && live(cv);
    }
    function noMotionData() {
        if (!motionDataWarned) {
            motionDataWarned = true;
            console.warn('touchpad: device motion samples have no numeric fields; such samples are skipped');
        }
    }

    function onMotion(e) {
        var cv = document.getElementById(CANVAS_ID);
        if (!motionOpen(cv)) {
            return;
        }
        var r = e.rotationRate, a = e.accelerationIncludingGravity;
        if (!r || !a || !complete([ r.alpha, r.beta, r.gamma, a.x, a.y, a.z ])) {
            noMotionData();
            return;
        }
        var q = quarterTurn();
        var g = toScreenFrame([ r.beta * DEG_TO_RAD, r.gamma * DEG_TO_RAD, r.alpha * DEG_TO_RAD ], q);
        var acc = toScreenFrame([ a.x, a.y, a.z ], q);
        if (Module._rv_pctouchpad_motion(g[0], g[1], g[2], acc[0], acc[1], acc[2]) === -1) {
            motionRefused = true;
        }
    }

    // A long press on the pad or the console canvas is a button hold; the browser's context menu would take it.
    // A mouse right click on the console canvas is stopped too, on purpose: the game does not need the browser menu.
    function onContextMenu(e) {
        var cv = document.getElementById(CANVAS_ID);
        if (e.target === cv || e.target === Module.canvas) {
            e.preventDefault();
        }
    }

    // Listeners are attached once, on the document and the window; the canvas is looked up at each event.
    document.addEventListener('pointerdown', onDown);
    document.addEventListener('pointermove', onMove);
    document.addEventListener('pointerup', function(e) { finish(e.pointerId, false); });
    document.addEventListener('pointercancel', function(e) { finish(e.pointerId, true); });
    document.addEventListener('contextmenu', onContextMenu);
    window.addEventListener('devicemotion', onMotion);
})();
