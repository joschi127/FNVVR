#pragma once

#include <string>
#include <vector>

#include "vr_paint.h"
#include "vr_types.h"

namespace dxvk {

  /**
   * \brief How HUD tiles anchored in the world are placed, see VrWorldTiles
   *
   * A tile has a fixed size in the world, so that it grows and shrinks with
   * the person it stands over. It stands at its anchor up to \c maxDistance
   * from the head; farther away it stands at that distance on the line to
   * the anchor with the same width, so that it gets no smaller in the view
   * and the eyes do not converge on something much farther than the scene
   * around it.
   */
  struct VrWorldTileConfig {
    /// Show tiles anchored in the world at all
    bool  enabled      = true;
    /// Farthest a tile stands from the head, in metres
    float maxDistance  = 20.0f;
    /// Width of a tile's image in the world, in metres
    float width        = 3.4f;
    /// Nearest a tile is shown, in metres: closer, it would fill the view
    float minDistance  = 0.3f;
    /// Colour of a person's tile (trait user6 = 1) drawn in the HUD's
    /// colour, and of its mark, 0xRRGGBB: the green of an old phosphor
    /// screen, the counterpart of the Pip-Boy's amber, as the compass's
    /// friends; a person's tile in a colour of its own (such as a downed
    /// friend's red) is drawn in alertColour, a red like the Pip-Boy's lamps
    uint32_t personColour = 0xA6E585;
    uint32_t alertColour  = 0xE0705A;
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

  /**
   * \brief Drops a last line that is only a "v", the flat screen's pointer
   *    under a person's name, which a person's mark replaces
   *
   * \returns Whether there was such a line
   */
  bool vrDropPointerLine(std::vector<std::string>& lines);

  /**
   * \brief A colour 0xRRGGBB, opaque
   */
  VrColour vrColourFromRgb(uint32_t rgb);

}
