'use strict';
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const path = require('node:path');
const {AXIS, scene} = require('./rv_web_touchpad_harness.js');

const C_BUTTON = '#4e5744';
const BUTTON = {
    LEFT_STICK : 7,
    RIGHT_STICK : 8,
    MISC3 : 22,
    MISC4 : 23,
    SOUTH : 0
};
// Design constants of the cap, from rv_web_touchpad_draw.js. The cap travel is computed here by the formula in the
// plan (AC5), not through rvTouchDraw.capTravel: the test fixes behaviour, and the helper is an implementation detail.
const CAP_SHARE = 0.4, CAP_GAP_PX = 3;
const TAP_MS = 300, CLICK_HOLD_MS = 50;

const capSide = el => Math.round(Math.min(el.w, el.h) * CAP_SHARE);
const travel = el => Math.min(el.w, el.h) / 2 - capSide(el) / 2 - CAP_GAP_PX;
const near = (a, b, eps, msg) => assert.ok(Math.abs(a - b) <= eps, `${msg || 'value'}: ${a} is not within ${eps} of ${b}`);
const axes = (s, x, y) => [s.axis(x), s.axis(y)];
// Offset (dx, dy) in pad pixels, clipped to the unit circle of radius R: the axes the page must send (+y up).
const expected = (dx, dy, R) => {
    const k = Math.max(1, Math.hypot(dx / R, dy / R));
    return [ dx / R / k, -dy / R / k ];
};
// Centre of the cap in the last redraw: the C_BUTTON square of the cap side near the stick plate.
function capCentre(s, id) {
    const el = s.el(id), side = capSide(el);
    const caps = s.ctx.calls.filter(r => r.fillStyle === C_BUTTON && r.w === side && r.h === side && r.x > el.x - side &&
                                         r.x < el.x + el.w && r.y > el.y - side && r.y < el.y + el.h);
    assert.strictEqual(caps.length, 1, `expected one cap square for ${id}, found ${caps.length}`);
    return {x : caps[0].x + side / 2, y : caps[0].y + side / 2};
}
function assertCap(s, id, x, y, msg) {
    const c = capCentre(s, id);
    near(c.x, x, 1, `${msg} cap x`);
    near(c.y, y, 1, `${msg} cap y`);
}
// A start point inside LS, off its centre.
const startOf = el => ({x : el.x + 20, y : el.y + 18});

test('AC4: a touch at the plate edge sends axes (0, 0)', () => {
    const s = scene(), el = s.el('LS');
    s.down(1, el.x + 1, el.y + 1);
    assert.deepStrictEqual(axes(s, AXIS.LEFTX, AXIS.LEFTY), [ 0, 0 ]);
    s.up(1, el.x + 1, el.y + 1);
    s.down(2, el.x + el.w - 2, el.y + el.h - 2);
    assert.deepStrictEqual(axes(s, AXIS.LEFTX, AXIS.LEFTY), [ 0, 0 ]);
});

test('AC5: a shift of R/2 to the right gives LEFTX 0.5', () => {
    const s = scene(), el = s.el('LS'), R = travel(el), p = startOf(el);
    s.down(1, p.x, p.y);
    s.move(1, p.x + R / 2, p.y);
    near(s.axis(AXIS.LEFTX), 0.5, 1e-6, 'LEFTX');
    near(s.axis(AXIS.LEFTY), 0, 1e-6, 'LEFTY');
});

test('AC5: a shift of (3R, 4R) is clipped to (0.6, -0.8), +y up', () => {
    const s = scene(), el = s.el('LS'), R = travel(el), p = startOf(el);
    s.down(1, p.x, p.y);
    s.move(1, p.x + 3 * R, p.y + 4 * R);
    near(s.axis(AXIS.LEFTX), 0.6, 1e-6, 'LEFTX');
    near(s.axis(AXIS.LEFTY), -0.8, 1e-6, 'LEFTY');
});

test('AC5: a shift up gives positive LEFTY', () => {
    const s = scene(), el = s.el('LS'), R = travel(el), p = startOf(el);
    s.down(1, p.x, p.y);
    s.move(1, p.x, p.y - R / 4);
    near(s.axis(AXIS.LEFTY), 0.25, 1e-6, 'LEFTY');
});

test('AC6: a finger past the plate edge keeps changing the axes and the centre stays', () => {
    const s = scene(), el = s.el('LS'), R = travel(el), p = startOf(el);
    s.down(1, p.x, p.y);
    // Far right of the plate (x > el.x + el.w) and above its top (y < el.y).
    const far = {dx : el.x + el.w + 40 - p.x, dy : -(p.y + 30)};
    s.move(1, p.x + far.dx, p.y + far.dy);
    const [ex, ey] = expected(far.dx, far.dy, R);
    near(s.axis(AXIS.LEFTX), ex, 1e-6, 'LEFTX out');
    near(s.axis(AXIS.LEFTY), ey, 1e-6, 'LEFTY out');
    s.move(1, p.x + R / 2, p.y);
    near(s.axis(AXIS.LEFTX), 0.5, 1e-6, 'LEFTX back (the centre moved)');
    near(s.axis(AXIS.LEFTY), 0, 1e-6, 'LEFTY back');
});

