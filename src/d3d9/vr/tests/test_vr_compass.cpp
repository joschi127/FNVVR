#include <algorithm>

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
  config.stripHeight = 1.0f;   // the full size
  config.markerSize  = 1.0f;
  config.backdrop = false;
  config.markerGapNoBackdrop = 0.0f;
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
  config.stripHeight = 1.0f;   // the full size
  config.markerSize  = 1.0f;
  config.backdrop = false;
  config.markerGapNoBackdrop = 0.0f;
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
  config.widthShare = 0.45f;
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
  CHECK(vrSetCompassValue(config, "gapnoband", "-20") && config.markerGapNoBackdrop == -20.0f);

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


TEST_CASE(compass_large_quest_marker_stays_inside_the_image) {
  VrCompassConfig config;
  config.backdrop   = false;
  config.markerGapNoBackdrop = 14.0f;
  config.questScale = 2.0f;
  VrPaintList list(VrCompassImage::Width, VrCompassImage::Height);
  std::vector<VrCompassMarker> markers = { { VrCompassMarkerKind::Quest, 0.0f, 0 } };
  vrPaintCompass(list, config, 0.0f, markers, { 1.0f, 0.7f, 0.2f, 1.0f }, nullptr, VrCompassPictures());

  for (const VrPaintVertex& v : list.vertices())
    CHECK(v.y <= 166.0f);   // above the health bar's row
}

TEST_CASE(compass_health_colour_turns_yellow_then_red) {
  VrCompassConfig config;
  VrColour well = vrHealthColour(config, 0.8f);
  VrColour low  = vrHealthColour(config, 0.4f);
  VrColour critical = vrHealthColour(config, 0.1f);
  CHECK_NEAR(well.g, float(0xE5) / 255.0f, 1.0e-4);       // A6E585, green
  CHECK_NEAR(low.g, float(0xD8) / 255.0f, 1.0e-4);        // E8D848, yellow
  CHECK_NEAR(critical.r, float(0xE0) / 255.0f, 1.0e-4);   // E0705A, red
  CHECK_NEAR(critical.g, float(0x70) / 255.0f, 1.0e-4);
  // The shares are the bounds: at them the better colour still shows
  CHECK_NEAR(vrHealthColour(config, 0.5f).g, well.g, 1.0e-6);
  CHECK_NEAR(vrHealthColour(config, 0.25f).g, low.g, 1.0e-6);
}


TEST_CASE(compass_health_pulses_only_when_critical) {
  VrCompassConfig config;
  CHECK(vrHealthPulse(config, 0.6f, 0.37) == 1.0f);
  CHECK(vrHealthPulse(config, 0.3f, 0.37) == 1.0f);

  // Two beats of 0.16 s, 0.08 s apart, at the start of every second: dark
  // in their middle, blended on their way, red between and after them
  CHECK(vrHealthPulse(config, 0.1f, 5.08) == 0.0f);
  CHECK(vrHealthPulse(config, 0.1f, 5.20) == 1.0f);
  CHECK(vrHealthPulse(config, 0.1f, 5.32) == 0.0f);
  CHECK(vrHealthPulse(config, 0.1f, 5.60) == 1.0f);
  float edge = vrHealthPulse(config, 0.1f, 5.03);
  CHECK(edge > 0.0f && edge < 1.0f);

  int dark = 0;
  int blended = 0;
  for (int i = 0; i < 1000; i++) {
    float p = vrHealthPulse(config, 0.1f, 7.0 + 0.001 * i);
    dark    += p < 0.5f;
    blended += p > 0.0f && p < 1.0f;
  }
  CHECK(dark >= 130 && dark <= 190);   // red most of the time
  CHECK(blended > 50);                 // and no hard switch

  // Odd values stay inside the period and between 0 and 1
  config.healthPulseBeat = 3.0f;
  config.healthPulseGap  = 3.0f;
  for (int i = 0; i < 100; i++) {
    float p = vrHealthPulse(config, 0.1f, 5.0 + 0.01 * i);
    CHECK(p >= 0.0f && p <= 1.0f);
  }
  config = VrCompassConfig();

  config.healthPulse = false;
  CHECK(vrHealthPulse(config, 0.1f, 5.5) == 1.0f);
}


