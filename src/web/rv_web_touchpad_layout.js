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
    // MIN_PLATE floors the top row height, the strip row height and the rear plate width. MIN_CELL floors a cell's width
    // and height. A cross plate is a third of the cell less the 4 px border, as cross() computes it. On a nearly square
    // portrait (height a little over width) cells get little height, and a cross plate can fall below MIN_CELL: down to
    // about 5 px at width 240.
    var MIN_PLATE = 16, MIN_CELL = 12;
    // A cross is a 3x3 grid inside its square, with a 2 px border on each side.
    var CROSS_BORDER = 4;
    // Cross plates: north, south, west, east. Stick and trackpad ids: left side first.
    var CROSS_IDS = {dpad : [ 'DN', 'DS', 'DW', 'DE' ], face : [ 'Y', 'A', 'X', 'B' ]};
    var SIDE_IDS = {stick : [ 'LS', 'RS' ], pad : [ 'LP', 'RP' ]};
    var SIDE_GROUP = {stick : GROUP.sticks, pad : GROUP.trackpads};
    // Cell slots of a column, top to bottom. A slot keeps its place when its group is hidden.
    var SLOTS = {left : [ 'dpad', 'stick', 'pad' ], right : [ 'face', 'stick', 'pad' ]};
    // Portrait strip rows as [id, group], left to right. A plate keeps its slot when its group is hidden.
    var PORTRAIT_ROW = [
        [ 'L2', GROUP.triggers ], [ 'L1', GROUP.bumpers ], [ 'VIEW', GROUP.menu ], [ 'MENU', GROUP.menu ],
        [ 'R1', GROUP.bumpers ], [ 'R2', GROUP.triggers ]
    ];
    var PORTRAIT_REAR = [ [ 'L4', GROUP.rear ], [ 'L5', GROUP.rear ], [ 'R5', GROUP.rear ], [ 'R4', GROUP.rear ] ];

    function plate(list, group, id, x, y, w, h) {
        list.push({id : id, kind : 'btn', group : group, x : x, y : y, w : w, h : h});
    }

    // Four plates around an empty middle; the well is the whole square.
    function cross(list, group, x, y, w, h, ids) {
        var c = Math.floor((Math.min(w, h) - CROSS_BORDER) / 3);
        var ox = x + Math.floor((w - 3 * c) / 2);
        var oy = y + Math.floor((h - 3 * c) / 2);
        list.push({kind : 'well', group : group, x : x, y : y, w : w, h : h});
        plate(list, group, ids[0], ox + c, oy, c, c);
        plate(list, group, ids[1], ox + c, oy + 2 * c, c, c);
        plate(list, group, ids[2], ox, oy + c, c, c);
        plate(list, group, ids[3], ox + 2 * c, oy + c, c, c);
    }

    // One slot of a column: a cross for dpad and face, otherwise a stick or trackpad plate.
    function cell(list, what, left, x, y, w, h) {
        if (CROSS_IDS[what]) {
            cross(list, GROUP[what], x, y, w, h, CROSS_IDS[what]);
            return;
        }
        list.push({id : SIDE_IDS[what][left ? 0 : 1], kind : what, group : SIDE_GROUP[what], x : x, y : y, w : w, h : h});
    }

    // Cells of one column, top to bottom: width is the larger of the height and wantW (the free field beside the frame),
    // capped at maxW. MIN_CELL floors the width after maxW, so it wins over maxW. Returns the width.
    function column(list, left, y0, wp, hp, wantW, maxW) {
        var slots = left ? SLOTS.left : SLOTS.right;
        var n = slots.length;
        var h = Math.max(MIN_CELL, Math.floor((hp - MARGIN - y0 - GAP * (n - 1)) / n));
        var w = Math.max(MIN_CELL, Math.min(Math.max(h, wantW), maxW));
        var x = left ? MARGIN : wp - MARGIN - w;
        slots.forEach(function(what, i) { cell(list, what, left, x, y0 + i * (h + GAP), w, h); });
        return w;
    }

    // Plates share the row width equally; a hidden plate keeps its slot, so the others do not move.
    function strip(list, row, y, wp, h) {
        var n = row.length;
        var w = Math.floor((wp - 2 * MARGIN - GAP * (n - 1)) / n);
        row.forEach(function(p, i) { plate(list, p[1], p[0], MARGIN + i * (w + GAP), y, w, h); });
        return h + GAP;
    }

    // Landscape: edge columns with the top row and rear plates, cells below; VIEW over L4 and MENU over R4.
    function landscape(wp, hp, frame, list) {
        var th = Math.max(MIN_PLATE, Math.round(hp * TOP_RATIO));
        var y0 = MARGIN + th + GAP;
        var rw = Math.max(MIN_PLATE, th);
        var rearSpace = rw + GAP;
        // Binds when one tall cell per side would be wider than half the surface: the sides stay off the centre line.
        var maxW = Math.floor(wp / 2) - MARGIN - rearSpace;
        [true, false].forEach(function(left) {
            var field = left ? frame.x : wp - frame.x - frame.w;
            var edgeW = column(list, left, y0, wp, hp, field - MARGIN - rearSpace, maxW);
            var pw = Math.floor((edgeW - GAP) / 2);
            // Trigger outside, bumper inside; the right side mirrors it.
            var outer = left ? MARGIN : wp - MARGIN - pw;
            var inner = left ? MARGIN + pw + GAP : wp - MARGIN - 2 * pw - GAP;
            plate(list, GROUP.triggers, left ? 'L2' : 'R2', outer, MARGIN, pw, th);
            plate(list, GROUP.bumpers, left ? 'L1' : 'R1', inner, MARGIN, pw, th);
            var rx = left ? MARGIN + edgeW + GAP : wp - MARGIN - edgeW - GAP - rw;
            var rh = Math.floor((hp - MARGIN - y0 - GAP) / 2);
            plate(list, GROUP.rear, left ? 'L4' : 'R4', rx, y0, rw, rh);
            plate(list, GROUP.rear, left ? 'L5' : 'R5', rx, y0 + rh + GAP, rw, rh);
            // The VIEW label is 29 px wide (4 glyphs of 8 px less the 3 px gap); the plate is rw = max(16, th) wide.
            // Below a surface height of 238 px (th < 29) the label is wider than the plate. drawBtn in
            // src/web/rv_web_touchpad_draw.js centres it with no clip, so it spills past both plate edges. Measured: at
            // H 187 the label touches the L1/R1 plates, and at H 180-186 it overlaps them by up to 1 px.
            plate(list, GROUP.menu, left ? 'VIEW' : 'MENU', rx, MARGIN, rw, th);
        });
    }

    // Portrait: frame at the top, two strip rows under it, then two half-width cell columns to the bottom.
    // The rows start at the frame bottom or PORTRAIT_TOP_SHARE of the height, whichever is higher.
    function portrait(wp, hp, frame, list) {
        var rowH = Math.max(MIN_PLATE, Math.round(wp * STRIP_RATIO));
        var y = Math.min(frame.y + frame.h, Math.floor(hp * PORTRAIT_TOP_SHARE)) + GAP;
        y += strip(list, PORTRAIT_ROW, y, wp, rowH);
        y += strip(list, PORTRAIT_REAR, y, wp, rowH);
        var half = Math.floor((wp - 2 * MARGIN - GAP) / 2);
        column(list, true, y, wp, hp, half, half);
        column(list, false, y, wp, hp, half, half);
    }

    return {
        GROUP : GROUP,
        // Limit: on a portrait surface narrower than 152 px, cells can run past the bottom edge.
        // The strip rows take a fixed share of the width, and MIN_CELL raises a cell above the room left.
        // Always lays out all groups, then keeps the shown ones: a hidden group leaves its place empty.
        compute : function(wp, hp, frame, groups) {
            var list = [];
            (wp >= hp ? landscape : portrait)(wp, hp, frame, list);
            return list.filter(function(e) { return e.group & groups; });
        }
    };
})();
