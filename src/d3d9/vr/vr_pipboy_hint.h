#pragma once

#include <string>

#include "vr_game_images.h"
#include "vr_paint_panel.h"

namespace dxvk {

  /**
   * \brief Layout of the sign that tells how to close the Pip-Boy's panel
   */
  struct VrPipBoyHintLayout {
    constexpr static uint32_t ImageWidth  = 280;
    constexpr static uint32_t ImageHeight = 150;
    /// Width of the sign, as a share of the panel's width: its text and
    /// button as high as the Pip-Boy's own button hints on the panel
    /// ("Equip A": capitals 2.16 %, button 3.95 % of the panel's width)
    constexpr static float    WidthShare  = 0.151f;
    /// Space between the panel's top edge and the sign, in metres
    constexpr static float    Gap         = 0.01f;

    // Text and button as the Pip-Boy's button hints, in units of the image

    /// Height of the image (its width is 140)
    constexpr static float    Units           = 75.0f;
    /// Height of the text's capitals, as a share of the button's image: the
    /// B fills about half of its image, and is 84 pixels to the capitals' 46
    /// on "Equip A"
    constexpr static float    CapitalsShare   = 0.27f;
    /// Space around the text in the frame, both sides together
    constexpr static float    HorizontalSpace = 20.0f;
    /// Space above and below the text, together
    constexpr static float    VerticalSpace   = 8.0f;
    /// The button's image, and how far it overlaps the frame's right end
    constexpr static float    ButtonSize      = 75.0f;
    constexpr static float    ButtonOverlap   = 17.0f;
    /// Space at the image's right edge
    constexpr static float    Edge            = 2.0f;
    constexpr static float    LineThickness   = 2.0f;
    constexpr static float    LineBrightness  = 200.0f / 255.0f;
  };

  /**
   * \brief Draws the sign as the game draws its text buttons: what B does,
   *    in a frame while a laser is on it, and the B button's image
   *
   * \param [out] list Paint list of the layout's size
   * \param [in] action What B does, "Close"
   * \param [in] colour The HUD's colour
   * \param [in] font Font of the text, or \c nullptr to draw no text
   * \param [in] button The game's B button image, or \c nullptr to draw none
   * \param [in] hovered Whether a laser is on it, which frames and lights it
   */
  void vrPaintPipBoyHint(VrPaintList& list, const std::string& action,
    const VrColour& colour, const VrPaintFont* font, const VrImage* button, bool hovered);

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
     * \brief Shows the panel through the renderer's frame submitter
     */
    void setSubmitter(VrFrameSubmitter* submitter) {
      m_panel.setSubmitter(submitter);
    }

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
