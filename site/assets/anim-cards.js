/* anim-cards.js: sample animation cards for the ESP32-S3 Matrix landing page.
 *
 * Self-contained: no imports, no build step. It loads real 8×8 frames captured
 * from the live board (anim-samples.json: raw pre-brightness colors straight off
 * /api/display/framebuffer) and replays them on a canvas with an additive-bloom
 * renderer, so each card shows the panel's ACTUAL output, not a re-implementation.
 */
(function () {
  "use strict";
  const REDUCED = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  // --- additive-bloom 8×8 renderer (one per canvas) -------------------------
  function makePanel(canvas) {
    const ctx = canvas.getContext("2d");
    const S = canvas.width;          // device px (square)
    const c = S / 8;                 // cell size
    return function draw(frame) {     // frame = 64 "RRGGBB"
      ctx.globalCompositeOperation = "source-over";
      ctx.fillStyle = "#060608";
      ctx.fillRect(0, 0, S, S);
      // faint grid dots for every cell
      ctx.fillStyle = "#10160f";
      for (let i = 0; i < 64; i++) {
        const x = i % 8, y = (i / 8) | 0;
        ctx.beginPath();
        ctx.arc(x * c + c / 2, y * c + c / 2, c * 0.13, 0, 7);
        ctx.fill();
      }
      // lit pixels, additive
      ctx.globalCompositeOperation = "lighter";
      for (let i = 0; i < 64; i++) {
        const hex = frame[i];
        const r = parseInt(hex.slice(0, 2), 16);
        const g = parseInt(hex.slice(2, 4), 16);
        const b = parseInt(hex.slice(4, 6), 16);
        if (r < 6 && g < 6 && b < 6) continue;
        const cx = (i % 8) * c + c / 2;
        const cy = ((i / 8) | 0) * c + c / 2;
        const halo = ctx.createRadialGradient(cx, cy, 0, cx, cy, c * 0.92);
        halo.addColorStop(0, `rgba(${r},${g},${b},.85)`);
        halo.addColorStop(0.45, `rgba(${r},${g},${b},.34)`);
        halo.addColorStop(1, `rgba(${r},${g},${b},0)`);
        ctx.fillStyle = halo;
        ctx.fillRect(cx - c, cy - c, c * 2, c * 2);
        const core = ctx.createRadialGradient(cx, cy, 0, cx, cy, c * 0.34);
        core.addColorStop(0, `rgba(${Math.min(255, r + 90)},${Math.min(255, g + 90)},${Math.min(255, b + 90)},1)`);
        core.addColorStop(1, `rgba(${r},${g},${b},.15)`);
        ctx.fillStyle = core;
        ctx.beginPath();
        ctx.arc(cx, cy, c * 0.34, 0, 7);
        ctx.fill();
      }
      ctx.globalCompositeOperation = "source-over";
    };
  }

  function buildCard(a) {
    const card = document.createElement("figure");
    card.className = "anim-card";
    const cv = document.createElement("canvas");
    cv.width = 200; cv.height = 200;
    cv.setAttribute("role", "img");
    cv.setAttribute("aria-label", `${a.label} animation running on the 8×8 panel`);
    const meta = document.createElement("figcaption");
    meta.innerHTML =
      `<span class="an">${a.label}</span><span class="ad">${a.desc}</span>` +
      `<code class="at">type:"${a.type}"</code>`;
    card.appendChild(cv);
    card.appendChild(meta);
    return { card, cv };
  }

  fetch("./assets/anim-samples.json")
    .then((r) => r.json())
    .then((data) => {
      const grid = document.getElementById("anim-grid");
      if (!grid) return;
      const players = [];
      for (const a of data.anims) {
        const { card, cv } = buildCard(a);
        grid.appendChild(card);
        const draw = makePanel(cv);
        draw(a.frames[0]);
        players.push({ draw, frames: a.frames, i: 0, visible: true });
      }
      if (REDUCED) return; // static first frame only

      const frameMs = 1000 / (data.fps || 12);

      // only animate cards that are on-screen
      if ("IntersectionObserver" in window) {
        const cards = grid.querySelectorAll(".anim-card");
        const io = new IntersectionObserver((entries) => {
          entries.forEach((e) => {
            const idx = [...cards].indexOf(e.target);
            if (idx >= 0) players[idx].visible = e.isIntersecting;
          });
        }, { threshold: 0.1 });
        cards.forEach((el) => io.observe(el));
      }

      let last = 0;
      function loop(now) {
        if (now - last >= frameMs) {
          last = now;
          for (const p of players) {
            if (!p.visible) continue;
            p.i = (p.i + 1) % p.frames.length;
            p.draw(p.frames[p.i]);
          }
        }
        requestAnimationFrame(loop);
      }
      requestAnimationFrame(loop);
    })
    .catch((err) => {
      const grid = document.getElementById("anim-grid");
      if (grid) grid.innerHTML =
        '<p style="color:var(--muted);font-family:var(--mono, monospace);font-size:.82rem">Sample frames unavailable.</p>';
      console.error("anim-cards:", err);
    });
})();
