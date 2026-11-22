# 0007: Where test automation lives

- Status: Accepted
- Date: 2026-11-22
- Decided by: the maintainer

## Context

[ADR 0002](0002-headless-ai-testing-first.md) asks for tests an agent can run without a headset and without
the maintainer. The first concrete need (2026-11-22): an agent has to run console commands in the running
game itself (add items for a Pip-Boy test, remove them again) and read values and console text back,
instead of asking the maintainer to type them.

Options considered for the game-level part:

- **The co-op mod's lab channel** (a named pipe that runs console lines, compiled into every build of the
  co-op mod, off by default). Works today, but only while the co-op mod is loaded, its reply does not carry
  the console text, and extending it means code under the co-op mod's licence (derived works must use the
  same licence).
- **Inside FNVVR.** FNVVR is a `d3d9.dll` loaded through DXVK, without any dependency on xNVSE; running
  console lines would need its own game addresses for the console and script compiler. It would also be a
  game-level feature in a VR mod whose upstream scope is VR adaptation only (no co-op logic, nothing
  invasive, no new dependencies), and it would widen the fork's distance from upstream.
- **A small xNVSE plugin of our own.** Uses xNVSE's console and script interfaces, works with and without
  FNVVR and the co-op mod, builds like other xNVSE plugins (CMake, Visual Studio, x86), own licence.

## Decision

Test automation is split by what it touches; each part lives where that knowledge already is:

| Part | Lives in | Contents |
|---|---|---|
| VR side | FNVVR (this repository) | The VR test channel ([docs/testing/vr-test-channel.md](../testing/vr-test-channel.md)): `d3d9.vrTestChannel`, off by default, a layer in front of the real backend with its own named pipe; head and hand poses, named actions, sticks, aiming at panels, waiting for frames, captures of eyes and panels. Works with the emulator, SteamVR's null driver and a headset. |
| Game side | [FNVVR-Test-Bridge](https://github.com/joschi127/FNVVR-Test-Bridge) (own repository, zlib) | xNVSE plugin: named pipe for the current Windows user on the same PC, runs console lines and `eval` expressions on the main thread, answers with the console text and `ok`/`err`. Off unless its INI says `Enabled=1`. Python tools `tools/fnvtest.py` and `tools/fnvtest/` for both channels. |
| Co-op side | Co-op mod / [FNVVR-Coop-Compat](https://github.com/joschi127/FNVVR-Coop-Compat) | `fakefriend`, fake lag, second-copy mode, co-op debug commands, as they are. |
| Orchestration | Generic parts public, machine details in the maintainer's local notes | Start a run, back up and restore, collect logs, verdict (ADR 0002). Helper scripts in Python. |

The co-op mod's lab channel is not used; it stays off (`LabChannel=0`).

Both channels are independent of each other and of the co-op mod: the VR channel needs only FNVVR, the
game channel only xNVSE and the bridge. The Python tools (in FNVVR-Test-Bridge) use whichever channels a
test needs; both channels report a frame number, and the tools measure the offset between them so a test
can use one numbering.

Requirements for FNVVR-Test-Bridge (maintainer, 2026-11-22):

- run console commands and read values back (expression results and the console's printed text);
- usable for FNVVR alone; the co-op mod is optional;
- test changes stay in memory; nothing is saved unless someone saves;
- a memory status of the game process (address space free, largest free block, used; committed and resident
  memory) on request, in both channels, also without the co-op mod;
- clean, short documentation of every implemented feature and how to use it (bridge README and protocol;
  for the VR channel `docs/testing/vr-test-channel.md`), updated in the same commit as the feature;
- **no performance cost**: with `Enabled=0` the plugin does nothing at all; enabled and idle it does one
  atomic load per frame. Whether enabled-and-idle is measurable has to be checked (frame times off against
  on). Until that is shown to be free, and in any case for real play and performance measurements, it stays
  `Enabled=0`.

## Consequences

- One more repository and DLL to build (Windows, Visual Studio) and install for test runs. The install is
  part of the test setup, never needed for play.
- FNVVR stays free of game-level test code; its own test additions stay VR-only and upstream-friendly.
- The bridge protocol is plain lines, so the same client serves a test on either PC (on another PC the
  client runs there, for example over SSH, since remote pipe clients are rejected).
- Follow-up: measure the idle cost; add waiting for states (for example "until loaded") when a test needs it.
