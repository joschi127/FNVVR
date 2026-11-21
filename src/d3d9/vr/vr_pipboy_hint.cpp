#include <algorithm>
#include <cstring>

#include "../../util/log/log.h"

#include "vr_math.h"
#include "vr_pipboy_hint.h"

namespace dxvk {

  namespace {

    using Layout = VrPipBoyHintLayout;

    // The dark of the menus' background the game's buttons stand on
    const VrColour Plate = { 0.0f, 0.0f, 0.0f, 0.7f };

    // The game's B button, as its menus show it with a gamepad
    constexpr char ButtonImage[] = "interface\\shared\\button\\general_button_b.dds";

    // Without the font, the frame is as wide as with "Close"
    constexpr float TextWidthWithoutFont = 65.0f;
    constexpr float LineHeightWithoutFont = 31.0f;

  }


  void vrPaintPipBoyHint(VrPaintList& list, const std::string& action,
      const VrColour& colour, const VrPaintFont* font, const VrImage* button, bool hovered) {
    float width  = float(list.width());
    float height = float(list.height());
    float unit   = height / Layout::Units;

    list.clear();
    list.fillRect(0.0f, 0.0f, width, height, Plate);

    // From the right: the button's image, and the frame its left part
    // overlaps, around the text
    float textWidth  = font ? VrPaintList::measureText(*font, action, 1.0f) : TextWidthWithoutFont;
    float lineHeight = font ? font->metrics.lineHeight() : LineHeightWithoutFont;

    float buttonRight = width - Layout::Edge * unit;
    float buttonLeft  = buttonRight - Layout::ButtonSize * unit;
    float boxRight    = buttonLeft + Layout::ButtonOverlap * unit;
    float boxLeft     = std::max(0.0f, boxRight - (textWidth + Layout::HorizontalSpace) * unit);
    float boxHeight   = (lineHeight + Layout::VerticalSpace) * unit;
    float boxTop      = 0.5f * (height - boxHeight);
    float boxBottom   = boxTop + boxHeight;

    // The text and the button are dimmed until a laser is on the sign,
    // which also frames it
    VrColour ink = colour.faded(hovered ? 1.0f : Layout::IdleAlpha);

    if (hovered) {
      VrColour line = { colour.r * Layout::LineBrightness, colour.g * Layout::LineBrightness,
        colour.b * Layout::LineBrightness, colour.a };
      float t = Layout::LineThickness * unit;
      list.fillRect(boxLeft, boxTop, boxRight, boxTop + t, line);
      list.fillRect(boxLeft, boxBottom - t, boxRight, boxBottom, line);
      list.fillRect(boxLeft, boxTop + t, boxLeft + t, boxBottom - t, line);
      list.fillRect(boxRight - t, boxTop + t, boxRight, boxBottom - t, line);
    }

    if (button) {
      float top = 0.5f * (height - Layout::ButtonSize * unit);
      list.drawImage(*button, VrPaintImageUse::Shape, buttonLeft, top, buttonRight, top + Layout::ButtonSize * unit,
        0.0f, 0.0f, 1.0f, 1.0f, ink);
    }

    if (font) {
      // Right-aligned in the frame, the capitals centred in its height
      float textRight = boxRight - 0.5f * Layout::HorizontalSpace * unit;
      float capitals  = font->metrics.glyph('A').ascent;
      float baseline  = 0.5f * (boxTop + boxBottom) + 0.5f * capitals * unit;
      list.drawText(*font, action, textRight - textWidth * unit, baseline, unit, ink);
    }
  }


  VrPipBoyHint::VrPipBoyHint(VrPainter& painter, IVRBackend& backend, VrGameImages& images, std::string action)
  : m_panel(painter, backend, VrPanelId::PipBoyHint),
    m_list(VrPipBoyHintLayout::ImageWidth, VrPipBoyHintLayout::ImageHeight),
    m_images(images),
    m_action(std::move(action)) { }


  void VrPipBoyHint::show(const VrPose& panel, float width, float height, bool hovered) {
    if (m_failed)
      return;

    if (!m_drawn || hovered != m_hovered) {
      vrPaintPipBoyHint(m_list, m_action, VrGameImages::hudColour(), m_images.font(),
        m_images.picture(ButtonImage), hovered);
      m_hovered = hovered;

      if (!m_panel.paint(m_list)) {
        Logger::err("VR: Failed to draw the Pip-Boy's close sign, it is not shown");
        m_failed = true;
        hide();
        return;
      }

      // The font may only be known once the game has loaded it
      m_drawn   = m_images.font() != nullptr;
      m_repaint = true;
    }

    float signWidth  = VrPipBoyHintLayout::WidthShare * width;
    float signHeight = signWidth * float(VrPipBoyHintLayout::ImageHeight) / float(VrPipBoyHintLayout::ImageWidth);

    // Right-aligned with the panel, just above its top edge, in its plane
    VrVector3 offset = {
      0.5f * (width - signWidth),
      0.5f * (height + signHeight) + VrPipBoyHintLayout::Gap,
      0.0f };

    VrPose pose = panel;
    pose.position = vrTransformPoint(panel, offset);

    // The runtime keeps the image and the place: given again only when it moves
    if (m_panel.shown() && !m_repaint && signWidth == m_shownWidth
     && std::memcmp(&pose, &m_shownPose, sizeof(pose)) == 0)
      return;

    m_panel.show(pose, signWidth);
    m_shownPose  = pose;
    m_shownWidth = signWidth;
    m_repaint    = false;
  }


  void VrPipBoyHint::hide() {
    m_panel.hide();
  }

}
