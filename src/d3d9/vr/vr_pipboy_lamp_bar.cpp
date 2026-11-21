#include <cstring>

#include "../../util/log/log.h"

#include "vr_math.h"
#include "vr_pipboy_lamp_bar.h"

namespace dxvk {

  namespace {

    using Layout = VrPipBoyLampBarLayout;

    // The arm's gamepad texture, with the LT and RT tabs, and the glow the
    // game puts on the knob of the open view
    constexpr char ArmTexture[]  = "pipboy3000\\xbox\\pipboyarm01.dds";
    constexpr char GlowTexture[] = "pipboy3000\\pipboybtnglow01.dds";

    const VrColour Plate    = { 0.17f, 0.18f, 0.17f, 1.0f };
    const VrColour TabMetal = { 0.35f, 0.38f, 0.38f, 1.0f };
    const VrColour TabInk   = { 0.08f, 0.08f, 0.08f, 1.0f };
    const VrColour White    = { 1.0f, 1.0f, 1.0f, 1.0f };
    // The game's glow is orange: the lamps are red
    const VrColour Lamp     = { 1.0f, 0.45f, 0.35f, 1.0f };

    void paintTab(VrPaintList& list, float left, float right, const char* name, const VrPaintFont* font) {
      float width  = float(list.width());
      float height = float(list.height());

      float x0 = left  * width;
      float x1 = right * width;
      float y0 = Layout::TabTop    * height;
      float y1 = Layout::TabBottom * height;

      list.fillRect(x0, y0, x1, y1, TabMetal);

      if (font)
        list.drawTextBlock(*font, name, 0.5f * (x0 + x1), 0.5f * (y0 + y1), x1 - x0, 1.0f, 0.4f, 1, TabInk);
    }

  }


  void vrPaintPipBoyLampBar(VrPaintList& list, const VrImage* arm, const VrImage* glow,
      VrPipBoyView view, const VrPaintFont* font) {
    float width  = float(list.width());
    float height = float(list.height());

    list.clear();

    if (arm) {
      list.drawImage(*arm, VrPaintImageUse::PictureWithMipmaps, 0.0f, 0.0f, width, height,
        Layout::StripLeft, Layout::StripTop, Layout::StripRight, Layout::StripBottom, White);
    } else {
      list.fillRect(0.0f, 0.0f, width, height, Plate);
    }

    paintTab(list, Layout::LeftTabLeft,  Layout::LeftTabRight,  "LG", font);
    paintTab(list, Layout::RightTabLeft, Layout::RightTabRight, "RG", font);

    if (view == VrPipBoyView::None || !glow)
      return;

    const float knobs[3] = { Layout::KnobX0, Layout::KnobX1, Layout::KnobX2 };
    float cx   = knobs[uint32_t(view) - 1] * width;
    float cy   = Layout::KnobY * height;
    float half = 1.6f * Layout::KnobRadius * width;

    list.drawImage(*glow, VrPaintImageUse::Picture, cx - half, cy - half, cx + half, cy + half,
      0.0f, 0.0f, 1.0f, 1.0f, Lamp);
  }


  VrPipBoyLampBar::VrPipBoyLampBar(VrPainter& painter, IVRBackend& backend, VrGameImages& images)
  : m_panel(painter, backend, VrPanelId::PipBoyLampBar),
    m_list(Layout::ImageWidth, Layout::ImageHeight),
    m_images(images) { }


  float VrPipBoyLampBar::heightFor(float panelWidth) {
    return Layout::WidthShare * panelWidth * float(Layout::ImageHeight) / float(Layout::ImageWidth);
  }


  void VrPipBoyLampBar::show(const VrPose& panel, float width, float height, VrPipBoyView view) {
    if (m_failed)
      return;

    if (!m_drawn || view != m_view) {
      const VrImage* arm  = m_images.picture(ArmTexture);
      const VrImage* glow = m_images.picture(GlowTexture);
      vrPaintPipBoyLampBar(m_list, arm, glow, view, m_images.font());

      if (!m_panel.paint(m_list)) {
        Logger::err("VR: Failed to draw the Pip-Boy's lamp bar, it is not shown");
        m_failed = true;
        hide();
        return;
      }

      if (!arm)
        Logger::warn("VR: The Pip-Boy arm's texture was not found, the lamp bar is a plain plate");

      // The font may only be known once the game has loaded it
      m_drawn   = m_images.font() != nullptr;
      m_view    = view;
      m_repaint = true;
    }

    float barWidth  = Layout::WidthShare * width;
    float barHeight = heightFor(width);

    // Centred below the panel, in its plane
    VrVector3 offset = { 0.0f, -(0.5f * (height + barHeight) + Layout::Gap), 0.0f };

    VrPose pose = panel;
    pose.position = vrTransformPoint(panel, offset);

    // The runtime keeps the image and the place: given again only when it moves
    if (m_panel.shown() && !m_repaint && barWidth == m_shownWidth
     && std::memcmp(&pose, &m_shownPose, sizeof(pose)) == 0)
      return;

    m_panel.show(pose, barWidth);
    m_shownPose  = pose;
    m_shownWidth = barWidth;
    m_repaint    = false;
  }


  void VrPipBoyLampBar::hide() {
    m_panel.hide();
  }

}
