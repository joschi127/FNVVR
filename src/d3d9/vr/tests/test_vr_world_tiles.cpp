#include <algorithm>

#include "test.h"

#include "../vr_math.h"
#include "../vr_world_tile_place.h"

using namespace dxvk;

namespace {

  VrPose headAt(float x, float y, float z) {
    VrPose head;
    head.position = { x, y, z };
    return head;
  }

  float distanceBetween(const VrVector3& a, const VrVector3& b) {
    return vrLength({ a.x - b.x, a.y - b.y, a.z - b.z });
  }

}


TEST_CASE(world_tile_stands_at_a_near_anchor) {
  VrWorldTileConfig config;
  VrPose head = headAt(0.0f, 1.6f, 0.0f);
  VrVector3 anchor = { 0.0f, 1.8f, -8.0f };

  VrWorldTilePlace place = vrPlaceWorldTile(config, head, anchor);
  CHECK(place.visible);
  CHECK_NEAR(distanceBetween(place.pose.position, anchor), 0.0, 1.0e-3);
  CHECK_NEAR(place.distance, distanceBetween(head.position, anchor), 1.0e-4);
}


TEST_CASE(world_tile_far_away_stands_at_the_greatest_distance_in_its_direction) {
  VrWorldTileConfig config;
  config.maxDistance = 20.0f;
  VrPose head = headAt(0.0f, 1.6f, 0.0f);
  VrVector3 anchor = { 30.0f, 1.6f, -40.0f };   // 50 m away

  VrWorldTilePlace place = vrPlaceWorldTile(config, head, anchor);
  CHECK(place.visible);
  CHECK_NEAR(distanceBetween(place.pose.position, head.position), 20.0, 1.0e-3);
  // In the anchor's direction: 3 : 4
  CHECK_NEAR(place.pose.position.x / -place.pose.position.z, 0.75, 1.0e-3);
}


TEST_CASE(world_tile_shrinks_with_the_person_and_hides_far_away) {
  VrWorldTileConfig config;
  VrPose head = headAt(0.0f, 0.0f, 0.0f);

  auto angleAt = [&] (float distance) {
    VrWorldTilePlace place = vrPlaceWorldTile(config, head, { 0.0f, 0.0f, -distance });
    float shown = std::min(distance, config.maxDistance);
    return 2.0f * std::atan(0.5f * place.width / shown);
  };

  // A fixed size in the world: half as large in the view at twice the
  // distance, also beyond the greatest distance a tile stands at
  CHECK_NEAR(std::tan(0.5f * angleAt(4.0f)) / std::tan(0.5f * angleAt(8.0f)), 2.0, 1.0e-4);
  CHECK_NEAR(std::tan(0.5f * angleAt(20.0f)) / std::tan(0.5f * angleAt(40.0f)), 2.0, 1.0e-4);
  CHECK(vrPlaceWorldTile(config, head, { 0.0f, 0.0f, -45.0f }).visible);
  CHECK(!vrPlaceWorldTile(config, head, { 0.0f, 0.0f, -55.0f }).visible);
}


TEST_CASE(person_tile_pointer_line_is_dropped) {
  std::vector<std::string> lines = { "FakeFriend", "v" };
  CHECK(vrDropPointerLine(lines) && lines.size() == 1);
  lines = { "FakeFriend", "DOWNED 12s", " v " };
  CHECK(vrDropPointerLine(lines) && lines.size() == 2 && lines[1] == "DOWNED 12s");
  lines = { "v" };
  CHECK(!vrDropPointerLine(lines));   // a name "v" stays
  lines = { "Hello", "vv" };
  CHECK(!vrDropPointerLine(lines));
  VrColour c = vrColourFromRgb(0x9CB88A);
  CHECK_NEAR(c.r, 0x9C / 255.0, 1.0e-5);
  CHECK_NEAR(c.b, 0x8A / 255.0, 1.0e-5);
}


TEST_CASE(world_tile_faces_the_head) {
  VrWorldTileConfig config;
  VrPose head = headAt(0.0f, 1.6f, 0.0f);
  VrWorldTilePlace place = vrPlaceWorldTile(config, head, { 5.0f, 1.6f, 0.0f });

  // The panel's +Z points back at the head
  VrVector3 normal = vrRotate(place.pose.orientation, { 0.0f, 0.0f, 1.0f });
  CHECK_NEAR(normal.x, -1.0, 1.0e-3);
  CHECK_NEAR(normal.z, 0.0, 1.0e-3);
}


TEST_CASE(world_tile_too_close_or_invalid_is_hidden) {
  VrWorldTileConfig config;
  VrPose head = headAt(0.0f, 1.6f, 0.0f);
  CHECK(!vrPlaceWorldTile(config, head, { 0.0f, 1.65f, -0.1f }).visible);
  CHECK(!vrPlaceWorldTile(config, head, { NAN, 0.0f, 0.0f }).visible);
}


TEST_CASE(tile_names_match_prefixes) {
  std::vector<std::string> prefixes = vrSplitNameList(" NVCoopChat, NVCoopTalk ;;NVCoopRevive ");
  CHECK(prefixes.size() == 3);
  CHECK(vrTileNameMatches("NVCoopTalkBar0", prefixes));
  CHECK(vrTileNameMatches("NVCoopChat", prefixes));
  CHECK(!vrTileNameMatches("NVCoopMark0", prefixes));
  CHECK(!vrTileNameMatches(nullptr, prefixes));
  CHECK(vrSplitNameList("").empty());
}


TEST_CASE(tile_text_lines_break_at_cr_or_lf) {
  std::vector<std::string> lines = vrTileTextLines("FakeFriend\r\nDOWNED 12s\nv");
  CHECK(lines.size() == 3);
  CHECK(lines[0] == "FakeFriend" && lines[1] == "DOWNED 12s" && lines[2] == "v");
  CHECK(vrTileTextLines(" ").empty());
  CHECK(vrTileTextLines("Name\r").size() == 1);
}
