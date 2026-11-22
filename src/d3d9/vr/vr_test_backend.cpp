#include "vr_test_backend.h"

#include <cstdio>
#include <new>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_compass_layout.h"
#include "vr_test_commands.h"
#include "vr_test_hooks.h"
#include "vr_test_png.h"

namespace dxvk {

  namespace {

    /// Frames a capture waits for its panel to be submitted again
    constexpr uint64_t CaptureFrames = 300;

    /// Requests taken per frame at most (a tool sends one at a time anyway)
    constexpr int RequestsPerFrame = 8;

    std::string formatVector(const VrVector3& v) {
      char text[96];
      std::snprintf(text, sizeof(text), "%.3f %.3f %.3f", v.x, v.y, v.z);
      return text;
    }

  }


  VrTestBackend::VrTestBackend(std::unique_ptr<IVRBackend> inner, const std::string& pipeName, float signalHold)
  : m_inner(std::move(inner)), m_signal(signalHold) {
    VrTestHooks::enable();

    std::string path = m_pipe.start(pipeName);

    if (!path.empty()) {
      Logger::warn(str::format("VR: Test channel listening on ", path,
        " (d3d9.vrTestChannel; test tools on this PC can drive VR input)"));
    }
  }


  VrTestBackend::~VrTestBackend() {
    m_pipe.stop();
  }


  bool VrTestBackend::beginSession(const VrGraphicsBinding& binding) {
    m_binding = binding;
    return m_inner->beginSession(binding);
  }


  VrFrameTiming VrTestBackend::waitFrame() {
    VrFrameTiming timing = m_inner->waitFrame();
    m_frame++;
    m_waitedSincePoll = true;
    return timing;
  }


  VrInputState VrTestBackend::pollInput(int64_t displayTime) {
    // While the game presents outside its main loop (loading screens, movies)
    // it polls at each present without waiting for a frame: count those too,
    // so waits end and presses are let go
    if (displayTime == 0 && !m_waitedSincePoll)
      m_frame++;

    m_waitedSincePoll = false;
    m_lastReal = m_inner->pollInput(displayTime);
    takeRequests();
    m_lastMerged = m_input.merge(m_lastReal, m_frame);

    auto now = std::chrono::steady_clock::now();
    m_signal.update(m_lastMerged, std::chrono::duration<float>(now - m_lastPoll).count());
    m_lastPoll = now;

    finishWaits();
    return m_lastMerged;
  }


  void VrTestBackend::setInputContext(VrInputContext context) {
    m_context = context;
    m_inner->setInputContext(context);
  }


  std::array<VrEyeView, VrEyeCount> VrTestBackend::locateViews(int64_t displayTime) {
    return m_input.views(m_inner->locateViews(displayTime), m_lastReal.headPose);
  }


  bool VrTestBackend::submitFrame(const VrFrameSubmission& frame) {
    if (m_pending && m_captureKind == CaptureKind::Eyes) {
      std::string error;
      std::vector<std::string> files;
      static const char* const suffix[VrEyeCount] = { "-left.png", "-right.png" };

      for (uint32_t e = 0; e < VrEyeCount && error.empty(); e++) {
        std::string file = m_capturePath + suffix[e];

        if (captureToPng(frame.images[e], true, file, error))
          files.push_back(file);
      }

      m_captureKind = CaptureKind::None;
      answer(m_pending, { error.empty() ? "ok " + files[0] + " " + files[1] : "err " + error });
    }

    return m_inner->submitFrame(frame);
  }


  bool VrTestBackend::submitPanel(VrPanelId id, const VrPanelSubmission& panel) {
    uint32_t index = uint32_t(id);

    if (index < VrPanelCount) {
      VrTestPanelPlace& place = m_places[index];
      place.shown  = true;
      place.pose   = panel.pose;
      place.width  = panel.width;
      place.height = panel.image.extent.width
        ? panel.width * float(panel.image.extent.height) / float(panel.image.extent.width) : 0.0f;
      place.anchor = panel.anchor;

      if (m_pending && m_captureKind == CaptureKind::Panel && m_capturePanel == id) {
        std::string error;
        captureToPng(panel.image, false, m_capturePath, error);
        m_captureKind = CaptureKind::None;
        answer(m_pending, { error.empty() ? "ok " + m_capturePath : "err " + error });
      }
    }

    return m_inner->submitPanel(id, panel);
  }


  void VrTestBackend::hidePanel(VrPanelId id) {
    if (uint32_t(id) < VrPanelCount)
      m_places[uint32_t(id)].shown = false;

    m_inner->hidePanel(id);
  }


