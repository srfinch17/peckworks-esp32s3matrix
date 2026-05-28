// ============================================================
// SECTION 8: FONT DATA AND TEXT SCROLLING
//
// THREE FONTS for three size modes:
//   Normal (default) — 5×7 pixels, one character visible at a time
//   Small            — 3×5 pixels, two characters visible at once
//   Tiny             — 3×3 pixels, three or more chars visible
//
// All fonts share the same encoding scheme:
//   - Column bytes: each byte describes one vertical column of pixels
//   - Bit0 = top row, bit6 (normal) or bit4 (small) = bottom row
//   - Characters start at ASCII 32 (space): index = ascii_code - 32
//   - Lowercase is auto-converted to uppercase at render time
//
// PROGMEM: the font tables are large (271 bytes for normal alone)
// and never change, so they live in flash memory. pgm_read_byte()
// reads each byte back at runtime.
//
// HOW SCROLLING WORKS:
//   The text is treated as one long horizontal pixel strip.
//   scrollOffset advances by 1 pixel per scroll tick.
//   renderScrollFrame() maps each screen column back to a position
//   in the pixel strip and draws the correct font column.
//   When the strip has fully scrolled off the right edge, the
//   loop pauses 1 second then resets to the beginning.
// ============================================================

// ── Normal Font (5×7) ─────────────────────────────────────────
// 59 characters from ASCII 32 (space) to ASCII 90 (Z).
// Each character: 5 bytes (one per column). Bit0 = top, bit6 = bottom.
// Only uppercase defined — lowercase is converted by toupper() at render time.
const uint8_t FONT[][5] PROGMEM = {
  { 0x00, 0x00, 0x00, 0x00, 0x00 }, // 32 space
  { 0x00, 0x00, 0x5F, 0x00, 0x00 }, // 33 !
  { 0x00, 0x07, 0x00, 0x07, 0x00 }, // 34 "
  { 0x14, 0x7F, 0x14, 0x7F, 0x14 }, // 35 #
  { 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, // 36 $
  { 0x23, 0x13, 0x08, 0x64, 0x62 }, // 37 %
  { 0x36, 0x49, 0x55, 0x22, 0x50 }, // 38 &
  { 0x00, 0x05, 0x03, 0x00, 0x00 }, // 39 '
  { 0x00, 0x1C, 0x22, 0x41, 0x00 }, // 40 (
  { 0x00, 0x41, 0x22, 0x1C, 0x00 }, // 41 )
  { 0x14, 0x08, 0x3E, 0x08, 0x14 }, // 42 *
  { 0x08, 0x08, 0x3E, 0x08, 0x08 }, // 43 +
  { 0x00, 0x50, 0x30, 0x00, 0x00 }, // 44 ,
  { 0x08, 0x08, 0x08, 0x08, 0x08 }, // 45 -
  { 0x00, 0x60, 0x60, 0x00, 0x00 }, // 46 .
  { 0x20, 0x10, 0x08, 0x04, 0x02 }, // 47 /
  { 0x3E, 0x51, 0x49, 0x45, 0x3E }, // 48 0
  { 0x00, 0x42, 0x7F, 0x40, 0x00 }, // 49 1
  { 0x42, 0x61, 0x51, 0x49, 0x46 }, // 50 2
  { 0x21, 0x41, 0x45, 0x4B, 0x31 }, // 51 3
  { 0x18, 0x14, 0x12, 0x7F, 0x10 }, // 52 4
  { 0x27, 0x45, 0x45, 0x45, 0x39 }, // 53 5
  { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, // 54 6
  { 0x01, 0x71, 0x09, 0x05, 0x03 }, // 55 7
  { 0x36, 0x49, 0x49, 0x49, 0x36 }, // 56 8
  { 0x06, 0x49, 0x49, 0x29, 0x1E }, // 57 9
  { 0x00, 0x36, 0x36, 0x00, 0x00 }, // 58 :
  { 0x00, 0x56, 0x36, 0x00, 0x00 }, // 59 ;
  { 0x08, 0x14, 0x22, 0x41, 0x00 }, // 60 <
  { 0x14, 0x14, 0x14, 0x14, 0x14 }, // 61 =
  { 0x00, 0x41, 0x22, 0x14, 0x08 }, // 62 >
  { 0x02, 0x01, 0x51, 0x09, 0x06 }, // 63 ?
  { 0x32, 0x49, 0x79, 0x41, 0x3E }, // 64 @
  { 0x7E, 0x11, 0x11, 0x11, 0x7E }, // 65 A
  { 0x7F, 0x49, 0x49, 0x49, 0x36 }, // 66 B
  { 0x3E, 0x41, 0x41, 0x41, 0x22 }, // 67 C
  { 0x7F, 0x41, 0x41, 0x22, 0x1C }, // 68 D
  { 0x7F, 0x49, 0x49, 0x49, 0x41 }, // 69 E
  { 0x7F, 0x09, 0x09, 0x09, 0x01 }, // 70 F
  { 0x3E, 0x41, 0x49, 0x49, 0x7A }, // 71 G
  { 0x7F, 0x08, 0x08, 0x08, 0x7F }, // 72 H
  { 0x00, 0x41, 0x7F, 0x41, 0x00 }, // 73 I
  { 0x20, 0x40, 0x41, 0x3F, 0x01 }, // 74 J
  { 0x7F, 0x08, 0x14, 0x22, 0x41 }, // 75 K
  { 0x7F, 0x40, 0x40, 0x40, 0x40 }, // 76 L
  { 0x7F, 0x02, 0x0C, 0x02, 0x7F }, // 77 M
  { 0x7F, 0x04, 0x08, 0x10, 0x7F }, // 78 N
  { 0x3E, 0x41, 0x41, 0x41, 0x3E }, // 79 O
  { 0x7F, 0x09, 0x09, 0x09, 0x06 }, // 80 P
  { 0x3E, 0x41, 0x51, 0x21, 0x5E }, // 81 Q
  { 0x7F, 0x09, 0x19, 0x29, 0x46 }, // 82 R
  { 0x46, 0x49, 0x49, 0x49, 0x31 }, // 83 S
  { 0x01, 0x01, 0x7F, 0x01, 0x01 }, // 84 T
  { 0x3F, 0x40, 0x40, 0x40, 0x3F }, // 85 U
  { 0x1F, 0x20, 0x40, 0x20, 0x1F }, // 86 V
  { 0x3F, 0x40, 0x38, 0x40, 0x3F }, // 87 W
  { 0x63, 0x14, 0x08, 0x14, 0x63 }, // 88 X
  { 0x07, 0x08, 0x70, 0x08, 0x07 }, // 89 Y
  { 0x61, 0x51, 0x49, 0x45, 0x43 }, // 90 Z
};

#define FONT_COUNT  (sizeof(FONT) / sizeof(FONT[0]))

// ── Small Font (3×5) ──────────────────────────────────────────
// Same encoding as FONT but 3 bytes wide and 5 pixels tall.
// Rendered with a 1-pixel top and bottom margin so it appears
// vertically centered on the 8-pixel-tall matrix (rows 1-5).
// Characters start at ASCII 32. Index = ascii_code - 32.
const uint8_t SMALL_FONT[][3] PROGMEM = {
  {  0,  0,  0 }, // 32 space
  {  0, 23,  0 }, // 33 !   (rows 0-2,4 on center col)
  {  3,  0,  3 }, // 34 "
  { 10, 31, 10 }, // 35 #
  { 23, 31, 29 }, // 36 $
  { 17,  4, 17 }, // 37 %
  { 14, 21, 12 }, // 38 &
  {  0,  3,  0 }, // 39 '
  { 14, 17,  0 }, // 40 (
  {  0, 17, 14 }, // 41 )
  { 10,  4, 10 }, // 42 *
  {  4, 14,  4 }, // 43 +
  {  0, 24,  0 }, // 44 ,   (rows 3,4)
  {  4,  4,  4 }, // 45 -   (row 2)
  {  0, 16,  0 }, // 46 .   (row 4)
  { 16,  4,  1 }, // 47 /
  { 31, 17, 31 }, // 48 0
  {  2, 31, 16 }, // 49 1
  { 29, 21, 23 }, // 50 2
  { 17, 21, 31 }, // 51 3
  {  7,  4, 31 }, // 52 4
  { 23, 21, 29 }, // 53 5
  { 31, 21, 29 }, // 54 6
  { 25,  5,  3 }, // 55 7
  { 31, 21, 31 }, // 56 8
  { 23, 21, 31 }, // 57 9
  {  0, 10,  0 }, // 58 :   (rows 1,3)
  {  0, 26,  0 }, // 59 ;
  {  4, 10, 17 }, // 60 <
  { 10, 10, 10 }, // 61 =
  { 17, 10,  4 }, // 62 >
  {  1, 21,  3 }, // 63 ?
  { 14, 21,  6 }, // 64 @
  { 30,  5, 30 }, // 65 A
  { 31, 21, 10 }, // 66 B
  { 14, 17, 17 }, // 67 C
  { 31, 17, 14 }, // 68 D
  { 31, 21, 17 }, // 69 E
  { 31,  5,  1 }, // 70 F
  { 14, 17, 29 }, // 71 G
  { 31,  4, 31 }, // 72 H
  { 17, 31, 17 }, // 73 I
  {  8, 17, 15 }, // 74 J
  { 31,  6, 25 }, // 75 K
  { 31, 16, 16 }, // 76 L
  { 31,  2, 31 }, // 77 M
  { 31,  1, 30 }, // 78 N
  { 14, 17, 14 }, // 79 O
  { 31,  5,  2 }, // 80 P
  { 14, 17, 30 }, // 81 Q
  { 31,  5, 26 }, // 82 R
  { 18, 21,  9 }, // 83 S
  {  1, 31,  1 }, // 84 T
  { 31, 16, 31 }, // 85 U
  {  7, 24,  7 }, // 86 V
  { 31,  8, 31 }, // 87 W
  { 27,  4, 27 }, // 88 X
  {  3, 28,  3 }, // 89 Y
  { 25, 21, 19 }, // 90 Z
};
#define SMALL_FONT_COUNT (sizeof(SMALL_FONT) / sizeof(SMALL_FONT[0]))

// ── drawCharCol ───────────────────────────────────────────────
// Draws one vertical column of a normal (5×7) character at screenX.
// Clears the entire column first (sets all 8 pixels), then lights
// only the bits that are set in the font byte.
// This ensures previous characters don't leave ghost pixels behind.
void drawCharCol(int fontIdx, int charCol, int screenX, CRGB color) {
  if (screenX < 0 || screenX >= MATRIX_W)       return;
  if (fontIdx < 0 || fontIdx >= (int)FONT_COUNT) return;

  uint8_t bits = pgm_read_byte(&FONT[fontIdx][charCol]);

  for (int row = 0; row < 7; row++) {
    bool on = (bits >> row) & 0x01;
    setPixel(screenX, row, on ? color : CRGB::Black);
  }
  setPixel(screenX, 7, CRGB::Black);   // row 7 always black (font is only 7 rows tall)
}

// Small font: 3×5 glyphs rendered at rows 1-5 (1px margin top and bottom).
// Only lights the set bits — doesn't clear surrounding pixels,
// so the caller is responsible for clearing before rendering.
void drawSmallCharCol(int fontIdx, int charCol, int screenX, CRGB color) {
  if (screenX < 0 || screenX >= MATRIX_W)             return;
  if (fontIdx < 0 || fontIdx >= (int)SMALL_FONT_COUNT) return;

  uint8_t bits = pgm_read_byte(&SMALL_FONT[fontIdx][charCol]);
  for (int row = 0; row < 5; row++) {
    if ((bits >> row) & 0x01) setPixel(screenX, row + 1, color);   // +1 for top margin
  }
}

// Tiny font: 3×3 glyphs from FONT_3X3 (defined in fonts.ino),
// rendered at rows 2-4 (vertically centered on the 8-row matrix).
// The FONT_3X3_LIGHT mask marks pixels that should be dimmed to 50%
// (currently only '?' and '!' use this for their dot pixels).
void drawTinyCharCol(char c, int charCol, int screenX, CRGB color) {
  if (screenX < 0 || screenX >= MATRIX_W) return;
  int idx = fontIdx(c);   // fontIdx() is defined in fonts.ino
  CRGB dimColor = CRGB(color.r / 2, color.g / 2, color.b / 2);
  uint8_t bits  = pgm_read_byte(&FONT_3X3[idx][charCol]);
  uint8_t light = pgm_read_byte(&FONT_3X3_LIGHT[idx][charCol]);
  for (int row = 0; row < 3; row++) {
    if ((bits >> row) & 1)
      setPixel(screenX, row + 2, ((light >> row) & 1) ? dimColor : color);
  }
}

// ── renderScrollFrame ─────────────────────────────────────────
// Computes the current visible portion of the scrolling text strip
// and draws it onto leds[]. Called once per scroll tick by loop().
//
// KEY MAPPING — how screen column → text pixel position works:
//
//   scrollOffset: how many pixels the text strip has advanced
//   The text starts off-screen to the RIGHT and scrolls LEFT.
//   textPixelX = screenX - MATRIX_W + scrollOffset
//
//   When scrollOffset == 0: textPixelX for screenX=7 is -1 (off-screen left).
//   As scrollOffset grows, text enters from the right.
//
//   charIdx = textPixelX / charTotal   → which character in the string
//   charCol = textPixelX % charTotal   → which column within that character
//   If charCol >= charW, it's a gap pixel (black, not drawn).
//
// GRADIENT: if scrollGradient is true, each text pixel's color is
// lerped between scrollColor and scrollColor2 based on its position
// in the total text strip (0 = left = scrollColor, 1 = right = scrollColor2).
void renderScrollFrame() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  int charW     = scrollTiny ? 3 : (scrollSmall ? SMALL_CHAR_W     : CHAR_W);
  int charTotal = scrollTiny ? TINY_CHAR_TOTAL : (scrollSmall ? SMALL_CHAR_TOTAL : CHAR_TOTAL);

  for (int screenX = 0; screenX < MATRIX_W; screenX++) {
    // textPixelX: position within the text's pixel strip
    int textPixelX = screenX - MATRIX_W + scrollOffset;
    if (textPixelX < 0) continue;   // text hasn't entered the screen yet from the right

    int charIdx = textPixelX / charTotal;   // which character
    int charCol = textPixelX % charTotal;   // which column within that character

    if (charIdx >= (int)scrollText.length()) continue;   // past the end of the text
    if (charCol >= charW) continue;                       // in the inter-character gap

    char c = toupper(scrollText.charAt(charIdx));
    int fontIdx = (int)c - 32;   // convert ASCII code to font array index

    // Choose color: flat or 4-color gradient across 3 equal segments
    CRGB col = scrollColor;
    if (scrollGradient && scrollPixelLen > 1) {
      float t = constrain((float)textPixelX / (float)(scrollPixelLen - 1), 0.0f, 1.0f);
      float seg;
      CRGB  from, to;
      if (t < 0.333f)      { seg = t * 3.0f;             from = scrollColor;  to = scrollColor2; }
      else if (t < 0.667f) { seg = (t - 0.333f) * 3.0f;  from = scrollColor2; to = scrollColor3; }
      else                 { seg = (t - 0.667f) * 3.0f;  from = scrollColor3; to = scrollColor4; }
      col = CRGB(
        (uint8_t)(from.r + (int16_t)(to.r - from.r) * seg),
        (uint8_t)(from.g + (int16_t)(to.g - from.g) * seg),
        (uint8_t)(from.b + (int16_t)(to.b - from.b) * seg)
      );
    }

    // Draw the pixel using the appropriate font
    if (scrollTiny) {
      drawTinyCharCol(c, charCol, screenX, col);
    } else if (scrollSmall) {
      drawSmallCharCol(fontIdx, charCol, screenX, col);
    } else {
      drawCharCol(fontIdx, charCol, screenX, col);
    }
  }
}

// ── overlayScrollText ─────────────────────────────────────────
// Paints scrolling normal-font text ON TOP of whatever is
// already in leds[] — only lit font pixels are written,
// leaving the background (from an animation) intact.
// Used for temperature display over a background gradient.
void overlayScrollText(const String& text, int offset, CRGB color) {
  for (int sx = 0; sx < MATRIX_W; sx++) {
    int tpx = sx - MATRIX_W + offset;
    if (tpx < 0) continue;
    int ci = tpx / CHAR_TOTAL, cc = tpx % CHAR_TOTAL;
    if (ci >= (int)text.length() || cc >= CHAR_W) continue;
    char c = toupper(text.charAt(ci));
    int fi = (int)c - 32;
    if (fi < 0 || fi >= 59) continue;
    uint8_t bits = pgm_read_byte(&FONT[fi][cc]);
    for (int row = 0; row < 7; row++)
      if ((bits >> row) & 1) setPixel(sx, row, color);   // lit pixels only
  }
}
