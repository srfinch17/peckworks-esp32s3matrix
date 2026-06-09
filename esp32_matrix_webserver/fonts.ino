// ============================================================
// fonts.ino — Two-line pixel font system for the 8×8 LED matrix
//
// Two fonts, both 3 pixels wide:
//   FONT_3X3  — 3 rows tall   (user-defined pixel art)
//   FONT_3X5  — 5 rows tall   (designed to complement 3×3)
//
// Two-line layouts (3 + 5 = 8 rows, no gap needed):
//   Layout A: 3×3 TOP,    3×5 BOTTOM  → rowOffset 3×3=0,  3×5=3
//   Layout B: 3×5 TOP,    3×3 BOTTOM  → rowOffset 3×5=0,  3×3=5
//
// Character index mapping (both arrays use same order):
//   'A'–'Z' / 'a'–'z'  →  0–25
//   '0'–'9'             →  26–35
//   ' ' / unknown       →  36
//
// Bit encoding — 3 bytes per char (col0, col1, col2):
//   3×3: bit0 = top row,  bit2 = bottom row
//   3×5: bit0 = top row,  bit4 = bottom row
// ============================================================

#define FONT_3X3_H    3
#define FONT_3X5_H    5
#define FONT_CHAR_W   3   // all chars 3 pixels wide
#define FONT_CHAR_GAP 1   // 1-pixel gap between chars; stride = 4

// Row offsets for the two two-line layouts
#define ROW_3X3_TOP    0   // 3×3 occupies rows 0–2, 3×5 occupies rows 3–7
#define ROW_3X5_BOTTOM 3
#define ROW_3X5_TOP    0   // 3×5 occupies rows 0–4, 3×3 occupies rows 5–7
#define ROW_3X3_BOTTOM 5

// Maps a char to a font array index. Lowercase treated as uppercase.
// A–Z/a–z → 0–25, 0–9 → 26–35, ' ' → 36, . , : ; ? ! → 37–42, unknown → 36
int fontIdx(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a';
  if (c >= '0' && c <= '9') return 26 + (c - '0');
  if (c == '.') return 37;
  if (c == ',') return 38;
  if (c == ':') return 39;
  if (c == ';') return 40;
  if (c == '?') return 41;
  if (c == '!') return 42;
  return 36;
}

// Returns the starting column to center numChars characters in 8 columns.
// Stride per char = 4 (3 wide + 1 gap), last char has no trailing gap.
int fontCenterCol(int numChars) {
  if (numChars <= 0) return 0;
  int totalW = numChars * FONT_CHAR_W + (numChars - 1) * FONT_CHAR_GAP;
  return max(0, (MATRIX_W - totalW) / 2);
}

