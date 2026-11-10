// On-screen gamepad drawing for the web shell, on a 2D canvas in the olive theme.
// Contract: a plain script for --pre-js, no modules, no DOM access at load.
var rvTouchDraw = (function() {
    // Bitmap font, copied byte for byte from rv_font_bits in pdk/lib/include/pdklib/rv_font/rv_font_data.hpp.
    // One string per glyph: 8 row bytes, top row first, high bit leftmost. Codes 32..126, then notdef.
    var GLYPH_HEX = [
        '0000000000000000', '2020202020002000', '5050000000000000', '5050F850F8505000', '2078A07028F02000',
        'C8D0102040589800', '60909060A8906800', '2020000000000000', '1020404040201000', '4020101010204000',
        '0020A870A8200000', '002020F820200000', '0000000030204000', '000000F800000000', '0000000000606000',
        '0810102040408000', '708898A8C8887000', '2060202020207000', '708808102040F800', 'F810201008887000',
        '10305090F8101000', 'F880F00808887000', '304080F088887000', 'F808102040404000', '7088887088887000',
        '7088887808106000', '0060600060600000', '0060600060204000', '1020408040201000', '0000F800F8000000',
        '4020100810204000', '7088081020002000', '7088B8A8B0807000', '20508888F8888800', 'F08888F08888F000',
        '7088808080887000', 'E09088888890E000', 'F88080F08080F800', 'F88080F080808000', '708880B888887000',
        '888888F888888800', '7020202020207000', '3810101010906000', '8890A0C0A0908800', '808080808080F800',
        '88D8A8A888888800', '8888C8A898888800', '7088888888887000', 'F08888F080808000', '70888888A8906800',
        'F08888F0A0908800', '7088807008887000', 'F820202020202000', '8888888888887000', '8888888888502000',
        '888888A8A8D88800', '8888502050888800', '8888502020202000', 'F80810204080F800', '7040404040407000',
        '8040402010100800', '7010101010107000', '2050880000000000', '000000000000F800', '4020000000000000',
        '0000700878887800', '8080F0888888F000', '0000708080887000', '0808788888887800', '00007088F8807000',
        '304840E040404000', '0000788878087000', '8080F08888888800', '2000602020207000', '1000301010906000',
        '808090A0C0A09000', '6020202020283000', '0000D0A8A8A8A800', '0000F08888888800', '0000708888887000',
        '0000F088F0808000', '0000788878080800', '0000B0C080808000', '000078807008F000', '4040F04040483000',
        '0000888888986800', '0000888888502000', '00008888A8A85000', '0000885020508800', '0000888878087000',
        '0000F8102040F800', '1820206020201800', '2020202020202000', 'C02020302020C000', '000048A890000000',
        'FFFFFFFFFFFFFFFF'
    ];
    var FONT = [];
    GLYPH_HEX.forEach(function(hex) {
        for (var i = 0; i < hex.length; i += 2) {
            FONT.push(parseInt(hex.substr(i, 2), 16));
        }
    });
    // Font geometry (rv_font_cell_width, rv_font_ink_width, rv_font_ink_height), codes 32..126, notdef index.
    var CELL = 8, INK_W = 5, INK_H = 7;
    var FIRST_CODE = 32, LAST_CODE = 126, NOTDEF = 95;

    // Colours from rv_theme_olive.hpp, 0xRRGGBB as CSS hex.
    var C_BUTTON = '#4e5744';   // rv_theme_button: plate face
    var C_INSET = '#3e4637';    // rv_theme_inset: field face
    var C_DARK = '#282e20';     // rv_theme_dark: well face
    var C_HI = '#7e8776';       // rv_theme_bevel_hi: light edge
    var C_LO = '#32392c';       // rv_theme_bevel_lo: shadow edge
    var C_TEXT = '#d8ded3';     // rv_theme_text: label
    var C_BRIGHT = '#f1f2f0';   // rv_theme_text_bright: pressed label
    var C_DISABLED = '#758666'; // rv_theme_text_disabled: field captions

    // Faces are a thin wash so the game shows through; edges and labels stay at half strength.
    var FACE_ALPHA = 0.10, LINE_ALPHA = 0.50;
    // Bevel width in pad pixels (rv_theme_bevel_px); a pressed plate sinks, its label moves PRESS_SHIFT.
    var BEVEL_PX = 2, PRESS_SHIFT = 1;
    // Local values of the screen-pad look, not values from C++: 2x labels on plates this big that fit.
    var BIG_PLATE_PX = 44, BIG_SCALE = 2;
    // BUTTON_PAD_X is rv_theme_button_pad_x (C++); the design-only CAP_* values are not from C++.
    var BUTTON_PAD_X = 8;
    // Screen-pad design, not C++: stick cap side as a share of the field's shorter side; CAP_GAP_PX from the edge.
    var CAP_SHARE = 0.4, CAP_GAP_PX = 3;
    // Screen-pad design, not C++: caption and finger inset from a field edge.
    var FIELD_INSET_PX = 5;
    // Screen-pad design, not C++: dots one per 1/DOT_DIVISOR of the longer side; finger is a FINGER_PX square.
    var DOT_DIVISOR = 4, FINGER_PX = 6;
    // Caption width in pad pixels; trailing spacing not counted. notdef draws 8 wide but counts 5: understated by 3 px.
    function ink(text, scale) { return text.length ? ((text.length - 1) * CELL + INK_W) * scale : 0; }

    function paint(ctx, x, y, w, h, col, alpha) {
        ctx.fillStyle = col;
        ctx.globalAlpha = alpha;
        ctx.fillRect(x, y, w, h);
    }
    // All 8 columns of a set bit are drawn: notdef is a solid 8x8 cell in rv_font_data.hpp, not 5 wide.
    function caption(ctx, x, y, text, col, scale) {
        for (var i = 0; i < text.length; i++) {
            var code = text.charCodeAt(i);
            var g = (code >= FIRST_CODE && code <= LAST_CODE) ? code - FIRST_CODE : NOTDEF;
            for (var row = 0; row < CELL; row++) {
                var bits = FONT[g * CELL + row];
                for (var b = 0; b < CELL; b++) {
                    if (bits & (0x80 >> b)) {
                        paint(ctx, x + (i * CELL + b) * scale, y + row * scale, scale, scale, col, LINE_ALPHA);
                    }
                }
            }
        }
    }

    // Lit on the top and left, shade on the bottom and right, BEVEL_PX pixels deep.
    function bevel(ctx, x, y, w, h, lit, shade) {
        for (var i = 0; i < BEVEL_PX; i++) {
            paint(ctx, x + i, y + i, w - 2 * i, 1, lit, LINE_ALPHA);
            paint(ctx, x + i, y + i, 1, h - 2 * i, lit, LINE_ALPHA);
            paint(ctx, x + i, y + h - 1 - i, w - 2 * i, 1, shade, LINE_ALPHA);
            paint(ctx, x + w - 1 - i, y + i, 1, h - 2 * i, shade, LINE_ALPHA);
        }
    }
    function face(ctx, c, col, lit, shade) {
        paint(ctx, c.x, c.y, c.w, c.h, col, FACE_ALPHA);
        bevel(ctx, c.x, c.y, c.w, c.h, lit, shade);
    }
    function drawBtn(ctx, c, label, pressed) {
        face(ctx, c, C_BUTTON, pressed ? C_LO : C_HI, pressed ? C_HI : C_LO);
        var fits = ink(label, BIG_SCALE) + 2 * BUTTON_PAD_X <= c.w;
        var scale = (c.w >= BIG_PLATE_PX && c.h >= BIG_PLATE_PX && fits) ? BIG_SCALE : 1;
        var shift = pressed ? PRESS_SHIFT : 0;
        var x = c.x + Math.floor((c.w - ink(label, scale)) / 2) + shift;
        var y = c.y + Math.floor((c.h - INK_H * scale) / 2) + shift;
        caption(ctx, x, y, label, pressed ? C_BRIGHT : C_TEXT, scale);
    }

    // A null at centres the cap; otherwise the cap moves to at, keeping CAP_GAP_PX from the field edge.
    function drawStick(ctx, c, label, pressed, at) {
        face(ctx, c, C_INSET, C_LO, C_HI);
        caption(ctx, c.x + FIELD_INSET_PX, c.y + FIELD_INSET_PX, label, C_DISABLED, 1);
        var a = at || {x : 0, y : 0};
        var side = Math.round(Math.min(c.w, c.h) * CAP_SHARE);
        var cx = c.x + c.w / 2 + a.x * (c.w / 2 - side / 2 - CAP_GAP_PX);
        var cy = c.y + c.h / 2 - a.y * (c.h / 2 - side / 2 - CAP_GAP_PX);
        face(ctx, {x : Math.round(cx - side / 2), y : Math.round(cy - side / 2), w : side, h : side}, C_BUTTON, C_HI, C_LO);
    }

    function drawWell(ctx, c) { face(ctx, c, C_DARK, C_LO, C_HI); }

    function drawPad(ctx, c, label, pressed, at) {
        face(ctx, c, C_INSET, C_LO, C_HI);
        caption(ctx, c.x + FIELD_INSET_PX, c.y + FIELD_INSET_PX, label, C_DISABLED, 1);
        // Dots skip the two steps nearest each corner.
        var dots = Math.floor(Math.max(c.w, c.h) / DOT_DIVISOR);
        for (var d = 2; d < dots - 1; d++) {
            var dx = Math.round(d * (c.w - 1) / dots);
            var dy = Math.round(d * (c.h - 1) / dots);
            paint(ctx, c.x + dx, c.y + dy, 1, 1, C_LO, LINE_ALPHA);
            paint(ctx, c.x + c.w - 1 - dx, c.y + dy, 1, 1, C_LO, LINE_ALPHA);
        }
        if (!at) {
            return;
        }
        var fx = Math.round(c.x + c.w / 2 + at.x * (c.w / 2 - FIELD_INSET_PX));
        var fy = Math.round(c.y + c.h / 2 - at.y * (c.h / 2 - FIELD_INSET_PX));
        paint(ctx, fx - FINGER_PX / 2, fy - FINGER_PX / 2, FINGER_PX, FINGER_PX, C_BRIGHT, LINE_ALPHA);
    }
    var DRAW = {btn : drawBtn, well : drawWell, stick : drawStick, pad : drawPad};
    // Draws element c = {kind, x, y, w, h} in pad pixels; the caller's context transform sets the pad-pixel scale.
    // Stick and pad take at = {x, y} in -1..1, +y up. Canvas alpha is restored even if a draw throws.
    function control(ctx, c, text, pressed, at) {
        var draw = DRAW[c.kind];
        if (!draw) {
            console.warn('touchpad: unknown control kind ' + c.kind);
            return;
        }
        var keep = ctx.globalAlpha;
        try {
            draw(ctx, c, text || '', pressed, at);
        } finally {
            ctx.globalAlpha = keep;
        }
    }

    return {control : control, ink : ink};
})();
