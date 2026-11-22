#include "test.h"

#include <cmath>

#include "../vr_preview_projection.h"
#include "../vr_test_commands.h"

using namespace dxvk;

namespace {

  VrEyeView squareEye() {
    VrEyeView eye;
    eye.pose = vrTestPose({ 0.0f, 1.6f, 0.0f }, 0.0f, 0.0f, 0.0f);
    float a = std::atan(1.0f);  // 45 degrees to each side
    eye.fov = { -a, a, a, -a };
    return eye;
  }

}


TEST_CASE(preview_projects_the_centre_and_corners) {
  VrEyeView eye = squareEye();
  VrEyePoint p;

  CHECK(vrProjectToEye(eye, { 0.0f, 1.6f, -2.0f }, p));
  CHECK_NEAR(p.u, 0.5f, 1e-5f);
  CHECK_NEAR(p.v, 0.5f, 1e-5f);
  CHECK_NEAR(p.rhw, 0.5f, 1e-5f);

  // 45 degrees up and to the right: the top right corner
  CHECK(vrProjectToEye(eye, { 2.0f, 3.6f, -2.0f }, p));
  CHECK_NEAR(p.u, 1.0f, 1e-5f);
  CHECK_NEAR(p.v, 0.0f, 1e-5f);
}


TEST_CASE(preview_skips_points_behind_the_eye) {
  VrEyeView eye = squareEye();
  VrEyePoint p;
  CHECK(!vrProjectToEye(eye, { 0.0f, 1.6f, 1.0f }, p));
}


TEST_CASE(preview_head_is_between_the_eyes) {
  VrEyeView left = squareEye();
  VrEyeView right = squareEye();
  left.pose.position.x = -0.032f;
  right.pose.position.x = 0.032f;
  VrPose head = vrHeadFromEyes(left, right);
  CHECK_NEAR(head.position.x, 0.0f, 1e-6f);
  CHECK_NEAR(head.position.y, 1.6f, 1e-6f);
}