TEST_CASE(compass_health_bar_draws_in_its_row_and_follows_the_alignment) {
  VrCompassConfig config;
  VrHealthState health;
  health.known    = true;
  health.fraction = 0.5f;

  auto extent = [&] (VrHealthAlign align, float& left, float& right) {
    config.healthAlign = align;
    VrPaintList list(VrCompassImage::Width, VrCompassImage::Height);
    config.enabled = false;   // only the health bar
    vrPaintCompass(list, config, 0.0f, { }, { 1.0f, 0.7f, 0.2f, 1.0f }, nullptr, VrCompassPictures(), health);
    left = 1.0e9f;
    right = -1.0e9f;

    for (const VrPaintVertex& v : list.vertices()) {
      CHECK(v.y >= 85.0f && v.y <= float(VrCompassImage::Height));
      left  = std::min(left, v.x);
      right = std::max(right, v.x);
    }

    return !list.vertices().empty();
  };

  float l = 0.0f, r = 0.0f;
  CHECK(extent(VrHealthAlign::Centre, l, r));
  CHECK_NEAR(0.5f * (l + r), 0.5 * VrCompassImage::Width, 2.0);
  CHECK(extent(VrHealthAlign::Right, l, r));
  CHECK(r > 0.9f * VrCompassImage::Width && r <= float(VrCompassImage::Width));

  // Not known, or switched off: nothing
  VrPaintList list(VrCompassImage::Width, VrCompassImage::Height);
  health.known = false;
  vrPaintCompass(list, config, 0.0f, { }, { 1.0f, 0.7f, 0.2f, 1.0f }, nullptr, VrCompassPictures(), health);
  CHECK(list.vertices().empty());
}


TEST_CASE(compass_flatter_strip_and_smaller_markers_move_the_health_bar_up) {
  VrCompassConfig config;
  config.stripHeight = 1.0f;   // the full size
  config.markerSize  = 1.0f;
  config.enabled = false;   // only the health bar
  VrHealthState health;
  health.known = true;

  auto barTop = [&] () {
    VrPaintList list(VrCompassImage::Width, VrCompassImage::Height);
    vrPaintCompass(list, config, 0.0f, { }, { 1.0f, 0.7f, 0.2f, 1.0f }, nullptr, VrCompassPictures(), health);
    float top = 1.0e9f;
    for (const VrPaintVertex& v : list.vertices())
      top = std::min(top, v.y);
    return top;
  };

  float full = barTop();
  CHECK(vrSetCompassValue(config, "stripheight", "0.8"));
  float flatter = barTop();
  CHECK(vrSetCompassValue(config, "markersize", "0.75"));
  float smaller = barTop();
  CHECK(flatter < full - 10.0f);
  CHECK(smaller < flatter - 10.0f);
  CHECK(!vrSetCompassValue(config, "stripheight", "1.2"));
  CHECK(vrDescribeCompass(config).find("stripheight=0.8 markersize=0.75") != std::string::npos);
  CHECK(vrSetCompassValue(config, "gamestrip", "on") && config.gameStrip);
  CHECK(!vrSetCompassValue(config, "gamestrip", "yes"));
  CHECK(vrSetCompassValue(config, "healthpicture", "on") && config.healthPicture);
}


