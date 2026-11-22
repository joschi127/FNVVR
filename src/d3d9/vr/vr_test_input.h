#pragma once

#include <array>
#include <string>

#include "vr_backend.h"
#include "vr_test_commands.h"

namespace dxvk {

  /**
   * \brief Where a panel was last shown, for aiming a hand at it
   *
   * The height follows from the width and the aspect ratio of the image.
   */
  struct VrTestPanelPlace {
    bool          shown  = false;
    VrPose        pose;
    float         width  = 0.0f;
    float         height = 0.0f;
    VrPanelAnchor anchor = VrPanelAnchor::Room;
  };

  /**
   * \brief Pose that points a hand's laser at a point of a panel
   *
   * The laser goes along the pose's -Z axis from \c origin. \c u runs from
   * the panel's left edge (0) to its right edge (1), \c v from its top (0)
   * to its bottom (1).
   *
   * \param [in] head Head pose, for panels anchored to the head
   * \param [out] target The point aimed at, in tracking space
   * \returns \c false if the panel is not shown
   */
  bool vrTestAimPose(const VrTestPanelPlace& panel, const VrPose& head, const VrVector3& origin,
    float u, float v, VrPose& aim, VrVector3& target);

  /**
   * \brief Input set by the test channel on top of the real backend's input
   *
   * Poses that are set replace the real ones until released. Actions are
   * added to the real ones (a real button still works). Sticks replace the
   * real stick while set. Not thread safe: used from the render thread only.
   */
  class VrTestInput {

  public:

    /// Frames a "press" holds an action down, so that menus see it
    constexpr static uint32_t PressFrames = 4;

    void setHead(const VrPose& pose);
    void setHand(VrHand hand, const VrPose& pose);
    void release(VrTestRelease what);
    void press(VrAction action);
    void hold(VrAction action);
    void up(VrAction action);
    void setStick(VrTestStick stick, const VrVector2& value);

    /**
     * \brief Applies the test input to one frame of real input
     *
     * Presses count down once per new \c frame, however often the frame
     * polls its input.
     */
    VrInputState merge(const VrInputState& real, uint64_t frame);

    /**
     * \brief Eye views for the set head pose
     *
     * Keeps each eye where it is relative to the real head, so the runtime's
     * eye distance and field of view stay as they are.
     */
    std::array<VrEyeView, VrEyeCount> views(const std::array<VrEyeView, VrEyeCount>& real,
      const VrPose& realHead) const;

    bool headSet() const { return m_headSet; }
    bool handSet(VrHand hand) const { return m_handSet[uint32_t(hand)]; }

    /// Short text of what is set, for the "state" answer
    std::string describe() const;

  private:

    bool m_headSet = false;
    VrPose m_head;
    std::array<bool, VrHandCount> m_handSet = { };
    std::array<VrPose, VrHandCount> m_hand;
    uint32_t m_held = 0;
    std::array<uint32_t, VrActionCount> m_pressLeft = { };
    uint64_t m_lastFrame = ~0ull;
    bool m_moveSet = false;
    bool m_turnSet = false;
    VrVector2 m_move;
    VrVector2 m_turn;

  };

}
