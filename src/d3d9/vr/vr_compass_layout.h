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
   * Below them, the player's health in the look of the game's HP meter.
   */
  enum class VrHealthAlign : uint32_t {
    /// Under the middle of the strip
    Centre,
    /// Under the strip's right end
    Right,
  };

  struct VrCompassConfig {
    /// The strip and its markers (the health bar has its own switch)
    bool  enabled   = true;
    /// Degrees of heading across the strip; the game's own compass shows
    /// about 120 (345 of the 1024 pixels of its strip)
    float span      = 120.0f;
    /// Width of the strip as an angle of the view, in degrees; 0: a share
    /// of the field of view the headset reports (widthShare)
    float angle     = 0.0f;
    /// How far above the middle of the view the strip's top edge is, in
    /// degrees; 0: a share of the upper half of the field of view (topShare)
    float top       = 0.0f;
    /// Width as a share of the horizontal field of view, when angle is 0
    float widthShare = 0.53f;
    /// Top edge as a share of the upper half of the field of view, when top is 0
    float topShare   = 0.8f;
    /// Distance from the head, in metres
    float distance  = 1.5f;
    /// A dark band behind the strip, which keeps it readable against a
    /// bright sky, and its opacity
    bool  backdrop        = false;
    float backdropOpacity = 0.25f;
    /// Markers moved down (positive) or up towards the strip (negative), in
    /// pixels of the strip's image, with the band and without it (nearer:
    /// without the band the markers look apart from the strip)
    float markerGap  = 0.0f;
    float markerGapNoBackdrop = -18.0f;
    /// Size of the quest objective's marker against the others
    float questScale = 1.0f;
    /// Colour of friends' markers, 0xRRGGBB: a phosphor green, as their name
    /// plates (VrWorldTileConfig::personColour); the others take the HUD's colour
    uint32_t friendColour = 0xA6E585;

    /// The player's health below the markers, in the look of the game's HP
    /// meter: green while well, orange below healthLow, red and pulsing
    /// below healthCritical (shares of the full health)
    bool     health          = true;
    VrHealthAlign healthAlign = VrHealthAlign::Centre;
    /// Width of the label and meter together, as a share of the strip's
    float    healthWidth     = 0.42f;
    float    healthLow       = 0.5f;
    float    healthCritical  = 0.25f;
    uint32_t healthColour    = 0xA6E585;
    uint32_t healthLowColour = 0xF07830;
    uint32_t healthCriticalColour = 0xE0705A;
    bool     healthPulse     = true;
  };

  /**
   * \brief The player's health as the compass shows it
   */
  struct VrHealthState {
    /// Whether the health could be read
    bool  known    = false;
    /// Current health as a share of the full health, 0 to 1
    float fraction = 1.0f;
    /// Brightness of the pulse while critical, 1 otherwise
    float pulse    = 1.0f;
  };

  /**
   * \brief Colour of the health bar for a share of the full health
   */
  VrColour vrHealthColour(const VrCompassConfig& config, float fraction);

  /**
   * \brief Brightness of the health bar's pulse
   *
   * Below the critical share, and with the pulse on, it rises and falls
   * between 0.45 and 1 about once a second, in steps of a sixth, so that the
   * image is drawn again at most twelve times a second; 1 otherwise.
   *
   * \param [in] seconds A steadily running time, in seconds
   */
  float vrHealthPulse(const VrCompassConfig& config, float fraction, double seconds);

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
    /// The HP meter: a line with tick marks
    const VrImage* healthBar = nullptr;
  };

  /**
   * \brief Image size of the compass strip, in pixels
   */
  struct VrCompassImage {
    constexpr static uint32_t Width  = 768;
    /// The strip and markers in the top 166 rows, the health bar below
    constexpr static uint32_t Height = 222;
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
   * \param [in] health The player's health, drawn when known and on
   */
  void vrPaintCompass(VrPaintList& list, const VrCompassConfig& config, float heading,
    const std::vector<VrCompassMarker>& markers, const VrColour& colour, const VrPaintFont* font,
    const VrCompassPictures& pictures, const VrHealthState& health = VrHealthState());

  /**
   * \brief Changes one setting by name, for tuning it live (VR test channel)
   *
   * Names: width, top (shares of the field of view), angle, topdeg (degrees
   * or "auto"), distance, span, backdrop (on|off), opacity, gap (with the
   * band), gapnoband, quest, compass (on|off: the strip and markers),
   * health (on|off), healthalign (centre|right), healthwidth, healthlow,
   * healthcritical, pulse (on|off).
   *
   * \returns \c false, the settings unchanged, for an unknown name or value
   */
  bool vrSetCompassValue(VrCompassConfig& config, const std::string& name, const std::string& value);

  /**
   * \brief The settings vrSetCompassValue changes, as "name=value" words
   */
  std::string vrDescribeCompass(const VrCompassConfig& config);

  /**
   * \brief Size and place of the strip in the view
   */
  struct VrCompassPlacement {
    /// Width as an angle, in degrees
    float angle     = 40.0f;
    /// Elevation of its centre above the view's middle, in degrees
    float elevation = 24.0f;
  };

  /**
   * \brief Size and place of the strip for a field of view
   *
   * \param [in] config Compass settings: fixed angles, or shares of the view
   * \param [in] fov The field of view of both eyes together, in radians
   *    (left and down negative); all zero when not known, which gives a
   *    40 degree strip with its top 30 degrees up
   */
  VrCompassPlacement vrCompassPlace(const VrCompassConfig& config, const VrFov& fov);

  /**
   * \brief Pose of the strip relative to the head
   *
   * Centred above the view's middle by the elevation, tilted to face the eye.
   */
  VrPose vrCompassPose(const VrCompassConfig& config, const VrCompassPlacement& place);

  /**
   * \brief Width of the strip, in metres
   */
  float vrCompassWidth(const VrCompassConfig& config, const VrCompassPlacement& place);

}
