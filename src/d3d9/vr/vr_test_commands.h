#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "vr_backend.h"

namespace dxvk {

  /**
   * \brief Requests of the VR test channel (d3d9.vrTestChannel)
   *
   * One text line per request, see docs/testing/vr-test-channel.md. Parsing
   * is kept free of Windows and Vulkan so that it can be unit tested.
   */
  enum class VrTestVerb : uint32_t {
    Invalid,
    Ping,        ///< ping
    State,       ///< state
    Memory,      ///< memory: address space and memory of the game process
    Head,        ///< head <x> <y> <z> <yaw> <pitch> [roll]
    Hand,        ///< hand left|right <x> <y> <z> <yaw> <pitch> [roll]
    Release,     ///< release head|left|right|move|turn|all
    Press,       ///< press <action>: down for a few frames
    Hold,        ///< hold <action>: down until "up"
    Up,          ///< up <action>
    Stick,       ///< stick move|turn <x> <y>
    Aim,         ///< aim left|right <panel> <u> <v>
    Wait,        ///< wait <frames>
    WaitFrame,   ///< waitframe <frame number>
    CaptureEyes, ///< capture eyes <file prefix>, the rest of the line (may hold spaces)
    CapturePanel,///< capture panel <panel> <file>, the rest of the line (may hold spaces)
    PipBoy,      ///< pipboy: where its screen is on the left hand, and whether it is open or looked at
    PipBoyOpen,  ///< pipboy open: opens it the way the gesture does, when the game allows
    PipBoyClose, ///< pipboy close: closes it the way looking away does
    SignalOn,    ///< signal on: counts the "done" signal (both hands above the head) from now on
    SignalOff,   ///< signal off
  };

  /**
   * \brief What "release" gives back to the real backend
   */
  enum class VrTestRelease : uint32_t {
    Head,
    Left,
    Right,
    Move,
    Turn,
    All,
  };

  enum class VrTestStick : uint32_t {
    Move,
    Turn,
  };

  struct VrTestCommand {
    VrTestVerb    verb    = VrTestVerb::Invalid;
    VrHand        hand    = VrHand::Left;
    VrPose        pose;
    VrAction      action  = VrAction::Attack;
    VrTestRelease release = VrTestRelease::All;
    VrTestStick   stick   = VrTestStick::Move;
    VrVector2     value;
    VrPanelId     panel   = VrPanelId::Menu;
    float         u       = 0.0f;
    float         v       = 0.0f;
    uint64_t      count   = 0;
    std::string   path;
    std::string   error;
  };

  /// Longest request line the channel accepts
  constexpr size_t VrTestMaxLine = 4096;

  /**
   * \brief Parses one request line
   *
   * Angles are in degrees: yaw turns left for positive values, pitch looks
   * up, roll tilts to the left, applied in that order, as for a head. Positions
   * are in metres in tracking space.
   */
  VrTestCommand vrParseTestCommand(std::string_view line);

  /**
   * \brief Pose from a position and yaw, pitch and roll in degrees
   */
  VrPose vrTestPose(const VrVector3& position, float yawDeg, float pitchDeg, float rollDeg);

  const char* vrTestActionName(VrAction action);
  const char* vrTestPanelName(VrPanelId panel);
  bool vrTestFindAction(std::string_view name, VrAction& action);
  bool vrTestFindPanel(std::string_view name, VrPanelId& panel);

}
