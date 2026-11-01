# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A native PC port of TSUKIHIME -A piece of blue glass moon- (PS4, CUSA47999). The game's original
x86-64 `eboot.bin` runs directly on the host CPU; a C runtime replaces the PS4 system libraries
(HLE, no CPU emulation), and a vendored shadPS4 video core translates GNM command buffers and
GCN shaders to Vulkan. Lineage: deadinside28/bloodborne_pc → Supermedo (Windows) → a port of the previous
game (`docs/gow3/ROADMAP.md`, branch `gow3`) → this fork. Each new game replaced the last one's code and
names (commits `3a0712a`, `a7cf186`; Spec 7 for this one) — the code is single-game, not a multi-game platform.

No game files are in the repo. A decrypted (fake-signed) dump folder with `eboot.bin`,
`sce_module/` and `sce_sys/` is needed to run anything beyond unit tests.

## Current goal: retarget to Tsukihime (branch `tsuki/main`)

This branch is porting **TSUKIHIME -A piece of blue glass moon-** (PS4, CUSA47999, v01.00) on top
of the previous game's codebase. Some sections below still describe code inherited from it; the
"Game-specific surface" section is the checklist of what had to be generalized or replaced.
Features only the previous game used (orb/damage hooks, actors, enemy health bar, its patches XML, GPU heuristics,
touchpad semantics, its own tests; Specs 1a and 1b removed them) must be **deleted** from the tree, not disabled,
isolated or adapted: no dead code behind a switch. The previous game stops being a supported title on this
branch (history stays in git and in the branch `gow3`). Generic code (loader, runtime, shadPS4 core)
stays; the temporal upscalers it carried went too once they could not run (Spec 7 §12). Order of work and acceptance criteria: `docs/tsuki/ROADMAP.md` (kernel Spec 2 first; Spec 1a deletes
hooks/patches early; a "first frame" milestone gates Spec 1b, which deletes the renderer heuristics
with before/after captures; the rename to `tsuki` is Spec 7, done last).

- Dump: `Tsukihime/CUSA47999/` (untracked, ~45 GB — never `git add` it). Run with
  `TSUKI_GAME_DIR=Tsukihime/CUSA47999`. Game data is in `image/` (`allpac*.hed/.mrg/.nam`,
  `bgm.*`, `allscr.mrg`) plus `data/`; it is a visual novel, so 2D/video/audio paths matter more
  than the 3D heuristics.
