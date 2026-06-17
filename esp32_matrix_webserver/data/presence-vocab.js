// presence-vocab.js — the desktop card's intent → appearance table.
// ESM so the browser (presence-card.html) and the node parity test both import it.
// Keys MUST stay in sync with INTENTS in mcp_server/presence.ts (parity test enforces).
// glyph = a single character drawn large; color = CSS hex; motion = CSS animation key.
export const PRESENCE_VOCAB = {
  working:   { label: "Working",   glyph: "◐", color: "#e0a020", motion: "pulse" },   // ◐
  thinking:  { label: "Thinking",  glyph: "…", color: "#3a78d0", motion: "shimmer" }, // …
  done:      { label: "Done",      glyph: "✓", color: "#33c06a", motion: "settle" },  // ✓
  ok:        { label: "OK",        glyph: "✓", color: "#33c06a", motion: "none" },    // ✓
  celebrate: { label: "Celebrate", glyph: "✦", color: "#d24bd2", motion: "burst" },   // ✦
  alert:     { label: "Needs you", glyph: "!",      color: "#e0a020", motion: "blink" },
  error:     { label: "Error",     glyph: "✗", color: "#e0473c", motion: "blink" },   // ✗
  question:  { label: "Question",  glyph: "?",      color: "#3a78d0", motion: "pulse" },
  info:      { label: "Info",      glyph: "i",      color: "#7a8aa0", motion: "none" },
  idle:      { label: "Idle",      glyph: "z",      color: "#46506a", motion: "breathe" },
};
