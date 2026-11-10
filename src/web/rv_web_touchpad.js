// On-screen gamepad for the web shell: a pad canvas over the console, redrawn only on show, layout and press.
// Plain script for --pre-js, after rv_web_pre.js, rv_web_touchpad_draw.js and rv_web_touchpad_layout.js.
var rvTouchpad = (function() {
    // Console frame width in its own pixels: the reference for the pad scale, not the frame width on screen.
    var FRAME_W = 320;
    // Id of the pad canvas in rv_web_shell.html, placed over the console canvas.
    var CANVAS_ID = 'touchpad';
    // Phone or tablet: coarse main pointer without hover. Laptops with touch and desktops stay false.
    var PHONE_QUERY = '(pointer: coarse) and (hover: none)';
    // Dataset key prefix for the labels data-label-<id> on the pad canvas; see labelOf.
    var LABEL_PREFIX = 'label';
    Module.rvTouchpadWanted = matchMedia(PHONE_QUERY).matches;

    var shown = false, groups = 0, frame = null, outW = 0, outH = 0;
    // pressed: id -> bool. warned: ids already reported for a missing label. noCanvasWarned: one-time flag.
    var pressed = {}, warned = {}, noCanvasWarned = false;

    // A missing canvas means a broken page build (shell and this script ship together): one warning, then silent.
    function canvas() {
        var cv = document.getElementById(CANVAS_ID);
        if (!cv && !noCanvasWarned) {
            noCanvasWarned = true;
            console.warn('touchpad: canvas #' + CANVAS_ID + ' is missing from the page; the pad is off');
        }
        return cv;
    }

    // Label of element id from data-label-<id>. The browser builds the dataset key from the lowercase attribute:
    // hyphen plus letter gives a capital, so the key is prefix, first letter of id capital, the rest lower (VIEW is labelView).
    // A missing label draws empty and warns once per id.
    function labelOf(cv, id) {
        var text = cv.dataset[LABEL_PREFIX + id.charAt(0) + id.slice(1).toLowerCase()];
        if (text !== undefined) {
            return text;
        }
        if (!warned[id]) {
            warned[id] = true;
            console.warn('touchpad: no data-label-' + id.toLowerCase() + ' on the touchpad canvas');
        }
        return '';
    }

    // Pad pixels and scale. The pad surface is floor(buffer / s) pad pixels; its grid covers wp * s buffer pixels,
    // and the remainder (under s) lies at the right and bottom edge. The canvas keeps the full buffer size: CSS
    // stretches it over the console area, and it must match the frame.
    function controls() {
        if (!frame) {
            return {scale : 1, elements : []};
        }
        var s = Math.max(1, Math.round(frame.w / FRAME_W));
        var wp = Math.floor(outW / s), hp = Math.floor(outH / s);
        var f = {
            x : Math.round(frame.x / s),
            y : Math.round(frame.y / s),
            w : Math.round(frame.w / s),
            h : Math.round(frame.h / s)
        };
        return {scale : s, elements : rvTouchLayout.compute(wp, hp, f, groups)};
    }

    function redraw() {
        var cv = canvas();
        if (!cv) {
            return;
        }
        // Hidden until both show and layout have come: an empty canvas would catch the pointer.
        var ready = shown && frame !== null;
        cv.hidden = !ready;
        if (!ready) {
            return;
        }
        var model = controls();
        var ctx = cv.getContext('2d');
        // Identity transform clears the whole buffer; then the pad-pixel scale applies.
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.clearRect(0, 0, cv.width, cv.height);
        ctx.setTransform(model.scale, 0, 0, model.scale, 0, 0);
        ctx.imageSmoothingEnabled = false;
        model.elements.forEach(function(el) {
            // Well has no id and no label; null at centres the stick and pad caps.
            var text = el.id ? labelOf(cv, el.id) : '';
            rvTouchDraw.control(ctx, el, text, !!pressed[el.id], null);
        });
    }

    Module.rvTouchpadShow = function(g) {
        groups = g;
        shown = true;
        redraw();
    };

    Module.rvTouchpadLayout = function(x, y, w, h, ow, oh) {
        frame = {x : x, y : y, w : w, h : h};
        outW = ow;
        outH = oh;
        var cv = canvas();
        if (cv) {
            // The bitmap is the console buffer, out_w x out_h; assigning a size clears it.
            cv.width = ow;
            cv.height = oh;
        }
        redraw();
    };

    // Hides the pad at load; nothing is drawn until show and layout.
    redraw();

    return {
        controls : controls,
        press : function(id, down) {
            pressed[id] = !!down;
            redraw();
        }
    };
})();
