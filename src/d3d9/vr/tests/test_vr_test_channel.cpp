#include "test.h"

#include <cmath>
#include <string>
#include <vector>

#include "../vr_math.h"
#include "../vr_test_commands.h"
#include "../vr_test_input.h"
#include "../vr_test_png.h"
#include "../vr_test_signal.h"

using namespace dxvk;


TEST_CASE(test_channel_parses_simple_requests) {
  CHECK(vrParseTestCommand("ping").verb == VrTestVerb::Ping);
  CHECK(vrParseTestCommand("  STATE \r").verb == VrTestVerb::State);
  CHECK(vrParseTestCommand("ping now").verb == VrTestVerb::Invalid);
  CHECK(vrParseTestCommand("").verb == VrTestVerb::Invalid);
  CHECK(vrParseTestCommand("jump around").verb == VrTestVerb::Invalid);
  CHECK(vrParseTestCommand(std::string("state\x01")).verb == VrTestVerb::Invalid);
  CHECK(vrParseTestCommand(std::string(VrTestMaxLine + 1, 'a')).verb == VrTestVerb::Invalid);
}


TEST_CASE(test_channel_parses_poses) {
  VrTestCommand head = vrParseTestCommand("head 0 1.7 0 90 0");
  CHECK(head.verb == VrTestVerb::Head);
  CHECK_NEAR(head.pose.position.y, 1.7f, 1e-5f);

  // Yaw 90 turns the forward axis (-Z) to the left (-X)
  VrVector3 forward = vrRotate(head.pose.orientation, { 0.0f, 0.0f, -1.0f });
  CHECK_NEAR(forward.x, -1.0f, 1e-4f);
  CHECK_NEAR(forward.z, 0.0f, 1e-4f);

  VrTestCommand hand = vrParseTestCommand("hand right 0.2 1.2 -0.4 0 30 5");
  CHECK(hand.verb == VrTestVerb::Hand);
  CHECK(hand.hand == VrHand::Right);

  // Pitch 30 looks up
  VrVector3 aim = vrRotate(hand.pose.orientation, { 0.0f, 0.0f, -1.0f });
  CHECK(aim.y > 0.4f);

  CHECK(vrParseTestCommand("hand middle 0 0 0 0 0").verb == VrTestVerb::Invalid);
  CHECK(vrParseTestCommand("head 0 1.7 0 90").verb == VrTestVerb::Invalid);
  CHECK(vrParseTestCommand("head 0 1.7 x 90 0").verb == VrTestVerb::Invalid);
}


TEST_CASE(test_channel_parses_actions_and_sticks) {
  VrTestCommand press = vrParseTestCommand("press MenuSelect");
  CHECK(press.verb == VrTestVerb::Press && press.action == VrAction::MenuSelect);
  CHECK(vrParseTestCommand("hold attack").verb == VrTestVerb::Hold);
  CHECK(vrParseTestCommand("up attack").verb == VrTestVerb::Up);
  CHECK(vrParseTestCommand("press fly").verb == VrTestVerb::Invalid);

  VrTestCommand stick = vrParseTestCommand("stick move 0 1");
  CHECK(stick.verb == VrTestVerb::Stick && stick.stick == VrTestStick::Move);
  CHECK_NEAR(stick.value.y, 1.0f, 1e-6f);
  CHECK(vrParseTestCommand("stick turn 2 0").verb == VrTestVerb::Invalid);

  VrTestCommand release = vrParseTestCommand("release right");
  CHECK(release.verb == VrTestVerb::Release && release.release == VrTestRelease::Right);
  CHECK(vrParseTestCommand("release nose").verb == VrTestVerb::Invalid);

  for (uint32_t i = 0; i < VrActionCount; i++) {
    VrAction found;
    CHECK(vrTestFindAction(vrTestActionName(VrAction(i)), found) && found == VrAction(i));
  }

  for (uint32_t i = 0; i < VrPanelCount; i++) {
    VrPanelId found;
    CHECK(vrTestFindPanel(vrTestPanelName(VrPanelId(i)), found) && found == VrPanelId(i));
  }
}


