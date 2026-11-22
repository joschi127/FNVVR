# HUD tiles anchored in the world

How another mod gets a text tile of the HUD shown at a place in the world in VR, such as a name above a
character. FNVVR draws such a tile in each eye at its place; without FNVVR the game ignores the extra values,
so the mod keeps working unchanged in the flat game. Decision: [ADR 0003](adr/0003-coop-markers-in-vr.md).

## For the other mod

Put a text tile directly into `HUDMainMenu` (as usual, for example with JIP LN NVSE's `InjectUIXML`) and set
these traits, which the game itself does not use:

| Trait | Value |
|---|---|
| `user5` | `1` while the tile should be shown at its place, `0` otherwise |
| `user2`, `user3`, `user4` | the anchor's position in the game world (X east, Y north, Z up, game units) |
| `user6` | optional: `1` marks the tile as a person's, such as a friend's name; FNVVR's compass then shows a mark for it |

Text and colour come from the tile as on the flat screen: its `string` (lines broken at CR or LF), and
`systemcolor` (non-zero: the HUD's colour) or `red`, `green` and `blue`.

Set the text and the anchor also while the anchor is off the flat screen: in VR the head may look elsewhere
than the flat camera. Keep the flat traits (`visible`, `x`, `y`, `user0`, ...) as they are for the flat game;
FNVVR hides every tile with `user5` set from all its interface passes, whatever `visible` says.

Example in a script (for a tile `HUDMainMenu/MyMark`):

```
SetUIFloat "HUDMainMenu/MyMark/user2" fX
SetUIFloat "HUDMainMenu/MyMark/user3" fY
SetUIFloat "HUDMainMenu/MyMark/user4" fZ
SetUIFloat "HUDMainMenu/MyMark/user5" 1
```

## What FNVVR does

- Reads the HUD's top-level tiles once per frame on the game's main thread; up to 9 anchored tiles are shown.
- Draws each tile's text with the game's font on its own panel at the anchor, facing the head
  (`d3d9.vrWorldTile*` in `dxvk.conf`). The panel has a fixed size in the world, so it grows and shrinks with
  the person it stands over; beyond 20 m it stands at 20 m in the anchor's direction and gets no smaller in the
  view. The panels have no depth test: like the flat HUD, a tile shows through walls.
- A person's tile (`user6` = 1) is drawn in the friends' colour (`d3d9.vrFriendColour`, a dusty green, the
  same as their mark on the compass); in a colour of its own (a downed friend's red) it is drawn in
  `d3d9.vrFriendAlertColour`. A last line that is only `v` (the flat screen's pointer under a name) is
  replaced by the friend's mark of the compass.
- Shows them while the HUD is shown in the world; hides them in menus.

Screen-fixed tiles of other mods (a chat line, a progress text) can be kept on the HUD panel in front of the
head and off the hand panels with `d3d9.vrHudHeadTiles` (a list of name prefixes).

## Users

- [FNVVR-Coop-Compat](https://github.com/joschi127/FNVVR-Coop-Compat) sets the traits on New Vegas True
  Co-op's name plates (`NVCoopMark0..3`, marked as a person's) and chat above heads (`NVCoopAbove0..4`), see its
  [behaviour.md](https://github.com/joschi127/FNVVR-Coop-Compat/blob/vr-compat/docs/behaviour.md).
