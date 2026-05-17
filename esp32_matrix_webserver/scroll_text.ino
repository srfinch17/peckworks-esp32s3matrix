// ============================================================
// SECTION 8: FONT AND TEXT SCROLLING
//
// Each character is 5 pixels wide and 7 pixels tall.
// Storage: 5 bytes per character, one byte per column.
// In each byte: bit 0 = top row, bit 6 = bottom row.
// Characters begin at ASCII 32 (space). Index = ascii_code - 32.
// Only uppercase letters are defined; lowercase is auto-converted.
// ============================================================

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

// ============================================================
// SMALL FONT — 3×5 pixels per character, 1-pixel gap
// Same encoding as FONT: column bytes, bit0=top row, bit4=bottom.
// Vertically centered: rendered at matrix rows 1–5 (1px top margin).
// Characters begin at ASCII 32 (space). Index = ascii_code - 32.
// ============================================================
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
  { 30,  5, 30 }, // 65 A   .X. / X.X / XXX / X.X / X.X
  { 31, 21, 10 }, // 66 B   XX. / X.X / XX. / X.X / XX.
  { 14, 17, 17 }, // 67 C   .XX / X.. / X.. / X.. / .XX
  { 31, 17, 14 }, // 68 D   XX. / X.X / X.X / X.X / XX.
  { 31, 21, 17 }, // 69 E   XXX / X.. / XX. / X.. / XXX
  { 31,  5,  1 }, // 70 F   XXX / X.. / XX. / X.. / X..
  { 14, 17, 29 }, // 71 G   .XX / X.. / X.X / X.X / .XX
  { 31,  4, 31 }, // 72 H   X.X / X.X / XXX / X.X / X.X
  { 17, 31, 17 }, // 73 I   XXX / .X. / .X. / .X. / XXX
  {  8, 17, 15 }, // 74 J   .XX / ..X / ..X / X.X / .X.
  { 31,  6, 25 }, // 75 K   X.X / XX. / XX. / X.X / X.X
  { 31, 16, 16 }, // 76 L   X.. / X.. / X.. / X.. / XXX
  { 31,  2, 31 }, // 77 M   X.X / XXX / X.X / X.X / X.X
  { 31,  1, 30 }, // 78 N   XX. / X.X / X.X / X.X / X.X
  { 14, 17, 14 }, // 79 O   .X. / X.X / X.X / X.X / .X.
  { 31,  5,  2 }, // 80 P   XX. / X.X / XX. / X.. / X..
  { 14, 17, 30 }, // 81 Q   .X. / X.X / X.X / X.X / .XX
  { 31,  5, 26 }, // 82 R   XX. / X.X / XX. / X.X / X.X
  { 18, 21,  9 }, // 83 S   .XX / X.. / .X. / ..X / XX.
  {  1, 31,  1 }, // 84 T   XXX / .X. / .X. / .X. / .X.
  { 31, 16, 31 }, // 85 U   X.X / X.X / X.X / X.X / XXX
  {  7, 24,  7 }, // 86 V   X.X / X.X / X.X / .X. / .X.
  { 31,  8, 31 }, // 87 W   X.X / X.X / X.X / XXX / X.X
  { 27,  4, 27 }, // 88 X   X.X / X.X / .X. / X.X / X.X
  {  3, 28,  3 }, // 89 Y   X.X / X.X / .X. / .X. / .X.
  { 25, 21, 19 }, // 90 Z   XXX / ..X / .X. / X.. / XXX
};
#define SMALL_FONT_COUNT (sizeof(SMALL_FONT) / sizeof(SMALL_FONT[0]))

void drawCharCol(int fontIdx, int charCol, int screenX, CRGB color) {
  if (screenX < 0 || screenX >= MATRIX_W)       return;
  if (fontIdx < 0 || fontIdx >= (int)FONT_COUNT) return;

  uint8_t bits = pgm_read_byte(&FONT[fontIdx][charCol]);

  for (int row = 0; row < 7; row++) {
    bool on = (bits >> row) & 0x01;
    setPixel(screenX, row, on ? color : CRGB::Black);
  }
  setPixel(screenX, 7, CRGB::Black);
}

// Small font: 3×5 glyphs rendered at matrix rows 1–5 (1px top margin).
void drawSmallCharCol(int fontIdx, int charCol, int screenX, CRGB color) {
  if (screenX < 0 || screenX >= MATRIX_W)             return;
  if (fontIdx < 0 || fontIdx >= (int)SMALL_FONT_COUNT) return;

  uint8_t bits = pgm_read_byte(&SMALL_FONT[fontIdx][charCol]);
  for (int row = 0; row < 5; row++) {
    if ((bits >> row) & 0x01) setPixel(screenX, row + 1, color);
  }
}

// Tiny font: 3×3 glyphs from FONT_3X3, rendered at matrix rows 2–4 (centered).
// C-marked pixels drawn at 50% brightness.
void drawTinyCharCol(char c, int charCol, int screenX, CRGB color) {
  if (screenX < 0 || screenX >= MATRIX_W) return;
  int idx = fontIdx(c);
  CRGB dimColor = CRGB(color.r / 2, color.g / 2, color.b / 2);
  uint8_t bits  = pgm_read_byte(&FONT_3X3[idx][charCol]);
  uint8_t light = pgm_read_byte(&FONT_3X3_LIGHT[idx][charCol]);
  for (int row = 0; row < 3; row++) {
    if ((bits >> row) & 1)
      setPixel(screenX, row + 2, ((light >> row) & 1) ? dimColor : color);
  }
}

void renderScrollFrame() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  int charW     = scrollTiny ? 3 : (scrollSmall ? SMALL_CHAR_W     : CHAR_W);
  int charTotal = scrollTiny ? TINY_CHAR_TOTAL : (scrollSmall ? SMALL_CHAR_TOTAL : CHAR_TOTAL);

  for (int screenX = 0; screenX < MATRIX_W; screenX++) {
    int textPixelX = screenX - MATRIX_W + scrollOffset;
    if (textPixelX < 0) continue;

    int charIdx = textPixelX / charTotal;
    int charCol = textPixelX % charTotal;

    if (charIdx >= (int)scrollText.length()) continue;
    if (charCol >= charW) continue;

    char c = toupper(scrollText.charAt(charIdx));
    int fontIdx = (int)c - 32;

    CRGB col = scrollColor;
    if (scrollGradient && scrollPixelLen > 1) {
      float t = constrain((float)textPixelX / (float)(scrollPixelLen - 1), 0.0f, 1.0f);
      col = CRGB(
        (uint8_t)(scrollColor.r + (int16_t)(scrollColor2.r - scrollColor.r) * t),
        (uint8_t)(scrollColor.g + (int16_t)(scrollColor2.g - scrollColor.g) * t),
        (uint8_t)(scrollColor.b + (int16_t)(scrollColor2.b - scrollColor.b) * t)
      );
    }

    if (scrollTiny) {
      drawTinyCharCol(c, charCol, screenX, col);
    } else if (scrollSmall) {
      drawSmallCharCol(fontIdx, charCol, screenX, col);
    } else {
      drawCharCol(fontIdx, charCol, screenX, col);
    }
  }
}

// Draw scrolling text on top of whatever is already in leds[].
// Only lit font pixels are painted; background is left intact.
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
      if ((bits >> row) & 1) setPixel(sx, row, color);
  }
}
