// Emscripten --pre-js: mounts /save on IndexedDB and loads it before main() runs,
// so the memory card saved by an earlier page session is already there.
// The run dependency holds main() back until the load finishes.
Module.preRun = Module.preRun || [];
Module.preRun.push(function() {
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