  bool VrTestBackend::movePanel(VrPanelId id, const VrPose& pose, float width, VrPanelAnchor anchor) {
    if (!m_inner->movePanel(id, pose, width, anchor))
      return false;

    // Moved, its image as before: its height keeps the image's aspect
    if (uint32_t(id) < VrPanelCount) {
      VrTestPanelPlace& place = m_places[uint32_t(id)];
      place.height = place.width > 0.0f ? place.height * width / place.width : 0.0f;
      place.pose   = pose;
      place.width  = width;
      place.anchor = anchor;
    }

    return true;
  }


  bool VrTestBackend::submitPanelPixels(VrPanelId id, const VrPanelPixels& panel) {
    // Only the launcher uses panels from system memory, and it runs without this layer
    return m_inner->submitPanelPixels(id, panel);
  }


  void VrTestBackend::answer(std::shared_ptr<VrTestJob>& job, std::vector<std::string> lines) {
    if (job) {
      job->reply.set_value(std::move(lines));
      job = nullptr;
    }
  }


  bool VrTestBackend::captureToPng(const VrEyeImage& image, bool opaque, const std::string& path, std::string& error) {
    if (!ensureCapture(error))
      return false;

    FILE* file = std::fopen(path.c_str(), "wb");

    if (!file) {
      error = "could not create " + path;
      return false;
    }

    bool ok = false;

    // The rows go from the staging buffer straight into the file, so only
    // the staging buffer and one row are held in the game's address space
    try {
      VrPngWriter png([file] (const uint8_t* data, size_t size) {
        return std::fwrite(data, 1, size, file) == size;
      }, image.extent.width, image.extent.height);

      ok = m_capture.read(image, opaque, [&png] (const uint8_t* row) { return png.row(row); }, error)
        && png.finish();

      if (!ok && error.empty())
        error = "could not write " + path;
    } catch (const std::bad_alloc&) {
      error = "out of memory";
    }

    ok = std::fclose(file) == 0 && ok;

    if (!ok) {
      std::remove(path.c_str());

      if (error.empty())
        error = "could not write " + path;
    }

    return ok;
  }


  bool VrTestBackend::dropAbandoned() {
    if (!m_pending || !m_pending->abandoned)
      return false;

    m_captureKind = CaptureKind::None;
    m_pending = nullptr;
    return true;
  }


  bool VrTestBackend::ensureCapture(std::string& error) {
    if (m_capture.ready() || m_capture.init(m_binding))
      return true;

    error = "no graphics device for captures";
    return false;
  }


  void VrTestBackend::takeRequests() {
    dropAbandoned();

    for (int i = 0; i < RequestsPerFrame && !m_pending; i++) {
      std::shared_ptr<VrTestJob> job = m_pipe.poll();

      if (!job)
        break;

      m_pending = job;
      std::vector<std::string> lines = execute(job->line);

      // An empty answer means the request goes on over the next frames
      if (!lines.empty())
        answer(m_pending, std::move(lines));
    }
  }


  void VrTestBackend::finishWaits() {
    if (!m_pending || dropAbandoned())
      return;

    if (m_captureKind == CaptureKind::None) {
      if (m_frame >= m_waitUntil)
        answer(m_pending, { str::format("ok frame=", m_frame) });
    } else if (m_frame > m_captureDeadline) {
      bool eyes = m_captureKind == CaptureKind::Eyes;
      m_captureKind = CaptureKind::None;
      answer(m_pending, { eyes ? "err no frame was submitted in time" : "err the panel was not shown again in time" });
    }
  }


  std::string VrTestBackend::describeState() const {
    std::string panels;

    for (uint32_t i = 0; i < VrPanelCount; i++) {
      if (m_places[i].shown) {
        if (!panels.empty())
          panels += ",";
        panels += vrTestPanelName(VrPanelId(i));
      }
    }

    return str::format("frame=", m_frame, " backend=", m_inner->name(),
      " context=", m_context == VrInputContext::Menu ? "menu" : "game",
      " panels=", panels.empty() ? "-" : panels, " ", m_input.describe(),
      " signal=", m_signal.armed() ? std::to_string(m_signal.count()) : std::string("off"));
  }


