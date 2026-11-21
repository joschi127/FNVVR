# 0004: Try Tale of Two Realities (TTWVR) as an alternative before committing to FNVVR + True Co-op

- Status: Proposed
- Date: 2026-11-18
- Decided by: open (the maintainer; agreed to try both)

## Context

The goal of FNVVR (this repository) is playing *Fallout: New Vegas* in VR together with friends. The plan so far
is FNVVR plus the separate co-op mod New Vegas True Co-op ([ADR 0001](0001-two-separate-mods-that-cooperate.md)),
which needs compatibility work in one or both mods ([conflicts.md](../analysis/conflicts.md)).

A second VR mod appeared in November 2026: **Tale of Two Realities**, also called TTWVR, by
KUPOkinz. What is known so far comes from public third-party projects built on it and from the
maintainer's reading of its Discord server; none of it is verified first-hand yet:

- VR for *Tale of Two Wastelands* (Fallout 3 and New Vegas as one game), so it needs both games and
  the TTW installation. Version 1.0.0 on 2026-11-08, 1.1.7 a week later.
- Reported to be an xNVSE plugin with its own renderer (DLSS/FSR, OpenXR).
- **Built-in multiplayer**: competitive modes (free-for-all, team deathmatch, server browser, bots)
  and **drop-in co-op** in the host's world, with co-op saves kept apart from the normal ones;
  players report joining other people's worlds, and other players can affect the host's NPCs.
- Its source code is not public (no repository by the author; third-party projects pin exact DLL
  hashes). On its Discord (2026-11-12) the author answered a question about publishing the source
  on GitHub with "after i am finished with it myself, yes". A permission by the author ("anyone may
  do anything with the mod as long as nobody posts it as their own work") is reported by one of
  the third-party projects but not seen by us.
- The release package of version 1.1.7 (read, not run) holds the plugin DLL, its own DXVK 3.1.1
  build, OpenVR and OpenXR support, DLSS/FSR, local speech recognition, data tables and meshes; no
  source. Its log messages describe campaign co-op: the host's quest stages, objectives, variables
  and globals are sent to and kept in sync with the guests, guests' characters are saved with the
  host's save, and the network layer has LAN, an internet server list and NAT traversal.

If TTWVR's co-op covers what we want, it could replace most of the work planned for FNVVR. If it does
not, it is still a useful reference for VR and networking in the same engine.

## Decision (proposed)

Try both before investing in compatibility fixes:

1. FNVVR alone, then FNVVR with True Co-op, as planned.
2. TTWVR with its co-op mode, on the same machine and headset.
3. Compare against the goal: campaign co-op (quests, NPCs and world in sync, host leads), whether
   every player needs VR, connection in a home network, stability over long sessions, the headset
   setup (Pico 4 over Virtual Desktop), effort to install and keep running, and how far problems
   can be fixed by us (open source vs. closed binary).
4. Read TTWVR's source once the author publishes it (announced for "after i am finished with it");
   until then only what its release package contains. No disassembling or decompiling of its DLL
   without the author's permission confirmed first-hand; any such material stays outside git
   (guidelines, "Data hygiene").

The outcome is a new ADR that either confirms ADR 0001's path, switches to TTWVR, or keeps both.

## Consequences

- TTWVR needs Fallout 3 (with its DLC) and a TTW installation in addition to New Vegas; that is
  extra setup, kept separate from the FNVVR test install and its backups.
- Compatibility work on FNVVR + True Co-op beyond the planned first tests waits for the comparison.
- If TTWVR is chosen, FNVVR's role shrinks to notes and tools; if FNVVR + True Co-op is chosen,
  TTWVR serves as a reference for behaviour (no code is taken from it).
