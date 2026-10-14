# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A native PC port of God of War III Remastered (PS4, CUSA01623/CUSA01715). The game's original
x86-64 `eboot.bin` runs directly on the host CPU; a C runtime replaces the PS4 system libraries
(HLE, no CPU emulation), and a vendored shadPS4 video core translates GNM command buffers and
GCN shaders to Vulkan. Lineage: deadinside28/bloodborne_pc → Supermedo (Windows) → this fork.
When GoW3 replaced Bloodborne, the Bloodborne-specific code was deleted and `bb`/`bbport` renamed
to `gow3` (commits `3a0712a`, `a7cf186`) — the code is single-game, not a multi-game platform.

No game files are in the repo. A decrypted (fake-signed) dump folder with `eboot.bin`,
`sce_module/` and `sce_sys/` is needed to run anything beyond unit tests.

## Build and test

Linux (deps via pkg-config: vulkan, sdl3, ffmpeg; `shell.nix` on NixOS, re-entered automatically):

```bash
bash build.sh            # out/gpu/libgow3gpu.so (CMake/Ninja, LTO on, PGO if pgo/ has profiles) + out/gow3-probe
bash build.sh --test     # also builds and runs the C tests (runtime, pad, sema, mp3, videodec, services, isa...)
GOW3_GAME_DIR=/path/to/CUSA01623 python3 run.py    # or: bash run.sh (builds first); --software uses lavapipe
```

Windows: MSYS2 CLANG64 shell (packages in `packaging/windows/README.md`), same `bash build.sh`;
there CMake builds `gow3-probe.exe` too and the C tests are CMake targets (`mp3-test`, `services-test`, ...).
Packaging: `packaging/windows/package.sh`, `packaging/linux/build.sh` (AppImage).

Build output of the GPU library goes to `out/gpu-build.log`; on failure only its tail is printed.
Submodules (`gpu/third_party/*`, `third_party/LibAtrac9`) are initialized by `build.sh` if missing,
and `gpu/patches/fsr-vulkan/*.patch` are applied to the FSR-Vulkan submodule working tree.

Tests:

```bash
python3 -m unittest discover -s tests                       # all Python tests
python3 -m unittest discover -s tests -p test_patches.py     # one file
python3 -m unittest test_patches.ExternalPatchTests.test_selection_and_unsupported_lines  # one test (run from tests/)
ninja -C out/gpu frame-capture-test && out/gpu/frame-capture-test   # one C++ test (.exe on Windows)
```

C++ tests are `EXCLUDE_FROM_ALL` executables in `gpu/CMakeLists.txt` (no CTest). Tests that inspect
game code (`stat-multipliers-test`, `cheat-hooks-test`, `signatures-test`, ...) take the prepared
`out/eboot.elf` as argument. Launcher tests need `tkinter`; most skip without a display.
There is no CI — run the relevant suites locally.

Useful loader modes: `out/gow3-probe out/boot-linked.bin --app0 <game> --check-imports` lists every
PS4 import with no implementation without starting the game; `--cpu-features`, `--vulkan-only`.

## Architecture: the launch pipeline

`run.py` orchestrates everything (the launcher and `run.sh` just call it). Each step is a script in
`scripts/` writing into `out/`:

1. `mods.py` / `game_update.py` — build a merged view of game + update folder + mods.
2. `prepare.py` — parse the SELF into a flat image (`boot.bin`, magic `G3PROBE2`), classify
   relocations; imports are kept as `NID#library` names. Writes `analysis.json` (imports, needed
   modules, bundled modules) — the first thing to read when bringing up a new eboot.
3. `link_modules.py` — links the game's own `sce_module/libc.prx` and `libSceFios2.prx` into the
   same image (`boot-linked.bin`, `G3PROBE5`); their exports become fallbacks for imports. Also
   rewrites `mov rax, fs:[0]` to `gs:` (host libc owns FS). Only that exact byte pattern is patched.
4. `isa_rewrite.py` — only on CPUs lacking MOVBE/BMI1/LZCNT: table of sites emulated by
   `src/isa_emulate.c` (runs inside guest code: `-mgeneral-regs-only`, no XMM).
5. `content_profile.py`, `patches.py` (XML → `patches.bin`), graphics options from `gow3.ini`.
6. Starts `gow3-probe` with those files. The in-game "Apply and restart" re-runs `run.py` via
   `GOW3_RESTART_COMMAND`.

`src/probe.c` (loader): maps the image below 1 TiB (GPU descriptors hold 40-bit addresses, so all
guest-visible memory, stacks and host objects handed to the guest must stay low), resolves each
import, applies patches, installs hooks, runs module initializers, jumps to the entry point.
Import resolution order: `runtime_resolve` (C runtime) → `gow3gpu_resolve` (shadPS4 HLE:
GnmDriver, VideoOut, AvPlayer, kernel equeue) → native export of a linked `.prx` → a trap.
**There are no auto-success stubs**: an unimplemented import stops with
`STOP: first unsupported PS4 import: <name>`; keep it that way.

## Architecture: the C runtime (`src/runtime_*.c`)

One file per PS4 library area; each exposes `runtime_<area>_resolve(name)` chained in
`runtime.c:runtime_resolve`. Functions called by the guest must be `ABI` (`sysv_abi`, matters on
Windows). Guest-visible errno values are FreeBSD's (`runtime_guest_errno`).

