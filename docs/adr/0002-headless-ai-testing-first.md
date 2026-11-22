# 0002: Headless, agent-run testing first

- Status: Proposed
- Date: 2026-11-15
- Decided by: open (the maintainer)

## Context

Getting FNVVR stable together with the co-op mod ([ADR 0001](0001-two-separate-mods-that-cooperate.md))
needs many game runs: load a save, host or join a session, move, fight, open menus, quit, look for
crashes. Headset time and the maintainer's time are scarce. An AI agent should be able to build,
start the game, drive it, collect evidence and decide pass or fail on its own, without a headset and
without the user. A headset session then only confirms what the harness already passed.

What already exists:

- **FNVVR unit tests** (`src/d3d9/vr/tests/`, `meson test`): host-compiled, no game.
- **FNVVR emulator backend** (`d3d9.vrBackend = emulator` or `DXVK_VR_BACKEND=emulator`): runs the
  real game with a simulated headset and controllers, needs no VR runtime. Time is virtual (one
  refresh period per frame) unless real time is set, so runs are reproducible. Its API already has
  a scripted head path (`setHeadScript`), an input source hook (`setInputSource`) and a frame sink
  (`IVRFrameSink`) that sees every submitted eye frame and panel; in the game it is currently wired
  only to the keyboard and a preview window.
- **FNVVR developer mode** (`d3d9.vrDeveloperMode`): god mode, weapons and ammunition after each
  loading screen. FNVVR also writes a crash log.
- **Co-op mod development aids** (in its public source): a "second copy" mode that lets two game
  instances run on one machine, a "lab channel" (named-pipe command channel, `LabChannel=1` in its
  INI) with debug commands, a `fakefriend` tool that plays a scripted remote player (walking,
  running, weapon out, sneaking), a fake-lag network layer and its own log (`NVCoop.log`).
- **The game**: console scripts (`bat <file>`), xNVSE console wrappers and script functions, INI
  settings, command-line start through `nvse_loader.exe`.

## Decision (proposed)

Build the test harness before compatibility fixes, in layers, each with an automatic verdict:

1. **Unit tests** as today, plus tests for every pure policy added for co-op compatibility.
2. **Single game, emulator backend, co-op mod loaded**: a launcher script (PowerShell on Windows,
   later Python for Proton) that backs up the test state (guidelines, "Test state and backups"),
   writes `dxvk.conf` and the co-op INI for the run, starts the game through xNVSE, waits with a
   time-out, collects logs and captures into `local/runs/<run>/`, restores everything and writes a
   `summary.txt`. Needed additions in FNVVR (small, upstream-friendly, off by default):
   - `d3d9.vrTestScript = <file>`: timed actions for the emulated head and controllers (head pose,
     buttons, sticks, triggers), fed into the existing `setHeadScript` / `setInputSource`;
   - a frame sink that writes eye images and panels to files at listed frames;
   - an exit action (quit the game after a step or a frame count) and fixed log markers (VR
     session started, first frame in game, save loaded, script finished).
3. **Two instances on one machine**: host with FNVVR (emulator) and the co-op mod, guest as a second
   copy (plain or also FNVVR emulator), joining over direct IP on loopback; or the co-op
   `fakefriend` as a lightweight guest. Game-level actions (load a save, teleport, spawn an enemy,
   query state) go through console `bat` files or the co-op lab channel.
4. **Real OpenVR path without a headset**: SteamVR with its null driver (`forcedDriver=null`), to
   cover `OpenVRBackend` and the compositor hand-off. Controllers are not simulated there, so input
   tests stay on layer 2.
5. **Headset session** by the maintainer for what only a person can judge (comfort, image quality,
   controller feel).

Pass/fail, automatic: process alive until the script's own exit; no crash log, crash dump or error
markers in the DXVK, FNVVR, xNVSE and co-op logs; all expected markers present; captures written and
plausible (not black, the two eyes differ by the expected parallax, panels present when expected);
for co-op both instances report the session and see each other.

## Unknowns

- Toolchain on Windows: FNVVR builds with Meson and mingw-w64 (Linux-native); WSL or MSYS2 is needed
  and not set up yet.
- Whether the game keeps running unfocused with the emulator backend (FNVVR only keeps it running
  without focus for headset backends) and whether two instances can both run in the foreground.
- Whether the co-op mod needs Steam running for direct-IP sessions, and whether its second-copy mode
  works with the Steam build's DRM and with FNVVR loaded in both instances.
- How to load a specific save reliably without menu navigation (console `load`, xNVSE functions,
  an INI start command, or menu input through the test script).
- Conflicts between FNVVR's engine patches (game heap replacement, fast math, timer, input hooks,
  hidden window, forced game settings) and the co-op mod's hooks; the harness is meant to find them.
- Whether emulator runs are close enough to OpenVR runs for co-op bugs (timing, hidden window).
- Which parts of the additions upstream FNVVR would accept.

## Consequences

- Compatibility fixes start only after layer 2 runs reliably; layer 3 follows for co-op-specific
  work.
- FNVVR gets test-only options; they stay off by default and are documented in `dxvk.conf`.
- Each game run still needs the maintainer's go-ahead until the maintainer decides otherwise.

## Update 2026-11-22: game-level control from the agent

Requirement (the maintainer): while the game runs, the agent runs console commands itself and reads
values and console text back, without changing saves. Where this lives is decided in
[ADR 0007](0007-where-test-automation-lives.md): a small xNVSE plugin of our own,
[FNVVR-Test-Bridge](https://github.com/joschi127/FNVVR-Test-Bridge) (named pipe, console lines and `eval`
expressions, console text in the reply, off by default, no performance cost when off). It replaces the
"console `bat` files or the co-op lab channel" mentioned for game-level actions in layer 3 above; the co-op
lab channel was looked at first and is not used.
