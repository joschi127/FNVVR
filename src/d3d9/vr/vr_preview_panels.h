#pragma once

#include "../d3d9_include.h"

#include "../../util/com/com_pointer.h"

#include "vr_backend.h"

namespace dxvk {

  class VrEmulatorBackend;

  /**
   * \brief A panel as the emulator's preview draws it
   */
  struct VrPreviewPanel {
    Com<IDirect3DTexture9> texture;
    VrPose        pose;
    float         width  = 0.0f;
    VrPanelAnchor anchor = VrPanelAnchor::Room;
    bool          premultiplied = false;
  };

  /**
   * \brief Panels and lasers for the emulator's preview window
   *
   * A headset's runtime composites the panels and lasers into its view; the
   * emulator has no compositor, so its preview draws them itself
   * (d3d9.vrPreviewPanels). Off unless enabled: nothing is kept then.
   * Render thread only.
   */
  class VrPreviewPanels {

  public:

    /// Turns the recording on, with the emulator that knows what is shown, or off with \c nullptr
    static void enable(VrEmulatorBackend* emulator);

    /// Lets go of every recorded panel's texture, as the device's textures go
    static void clear();

    static bool enabled();

    /// Called for every panel handed to the backend through the D3D9 bridge
    static void record(VrPanelId id, IDirect3DTexture9* texture, const VrPose& pose, float width,
      VrPanelAnchor anchor, bool premultiplied);

    /// The panel if it is shown
    static bool panel(VrPanelId id, VrPreviewPanel& result);

    /// The hand's laser if it is shown
    static bool pointer(VrHand hand, VrPointerSubmission& result);

  };

}