Adding an API: most modules use a `RuntimeExport {name, fn}` table looked up via
`RUNTIME_LOOKUP`, which maps NID → name through `src/import_names.inc`. **A function in a table
is not reachable unless its `NID#library` entry exists in `import_names.inc`** (names from
shadPS4's aerolib; NID = base64 of SHA1(name + salt), see `nid()` in `prepare.py`). Some modules
(`runtime_mutex.c`, `runtime_thread.c`, `runtime_content.c`, `runtime.c`) instead compare literal
NID strings. `libScePosix` imports are canonicalized to `libkernel`. Pair new APIs with a C test
(e.g. `tests/test_services.c`) and add it to `build.sh --test` and/or CMake.

Notable modules: `runtime_memory.c` (direct pool backed by one memfd / Windows section, flexible
memory, PS4 16 KiB pages, GPU page-protection hooks; `GOW3_DMEM_MB`), `runtime_file.c` (guest
mounts `/app0`, `/temp0`, `/download0`, `/data`, `/savedataN`; `..` rejected; mod overlay),
`runtime_services.c` (offline single-user console: user 1, no PSN, no network, PlayGo fully
installed, dialogs through the GPU overlay), `runtime_thread.c` (guest TCB via GS on Linux and a
TEB TLS slot on Windows, patched by `patch_tcb_loads`).

## Architecture: GPU (`gpu/`)

- `gpu/shadps4/` — vendored shadPS4 (commit and backports in `gpu/VENDOR.txt`) plus cuesta4's
  GoW3 rendering fixes. Every local change is marked with a `gow3:` comment; keep doing that.
- `gpu/shim/` — glue replacing the parts of shadPS4's emulator core (`core/`, `common/`), the SDL
  window, the ImGui in-game menu/perf overlay (`gow3_overlay.cpp`), settings (`gow3_settings.cpp`,
  `gow3.ini`), and the GoW3 feature backends (orbs, actors).
- `gpu/gow3gpu.h` — the C interface between loader/runtime and the GPU library.
- Linux: `libgow3gpu.so` loaded via rpath `$ORIGIN/gpu`; Windows: static, linked into `gow3-probe.exe`.
- Many behaviours are toggled by `GOW3_*` environment variables (diagnostics, readbacks,
  upscalers, multisampling); `run.py` and `scripts/graphics_options.py` derive them from the
  launcher/`gow3.ini`. Temporal upscalers/motion vectors are experimental and off by default.

## Game-specific surface (what to change to target another title)

- Hooks (cheats, orb/damage multipliers, enemy health bar): `install_*` in `src/probe.c`,
  `src/*_hook.h`, `gpu/shim/gow3_orbs.cpp`, `gow3_actors.cpp`. Sites are found by byte signature
  (`src/signature.h`); a missing site disables only that feature.
- Patches: `patches/God_of_War_III_Remastered.xml` (shadPS4/GoldHEN XML; `Pattern`/`At`
  signatures generated by `tools/make_signatures.py`), the `GAMES` profile and
  `REFERENCE_SHA256` in `scripts/patches.py`. Literal writes only; mask patches are rejected.
- GPU heuristics: `gbuffer_draw` / 864-byte camera constant buffer and 1920×1080 scene scaling in
  `vk_rasterizer.cpp`; vendor-based guest MSAA decision; `GOW3_READBACK_LINEAR=1` default in `run.py`.
- Pad touchpad semantics (photo mode / pause) in `runtime_pad.c`; title/serial defaults in
  `probe.c`; launcher, Steam shortcut and packaging identity.

Known limits for other eboots: SELF must be unencrypted/uncompressed; image ≤ 512 MiB; ≤ 16 linked
modules, TLS module IDs 2–7; relocation types 1, 6, 7, 8 only; extra bundled `.prx` need
`link_modules.py --modules` (not exposed by `run.py`). Not implemented: libSceNgs2, libSceFiber,
kernel event flags, AIO, `sceKernelLoadStartModule`.

## Debugging

The loader reports guest faults as image-relative offsets with an rbp backtrace; `--timeout N`
arms a watchdog that dumps all thread stacks (Linux; SIGUSR2 dumps one thread). Scripted input:
`GOW3_PAD_FILE`, `GOW3_PAD_RECORD`/`GOW3_PAD_REPLAY`. `tools/got_import.py <offset>` names the import
bound to a GOT slot. Frame capture: `GOW3_FRAME_CAPTURE_KEY` (F11). The full log of a launcher run is
`user/last_run.log`.

## Conventions

- Development branch is `gow3`; `main` receives tested merges.
- Commit messages are in Portuguese, `Type(Scope): description` (e.g. `Fix(GPU): ...`,
  `Feat(Launcher): ...`, `Test(GPU): ...`, `Docs(Roadmap): ...`), body explaining the cause.
- C is compiled with `-std=c11 -Wall -Wextra -Werror`; code style is dense (short lines joined with
  `;`, few braces) — match the surrounding file. Comments explain why, often referencing the
  file that depends on a contract.
- User-facing docs: `README.md` (English) and `README.pt-BR.md` must stay in sync;
  progress lives in `docs/gow3/ROADMAP.md` (Portuguese).
- never do a commit