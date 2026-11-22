#include <algorithm>
#include <cmath>
#include <cstring>

#include "vr_math.h"
#include "vr_world_tile_place.h"

namespace dxvk {

  namespace {

    // Steepest a tile's line from the head may be: a friend right above or
    // below still gets a tile facing the head
    constexpr float MaxPitch = 1.45f;

    constexpr float Pi = 3.14159265f;

  }


  float vrWorldTileScale(const VrWorldTileConfig& config, float distance) {
    if (distance <= config.nearDistance || config.farDistance <= config.nearDistance)
      return 1.0f;

    float t = std::min(1.0f, (distance - config.nearDistance) / (config.farDistance - config.nearDistance));
    return 1.0f + t * (config.farScale - 1.0f);
  }


  VrWorldTilePlace vrPlaceWorldTile(const VrWorldTileConfig& config, const VrPose& head, const VrVector3& anchor) {
    VrWorldTilePlace place;

    VrVector3 offset = {
      anchor.x - head.position.x,
      anchor.y - head.position.y,
      anchor.z - head.position.z };

    float distance = vrLength(offset);
    place.distance = distance;

    if (!std::isfinite(distance) || distance < config.minDistance)
      return place;

    float shown = std::min(distance, std::max(config.maxDistance, config.minDistance));
    float angle = config.angle * vrWorldTileScale(config, distance) * Pi / 180.0f;

    place.pose    = vrComputePanelPoseTowards(head, anchor, shown, MaxPitch);
    place.width   = 2.0f * shown * std::tan(0.5f * angle);
    place.visible = place.width > 0.0f;
    return place;
  }


  bool vrTileNameMatches(const char* name, const std::vector<std::string>& prefixes) {
    if (!name)
      return false;

    for (const std::string& prefix : prefixes) {
      if (!prefix.empty() && std::strncmp(name, prefix.c_str(), prefix.size()) == 0)
        return true;
    }

    return false;
  }


  std::vector<std::string> vrSplitNameList(const std::string& list) {
    std::vector<std::string> names;
    std::string current;

    for (size_t i = 0; i <= list.size(); i++) {
      char c = i < list.size() ? list[i] : ',';

      if (c == ',' || c == ';') {
        if (!current.empty())
          names.push_back(current);
        current.clear();
      } else if (c != ' ' && c != '\t') {
        current += c;
      }
    }

    return names;
  }


  std::vector<std::string> vrTileTextLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;

    for (size_t i = 0; i < text.size(); i++) {
      char c = text[i];

      if (c == '\r' || c == '\n') {
        lines.push_back(current);
        current.clear();

        if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
          i++;
      } else {
        current += c;
      }
    }

    lines.push_back(current);

    // Spaces only, as the co-op mod's empty text " ", is no line
    while (!lines.empty() && lines.back().find_first_not_of(' ') == std::string::npos)
      lines.pop_back();

    return lines;
  }

}
