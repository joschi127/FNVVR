#include "vr_preview_projection.h"

#include <cmath>

#include "vr_math.h"

namespace dxvk {

  bool vrProjectToEye(const VrEyeView& eye, const VrVector3& point, VrEyePoint& result) {
    VrVector3 local = vrTransformPoint(vrInverse(eye.pose), point);
    float depth = -local.z;

    if (depth < 1e-3f)
      return false;

    float tanX = local.x / depth;
    float tanY = local.y / depth;
    float left  = std::tan(eye.fov.angleLeft);
    float right = std::tan(eye.fov.angleRight);
    float up    = std::tan(eye.fov.angleUp);
    float down  = std::tan(eye.fov.angleDown);

    if (right - left < 1e-6f || up - down < 1e-6f)
      return false;

    result.u   = (tanX - left) / (right - left);
    result.v   = (up - tanY) / (up - down);
    result.rhw = 1.0f / depth;
    return true;
  }


  VrPose vrHeadFromEyes(const VrEyeView& left, const VrEyeView& right) {
    VrPose head;
    head.orientation = left.pose.orientation;
    head.position = (left.pose.position + right.pose.position) * 0.5f;
    return head;
  }

}
