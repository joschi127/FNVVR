# Architecture decision records

Decisions about this fork that shape the code or the project and are hard to reverse. When to write
one: see [Architecture decision records](../guidelines.md#architecture-decision-records) in the
guidelines.

## How to add one

1. Copy [template.md](template.md) to `NNNN-short-title.md` with the next free number.
2. Fill in context, decision and consequences. Keep it short; link to code, docs, issues or commits
   instead of repeating them.
3. Status `Proposed` while it is open, `Accepted` once the maintainer agrees. An accepted ADR is not
   rewritten: a later change is a new ADR that supersedes it, and the old one gets status
   `Superseded by NNNN`.
4. Add it to the list below in the same commit.

Committed ADRs follow the data hygiene rules of the guidelines: no local paths, logs or
reverse-engineering material.

## Records

| No. | Title | Status |
|---|---|---|
| [0001](0001-two-separate-mods-that-cooperate.md) | FNVVR and New Vegas True Co-op stay two separate mods | Accepted |
| [0002](0002-headless-ai-testing-first.md) | Headless, agent-run testing first | Proposed |
| [0003](0003-coop-markers-in-vr.md) | Co-op name markers in VR: drawn by FNVVR in the direction of the friend | Proposed |
| [0004](0004-try-ttwvr-as-alternative.md) | Try Tale of Two Realities (TTWVR) as an alternative before committing to FNVVR + True Co-op | Proposed |
