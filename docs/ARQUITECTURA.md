# Arquitectura: runtime C, GPU y superficie específica del juego

El resumen del pipeline de lanzamiento sigue en CLAUDE.md.

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