  std::vector<std::string> VrTestBackend::execute(const std::string& line) {
    VrTestCommand command = vrParseTestCommand(line);

    switch (command.verb) {
      case VrTestVerb::Invalid:
        return { "err " + command.error };

      case VrTestVerb::Ping:
        return { "ok pong fnvvr-test-vr 1" };

      case VrTestVerb::State:
        return { "ok " + describeState() };

      case VrTestVerb::Memory:
        return { "ok " + vrTestMemoryStatus() };

      case VrTestVerb::PipBoy: {
        const VrTestPipBoyInfo& info = VrTestHooks::pipBoy();
        static const char* const States[] = { "closed", "changing", "open" };
        std::string where = info.known
          ? str::format(" screen ", info.screen.x, " ", info.screen.y, " ", info.screen.z,
                        " facing ", info.facing.x, " ", info.facing.y, " ", info.facing.z)
          : std::string(" screen unknown");
        return { str::format("ok state=", States[info.state < 3 ? info.state : 1],
          " looking=", info.looking ? 1 : 0, where) };
      }

      case VrTestVerb::PipBoyOpen:
        VrTestHooks::requestPipBoy(VrTestPipBoyRequest::Open);
        return { "ok asked; see 'pipboy' for its state" };

      case VrTestVerb::PipBoyClose:
        VrTestHooks::requestPipBoy(VrTestPipBoyRequest::Close);
        return { "ok asked; see 'pipboy' for its state" };

      case VrTestVerb::Compass: {
        if (command.path.empty()) {
          const std::string& settings = VrTestHooks::compass();
          return { settings.empty() ? "err no compass yet" : "ok " + settings };
        }

        size_t space = command.path.find(' ');
        std::string name  = command.path.substr(0, space);
        std::string value = command.path.substr(space + 1);
        VrCompassConfig check;

        if (VrTestHooks::compass().empty())
          return { "err no compass yet (off, or no frame in game drawn)" };

        if (!vrSetCompassValue(check, name, value))
          return { "err unknown setting or value; names: width top angle topdeg distance span backdrop opacity gap quest" };

        VrTestHooks::requestCompass(name, value);
        return { "ok asked; see 'compass' a frame later" };
      }

      case VrTestVerb::SignalOn:
        m_signal.arm(true);
        return { str::format("ok signal=", m_signal.count()) };

      case VrTestVerb::SignalOff:
        m_signal.arm(false);
        return { "ok signal=off" };

      case VrTestVerb::Head:
        m_input.setHead(command.pose);
        return { "ok" };

      case VrTestVerb::Hand:
        m_input.setHand(command.hand, command.pose);
        return { "ok" };

      case VrTestVerb::Release:
        m_input.release(command.release);
        return { "ok" };

      case VrTestVerb::Press:
        m_input.press(command.action);
        return { "ok" };

      case VrTestVerb::Hold:
        m_input.hold(command.action);
        return { "ok" };

      case VrTestVerb::Up:
        m_input.up(command.action);
        return { "ok" };

      case VrTestVerb::Stick:
        m_input.setStick(command.stick, command.value);
        return { "ok" };

      case VrTestVerb::Aim: {
        const VrControllerState& real = m_lastReal.controllers[uint32_t(command.hand)];
        const VrControllerState& now  = m_lastMerged.controllers[uint32_t(command.hand)];

        if (!now.isActive && !real.isActive)
          return { "err the hand is not tracked; place it with 'hand' first" };

        VrVector3 origin = (now.isActive ? now : real).aimPose.position;
        VrPose aim;
        VrVector3 target;

        if (!vrTestAimPose(m_places[uint32_t(command.panel)], m_lastMerged.headPose, origin,
            command.u, command.v, aim, target))
          return { std::string("err the panel '") + vrTestPanelName(command.panel) + "' is not shown" };

        m_input.setHand(command.hand, aim);
        return { "ok target " + formatVector(target) };
      }

      case VrTestVerb::Wait:
        m_waitUntil = m_frame + command.count;
        return { };

      case VrTestVerb::WaitFrame:
        m_waitUntil = command.count;
        return { };

      case VrTestVerb::CaptureEyes:
        m_captureKind     = CaptureKind::Eyes;
        m_capturePath     = command.path;
        m_captureDeadline = m_frame + CaptureFrames;
        return { };

      case VrTestVerb::CapturePanel: {
        uint32_t index = uint32_t(command.panel);

        if (!m_places[index].shown)
          return { std::string("err the panel '") + vrTestPanelName(command.panel) + "' is not shown" };

        m_captureKind     = CaptureKind::Panel;
        m_capturePanel    = command.panel;
        m_capturePath     = command.path;
        m_captureDeadline = m_frame + CaptureFrames;
        return { };
      }
    }

    return { "err unknown request" };
  }

}
