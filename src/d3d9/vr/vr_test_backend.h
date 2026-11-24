#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "../../util/thread.h"

#include "vr_backend.h"
#include "vr_test_capture.h"
#include "vr_test_input.h"
#include "vr_test_pipe.h"
#include "vr_test_signal.h"

namespace dxvk {

  /**
   * \brief Backend in front of the real one that lets test tools drive VR
   *
   * Only created with d3d9.vrTestChannel = True. Forwards every call to the
   * real backend (emulator, OpenVR or OpenXR) and adds what a test tool sets through
   * the named pipe: head and hand poses, actions, sticks, aiming at panels,
   * waiting for frames and captures of the eyes and panels. Without requests
   * it only forwards; see docs/testing/vr-test-channel.md.
   *
   * With d3d9.vrAsyncSubmit, submitFrame, submitPanel and hidePanel come
   * from the renderer's submission thread (see VrFrameSubmitter) while the
   * game thread takes requests in pollInput. What both use (the panels' places, the pending request and
   * its capture) is guarded by m_mutex.
   */
  class VrTestBackend : public IVRBackend {

  public:

    VrTestBackend(std::unique_ptr<IVRBackend> inner, const std::string& pipeName, float signalHold);
    ~VrTestBackend();

    const char* name() const override { return m_inner->name(); }
    VrVulkanRequirements queryVulkanRequirements() override { return m_inner->queryVulkanRequirements(); }
    std::vector<std::string> queryDeviceExtensions(VkPhysicalDevice physicalDevice) override {
      return m_inner->queryDeviceExtensions(physicalDevice);
    }
    VkPhysicalDevice selectPhysicalDevice(VkInstance instance) override {
      return m_inner->selectPhysicalDevice(instance);
    }

    bool beginSession(const VrGraphicsBinding& binding) override;
    void endSession() override { m_inner->endSession(); }
    bool beginPanelSession() override { return m_inner->beginPanelSession(); }
    VrSessionState sessionState() const override { return m_inner->sessionState(); }
    VrExtent recommendedEyeExtent() const override { return m_inner->recommendedEyeExtent(); }

    VrFrameTiming waitFrame() override;
    void beginFrame() override { m_inner->beginFrame(); }
    VrInputState pollInput(int64_t displayTime) override;
    void setInputContext(VrInputContext context) override;
    std::array<VrEyeView, VrEyeCount> locateViews(int64_t displayTime) override;

    bool supportsEyeTracking() override { return m_inner->supportsEyeTracking(); }
    bool locateGaze(std::array<VrGaze, VrEyeCount>& gaze) override { return m_inner->locateGaze(gaze); }

    bool submitFrame(const VrFrameSubmission& frame) override;
    void submitEmptyFrame(int64_t displayTime) override { m_inner->submitEmptyFrame(displayTime); }
    bool submitPanel(VrPanelId id, const VrPanelSubmission& panel) override;
    bool panelsNeedFrames() const override { return m_inner->panelsNeedFrames(); }
    void hidePanel(VrPanelId id) override;
    bool movePanel(VrPanelId id, const VrPose& pose, float width, VrPanelAnchor anchor) override;
    bool submitPanelPixels(VrPanelId id, const VrPanelPixels& panel) override;

    void showPointer(VrHand hand, const VrPointerSubmission& pointer) override { m_inner->showPointer(hand, pointer); }
    void hidePointer(VrHand hand) override { m_inner->hidePointer(hand); }
    void applyHaptic(VrHand hand, float amplitude, int64_t durationNs) override {
      m_inner->applyHaptic(hand, amplitude, durationNs);
    }
    void fadeView(float seconds, float opacity) override { m_inner->fadeView(seconds, opacity); }
    bool showKeyboard(const VrKeyboardRequest& request) override { return m_inner->showKeyboard(request); }
    void hideKeyboard() override { m_inner->hideKeyboard(); }
    VrKeyboardEvent takeKeyboardEvent(std::string& typed) override { return m_inner->takeKeyboardEvent(typed); }

  private:

    enum class CaptureKind { None, Eyes, Panel };

    void takeRequests();
    void finishWaits();
    std::vector<std::string> execute(const std::string& line);
    std::string describeState() const;
    void answer(std::shared_ptr<VrTestJob>& job, std::vector<std::string> lines);
    bool ensureCapture(std::string& error);
    bool captureToPng(const VrEyeImage& image, bool opaque, const std::string& path, std::string& error);
    bool dropAbandoned();

    std::unique_ptr<IVRBackend> m_inner;
    VrTestPipe                  m_pipe;
    VrTestInput                 m_input;
    VrTestCapture               m_capture;
    VrGraphicsBinding           m_binding;

    uint64_t                    m_frame = 0;
    bool                        m_waitedSincePoll = false;
    VrInputState                m_lastReal;
    VrInputState                m_lastMerged;
    VrInputContext              m_context = VrInputContext::Game;
    VrTestSignal                m_signal;
    std::chrono::steady_clock::time_point m_lastPoll = std::chrono::steady_clock::now();

    /// Guards m_places and the pending request with its capture
    dxvk::mutex                 m_mutex;

    std::array<VrTestPanelPlace, VrPanelCount> m_places;

    /// The request being worked on over several frames (wait, capture)
    std::shared_ptr<VrTestJob>  m_pending;
    uint64_t                    m_waitUntil = 0;
    CaptureKind                 m_captureKind = CaptureKind::None;
    VrPanelId                   m_capturePanel = VrPanelId::Menu;
    std::string                 m_capturePath;
    uint64_t                    m_captureDeadline = 0;

  };

}
