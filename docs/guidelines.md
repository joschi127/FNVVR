# Project guidelines

Guidelines for everyone working on this fork: developers and AI coding agents alike. Agent
entry files (`CLAUDE.md`, `AGENTS.md`) only point here; keep the content in this file.

Fork of [df4l/FNVVR](https://github.com/df4l/FNVVR) ("FNVVR Reborn", a VR mod for *Fallout: New
Vegas* built into a fork of DXVK). Long-term goal of this fork: FNVVR runs stable together with the
co-op mod "New Vegas True Co-op" (source:
[Zyrcxies/NewVegasCoop](https://github.com/Zyrcxies/NewVegasCoop), see
[ADR 0001](adr/0001-two-separate-mods-that-cooperate.md)). Hobby project. The upstream README and
CHANGELOG remain authoritative for everything that is not specific to this fork.

"The maintainer" below is the repository owner, who decides on side effects, pushes, releases and
contact with other projects.

## Local notes (git-ignored)

Machine-specific paths, the current state and the to-do list live outside git, under `local/`
(git-ignored as a whole). Read them first if present.

- `local/NOTES.md`: current state only. Environment, game installation, installed builds, test
  commands, open points and the to-do list. Keep it short; replace outdated statements instead of
  appending.
- `local/HISTORY.md`: older snapshots and completed steps, dated, in chronological order. Move
  sections of `NOTES.md` here when they are no longer current instead of deleting them.
- `local/README.md`: what else is stored under `local/` (runs, backups, builds, tools).
- `CLAUDE.local.md` (repo root, git-ignored) only imports `local/NOTES.md` for Claude Code; other
  tools and developers read `local/NOTES.md` directly.

The notes may contain local paths and machine details; that is why they stay out of git. Their
language is the maintainer's choice; committed files are in English.

**Keeping `local/` safe (optional):** `local/` may be its own private git repository, separate from
this one, pushed to a private remote of the maintainer's choice (for example a bare repository on a
home server). That keeps the notes available on several machines and in backups. Its remote is
recorded in `local/NOTES.md`, never in this repository.

- `local/` holds text only: notes, scripts, run records, checksum lists. Its own `.gitignore` is an
  allow-list: everything is ignored except text types (`.md`, `.txt`, `.sh`, `.ps1`, `.py`, `.tsv`,
  `.sha256`, `.patch`), and copies of user data (documents folder, settings) are excluded even if
  they are text.
- Large binaries and temporary files do not go into `local/` at all: builds, staging folders,
  downloaded packages and third-party DLLs go to `tmp/` in the repository root, which this
  repository ignores. Backups, saves, logs and crash dumps that belong to a run may stay under
  `local/` and are kept out by the allow-list.
- The same rules as for this repository apply to anything that might later be shared: tokens and
  credentials never go into either repository.
- Work directly on `main` in the `local/` repository and avoid branches where possible: commit and
  push notes as they change, so every machine sees one current state.

## The two mods

| | FNVVR (this repository) | New Vegas True Co-op |
|---|---|---|
| What | 32-bit `d3d9.dll`: DXVK fork (D3D9 to Vulkan) with the VR layer in `src/d3d9/vr/` | xNVSE plugin `NVCoop.dll`, networking over Steam or direct IP (ENet) |
| Upstream | [df4l/FNVVR](https://github.com/df4l/FNVVR) | [Zyrcxies/NewVegasCoop](https://github.com/Zyrcxies/NewVegasCoop); release on Nexus Mods (New Vegas mod 100267; that it is the same project is inferred, not yet confirmed) |
| Licence | zlib (DXVK) | Own licence: use and change freely; no reuploads of the original; modified versions must credit, say they are modified and publish their full source under the same licence |
| Build | Meson + Ninja, mingw-w64 (i686, posix threads), Linux-native | CMake presets, Visual Studio 2026, x86 |

Rules:
- The two mods stay separate ([ADR 0001](adr/0001-two-separate-mods-that-cooperate.md)). A fix
  that belongs in the co-op mod goes into the co-op mod's repository (upstream issue, or our own
  fork of it once we have one), not into FNVVR, and vice versa.
- **Never copy code from the co-op mod into FNVVR.** Its licence requires derived works to use the
  same licence, which FNVVR (zlib) cannot. Reading it to understand behaviour is fine; record what
  was learned as facts (addresses, call order, hooks used), not as copied code. Code from FNVVR may
  go into a co-op fork with the zlib notice kept.
