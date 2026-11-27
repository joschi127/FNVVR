#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <memory>

#include "../../dxvk/dxvk_instance.h"

#include "../../util/config/config.h"
#include "../../util/log/log.h"
#include "../../util/util_env.h"
#include "../../util/util_string.h"

#include "vr_backend_factory.h"
#include "vr_crash_log.h"
#include "vr_d3d9_bridge.h"
#include "vr_emulator_backend.h"
#include "vr_emulator_keyboard.h"
#include "vr_preview_panels.h"
#include "vr_preview_window.h"
#include "vr_test_backend.h"
#include "vr_extension_provider.h"
#include "vr_game_console.h"
#include "vr_game_settings.h"
#include "vr_game_space.h"
#include "vr_gamepad_head.h"
#include "vr_game_hotkeys.h"
#include "vr_gamepad_hook.h"
#include "vr_large_address.h"
#include "vr_live_casino.h"
#include "vr_engine_patches.h"
#include "vr_game_heap.h"
#include "vr_fast_math.h"
#include "vr_loader_queue.h"
#include "vr_sound_focus.h"
#include "vr_game_window.h"
#include "vr_interface_shape.h"
#include "vr_launcher_overlay.h"
#include "vr_menu_cursor.h"
#include "vr_options_file.h"
#include "vr_options_menu.h"
#include "vr_stereo_renderer.h"
#include "vr_system.h"
#include "vr_text_files.h"
#include "vr_weapon_types.h"

namespace dxvk {

  static std::unique_ptr<VrSystem> g_vrSystem;
  static bool                      g_vrInitialized = false;

  // Never deleted: it runs on its own thread until the launcher's process ends
  static VrLauncherOverlay*        g_launcherOverlay = nullptr;


  namespace {

    std::wstring lowercase(std::wstring text) {
      for (wchar_t& c : text)
        c = wchar_t(std::towlower(c));
      return text;
    }

    /**
     * \brief Logs the modules loaded from outside the Windows folder
     *
     * Script extenders, plugins and other graphics wrappers (ENB, ReShade)
     * can change the camera or the image, so their presence is part of
     * what a distortion report needs.
     */
    void logLoadedModules() {
      WCHAR windows[MAX_PATH] = { };
      UINT length = GetSystemWindowsDirectoryW(windows, MAX_PATH);
      std::wstring windowsPath = lowercase(std::wstring(windows, length < MAX_PATH ? length : 0));

      HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);

      if (snapshot == INVALID_HANDLE_VALUE)
        return;

      std::string list;
      MODULEENTRY32W entry = { };
      entry.dwSize = sizeof(entry);

      for (BOOL found = Module32FirstW(snapshot, &entry); found; found = Module32NextW(snapshot, &entry)) {
        std::wstring path = lowercase(entry.szExePath);

        if (!windowsPath.empty() && path.compare(0, windowsPath.size(), windowsPath) == 0)
          continue;

        if (!list.empty())
          list += ", ";

        list += str::fromws(entry.szExePath);
      }

