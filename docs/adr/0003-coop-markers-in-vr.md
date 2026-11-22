# 0003: Co-op name markers in VR: drawn by FNVVR in the direction of the friend

- Status: Accepted (2026-11-22: C2 as a general interface, see "Decision 2026-11-22" below)
- Date: 2026-11-16, updated 2026-11-22
- Decided by: the maintainer

## Context

New Vegas True Co-op shows its overlays through the game's own HUD: it injects tiles into
`HUDMainMenu` and sets their values from scripts. Two kinds
([conflicts.md](../analysis/conflicts.md) 3.2, [nvcoop-codebase.md](../analysis/nvcoop-codebase.md)
section 4):

- **Screen-fixed**: chat line, talk log, revive progress, corner messages, message boxes.
- **World-anchored**: name plates `NVCoopMark0..3` (player name, or "DOWNED" with a timer) and chat
  above heads `NVCoopAbove0..4`. Each frame the co-op mod projects the friend's character (150 units
  above it) with JohnnyGuitar's `WorldToScreen`, which uses the game's flat camera, and writes the
  result as screen fractions into the tile.

In FNVVR the HUD is drawn several times per frame into separate panels: a head panel (messages,
objectives, subtitles) and hand panels (health, compass, AP, ammunition). For each pass FNVVR culls
the HUD groups that do not belong to that panel; it knows the game's 23 HUD groups and yUI's tiles by
name (`vr_hud_layers.cpp`). The co-op tiles are in neither list, so they are never culled and likely
show on every panel. World-anchored markers end up on the head-locked panel at flat-camera
positions: wrong depth (1 m instead of the friend's distance), probably wrong direction near the
edges, missing when the friend is outside the flat camera's view.

Only FNVVR renders per eye, so only FNVVR can place a marker in the world. The co-op mod cannot
draw it correctly on its own.

Each remote player is a real actor in the host's and the guests' game ("stand-in"): a runtime copy of
the NPC base form `0x00120EB1` (`FalloutNV.esm`) named after the player, moved from network data,
with a compass and map marker (co-op source, `game/game.cpp` `SpawnStandIn`, `game/standins.cpp`).
Its position, head node and name are in game memory like those of any NPC, so FNVVR can read them
the same way it reads the player.

Options considered:

- A. Turn the markers off (`bFriendMarkers=0`, co-op MCM) and rely on compass and map. No code;
  loses the "where is my friend" cue in view.
- B. FNVVR assigns the co-op screen-fixed tiles to the head panel and hides the world-anchored ones
  (as it does for yUI). FNVVR only, small.
- C. FNVVR draws its own marker per friend, placed from the friend's world position.
  - C1. FNVVR finds the friends itself (no co-op change).
  - C2. The co-op mod tells FNVVR which marker belongs to which position (small co-op change).
- D. Co-op only, e.g. text hints ("friend 30 m north"). Not a real replacement.

A marker does not need the correct depth. The correct **direction** from the head already tells the
player where the friend is; depth can come later.

## Decision (proposed)

FNVVR draws co-op markers itself (C). The co-op mod's flat markers are hidden in VR.

1. **Interim** (A + B): `bFriendMarkers=0` in the co-op setup; FNVVR recognises the co-op tiles by
   name (`NVCoop*`), shows the screen-fixed ones on the head panel only and culls the world-anchored
   ones in every pass.
2. **Prototype, C1**: FNVVR finds the stand-ins without help from the co-op mod and shows the
   friend's name in a marker that is world-locked (stays put when the head turns), at a fixed
   distance from the head in the direction of the friend's head, with a fixed angular size. Off by
   default behind a `dxvk.conf` option. Shows names only: which "DOWNED" or chat text belongs to
   which friend cannot be told reliably without the co-op mod's help.
3. **Target, C2**: a small interface. The co-op mod writes, per marker tile, the world position of
   the anchor into tile values that the flat game does not read (for example three float user
   values; tile values are floats, so a 32-bit form ID would not fit exactly). FNVVR reads the tile's
   text and position and draws the marker; the co-op mod keeps working unchanged without FNVVR.
   Then "DOWNED" and chat above heads work as well, and FNVVR no longer depends on co-op internals.
   The co-op part goes into a local branch of the co-op mod or is offered to its author (the
   maintainer's decision); the interface is documented in both repositories
   ([guidelines](../guidelines.md), "The two mods").

The correct depth (marker at the friend, scaled with distance) can become an option later; the
interface does not change for it.

## Test results (2026-11-19)

First sessions with the host in VR (Pico 4 over Virtual Desktop, SteamVR) and `fakefriend` as second player,
without any marker code in FNVVR:

- The co-op name plate is visible and points roughly in the direction of the friend; it does not jitter with head
  movement. Close up it sits well; at a larger distance it is noticeably off, but still usable.
- So the flat-camera placement is good enough to start playing. This ADR stays Proposed and is taken up again only
  if the placement bothers in real sessions.
- FNVVR's upstream author does not want co-op-specific code in FNVVR (df4l/FNVVR#16). If markers need work, C2
  (co-op writes the position, in [FNVVR-Coop-Compat](0005-small-coop-fork-for-vr.md)) is preferred over C1.

## Unknowns

- How FNVVR tells a stand-in from other actors in C1 (base form copied from `0x00120EB1` at run
  time, the actor's name, the compass/map marker flag); how stable this is across co-op versions,
  which arrive as whole source drops.
- Whether the free tile values are really free (co-op uses `user0`/`user1` for the screen position)
  and whether FNVVR can read them at render time without a race with the co-op scripts.
- How to draw: an OpenVR overlay per marker (like FNVVR's panels) or a quad rendered into both eye
  images (allows occlusion and per-eye correct placement); readability at distance.
- Whether to show an arrow at the edge of the head panel for friends behind the player.
- Whether df4l's FNVVR wants co-op-specific code; it may stay in FNVVR.

## Consequences

- FNVVR gets a small co-op compatibility module (tile names, stand-in detection, marker drawing),
  off by default, kept separate so it rebases easily onto upstream.
- The prototype depends on co-op internals and has to be checked after each co-op update; the C2
  interface removes that dependency.
- C2 needs a change in the co-op mod and therefore a decision on FNVVR-Coop-Compat or contact with its
  author.
- Test: in the harness of [ADR 0002](0002-headless-ai-testing-first.md) with `fakefriend` standing in
  view and behind the player; captures must show the marker over or in the direction of the friend.

## Decision 2026-11-22

The first runs of co-op in the emulator (fakefriend, run c34) showed the flat placement clearly off: at 3 and
8 m straight ahead the plate stood about a body width beside the friend, in both eyes at the same image place
(no depth), and it is missing whenever the friend is outside the flat camera's view. The maintainer decided:

- **Way: C2, as a general interface** ([docs/hud-world-tiles.md](../hud-world-tiles.md)). Any mod may anchor a
  HUD tile in the world with traits the game does not use (`user5` = 1, anchor in `user2` to `user4`,
  `user6` = 1 for a person). FNVVR draws such tiles at their place and hides them from its interface passes;
  FNVVR holds no co-op code. FNVVR-Coop-Compat sets the traits on the co-op name plates and chat above heads,
  also off the flat screen.
- **Placement**: at the anchor up to 20 m, farther at 20 m in its direction (so the eyes do not converge on
  something far behind nearer scenery); the same angular size up to 5 m, shrinking to about 60 % at 40 m as a
  cue for distance. All values in `dxvk.conf` (`d3d9.vrWorldTile*`). No occlusion (panels have no depth test);
  drawing into the eye images with a depth test was judged too much effort for now.
- **Scope**: name plates, "DOWNED" and chat above heads; the co-op mod's screen-fixed tiles (chat line,
  revive text, talk list) can be kept on the head panel with `d3d9.vrHudHeadTiles`.
- **Look**: like the game's (its font and the tile's colour), as on the flat screen.

Updated after the guided headset session h05 (2026-11-22): the plates were too small at 3 and 8 m and readable
at 20 m. The maintainer decided: a fixed size in the world, so a plate grows and shrinks with the friend's body,
no smaller beyond 20 m (`d3d9.vrWorldTileWidth`, a bit smaller than the old size at 20 m); the `v` under the
name becomes the compass's friend mark; plate and compass mark share one colour (`d3d9.vrFriendColour`). After
emulator screenshots the maintainer chose a bright green like an old phosphor screen, the counterpart of the
Pip-Boy's amber (a dusty green was too dark to read), and a red like the Pip-Boy's lamps for a downed friend
(`d3d9.vrFriendAlertColour`), then picked on a colour chart on the game's sky: green A6E585 (warmer and paler than a
pure phosphor green, and light enough to stand out against the sky), red E0705A; the plate's image stands on the anchor, so the larger plate keeps its distance
above the head.
- Friends also get a mark on the compass along the top of the view ([ADR 0008](0008-compass-at-the-top-of-the-view.md)).