test('AC7: a new touch of the same stick takes a new centre at its point', () => {
    const s = scene(), el = s.el('LS'), R = travel(el), p = startOf(el);
    s.down(1, p.x, p.y);
    s.move(1, p.x + R, p.y);
    s.up(1, p.x + R, p.y);
    assert.deepStrictEqual(axes(s, AXIS.LEFTX, AXIS.LEFTY), [ 0, 0 ]);
    const q = {x : el.x + el.w - 15, y : el.y + el.h - 10};
    s.down(2, q.x, q.y);
    assert.deepStrictEqual(axes(s, AXIS.LEFTX, AXIS.LEFTY), [ 0, 0 ], 'axes after the new touch');
    s.move(2, q.x - R / 2, q.y);
    near(s.axis(AXIS.LEFTX), -0.5, 1e-6, 'LEFTX');
});

test('AC7: when the driver lifts, the second finger gets its current point as the centre', () => {
    const s = scene(), el = s.el('LS'), R = travel(el), p = startOf(el);
    s.down(1, p.x, p.y);
    s.move(1, p.x + R / 2, p.y);
    const q = {x : el.x + el.w - 20, y : el.y + el.h - 12};
    s.down(2, q.x, q.y);
    s.move(2, q.x, q.y + 2);
    // The second finger does not drive while the first holds the stick.
    near(s.axis(AXIS.LEFTX), 0.5, 1e-6, 'LEFTX while the first finger drives');
    s.up(1, p.x + R / 2, p.y);
    assert.deepStrictEqual(axes(s, AXIS.LEFTX, AXIS.LEFTY), [ 0, 0 ], 'axes after the handover');
    s.move(2, q.x, q.y + 2 - R / 2);
    near(s.axis(AXIS.LEFTX), 0, 1e-6, 'LEFTX');
    near(s.axis(AXIS.LEFTY), 0.5, 1e-6, 'LEFTY');
});

test('AC8: the cap sits at the finger, also right after the touch and near the plate edge', () => {
    const s = scene(), el = s.el('LS'), p = startOf(el);
    s.down(1, p.x, p.y);
    assertCap(s, 'LS', p.x, p.y, 'after down');
    s.move(1, p.x + 8, p.y - 6);
    assertCap(s, 'LS', p.x + 8, p.y - 6, 'after move');
    s.up(1, p.x + 8, p.y - 6);
    const e = {x : el.x + 4, y : el.y + 4};
    s.down(2, e.x, e.y);
    assertCap(s, 'LS', e.x, e.y, 'edge down');
    s.move(2, e.x + 5, e.y + 3);
    assertCap(s, 'LS', e.x + 5, e.y + 3, 'edge move');
});

test('AC8: past R the cap stays R from the touch centre, towards the finger', () => {
    const s = scene(), el = s.el('LS'), R = travel(el), p = startOf(el);
    s.down(1, p.x, p.y);
    s.move(1, p.x + 3 * R, p.y + 4 * R);
    assertCap(s, 'LS', p.x + 0.6 * R, p.y + 0.8 * R, 'clipped');
});

test('AC8: the cap follows the second finger after the handover', () => {
    const s = scene(), el = s.el('LS'), p = startOf(el);
    s.down(1, p.x, p.y);
    const q = {x : el.x + el.w - 20, y : el.y + el.h - 12};
    s.down(2, q.x, q.y);
    s.up(1, p.x, p.y);
    assertCap(s, 'LS', q.x, q.y, 'after handover');
});

test('AC9: after up or cancel the cap is back in the plate centre', () => {
    const s = scene(), el = s.el('LS'), p = startOf(el);
    for (const end of ['up', 'cancel']) {
        s.down(1, p.x, p.y);
        s.move(1, p.x + 8, p.y - 6);
        s[end](1, p.x + 8, p.y - 6);
        assertCap(s, 'LS', el.x + el.w / 2, el.y + el.h / 2, end);
        assert.deepStrictEqual(axes(s, AXIS.LEFTX, AXIS.LEFTY), [ 0, 0 ], `${end} axes`);
    }
});

