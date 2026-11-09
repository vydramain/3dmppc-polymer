// Emscripten --js-library: WebHID device table for the hidapi backend.
// A device joins the table only after open() succeeds; ids start at 1 and are never reused.
// A run dependency holds main() until the devices present at load are opened or have failed.
addToLibrary({
    $rvWebhid : {
        // hidapi's libusb list holds up to 32 reports: its comment says 30, but num_queued counts the
        // nodes after the head and pops only above 30 (hid.c:1269-1291).
        MAX_QUEUED_REPORTS : 32,
        // hidapi's libusb hid_write fails when the transfer does not finish in 1000 ms (hid.c:1797-1801);
        // the feature control transfers use the same 1000 ms (hid.c:1950, hid.c:1981).
        STALL_MS : 1000,
        // A UTF-8 continuation byte is 10xxxxxx; a name cut must not start on one.
        UTF8_CONT_MASK : 0xC0,
        UTF8_CONT_TAG : 0x80,
        // Returned by timed() when the limit passes before the promise settles, and by wait() when ms > 0 passes.
        TIMED_OUT : {},
        RUN_DEPENDENCY : 'rv_webhid',
        // HIDDevice -> its open attempt { promise }; a device in the table keeps its attempt.
        known : new Map(),
        devices : [],
        nextId : 1,
        changes : 0,
        find : function(id) { return rvWebhid.devices.find(function(entry) { return entry.id === id; }); },
        // WebHID gives no bus. A pad in the table is in Bluetooth mode when the report's presence equals btIfPresent;
        // otherwise its bus is not determined (USB is not told apart). Devices outside the table: not determined.
        busRules : [
            // DualSense and DualSense Edge: Bluetooth report 0x31, USB 0x02 (Linux drivers/hid/hid-playstation.c,
            // DS_OUTPUT_REPORT_BT and DS_OUTPUT_REPORT_USB). Checked in the USB and Bluetooth descriptors of
            // github.com/nondebug/dualsense: output 0x31 is in the Bluetooth descriptor, output 0x02 in the USB one.
            // Medium confidence: one source; hardware check in W9.
            {vendor : 0x054C, products : [ 0x0CE6, 0x0DF2 ], report : 0x31, btIfPresent : true},
            // DualShock 4: Bluetooth report 0x11, USB 0x05 (hid-playstation.c, DS4_OUTPUT_REPORT_BT and
            // DS4_OUTPUT_REPORT_USB). Low confidence: kernel constants only, descriptors not read; hardware check in W9.
            {vendor : 0x054C, products : [ 0x05C4, 0x09CC ], report : 0x11, btIfPresent : true},
        ],
        // 1 when the device is a known pad in its Bluetooth mode, else 0 (the bus is not determined).
        bluetooth : function(device) {
            var rule = rvWebhid.busRules.find(function(
                entry) { return entry.vendor === device.vendorId && entry.products.indexOf(device.productId) >= 0; });
            if (!rule) {
                return 0;
            }
            return rvWebhid.hasReport(device.collections, rule.report) === rule.btIfPresent ? 1 : 0;
        },
        // Searches every collection and its children: a numbered output report can sit in a nested one.
        hasReport : function(collections, reportId) {
            return collections.some(function(collection) {
                return collection.outputReports.some(function(report) { return report.reportId === reportId; }) ||
                       rvWebhid.hasReport(collection.children, reportId);
            });
        },
        // Every early exit with -1 says which C function refused and why.
        fail : function(name, id, reason) { console.warn('webhid: ' + name + '(' + id + ') ' + reason); },
        // Opens a device once; a second call gets the same attempt. The entry is added only if this
        // attempt is still the device's attempt when open() resolves.
        open : function(device) {
            var attempt = rvWebhid.known.get(device);
            if (attempt) {
                return attempt.promise;
            }
            attempt = {};
            rvWebhid.known.set(device, attempt);
            attempt.promise = device.open().then(
                function() {
                    if (rvWebhid.known.get(device) !== attempt) {
                        // Removed while opening: close it unless a newer attempt now owns the device.
                        if (!rvWebhid.known.has(device)) {
                            return device.close().catch(function(
                                err) { console.warn('webhid: close failed for ' + device.productName + ': ' + err); });
                        }
                        return;
                    }
                    var entry = {
                        id : rvWebhid.nextId++,
                        device : device,
                        queue : [],
                        waiters : [],
                        sendChain : Promise.resolve(),
                        unsettled : []
                    };
                    entry.onReport = function(ev) {
                        var bytes = new Uint8Array(ev.data.buffer, ev.data.byteOffset, ev.data.byteLength);
                        var report = {reportId : ev.reportId, bytes : bytes};
                        // A waiting read takes the report first, in call order; otherwise it is queued.
                        var waiter = entry.waiters.shift();
                        if (waiter) {
                            waiter(report);
                            return;
                        }
                        entry.queue.push(report);
                        if (entry.queue.length > rvWebhid.MAX_QUEUED_REPORTS) {
                            entry.queue.shift();
                        }
                    };
                    device.addEventListener('inputreport', entry.onReport);
                    rvWebhid.devices.push(entry);
                    rvWebhid.changes++;
                },
                function(err) {
                    if (rvWebhid.known.get(device) === attempt) {
                        rvWebhid.known.delete(device);
                    }
                    console.warn('webhid: open failed for ' + device.productName + ': ' + err);
                });
            return attempt.promise;
        },
        remove : function(device) {
            rvWebhid.known.delete(device);
            var index = rvWebhid.devices.findIndex(function(entry) { return entry.device === device; });
            if (index < 0) {
                return;
            }
            var entry = rvWebhid.devices[index];
            device.removeEventListener('inputreport', entry.onReport);
            rvWebhid.devices.splice(index, 1);
            rvWebhid.changes++;
            // The device is gone: every waiting read ends with null.
            entry.waiters.slice().forEach(function(settle) { settle(null); });
        },
        // Prefixes the report id byte, as hidapi does for every feature report and numbered input report.
        withId : function(reportId, bytes) {
            var out = new Uint8Array(bytes.length + 1);
            out[0] = reportId;
            out.set(bytes, 1);
            return out;
        },
        // Copies at most len bytes of src to the C buffer and returns the count.
        copyOut : function(ptr, len, src) {
            var n = Math.max(0, Math.min(len, src.length));
            HEAPU8.set(src.subarray(0, n), ptr);
            return n;
        },
        // Queues a send behind the device's earlier sends. Resolves true when fn settled without error.
        enqueue : function(entry, name, fn) {
            entry.unsettled.push(performance.now());
            var sent = entry.sendChain.then(fn).then(function() { return true; },
                                                     function(err) {
                                                         rvWebhid.fail(name, entry.id, 'send failed: ' + err);
                                                         return false;
                                                     });
            entry.sendChain = sent.then(function() { entry.unsettled.shift(); });
            return sent;
        },
        // Age in ms of the oldest unsettled send, 0 when none is pending.
        stallAge : function(entry) { return entry.unsettled.length > 0 ? performance.now() - entry.unsettled[0] : 0; },
        // A send is accepted only while the oldest unsettled one is younger than STALL_MS, so the backlog
        // holds only sends queued within one STALL_MS window.
        stalled : function(entry) { return rvWebhid.stallAge(entry) >= rvWebhid.STALL_MS; },
        failStalled : function(name, entry) {
            rvWebhid.fail(name, entry.id,
                          'oldest unsettled send queued ' + Math.round(rvWebhid.stallAge(entry)) + ' ms ago, at or over ' +
                              rvWebhid.STALL_MS + ' ms; refused');
        },
        // Settles with the promise's outcome, or with TIMED_OUT once STALL_MS has passed; onGiveUp runs
        // first. The timer is cleared when the promise settles first, so a late answer is ignored.
        timed : function(promise, onGiveUp) {
            var timer;
            var limit = new Promise(function(resolve) {
                timer = setTimeout(function() {
                    if (onGiveUp) {
                        onGiveUp();
                    }
                    resolve(rvWebhid.TIMED_OUT);
                }, rvWebhid.STALL_MS);
            });
            return Promise.race([ promise, limit ]).finally(function() { clearTimeout(timer); });
        },
        // Settles with the next report of entry, null when the device is removed, or TIMED_OUT when ms > 0 passes.
        // ms <= 0 waits without a limit; ms 0 is for the caller to return at once, as hidapi does.
        // Waiters are served in call order, one report each; a report that arrives with no waiter is queued.
        wait : function(entry, ms) {
            return new Promise(function(resolve) {
                var timer;
                var settle = function(value) {
                    clearTimeout(timer);
                    var index = entry.waiters.indexOf(settle);
                    if (index >= 0) {
                        entry.waiters.splice(index, 1);
                    }
                    resolve(value);
                };
                entry.waiters.push(settle);
                if (ms > 0) {
                    timer = setTimeout(function() { settle(rvWebhid.TIMED_OUT); }, ms);
                }
            });
        },
        start : function() {
            // Without JSPI or WebHID the page has no HIDAPI path, so start() does nothing.
            if (!Module.rvWebhidUsable) {
                return;
            }
            // The page's permission button passes devices granted by requestDevice here; open() skips a known device.
            Module.rvWebhidOpen = rvWebhid.open;
            addRunDependency(rvWebhid.RUN_DEPENDENCY);
            navigator.hid.addEventListener('connect', function(ev) { rvWebhid.open(ev.device); });
            navigator.hid.addEventListener('disconnect', function(ev) { rvWebhid.remove(ev.device); });
            navigator.hid.getDevices()
                .then(function(devices) { return Promise.all(devices.map(rvWebhid.open)); })
                .catch(function(err) { console.warn('webhid: getDevices failed: ' + err); })
                .finally(function() { removeRunDependency(rvWebhid.RUN_DEPENDENCY); });
        },
    },
    $rvWebhid__deps : [ '$addRunDependency', '$removeRunDependency' ],
    $rvWebhid__postset : 'rvWebhid.start();',

    rv_webhid_change_count__deps : [ '$rvWebhid' ],
    rv_webhid_change_count : function() { return rvWebhid.changes; },

    rv_webhid_count__deps : [ '$rvWebhid' ],
    rv_webhid_count : function() { return rvWebhid.devices.length; },

    rv_webhid_id__deps : [ '$rvWebhid' ],
    rv_webhid_id : function(index) {
        var entry = rvWebhid.devices[index];
        if (!entry) {
            rvWebhid.fail('rv_webhid_id', index, 'index out of range');
            return -1;
        }
        return entry.id;
    },

    rv_webhid_info__deps : [ '$rvWebhid' ],
    rv_webhid_info : function(id, vendor, product, usagePage, usage, bluetooth) {
        var entry = rvWebhid.find(id);
        if (!entry) {
            rvWebhid.fail('rv_webhid_info', id, 'unknown id');
            return -1;
        }
        var first = entry.device.collections[0];
        HEAP32[vendor >> 2] = entry.device.vendorId;
        HEAP32[product >> 2] = entry.device.productId;
        HEAP32[usagePage >> 2] = first ? first.usagePage : 0;
        HEAP32[usage >> 2] = first ? first.usage : 0;
        HEAP32[bluetooth >> 2] = rvWebhid.bluetooth(entry.device);
        return 0;
    },

    rv_webhid_product_name__deps : [ '$rvWebhid' ],
    rv_webhid_product_name : function(id, buf, len) {
        var entry = rvWebhid.find(id);
        if (!entry) {
            rvWebhid.fail('rv_webhid_product_name', id, 'unknown id');
            return -1;
        }
        if (len < 1) {
            rvWebhid.fail('rv_webhid_product_name', id, 'len < 1');
            return -1;
        }
        var bytes = new TextEncoder().encode(entry.device.productName);
        // Cut at a character start so the UTF-8 stays whole; one byte is kept for the NUL.
        var n = Math.min(bytes.length, len - 1);
        while (n > 0 && (bytes[n] & rvWebhid.UTF8_CONT_MASK) === rvWebhid.UTF8_CONT_TAG) {
            n--;
        }
        HEAPU8.set(bytes.subarray(0, n), buf);
        HEAPU8[buf + n] = 0;
        return n;
    },

    rv_webhid_read__deps : [ '$rvWebhid' ],
    rv_webhid_read : function(id, buf, len) {
        var entry = rvWebhid.find(id);
        if (!entry) {
            rvWebhid.fail('rv_webhid_read', id, 'unknown id');
            return -1;
        }
        if (len < 1) {
            rvWebhid.fail('rv_webhid_read', id, 'len < 1');
            return -1;
        }
        var report = entry.queue.shift();
        if (!report) {
            return 0;
        }
        var data = report.reportId !== 0 ? rvWebhid.withId(report.reportId, report.bytes) : report.bytes;
        return rvWebhid.copyOut(buf, len, data);
    },

    rv_webhid_read_timeout__deps : [ '$rvWebhid' ],
    rv_webhid_read_timeout__async : true,
    // Like hid_read_timeout: ms 0 does not wait, ms > 0 waits at most ms, ms < 0 waits until a report or removal.
    rv_webhid_read_timeout : async function(id, buf, len, ms) {
        var entry = rvWebhid.find(id);
        if (!entry) {
            rvWebhid.fail('rv_webhid_read_timeout', id, 'unknown id');
            return -1;
        }
        if (len < 1) {
            rvWebhid.fail('rv_webhid_read_timeout', id, 'len < 1');
            return -1;
        }
        var report = entry.queue.shift();
        if (!report && ms === 0) {
            return 0;
        }
        if (!report) {
            report = await rvWebhid.wait(entry, ms);
        }
        if (report === rvWebhid.TIMED_OUT) {
            return 0;
        }
        if (report === null) {
            rvWebhid.fail('rv_webhid_read_timeout', id, 'device removed while waiting');
            return -1;
        }
        var data = report.reportId !== 0 ? rvWebhid.withId(report.reportId, report.bytes) : report.bytes;
        // copyOut reads HEAPU8 after the await: memory may have grown while suspended.
        return rvWebhid.copyOut(buf, len, data);
    },

    rv_webhid_write__deps : [ '$rvWebhid' ],
    rv_webhid_write : function(id, buf, len) {
        var entry = rvWebhid.find(id);
        if (!entry) {
            rvWebhid.fail('rv_webhid_write', id, 'unknown id');
            return -1;
        }
        if (len < 1) {
            rvWebhid.fail('rv_webhid_write', id, 'len < 1');
            return -1;
        }
        if (rvWebhid.stalled(entry)) {
            rvWebhid.failStalled('rv_webhid_write', entry);
            return -1;
        }
        // Copied now: the C buffer may change before the queued send runs.
        var bytes = HEAPU8.slice(buf, buf + len);
        var reportId = bytes[0];
        var data = bytes.subarray(1);
        rvWebhid.enqueue(entry, 'rv_webhid_write', function() { return entry.device.sendReport(reportId, data); });
        return len;
    },

    rv_webhid_get_feature__deps : [ '$rvWebhid' ],
    rv_webhid_get_feature__async : true,
    rv_webhid_get_feature : async function(id, buf, len) {
        var entry = rvWebhid.find(id);
        if (!entry) {
            rvWebhid.fail('rv_webhid_get_feature', id, 'unknown id');
            return -1;
        }
        if (len < 1) {
            rvWebhid.fail('rv_webhid_get_feature', id, 'len < 1');
            return -1;
        }
        if (rvWebhid.stalled(entry)) {
            rvWebhid.failStalled('rv_webhid_get_feature', entry);
            return -1;
        }
        // The report id is read before the await.
        var reportId = HEAPU8[buf];
        var started = performance.now();
        var view;
        var gaveUp = false;
        try {
            // Sends queued before this call land first; the call as a whole gives up after STALL_MS.
            view = await rvWebhid.timed((async function() {
                                            await entry.sendChain;
                                            // Gave up while waiting for the chain: the read is not sent.
                                            if (gaveUp) {
                                                return null;
                                            }
                                            return entry.device.receiveFeatureReport(reportId);
                                        })(),
                                        function() { gaveUp = true; });
        } catch (err) {
            rvWebhid.fail('rv_webhid_get_feature', id, 'receiveFeatureReport failed: ' + err);
            return -1;
        }
        if (view === rvWebhid.TIMED_OUT) {
            rvWebhid.fail('rv_webhid_get_feature', id,
                          'no answer after ' + Math.round(performance.now() - started) + ' ms, limit ' + rvWebhid.STALL_MS +
                              ' ms; a late answer is dropped');
            return -1;
        }
        var data = rvWebhid.withId(reportId, new Uint8Array(view.buffer, view.byteOffset, view.byteLength));
        // copyOut reads HEAPU8 after the await: memory may have grown while suspended.
        return rvWebhid.copyOut(buf, len, data);
    },

    rv_webhid_send_feature__deps : [ '$rvWebhid' ],
    rv_webhid_send_feature__async : true,
    rv_webhid_send_feature : async function(id, buf, len) {
        var entry = rvWebhid.find(id);
        if (!entry) {
            rvWebhid.fail('rv_webhid_send_feature', id, 'unknown id');
            return -1;
        }
        if (len < 1) {
            rvWebhid.fail('rv_webhid_send_feature', id, 'len < 1');
            return -1;
        }
        if (rvWebhid.stalled(entry)) {
            rvWebhid.failStalled('rv_webhid_send_feature', entry);
            return -1;
        }
        var bytes = HEAPU8.slice(buf, buf + len);
        var started = performance.now();
        // Same chain as write: the device sees the sends in call order.
        var ok = await rvWebhid.timed(
            rvWebhid.enqueue(entry, 'rv_webhid_send_feature',
                             function() { return entry.device.sendFeatureReport(bytes[0], bytes.subarray(1)); }));
        if (ok === rvWebhid.TIMED_OUT) {
            // A WebHID send cannot be cancelled: it stays unsettled in the chain, so the stall rule
            // refuses later sends until it settles.
            rvWebhid.fail('rv_webhid_send_feature', id,
                          'no answer after ' + Math.round(performance.now() - started) + ' ms, limit ' + rvWebhid.STALL_MS +
                              ' ms; the send stays queued');
            return -1;
        }
        return ok ? len : -1;
    },

    // Replaces the system fd_sync from libwasi.js. With JSPI it becomes async and syncs the whole IDBFS mount.
    // Each fsync would then sync the whole IDBFS mount to IndexedDB, so this override keeps fsync sync;
    // the memory card reaches IndexedDB through Module.rvSaveFlush.
    // Errors return errno as system wasi calls do; the override replaces the system fd_sync whole.
    fd_sync__deps : [ '$SYSCALLS' ],
    fd_sync__async : false,
    fd_sync : function(fd) {
        try {
            var stream = SYSCALLS.getStreamFromFD(fd);
            return stream.stream_ops?.fsync?.(stream);
        } catch (e) {
            if (typeof FS == 'undefined' || e.name !== 'ErrnoError') {
                throw e;
            }
            return e.errno;
        }
    },
});
