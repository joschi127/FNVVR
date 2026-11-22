#pragma once

#include <string>
#include <vector>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief How HUD tiles anchored in the world are placed, see VrWorldTiles
   *
   * A tile stands at its anchor up to \c maxDistance from the head, and
   * further away on the line to the anchor at that distance, so that the
   * eyes do not converge on something much farther than the scene around
   * it. It keeps its angular size up to \c nearDistance and shrinks with
   * the distance down to \c farScale at \c farDistance, which tells near
   * from far without making far tiles unreadable.
   */
  struct VrWorldTileConfig {
    /// Show tiles anchored in the world at all
    bool  enabled      = true;
    /// Farthest a tile stands from the head, in metres
    float maxDistance  = 20.0f;
    /// Width of a tile's image as an angle up to nearDistance, in degrees
    float angle        = 14.0f;
    /// Up to this distance a tile keeps its angular size, in metres
    float nearDistance = 5.0f;
    /// At this distance and beyond a tile is farScale of its size, in metres
    float farDistance  = 40.0f;
    float farScale     = 0.6f;
    /// Nearest a tile is shown, in metres: closer, it would fill the view
    float minDistance  = 0.3f;
  };

  /**
   * \brief Where a tile anchored in the world is shown
   */
  struct VrWorldTilePlace {
    bool   visible  = false;
    /// Pose in tracking space, facing the head
    VrPose pose     = { };
    /// Width in metres
    float  width    = 0.0f;
    /// Distance from the head to the anchor, in metres
    float  distance = 0.0f;
  };

  /**
   * \brief Scale of a tile's angular size at a distance
   */
  float vrWorldTileScale(const VrWorldTileConfig& config, float distance);

  /**
   * \brief Places a tile anchored in the world
   *
   * \param [in] config Placement rules
   * \param [in] head Head pose in tracking space
   * \param [in] anchor The tile's anchor in tracking space, in metres
   */
  VrWorldTilePlace vrPlaceWorldTile(const VrWorldTileConfig& config, const VrPose& head, const VrVector3& anchor);

  /**
   * \brief Checks whether a HUD tile's name starts with one of the prefixes
   */
  bool vrTileNameMatches(const char* name, const std::vector<std::string>& prefixes);

  /**
   * \brief Splits a comma-separated list, dropping spaces and empty entries
   */
  std::vector<std::string> vrSplitNameList(const std::string& list);

  /**
   * \brief Lines of a tile's text: the game breaks lines at CR or LF
   */
  std::vector<std::string> vrTileTextLines(const std::string& text);

}