test('AC14: a tap on the stick clicks LEFT_STICK for CLICK_HOLD_MS and holds MISC3 while touched', () => {
    const s = scene(), el = s.el('LS'), p = startOf(el);
    s.down(1, p.x, p.y);
    assert.deepStrictEqual(s.buttons(BUTTON.MISC3), [ 1 ]);
    assert.deepStrictEqual(s.buttons(BUTTON.LEFT_STICK), []);
    s.advance(100);
    s.up(1, p.x, p.y);
    assert.deepStrictEqual(s.buttons(BUTTON.MISC3), [ 1, 0 ]);
    assert.deepStrictEqual(s.buttons(BUTTON.LEFT_STICK), [ 1 ]);
    s.advance(CLICK_HOLD_MS - 1);
    assert.deepStrictEqual(s.buttons(BUTTON.LEFT_STICK), [ 1 ], 'released early');
    s.advance(1);
    assert.deepStrictEqual(s.buttons(BUTTON.LEFT_STICK), [ 1, 0 ]);
});

test('AC14: a cancel, a long touch and a moved touch send no click; MISC3 still ends', () => {
    const s = scene(), el = s.el('LS'), p = startOf(el);
    s.down(1, p.x, p.y);
    s.cancel(1, p.x, p.y);
    s.down(2, p.x, p.y);
    s.advance(TAP_MS + 1);
    s.up(2, p.x, p.y);
    s.down(3, p.x, p.y);
    s.move(3, p.x + 20, p.y);
    s.up(3, p.x + 20, p.y);
    s.advance(1000);
    assert.deepStrictEqual(s.buttons(BUTTON.LEFT_STICK), []);
    assert.deepStrictEqual(s.buttons(BUTTON.MISC3), [ 1, 0, 1, 0, 1, 0 ]);
});

test('AC14: RS taps with RIGHT_STICK and MISC4', () => {
    const s = scene(), el = s.el('RS');
    s.down(1, el.x + 10, el.y + 10);
    s.up(1, el.x + 10, el.y + 10);
    assert.deepStrictEqual(s.buttons(BUTTON.MISC4), [ 1, 0 ]);
    assert.deepStrictEqual(s.buttons(BUTTON.RIGHT_STICK), [ 1 ]);
});

test('AC15: LS and RS work independently under two fingers, and plates are not pressed', () => {
    const s = scene(), l = s.el('LS'), r = s.el('RS'), RL = travel(l), RR = travel(r);
    const p = startOf(l), q = {x : r.x + 25, y : r.y + 30};
    s.down(1, p.x, p.y);
    s.down(2, q.x, q.y);
    s.move(1, p.x + RL / 2, p.y);
    assert.deepStrictEqual(axes(s, AXIS.RIGHTX, AXIS.RIGHTY), [ 0, 0 ], 'RS moved with LS');
    s.move(2, q.x, q.y - RR / 2);
    near(s.axis(AXIS.LEFTX), 0.5, 1e-6, 'LEFTX');
    near(s.axis(AXIS.LEFTY), 0, 1e-6, 'LEFTY');
    near(s.axis(AXIS.RIGHTX), 0, 1e-6, 'RIGHTX');
    near(s.axis(AXIS.RIGHTY), 0.5, 1e-6, 'RIGHTY');
    s.up(1, p.x + RL / 2, p.y);
    assert.deepStrictEqual(axes(s, AXIS.LEFTX, AXIS.LEFTY), [ 0, 0 ]);
    near(s.axis(AXIS.RIGHTY), 0.5, 1e-6, 'RIGHTY after LS lift');
    const a = s.el('A');
    s.down(3, a.x + a.w / 2, a.y + a.h / 2);
    assert.deepStrictEqual(s.buttons(BUTTON.SOUTH), [ 1 ]);
    near(s.axis(AXIS.RIGHTY), 0.5, 1e-6, 'RIGHTY after a plate press');
    assert.deepStrictEqual(s.buttons(BUTTON.MISC3), [ 1, 0 ]);
    assert.deepStrictEqual(s.buttons(BUTTON.MISC4), [ 1 ]);
});

test('AC18: a shift of 1 pad pixel gives a nonzero axis, 1/R', () => {
    const s = scene(), el = s.el('LS'), R = travel(el), p = startOf(el);
    s.down(1, p.x, p.y);
    s.move(1, p.x + 1, p.y);
    assert.notStrictEqual(s.axis(AXIS.LEFTX), 0);
    near(s.axis(AXIS.LEFTX), 1 / R, 1e-6, 'LEFTX');
});

test('AC16, AC17: the page scripts have no requestAnimationFrame, setInterval, import or export', () => {
    const dir = path.join(__dirname, '..');
    const files = fs.readdirSync(dir).filter(f => f.endsWith('.js'));
    assert.ok(files.length >= 5, 'page scripts not found');
    for (const f of files) {
        const text = fs.readFileSync(path.join(dir, f), 'utf8');
        const hit = text.match(/requestAnimationFrame|setInterval|import |export /);
        assert.strictEqual(hit, null, `${f} contains ${hit && hit[0]}`);
    }
});
