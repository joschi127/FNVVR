#include "vr_test_backend.h"

#include <cstdio>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_test_commands.h"
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


  VrTestBackend::VrTestBackend(std::unique_ptr<IVRBackend> inner, const std::string& pipeName)
  : m_inner(std::move(inner)) {
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
    return timing;
  }


  VrInputState VrTestBackend::pollInput(int64_t displayTime) {
    m_lastReal = m_inner->pollInput(displayTime);
    takeRequests();
    m_lastMerged = m_input.merge(m_lastReal, m_frame);
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
        std::vector<uint8_t> rgba;

        if (ensureCapture(error) && m_capture.read(frame.images[e], true, rgba, error)) {
          std::string file = m_capturePath + suffix[e];

          if (vrWritePng(file, rgba.data(), frame.images[e].extent.width, frame.images[e].extent.height))
            files.push_back(file);
          else
            error = "could not write " + file;
        }
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
      m_pixels[index].clear();

      if (m_pending && m_captureKind == CaptureKind::Panel && m_capturePanel == id) {
        std::string error;
        std::vector<uint8_t> rgba;

        if (ensureCapture(error) && m_capture.read(panel.image, false, rgba, error)
         && !vrWritePng(m_capturePath, rgba.data(), panel.image.extent.width, panel.image.extent.height))
          error = "could not write " + m_capturePath;

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


  bool VrTestBackend::submitPanelPixels(VrPanelId id, const VrPanelPixels& panel) {
    uint32_t index = uint32_t(id);

    if (index < VrPanelCount && panel.rgba && panel.width && panel.height) {
      VrTestPanelPlace& place = m_places[index];
      place.shown  = true;
      place.pose   = panel.pose;
      place.width  = panel.metres;
      place.height = panel.metres * float(panel.height) / float(panel.width);
      place.anchor = VrPanelAnchor::Room;

      // Kept for captures: such panels are submitted again only when they change
      m_pixels[index].assign(panel.rgba, panel.rgba + size_t(panel.width) * panel.height * 4);
      m_pixelExtent[index] = { panel.width, panel.height };
    }

    return m_inner->submitPanelPixels(id, panel);
  }


  void VrTestBackend::answer(std::shared_ptr<VrTestJob>& job, std::vector<std::string> lines) {
    if (job) {
      job->reply.set_value(std::move(lines));
      job = nullptr;
    }
  }


  bool VrTestBackend::ensureCapture(std::string& error) {
    if (m_capture.ready() || m_capture.init(m_binding))
      return true;

    error = "no graphics device for captures";
    return false;
  }


  void VrTestBackend::takeRequests() {
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
    if (!m_pending)
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
      " panels=", panels.empty() ? "-" : panels, " ", m_input.describe());
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

        if (!m_pixels[index].empty()) {
          if (!vrWritePng(command.path, m_pixels[index].data(), m_pixelExtent[index].width, m_pixelExtent[index].height))
            return { "err could not write " + command.path };
          return { "ok " + command.path };
        }

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
