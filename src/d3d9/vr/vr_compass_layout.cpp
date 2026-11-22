#include <algorithm>
#include <cmath>

#include "vr_compass_layout.h"
#include "vr_math.h"

namespace dxvk {

  namespace {

    constexpr float Pi    = 3.14159265f;
    constexpr float TwoPi = 6.2831853f;

    // The game's direction strip (glow_hud_comp_direction_strip.dds): 1024
    // texels for the whole circle, N at u = 0; its labels and line fill the
    // rows from 17 to 45 of 64, the line's glow rows 23 to 34
    constexpr float StripTexels   = 1024.0f;
    constexpr float StripRows     = 64.0f;
    constexpr float StripTop      = 12.0f;   // rows drawn, with some glow around
    constexpr float StripBottom   = 52.0f;
    constexpr float StripLineRow  = 28.5f;

    // The game's markers (glow_hud_compass_objective_marker.dds and
    // _pc_marker.dds, 64 x 64): the arrow covers x 5 to 32, y 6 to 45
    constexpr float MarkerSize    = 64.0f;
    constexpr float MarkerCentreX = 18.5f;
    constexpr float MarkerTop     = 6.0f;
    constexpr float MarkerBottom  = 45.0f;

    // The game's mark of a person (glow_hud_tick_mark.dds, 32 x 32): x 3 to 13, y 3 to 28
    constexpr float TickSize      = 32.0f;
    constexpr float TickCentreX   = 8.0f;
    constexpr float TickTop       = 3.0f;
    constexpr float TickBottom    = 28.0f;

    // Layout of the panel's image, in its pixels
    constexpr float StripY        = 2.0f;    // top of the strip
    constexpr float MarkerRowTop  = 98.0f;   // markers between this and the bottom
    constexpr float MarkerRowEnd  = 146.0f;
    constexpr float EdgeSpace     = 16.0f;   // markers held at an end stay this far inside
    constexpr float HeldAlpha     = 0.55f;   // a marker held at an end is fainter

