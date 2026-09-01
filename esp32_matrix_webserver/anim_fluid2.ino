/*
Copyright 2022 Matthias Müller - Ten Minute Physics,
www.youtube.com/c/TenMinutePhysics
www.matthiasMueller.info/tenMinutePhysics

MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
// This file is a port of Ten Minute Physics episode 18 (the FLIP water simulator,
// 18-flip.html) to the ESP32-S3 LED matrix, with the deviations listed in the
// project plan (docs/superpowers/plans/2026-09-01-fluid2-flip.md, section 10).

// Compile-safety rule R1: the ESP32-S3 has no double-precision FPU, so any bare
// double literal (0.5 instead of 0.5f) silently drags in soft-float library calls
// that are ~20x slower than an FPU multiply. This pragma turns that mistake into a
// compile error, but ONLY inside this file: the matching pop at the very bottom
// keeps the 17 files that follow this one in the translation unit out of the blast
// radius (several of them legitimately printf floats through varargs).
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wdouble-promotion"

#include "esp_heap_caps.h"   // heap_caps_malloc: pick DRAM vs PSRAM explicitly (R6)

// ============================================================
// FLUID2: FLIP/PIC HYBRID PARTICLE FLUID
//
// Where anim_liquid.ino fakes a fluid with one scalar threshold
// (six floats of state, can never splash), this is the real
// thing: a FLIP (FLuid Implicit Particle) simulation.
//
//   1. PARTICLES carry the fluid's velocity and mass. They fall
//      under gravity (the IMU's gravity vector, so tilting the
//      board tilts the fluid) and get pushed apart when they
//      overlap, which is what gives water its volume.
//   2. A staggered "MAC" GRID overlays the tank. Each substep the
//      particle velocities are splatted onto the grid faces
//      (particle-to-grid transfer): horizontal velocity u on the
//      left face of each cell, vertical velocity v on the bottom.
//   3. PRESSURE SOLVE: a Gauss-Seidel relaxation nudges the grid
//      velocities until each fluid cell's inflow equals its
//      outflow (incompressibility). This is the step that makes
//      it behave like water instead of falling sand.
//   4. GRID-TO-PARTICLE transfer hands the corrected velocities
//      back. The FLIP ratio blends two flavors: PIC (resample
//      from the grid, heavily damped, syrupy) and FLIP (keep the
//      particle's velocity plus the grid's correction, lively and
//      splashy). That blend IS the viscosity control.
//
// The tank is (MATRIX_W*K) x (MATRIX_H*K) simulation cells plus a
// one-cell solid border on all four sides; K is "sim cells per
// LED" (1 or 2). Nothing in this file hardcodes the panel size.
//
// Frame convention: the SOLVER works in y-UP coordinates (like
// the reference: j=0 is the tank floor). The render at the bottom
// of this file flips to the panel's y-DOWN screen space.
// ============================================================

// ── Constants ─────────────────────────────────────────────────

// K_MAX: the arena below is allocated once at the largest K this panel is allowed,
// so K can be changed live without reallocating. Memory scales with panel area
// times K squared; above roughly 32x32 LEDs a K of 2 would blow past PSRAM, so the
// ceiling drops to 1 at compile time (plan section 6).
static const int F2_K_MAX = (MATRIX_W * MATRIX_H > 32 * 32) ? 1 : 2;

// Cell types. Plain int8_t constants, NOT an enum: Arduino emits every function
// prototype at the top of the generated translation unit, before this file's body,
// so any type declared here would be unknown at prototype time (rule R3).
static const int8_t F2_FLUID_CELL = 0;
static const int8_t F2_AIR_CELL   = 1;
static const int8_t F2_SOLID_CELL = 2;

// Geometry constants. We work in CELL units (h = 1), which kills a whole family of
// h-vs-1/h bookkeeping bugs; the reference's h and fInvSpacing both collapse to 1.
static const float f2H = 1.0f;              // grid spacing, always 1 cell
static const float f2R = 0.3f;              // particle radius, FIXED (it sizes the hash grid)
static const float f2PInvSpacing = 1.0f / (2.2f * f2R);   // pushApart hash cell size

// ── The single declarations block ─────────────────────────────
// ALL file-scope state for this animation lives here. Every name is f2-prefixed
// (rule R4): these are file-static but still visible to api_handlers.ino, which
// sorts AFTER this file in the single translation unit and assigns the parameter
// globals directly. Never #define short names: a macro would leak into the 17
// later files (anim_sound.ino declares a float dt, weather.ino a parameter rows,
// calibration.ino locals r, g, b).

// Parameters (defaults are the shipping look; api_handlers clamps ranges).
static int   f2Fill         = 50;     // fill percent, 0..100
static int   f2K            = 2;      // sim cells per LED, 1..F2_K_MAX
static int   f2Iters        = 30;     // pressure iterations, 5..60
static int   f2Substeps     = 2;      // 2 = real time at 33 ms/frame, 1 = half-speed slow motion
static float f2GravityScale = 1.0f;   // 0..2, 0 = zero-g blob drift
static float f2FoamGain     = 5.0f;   // 0..10, spray whitening strength
static float f2FlipRatio    = 0.9f;   // 0.65..1.0; viscosity maps here, lower = thicker
static int   f2AchievedFill = 0;      // what seedFluid2() actually landed on (read by api_handlers)

// Gravity direction, SOLVER frame (+y = UP).
// The initial value is -1.0f (DOWN), NOT liquid's +1.0f: liquid's frame is y-DOWN
// and this solver's frame is y-UP, and here the sign lives in the direction vector
// (the magnitude 3.27f * f2IH below is positive) rather than in a negative scalar
// like the reference's gravity = -9.81. Copy liquid's +1.0f and the fluid falls UP
// on a flat board. See plan section 4.
static float f2GXn = 0.0f;
static float f2GYn = -1.0f;

// Geometry, recomputed by f2LayoutArena() on every reseed (K is a live control).
static int f2IW = 0, f2IH = 0;             // interior cells: MATRIX_W*K x MATRIX_H*K
static int f2NX = 0, f2NY = 0;             // grid incl. the solid border: IW+2 x IH+2
static int f2FNumCells = 0;                // f2NX * f2NY
static int f2PNumX = 0, f2PNumY = 0;       // pushApart hash grid, PER AXIS: collapsing
static int f2PNumCells = 0;                //   them to one breaks non-square panels
static int f2MaxParticles = 0;             // lattice capacity at 100% fill

// Counts and solver state.
static int   f2NumParticles        = 0;
static int   f2NumFluidCells       = 0;    // rebuilt every substep (compacted list, O4)
static int   f2NumInteriorCells    = 0;    // f2IW * f2IH, set at seed
static bool  f2CompensateDrift     = true; // recomputed per substep (section 5 hysteresis)
static float f2ParticleRestDensity = 0.0f; // measured ONCE per seed, gated on == 0
// Depth normalization, from the four tank corners (section 9).
static float f2ProjMin = 0.0f, f2ProjMax = 1.0f;

// The arena: ONE heap block, allocated once at F2_K_MAX and 100% fill, never
// freed (allocate/free churn per animation switch is the frames-heap-crash
// pattern). Every array below is an offset into it.
static uint8_t*  f2Arena = nullptr;
static float*    f2U = nullptr;                  // horizontal velocity, on cell LEFT faces
static float*    f2V = nullptr;                  // vertical velocity, on cell BOTTOM faces
static float*    f2DU = nullptr;                 // splat weight accumulators for u
static float*    f2DV = nullptr;                 //   and for v
static float*    f2PrevU = nullptr;              // pre-solve snapshot, feeds the FLIP delta
static float*    f2PrevV = nullptr;
static float*    f2S = nullptr;                  // per-cell solidity: 0 = solid wall, 1 = open
static float*    f2ParticleDensity = nullptr;    // particles-per-cell field (drift control)
static float*    f2InvSSum = nullptr;            // O3: 1/(s-neighbor sum), precomputed at seed
static int8_t*   f2CellType = nullptr;           // FLUID / AIR / SOLID, rebuilt per substep
static uint16_t* f2FluidCells = nullptr;         // O4: compacted list of fluid cell indices
static float*    f2ParticlePos = nullptr;        // xy pairs
static float*    f2ParticleVel = nullptr;        // xy pairs
static uint16_t* f2NumCellParticles = nullptr;   // pushApart spatial hash: counts,
static uint16_t* f2FirstCellParticle = nullptr;  //   prefix-sum starts (+1 guard entry),
static uint16_t* f2CellParticleIds = nullptr;    //   and the particle ids themselves
static float*    f2Density = nullptr;            // render accumulators, one per LED
static float*    f2Foam = nullptr;
static float*    f2DepthAcc = nullptr;

// ── f2idx: float -> clamped int index ─────────────────────────
// The reference survives NaN positions only because JavaScript typed arrays
// silently DISCARD a write at index NaN. In C++, (int)floorf(NaN) is undefined
// behavior: a write through an arbitrary offset into a 30 kB arena, twice per
// substep. Every floor-to-index site below goes through this, keeping THAT SITE'S
// reference bounds (a uniform clamp would silently change the algorithm, e.g.
// inverting a bilinear pair against the solid border). NaN returns lo, which each
// site chooses so no derived index (like nr - offset) can leave its array.
static int f2idx(float v, int lo, int hi) {
  if (v != v) return lo;         // NaN is the only value for which (v != v) is true
  int i = (int)floorf(v);
  if (i < lo) return lo;
  if (i > hi) return hi;
  return i;
}

// ── f2LayoutArena: one source of truth for geometry + arena offsets ──
// Called two ways:
//   assign=false: compute the total byte size for a given interior (used once, at
//                 F2_K_MAX and 100% fill, to size the single allocation).
//   assign=true:  recompute geometry for the CURRENT K and point every f2* array
//                 at its slice of the arena (every reseed; the allocation never
//                 moves, only the layout inside it).
// Rule R7: heap_caps_malloc only guarantees the BASE pointer's alignment, and an
// unaligned float* load on Xtensa is a LoadStoreAlignment crash, not a compile
// error. So after every non-float region the offset is aligned up to 4 bytes.
// (At 8x8/K=2 the counts happen to be multiples of 4, so skipping this would work
// by luck and crash on the first non-square panel.)
static size_t f2LayoutArena(int iw, int ih, bool assign) {
  int nx = iw + 2;                       // +2: one solid border cell each side
  int ny = ih + 2;
  int fNumCells = nx * ny;

  // pushApart hash grid, per axis. The 1e-4f epsilon is load-bearing: at
  // 64x64/K=1, nx * f2PInvSpacing is exactly 100.0 and a float floorf() of an
  // exact integer can land on 99 (plan section 4).
  int pNumX = (int)floorf((float)nx * f2PInvSpacing + 1e-4f) + 1;
  int pNumY = (int)floorf((float)ny * f2PInvSpacing + 1e-4f) + 1;
  int pNumCells = pNumX * pNumY;

  // Particle capacity: the full seed lattice at 100% fill (nX*nY overstates the
  // staggered count by ceil(nY/2), which is exactly the slack we want). Must use
  // the SAME formula as seedFluid2() so capacity always covers the seed.
  const float dx0 = 2.0f * f2R;
  const float dy0 = 0.8660254f * dx0;    // sqrt(3)/2 * dx: hex close-packing row pitch
  int latX = max(2, (int)roundf(((float)iw - dx0) / dx0));   // R2: int/int
  int latY = max(2, (int)roundf(((float)ih - dx0) / dy0));
  int maxParticles = latX * latY;

  size_t off = 0;
  // Nine float fields, fNumCells each. Floats are 4 bytes so these stay aligned.
  size_t offU    = off;  off += (size_t)fNumCells * sizeof(float);
  size_t offV    = off;  off += (size_t)fNumCells * sizeof(float);
  size_t offDU   = off;  off += (size_t)fNumCells * sizeof(float);
  size_t offDV   = off;  off += (size_t)fNumCells * sizeof(float);
  size_t offPU   = off;  off += (size_t)fNumCells * sizeof(float);
  size_t offPV   = off;  off += (size_t)fNumCells * sizeof(float);
  size_t offS    = off;  off += (size_t)fNumCells * sizeof(float);
  size_t offPD   = off;  off += (size_t)fNumCells * sizeof(float);
  size_t offInvS = off;  off += (size_t)fNumCells * sizeof(float);
  // int8 region, then re-align (R7).
  size_t offCT   = off;  off += (size_t)fNumCells * sizeof(int8_t);
  off = (off + 3u) & ~3u;
  // uint16 region, then re-align before the float regions that follow.
  size_t offFC   = off;  off += (size_t)fNumCells * sizeof(uint16_t);
  off = (off + 3u) & ~3u;
  size_t offPPos = off;  off += (size_t)maxParticles * 2u * sizeof(float);
  size_t offPVel = off;  off += (size_t)maxParticles * 2u * sizeof(float);
  size_t offNCP  = off;  off += (size_t)pNumCells * sizeof(uint16_t);
  off = (off + 3u) & ~3u;
  size_t offFCP  = off;  off += ((size_t)pNumCells + 1u) * sizeof(uint16_t);  // +1 guard
  off = (off + 3u) & ~3u;
  size_t offCPI  = off;  off += (size_t)maxParticles * sizeof(uint16_t);
  off = (off + 3u) & ~3u;
  // Render accumulators, one float per LED.
  size_t offDen  = off;  off += (size_t)NUM_LEDS * sizeof(float);
  size_t offFoam = off;  off += (size_t)NUM_LEDS * sizeof(float);
  size_t offDep  = off;  off += (size_t)NUM_LEDS * sizeof(float);

  if (assign) {
    f2IW = iw;  f2IH = ih;  f2NX = nx;  f2NY = ny;  f2FNumCells = fNumCells;
    f2PNumX = pNumX;  f2PNumY = pNumY;  f2PNumCells = pNumCells;
    f2NumInteriorCells = iw * ih;
    f2MaxParticles = maxParticles;
    f2U     = (float*)  (f2Arena + offU);
    f2V     = (float*)  (f2Arena + offV);
    f2DU    = (float*)  (f2Arena + offDU);
    f2DV    = (float*)  (f2Arena + offDV);
    f2PrevU = (float*)  (f2Arena + offPU);
    f2PrevV = (float*)  (f2Arena + offPV);
    f2S     = (float*)  (f2Arena + offS);
    f2ParticleDensity   = (float*)   (f2Arena + offPD);
    f2InvSSum           = (float*)   (f2Arena + offInvS);
    f2CellType          = (int8_t*)  (f2Arena + offCT);
    f2FluidCells        = (uint16_t*)(f2Arena + offFC);
    f2ParticlePos       = (float*)   (f2Arena + offPPos);
    f2ParticleVel       = (float*)   (f2Arena + offPVel);
    f2NumCellParticles  = (uint16_t*)(f2Arena + offNCP);
    f2FirstCellParticle = (uint16_t*)(f2Arena + offFCP);
    f2CellParticleIds   = (uint16_t*)(f2Arena + offCPI);
    f2Density  = (float*)(f2Arena + offDen);
    f2Foam     = (float*)(f2Arena + offFoam);
    f2DepthAcc = (float*)(f2Arena + offDep);
  }
  return off;
}

// ── fluid2EnsureArena ─────────────────────────────────────────
// Allocate-once. Called by api_handlers BEFORE stopAll() so a failed allocation
// returns false (HTTP turns that into a 503, boot auto-resume into the rainbow
// fallback) instead of leaving a dark panel. Sized at F2_K_MAX and 100% fill
// because K and fill are live controls: sizing for the current K would overflow
// on the first K=2 POST into a K=1 arena.
// Internal DRAM first (O7: 3-4x PSRAM throughput, and the Gauss-Seidel sweep at
// stride f2NY floats is PSRAM's worst random-access case); PSRAM only when
// internal will not fit. The 8BIT flag is required alongside INTERNAL: INTERNAL
// alone does not guarantee byte addressability.
bool fluid2EnsureArena() {
  if (f2Arena != nullptr) return true;
  size_t need = f2LayoutArena(MATRIX_W * F2_K_MAX, MATRIX_H * F2_K_MAX, false);
  f2Arena = (uint8_t*)heap_caps_malloc(need, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  bool internal = (f2Arena != nullptr);
  if (f2Arena == nullptr) {
    f2Arena = (uint8_t*)heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  if (f2Arena == nullptr) {
    Serial.printf("fluid2: arena allocation FAILED (%u bytes); refusing to start\n",
                  (unsigned)need);
    return false;
  }
  Serial.printf("fluid2: arena %u bytes in %s\n", (unsigned)need,
                internal ? "internal DRAM" : "PSRAM");
  return true;
}

// ── seedFluid2 ────────────────────────────────────────────────
// Full reseed from the current params. Every knob change and the NaN recovery
// path route through here. The reference's constructor is NOT ported: it computes
// fNumX = floor(width/spacing)+1 and then silently redefines h, so asking for 16
// cells yields a 15x15 interior (plan section 10, deviation 1). We set the
// geometry directly.
void seedFluid2() {
  // (1) FIRST LINE, non-negotiable: clear the rest-density measurement gate.
  // It is measured once ever (gated on == 0.0 in f2UpdateParticleDensity), and
  // its only clearing site in the reference is the constructor we deleted. A
  // stale value makes drift compensation push a phantom force forever, silently.
  f2ParticleRestDensity = 0.0f;

  // (2) Gravity direction reset (plan section 4). Without this AND the static
  // initializer above, a board lying flat never exceeds the IMU dead zone, holds
  // a zeroed direction forever, and the panel shows one flat color.
  f2GXn = 0.0f;
  f2GYn = -1.0f;

  if (f2Arena == nullptr) { f2NumParticles = 0; f2AchievedFill = 0; return; }

  // (3) Geometry + arena layout for the CURRENT K (the allocation stays put).
  f2K = constrain(f2K, 1, F2_K_MAX);
  f2LayoutArena(MATRIX_W * f2K, MATRIX_H * f2K, true);

  // (4) Zero the dynamic fields. The arena is reused across reseeds, so stale
  // velocities from the previous tank would otherwise leak into the new one.
  memset(f2U,     0, (size_t)f2FNumCells * sizeof(float));
  memset(f2V,     0, (size_t)f2FNumCells * sizeof(float));
  memset(f2DU,    0, (size_t)f2FNumCells * sizeof(float));
  memset(f2DV,    0, (size_t)f2FNumCells * sizeof(float));
  memset(f2PrevU, 0, (size_t)f2FNumCells * sizeof(float));
  memset(f2PrevV, 0, (size_t)f2FNumCells * sizeof(float));
  memset(f2ParticleDensity, 0, (size_t)f2FNumCells * sizeof(float));
  memset(f2ParticleVel, 0, (size_t)f2MaxParticles * 2u * sizeof(float));
  f2NumFluidCells = 0;
  f2CompensateDrift = true;   // recomputed every substep; harmless default

  // (5) Tank walls: s = 0 (solid) on ALL FOUR borders. The reference leaves the
  // top row open (i==0 || i==fNumX-1 || j==0); we seal it so the tank survives a
  // full 360-degree tilt (plan section 9.5). Only s[] changes: the particle wall
  // clamp already covered the top.
  for (int i = 0; i < f2NX; i++) {
    for (int j = 0; j < f2NY; j++) {
      float s = 1.0f;   // fluid-capable
      if (i == 0 || i == f2NX - 1 || j == 0 || j == f2NY - 1) s = 0.0f;   // solid
      f2S[i * f2NY + j] = s;
    }
  }

  // (6) O3: precompute 1/(neighbor s-sum) per interior cell. The pressure loop's
  // hottest line divides by this sum in the reference; on Xtensa a float divide
  // is a 30-instruction ROM call, 10-30x a multiply. s[] only changes at reseed
  // (there are no interior obstacles), so the reciprocal is genuinely invariant.
  // A zero sum (all four neighbors solid, which this grid never produces) maps to
  // 0, exactly reproducing the reference's continue once the pressure array is gone.
  // LANDMINE: if an interior obstacle is ever added, invSSum must be recomputed
  // on every s[] change or the solve silently uses stale walls.
  for (int i = 0; i < f2NX; i++) {
    for (int j = 0; j < f2NY; j++) {
      int c = i * f2NY + j;
      if (i == 0 || i == f2NX - 1 || j == 0 || j == f2NY - 1) { f2InvSSum[c] = 0.0f; continue; }
      float sum = f2S[(i - 1) * f2NY + j] + f2S[(i + 1) * f2NY + j]
                + f2S[i * f2NY + j - 1]   + f2S[i * f2NY + j + 1];
      f2InvSSum[c] = (sum > 0.0f) ? (1.0f / sum) : 0.0f;
    }
  }

  // (7) Fill: round the REGION to whole sim rows, then rescale the lattice to
  // span it exactly. The reference's floor()-based seed throws the remainder
  // away: at 8x8/K=1/50% it actually fills 35%, a ragged half-lit surface row.
  // See plan section 5.
  int rows = (int)roundf((float)f2Fill * 0.01f * (float)f2IH);
  rows = constrain(rows, 0, f2IH);          // R2 note: constrain is a macro, all-int here
  if (rows == 0) { f2NumParticles = 0; f2AchievedFill = 0; return; }   // valid empty tank

  const float dx0 = 2.0f * f2R;             // 0.6f: touching-particle spacing
  const float dy0 = 0.8660254f * dx0;       // sqrt(3)/2 * dx: hex row pitch
  int nX = max(2, (int)roundf(((float)f2IW - dx0) / dx0));   // R2: int/int
  int nY = max(2, (int)roundf(((float)rows - dx0) / dy0));
  float dxE = ((float)f2IW - dx0) / (float)(nX - 1);         // exact span, not floor
  float dyE = ((float)rows - dx0) / (float)(nY - 1);

  // Lattice ORIGIN is h + r = 1.3f in cell units, matching the reference's seed.
  // Start at r instead and the whole tank seeds inside the solid border.
  // ODD ROWS GET ONE FEWER COLUMN: even rows span exactly [h+r, (nx-1)h-r], which
  // is exactly the wall clamp, so offsetting a full-length odd row by dxE/2 (or
  // by r, the reference's stagger) overruns the wall and frame 1 piles those
  // particles against it. One fewer column keeps the offset row's last particle
  // strictly inside. (Plan section 5; two reviewers proposed full-length offsets
  // and both overran.)
  int p = 0;
  for (int j = 0; j < nY; j++) {
    int   cols = (j & 1) ? (nX - 1) : nX;
    float offx = (j & 1) ? dxE * 0.5f : 0.0f;
    for (int i = 0; i < cols; i++) {
      f2ParticlePos[p++] = f2H + f2R + dxE * (float)i + offx;
      f2ParticlePos[p++] = f2H + f2R + dyE * (float)j;
    }
  }
  f2NumParticles = p / 2;
  f2AchievedFill = rows * 100 / f2IH;
}

// ============================================================
// THE SEVEN PORTED METHODS (plan build step 5). Function-local
// names deliberately match the reference so the port stays
// diffable against 18-flip.html. No structs cross any function
// boundary (R3): built-in types only.
// ============================================================

// ── integrateParticles (reference :143-150) ───────────────────
// Deviation: gravity is a VECTOR applied to both components, not a scalar on y.
// That is the point of having an IMU: tilt the board and "down" moves with it.
static void f2IntegrateParticles(float dt, float gX, float gY) {
  for (int i = 0; i < f2NumParticles; i++) {
    f2ParticleVel[2 * i]     += dt * gX;
    f2ParticleVel[2 * i + 1] += dt * gY;
    f2ParticlePos[2 * i]     += f2ParticleVel[2 * i] * dt;
    f2ParticlePos[2 * i + 1] += f2ParticleVel[2 * i + 1] * dt;
  }
}

// ── pushParticlesApart (reference :152-251) ───────────────────
// Gives the fluid its volume: any two particles closer than 2r get separated
// along their connecting line. A spatial hash (counting sort into cells of size
// 2.2r) keeps the neighbor search cheap. The reference's color-diffusion block
// is deleted with particleColor (we render a depth gradient, not per-particle
// colors). This is the single most expensive stage; O11 (a fast inverse sqrt)
// is deferred until measurement asks for it.
static void f2PushParticlesApart(int numIters) {
  // count particles per cell
  memset(f2NumCellParticles, 0, (size_t)f2PNumCells * sizeof(uint16_t));
  for (int i = 0; i < f2NumParticles; i++) {
    float x = f2ParticlePos[2 * i];
    float y = f2ParticlePos[2 * i + 1];
    int xi = f2idx(x * f2PInvSpacing, 0, f2PNumX - 1);   // the reference's own clamp bounds
    int yi = f2idx(y * f2PInvSpacing, 0, f2PNumY - 1);
    int cellNr = xi * f2PNumY + yi;   // hash index is x-major BY f2PNumY: per-axis, never square
    f2NumCellParticles[cellNr]++;
  }

  // partial sums (prefix), plus the guard entry at [pNumCells]
  int first = 0;
  for (int i = 0; i < f2PNumCells; i++) {
    first += f2NumCellParticles[i];
    f2FirstCellParticle[i] = (uint16_t)first;
  }
  f2FirstCellParticle[f2PNumCells] = (uint16_t)first;    // guard

  // fill particles into cells (counting sort, walking the starts backwards)
  for (int i = 0; i < f2NumParticles; i++) {
    float x = f2ParticlePos[2 * i];
    float y = f2ParticlePos[2 * i + 1];
    int xi = f2idx(x * f2PInvSpacing, 0, f2PNumX - 1);
    int yi = f2idx(y * f2PInvSpacing, 0, f2PNumY - 1);
    int cellNr = xi * f2PNumY + yi;
    f2FirstCellParticle[cellNr]--;
    f2CellParticleIds[f2FirstCellParticle[cellNr]] = (uint16_t)i;
  }

  // push particles apart
  float minDist = 2.0f * f2R;
  float minDist2 = minDist * minDist;

  for (int iter = 0; iter < numIters; iter++) {
    for (int i = 0; i < f2NumParticles; i++) {
      float px = f2ParticlePos[2 * i];
      float py = f2ParticlePos[2 * i + 1];

      // The reference leaves this floor UNGUARDED (section 7: "unguarded
      // upstream"); f2idx supplies the same bounds the two passes above use.
      int pxi = f2idx(px * f2PInvSpacing, 0, f2PNumX - 1);
      int pyi = f2idx(py * f2PInvSpacing, 0, f2PNumY - 1);
      int x0 = max(pxi - 1, 0);                 // R2: int/int at every min/max here
      int y0 = max(pyi - 1, 0);
      int x1 = min(pxi + 1, f2PNumX - 1);
      int y1 = min(pyi + 1, f2PNumY - 1);

      for (int xi = x0; xi <= x1; xi++) {
        for (int yi = y0; yi <= y1; yi++) {
          int cellNr = xi * f2PNumY + yi;
          int firstP = f2FirstCellParticle[cellNr];
          int last   = f2FirstCellParticle[cellNr + 1];
          for (int j = firstP; j < last; j++) {
            int id = f2CellParticleIds[j];
            if (id == i) continue;
            float qx = f2ParticlePos[2 * id];
            float qy = f2ParticlePos[2 * id + 1];

            float dx = qx - px;
            float dy = qy - py;
            float d2 = dx * dx + dy * dy;
            if (d2 > minDist2 || d2 == 0.0f) continue;
            float d = sqrtf(d2);
            float s = 0.5f * (minDist - d) / d;
            dx *= s;
            dy *= s;
            // px/py stay stale within this neighbor walk, exactly like the
            // reference: the separation is deliberately Jacobi-flavored.
            f2ParticlePos[2 * i]      -= dx;
            f2ParticlePos[2 * i + 1]  -= dy;
            f2ParticlePos[2 * id]     += dx;
            f2ParticlePos[2 * id + 1] += dy;
            // (reference :237-245, color diffusion: deleted with particleColor)
          }
        }
      }
    }
  }
}

// ── handleParticleCollisions (reference :253-311), walls only ─
// The mouse-driven obstacle is deleted (no pointer on an LED panel). What
// remains is the wall handling: position clamped to [h+r, (n-1)h-r] on each
// axis, and the velocity component INTO the wall zeroed. All four walls clamp
// symmetrically, which is why full inversion (plan section 9.5) just works.
static void f2HandleParticleCollisions() {
  float minX = f2H + f2R;
  float maxX = (float)(f2NX - 1) * f2H - f2R;
  float minY = f2H + f2R;
  float maxY = (float)(f2NY - 1) * f2H - f2R;

  for (int i = 0; i < f2NumParticles; i++) {
    float x = f2ParticlePos[2 * i];
    float y = f2ParticlePos[2 * i + 1];

    if (x < minX) { x = minX; f2ParticleVel[2 * i] = 0.0f; }
    if (x > maxX) { x = maxX; f2ParticleVel[2 * i] = 0.0f; }
    if (y < minY) { y = minY; f2ParticleVel[2 * i + 1] = 0.0f; }
    if (y > maxY) { y = maxY; f2ParticleVel[2 * i + 1] = 0.0f; }
    f2ParticlePos[2 * i] = x;
    f2ParticlePos[2 * i + 1] = y;
  }
}

// ── transferVelocities, particle -> grid (reference :382-497, toGrid half) ──
// Restructured per O5 and O6:
//   O6: the reference's `component` loop re-resolves u-vs-v pointers inside the
//       hottest particle loop; here every stencil addresses its arrays directly.
//   O5: the particle-density accumulation (reference updateParticleDensity
//       :313-346) shares this pass's position load and outer loop. It does NOT
//       share the index math: u offsets by (0, h/2), v by (h/2, 0), density by
//       (h/2, h/2). THREE distinct stencils; collapsing them splats density half
//       a cell wrong, mis-measures rest density, and pushes a phantom drift
//       force forever.
//   The solid-face restore is hoisted to a single post-pass, which is exact: the
//   reference runs it once per component, and the second run overwrites with the
//   same prevU/prevV values.
// Staggered MAC grid convention: u lives on cell LEFT faces, v on BOTTOM faces,
// which is why each component's stencil shifts by half a cell on the OTHER axis.
// f2idx lower bounds here are the natural range of the clamped input (identity
// for all finite values, pure NaN armor): the u stencil's x and the v stencil's
// y use lo=1 so the gather's nr-minus-offset lookup can never leave the array.
static void f2TransferToGrid() {
  int n = f2NY;                  // cell index = i * n + j (x-major, y-minor)
  const float h  = f2H;          // 1.0f: kept symbolic for diffability
  const float h1 = 1.0f / f2H;   // the reference's fInvSpacing
  const float h2 = 0.5f * f2H;

  memcpy(f2PrevU, f2U, (size_t)f2FNumCells * sizeof(float));
  memcpy(f2PrevV, f2V, (size_t)f2FNumCells * sizeof(float));
  memset(f2DU, 0, (size_t)f2FNumCells * sizeof(float));
  memset(f2DV, 0, (size_t)f2FNumCells * sizeof(float));
  memset(f2U,  0, (size_t)f2FNumCells * sizeof(float));
  memset(f2V,  0, (size_t)f2FNumCells * sizeof(float));
  memset(f2ParticleDensity, 0, (size_t)f2FNumCells * sizeof(float));   // O5: density rides along

  // Cell typing: solid walls stay SOLID, everything else starts AIR...
  for (int i = 0; i < f2FNumCells; i++) {
    f2CellType[i] = (f2S[i] == 0.0f) ? F2_SOLID_CELL : F2_AIR_CELL;
  }
  // ...and any cell holding a particle becomes FLUID. This defines the free
  // surface: AIR cells are skipped by the solve but still carry s = 1.
  for (int i = 0; i < f2NumParticles; i++) {
    float x = f2ParticlePos[2 * i];
    float y = f2ParticlePos[2 * i + 1];
    int xi = f2idx(x * h1, 0, f2NX - 1);   // the reference's own clamp at :405-406
    int yi = f2idx(y * h1, 0, f2NY - 1);
    int cellNr = xi * n + yi;
    if (f2CellType[cellNr] == F2_AIR_CELL) f2CellType[cellNr] = F2_FLUID_CELL;
  }

  // The O5 shared particle loop: three stencils per particle.
  for (int i = 0; i < f2NumParticles; i++) {
    float x = f2ParticlePos[2 * i];
    float y = f2ParticlePos[2 * i + 1];
    x = constrain(x, h, (float)(f2NX - 1) * h);   // R2: constrain double-evaluates; x is side-effect free
    y = constrain(y, h, (float)(f2NY - 1) * h);

    // u stencil, offset (0, h2).
    {
      int   x0 = f2idx(x * h1, 1, f2NX - 2);
      float tx = (x - (float)x0 * h) * h1;
      int   x1 = min(x0 + 1, f2NX - 2);            // R2: int/int
      int   y0 = f2idx((y - h2) * h1, 0, f2NY - 2);
      float ty = ((y - h2) - (float)y0 * h) * h1;
      int   y1 = min(y0 + 1, f2NY - 2);
      float sx = 1.0f - tx;
      float sy = 1.0f - ty;
      float d0 = sx * sy, d1 = tx * sy, d2 = tx * ty, d3 = sx * ty;
      int nr0 = x0 * n + y0, nr1 = x1 * n + y0, nr2 = x1 * n + y1, nr3 = x0 * n + y1;
      float pv = f2ParticleVel[2 * i];
      f2U[nr0] += pv * d0;  f2DU[nr0] += d0;
      f2U[nr1] += pv * d1;  f2DU[nr1] += d1;
      f2U[nr2] += pv * d2;  f2DU[nr2] += d2;
      f2U[nr3] += pv * d3;  f2DU[nr3] += d3;
    }

    // v stencil, offset (h2, 0).
    {
      int   x0 = f2idx((x - h2) * h1, 0, f2NX - 2);
      float tx = ((x - h2) - (float)x0 * h) * h1;
      int   x1 = min(x0 + 1, f2NX - 2);
      int   y0 = f2idx(y * h1, 1, f2NY - 2);
      float ty = (y - (float)y0 * h) * h1;
      int   y1 = min(y0 + 1, f2NY - 2);
      float sx = 1.0f - tx;
      float sy = 1.0f - ty;
      float d0 = sx * sy, d1 = tx * sy, d2 = tx * ty, d3 = sx * ty;
      int nr0 = x0 * n + y0, nr1 = x1 * n + y0, nr2 = x1 * n + y1, nr3 = x0 * n + y1;
      float pv = f2ParticleVel[2 * i + 1];
      f2V[nr0] += pv * d0;  f2DV[nr0] += d0;
      f2V[nr1] += pv * d1;  f2DV[nr1] += d1;
      f2V[nr2] += pv * d2;  f2DV[nr2] += d2;
      f2V[nr3] += pv * d3;  f2DV[nr3] += d3;
    }

    // density stencil, offset (h2, h2) on BOTH axes (reference :331-337).
    // x0/y0 are "free" upstream; these bounds equal the natural range of the
    // clamped position, so f2idx is identity for finite values.
    {
      int   x0 = f2idx((x - h2) * h1, 0, f2NX - 2);
      float tx = ((x - h2) - (float)x0 * h) * h1;
      int   x1 = min(x0 + 1, f2NX - 2);
      int   y0 = f2idx((y - h2) * h1, 0, f2NY - 2);
      float ty = ((y - h2) - (float)y0 * h) * h1;
      int   y1 = min(y0 + 1, f2NY - 2);
      float sx = 1.0f - tx;
      float sy = 1.0f - ty;
      // The reference's range guards are kept for diffability; after f2idx they
      // are always true.
      if (x0 < f2NX && y0 < f2NY) f2ParticleDensity[x0 * n + y0] += sx * sy;
      if (x1 < f2NX && y0 < f2NY) f2ParticleDensity[x1 * n + y0] += tx * sy;
      if (x1 < f2NX && y1 < f2NY) f2ParticleDensity[x1 * n + y1] += tx * ty;
      if (x0 < f2NX && y1 < f2NY) f2ParticleDensity[x0 * n + y1] += sx * ty;
    }
  }

  // Normalize the splats: each face velocity becomes the weighted mean of the
  // particle velocities that touched it (reference :479-483, both components).
  for (int i = 0; i < f2FNumCells; i++) {
    if (f2DU[i] > 0.0f) f2U[i] /= f2DU[i];
    if (f2DV[i] > 0.0f) f2V[i] /= f2DV[i];
  }

  // Restore solid cell faces (reference :487-495), single hoisted pass: any face
  // touching a solid cell keeps its pre-transfer velocity. This is the
  // no-penetration condition on the wall faces.
  for (int i = 0; i < f2NX; i++) {
    for (int j = 0; j < f2NY; j++) {
      bool solid = (f2CellType[i * n + j] == F2_SOLID_CELL);
      if (solid || (i > 0 && f2CellType[(i - 1) * n + j] == F2_SOLID_CELL))
        f2U[i * n + j] = f2PrevU[i * n + j];
      if (solid || (j > 0 && f2CellType[i * n + j - 1] == F2_SOLID_CELL))
        f2V[i * n + j] = f2PrevV[i * n + j];
    }
  }
}

// ── updateParticleDensity (reference :313-380), what remains of it ──
// The bilinear density accumulation moved into f2TransferToGrid (O5). Kept here,
// in one interior scan:
//   (a) O4: the compacted fluid-cell list, built i-major/j-minor to match the
//       reference's Gauss-Seidel sweep order (Gauss-Seidel is order-dependent; a
//       different sweep order is a different iteration). The solve walks this
//       list numIters times per substep instead of re-scanning the whole grid
//       and skipping non-fluid cells every pass.
//   (b) the one-time rest density measurement (reference :348-361, gated on
//       == 0.0f, cleared only by seedFluid2). Border cells are never FLUID, so
//       scanning the interior only is equivalent to the reference's full scan.
//   (c) the drift-compensation hysteresis (plan section 5): with (nearly) every
//       interior cell FLUID the compensation term makes the all-Neumann pressure
//       problem inconsistent, and over-relaxed Gauss-Seidel diverges to NaN. A
//       bare "< numInteriorCells" chatters as single particles drift between
//       cells and pops the surface; the 3% air band settles it.
static void f2UpdateParticleDensity() {
  int n = f2NY;
  int count = 0;
  float sum = 0.0f;
  for (int i = 1; i < f2NX - 1; i++) {
    for (int j = 1; j < f2NY - 1; j++) {
      int c = i * n + j;
      if (f2CellType[c] == F2_FLUID_CELL) {
        f2FluidCells[count] = (uint16_t)c;
        count++;
        sum += f2ParticleDensity[c];
      }
    }
  }
  f2NumFluidCells = count;

  if (f2ParticleRestDensity == 0.0f && count > 0) {
    f2ParticleRestDensity = sum / (float)count;
  }

  f2CompensateDrift = (f2NumFluidCells < (int)(f2NumInteriorCells * 0.97f));
}

// ── solveIncompressibility (reference :500-558) ───────────────
// Gauss-Seidel with over-relaxation, walking ONLY the compacted fluid-cell list
// (O4) and multiplying by the precomputed 1/s-sum (O3) instead of dividing.
// Deviations, all recorded in plan section 10:
//   - the pressure array p is deleted: the reference writes it and never reads
//     it back (it fed only the sci-color view), and deleting it also kills
//     cp = density*h/dt, so this function needs no dt at all;
//   - the dead loop at reference :509-512 is deleted (empty body: legal JS, an
//     unused-variable wart here);
//   - the compensateDrift = true default argument is dropped (R5: Arduino's
//     generated prototype repeats the default, a hard compile error) and the
//     flag is computed per substep instead;
//   - the reference's duplicate `var s` pair (:528 dead, :533 the neighbor sum,
//     a C++ redefinition error) vanishes entirely with O3: both fed the divide
//     that is now a precomputed multiply.
static void f2SolveIncompressibility(int numIters) {
  // Snapshot AFTER the transfer, BEFORE the solve: the FLIP delta handed back to
  // the particles must measure only the change the pressure solve made.
  memcpy(f2PrevU, f2U, (size_t)f2FNumCells * sizeof(float));
  memcpy(f2PrevV, f2V, (size_t)f2FNumCells * sizeof(float));

  int n = f2NY;
  const float overRelaxation = 1.9f;    // reference scene value
  bool drift = f2CompensateDrift && (f2ParticleRestDensity > 0.0f);

  for (int iter = 0; iter < numIters; iter++) {
    for (int fc = 0; fc < f2NumFluidCells; fc++) {
      int center = f2FluidCells[fc];
      int left   = center - n;
      int right  = center + n;
      int bottom = center - 1;
      int top    = center + 1;

      // Divergence: net outflow of this cell. The solve pushes it toward zero.
      float div = f2U[right] - f2U[center] + f2V[top] - f2V[center];

      // Drift compensation: where particles have bunched past rest density, fake
      // a little extra outflow so the pressure solve pushes them apart again.
      if (drift) {
        float k = 1.0f;   // stiffness, reference value
        float compression = f2ParticleDensity[center] - f2ParticleRestDensity;
        if (compression > 0.0f) div = div - k * compression;
      }

      float p = -div * f2InvSSum[center];   // O3: was -div / (sx0+sx1+sy0+sy1)
      p *= overRelaxation;

      f2U[center] -= f2S[left]   * p;
      f2U[right]  += f2S[right]  * p;
      f2V[center] -= f2S[bottom] * p;
      f2V[top]    += f2S[top]    * p;
    }
  }
}

// ── transferVelocities, grid -> particle (reference :382-497, fromGrid half) ──
// O6: split into two specialized component routines so the u/prevU and v/prevV
// pointers stay in registers instead of being re-selected per particle. The
// reference's inner `var d` (the valid-weight sum, :465) shadowed its outer
// du/dv pointer (:420), a C++ redefinition error; the split makes the shadow
// moot, so `d` keeps its reference spelling.
// A face's value is trusted (valid) only if at least one of the two cells
// flanking it is non-air; invalid corners drop out and the weights renormalize.
// Each routine uses the SAME stencil as its toGrid splat: gather must agree
// with splat or the transfer is between two different fields.
static void f2TransferFromGridU(float flipRatio) {
  int n = f2NY;
  const float h  = f2H;
  const float h1 = 1.0f / f2H;
  const float h2 = 0.5f * f2H;

  for (int i = 0; i < f2NumParticles; i++) {
    float x = f2ParticlePos[2 * i];
    float y = f2ParticlePos[2 * i + 1];
    x = constrain(x, h, (float)(f2NX - 1) * h);
    y = constrain(y, h, (float)(f2NY - 1) * h);

    int   x0 = f2idx(x * h1, 1, f2NX - 2);   // lo=1 keeps nr - n in range even on NaN
    float tx = (x - (float)x0 * h) * h1;
    int   x1 = min(x0 + 1, f2NX - 2);
    int   y0 = f2idx((y - h2) * h1, 0, f2NY - 2);
    float ty = ((y - h2) - (float)y0 * h) * h1;
    int   y1 = min(y0 + 1, f2NY - 2);
    float sx = 1.0f - tx;
    float sy = 1.0f - ty;
    float d0 = sx * sy, d1 = tx * sy, d2 = tx * ty, d3 = sx * ty;
    int nr0 = x0 * n + y0, nr1 = x1 * n + y0, nr2 = x1 * n + y1, nr3 = x0 * n + y1;

    // offset = n for u: a u face's flanking cells sit one COLUMN apart.
    float valid0 = (f2CellType[nr0] != F2_AIR_CELL || f2CellType[nr0 - n] != F2_AIR_CELL) ? 1.0f : 0.0f;
    float valid1 = (f2CellType[nr1] != F2_AIR_CELL || f2CellType[nr1 - n] != F2_AIR_CELL) ? 1.0f : 0.0f;
    float valid2 = (f2CellType[nr2] != F2_AIR_CELL || f2CellType[nr2 - n] != F2_AIR_CELL) ? 1.0f : 0.0f;
    float valid3 = (f2CellType[nr3] != F2_AIR_CELL || f2CellType[nr3 - n] != F2_AIR_CELL) ? 1.0f : 0.0f;

    float v = f2ParticleVel[2 * i];
    float d = valid0 * d0 + valid1 * d1 + valid2 * d2 + valid3 * d3;

    if (d > 0.0f) {
      // PIC: resample the grid outright (max damping). FLIP: keep the particle's
      // velocity, add only the grid's pressure-solve CORRECTION (max liveliness).
      float picV = (valid0 * d0 * f2U[nr0] + valid1 * d1 * f2U[nr1]
                  + valid2 * d2 * f2U[nr2] + valid3 * d3 * f2U[nr3]) / d;
      float corr = (valid0 * d0 * (f2U[nr0] - f2PrevU[nr0]) + valid1 * d1 * (f2U[nr1] - f2PrevU[nr1])
                  + valid2 * d2 * (f2U[nr2] - f2PrevU[nr2]) + valid3 * d3 * (f2U[nr3] - f2PrevU[nr3])) / d;
      float flipV = v + corr;
      f2ParticleVel[2 * i] = (1.0f - flipRatio) * picV + flipRatio * flipV;
    }
  }
}

// The v twin: stencil offset (h2, 0), flanking cells one ROW apart (offset = 1).
static void f2TransferFromGridV(float flipRatio) {
  int n = f2NY;
  const float h  = f2H;
  const float h1 = 1.0f / f2H;
  const float h2 = 0.5f * f2H;

  for (int i = 0; i < f2NumParticles; i++) {
    float x = f2ParticlePos[2 * i];
    float y = f2ParticlePos[2 * i + 1];
    x = constrain(x, h, (float)(f2NX - 1) * h);
    y = constrain(y, h, (float)(f2NY - 1) * h);

    int   x0 = f2idx((x - h2) * h1, 0, f2NX - 2);
    float tx = ((x - h2) - (float)x0 * h) * h1;
    int   x1 = min(x0 + 1, f2NX - 2);
    int   y0 = f2idx(y * h1, 1, f2NY - 2);   // lo=1 keeps nr - 1 in range even on NaN
    float ty = (y - (float)y0 * h) * h1;
    int   y1 = min(y0 + 1, f2NY - 2);
    float sx = 1.0f - tx;
    float sy = 1.0f - ty;
    float d0 = sx * sy, d1 = tx * sy, d2 = tx * ty, d3 = sx * ty;
    int nr0 = x0 * n + y0, nr1 = x1 * n + y0, nr2 = x1 * n + y1, nr3 = x0 * n + y1;

    float valid0 = (f2CellType[nr0] != F2_AIR_CELL || f2CellType[nr0 - 1] != F2_AIR_CELL) ? 1.0f : 0.0f;
    float valid1 = (f2CellType[nr1] != F2_AIR_CELL || f2CellType[nr1 - 1] != F2_AIR_CELL) ? 1.0f : 0.0f;
    float valid2 = (f2CellType[nr2] != F2_AIR_CELL || f2CellType[nr2 - 1] != F2_AIR_CELL) ? 1.0f : 0.0f;
    float valid3 = (f2CellType[nr3] != F2_AIR_CELL || f2CellType[nr3 - 1] != F2_AIR_CELL) ? 1.0f : 0.0f;

    float v = f2ParticleVel[2 * i + 1];
    float d = valid0 * d0 + valid1 * d1 + valid2 * d2 + valid3 * d3;

    if (d > 0.0f) {
      float picV = (valid0 * d0 * f2V[nr0] + valid1 * d1 * f2V[nr1]
                  + valid2 * d2 * f2V[nr2] + valid3 * d3 * f2V[nr3]) / d;
      float corr = (valid0 * d0 * (f2V[nr0] - f2PrevV[nr0]) + valid1 * d1 * (f2V[nr1] - f2PrevV[nr1])
                  + valid2 * d2 * (f2V[nr2] - f2PrevV[nr2]) + valid3 * d3 * (f2V[nr3] - f2PrevV[nr3])) / d;
      float flipV = v + corr;
      f2ParticleVel[2 * i + 1] = (1.0f - flipRatio) * picV + flipRatio * flipV;
    }
  }
}

// ── updateParticleColors (reference :560-599), reduced to the spray test ──
// The per-particle RGB state and its diffusion are deleted (we render a depth
// gradient, not particle colors). What survives is the one physical insight in
// that method: a particle sitting in a cell whose density is well under rest
// density (< 0.7x) is AIRBORNE fluid, i.e. spray. The render splats this flag
// into the foam accumulator and whitens accordingly. Density-based, so it is
// orientation-free for free (section 9.5).
static float f2ParticleSpray(float x, float y) {
  if (f2ParticleRestDensity <= 0.0f) return 0.0f;   // reference gates on d0 > 0
  // h = 1, so the position IS the cell coordinate; the reference's own clamp
  // bounds ([1, n-1], section 7 table) apply.
  int xi = f2idx(x, 1, f2NX - 1);
  int yi = f2idx(y, 1, f2NY - 1);
  int cellNr = xi * f2NY + yi;
  float relDensity = f2ParticleDensity[cellNr] / f2ParticleRestDensity;
  return (relDensity < 0.7f) ? 1.0f : 0.0f;
}

// ============================================================
// FRAME DRIVER + RENDER
// ============================================================

// ── stepFluid2Frame ───────────────────────────────────────────
// One rendered frame: read the IMU once (O8), run 1-2 substeps of FIXED
// dt = 1/60 (a variable dt into a pressure solve is how these diverge; at the
// 33 ms frame interval, 2 substeps advance 33.3 ms of sim time per 33 ms of
// wall clock, i.e. real time, and 1 substep is deliberate half-speed), check
// for NaN, render, and keep the frame-time books for the O10 degrade ladder.
void stepFluid2Frame() {
  if (f2Arena == nullptr || f2FNumCells == 0) return;   // not allocated / not seeded

  uint32_t tFrame0 = micros();

  // ── Gravity direction, once per frame (plan section 4) ──────
  // Accelerometer (g-units, board axes) -> gravity direction (SOLVER frame, y UP).
  // x mapping and the -ay sign are liquid's, CALIBRATED ON HARDWARE 2026-06-08.
  // y is NEGATED on top of that: the board's +y is down, the solver's +y is up.
  float ax, ay;
  if (imuReady) readAccelXY(ax, ay);          // R9: the new XY-only reader, Z not read
  else          { ax = 0.0f; ay = 0.0f; }     // dead IMU: the dead zone holds the default
  float gxRaw = -ay;
  float gyRaw = -ax;
  float mag   = sqrtf(gxRaw * gxRaw + gyRaw * gyRaw);
  if (mag > 0.08f) {                          // liquid's dead zone, on the INSTANTANEOUS magnitude
    f2GXn += (gxRaw / mag - f2GXn) * 0.30f;   // low-pass the normalized direction
    f2GYn += (gyRaw / mag - f2GYn) * 0.30f;
  }
  // else: hold the last direction (board near flat, in-plane direction is noise)

  // Gravity vector handed to integrateParticles, with the user's scale knob.
  // Magnitude is calibrated to tank HEIGHT (3.27 cells/s^2 per cell of height)
  // so the settle feel is panel-relative; 0 = zero-g.
  float gMag = 3.27f * (float)f2IH * f2GravityScale;
  float gX   = f2GXn * gMag;
  float gY   = f2GYn * gMag;

  // ── Substeps ────────────────────────────────────────────────
  const float dt = 1.0f / 60.0f;              // FIXED. Never scaled to the frame interval.
  const int numParticleIters = 2;             // reference scene value
  int substeps = constrain(f2Substeps, 1, 2);
  int iters    = constrain(f2Iters, 5, 60);

  uint32_t tSolve0 = micros();
  for (int step = 0; step < substeps; step++) {
    f2IntegrateParticles(dt, gX, gY);
    f2PushParticlesApart(numParticleIters);
    f2HandleParticleCollisions();
    f2TransferToGrid();
    f2UpdateParticleDensity();
    f2SolveIncompressibility(iters);
    f2TransferFromGridU(f2FlipRatio);
    f2TransferFromGridV(f2FlipRatio);
  }
  uint32_t solverThisUs = micros() - tSolve0;

  // ── NaN diagnostic (plan section 7) ─────────────────────────
  // The f2idx clamps PROTECT the arena; this line REPORTS. One sum per frame:
  // NaN is infectious, so any diverged particle poisons the total.
  float xsum = 0.0f;
  for (int i = 0; i < f2NumParticles; i++) xsum += f2ParticlePos[2 * i];
  if (isnan(xsum)) {
    Serial.printf("fluid2: NaN! config K=%d fill=%d iters=%d substeps=%d flip=%.2f grav=%.2f particles=%d; reseeding\n",
                  f2K, f2Fill, f2Iters, f2Substeps,
                  (double)f2FlipRatio, (double)f2GravityScale, f2NumParticles);
    seedFluid2();
  }

  f2Render();

  // ── Frame-time books + degrade ladder (O10, plan section 12) ──
  // Brackets the WHOLE frame, not just the solver: readAccelXY plus render
  // together rival the optimized solver, and measuring only the solver would
  // report success while the frame blows the budget. The accumulators feed the
  // ladder, so they ship.
  static uint32_t accumUs = 0, solverUs = 0, worstUs = 0, frames = 0;
  static uint32_t lastReportMs = 0;
  uint32_t frameUs = micros() - tFrame0;
  accumUs  += frameUs;
  solverUs += solverThisUs;
  frames++;
  if (frameUs > worstUs) worstUs = frameUs;

  uint32_t nowMs = millis();
  if (lastReportMs == 0) lastReportMs = nowMs;   // first frame: start the 5 s window HERE, not at boot, so one cold frame never triggers the ladder
  if (nowMs - lastReportMs >= 5000 && frames > 0) {
    uint32_t meanUs = accumUs / frames;
    // SCAFFOLDING: remove before PR (this report print only; the O10 ladder
    // below ships and keeps the measurement above as its trigger).
    Serial.printf("fluid2: frame mean %lu us (solver %lu us), worst %lu us over %lu frames\n",
                  (unsigned long)meanUs, (unsigned long)(solverUs / frames),
                  (unsigned long)worstUs, (unsigned long)frames);

    // O10: degrade in order when the 33 ms budget is blown, announcing each step:
    // iterations first, then detail, then honesty about dropped frames.
    if (meanUs > 33000u) {
      if (f2Iters > 5) {
        f2Iters = max(5, f2Iters / 2);     // R2: int/int
        Serial.printf("fluid2: over budget, reducing iters to %d\n", f2Iters);
      } else if (f2K > 1) {
        f2K = 1;
        Serial.println("fluid2: over budget, dropping K to 1 (reseeding)");
        seedFluid2();
      } else {
        Serial.println("fluid2: over budget at minimum config; accepting dropped frames");
      }
    }
    accumUs = 0; solverUs = 0; worstUs = 0; frames = 0;
    lastReportMs = nowMs;
  }
}

// ── f2Render (plan section 9) ─────────────────────────────────
// Bilinear splat of every particle into three per-LED accumulators (mass, spray,
// depth), then one coloring pass. Carries no literal 8 anywhere: the translation
// boundary lives in particle coordinates and MATRIX_W/MATRIX_H.
static void f2Render() {
  memset(f2Density,  0, (size_t)NUM_LEDS * sizeof(float));
  memset(f2Foam,     0, (size_t)NUM_LEDS * sizeof(float));
  memset(f2DepthAcc, 0, (size_t)NUM_LEDS * sizeof(float));

  // Depth is measured along the GRAVITY axis (screen-y "deeper is darker"
  // inverts the moment the board does) and normalized against the four FIXED
  // interior tank corners, not the particle extremes: one droplet flung six
  // cells into the air must not compress every other particle's depth and make
  // the whole pool pulse. Corner projection also stays defined at fill = 0.
  float cxLo = 1.0f, cxHi = (float)(f2NX - 1);
  float cyLo = 1.0f, cyHi = (float)(f2NY - 1);
  float c0 = cxLo * f2GXn + cyLo * f2GYn;
  float c1 = cxHi * f2GXn + cyLo * f2GYn;
  float c2 = cxLo * f2GXn + cyHi * f2GYn;
  float c3 = cxHi * f2GXn + cyHi * f2GYn;
  f2ProjMin = min(min(c0, c1), min(c2, c3));      // R2: float/float
  f2ProjMax = max(max(c0, c1), max(c2, c3));
  // The 1e-3f floor is not optional: f2GXn/f2GYn are a lerp of unit vectors and
  // never renormalized, so during a fast rotation their magnitude can shrink and
  // collapse the corner projections onto each other. A 0/0 here is a NaN the
  // section 7 diagnostic never sees (it only sums particle positions).
  float span    = max(f2ProjMax - f2ProjMin, 1e-3f);   // R2: float/float
  float invSpan = 1.0f / span;                         // one divide per frame, not per particle

  // Panel mapping factors, hoisted out of the loop (pure algebra on section 9's
  // per-particle divides).
  float kx = (float)MATRIX_W / (float)f2IW;
  float ky = (float)MATRIX_H / (float)f2IH;

  for (int i = 0; i < f2NumParticles; i++) {
    float px = f2ParticlePos[2 * i];
    float py = f2ParticlePos[2 * i + 1];

    // Particle coords -> panel coords. The interior starts at cell 1, and y
    // flips from the solver's y-UP to the panel's y-DOWN. Verified symmetric:
    // the wall clamps land at 0.150 and MATRIX-0.150, the inset being exactly
    // the particle radius.
    float sx = (px - 1.0f) * kx;
    float sy = ((float)(f2IH + 1) - py) * ky;

    // Depth along gravity: 0 = the surface side of the tank, 1 = deep.
    float proj  = px * f2GXn + py * f2GYn;
    float depth = constrain((proj - f2ProjMin) * invSpan, 0.0f, 1.0f);

    float spray = f2ParticleSpray(px, py);

    // CORNER-convention bilinear splat (LED x covers [x, x+1)): conserves mass
    // at both walls with no drop rule. A pixel-CENTRE convention would put
    // x0 = -1 at the resting wall and either fold a third of the particle onto
    // a hot rim (clamp) or lose it (dead rim, drop), along the entire bottom row.
    int   x0 = f2idx(sx, 0, MATRIX_W - 1);
    int   x1 = min(x0 + 1, MATRIX_W - 1);     // R2: int/int
    float tx = sx - (float)x0;
    int   y0 = f2idx(sy, 0, MATRIX_H - 1);
    int   y1 = min(y0 + 1, MATRIX_H - 1);
    float ty = sy - (float)y0;

    float w00 = (1.0f - tx) * (1.0f - ty);
    float w10 = tx * (1.0f - ty);
    float w11 = tx * ty;
    float w01 = (1.0f - tx) * ty;
    int k00 = y0 * MATRIX_W + x0;
    int k10 = y0 * MATRIX_W + x1;
    int k11 = y1 * MATRIX_W + x1;
    int k01 = y1 * MATRIX_W + x0;

    f2Density[k00] += w00;  f2Foam[k00] += spray * w00;  f2DepthAcc[k00] += depth * w00;
    f2Density[k10] += w10;  f2Foam[k10] += spray * w10;  f2DepthAcc[k10] += depth * w10;
    f2Density[k11] += w11;  f2Foam[k11] += spray * w11;  f2DepthAcc[k11] += depth * w11;
    f2Density[k01] += w01;  f2Foam[k01] += spray * w01;  f2DepthAcc[k01] += depth * w01;
  }

  // Per-LED coloring pass.
  float foamGain = f2FoamGain * 0.1f;   // the 0..10 "Splash" knob -> 0..1
  // Density -> brightness ramp, simulation-tuned at K=2 (full at density 10,
  // cutoff 1.0) and scaled by K*K because particles-per-LED scales with cell
  // area: without the K*K term, K=1 panels would render everything dim.
  // These two are the calibration knobs if the look needs tuning on hardware.
  float full    = 2.5f * (float)(f2K * f2K);
  float cut     = 0.25f * (float)(f2K * f2K);
  float invFull = 1.0f / full;
  for (int y = 0; y < MATRIX_H; y++) {
    for (int x = 0; x < MATRIX_W; x++) {
      int k = y * MATRIX_W + x;
      // Explicit empty branch instead of dividing: 0/0 is a NaN the frame
      // diagnostic does not watch for. A barely-touched pixel must stay dark,
      // not just a strictly-empty one; full-brightness-on-any-touch was the
      // whole-panel-lights-up bug.
      if (f2Density[k] < cut) { setPixel(x, y, CRGB::Black); continue; }

      float inv     = 1.0f / f2Density[k];
      float depth01 = f2DepthAcc[k] * inv;               // 0 = surface, 1 = deep
      float spray   = f2Foam[k] * inv * foamGain;

      // Color rides liquid's globals and path: no new color globals, no second
      // depth lerp. (The fluid2 param block in api_handlers must set all three,
      // including liquidGradient, or a warm switch from liquid inherits stale
      // state; that wiring is plan step 8.)
      CRGB col;
      if (liquidGradient) {
        // Custom pair: lerp deep -> top by surface proximity (s = 1 at surface).
        float s = 1.0f - depth01;
        col = CRGB(
          liquidBottomColor.r + (int)((liquidTopColor.r - liquidBottomColor.r) * s),
          liquidBottomColor.g + (int)((liquidTopColor.g - liquidBottomColor.g) * s),
          liquidBottomColor.b + (int)((liquidTopColor.b - liquidBottomColor.b) * s));
      } else {
        // Palette path (fire's activePalette): surface bright, deep darker.
        uint8_t h = (uint8_t)(210.0f - depth01 * 100.0f);
        col = heatToColor(h);
      }

      // Spray whitening: airborne fluid reads as foam.
      uint8_t f = (uint8_t)(constrain(spray, 0.0f, 1.0f) * 255.0f);
      col.r = qadd8(col.r, f);
      col.g = qadd8(col.g, f);
      col.b = qadd8(col.b, f);

      // Scale the whole colour by how much fluid is actually here. A pixel a
      // splash grazes renders as a dim droplet, not a solid block of water.
      float bri01 = min(f2Density[k] * invFull, 1.0f);
      col.nscale8((uint8_t)(bri01 * 255.0f));

      // Near-black floors to true off: a WS2812B renders 1-2/255 as flickery
      // noise-colored specks, and the calibration layer lifts any nonzero
      // channel, so "almost off" must become exactly off.
      if (col.r < 3 && col.g < 3 && col.b < 3) col = CRGB::Black;

      setPixel(x, y, col);
    }
  }
}

#pragma GCC diagnostic pop
