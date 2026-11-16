# FNVVR and New Vegas True Co-op: conflicts and incompatibilities

Cross-check of the two code analyses in this folder:
[fnvvr-codebase.md](fnvvr-codebase.md) (FNVVR, this fork) and [nvcoop-codebase.md](nvcoop-codebase.md)
(New Vegas True Co-op 0.1.11, `NVCoop.dll`). Both were read from source only; nothing here was run
yet. Each item says how sure we are and how a test can confirm it. Addresses are virtual addresses of
`FalloutNV.exe` 1.4.0.525. The two mods stay separate ([ADR 0001](../adr/0001-two-separate-mods-that-cooperate.md)):
each fix belongs in the mod where the cause is.

Status labels: **confirmed in code** (both sides read), **likely** (follows from the code, needs a
run), **open** (cannot be decided from the code).

## Summary

- **No rendering clash.** The co-op mod has no D3D9, Present or overlay hooks and no DirectInput /
  XInput / raw input; everything it shows goes through the game's own UI tiles, which FNVVR already
  renders into its VR panels.
- **Few shared code addresses.** Of 424 FNVVR and 86 co-op addresses in the reports, 16 are equal.
  Only one group is a real conflict (the `IsInMenuMode` call sites, together with Real Time Menus);
  the others are calls to the same game function or reads/writes of the same setting.
- **The likely problems are behavioural:** the window (borderless resize, WndProc subclass vs. FNVVR's
  hidden window), input focus (push-to-talk, chat), world-anchored co-op markers drawn for the flat
  camera, time manipulation (FNVVR's timer vs. co-op's slowdown and real-time VATS), and what the
  other players see of a VR player (actor rotation, hand-fired shots).

## 1. Shared code sites

| Address(es) | FNVVR | True Co-op | Assessment |
|---|---|---|---|
| `0x0086E79E`, `0x0086EEAF`, `0x008898BD`, `0x0086A4A1`, `0x0086A4DE`, `0x0063E0F8`, `0x00837D05`, `0x008387C5` (calls to `IsInMenuMode 0x00702360`) | `redirectCall` for "world keeps running" in casino / vigor tester / Caravan (`d3d9.vrRealTime*`, default on) | G10: redirects 14 `InMenuMode` call sites (these 8 plus 6 more) for the live escape menu, chained on top of **Real Time Menus** (a co-op requirement), which redirects them first | **Confirmed in code.** xNVSE plugins (RTM, co-op) load before FNVVR installs (on `Direct3DCreate9`). FNVVR's `redirectCall` checks that the call still reaches `0x00702360`, finds it redirected and **skips its patch** (logs it). Result: FNVVR's real-time casino/Caravan feature is off at these sites, which is harmless because RTM + co-op already keep the world running. Recommendation: set `d3d9.vrRealTimeCasino/VigorTester/Caravan = False` in the co-op setup so the log stays clean and nothing depends on install order. Test: log line from `reportForeignCode` absent/present. |
| `0x00703E80` `ShowMessageBox` | called (and two call sites redirected to suppress controller messages) | called for co-op popups | Same function called by both; no conflict. |
| `0x007CB2E0` / `0x007CB886` option lists | appends "VR"/"Console" to the Settings list | appends "Host Co-op"/"Join Co-op" to the main menu (different call site `0x007CB886`) | Different menus; no conflict. Check in a run that both entries appear and FNVVR's menu panel can click the co-op buttons. |
| `0x00A59C60` `NiAVObject::Update` | first-person node update (arms/weapon at the controllers) | updates its injected HUD tiles | Calls only; no conflict. |
| `0x011C73DC`/`0x011C718C` `iSize W/H:Display` | sets the headset render size (restored after device creation) | reads the screen size to place markers | **Likely** small effect: co-op markers are placed in screen fractions; see 3.2. |
| `0x011DEED8` `bAlwaysActive:General` | sets 1 (game runs without focus, window hidden) | sets 1 while co-op runs and restores the value it read when co-op ends | **Likely** fine: co-op reads after FNVVR has set 1 and restores 1. Test: value stays 1 after leaving a co-op session. |
| `0x011F6394` game timer | `vrPreciseGameTimer`: rewrites the fixed step / last tick fields every frame; also rewrites `fMaxTime:HAVOK` | sets the global time multiplier (0.0001 while a player loads; VATS slow-motion removed, G15) | **Open.** Different fields of the same timer; FNVVR derives frame time from its own clock, co-op scales time. Test: world freeze while a friend loads must still freeze in VR, and Havok must not explode on resume. |
| Actor mover vtable `0x0109223C` (co-op slots 4, 14) / `0x0109225C` (FNVVR slot 8 `GetMoveMode`) | VR movement backend | holds remote players' "puppet" legs | Different slots; **likely** fine. Note that FNVVR also hooks `Move` for all characters and creatures (`0x01086CBC`, `0x010872FC`): check that puppets (remote players) are not affected by FNVVR's player-only physics changes. |

