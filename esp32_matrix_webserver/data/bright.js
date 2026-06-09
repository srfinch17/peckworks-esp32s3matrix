/* ============================================================
 * bright.js — shared per-app brightness widget (Roadmap S1)
 * ------------------------------------------------------------
 * One source of truth for the brightness slider, the power-safety
 * high-brightness lock, debounced POSTs to /api/brightness, and
 * localStorage persistence. The board has ONE physical brightness,
 * so every page that mounts this controls the SAME global value.
 *
 * Usage — explicit (when you want to place it / wire status):
 *     <div id="brightnessSlot"></div>
 *     <script src="bright.js"></script>
 *     <script>MatrixBright.mount('#brightnessSlot', { onStatus: setStatus });</script>
 *
 * Usage — auto (drop-in, no markup needed):
 *     <script src="bright.js" data-auto></script>
 *   Mounts into #brightnessSlot if present, else prepends itself to
 *   the page's main container. The widget carries its own status line.
 *
 * Served straight from LittleFS (onNotFound streams it as
 * application/javascript) — no firmware changes, no build step.
 * ============================================================ */
(function (global) {
  'use strict';

  var STORE_VAL  = 'matrix_brightness';   // last value, 0-255
  var STORE_LOCK = 'matrix_highbright';   // '1' when high-bright cap is unlocked
  var SAFE_CAP   = 100;                    // max allowed while locked (power safety)
  var MAX_VAL    = 255;
  var DEFAULT    = 10;
  var DEBOUNCE   = 250;                     // ms between slider move and POST

  var state = { val: DEFAULT, unlocked: false, timer: null, onStatus: null, els: {} };

  var CSS =
    '.mb-wrap{font-family:system-ui,-apple-system,sans-serif;color:#e0e0e0}' +
    '.mb-row{display:flex;align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:6px}' +
    '.mb-label{font-size:.85rem;color:#aaa}' +
    '.mb-val{display:inline-block;min-width:3ch;text-align:right;color:#e0e0e0}' +
    '.mb-slider{flex:1;min-width:120px;max-width:220px;accent-color:#00ff88;cursor:pointer}' +
    '.mb-track{position:relative;height:5px;border-radius:3px;margin:4px 0;' +
      'background:linear-gradient(to right,#22882a 0%,#22882a 35%,#e8b800 39%,#f97316 55%,#cc2200 100%)}' +
    '.mb-lock-line{position:absolute;left:39.2%;top:-3px;bottom:-3px;width:2px;background:rgba(255,255,255,.22);border-radius:1px}' +
    '.mb-labels{display:flex;justify-content:space-between;font-size:.62rem;color:#555;margin-bottom:10px}' +
    '.mb-safe{color:#22882a}.mb-hot{color:#f97316}' +
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
      '  <div class="mb-row"><span class="mb-label">Brightness: <strong class="mb-val">' + DEFAULT + '</strong></span>' +
      '    <input type="range" class="mb-slider" min="0" max="' + MAX_VAL + '" value="' + DEFAULT + '"></div>' +
      '  <div class="mb-track"><div class="mb-lock-line"></div></div>' +
      '  <div class="mb-labels"><span>0</span><span class="mb-safe">safe</span><span>100</span>' +
      '<span class="mb-hot">⚠ hot</span><span>255</span></div>' +
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

  // Announce the value immediately (un-debounced) so previews (ledsim.js) can
  // track the slider in real time, even though the board POST is debounced.
  function broadcast() {
    try {
      global.dispatchEvent(new CustomEvent('matrixbrightness', { detail: { level: state.val } }));
    } catch (e) { /* old browsers: previews just won't live-update */ }
  }

  // Push the current value to the board (debounced). report=true => show
  // success text; report=false => only surface failures (used on load/set).
  function commit(report) {
    state.els.val.textContent = state.val;
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

  function clampToLock(v) {
    return (!state.unlocked && v > SAFE_CAP) ? SAFE_CAP : v;
  }

  function onInput() {
    var v = clampToLock(+state.els.slider.value);
    state.els.slider.value = v;     // snap back if it overran the safe cap
    state.val = v;
    commit(true);
  }

  function onToggle() {
    state.unlocked = state.els.cb.checked;
    localStorage.setItem(STORE_LOCK, state.unlocked ? '1' : '0');
    if (!state.unlocked && state.val > SAFE_CAP) {   // re-locking above the cap snaps down
      state.val = SAFE_CAP;
      state.els.slider.value = SAFE_CAP;
      commit(true);
    }
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

    // Restore persisted state (shared across every page).
    var stored = localStorage.getItem(STORE_VAL);
    state.unlocked = localStorage.getItem(STORE_LOCK) === '1';
    state.val = clampToLock(stored !== null ? +stored : DEFAULT);
    state.els.slider.value = state.val;
    state.els.val.textContent = state.val;
    state.els.cb.checked = state.unlocked;

    state.els.slider.addEventListener('input', onInput);
    state.els.cb.addEventListener('change', onToggle);

    broadcast();  // let any preview render at the restored brightness on load

    // Sync the board to the UI on load (surface failures only).
    postLevel(state.val).then(function (ok) {
      if (!ok) status('Board unreachable — brightness not synced.', true);
    });
  }

  global.MatrixBright = {
    mount: mount,
    get: function () { return state.val; },
    set: function (v) {
      state.val = clampToLock(+v);
      if (state.els.slider) { state.els.slider.value = state.val; state.els.val.textContent = state.val; }
      commit(false);
    }
  };

  // ── Auto-mount (data-auto on the script tag) ──────────────────────────────
  var cs = document.currentScript;
  if (cs && cs.hasAttribute('data-auto')) {
    var run = function () {
      var slot = document.getElementById('brightnessSlot');
      if (!slot) {
        slot = document.createElement('div');
        slot.id = 'brightnessSlot';
        slot.style.margin = '16px 0';
        // Prefer placing the widget right under the page heading (and its
        // subtitle, if any); fall back to the top of the main container.
        var h1 = document.querySelector('h1');
        if (h1) {
          var after = h1;
          var sib = h1.nextElementSibling;
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