- `prepare.py` parses the eboot cleanly: 34 MB image, entry `0xef390`, 337 imports, relocation
  types 1/7/8 only (within the loader's limits).
- Bundled modules: `libc.prx`, `libSceFios2.prx` and `libSceNpToolkit2.prx`. The eboot does not
  import NpToolkit2 (not in its needed modules), so the default `link_modules.py` set is enough.
- After linking, 359 imports go to the host runtime. A static inventory (2026-10-13, not yet
  confirmed by `--check-imports`) found ~61 without implementation: 29 libkernel (notably
  **kernel event flags**, process time counter, `pthread_rwlock_*`, thread dtors/atexit hooks),
  libSceAudiodec (6) + libSceAudiodecCpu (3) — the game decodes audio through these, **not AJM** —
  SaveData (Initialize3, Mount2, backup APIs), ScreenShot, SharePlay, GameLiveStreaming,
  VideoRecording, Sysmodule, SystemService, UserService, MsgDialog, DiscMap, LibcInternalExt.
  Spec 2 is closed (2026-10-17): libkernel has only three deliberate traps left
  (`scePthreadCancel`, `sceKernelGetModuleInfoForUnwind`, `sceKernelInternalMemoryGetModuleSegmentInfo`);
  Spec 3 is closed (2026-10-19): its 23 platform APIs all have an explicit, logged behaviour (no
  faked PSN or network). Spec 4 is closed (2026-10-21; its adversarial review, plan task 13, is pending): `libSceAudiodec`
  decodes ATRAC9 (LibAtrac9) and `libSceAudiodecCpu` decodes HEVAG (`third_party/vgmstream_hevag`, state in
  the guest's work memory; `docs/audiodec.md`), so `--check-imports` lists only those 3 traps.
  GnmDriver/VideoOut/AvPlayer NIDs all exist in the shadPS4 tables. Cutscenes are 134 `.mp4` in
  `image/movie/` played through **`sceAvPlayer`** (not `sceVideodec`). Spec 5 is closed (2026-10-25; its
  adversarial review, plan task 8 step 4, is pending): the vendored AvPlayer plays all 134 outside the game
  (`avplayer-test --movie-dir`), and in game the opening, the prologue, the chapter 1 title, skipping the
  logo and the title-menu loop work; the game's AvPlayer contract is in `docs/avplayer.md`. Start bring-up with
  `out/tsuki-probe out/boot-linked.bin --app0 Tsukihime/CUSA47999 --check-imports`.
- What a run shows depends on the save in `user/savedata/1/CUSA47999/`. With no progress in it, the
  boot plays the prologue video first (`prologue_en.mp4`, ~6 min 29 s, buttons do not skip it). The first run to get past the video (2026-10-20, once ATRAC9 decoded) saved progress, and
  since then every launch **goes straight to the title menu** (music in < 10 s, no video). To see the
  prologue again (AvPlayer work, the video frames of `out/baseline/`), restore the save from before
  that run: `out/savedata-sin-progreso/` (untracked) holds a copy, because `run.py` backs up
  `user/savedata` into `user/savedata_backups/` on every start and keeps only the last 10
  (`scripts/save_backup.py`).
- Boot frames with the current save (rechecked 2026-10-25; fullscreen `TSUKI_FULLSCREEN=1`, so captures are
  1920×1080): 120–420 the autosave notice, whose spinning "Saving…" icon (box x 1750–1829, y 955–1034) differs
  from run to run; 900–1020 the `TM_CI.mp4` logo video (not deterministic); 1080–1560 the legal warning,
  pixel-identical from 1140 to 1500; then the title menu. Pixel oracle frames: 120/300/420 with the icon box
  masked, and 1320 whole. `out/spec1b-t2/compare-oracle.sh <before>/ <after>/` (untracked, runs inside the
  distrobox) compares them; the reference is `out/baseline/1b/antes/`. Since Spec 6 the user's save changed and
  the game sees a mouse, so retakes use `out/spec1b-t3/run-oracle.sh <take> [VAR=value…]` (untracked, run from the
  host): the fixed save `out/savedata-usuario-1b/` in its own `TSUKI_USER_DIR`, `TSUKI_MOUSE=0`, 40 s fullscreen.
- Spec 1b (GPU heuristics) is closed (2026-10-28; branch `tsuki/spec-1b-cierre`, fast-forward merged into `tsuki/main` the same day).
  For Tsukihime none of the heuristics was needed or in the way; the delete-don't-disable rule above is why they went.
  A temporary counter with them switched on (the experimental upscaler switch, gone since Spec 7 §10) found 0
  matches at boot and on the recorded route; after the deletions the oracle frames and the route match
  `out/baseline/6a/`.
  Scope, as the user decided on 2026-10-25 (spec §2.2):
  - deleted: `gbuffer_draw` and the 864-byte constant (task 3), and the temporal upscaler's hard-coded shader
    hashes from another game (task 3b). After that the upscaler had no trigger by default, and Spec 7 §10 removed
    the variables that could give `UiComposition::Trigger` one;
  - linear readback: only the `TSUKI_READBACK_LINEAR=1` default in `run.py` went (task 4, done). With it on,
    Tsukihime put no image in `download_images`, either at boot or in the novel, so it did nothing for this
    game. `readback_linear_images` is a shadPS4 option and stays: off by default, and
    `TSUKI_READBACK_LINEAR=1` still turns it on;
  - **not deleted:** the vendor-based guest MSAA decision, which is a Windows driver workaround and cannot be
    tested on RADV. The 1920×1080 scene scaling was kept here too, but it only ran with the temporal upscaler and
    went with it (Spec 7 §12).

  Without `gbuffer_draw`, `CameraMotion` has no inputs (`OnGBufferPass`/`OnConstants` have no callers), so
  `CameraMotion`, `ObjectMotion` and the temporal upscaler (FSR 3.1/4, DLSS, TAA) could no longer run with this game.
  Spec 7 §10 removed the options that switched it on (2026-10-30), and then §12 (user decision, 2026-10-30) deleted
  the code: the upscaler, the camera and object motion vectors (also in the shader recompiler), the reduced-size
  scene targets (`SceneTargets`), the DLSS bridge and the `fsr-vulkan` submodule with its patches. The recorded
  route still matches `out/baseline/6a/`. FSR 1 and RCAS are shadPS4's, spatial, run at present time and stay:
  they scale the game's 1080p image to the window.
- Spec 6 (2D, mouse, saves, language) is closed (2026-10-28): blocks B, C and D were done on 2026-10-25 and block A,
  the 2D render baseline, on 2026-10-28 (branch `tsuki/spec-6bcd`, fast-forward merged into `tsuki/main`).
  - The user closed it without waiting for Spec 1b. Replaying the route after Spec 1b's deletions and matching
    `out/baseline/6a/` became a Spec 1b criterion, met on 2026-10-28; Spec 7 is unblocked.
  - Recorded route: `tools/route.sh record | replay <name>` (inside the distrobox; it re-enters it from the host).
    Everything lives in `out/route/` (untracked):
    - every run starts from the fixed save copy `out/route/save/` in its own `TSUKI_USER_DIR`, so `user/` is never
      touched;
    - `out/route/tsuki.ini` sets `async_shaders=0`; runs are fullscreen with no mouse;
    - `pad.txt` is the user's recording (pad reads); `frames.txt` says how each frame is compared.
    `tools/compare_route.sh <a>/ <b>/ out/route/frames.txt` compares two capture folders. Frames are `exact`, or
    `mask` with boxes that hide the animated click-wait cursor, or `eye` for the load menu over `titlebg1.mp4`.
  - The approved reference is `out/baseline/6a/` (8 frames, untracked). Replays are pixel-identical to it with
    those masks; a replay takes ~3 min 40 s.
  - The pixelated save-slot thumbnail is the game's own: it builds it on the CPU from `allpac2.mrg` into a
    512×256 one-mip texture, which Vulkan samples as the guest asks. The user decided to leave it as is
    (2026-10-28).
  - Mouse: `src/runtime_mouse.c`. The window thread accumulates SDL events (`gpu/shim/tsuki_mouse.h`) and
    `sceMouseRead` takes them through `tsukigpu_mouse_read`. The game's cursor lives in a 640×448 space, so
    the bridge sends deltas that put that cursor where the host pointer is, not the raw SDL deltas. While
    the Insert menu is open the game sees a connected mouse with no movement and no buttons.
  - Saves: the game keeps every slot in one 4 MiB `savedata000`. At boot it reads the whole file read-only.
    To save it truncates and rewrites the file, calls `SaveIcon`, then `UmountWithBackup`. It never calls
    `SetParam` or `DirNameSearch`. The runtime logs mount, param, icon and unmount.
- Spec 7 (rename to `tsuki`) is closed (2026-10-30; branch `tsuki/spec-7`, fast-forward merged into `tsuki/main`): `TSUKI_*` variables, `tsuki_*`
  files, `tsuki-probe`, `libtsukigpu.so`, `tsuki.ini`, the `tsuki:` mark. No old name is read and nothing is migrated:
  an `.ini` with the old name left in the root is ignored. `bash build.sh --test` ends with `tools/check_rename.sh`, which fails when
  an old name comes back (exceptions: `docs/gow3/`, `docs/specs/`, the branch `gow3`, lines marked `rename-check: ok`;
  full list in the script; since Spec 8 the player READMEs are checked too). The user confirmed the title in the window and the Insert menu. The
  performance overlay now measures CPU, RAM, GPU and VRAM on Linux too (spec §9): `/proc/self/statm`, process CPU time,
  and `drm-engine-gfx` from the fdinfo of the process's `/dev/dri` descriptors (`tsuki_perf_stats.h`).
- Spec 8 (public Alpha, approved 2026-10-30) is technically closed (2026-10-31, branch `tsuki/spec-8`; spec
  `docs/specs/2026-10-30-spec-8-alpha.md`, task by task). The recorded route and the boot oracle still match
  (`run-oracle.sh` takes need `TSUKI_CONFIG=out/t9-defaults.ini`: the root `tsuki.ini` holds the user's own settings,
  and its RCAS changes every pixel). The user tried the launcher on Linux (source and AppImage) and approved the logos and the
  audio. Before publishing, the user still has to do the title font, the repository, the real-Windows test and the release. The Alpha goes to a new GitHub repository (it does not
  exist yet). The user creates it and uploads the release (AppImage + Windows `.zip`); the spec pushes nothing and adds no remotes.
  User decisions:
  - The public name is **"Project Blue Moon"**, so the port does not present itself under the game's trademark.
    It covers the launcher's `APP_NAME`, the `.exe` names, shortcuts, `.desktop`, Steam and package names.
    Internal `tsuki_*`/`TSUKI_*` names stay, and text that names the dump a player needs still says TSUKIHIME (CUSA47999).
  - The prologue criterion is approved without a new recording.
  - Windows is in the Alpha. It is cross-built from Linux (llvm-mingw + MSYS2 CLANG64 packages in `out/cross/`,
    `build.sh --windows`, exit code 0) and tried under the `org.winehq.Wine` Flatpak; the user then tests it on real
    Windows.
  - The launcher recognises the dump by its `param.sfo` (`scripts/game_identity.py`, mirrored by `SUPPORTED_TITLE_ID`
    in `src/probe.c`), and both packages build: `packaging/linux/build.sh` → `project-blue-moon-x86_64.AppImage`,
    `packaging/windows/package.sh` (from Linux, PyInstaller under Wine) → `project-blue-moon-windows.zip`. Mods stay
    off by default: `scripts/mods.py` only mounts the previous game's `dvdroot_ps4/` layout (roadmap §8).
  - The READMEs are rewritten for this project (`README.md`, `README.es.md` in Spanish and
    `packaging/windows/README-Windows.txt`). Their repository links use the one placeholder
    `https://github.com/Waffle631/Project-Blue-Moon`, also in `.github/ISSUE_TEMPLATE/`, which the user replaces when the repository
    exists.
  - The title font (`launcher/gowfont.ttf`), the launcher palette and the icon are not changed by the spec. It only
    lists their files and how to regenerate the `.ico` and the AppImage icon (§7); having them changed by the user is
    a criterion for publishing. Done by the user on 2026-10-31: the accent is blue (`ACCENT` `#1a43a5`, `ACCENT_HI`
    `#3a66d0`, lighter because it is also text) and the icon is a blue moon; the font is still the old one.
  - The port's commits are authored with the GitHub `noreply` address (history rewritten on 2026-10-31; the old state
    is in `out/backup-antes-noreply.bundle`).
- Target language: **English**. The game ships ja/en/zc/zt (`ft_*.mrg` fonts, `*_en.mp4` videos)
  and picks it from the system language (`sceSystemServiceParamGetInt`); `runtime_services.c`
  already reports 1 = English (US) unless `TSUKI_LANGUAGE` overrides it, and logs the first query. Japanese
  and the other languages are not a goal. `image/ft_ja.mrg` is opened at boot whatever the language is, so
  that open alone does not mean the game picked Japanese.
- Roadmap (Spanish): `docs/tsuki/ROADMAP.md` — stages, specs and the full missing-API table.
- Dev/test machine: Bazzite 44 (Fedora-based, immutable), KDE Plasma 6 on Wayland, Ryzen 5 7600,
  16 GiB RAM, **AMD Radeon RX 6650 XT** (RADV). Linux is the primary platform; Windows is part of the Alpha (Spec 8).
- Build environment: everything is built and tested inside the Fedora 44 distrobox `ps4dev`
  (the host is immutable; no `rpm-ostree`). Claude Code runs in the VS Code Flatpak, which cannot
  see `distrobox`, so run commands as
  `flatpak-spawn --host distrobox enter ps4dev -- sh -c 'cd <repo> && bash build.sh'`, where `<repo>` is the
  repository's absolute path (the working directory; `$HOME` is shared, so it is the same inside the container).
  `/tmp` of the Flatpak sandbox is not visible inside the container: write logs under `out/`.
  Container setup, dependencies (RPM Fusion full ffmpeg; the system Vulkan-Headers 1.4.341 are enough since 2026-10-30)
  and command reference: `docs/ENTORNO.md`.

## Build and test

Linux (deps via pkg-config: vulkan, sdl3, ffmpeg; `shell.nix` on NixOS, re-entered automatically):

```bash
bash build.sh            # out/gpu/libtsukigpu.so (CMake/Ninja, LTO on, PGO if pgo/ has profiles) + out/tsuki-probe
bash build.sh --test     # also builds and runs the C tests (runtime, pad, sema, mp3, videodec, services, isa...)
TSUKI_GAME_DIR=Tsukihime/CUSA47999 python3 run.py    # or: bash run.sh (builds first); --software uses lavapipe
```

Windows: MSYS2 CLANG64 shell (packages in `packaging/windows/README.md`), same `bash build.sh`;
there CMake builds `tsuki-probe.exe` too and the C tests are CMake targets (`mp3-test`, `services-test`, ...).
Packaging: `packaging/windows/package.sh`, `packaging/linux/build.sh` (AppImage).

Build output of the GPU library goes to `out/gpu-build.log`; on failure only its tail is printed.
Submodules (`gpu/third_party/*`, `third_party/LibAtrac9`) are initialized by `build.sh` if missing.

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

Useful loader modes: `out/tsuki-probe out/boot-linked.bin --app0 <game> --check-imports` lists every
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
5. `content_profile.py`, `patches.py` (XML → `patches.bin`).
6. Starts `tsuki-probe` with those files. The game reads `tsuki.ini` itself (`gpu/shim/tsuki_settings.cpp`);
   nothing restarts it from the in-game menu (Spec 8 §5 took out "Apply and restart").

`src/probe.c` (loader): maps the image below 1 TiB (GPU descriptors hold 40-bit addresses, so all
guest-visible memory, stacks and host objects handed to the guest must stay low), resolves each
import, applies patches, installs hooks, runs module initializers, jumps to the entry point.
Import resolution order: `runtime_resolve` (C runtime) → `tsukigpu_resolve` (shadPS4 HLE:
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
memory, PS4 16 KiB pages, GPU page-protection hooks; `TSUKI_DMEM_MB`), `runtime_file.c` (guest
mounts `/app0`, `/temp0`, `/download0`, `/data`, `/savedataN`; `..` rejected; mod overlay),
`runtime_services.c` (offline single-user console: user 1, no PSN, no network, PlayGo fully
installed, dialogs through the GPU overlay), `runtime_thread.c` (guest TCB via GS on Linux and a
TEB TLS slot on Windows, patched by `patch_tcb_loads`).

## Architecture: GPU (`gpu/`)

- `gpu/shadps4/` — vendored shadPS4 (commit and backports in `gpu/VENDOR.txt`) plus cuesta4's
  rendering fixes for the previous game. Every local change is marked with a `tsuki:` comment; keep doing that.
- `gpu/shim/` — glue replacing the parts of shadPS4's emulator core (`core/`, `common/`), the SDL
  window, the ImGui in-game menu/perf overlay (`tsuki_overlay.cpp`), settings (`tsuki_settings.cpp`,
  `tsuki.ini`).
- `gpu/tsukigpu.h` — the C interface between loader/runtime and the GPU library.
- Linux: `libtsukigpu.so` loaded via rpath `$ORIGIN/gpu`; Windows: static, linked into `tsuki-probe.exe`.
- Many behaviours are toggled by `TSUKI_*` environment variables (diagnostics, readbacks,
  multisampling); the launcher derives some of them from its settings. There are no temporal upscalers or
  motion vectors (deleted, Spec 7 §12). `run.py` always turns on deferred and stale image readback
  (`TSUKI_DEFERRED_READBACK`, `TSUKI_STALE_READBACK`; the environment can turn them off), so CPU reads of
  GPU-written images use the previous frame's copy. Tsukihime makes no readbacks on the recorded route, so
  Spec 8 §5 took their options out of the launcher and the in-game menu, with the frame rate limit (30 FPS
  slowed the whole game down), the multisampling choice and the shader cache switches.

## Game-specific surface (the previous game's code, deleted; what any other title would need)

- ~~Hooks (cheats, orb/damage multipliers, enemy health bar), their byte signatures and backends~~ (Spec 1a).
- ~~The built-in patch XML and its profile~~ (Spec 1a); ~~the per-game patch selection (`GAMES`,
  `game_profile`) and the render resolution / engine frame rate options~~ (Spec 8). `scripts/patches.py` still reads
  third-party shadPS4/GoldHEN XML for CUSA47999 v01.00 (literal writes only, mask patches are rejected), and
  `tools/make_signatures.py` still generates `Pattern`/`At` signatures.
- GPU heuristics (Spec 1b, closed 2026-10-28), all removed:
  - ~~`gbuffer_draw` and the 864-byte camera constant buffer in `vk_rasterizer.cpp`~~ (task 3). The 864-byte
    classifier in `motion_history.h` went later with `CameraMotion`/`ObjectMotion` (Spec 7 §12).
  - ~~The temporal upscaler's hard-coded shader hashes (`UiComposition::MovieShader`, `trigger_hash`,
    `ui_trigger_vs`)~~ (task 3b).
  - ~~The `TSUKI_READBACK_LINEAR=1` default in `run.py`~~ (task 4, 2026-10-25). The option stays.

  Not the previous game's code, so it stays: the vendor-based guest MSAA decision (`tsuki_msaa_default.h`, a
  workaround for AMD's and Intel's Windows drivers; user decision of 2026-10-25).