Not cross-checked automatically: whether a co-op **prologue detour** (e.g. `0x00850760`, `0x0083C7B0`,
`0x00AD8E60`) lies inside one of FNVVR's 748 inline engine patches (lStewieAl's tweaks, appendix A of
the FNVVR report). That needs address ranges with sizes; TODO for the first test round. FNVVR can
switch those patch groups off (`d3d9.vrEnginePatchGroups`) to bisect a crash.

## 2. Install order and call-site ownership

- xNVSE plugins (co-op, RTM, JIP, JohnnyGuitar, kNVSE) load at process start. FNVVR installs on the
  game's first `Direct3DCreate9` (input, engine patches, heap, timer), right after it (resolution,
  settings) and on `CreateDevice` (renderer, player/combat/physics/Pip-Boy hooks).
- Co-op installs most game hooks in NVSE `PostLoad`, VATS/time hooks in `StartCoop` (when a session
  starts, later than FNVVR).
- FNVVR's `redirectCall` and byte patches **refuse** a site that is not in its expected state and
  disable that feature with a log line; nothing FNVVR does overwrites a co-op hook. A plugin that
  patches **after** FNVVR at the same site would overwrite FNVVR silently. For co-op's late hooks
  (G15/G16 VATS) no FNVVR site matches; keep it that way.
- Action: the test harness greps FNVVR's log for `reportForeignCode` / "already patched" lines and
  fails the run on new ones.

## 3. Behavioural conflicts

### 3.1 Window and focus (likely, high)
- Co-op forces windowed mode through an IAT hook on `GetPrivateProfileString/Int` (`bFull Screen=0`)
  and then resizes the window borderless to the monitor (`Borderless=1`, default) after the device
  exists. FNVVR forces its own windowed mode and resolution and **hides the game window**.
- Co-op subclasses "the first visible top-level window" of the process for chat/address input (P5),
  installed on first use, never removed. With FNVVR's window hidden it may pick another window or
  none: **chat and IP typing may not work**.
- Co-op's push-to-talk reads keys only while the game window is in the foreground; FNVVR's window is
  hidden and the game runs without focus: **push-to-talk likely dead** in VR.
- Recommendation for the co-op setup: `Borderless=0`. Fix candidates (co-op side): pick the game's
  real main window (by class/thread) for the subclass, and let push-to-talk work without foreground
  (or via an NVSE key query). Test: chat line opens and accepts text; push-to-talk transmits.

### 3.2 World-anchored co-op markers (confirmed in code, medium)
- Name plates, "DOWNED" and chat bubbles are positioned with JohnnyGuitar's `WorldToScreen` for the
  game's flat camera and drawn as HUD tiles. FNVVR re-projects the HUD onto a panel in front of the
  head and renders the world per eye: the markers end up on the HUD panel at flat-camera positions,
  **not over the friend in the 3D world**.
- Fix candidates: FNVVR could offer world-anchored HUD elements (the hook point would be the co-op
  tiles), or co-op could place markers through a VR-aware projection if one is exposed. Needs a
  design decision (ADR) because it touches the interface between the mods. Test: capture with a
  `fakefriend` standing in view; marker should sit over the friend.
- **Likely**: FNVVR culls only the game's own HUD groups and yUI's tiles per panel pass
  (`vr_hud_layers.cpp`); the injected co-op tiles are never culled and may show on the head panel and
  on every hand panel. Proposed handling: [ADR 0003](../adr/0003-coop-markers-in-vr.md).

### 3.3 Menus that do not pause (confirmed in code, medium)
- With RTM + co-op, menus, the Pip-Boy, VATS and the escape menu run in real time. FNVVR shows menus
  and the Pip-Boy on panels; its own "world keeps running" logic is the casino patch above. **Open**:
  whether FNVVR still detects these menus as menus (its menu detection reads the menu state, not the
  pause) and whether its Pip-Boy live render copes with a world that moves. Test: open Pip-Boy and
  escape menu in a co-op session, check panels, head tracking and that the world keeps moving.
- VATS: co-op removes the slow-motion and suppresses the VATS camera path (G15/G16) so playback
  stays in the player's view; FNVVR drives the camera from the headset. **Likely** compatible (no
  camera path to fight), test VATS once.