- Interfaces between the mods (detecting each other, shared hooks, call order) are documented in
  both repositories and kept as small as possible.

## Game versions and setup

The full list of what to install (game, True Co-op requirements, VR runtime) is in
[docs/setup.md](setup.md).

- Supported executable: `FalloutNV.exe` 1.4.0.525. All game addresses are in
  `src/d3d9/vr/vr_game_addresses.h`; upstream reads them from the Steam executable and notes that
  the GOG executable has the same code addresses. The Epic Games Store build is unverified; do not
  assume it matches.
- The co-op mod targets the Steam version (Steam friends joining through `steam_api.dll`; direct IP
  through ENet). Whether it works without Steam (GOG, direct IP only) is not verified.
- Script extender: [xNVSE](https://github.com/xNVSE/NVSE), started through `nvse_loader.exe` or
  through the NVSE loading patch (FNVVR offers it; the FNV 4GB Patcher does the same). The 4 GB
  (large address aware) patch modifies `FalloutNV.exe`: back up the exe first and only with the
  maintainer's go-ahead.
- Mod managers: `d3d9.dll`, `dxvk.conf` and `fnvvr/` belong in the game root next to
  `FalloutNV.exe`, not in `Data/`. With Mod Organizer 2 that needs Root Builder or a manual copy;
  record in `local/NOTES.md` which manager and profile a test used.
- VR runtime: FNVVR talks to OpenVR, so a headset needs SteamVR (an OpenXR backend exists only as
  an upstream probe branch). Windows and Linux/Proton are both supported by upstream.
- Pin the game on test machines (Steam: "Only update this game when I launch it", no beta branch;
  GOG: auto-update off). Test results are only comparable with the same exe, xNVSE and mod list.

## Build

Upstream builds on Linux with Meson and Ninja, cross-compiling with mingw-w64:

```
git submodule update --init --recursive
meson setup --cross-file build-win32.txt --buildtype release build.32
ninja -C build.32                     # output: build.32/src/d3d9/d3d9.dll
meson test -C build.32 --print-errorlogs
```

On Windows this needs WSL or MSYS2 with an i686 mingw-w64 toolchain (posix thread model); which one
this fork uses is recorded in `local/NOTES.md` once set up. The co-op mod builds with its own CMake
presets (see its README).

## Tests: what they do and do not cover

- `src/d3d9/vr/tests/`: unit tests of the VR layer, built with the host compiler (no Wine, no GPU).
  They test policy and math, not the game.
- The emulator backend (`d3d9.vrBackend = emulator`, or `DXVK_VR_BACKEND=emulator`) runs the real
  game without a headset: simulated head and controllers, virtual time, a preview window.
- **Nothing automated tests the real game together with the co-op mod yet.** Building that harness
  is the first task of this fork ([ADR 0002](adr/0002-headless-ai-testing-first.md)). Until then a
  green unit test run says nothing about hooks, addresses or the co-op interplay; that needs a game
  run and finally a headset session by the maintainer.
- Every game run needs the maintainer's go-ahead (it changes the game and documents folders, see
  below). Name runs `<date>_<id><number>_<description>` and keep their logs and captures in
  `local/runs/<run>/` with a short `README.txt` (setup, mod list, config changes, result).

## Batching changes and test runs (optional)

Several changes may be developed in one go and tested together: one automated run and one headset
session instead of one per change. Headset time is the scarcest resource.

- Each change is still its own commit that builds.
- Batch changes that touch different areas; test changes that could cause the same symptom
  separately or plan a bisect.
- Keep a short test list for the batch, so one session covers all changes (VR alone, co-op alone,
  both together; host and guest where it matters).
- If the batch fails, bisect by commit: every commit builds.

## Test state and backups

Tests change the game folder and the documents folder (`Documents\My Games\FalloutNV`: `Fallout.ini`,
`FalloutPrefs.ini`, `Saves`; Proton: inside the prefix). The maintainer may have a real playthrough
there. Every test that touches them follows this routine:

