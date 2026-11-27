# 0008: A compass along the top of the view, in the look of the game's

- Status: Accepted
- Date: 2026-11-22
- Decided by: the maintainer

## Context

In FNVVR the game's compass is only on the back of the right hand (a crop of the HUD's HitPoints group), so
the player has to look at the hand to find a direction or the quest's target. The maintainer wants a compass
at the top edge of the view: marks with the directions N, NE, E, SE, S, SW, W, NW and a marker for the
current quest's target.

Options considered:

- A. Show the game's compass (the crop the hand panel already has) on a panel at the top of the view. Little
  work and every marker of the game (enemies, companions, map markers). But the game turns it with the
  player's heading at game time, a frame or two behind the head, and not at all where the head does not turn
  the player (V.A.T.S., furniture, dialogues).
- B. Draw a compass of FNVVR's own on a panel fixed to the head, from the head's heading when the frame is
  drawn, in the look of the game's compass.

## Decision

B, drawn with the game's own pictures so that it looks and feels like the game's compass
(`vr_compass_layout.cpp`, `vr_compass.cpp`):

- The game's direction strip (`glow_hud_comp_direction_strip.dds`, with its marks and the English labels
  N to NW), tinted in the HUD's colour, about 120 degrees wide as on the game's HUD, fading out towards its
  ends, on a light dark band for contrast against the sky. Its middle is the heading, as on the game's HUD.
- Below it, the game's markers, told apart by shape and colour: the active quest objective's targets as the
  game's filled arrow (`glow_hud_compass_objective_marker.dds`); a target in another place points at the
  door that leads there, as on the game's compass. The marker the player placed on the map as the game's
  hollow arrow (`glow_hud_compass_pc_marker.dds`). Friends in co-op as the game's mark of a person
  (`glow_hud_tick_mark.dds`) in a phosphor green, the colour of their name plates, with their initial; friends come from
  HUD tiles marked as a person's ([docs/hud-world-tiles.md](../hud-world-tiles.md)), so the compass works
  the same without the co-op mod. Markers outside the strip stay at its nearer end, fainter.
- In an interior the cell's own north is used, as on the game's compass.
- The compass on the hand stays.
- **Requirement: a good default** for the strip's size and place (width and top edge shares, distance, the
  band behind it), settled together in guided headset sessions, so that other players start with a compass
  that fits without changing `dxvk.conf`. Guided session h05 (2026-11-22): size and height fitted, the quest
  marker fitted, the markers' gap fitted with the band and was wanted nearer without it (-18). The maintainer
  then set the defaults: no band (with its nearer gap) and a 1.5 times wider strip (0.68 of the field of
  view); to be confirmed in the next session. After playing with it (2026-11-26) the maintainer asked for a
  smaller compass, about 75 to 80 percent: the default width is now 0.53 of the field of view (78 percent of
  0.68). Markers and gaps are drawn in the strip's image, so they shrink by the same factor.
- Lighter look (2026-11-27): in the headset the game's strip looked heavy, beside the player's health bar
  (the game's thin HP meter, below the compass at its right end) even more so. The maintainer chose a strip
  drawn in the look of the HP meter by default: a thin line with a faint glow, tick marks rising from it
  every 5 degrees (taller every 15) and the labels in the line, in the game's font and the HUD's colour. The
  strip is also flatter (0.7 of its height) with smaller markers (0.65); the health bar thins with the strip.
  The game's picture stays an option (`d3d9.vrCompassGameStrip`), as do the strip's height and the markers'
  size (`d3d9.vrCompassStripHeight`, `d3d9.vrCompassMarkerSize`). To be confirmed in a headset session.
- Size and place follow the field of view the headset reports (the maintainer often plays with a field of
  view reduced in Virtual Desktop): the width is a share of the horizontal field of view, the top edge a share
  of its upper half. `dxvk.conf` overrides them in degrees (`auto` or not set: from the field of view); the dark
  band behind the strip can be switched off and its opacity set. Colours too are `dxvk.conf` options
  (`d3d9.vrCompass*`); on by default.
- Requested and rejected: labels set in `dxvk.conf` (for example German N, NO, O, ...). The game's strip has
  its labels drawn in; the maintainer chose its look over changeable labels (the own strip, now the default,
  keeps the same English labels). Without the strip picture the
  compass draws marks and English labels of its own in the same style.

Where the game keeps the data (the quest targets and the placed marker in PlayerCharacter, the interior's
north in ExtraNorthRotation) is listed in `vr_game_addresses.h`, from the public plugin headers of xNVSE and
JIP LN NVSE.

## Consequences

- The compass turns with the head at once and is right in V.A.T.S., on furniture and in dialogues.
- It shows fewer markers than the game's (no enemies, map markers or companions of the player).
- The sign of an interior's north rotation is taken from public plugin sources and has to be checked against
  the game's compass in an interior.
- Test: emulator runs that turn the head to known headings, with a quest target, a placed marker and a
  fakefriend; captures compared with the compass on the hand.
