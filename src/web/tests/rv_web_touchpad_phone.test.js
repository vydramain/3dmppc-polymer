'use strict';
const test = require('node:test');
const assert = require('node:assert');
const {scene, loadParts} = require('./rv_web_touchpad_harness.js');

const UA_ANDROID_FIREFOX = 'Mozilla/5.0 (Android 14; Mobile; rv:133.0) Gecko/133.0 Firefox/133.0';
const UA_SAMSUNG = 'Mozilla/5.0 (Linux; Android 14; SM-S918B) AppleWebKit/537.36 (KHTML, like Gecko) ' +
                   'SamsungBrowser/25.0 Chrome/121.0.0.0 Mobile Safari/537.36';
const UA_WINDOWS =
    'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36';
const UA_MAC =
    'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15';
const UA_LINUX = 'Mozilla/5.0 (X11; Linux x86_64; rv:133.0) Gecko/20100101 Firefox/133.0';
const ua = (device) =>
    `Mozilla/5.0 (${device}; CPU ${device} OS 17_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Mobile/15E148`;

// The flag after loading the page scripts: matches is the (pointer: coarse) and (hover: none) answer.
const wanted = (matches, navigator, extra = {}) => loadParts({matches, navigator, ...extra}).Module.rvTouchpadWanted;

test('AC1: the coarse no-hover query matches on a desktop UA: wanted',
     () => { assert.strictEqual(wanted(true, {userAgent : UA_WINDOWS, maxTouchPoints : 0}), true); });

test('AC2: Android Firefox, query misses, no userAgentData: wanted',
     () => { assert.strictEqual(wanted(false, {userAgent : UA_ANDROID_FIREFOX}), true); });

test('AC3: Samsung Chrome, query misses, userAgentData.mobile true: wanted',
     () => { assert.strictEqual(wanted(false, {userAgent : UA_SAMSUNG, userAgentData : {mobile : true}}), true); });

test('AC4: desktop UA, query misses, userAgentData.mobile true: wanted',
     () => { assert.strictEqual(wanted(false, {userAgent : UA_WINDOWS, userAgentData : {mobile : true}}), true); });

test('AC5: iPhone, iPad and iPod UA, query misses: wanted', () => {
    for (const device of ['iPhone', 'iPad', 'iPod']) {
        assert.strictEqual(wanted(false, {userAgent : ua(device)}), true, device);
    }
});

test('AC6: iPad in desktop mode (Macintosh UA, 5 touch points), query misses: wanted',
     () => { assert.strictEqual(wanted(false, {userAgent : UA_MAC, maxTouchPoints : 5}), true); });

test('AC7: Macintosh UA without touch, query misses: not wanted',
     () => { assert.strictEqual(wanted(false, {userAgent : UA_MAC, maxTouchPoints : 0}), false); });

test('AC8: touch laptop (Windows Chrome, 10 touch points, userAgentData.mobile false): not wanted', () => {
    assert.strictEqual(wanted(false, {userAgent : UA_WINDOWS, maxTouchPoints : 10, userAgentData : {mobile : false}}), false);
});

test('AC9: Linux desktop, query misses: not wanted',
     () => { assert.strictEqual(wanted(false, {userAgent : UA_LINUX, maxTouchPoints : 0}), false); });

test('AC10: no userAgentData and no maxTouchPoints, desktop UA: not wanted, no throw', () => {
    assert.doesNotThrow(() => wanted(false, {userAgent : UA_WINDOWS}));
    assert.strictEqual(wanted(false, {userAgent : UA_WINDOWS}), false);
});

test('AC11: no matchMedia, Android UA: wanted, no throw', () => {
    const env = {navigator : {userAgent : UA_ANDROID_FIREFOX}, matchMedia : null};
    assert.doesNotThrow(() => loadParts(env));
    assert.strictEqual(loadParts(env).Module.rvTouchpadWanted, true);
});

test('AC12: with the AC2 environment the pad draws and has controls', () => {
    const s = scene(undefined, {matches : false, navigator : {userAgent : UA_ANDROID_FIREFOX}});
    assert.ok(s.ctx.calls.length > 0, 'the canvas got no fillRect calls');
    assert.ok(s.controls().length > 0, 'the pad has no controls');
});
