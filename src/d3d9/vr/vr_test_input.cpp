#include "vr_test_input.h"

#include <cmath>
#include <cstdio>

#include "vr_math.h"

namespace dxvk {

  namespace {

    VrVector3 cross(const VrVector3& a, const VrVector3& b) {
      return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    /// Shortest rotation that turns the -Z axis into \c dir (normalized)
    VrQuaternion rotationFromForward(const VrVector3& dir) {
      VrVector3 forward = { 0.0f, 0.0f, -1.0f };
      float d = vrDot(forward, dir);

      if (d < -0.9999f)
        return vrQuaternionFromAxisAngle({ 0.0f, 1.0f, 0.0f }, 3.14159265f);

      VrVector3 axis = cross(forward, dir);
      return vrNormalize(VrQuaternion { axis.x, axis.y, axis.z, 1.0f + d });
    }

    const char* poseState(bool set) {
      return set ? "set" : "real";
    }

  }


  bool vrTestAimPose(const VrTestPanelPlace& panel, const VrPose& head, const VrVector3& origin,
      float u, float v, VrPose& aim, VrVector3& target) {
    if (!panel.shown || panel.width <= 0.0f)
      return false;

    VrPose place = panel.anchor == VrPanelAnchor::Head ? vrCompose(head, panel.pose) : panel.pose;
    VrVector3 local = { (u - 0.5f) * panel.width, (0.5f - v) * panel.height, 0.0f };
    target = vrTransformPoint(place, local);

    VrVector3 dir = target - origin;
    float length = vrLength(dir);

    if (length < 1e-4f)
      return false;

    aim.position    = origin;
    aim.orientation = rotationFromForward(dir * (1.0f / length));
    return true;
  }


  void VrTestInput::setHead(const VrPose& pose) {
    m_head = pose;
    m_headSet = true;
  }


  void VrTestInput::setHand(VrHand hand, const VrPose& pose) {
    m_hand[uint32_t(hand)] = pose;
    m_handSet[uint32_t(hand)] = true;
  }


  void VrTestInput::release(VrTestRelease what) {
    bool all = what == VrTestRelease::All;

    if (all || what == VrTestRelease::Head)  m_headSet = false;
    if (all || what == VrTestRelease::Left)  m_handSet[uint32_t(VrHand::Left)] = false;
    if (all || what == VrTestRelease::Right) m_handSet[uint32_t(VrHand::Right)] = false;
    if (all || what == VrTestRelease::Move)  m_moveSet = false;
    if (all || what == VrTestRelease::Turn)  m_turnSet = false;

    if (all) {
      m_held = 0;
      m_pressLeft = { };
    }
  }


  void VrTestInput::press(VrAction action) {
    m_pressLeft[uint32_t(action)] = PressFrames;
  }


  void VrTestInput::hold(VrAction action) {
    m_held |= 1u << uint32_t(action);
  }


  void VrTestInput::up(VrAction action) {
    m_held &= ~(1u << uint32_t(action));
    m_pressLeft[uint32_t(action)] = 0;
  }


  void VrTestInput::setStick(VrTestStick stick, const VrVector2& value) {
    if (stick == VrTestStick::Move) {
      m_move = value;
      m_moveSet = true;
    } else {
      m_turn = value;
      m_turnSet = true;
    }
  }


  VrInputState VrTestInput::merge(const VrInputState& real, uint64_t frame) {
    VrInputState state = real;

    if (m_headSet) {
      state.isHeadTracked = true;
      state.headPose = m_head;
    }

    for (uint32_t h = 0; h < VrHandCount; h++) {
      if (m_handSet[h]) {
        state.controllers[h].isActive = true;
        state.controllers[h].gripPose = m_hand[h];
        state.controllers[h].aimPose  = m_hand[h];
      }
    }

    uint32_t pressed = m_held;
    bool newFrame = frame != m_lastFrame;
    m_lastFrame = frame;

    for (uint32_t i = 0; i < VrActionCount; i++) {
      if (m_pressLeft[i]) {
        pressed |= 1u << i;

        if (newFrame)
          m_pressLeft[i]--;
      }
    }

    if (pressed || m_moveSet || m_turnSet) {
      if (!state.hasActions)
        state.actions = VrActionState();

      state.hasActions = true;
      state.actions.pressed |= pressed;

      if (m_moveSet) state.actions.move = m_move;
      if (m_turnSet) state.actions.turn = m_turn;
    }

    return state;
  }


  std::array<VrEyeView, VrEyeCount> VrTestInput::views(const std::array<VrEyeView, VrEyeCount>& real,
      const VrPose& realHead) const {
    if (!m_headSet)
      return real;

    std::array<VrEyeView, VrEyeCount> result = real;
    VrPose toHead = vrInverse(realHead);

    for (uint32_t e = 0; e < VrEyeCount; e++)
      result[e].pose = vrCompose(m_head, vrCompose(toHead, real[e].pose));

    return result;
  }


  std::string VrTestInput::describe() const {
    std::string held;

    for (uint32_t i = 0; i < VrActionCount; i++) {
      if ((m_held & (1u << i)) || m_pressLeft[i]) {
        if (!held.empty())
          held += ",";
        held += vrTestActionName(VrAction(i));
      }
    }

    char sticks[96];
    std::snprintf(sticks, sizeof(sticks), " move=%s turn=%s",
      m_moveSet ? "set" : "real", m_turnSet ? "set" : "real");

    return std::string("head=") + poseState(m_headSet)
      + " left=" + poseState(m_handSet[uint32_t(VrHand::Left)])
      + " right=" + poseState(m_handSet[uint32_t(VrHand::Right)])
      + sticks + " actions=" + (held.empty() ? "-" : held);
  }

}
