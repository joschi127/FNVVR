#include <cstring>

#include "../../util/log/log.h"

#include "vr_math.h"
#include "vr_pipboy_hint.h"

namespace dxvk {

  namespace {

    const VrColour Plate = { 0.0f, 0.0f, 0.0f, 0.7f };

    constexpr float FullTurn = 6.2831853f;

  }


  void vrPaintPipBoyHint(VrPaintList& list, const std::string& action,
      const VrColour& colour, const VrPaintFont* font, bool hovered) {
    float width  = float(list.width());
    float height = float(list.height());

    list.clear();
    list.fillRect(0.0f, 0.0f, width, height, Plate);

    // Square corners, and a thin frame in the HUD's colour while a laser
    // is on it, as the game's own buttons have
    if (hovered) {
      float frame = 0.025f * height;
      list.fillRect(0.0f, 0.0f, width, frame, colour);
      list.fillRect(0.0f, height - frame, width, height, colour);
      list.fillRect(0.0f, frame, frame, height - frame, colour);
      list.fillRect(width - frame, frame, width, height - frame, colour);
    }

    // The button: a ring around its letter, as on the game's button images
    float cx = 0.5f * height;
    float cy = 0.5f * height;
    list.fillSector(cx, cy, 0.29f * height, 0.35f * height, 0.0f, FullTurn, colour);

    if (!font)
      return;

    list.drawTextBlock(*font, "B", cx, cy, 0.4f * height, 1.0f, 0.5f, 1, colour);
    list.drawTextBlock(*font, action, 0.5f * (height + width), cy, width - 1.15f * height, 1.0f, 0.5f, 1, colour);
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
      vrPaintPipBoyHint(m_list, m_action, VrGameImages::hudColour(), m_images.font(), hovered);
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