    // Own marks, only without the game's strip, in its look
    const char* const Labels[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    constexpr float CardinalCap = 26.0f;
    constexpr float OrdinalCap  = 20.0f;

    float wrap(float angle) {
      angle = std::fmod(angle + Pi, TwoPi);

      if (angle < 0.0f)
        angle += TwoPi;

      return angle - Pi;
    }

    // Fades towards the strip's ends, as the game's compass window does
    float edgeFade(float x, float width) {
      float t = std::fabs(x - 0.5f * width) / (0.5f * width);
      return std::clamp(1.0f - t * t * t, 0.0f, 1.0f);
    }

    // Texels of the strip per pixel of the image, for the span
    float stripScale(const VrCompassConfig& config, float width) {
      return width / (std::max(config.span, 1.0f) / 360.0f * StripTexels);
    }

    void paintGameStrip(VrPaintList& list, const VrCompassConfig& config, float heading,
        const VrColour& colour, const VrImage& strip) {
      float width = float(list.width());
      float k     = stripScale(config, width);
      float top   = StripY;
      float bottom = StripY + (StripBottom - StripTop) * k;
      float v0    = StripTop / StripRows;
      float v1    = StripBottom / StripRows;

      // In pieces, each faded for its place and split where the strip wraps
      constexpr uint32_t Pieces = 48;
      float centreU = heading / TwoPi;

      for (uint32_t i = 0; i < Pieces; i++) {
        float x0 = width * float(i) / float(Pieces);
        float x1 = width * float(i + 1) / float(Pieces);
        VrColour c = colour.faded(edgeFade(0.5f * (x0 + x1), width));

        float u0 = centreU + (x0 - 0.5f * width) / k / StripTexels;
        float u1 = centreU + (x1 - 0.5f * width) / k / StripTexels;
        float base = std::floor(u0);
        u0 -= base;
        u1 -= base;

        if (u1 <= 1.0f) {
          list.drawImage(strip, VrPaintImageUse::Shape, x0, top, x1, bottom, u0, v0, u1, v1, c);
        } else {
          float xs = x0 + (x1 - x0) * (1.0f - u0) / (u1 - u0);
          list.drawImage(strip, VrPaintImageUse::Shape, x0, top, xs, bottom, u0, v0, 1.0f, v1, c);
          list.drawImage(strip, VrPaintImageUse::Shape, xs, top, x1, bottom, 0.0f, v0, u1 - 1.0f, v1, c);
        }
      }
    }

    void paintOwnStrip(VrPaintList& list, const VrCompassConfig& config, float heading,
        const VrColour& colour, const VrPaintFont* font) {
      float width = float(list.width());
      float k     = stripScale(config, width);
      float line  = StripY + (StripLineRow - StripTop) * k;
      float capitals = font ? std::max(font->metrics.glyph('A').ascent, 1.0f) : 1.0f;

      // Each 15 degree step: its label or dot, and the line to the next step,
      // broken around labels as on the game's strip
      struct Step { float x; float gap; };
      float half = 0.5f * std::max(config.span, 1.0f) * Pi / 180.0f;
      float stepAngle = TwoPi / 24.0f;
      int32_t first = int32_t(std::floor((heading - half) / stepAngle)) - 1;
      int32_t last  = int32_t(std::ceil((heading + half) / stepAngle)) + 1;
      float pixelsPerRadian = 0.5f * width / half;

      auto stepOf = [&] (int32_t n) {
        Step step = { 0.5f * width + (float(n) * stepAngle - heading) * pixelsPerRadian, 0.0f };
        int32_t m = ((n % 24) + 24) % 24;
        uint32_t index = uint32_t(m / 3);

        if (m % 3 == 0 && font) {
          float scale = (index % 2 == 0 ? CardinalCap : OrdinalCap) / capitals;
          step.gap = 0.5f * VrPaintList::measureText(*font, Labels[index], scale) + 6.0f;
        }

        return step;
      };

      for (int32_t n = first; n <= last; n++) {
        Step step = stepOf(n);
        Step next = stepOf(n + 1);

        // The line to the next step, in pieces for the fade
        float x0 = std::max(step.x + step.gap, 0.0f);
        float x1 = std::min(next.x - next.gap, width);

        for (float x = x0; x < x1; x += 12.0f) {
          float xe = std::min(x + 12.0f, x1);
          list.fillRect(x, line - 1.0f, xe, line + 1.0f, colour.faded(0.8f * edgeFade(0.5f * (x + xe), width)));
        }

        if (step.x < 0.0f || step.x > width)
          continue;

        float fade = edgeFade(step.x, width);
        int32_t m = ((n % 24) + 24) % 24;

        if (step.gap == 0.0f) {
          float r = m % 3 == 0 ? 4.0f : 3.0f;
          list.fillSector(step.x, line, 0.0f, r, 0.0f, TwoPi, colour.faded(fade));
          continue;
        }

        uint32_t index = uint32_t(m / 3);
        float cap   = index % 2 == 0 ? CardinalCap : OrdinalCap;
        float scale = cap / capitals;
        const std::string label = Labels[index];
        float textWidth = VrPaintList::measureText(*font, label, scale);
        list.drawText(*font, label, step.x - 0.5f * textWidth, line + 0.5f * cap, scale, colour.faded(fade));
      }
    }

  }


  float vrCompassBearing(const VrVector3& from, const VrVector3& to) {
    float dx = to.x - from.x;
    float dy = to.y - from.y;

    if (dx == 0.0f && dy == 0.0f)
      return 0.0f;

    float bearing = std::atan2(dx, dy);
    return bearing < 0.0f ? bearing + TwoPi : bearing;
  }


  float vrCompassOffset(float bearing, float heading) {
    return wrap(bearing - heading);
  }


  float vrCompassX(const VrCompassConfig& config, float offset, float width, bool& clamped) {
    float half = 0.5f * std::max(config.span, 1.0f) * Pi / 180.0f;
    clamped = std::fabs(offset) > half;

    float t = std::clamp(offset / half, -1.0f, 1.0f);
    return 0.5f * width * (1.0f + t);
  }


