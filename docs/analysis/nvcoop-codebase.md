# New Vegas Co-op (NVCoop.dll): code base analysis

Analysed source: `github.com/Zyrcxies/NewVegasCoop`, local clone next to this repository (`../NewVegasCoop`),
HEAD `0cded49` ("New Vegas Co-op 0.1.11"), plugin version `0.1.11` / version number 21
(`src/version.h`), network protocol version 78 (`src/net/protocol.h:17`).

Purpose: input for a later cross-check against FNVVR (DXVK-based `d3d9.dll` VR mod). This document covers the co-op
mod only. All `path:line` references point into the co-op repository. The co-op mod's license forbids re-uploading
and requires credit and the same license for modified versions, so this document describes the code and quotes only
identifiers, addresses and settings names; no code is reproduced.

Submodules (`external/xNVSE`, `external/enet`) are not checked out in the local clone; statements about ENet internals
refer to upstream ENet.

---

## 1. Overview

### 1.1 What it is

- An xNVSE plugin `NVCoop.dll` (32-bit, C++23, static CRT) for Fallout: New Vegas **1.4.0.525 (Steam)**
  (`CMakeLists.txt`, `src/main.cpp:2599`). 2 to 5 players (host + up to `kMaxFriends = 4`, `src/coop/coop.h:33`).