TEST_CASE(test_channel_parses_aim_wait_capture) {
  VrTestCommand aim = vrParseTestCommand("aim right menu 0.25 0.9");
  CHECK(aim.verb == VrTestVerb::Aim && aim.panel == VrPanelId::Menu && aim.hand == VrHand::Right);
  CHECK_NEAR(aim.v, 0.9f, 1e-6f);
  CHECK(vrParseTestCommand("aim right mirror 0.5 0.5").verb == VrTestVerb::Invalid);

  VrTestCommand wait = vrParseTestCommand("wait 10");
  CHECK(wait.verb == VrTestVerb::Wait && wait.count == 10);
  CHECK(vrParseTestCommand("waitframe 12345").count == 12345);
  CHECK(vrParseTestCommand("wait -1").verb == VrTestVerb::Invalid);

  VrTestCommand eyes = vrParseTestCommand("capture eyes C:/runs/a b/shot");
  CHECK(eyes.verb == VrTestVerb::CaptureEyes && eyes.path == "C:/runs/a b/shot");

  VrTestCommand panel = vrParseTestCommand("capture panel pipboylampbar  out/bar.png ");
  CHECK(panel.verb == VrTestVerb::CapturePanel && panel.panel == VrPanelId::PipBoyLampBar);
  CHECK(panel.path == "out/bar.png");
  CHECK(vrParseTestCommand("capture eyes").verb == VrTestVerb::Invalid);
}


TEST_CASE(test_input_adds_actions_and_replaces_poses) {
  VrTestInput input;
  VrInputState real;
  real.hasActions = true;
  real.actions.setPressed(VrAction::Jump, true);
  real.controllers[0].isActive = true;
  real.controllers[0].aimPose.position = { 1.0f, 2.0f, 3.0f };

  // Nothing set: the real input passes through unchanged
  VrInputState same = input.merge(real, 1);
  CHECK(same.actions.pressed == real.actions.pressed);
  CHECK_NEAR(same.controllers[0].aimPose.position.x, 1.0f, 1e-6f);

  input.press(VrAction::Attack);
  input.setHand(VrHand::Left, vrTestPose({ 0.5f, 1.0f, -0.3f }, 0.0f, 0.0f, 0.0f));
  input.setStick(VrTestStick::Move, { 0.0f, 1.0f });

  VrInputState merged = input.merge(real, 2);
  CHECK(merged.actions.isPressed(VrAction::Attack));
  CHECK(merged.actions.isPressed(VrAction::Jump));
  CHECK_NEAR(merged.controllers[0].aimPose.position.x, 0.5f, 1e-6f);
  CHECK_NEAR(merged.actions.move.y, 1.0f, 1e-6f);

  input.release(VrTestRelease::Left);
  CHECK(!input.handSet(VrHand::Left));
  CHECK_NEAR(input.merge(real, 3).controllers[0].aimPose.position.x, 1.0f, 1e-6f);
}


TEST_CASE(test_input_press_lasts_frames_not_polls) {
  VrTestInput input;
  VrInputState real;
  input.press(VrAction::MenuSelect);

  uint32_t frames = 0;

  for (uint64_t frame = 1; frame <= 10; frame++) {
    // Two polls in the same frame count once
    bool first  = input.merge(real, frame).actions.isPressed(VrAction::MenuSelect);
    bool second = input.merge(real, frame).actions.isPressed(VrAction::MenuSelect);

    if (first)
      frames++;

    CHECK(!second || first);
  }

  CHECK(frames == VrTestInput::PressFrames);

  input.hold(VrAction::Grab);
  CHECK(input.merge(real, 20).actions.isPressed(VrAction::Grab));
  CHECK(input.merge(real, 99).actions.isPressed(VrAction::Grab));
  input.up(VrAction::Grab);
  CHECK(!input.merge(real, 100).actions.isPressed(VrAction::Grab));
}