- ~~Pad touchpad semantics (photo mode / pause) in `runtime_pad.c`~~ (Spec 1a). Game identity: `scripts/game_identity.py`
  and `SUPPORTED_TITLE_ID` in `probe.c`; the public name ("Project Blue Moon") in the launcher, shortcuts and packages.

Known limits for other eboots: SELF must be unencrypted/uncompressed; image ≤ 512 MiB; ≤ 16 linked
modules, TLS module IDs 2–7; relocation types 1, 6, 7, 8 only; extra bundled `.prx` need
`link_modules.py --modules` (not exposed by `run.py`). Not implemented: libSceNgs2, libSceFiber,
AIO, `sceKernelLoadStartModule`.

## Debugging

The loader reports guest faults as image-relative offsets with an rbp backtrace; `--timeout N`
arms a watchdog that dumps all thread stacks (Linux; SIGUSR2 dumps one thread). Scripted input:
`TSUKI_PAD_FILE`, `TSUKI_PAD_RECORD`/`TSUKI_PAD_REPLAY` (both from the game's first pad read, F9 only stops
recording; clocked in pad reads, file header `reads`, old ms files are refused), `TSUKI_MOUSE=0` (no mouse, for replays). `TSUKI_FILE_TRACE=1` logs every guest file open
(`File trace: open(<path>) -> fd <n>, <size> bytes` or `-> errno <e>`); `TSUKI_AUDIO_TRACE` only covers failed reads and Ajm. `tools/got_import.py [--linked] <offset>` names the import
bound to a GOT slot (`--linked`: `out/boot-linked.bin`, which includes `libc.prx`). Frame capture: `TSUKI_FRAME_CAPTURE_KEY` (F11), or `TSUKI_CAPTURE_TRIGGER=<file>` (creating the file records the next frame: passes, sampled images with their mips, and samplers as the guest asks for them and as Vulkan gets them). The full log of a launcher run is
`user/last_run.log`.

## Conventions

- Development happens on `tsuki/main`, the main branch (the previous game's was the branch `gow3`). `main` is kept as
  history and is not touched, so the repository this was forked from is left alone (user decision, 2026-10-30).
- Commit messages are in **Spanish**, `Tipo(Ámbito): descripción` (e.g. `Feat(Kernel): implementar event flags`,
  `Fix(GPU): ...`, `Test(GPU): ...`, `Docs(Spec1b): ...`), body explaining the cause. This has been the user's rule
  since 2026-10-17; older commits from the previous game's era are in Portuguese.
- C is compiled with `-std=c11 -Wall -Wextra -Werror`; code style is dense (short lines joined with
  `;`, few braces) — match the surrounding file. Comments explain why, often referencing the
  file that depends on a contract.
- User-facing docs: `README.md` (English) and `README.es.md` (Spanish) must stay in sync, with
  `packaging/windows/README-Windows.txt` for the Windows package;
  progress lives in `docs/tsuki/ROADMAP.md` (Spanish; `docs/gow3/ROADMAP.md` is the previous game's, in Portuguese).
- Repo is only local, so u can do commits or create a branch to test something