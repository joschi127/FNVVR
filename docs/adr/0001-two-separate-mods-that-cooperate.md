# 0001: FNVVR and New Vegas True Co-op stay two separate mods

- Status: Accepted
- Date: 2026-11-15
- Decided by: the maintainer

## Context

The goal of FNVVR (joschi127/FNVVR, a fork of df4l's FNVVR) is to play *Fallout: New Vegas* in VR with FNVVR and in co-op with
"New Vegas True Co-op" at the same time, stable. The two mods are different kinds of software:

- FNVVR is a 32-bit `d3d9.dll`, a fork of DXVK with the VR layer built in, zlib licence, built with
  Meson and mingw-w64. It patches the game from inside the renderer (camera, input, HUD, game
  heap, math functions, timer).
- The co-op mod is an xNVSE plugin (`NVCoop.dll`) with its own networking (Steam and ENet), its own
  licence (modified versions must keep that licence and publish their source), built with CMake and
  Visual Studio.

Both hook the same game executable and have independent authors, release cycles and communities.
Merging them would mean one combined licence (not possible without both authors), one release
cycle, and losing the ability to take each upstream's updates.

## Decision

FNVVR and the co-op mod stay two separate mods that work together. Compatibility work is done in
each mod's own repository:

- FNVVR side: FNVVR (`joschi127/FNVVR`), changes kept small and upstream-friendly so they can be
  offered to df4l.
- Co-op side: issues to the co-op author, or our own fork (since ADR 0005: FNVVR-Coop-Compat) of
  [Zyrcxies/NewVegasCoop](https://github.com/Zyrcxies/NewVegasCoop) when changes are needed there.
  A published fork must follow its licence (credit, "modified, not the official mod", same
  licence, full source).

No code is copied from the co-op mod into FNVVR. Where the two need to know about each other, the
contract is explicit and minimal (for example detecting the other DLL, agreeing on who hooks a
shared function, or a documented exported function), and documented in both repositories.

## Consequences

- Conflicts are found by testing both mods together; the fix goes where the cause is, which may
  mean waiting for or forking the other project.
- Users install two mods; the documentation must say which versions were tested together.
- A combined test harness ([ADR 0002](0002-headless-ai-testing-first.md)) has to load both mods
  without either depending on the other.
