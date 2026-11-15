# FNVVR code base analysis (for the co-op compatibility check)

State analysed: branch `coop-compat` at `3100f9e9` (upstream `ef67947b`, v0.19.1). Only FNVVR is
analysed here; the New Vegas True Co-op side is covered separately. All game addresses are virtual
addresses in `FalloutNV.exe` 1.4.0.525 (image base `0x00400000`, so RVA = VA - `0x00400000`). File
references are relative to `src/d3d9/vr/` unless they start with `src/`.

Contents:
1. Overview (architecture, frame flow, threads, backends, build, config, logs, tests)
2. Inventory of game-process interventions (the main part)
3. Game-state assumptions
4. External interfaces
5. Robustness
6. Potential friction points with other NVSE plugins / multiplayer mods

---

## 1. Overview

### 1.1 Architecture

- FNVVR is a fork of DXVK reduced to the 32-bit D3D9 front end. The game loads it as `d3d9.dll`
  from its own folder. The VR layer lives in `src/d3d9/vr/` (about 330 files, ~81k lines) and is
  linked into the same DLL (`meson.build`: `vr_src` for platform-independent code that the unit tests
  also build, `vr_dxvk_src` for the parts that need DXVK/Vulkan).
- It is **not an NVSE plugin**: no `NVSEPlugin_Load`, no NVSE API use, no script commands. It reaches
  the game only by address (all addresses in `vr_game_addresses.h`, ~1500 constants) and by patching
  `FalloutNV.exe` code in memory through `VrGameMemory` (`vr_game_memory.h/.cpp`).
