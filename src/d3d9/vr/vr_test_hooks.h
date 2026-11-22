#pragma once

#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief What the VR test channel asks of the Pip-Boy
   */
  enum class VrTestPipBoyRequest : uint32_t {
    None,
    Open,
    Close,
  };

  /**
   * \brief The Pip-Boy as the renderer last saw it, for the VR test channel
   *
   * \c screen and \c facing are where the Pip-Boy's screen is on the left
   * controller, in metres forward, up and right of it, and the way it faces.
   */
  struct VrTestPipBoyInfo {
    bool      known   = false;
    VrVector3 screen;
    VrVector3 facing;
    bool      looking = false;
    /// 0 closed, 1 opening or closing, 2 open
    uint32_t  state   = 0;
  };

  /**
   * \brief Where the VR test channel and the renderer meet
   *
   * Only enabled with d3d9.vrTestChannel; otherwise the renderer neither
   * publishes nor takes anything. Render thread only: the test backend
   * works its requests off while the renderer polls input.
   */
  class VrTestHooks {

  public:

    static void enable() { s_enabled = true; }

    static bool enabled() { return s_enabled; }

    /// Asks the renderer to open or close the Pip-Boy the way the gesture does
    static void requestPipBoy(VrTestPipBoyRequest request) { s_pipBoyRequest = request; }

    /// The request, once: the renderer acts on it when the game allows
    static VrTestPipBoyRequest takePipBoyRequest() {
      VrTestPipBoyRequest request = s_pipBoyRequest;
      s_pipBoyRequest = VrTestPipBoyRequest::None;
      return request;
    }

    static void publishPipBoy(const VrTestPipBoyInfo& info) { s_pipBoy = info; }

    static const VrTestPipBoyInfo& pipBoy() { return s_pipBoy; }

  private:

    static inline bool                s_enabled       = false;
    static inline VrTestPipBoyRequest s_pipBoyRequest = VrTestPipBoyRequest::None;
    static inline VrTestPipBoyInfo    s_pipBoy;

  };

}