// ── 3×3 Font (user-defined) ────────────────────────────────────
// bit0 = top row (row 0), bit2 = bottom row (row 2)
const uint8_t FONT_3X3[43][3] PROGMEM = {
  // A – Z  (indices 0–25)
  {6, 1, 6},   // A  . # .  /  # . #  /  # . #
  {7, 7, 6},   // B  # # .  /  # # #  /  # # #
  {7, 5, 5},   // C  # # #  /  # . .  /  # # #
  {7, 5, 2},   // D  # # .  /  # . #  /  # # .
  {7, 7, 5},   // E  # # #  /  # # .  /  # # #
  {7, 3, 1},   // F  # # #  /  # # .  /  # . .
  {7, 5, 6},   // G  # # .  /  # . #  /  # # #
  {7, 2, 7},   // H  # . #  /  # # #  /  # . #
  {5, 7, 5},   // I  # # #  /  . # .  /  # # #
  {6, 4, 7},   // J  . . #  /  # . #  /  # # #
  {7, 2, 5},   // K  # . #  /  # # .  /  # . #
  {7, 4, 4},   // L  # . .  /  # . .  /  # # #
  {7, 3, 7},   // M  # # #  /  # # #  /  # . #
  {7, 1, 7},   // N  # # #  /  # . #  /  # . #
  {7, 5, 7},   // O  # # #  /  # . #  /  # # #
  {7, 3, 3},   // P  # # #  /  # # #  /  # . .
  {3, 3, 7},   // Q  # # #  /  # # #  /  . . #
  {7, 1, 1},   // R  # # #  /  # . .  /  # . .
  {4, 7, 1},   // S  . # #  /  . # .  /  # # .
  {1, 7, 1},   // T  # # #  /  . # .  /  . # .
  {7, 4, 7},   // U  # . #  /  # . #  /  # # #
  {3, 4, 3},   // V  # . #  /  # . #  /  . # .
  {7, 6, 7},   // W  # . #  /  # # #  /  # # #
  {5, 2, 5},   // X  # . #  /  . # .  /  # . #
  {1, 6, 1},   // Y  # . #  /  . # .  /  . # .
  {1, 7, 4},   // Z  # # .  /  . # .  /  . # #
  // 0 – 9  (indices 26–35)
  {7, 5, 7},   // 0  # # #  /  # . #  /  # # #
  {5, 7, 4},   // 1  # # .  /  . # .  /  # # #
  {1, 7, 4},   // 2  # # .  /  . # .  /  . # #
  {5, 7, 7},   // 3  # # #  /  . # #  /  # # #
  {3, 2, 7},   // 4  # . #  /  # # #  /  . . #
  {4, 7, 1},   // 5  . # #  /  . # .  /  # # .
  {7, 6, 6},   // 6  # . .  /  # # #  /  # # #
  {1, 1, 7},   // 7  # # #  /  . . #  /  . . #
  {7, 7, 7},   // 8  # # #  /  # # #  /  # # #
  {3, 3, 7},   // 9  # # #  /  # # #  /  . . #
  // space (index 36)
  {0, 0, 0},   // ' '
  // . , : ; ? ! (indices 37–42)
  {4, 0, 0},   // .   bottom-left pixel only
  {4, 2, 0},   // ,   bottom-left + mid-center
  {0, 5, 0},   // :   top + bottom of center col
  {4, 5, 0},   // ;   top + bottom center, plus bottom-left
  {0, 5, 3},   // ?   center col top+bottom, right col top+mid (dot is dimmed — see LIGHT mask)
  {0, 7, 0},   // !   center col all three rows (dot is dimmed — see LIGHT mask)
};

// Light-color mask for FONT_3X3: bits matching FONT_3X3 but drawn at 50% brightness.
// Only '?' (idx 41) and '!' (idx 42) have non-zero entries.
const uint8_t FONT_3X3_LIGHT[43][3] PROGMEM = {
  // A–Z (0–25)
  {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},
  {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},
  {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},
  {0,0,0},{0,0,0},
  // 0–9 (26–35)
  {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},
  {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},
  // space, . , : ; (36–40)
  {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},
  // ? (41): col1 bit2 (bottom center) is the dim dot
  {0, 4, 0},
  // ! (42): col1 bit2 (bottom center) is the dim dot
  {0, 4, 0},
};

