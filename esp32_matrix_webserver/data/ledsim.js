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
 * fidelity matters (sketch, calibration).
 *
 * Served straight from LittleFS — no firmware change, no build step.
 * ============================================================ */
(function (global) {
  'use strict';

  var DEFAULT_BRI = 10;

  // Measured LED calibration (data/calibration.json), mirrored from the firmware
  // so color-fidelity previews match the corrected board. Identity until fetched.
  var CALIB = { floorR:1, floorG:1, floorB:1, gainR:1, gainG:1, gainB:1, gamma:1, on:true };
  fetch('/api/calibration').then(function (r) { return r.json(); }).then(function (j) {
    if (j.floors)        { CALIB.floorR = j.floors.r || 1; CALIB.floorG = j.floors.g || 1; CALIB.floorB = j.floors.b || 1; }
    if (j.white_balance) { CALIB.gainR = j.white_balance.r || 1; CALIB.gainG = j.white_balance.g || 1; CALIB.gainB = j.white_balance.b || 1; }
    if (j.gamma)         { CALIB.gamma = j.gamma; }
  }).catch(function () { /* identity fallback already set */ });
  // Honor the board's correction toggle so previews match when it's off.
  fetch('/api/settings').then(function (r) { return r.json(); }).then(function (s) {
    if (s && typeof s.calibration_correction === 'boolean') CALIB.on = s.calibration_correction;
  }).catch(function () {});

  // Value-domain correction (floor-lift -> white-balance -> gamma), matching the
  // firmware applyCalibration(). Applied BEFORE the brightness-scaling effective().
  function liftFloor(c, floor) { return (c > 0 && c < floor) ? floor : c; }
  function correctChannel(c, floor, gain) {
    if (!CALIB.on) return c;
    c = liftFloor(c, floor);
    c = c * gain;
    return Math.round(255 * Math.pow(c / 255, CALIB.gamma));
  }

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
    var r = displayGamma(effective(correctChannel(rgb[0], CALIB.floorR, CALIB.gainR), bri));
    var g = displayGamma(effective(correctChannel(rgb[1], CALIB.floorG, CALIB.gainG), bri));
    var b = displayGamma(effective(correctChannel(rgb[2], CALIB.floorB, CALIB.gainB), bri));
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
