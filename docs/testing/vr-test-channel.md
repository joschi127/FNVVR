# VR test channel

Lets test tools drive FNVVR's VR input and look at what it shows, without a person in the headset. Part
of the test split in [ADR 0007](../adr/0007-where-test-automation-lives.md): the VR side lives here in
FNVVR, the game side (console commands, values) in
[FNVVR-Test-Bridge](https://github.com/joschi127/FNVVR-Test-Bridge), whose Python tools speak both
channels. Each channel works on its own: this one needs neither xNVSE nor the bridge nor the co-op mod.
With a person in the headset, the tools run a guided session (HUD steps, questions, the hands-up signal
counted here): see FNVVR-Test-Bridge's [guided headset sessions](https://github.com/joschi127/FNVVR-Test-Bridge/blob/main/docs/headset-sessions.md).

## Switching it on

```
d3d9.vrTestChannel = True            # dxvk.conf; default False
d3d9.vrTestPipeName = FNVVR-Test-VR  # optional
```

FNVVR then puts a test layer in front of the real backend (emulator or OpenVR). With no requests it only
passes everything through. The DXVK log names the pipe (`VR: Test channel listening on ...`). Leave it off
on while playing if convenient: idle it costs no measurable frame time (measured 2026-11-22); switch it off
for performance measurements of other changes.

It works with every backend:

- **Emulator**: the usual case for automated tests. With the test channel on, the emulator's controllers drive
  the game as a headset's do (actions, sticks, the laser on the menu panel); without it the emulator keeps its
  keyboard gamepad. Set `d3d9.vrEmulatorRunWithoutFocus = True` as well, or the game stalls whenever another
  window has the focus.

  What the screen shows in the emulator: the game's window shows a flat view of the world, and the preview
  window ("VR preview") the two eye images FNVVR hands to the VR runtime. In VR the menus, the Pip-Boy on a
  panel, the HUD and the lamp bar are not drawn into those images but handed over as separate panels, which a
  real runtime (SteamVR) composites into the headset's view. The emulator has no compositor of its own, so its
  preview window draws the panels and the lasers itself (`d3d9.vrPreviewPanels`, default on); the game's
  window shows neither. `capture panel <panel> <file>` gives a panel's own image, `capture eyes` the eyes
  without panels, both at full resolution; a screenshot of the preview window shows everything together, as
  sharp as `d3d9.vrPreviewWidth` (default 1000 pixels for both eyes) makes it.
- **OpenVR with SteamVR's null driver**: the real OpenVR path without a headset.
- **OpenVR with a headset**: allowed, for example to press actions while a person watches. A set head
  pose makes the image disagree with the real head, so leave the head alone there.

## Protocol

Named pipe `\\.\pipe\FNVVR-Test-VR`. One tool at a time, only the Windows user running the game, no
remote clients. One request line in (`\n`), answer lines back; the last answer line is `ok ...` or
`err ...`. Requests are handled on the render thread when the next frame polls its input. A request
without an answer after 60 seconds gets an `err` (split longer waits).

Poses are `<x> <y> <z> <yaw> <pitch> [roll]`: metres in tracking space (+Y up, -Z forward) and degrees
(yaw turns left, pitch looks up, roll tilts left).

| Request | Answer | What it does |
|---|---|---|
| `ping` | `ok pong fnvvr-test-vr 1` | |
| `memory` | `ok vfree=<MB> vlargest=<MB> vused=<MB> vtotal=<MB> private=<MB> workingset=<MB>` | The game process's address space (free, largest free block, used, total) and committed / resident memory; answered on the pipe's thread, so also during loading screens |
| `state` | `ok frame=<n> backend=<name> context=<game\|menu> panels=<shown> head=<real\|set> left=... right=... move=... turn=... actions=<held> signal=<n\|off>` | `frame` counts VR frames since start (during loading screens and movies, each present counts as a frame) |
| `head <pose>` | `ok` | Sets the head until released; the eyes keep their place relative to the head |
| `hand left\|right <pose>` | `ok` | Sets a controller's grip and aim pose until released |
| `release head\|left\|right\|move\|turn\|all` | `ok` | Gives it back to the real backend (`all` also lets go of held actions) |
| `press <action>` | `ok` | Action down for 4 frames, so menus see it |
| `hold <action>` / `up <action>` | `ok` | Action down until `up` |
| `stick move\|turn <x> <y>` | `ok` | Replaces the stick (-1..1, +X right, +Y forward) until released |
| `aim left\|right <panel> <u> <v>` | `ok target <x> <y> <z>` | Sets the hand so its laser points at that point of the panel (u: 0 left .. 1 right, v: 0 top .. 1 bottom); the hand stays where it is |
| `wait <frames>` | `ok frame=<n>` | Answers after that many frames |
| `waitframe <n>` | `ok frame=<n>` | Answers once frame `n` is reached |
| `capture eyes <prefix>` | `ok <prefix>-left.png <prefix>-right.png` | Both eye images of the next frame, full resolution |
| `capture panel <panel> <file>` | `ok <file>` | The panel's image the next time it is submitted (at once for panels drawn from system memory) |
| `pipboy` | `ok state=<closed\|changing\|open> looking=<0\|1> screen <f> <u> <r> facing <f> <u> <r>` | Where the Pip-Boy's screen is on the left controller (metres forward, up and right of it, and the way it faces) and whether the wrist is looked at; `screen unknown` before the game placed it |
| `pipboy open` / `pipboy close` | `ok asked; ...` | Opens it the way looking at the wrist does (only when the game could open it then), or closes it as B or the close sign do (also on the panel); `pipboy` shows the result a few frames later |
| `signal on` / `signal off` | `ok signal=<n\|off>` | Counts the person's "done" signal from now on: both hands held above the head for `d3d9.vrTestSignalHold` seconds (default 1), once per raise; `state` shows `signal=<n>`. Off by default and after each start, in memory only |

Actions are FNVVR's named actions, not controller buttons (bindings are the runtime's business):
`attack twohandgrip activate jump reload sneak pausehold vats pause grab wait magazine menuselect
menuback menualternate menuoption menuprevious menunext menulefttrigger menurighttrigger`. Actions set by
the channel are added to the real controllers' (a real button still works); poses and sticks that are set
replace the real ones.

