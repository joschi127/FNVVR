#pragma once

#include "vr_game_images.h"
#include "vr_paint_panel.h"
#include "vr_pipboy_lamp_bar_layout.h"

namespace dxvk {

  /**
   * \brief Draws the lamp bar
   *
   * \param [out] list Paint list of the layout's size
   * \param [in] arm The arm's texture, or \c nullptr for a plain plate
   * \param [in] view The view whose knob is lit
   * \param [in] font Font of the grips' names, or \c nullptr
   */
  void vrPaintPipBoyLampBar(VrPaintList& list, const VrImage* arm,
    VrPipBoyView view, const VrPaintFont* font);

  /**
   * \brief The lamp bar below the panel that shows the open Pip-Boy
   *
   * The panel shows the Pip-Boy's screen only; the arm around it, with the
   * knobs that switch the views and their lamps, stays on the wrist. See
   * VrStereoRenderer::setPipBoyPanel and docs/adr/0006-pipboy-on-a-panel.md.
   */
  class VrPipBoyLampBar {

  public:

    VrPipBoyLampBar(VrPainter& painter, IVRBackend& backend, VrGameImages& images);

    /**
     * \brief Shows the panel through the renderer's frame submitter
     */
    void setSubmitter(VrFrameSubmitter* submitter) {
      m_panel.setSubmitter(submitter);
    }

    /**
     * \brief Shows the bar below the panel
     *
     * \param [in] panel The panel's pose in tracking space, +Y up
     * \param [in] width The panel's width, in metres
     * \param [in] height The panel's height, in metres
     * \param [in] view The view whose knob is lit
     */
    void show(const VrPose& panel, float width, float height, VrPipBoyView view);

    void hide();

    /// Height of the bar below a panel of that width, in metres
    static float heightFor(float panelWidth);

  private:

    VrPaintPanel   m_panel;
    VrPaintList    m_list;
    VrGameImages&  m_images;
    VrPipBoyView   m_view    = VrPipBoyView::None;
    bool           m_drawn   = false;
    bool           m_failed  = false;
    bool           m_repaint = false;
    VrPose         m_shownPose  = { };
    float          m_shownWidth = 0.0f;

  };

}
