// Desktop stand-in for Arduino_GFX. Renders into the same RGB565 framebuffer
// the real Arduino_Canvas uses; flush() just marks the frame ready for SDL.
#pragma once
#include "Arduino.h"
#include <vector>
#include <algorithm>

#define RGB565_BLACK 0x0000
#define RGB565_WHITE 0xFFFF
#define RGB565_RED 0xF800
#define RGB565_GREEN 0x07E0
#define RGB565_BLUE 0x001F

extern const uint8_t GLCD_FONT[];   // classic 5x7, from the real library

class Arduino_DataBus {};
class Arduino_ESP32QSPI : public Arduino_DataBus {
public:
  Arduino_ESP32QSPI(int, int, int, int, int, int) {}
};
class Arduino_CO5300 {
public:
  Arduino_CO5300(Arduino_DataBus *, int, int, int, int, int, int, int, int) {}
  void setBrightness(uint8_t b) { brightness = b; }
  uint8_t brightness = 255;
};

class Arduino_Canvas {
public:
  int16_t _w, _h;
  Arduino_CO5300 *_panel;
  std::vector<uint16_t> fb;
  // Named as in the real Arduino_GFX (protected there), so a subclass in the
  // sketch -- the UTF-8 / CJK text canvas -- compiles against both.
  int16_t cursor_x = 0, cursor_y = 0;
  uint16_t textcolor = 0xFFFF;
  uint8_t textsize_x = 1;
  volatile bool frameReady = false;

  Arduino_Canvas(int16_t w, int16_t h, Arduino_CO5300 *p)
      : _w(w), _h(h), _panel(p), fb(w * h, 0) {}

  bool begin(uint32_t = 0) { return true; }
  void flush() { frameReady = true; }
  const uint16_t *buffer() const { return fb.data(); }

  inline void px(int x, int y, uint16_t c) {
    if (x < 0 || y < 0 || x >= _w || y >= _h) return;
    fb[(size_t)y * _w + x] = c;
  }

  void fillScreen(uint16_t c) { std::fill(fb.begin(), fb.end(), c); }

  void fillRect(int x, int y, int w, int h, uint16_t c) {
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) px(x + i, y + j, c);
  }
  void drawFastHLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void drawRect(int x, int y, int w, int h, uint16_t c) {
    fillRect(x, y, w, 1, c); fillRect(x, y + h - 1, w, 1, c);
    fillRect(x, y, 1, h, c); fillRect(x + w - 1, y, 1, h, c);
  }

  void fillCircle(int x0, int y0, int r, uint16_t c) {
    if (r < 0) return;
    for (int y = -r; y <= r; y++)
      for (int x = -r; x <= r; x++)
        if (x * x + y * y <= r * r) px(x0 + x, y0 + y, c);
  }
  void drawCircle(int x0, int y0, int r, uint16_t c) {
    if (r <= 0) return;
    int f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
    px(x0, y0 + r, c); px(x0, y0 - r, c); px(x0 + r, y0, c); px(x0 - r, y0, c);
    while (x < y) {
      if (f >= 0) { y--; ddy += 2; f += ddy; }
      x++; ddx += 2; f += ddx;
      px(x0 + x, y0 + y, c); px(x0 - x, y0 + y, c);
      px(x0 + x, y0 - y, c); px(x0 - x, y0 - y, c);
      px(x0 + y, y0 + x, c); px(x0 - y, y0 + x, c);
      px(x0 + y, y0 - x, c); px(x0 - y, y0 - x, c);
    }
  }

  // rounded-rect containment, used by both the filled and outlined versions
  static bool inRR(int x, int y, int rx, int ry, int w, int h, int r) {
    if (x < rx || y < ry || x >= rx + w || y >= ry + h) return false;
    int dx = 0, dy = 0;
    if (x < rx + r) dx = rx + r - x; else if (x >= rx + w - r) dx = x - (rx + w - r - 1);
    if (y < ry + r) dy = ry + r - y; else if (y >= ry + h - r) dy = y - (ry + h - r - 1);
    if (dx == 0 || dy == 0) return true;
    return dx * dx + dy * dy <= r * r;
  }
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    r = std::min(r, std::min(w / 2, h / 2));
    for (int j = y; j < y + h; j++)
      for (int i = x; i < x + w; i++)
        if (inRR(i, j, x, y, w, h, r)) px(i, j, c);
  }
  void drawRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    r = std::min(r, std::min(w / 2, h / 2));
    for (int j = y; j < y + h; j++)
      for (int i = x; i < x + w; i++)
        if (inRR(i, j, x, y, w, h, r) && !inRR(i, j, x + 1, y + 1, w - 2, h - 2, r))
          px(i, j, c);
  }

  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
      px(x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }

  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
    int minx = std::min({ x0, x1, x2 }), maxx = std::max({ x0, x1, x2 });
    int miny = std::min({ y0, y1, y2 }), maxy = std::max({ y0, y1, y2 });
    auto sign = [](int ax, int ay, int bx, int by, int cx_, int cy_) {
      return (ax - cx_) * (by - cy_) - (bx - cx_) * (ay - cy_);
    };
    for (int y = miny; y <= maxy; y++)
      for (int x = minx; x <= maxx; x++) {
        int d1 = sign(x, y, x0, y0, x1, y1);
        int d2 = sign(x, y, x1, y1, x2, y2);
        int d3 = sign(x, y, x2, y2, x0, y0);
        bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        if (!(neg && pos)) px(x, y, c);
      }
  }

  // --- text: the classic 5x7 glyphs, 6*size advance, same as Adafruit_GFX ---
  void setTextColor(uint16_t c) { textcolor = c; }
  void setTextSize(uint8_t s) { textsize_x = s ? s : 1; }
  void setCursor(int16_t x, int16_t y) { cursor_x = x; cursor_y = y; }
  int16_t getCursorX() const { return cursor_x; }
  int16_t getCursorY() const { return cursor_y; }

  void drawChar(char ch, int x, int y) {
    const uint8_t *g = GLCD_FONT + (uint8_t)ch * 5;
    for (int i = 0; i < 5; i++) {
      uint8_t line = g[i];
      for (int j = 0; j < 8; j++, line >>= 1)
        if (line & 1) fillRect(x + i * textsize_x, y + j * textsize_x, textsize_x, textsize_x, textcolor);
    }
  }
  // As in the real library: print() goes through the virtual write(), one byte
  // at a time, so a subclass sees every byte.
  virtual size_t write(uint8_t ch) {
    if (ch == '\n') { cursor_y += 8 * textsize_x; cursor_x = 0; return 1; }
    drawChar((char)ch, cursor_x, cursor_y);
    cursor_x += 6 * textsize_x;
    return 1;
  }
  void print(char ch) { write((uint8_t)ch); }
  void print(const char *s) { while (*s) print(*s++); }
  template <typename... A> void printf(const char *f, A... a) {
    char buf[256];
    snprintf(buf, sizeof(buf), f, a...);
    print(buf);
  }
};