TEST_CASE(test_input_moves_eyes_with_the_set_head) {
  VrTestInput input;
  VrPose realHead = vrTestPose({ 0.0f, 1.6f, 0.0f }, 0.0f, 0.0f, 0.0f);
  std::array<VrEyeView, VrEyeCount> real = { };
  real[0].pose = vrTestPose({ -0.032f, 1.6f, 0.0f }, 0.0f, 0.0f, 0.0f);
  real[1].pose = vrTestPose({  0.032f, 1.6f, 0.0f }, 0.0f, 0.0f, 0.0f);

  input.setHead(vrTestPose({ 2.0f, 1.0f, 0.0f }, 90.0f, 0.0f, 0.0f));
  auto views = input.views(real, realHead);

  // Turned left by 90 degrees, the left eye sits behind (+Z) the head
  CHECK_NEAR(views[0].pose.position.x, 2.0f, 1e-4f);
  CHECK_NEAR(views[0].pose.position.z, 0.032f, 1e-4f);
  CHECK_NEAR(views[1].pose.position.z, -0.032f, 1e-4f);
  CHECK_NEAR(views[0].pose.position.y, 1.0f, 1e-4f);
}


TEST_CASE(test_aim_points_the_laser_at_the_panel_point) {
  VrTestPanelPlace panel;
  panel.shown  = true;
  panel.pose   = vrTestPose({ 0.0f, 1.5f, -2.0f }, 0.0f, 0.0f, 0.0f);
  panel.width  = 2.0f;
  panel.height = 1.0f;

  VrPose aim;
  VrVector3 target;
  VrVector3 origin = { 0.0f, 1.0f, 0.0f };
  CHECK(vrTestAimPose(panel, VrPose(), origin, 1.0f, 1.0f, aim, target));

  // Bottom right corner
  CHECK_NEAR(target.x, 1.0f, 1e-4f);
  CHECK_NEAR(target.y, 1.0f, 1e-4f);
  CHECK_NEAR(target.z, -2.0f, 1e-4f);

  VrVector3 dir = vrRotate(aim.orientation, { 0.0f, 0.0f, -1.0f });
  VrVector3 expected = (target - origin) * (1.0f / vrLength(target - origin));
  CHECK_NEAR(dir.x, expected.x, 1e-4f);
  CHECK_NEAR(dir.y, expected.y, 1e-4f);
  CHECK_NEAR(dir.z, expected.z, 1e-4f);

  panel.shown = false;
  CHECK(!vrTestAimPose(panel, VrPose(), origin, 0.5f, 0.5f, aim, target));
}


TEST_CASE(test_aim_follows_a_head_anchored_panel) {
  VrTestPanelPlace panel;
  panel.shown  = true;
  panel.anchor = VrPanelAnchor::Head;
  panel.pose   = vrTestPose({ 0.0f, 0.0f, -1.0f }, 0.0f, 0.0f, 0.0f);
  panel.width  = 1.0f;
  panel.height = 1.0f;

  VrPose head = vrTestPose({ 5.0f, 1.7f, 0.0f }, 0.0f, 0.0f, 0.0f);
  VrPose aim;
  VrVector3 target;
  CHECK(vrTestAimPose(panel, head, { 5.0f, 1.2f, 0.0f }, 0.5f, 0.5f, aim, target));
  CHECK_NEAR(target.x, 5.0f, 1e-4f);
  CHECK_NEAR(target.y, 1.7f, 1e-4f);
  CHECK_NEAR(target.z, -1.0f, 1e-4f);
}