TEST_CASE(compass_health_ecg_trace_draws_and_runs) {
  VrCompassConfig config;
  config.enabled = false;
  config.ammo    = false;
  VrHealthState health;
  health.known    = true;
  health.fraction = 0.8f;
  VrColour amber = { 1.0f, 0.7f, 0.2f, 1.0f };

  CHECK(config.healthEcg);   // the default
  config.healthEcg = false;
  VrPaintList ticks(VrCompassImage::Width, VrCompassImage::Height);
  vrPaintCompass(ticks, config, 0.0f, { }, amber, nullptr, VrCompassPictures(), health);

  config.healthEcg = true;
  VrPaintList ecg(VrCompassImage::Width, VrCompassImage::Height);
  vrPaintCompass(ecg, config, 0.0f, { }, amber, nullptr, VrCompassPictures(), health);
  CHECK(ecg.vertices().size() > ticks.vertices().size());

  // The spike rises above the ticks' top, all of it stays in the image
  float topTicks = 1.0e9f, topEcg = 1.0e9f;
  for (const VrPaintVertex& v : ticks.vertices()) topTicks = std::min(topTicks, v.y);
  for (const VrPaintVertex& v : ecg.vertices()) {
    topEcg = std::min(topEcg, v.y);
    CHECK(v.y >= 0.0f && v.y <= float(VrCompassImage::Height));
  }
  CHECK(topEcg < topTicks);

  // The wave: flat at the start, the spike at 0.4, a dip right after it
  CHECK(std::fabs(vrEcgWave(0.02f)) < 0.01f);
  CHECK(vrEcgWave(0.40f) > 0.95f);
  CHECK(vrEcgWave(0.44f) < -0.2f);

  // Running: in 32nds of a beat, faster when low, the pulse's period when critical
  CHECK(vrHealthEcg(config, 0.8f, 0.7) == 0.5f);     // 0.7 s of 1.4 s
  float run = vrHealthEcg(config, 0.8f, 0.71);
  CHECK(run * 32.0f == std::floor(run * 32.0f));
  CHECK(vrHealthEcg(config, 0.4f, 0.56) == 0.5f);    // 0.8 * 1.4 s
  CHECK(vrHealthEcg(config, 0.1f, 0.5) == 0.5f);     // the pulse's 1 s
  config.healthEcgScroll = false;
  CHECK(vrHealthEcg(config, 0.8f, 0.7) == 0.0f);
  config.healthEcg = false;
  config.healthEcgScroll = true;
  CHECK(vrHealthEcg(config, 0.8f, 0.7) == 0.0f);

  CHECK(vrSetCompassValue(config, "healthecg", "on") && config.healthEcg);
  CHECK(vrSetCompassValue(config, "healthecgscroll", "off") && !config.healthEcgScroll);
  CHECK(vrSetCompassValue(config, "healthecgbeat", "0.5") && config.healthEcgBeat == 0.5f);
  CHECK(vrSetCompassValue(config, "healthecgperiod", "2") && config.healthEcgPeriod == 2.0f);
  CHECK(!vrSetCompassValue(config, "healthecgbeat", "2"));
  CHECK(vrDescribeCompass(config).find("healthecg=on healthecgscroll=off healthecgbeat=0.5 healthecgperiod=2")
    != std::string::npos);
}


TEST_CASE(compass_stays_inside_its_image_for_extreme_settings) {
  struct Case { const char* strip; const char* markers; const char* gap; bool band; const char* quest; };
  const Case cases[] = {
    { "1", "1.5", "14", true, "2" }, { "1", "1.5", "14", false, "1" },
    { "0.5", "0.5", "-40", false, "2" }, { "0.7", "0.65", "0", true, "2" } };
  std::vector<VrCompassMarker> markers = {
    { VrCompassMarkerKind::Quest, degrees(10.0f), 0 }, { VrCompassMarkerKind::Custom, degrees(-20.0f), 0 },
    { VrCompassMarkerKind::Friend, degrees(30.0f), 'F' } };
  VrHealthState health;
  health.known = true;
  health.fraction = 0.5f;

  for (const Case& c : cases) {
    VrCompassConfig config;
    CHECK(vrSetCompassValue(config, "stripheight", c.strip));
    CHECK(vrSetCompassValue(config, "markersize", c.markers));
    CHECK(vrSetCompassValue(config, c.band ? "gap" : "gapnoband", c.gap));
    CHECK(vrSetCompassValue(config, "backdrop", c.band ? "on" : "off"));
    CHECK(vrSetCompassValue(config, "quest", c.quest));
    VrPaintList list(VrCompassImage::Width, VrCompassImage::Height);
    vrPaintCompass(list, config, 0.0f, markers, { 1.0f, 0.7f, 0.2f, 1.0f }, nullptr, VrCompassPictures(), health);
    for (const VrPaintVertex& v : list.vertices()) {
      // a hollow arrow's stroke may reach a pixel past its shape
      CHECK(v.y >= -2.0f && v.y <= float(VrCompassImage::Height) + 0.5f);
    }
    // The health bar is still drawn
    VrPaintList without(VrCompassImage::Width, VrCompassImage::Height);
    config.health = false;
    vrPaintCompass(without, config, 0.0f, markers, { 1.0f, 0.7f, 0.2f, 1.0f }, nullptr, VrCompassPictures(), health);
    CHECK(list.vertices().size() > without.vertices().size());
  }
}


