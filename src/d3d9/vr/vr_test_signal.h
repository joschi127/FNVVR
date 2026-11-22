#pragma once

#include <cstdint>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief The "done" signal of a person in the headset: both hands held above the head
   *
   * A pose, no buttons (the sticks' clicks are sneak and VATS). Counted once
   * per raise, after both hands have been above the head for the hold time;
   * the next one needs a hand to come down first. Only while armed: a test
   * tool turns it on for a test session, so it never fires while someone
   * plays alone with the test channel left on.
   */
  class VrTestSignal {

  public:

    /// How far above the head's position both controllers must be, in metres
    static constexpr float Above = 0.05f;
    /// How far a hand must come below that again before the next signal
    static constexpr float Lowered = 0.15f;

    explicit VrTestSignal(float holdSeconds) : m_hold(holdSeconds) { }

    void arm(bool on) {
      m_armed = on;
      m_held  = 0.0f;
      m_latched = false;
    }

    bool armed() const { return m_armed; }

    uint32_t count() const { return m_count; }

    /// Looks at one frame's head and hands, \c dt seconds after the last
    void update(const VrInputState& input, float dt);

  private:

    float    m_hold;
    bool     m_armed   = false;
    bool     m_latched = false;
    float    m_held    = 0.0f;
    uint32_t m_count   = 0;

  };

}
