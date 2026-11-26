#pragma once

#include "vr_backend.h"
#include "vr_painter.h"

namespace dxvk {

  class VrFrameSubmitter;

  /**
   * \brief Shows an image drawn by the GPU on one of the backend's panels
   *
   * The paint list is drawn into a render target texture the runtime
   * reads, with premultiplied alpha. It is only drawn again when it
   * changed.
   *
   * With a frame submitter, the panel is shown and hidden through it, so
   * within a frame it goes to the backend with the eyes (on the submission
   * thread when that is on) instead of flushing the device on the game
   * thread itself. A move still goes to the backend at once: the panel was
   * shown in an earlier frame, which the submitter has finished.
   */
  class VrPaintPanel {

  public:

    VrPaintPanel(VrPainter& painter, IVRBackend& backend, VrPanelId id)
    : m_painter(painter), m_backend(backend), m_id(id) { }

    ~VrPaintPanel();

    /**
     * \brief Shows and hides the panel through a frame submitter
     *
     * \param [in] submitter The renderer's submitter, which outlives the
     *    panel, or \c nullptr to go to the backend directly
     */
    void setSubmitter(VrFrameSubmitter* submitter) {
      m_submitter = submitter;
    }

    /**
     * \brief Draws a new image of the panel
     *
     * \param [in] list What the image shows; its size is the image's
     * \returns \c false if the texture could not be made or drawn
     */
    bool paint(const VrPaintList& list);

    /**
     * \brief Shows the last image drawn
     *
     * \param [in] pose Panel pose in tracking space, or relative to the
     *    head for VrPanelAnchor::Head
     * \param [in] width Panel width, in metres
     * \param [in] anchor What the pose is relative to
     * \returns Whether the backend shows it
     */
    bool show(const VrPose& pose, float width, VrPanelAnchor anchor = VrPanelAnchor::Room);

    /**
     * \brief Moves the shown image, without giving it again where the backend can
     *
     * For an image that did not change since it was last shown; otherwise
     * the same as show.
     * Not after show() in the same frame: with a submitter, the show is
     * queued and would reach the backend after the move.
     */
    bool move(const VrPose& pose, float width, VrPanelAnchor anchor = VrPanelAnchor::Room);

    /**
     * \brief Hides the panel and lets go of its texture
     *
     * For a panel that may stay unused for long; the next paint makes it again.
     */
    void release();

    /**
     * \brief Hides the panel
     */
    void hide();

    bool shown() const {
      return m_shown;
    }

  private:

    VrPainter&             m_painter;
    IVRBackend&            m_backend;
    VrFrameSubmitter*      m_submitter = nullptr;
    VrPanelId              m_id;

    Com<IDirect3DTexture9> m_texture;
    uint32_t               m_width  = 0;
    uint32_t               m_height = 0;
    bool                   m_drawn  = false;
    bool                   m_shown  = false;

  };

}
