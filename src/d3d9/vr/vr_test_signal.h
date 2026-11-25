#pragma once

#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief The signals of a person in the headset: "done" with both hands held
   *    above the head, "repeat" with the left hand alone
   *
   * Poses, no buttons (the sticks' clicks are sneak and VATS). Each is
   * counted once per raise, after the pose has been held for the hold time;
   * the next one needs the hand to come down first. "Repeat" (show the
   * current step again) needs the right hand, the weapon hand, well below
   * the head the whole time, so raising both hands with the left a little
   * first never counts as one. Only while armed: a test tool turns them on
   * for a test session, so they never fire while someone plays alone with
   * the test channel left on.
   */
  class VrTestSignal {

  public:

    /// How far above the head's position both controllers must be, in metres
    static constexpr float Above = 0.05f;
    /// How far a hand must come below that again before the next signal
    static constexpr float Lowered = 0.15f;
    /// How far below the head the right hand must stay for "repeat", in metres
    static constexpr float RightDown = 0.25f;

    explicit VrTestSignal(float holdSeconds) : m_hold(holdSeconds) { }

    void arm(bool on) {
      m_armed = on;
      m_held  = 0.0f;
      m_latched = false;
      m_repeatHeld    = 0.0f;
      m_repeatLatched = false;
    }

    bool armed() const { return m_armed; }

    uint32_t count() const { return m_count; }

    /// How often "repeat" was given
    uint32_t repeatCount() const { return m_repeatCount; }

    /// Looks at one frame's head and hands, \c dt seconds after the last
    void update(const VrInputState& input, float dt);

  private:

    float    m_hold;
    bool     m_armed   = false;
    bool     m_latched = false;
    float    m_held    = 0.0f;
    uint32_t m_count   = 0;

    bool     m_repeatLatched = false;
    float    m_repeatHeld    = 0.0f;
    uint32_t m_repeatCount   = 0;

    void updateRepeat(float head, float left, float right, float dt);

  };

}
