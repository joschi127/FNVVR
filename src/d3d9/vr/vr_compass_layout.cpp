#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

    // The health bar's row below the markers, and the game's HP meter
    // (glow_hp_bar.dds, 512 x 32): its line and ticks cover x 98 to 413, y 6 to 25
    // Lines drawn by FNVVR: the own strip and the health bar, alike
    constexpr float OwnLine     = 3.0f;    // thickness of the line, as the HP meter's
    constexpr float OwnGlow     = 9.0f;    // and of the faint glow around it
    constexpr float OwnTick     = 2.5f;    // width of the tick marks
    constexpr float OwnTickRise = 16.0f;   // their height above the line, at the full strip height

    constexpr float HealthSpace     = 8.0f;    // between the markers' bottom and the bar
    constexpr float HealthRowHeight = 30.0f;
    constexpr float HealthCapFull   = 17.0f;   // height of the "HP" label's capitals
    constexpr float HealthLabelGap  = 8.0f;
    constexpr float HealthEmpty     = 0.22f;   // brightness of the meter's empty part
    constexpr int32_t MaxRoundTicks = 30;      // a tick per round up to this clip size
    constexpr float MeterTexels     = 512.0f;
    constexpr float MeterRows       = 32.0f;
    constexpr float MeterLeft       = 98.0f;
    constexpr float MeterRight      = 414.0f;
    constexpr float MeterTop        = 6.0f;
    constexpr float MeterBottom     = 26.0f;

    VrColour rgb(uint32_t colour, float alpha = 1.0f) {
      return { float((colour >> 16) & 0xFF) / 255.0f, float((colour >> 8) & 0xFF) / 255.0f,
        float(colour & 0xFF) / 255.0f, alpha };
    }

    float stripRowScale(const VrCompassConfig& config, float width);
    float markerRowTop(const VrCompassConfig& config, float width);
    float markerRowHeight(const VrCompassConfig& config);

    // Top of the health bar's row: just below the markers, which move with the
    // gap, the strip's height and their size, but always inside the image (the
    // markers then keep above it)
    float healthRowTop(const VrCompassConfig& config, float width) {
      float gap   = config.backdrop ? config.markerGap : config.markerGapNoBackdrop;
      float top   = markerRowTop(config, width) + markerRowHeight(config) + std::min(gap, 14.0f) + HealthSpace;
      float light = std::clamp(config.stripHeight, 0.5f, 1.0f);
      return std::min(top, float(VrCompassImage::Height) - HealthRowHeight * light - 1.0f);
    }

    // The label "HP" and the meter, its full part bright and the rest faint
    void paintHealth(VrPaintList& list, const VrCompassConfig& config, const VrPaintFont* font,
        const VrImage* meter, const VrHealthState& health) {
      float width    = float(list.width());
      float fraction = std::clamp(health.fraction, 0.0f, 1.0f);
      VrColour c     = vrHealthColour(config, fraction).faded(std::clamp(health.pulse, 0.0f, 1.0f));

      float group = std::clamp(config.healthWidth, 0.1f, 1.0f) * width;
      float left  = config.healthAlign == VrHealthAlign::Right
        ? width - EdgeSpace - group : 0.5f * (width - group);
      // As light as the strip: thinner with it
      float light        = std::clamp(config.stripHeight, 0.5f, 1.0f);
      float HealthRowTop = healthRowTop(config, width);
      float rowHeight    = HealthRowHeight * light;
      float HealthRowEnd = HealthRowTop + rowHeight;
      float cap          = HealthCapFull * light;

      float labelWidth = 0.0f;

      if (font) {
        float capitals = std::max(font->metrics.glyph('H').ascent, 1.0f);
        float scale = cap / capitals;
        labelWidth = VrPaintList::measureText(*font, "HP", scale) + HealthLabelGap;
        list.drawText(*font, "HP", left, HealthRowTop + 0.5f * (rowHeight + cap), scale, c);
      }

      float x0 = left + labelWidth;
      float x1 = left + group;

      if (x1 - x0 < 8.0f)
        return;

      float xs = x0 + (x1 - x0) * fraction;

      if (meter && config.healthPicture) {
        // The meter's visible part stretched over the row, cut where the health ends
        float u0 = MeterLeft / MeterTexels;
        float u1 = MeterRight / MeterTexels;
        float v0 = MeterTop / MeterRows;
        float v1 = MeterBottom / MeterRows;
        float us = u0 + (u1 - u0) * fraction;

        if (xs > x0)
          list.drawImage(*meter, VrPaintImageUse::Shape, x0, HealthRowTop, xs, HealthRowEnd, u0, v0, us, v1, c);
        if (xs < x1)
          list.drawImage(*meter, VrPaintImageUse::Shape, xs, HealthRowTop, x1, HealthRowEnd, us, v0, u1, v1,
            c.faded(HealthEmpty));
      } else {
        // Drawn as the own strip: a thin line with a faint glow, and tick marks
        // rising from it as densely as on the game's HP meter
        float line = HealthRowTop + 0.75f * rowHeight;
        float rise = 0.75f * OwnTickRise * light;
        constexpr uint32_t Ticks = 14;

        auto part = [&] (float a, float b, const VrColour& colour) {
          if (b <= a)
            return;
          list.fillRect(a, line - 0.5f * OwnGlow, b, line + 0.5f * OwnGlow, colour.faded(0.15f));
          list.fillRect(a, line - 0.5f * OwnLine, b, line + 0.5f * OwnLine, colour.faded(0.9f));
        };

        part(x0, xs, c);
        part(xs, x1, c.faded(HealthEmpty));

        for (uint32_t i = 0; i <= Ticks; i++) {
          float x = x0 + (x1 - x0) * float(i) / float(Ticks);
          list.fillRect(x - 0.5f * OwnTick, line - rise, x + 0.5f * OwnTick, line,
            x <= xs + 0.5f ? c : c.faded(HealthEmpty));
        }
      }
    }

    // The drawn weapon's rounds at the strip's left end, in the health bar's
    // row and look: "loaded/size", a tick per round (or a filled line for a
    // large clip) and the rounds left; or the numbers only
    void paintAmmo(VrPaintList& list, const VrCompassConfig& config, const VrPaintFont* font,
        const VrColour& hud, const VrAmmoState& ammo) {
      float width = float(list.width());
      float group = std::clamp(config.healthWidth, 0.1f, 1.0f) * width;
      float left  = EdgeSpace;
      float right = left + group;
      float light = std::clamp(config.stripHeight, 0.5f, 1.0f);
      float top   = healthRowTop(config, width);
      float rowHeight = HealthRowHeight * light;
      float line  = top + 0.75f * rowHeight;
      float rise  = 0.75f * OwnTickRise * light;
      float cap   = HealthCapFull * light;
      float base  = top + 0.5f * (rowHeight + cap);

      auto bandColour = [&] (uint32_t band) {
        return band == 2 ? rgb(config.healthCriticalColour) : band == 1 ? rgb(config.healthLowColour) : hud;
      };

      int32_t capacity = std::max(ammo.capacity, 1);
      int32_t clip     = std::clamp(ammo.clip, 0, capacity);
      VrColour clipColour    = bandColour(vrAmmoClipBand(config, ammo));
      VrColour reserveColour = bandColour(vrAmmoReserveBand(config, ammo));

      std::string clipText    = std::to_string(clip) + "/" + std::to_string(capacity);
      std::string reserveText = std::to_string(std::max(ammo.reserve, 0));

      float x0 = left;
      float x1 = right;

      if (font) {
        float capitals = std::max(font->metrics.glyph('8').ascent, 1.0f);
        float scale = 1.2f * cap / capitals;
        float gap   = HealthLabelGap * light + 4.0f;

        if (config.ammoNumbers) {
          // "2/6 | 48": the clip, a short bar between, the rounds left
          float clipWidth = VrPaintList::measureText(*font, clipText, scale);
          list.drawText(*font, clipText, left, base, scale, clipColour);
          float bar = left + clipWidth + gap;
          list.fillRect(bar, base - 1.2f * cap, bar + OwnTick, base, hud.faded(0.5f));
          list.drawText(*font, reserveText, bar + OwnTick + gap, base, scale, reserveColour);
          return;
        }

        // Places as wide as the largest numbers of this clip, so that the
        // ticks do not move while firing
        std::string digits(std::to_string(capacity).size(), '8');
        float clipSlot    = VrPaintList::measureText(*font, digits + "/" + digits, scale);
        float reserveSlot = VrPaintList::measureText(*font,
          std::string(std::max<size_t>(reserveText.size(), 3), '8'), scale);

        list.drawText(*font, clipText, left, base, scale, clipColour);
        float reserveWidth = VrPaintList::measureText(*font, reserveText, scale);
        list.drawText(*font, reserveText, right - reserveWidth, base, scale, reserveColour);

        x0 = left + clipSlot + gap;
        x1 = right - reserveSlot - gap;
      } else if (config.ammoNumbers) {
        return;
      }

      if (x1 - x0 < 8.0f)
        return;

      float xs = x0 + (x1 - x0) * float(clip) / float(capacity);

      auto part = [&] (float a, float b, const VrColour& colour) {
        if (b <= a)
          return;
        list.fillRect(a, line - 0.5f * OwnGlow, b, line + 0.5f * OwnGlow, colour.faded(0.15f));
        list.fillRect(a, line - 0.5f * OwnLine, b, line + 0.5f * OwnLine, colour.faded(0.9f));
      };

      VrColour empty = clipColour.faded(HealthEmpty);
      part(x0, xs, clipColour);
      part(xs, x1, empty);

      if (capacity <= MaxRoundTicks) {
        // A tick per round, in the middle of its share of the line
        for (int32_t i = 0; i < capacity; i++) {
          float x = x0 + (x1 - x0) * (float(i) + 0.5f) / float(capacity);
          list.fillRect(x - 0.5f * OwnTick, line - rise, x + 0.5f * OwnTick, line, i < clip ? clipColour : empty);
        }
      } else {
        constexpr uint32_t Ticks = 14;

        for (uint32_t i = 0; i <= Ticks; i++) {
          float x = x0 + (x1 - x0) * float(i) / float(Ticks);
          list.fillRect(x - 0.5f * OwnTick, line - rise, x + 0.5f * OwnTick, line, x <= xs + 0.5f ? clipColour : empty);
        }
      }
    }

    // Own marks, only without the game's strip, in its look
    const char* const Labels[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    constexpr float CardinalCapFull = 32.0f;   // at the full strip height
    constexpr float OrdinalCapFull  = 24.0f;

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

    // Pixels of the image per texel of the strip across, for the span
    float stripScale(const VrCompassConfig& config, float width) {
      return width / (std::max(config.span, 1.0f) / 360.0f * StripTexels);
    }

    // Pixels per texel up and down: as across, but never into the markers' row,
    // and flattened by the strip's height setting
    float stripRowScale(const VrCompassConfig& config, float width) {
      return std::clamp(config.stripHeight, 0.5f, 1.0f)
        * std::min(stripScale(config, width), (MarkerRowTop - StripY - 4.0f) / (StripBottom - StripTop));
    }

    // The markers' row: below the strip, as far from it as at the full height,
    // and as high as the marker size gives
    float markerRowTop(const VrCompassConfig& config, float width) {
      float full = (MarkerRowTop - StripY - 4.0f) / (StripBottom - StripTop);
      float k    = std::min(stripScale(config, width), full);
      return MarkerRowTop - (StripBottom - StripTop) * (k - stripRowScale(config, width));
    }

    float markerRowHeight(const VrCompassConfig& config) {
      return std::clamp(config.markerSize, 0.5f, 1.5f) * (MarkerRowEnd - MarkerRowTop);
    }

    void paintGameStrip(VrPaintList& list, const VrCompassConfig& config, float heading,
        const VrColour& colour, const VrImage& strip) {
      float width = float(list.width());
      float k     = stripScale(config, width);
      float top   = StripY;
      float bottom = StripY + (StripBottom - StripTop) * stripRowScale(config, width);
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

    // The strip drawn in the look of the game's HP meter: a thin line with tick
    // marks rising from it and the labels in it, lighter than the game's picture
    void paintOwnStrip(VrPaintList& list, const VrCompassConfig& config, float heading,
        const VrColour& colour, const VrPaintFont* font) {
      float width = float(list.width());
      float light = std::clamp(config.stripHeight, 0.5f, 1.0f);
      float line  = StripY + (StripLineRow - StripTop) * stripRowScale(config, width);
      float capitals = font ? std::max(font->metrics.glyph('A').ascent, 1.0f) : 1.0f;
      float CardinalCap = CardinalCapFull * light;
      float OrdinalCap  = OrdinalCapFull * light;
      float rise        = OwnTickRise * light;

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
          float xe   = std::min(x + 12.0f, x1);
          float fade = edgeFade(0.5f * (x + xe), width);
          list.fillRect(x, line - 0.5f * OwnGlow, xe, line + 0.5f * OwnGlow, colour.faded(0.15f * fade));
          list.fillRect(x, line - 0.5f * OwnLine, xe, line + 0.5f * OwnLine, colour.faded(0.9f * fade));
        }

        // Short ticks every 5 degrees between the steps, as dense as the HP meter's
        for (int32_t third = 1; third <= 2; third++) {
          float xm = step.x + (next.x - step.x) * float(third) / 3.0f;

          if (xm > x0 && xm < x1)
            list.fillRect(xm - 0.5f * OwnTick, line - 0.6f * rise, xm + 0.5f * OwnTick, line,
              colour.faded(0.8f * edgeFade(xm, width)));
        }

        if (step.x < 0.0f || step.x > width)
          continue;

        float fade = edgeFade(step.x, width);
        int32_t m = ((n % 24) + 24) % 24;

        if (step.gap == 0.0f) {
          // A tick mark rising from the line, as on the HP meter
          float h = m % 3 == 0 ? 1.4f * rise : rise;
          list.fillRect(step.x - 0.5f * OwnTick, line - h, step.x + 0.5f * OwnTick, line, colour.faded(fade));
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
      const VrCompassPictures& pictures, const VrHealthState& health, const VrAmmoState& ammo) {
    float width  = float(list.width());
    float height = float(list.height());

    list.clear();

    if (config.health && health.known)
      paintHealth(list, config, font, pictures.healthBar, health);

    if (config.ammo && ammo.known && ammo.capacity > 0)
      paintAmmo(list, config, font, colour, ammo);

    if (!config.enabled)
      return;

    // The markers keep out of the health bar's row
    height = std::min(height, healthRowTop(config, width) - 6.0f);

    // A dark band behind the strip, round at its ends
    if (config.backdrop && config.backdropOpacity > 0.0f) {
      float bandBottom = StripY + (StripBottom - StripTop) * stripRowScale(config, width);
      list.fillRoundedRect(0.0f, StripY, width, bandBottom, 0.5f * (bandBottom - StripY),
        { 0.0f, 0.0f, 0.0f, config.backdropOpacity });
    }

    if (pictures.strip && config.gameStrip)
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

    float rowHeight = markerRowHeight(config);
    float gap       = config.backdrop ? config.markerGap : config.markerGapNoBackdrop;
    float rowTop    = markerRowTop(config, width) + gap;
    float rowEnd    = rowTop + rowHeight;
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
            float top  = rowTop - TickTop * scale;
            list.drawImage(*pictures.person, VrPaintImageUse::Shape, left, top,
              left + TickSize * scale, top + TickSize * scale, 0.0f, 0.0f, 1.0f, 1.0f, c);
          } else {
            list.fillRect(x - 3.0f, rowTop, x + 3.0f, rowEnd, c);
          }

          if (font && marker.initial) {
            std::string initial(1, marker.initial);
            float size      = std::clamp(config.markerSize, 0.5f, 1.5f);
            float textScale = 22.0f * size / capitals;
            list.drawText(*font, initial, x + 8.0f * size, rowEnd - 4.0f * size, textScale, c);
          }

          continue;
        }

        const VrImage* picture = kind == VrCompassMarkerKind::Quest ? pictures.quest : pictures.custom;
        VrColour c = colour.faded(alpha);

        if (picture) {
          // The quest's arrow may be larger; it grows upwards, clear of the
          // health bar's row, but not above the image
          float size  = kind == VrCompassMarkerKind::Quest ? std::clamp(config.questScale, 0.5f, 2.0f) : 1.0f;
          float scale = size * rowHeight / (MarkerBottom - MarkerTop);
          float left = x - MarkerCentreX * scale;
          // Kept inside the image: a large arrow moved down grows upwards
          float tip  = std::max(std::min(rowTop, height - 2.0f - size * rowHeight), 0.0f);
          float top  = tip - MarkerTop * scale;
          list.drawImage(*picture, VrPaintImageUse::Shape, left, top,
            left + MarkerSize * scale, top + MarkerSize * scale, 0.0f, 0.0f, 1.0f, 1.0f, c);
        } else {
          // The game's arrow: a pentagon pointing up, filled or hollow
          float h = (kind == VrCompassMarkerKind::Quest ? std::clamp(config.questScale, 0.5f, 2.0f) : 1.0f) * rowHeight;
          float w = 0.33f * h;
          float t = std::max(std::min(rowTop, height - 2.0f - h), 0.0f);
          const float arrow[] = {
            x, t,  x + w, t + 0.45f * h,  x + w, t + h,
            x - w, t + h,  x - w, t + 0.45f * h };

          if (kind == VrCompassMarkerKind::Quest)
            list.fillPolygon(arrow, 5, c);
          else
            list.strokePolygon(arrow, 5, 3.0f, c);
        }
      }
    }

  }


  bool vrSetCompassValue(VrCompassConfig& config, const std::string& name, const std::string& value) {
    auto number = [&] (float low, float high, float& out) {
      char* end = nullptr;
      float v = std::strtof(value.c_str(), &end);

      if (value.empty() || !end || *end != '\0' || !std::isfinite(v) || v < low || v > high)
        return false;

      out = v;
      return true;
    };

    // Degrees, or "auto" for a share of the field of view (stored as 0)
    auto degrees = [&] (float low, float high, float& out) {
      if (value == "auto") {
        out = 0.0f;
        return true;
      }

      float v = 0.0f;

      if (!number(low, high, v))
        return false;

      out = v == 0.0f ? 0.01f : v;
      return true;
    };

    VrCompassConfig c = config;
    bool ok = false;

    if (name == "width")         ok = number(0.05f, 1.0f, c.widthShare);
    else if (name == "top")      ok = number(-1.0f, 1.0f, c.topShare);
    else if (name == "angle")    ok = degrees(10.0f, 120.0f, c.angle);
    else if (name == "topdeg")   ok = degrees(-60.0f, 80.0f, c.top);
    else if (name == "distance") ok = number(0.3f, 10.0f, c.distance);
    else if (name == "span")     ok = number(30.0f, 360.0f, c.span);
    else if (name == "opacity")  ok = number(0.0f, 1.0f, c.backdropOpacity);
    else if (name == "gap")      ok = number(-40.0f, 14.0f, c.markerGap);
    else if (name == "gapnoband") ok = number(-40.0f, 14.0f, c.markerGapNoBackdrop);
    else if (name == "quest")    ok = number(0.5f, 2.0f, c.questScale);
    else if (name == "stripheight") ok = number(0.5f, 1.0f, c.stripHeight);
    else if (name == "markersize")  ok = number(0.5f, 1.5f, c.markerSize);
    else if (name == "ammolow")        ok = number(0.0f, 1.0f, c.ammoLow);
    else if (name == "ammoreservelow") ok = number(0.0f, 10.0f, c.ammoReserveLow);
    else if (name == "ammostyle") {
      ok = value == "bars" || value == "numbers";
      c.ammoNumbers = value == "numbers";
    }
    else if (name == "ammo") {
      ok = value == "on" || value == "off";
      c.ammo = value == "on";
    }
    else if (name == "healthpicture") {
      ok = value == "on" || value == "off";
      c.healthPicture = value == "on";
    }
    else if (name == "gamestrip") {
      ok = value == "on" || value == "off";
      c.gameStrip = value == "on";
    }
    else if (name == "healthwidth")    ok = number(0.1f, 1.0f, c.healthWidth);
    else if (name == "healthlow")      ok = number(0.0f, 1.0f, c.healthLow);
    else if (name == "healthcritical") ok = number(0.0f, 1.0f, c.healthCritical);
    else if (name == "healthalign") {
      ok = value == "centre" || value == "center" || value == "right";
      c.healthAlign = value == "right" ? VrHealthAlign::Right : VrHealthAlign::Centre;
    } else if (name == "backdrop" || name == "compass" || name == "health" || name == "pulse") {
      ok = value == "on" || value == "off";
      bool on = value == "on";
      (name == "backdrop" ? c.backdrop : name == "compass" ? c.enabled : name == "health" ? c.health : c.healthPulse) = on;
    }

    if (ok)
      config = c;

    return ok;
  }


  std::string vrDescribeCompass(const VrCompassConfig& c) {
    char text[640];
    auto deg = [] (float v, char* out, size_t size) {
      if (v == 0.0f)
        std::snprintf(out, size, "auto");
      else
        std::snprintf(out, size, "%g", v);
    };
    char angle[16];
    char top[16];
    deg(c.angle, angle, sizeof(angle));
    deg(c.top, top, sizeof(top));
    std::snprintf(text, sizeof(text),
      "width=%g top=%g angle=%s topdeg=%s distance=%g span=%g backdrop=%s opacity=%g gap=%g gapnoband=%g quest=%g"
      " stripheight=%g markersize=%g gamestrip=%s ammo=%s ammostyle=%s ammolow=%g ammoreservelow=%g compass=%s health=%s healthpicture=%s healthalign=%s healthwidth=%g healthlow=%g healthcritical=%g pulse=%s",
      c.widthShare, c.topShare, angle, top, c.distance, c.span, c.backdrop ? "on" : "off",
      c.backdropOpacity, c.markerGap, c.markerGapNoBackdrop, c.questScale,
      c.stripHeight, c.markerSize, c.gameStrip ? "on" : "off",
      c.ammo ? "on" : "off", c.ammoNumbers ? "numbers" : "bars", c.ammoLow, c.ammoReserveLow,
      c.enabled ? "on" : "off", c.health ? "on" : "off", c.healthPicture ? "on" : "off", c.healthAlign == VrHealthAlign::Right ? "right" : "centre",
      c.healthWidth, c.healthLow, c.healthCritical, c.healthPulse ? "on" : "off");
    return text;
  }


  uint32_t vrAmmoClipBand(const VrCompassConfig& config, const VrAmmoState& ammo) {
    if (ammo.clip <= 0)
      return 2;

    // At least one round low, so that a small clip warns before its last shot
    int32_t low = std::max(int32_t(std::lround(float(ammo.capacity) * std::clamp(config.ammoLow, 0.0f, 1.0f))), 1);
    return ammo.clip <= low && ammo.capacity > 1 ? 1 : 0;
  }


  uint32_t vrAmmoReserveBand(const VrCompassConfig& config, const VrAmmoState& ammo) {
    if (ammo.reserve <= 0)
      return 2;

    return float(ammo.reserve) < float(std::max(ammo.capacity, 1)) * std::max(config.ammoReserveLow, 0.0f) ? 1 : 0;
  }


  VrColour vrHealthColour(const VrCompassConfig& config, float fraction) {
    if (fraction < config.healthCritical)
      return rgb(config.healthCriticalColour);

    if (fraction < config.healthLow)
      return rgb(config.healthLowColour);

    return rgb(config.healthColour);
  }


  float vrHealthPulse(const VrCompassConfig& config, float fraction, double seconds) {
    if (!config.healthPulse || !(fraction < config.healthCritical))
      return 1.0f;

    // A smooth rise and fall once a second, in six steps between 0.45 and 1
    double phase = std::fmod(seconds, 1.0);
    float wave = 0.5f + 0.5f * float(std::cos(phase * 2.0 * 3.14159265358979));
    float step = std::round(wave * 6.0f) / 6.0f;
    return 0.45f + 0.55f * step;
  }


  VrCompassPlacement vrCompassPlace(const VrCompassConfig& config, const VrFov& fov) {
    constexpr float Degrees = 180.0f / 3.14159265f;
    float horizontal = (fov.angleRight - fov.angleLeft) * Degrees;
    float up = fov.angleUp * Degrees;

    VrCompassPlacement place;
    place.angle = config.angle > 0.0f ? config.angle
      : horizontal > 1.0f ? std::clamp(config.widthShare, 0.05f, 1.0f) * horizontal : 40.0f;

    float top = config.top != 0.0f ? config.top
      : up > 1.0f ? std::clamp(config.topShare, -1.0f, 1.0f) * up : 30.0f;

    // The strip's height as an angle, from its image's aspect
    float height = place.angle * float(VrCompassImage::Height) / float(VrCompassImage::Width);
    place.elevation = top - 0.5f * height;
    return place;
  }


  VrPose vrCompassPose(const VrCompassConfig& config, const VrCompassPlacement& place) {
    float e = place.elevation * Pi / 180.0f;

    // The head looks along -Z with +Y up; the panel faces along its +Z,
    // tilted about X by the elevation to face the eye
    VrPose pose;
    pose.position    = { 0.0f, config.distance * std::sin(e), -config.distance * std::cos(e) };
    pose.orientation = vrQuaternionFromAxisAngle({ 1.0f, 0.0f, 0.0f }, e);
    return pose;
  }


  float vrCompassWidth(const VrCompassConfig& config, const VrCompassPlacement& place) {
    return 2.0f * config.distance * std::tan(0.5f * place.angle * Pi / 180.0f);
  }

}
