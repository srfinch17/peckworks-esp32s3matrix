/* ============================================================
 * header.js — shared board-identity header card (logo + name)
 * ------------------------------------------------------------
 * Drop-in, mirrors bright.js: include once per page as
 *   <script src="header.js" data-auto></script>
 * On load it injects its own <style> and PREPENDS an identical
 * logo header card as the first child of the page's .wrap
 * (fallback <body>). Idempotent — never injects a second card.
 * The logo is the quincunx mark (same motif/palette as the
 * favicon), rendered DEAD-STATIC. Served from LittleFS — no
 * firmware change, no build step.
 * ============================================================ */
(function (global) {
  'use strict';

  // Quincunx logo: 5 lit dots over a faint ghosted panel grid, on a dark
  // rounded tile. ~44px. Palette matches the favicon exactly.
  var LOGO_SVG =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 44 44' width='44' height='44'>" +
      "<rect width='44' height='44' rx='9' fill='#0d0d0d'/>" +
      // faint unlit panel texture: 4x4 grid of tiny dots, offset from the lit
      // quincunx positions (8/18/28/38, not 12/22/32) so it reads as panel
      // backing rather than tracing the logo.
      "<g fill='#ffffff' opacity='0.05'>" +
        "<circle cx='8' cy='8' r='1.4'/><circle cx='18' cy='8' r='1.4'/><circle cx='28' cy='8' r='1.4'/><circle cx='38' cy='8' r='1.4'/>" +
        "<circle cx='8' cy='18' r='1.4'/><circle cx='18' cy='18' r='1.4'/><circle cx='28' cy='18' r='1.4'/><circle cx='38' cy='18' r='1.4'/>" +
        "<circle cx='8' cy='28' r='1.4'/><circle cx='18' cy='28' r='1.4'/><circle cx='28' cy='28' r='1.4'/><circle cx='38' cy='28' r='1.4'/>" +
        "<circle cx='8' cy='38' r='1.4'/><circle cx='18' cy='38' r='1.4'/><circle cx='28' cy='38' r='1.4'/><circle cx='38' cy='38' r='1.4'/>" +
      "</g>" +
      // 5 lit quincunx dots
      "<circle cx='12' cy='12' r='4' fill='#00ff88'/>" +
      "<circle cx='32' cy='12' r='4' fill='#ffb000'/>" +
      "<circle cx='22' cy='22' r='4' fill='#22ddff'/>" +
      "<circle cx='12' cy='32' r='4' fill='#ffb000'/>" +
      "<circle cx='32' cy='32' r='4' fill='#00ff88'/>" +
    "</svg>";

  var CSS =
    '.mh-card{display:flex;align-items:center;gap:14px;background:#161616;border:1px solid #2a2a2a;' +
      'border-radius:12px;padding:14px 18px;margin-bottom:20px;text-decoration:none;transition:border-color .15s}' +
    '.mh-card:hover{border-color:#444}' +
    '.mh-logo{flex:0 0 auto;line-height:0}' +
    '.mh-logo svg{display:block;width:44px;height:44px}' +
    '.mh-text{display:flex;flex-direction:column;gap:2px}' +
    '.mh-name{font-size:1.5rem;font-weight:600;color:#00ff88;letter-spacing:-0.02em;line-height:1.1}' +
    '.mh-sub{font-size:.82rem;color:#666}' +
    '@media (max-width:380px){.mh-name{font-size:1.25rem}}';

  function injectStyleOnce() {
    if (document.getElementById('mh-style')) return;
    var s = document.createElement('style');
    s.id = 'mh-style';
    s.textContent = CSS;
    document.head.appendChild(s);
  }

  function markup() {
    return '<a class="mh-card" href="/" aria-label="Home">' +
             '<span class="mh-logo">' + LOGO_SVG + '</span>' +
             '<span class="mh-text">' +
               '<span class="mh-name">ESP32-S3 Matrix</span>' +
               '<span class="mh-sub">Web control panel</span>' +
             '</span>' +
           '</a>';
  }

  function mount() {
    if (document.querySelector('.mh-card')) return; // idempotent
    injectStyleOnce();
    var host = document.querySelector('.wrap') || document.body;
    var tmp = document.createElement('div');
    tmp.innerHTML = markup();
    host.insertBefore(tmp.firstChild, host.firstChild);
  }

  global.MatrixHeader = { mount: mount };

  var cs = document.currentScript;
  if (cs && cs.hasAttribute('data-auto')) {
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', mount);
    else mount();
  }
})(window);
