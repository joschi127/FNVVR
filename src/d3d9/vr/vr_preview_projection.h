#pragma once

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Where a point in tracking space lands in an eye's image
   *
   * For drawing panels and lasers into the emulator's preview, the way a
   * runtime's compositor places them in the headset's view.
   */
  struct VrEyePoint {
    /// From 0 at the image's left edge to 1 at its right edge
    float u   = 0.0f;
    /// From 0 at the image's top edge to 1 at its bottom edge
    float v   = 0.0f;
    /// One over the distance in front of the eye, for perspective correct texturing
    float rhw = 0.0f;
  };

  /**
   * \brief Projects a point into an eye's image
   *
   * \returns \c false if the point is not in front of the eye
   */
  bool vrProjectToEye(const VrEyeView& eye, const VrVector3& point, VrEyePoint& result);

  /**
   * \brief The head pose between the two eyes, for head-anchored panels
   */
  VrPose vrHeadFromEyes(const VrEyeView& left, const VrEyeView& right);

}
