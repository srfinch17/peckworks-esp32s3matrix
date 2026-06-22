/* ============================================================
 * backnav.js — shared "back one level" pill (UI revamp Phase 1)
 * ------------------------------------------------------------
 * Drop-in, mirrors header.js:
 *   <script src="backnav.js" data-auto data-parent="/animations.html" data-label="Animations"></script>
 * Injects its own <style> and inserts a prominent back-pill right
 * AFTER the header card (so it reads as the page's back control,
 * not a tiny grey link). Defaults to Home. Idempotent.
 * Served from LittleFS — no firmware change, no build step.
 * ============================================================ */
(function (global) {
  'use strict';

  var CSS =
    '.bn-pill{display:inline-flex;align-items:center;gap:6px;background:#1c1c1c;' +
      'border:1px solid #333;border-radius:999px;padding:6px 14px;margin:0 0 16px;' +
      'color:#bdbdbd;text-decoration:none;font-size:.82rem;font-family:system-ui,-apple-system,sans-serif;' +
      'transition:border-color .15s,color .15s}' +
    '.bn-pill:hover{border-color:#555;color:#fff}' +
    '.bn-pill b{color:#e8e8e8;font-weight:600}';

  function injectStyleOnce() {
    if (document.getElementById('bn-style')) return;
    var s = document.createElement('style');
    s.id = 'bn-style';
    s.textContent = CSS;
    document.head.appendChild(s);
  }

  function markup(href, label) {
    return '<a class="bn-pill" href="' + href + '">← <b>' + label + '</b></a>';
  }

  function mount(opts) {
    opts = opts || {};
    if (document.querySelector('.bn-pill')) return; // idempotent
    injectStyleOnce();
    var host = document.querySelector('.wrap') || document.body;
    var tmp = document.createElement('div');
    tmp.innerHTML = markup(opts.parent || '/', opts.label || 'Home');
    var node = tmp.firstChild;
    var headerCard = host.querySelector('.mh-card');
    if (headerCard && headerCard.nextSibling) host.insertBefore(node, headerCard.nextSibling);
    else if (headerCard) host.appendChild(node);
    else host.insertBefore(node, host.firstChild);
  }

  global.MatrixBackNav = { mount: mount };

  var cs = document.currentScript;
  if (cs && cs.hasAttribute('data-auto')) {
    var opts = { parent: cs.getAttribute('data-parent'), label: cs.getAttribute('data-label') };
    var run = function () { mount(opts); };
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', run);
    else run();
  }
})(window);
