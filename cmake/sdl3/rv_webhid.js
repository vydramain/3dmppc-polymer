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
        // Returned by timed() when the limit passes before the promise settles.
        TIMED_OUT : {},
        RUN_DEPENDENCY : 'rv_webhid',
        // HIDDevice -> its open attempt { promise }; a device in the table keeps its attempt.
        known : new Map(),
        devices : [],
        nextId : 1,
        changes : 0,
        find : function(id) { return rvWebhid.devices.find(function(entry) { return entry.id === id; }); },
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
                    var entry =
                        {id : rvWebhid.nextId++, device : device, queue : [], sendChain : Promise.resolve(), unsettled : []};
                    entry.onReport = function(ev) {
                        var bytes = new Uint8Array(ev.data.buffer, ev.data.byteOffset, ev.data.byteLength);
                        entry.queue.push({reportId : ev.reportId, bytes : bytes});
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
            device.removeEventListener('inputreport', rvWebhid.devices[index].onReport);
            rvWebhid.devices.splice(index, 1);
            rvWebhid.changes++;
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
        start : function() {
            if (typeof navigator === 'undefined' || !navigator.hid) {
                return;
            }
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
    rv_webhid_info : function(id, vendor, product, usagePage, usage) {
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
});
