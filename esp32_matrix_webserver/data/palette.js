/* ============================================================
 * palette.js — shared N-color palette / picker component (Roadmap S2)
 * ------------------------------------------------------------
 * One reusable color chooser for the whole app: preset swatches +
 * N labeled color pickers, reported via onChange. Stops every page
 * hand-rolling its own palette UI and gives a unified look.
 *
 *   var pal = Palette.mount('#slot', {
 *     count: 3,
 *     labels: ['Hours','Colon','Minutes'],
 *     defaults: ['#ff3300','#ffffff','#00ccff'],
 *     onChange: function (colors) { ... }   // colors = ['#..', ...]
 *   });
 *   pal.get();  pal.set(['#..', ...]);
 *
 * Served from LittleFS — no firmware change, no build step.
 * ============================================================ */
(function (global) {
  'use strict';

  // Curated presets; each has >=4 colors so any count up to 4 works.
  var PRESETS = [
    { name: 'Fire',   colors: ['#ff2a00','#ff7b00','#ffd000','#fff3a0'] },
    { name: 'Ocean',  colors: ['#003cff','#0090ff','#00d0ff','#bff4ff'] },
    { name: 'Lime',   colors: ['#0a8a00','#5fd000','#b6ff3a','#f0ffb0'] },
    { name: 'Plasma', colors: ['#6a00ff','#c000ff','#ff4ad0','#ffc0f0'] },
    { name: 'Sunset', colors: ['#ff004c','#ff5a00','#ffb000','#ffe88a'] },
    { name: 'Ice',    colors: ['#0040a0','#3aa0ff','#9fe0ff','#ffffff'] },
    { name: 'Mono',   colors: ['#ffffff','#bbbbbb','#777777','#333333'] },
    { name: 'RGBY',   colors: ['#ff0000','#00ff00','#2060ff','#ffe000'] }
  ];

  var CSS =
    '.pal-wrap{font-family:system-ui,-apple-system,sans-serif}' +
    '.pal-title{font-size:.72rem;text-transform:uppercase;letter-spacing:.07em;color:#777;margin-bottom:8px}' +
    '.pal-presets{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:14px}' +
    '.pal-chip{display:flex;width:38px;height:26px;border-radius:6px;overflow:hidden;border:2px solid #2a2a2a;cursor:pointer;transition:transform .1s}' +
    '.pal-chip:hover{transform:scale(1.1)}' +
    '.pal-chip.active{border-color:#fff}' +
    '.pal-chip span{flex:1}' +
    '.pal-pickers{display:flex;gap:14px;flex-wrap:wrap}' +
    '.pal-picker{display:flex;flex-direction:column;align-items:center;gap:4px}' +
    '.pal-picker input[type=color]{width:46px;height:34px;border:1px solid #333;border-radius:6px;background:none;cursor:pointer;padding:2px}' +
    '.pal-picker label{font-size:.62rem;color:#888;text-transform:uppercase;letter-spacing:.05em}';

  function injectStyleOnce() {
    if (document.getElementById('pal-style')) return;
    var s = document.createElement('style');
    s.id = 'pal-style';
    s.textContent = CSS;
    document.head.appendChild(s);
  }

  function mount(target, opts) {
    opts = opts || {};
    var el = typeof target === 'string' ? document.querySelector(target) : target;
    if (!el) { console.warn('Palette: mount target not found:', target); return; }

    var count   = opts.count || 2;
    var labels  = opts.labels || [];
    var presets = opts.presets || PRESETS;
    var onChange = opts.onChange || function () {};

    // Initial colors: defaults, padded from the first preset if short.
    var colors = (opts.defaults || []).slice(0, count);
    while (colors.length < count) colors.push(presets[0].colors[colors.length] || '#ffffff');

    injectStyleOnce();

    // Build markup.
    var html = '<div class="pal-wrap"><div class="pal-title">Palette</div><div class="pal-presets">';
    presets.forEach(function (p, pi) {
      var stripes = '';
      for (var i = 0; i < count; i++) stripes += '<span style="background:' + (p.colors[i] || '#000') + '"></span>';
      html += '<div class="pal-chip" data-preset="' + pi + '" title="' + p.name + '">' + stripes + '</div>';
    });
    html += '</div><div class="pal-pickers">';
    for (var i = 0; i < count; i++) {
      html += '<div class="pal-picker"><input type="color" data-i="' + i + '" value="' + colors[i] + '">' +
              '<label>' + (labels[i] || ('Color ' + (i + 1))) + '</label></div>';
    }
    html += '</div></div>';
    el.innerHTML = html;

    var pickers = el.querySelectorAll('.pal-picker input');
    var chips   = el.querySelectorAll('.pal-chip');

    function clearActive() { chips.forEach(function (c) { c.classList.remove('active'); }); }
    function readPickers() {
      colors = Array.prototype.map.call(pickers, function (p) { return p.value; });
    }

    chips.forEach(function (chip) {
      chip.addEventListener('click', function () {
        var p = presets[+chip.dataset.preset];
        for (var i = 0; i < count; i++) pickers[i].value = p.colors[i] || '#ffffff';
        readPickers();
        clearActive();
        chip.classList.add('active');
        onChange(colors.slice());
      });
    });
    pickers.forEach(function (p) {
      p.addEventListener('input', function () { readPickers(); clearActive(); onChange(colors.slice()); });
    });

    return {
      get: function () { return colors.slice(); },
      set: function (cols) {
        for (var i = 0; i < count && i < cols.length; i++) pickers[i].value = cols[i];
        readPickers();
        clearActive();
        onChange(colors.slice());
      }
    };
  }

  global.Palette = { mount: mount, PRESETS: PRESETS };
})(window);
