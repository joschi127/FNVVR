#include "vr_test_signal.h"

#include <algorithm>

namespace dxvk {

  void VrTestSignal::update(const VrInputState& input, float dt) {
    if (!m_armed)
      return;

    const VrControllerState& left  = input.controllers[uint32_t(VrHand::Left)];
    const VrControllerState& right = input.controllers[uint32_t(VrHand::Right)];

    if (!input.isHeadTracked || !left.isActive || !right.isActive) {
      m_held = 0.0f;
      m_repeatHeld = 0.0f;
      return;
    }

    float head   = input.headPose.position.y;
    float lowest = std::min(left.gripPose.position.y, right.gripPose.position.y);

    updateRepeat(head, left.gripPose.position.y, right.gripPose.position.y, dt);

    if (m_latched) {
      if (lowest < head + Above - Lowered)
        m_latched = false;
      return;
    }

    if (lowest < head + Above) {
      m_held = 0.0f;
      return;
    }

    m_held += std::max(dt, 0.0f);

    if (m_held >= m_hold) {
      m_count  += 1;
      m_latched = true;
      m_held    = 0.0f;
      // Lowering the right hand first after "done" is no repeat: the left
      // hand has to come down before the next one
      m_repeatLatched = true;
      m_repeatHeld    = 0.0f;
    }
  }


  void VrTestSignal::updateRepeat(float head, float left, float right, float dt) {
    if (m_repeatLatched) {
      if (left < head + Above - Lowered)
        m_repeatLatched = false;
      return;
    }

    if (left < head + Above || right > head - RightDown) {
      m_repeatHeld = 0.0f;
      return;
    }

    m_repeatHeld += std::max(dt, 0.0f);

    if (m_repeatHeld >= m_hold) {
      m_repeatCount  += 1;
      m_repeatLatched = true;
      m_repeatHeld    = 0.0f;
    }
  }

}
