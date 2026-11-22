#include "vr_pipboy_panel_layout.h"

#include <algorithm>
#include <cmath>

namespace dxvk {

  namespace {

    /// Grows [a, b] about its centre to \c length, kept inside [0, limit]
    void growTo(float& a, float& b, float length, float limit) {
      length = std::min(length, limit);
      float centre = 0.5f * (a + b);
      a = centre - 0.5f * length;
      b = centre + 0.5f * length;

      if (a < 0.0f) { b -= a; a = 0.0f; }
      if (b > limit) { a -= b - limit; b = limit; }

      a = std::max(a, 0.0f);
    }

    uint8_t clampByte(float v) {
      return uint8_t(std::min(std::max(v, 0.0f), 255.0f) + 0.5f);
    }

  }


  VrPipBoyPanelLayout vrComputePipBoyPanelLayout(uint32_t panelWidth, uint32_t panelHeight,
      uint32_t textureWidth, uint32_t textureHeight, const VrShareRect& crop, float frame) {
    VrPipBoyPanelLayout layout;

    if (!panelWidth || !panelHeight || !textureWidth || !textureHeight)
      return layout;

    float w = float(panelWidth);
    float h = float(panelHeight);
    float f = std::min(std::max(frame, 0.0f), 0.2f) * w;

    layout.inside = { f / w, f / h, (w - f) / w, (h - f) / h };
    float insideAspect = (w - 2.0f * f) / std::max(h - 2.0f * f, 1.0f);

    // The crop in texture pixels, then to the inside's aspect
    float tw = float(textureWidth);
    float th = float(textureHeight);
    float left   = std::min(std::max(crop.left,   0.0f), 1.0f) * tw;
    float right  = std::min(std::max(crop.right,  0.0f), 1.0f) * tw;
    float top    = std::min(std::max(crop.top,    0.0f), 1.0f) * th;
    float bottom = std::min(std::max(crop.bottom, 0.0f), 1.0f) * th;

    if (right - left < 1.0f) { left = 0.0f; right = tw; }
    if (bottom - top < 1.0f) { top = 0.0f; bottom = th; }

    float cropAspect = (right - left) / (bottom - top);

    if (cropAspect < insideAspect)
      growTo(left, right, (bottom - top) * insideAspect, tw);
    else if (cropAspect > insideAspect)
      growTo(top, bottom, (right - left) / insideAspect, th);

    layout.crop = { left / tw, top / th, right / tw, bottom / th };
    return layout;
  }


  std::vector<uint8_t> vrPaintPipBoyCasing(uint32_t width, uint32_t height,
      uint32_t insideLeft, uint32_t insideTop, uint32_t insideRight, uint32_t insideBottom, uint32_t cornerRadius,
      uint32_t outerRadius) {
    std::vector<uint8_t> rgba(size_t(width) * height * 4, 0);

    float frame = float(std::max<uint32_t>(1, std::min(insideLeft, insideTop)));
    float lip   = std::max(1.0f, frame * 0.18f);
    float r     = float(cornerRadius);
    float ro    = std::min(float(outerRadius), 0.5f * float(std::min(width, height)));

    for (uint32_t y = 0; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        float px = float(x) + 0.5f;
        float py = float(y) + 0.5f;
        uint8_t* out = &rgba[(size_t(y) * width + x) * 4];
        out[3] = 255;

        // Distance outside the screen's rounded rectangle (negative inside)
        float cx = std::min(std::max(px, float(insideLeft) + r), float(insideRight) - r);
        float cy = std::min(std::max(py, float(insideTop) + r), float(insideBottom) - r);
        float dx = px - cx;
        float dy = py - cy;
        float outside = std::sqrt(dx * dx + dy * dy) - r;

        if (outside <= 0.0f) {
          out[3] = 0;
          continue;  // the screen: clear, the copy of the Pip-Boy's screen shows through
        }

        // The plastic: lighter towards its top edge, darker towards its bottom,
        // a soft rounding towards the outer edge and a dark lip at the screen
        float fromTop    = py / float(height);
        float edge       = std::min(std::min(px, float(width) - px), std::min(py, float(height) - py));
        float rounding   = std::min(edge / frame, 1.0f);
        float shade      = 1.0f + 0.22f * (0.5f - fromTop) - 0.25f * (1.0f - rounding);
        float grain      = float((x * 73856093u ^ y * 19349663u) % 997u) / 997.0f - 0.5f;

        float red   = (86.0f + 6.0f * grain) * shade;
        float green = (88.0f + 6.0f * grain) * shade;
        float blue  = (74.0f + 6.0f * grain) * shade;

        if (outside < lip) {
          float t = outside / lip;
          red *= 0.25f + 0.55f * t;
          green *= 0.25f + 0.55f * t;
          blue *= 0.25f + 0.55f * t;
        }

        out[0] = clampByte(red);
        out[1] = clampByte(green);
        out[2] = clampByte(blue);
        // A soft edge at the screen, and the device's rounded outer corners
        float ox = std::min(std::max(px, ro), float(width)  - ro);
        float oy = std::min(std::max(py, ro), float(height) - ro);
        float beyond = std::sqrt((px - ox) * (px - ox) + (py - oy) * (py - oy)) - ro;
        float cover = ro > 0.0f ? std::min(std::max(0.5f - beyond, 0.0f), 1.0f) : 1.0f;
        out[3] = clampByte(std::min(std::min(outside, 1.0f), cover) * 255.0f);
      }
    }

    return rgba;
  }

}