  void vrPaintCompass(VrPaintList& list, const VrCompassConfig& config, float heading,
      const std::vector<VrCompassMarker>& markers, const VrColour& colour, const VrPaintFont* font,
      const VrCompassPictures& pictures) {
    float width  = float(list.width());
    float height = float(list.height());
    float k      = stripScale(config, width);

    list.clear();

    // A dark band behind the strip, round at its ends
    if (config.backdrop > 0.0f) {
      float bandBottom = StripY + (StripBottom - StripTop) * k;
      list.fillRoundedRect(0.0f, StripY, width, bandBottom, 0.5f * (bandBottom - StripY),
        { 0.0f, 0.0f, 0.0f, config.backdrop });
    }

    if (pictures.strip)
      paintGameStrip(list, config, heading, colour, *pictures.strip);
    else
      paintOwnStrip(list, config, heading, colour, font);

    // Markers: friends first, so that the quest's arrow is drawn over them
    VrColour friendColour = {
      float((config.friendColour >> 16) & 0xFF) / 255.0f,
      float((config.friendColour >> 8) & 0xFF) / 255.0f,
      float(config.friendColour & 0xFF) / 255.0f, 1.0f };

    static const VrCompassMarkerKind DrawOrder[] = {
      VrCompassMarkerKind::Friend, VrCompassMarkerKind::Custom, VrCompassMarkerKind::Quest };

    float rowHeight = MarkerRowEnd - MarkerRowTop;
    float capitals  = font ? std::max(font->metrics.glyph('A').ascent, 1.0f) : 1.0f;

    for (VrCompassMarkerKind kind : DrawOrder) {
      for (const VrCompassMarker& marker : markers) {
        if (marker.kind != kind)
          continue;

        bool  clamped = false;
        float x = vrCompassX(config, vrCompassOffset(marker.bearing, heading), width, clamped);
        x = std::clamp(x, EdgeSpace, width - EdgeSpace);
        float alpha = clamped ? HeldAlpha : 1.0f;

        if (kind == VrCompassMarkerKind::Friend) {
          VrColour c = friendColour.faded(alpha);
          float scale = rowHeight / (TickBottom - TickTop);

          if (pictures.person) {
            float left = x - TickCentreX * scale;
            float top  = MarkerRowTop - TickTop * scale;
            list.drawImage(*pictures.person, VrPaintImageUse::Shape, left, top,
              left + TickSize * scale, top + TickSize * scale, 0.0f, 0.0f, 1.0f, 1.0f, c);
          } else {
            list.fillRect(x - 3.0f, MarkerRowTop, x + 3.0f, MarkerRowEnd, c);
          }

          if (font && marker.initial) {
            std::string initial(1, marker.initial);
            float textScale = 22.0f / capitals;
            list.drawText(*font, initial, x + 8.0f, MarkerRowEnd - 4.0f, textScale, c);
          }

          continue;
        }

        const VrImage* picture = kind == VrCompassMarkerKind::Quest ? pictures.quest : pictures.custom;
        VrColour c = colour.faded(alpha);

        if (picture) {
          float scale = rowHeight / (MarkerBottom - MarkerTop);
          float left = x - MarkerCentreX * scale;
          float top  = MarkerRowTop - MarkerTop * scale;
          list.drawImage(*picture, VrPaintImageUse::Shape, left, top,
            left + MarkerSize * scale, top + MarkerSize * scale, 0.0f, 0.0f, 1.0f, 1.0f, c);
        } else {
          // The game's arrow: a pentagon pointing up, filled or hollow
          float w = 0.33f * rowHeight;
          const float arrow[] = {
            x, MarkerRowTop,  x + w, MarkerRowTop + 0.45f * rowHeight,  x + w, MarkerRowEnd,
            x - w, MarkerRowEnd,  x - w, MarkerRowTop + 0.45f * rowHeight };

          if (kind == VrCompassMarkerKind::Quest)
            list.fillPolygon(arrow, 5, c);
          else
            list.strokePolygon(arrow, 5, 3.0f, c);
        }
      }
    }

    (void)height;
  }


  VrPose vrCompassPose(const VrCompassConfig& config) {
    float e = config.elevation * Pi / 180.0f;

    // The head looks along -Z with +Y up; the panel faces along its +Z,
    // tilted about X by the elevation to face the eye
    VrPose pose;
    pose.position    = { 0.0f, config.distance * std::sin(e), -config.distance * std::cos(e) };
    pose.orientation = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, e);
    return pose;
  }


  float vrCompassWidth(const VrCompassConfig& config) {
    return 2.0f * config.distance * std::tan(0.5f * config.angle * Pi / 180.0f);
  }

}