- Touch points in the DXVK code (deliberately small):
  - `src/d3d9/d3d9_main.cpp:25` `VrSystem::initialize()` and `:28` `VrSystem::onInterfaceCreated()`
    in `CreateD3D9` (i.e. on the game's `Direct3DCreate9`).
  - `src/d3d9/d3d9_interface.cpp:437` `vr->attachDevice(device)` at the end of `CreateDevice`.
  - `src/d3d9/d3d9_device.cpp:166` `vr->detachDevice()` in the device destructor.
  - `src/d3d9/d3d9_swapchain.cpp:130` `VrSystem::onPresent(this)` in `D3D9SwapChainEx::Present`
    (drop, hide or immediate present, see 1.2).
  - `src/d3d9/d3d9_device.cpp:2539`, `src/d3d9/d3d9_adapter.cpp:170`: RESZ depth resolve offered on
    any GPU when `DepthResolve.dll` (an NVSE plugin) is loaded (`vr_depth_resolve.cpp:11`).
  - `src/dxvk/dxvk_instance.cpp:22` extension provider: the VR backend adds the Vulkan instance and
    device extensions the runtime needs (`vr_extension_provider.cpp`).
- Orchestration: `vr_system.cpp` (config, install order), `vr_stereo_renderer.cpp` (3325 lines, the
  per-frame driver and most hook installs), backend interface `vr_backend.h` (`IVRBackend`).

### 1.2 Data flow per frame

1. The game's main loop (TESMain::OnIdle) runs the game logic once.
2. Its call to `Main::Swap` at `0x0086EDE8` is redirected to `VrStereoRenderer::swapHook`
   (`vr_stereo_renderer.cpp:222`, hook body `:495`), which calls `renderFrame` (`:596`).
3. `renderFrame`: clears the static menu background flag (`:602`, `:998`), reads the game state
   (`vr_game_state.cpp:32`), updates comfort/developer/reload helpers, then:
   - Main menu or loading screen: `renderPanelFrame` (`:868`): one normal game frame whose whole
     image (interface included) goes to a flat panel overlay; `waitFrame` still paces the game.
   - Otherwise: `m_backend.waitFrame()` (`:653`; OpenVR `WaitGetPoses`, blocking, this is the frame
     pacing), `pollInput`, head tracking, controls (gamepad state, head look, physics grab, hand pick,
     Pip-Boy gaze), scope render, then **`renderEye(0)` and `renderEye(1)`**: each sets the eye pose on
     the game's camera and calls the original `Main::Swap` once (`:1106`). So the game's whole render
     path (world, first person, image space, interface, `Present`) runs **twice per game-logic frame**.
4. Inside each `Main::Swap`: `PlaceCamera` (3 call sites) and `NiCamera::UpdateWorldData` (vtable)
   are hooked to keep the eye pose; culling (`vr_culling_camera.cpp`), sky (`vr_sky_anchor.cpp`), tree
   leaves, render passes (shadow map shared between eyes, timing), first-person weapon placement
   (`vr_weapon_hand.cpp`) and the Pip-Boy render (`vr_pipboy_render.cpp`) hook their respective calls.
   At `InterfaceManager::RenderInterface` (`0x007144D3`, chained) the finished 3D image is copied
   into the eye texture (`captureEye`), and the interface pass is used to draw HUD/menu layers into
   panel textures instead of the backbuffer (`renderInterfaceHook`, `:505`).
5. `Present` per eye: `VrStereoRenderer::onPresent` returns `Drop` for eye 0 and `Immediate`/`Game`
   for eye 1 (`:569`); `VrSystem::onPresent` (`vr_system.cpp:396`) hides the window and returns
   `Hidden` (frame ended and flushed, nothing shown on the desktop) when `vrHideGameWindow` is on.
6. After both eyes: faders drawn over the eye images, HUD/hand-HUD/menu panels submitted, then
   `VrD3D9Bridge::submitStereoFrame` hands the Vulkan images of the D3D9 eye textures to the backend
   (`vr_d3d9_bridge.*`, DXVK interop interfaces), OpenVR `Submit` + explicit timing; the
   `PostPresentHandoff` runs on the `vr-handoff` thread (`vr_openvr_backend.cpp:913`, `:931`).

### 1.3 Threads

| Thread | Created at | Purpose |
|---|---|---|
| `vr-handoff` | `vr_openvr_backend.cpp:913` (`dxvk::thread`, per session) | `PostPresentHandoff` off the game thread; locks DXVK's queue (`:946`). Off with `d3d9.vrAsyncHandOff = False`. |
| Launcher overlay thread | `vr_launcher_overlay.cpp:68` (`CreateThread`) | Only in `FalloutNVLauncher.exe`: VR panel of the launcher window. Also `SetWindowsHookExW(WH_CALLWNDPROC)` on the launcher's thread (`vr_launcher_windows.cpp:177`). |
| Preview window | `vr_preview_window.cpp` | Emulator only by default (`d3d9.vrPreviewWindow`), shows both eyes. |
| DXVK's own threads | stock DXVK | CS thread, submission/queue, presenter, shader compile workers. |

Almost all VR code runs on the game's main thread inside the redirected calls. Exceptions:
`vr_tree_leaves.cpp` runs on the game's render thread; `PlayerCharacter::GetLookingAtLocation`
(NPC gaze, `vr_npc_gaze.cpp`) may be called from game worker threads and reads under a lock.

### 1.4 Backends

- `IVRBackend` (`vr_backend.h:195`ff): session, `waitFrame`, `pollInput`, `locateViews`,
  `submitFrame`, panels, pointers, haptics, fade, SteamVR keyboard, eye-tracking gaze.
- **OpenVR** (`vr_openvr_backend.cpp`): loads `openvr_api.dll` or `openvr_api_dxvk.dll`
  (`:34`, `:224`), `VR_InitInternal` as `VRApplication_Scene` (`:250`), SteamVR Input with the action
  manifest `fnvvr\actions.json` (`:542`, `:565`; action sets `/actions/game`, `/actions/menu`,
  `/actions/hands`), explicit timing mode with application-performed `PostPresentHandoff` (`:688`).
  Requires SteamVR. An OpenXR backend exists only as an upstream probe branch.
- **Emulator** (`vr_emulator_backend.cpp`, `vr_emulator_rig.cpp`, `vr_emulator_keyboard.cpp`): no
  headset, simulated head and hands driven by the keyboard (global key state, see 2.4), default
  1440x1600 per eye, 90 Hz, **virtual time** (`realtime = false`: `waitFrame` does not sleep,
  `vr_emulator_backend.cpp:81`). With the emulator, forced game settings, tutorial rewriting and
  window hiding are skipped (`vr_system.cpp:276-279`).
- Selection: env `DXVK_VR_BACKEND`, else `d3d9.vrBackend` (`vr_system.cpp:131-137`); default `off`
  (then nothing below is installed at all).

### 1.5 Build

Meson + Ninja, mingw-w64 i686 (posix threads), Linux-native cross build (`build-win32.txt`). Output
`build.32/src/d3d9/d3d9.dll`. Unit tests are a native host executable `vr_tests`
(`meson.build` test `vr`). Exports: the standard `d3d9.def` set (`Direct3DCreate9`, `...Ex`,
`D3DPERF_*`, `Direct3DShaderValidatorCreate9`, ...), nothing FNVVR-specific.

### 1.6 Configuration

`dxvk.conf` next to the executable (plus DXVK's user config), read in `VrSystem::initialize`
(`vr_system.cpp:128-129`). Settings changed on the in-game VR page are saved in
`fnvvr\vr_options.ini` and override `dxvk.conf` (`VrOptionsFile`, `vr_system.cpp:189`, `:387`).

| Key | Default | Effect |
|---|---|---|
| `d3d9.vrBackend` | `off` | `off` / `emulator` / `openvr`; env `DXVK_VR_BACKEND` wins |
| `d3d9.vrPreviewWindow` | on for emulator | eye preview window |
| `d3d9.vrPanelDistance` / `vrPanelWidth` | 2 / 2 m | main menu / loading panel |
| `d3d9.vrHudDistance` / `vrHudWidth` / `vrHudHeight` / `vrHudMessagesOffset` | 1 / 1.5 / -0.15 / 0.25 m | HUD and in-game menu panel |
| `d3d9.vrSmoothTurn` / `vrSnapTurnAngle` / `vrSmoothTurnSpeed` | off / 30 / 120 | turning (also VR page) |
| `d3d9.vrPipBoyScale` | 1 | Pip-Boy growth (VR page) |
| `d3d9.vrManualReload` / `vrPhysicalSneak` / `vrScopes` / `vrRoomscale` / `vrFoveation` | see `VrOptions` | VR page options |
| `d3d9.vrControllers` | on | VR controllers reported as gamepad; enables menu cursor hook |
| `d3d9.vrHideGamepad` | on | real gamepads hidden from the game (headset backend) |
| `d3d9.vrVirtualGamepad` / `vrGamepadHead` | on / on | emulator only: keyboard virtual pad, LB+RB head control |
| `d3d9.vrWeaponInHand` | on | weapon in right hand; prerequisite for melee swing, hand throw, scope render, part highlight |
| `d3d9.vrHeadsetResolution` / `vrEyeAspect` | on / on | render size from the headset; interface shape patches |
| `d3d9.vrPreciseGameTimer` / `vrHavokStepPerFrame` | on / on | frame-timer fix, Havok step per frame |
| `d3d9.vrShareShadows` / `vrHandsDepthTest` / `vrScopeResolution` | see `VrRenderPassConfig` | render passes |
| `d3d9.vrFoveationFullAngle` / `HalfAngle` / `TestGaze` | 20 / 35 / off | foveation |
| `d3d9.vrDeveloperMode` / `vrDeveloperWeapons` | off / "" | god mode, ammo, weapons after each load |
| `d3d9.vrCrouchDepth` | `VrCrouchConfig` | physical sneak |
| `d3d9.vrMeleeHitSpeed` / `vrMeleeHitsPerSecond` | 2.0 / 3 | melee swing |
| `d3d9.vrPowerAttackApCost` / `...Max` / `...WeightMult` | `VrSwingConfig` | power attacks |
| `d3d9.vrHandPush` / `vrAsyncHandOff` / `vrImmersiveMinigames` | on / on / on | |
| `d3d9.vrForceGameSettings` | on (not emulator) | forces game INI values in memory (2.5) |
| `d3d9.vrTutorials` | on (not emulator) | tutorial/help texts replaced |
| `d3d9.vrHideGameWindow` | on (not emulator) | hides window, `bAlwaysActive=1`, itr-nvse IAT hook |
| `d3d9.vrFastMath` | on | math function rewrites (2.2.5) |
| `d3d9.vrEnginePatches` / `vrEnginePatchGroups` | on / `all` | 748 inline patches (2.2.6) |
| `d3d9.vrGameHeap` | on | small-block heap replacement (2.2.7) |
| `d3d9.vrRealTimeCasino` / `vrRealTimeVigorTester` / `vrRealTimeCaravan` | on / on / on | world keeps running in those menus (2.2.3) |
| `d3d9.vrLauncherOverlay` | on | launcher in the headset |

Stock DXVK keys still apply (`d3d9.maxFrameRate`, `d3d9.presentInterval`, `d3d9.hideNvidiaGpu`, ...).
The repository `dxvk.conf` documents the VR keys at the top and `vrBackend` at `dxvk.conf:1071-1089`.

Data files in `fnvvr\` next to `d3d9.dll`: `actions.json`, `bindings_*.json`,
`texts_<language>.txt`, `scopes.ini`, `vr_options.ini` (written), `no_large_address_prompt.txt`,
`no_nvse_prompt.txt` (written when the player declines the 4 GB / NVSE offer).

### 1.7 Logging

- One log, DXVK's logger: `Logger::s_instance("d3d9.log")` (`src/d3d9/d3d9_main.cpp:14`), file name
  `<exe base name>_d3d9.log` next to the executable (`src/util/log/log.cpp:134-150`), i.e.
  `FalloutNV_d3d9.log` and `FalloutNVLauncher_d3d9.log` in the game folder. `DXVK_LOG_PATH` moves it
  (`none` disables), `DXVK_LOG_LEVEL` sets the level. All VR lines start with `VR: `.
- Useful markers: `VR: Using backend`, `VR: Modules loaded outside the Windows folder: ...`
  (`vr_system.cpp:94`, logged at device creation: lists every plugin DLL), `VR: Game code at 0x...
  is already patched, it reaches 0x... in '<module>'` (`vr_game_memory.cpp`, `reportForeignCode`,
  names the other mod holding a call site), `VR: Engine patches '<group>': N of M applied, K left as
  found` (`vr_engine_patches.cpp:117`), per-feature "was not found" lines, timer and heap statistics
  (about every 10 s / 60 s), crash records from the vectored exception handler (`vr_crash_log.cpp`).

### 1.8 Existing tests

`src/d3d9/vr/tests/`: 58 test files, ~650 `TEST_CASE`s, host-compiled, no Wine/GPU
(`meson.build` `vr_tests`). They cover math, policy and parsers (game space, timers, input mapping,
menu pointer, HUD layout, scopes, grab, reload, sneak, LAA patch identity/bytes, engine patch table
consistency, emulator). Nothing tests the real executable, the hooks, or the interplay with other
plugins.

---

## 2. Inventory of game-process interventions

### 2.1 Patch mechanisms (`vr_game_memory.cpp`)

All writes to game code go through these helpers; every one compares the expected bytes first and
leaves the site untouched on mismatch (other executable, or another mod already there):

| Helper | Writes | Notes |
|---|---|---|
| `redirectCall(site, expected, new)` | rel32 of a 5-byte `E8` call | fails if the call does not reach `expected` (so a site already hooked by another plugin disables the feature) |
| `chainCall` / `chainCalls` | same, but accepts a call that reaches foreign code (another module or executable memory) and calls that "previous" target | used where JohnnyGuitar, JIP LN, kNVSE, xNVSE, Tweaks are known to hook (`vr_game_memory.cpp:185-235`); logs the holder |
| `redirectVirtual` / `chainVirtual` | one vtable entry (4 bytes, `VirtualProtect` RW) | `:297` |
| `replaceCode(site, expected, replacement, n)` | n bytes of code | `VirtualProtect` RWX, `FlushInstructionCache` (`:253-259`) |
| `redirectFunction` | 5-byte `E9` jmp at a function entry | detour without trampoline (callee replaced entirely) |
| `VrGameHeap::redirectEntry` | 8-byte atomic `InterlockedCompareExchange64` of a jmp + trampolines in `VirtualAlloc` memory | `vr_game_heap.cpp:114-167` |
| IAT write | import table entry of another module | `vr_sound_focus.cpp:43-73` |

No external detour library (MinHook, Detours, SafetyHook) is used. **Nothing is ever unpatched**:
hooks stay and fall through to the game when the owning object is gone. Hooks of `redirectCall` type
call the game function by its constant address, not the value found at the site.

### 2.2 Code patches in `FalloutNV.exe`

Abbreviations: CS = call-site redirect (`E8` rel32), CH = chained call-site redirect, VT = vtable
entry, BP = byte patch, JMP = function entry jump. "Init" = `VrSystem::initialize` on the first
`Direct3DCreate9` (`vr_system.cpp:122-309`); "Iface" = `onInterfaceCreated` right after it
(`:312-335`); "Device" = `VrStereoRenderer::install` from `CreateDevice` (`vr_stereo_renderer.cpp:205-480`).
"Off" names what disables it; "backend" means only `d3d9.vrBackend = off` (or a failed check).

#### 2.2.1 Stereo rendering core (Device, all-or-nothing, `vr_stereo_renderer.cpp:222-245`)

| Address | Kind | Target replaced / hooked | What it does | Off |
|---|---|---|---|---|
| `0x0086EDE8` | CS | `Main::Swap 0x0086FF70` | per-frame VR driver; Swap called once per eye | backend |
| `0x007144D3` | CH | `InterfaceManager::RenderInterface 0x007134D0` | eye capture before HUD; HUD/menu layers into panel textures (JohnnyGuitar holds it) | backend |
| `0x007136B7` | CS | `InterfaceCull 0x00B6BEE0` | isolates HUD groups per pass | backend |
| `0x00713F97` | CS | `AddAccumTask 0x00BA3390` | interface accumulation per layer | backend |
| `0x00870F33`, `0x00870927`, `0x00870AF8` | CS | `PlaceCamera 0x00874C10` | re-applies eye pose after the game places the camera | backend |
| `0x0109CC54` | VT | `NiCamera::UpdateWorldData 0x00A711B0` | re-applies eye pose after world-transform update (all NiCameras pass through, only the render camera is changed) | backend |

#### 2.2.2 Rendering, camera and scene (Device unless noted)

| Address | Kind | Hooked | Purpose (file:line) | Off |
|---|---|---|---|---|
| `0x0086EBF5` | CS | `OcclusionPlaneVisibility 0x00B5AC90` | culling against head frustum (`vr_culling_camera.cpp:37`) | backend |
| `0x0086EC18` | CS | `MultiBoundVisibility 0x0045B070` | same (`:39`) | backend |
| `0x00870C1E`, `0x00870A56` | CS | `DrawWorld_Init 0x00872B00` | sky/weather root centred on each eye (`vr_sky_anchor.cpp:75`) | backend |
| `0x00BB26B0` | CS | `SpeedTreeLeafShader::UpdateScaleAdjustment 0x00BB1A90` | leaf card axes per eye, render thread (`vr_tree_leaves.cpp:85`) | backend |
| `0x00870C29` | CS | `DrawWorld_UpdateWater 0x008727D0` | pass timing/sharing (`vr_render_passes.cpp:44`) | backend |
| `0x00870C3C` | CS | `DrawWorld_Shadows 0x00871290` | shadow map once for both eyes (same) | `vrShareShadows` (behaviour) |
| `0x00870E18` | CS | `DrawWorld_DrawScene 0x00873200` | pass timing / foveation | backend |
| `0x00870F74` | CS | `DrawWorld_FirstPerson 0x00875110` | first-person pass per eye | backend |
| `0x008710E4` | CS | `DrawWorld_ImageSpace 0x00875FD0` | image space per eye | backend |
| `0x008751C6` | CH | `Renderer::ClearBuffer 0x007148C0` | hands depth-tested against world (JIP LN `ToggleDepthClear` holds it) (`vr_render_passes.cpp:122`) | `vrHandsDepthTest` |
| `0x00876136` | CS | `RenderImageSpaceEffects 0x00B55AC0` | V.A.T.S.-style glow on weapon parts (`vr_part_highlight.cpp:149`) | needs manual reload + weapon in hand |
| `0x00870244` | CH | `Render 0x008706B0` (world branch) | live world behind the Pip-Boy (JohnnyGuitar holds it) (`vr_pipboy_render.cpp:108`) | backend |
| `0x007FD6F6` | CS | `PipBoyBackground 0x00871DC0` | skips frozen background capture (`vr_pipboy_render.cpp:85-106`) | backend |
| `0x0087086B` | CS | `InitRenderTexture 0x00872F50` | rendered-menu target | backend |
| `0x00870887` | CS | `CopyMenuBackground 0x00874B90` | replaces frozen background by live world | backend |
| `0x008743B0` | CS | `SettingInt 0x0043D4D0` (render threads setting read) | thread count in the rendered-menu path | backend |
| `0x008709AA` | CS | `RenderPipBoy 0x008761E0` | Pip-Boy screen pass in first-person space | backend |
| `0x00870967` | CS | `DrawRenderedMenuParent 0x00875BF0` | rendered menus (Reflectron etc.) drawn with menu scenes (`vr_menu_scenes.cpp:100`) | backend |
| `0x00733530`, `0x007C19F0`, `0x007BBD20`, `0x0079491C`, `0x00740F60` | CS | `Renderer::ClearBuffer 0x007148C0` | keep depth in Blackjack, slots, roulette, vigor tester, Caravan scenes (`vr_minigames.cpp:531-535`) | `vrImmersiveMinigames` |
| `0x008719F8` | CS | `UpdateOffscreenInterface 0x00871A50` | console terminal screen (`vr_console_screen.cpp:217`) | backend (console terminal) |
| `0x00A01B87` | CS (temporary) | `Tile::ParseFile 0x00A01E20` | redirected to the game's parser while `fnvvr_console.xml` is read, then restored (`vr_console_screen.cpp:473-480`) | runtime, transient |
| `0x00700AB2`, `0x00700ACC` | BP | fader half width/height code | fader quads get no extent; faders drawn over each eye (`vr_fader_size.cpp:28-29`) | backend |
| `0x00B99598` | CH | `RenderSkinnedPass 0x00B99110` | sleeve/forearm cut (`vr_sleeve_cut.cpp:101`) | weapon in hand |
| `0x00B992E3`, `0x00B99339` | CH (both) | `DrawPartition 0x00E6D310` | same (`vr_sleeve_cut.cpp:99`) | weapon in hand |

#### 2.2.3 Interface shape and resolution (Iface, `vr_interface_shape.cpp:166-215`, only with `vrHeadsetResolution` + `vrEyeAspect`, all-or-nothing)

| Address | Kind | What |
|---|---|---|
| `0x00715D40` | JMP | `Interface width` function replaced (`:197`) |
| `0x00715DA0` | JMP | `Interface height` replaced (`:199`) |
| `0x00706E90` | JMP | `Interface scaled height` replaced (`:201`) |
| `0x00712FBB` | CS | `RendererScreenWidth 0x004DEE10` in the interface camera (`:203`) |
| `0x007FB9B0`, `0x007F88EE` | BP | `fmul [aspect]` operand points into FNVVR memory: pick aspect of rendered menus / Pip-Boy (`:205-206`) |
| `0x008790ED`, `0x0084D58B`, `0x0084D6D9`, `0x0077EF9A`, `0x0078EA1D`, `0x00794743`, `0x007C22A6`, `0x007C9B13`, `0x007FD200`, `0x007FF342`, `0x0075F51D` | CS | `GameScreenWidth 0x004DC1F0` -> 4:3 layout width (`:144-146`, `vr_game_addresses.h:1925`) |
| `0x0057D669`, `0x0058D3F5` | CS | `RendererScreenWidth 0x004DEE10` -> camera width (`:147-149`) |
| `0x0078965F` | CS | `Interface::GetRealScreenWidth 0x00706E40` in LoadingMenu (`:151`) |
| `0x00B6CD75`, `0x00B6CF0C` | BP (12 bytes) | virtual texture width call replaced by `push 0; mov ecx,edi; call <FNVVR>` + nops (`:158`) |

#### 2.2.4 Input (Init unless noted)

| Address | Kind | Hooked | Purpose | Off |
|---|---|---|---|---|
| `0x00A2304C` | CS | `XInputGetState` thunk `0x009F996E` (game's pad poll) | reports VR controllers as pad 0, hides real pads, or virtual keyboard pad (emulator) (`vr_gamepad_hook.cpp:204`) | `vrHideGamepad`+`vrControllers` (openvr), `vrVirtualGamepad`+`vrGamepadHead` (emulator) |
| `0x0070C525`, `0x00709FFB`, `0x007D4399` | CS | same thunk | interface's own pad checks (`:212-223`) | hide/controllers |
| `0x0070C5AA`, `0x0070C5E2` | CS | `ShowMessageBox 0x00703E80` | suppress "controller lost/connected" boxes (`:237`) | controllers |
| `0x007195D1`, `0x007195EB` | CH (both) | `StringCompare 0x00408B20` | null-safe tile name compare (Tweaks hooks the same) (`:247`) | controllers |
| `0x0071966F`, `0x007195FC`, `0x007039F3`, `0x007D090E`, `0x007E8A05`, `0x007DF6CD` | CS | `Has360Controller 0x004B71D0` | gamepad button labels while lasers act as mouse (`:261`) | controllers |
| `0x0086F39E` | CH | `OSInputGlobals::Poll 0x00A23010` | after the poll: mouse dx/dy zeroed, button and wheel from the laser while the pointer drives the cursor (`vr_menu_cursor.cpp:36-48`, `:91`) | `vrControllers` |
| `0x0070CD31` | CS | `UpdateCursor 0x007118D0` | cursor node placed at the laser point (`:93`) | `vrControllers` |
| `0x0070CE9E` | CH | `MouseAxis 0x00A239E0` (wheel) | laser wheel (`:100`) | `vrControllers` |
| `0x0070E02F` | CS | `ConsoleReadKey 0x007154B0` (interface keyboard read) | injects one key per frame for the console and the name text field (`vr_game_console.cpp:53-76`, `:96`) | Device; backend |
| `0x0093F8D9` | CS | `HandleLook 0x009445B0` (player update) | head yaw/pitch become the player's heading/look (`vr_head_look.cpp:78`) | Device; backend |
| `0x00762F85` | CS | `FocusOnActor 0x00953060` (dialogue turn) | dialogue does not turn the player (`vr_head_look.cpp:86`) | Device |

#### 2.2.5 Player, combat, weapons, physics (Device)

| Address | Kind | Hooked | Purpose | Off |
|---|---|---|---|---|
| `0x005245BD` | CH | `Projectile::Create 0x009BCA60` in `TESObjectWEAP::Fire` | shots of the player start at the hand's muzzle along the hand's aim; hand throws (`vr_weapon_hand.cpp:657`, body `:813-863`, only when `shooter == player`) | `vrWeaponInHand` |
| `0x009BEBD9` | CS | `Projectile::GetSpeed 0x009669C0` | thrown speed (`:663`) | same |
| `0x0108F600`, `0x0108F604` | VT | Flame `Process3D 0x009B3320`, `Update 0x009B34C0` | flames from the hand (`:668-670`) | same |
| `0x0108C6D0`, `0x0108C6D4` | VT | Beam `Process3D 0x00979140`, `Update 0x00979330` | beams from the hand (`:675-677`) | same |
| `0x009BD9E2` | CS | `ApplyAutoAim 0x00965620` | auto-aim for hand-aimed shots (`:682`) | same |
| `0x00494989`, `0x004956FF`, `0x00495E2A`, `0x004973B4` | CH (all) | `PlaySequence 0x004949A0` | aim-sight anim sequences (kNVSE hooks the four together) (`:688`) | same |
| `0x00874F55` | CS | `NiAVObject::Update 0x00A59C60` (first-person update) | places weapon/arms at the controllers (`:646`) | same |
| `0x008762C6` | CS | same (Pip-Boy model update) | Pip-Boy arm (`:651`) | same |
| `0x0094380E` | CS | `HandlePostAnimationActions 0x008BA600` | throw from the hand (`vr_hand_throw.cpp:133`) | weapon in hand |
| `0x009B562D` | CS | `SetTargetedLimb 0x009B6620` | limb hit by a melee swing (`vr_melee_swing.cpp:198`) | weapon in hand |
| `0x008BAEFE` | CS | `MeleeAttack 0x00899200` | melee done by the hand (`:209`) | weapon in hand |
| `0x010879AC` | VT | HighProcess `GetCurrentWeapon 0x008D81E0` | bare-hand strikes see no weapon (`vr_bare_hand.cpp:64`) | weapon in hand |
| `0x0087C105` | CH | `Actor::CreateBlood 0x0088E8D0` (queued) | same (`:68`) | weapon in hand |
| `0x0087C138` | CS | `CreateBloodOnWeapon 0x0088FB00` | same (`:70`) | weapon in hand |
| `0x008AE7E2` | CS | `Actor::GetEyeVector 0x008AFF80` in `ObjectHit` | melee hits world objects (`vr_melee_object_hit.cpp:106`) | weapon in hand + grab |
| `0x008AEBCD` | CS | `Actor::GetWeaponPosition 0x008A5340` | same (`:108`) | same |
| `0x0108AE28` | VT (chain) | PlayerCharacter `ReloadWeapon 0x0095D3F0` | manual reload (xNVSE hooks it) (`vr_manual_reload.cpp:215`) | installed always; behaviour `vrManualReload` |
| `0x00894040` | CS | `Actor::GetAnimGroup 0x00897910` (jam) | (`:219`) | same |
| `0x00893B88`, `0x00893C24` | CS | `InventoryEntry::GetCount 0x00726070` in StartAttack | clip count (`:224-226`) | same |
| `0x008B0FBF` | CH (late) | `Actor::GetIronSights 0x008BBC10` in gun skill | steadied aim (re-chained whenever the left hand grips; JIP LN rewrites it at deferred init) (`vr_scope_game.cpp:61`, `:109-121`) | Device |
| `0x00709C48`, `0x00771C36`, `0x00771C57` | CS | `SetGunScopeVisible 0x0077F3C0` | game's flat scope off (`vr_scope_game.cpp:75-79`) | backend |
| `0x007718C6` | CS | `NiNode::GetAt 0x0045BC00` (scope model child) | (`:81`) | backend |
| `0x008BB923` | CS | `NiAVObject::SetAppCulled 0x00450F90` (iron sights cull) | (`:83`) | backend |
| `0x00965056` | CS | `ImageSpaceIronSights 0x009650A0` | scope blur off (`:85`) | backend |
| `0x0093F098` | CS | `WeaponHasNightScope 0x009442C0` | night vision off (`:87`) | backend |
| `0x009634A5`, `0x009634C9` | CS | `PlayerModLooking 0x00931E50`, `PlayerModHeading 0x00931D30` | scope wobble (`:90-92`) | backend |
| `0x008B2BA2`, `0x008BBB2E` | CS | `WeaponUseFirstPersonAimAnims 0x008B2BF0` | (`:94-96`) | backend |
| `0x008BBB4A` | CS | `WeaponHasModScope 0x004AD030` | (`:98`) | backend |
| `0x0070F0A4` | CS | `AnimGetGroupAction 0x0070F490` (Pip-Boy raise wait) | skip arm animation (`vr_pipboy_animation.cpp:53`) | backend |
| `0x0070F3DA` | CS | `IsPipBoyActive 0x00967AE0` (lower wait) | same (`:55`) | backend |
| `0x0070E913` | CH | `OpenPipBoy 0x0070F4E0` | blocks the release that would reopen it (`vr_pipboy_control.cpp:71`) | backend |
| `0x0089606E` | CS | `AnimGetGroupAction 0x0070F490` (equip wait) | no draw/holster animation (`vr_weapon_drawn.cpp:62`) | backend |
| `0x00940C05` | CS | `SetWantWeaponDrawn 0x008A6840` (holster input) | hold-to-holster disabled (`:64`) | backend |
| `0x008A6339` | CS | `MobileObject::Move 0x0092F260` in `Actor::Move` | room-scale move, **player only** (`vr_roomscale.cpp:56`, `:145-155`) | backend (behaviour `vrRoomscale`) |
| `0x0094363C` | CS | `HandlePhysicsGrab 0x0095F6C0` | hand grab instead of game grab (`vr_physics_grab.cpp:412`) | backend |
| `0x00C7D7D7` | CS | `Ragdoll::UpdateDebugLines 0x00C7CB50` (feedback pose site) | ragdoll feedback for held actors (`:431`) | backend |
| `0x00C7D6F7` | CS | same (drive stage) | (`:437`) | backend |
| `0x0093F954` | CS | `ForceGrenadeHold 0x009481D0` (not-in-control) | hand bodies (`:444`) | backend |
| `0x0108AC64` | VT | PlayerCharacter `SetParentCell 0x0094CAE0` | grab state across cells (`:449`) | backend |
| `0x01086CBC`, `0x010872FC` | VT | **Character** and **Creature** `Move 0x008A62B0` | drag of held actors; passes through otherwise (`:456-458`) | backend |
| `0x0109225C` | VT | `ActorMover::GetMoveMode 0x009DDB70` | (`:465`) | backend |
| `0x010C3C10` | VT | `CollidableFilterEnabled 0x00C84880` | held objects pass through the player (`:489`) | backend |
| `0x0108AC8C` | VT | PlayerCharacter `Move 0x008A62B0` | player move while holding (`:492`) | backend |
| `0x0108ABD0` | VT | PlayerCharacter `GetLookingAtLocation 0x00952FF0` | NPCs look at the headset (`vr_npc_gaze.cpp:32`) | backend |
| `0x0070C130` | CS | `FindActivateRef 0x00631D60` | activation target from the hand ray (`vr_hand_activate_pick.cpp:46`) | backend |

#### 2.2.6 Menus, UI, real-time mini-games (Init)

| Address | Kind | Hooked | Purpose | Off |
|---|---|---|---|---|
| `0x007CCD02` | CS | `OptionArrayAppend 0x007CB2E0` | adds "VR" (and "Console") to Settings (`vr_options_menu.cpp:490`) | backend |
| `0x007CBBFD` | CS | `ListBox::Initialize 0x0075A0D0` | (`:496`) | backend |
| `0x007CBF27` | CS | `OptionIsForModes 0x007CC5D0` | (`:500`) | backend |
| `0x010337A4` | VT | `TESDescription::GetText 0x00482E90` (message descriptions) | tutorial texts (`vr_tutorials.cpp:42`) | `vrTutorials`, Iface |
| `0x00707118` | CS | `ControlPushText 0x007039B0` | control names in tutorials (`:49`) | `vrTutorials`, Iface |
| `0x007CFA8C` | CS | `MainMenuIdle 0x007D5830` | 4 GB / NVSE offer message box (`vr_large_address.cpp:415`); only if the offer applies | conditional, declinable |
| `0x0086E79E` | CS | `IsInMenuMode 0x00702360` (OnIdle second read) | world keeps running in casino/vigor tester/Caravan (`vr_live_casino.cpp:325`) | `vrRealTime*` (all three off: no patch) |
| `0x0063E0F8`, `0x00837D05`, `0x008387C5`, `0x0086A4A1`, `0x0086A4DE`, `0x0086EEAF`, `0x008898BD` | CS | `IsInMenuMode 0x00702360` | other "world stands still" tests (`:329`, `vr_game_addresses.h:1503`) | same |
| `0x0086EC78` | CS | `SetMainRendering 0x008C80E0` | interface idle on main thread (`:331`) | same |
| `0x008C7BE9`, `0x008C7DB9` | CS | `InterfaceIdle 0x0086FD70` on AI threads | (`:335`) | same |

#### 2.2.7 Performance and system patches (Init/Iface)

| Address | Kind | What | Off |
|---|---|---|---|
| `0x00EC6CDE` | BP 7 bytes | CRT `fabs` -> `fld; fabs; ret` (`vr_fast_math.cpp:52`) | `vrFastMath` |
| `0x00416870`, `0x00439E90`, `0x00439EF0`, `0x004A3760`, `0x004B4500`, `0x0043F8D0`, `0x0062C250`, `0x00B9BBB0`, `0x00EC6040`, `0x00EC6054`, `0x004B6190`, `0x004B6160`, `0x004B6100`, `0x00A7F3F0` | JMP | NiPoint3 ctor/add/sub/scale, NiMatrix3*NiPoint3, NiMatrix3*NiMatrix3, NiTransform*NiTransform, shadow light bound test, `__CIsqrt`, `sqrt`, dot, plane distance, bound side, bound merge (`vr_fast_math.cpp:70-94`) | `vrFastMath` |
| `0x008D8520`, `0x0045CD60`, `0x00559450` | BP | getters rewritten as `mov eax,[ecx+off]; ret` (`:111-130`) | `vrFastMath` |
| 748 units / 806 ranges, `0x00401020`-`0x00EEA64A`, 13,636 bytes | BP | lStewieAl's Tweaks inline groups + "Accessors": Misc 172, Accessors 342, Rendering 131, Pathing 37, Process 29, SaveLoad 13, UI 12, Audio 7, DynamicCasts 4, Scripts 1 units (`vr_engine_patch_table.h`, applied by `vr_engine_patches.cpp:69-119`). 583 ranges rewrite function entries; **109 ranges replace `E8` call instructions inside functions with inline code** (list in Appendix A). Each unit applied only if all its ranges hold the original bytes. | `vrEnginePatches`, `vrEnginePatchGroups` |
| `0x00AA3E40`, `0x00AA4060`, `0x00AA44C0` | JMP (8-byte atomic) + trampolines | `MemoryManager::Allocate`, `Deallocate`, `Size`: blocks <= 4 KiB from FNVVR's `VrBlockHeap`, rest through trampolines to the game's code (`vr_game_heap.cpp:182-228`); checked again once a minute (`:249-257`) | `vrGameHeap` |
| `0x00AEB943`, `0x00AEC4B0`, `0x00AF08C6`, `0x00AF1643` | BP (imm32) | `DSBCAPS_GLOBALFOCUS` OR-ed into DirectSound buffer flags: sound keeps playing without focus (`vr_sound_focus.cpp:78-97`) | always with a backend |
| `0x00AEA4DC` | BP 4 nops | audio shutdown clear, so `bMultiThreadAudio=1` can exit (`vr_game_settings.cpp:87-94`) | `vrForceGameSettings` |
| `0x0086F296` | CS | `BSTimer::Update`: replaced by precise step (real interval as fixed step; game step kept while loading), or only measured when another module (NVTF) owns it (`vr_game_timer.cpp:104-153`) | always installed; precise step `vrPreciseGameTimer` |

### 2.3 Data writes into game memory (globals and settings)

| Address | Written | When | File:line | Off |
|---|---|---|---|---|
| `0x011C73DC`+4, `0x011C718C`+4 (`iSize W/H:Display`) | headset render size, restored after device creation | Iface / `attachDevice` | `vr_game_resolution.cpp:32-52`, `vr_system.cpp:338-363`, `:417` | `vrHeadsetResolution` |
| `0x011C77B4`+4 (`bFull Screen:Display`) | 0 for the session (forced windowed) | Iface | `vr_game_resolution.cpp:40` | same |
| `0x011C7C00` `bForceHighDetailReflections:Water`=0, `0x011F6DFC` `bMultiThreadAudio`=1, `0x011F6E5C` `bUseAudioDebugInformation`=0, `0x011F6EC8` `iAudioCacheSize`=16384, `0x011F6ED4` `iMaxSizeForCachedSound`=2048, `0x011C3C10` `bSelectivePurgeUnusedOnFastTravel`=1, `0x011E05EC` `bBackgroundLoadLipFiles`=1, `0x011C3D40` `bPreemptivelyUnloadCells`=1, `0x011C7308` `b30GrassVS`=1, `0x011C76C0` `bDoHighDynamicRange`=1, `0x011C7768` `bUseBlurShader`=0, `0x011DB2BC` `fLightEffectFadeDuration:Pipboy`=400, `0x011C74D4` `bDrawShadows`=0, `0x011C72D8` / `0x011C72E4` `iActorShadowCountInt/Ext`=0 | in-memory setting values (name checked, logged old -> new), not written to INI | Iface | `vr_game_settings.cpp:29-45`, `:107-121` | `vrForceGameSettings` (headset only) |
| `0x011DEED8`+4 `bAlwaysActive:General` | 1 | Iface | `vr_game_settings.cpp:97-104` | `vrHideGameWindow` |
| `0x011E09E0`+4 `bDisableAutoVanityMode:General` | 1 | Device | `vr_comfort_settings.cpp:41` | backend |
| `0x011DEA28` (Main's `bStaticMenuBackground` copy) | 0, **every frame** | per frame | `vr_stereo_renderer.cpp:998-1015` | backend |
| `0x01267B38` (`fMaxTime:HAVOK` value) | per-frame Havok max step; restored on destruction | per frame | `vr_game_timer.cpp:216-232`, `:100` | `vrHavokStepPerFrame` |
| AppTimer `0x011F6394` +0x04 fixed step, +0x14 last tick | set around the update, cleared again | per frame | `vr_game_timer.cpp:162-205` | `vrPreciseGameTimer` |
| `0x011C632C` frustum cache camera | `nullptr` before each camera move | per eye | `vr_game_camera.cpp:102` | backend |
| `0x011DEA2F` open-console request | 1 when the player opens the console from the VR page | on request | `vr_game_console.cpp:116` | backend |
| `0x011DEC64` interface FX texture pointer | swapped during the console screen draw and mini-game draws, restored | transient | `vr_console_screen.cpp:401`, `vr_minigames.cpp:3457` | backend |
| OSInputGlobals mouse fields | dx/dy=0, wheel, left button | per poll while the laser drives the cursor | `vr_menu_cursor.cpp:43-48` | `vrControllers` |

Object-level writes (through game functions or fields of game objects, all on the main thread):

- **Player (`PlayerCharacter*` at `0x011DEA3C`)**: heading via vtable `SetHeading` and look angle via
  `SetLooking`, every frame while the head controls the look (`vr_head_look.cpp:220-251`; not in
  V.A.T.S. camera, controls disabled, dead, furniture/sitting: `:199-217`); room-scale move vector
  (`vr_roomscale.cpp`); sneak via the game's sneak control (`vr_sneak.*`); weapon kept drawn via
  `SetWantWeaponDrawn` (`vr_weapon_drawn.*`); `SetFirstPerson` whenever the game goes to third
  person in play, kill cam mode via `SetKillCamModeCallback` (`vr_comfort_settings.*`); action
  points (`PlayerDamageActionPoints`) for power attacks; quick wheel uses/equips via the game's
  hotkey wheel functions (`vr_game_hotkeys.cpp`), ammo swap (`vr_game_ammo.cpp`); developer mode:
  `SetGodMode`, adds items/ammo after each loading screen (`vr_developer_mode.*`, off by default).
- **Other actors**: hand grab moves/drags actors, puts them in ragdoll feedback, knocks down, sets
  walk/run speed, activates references, `NotifyGuards`, `RemoveExtra` (`vr_physics_grab.cpp`); NPC
  reactions say comment lines or start combat (`ActorAttackAlarm`) (`vr_npc_reaction.cpp`); Caravan
  challenger `SetRestrained` + special idle (`vr_live_casino.cpp`); challenger's arm bent per frame
  and restored (`vr_minigames.cpp`). Melee/bare-hand hits call `CombatHit`/`ObjectHit`.
- **Havok world**: sphere rigid bodies for both hands and springs are added to the player's cell
  world (`vr_hand_body.cpp`, `vr_physics_grab.cpp`).
- **Scene graph**: the render camera's transform and frustum per eye (`vr_game_camera.cpp`,
  `applyEyePose`); sky and weather roots moved per eye (`vr_sky_anchor.cpp`); first-person
  skeleton/weapon nodes; the body's head shrunk for a frame (`vr_body_view.cpp`); app-culled flags of
  HUD tile groups toggled per interface pass (`vr_hud_layers.cpp`, groups `vr_game_addresses.h:1781`);
  extra NIF models (terminal keyboard, scene models) added to/removed from the ShadowSceneNode
  (`vr_scene_model.cpp`, `vr_terminal_keyboard_model.cpp`).
- **Interface tiles**: values set with `TileSetFloat`/`TileSetString` (HUD meters, prompt icon,
  options page, mini-games, console), extra XML `fnvvr_console.xml` parsed into the interface
  (`vr_console_screen.cpp`), message boxes (`ShowMessageBox`) for the options page and the 4 GB offer.

### 2.4 D3D9 / Windows / process-level interventions

- **D3D9**: FNVVR *is* the D3D9 runtime, so it intercepts nothing through vtables. Behavioural
  changes in its D3D9 code: see 1.1 (Present dropped for eye 0, not shown with hidden window;
  `Immediate` present interval when paced; RESZ advertised with `DepthResolve.dll`).
- **Window**: `ShowWindow(root, SW_HIDE)` on every present when visible (`vr_game_window.cpp:16-34`);
  stock DXVK subclasses the window procedure only for exclusive fullscreen
  (`src/d3d9/d3d9_swapchain.cpp:1260`, `src/d3d9/d3d9_window.cpp:127-140`), which FNVVR's forced
  windowed mode avoids; `ActivateFocusWindow` (`SetWindowPos`) only for fullscreen
  (`src/d3d9/d3d9_interface.cpp:426`). No cursor clipping or `SetForegroundWindow`.
- **Focus**: game kept active without focus (`bAlwaysActive=1`), sounds with `DSBCAPS_GLOBALFOCUS`.
  **IAT hook in another plugin**: `itr-nvse.dll`'s import of `user32!GetForegroundWindow` is replaced
  by a function that returns the game's window (`Main+0x8` at `0x011DEA0C`), so its alt-tab mute
  does not silence the hidden window (`vr_sound_focus.cpp:100-112`; only with `vrHideGameWindow`).
- **Input APIs**: no DirectInput, raw input or IAT hook of XInput. The game's own XInput calls are
  redirected at call sites (2.2.4); other modules calling `XInputGetState` directly see the real
  pad. `GetAsyncKeyState` is polled (global, focus-independent) for the emulator keyboard
  (`vr_emulator_keyboard.cpp`) and the virtual keyboard pad (`vr_gamepad_hook.cpp:89-106`).
  Text entry uses the SteamVR overlay keyboard (`IVRBackend::showKeyboard`).
- **Timers / pacing**: no `QueryPerformanceCounter`/`Sleep` hooks. Game timer redirect (2.2.7);
  OpenVR `WaitGetPoses` blocks the game thread once per frame; `Sleep(11)` per frame when the window
  is hidden and nothing else paces (loading screens, `vr_game_window.cpp:37-39`,
  `vr_system.cpp:408-409`). `GetTickCount` IAT is only read (`vr_game_timer.cpp:67-70`).
- **Memory**: small-block heap replacement (2.2.7); `VirtualAlloc` for heap trampolines and heap
  regions (`MEM_TOP_DOWN` reservations, `vr_game_heap.cpp:40-44`, `:114`).
- **Exceptions**: vectored exception handler added *last* (`AddVectoredExceptionHandler(0, ...)`,
  `vr_crash_log.cpp:176`), logs and passes on; logs only the first few.
- **4 GB / NVSE loading patch (on disk)**: `VrLargeAddress` (`vr_large_address.cpp:345-423`) offers
  via a main-menu message box, when the process has 2 GB and `FalloutNV.exe` is an unmodified Steam
  exe (SHA-1 identified), or under Proton when `nvse_loader.exe` + `nvse_steam_loader.dll` exist, to
  rewrite `FalloutNV.exe` exactly as the FNV 4GB Patcher does (LAA flag, new entry point that sets
  `SteamAppId`/`SteamGameId`, SteamStub check disabled, `LoadLibraryA("nvse_steam_loader.dll")`;
  `vr_large_address_patch.cpp:40-90`); original kept as `FalloutNV_backup.exe`
  (`vr_large_address.cpp:242-250`). Not offered for an already patched exe or unknown versions.
- **Settings forced**: 2.3. Not touched: FOV setting (the camera frustum is replaced per eye
  instead), VSync INI (DXVK present interval as above), `iPresentInterval`.

---

## 3. Game-state assumptions

- **State detection** (`vr_game_state.cpp:32-53`): `IsInStartMenu 0x0070EDF0` -> MainMenu; non-null
  `LoadingMenu 0x011DA0C0` -> Loading; `MenuMode 0x011DEA2B` byte or `IsInMenuMode 0x00702360` ->
  PipBoy (`IsPipBoyShown 0x00705A00`) or Menu; else InGame. Read once per frame on the main thread.
- **Pausing**: the enum comment says "a menu other than the Pip-Boy pauses the game"
  (`vr_game_state.h`); behaviour depends on the state, not on time actually stopping. The world is
  rendered live behind every menu (static menu background disabled each frame, Pip-Boy over the
  live world). The real-time casino/vigor tester/Caravan feature makes the world run on purpose
  using the same technique as Real Time Menus (credited in the README, "no code copied").
- **Menus**: in-game menus are drawn without HUD into a menu panel in front of the head
  (`chooseLayer`, `vr_stereo_renderer.cpp:2433-2447`); the panel is placed when it appears. Main menu
  and loading screens: whole frame on a panel, no stereo (`showsPanel`, `:149`). Menus with 3D scenes
  (lockpicking, casino) get their scene drawn by FNVVR (`vr_menu_scenes.cpp`). Lasers drive the
  game's mouse cursor (mouse mode is forced by hiding the gamepad from the interface checks).
- **Pip-Boy**: opened by looking at the left forearm (gaze) or the game's control; arm animations
  skipped; the Pip-Boy model follows the left controller and can grow (`vrPipBoyScale`); closed with
  the menu back button. Assumes `IsPipBoyShown` and the Pip-Boy state machine of the vanilla exe.
- **Dialogue**: player not turned towards the speaker (`FocusOnActor` hook); the user turns the head.
  Dialogue menu shown on the menu panel; head-HUD messages shown separately.
- **Loading screens**: panel; game timer keeps the game's own step while loading
  (`keepsGameStep`, `vr_game_timer.cpp:240`); developer mode acts after each load.
- **V.A.T.S.**: the game keeps control of heading while `VatsCameraState 0x011F2258 != 0`
  (`vr_head_look.cpp:200`); a separate VATS camera adjustment exists (`adjustVatsCamera`, `:156`).
  VATS slow motion and cameras are not changed.
- **Cameras**: auto vanity off, kill cam forced to player mode, third person forced back to first
  person while playing (`vr_comfort_settings.h`); temporary third person (sitting, Sandman/Cannibal)
  rendered from the body's head (`vr_body_view.h`). Third person is effectively unsupported.
- **Head tracking injection**: (a) player heading/pitch from the head through game setters
  (`vr_head_look.cpp`); (b) eye pose written to the render camera after `PlaceCamera`/`UpdateWorldData`;
  (c) culling and sky/leaf adjustments per eye; (d) NPC look-at target = HMD position.
- **HUD**: drawn by the game's interface pass, split per element group with app-culled flags into
  a head panel (messages, objectives, subtitles) and hand panels (health/compass/AP/ammo on the
  back of the right hand, prompt at the fingertip) (`vr_stereo_renderer.h` class comment). The
  desktop window shows no HUD. Groups are identified by offsets into HUDMainMenu
  (`vr_game_addresses.h:1781-1785`); yUI-compatible layouts are handled in `vr_hud_layers.cpp`.
- **Input mapping**: SteamVR actions -> filtered (`vr_action_filter.*`) -> XINPUT gamepad state
  reported to the game for pad 0 (`vr_controller_pad.*`, `vr_gamepad_hook.cpp:130`); head pose and
  turn stick to `VrHeadLook`; lasers to the cursor; hands to grab/melee/throw/reload. Real gamepads
  are hidden by default.
- **Window**: hidden, game kept active, sound global; the desktop gets no frames.
- **Single local player**: everything is keyed to the one `PlayerCharacter` singleton
  (`VrGame::Player 0x011DEA3C`), e.g. `shooter == player()` in projectile creation, `mobile ==
  player()` in room-scale. Hooks on shared vtables (Character/Creature `Move`, ActorMover,
  HighProcess `GetCurrentWeapon`, collision filter) pass through for other actors unless FNVVR
  holds them (grab) or a bare-hand strike is in progress.

---

## 4. External interfaces

- **xNVSE / other plugins**: no API, no messaging, no console commands, no script functions. It
  detects plugins only by module presence/code: `DepthResolve.dll` (RESZ), `itr-nvse.dll` (IAT patch),
  foreign code at call sites (chain logic and log lines), `nvse_loader.exe` /
  `nvse_steam_loader.dll` files (NVSE offer). Explicit cooperation is coded for JohnnyGuitar
  (RenderInterface, world render), JIP LN (first-person depth clear, gun skill call), kNVSE
  (PlaySequence), xNVSE (reload vtable), lStewieAl's Tweaks (tile name compare), NVTF/Tick Fix (timer),
  Engine Optimizations (`matchesUnlessRewritten`), heap replacers (heap skipped), yUI (HUD layout).
  `VrGameMemory::isOtherModule` lets hooks tell callers from other mods apart by return address.
- **Save data**: none read or written. Options in `fnvvr\vr_options.ini`.
- **Console**: the game's own console can be opened from the VR page; keys are injected through
  the interface key read (`vr_game_console.cpp`).
- **Required mods**: none. The 4 GB patch is recommended (and offered); xNVSE is optional.
- **Network / Steam API**: none. The only Steam-related code is the on-disk 4 GB patch, which sets
  `SteamAppId`/`SteamGameId` env vars in the patched exe's entry point (same as the 4GB Patcher).
- **VR runtime**: OpenVR (SteamVR), scene application, SteamVR Input, overlays for panels, keyboard.

---

## 5. Robustness

- **Unknown executable**: every patch site is byte-checked; unknown or foreign bytes leave the site
  untouched and log a "was not found" / "left as found" line. The core stereo hooks are
  all-or-nothing (`vr_stereo_renderer.cpp:240-243`). Several features check all their sites first
  (`vr_interface_shape.cpp:182-191`, `vr_pipboy_render.cpp:95-104`, `vr_live_casino.cpp:304-317`,
  `vr_sound_focus.cpp:81-94`). There is no explicit version check (no exe hash) except for the
  on-disk LAA patch (SHA-1). Data addresses (globals, struct offsets) are mostly only checked for
  readability; their meaning is assumed (vanilla 1.4.0.525 layout, GOG noted as same code
  addresses).
- **Partial failure**: a hook that stays installed after a later failure is inert ("every hook
  forwards to the game's own function while no renderer is active", `:219-220`). Patches are never
  removed, also not on device loss/recreation; `attachDevice` creates the stereo renderer once.
- **Later overwrites by other mods**: only the heap entries are re-verified (once a minute). A plugin
  that rewrites one of FNVVR's call sites after FNVVR installed (e.g. at xNVSE deferred init) silently
  takes it over; `VrScopeGame` re-chains its one known case.
- **Crash handling**: vectored handler logs fatal exceptions with module/offset and stack return
  addresses (`vr_crash_log.cpp`), does not handle them.
- **Known issues** (upstream `gh issue list -R df4l/FNVVR --state all --limit 50`, 14 issues):
  open: #13 "TOTNW 2.0 issue" (controller tracking fails with that mod), #12 "virtual desktop"
  (OpenXR backend wanted). Closed: #14 Scopes, #11 no working scopes, #10 4 GB on Linux?, #9
  V.A.T.S. aiming issues, #8 crashes, offset objects (ragdoll armour pieces; crashes fixed, then
  out-of-memory without 4 GB patch), #7 manual reloading issues, #6 Steam Frame controllers, #5/#4
  feature requests, #3 melee weapons, #2 crash on start (fixed by 4GB Patcher), #1 JohnnyGuitar
  incompatibility (blank Pip-Boy; fixed in v0.13.0 by chaining the shared render call). The
  maintainer stated xNVSE plugins were "not supported for the moment" (pre-release) and planned a
  compatibility pass over the Viva New Vegas utility list.
- README "Status" still lists items as not done that the CHANGELOG reports as done (HUD on the hand,
  Pip-Boy, scopes); the CHANGELOG (v0.19.1, 2026-11-15) is the current reference.

---

## 6. Potential friction points with other NVSE plugins / multiplayer mods

Ordered by expected impact for a co-op mod (Real Time Menus, JIP LN, JohnnyGuitar, kNVSE, NVTF-like
timing and a network layer in the same process).

1. **Render path runs twice per logic frame.** Anything another plugin hooks inside `Main::Swap`
   (render events, `RenderInterface`, HUD/menu updates driven from the render, overlays, D3D9
   `BeginScene`/`EndScene`/`Present` hooks, ImGui-style UIs, screenshot/name-tag drawing) runs once
   per eye, and its eye-0 `Present` is dropped. Per-frame counters or network sends placed in render
   hooks would double. Game logic (OnIdle) still runs once.
2. **Call-site ownership conflicts.** FNVVR mostly uses `redirectCall`, which refuses sites another
   module already took (feature then off, logged with the module name) and is silently overridden
   by a plugin that writes the same site later. Sites likely shared with a co-op mod and its
   requirements: `0x0086EDE8` (Swap), `0x0086F296` (timer), `0x0086F39E` (input poll), `0x00A2304C`
   (XInput poll), `0x0093F8D9` (player look), `0x005245BD` (projectile create), `0x0094363C`
   (grab), `0x007144D3` / `0x00870244` (render, chained), the `IsInMenuMode` sites (2.2.6), the
   Pip-Boy render sites (2.2.2). Install order: xNVSE plugins load before FNVVR (FNVVR installs at
   `Direct3DCreate9`/`CreateDevice`); plugins hooking at deferred init or later come after FNVVR.
3. **Real Time Menus (co-op requirement) overlaps directly.** FNVVR's real-time casino patches the
   same kind of `IsInMenuMode` tests and the interface-idle sites RTM is built around; with RTM
   loaded first those checks fail and FNVVR's feature switches itself off (benign), but the two
   mechanisms for "world runs during menus" must not both be active. FNVVR's Pip-Boy live render
   (`0x007FD6F6`, `0x0087086B`, `0x00870887`, `0x008709AA`, `0x008743B0`) and the per-frame clearing
   of `bStaticMenuBackground` (`0x011DEA28`) touch the same territory as RTM's own menu rendering.
   FNVVR's state logic treats `MenuMode`/`IsInMenuMode` as "menu"; if RTM clears `MenuMode` while a
   menu is open, FNVVR may classify a menu as InGame (HUD layer instead of menu panel, head look and
   hand features active). Needs a test.
4. **Engine patches rewrite 13.6 kB of game code, including 109 call instructions.** A co-op hook
   installed after FNVVR at one of the Appendix A call sites (or at a byte offset inside one of the
   583 rewritten function starts, e.g. `Actor::GetCurrentWeapon 0x008A1710`, `ProcessLists::*`,
   `BGSSaveLoadManager::SaveGame 0x008503B0`, `MobileObject::SaveGameBGS 0x00932880`,
   `Script::ClearLocalVariableRefObjects 0x005AE2E0`) finds different bytes: a byte-checking hook
   refuses, a blind one corrupts code. Entry-point detours by a disassembling library are fine.
   `d3d9.vrEnginePatchGroups` can exclude groups (e.g. `Process`, `SaveLoad`, `Scripts`, `Pathing`)
   for testing.
5. **Heap replacement.** `MemoryManager::Allocate/Deallocate/Size` are redirected (8-byte jumps);
   blocks allocated by FNVVR's heap are unknown to the game's pools. A plugin that hooks these three
   functions, inspects pool internals, or frees game memory through another path would conflict;
   another heap replacer that loads first makes FNVVR skip its heap. Option `vrGameHeap`.
6. **Game timer and Havok step.** FNVVR replaces the frame timer update (precise real-time steps,
   no 10 ms sleep) and rewrites `fMaxTime:HAVOK` every frame. A co-op mod that syncs time, runs
   physics/lag compensation or itself replaces the timer (NVTF-style) would see different deltas;
   FNVVR backs off only if the timer call or the `GetTickCount` import is already foreign.
7. **Input is rewritten at the game's call sites.** Real gamepads are hidden from the game, VR
   controllers appear as pad 0, mouse movement is zeroed while a laser drives the cursor, keys are
   injected through the interface key read. A plugin that reads input through the game's
   `OSInputGlobals` (e.g. chat, push-to-talk, hotkeys, its own menus) sees FNVVR's state; one that
   reads `XInputGetState`/`GetAsyncKeyState` directly sees the real devices, which in VR the player
   does not use. Keyboard-only co-op features (chat, MCM, console) are hard to reach in a headset.
8. **Player heading/position written every frame.** Head look sets the local player's heading and
   pitch via game setters; room-scale adds the head's movement to the player's move; physical sneak
   toggles sneaking. A co-op mod that replicates the local player's transform will replicate these
   (fine), but one that writes the local player's position/rotation (teleport, correction) will be
   blended into FNVVR's "body turn" logic. Animation state of the local player (first-person
   skeleton driven by controllers, weapon always drawn, draw/holster and Pip-Boy animations
   skipped, temporary third-person views) differs from what remote peers would expect.
9. **Shots and hits originate from the hand.** Projectile origin/direction are changed inside
   `Projectile::Create` arguments for the player only; melee hits are created by FNVVR
   (`MeleeAttack`, `CombatHit`, `ObjectHit`) from hand motion; bare-hand strikes hide the weapon.
   A co-op mod capturing fire/attack events higher up (e.g. at `TESObjectWEAP::Fire` or attack
   animations) would replicate the head-aimed or animation-driven version, not the hand-aimed one.
10. **Grab, drag, ragdoll and NPC reactions affect other actors.** Remote players represented as
    actors can be grabbed, dragged, knocked down or provoked (`ActorAttackAlarm`); hand rigid bodies
    push Havok objects. These are local, unsynchronised authority actions in a networked world; vtable
    hooks on Character/Creature `Move` and `ActorMover::GetMoveMode` apply to all actors.
11. **Window hidden, game always active.** `SW_HIDE` every present, `bAlwaysActive=1`, global sound
    focus, and the `itr-nvse` IAT patch. Co-op notes (alt-tab, borderless window recommended,
    Discord overlay blocked) assume a visible desktop window; any co-op UI drawn to the desktop
    window (or a second instance's window) is invisible in VR. With the emulator backend the window
    stays visible.
12. **Forced settings.** In-memory overrides (shadows off, audio threading/cache, cell unloading,
    background loading, HDR, grass, `bAlwaysActive`, `bDisableAutoVanityMode`, windowed, resolution)
    are local; co-op checks that compare INI files would not see them, but behaviour (cell unloading,
    `bPreemptivelyUnloadCells`, `bSelectivePurgeUnusedOnFastTravel`) can matter for reference
    lifetime that a sync layer tracks.
13. **Single-player assumptions.** Everything keys on the one `PlayerCharacter` singleton and on
    "the" menu/Pip-Boy/VATS state of this process; comfort settings force first person, kill cam to
    player mode and suppress auto vanity; dialogue does not turn the player; developer mode adds
    items and god mode (must stay off in co-op).
14. **Two instances on one PC (co-op second-copy tests).** Only one process can be the SteamVR
    scene application; the emulator backend reads global keyboard state (`GetAsyncKeyState`), so two
    emulator instances react to the same keys; both would write `FalloutNV_d3d9.log` in the same
    game folder if they share it, and the hidden-window logic applies to both unless the backend is
    `off` for one.
15. **Late-binding plugins overriding FNVVR silently.** Only the heap is re-verified. If the co-op
    mod (or its requirements) patches FNVVR's sites after device creation, features stop without a
    log line; `VR: Modules loaded outside the Windows folder` and `VR: Game code at 0x... is already
    patched` lines in `FalloutNV_d3d9.log` are the first place to look.

---

## Appendix A: call instructions replaced by engine patches

Each address held an `E8` call in the original executable and holds inline code after
`VrEnginePatches::install` (when its unit applied). Source: `vr_engine_patch_table.h`, `Ranges` with
original bytes starting with `E8` inside a unit.

- Misc (33): `0x00403803, 0x004048FE, 0x0040FB24, 0x0040FFC8, 0x0040FFD4, 0x0040FFFA, 0x00410173, 0x00410434, 0x0044B947, 0x0044B9B2, 0x0044C6B7, 0x0044C722, 0x0044D5D0, 0x004939DF, 0x004939F9, 0x00493B50, 0x004943A1, 0x00494410, 0x005658AA, 0x0056B900, 0x0056BA92, 0x00588211, 0x00626B14, 0x00629BF6, 0x0062EF7C, 0x006531F8, 0x006FE13B, 0x006FE142, 0x006FE189, 0x006FE190, 0x009F413F, 0x00B66221, 0x00E68BA9`
- Pathing (37): `0x0047CF8C, 0x0047CFAC, 0x0069F085, 0x0069F850, 0x006C381B, 0x006C3839, 0x006C388F, 0x006C3A16, 0x006C3A22, 0x006C3A35, 0x006C3B86, 0x006CB130, 0x006CB543, 0x006EA008, 0x006EA01B, 0x006EA15D, 0x006EA1E9, 0x006EA208, 0x006EA220, 0x006EA2C7, 0x006EA2F0, 0x00850BA7, 0x00850BBE, 0x00850BD1, 0x00850C02, 0x00850D1C, 0x00850D2F, 0x00850D49, 0x00850E8A, 0x0089FC0B, 0x008B31A5, 0x008DF71C, 0x008DF893, 0x009A164C, 0x009A1760, 0x009DB890, 0x009DFB37`
- Process (24): `0x00455504, 0x00578618, 0x00678620, 0x006C4673, 0x006C4695, 0x008A6671, 0x008A66B6, 0x008C3C6B, 0x0092EDE2, 0x0092EE22, 0x0092EE53, 0x0092EFA6, 0x0092EFD7, 0x0096F6D3, 0x0096F8B6, 0x0096FA2C, 0x0096FDE8, 0x00970235, 0x00970454, 0x00976741, 0x009767AD, 0x009767F9, 0x00976CE8, 0x00976D06`
- DynamicCasts (6): `0x00513BF9, 0x0057F10C, 0x005F6AD2, 0x005F6B2C, 0x005F6B49, 0x00602B02`
- Rendering (6): `0x0049393A, 0x00493C32, 0x004968CF, 0x00496925, 0x005655F2, 0x00A5BE30`
- SaveLoad (1): `0x0093289C`; UI (1): `0x00771367`; Audio (1): `0x00595628`

The full unit list (function address, group, Tweaks source reference) is in
`vr_engine_patch_table.h:2002ff`; units of the groups SaveLoad, UI, Scripts, Audio, DynamicCasts
and Process are at lines 2212-2277.

## Appendix B: how this inventory was produced

- Every caller of `VrGameMemory::redirectCall/chainCall/chainCalls/redirectVirtual/chainVirtual/
  replaceCode/redirectFunction` (127 call expressions in 41 files) was listed and its address
  constants resolved against `vr_game_addresses.h`; array and indirect sites were resolved by hand
  from the installing function.
- Direct `VirtualProtect`/`VirtualAlloc` users: `vr_game_memory.cpp`, `vr_game_heap.cpp`,
  `vr_sound_focus.cpp` (IAT). No `WriteProcessMemory`, no detour library.
- Writes to fixed game globals were searched with `*reinterpret_cast<T*>(VrGame::...)` patterns;
  object-level writes are summarised per subsystem, not enumerated.
