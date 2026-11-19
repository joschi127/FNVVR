# 0005: A small public fork of the co-op mod for VR and gamepad fixes (FNVVR-Coop-Compat)

- Status: Accepted
- Date: 2026-11-19
- Decided by: the maintainer

## Context

[ADR 0001](0001-two-separate-mods-that-cooperate.md) keeps FNVVR and New Vegas True Co-op separate and puts
co-op-side fixes into the co-op mod, either upstream or in a fork of our own. Until now there was no fork: the
co-op source was only cloned for reading.

The first co-op sessions in VR (host in VR, `fakefriend` as second player) found a co-op-side blocker: a VR player
cannot revive a downed friend, drag them or give them a Stimpak. Co-op checks these controls with xNVSE's
`IsControlPressed`, which reads only the keyboard and the mouse; FNVVR hands the VR controllers to the game as a
gamepad. The prompts also name the keyboard keys ("Hold E", "Z to drag"). The same holds for anyone playing flat with
a gamepad, so the fix is not VR-specific.

FNVVR's upstream author confirmed the split in df4l/FNVVR#16: co-op features do not go into FNVVR; generic problems
on the FNVVR side may be raised as issues, fixes must leave FNVVR unchanged without the other mod.

The co-op licence allows published modified versions only with credit, a clear "modified, not the official mod"
notice, without using the name "New Vegas Co-op" as their own name, with the licence included and with their full
source public under the same licence.

## Decision

- Keep a small public fork of the co-op mod: [joschi127/FNVVR-Coop-Compat](https://github.com/joschi127/FNVVR-Coop-Compat),
  forked from Zyrcxies/NewVegasCoop. Remotes and branches as in this repository: `origin` = fork, `upstream` =
  original (fetch only), `main` mirrors upstream, work happens on `vr-compat`.
- It holds only changes needed for FNVVR + co-op and fixes that help every player; no VR code. Its own
  `docs/guidelines.md` carries the licence rules; its version string is upstream's plus `+fnvvr.<n>`.
- First change: co-op's own controls (revive, drag, give a Stimpak) also read the gamepad state the game polls,
  and the prompts name the key or gamepad button actually bound.
- Whether and when changes are offered to the co-op author is the maintainer's decision.

## Consequences

- Both players need the identical modified `NVCoop.dll` (co-op compares NVSE plugins between players); the
  install packages for test PCs carry it once it has passed a game test.
- Upstream co-op releases arrive as one source drop per version and have to be merged into `vr-compat`; keep the
  fork's changes few and marked (`FNVVR-Coop-Compat:` comments) so this stays easy.
- The fork depends on facts about the game's input object (gamepad state and control bindings); they are the same
  facts FNVVR uses and are documented in both repositories.
- Code never moves from the fork into FNVVR (licence); FNVVR code may move into the fork with its zlib notice.
