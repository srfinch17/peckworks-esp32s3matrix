/* ============================================================
 * palettes.js — shared palette + preset data for the UI
 * ------------------------------------------------------------
 * Extracted from animations.html so every per-animation page
 * (and the dance-floor / rainbow / fireworks pickers) shares
 * ONE source of truth. Pure data on window + small grid helpers.
 * Served from LittleFS — no firmware change, no build step.
 * ============================================================ */
(function (global) {
  'use strict';

  // 64 dance-floor palettes: [name, c1, c2, c3, c4]. Used by Dance Floor,
  // Rainbow (palette mode), Comet, Wave, Spiral swatch grids.
  global.DF_PAL = [
    ['Neon Classic',  '#ff00ff','#00ffff','#ffff00','#00ff00'],
    ['Neon Shifted',  '#ff0080','#00ff80','#8000ff','#ff8000'],
    ['Neon Soft',     '#ff00c8','#00c8ff','#c8ff00','#ffc800'],
    ['Primary Neon',  '#ff0050','#5000ff','#00ff50','#ff5000'],
    ['Electric',      '#dc00ff','#00ffdc','#ffdc00','#00dcff'],
    ['Pink Purple',   '#ff00ff','#ff0064','#6400ff','#0064ff'],
    ['Cyan Family',   '#00ffff','#00c8ff','#00ffc8','#0096ff'],
    ['Yellow Family', '#ffff00','#ffc800','#c8ff00','#ff9600'],
    ['Fire',          '#ff3200','#ff9600','#ffc800','#ffff32'],
    ['Red Hot',       '#ff0000','#ff5000','#c80000','#ff2828'],
    ['Amber',         '#ff6400','#ffc832','#ff3200','#c86400'],
    ['Ember',         '#ff1400','#ff6400','#ffa000','#ffff64'],
    ['Lava',          '#ff0032','#ff3200','#c80064','#ff6432'],
    ['Sunset Warm',   '#ff5050','#ff2800','#c81400','#ffa050'],
    ['Gold',          '#ffc800','#ff6400','#ff3232','#c8c800'],
    ['Hot Candy',     '#ff0050','#ff5000','#ffb400','#c80050'],
    ['Ocean',         '#0064ff','#00c8ff','#00ffc8','#3232c8'],
    ['Aqua',          '#0096ff','#00ffff','#00c8c8','#64c8ff'],
    ['Deep Blue',     '#0032c8','#3264ff','#00c8ff','#6432c8'],
    ['Teal',          '#00c8c8','#0096c8','#32c8ff','#006496'],
    ['Blue Purple',   '#6400ff','#0064ff','#00c8ff','#3200c8'],
    ['Ice',           '#00ffff','#00c8ff','#0096ff','#0064c8'],
    ['Violet',        '#3200c8','#6400ff','#9632ff','#c864ff'],
    ['Navy',          '#0064c8','#003296','#3296ff','#64c8ff'],
    ['Forest',        '#00c800','#32ff32','#64ff00','#009632'],
    ['Lime',          '#00ff00','#64ff00','#00c832','#32ff64'],
    ['Chartreuse',    '#64ff00','#c8ff00','#32c800','#96ff32'],
    ['Emerald',       '#009632','#00c864','#32ff96','#006432'],
    ['Earth',         '#c89632','#966400','#64c832','#c8c864'],
    ['Autumn',        '#ff9600','#c86400','#64c800','#ffc832'],
    ['Spring',        '#ff6496','#c8ff64','#64c8ff','#ffc864'],
    ['Jade',          '#00c896','#009664','#32ffc8','#64ffc8'],
    ['Pastel Rainbow','#ff96c8','#96c8ff','#c8ff96','#ffff96'],
    ['Pastel Pink',   '#ff96c8','#ff6496','#c864c8','#ffc8dc'],
    ['Pastel Blue',   '#96c8ff','#6496ff','#9696ff','#c8dcff'],
    ['Pastel Green',  '#c8ffc8','#96ff96','#64dc96','#c8ffb4'],
    ['Pastel Warm',   '#ffc896','#ffdc96','#c89664','#ffe6b4'],
    ['Pastel Purple', '#c896ff','#dcb4ff','#b464ff','#f0c8ff'],
    ['Pastel Mint',   '#96fff0','#96dcff','#b4ffdc','#c8ffff'],
    ['Pastel Yellow', '#ffff96','#fff064','#ffc864','#ffffc8'],
    ['Red Mono',      '#ff0000','#c80000','#960000','#640000'],
    ['Orange Mono',   '#ff5000','#c83c00','#962800','#ff7800'],
    ['Yellow Mono',   '#ffff00','#c8c800','#969600','#ffdc32'],
    ['Green Mono',    '#00ff00','#00c800','#009600','#32ff32'],
    ['Blue Mono',     '#0000ff','#0000c8','#0032ff','#3232ff'],
    ['Purple Mono',   '#9600ff','#6400c8','#c832ff','#5000b4'],
    ['Pink Mono',     '#ff0096','#c80064','#ff32b4','#960050'],
    ['Teal Mono',     '#00ffc8','#00c896','#32ffdc','#009678'],
    ['80s Classic',   '#ff00ff','#00ff00','#ffff00','#0000ff'],
    ['Miami Vice',    '#ff0064','#00c8ff','#c8ff00','#ff6400'],
    ['Synthwave',     '#6400ff','#ff00ff','#00c8c8','#ffc800'],
    ['VHS',           '#ff3296','#9600ff','#00c8ff','#ffc832'],
    ['Arcade',        '#00ff00','#00c800','#ff0000','#0000ff'],
    ['Pac-Man',       '#ffc800','#ff6400','#00c800','#c800c8'],
    ['Pinball',       '#ffff00','#ff0000','#0000ff','#ffffff'],
    ['Funky',         '#00ff96','#ff0064','#ff9600','#6400ff'],
    ['Dark Galaxy',   '#640096','#003296','#960064','#006464'],
    ['Ember Dark',    '#500000','#320032','#000050','#502800'],
    ['Noir',          '#960032','#640064','#320096','#003264'],
    ['Deep Forest',   '#006400','#005032','#326400','#003c3c'],
    ['Rust',          '#643200','#500000','#323200','#3c1e00'],
    ['Abyss',         '#005064','#003250','#320064','#006450'],
    ['Dusk',          '#500050','#3c003c','#640032','#320050'],
    ['Grayscale',     '#323232','#505050','#787878','#1e1e1e'],
  ];

  // Fireworks preset shows: [name, c1, c2, c3].
  global.FW_PRESETS = [
    ['Classic Burst', '#ff3200', '#ffc800', '#0064ff'],
    ['Golden Willow', '#ffc832', '#ff8c00', '#c83200'],
    ['Patriot',       '#ff3200', '#ffffff', '#0064ff'],
    ['Emerald Rain',  '#00ff64', '#64ffc8', '#00a050'],
    ['Pink Peony',    '#ff2d78', '#ff96c8', '#b400ff'],
    ['Ice Crystal',   '#dcebff', '#78c8ff', '#0064ff'],
    ['Violet Storm',  '#b400ff', '#ff00a0', '#5000ff'],
    ['Ember Glow',    '#ff6400', '#ff9600', '#c84600'],
    ['Aurora',        '#00ffc8', '#00c8ff', '#9600ff'],
    ['White Strobe',  '#ffffff', '#c8dcff', '#ffc8dc'],
  ];

  // Frostbite presets: { name, color }.
  global.FB_PRESETS = [
    { name:'Arctic',   color:'#dce8ff' },
    { name:'Ice Blue', color:'#4488ff' },
    { name:'Mint',     color:'#44ffcc' },
    { name:'Aurora',   color:'#aa44ff' },
    { name:'Rose Ice', color:'#ff88aa' },
    { name:'Gold Ice', color:'#ffe8aa' },
  ];

  // Sun presets: { name, c1..c5 }.
  global.SUN_PRESETS = [
    { name:'Solar',   c1:'#ffb700', c2:'#ff6600', c3:'#ff3300', c4:'#cc1100', c5:'#660000' },
    { name:'Arctic',  c1:'#ffffff', c2:'#88ddff', c3:'#4499ff', c4:'#0055cc', c5:'#002288' },
    { name:'Twilight',c1:'#ff99ff', c2:'#cc44ff', c3:'#9900cc', c4:'#550088', c5:'#220044' },
    { name:'Neon',    c1:'#aaffaa', c2:'#00ff44', c3:'#00cc22', c4:'#005511', c5:'#002208' },
    { name:'Lava',    c1:'#ffff00', c2:'#ff4400', c3:'#cc0000', c4:'#660000', c5:'#220000' },
  ];

  // Build a 4-color swatch grid from DF_PAL into gridEl. onPick(i, palette) fires
  // on click; the picked swatch gets `.active`. Returns nothing.
  global.buildDfPalGrid = function (gridEl, onPick, activeIdx) {
    if (!gridEl) return;
    global.DF_PAL.forEach(function (pal, i) {
      var name = pal[0], c1 = pal[1], c2 = pal[2], c3 = pal[3], c4 = pal[4];
      var el = document.createElement('div');
      el.className = 'df-swatch' + (i === activeIdx ? ' active' : '');
      el.title = i + ': ' + name;
      el.style.background = 'linear-gradient(to right,' + c1 + ' 25%,' + c2 + ' 25% 50%,' + c3 + ' 50% 75%,' + c4 + ' 75%)';
      el.onclick = function () {
        gridEl.querySelectorAll('.df-swatch').forEach(function (s, j) { s.classList.toggle('active', j === i); });
        onPick(i, pal);
      };
      gridEl.appendChild(el);
    });
  };
})(window);