TEST_CASE(compass_ammo_bands_warn_for_a_low_clip_and_few_rounds_left) {
  VrCompassConfig config;
  VrAmmoState a;
  a.known = true;
  a.capacity = 6;
  a.reserve  = 40;

  a.clip = 6; CHECK(vrAmmoClipBand(config, a) == 0);
  a.clip = 3; CHECK(vrAmmoClipBand(config, a) == 0);
  a.clip = 2; CHECK(vrAmmoClipBand(config, a) == 1);   // a quarter of 6, rounded
  a.clip = 0; CHECK(vrAmmoClipBand(config, a) == 2);

  // A small clip warns at its last round, a single round only when empty
  a.capacity = 2; a.clip = 1; CHECK(vrAmmoClipBand(config, a) == 1);
  a.capacity = 1; a.clip = 1; CHECK(vrAmmoClipBand(config, a) == 0);

  a.capacity = 6;
  a.reserve = 6; CHECK(vrAmmoReserveBand(config, a) == 0);
  a.reserve = 5; CHECK(vrAmmoReserveBand(config, a) == 1);   // fewer than one clip
  a.reserve = 0; CHECK(vrAmmoReserveBand(config, a) == 2);
  config.ammoReserveLow = 2.0f;
  a.reserve = 11; CHECK(vrAmmoReserveBand(config, a) == 1);
}


