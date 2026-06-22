/* ============================================================
 * previews.js — shared 8×8 canvas preview engine
 * ------------------------------------------------------------
 * Extracted from animations.html so each per-animation page can
 * mount the SAME preview. The step functions are JS mirrors of the
 * firmware animations (relocated verbatim — do not "improve" them).
 *
 *   MatrixPreview.start(canvasEl, type)   // begin animating `type` on a square canvas
 *   MatrixPreview.stop()                  // stop the loop
 *   MatrixPreview.setRainbowMode('rainbow'|'palette')
 *   MatrixPreview.setRainbowPalette(i)
 *   MatrixPreview.setDancefloorPalette(i)
 *
 * Reads control values from the page by element id (color pickers,
 * sliders) exactly as the original did — a page just needs the same
 * input ids. Requires palettes.js (window.DF_PAL). LittleFS, no build.
 * ============================================================ */
(function (global) {
  'use strict';

  // ── Module state ──────────────────────────────────────────────
  var prevCanvas = null, prevCtx = null;
  var prevCells  = new Array(64).fill('#111');
  var prevType   = 'rainbow';
  var prevIval   = null;

  // ── Helpers ───────────────────────────────────────────────────
  function hex2rgb(hex) {
    return { r: parseInt(hex.slice(1,3),16), g: parseInt(hex.slice(3,5),16), b: parseInt(hex.slice(5,7),16) };
  }
  function lerp(a, b, t) { return a + (b-a)*t; }
  function lerpc(c1, c2, t) { return 'rgb(' + (~~lerp(c1.r,c2.r,t)) + ',' + (~~lerp(c1.g,c2.g,t)) + ',' + (~~lerp(c1.b,c2.b,t)) + ')'; }
  function getEl(id) { return document.getElementById(id); }
  function pickVal(id, fallback) { var e = getEl(id); return (e && e.value) || fallback; }
  function sc8(ch, scale) { return (ch * (scale + 1)) >> 8; }   // FastLED scale8

  // ── Driver (canvas-size-agnostic: cell = width/8) ─────────────
  function drawPrevGrid() {
    var W = prevCanvas.width, cell = W / 8;
    prevCtx.fillStyle = '#0d0d0d';
    prevCtx.fillRect(0, 0, W, W);
    for (var y = 0; y < 8; y++) {
      for (var x = 0; x < 8; x++) {
        prevCtx.fillStyle = prevCells[y*8+x];
        prevCtx.fillRect(x*cell+1, y*cell+1, cell-2, cell-2);
      }
    }
  }
  function startPreview(type) {
    prevType = type;
    if (prevIval) clearInterval(prevIval);
    initPrevState(type);
    prevIval = setInterval(tickPreview, 66);
    tickPreview();
  }
  function tickPreview() { stepPreview(prevType); drawPrevGrid(); }
  function stepPreview(type) {
    switch (type) {
      case 'rainbow':    stepPrevRainbow();    break;
      case 'breathe':    stepPrevBreathe();    break;
      case 'wave':       stepPrevWave();       break;
      case 'solid':      stepPrevSolid();      break;
      case 'spiral':     stepPrevSpiral();     break;
      case 'starfield':  stepPrevStarfield();  break;
      case 'frostbite':  stepPrevFrostbite();  break;
      case 'fireworks':  stepPrevFW(false);    break;
      case 'fireworks2': stepPrevFW(true);     break;
      case 'comet':      stepPrevComet();      break;
      case 'sun':        stepPrevSun();        break;
      case 'dancefloor': stepPrevDanceFloor(); break;
    }
  }

  // ── Rainbow ───────────────────────────────────────────────────
  var rnPhase = 0, rnPalMode = 'rainbow', rnPalIdx = 0;
  function rnPalAt(t) {
    var pal = global.DF_PAL[rnPalIdx];
    var seg = (t >> 6) & 3, frac = (t & 63) / 63;
    return lerpc(hex2rgb(pal[seg+1]), hex2rgb(pal[((seg+1)%4)+1]), frac);
  }
  function stepPrevRainbow() {
    for (var x = 0; x < 8; x++) {
      var hue8 = (rnPhase + x*32) & 255, color;
      if (rnPalMode === 'palette') {
        color = rnPalAt(hue8);
      } else {
        var h = (hue8 * 360 / 256) | 0;
        var sinVal = Math.sin((hue8 / 256) * Math.PI * 2);
        var lum = 34 + sinVal * 11;
        color = 'hsl(' + h + ',100%,' + lum.toFixed(1) + '%)';
      }
      for (var y = 0; y < 8; y++) prevCells[y*8+x] = color;
    }
    rnPhase = (rnPhase + 5) & 255;
  }

  // ── Breathe ───────────────────────────────────────────────────
  var bPhase = 0;
  function stepPrevBreathe() {
    var bri = Math.sin(bPhase)*0.45 + 0.5; bPhase += 0.07;
    var c = hex2rgb(pickVal('color','#0064ff'));
    prevCells.fill('rgb(' + (~~(c.r*bri)) + ',' + (~~(c.g*bri)) + ',' + (~~(c.b*bri)) + ')');
  }

  // ── Wave ──────────────────────────────────────────────────────
  var wvPhase = 0;
  function stepPrevWave() {
    prevCells.fill('#000');
    var c1 = hex2rgb(pickVal('wave-c1','#0000ff')), c2 = hex2rgb(pickVal('wave-c2','#000028'));
    for (var x = 0; x < 8; x++) {
      var ht = Math.round((Math.sin(wvPhase + x*0.75)*0.5 + 0.5) * 8);
      var surface = 8 - ht;
      for (var y = surface; y < 8; y++) {
        var t = ht <= 1 ? 0 : (y - surface) / Math.max(1, 7 - surface);
        prevCells[y*8+x] = lerpc(c1, c2, t);
      }
    }
    wvPhase += 0.12;
  }

  // ── Solid ─────────────────────────────────────────────────────
  function stepPrevSolid() { prevCells.fill(pickVal('color','#0064ff')); }

  // ── Spiral ────────────────────────────────────────────────────
  var SPIRAL_PATH = (function () {
    var p = [], top=0,bot=7,left=0,right=7;
    while (p.length < 64) {
      for (var x=left;x<=right;x++) p.push([x,top]);  top++;
      for (var y=top;y<=bot;y++)    p.push([right,y]); right--;
      for (var x2=right;x2>=left;x2--) p.push([x2,bot]); bot--;
      for (var y2=bot;y2>=top;y2--) p.push([left,y2]); left++;
    }
    return p;
  })();
  var spPhase = 0;
  function stepPrevSpiral() {
    var c1 = hex2rgb(pickVal('spiral-c1','#ff0000')), c2 = hex2rgb(pickVal('spiral-c2','#0000ff'));
    for (var i = 0; i < 64; i++) {
      var t = ((i + 64 - spPhase) % 64) / 63;
      prevCells[SPIRAL_PATH[i][1]*8 + SPIRAL_PATH[i][0]] = lerpc(c1, c2, t);
    }
    spPhase = (spPhase+1) % 64;
  }

  // ── Starfield ─────────────────────────────────────────────────
  var sfStars = [], sfReady = false;
  function spawnSfStar(s) {
    var inward = (getEl('star-inward') && getEl('star-inward').checked) || false;
    if (inward) {
      var edge = Math.floor(Math.random()*4);
      if      (edge===0) { s.x=Math.random()*7; s.y=0; }
      else if (edge===1) { s.x=7; s.y=Math.random()*7; }
      else if (edge===2) { s.x=Math.random()*7; s.y=7; }
      else               { s.x=0; s.y=Math.random()*7; }
      var cx=3.5-s.x, cy=3.5-s.y, len=Math.sqrt(cx*cx+cy*cy)||0.1, spd=0.18+Math.random()*0.12;
      s.dx=cx/len*spd; s.dy=cy/len*spd;
    } else {
      s.x=3.5; s.y=3.5;
      var a=Math.random()*Math.PI*2, spd2=0.15+Math.random()*0.18;
      s.dx=Math.cos(a)*spd2; s.dy=Math.sin(a)*spd2;
    }
    s.age=Math.floor(Math.random()*8); s.maxAge=25+Math.floor(Math.random()*15);
    s.bri=100+Math.floor(Math.random()*155); s.active=true;
  }
  function initSfStars() { sfStars = Array.from({length:8},function(){return {};}); sfStars.forEach(spawnSfStar); sfReady = true; }
  function stepPrevStarfield() {
    if (!sfReady) initSfStars();
    prevCells.fill('#000');
    var n = Math.min(+((getEl('star-density') && getEl('star-density').value) || 8), 8);
    var c1 = hex2rgb(pickVal('star-c1','#ffffff')), c2 = hex2rgb(pickVal('star-c2','#0064ff'));
    for (var i=0; i<n; i++) {
      var s = sfStars[i];
      if (!s.active || s.age >= s.maxAge) { spawnSfStar(s); continue; }
      s.x+=s.dx; s.y+=s.dy; s.age++;
      if (s.x<-0.5||s.x>7.5||s.y<-0.5||s.y>7.5) { spawnSfStar(s); continue; }
      var atCenter = getEl('star-inward') && getEl('star-inward').checked && Math.abs(s.x-3.5)<0.7 && Math.abs(s.y-3.5)<0.7;
      if (atCenter) { spawnSfStar(s); continue; }
      var t=s.age/s.maxAge, f=s.bri/255;
      var r=~~(lerp(c1.r,c2.r,t)*f), g=~~(lerp(c1.g,c2.g,t)*f), b=~~(lerp(c1.b,c2.b,t)*f);
      prevCells[~~s.y*8+~~s.x] = 'rgb(' + r + ',' + g + ',' + b + ')';
    }
  }

  // ── Frostbite ─────────────────────────────────────────────────
  var FB_BRI = new Uint8Array(64), FB_DIR = new Int8Array(64);
  var FB_SPARKS = Array.from({length:8},function(){return {idx:0,phase:40,active:false};});
  var fbPrevInit = false;
  function stepPrevFrostbite() {
    var mistPct = +((getEl('fb-mist') && getEl('fb-mist').value) || 40);
    var mistMax = Math.round(mistPct * 2), lo = Math.max(8, mistMax >> 1);
    if (!fbPrevInit) {
      for (var i=0;i<64;i++) { FB_BRI[i] = lo + Math.floor(Math.random()*(mistMax-lo+1)); FB_DIR[i] = Math.random()<0.5 ? 1 : -1; }
      fbPrevInit = true;
    }
    for (var i2=0; i2<64; i2++) {
      if (Math.random() < 0.033) FB_DIR[i2] = -FB_DIR[i2];
      var next = FB_BRI[i2] + FB_DIR[i2];
      if (next >= mistMax) { FB_BRI[i2] = mistMax; FB_DIR[i2] = -1; }
      else if (next <= lo) { FB_BRI[i2] = lo; FB_DIR[i2] = 1; }
      else { FB_BRI[i2] = next; }
    }
    var col = hex2rgb(pickVal('fb-color','#dce8ff'));
    for (var i3=0; i3<64; i3++) { var f = FB_BRI[i3]/255; prevCells[i3] = 'rgb(' + (~~(col.r*f)) + ',' + (~~(col.g*f)) + ',' + (~~(col.b*f)) + ')'; }
    var rate = +((getEl('fb-sparkle') && getEl('fb-sparkle').value) || 20);
    if (Math.random()*100 < rate) {
      var free = FB_SPARKS.find(function(s){return !s.active;});
      if (free) { free.idx=Math.floor(Math.random()*64); free.phase=0; free.active=true; }
    }
    FB_SPARKS.forEach(function(sp) {
      if (!sp.active) return;
      var bri = (Math.sin(sp.phase * Math.PI / 39) * 255) | 0;
      if (bri > 0) { var f2 = bri/255; prevCells[sp.idx] = 'rgb(' + (~~(col.r*f2)) + ',' + (~~(col.g*f2)) + ',' + (~~(col.b*f2)) + ')'; }
      sp.phase++; if (sp.phase >= 40) sp.active = false;
    });
  }

  // ── Fireworks / Fireworks2 ────────────────────────────────────
  var fwState = {};
  function initFWState() { fwState = { phase:'idle', timer:0, mx:3, my:7, mdx:0, mdy:0, ey:3, tendrils:[] }; }
  function stepPrevFW(comet) {
    prevCells.fill('#000');
    if (!fwState.phase) initFWState();
    if (fwState.phase==='idle') {
      if (++fwState.timer>10) {
        fwState.mx=2+Math.floor(Math.random()*4); fwState.my=7;
        fwState.mdx=(Math.random()-0.5)*0.3; fwState.mdy=-(0.7+Math.random()*0.3);
        fwState.ey=1+Math.floor(Math.random()*4); fwState.phase='launch'; fwState.timer=0;
      }
      return;
    }
    if (fwState.phase==='launch') {
      fwState.mx+=fwState.mdx; fwState.my+=fwState.mdy;
      if (fwState.my<=fwState.ey) {
        var colors = comet
          ? [pickVal('fw2-c1','#ff3200'),pickVal('fw2-c2','#ffc800'),pickVal('fw2-c3','#0064ff')]
          : [pickVal('fw-c1','#ff3200'),pickVal('fw-c2','#ffc800'),pickVal('fw-c3','#0064ff')];
        fwState.tendrils = Array.from({length:12},function(_,i){
          var a=i*Math.PI*2/12+Math.random()*0.3, spd=0.3+Math.random()*0.12;
          return {x:fwState.mx,y:fwState.my,dx:Math.cos(a)*spd,dy:Math.sin(a)*spd,bri:255,active:true,colors:colors};
        });
        prevCells[~~fwState.my*8+~~fwState.mx]='#fff'; fwState.phase='fade';
      } else {
        var ix=~~fwState.mx, iy=~~fwState.my;
        if (ix>=0&&ix<8&&iy>=0&&iy<8) prevCells[iy*8+ix]='#fff';
      }
      return;
    }
    var any=false;
    fwState.tendrils.forEach(function(t){
      if (!t.active) return; any=true;
      if (comet) {
        for (var j=3;j>=1;j--) {
          var tx=~~(t.x-j*t.dx), ty=~~(t.y-j*t.dy);
          if (tx>=0&&tx<8&&ty>=0&&ty<8) { var f=t.bri/255*(4-j)/5; prevCells[ty*8+tx]='rgb(' + (~~(220*f)) + ',' + (~~(220*f)) + ',' + (~~(255*f)) + ')'; }
        }
      }
      t.x+=t.dx; t.y+=t.dy; t.bri=Math.max(0,t.bri-18);
      if (t.bri<5||t.x<0||t.x>7.5||t.y<0||t.y>7.5){t.active=false;return;}
      var f1=t.bri/255, rgb=t.colors[0].slice(1).match(/../g).map(function(h){return parseInt(h,16);});
      prevCells[~~t.y*8+~~t.x]='rgb(' + (~~(rgb[0]*f1)) + ',' + (~~(rgb[1]*f1)) + ',' + (~~(rgb[2]*f1)) + ')';
    });
    if (!any){fwState.phase='idle';fwState.timer=0;}
  }

  // ── Comet ─────────────────────────────────────────────────────
  var cmPhase = 0; var CM_HIST = new Array(8).fill(3.5); var cmHistIdx = 0;
  function stepPrevComet() {
    prevCells.fill('#000'); cmPhase += 0.10;
    var cy = 3.5 + Math.sin(cmPhase)*2;
    CM_HIST[cmHistIdx] = cy; cmHistIdx = (cmHistIdx+1) % 8;
    var histY = function(n){ return CM_HIST[(cmHistIdx+16-1-n)%8]; };
    var c1 = hex2rgb(pickVal('comet-c1','#ffc832')), c2 = hex2rgb(pickVal('comet-c2','#ff6400'));
    var c3 = hex2rgb(pickVal('comet-c3','#c83200')), c4 = hex2rgb(pickVal('comet-c4','#500a00'));
    var drawCol = function(x, histN, rowOff, rows, col, scale) {
      var baseY = ~~histY(histN);
      for (var r=0;r<rows;r++) { var py=baseY+rowOff+r; if (py>=0&&py<8) prevCells[py*8+x]='rgb(' + sc8(col.r,scale) + ',' + sc8(col.g,scale) + ',' + sc8(col.b,scale) + ')'; }
    };
    drawCol(5,1,-1,4,c2,192); drawCol(4,2,-1,4,c3,140); drawCol(3,3,-1,4,c3,102); drawCol(2,4,-1,4,c4,64);
    for (var dx=0;dx<2;dx++) for (var dy=0;dy<2;dy++) { var py=~~cy+dy; if (py>=0&&py<8) prevCells[py*8+6+dx]='rgb(' + c1.r + ',' + c1.g + ',' + c1.b + ')'; }
  }

  // ── Sun ───────────────────────────────────────────────────────
  var SUN_BX=[3,6,7,6,4,1,0,1], SUN_BY=[0,1,3,6,7,6,4,1], sunSlot=0;
  function stepPrevSun() {
    prevCells.fill('#000');
    var discPct = +((getEl('sun-disc') && getEl('sun-disc').value) || 78);
    var ringPct = +((getEl('sun-ring') && getEl('sun-ring').value) || 78);
    function sunCh(ch, pct) { var s1 = (pct/100*255)|0; return sc8(ch, s1); }
    var c1=hex2rgb(pickVal('sun-c1','#ffb700')), c2=hex2rgb(pickVal('sun-c2','#ff6600'));
    var c3=hex2rgb(pickVal('sun-c3','#ff3300')), c4=hex2rgb(pickVal('sun-c4','#cc1100')), c5=hex2rgb(pickVal('sun-c5','#880000'));
    for (var y=2;y<=5;y++) for (var x=2;x<=5;x++)
      if (!((x===2||x===5)&&(y===2||y===5)))
        prevCells[y*8+x]='rgb(' + sunCh(c1.r,discPct) + ',' + sunCh(c1.g,discPct) + ',' + sunCh(c1.b,discPct) + ')';
    var dotColors=[c2,c3,c4,c5];
    for (var d=0;d<4;d++) { var pos=(sunSlot+d*2)%8, col=dotColors[d]; prevCells[SUN_BY[pos]*8+SUN_BX[pos]]='rgb(' + sunCh(col.r,ringPct) + ',' + sunCh(col.g,ringPct) + ',' + sunCh(col.b,ringPct) + ')'; }
    sunSlot=(sunSlot+1)%8;
  }

  // ── Dance Floor ───────────────────────────────────────────────
  var DF_SIM_BLEND = 10;
  var dfSim = { pal:0, slotCur:[0,1,2,3], slotNxt:[1,2,3,0], blendPos:DF_SIM_BLEND, holdCount:22 };
  function dfSimShuffle() { var p=[0,1,2,3]; for (var i=3;i>0;i--){var j=~~(Math.random()*(i+1));var t=p[i];p[i]=p[j];p[j]=t;} return p; }
  function dfSimReset(palIdx) { dfSim.pal=palIdx; dfSim.slotCur=dfSimShuffle(); dfSim.slotNxt=dfSimShuffle(); dfSim.blendPos=DF_SIM_BLEND; dfSim.holdCount=22; }
  function stepPrevDanceFloor() {
    var hold = +((getEl('df-hold') && getEl('df-hold').value) || 12);
    if (dfSim.blendPos < DF_SIM_BLEND) { dfSim.blendPos++; }
    else if (dfSim.holdCount > 0) { dfSim.holdCount--; }
    else { dfSim.slotCur=dfSim.slotNxt.slice(); dfSim.slotNxt=dfSimShuffle(); dfSim.blendPos=0; dfSim.holdCount=hold; }
    var t = dfSim.blendPos >= DF_SIM_BLEND ? 1 : dfSim.blendPos/DF_SIM_BLEND;
    var pal = global.DF_PAL[dfSim.pal];
    for (var ty=0;ty<4;ty++) for (var tx=0;tx<4;tx++) {
      var slot=(tx%2)+(ty%2)*2;
      var fromC=hex2rgb(pal[dfSim.slotCur[slot]+1]), toC=hex2rgb(pal[dfSim.slotNxt[slot]+1]);
      var c=lerpc(fromC,toC,t);
      prevCells[(ty*2)*8+tx*2]=c; prevCells[(ty*2)*8+tx*2+1]=c; prevCells[(ty*2+1)*8+tx*2]=c; prevCells[(ty*2+1)*8+tx*2+1]=c;
    }
  }

  function initPrevState(type) {
    if (type==='rainbow')   { rnPhase=0; }
    if (type==='starfield') { sfReady=false; }
    if (type==='fireworks'||type.indexOf('fireworks')===0) initFWState();
    if (type==='comet')     { cmPhase=0; CM_HIST.fill(3.5); cmHistIdx=0; }
    if (type==='frostbite') { fbPrevInit=false; }
    if (type==='sun')       { sunSlot=0; }
  }

  // ── Public API ────────────────────────────────────────────────
  global.MatrixPreview = {
    start: function (canvasEl, type) { prevCanvas = canvasEl; prevCtx = canvasEl.getContext('2d'); startPreview(type); },
    stop:  function () { if (prevIval) { clearInterval(prevIval); prevIval = null; } },
    setRainbowMode:      function (m) { rnPalMode = m; },
    setRainbowPalette:   function (i) { rnPalIdx = i; },
    setDancefloorPalette: function (i) { dfSimReset(i); }
  };
})(window);