      CloseHandle(snapshot);
      Logger::info(str::format("VR: Modules loaded outside the Windows folder: ", list));
    }

  }


  VrSystem::VrSystem(std::unique_ptr<IVRBackend> backend, bool showPreview,
    const VrPanelConfig& panel, const VrTurnConfig& turn, bool weaponInHand, bool headsetResolution,
    const VrGameTimerConfig& timer, const VrRenderPassConfig& passes, const VrDeveloperConfig& developer,
    const VrSwingConfig& swing)
  : m_backend(std::move(backend)),
    m_extensionProvider(std::make_unique<VrExtensionProvider>(*m_backend)),
    m_showPreview(showPreview),
    m_panelConfig(panel),
    m_turnConfig(turn),
    m_weaponInHand(weaponInHand),
    m_timerConfig(timer),
    m_passConfig(passes),
    m_developerConfig(developer),
    m_swingConfig(swing),
    m_headsetResolution(headsetResolution) { }


  VrSystem::~VrSystem() {
    detachDevice();

    // The emulator goes with the backend
    VrPreviewPanels::enable(nullptr);
  }


  void VrSystem::initialize() {
    if (g_vrInitialized)
      return;

    g_vrInitialized = true;

    Config config = Config::getUserConfig();
    config.merge(Config::getAppConfig(env::getExePath()));

    std::string name = env::getEnvVar("DXVK_VR_BACKEND");

    if (name.empty())
      name = config.getOption<std::string>("d3d9.vrBackend", "off");

    if (name == "off" || name.empty())
      return;

    // The launcher loads this DLL too, and has no game to render
    if (VrLauncherOverlay::isLauncherProcess()) {
      if ((name == "openvr" || name == "openxr") && config.getOption<bool>("d3d9.vrLauncherOverlay", true)) {
        VrPanelConfig panel;
        g_launcherOverlay = new VrLauncherOverlay(name,
          config.getOption<float>("d3d9.vrPanelDistance", panel.distance),
          config.getOption<float>("d3d9.vrPanelWidth", panel.width));
      }

      return;
    }

    std::unique_ptr<IVRBackend> backend = vrCreateBackend(name);

    if (!backend) {
      Logger::err(str::format("VR: Backend '", name, "' is unknown or not available, VR is disabled"));
      return;
    }

    Logger::info(str::format("VR: Using backend '", backend->name(), "'"));

    VrCrashLog::install();

    // The emulator has no headset to look through, so it shows a window by default
    bool showPreview = config.getOption<bool>("d3d9.vrPreviewWindow", name == "emulator");

    VrPanelConfig panel;
    panel.distance = config.getOption<float>("d3d9.vrPanelDistance", panel.distance);
    panel.width    = config.getOption<float>("d3d9.vrPanelWidth",    panel.width);
    panel.hudDistance = config.getOption<float>("d3d9.vrHudDistance", panel.hudDistance);
    panel.hudWidth    = config.getOption<float>("d3d9.vrHudWidth",    panel.hudWidth);
    panel.hudHeight   = config.getOption<float>("d3d9.vrHudHeight",   panel.hudHeight);
    panel.hudMessagesOffset = config.getOption<float>("d3d9.vrHudMessagesOffset", panel.hudMessagesOffset);

    // HUD tiles other mods anchor in the world, and their tiles kept in front of the head
    VrWorldTileConfig& worldTiles = panel.worldTiles;
    worldTiles.enabled      = config.getOption<bool>("d3d9.vrWorldTiles", worldTiles.enabled);
    worldTiles.width        = std::clamp(config.getOption<float>("d3d9.vrWorldTileWidth", worldTiles.width), 0.1f, 20.0f);
    worldTiles.hideDistance = std::max(config.getOption<float>("d3d9.vrWorldTileHideDistance", worldTiles.hideDistance), 1.0f);
    worldTiles.maxDistance  = std::max(config.getOption<float>("d3d9.vrWorldTileMaxDistance", worldTiles.hideDistance), 1.0f);
    panel.hudHeadTiles = vrSplitNameList(config.getOption<std::string>("d3d9.vrHudHeadTiles", ""));

    // The compass along the top of the view
    VrCompassConfig& compass = panel.compass;
    compass.enabled   = config.getOption<bool>("d3d9.vrCompass", compass.enabled);
    compass.span      = std::clamp(config.getOption<float>("d3d9.vrCompassSpan", compass.span), 30.0f, 360.0f);
    // Width and top edge in degrees, or "auto" (or not set): from the headset's field of view
    auto degreesOrAuto = [&] (const char* name, float low, float high) {
      std::string text = config.getOption<std::string>(name, "");

      if (text.empty() || text == "auto" || text == "Auto")
        return 0.0f;

      char* end = nullptr;
      float value = std::strtof(text.c_str(), &end);

      if (!end || *end != '\0' || !std::isfinite(value)) {
        Logger::warn(str::format("VR: ", name, " '", text, "' is neither a number nor auto, auto is used"));
        return 0.0f;
      }

      // 0 means auto, so a fixed value keeps away from it
      value = std::clamp(value, low, high);
      return value == 0.0f ? 0.01f : value;
    };

    compass.angle      = degreesOrAuto("d3d9.vrCompassAngle", 10.0f, 120.0f);
    compass.top        = degreesOrAuto("d3d9.vrCompassTop", -60.0f, 80.0f);
    compass.widthShare = std::clamp(config.getOption<float>("d3d9.vrCompassWidthShare", compass.widthShare), 0.05f, 1.0f);
    compass.topShare   = std::clamp(config.getOption<float>("d3d9.vrCompassTopShare", compass.topShare), -1.0f, 1.0f);
    compass.distance   = std::clamp(config.getOption<float>("d3d9.vrCompassDistance", compass.distance), 0.3f, 10.0f);
    compass.backdrop   = config.getOption<bool>("d3d9.vrCompassBackdrop", compass.backdrop);
    compass.markerGap  = std::clamp(config.getOption<float>("d3d9.vrCompassMarkerGap", compass.markerGap), -40.0f, 14.0f);
    compass.markerGapNoBackdrop = std::clamp(config.getOption<float>("d3d9.vrCompassMarkerGapNoBackdrop",
      compass.markerGapNoBackdrop), -40.0f, 14.0f);
    compass.questScale = std::clamp(config.getOption<float>("d3d9.vrCompassQuestScale", compass.questScale), 0.5f, 2.0f);
    compass.stripHeight = std::clamp(config.getOption<float>("d3d9.vrCompassStripHeight", compass.stripHeight), 0.5f, 1.0f);
    compass.markerSize  = std::clamp(config.getOption<float>("d3d9.vrCompassMarkerSize", compass.markerSize), 0.5f, 1.5f);
    compass.gameStrip   = config.getOption<bool>("d3d9.vrCompassGameStrip", compass.gameStrip);
    compass.backdropOpacity = std::clamp(config.getOption<float>("d3d9.vrCompassBackdropOpacity", compass.backdropOpacity), 0.0f, 1.0f);

    // Colours as RRGGBB; DXVK gives an empty text for an option not set
    auto colourOption = [&] (const char* name, uint32_t& colour) {
      std::string text = config.getOption<std::string>(name, "");

      if (text.empty())
        return;

      char* end = nullptr;
      unsigned long value = std::strtoul(text.c_str(), &end, 16);

      if (end && *end == '\0' && text.size() == 6)
        colour = uint32_t(value);
      else
        Logger::warn(str::format("VR: ", name, " '", text, "' is not RRGGBB, kept the default"));
    };

    // Friends: their name plates and their marks on the compass in one colour
    colourOption("d3d9.vrFriendColour", worldTiles.personColour);
    colourOption("d3d9.vrFriendAlertColour", worldTiles.alertColour);
    compass.friendColour = worldTiles.personColour;

    // The health bar below the compass
    compass.health = config.getOption<bool>("d3d9.vrHealthBar", compass.health);
    std::string healthAlign = config.getOption<std::string>("d3d9.vrHealthBarAlign", "");
    if (healthAlign == "right" || healthAlign == "Right")
      compass.healthAlign = VrHealthAlign::Right;
    else if (healthAlign == "centre" || healthAlign == "center" || healthAlign == "Centre")
      compass.healthAlign = VrHealthAlign::Centre;
    else if (!healthAlign.empty())
      Logger::warn(str::format("VR: d3d9.vrHealthBarAlign '", healthAlign, "' is neither centre nor right, right is used"));
    compass.healthWidth    = std::clamp(config.getOption<float>("d3d9.vrHealthBarWidthShare", compass.healthWidth), 0.1f, 1.0f);
    compass.healthLow      = std::clamp(config.getOption<float>("d3d9.vrHealthBarLow", compass.healthLow), 0.0f, 1.0f);
    compass.healthCritical = std::clamp(config.getOption<float>("d3d9.vrHealthBarCritical", compass.healthCritical), 0.0f, 1.0f);
    compass.healthPulse    = config.getOption<bool>("d3d9.vrHealthBarPulse", compass.healthPulse);
    colourOption("d3d9.vrHealthBarColour", compass.healthColour);
    colourOption("d3d9.vrHealthBarLowColour", compass.healthLowColour);
    colourOption("d3d9.vrHealthBarCriticalColour", compass.healthCriticalColour);

    VrTurnConfig turn;
    turn.smooth      = config.getOption<bool>("d3d9.vrSmoothTurn", turn.smooth);
    turn.snapAngle   = config.getOption<float>("d3d9.vrSnapTurnAngle", turn.snapAngle);
    turn.smoothSpeed = config.getOption<float>("d3d9.vrSmoothTurnSpeed", turn.smoothSpeed);

    // What the player picked on the options page overrides dxvk.conf
    VrOptions options;
    options.smoothTurn      = turn.smooth;
    options.smoothTurnSpeed = turn.smoothSpeed;
    options.pipBoyScale     = std::min(std::max(config.getOption<float>("d3d9.vrPipBoyScale", options.pipBoyScale),
      VrPipBoyScaleMin), vrPipBoyStepToScale(VrPipBoyScaleSteps - 1));
    options.pipBoyAngle     = std::min(std::max(config.getOption<float>("d3d9.vrPipBoyAngle", options.pipBoyAngle),
      0.0f), vrPipBoyStepToAngle(VrPipBoyAngleSteps - 1));
    options.pipBoyPanel     = config.getOption<bool>("d3d9.vrPipBoyPanel", options.pipBoyPanel);
    options.manualReload    = config.getOption<bool>("d3d9.vrManualReload", options.manualReload);
    options.physicalSneak   = config.getOption<bool>("d3d9.vrPhysicalSneak", options.physicalSneak);
    options.scopeLens       = config.getOption<bool>("d3d9.vrScopes", options.scopeLens);
    options.roomscale       = config.getOption<bool>("d3d9.vrRoomscale", options.roomscale);
    options.foveation       = config.getOption<bool>("d3d9.vrFoveation", options.foveation);
    VrOptionsFile::load(options);
    turn.smooth      = options.smoothTurn;
    turn.smoothSpeed = options.smoothTurnSpeed;

    bool emulated = dynamic_cast<VrEmulatorBackend*>(backend.get()) != nullptr;
    bool testChannel = config.getOption<bool>("d3d9.vrTestChannel", false);

    // With the test channel, the emulator's controllers drive the game as a
    // headset's do (actions, sticks, the laser on the menu panel), so that
    // tests reach the same paths; the keyboard still moves the head
    bool emulatedControllers = emulated && testChannel;

    if (auto* emulator = dynamic_cast<VrEmulatorBackend*>(backend.get())) {
      VrEmulatorKeyboard keyboard;
      emulator->setInputSource([keyboard] {
        return vrMergeInput(keyboard.sample(), VrGamepadHook::headInput());
      });
    }

    if (emulated && !emulatedControllers) {
      VrGamepadHook::Options gamepad;
      gamepad.virtualPad  = config.getOption<bool>("d3d9.vrVirtualGamepad", true);
      gamepad.headControl = config.getOption<bool>("d3d9.vrGamepadHead", true);

      if (gamepad.virtualPad || gamepad.headControl)
        VrGamepadHook::install(gamepad);
    } else {
      // A headset session can come with a gamepad the player never touches
      // (Steam's virtual controllers), which takes the mouse cursor away.
      // The VR controllers take its place while they are in use.
      VrGamepadHook::Options gamepad;
      gamepad.hidePad     = config.getOption<bool>("d3d9.vrHideGamepad", true);
      gamepad.controllers = config.getOption<bool>("d3d9.vrControllers", true);

      // The controllers point at the menu panel with the game's cursor
      if ((gamepad.hidePad || gamepad.controllers) && VrGamepadHook::install(gamepad) && gamepad.controllers) {
        VrMenuCursor::install();
        VrGameHotKeys::muteHudGamepadWheel();
      }
    }

    bool weaponInHand = config.getOption<bool>("d3d9.vrWeaponInHand", true);
    bool headsetResolution = config.getOption<bool>("d3d9.vrHeadsetResolution", true);

    VrGameTimerConfig timer;
    timer.preciseStep   = config.getOption<bool>("d3d9.vrPreciseGameTimer", timer.preciseStep);
    timer.havokPerFrame = config.getOption<bool>("d3d9.vrHavokStepPerFrame", timer.havokPerFrame);

    VrRenderPassConfig passes;
    passes.shareShadows = config.getOption<bool>("d3d9.vrShareShadows", passes.shareShadows);
    passes.handsDepthTest = config.getOption<bool>("d3d9.vrHandsDepthTest", passes.handsDepthTest);
    passes.scopeResolution = uint32_t(std::max<int32_t>(0,
      config.getOption<int32_t>("d3d9.vrScopeResolution", int32_t(passes.scopeResolution))));
    passes.gpuTimings = config.getOption<bool>("d3d9.vrGpuTimings", passes.gpuTimings);
    passes.spikeLog   = config.getOption<bool>("d3d9.vrSpikeLog", passes.spikeLog);
    passes.cpuSamples = config.getOption<bool>("d3d9.vrCpuSamples", passes.cpuSamples);
    passes.foveationZones.fullDegrees = std::min(std::max(config.getOption<float>("d3d9.vrFoveationFullAngle",
      passes.foveationZones.fullDegrees), 1.0f), 89.0f);
    passes.foveationZones.halfDegrees = std::min(std::max(config.getOption<float>("d3d9.vrFoveationHalfAngle",
      passes.foveationZones.halfDegrees), passes.foveationZones.fullDegrees), 89.0f);
    passes.foveationTestGaze = config.getOption<bool>("d3d9.vrFoveationTestGaze", passes.foveationTestGaze);

    VrDeveloperConfig developer;
    developer.enabled = config.getOption<bool>("d3d9.vrDeveloperMode", developer.enabled);

    if (developer.enabled) {
      std::vector<std::string> unknown;
      developer.weaponTypes = vrParseWeaponTypes(config.getOption<std::string>("d3d9.vrDeveloperWeapons", ""), unknown);

      for (const auto& name : unknown)
        Logger::warn(str::format("VR: d3d9.vrDeveloperWeapons: '", name, "' is not a weapon type, it is ignored"));
    }

    // The head rises back by more than the hysteresis before standing
    VrCrouchConfig crouch;
    crouch.depth = std::min(std::max(config.getOption<float>("d3d9.vrCrouchDepth", crouch.depth),
      2.0f * crouch.hysteresis), 1.0f);

    VrSwingConfig swing;
    swing.hitSpeed = config.getOption<float>("d3d9.vrMeleeHitSpeed", swing.hitSpeed);
    swing.hitsPerSecond = std::min(std::max(config.getOption<float>("d3d9.vrMeleeHitsPerSecond",
      swing.hitsPerSecond), 1.0f), 30.0f);
    swing.powerCost.base = config.getOption<int32_t>("d3d9.vrPowerAttackApCost", swing.powerCost.base);
    swing.powerCost.max  = config.getOption<int32_t>("d3d9.vrPowerAttackApCostMax", swing.powerCost.max);
    swing.powerCost.weightMult = config.getOption<float>("d3d9.vrPowerAttackApWeightMult", swing.powerCost.weightMult);

    if (emulatedControllers)
      Logger::info("VR: The test channel is on: the emulator's controllers drive the game like a headset's");

    // The emulator has no compositor: its preview draws the panels and lasers itself
    VrPreviewWindow::setWidth(uint32_t(std::max(config.getOption<int32_t>("d3d9.vrPreviewWidth",
      int32_t(VrPreviewWindow::DefaultWidth)), 0)));

    if (showPreview && config.getOption<bool>("d3d9.vrPreviewPanels", true)) {
      if (auto* emulator = dynamic_cast<VrEmulatorBackend*>(backend.get()))
        VrPreviewPanels::enable(emulator);
    }

    // Test tools drive VR input through a named pipe, off by default
    if (testChannel) {
      // String options come back empty, not as the fallback, when they are not set
      std::string pipeName = config.getOption<std::string>("d3d9.vrTestPipeName", "");
      float signalHold = std::min(std::max(config.getOption<float>("d3d9.vrTestSignalHold", 1.0f), 0.2f), 10.0f);
      backend = std::make_unique<VrTestBackend>(std::move(backend), pipeName.empty() ? "FNVVR-Test-VR" : pipeName,
        signalHold);
    }

    g_vrSystem.reset(new VrSystem(std::move(backend), showPreview, panel, turn, weaponInHand, headsetResolution,
      timer, passes, developer, swing));
    g_vrSystem->m_texts = vrReadTexts();
    g_vrSystem->m_pipBoyScale = options.pipBoyScale;
    g_vrSystem->m_pipBoyAngle = options.pipBoyAngle;
    g_vrSystem->m_pipBoyPanel = options.pipBoyPanel;
    g_vrSystem->m_manualReload = options.manualReload;
    g_vrSystem->m_handPush = config.getOption<bool>("d3d9.vrHandPush", true);
    g_vrSystem->m_eyeAspect = config.getOption<bool>("d3d9.vrEyeAspect", true);
    g_vrSystem->m_asyncHandOff = config.getOption<bool>("d3d9.vrAsyncHandOff", true);
    g_vrSystem->m_asyncSubmit = config.getOption<bool>("d3d9.vrAsyncSubmit", false);
    g_vrSystem->m_occlusionCulling = config.getOption<bool>("d3d9.vrOcclusionCulling", true);
    g_vrSystem->m_scopes = options.scopeLens;
    g_vrSystem->m_roomscale = options.roomscale;
    g_vrSystem->m_foveation = options.foveation;
    g_vrSystem->m_immersiveMinigames = config.getOption<bool>("d3d9.vrImmersiveMinigames", true);
    auto share = [&config] (const char* name, float value) {
      return std::min(std::max(config.getOption<float>(name, value), 0.0f), 1.0f);
    };

    g_vrSystem->m_pipBoyCrop.left   = share("d3d9.vrPipBoyPanelCropLeft",   VrPipBoyPanelDefaultCrop.left);
    g_vrSystem->m_pipBoyCrop.top    = share("d3d9.vrPipBoyPanelCropTop",    VrPipBoyPanelDefaultCrop.top);
    g_vrSystem->m_pipBoyCrop.right  = share("d3d9.vrPipBoyPanelCropRight",  VrPipBoyPanelDefaultCrop.right);
    g_vrSystem->m_pipBoyCrop.bottom = share("d3d9.vrPipBoyPanelCropBottom", VrPipBoyPanelDefaultCrop.bottom);
    g_vrSystem->m_pipBoyFrame = std::min(share("d3d9.vrPipBoyPanelFrame", VrPipBoyPanelDefaultFrame), 0.2f);
    g_vrSystem->m_pipBoyCornerRadius = std::min(share("d3d9.vrPipBoyPanelCornerRadius",
      VrPipBoyPanelDefaultCornerRadius), 0.2f);
    g_vrSystem->m_sneakConfig.physical = options.physicalSneak;
    g_vrSystem->m_sneakConfig.crouch   = crouch;
    g_vrSystem->m_forceGameSettings = !emulated && config.getOption<bool>("d3d9.vrForceGameSettings", true);
    g_vrSystem->m_rewriteTutorials = !emulated && config.getOption<bool>("d3d9.vrTutorials", true);
    g_vrSystem->m_hideGameWindow = !emulated && config.getOption<bool>("d3d9.vrHideGameWindow", true);
    g_vrSystem->m_keepGameActive = g_vrSystem->m_hideGameWindow
      || (emulated && config.getOption<bool>("d3d9.vrEmulatorRunWithoutFocus", false));
    DxvkInstance::registerExtensionProvider(g_vrSystem->m_extensionProvider.get());

    VrOptionsMenu::install(options, g_vrSystem->m_texts, [] (const VrOptions& changed) {
      if (g_vrSystem)
        g_vrSystem->applyOptions(changed);
    }, [] {
      VrGameConsole::requestOpen();
    }, [] {
      return g_vrSystem && g_vrSystem->m_stereoRenderer
        ? g_vrSystem->m_stereoRenderer->foveationSupport()
        : VrFoveationSupport::NoGpuSupport;
    });

    VrLargeAddress::install(g_vrSystem->m_texts);
    VrSoundFocus::install();
    if (config.getOption<bool>("d3d9.vrFastMath", true))
      VrFastMath::install();
    if (config.getOption<bool>("d3d9.vrLoaderQueueSkip", true))
      VrLoaderQueue::install();
    if (config.getOption<bool>("d3d9.vrEnginePatches", true))
      VrEnginePatches::install(config.getOption<std::string>("d3d9.vrEnginePatchGroups", "all"));
    if (config.getOption<bool>("d3d9.vrGameHeap", true))
      VrGameHeap::install();

    if (g_vrSystem->m_hideGameWindow)
      VrSoundFocus::keepPluginSound();

    VrLiveCasino::install(
      config.getOption<bool>("d3d9.vrRealTimeCasino", true),
      config.getOption<bool>("d3d9.vrRealTimeVigorTester", true),
      config.getOption<bool>("d3d9.vrRealTimeCaravan", true));
  }


  void VrSystem::onInterfaceCreated() {
    if (g_launcherOverlay)
      g_launcherOverlay->start();

    if (!g_vrSystem)
      return;

    g_vrSystem->chooseResolution();

    if (g_vrSystem->m_forceGameSettings) {
      g_vrSystem->m_forceGameSettings = false;
      VrGameSettings::apply();
    }

    if (g_vrSystem->m_rewriteTutorials) {
      g_vrSystem->m_rewriteTutorials = false;
      g_vrSystem->m_tutorials = VrTutorials::install(g_vrSystem->m_texts);
    }

    if (g_vrSystem->m_keepGameActive) {
      g_vrSystem->m_keepGameActive = false;
      VrGameSettings::keepActive();
    }
  }


  void VrSystem::chooseResolution() {
    if (!m_headsetResolution || m_resolutionChosen)
      return;

    m_resolutionChosen = true;

    VrExtent eye = m_backend->recommendedEyeExtent();

    if (!eye.width || !eye.height) {
      Logger::warn("VR: The headset reported no eye resolution, the game keeps its own");
      return;
    }

    // At the eye's aspect, the interface keeps its 4:3 layout and is
    // stretched over the narrower target; without that, the 4:3 target
    bool eyeAspect = m_eyeAspect && VrInterfaceShape::install();
    VrExtent size = vrComputeGameResolution(eye, eyeAspect ? 0.0f : VrInterfaceMinAspect);

    if (!m_resolution.apply(size)) {
      Logger::info("VR: The game's display settings were not found, the game keeps its own resolution");
      return;
    }

    Logger::info(str::format("VR: The game renders at ", size.width, "x", size.height,
      " in a window, for an eye resolution of ", eye.width, "x", eye.height));
  }


  void VrSystem::applyOptions(const VrOptions& options) {
    m_turnConfig.smooth      = options.smoothTurn;
    m_turnConfig.smoothSpeed = options.smoothTurnSpeed;

    m_pipBoyScale = options.pipBoyScale;
    m_pipBoyAngle = options.pipBoyAngle;
    m_pipBoyPanel = options.pipBoyPanel;
    m_manualReload = options.manualReload;
    m_sneakConfig.physical = options.physicalSneak;
    m_scopes = options.scopeLens;
    m_roomscale = options.roomscale;
    m_foveation = options.foveation;

    if (m_stereoRenderer) {
      m_stereoRenderer->setScopesEnabled(m_scopes);
      m_stereoRenderer->setRoomscale(m_roomscale);
      m_stereoRenderer->setFoveationEnabled(m_foveation);
      m_stereoRenderer->setTurnConfig(m_turnConfig);
      m_stereoRenderer->setPipBoyScale(m_pipBoyScale);
      m_stereoRenderer->setPipBoyAngle(m_pipBoyAngle);
      m_stereoRenderer->setPipBoyPanel(m_pipBoyPanel);
      m_stereoRenderer->setManualReload(m_manualReload);
      m_stereoRenderer->setSneakConfig(m_sneakConfig);
    }

    VrOptionsFile::save(options);
  }


  VrSystem* VrSystem::get() {
    return g_vrSystem.get();
  }


  VrPresentMode VrSystem::onPresent(IDirect3DSwapChain9* swapchain) {
    VrGameHeap::logStatsIfDue();

    if (!g_vrSystem || !g_vrSystem->m_stereoRenderer)
      return VrPresentMode::Game;

    VrPresentMode mode = g_vrSystem->m_stereoRenderer->onPresent(swapchain);

    if (!g_vrSystem->m_hideGameWindow || mode == VrPresentMode::Drop || !VrGameWindow::hide(swapchain))
      return mode;

    // Without a present, nothing else paces the frames the headset does not pace
    if (mode == VrPresentMode::Game)
      VrGameWindow::waitUnpacedFrame();

    return VrPresentMode::Hidden;
  }


  bool VrSystem::attachDevice(IDirect3DDevice9* device) {
    // The renderer has read the resolution by the time the device exists
    m_resolution.restoreSettings();

    if (m_hasSession)
      return true;

    VrGraphicsBinding binding;

    if (!VrD3D9Bridge::queryGraphicsBinding(device, binding))
      return false;

    binding.asyncHandOff = m_asyncHandOff;

    logLoadedModules();

    VkPhysicalDevice preferred = m_backend->selectPhysicalDevice(binding.instance);

    if (preferred && preferred != binding.physicalDevice)
      Logger::warn("VR: The VR runtime prefers a different GPU than the one DXVK renders on");

    m_hasSession = m_backend->beginSession(binding);

    if (!m_hasSession) {
      Logger::err("VR: Failed to start the VR session");
      return false;
    }

    m_stereoRenderer = VrStereoRenderer::install(*m_backend, device, m_showPreview, m_panelConfig, m_turnConfig,
      m_weaponInHand, m_timerConfig, m_passConfig, m_developerConfig, m_swingConfig, m_texts);

    if (m_stereoRenderer) {
      m_stereoRenderer->setPipBoyScale(m_pipBoyScale);
      m_stereoRenderer->setPipBoyAngle(m_pipBoyAngle);
      m_stereoRenderer->setPipBoyPanel(m_pipBoyPanel);
      m_stereoRenderer->setMinigamesEnabled(m_immersiveMinigames);
      m_stereoRenderer->setPipBoyPanelCrop(m_pipBoyCrop, m_pipBoyFrame, m_pipBoyCornerRadius);
      m_stereoRenderer->setScopesEnabled(m_scopes);
      m_stereoRenderer->setManualReload(m_manualReload);
      m_stereoRenderer->setRoomscale(m_roomscale);
      m_stereoRenderer->setFoveationEnabled(m_foveation);
      m_stereoRenderer->setHandPush(m_handPush);
      m_stereoRenderer->setSneakConfig(m_sneakConfig);
      m_stereoRenderer->setAsyncSubmit(m_asyncSubmit);
      m_stereoRenderer->setOcclusionCulling(m_occlusionCulling);
    }

    return true;
  }


  void VrSystem::detachDevice() {
    if (!m_hasSession)
      return;

    // The eye textures belong to the device and must go before the session
    VrPreviewPanels::clear();
    m_stereoRenderer = nullptr;
    m_backend->endSession();
    m_hasSession = false;
  }

}