TEST_CASE(test_png_has_valid_structure) {
  const uint8_t pixels[2 * 2 * 4] = {
    255, 0, 0, 255,   0, 255, 0, 255,
    0, 0, 255, 255,   255, 255, 255, 128,
  };

  std::vector<uint8_t> png = vrEncodePng(pixels, 2, 2);
  CHECK(png.size() > 8 + 25 + 12);
  CHECK(png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
  CHECK(std::string(png.begin() + 12, png.begin() + 16) == "IHDR");

  // Width and height in the header, big endian
  CHECK(png[19] == 2 && png[23] == 2);
  CHECK(std::string(png.end() - 8, png.end() - 4) == "IEND");

  // IEND's CRC is fixed
  CHECK(png[png.size() - 4] == 0xAE && png[png.size() - 3] == 0x42
     && png[png.size() - 2] == 0x60 && png[png.size() - 1] == 0x82);
}


TEST_CASE(test_png_stores_the_rows_across_blocks) {
  // Wide enough that the raw data spans several stored blocks (65535 bytes each)
  const uint32_t width = 9000, height = 3;
  std::vector<uint8_t> pixels(size_t(width) * height * 4);

  for (size_t i = 0; i < pixels.size(); i++)
    pixels[i] = uint8_t(i * 7 + 3);

  std::vector<uint8_t> png = vrEncodePng(pixels.data(), width, height);

  // IDAT starts after the signature (8) and IHDR (25); its data after 8 bytes
  size_t idat = 8 + 25;
  uint32_t length = (uint32_t(png[idat]) << 24) | (uint32_t(png[idat + 1]) << 16)
                  | (uint32_t(png[idat + 2]) << 8) | uint32_t(png[idat + 3]);
  CHECK(std::string(png.begin() + idat + 4, png.begin() + idat + 8) == "IDAT");
  CHECK(idat + 8 + length + 4 + 12 == png.size());

  // Unpack the stored blocks
  size_t pos = idat + 8 + 2;
  std::vector<uint8_t> raw;
  bool last = false;

  while (!last) {
    last = png[pos] & 1;
    uint32_t len  = png[pos + 1] | (uint32_t(png[pos + 2]) << 8);
    uint32_t nlen = png[pos + 3] | (uint32_t(png[pos + 4]) << 8);
    CHECK((len ^ nlen) == 0xFFFF);
    raw.insert(raw.end(), png.begin() + pos + 5, png.begin() + pos + 5 + len);
    pos += 5 + len;
  }

  CHECK(raw.size() == (size_t(width) * 4 + 1) * height);

  bool same = true;

  for (uint32_t y = 0; y < height; y++) {
    same = same && raw[y * (width * 4 + 1)] == 0;

    for (size_t x = 0; x < size_t(width) * 4; x++)
      same = same && raw[y * (width * 4 + 1) + 1 + x] == pixels[y * width * 4 + x];
  }

  CHECK(same);

  uint32_t a = 1, b = 0;

  for (uint8_t byte : raw) {
    a = (a + byte) % 65521;
    b = (b + a) % 65521;
  }

  uint32_t adler = (uint32_t(png[pos]) << 24) | (uint32_t(png[pos + 1]) << 16)
                 | (uint32_t(png[pos + 2]) << 8) | uint32_t(png[pos + 3]);
  CHECK(adler == ((b << 16) | a));
}


TEST_CASE(test_png_writer_refuses_missing_rows) {
  size_t bytes = 0;
  VrPngWriter writer([&bytes] (const uint8_t*, size_t size) { bytes += size; return true; }, 2, 2);
  const uint8_t row[8] = { };
  CHECK(writer.row(row));
  CHECK(!writer.finish());
}


TEST_CASE(test_channel_parses_memory) {
  CHECK(vrParseTestCommand("memory").verb == VrTestVerb::Memory);
  CHECK(vrParseTestCommand("memory now").verb == VrTestVerb::Invalid);
}


TEST_CASE(test_channel_parses_compass) {
  CHECK(vrParseTestCommand("compass").verb == VrTestVerb::Compass);
  VrTestCommand set = vrParseTestCommand("compass Width 0.5");
  CHECK(set.verb == VrTestVerb::Compass && set.path == "width 0.5");
  CHECK(vrParseTestCommand("compass width").verb == VrTestVerb::Invalid);
}


TEST_CASE(test_channel_parses_pipboy_and_signal) {
  CHECK(vrParseTestCommand("pipboy").verb == VrTestVerb::PipBoy);
  CHECK(vrParseTestCommand("pipboy open").verb == VrTestVerb::PipBoyOpen);
  CHECK(vrParseTestCommand("PIPBOY Close").verb == VrTestVerb::PipBoyClose);
  CHECK(vrParseTestCommand("pipboy toss").verb == VrTestVerb::Invalid);
  CHECK(vrParseTestCommand("signal on").verb == VrTestVerb::SignalOn);
  CHECK(vrParseTestCommand("signal off").verb == VrTestVerb::SignalOff);
  CHECK(vrParseTestCommand("signal").verb == VrTestVerb::Invalid);
}


namespace {

  VrInputState signalPose(float head, float left, float right) {
    VrInputState input;
    input.isHeadTracked = true;
    input.headPose.position = { 0.0f, head, 0.0f };
    input.controllers[0].isActive = true;
    input.controllers[0].gripPose.position = { -0.2f, left, 0.0f };
    input.controllers[1].isActive = true;
    input.controllers[1].gripPose.position = { 0.2f, right, 0.0f };
    return input;
  }

}


TEST_CASE(test_signal_counts_hands_above_the_head_only_when_armed) {
  VrTestSignal signal(1.0f);
  VrInputState up = signalPose(1.65f, 1.85f, 1.9f);

  // Not armed: nothing, however long
  for (int i = 0; i < 30; i++)
    signal.update(up, 0.1f);
  CHECK(signal.count() == 0);

  signal.arm(true);
  for (int i = 0; i < 9; i++)
    signal.update(up, 0.1f);
  CHECK(signal.count() == 0);
  signal.update(up, 0.11f);
  CHECK(signal.count() == 1);

  // Held on: still one; one hand down a little is not enough to count again
  for (int i = 0; i < 30; i++)
    signal.update(up, 0.1f);
  CHECK(signal.count() == 1);

  // Hands down, then up again for the hold time: two
  signal.update(signalPose(1.65f, 1.2f, 1.9f), 0.1f);
  for (int i = 0; i < 11; i++)
    signal.update(up, 0.1f);
  CHECK(signal.count() == 2);
}


TEST_CASE(test_signal_needs_both_hands) {
  VrTestSignal signal(0.5f);
  signal.arm(true);
  for (int i = 0; i < 20; i++)
    signal.update(signalPose(1.65f, 1.85f, 1.3f), 0.1f);
  CHECK(signal.count() == 0);
}


TEST_CASE(test_repeat_counts_the_left_hand_alone_with_the_right_down) {
  VrTestSignal signal(1.0f);
  VrInputState left = signalPose(1.65f, 1.85f, 1.2f);

  for (int i = 0; i < 20; i++)
    signal.update(left, 0.1f);
  CHECK(signal.repeatCount() == 0);

  signal.arm(true);
  for (int i = 0; i < 11; i++)
    signal.update(left, 0.1f);
  CHECK(signal.repeatCount() == 1);
  CHECK(signal.count() == 0);

  // Held on: still one; left hand down, then up again: two
  for (int i = 0; i < 20; i++)
    signal.update(left, 0.1f);
  CHECK(signal.repeatCount() == 1);
  signal.update(signalPose(1.65f, 1.3f, 1.2f), 0.1f);
  for (int i = 0; i < 11; i++)
    signal.update(left, 0.1f);
  CHECK(signal.repeatCount() == 2);
}


TEST_CASE(test_repeat_ignores_raising_both_hands) {
  VrTestSignal signal(1.0f);
  signal.arm(true);

  // The left hand first, the right one following through shoulder height: no repeat, then done
  for (int i = 0; i < 3; i++)
    signal.update(signalPose(1.65f, 1.85f, 1.2f), 0.1f);
  for (int i = 0; i < 5; i++)
    signal.update(signalPose(1.65f, 1.85f, 1.5f), 0.1f);
  for (int i = 0; i < 11; i++)
    signal.update(signalPose(1.65f, 1.85f, 1.9f), 0.1f);
  CHECK(signal.repeatCount() == 0);
  CHECK(signal.count() == 1);

  // Done, then the right hand down first and the left one kept up: no repeat until the left comes down
  for (int i = 0; i < 20; i++)
    signal.update(signalPose(1.65f, 1.85f, 1.2f), 0.1f);
  CHECK(signal.repeatCount() == 0);
  signal.update(signalPose(1.65f, 1.3f, 1.2f), 0.1f);
  for (int i = 0; i < 11; i++)
    signal.update(signalPose(1.65f, 1.85f, 1.2f), 0.1f);
  CHECK(signal.repeatCount() == 1);

  // The right hand alone up is nothing
  signal.arm(true);
  for (int i = 0; i < 20; i++)
    signal.update(signalPose(1.65f, 1.2f, 1.9f), 0.1f);
  CHECK(signal.repeatCount() == 1);
}
