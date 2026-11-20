'use strict';
const test = require('node:test');
const assert = require('node:assert');
const {scene} = require('./rv_web_touchpad_harness.js');

const C_BRIGHT = '#f1f2f0';
const BUTTON = {
    TOUCHPAD : 20,
    MISC2 : 21
};
// Design constants of the finger point, from rv_web_touchpad_draw.js.
const FINGER_PX = 6, FIELD_INSET_PX = 5;
const TAP_MS = 300, CLICK_HOLD_MS = 50;

const near = (a, b, eps, msg) => assert.ok(Math.abs(a - b) <= eps, `${msg || 'value'}: ${a} is not within ${eps} of ${b}`);
const centre = el => ({x : el.x + el.w / 2, y : el.y + el.h / 2});
// Finger squares in the last redraw: C_BRIGHT, FINGER_PX by FINGER_PX, on the plate.
function fingers(s, id) {
    const el = s.el(id);
    return s.ctx.calls.filter(r => r.fillStyle === C_BRIGHT && r.w === FINGER_PX && r.h === FINGER_PX &&
                                   r.x >= el.x - FINGER_PX && r.x <= el.x + el.w && r.y >= el.y - FINGER_PX &&
                                   r.y <= el.y + el.h);
}
function assertFinger(s, id, x, y, msg) {
    const f = fingers(s, id);
    assert.strictEqual(f.length, 1, `${msg}: expected one finger square on ${id}, found ${f.length}`);
    near(f[0].x + FINGER_PX / 2, x, 1, `${msg} finger x`);
    near(f[0].y + FINGER_PX / 2, y, 1, `${msg} finger y`);
}

test('touch on a trackpad draws the finger square at the finger', () => {
    const s = scene([ 'trackpads' ]);
    const el = s.el('LP'), c = centre(el);
    const x = el.x + el.w * 0.25, y = el.y + el.h * 0.75;
    s.down(1, x, y);
    assertFinger(s, 'LP', x, y, 'down');
    s.move(1, c.x, c.y);
    assertFinger(s, 'LP', c.x, c.y, 'moved to the centre');
    s.move(1, el.x + el.w * 0.8, el.y + el.h * 0.3);
    assertFinger(s, 'LP', el.x + el.w * 0.8, el.y + el.h * 0.3, 'moved again');
});

test('the right trackpad draws its own finger', () => {
    const s = scene([ 'trackpads' ]);
    const el = s.el('RP');
    const x = el.x + el.w * 0.6, y = el.y + el.h * 0.4;
    s.down(1, x, y);
    assertFinger(s, 'RP', x, y, 'RP down');
    assert.strictEqual(fingers(s, 'LP').length, 0, 'the other pad has no finger');
});

test('a point beyond the plate edge keeps the finger square at the edge', () => {
    const s = scene([ 'trackpads' ]);
    const el = s.el('LP'), c = centre(el);
    s.down(1, c.x, c.y);
    s.move(1, el.x + el.w + 40, el.y - 40);
    assertFinger(s, 'LP', el.x + el.w - FIELD_INSET_PX, el.y + FIELD_INSET_PX, 'far top right');
    s.move(1, el.x - 40, el.y + el.h + 40);
    assertFinger(s, 'LP', el.x + FIELD_INSET_PX, el.y + el.h - FIELD_INSET_PX, 'far bottom left');
});

test('no finger square after up', () => {
    const s = scene([ 'trackpads' ]);
    const el = s.el('LP'), c = centre(el);
    s.down(1, c.x, c.y);
    s.up(1, c.x, c.y);
    assert.strictEqual(fingers(s, 'LP').length, 0);
});

test('no finger square after cancel', () => {
    const s = scene([ 'trackpads' ]);
    const el = s.el('LP'), c = centre(el);
    s.down(1, c.x, c.y);
    s.cancel(1, c.x, c.y);
    assert.strictEqual(fingers(s, 'LP').length, 0);
});

test('a tap on LP clicks TOUCHPAD for CLICK_HOLD_MS', () => {
    const s = scene([ 'trackpads' ]);
    const c = centre(s.el('LP'));
    s.down(1, c.x, c.y);
    s.advance(TAP_MS / 3);
    s.up(1, c.x, c.y);
    assert.deepStrictEqual(s.buttons(BUTTON.TOUCHPAD), [ 1 ]);
    s.advance(CLICK_HOLD_MS - 1);
    assert.deepStrictEqual(s.buttons(BUTTON.TOUCHPAD), [ 1 ]);
    s.advance(1);
    assert.deepStrictEqual(s.buttons(BUTTON.TOUCHPAD), [ 1, 0 ]);
});

test('a tap on RP clicks MISC2', () => {
    const s = scene([ 'trackpads' ]);
    const c = centre(s.el('RP'));
    s.down(1, c.x, c.y);
    s.advance(TAP_MS / 3);
    s.up(1, c.x, c.y);
    s.advance(CLICK_HOLD_MS);
    assert.deepStrictEqual(s.buttons(BUTTON.MISC2), [ 1, 0 ]);
    assert.deepStrictEqual(s.buttons(BUTTON.TOUCHPAD), []);
});

test('trackpad calls: 0..1 from the top-left corner, down 1 then 0', () => {
    const s = scene([ 'trackpads' ]);
    const el = s.el('LP');
    s.down(1, el.x + el.w * 0.25, el.y + el.h * 0.75);
    s.move(1, el.x + el.w + 40, el.y - 40);
    s.up(1, el.x + el.w + 40, el.y - 40);
    const t = s.calls.trackpad;
    assert.deepStrictEqual(t.map(c => [c.pad, c.down]), t.map((c, i) => [0, i < t.length - 1]));
    near(t[0].x, 0.25, 1e-6, 'down x');
    near(t[0].y, 0.75, 1e-6, 'down y');
    near(t[t.length - 1].x, 1, 1e-6, 'up x clamped');
    near(t[t.length - 1].y, 0, 1e-6, 'up y clamped');
    assert.strictEqual(t[0].down, true);
});

test('RP sends trackpad pad 1', () => {
    const s = scene([ 'trackpads' ]);
    const c = centre(s.el('RP'));
    s.down(1, c.x, c.y);
    assert.deepStrictEqual(s.calls.trackpad.map(c => [c.pad, c.down]), [ [ 1, true ] ]);
});
