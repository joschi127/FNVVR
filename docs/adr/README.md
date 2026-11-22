# Architecture decision records

Decisions about this fork that shape the code or the project and are hard to reverse. When to write
one: see [Architecture decision records](../guidelines.md#architecture-decision-records) in the
guidelines.

Decisions about the co-op side live in FNVVR-Coop-Compat:
[joschi127/FNVVR-Coop-Compat docs/adr](https://github.com/joschi127/FNVVR-Coop-Compat/blob/vr-compat/docs/adr/README.md).

## How to add one

1. Copy [template.md](template.md) to `NNNN-short-title.md` with the next free number.
2. Fill in context, decision and consequences. Keep it short; link to code, docs, issues or commits
   instead of repeating them.
3. Status `Proposed` while it is open, `Accepted` once the maintainer agrees. This is a hobby
   project, so ADRs are handled lightly: an ADR may be updated with test results, clarifications and
   small adjustments (say what changed and when). A real change of mind is a new ADR that supersedes
   the old one (`Superseded by NNNN`).
4. Add it to the list below in the same commit.

Committed ADRs follow the data hygiene rules of the guidelines: no local paths, logs or
reverse-engineering material.

## Records

| No. | Title | Status |
|---|---|---|
| [0001](0001-two-separate-mods-that-cooperate.md) | FNVVR and New Vegas True Co-op stay two separate mods | Accepted |
| [0002](0002-headless-ai-testing-first.md) | Headless, agent-run testing first | Proposed |
| [0003](0003-coop-markers-in-vr.md) | Co-op name markers in VR: drawn by FNVVR at the friend's place (general HUD tile interface) | Accepted |
| [0004](0004-try-ttwvr-as-alternative.md) | Try Tale of Two Realities (TTWVR) as an alternative before committing to FNVVR + True Co-op | Proposed |
| [0005](0005-small-coop-fork-for-vr.md) | A small public fork of the co-op mod for VR and gamepad fixes (FNVVR-Coop-Compat) | Accepted |
| [0006](0006-pipboy-on-a-panel.md) | The Pip-Boy on a panel in VR, with a lamp bar and the VR controllers' buttons | Accepted |
| [0007](0007-where-test-automation-lives.md) | Where test automation lives (VR side in FNVVR, game side in FNVVR-Test-Bridge) | Accepted |
