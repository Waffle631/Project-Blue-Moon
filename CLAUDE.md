# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A native PC port of TSUKIHIME -A piece of blue glass moon- (PS4, CUSA47999, v01.00), branch `tsuki/main`.
The game's original x86-64 `eboot.bin` runs directly on the host CPU; a C runtime replaces the PS4 system
libraries (HLE, no CPU emulation), and a vendored shadPS4 video core translates GNM command buffers and GCN
shaders to Vulkan. It is a visual novel, so 2D/video/audio paths matter more than 3D. Target language: English.
Public name: "Project Blue Moon" (launcher, shortcuts, packages); internal names stay `tsuki_*`/`TSUKI_*`.
The code is single-game, ported from the previous game (branch `gow3`).

Status: Specs 1–8 are technically closed. History, decisions and oracle details: `docs/tsuki/ESTADO.md`.
Roadmap (Spanish): `docs/tsuki/ROADMAP.md`.

## Hard rules

- The dump `Tsukihime/CUSA47999/` (~45 GB) is untracked and ignored: never `git add` it. Run with
  `TSUKI_GAME_DIR=Tsukihime/CUSA47999`. It needs `eboot.bin`, `sce_module/` and `sce_sys/`.
- Code only the previous game used is **deleted**, never disabled or hidden behind a switch.
- **No auto-success stubs.** An unimplemented import stops with `STOP: first unsupported PS4 import: <name>`; keep it so.
- Every local change in `gpu/shadps4/` is marked with a `tsuki:` comment.
- A function in a runtime export table is unreachable unless its `NID#library` entry exists in
  `src/import_names.inc`. New APIs get a C test (`build.sh --test` and/or CMake).
- The render oracle is the recorded route (`out/route/`, `out/baseline/6a/`): GPU changes must still match it
  (`tools/route.sh record | replay <name>`, `tools/compare_route.sh`). Details in `docs/tsuki/ESTADO.md`.
- Never rely on the saved game: with progress in it the boot goes straight to the title menu. The prologue
  needs `out/savedata-sin-progreso/`.

## Environment

Everything is built and tested in the Fedora 44 distrobox `ps4dev` (immutable host). Claude Code runs in the
VS Code Flatpak, which cannot see `distrobox`, so run:
`flatpak-spawn --host distrobox enter ps4dev -- sh -c 'cd <repo> && bash build.sh'`
(`<repo>` is the absolute path of the working directory; `$HOME` is shared). The Flatpak `/tmp` is not visible
in the container: write logs under `out/`. Setup and command reference: `docs/ENTORNO.md`.
Dev machine: Bazzite 44, KDE Wayland, Ryzen 5 7600, 16 GiB, RX 6650 XT (RADV). Linux is primary; Windows is
in the Alpha (cross-built from Linux, see `packaging/windows/`).

## Build and test

```bash
bash build.sh            # out/gpu/libtsukigpu.so + out/tsuki-probe
bash build.sh --test     # also builds and runs the C tests, ends with tools/check_rename.sh
TSUKI_GAME_DIR=Tsukihime/CUSA47999 python3 run.py    # or: bash run.sh (builds first); --software uses lavapipe
python3 -m unittest discover -s tests                # Python tests (one file: -p test_x.py)
ninja -C out/gpu frame-capture-test && out/gpu/frame-capture-test   # one C++ test (EXCLUDE_FROM_ALL, no CTest)
```

GPU build log: `out/gpu-build.log` (the tail is printed on failure). Submodules (`gpu/third_party/*`,
`third_party/LibAtrac9`) are initialized by `build.sh`. There is no CI: run the relevant suites locally.
Tests that inspect game code take the prepared `out/eboot.elf`. Launcher tests need `tkinter` and skip without a display.

## Launch pipeline

`run.py` orchestrates everything; each step is a script in `scripts/` writing into `out/`:
`mods.py`/`game_update.py` (merged view) → `prepare.py` (SELF → `boot.bin`, writes `analysis.json`, the first thing
to read for a new eboot) → `link_modules.py` (links the game's `libc.prx`, `libSceFios2.prx` into `boot-linked.bin`)
→ `isa_rewrite.py` (only on CPUs lacking MOVBE/BMI1/LZCNT) → `content_profile.py`, `patches.py` → starts
`tsuki-probe`. Import resolution (`src/probe.c`): `runtime_resolve` (C runtime) → `tsukigpu_resolve` (shadPS4 HLE)
→ native export of a linked `.prx` → a trap. Guest-visible memory must stay below 1 TiB.
Full detail, runtime modules, GPU layout and loader limits: `docs/ARQUITECTURA.md`.
Other contracts: `docs/audiodec.md` (ATRAC9/HEVAG), `docs/avplayer.md` (cutscenes through `sceAvPlayer`).

## Debugging

- `out/tsuki-probe out/boot-linked.bin --app0 Tsukihime/CUSA47999 --check-imports` lists imports with no implementation.
- Guest faults are reported as image-relative offsets with an rbp backtrace; `--timeout N` dumps all thread stacks.
- `TSUKI_FILE_TRACE=1` logs every guest file open; `tools/got_import.py [--linked] <offset>` names a GOT import.
- Scripted input: `TSUKI_PAD_FILE`, `TSUKI_PAD_RECORD`/`TSUKI_PAD_REPLAY`, `TSUKI_MOUSE=0`.
- Frame capture: `TSUKI_FRAME_CAPTURE_KEY` (F11) or `TSUKI_CAPTURE_TRIGGER=<file>`.
- The full log of a launcher run is `user/last_run.log`.

## Conventions

- Develop on `tsuki/main`; `main` is kept as history and is not touched. The repo is only local, so commits and test branches are fine.
- Commit messages in **Spanish**, `Tipo(Ámbito): descripción` (e.g. `Feat(Kernel): ...`, `Fix(GPU): ...`), body explaining the cause.
- C is compiled with `-std=c11 -Wall -Wextra -Werror`; dense style (short lines joined with `;`, few braces):
  match the surrounding file. Comments explain why, often naming the file that depends on a contract.
- Functions called by the guest must be `ABI` (`sysv_abi`). Guest-visible errno values are FreeBSD's.
- `README.md` (English), `README.es.md` (Spanish) and `packaging/windows/README-Windows.txt` stay in sync.
  Progress lives in `docs/tsuki/ROADMAP.md` (Spanish).
- Commits are authored with the GitHub `noreply` address.
