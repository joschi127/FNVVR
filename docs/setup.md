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
- **4 GB patch on `FalloutNV.exe` is required for FNVVR on Windows**, also without True Co-op (FNVVR offers
  it in the main menu as "Use 4 GB"; the FNV 4GB Patcher does the same). Without it the game only has
  2 GB of address space: on an AMD Radeon RX 7900 XTX it crashed in the 32-bit Vulkan driver right after
  start (with FNVVR's game heap on) or hung when opening menus, V.A.T.S. or tutorials ("Failed to end
  command buffer"). Proton gives 32-bit games 4 GB by default, so Linux players do not see this.
- Started outside Steam's launcher: Steam can start another installed edition of the game (here the
  PCR edition, whose install script rewrites the registry path the launcher uses). Start
  `FalloutNV.exe` directly with a `steam_appid.txt` containing `22380` next to it.
- FNVVR needs the 32-bit `openvr_api.dll` next to `FalloutNV.exe` (shipped in FNVVR releases; for own
  builds it can be taken from SteamVR's `bin\win32`).

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

## Recommended graphics mods (the tested setup)

This fork recommends two graphics mods and **always tests with them installed**, so test results match
what is played. Both are installed identically on every PC of a co-op session.

| Mod | Version | Source |
|---|---|---|
| NMC's Texture Pack, **Medium** (both parts, the "for NMM" archives) | as on Nexus, 2026-11 | https://www.nexusmods.com/newvegas/mods/43135 |
| Desert Natural Weathers | 7.9.2 | https://www.nexusmods.com/newvegas/mods/75437 |

- **NMC's Texture Pack** is loose files only (textures and a few meshes, about 5 GB); copy both parts
  completely into `Data`. No plugin.
- **Desert Natural Weathers** brings `DNWeathers.esp` with its archive, sky meshes and settings under
  `Data/config/HDRWeatherAndColor/` (depth of field and saturation effects off). Activate
  `DNWeathers.esp` and put it **last** in the load order (newest file date, see "Load order on several
  PCs").
- `Fallout.ini`, section `[Archive]`: `bInvalidateOlderFiles=1`, so loose files win over the game's
  archives.
- Co-op: True Co-op compares plugins and archives (so it sees `DNWeathers.esp`), not loose textures;
  install both mods the same way on every PC anyway. True Co-op does not synchronise the weather, so
  two players can see different weather.
- Memory: the game stays a 32-bit process; the 4 GB patch (see "Game") is required. A measured
  comparison with and without the texture pack (address space, frame time in VR) is still open.
- Not part of the setup: New Vegas Reloaded and ReShade presets (too much for this setup, and untested
  with FNVVR).

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

## Load order on several PCs

- New Vegas orders plugins (`.esm`/`.esp`) by the **files' modification dates**; `plugins.txt` only says
  which are active. True Co-op refuses a friend whose order differs from the host's ("the load order is
  different from plugin N").
- Copying a game folder or extracting an archive can change those dates: zip stores them with 2 s
  precision, and a fresh install can give several DLC files the same second. Equal dates make the order
  arbitrary, so two PCs with identical files can still load them in a different order.
- Install scripts therefore set the plugins' dates explicitly after copying: strictly increasing in the
  host's order (one minute apart is enough), on every PC, with the game closed (a running game locks the
  files). Check the result in `NVCoop.log` ("Load order (N): ...") on both sides.
- Without that, True Co-op offers to fix a friend's order when joining ("Mods: asking whether to fix
  ours"); it keeps a backup of the old state and asks for a restart.

## Saves, characters and settings in co-op

Changes of FNVVR-Coop-Compat (reviving and dragging with gamepads and VR controllers, dragging at arm's length,
Quick start, menus) and their settings are described in
[FNVVR-Coop-Compat docs/behaviour.md](https://github.com/joschi127/FNVVR-Coop-Compat/blob/vr-compat/docs/behaviour.md).

How True Co-op 0.1.11 handles saves and characters, read from its source (`src/linked_saves.cpp`,
`src/main.cpp`, CHANGELOG); not all of it is tested yet:

- **Each player keeps their own saves and their own character** (level, XP, perks, inventory). The
  host plays in a normal save; friends join the host's running game. While joined, the host's world
  counts: quests, world state and fast travel points are matched to the host's.
- **Linked saves:** when the host saves during a session, every friend's game saves too, under a name
  linked to the host's save. When the host later loads that save, each friend's game loads its
  matching save.
- **Joining without a matching save** (for example after the host played alone and saved since): the
  friend's load menu opens with "pick the character to bring into the host's game". The friend picks
  one of their own saves, normally their newest, and joins with that character as saved. With no saves
  at all, the friend starts a new game and joins once out of Doc Mitchell's house.
- **Host loads a save the friend has no match for** during a session: the friend keeps their current
  character ("No save matching the host's here: keeping your character").
- So the host can play the same save alone in between. A friend who was not there joins with their
  last character: they get the host's world, but no XP or items from sessions they missed.
- **Co-op balanced and friendly fire** are per-PC settings for all games (`Data/Config/NVCoop.ini`,
  `bCoopBalanced`, `bFriendlyFire`, or the MCM page), not stored in saves; both are off by default and
  can be changed any time. In a session the host's settings count for everyone. A new game asks once
  and its answer changes the PC-wide setting. Co-op balanced also applies when playing alone (more
  damage to the player, less XP). Friendly fire off means players cannot hurt each other.
- The co-op MCM page cannot be operated in VR; change these values in the INI (re-read every 2 s).

## VR runtime

- FNVVR needs **OpenVR (SteamVR)**. With a Pico 4 over Virtual Desktop, SteamVR is the runtime (not
  VDXR).
- Headset-free runs use FNVVR's emulator backend or SteamVR's null driver (ADR 0002).

## Known problems

- **SteamVR left on its null driver.** SteamVR's null driver (a virtual headset for headset-free tests, also
  used by other projects) is switched on in `Steam/config/steamvr.vrsettings` (`steamvr.forcedDriver = "null"`,
  `driver_null.enable = true`). As long as it is, the real headset shows nothing and FNVVR talks to the virtual
  one. Check it before playing or a headset session: `python -I tools/fnvtest/steamvr.py` in
  [FNVVR-Test-Bridge](https://github.com/joschi127/FNVVR-Test-Bridge) warns (it only reads). Switch it back in
  SteamVR's settings or restore the file from a backup made before the change. FNVVR's emulator backend does
  not use SteamVR and is not affected.