### 3.4 Time (open, medium)
- FNVVR replaces the frame timing (`vrPreciseGameTimer`, Havok max step) and renders the scene twice
  per logic frame (one `Main::Swap` per eye). Co-op freezes the world (time multiplier 0.0001) while
  a player loads, runs a freeze watchdog (>8 s hang: briefly suspends the main thread) and logs
  "Hitch:" lines. Risks: the watchdog firing during long VR loads/shader compilation; resume after
  the freeze with a large time step. Test: join while the host is in VR; check `NVCoop.log` for
  Hitch/Freeze lines and that physics stays calm.

### 3.5 What other players see of a VR player (confirmed in code, low-medium)
- Co-op sends the player's actor rotation (`rotZ`/`rotX`, 20 Hz), not the head pose: friends see
  the body direction, not where the VR player looks. Acceptable for a first version.
- FNVVR fires projectiles from the hand (`Projectile::Create 0x005245BD`, player only) and melee from
  the hand. If co-op replicates shots higher up (weapon fire / attack events), friends may see shots
  leave the head direction. **Open**: check co-op's replication point for player attacks. FNVVR's
  grab/push of other actors is not synced by co-op.
- Co-op teleports and pulls the player (doors, fast travel, "pull everyone", downed state) without a
  fade: comfort issue in VR; FNVVR could fade on large player position jumps.

### 3.6 Input (likely, medium)
- FNVVR hides real gamepads (XInput poll `0x00A2304C`) and presents the VR controllers as pad 0;
  mouse movement is zeroed and the laser drives the cursor. Co-op's MCM page, chat and IP entry need
  text input: reachable through FNVVR's injected keyboard/console path at best. Test: Host/Join from
  the main menu with VR controllers only; join by IP with typed address.

### 3.7 Memory (likely, medium)
- Both are 32-bit in one process: FNVVR (DXVK, heap replacement, 4GB patch offered) and co-op ("far
  cells": the host keeps extra cells loaded around far friends). Co-op logs free address space every
  30 s ("Frame cost"). Risk: out-of-memory crashes on the host in VR. Test: long host session with a
  `fakefriend` walking far away; watch the logged free address space.

### 3.8 Two game instances on one PC (confirmed in code, affects testing)
- Only one instance can own the SteamVR scene. FNVVR's emulator reads global keys
  (`GetAsyncKeyState`): two instances react to the same keys. Co-op's second-copy mode renames the
  game mutex and hides Fallout windows from `FindWindowA`.
- Recommendation for the test harness: **one VR instance plus co-op's `fakefriend`** (scripted
  remote player over ENet on localhost, `Mode`/`IpHosting` settings) instead of a second game
  instance. A second instance without FNVVR (flat) is the next step when needed.

### 3.9 Discord overlay, sound (low)
- Co-op patches `ntdll!LdrLoadDll` to refuse `discordhook.dll` (switch `BlockDiscordOverlay`); no
  FNVVR interaction expected.
- Co-op voice and text-to-speech play through `waveOut` on the Windows default device, not
  necessarily the headset. Setup note: make the headset the default output while playing.

## 4. Recommended settings for running both mods

| Where | Setting | Why |
|---|---|---|
| `NVCoop.ini` `[Coop]` | `Borderless=0` | avoid the borderless resize fighting FNVVR's window (3.1) |
| `dxvk.conf` | `d3d9.vrRealTimeCasino = False`, `d3d9.vrRealTimeVigorTester = False`, `d3d9.vrRealTimeCaravan = False` | RTM + co-op already keep menus live; avoids order-dependent patches (1) |
| `NVCoop.ini` | `iFieldOfView=0` (default) | keep co-op from writing the FOV; FNVVR owns the projection |
| Windows | headset as default audio output | co-op voice/TTS use the default device (3.9) |

## 5. Test plan derived from this (for ADR 0002's harness)

1. Single instance, FNVVR emulator, all co-op requirements, co-op loaded but no session: game reaches
   gameplay, no new "already patched" lines, co-op menu entries visible.
2. Same with `Mode=host` + `fakefriend join` on localhost: session starts after Doc Mitchell's house;
   captures with the friend in view (marker position, 3.2), Pip-Boy and escape menu open (3.3),
   VATS once (3.3), chat line (3.1), `NVCoop.log` free of Hitch/Freeze beyond loading (3.4).
3. Long run with the friend far away: address space (3.7).
4. Later: real OpenVR path (SteamVR null driver), then a headset session; a second flat instance only
   when `fakefriend` is not enough.

## 6. Open questions

- Where exactly co-op replicates the local player's attacks (3.5).
- Whether any co-op prologue detour lies inside an FNVVR inline engine patch (1, TODO).
- Whether FNVVR's menu detection still works with RTM's live menus (3.3).
- Which window co-op's subclass picks when FNVVR hides the game window (3.1).