Panels: `menu hudhead hudwrist hudprompt hudcharge hudammo quickwheel terminal caravanvalues pipboyhint
pipboylampbar`. The Pip-Boy on a panel ([ADR 0006](../adr/0006-pipboy-on-a-panel.md)) is shown on `menu`.

Captures cost one stalled frame each (the image is copied back from the GPU); without a capture request
nothing is copied. The rows go from the GPU's staging buffer straight into the file, so the game's address
space only holds that buffer (the size of one image, about 9 MB at 1440x1600) for the moment of the
capture. The PNG is stored uncompressed. Floating-point images are converted from linear to sRGB. Paths
may contain spaces; the folder has to exist.

A request the pipe gave up on (after 60 seconds) is dropped by the render thread too, so a long `wait`
does not block later requests.

## Frame numbers across both channels

The bridge counts game frames on the game side, this channel counts VR frames; both report `frame=<n>`.
They start counting at different moments, so the Python tools in FNVVR-Test-Bridge measure the fixed
offset once (to about one frame) and offer one frame number for both.

## Code

`src/d3d9/vr/vr_test_*`: commands and input merging (`vr_test_commands`, `vr_test_input`, unit tested in
`tests/test_vr_test_channel.cpp`), the pipe (`vr_test_pipe`), GPU read-back (`vr_test_capture`), PNG files
(`vr_test_png`) and the layer in front of the backend (`vr_test_backend`), created in `vr_system.cpp`.
