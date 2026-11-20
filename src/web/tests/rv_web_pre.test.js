'use strict';
const test = require('node:test');
const assert = require('node:assert');
const vm = require('node:vm');
const {run} = require('./rv_web_touchpad_harness.js');

// Loads rv_web_pre.js with a spy console, a navigator without hid and a WebAssembly without Suspending.
function loadPre(module = {}) {
    const calls = [];
    const spy = method => (...args) => calls.push({method, args});
    const console = {debug : spy('debug'), info : spy('info'), warn : spy('warn'), error : spy('error'), log : spy('log')};
    const context = vm.createContext({console, Module : module, navigator : {}, WebAssembly : {}});
    run(context, 'rv_web_pre.js');
    return {context, calls, module};
}

const LINES = [
    [ '[mppc][INF] pcboot: ca: ok', 'info' ],
    [ '[disc][INF] sound: x', 'info' ],
    [ '[mppc][DBG] a: b', 'debug' ],
    [ '[mppc][WRN] a: b', 'warn' ],
    [ '[mppc][ERR] a: b', 'error' ],
    [ '[mppc][EMG] a: b', 'error' ],
    [ 'SDL: something', 'error' ],
];

for (const [line, method] of LINES) {
    test(`AC1 printErr sends "${line}" to console.${method} once, as is`, () => {
        const {calls, module} = loadPre();
        module.printErr(line);
        assert.deepStrictEqual(calls, [ {method, args : [ line ]} ]);
    });
}

test('AC2 a printErr set before the load stays the same function, print is not created', () => {
    const mine = () => {};
    const {module} = loadPre({printErr : mine});
    assert.strictEqual(module.printErr, mine);
    assert.ok(!('print' in module));
});

test('AC2 print set before the load is not changed', () => {
    const mine = () => {};
    const {module} = loadPre({print : mine});
    assert.strictEqual(module.print, mine);
});

test('AC5 the "webhid: HIDAPI off" line goes to console.info, not console.warn', () => {
    const {context, calls, module} = loadPre();
    context.ENV = {};
    context.IDBFS = {};
    context.FS = {analyzePath : () => ({exists : true}), mkdir() {}, mount() {}, syncfs() {}};
    context.addRunDependency = () => {};
    context.removeRunDependency = () => {};
    module.preRun[0]();
    const line = calls.filter(c => String(c.args[0]).startsWith('webhid: HIDAPI off'));
    assert.deepStrictEqual(line.map(c => c.method), [ 'info' ]);
    assert.strictEqual(calls.filter(c => c.method === 'warn').length, 0);
});
