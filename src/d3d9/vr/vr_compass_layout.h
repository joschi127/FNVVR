#pragma once

#include <string>
#include <vector>

#include "vr_paint.h"
#include "vr_types.h"

namespace dxvk {

  /**
   * \brief The compass along the top of the view, see VrCompass
   *
   * The game's direction strip around the heading the head faces, tinted
   * and fading out towards its ends as on the HUD, and below it the game's
   * markers for the active quest's target and the player's own map marker,
   * and friends. Markers outside the strip's span stay at its nearer end.
   */
  struct VrCompassConfig {
    bool  enabled   = true;
    /// Degrees of heading across the strip; the game's own compass shows
    /// about 120 (345 of the 1024 pixels of its strip)
    float span      = 120.0f;
    /// Width of the strip as an angle of the view, in degrees
    float angle     = 40.0f;
    /// How far above the middle of the view the strip is, in degrees
    float elevation = 24.0f;
    /// Distance from the head, in metres
    float distance  = 1.5f;
    /// Opacity of the dark band behind the strip, which keeps it readable
    /// against a bright sky
    float backdrop  = 0.25f;
    /// Colour of friends' markers, 0xRRGGBB, green as the game's companions;
    /// the others take the HUD's colour
    uint32_t friendColour = 0x4CE04C;
  };

  /**
   * \brief The game's compass pictures, any of them may be missing
   *
   * Without a picture its part is drawn in shapes of the same look.
   */
  struct VrCompassPictures {
    /// The direction strip: 1024 pixels for the whole circle, N at both ends
    const VrImage* strip  = nullptr;
    /// The quest objective's marker, a filled arrow
    const VrImage* quest  = nullptr;
    /// The player's own marker, the same arrow hollow
    const VrImage* custom = nullptr;
    /// The mark of a person (the game tints it per kind of actor)
    const VrImage* person = nullptr;
  };

  /**
   * \brief Image size of the compass strip, in pixels
   */
  struct VrCompassImage {
    constexpr static uint32_t Width  = 768;
    constexpr static uint32_t Height = 150;
  };

  enum class VrCompassMarkerKind : uint32_t {
    /// Target of the active quest's objective: the game's filled arrow
    Quest,
    /// The marker the player placed on the map: the game's hollow arrow
    Custom,
    /// A friend in co-op: the game's mark of a person, in green, with their initial
    Friend,
  };

  struct VrCompassMarker {
    VrCompassMarkerKind kind    = VrCompassMarkerKind::Quest;
    /// Heading towards it, radians, 0 north, clockwise, as the heading
    float               bearing = 0.0f;
    /// Initial written beside a friend's mark
    char                initial = 0;
  };

  /**
   * \brief Heading from one point to another in the game's convention
   *
   * \param [in] from Point in the game world (X east, Y north)
   * \param [in] to Point in the game world
   * \returns Radians, 0 north, clockwise
   */
  float vrCompassBearing(const VrVector3& from, const VrVector3& to);

  /**
   * \brief Angle from a heading to a bearing, between -pi and pi
   *
   * Positive is to the right (clockwise).
   */
  float vrCompassOffset(float bearing, float heading);

  /**
   * \brief Horizontal place in the strip of an angle from the heading
   *
   * \param [in] config Compass settings
   * \param [in] offset Angle from the heading, radians, positive to the right
   * \param [in] width Width of the strip, in pixels
   * \param [out] clamped Whether the angle is outside the span, so placed at its end
   * \returns Pixels from the strip's left edge
   */
  float vrCompassX(const VrCompassConfig& config, float offset, float width, bool& clamped);

  /**
   * \brief Draws the strip for a heading
   *
   * \param [out] list Paint list of the strip's size
   * \param [in] config Compass settings
   * \param [in] heading Heading the head faces, radians, 0 north, clockwise
   * \param [in] markers Markers to show
   * \param [in] colour The HUD's colour
   * \param [in] font Font of labels and initials, or \c nullptr to write none
   * \param [in] pictures The game's compass pictures
   */
  void vrPaintCompass(VrPaintList& list, const VrCompassConfig& config, float heading,
    const std::vector<VrCompassMarker>& markers, const VrColour& colour, const VrPaintFont* font,
    const VrCompassPictures& pictures);

  /**
   * \brief Pose of the strip relative to the head
   *
   * Centred above the view's middle by the elevation, tilted to face the eye.
   */
  VrPose vrCompassPose(const VrCompassConfig& config);

  /**
   * \brief Width of the strip, in metres
   */
  float vrCompassWidth(const VrCompassConfig& config);

}