1. Ask the maintainer first.
2. Back up before changing anything, into a dated folder under `local/` with a `sha256.txt`
   (`sha256sum` / `Get-FileHash` of every file):
   - game folder: `d3d9.dll`, `dxvk.conf`, `fnvvr/`, `FalloutNV.exe` (if a patch is applied),
     `Data/NVSE/Plugins/` (co-op DLL and INI), `nvse_*.dll` -> `local/game-backups/<purpose>_<date>/`;
   - documents folder INIs -> `local/settings-backups/<purpose>_<date>/`;
   - saves (also mod-manager profile saves) -> `local/saves/<purpose>_<date>/`. Put test saves in
     only for the test and restore the previous state afterwards. Never delete or overwrite saves
     of a real playthrough.
3. After the test, restore everything and check it against `sha256.txt`. Move logs, captures and
   test scripts the run left in the game folder to `local/runs/<run>/`.
4. Keep a list of backups (what, when, why, whether it can go) in `local/BACKUPS.md`.

Co-op tests: a session with other people's machines is never started by an agent. Steam Cloud can
bring saves back or spread test saves; keep it off for test installs or check after the run.

## Data hygiene (what may be committed)

Allowed in git: source, tests, and docs whose content is derivable from the public source
(addresses used by the code, hook lists, methodology, plans).

Never commit (keep under `local/`):
- game files and assets, game or third-party DLLs, `FalloutNV.exe` copies, save files
- user INIs from the documents folder, mod-manager profiles
- runtime logs (DXVK, FNVVR, xNVSE, `NVCoop.log`, Proton logs), crash dumps, screenshots with game
  content
- reverse-engineering artifacts: Ghidra/IDA databases, decompiled code, byte dumps, exe hashes
- code copied from the co-op mod (licence, see above)
- machine and user data: local paths, user names, Steam IDs, IP addresses, exact hardware
  identification, tokens, AI agent session transcripts

The test setup may be named at the level needed to interpret a result: GPU vendor or model,
headset model, VR runtime.

Temporary builds and large files go to `tmp/` (ignored), not into the source tree or `local/`.

## Git workflow

- Remotes: `origin` = the fork (https://github.com/joschi127/FNVVR), default push target.
  `upstream` = df4l/FNVVR, fetch only (push URL disabled). Never push to `upstream`.
- `master` mirrors `upstream/master` and gets fast-forwards only, no fork commits.
- Fork work happens on the working branch `coop-compat` in small, self-contained commits that each
  build, so single changes can be offered upstream later. Separate branches only for experiments
  and for changes offered upstream.
- Upstream moves fast (many commits per day, AI-assisted). Keep fork changes small and easy to
  rebase onto a new upstream state; merge `upstream/master` into `coop-compat` (or rebase short
  branches) regularly instead of letting long divergent branches pile up. Changes offered upstream
  must be small, tested and explained by the maintainer in their own words.
- **No AI attribution in commits or PRs** (no `Co-Authored-By: <AI agent>` trailers, no "generated
  with" lines). Commits are authored by the repository owner only.
- Check before every push: nothing from the "never commit" list, no binaries, no local paths.
- Agents ask the maintainer before side effects: installs, pushes, PRs, deletes, anything in the
  game folder, the documents folder, Steam or mod-manager settings.

## Architecture decision records

Decisions that are hard to reverse (architecture, supported game versions, the relationship of the
two mods, scope, workflow) are recorded as ADRs in [docs/adr/](adr/README.md). An accepted ADR is
not rewritten; a change of mind is a new ADR that supersedes it. Open questions are ADRs with status
Proposed until the maintainer decides. Bug fixes and test results do not need ADRs.

## Upstream relationship

- Coordination with df4l (FNVVR) and Zyrcxies (co-op) is the maintainer's decision; agents never
  open issues, PRs or comments in other projects on the maintainer's behalf.
- FNVVR has no contribution rules of its own. Its PR and issue templates are inherited from DXVK
  (the PR template, by DXVK's author, says contributions the author cannot explain, including
  AI-generated summaries, are ignored); FNVVR's author also works with AI assistance and asks for
  bug reports with a log on Discord or GitHub. Scope: features that do not fit the mod are refused
  as bloat. Anything offered upstream must be reviewed and explained by the maintainer; keep it
  small, in upstream's code style and with upstream's commit message style.
- This fork's own `.github/` templates are still the inherited DXVK ones (they ask for Wine
  versions and apitrace files); adapting them is a to-do.
- Upstream discussion happens mostly on the FNVVR Discord (linked in the README). The co-op mod has
  its own Discord and Nexus page.
- Do not report problems that only occur with this fork as regular bugs upstream.
