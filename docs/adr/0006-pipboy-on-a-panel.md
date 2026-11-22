# 0006: The Pip-Boy on a panel in VR, with a lamp bar and the VR controllers' buttons

- Status: Accepted
- Date: 2026-11-21
- Decided by: the maintainer

## Context

FNVVR shows the Pip-Boy on the left forearm, as the game's model. In the first headset sessions it was hard to read and
to operate there: the screen is small, the wrist has to be held still and turned towards the eyes, and the stick's
directions do not follow a turned wrist. Upstream offers a larger Pip-Boy (`d3d9.vrPipBoyScale`) and a turn about the
forearm (`d3d9.vrPipBoyAngle`); both helped a little.

On a gamepad the Pip-Boy pages through its main views (Stats, Items, Data) with LT and RT. With VR controllers the
triggers are the lasers' clicks in menus, and the grips press LB and RB. LT and RT are painted on the Pip-Boy arm's
texture (the gamepad variant, `textures\pipboy3000\xbox\pipboyarm01.dds`), next to the three tab knobs with their lamps;
they are not menu images.

This is a change for this fork only. It is not proposed to df4l (FNVVR's author refuses features that do not fit the
mod as bloat); whether that changes after more play is the maintainer's decision.

## Decision

**Pip-Boy panel** (Settings > VR > "Pip-Boy on panel", `d3d9.vrPipBoyPanel`, off by default):
- The Pip-Boy is still opened by looking at the wrist and pressing the button. While it is open, the texture the game
  draws its menus into is copied onto the flat menu panel the other menus use. The wrist keeps its screen.
- The panel is placed straight ahead: where the head looked before it bent down to the wrist, at the head's place.
- Only the part of the texture that holds the menus is shown, centred, with the dark rim around them that the wrist
  screen shows too (`d3d9.vrPipBoyPanelCropLeft` / `Top` / `Right` / `Bottom`). It is widened to the panel's aspect,
  so nothing is stretched. Around it a thin casing like the wrist's is drawn, as wide on every side
  (`d3d9.vrPipBoyPanelFrame`), with rounded screen corners; it is blended over the copy. The panel's own corners are
  rounded too and clear outside (`d3d9.vrPipBoyPanelCornerRadius`), so it has the shape of a device; other panels show
  only their content as well. The laser maps through the
  same crop. Tuning values in `dxvk.conf`. Large Pip-Boy text comes first: the casing stays thin. It is painted
  once, at half the panel's size (a few MB for a moment; full size would cost about four times as much and looks the
  same, the casing being a smooth shade). Checked in the emulator on ITEMS, STATS and DATA (runs c21, c22, c25); casing and list fix confirmed in the headset.
- Looking away does not close it (looking at the panel would); B closes it.
- A sign above the panel's top right corner shows "Close" and the game's B button image like the Pip-Boy's own button
  hints on the panel ("Equip A"): no box behind it, the same size (capitals 2.16 %, button 3.95 % of the panel's width)
  and the full colour of a hint that can be used. A laser on it adds a thin frame around the text and the game's focus
  sound. It is clicked with the laser as well.
- The lasers point at the panel; the laser's point becomes the point the Pip-Boy picks its tiles at
  (InterfaceManager's Pip-Boy point, as named in public layouts of the game). The triggers click. A message box over
  the Pip-Boy takes the laser as other menus do.
- **Lamp bar** ("lamp bar", German "Leiste mit Lampen") directly below the panel: the strip of the Pip-Boy arm with
  STATS, ITEMS and DATA and their three knobs, read at run time from the player's own game texture (no game asset is
  shipped), the active view's knob lit with the game's button glow, and arrow tabs with the grips' names (LG, RG) in
  place of LT and RT. The knobs are clicked with the laser to switch the view: the view is paged to a step at a time
  (LT/RT), each step pressed only once the game shows the last, as it ignores a press while it switches (headset run
  h02: STATS to DATA ended on ITEMS before; fixed, emulator run c24).

**Buttons in VR, panel or not:**
- While the Pip-Boy is open, the grips page through the main views (they press LT and RT) and the triggers press LB
  and RB unless the lasers take them. A real gamepad and other menus are unchanged.
- The Pip-Boy shows the VR controllers' buttons: menu images of LT/RT show the grips, LB/RB the triggers. The LT and RT
  painted on the arm's texture show the grips as well while VR is on, so that the wrist is right with the panel turned
  off: while the eyes are drawn, the arm's base map is swapped for a copy of the player's own gamepad texture with
  LG and RG on the tabs, and the game's own is put back right after (as FNVVR already does with the terminal's
  keyboard), so the game without VR and the gamepad keep LT and RT.
- With the panel, the larger Pip-Boy and the turned Pip-Boy are not needed: both test PCs use size 1.0 and angle 0.

## Consequences

- Several game structures are used that FNVVR did not use before: the Pip-Boy's menu texture, InterfaceManager's
  Pip-Boy point, the tile traits `filename` and `texatlas`. The facts come from public sources (xNVSE, JIP LN NVSE,
  commonlibnv, a public decompilation), recorded as facts, no code copied. Removing the copy protection of the game's
  executable to analyse it is not done.
- The grip images are loose files in `Data\textures\interface\shared\button\`, drawn by
  `src/d3d9/vr/tools/make_grip_icons.py`; the install of a test PC has to put them there.
- Each part is its own commit, so the laser or the buttons can be taken back alone.
- Tested in the headset on 2026-11-21 (runs c11): the panel is readable, the grips page, the laser clicks entries,
  B closes, the option turns the panel on and off. Open: the placement straight ahead, the border values, the
  grip images, the lamp bar, the wrist's painted buttons.

## Update 2026-11-22: the item list's lower rows

The laser could not point at the lower rows of the Pip-Boy's item list (from row 8 in the headset, from row 5
in the emulator), while the buttons below worked. Cause: `InterfaceManager::PickMenus` tests the cursor on a
rendered menu against the tile's clip rectangle and scales all four sides from units to pixels with the
window's width over 1280; on a window narrower than 4:3 (every headset eye) the top and bottom come out short.
FNVVR now gives those two multiplications the height's own scale on such windows; 4:3 and wider keep the
game's factor. Found and checked with the test channels in the emulator (run c19): all rows take the pointer,
also after scrolling, and drawing is unchanged.

