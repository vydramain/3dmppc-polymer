// Boot-journal facts, read by C from Module. Keep this block above any WebAssembly stand-in (W4a):
// rvWebhidUsable is the browser's own answer, so it must be read before a stand-in makes JSPI look present.
// rvSaveLoadFailed is set by the IndexedDB load below; main() waits for that load.
Module.rvWebhidUsable = !!navigator.hid && typeof WebAssembly.Suspending === 'function';
Module.rvSaveLoadFailed = false;

// Without JSPI the glue calls new WebAssembly.Suspending at load and the page throws there. Stand-ins let it load.
// The stand-ins make JSPI look present to any later check, so rvWebhidUsable is read above them.
// SDL_EMSCRIPTEN_ASYNCIFY=0 keeps async code from running.
var rvJspiStub = typeof WebAssembly.Suspending !== 'function';
if (rvJspiStub) {
    WebAssembly.Suspending = function(fn) { return fn; };
    WebAssembly.promising = function(fn) {
        return function() {
            try {
                return Promise.resolve(fn.apply(this, arguments));
            } catch (err) {
                return Promise.reject(err);
            }
        };
    };
}

// Emscripten --pre-js: mounts /save on IndexedDB and loads it before main() runs,
// so the memory card saved by an earlier page session is already there.
// The run dependency holds main() back until the load finishes.
Module.preRun = Module.preRun || [];
Module.preRun.push(function() {
    // Without WebHID the HIDAPI joystick driver would wait for devices that cannot come; pads use the Gamepad API.
    if (!Module.rvWebhidUsable) {
        ENV.SDL_JOYSTICK_HIDAPI = '0';
    }
    // Stand-ins cannot suspend: emscripten_sleep would hand wasm a Promise, so the hint is off only there.
    // With real JSPI the hint stays on: SDL_Delay yields to the browser through JSPI; present does not (patch 0005).
    if (rvJspiStub) {
        ENV.SDL_EMSCRIPTEN_ASYNCIFY = '0';
    }
    if (Module.rvWebhidUsable) {
        console.log('webhid: HIDAPI joysticks on, WebHID and JSPI present');
    } else {
        console.warn('webhid: HIDAPI off, pads come through the Gamepad API; WebHID ' +
                     (navigator.hid ? 'present' : 'missing') + ', JSPI ' + (rvJspiStub ? 'missing' : 'present'));
    }
    var loadDependency = 'rv_save_load';
    // A flush before a successful load would reconcile IndexedDB against an empty card and erase it.
    var loaded = false;
    // Defers a flush while the previous one is still writing, so two syncfs calls never overlap.
    var flushing = false;
    // Set when a flush was requested during another one.
    var pending = false;
    if (!FS.analyzePath('/save').exists) {
        FS.mkdir('/save');
    }
    FS.mount(IDBFS, {}, '/save');
    addRunDependency(loadDependency);
    FS.syncfs(true, function(err) {
        // An empty card beats a game that never starts, so a failed load still runs, but never flushes.
        if (err) {
            Module.rvSaveLoadFailed = true;
            console.error('save: IndexedDB load failed, card is not saved this session: ' + err);
        } else {
            loaded = true;
        }
        removeRunDependency(loadDependency);
    });

    // Writes /save back to IndexedDB. Called by main_web.cpp on a timer and on pagehide.
    Module.rvSaveFlush = function() {
        if (!loaded) {
            return;
        }
        if (flushing) {
            // The in-flight snapshot may predate a write, so flush again when it ends.
            pending = true;
            return;
        }
        flushing = true;
        FS.syncfs(false, function(err) {
            flushing = false;
            if (err) {
                console.error('save: IndexedDB flush failed: ' + err);
            }
            if (pending) {
                pending = false;
                Module.rvSaveFlush();
            }
        });
    };
});
