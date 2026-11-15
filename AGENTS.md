# AGENTS.md

Read [docs/guidelines.md](docs/guidelines.md) before working in this repository. It covers the two
mods and how they relate, game versions, the build, what the tests do and do not cover, test-state
backups, data hygiene and the git workflow. Current state and to-dos are in the git-ignored
`local/NOTES.md`; read it first if present.

Rules that always apply:
- Ask the maintainer before side effects: installs, game runs, pushes, PRs, deletes, anything in
  the game folder, `Documents\My Games\FalloutNV`, Steam or mod-manager settings.
- FNVVR and the co-op mod stay separate mods; never copy co-op code into this repository.
- Never commit game files, saves, INIs, logs, reverse-engineering artifacts, local paths or tokens.
- No AI attribution in commits or PR descriptions. Never contact upstream projects.
