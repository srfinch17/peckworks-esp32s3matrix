/* ============================================================
 * bright.js — shared per-app brightness widget (Roadmap S1)
 * ------------------------------------------------------------
 * One source of truth for the brightness control: a non-linear
 * stepped slider (lots of resolution at the low end where most use
 * lives, compressed up top), the power-safety high-brightness lock,
 * debounced POSTs to /api/brightness, and localStorage persistence.
 * The board has ONE physical brightness, so every page that mounts
 * this controls the SAME global value.
 *
 * Usage — explicit:  MatrixBright.mount('#slot', { onStatus: setStatus });
 * Usage — auto:      <script src="bright.js" data-auto></script>
 *   Auto mounts inside the page's first .panel (so it matches the app
 *   controls' width), else under the heading, else the main container.
 *
 * Served straight from LittleFS — no firmware change, no build step.
 * ============================================================ */
(function (global) {
  'use strict';

  var STORE_VAL  = 'matrix_brightness';   // last value, 0-255
  var STORE_LOCK = 'matrix_highbright';    // '1' when high-bright cap is unlocked

  // Non-linear stops. The slider moves in EQUAL steps between these values
  // (hard stops), so the low end — where ~everyone lives — gets the most travel,
  // and the high end is squeezed to the right.
  var STOPS = [0,1,2,3,4,5,6,7,8,9,10,12,14,16,18,20,25,30,40,50,65,85,100,130,160,200,255];
  var MAX_IDX  = STOPS.length - 1;
  var SAFE_IDX = STOPS.indexOf(100);       // highest stop allowed while locked
  var DEBOUNCE = 250;

  var state = { idx: 0, val: STOPS[0], unlocked: false, timer: null, onStatus: null, els: {} };

  var CSS =
    '.mb-wrap{font-family:system-ui,-apple-system,sans-serif;color:#e0e0e0}' +
    '.mb-row{display:flex;align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:6px}' +
    '.mb-label{font-size:.85rem;color:#aaa}' +
    '.mb-val{display:inline-block;min-width:3ch;text-align:right;color:#e0e0e0}' +
    '.mb-slider{flex:1;min-width:120px;accent-color:#00ff88;cursor:pointer}' +
    '.mb-track{position:relative;height:5px;border-radius:3px;margin:4px 0;background:#222}' +
    '.mb-lock-line{position:absolute;top:-3px;bottom:-3px;width:2px;background:rgba(255,255,255,.35);border-radius:1px}' +
    '.mb-caption{font-size:.62rem;color:#555;margin-bottom:10px}' +
    '.mb-warn{display:flex;align-items:flex-start;gap:8px;padding:8px 10px;border:1px solid #3a1a00;border-radius:7px;background:#130900}' +
    '.mb-warn input[type=checkbox]{accent-color:#f97316;width:14px;height:14px;margin-top:2px;flex-shrink:0;cursor:pointer}' +
    '.mb-warntext{font-size:.75rem;color:#c05000;line-height:1.4;cursor:pointer}' +
    '.mb-warntext strong{color:#f97316}' +
    '.mb-status{font-size:.72rem;min-height:1em;margin-top:6px;color:#00cc66}' +
    '.mb-status.mb-err{color:#ff5555}';

  function injectStyleOnce() {
    if (document.getElementById('mb-style')) return;
    var s = document.createElement('style');
    s.id = 'mb-style';
    s.textContent = CSS;
    document.head.appendChild(s);
  }

  function markup() {
    return (
      '<div class="mb-wrap">' +
      '  <div class="mb-row"><span class="mb-label">Brightness: <strong class="mb-val">' + STOPS[0] + '</strong></span>' +
      '    <input type="range" class="mb-slider" min="0" max="' + MAX_IDX + '" step="1" value="0"></div>' +
      '  <div class="mb-track"><div class="mb-lock-line"></div></div>' +
      '  <div class="mb-caption">Fine steps at low levels · above 100 needs unlock</div>' +
      '  <div class="mb-warn"><input type="checkbox" id="mbCb">' +
      '<label for="mbCb" class="mb-warntext"><strong>⚠ Heat warning:</strong> above 100 risks overheating. Check to unlock.</label></div>' +
      '  <div class="mb-status"></div>' +
      '</div>'
    );
  }

  function postLevel(level) {
    return fetch('/api/brightness', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ level: level })
    }).then(function (r) { return r.ok; }, function () { return false; });
  }

  function status(msg, isErr) {
    if (state.els.status) {
      state.els.status.textContent = msg || '';
      state.els.status.className = 'mb-status' + (isErr ? ' mb-err' : '');
    }
    if (state.onStatus) state.onStatus(msg, isErr);
  }

  // Announce immediately (un-debounced) so previews (ledsim.js) track the slider.
  function broadcast() {
    try { global.dispatchEvent(new CustomEvent('matrixbrightness', { detail: { level: state.val } })); }
    catch (e) { /* old browsers: previews just won't live-update */ }
  }

  // Nearest stop index for a stored 0-255 brightness value.
  function idxForValue(v) {
    var best = 0, bd = Infinity;
    for (var i = 0; i < STOPS.length; i++) {
      var d = Math.abs(STOPS[i] - v);
      if (d < bd) { bd = d; best = i; }
    }
    return best;
  }

  function clampIdx(i) {
    return (!state.unlocked && i > SAFE_IDX) ? SAFE_IDX : i;
  }

  // Apply a slider index: update UI, persist, broadcast, debounced POST.
  // report=true shows success text; false only surfaces failures (load/set).
  function applyIdx(i, report) {
    i = clampIdx(i);
    state.idx = i;
    state.val = STOPS[i];
    if (state.els.slider) state.els.slider.value = i;
    if (state.els.val) state.els.val.textContent = state.val;
    localStorage.setItem(STORE_VAL, String(state.val));
    broadcast();
    clearTimeout(state.timer);
    state.timer = setTimeout(function () {
      postLevel(state.val).then(function (ok) {
        if (report) status(ok ? 'Brightness updated.' : 'Error updating brightness.', !ok);
        else if (!ok) status('Board unreachable — brightness not synced.', true);
      });
    }, DEBOUNCE);
  }

  function onInput() { applyIdx(+state.els.slider.value, true); }

  function onToggle() {
    state.unlocked = state.els.cb.checked;
    localStorage.setItem(STORE_LOCK, state.unlocked ? '1' : '0');
    if (!state.unlocked && state.idx > SAFE_IDX) applyIdx(SAFE_IDX, true);  // re-lock snaps down
  }

  function mount(target, opts) {
    opts = opts || {};
    var el = typeof target === 'string' ? document.querySelector(target) : target;
    if (!el) { console.warn('MatrixBright: mount target not found:', target); return; }

    state.onStatus = opts.onStatus || null;
    injectStyleOnce();
    el.innerHTML = markup();

    state.els.slider = el.querySelector('.mb-slider');
    state.els.val    = el.querySelector('.mb-val');
    state.els.cb     = el.querySelector('#mbCb');
    state.els.status = el.querySelector('.mb-status');
    state.els.track  = el.querySelector('.mb-track');

    // Place the safe→hot gradient + lock line where 100 actually falls on the
    // non-linear scale (most of the track is the "safe" low range).
    var lockPct = (SAFE_IDX / MAX_IDX) * 100;
    state.els.track.style.background =
      'linear-gradient(to right,#22882a 0%,#22882a ' + (lockPct - 4) + '%,#e8b800 ' +
      lockPct + '%,#f97316 ' + Math.min(lockPct + 6, 100) + '%,#cc2200 100%)';
    el.querySelector('.mb-lock-line').style.left = lockPct + '%';

    state.unlocked = localStorage.getItem(STORE_LOCK) === '1';
    state.els.cb.checked = state.unlocked;

    var stored = localStorage.getItem(STORE_VAL);
    applyIdx(clampIdx(idxForValue(stored !== null ? +stored : 10)), false);  // restore + sync board

    state.els.slider.addEventListener('input', onInput);
    state.els.cb.addEventListener('change', onToggle);
  }

  global.MatrixBright = {
    mount: mount,
    get: function () { return state.val; },
    set: function (v) { applyIdx(idxForValue(+v), false); }
  };

  // ── Auto-mount (data-auto on the script tag) ──────────────────────────────
  var cs = document.currentScript;
  if (cs && cs.hasAttribute('data-auto')) {
    var run = function () {
      var slot = document.getElementById('brightnessSlot');
      if (!slot) {
        slot = document.createElement('div');
        slot.id = 'brightnessSlot';
        slot.style.margin = '14px 0';
        // Prefer inside the first .panel so the widget matches the app controls'
        // width; else just under the heading; else the top of the container.
        var panel = document.querySelector('.panel');
        var h1 = document.querySelector('h1');
        if (panel) {
          panel.insertBefore(slot, panel.firstChild);
        } else if (h1) {
          var after = h1, sib = h1.nextElementSibling;
          if (sib && sib.classList && sib.classList.contains('subtitle')) after = sib;
          after.parentNode.insertBefore(slot, after.nextSibling);
        } else {
          var host = document.querySelector('.wrap, .container, main') || document.body;
          host.insertBefore(slot, host.firstChild);
        }
      }
      mount(slot, {});
    };
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', run);
    else run();
  }
})(window);
