#pragma once

#include <string>

#include "vr_game_images.h"
#include "vr_paint_panel.h"

namespace dxvk {

  /**
   * \brief Layout of the sign that tells how to close the Pip-Boy's panel
   */
  struct VrPipBoyHintLayout {
    constexpr static uint32_t ImageWidth  = 320;
    constexpr static uint32_t ImageHeight = 96;
    /// Width of the sign, as a share of the panel's width
    constexpr static float    WidthShare  = 0.16f;
    /// Space between the panel's top edge and the sign, in metres
    constexpr static float    Gap         = 0.01f;
  };

  /**
   * \brief Draws the sign: the B button as the game's button images show it,
   *    and what it does
   *
   * \param [out] list Paint list of the layout's size
   * \param [in] action What B does, "Close"
   * \param [in] colour The HUD's colour
   * \param [in] font Font of the text, or \c nullptr to draw no text
   * \param [in] hovered Whether a laser is on it, which frames it
   */
  void vrPaintPipBoyHint(VrPaintList& list, const std::string& action,
    const VrColour& colour, const VrPaintFont* font, bool hovered);

  /**
   * \brief The sign above the top right corner of the panel that shows the
   *    open Pip-Boy, outside of its picture
   *
   * The panel's copy of the Pip-Boy fills the whole panel, so the hint the
   * other menus carry has no place on it. See VrStereoRenderer::setPipBoyPanel.
   */
  class VrPipBoyHint {

  public:

    VrPipBoyHint(VrPainter& painter, IVRBackend& backend, VrGameImages& images, std::string action);

    /**
     * \brief Shows the sign over the panel
     *
     * \param [in] panel The panel's pose in tracking space, +Y up
     * \param [in] width The panel's width, in metres
     * \param [in] height The panel's height, in metres
     * \param [in] hovered Whether a laser is on the sign
     */
    void show(const VrPose& panel, float width, float height, bool hovered);

    void hide();

    /// Height of the sign above a panel of that width, in metres
    static float heightFor(float panelWidth) {
      return VrPipBoyHintLayout::WidthShare * panelWidth
        * float(VrPipBoyHintLayout::ImageHeight) / float(VrPipBoyHintLayout::ImageWidth);
    }

  private:

    VrPaintPanel   m_panel;
    VrPaintList    m_list;
    VrGameImages&  m_images;
    std::string    m_action;
    bool           m_drawn  = false;
    bool           m_failed = false;
    bool           m_repaint = false;
    bool           m_hovered = false;
    VrPose         m_shownPose = { };
    float          m_shownWidth = 0.0f;

  };

}