TEST_CASE(compass_ammo_counter_draws_at_the_left_end_in_the_health_row) {
  VrCompassConfig config;
  config.enabled = false;   // only the counter
  config.health  = false;
  VrAmmoState ammo;
  ammo.known = true;
  ammo.clip = 2;
  ammo.capacity = 6;
  ammo.reserve = 48;
  VrColour amber = { 1.0f, 0.7f, 0.2f, 1.0f };

  VrPaintList list(VrCompassImage::Width, VrCompassImage::Height);
  vrPaintCompass(list, config, 0.0f, { }, amber, nullptr, VrCompassPictures(), VrHealthState(), ammo);
  CHECK(!list.vertices().empty());

  float left = 1.0e9f, right = -1.0e9f;
  for (const VrPaintVertex& v : list.vertices()) {
    CHECK(v.y >= 85.0f && v.y <= float(VrCompassImage::Height));
    left  = std::min(left, v.x);
    right = std::max(right, v.x);
  }
  CHECK(left < 0.1f * VrCompassImage::Width && right < 0.5f * VrCompassImage::Width);

  // With the health bar: both in the same row, apart
  VrHealthState health;
  health.known = true;
  config.health = true;
  VrPaintList both(VrCompassImage::Width, VrCompassImage::Height);
  vrPaintCompass(both, config, 0.0f, { }, amber, nullptr, VrCompassPictures(), health, ammo);
  CHECK(both.vertices().size() > list.vertices().size());

  // A centred health bar: the counter ends before it
  {
    VrCompassConfig c = config;
    c.healthAlign = VrHealthAlign::Centre;
    VrPaintList healthOnly(VrCompassImage::Width, VrCompassImage::Height);
    vrPaintCompass(healthOnly, c, 0.0f, { }, amber, nullptr, VrCompassPictures(), health);
    VrPaintList ammoOnly(VrCompassImage::Width, VrCompassImage::Height);
    VrHealthState unknown;
    vrPaintCompass(ammoOnly, c, 0.0f, { }, amber, nullptr, VrCompassPictures(), unknown, ammo);
    float healthLeft = 1.0e9f, ammoRight = -1.0e9f;
    for (const VrPaintVertex& v : healthOnly.vertices())
      healthLeft = std::min(healthLeft, v.x);
    for (const VrPaintVertex& v : ammoOnly.vertices())
      ammoRight = std::max(ammoRight, v.x);
    CHECK(!ammoOnly.vertices().empty());
    CHECK(ammoRight < healthLeft - 8.0f);
  }

  // Not known, switched off, or numbers without a font: nothing of it
  for (int k = 0; k < 3; k++) {
    VrCompassConfig c = config;
    c.health = false;
    VrAmmoState a = ammo;
    if (k == 0) a.known = false;
    if (k == 1) c.ammo = false;
    if (k == 2) c.ammoNumbers = true;
    VrPaintList none(VrCompassImage::Width, VrCompassImage::Height);
    vrPaintCompass(none, c, 0.0f, { }, amber, nullptr, VrCompassPictures(), VrHealthState(), a);
    CHECK(none.vertices().empty());
  }

  // A large clip: a filled line with ticks, no tick per round
  ammo.capacity = 240;
  ammo.clip = 100;
  VrPaintList large(VrCompassImage::Width, VrCompassImage::Height);
  vrPaintCompass(large, config, 0.0f, { }, amber, nullptr, VrCompassPictures(), VrHealthState(), ammo);
  CHECK(!large.vertices().empty() && large.vertices().size() < 200);
}


TEST_CASE(compass_ammo_settings_change_by_name) {
  VrCompassConfig config;
  CHECK(vrSetCompassValue(config, "ammostyle", "numbers") && config.ammoNumbers);
  CHECK(vrSetCompassValue(config, "ammostyle", "bars") && !config.ammoNumbers);
  CHECK(vrSetCompassValue(config, "ammo", "off") && !config.ammo);
  CHECK(vrSetCompassValue(config, "ammolow", "0.5") && config.ammoLow == 0.5f);
  CHECK(vrSetCompassValue(config, "ammoreservelow", "2") && config.ammoReserveLow == 2.0f);
  CHECK(!vrSetCompassValue(config, "ammostyle", "dots"));
  CHECK(!vrSetCompassValue(config, "ammolow", "2"));
  std::string text = vrDescribeCompass(config);
  CHECK(text.find("ammo=off ammostyle=bars ammolow=0.5 ammoreservelow=2") != std::string::npos);
}


TEST_CASE(compass_health_settings_change_by_name) {
  VrCompassConfig config;
  CHECK(vrSetCompassValue(config, "healthalign", "right") && config.healthAlign == VrHealthAlign::Right);
  CHECK(vrSetCompassValue(config, "healthalign", "center") && config.healthAlign == VrHealthAlign::Centre);
  CHECK(vrSetCompassValue(config, "health", "off") && !config.health);
  CHECK(vrSetCompassValue(config, "compass", "off") && !config.enabled);
  CHECK(vrSetCompassValue(config, "pulse", "off") && !config.healthPulse);
  CHECK(vrSetCompassValue(config, "healthlow", "0.6") && config.healthLow == 0.6f);
  CHECK(!vrSetCompassValue(config, "healthalign", "left"));
  CHECK(!vrSetCompassValue(config, "healthwidth", "3"));

  std::string text = vrDescribeCompass(config);
  CHECK(text.find("health=off") != std::string::npos);
  CHECK(text.find("healthalign=centre") != std::string::npos);
  CHECK(text.find("healthlow=0.6") != std::string::npos);
}
