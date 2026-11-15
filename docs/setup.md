# Test and play setup: FNVVR together with New Vegas True Co-op

The setup this fork targets and tests against. FNVVR and True Co-op stay two separate mods
(see [ADR 0001](adr/0001-two-separate-mods-that-cooperate.md)); this page only lists what has to be
installed so both run in the same game.

## Game

- **Fallout: New Vegas, Steam version, with all DLC** (True Co-op requires the Steam version; FNVVR's
  engine addresses are for `FalloutNV.exe` 1.4.0.525).
- Steam: "Only update this game when I launch it", no beta branch. Steam Cloud off for test installs
  (see the test-state rules in the guidelines).
- Started with `nvse_loader.exe` while Steam is running (True Co-op needs Steam open).
- Borderless window recommended (True Co-op: full screen can fail to come back after alt-tab).

## Requirements of True Co-op (everyone in a co-op session)

From the mod's Nexus page ("New Vegas Co-op", mod 100267) and its source
([Zyrcxies/NewVegasCoop](https://github.com/Zyrcxies/NewVegasCoop)):

| Mod | Source |
|---|---|
| xNVSE | https://github.com/xNVSE/NVSE/releases |
| JIP LN NVSE | https://www.nexusmods.com/newvegas/mods/58277 |
| kNVSE Animation Plugin | https://www.nexusmods.com/newvegas/mods/71336 |
| JohnnyGuitar NVSE | https://www.nexusmods.com/newvegas/mods/66927 |
| Real Time Menus - NVSE (with its Fixes plugin) | https://www.nexusmods.com/newvegas/mods/94910 |
| FNV 4GB Patcher | https://www.nexusmods.com/newvegas/mods/62552 |

Recommended by True Co-op:

| Mod | Source |
|---|---|
| The Mod Configuration Menu (+ MCM Extender) | https://www.nexusmods.com/newvegas/mods/42507 |
| LOD Flicker Fix - NVSE | https://www.nexusmods.com/newvegas/mods/91705 |

Then True Co-op itself (NVSE and MCM folders into `Data`) and FNVVR (`d3d9.dll` and its files into
the game folder, see the FNVVR README).

All players need the same plugins in the same load order; True Co-op checks this when joining and can
fetch plugins and archives, but not loose files or xNVSE plugins.

## Points that matter for FNVVR + True Co-op

Facts from True Co-op's description that the compatibility work has to keep in mind:

- **No pausing:** menus and the Pip-Boy run in real time (Real Time Menus). FNVVR's menu and Pip-Boy
  presentation must cope with a world that keeps moving behind them.
- Co-op starts only once the player is outside Doc Mitchell's house; new games use "Quick start".
- Discord's in-game overlay is blocked by True Co-op (it crashes New Vegas).
- Co-op sessions: host over Steam friends or by IP address (UDP port 27960). For tests on one PC the
  mod has a second-copy mode and a scripted fake friend (see its source); details in ADR 0002.
- Logs: `NVCoop.log`, `NVCoop.previous.log` and crash dumps `NVCoop-crash-*.dmp` in the game folder
  (next to `FalloutNV.exe`).
- Don't use the console during co-op (desyncs); test scripts that use console commands run outside a
  co-op session or on the host only, and must say so.

## VR runtime

- FNVVR needs **OpenVR (SteamVR)**. With a Pico 4 over Virtual Desktop, SteamVR is the runtime (not
  VDXR).
- Headset-free runs use FNVVR's emulator backend or SteamVR's null driver (ADR 0002).
