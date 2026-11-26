# Building FNVVR

FNVVR is a 32-bit `d3d9.dll` (a DXVK fork), cross-compiled with mingw-w64 and built with Meson and Ninja.
Whichever way you build, you need:

- the git submodules (`git submodule update --init --recursive`; Vulkan headers, SPIR-V and others);
- an i686 mingw-w64 compiler with the **posix thread model**;
- Meson, Ninja and glslang;
- a host C++ compiler for the VR layer's unit tests (`src/d3d9/vr/tests/`), which run without Wine and without a
  GPU.

The ways below produce the same `d3d9.dll`. To install a build, copy `d3d9.dll` and a `dxvk.conf` into the game
folder and `src/d3d9/vr/openvr/*.json` and `src/d3d9/vr/texts/*.txt` into `fnvvr/` there; the README's install
steps say what else a release contains.

| Way | Where | Notes |
|---|---|---|
| [Linux](#linux) | Linux machine | upstream's way |
| [Windows with MSYS2](#windows-with-msys2) | the gaming PC itself | no virtual machine, no restart |
| [Windows with WSL](#windows-with-wsl) | the gaming PC itself | not set up yet |
| [Another machine over SSH](#another-machine-or-virtual-machine-over-ssh) | a Linux machine or VM | build there, copy the DLL over |

## Linux

As in the README:

```
git submodule update --init --recursive
meson setup --cross-file build-win32.txt --buildtype release build.32
ninja -C build.32                     # output: build.32/src/d3d9/d3d9.dll
meson test -C build.32 --print-errorlogs
```

Packages: a mingw-w64 cross compiler with posix threads, Meson, Ninja and glslang. Debian/Ubuntu:
`g++-mingw-w64-i686-posix meson ninja-build glslang-tools` (select the posix variant with
`update-alternatives` if the win32 one is the default); Arch: `mingw-w64-gcc meson ninja glslang`.

## Windows with MSYS2

[MSYS2](https://www.msys2.org) provides the compilers and tools natively on Windows. FNVVR uses three of its
environments, because none has everything:

- **MINGW32**: the i686 compiler `mingw-w64-i686-gcc` (posix thread model);
- **UCRT64**: Meson, Ninja and glslang (MINGW32 has no glslang);
- **MSYS**: the POSIX `gcc` for the unit tests, which use POSIX calls (`mmap`) as on Linux.

`build-win32-msys2.txt` (cross file) and `build-native-msys2.txt` (native file) in the repository root name these
compilers; they assume MSYS2 in `C:\msys64`.

**Install once:**

1. Install MSYS2, from its website or with `winget install --id MSYS2.MSYS2 -e`.
2. In an MSYS2 shell, update it (`pacman -Syuu`, repeat until there is nothing to do) and install the packages:

   ```
   pacman -S --needed mingw-w64-i686-gcc mingw-w64-ucrt-x86_64-meson mingw-w64-ucrt-x86_64-ninja \
                      mingw-w64-ucrt-x86_64-glslang mingw-w64-ucrt-x86_64-gcc gcc
   ```

3. In the repository (Git Bash): `git submodule update --init --recursive`.

**Build** (Git Bash or an MSYS2 shell, in the repository root):

```
tools/build-msys2.sh --setup          # first time, or after changing meson files: new build.msys
tools/build-msys2.sh                  # later builds; output: build.msys/src/d3d9/d3d9.dll
```

The script runs the steps of the Linux build (`meson setup` with the two files above, `ninja`, `meson test`)
in a UCRT64 shell with MINGW32's compiler on the path. `MSYS2_ROOT` points it at another MSYS2 folder.

**Notes:**

- The unit tests for the block heap need address space below 4 GiB. Linux has `MAP_32BIT` for it; elsewhere the
  tests ask for it at address hints and say "skipped" if the system does not give it.
- MSYS2 is phasing out its environments without UCRT (MINGW64 from 2026); MINGW32 is one of those, and MSYS2
  builds fewer 32-bit packages each year. If the i686 compiler goes away, WSL is the way to go.

## Windows with WSL

Not set up yet. Inside a Linux distribution in WSL 2 the [Linux](#linux) steps apply unchanged; a
section with the details follows once it has been tried. (WSL 2 uses Hyper-V, which other hypervisors on the
same PC must be able to share.)

## Another machine or virtual machine over SSH

Any Linux machine or VM with the [Linux](#linux) packages can build for the gaming PC:

1. Keep a clone there and bring it to the commit you want, by pushing and pulling through a remote or with
   `git bundle` copied over.
2. Build there with the Linux steps (`ninja -C build.32`, `meson test -C build.32`), for example through `ssh`.
3. Copy `build.32/src/d3d9/d3d9.dll` back (`scp`) and install it as above.

This needs nothing on the gaming PC but an SSH client, but a full build is limited by that machine's CPU.
