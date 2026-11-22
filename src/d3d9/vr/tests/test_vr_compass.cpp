#include "test.h"
#include "paint_probe.h"

#include "../vr_compass_layout.h"
#include "../vr_math.h"

using namespace dxvk;

namespace {

  constexpr float Pi = 3.14159265f;

  float degrees(float d) {
    return d * Pi / 180.0f;
  }

}


TEST_CASE(compass_bearing_counts_clockwise_from_north) {
  VrVector3 from = { 100.0f, 100.0f, 0.0f };
  CHECK_NEAR(vrCompassBearing(from, { 100.0f, 200.0f, 0.0f }), 0.0, 1.0e-5);          // north
  CHECK_NEAR(vrCompassBearing(from, { 200.0f, 100.0f, 0.0f }), degrees(90.0f), 1.0e-5);  // east
  CHECK_NEAR(vrCompassBearing(from, { 100.0f, 0.0f, 0.0f }), degrees(180.0f), 1.0e-5);   // south
  CHECK_NEAR(vrCompassBearing(from, { 0.0f, 100.0f, 0.0f }), degrees(270.0f), 1.0e-5);   // west
}


TEST_CASE(compass_offset_wraps_to_the_shorter_side) {
  CHECK_NEAR(vrCompassOffset(degrees(10.0f), degrees(350.0f)), degrees(20.0f), 1.0e-5);
  CHECK_NEAR(vrCompassOffset(degrees(350.0f), degrees(10.0f)), degrees(-20.0f), 1.0e-5);
  CHECK_NEAR(std::fabs(vrCompassOffset(degrees(180.0f), 0.0f)), degrees(180.0f), 1.0e-4);
}


TEST_CASE(compass_places_the_heading_in_the_middle_and_holds_markers_at_the_ends) {
  VrCompassConfig config;
  config.span = 120.0f;
  bool clamped = false;

  CHECK_NEAR(vrCompassX(config, 0.0f, 600.0f, clamped), 300.0, 1.0e-3);
  CHECK(!clamped);
  CHECK_NEAR(vrCompassX(config, degrees(60.0f), 600.0f, clamped), 600.0, 1.0e-3);
  CHECK(!clamped);
  CHECK_NEAR(vrCompassX(config, degrees(30.0f), 600.0f, clamped), 450.0, 1.0e-3);
  CHECK_NEAR(vrCompassX(config, degrees(-90.0f), 600.0f, clamped), 0.0, 1.0e-3);
  CHECK(clamped);
}


TEST_CASE(compass_draws_markers_where_their_bearing_is) {
  VrCompassConfig config;
  config.backdrop = false;
  VrPaintList list(VrCompassImage::Width, VrCompassImage::Height);
  VrColour amber = { 1.0f, 0.7f, 0.2f, 1.0f };

  // Without pictures and font: shapes. A quest target 30 degrees right of
  // the heading, 60 degrees across half the strip
  std::vector<VrCompassMarker> markers = { { VrCompassMarkerKind::Quest, degrees(120.0f), 0 } };
  vrPaintCompass(list, config, degrees(90.0f), markers, amber, nullptr, VrCompassPictures());

  float x = 0.75f * float(VrCompassImage::Width);
  float y = 0.5f * (98.0f + 146.0f) + 8.0f;
  CHECK(test::probePaint(list, x + 0.3f, y).a > 0.9f);
  CHECK(test::probePaint(list, x - 0.25f * float(VrCompassImage::Width), y).a < 0.01f);
}


TEST_CASE(compass_friend_marker_is_in_the_friend_colour) {
  VrCompassConfig config;
  config.backdrop = false;
  config.friendColour = 0x00FF00;
  VrPaintList list(VrCompassImage::Width, VrCompassImage::Height);

  std::vector<VrCompassMarker> markers = { { VrCompassMarkerKind::Friend, 0.0f, 'F' } };
  vrPaintCompass(list, config, 0.0f, markers, { 1.0f, 0.7f, 0.2f, 1.0f }, nullptr, VrCompassPictures());

  test::PaintProbe p = test::probePaint(list, 0.5f * float(VrCompassImage::Width) + 0.3f, 120.0f);
  CHECK(p.a > 0.9f);
  CHECK(p.g > 0.9f && p.r < 0.1f);
}