- Exports only `NVSEPlugin_Query` and `NVSEPlugin_Load` (`src/exports.def`).
- Installed into `Data\NVSE\Plugins\`; MCM page `Data\MCM\NVCoop.json`; optional textures
  (`interface\nvcoop\...`) in `Data\Textures` (`CMakeLists.txt` post-build copy).
- No game code included; game knowledge from JIP LN, JohnnyGuitar, kNVSE, ShowOff, Real Time Menus sources, the
  public FNV decompilation and Ghidra (README).

### 1.2 Module layout

| Area | Files | Role |
|---|---|---|
| Entry / frame loop | `src/main.cpp` (2647 lines) | NVSE entry points, NVSE message handler, per-frame orchestration (`EveryFrame`, `EveryFrameWork`), main-menu Host/Join flow, Lab command interpreter |
| Core, game-independent (`nvcoop_core` static lib) | `src/net/*`, `src/coop/*`, `src/config.*`, `src/log.*`, `src/dev/lab_channel.*`, `src/dev/inspect_text.*` | Protocol, ENet session, Steam P2P session, session/role state machine (`coop::Coop`, `src/coop/coop.cpp` 3412 lines), smoothing, mod list hashing/transfer, chat/voice limiters. Shared with `fakefriend` and the unit tests. |
| Game bridge | `src/game/*.cpp` (`game.cpp` 4153, `scripts.cpp` 1931, `commands.cpp` 1299, `npcs.cpp` 1023 lines, ...) | Everything that touches `FalloutNV.exe`: hooks, memory reads/writes, compiled NVSE script functions, UI tiles. Addresses in `src/game/addresses.h`. |
| Features | `src/downed.cpp`, `src/teleporter.cpp`, `src/linked_saves.cpp`, `src/quick_start.cpp`, `src/chat_voice.cpp`, `src/speech.cpp`, `src/voice_out.cpp` | Downed/revive/drag, teleporter item, save linking, new-game quick start, chat + voice |
| Process-level helpers | `src/crash.cpp`, `src/display.cpp`, `src/dll_blocker.cpp`, `src/dev/second_copy.cpp` | Crash reporter, borderless window, Discord overlay blocker, second-copy mode |
| Dev | `src/dev/commands.cpp` (`coop` console command), `src/dev/dump_game.cpp`, `tools/fakefriend/main.cpp`, `tests/test_main.cpp` (~6500 lines) | Developer aids |

### 1.3 Threads

All game interaction runs on the game's main thread inside NVSE's `kMessage_MainGameLoop` callback
(`src/main.cpp:2495-2498`). Additional threads:

| Thread | Created at | Purpose |
|---|---|---|
| Log writer (detached) | `src/log.cpp:90` | Flushes `NVCoop.log` every 200 ms |
| Crash dump writer | `src/crash.cpp:326` (in `NVSEPlugin_Load`) | Waits on an event, writes the minidump (256 KB stack) |
| Freeze watch | `src/crash.cpp:312` (first main-loop frame) | Every 1 s; if no frame for 8 s while in co-op, **suspends the main thread**, reads its context/stack, resumes, logs (max 5 reports) |
| ENet session | `src/net/session.cpp:120/151` | `enet_host_service` loop with 2 ms wait; queues events for the main thread |
| Mod file server | `src/coop/mod_transfer.cpp:87` | Reads mod files for friends that download them |
| Voice recorder (detached) | `src/game/steam.cpp:295` | Pulls Steam voice data |
| Speech (TTS) | `src/speech.cpp:131` | SAPI text-to-speech rendering, output via `waveOut` |
| Voice playback | `src/voice_out.cpp:32` | Mixes received voice, output via `waveOut` (`WAVE_MAPPER`) |
| Lab channel | `src/dev/lab_channel.cpp:77` | Named-pipe server (only with `LabChannel=1`) |
| Second-copy heartbeat | `src/dev/second_copy.cpp:256` | Logs "Still running" for 120 s (only with `AllowSecondCopy=1`) |
| Mod scan (`std::async`) | `src/main.cpp` `StartCoop` (~line 866) | Hashes plugins/BSAs/NVSE DLLs before connecting |

Steam P2P networking is polled on the main thread (`src/net/steam_session.cpp:209`, `:260`). Steam callbacks are
registered through `SteamAPI_RegisterCallback` (`src/game/steam.cpp:191-192`) and are therefore dispatched by the
game's own `SteamAPI_RunCallbacks`.

### 1.4 Networking model

- **Host-authoritative.** The host's game runs quests, NPC AI, containers, doors, world items, terminals,
  activators and scripted sequences. Friends' games show **puppets** ("copies") of the host's NPCs that are moved from
  the host's data (`src/game/npcs.cpp`); friends' hits on NPCs are reported to the host, who applies damage
  (`src/main.cpp:2101-2155`, `ReportHit`, `DamagePlayer`). Quest stages/objectives, reputation, fast-travel markers,
  party perks, player-affecting script commands (items, perks, notes, image-space modifiers, control locks) are
  relayed from the host (`src/game/commands.cpp`, `src/game/progress.cpp`, `src/game/quests.cpp`).
- Transports (`src/net/transport.h`): **ENet over UDP**, default port **27960** (`src/net/session.cpp`, two channels:
  0 reliable, 1 unreliable, `:69`; peer timeout 5-10 s, `:282`) and **Steam P2P** via the deprecated
  `ISteamNetworking` (`SteamNetworking006`) obtained directly from `steamclient` interfaces
  (`src/game/steam.cpp:153-198`, `src/game/addresses.h` namespace `steamslots`). Hosting can run both at once
  (`HostBoth`, `src/main.cpp:899`). Steam Rich Presence is set while hosting (`SetHostingOnSteam`, `src/main.cpp:2088`).
- Join flow: Hello with protocol version (mismatch = kick, `src/coop/coop.cpp:400-402`), mod list comparison
  (plugins, BSAs, `Data\NVSE\Plugins\*.dll`), host approval question (60 s timeout, `kApprovalTimeoutMs`) unless
  `AutoApproveJoins=1`. NVSE plugin DLLs are compared and reported but are **excluded from the mod fingerprint**
  (`src/coop/mods.cpp:65-67`) and never downloaded (`src/coop/coop.cpp:2275`), so a DLL present on only one side does
  not block joining. Missing/different plugins and archives can be downloaded from the host (`src/coop/mod_transfer.cpp`),
  which **writes files into the friend's `Data` folder** after a confirmation question.
- Tick rates (all on `GetTickCount`):
  - Own pose: every **50 ms (20 Hz)** while moving, 1 s idle (`kPoseIntervalMs`, `kIdlePoseIntervalMs`,
    `src/coop/coop.h:49-50`, used `src/coop/coop.cpp:2548`). Pose = cell/worldspace, x/y/z, **heading = player
    `rotZ`, pitch = player `rotX`**, stance flags, movement flags, run speed (`src/game/game.cpp:70-81`,
    `src/coop/smoothing.h:11-24`). Remote poses are interpolated with a 50 ms delay (`Smoother::kDelayMs`).
  - Host NPC states: every 50 ms (`src/coop/coop.cpp:2572`, `src/game/npcs.cpp:72-74`), NPC timeout 3 s.
  - Health 200 ms (local read every 100 ms, `src/main.cpp:2158-2168`), gear 500 ms, looks 5 s, game time 5 s,
    doors/containers/mines/objects scans 250 ms, items 500 ms, quests 1 s (`src/game/*.cpp` `kLookMs` constants).
- Load synchronisation: while any player loads, the others' world speed is set to 0.0001 and a notification shows
  ("Waiting for ... to finish loading"), `UpdateFreeze`, `src/main.cpp:2025-2043`.

### 1.5 Configuration

Two INI files.

**`Data\NVSE\Plugins\NVCoop.ini`, section `[Coop]`** (`src/config.cpp:14-38` default file, `:179-272` reader).
A missing file is created with the documented keys. Keys and defaults:

| Key | Default | Meaning |
|---|---|---|
| `Mode` | `menu` | `menu` (Host/Join buttons), `off`, `host`, `join`, `auto` (testing: first copy hosts, next joins `127.0.0.1`); unknown value = `menu` |
| `Address` | `127.0.0.1` | Host address for `Mode=join` / LanCoop |
| `Port` | `27960` | UDP port |
| `Name` | `Player` | Player name for ENet/`Mode=auto` sessions (Steam hosting uses `MyName()`) |
| `HostQuestMods` | empty | Comma list of quest mods whose quests the host runs for all |
| `AllowSecondCopy` | `0` | Developer: allow a second game copy on the same PC (see 1.8) |
| `SecondCopyWaits` | `0` | Second copy shows a message box to attach a debugger |
| `BlockDiscordOverlay` | `1` | Hot-patches `ntdll!LdrLoadDll` to refuse `discordhook.dll` |
| `Borderless` | `1` | **Forces windowed mode and makes the window borderless full-screen** (see 2.2) |
| `FarCells` | `1` | Host loads cells around far friends |
| `LabChannel` | `0` | Developer: named pipe `\\.\pipe\NVCoopLab` |
| `LogLevel` | `debug` | `debug`, `info`, `warn` |
| `AutoApproveJoins` | `0` | Host lets joiners in without the question (not in the default file) |
| `LanCoop` | `0` | Join menu offers "LanCoop (NVCoop.ini's Address)"; host also listens on ENet |
| `IpHosting` | `0` | Host also accepts joins by IP |
| `JoinAddress` | empty | Last typed IP (`ip` or `ip:port`), written by the address box (`src/main.cpp:544`) |
| `FakeLag` | empty | `ping[,jitter[,loss%]]` (ms, ms, %), applies to the ENet session (`src/net/fake_lag.h:19-43`) |
| `DesyncNotices`, `SyncRadio`, `Chat`, `ChatSpeech`, `ChatSpeechVolume`, `ChatKey` | 1,1,1,1,80,21 | Older location of player settings (read, then overridden by the MCM file) |
| `AskMode`, `AskHardcore` | 1, 1 | New game questions; may be overridden by patch files `NVCoop_*.ini` in the same folder (e.g. `data/patches/NVCoop_DUST.ini`) |
| `DumpGameCode` | 0 | Read in `NVSEPlugin_Query`: dumps the in-memory exe to `NVCoop-game-memory.exe` (`src/main.cpp:2604`) |

**`Data\config\NVCoop.ini`, section `[Settings]`** (MCM-managed, `src/config.cpp:62-165`, path
`src/main.cpp:194`). Re-read every 2 s when its timestamp changes (`FollowPlayerSettings`, `src/main.cpp:1262`).
Keys (defaults): `bChat`=1, `iChatKey`=21 (DIK `Y`), `bChatSpeech`=1, `iChatSpeechVolume`=80, `iVoice`,
`iVoiceVariant`, `iVoicePitch`, `iVoiceSpeed`, `iVoiceEffect`=0, `bVoiceChat`=1, `iVoiceChatKey`=48 (DIK `B`,
push-to-talk), `iVoiceChatMode`=0 (0 push-to-talk, 1 open mic), `iVoiceChatVolume`=100, `bMouths`=1,
`bSkipDlcPopups`=1, `bSyncRadio`=1, `bDesyncNotices`=1, **`iFieldOfView`=0** (0 = leave the game's FOV; else 60-120),
`bFriendMarkers`=1, `bTalkLog`=1 plus `iTalkLog*`/`bTalkLog*` layout keys, `bShowAddress`=0, `bFarAreas`=1,
`bCoopBalanced`=0, `bFriendlyFire`=0, `iSettingsVersion`.

### 1.6 Logs and crash dumps

- `NVCoop.log` in the **current directory (game folder)**; previous run renamed to `NVCoop.previous.log`. If the file
  is locked (a second game copy), `NVCoop-2.log` ... `NVCoop-4.log` are used (`src/log.cpp:72-90`). Errors flush
  immediately. Every 30 s a "Frame cost" line with NVCoop's per-frame time by part and the free address space
  (`src/main.cpp:1975-1999`); frames > 50 ms log a "Hitch" line (`WatchHitches`, `:1926`).
- Crash reporter (`src/crash.cpp:319-337`): `SetUnhandledExceptionFilter`, `SIGABRT`, `std::set_terminate`,
  invalid-parameter handler; on crash logs module+offset, a stack scan, open menus, address space and writes
  `NVCoop-crash-YYYYMMDD-HHMMSS.dmp` (game folder) from the dump thread. Reserves 32 MB address space at start and
  frees it on crash. It **chains** to the previous filter, and `CheckCrashReporter` (PostLoad, every world load,
  `src/main.cpp:1007`, `:2492`) puts itself back **in front of any filter installed later**, then chains to that one
  (`src/crash.cpp:340-351`).
- Map file `build/<config>/NVCoop.map` for symbolising (`CMakeLists.txt`).

### 1.7 Build

CMake + Visual Studio 2026, preset `x86` (Win32, generator "Visual Studio 18 2026"), `cmake --preset local` /
`cmake --build --preset local-debug` with an optional `CMakeUserPresets.json` that sets `GAME_DIR` for automatic
copying (README, `CMakePresets.json`). Targets: `NVCoop` (DLL), `fakefriend` (console exe), `nvcoop_tests`
(CTest). Links `dbghelp`, `psapi`, ENet (`ws2_32`, `winmm`). Runtime `RUNTIME_VERSION=0x040020D0`.

### 1.8 Developer and test aids (how to use them)

All are off by default. They are documented in the default INI text but not in the README.

**Second copy on one PC (`AllowSecondCopy=1`)** - `src/dev/second_copy.cpp:241-272`, enabled from
`NVSEPlugin_Load` (`src/main.cpp:2623-2624`).
- First copy creates mutex `NVCoop_GameRunning`; a copy that finds it already existing is the "second copy".
- In **every** copy with the setting on: IAT hooks in `FalloutNV.exe` on `CreateMutexA`/`OpenMutexA` (renames the
  game's single-instance mutex `FalloutNVGameMutex` to `FalloutNVGameMutex_<pid>`), `FindWindowA` (returns null for
  class/title containing "Fallout"), `ExitProcess`, `PostQuitMessage`, `TerminateProcess` (logging only),
  `steam_api.dll!SteamAPI_Init` (the second copy returns false: **no Steam in the second copy**, so it can only use
  ENet/IP); a 5-byte inline hook on `ntdll!NtTerminateProcess` (stack trace on exit, self-removing); a vectored
  exception handler logging the first 30 exceptions; the 120 s heartbeat thread.
- `SecondCopyWaits=1`: the second copy shows a top-most message box with its PID to attach a debugger.
- Both copies share the same `Data` folder, INI files and saves folder; logs go to `NVCoop.log` and `NVCoop-2.log`.

**Automatic host/join without menus**: `Mode=auto` makes the first copy host on `Port` via ENet and the second join
`127.0.0.1:Port` as `"<Name> 2"` (`src/main.cpp:924-931`). `Mode=host` / `Mode=join` (`Address`/`JoinAddress`) do the
same with a fixed role. Co-op only starts after a world is loaded **and the character is past the intro** (left Doc
Mitchell's house or tutorial quest done, `IsPastIntro`, `src/game/game.cpp:173-182`; `TryStartCoop`,
`src/main.cpp:939-959`), retried every 2 s. Combine with `AutoApproveJoins=1` on the host so no approval question
appears. JIP LN, kNVSE and Real Time Menus must be loaded or co-op refuses to start (`src/main.cpp:2452-2465`).

**Lab Channel (`LabChannel=1`)** - `src/dev/lab_channel.cpp`, started at `src/main.cpp:2640`.
- Pipe `\\.\pipe\NVCoopLab`, byte mode, duplex, **one instance** (`FILE_FLAG_FIRST_PIPE_INSTANCE`), local clients
  only (`PIPE_REJECT_REMOTE_CLIENTS`), DACL = current user only (`:17-36`, `:63-65`). Consequence: with two game
  copies on one PC only the first copy gets the pipe; the second logs an error.
- Protocol: write one line terminated by `\n` (CR stripped), max 1000 chars, no control characters; one reply line
  comes back (newlines in the reply replaced by `" | "`). Lines are queued to the main thread and executed in
  `RunLabLines` (max 4 per frame, **not while a save is loading**, `src/main.cpp:1880-1886`); reply timeout 30 s
  (`kLabReplyWaitMs`) -> `"timeout: the game didn't take it ..."`. One client at a time; reconnect after disconnect.
- Commands (`RunLabLine`, `src/main.cpp:1602-1878`): `status` (world/co-op state, player position, heading,
  health, locked controls, other players' positions; `LabStatus`, `:1233-1260`), `say <text>` (chat line),
  `mode <0-3>`, `talk on|off` (voice without key), `chatbox`, `type <text>|enter|esc` (posts `WM_CHAR` to the game
  window), `modfix yes|no|cancel`, `approve yes|no`, `sit [ref]`, `teleporter`, `teleport <n>`,
  `typeaddress <ip>`, `pick <n>` (answers the Host-who / Join-how questions), `menu host|join` (clicks the main-menu
  buttons), `companion follow|wait <ref>`, `crashtest`, `crashtest exit`, `crashtest param`, `freezetest` (blocks
  10 s), `quitgame`, `loadwatch`, `eval <expr>`, `evalon <ref> <expr>`, `on <ref> <console line>`, `note <text>`,
  `healhold <player> [ms]`, `activate <ref>`, `terminalpick <n>`, `killcopies <n>`, `friendhit <dmg>`;
  **any other line is run as a console command** (`RunConsoleLine`). This makes it a complete remote-control hook for
  an automated harness (e.g. `menu host`, `pick 0`, `status`, console `coc`, `load`, `player.setpos`).

**`fakefriend`** (`tools/fakefriend/main.cpp`, usage at `:464-471`): standalone console program using the same core
library, connects over **ENet only** (no Steam):
`fakefriend join [address] [port] [name] [hit=<ref>] [still] [down] [revive] [attack] [far]` or
`fakefriend host [port] [name] [hurt] [still] [down] [revive] [attack] [far] [npcs=<n> [cycle]] [npc=<ref>]`, plus
`record=<file>`, `replay=<file> [here]`, `lag=<ping>[,<jitter>[,<loss%>]]`, also `heal`. As a joiner it walks
around the first real player in phases (walk, run, weapon out, sneak, stand), can go down, revive, attack, hit a
given ref, or replay a recorded movement file. It sends no mod list, so the host logs "mods not checked" and lets
it in (approval still required unless `AutoApproveJoins=1`). Default port 27960; the game host must accept ENet
(`IpHosting=1` or `LanCoop=1`, or `Mode=host` without Steam, or `Mode=auto`).

**Fake lag**: `FakeLag=ping,jitter,loss` in `NVCoop.ini` (game) or `lag=` (fakefriend); implemented as delay queues
in the ENet session thread (`src/net/fake_lag.h`, `src/net/session.cpp:228-268`). Does not apply to Steam P2P.

**`coop` console command** (`src/dev/commands.cpp:1529-1545`): registered at opcode base `kNVSEOpcodeTest`
(test range). `coop help` lists sub-commands: `menus`, `inspect`, `check` (memory map), `spawn`, `ganger`, `vatscam`,
`deathcam`, `killcam`, `mirror`, `doors`, `boxes`, `quests*`, `items`, `fight`, `face`, `watchface`/`watchlog`
(hardware data breakpoint on the main thread via `SetThreadContext` + vectored handler, `:79-96`, `:1063`,
`:1092`), etc.

---

## 2. Complete inventory of game-process interventions

Notation: "call redirect" = the rel32 of an existing `E8 call` is rewritten (5 bytes, original target kept and
called); "prologue detour" = first N bytes replaced by `E9 jmp` + NOPs after a byte-pattern check, trampoline in
`VirtualAlloc` RWX memory (`HookStart`, `src/game/hooks.cpp:42-67`; `HookCall`, `:69-85`). All game addresses are
`FalloutNV.exe` 1.4.0.525 virtual addresses. Every hook checks the opcode/pattern first and logs and skips when it
differs; when another plugin already redirected the site, the co-op hook chains to that target.

"Switch" lists how the effect can be turned off. Most game hooks have **no off switch** other than not loading the
plugin; many are behaviour-gated (pass-through unless co-op is active).

### 2.1 Process / OS level (other modules)

| # | Target | Kind | What it changes | Installed | Switch |
|---|---|---|---|---|---|
| P1 | `kernel32!SetUnhandledExceptionFilter` usage, CRT `SIGABRT`, `std::set_terminate`, `_set_invalid_parameter_handler` | API registration | Crash reporter, chains to previous; re-asserts itself in front at PostLoad and every world load | `NVSEPlugin_Load` (`src/main.cpp:2613`, `src/crash.cpp:319`), `src/crash.cpp:340` | none |
| P2 | `ntdll!LdrLoadDll` | hot-patch (`8B FF` -> short jmp back into the 5-byte padding, which becomes `E9` to the hook) | Refuses loads whose file name is `discordhook.dll` (returns `STATUS_DLL_NOT_FOUND`), process-wide for all callers | `NVSEPlugin_Load` (`src/main.cpp:2628`, `src/dll_blocker.cpp:35-69`) | `BlockDiscordOverlay=0`; skipped if the prologue is not `8B FF` with `90/CC` padding (already hooked) |
| P3 | `FalloutNV.exe` IAT: `KERNEL32!GetPrivateProfileStringA`, `GetPrivateProfileIntA`, `WritePrivateProfileStringA` | IAT hook | For `[Display] bFull Screen`: remembers the real value, **returns 0 to the game** (game creates a window, not exclusive full screen) and writes the remembered value back on save | `NVSEPlugin_Load` (`src/main.cpp:2626`, `src/display.cpp:103-124`) | `Borderless=0` |
| P4 | Game main window | `SetWindowLongPtrA(GWL_STYLE, WS_POPUP\|WS_VISIBLE)` + `SetWindowPos(HWND_TOP, monitor rect, SWP_FRAMECHANGED)` | If the INI asked for full screen: turns the (windowed) game window into a borderless window covering the primary monitor | Main loop, once, as soon as a visible top-level window of the process exists (`src/main.cpp:1971-1972`, `src/display.cpp:126-148`) | `Borderless=0`; no-op if `bFull Screen=0` |
| P5 | Game main window procedure | `SetWindowLongPtrA(GWLP_WNDPROC)` subclass (`ChatWndProc`) | While the chat line or an address box is active, swallows `WM_CHAR`/`WM_KEYDOWN`/`WM_KEYUP`/`WM_SYSCHAR` and feeds the text; otherwise `CallWindowProcA` to the previous proc. Never removed. Window found via `EnumWindows`: first visible, unowned top-level window of the process | Lazily on first `OpenChat`/`StartTextEntry` (chat key, Join-by-IP address box) (`src/game/game.cpp:2742-2802`) | `bChat=0` avoids the chat path; the address box still installs it |
| P6 | `FalloutNV.exe` IAT: `CreateMutexA`, `OpenMutexA`, `FindWindowA`, `ExitProcess`, `TerminateProcess`, `user32!PostQuitMessage`, `steam_api!SteamAPI_Init` | IAT hooks | Second-copy mode (see 1.8) | `NVSEPlugin_Load` (`src/dev/second_copy.cpp:259-265`) | `AllowSecondCopy=0` (default) |
| P7 | `ntdll!NtTerminateProcess` | 5-byte inline detour, restores itself on first call | Logs exit stack | same | `AllowSecondCopy=0` |
| P8 | `AddVectoredExceptionHandler` | VEH | Logs first exceptions (second-copy mode); hardware-breakpoint handler (`coop watchface`) | `src/dev/second_copy.cpp:255`, `src/dev/commands.cpp:1092` | dev only |
| P9 | ENet `enet_initialize` | library init | Upstream ENet on Windows calls `timeBeginPeriod(1)` in `enet_initialize` and `timeEndPeriod` in `enet_deinitialize` (1 ms system timer resolution while an ENet host exists) | When an ENet host/client starts (`src/net/session.cpp:82-96`) | Only Steam-only sessions avoid it |
| P10 | Audio | `waveOutOpen(WAVE_MAPPER)` | TTS and voice chat play on the **Windows default output device**, outside the game's audio engine | Speech/voice threads | `bChatSpeech=0`, `bVoiceChat=0` |
| P11 | Steam | `SteamAPI_RegisterCallback` (P2P session request, connect fail), `ISteamNetworking::AllowP2PPacketRelay(true)`, `ISteamFriends::SetRichPresence`, `ISteamUser` voice | API use, no patching | On first `SteamReady()` (`src/game/steam.cpp:153-198`) | `Mode=off` |

### 2.2 Code patches in `FalloutNV.exe`

| # | Address | Kind | What it changes | Installed | Switch / gating |
|---|---|---|---|---|---|
| G1 | `0x007CB886` (call to `0x007CC6E0` MakeStartMenuOptions) | call redirect | After the game builds the main-menu option list, appends **"Host Co-op" / "Join Co-op"** start-menu options (allocated from the game heap, `0x007CE620` init, `0x007CB2E0` append) and moves them after "Load" | PostLoad (`src/main.cpp:2468`, `src/game/game.cpp:1917-1928`, `MakeOptions` `:1880-1911`) | `Mode=off` |
| G2 | `0x00850760` (load save, `55 8B EC 81 EC 2C 02 00 00`) | prologue detour (9 bytes) | Friends: a load of a save not linked to the host's game is refused and the host is asked to load for everyone (`MayLoad`, `src/linked_saves.cpp:326`) | PostLoad (`src/main.cpp:2480`, `src/game/game.cpp:843-852`) | gated by co-op state |
| G3 | `0x0061F170` TopicInfo run-result-script (`55 8B EC 83 EC 0C`, or chains an existing `E9`) | prologue detour (6 bytes, own code) | Dialogue result scripts: shares/replays friends' conversation lines | PostLoad (`Dialogue::Install`, `src/main.cpp:2470`, `src/game/dialogue.cpp:53-113`) | gated |
| G4 | `0x0061B361` (call to `0x0061A790` pick line) | call redirect | Line picking for other players' conversations | PostLoad (`src/game/say_hooks.cpp:229`) | gated |
| G5 | `0x0083C7B0` DialogueItem first response (`55 8B EC 51 89 4D FC`) | prologue detour (7 bytes) | Forces the response of an "armed" replayed line | PostLoad (`src/game/say_hooks.cpp:237`) | gated |
| G6 | `0x00AD8E60` SoundHandle::SetEndCallback (`55 8B EC 83 EC 0C`) | prologue detour (6 bytes) | Tracks end of spoken lines (voices of replayed dialogue) | PostLoad (`src/game/say_hooks.cpp:242`) | gated |
| G7 | `0x00758160`, `0x0075819A` (calls to `0x00501830` terminal run entry) | call redirect x2 | Shares terminal entries with the host | PostLoad (`src/main.cpp:2471`, `src/game/terminals.cpp:65-100`) | gated |
| G8 | `0x00453121` (call to `0x00451A50` TES fill grid slot) | call redirect | "Far cells": host keeps cells around far friends loaded (attaches extra cells, Havok attach) | PostLoad (`src/main.cpp:2472`, `src/game/far_cells.cpp:352-377`) | `FarCells=0` / `bFarAreas=0` (behaviour), hook always installed |
| G9 | `0x00973817` (call to `0x00566950` IsTeammate, in detection) | call redirect | Other players' characters count as teammates for the sneak meter (no [DETECTED]) | PostLoad (`src/main.cpp:2473`, `src/game/game.cpp:3817-3829`) | none |
| G10 | `0x0086E79E, 0x0086EEAF, 0x008898BD, 0x0086A4A1, 0x0086A4DE, 0x0063E0F8, 0x00837D05, 0x008387C5, 0x0089C6CA, 0x0089C74A, 0x0089CEBA, 0x0089CF56, 0x008A7EA9, 0x008D1AA1` (14 calls to `0x00702360` InMenuMode, or to Real Time Menus' replacements) | call redirect x14 | **"Live" escape menu**: while in co-op, `InMenuMode` returns false at these 14 sites when the pause (start) menu is up (`0x004A4040`), so the world keeps simulating behind the pause menu | First main-loop frame after `kMessage_DeferredInit` (`src/main.cpp:1960-1967`, `src/game/escape_menu.cpp:38-63`) | gated by `EscapeMenuLive(g_coop.InGame())` (`src/main.cpp:1968`); no switch |
| G11 | `0x00784EE3` (call to `0x00648BC0` level-up skill points) | call redirect | Co-op balanced: fewer skill points | same (`src/game/level_up.cpp:31-50`) | `bCoopBalanced` |
| G12 | `0x00785931` (`push imm32` of a callback `0x007864E0`) | immediate patch | Level-up perk list alpha: party perks shown red | same (`src/game/perk_menu.cpp:105-118`) | behaviour only |
| G13 | `0x0078543A` (call to `0x005EBAC0` perk describe) | call redirect | Perk description note "someone in your party has it" | same | behaviour only |
| G14 | `0x004CE913` (call to `0x004C0CF0` take off item, in RemoveAllItems) | call redirect | Crash fix for duplicate worn outfits | same (`src/game/remove_all_items.cpp:39-58`) | none |
| G15 | `0x009C7E7D, 0x009C7EA9, 0x009C7EC6, 0x009C852D, 0x009C8565` (calls to `0x00AA4DB0` SetTimeMultiplier inside VATS) | call redirect to a no-op x5 | **VATS no longer slows time** (real-time VATS) | `StartCoop` (`src/main.cpp:846`, `src/game/hooks.cpp:128-144`) | none once installed; never restored |
| G16 | `0x009C7489` (call to `0x0058BB50` VATS pick camera path) | call redirect | VATS playback **camera path suppressed** (returns null => playback in the player's own view) | `StartCoop` (`src/main.cpp:847`, `src/game/hooks.cpp:193-216`) | `coop vatscam 0/1` dev toggle (`SetVatsOwnViewPlayback`) |
| G17 | `0x009C96E0` (call to `0x009E32D0` shot stage, melee) | call redirect | Lets melee VATS actions pass without a camera shot | `StartCoop` (`src/game/hooks.cpp:198`) | none |
| G18 | `0x009438F1` (call to `0x009CA1F0` VATS player death playback) | call redirect | No death camera playback while the player is downed (life state 6) | `StartCoop` (`src/main.cpp:848`, `src/game/hooks.cpp:244-257`) | `coop deathcam` dev toggle |
| G19 | `0x0077C58C`, `0x0077D6BE` (calls to `0x00953C20` player combat count) | call redirect x2 | Level-up menu waits while the party is fighting | lazily on first `UpdateFighting` (`src/main.cpp:1315`, `src/game/game.cpp:1806-1829`) | gated |
| G20 | `0x009C3BD1` (call to `0x009C3930` mine reacts to target) | call redirect | Mines ignore puppet characters in friends' games | lazily (`src/game/mines.cpp:89`, `src/game/game.cpp:1831-1853`) | gated |
| G21 | `0x00AA4DB0` SetTimeMultiplier (`55 8B EC 83 EC 08`) | prologue detour | Logs speed changes | dev command only (`src/dev/commands.cpp:1221`, `src/game/hooks.cpp:111-120`) | dev |

### 2.3 Vtable / function-pointer patches

| # | Address | What | Installed | Switch |
|---|---|---|---|---|
| V1 | `0x0108F108` (Explosion vtable slot, original `0x009ACD60`) | Explosion init: records new explosions for sharing | PostLoad (`src/main.cpp:2483`, `src/game/game.cpp:990-1009`) | none |
| V2 | `0x010763B8` (SleepWait menu click handler, original `0x007C0220`) | Wait/sleep votes for the party | PostLoad (`src/main.cpp:2478`, `src/game/sleepwait.cpp:66-82`) | gated |
| V3 | Actor mover vtable `0x0109223C` slots 4 (`0x009DE080` release flags) and 14 (`0x009DFB80` reset) | "Holds" puppets' legs against their AI (atomic swap) | First world load after `CheckMoverMap()` passes (`src/main.cpp:1013`, `src/game/mover.cpp:129-150`, `:162-188`) | none |
| V4 | NVSE command table `execute` pointers of: `PlayGroup`, `SetDestroyed`, `RewardXP` | Object state sharing | PostLoad (`src/game/commands.cpp:501-527`) | gated |
| V5 | `Activate`, `MoveTo`, `StartConversation`, `SetStage`, `SetObjectiveDisplayed`, `SetObjectiveCompleted`, `CompleteQuest`, plus 31 "opens a screen" commands by opcode (`src/game/addresses.h` `opcodes::kOpensScreen` and `kOpensScreenToo`, 32 opcodes minus `StartConversation` 0x1056) | Activator sharing; screens/menus opened by another player's replayed use are not opened here | PostLoad (`src/game/commands.cpp:886-960`) | gated |
| V6 | `AddItem`, `AddItemHealthPercent`, `RemoveItem`, `RemoveAllItems`, `RemoveAllTypedItems`, `EquipItem`, `UnequipItem`, `AddPerk`, `RemovePerk`, `AddNote`, `RemoveNote`, **`ApplyImageSpaceModifier`, `RemoveImageSpaceModifier`, `DisablePlayerControls`, `EnablePlayerControls`**, `CastImmediateOnSelf`, `AddScriptPackage`, `RemoveScriptPackage`, `Kill`, `KillActor`, **`ShowMessage`** (`g_watched`, `src/game/commands.cpp:83`) | Host: relays what quest scripts do to the player to friends. Friend: blocks local scripts that take away screen/controls/inventory ("the host's game decides that") and replays the host's; skips DLC intro popups (`bSkipDlcPopups`) | PostLoad (`src/game/commands.cpp:1087-1118`, wrapper `:295-420`) | gated by role |
| V7 | NVSE native event handlers `OnHit`, `OnEnable`, `OnDisable`, `OnActivate` | Hit sharing, enable-state sharing, activation sharing | PostLoad (`src/game/scripts.cpp:315-330`, `src/game/commands.cpp:896`) | none |
| V8 | NVSE messaging listener (`DeferredInit`, `PostLoad`, `MainGameLoop`, `PreLoadGame`, `ExitToMainMenu`, `SaveGame`, `PostLoadGame`, `NewGame`, `ExitGame`, `ExitGame_Console`) | Plugin lifecycle and frame loop | `NVSEPlugin_Load` (`src/main.cpp:2632-2637`, handler `:2439-2581`) | none |
| V9 | New console/script command `Coop` at opcode base `kNVSEOpcodeTest` | dev | `src/dev/commands.cpp:1535-1545` | none |

### 2.4 Direct data writes (game settings, objects, UI)

| # | Address / target | What | When | Switch |
|---|---|---|---|---|
| D1 | `0x011DEEDC` (`bAlwaysActive:General` value, setting at `0x011DEED8`) | **Game keeps running when its window is not active** while a co-op role exists; old value restored when co-op ends | Main loop on role change (`src/main.cpp:2072-2083`, `src/game/popups.cpp:253-259`) | none (tied to co-op) |
| D2 | Global time multiplier `0x011F6394` via `0x00AA4DB0` | World speed 0.0001 while waiting for a player to load, back to 1.0 | `UpdateFreeze` (`src/main.cpp:2025-2043`, `src/game/hooks.cpp:122-126`) | none |
| D3 | `0x011CEA48` (value of the VATS target-FOV floor setting at `0x011CEA44`, code name `kVatsTargetFovMin`) | Set every frame in co-op to the player's default world FOV (value at `0x01203160`, setting `kDefaultWorldFov` `0x0120315C`) so VATS does not zoom | `KeepVatsFeel`, every frame in co-op (`src/main.cpp:1969-1970`, `src/game/hooks.cpp:277-295`) | none |
| D4 | Player `+0x7C0` (kill-cam mode) | Forced to 0 every frame in co-op (no kill camera) | same | `coop killcam N` dev hold |
| D5 | Game settings via console `SetGameSetting`: `fVATSTimeMult 1`, `fVATSTargetFOVMultNear 0`, `fVATSTargetFOVMultFar 0`, `fVATSTargetSelectCamPanTime 0.25`, `fVATSCamZoomInTime 0.25`, `fVATSPlaybackDelay 0.04` | Real-time VATS feel | `StartCoop` (`src/main.cpp:844-856`, values `src/coop/vats_feel.h:13-19`) | none |
| D6 | `SetNumericGameSetting` for balance (skill points, carry weight, XP per kill, ammo value, body-part damage via `SetBodyPartTraitNumeric`) | Co-op balanced | `ApplyBalance` (`src/game/game.cpp:3293-3345`) | `bCoopBalanced=0` restores originals |
| D7 | Console `fov <world> <hands>` | Only when `iFieldOfView` != 0: sets the camera FOV after each load / on change | `src/main.cpp:2233-2242`, `src/game/game.cpp:1239-1259` | `iFieldOfView=0` (default: never runs) |
| D8 | `0x011D892C` (`bGeneralSubtitles` value) | Temporarily overridden around `Say`/`ShowSubtitle` calls for replayed lines | `src/game/say_hooks.cpp:423-428`, `:487-493` | none |
| D9 | `0x011DE7B8 + 0x1C` | "reload game time" flag after wait/sleep sync | `src/game/sleepwait.cpp:190` | gated |
| D10 | HUDMainMenu prompt tiles (`HUDMainMenu+0x9C/0xA8/0xAC/0xB0`, `kHudPromptRef +0x1B8`) via Tile set-value `0x00A012D0` | Hides "E) Talk" prompt on other players' characters, every frame | `HideTalkPromptOnFriends` (`src/main.cpp:2416`, `src/game/game.cpp:3831-3857`) | none |
| D11 | Injected HUD tiles and their NiNode scale (`NiAVObject::Update` `0x00A59C60`) | See section 4 | on demand | MCM switches |
| D12 | Player-controls via script `DisablePlayerControls 1 1 1 0 0 1 1` / `EnablePlayerControls` | Downed state, waiting for host load (movement, Pip-Boy, fighting, rollover, sneak; **not** look/POV) | `src/game/scripts.cpp` "player controls", `src/downed.cpp:207/246/280`, `src/main.cpp:1003-1006` | gated |
| D13 | Player position | `MoveTo`/`SetPos` on the player: follow the group through doors/fast travel (`FollowGroup`, `src/main.cpp:1037`), teleporter (`src/teleporter.cpp`), being dragged when downed (`src/downed.cpp:336`) | gated |
| D14 | Player animation | `PlayAnimationPath` (kNVSE) on the player while downed (lying idle) (`src/downed.cpp:209`, `:346`) | gated |
| D15 | Image-space modifiers | `RemoveImageSpaceModifier` for tutorial fades (`src/game/game.cpp:3044-3052`); host's `ApplyImageSpaceModifier`/`Remove...` replayed on friends (V6) | gated |
| D16 | Save/load via console | `save "..."`/`load "..."` for linked saves (`src/linked_saves.cpp:152-282`), `SetHardcore` (`src/quick_start.cpp:158`) | gated |
| D17 | Pip-Boy radio | `0x008324E0`/`0x00832240` radio on/station: friends hear the host's radio lines (`src/game/say_hooks.cpp:250-261`) | `bSyncRadio=0` |
| D18 | Map/compass | `AddRefMapMarker`/`RemoveRefMapMarker` on friends' characters, compass targets `0x00966F20`/`0x00967350` (`src/game/standins.cpp:96`, `:308-310`) | none |
| D19 | Game heap | Form heap alloc `0x00401000` for menu options and names (`src/game/game.cpp:1869`, `:206-219`) | - |
| D20 | Actors spawned | `PlaceAtMe` of `TempCloneForm`ed NPC base `0x00120EB1` for each remote player; NPC copies; extra enemies (balanced) | gated |

### 2.5 Input APIs

- **No DirectInput, XInput, raw-input, low-level hooks or cursor APIs** (verified by search). Key input comes from:
  - NVSE `IsKeyPressed <DIK>` inside a compiled script function (returns 0 in menu mode) for the chat key
    (`src/game/scripts.cpp:681-689`, `src/chat_voice.cpp:74`);
  - `GetAsyncKeyState` after `MapVirtualKeyA(DIK -> VK)` for **push-to-talk**, only if `GetForegroundWindow()` belongs
    to the game process (`KeyDown`, `src/game/game.cpp:3405-3416`, `src/chat_voice.cpp:153`);
  - NVSE `IsControlPressed` (control codes, e.g. activate for revive/heal hold, grab `0x1B` for drag)
    (`src/game/scripts.cpp:1115-1121`, `src/downed.cpp:255`, `:389`);
  - window messages through the WndProc subclass (P5) for typed text.
- While typing, NVSE `DisableKey` is called on every not-yet-disabled DIK code 1..263 and re-enabled afterwards
  (`KeepKeysFromGame`, `src/game/game.cpp:2891-2911`).
- Gamepad: nothing specific; chat requires a keyboard.

### 2.6 Dependencies on other NVSE plugins

Checked at PostLoad by module name (`src/main.cpp:2452-2465`); co-op refuses to start if one is missing:
- **JIP LN NVSE** (`jip_nvse.dll`): `InjectUIComponent`, `GetLoadedTypeArray`, `GetPlayerCamFOV`, `SetSpeedMult`,
  `ToggleObjectCollision`, `SetBodyPartTraitNumeric`, `GetRefs`, `SetPosEx`/`SetAngleEx`, `AddRefMapMarker`, etc.
- **kNVSE** (`kNVSE.dll`): `PlayAnimationPath`, `SetActorAnimationPath` (remote players' modded animations, downed
  idle), reading the player's kNVSE animation files (`ModAnimFiles`).
- **Real Time Menus** (`RealTimeMenus.dll`): menus (containers, barter, Pip-Boy, dialogue, VATS, ...) do not pause the
  world. The mod reads `Data\NVSE\Plugins\RealTimeMenus.ini [Menus] bPause*` and warns if any is non-zero
  (`CheckRealTimeMenus`, `src/main.cpp:208-229`). Its own escape-menu hook (G10) chains on top of RTM's.
- **JohnnyGuitar NVSE** (soft): `WorldToScreen` / `JGLegacyWorldToScreen` for placing nameplates and chat above heads
  (`src/game/scripts.cpp:702-726`); without it, no markers/chat above heads (logged).
- **MCM + MCM Extender** (`MCM.dll`) for the settings page (warning only, `src/main.cpp:832-842`).
- xNVSE interfaces used: Script (`CompileScript`, `CallFunction`), EventManager, Console (`RunScriptLine2`), ArrayVar,
  CommandTable, Messaging.

About 150 small script functions are compiled at the first world load (`CompileScripts`, `src/game/scripts.cpp`
from `:333`, list of names via `Compile("...")`); most game actions (spawn, move, equip, health, kill, UI) are done by
calling those functions on the main thread.

---

## 3. Game-state assumptions

- **Menus**: assumes Real Time Menus keeps menus live; adds the live escape menu (G10). `InMenuMode` checks use a
  compiled `MenuMode` script (`src/game/scripts.cpp:669-680`); loading screen = `MenuMode 1007`. Co-op questions
  (host approval, join method, mode, teleport target, mod fix) are game **message boxes** shown with the game's
  `ShowMessageBox` (`0x00703E80`, `src/game/popups.cpp:72-110`), retried while another box is busy. Contents and
  barter menus are matched to the host (`kContainerMenu`, `kBarterMenu`); the "how many?" prompt delays syncing.
- **Pip-Boy**: real time via RTM; friends' characters appear on the Pip-Boy map and compass (D18).
- **Dialogue**: real time; other players' conversations nearby are replayed as subtitles/voice and in the "talk log"
  HUD list; the talking player is not frozen. Remote players' characters cannot be talked to (`g_noTalk` script on
  the cloned base).
- **Loading**: `PreLoadGame` / `ExitToMainMenu` clear state; `PostLoadGame` / `NewGame` -> `OnWorldLoaded`
  (`src/main.cpp:961-1035`). The others' worlds slow to 0.0001 while someone loads (D2). Friends follow the host's
  saves/loads (`src/linked_saves.cpp`); friends' own loads are intercepted (G2).
- **VATS**: made real time (G15, D3, D5), playback in the player's own view without the cinematic camera path (G16,
  G17), no kill camera (D4), no death playback while downed (G18).
- **Cutscenes**: detected as "player movement controls disabled" (`PlayerDisabledControls() & 1`,
  `src/main.cpp:2256`); remote characters are then made invisible (`SetActorAlpha 0`, `src/game/standins.cpp:105-109`)
  and their nameplates hidden (`src/chat_voice.cpp:129`).
- **First/third person**: nothing forces a view. Pitch of remote characters is taken from the sender's player
  `rotX` and applied to the clone's `rotX` plus aim animation slots (`SetPitch`, `src/game/game.cpp:318-331`).
- **Remote players** ("stand-ins"): for each remote player a `TempCloneForm` of NPC base `0x00120EB1` gets the
  player's name, face/race/hair/skin tone ("looks", sent every 5 s), no inventory, a no-talk script, and is placed
  with `PlaceAtMe` (`SpawnStandIn`, `src/game/game.cpp:223-251`). They are normal game actors rendered by the engine;
  moved by `MoveTo`/`SetPos`/`SetAngle` script calls and the mover vtable hold (V3); equipment mirrors the remote
  gear; animations via kNVSE; mouth movement from voice level (`SetMouth`). Collision toggled, ghosted when downed.
- **Window focus / alt-tab**: game forced to keep running in the background during co-op (D1). Push-to-talk works only
  when the game window is the foreground window. Borderless window by default (P3/P4) "so alt-tab is safe".
- **Resolution**: HUD layout reads the root tile's width/height or `iSize W`/`iSize H` times menu units per pixel
  (`0x011C73DC`, `0x011C718C`, `0x011D8A48`, `src/game/game.cpp:3989-3995`); markers use screen fractions
  `0..1` from `WorldToScreen`.
- **Local player's camera**: never moved or rotated by the mod except indirectly (teleport/follow, downed animation,
  VATS camera-path removal, kill-cam off, optional `fov`). Chat-to-speech and voice panning use the player's body
  heading `rotZ` (`MixSpeech`, `src/chat_voice.cpp:107`, `:180`).
- **Memory**: 32-bit address space is logged and warned about (4 GB patch check at PostLoad); far cells on the host
  load extra cells and track used address space (`src/main.cpp:2266-2283`).

## 4. Rendering

- **No own renderer.** No D3D9/DirectX, no ImGui, no overlay, no `Present`/`EndScene` hooks, no swap chain or
  device access (verified: no `d3d9`, `IDirect3D`, `imgui` references in `src/`).
- Everything visible is drawn **by the game's UI system (Tile/XML menus) or as game objects**:
  - **HUD components injected into `HUDMainMenu`** with JIP `InjectUIComponent` on first use (`MakeChatHud`,
    `src/game/game.cpp:2804-2870`): chat line `NVCoopChat/NVCoopChatText` (centred, y=600), 5 "chat above head" texts
    `NVCoopAbove0..4`, 4 **nameplates** `NVCoopMark0..3` (name, or red "DOWNED Ns"), revive/heal progress text
    `NVCoopRevive` (centre, 58% height, text bar of `#`), talk log rows `NVCoopTalkN`, `NVCoopTalkEdgeNxE`,
    `NVCoopTalkBarN` (image `interface\shared\solid_black.dds`), `NVCoopTalkLineN` (divider images). The level-up
    menu gets `NVCoopPartyNote` (`src/game/scripts.cpp:1710`).
  - Values are set with `SetUIFloat`/`SetUIStringEx` from compiled scripts (`src/game/scripts.cpp:695-771`,
    `:919-1021`); talk-log tiles are also scaled by writing NiNode local scale and calling `NiAVObject::Update`
    directly (`ScaleTile`, `src/game/game.cpp:3882-3896`).
  - **World-anchored markers** (nameplates, chat above heads) are positioned every frame by calling JohnnyGuitar's
    **`WorldToScreen`** on the remote character (offset 150 units up), which projects with the **game's flat
    camera**; the result (0..1 screen fractions) is written to the tile's `user0/user1` and the tile computes
    `x = screen width * user0`, `y = screen height * user1 - 75/30` (`ShowFriendMarkers`, `UpdateChatAbove`,
    `src/game/game.cpp:3464-3544`; caller `src/chat_voice.cpp:114-145`).
  - On-screen notifications via `MessageEx`/`MessageExAlt` (corner messages) and game message boxes; subtitles via
    the game's `ShowSubtitle` (`0x00705210`).
  - Remote players, NPC copies, dropped items are ordinary game references rendered by the engine.
- Window: forced windowed + borderless resize (P3/P4) is the only window/presentation-related intervention.

## 5. Robustness

- **Version check**: `NVSEPlugin_Query` refuses anything but runtime `1.4.0.525` (`src/main.cpp:2599-2603`); editor
  refused. No check for the GOG/Epic or other builds; addresses are hard-coded.
- **Patch safety**: every code patch verifies opcode (`E8`, `68`) or a prologue byte pattern, logs and skips on
  mismatch, and chains to other plugins' redirects. Missing hooks degrade features with a log line. `CheckMoverMap`
  validates mover vtable slots before patching (`src/game/mover.cpp:162-188`).
- **Protocol**: version mismatch is a hard reject; message sizes bounded; queues capped (`src/coop/coop.h:36-80`).
- **Crash reporting**: own minidumps and log stack scan (P1); freeze detector (8 s) suspends the main thread to log
  its stack; frame-cost and hitch logging.
- **Unit tests**: ~220 checks over the core (`tests/test_main.cpp`); no game tests.
- **Known issues / history** (`CHANGELOG.md`): early release, "playable start to finish, with bugs" (README). Fixed
  items show sensitivity to: closing the window during co-op (crash at exit, 0.1.9), Discord overlay crashes
  (blocked), friends' games crashing in big fights and in VATS on levelled creatures (0.1.4, 0.1.11), MO2 load
  order, `uGridsToLoad` vs `uExterior Cell Buffer` (warned at load, `src/main.cpp:980-994`), freeze reports (0.1.1).
  GitHub issues: only #7 (open, translatable UI strings) and #1 (closed, FNV decompilation hint)
  (`gh issue list`, 2026-11-16).

## 6. Potential friction points with a VR mod that replaces `d3d9.dll`, drives the camera from a headset and re-renders HUD/menus in VR

1. **Forced windowed + borderless resize (P3/P4, `Borderless=1` by default).** The co-op plugin hides
   `bFull Screen=1` from the game via the exe's IAT and later restyles and resizes the game window to the primary
   monitor with `SetWindowPos(... SWP_FRAMECHANGED)`, after the D3D9 device already exists. A DXVK-based `d3d9.dll`
   sees a windowed device and then a window resize: possible swapchain resize/back-buffer mismatch, a changed mirror
   window, or interference with any window handling FNVVR does. Recommendation for combined use:
   `Borderless=0` (or verify FNVVR with a borderless window). Note: P3 only patches `FalloutNV.exe`'s imports, not
   `d3d9.dll`'s own INI reads.
2. **WndProc subclass (P5).** Installed lazily on the first chat/address box, never removed, on the "first visible,
   unowned top-level window of the process". If FNVVR subclasses the same window (order: whoever is later is outer),
   or creates its own visible top-level window (mirror/companion window) that is found first, chat typing goes to the
   wrong window or keystrokes are swallowed. Also `type`/`TypeChat` posts `WM_CHAR` to that window.
3. **HUD markers projected with the flat camera.** Nameplates, "DOWNED" timers and chat-above-head texts are placed
   with JohnnyGuitar `WorldToScreen` (game camera) into `HUDMainMenu` tiles. If FNVVR re-projects the HUD as a flat
   panel in VR, these world-anchored labels will sit at the flat-camera projection on that panel, not over the
   remote character in the headset view (parallax, wrong position when the HMD orientation differs from the game
   camera; off-panel when the game camera's FOV/aspect differ from the VR panel's mapping). A VR-correct version
   needs per-eye projection of the remote player's head position.
4. **Injected HUD tiles depend on screen size.** The talk log and chat line compute positions from the root tile size
   or `iSize W/H` and the HitPoints tile; a VR HUD re-render with a non-standard UI resolution or a cropped HUD region
   may cut off the left-side talk log (anchored near the HP bar), the centred chat line (y=600) or the revive bar.
5. **Menus and pause behaviour.** In co-op, menus, Pip-Boy, dialogue, VATS and even the escape menu do not pause the
   world (Real Time Menus + G10). A VR mod that assumes the game is paused while a menu/Pip-Boy is open (e.g. freezing
   the world view, locking the camera, or rendering menus head-locked) will see the world and NPCs keep moving and
   enemies attacking. G10 rewrites 14 `InMenuMode` call sites; if FNVVR hooks the same calls or `0x00702360`, order
   and chaining matter (the co-op hook chains to the previous target).
6. **VATS changes.** Time multiplier calls in VATS are disabled (G15), the VATS camera path is suppressed (G16), the
   VATS target-FOV floor is rewritten every frame (D3), several `fVATS*` settings changed (D5) and the kill cam is
   forced off (D4). Any FNVVR VATS handling (e.g. disabling VATS camera, own slow motion, FOV handling) must expect
   these and may conflict at the same call sites (`0x009C7489`, `0x009C96E0`, `0x009438F1`, `0x009C7E7D` etc.).
7. **FOV.** With `iFieldOfView` != 0 the mod runs `fov <world> <hands>` after every load and on change (D7), and
   copies `fDefaultWorldFOV` into the VATS FOV floor every frame. A VR mod that controls the projection/FOV must keep
   `iFieldOfView=0` (default) on the co-op side; D3 still reads the world FOV value.
8. **Remote avatar orientation vs. headset.** Sent heading/pitch are the local player reference's `rotZ`/`rotX`, not
   the HMD pose. If FNVVR decouples head look from the player actor (or leaves `rotX` at 0), friends see the VR
   player's character facing/aiming wherever the actor points; conversely, if FNVVR writes HMD yaw into the actor,
   20 Hz pose updates and 50 ms smoothing make head motion visible as body turning. Voice/TTS panning also uses the
   body heading.
9. **Moving the local player.** Teleport-to-friend, follow-the-group travel, being dragged while downed, load
   following and spawn placement move the player with `MoveTo`/`SetPos` (D13), and a downed player gets a lying
   animation via kNVSE while movement/Pip-Boy/fighting controls are disabled (D12/D14; look stays enabled). A VR mod
   that keeps its own playspace/recentre offsets or assumes upright first-person locomotion should handle sudden
   repositioning and the "lying" state (camera height, comfort).
10. **Cutscene detection by disabled movement.** Remote characters become invisible whenever the local player's
    movement controls are disabled (`PlayerDisabledControls() & 1`). If FNVVR disables player movement controls for
    its own purposes, remote players and their nameplates vanish.
11. **Background running and focus.** In co-op `bAlwaysActive` is forced on (D1), so the game keeps rendering and
    simulating when the desktop window loses focus (normal for VR). Push-to-talk (`GetAsyncKeyState`) only works when
    the game window is the foreground window: with a VR runtime/dashboard or another window in front, PTT fails;
    open-mic mode (`iVoiceChatMode=1`) avoids it. Chat typing requires a physical keyboard.
12. **Freeze watch suspends the main thread.** If the main loop stalls for > 8 s during co-op (e.g. a VR compositor
    or headset sleep blocking `Present`), NVCoop's watchdog suspends the main thread to read its stack (max 5 times).
    Harmless in principle but can coincide with FNVVR's own waits; also other players then see the world freeze-wait
    logic only for loading, not for stalls.
13. **Process-wide hot patch of `ntdll!LdrLoadDll` (P2).** Applied in `NVSEPlugin_Load`; FNVVR's later DLL loads
    (OpenXR/OpenVR runtime, DXVK helpers) go through the co-op hook. It only refuses `discordhook.dll`, but if FNVVR or
    a VR runtime layer also hot-patches `LdrLoadDll` (same `8B FF` prologue), one of them backs off or chains
    incorrectly. Switch: `BlockDiscordOverlay=0`.
14. **Crash handler ordering.** NVCoop re-installs itself as the top-level unhandled-exception filter at PostLoad and
    after every world load, then chains. FNVVR crash handling installed later is pushed behind it; dumps from both may
    be written.
15. **Timer resolution and threads.** ENet sets a 1 ms system timer (P9) and its thread wakes every 2 ms; minor, but
    relevant when comparing frame-pacing measurements with and without co-op.
16. **Audio device.** TTS and voice chat use the Windows default output device (`waveOut`), not the game's device. If
    the headset audio is not the default device, co-op voice/TTS plays on the desktop speakers.
17. **Address space.** Both DXVK (`d3d9.dll`, especially with VR resolution/swapchains) and NVCoop's far-cell loading
    on the host consume the 32-bit address space; NVCoop logs free MB every 30 s, which is useful for diagnosing
    out-of-memory crashes in a combined setup.
18. **Second-copy testing with VR.** The second-copy IAT hooks (mutex, `FindWindowA` hiding windows whose class/title
    contains "Fallout") apply to both copies when enabled. Two copies would each load FNVVR's `d3d9.dll`; only one can
    own the VR runtime/headset, so the second copy must run FNVVR in a flat/disabled mode or without FNVVR. The second
    copy has no Steam (ENet only), and only the first copy gets the Lab pipe.
19. **Image-space modifiers and control locks from the host.** Friends' games replay the host's
    `ApplyImageSpaceModifier`/`Remove...` and `Disable/EnablePlayerControls` for scripted sequences and block local
    ones. Full-screen effects and control locks can therefore appear without a local cause; FNVVR's handling of
    image-space effects/fades in VR applies to them too.
20. **No D3D-level conflicts expected.** NVCoop never touches D3D9, the device, `Present`, DirectInput or XInput, so
    there is no direct hook collision with a `d3d9.dll` replacement; all conflicts are at the window, game-function
    and HUD-semantics level listed above.

### Notes for an automated test harness

- Use `Mode=auto` (or `host`/`join`), `AutoApproveJoins=1`, `IpHosting=1`, `LabChannel=1`, `LogLevel=debug`,
  `Borderless=0`; start with a save past Doc Mitchell's house (co-op starts only then).
- Drive the game through `\\.\pipe\NVCoopLab`: `status` for state polling (world loaded, role, position, controls
  locked, other players), console lines for movement/loading, `say`, `menu host|join` + `pick <n>` for the menu
  flow, `quitgame` to exit. Replies time out after 30 s and lines are not run while a save loads.
- For a two-player run on one PC use `fakefriend join 127.0.0.1 27960 FakeFriend [still|attack|down...]` instead of a
  second game copy, optionally with `lag=`; it needs no headset and no second FNVVR instance.
- Watch `NVCoop.log` lines: "Hello from NVCoop! Plugin loaded", "All plugins loaded", "Main menu: added Host Co-op",
  "Game scripts ready", "Display: ...", "Chat: typing goes through the game's window", "Hitch:", "Frame cost:",
  "Freeze:", "CRASH:".
