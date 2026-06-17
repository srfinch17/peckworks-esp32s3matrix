/* ============================================================
 * ledsim.js — brightness → on-screen appearance model (Roadmap S4)
 * ------------------------------------------------------------
 * One source of truth for "what does the LED actually show?" — the
 * FastLED nscale8x3 dimming, the visibility threshold, and the gamma
 * correction needed so a web canvas pixel LOOKS like the real LED.
 * Reference + threshold table: docs/LED_BRIGHTNESS.md.
 *
 * Make a preview accurate in one line:
 *     LedSim.onChange(render);          // render() re-runs on brightness change
 *     // inside render(): ctx.fillStyle = LedSim.previewColor(hex, LedSim.bri());
 *
 * NOTE: accurate-dim preview is OPT-IN. Animation previews intentionally
 * render at full brightness (see the spec) — only call this where color
 * fidelity matters (emoji, sketch, calibration).
 *
 * Served straight from LittleFS — no firmware change, no build step.
 * ============================================================ */
(function (global) {
  'use strict';

  var DEFAULT_BRI = 10;

  // FastLED nscale8x3: an LED sub-pixel is OFF when this returns 0.
  function effective(channel, bri) {
    return (channel * (bri + 1)) >> 8;
  }

  // Smallest channel value that survives at this brightness; below it = dark.
  function minVisibleChannel(bri) {
    return Math.ceil(256 / (bri + 1));
  }

  // LED output is linear; screens are gamma ~2.2. Map effective -> display.
  function displayGamma(v) {
    return v === 0 ? 0 : Math.round(255 * Math.pow(v / 255, 1 / 2.2));
  }

  function parseColor(color) {
    if (Array.isArray(color)) return color;
    var h = String(color).replace('#', '');
    if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
    var n = parseInt(h, 16);
    return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
  }

  // CSS color a screen should paint to mimic the board at `bri`.
  function previewColor(color, bri) {
    var rgb = parseColor(color);
    var r = displayGamma(effective(rgb[0], bri));
    var g = displayGamma(effective(rgb[1], bri));
    var b = displayGamma(effective(rgb[2], bri));
    return 'rgb(' + r + ',' + g + ',' + b + ')';
  }

  // Current global brightness: prefer the shared widget, then localStorage.
  function bri() {
    if (global.MatrixBright && typeof global.MatrixBright.get === 'function') {
      return global.MatrixBright.get();
    }
    var stored = null;
    try { stored = localStorage.getItem('matrix_brightness'); } catch (e) {}
    return stored !== null ? +stored : DEFAULT_BRI;
  }

  // Call cb(bri) now and whenever brightness changes. Returns an unsubscribe fn.
  function onChange(cb) {
    var handler = function (e) {
      cb(e && e.detail && typeof e.detail.level === 'number' ? e.detail.level : bri());
    };
    global.addEventListener('matrixbrightness', handler);
    cb(bri()); // fire once with the current value
    return function () { global.removeEventListener('matrixbrightness', handler); };
  }

  global.LedSim = {
    effective: effective,
    minVisibleChannel: minVisibleChannel,
    displayGamma: displayGamma,
    previewColor: previewColor,
    bri: bri,
    onChange: onChange
  };
})(window);
