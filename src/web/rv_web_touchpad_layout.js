// Touch gamepad layout: places the on-screen controls around the game frame on the pad surface.
// Plain script for --pre-js: no modules, no DOM. Coordinates are pad-surface pixels.
var rvTouchLayout = (function() {
    // Bit per group. The bits repeat enum class rv_touch_group (pdk/lib/include/pdklib/rv_manifest/rv_manifest.hpp)
    // in its enum order: sticks 0 ... gyro 8. Change them only together with that enum.
    var GROUP = {
        sticks : 1 << 0,
        trackpads : 1 << 1,
        dpad : 1 << 2,
        face : 1 << 3,
        bumpers : 1 << 4,
        triggers : 1 << 5,
        rear : 1 << 6,
        menu : 1 << 7,
        gyro : 1 << 8
    };
    // Margin from the surface edge, and gap between plates.
    var MARGIN = 3, GAP = 3;
    // Top row height as a share of the surface height (landscape); strip row height as a share of the width (portrait).
    var TOP_RATIO = 0.12, STRIP_RATIO = 0.11;
    // Portrait rows start no lower than this share of the height, so cells keep room on tall screens.
    // The rows may overlap the frame.
    var PORTRAIT_TOP_SHARE = 0.45;
    // MIN_PLATE floors the top row height, the strip row height and the rear plate width. MIN_CELL floors a cell.
    // A cross plate is a third of the cell less the 4 px border, as cross() computes it. On a nearly square
    // portrait (height a little over width) cells get little height, and a cross plate can fall below MIN_CELL:
    // down to about 5 px at width 240.
    var MIN_PLATE = 16, MIN_CELL = 12;
    // Edge column width as a share of the surface width, used when no cell group is shown.
    var EDGE_FALLBACK = 0.2;
    // VIEW and MENU. The label VIEW at scale 1 is 4 glyphs of 8 px less the 3 px trailing gap: 29 px.
    // Plus 6 px padding per side gives the plate width. The gap keeps the plates off the centre line.
    var VIEW_LABEL_W = 29, CENTER_PAD = 6;
    var CENTER_W = VIEW_LABEL_W + 2 * CENTER_PAD;
    var CENTER_H = 16, CENTER_GAP = 2;
    // A cross is a 3x3 grid inside its square, with a 2 px border on each side.
    var CROSS_BORDER = 4;
    // Cross plates: north, south, west, east. Stick and trackpad ids: left side first.
    var CROSS_IDS = {dpad : [ 'DN', 'DS', 'DW', 'DE' ], face : [ 'Y', 'A', 'X', 'B' ]};
    var SIDE_IDS = {stick : [ 'LS', 'RS' ], pad : [ 'LP', 'RP' ]};

    function plate(list, id, x, y, w, h) { list.push({id : id, kind : 'btn', x : x, y : y, w : w, h : h}); }

    // Four plates around an empty middle; the well is the whole square.
    function cross(list, x, y, w, h, ids) {
        var c = Math.floor((Math.min(w, h) - CROSS_BORDER) / 3);
        var ox = x + Math.floor((w - 3 * c) / 2);
        var oy = y + Math.floor((h - 3 * c) / 2);
        list.push({kind : 'well', x : x, y : y, w : w, h : h});
        plate(list, ids[0], ox + c, oy, c, c);
        plate(list, ids[1], ox + c, oy + 2 * c, c, c);
        plate(list, ids[2], ox, oy + c, c, c);
        plate(list, ids[3], ox + 2 * c, oy + c, c, c);
    }

    function cell(list, what, left, x, y, w, h) {
        if (CROSS_IDS[what]) {
            cross(list, x, y, w, h, CROSS_IDS[what]);
            return;
        }
        list.push({id : SIDE_IDS[what][left ? 0 : 1], kind : what, x : x, y : y, w : w, h : h});
    }

    // Cells of one column, top to bottom: cross (dpad left, face right), stick, trackpad.
    function cellKinds(left, groups) {
        var out = [];
        if (groups & (left ? GROUP.dpad : GROUP.face)) {
            out.push(left ? 'dpad' : 'face');
        }
        if (groups & GROUP.sticks) {
            out.push('stick');
        }
        if (groups & GROUP.trackpads) {
            out.push('pad');
        }
        return out;
    }

    // Cells share the height from y0 to the bottom margin. Width is the larger of the height and wantW (the free
    // field beside the frame), capped at maxW. Returns the width, 0 without cells.
    function column(list, left, y0, wp, hp, groups, wantW, maxW) {
        var kinds = cellKinds(left, groups);
        var n = kinds.length;
        if (!n) {
            return 0;
        }
        var h = Math.max(MIN_CELL, Math.floor((hp - MARGIN - y0 - GAP * (n - 1)) / n));
        var w = Math.max(MIN_CELL, Math.min(Math.max(h, wantW), maxW));
        var x = left ? MARGIN : wp - MARGIN - w;
        kinds.forEach(function(what, i) { cell(list, what, left, x, y0 + i * (h + GAP), w, h); });
        return w;
    }

    // Plates share the row width equally, so a removed group leaves the others wider.
    function strip(list, ids, y, wp, h) {
        var n = ids.length;
        if (!n) {
            return 0;
        }
        var w = Math.floor((wp - 2 * MARGIN - GAP * (n - 1)) / n);
        ids.forEach(function(id, i) { plate(list, id, MARGIN + i * (w + GAP), y, w, h); });
        return h + GAP;
    }

    // Landscape: edge columns with the top row and inner rear plates, cells below; VIEW and MENU at the top centre.
    function landscape(wp, hp, frame, groups, list) {
        var th = Math.max(MIN_PLATE, Math.round(hp * TOP_RATIO));
        var y0 = MARGIN + ((groups & (GROUP.bumpers | GROUP.triggers)) ? th + GAP : 0);
        var rw = (groups & GROUP.rear) ? Math.max(MIN_PLATE, th) : 0;
        var rearSpace = rw ? rw + GAP : 0;
        // Binds when one tall cell per side would be wider than half the surface: the sides stay off the centre line.
        var maxW = Math.floor(wp / 2) - MARGIN - rearSpace;
        [true, false].forEach(function(left) {
            var field = left ? frame.x : wp - frame.x - frame.w;
            var cw = column(list, left, y0, wp, hp, groups, field - MARGIN - rearSpace, maxW);
            var edgeW = cw || Math.round(wp * EDGE_FALLBACK);
            // Trigger outside, bumper inside; the right side mirrors it.
            var top = [];
            if (groups & GROUP.triggers) {
                top.push(left ? 'L2' : 'R2');
            }
            if (groups & GROUP.bumpers) {
                top.push(left ? 'L1' : 'R1');
            }
            var pw = top.length ? Math.floor((edgeW - GAP * (top.length - 1)) / top.length) : 0;
            top.forEach(function(id, i) {
                var off = i * (pw + GAP);
                plate(list, id, left ? MARGIN + off : wp - MARGIN - off - pw, MARGIN, pw, th);
            });
            if (groups & GROUP.rear) {
                var rx = left ? MARGIN + edgeW + GAP : wp - MARGIN - edgeW - GAP - rw;
                var rh = Math.floor((hp - MARGIN - y0 - GAP) / 2);
                plate(list, left ? 'L4' : 'R4', rx, y0, rw, rh);
                plate(list, left ? 'L5' : 'R5', rx, y0 + rh + GAP, rw, rh);
            }
        });
        var mid = Math.floor(wp / 2);
        if (groups & GROUP.menu) {
            plate(list, 'VIEW', mid - CENTER_GAP - CENTER_W, MARGIN, CENTER_W, CENTER_H);
            plate(list, 'MENU', mid + CENTER_GAP, MARGIN, CENTER_W, CENTER_H);
        }
    }

    // Portrait: frame at the top, two strip rows under it, then two half-width cell columns to the bottom.
    // The rows start at the frame bottom or PORTRAIT_TOP_SHARE of the height, whichever is higher.
    function portrait(wp, hp, frame, groups, list) {
        var rowH = Math.max(MIN_PLATE, Math.round(wp * STRIP_RATIO));
        var row = [];
        if (groups & GROUP.triggers) {
            row.push('L2');
        }
        if (groups & GROUP.bumpers) {
            row.push('L1');
        }
        if (groups & GROUP.menu) {
            row.push('VIEW', 'MENU');
        }
        if (groups & GROUP.bumpers) {
            row.push('R1');
        }
        if (groups & GROUP.triggers) {
            row.push('R2');
        }
        var rear = (groups & GROUP.rear) ? [ 'L4', 'L5', 'R5', 'R4' ] : [];
        var y = Math.min(frame.y + frame.h, Math.floor(hp * PORTRAIT_TOP_SHARE)) + GAP;
        y += strip(list, row, y, wp, rowH);
        y += strip(list, rear, y, wp, rowH);
        var half = Math.floor((wp - 2 * MARGIN - GAP) / 2);
        column(list, true, y, wp, hp, groups, half, half);
        column(list, false, y, wp, hp, groups, half, half);
    }

    return {
        GROUP : GROUP,
        // Limit: on a portrait surface narrower than 152 px, cells can run past the bottom edge.
        // The strip rows take a fixed share of the width, and MIN_CELL raises a cell above the room left.
        compute : function(wp, hp, frame, groups) {
            var list = [];
            (wp >= hp ? landscape : portrait)(wp, hp, frame, groups, list);
            return list;
        }
    };
})();