// ── 3×5 Font ───────────────────────────────────────────────────
// bit0 = top row (row 0), bit4 = bottom row (row 4)
// Digits 0–9 match the existing MINI_FONT in clock_timer.ino.
const uint8_t FONT_3X5[37][3] PROGMEM = {
  // A – Z  (indices 0–25)
  {31,  5, 31},  // A  ###/# #/###/# #/# #
  {31, 21, 10},  // B  ##./# #/##./# #/##.
  {31, 17, 17},  // C  ###/#../#../#../###
  {31, 17, 14},  // D  ##./# #/# #/# #/##.
  {31, 21, 17},  // E  ###/#../##./#../###
  {31,  5,  1},  // F  ###/#../##./#../#..
  {31, 17, 29},  // G  ###/#../#  #/# #/###  (# in col2 rows 2-4)
  {31,  4, 31},  // H  # #/# #/###/# #/# #
  {17, 31, 17},  // I  ###/.#./.#./.#./###
  {24, 16, 15},  // J  ..#/..#/..#/# #/##.
  {31, 10, 17},  // K  # #/##./#../##./# #
  {31, 16, 16},  // L  #../#../#../#../###
  {31,  3, 31},  // M  ###/###/# #/# #/# #
  {31,  2, 31},  // N  # #/###/# #/# #/# #
  {31, 17, 31},  // O  ###/# #/# #/# #/###
  {31,  5,  2},  // P  ##./# #/##./#../#..
  {15,  9, 31},  // Q  ###/# #/# #/###/..#
  {31,  5, 26},  // R  ##./# #/##./# #/# #
  {23, 21, 29},  // S  ###/#../###/..#/###
  { 1, 31,  1},  // T  ###/.#./.#./.#./.#.
  {31, 16, 31},  // U  # #/# #/# #/# #/###
  {15, 16, 15},  // V  # #/# #/# #/# #/.#.
  {31, 24, 31},  // W  # #/# #/# #/###/###
  {27,  4, 27},  // X  # #/# #/.#./# #/# #
  { 3, 28,  3},  // Y  # #/# #/.#./.#./.#.
  {25, 21, 19},  // Z  ###/..#/.#./#../###
  // 0 – 9  (indices 26–35) — matches MINI_FONT in clock_timer.ino
  {31, 17, 31},  // 0
  { 2, 31, 16},  // 1
  {29, 21, 23},  // 2
  {17, 21, 31},  // 3
  { 7,  4, 31},  // 4
  {23, 21, 29},  // 5
  {31, 21, 29},  // 6
  {25,  5,  3},  // 7
  {31, 21, 31},  // 8
  {23, 21, 31},  // 9
  // space (index 36)
  { 0,  0,  0},  // ' '
};

// ── Draw functions ─────────────────────────────────────────────

void drawChar3x3(char c, int startCol, int rowOffset, CRGB color) {
  int idx = fontIdx(c);
  CRGB dimColor = CRGB(color.r / 2, color.g / 2, color.b / 2);
  for (int col = 0; col < 3; col++) {
    uint8_t bits  = pgm_read_byte(&FONT_3X3[idx][col]);
    uint8_t light = pgm_read_byte(&FONT_3X3_LIGHT[idx][col]);
    for (int row = 0; row < 3; row++) {
      if ((bits >> row) & 1)
        setPixel(startCol + col, rowOffset + row,
                 ((light >> row) & 1) ? dimColor : color);
    }
  }
}

void drawChar3x5(char c, int startCol, int rowOffset, CRGB color) {
  int idx = fontIdx(c);
  for (int col = 0; col < 3; col++) {
    uint8_t bits = pgm_read_byte(&FONT_3X5[idx][col]);
    for (int row = 0; row < 5; row++)
      if ((bits >> row) & 1)
        setPixel(startCol + col, rowOffset + row, color);
  }
}

// Draw a string left-to-right starting at startCol, 4px stride per char.
// Stops when it runs off the right edge of the matrix.
void drawStr3x3(const char* str, int startCol, int rowOffset, CRGB color) {
  for (int i = 0; str[i] != '\0'; i++) {
    int x = startCol + i * (FONT_CHAR_W + FONT_CHAR_GAP);
    if (x >= MATRIX_W) continue;   // keep scanning; drawChar clips off-screen cols (safe for scroll)
    drawChar3x3(str[i], x, rowOffset, color);
  }
}

void drawStr3x5(const char* str, int startCol, int rowOffset, CRGB color) {
  for (int i = 0; str[i] != '\0'; i++) {
    int x = startCol + i * (FONT_CHAR_W + FONT_CHAR_GAP);
    if (x >= MATRIX_W) continue;   // keep scanning; drawChar clips off-screen cols (safe for scroll)
    drawChar3x5(str[i], x, rowOffset, color);
  }
}

// Centered variants — automatically compute startCol for the string length.
void drawStrCentered3x3(const char* str, int rowOffset, CRGB color) {
  drawStr3x3(str, fontCenterCol(strlen(str)), rowOffset, color);
}

void drawStrCentered3x5(const char* str, int rowOffset, CRGB color) {
  drawStr3x5(str, fontCenterCol(strlen(str)), rowOffset, color);
}
