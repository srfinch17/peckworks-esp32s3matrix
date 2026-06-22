/* ============================================================
 * backnav.js — shared breadcrumb trail (UI revamp Phase 3)
 * ------------------------------------------------------------
 * Drop-in, mirrors header.js. The page declares only its IMMEDIATE
 * parent; the trail is synthesised as  Home › Parent › ThisPage:
 *   <script src="backnav.js" data-auto data-parent="/animations.html" data-label="Animations"></script>
 *
 * - Home (/) is always the root crumb.
 * - The parent crumb is skipped when the parent IS Home (control pages
 *   that sit directly under Home → trail is just "Home › ThisPage").
 * - "ThisPage" is read from the page <h1> (leading emoji stripped) and
 *   rendered as the non-link current crumb.
 * Ancestor crumbs are tappable pills (the prominent back affordance);
 * the last link is the one-level-up target. Injects its own <style>,
 * inserts right AFTER the header card, idempotent.
 * Served from LittleFS — no firmware change, no build step.
 * ============================================================ */
(function (global) {
  'use strict';

  var CSS =
    '.bn-crumbs{display:flex;flex-wrap:wrap;align-items:center;gap:6px;margin:0 0 16px;' +
      'font-size:.86rem;font-family:system-ui,-apple-system,sans-serif}' +
    '.bn-crumb{display:inline-flex;align-items:center;background:#1c1c1c;border:1px solid #333;' +
      'border-radius:999px;padding:6px 13px;color:#cfcfcf;text-decoration:none;' +
      'transition:border-color .15s,color .15s}' +
    '.bn-crumb:hover{border-color:#555;color:#fff}' +
    '.bn-sep{color:#555;font-size:.95rem;line-height:1;user-select:none}' +
    '.bn-here{display:inline-flex;align-items:center;padding:6px 4px;color:#8a8a8a;font-weight:600}';

  function injectStyleOnce() {
    if (document.getElementById('bn-style')) return;
    var s = document.createElement('style');
    s.id = 'bn-style';
    s.textContent = CSS;
    document.head.appendChild(s);
  }

  function esc(s) {
    return String(s).replace(/[&<>"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  }

  // The current page's name, from its <h1>, with any leading emoji/symbol
  // and whitespace stripped ("🌈 Rainbow" → "Rainbow"). Falls back to the
  // <title> up to the first dash/pipe.
  function currentLabel() {
    var h1 = document.querySelector('.wrap h1') || document.querySelector('h1');
    if (h1) {
      var t = (h1.textContent || '').replace(/^[^A-Za-z0-9]+/, '').trim();
      if (t) return t;
    }
    return (document.title || '').split(/\s+[—|]\s+/)[0].trim();
  }

  function buildCrumbs(parent, label) {
    var crumbs = [{ href: '/', text: 'Home' }];
    if (parent && parent !== '/' && parent !== '') {
      crumbs.push({ href: parent, text: label || 'Back' });
    }
    var here = currentLabel();
    if (here && here.toLowerCase() !== 'home') crumbs.push({ here: true, text: here });
    return crumbs;
  }

  function markup(crumbs) {
    var html = '<nav class="bn-crumbs" aria-label="Breadcrumb">';
    for (var i = 0; i < crumbs.length; i++) {
      if (i) html += '<span class="bn-sep" aria-hidden="true">›</span>';
      var c = crumbs[i];
      if (c.here) html += '<span class="bn-here" aria-current="page">' + esc(c.text) + '</span>';
      else html += '<a class="bn-crumb" href="' + esc(c.href) + '">' + esc(c.text) + '</a>';
    }
    return html + '</nav>';
  }

  function mount(opts) {
    opts = opts || {};
    if (document.querySelector('.bn-crumbs')) return; // idempotent
    injectStyleOnce();
    var host = document.querySelector('.wrap') || document.body;
    var tmp = document.createElement('div');
    tmp.innerHTML = markup(buildCrumbs(opts.parent, opts.label));
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
