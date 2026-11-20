'use strict';
const test = require('node:test');
const assert = require('node:assert');
const {load, recordingContext} = require('./rv_web_touchpad_harness.js');

const C_BUTTON = '#4e5744', C_INSET = '#3e4637', C_DARK = '#282e20';
const C_HI = '#7e8776', C_LO = '#32392c', C_TEXT = '#d8ded3', C_BRIGHT = '#f1f2f0';

const draw = load().rvTouchDraw;

function plate(c, pressed) {
    const ctx = recordingContext();
    draw.control(ctx, Object.assign({kind : 'btn'}, c), 'A', pressed, null);
    return ctx.calls;
}
const of = (calls, col, alpha) => calls.filter(r => r.fillStyle === col && (alpha === undefined || r.globalAlpha === alpha));
// Label pixels as sorted "x,y,w,h" keys, optionally shifted.
const keys = (calls, dx, dy) => of(calls, C_TEXT).map(r => `${r.x + dx},${r.y + dy},${r.w},${r.h}`).sort();

// A big plate takes the 2x label, a small one the 1x label.
for (const c of [{x : 10, y : 20, w : 60, h : 60}, {x : 10, y : 20, w : 30, h : 30}]) {
    const name = `${c.w}x${c.h} plate`;

    test(`AC10 pressed ${name}: dark outline and opaque inset face`, () => {
        const calls = plate(c, true);
        const dark = of(calls, C_DARK, 1);
        assert.ok(dark.length > 0, 'no opaque C_DARK outline');
        for (const r of dark) {
            assert.ok(r.x >= c.x && r.y >= c.y && r.x + r.w <= c.x + c.w && r.y + r.h <= c.y + c.h,
                      'outline outside the plate');
        }
        const inset = of(calls, C_INSET, 1);
        assert.ok(inset.length > 0, 'no opaque C_INSET face');
        assert.strictEqual(of(calls, C_BUTTON).length, 0, 'released face colour on a pressed plate');
    });

    test(`AC10 pressed ${name}: bevel is sunk`, () => {
        const calls = plate(c, true);
        const mid = c.y + c.h / 2, midX = c.x + c.w / 2;
        const top = r => r.h === 1 && r.w > 1 && r.y < mid, left = r => r.w === 1 && r.h > 1 && r.x < midX;
        const bottom = r => r.h === 1 && r.w > 1 && r.y >= mid, right = r => r.w === 1 && r.h > 1 && r.x >= midX;
        const lo = of(calls, C_LO), hi = of(calls, C_HI);
        assert.ok(lo.some(top) && lo.some(left), 'C_LO missing on top/left');
        assert.ok(hi.some(bottom) && hi.some(right), 'C_HI missing on bottom/right');
        assert.ok(!hi.some(top) && !hi.some(left), 'C_HI on top/left');
        assert.ok(!lo.some(bottom) && !lo.some(right), 'C_LO on bottom/right');
    });

    test(`AC10 pressed ${name}: label is C_TEXT shifted by (+1,+1)`, () => {
        const pressed = plate(c, true), released = plate(c, false);
        assert.strictEqual(of(pressed, C_BRIGHT).length, 0, 'C_BRIGHT label on a pressed plate');
        assert.ok(keys(released, 0, 0).length > 0, 'released plate has no label');
        assert.deepStrictEqual(keys(pressed, 0, 0), keys(released, 1, 1));
    });

    test(`AC11 released ${name}: unchanged look`, () => {
        const calls = plate(c, false);
        assert.deepStrictEqual(calls[0], {x : c.x, y : c.y, w : c.w, h : c.h, fillStyle : C_BUTTON, globalAlpha : 0.10});
        assert.strictEqual(of(calls, C_DARK).length, 0);
        assert.strictEqual(of(calls, C_INSET).length, 0);
        assert.strictEqual(of(calls, C_BRIGHT).length, 0);
        // Raised bevel: C_HI on top/left, C_LO on bottom/right, all at half strength.
        const hiTop = {x : c.x, y : c.y, w : c.w, h : 1, fillStyle : C_HI, globalAlpha : 0.5};
        const hiLeft = {x : c.x, y : c.y, w : 1, h : c.h, fillStyle : C_HI, globalAlpha : 0.5};
        const loBottom = {x : c.x, y : c.y + c.h - 1, w : c.w, h : 1, fillStyle : C_LO, globalAlpha : 0.5};
        const loRight = {x : c.x + c.w - 1, y : c.y, w : 1, h : c.h, fillStyle : C_LO, globalAlpha : 0.5};
        for (const want of [hiTop, hiLeft, loBottom, loRight]) {
            assert.deepStrictEqual(calls.filter(r => JSON.stringify(r) === JSON.stringify(want)).length, 1);
        }
        // 8 bevel rects, then the label at C_TEXT / 0.5, centred with no shift.
        assert.strictEqual(calls.length, 1 + 8 + of(calls, C_TEXT, 0.5).length);
        assert.ok(of(calls, C_TEXT, 0.5).length > 0);
    });
}