TEST_CASE(compass_size_and_place_follow_the_field_of_view) {
  VrCompassConfig config;
  VrFov fov;
  fov.angleLeft  = degrees(-50.0f);
  fov.angleRight = degrees(50.0f);
  fov.angleUp    = degrees(45.0f);
  fov.angleDown  = degrees(-50.0f);

  // Shares of the view: 45 % of 100 degrees wide, top edge at 80 % of 45 degrees
  VrCompassPlacement place = vrCompassPlace(config, fov);
  float height = 45.0f * float(VrCompassImage::Height) / float(VrCompassImage::Width);
  CHECK_NEAR(place.angle, 45.0, 1.0e-3);
  CHECK_NEAR(place.elevation, 36.0 - 0.5 * height, 1.0e-3);

  // A narrower view (Virtual Desktop's reduced field of view): smaller and lower
  fov.angleLeft = degrees(-35.0f);
  fov.angleRight = degrees(35.0f);
  fov.angleUp = degrees(30.0f);
  VrCompassPlacement narrow = vrCompassPlace(config, fov);
  CHECK(narrow.angle < place.angle && narrow.elevation < place.elevation);

  // Fixed angles override the view
  config.angle = 50.0f;
  config.top   = 20.0f;
  VrCompassPlacement fixed = vrCompassPlace(config, fov);
  CHECK_NEAR(fixed.angle, 50.0, 1.0e-4);
  CHECK_NEAR(fixed.elevation, 20.0 - 25.0 * float(VrCompassImage::Height) / float(VrCompassImage::Width), 1.0e-3);

  // An unknown view
  VrCompassConfig unset;
  CHECK_NEAR(vrCompassPlace(unset, VrFov()).angle, 40.0, 1.0e-4);
}


TEST_CASE(compass_strip_is_above_the_eyes_and_faces_them) {
  VrCompassConfig config;
  config.distance  = 2.0f;
  VrCompassPlacement place;
  place.elevation = 30.0f;
  VrPose pose = vrCompassPose(config, place);

  CHECK_NEAR(pose.position.y, 1.0, 1.0e-4);
  CHECK(pose.position.z < 0.0f);

  // The panel's +Z points back at the eye
  VrVector3 normal = vrRotate(pose.orientation, { 0.0f, 0.0f, 1.0f });
  float length = vrLength(pose.position);
  CHECK_NEAR(normal.x, 0.0, 1.0e-4);
  CHECK_NEAR(normal.y, -pose.position.y / length, 1.0e-4);
  CHECK_NEAR(normal.z, -pose.position.z / length, 1.0e-4);
}


TEST_CASE(paint_fills_and_strokes_convex_polygons) {
  VrPaintList list(64, 64);
  const float square[] = { 10.0f, 10.0f,  30.0f, 10.0f,  30.0f, 30.0f,  10.0f, 30.0f };
  list.fillPolygon(square, 4, { 1.0f, 1.0f, 1.0f, 1.0f });
  CHECK(test::probePaint(list, 20.3f, 20.2f).a > 0.99f);
  CHECK(test::probePaint(list, 40.3f, 20.2f).a < 0.01f);

  // The same in the other winding
  VrPaintList other(64, 64);
  const float reversed[] = { 10.0f, 10.0f,  10.0f, 30.0f,  30.0f, 30.0f,  30.0f, 10.0f };
  other.fillPolygon(reversed, 4, { 1.0f, 1.0f, 1.0f, 1.0f });
  CHECK(test::probePaint(other, 20.3f, 20.2f).a > 0.99f);

  VrPaintList outline(64, 64);
  outline.strokePolygon(square, 4, 4.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
  CHECK(test::probePaint(outline, 10.2f, 20.3f).a > 0.99f);   // on the edge
  CHECK(test::probePaint(outline, 20.3f, 20.2f).a < 0.01f);   // hollow inside
}


TEST_CASE(compass_settings_change_by_name_for_live_tuning) {
  VrCompassConfig config;
  CHECK(vrSetCompassValue(config, "width", "0.5") && config.widthShare == 0.5f);
  CHECK(vrSetCompassValue(config, "angle", "55") && config.angle == 55.0f);
  CHECK(vrSetCompassValue(config, "angle", "auto") && config.angle == 0.0f);
  CHECK(vrSetCompassValue(config, "backdrop", "off") && !config.backdrop);
  CHECK(vrSetCompassValue(config, "gap", "-12") && config.markerGap == -12.0f);

  // Unknown names and values leave the settings as they are
  VrCompassConfig before = config;
  CHECK(!vrSetCompassValue(config, "width", "7"));
  CHECK(!vrSetCompassValue(config, "colour", "1"));
  CHECK(!vrSetCompassValue(config, "backdrop", "maybe"));
  CHECK(config.widthShare == before.widthShare && config.backdrop == before.backdrop);

  std::string text = vrDescribeCompass(config);
  CHECK(text.find("width=0.5") != std::string::npos);
  CHECK(text.find("angle=auto") != std::string::npos);
  CHECK(text.find("backdrop=off") != std::string::npos);
}
