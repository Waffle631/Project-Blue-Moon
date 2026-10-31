# tsuki on Windows

The port builds and runs natively on 64-bit Windows 10 (1903+) / 11 with the same sources as
on Linux. Players: see `README-Windows.txt` (it ships in the package).

## Build

Install [MSYS2](https://www.msys2.org) (`winget install MSYS2.MSYS2`), open the
**MSYS2 CLANG64** shell and install the toolchain and libraries (the counterpart of
`shell.nix`):

```bash
pacman -S --needed git patch \
  mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,python} \
  mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} \
  mingw-w64-clang-x86_64-{sdl3,fmt,boost,robin-map,xxhash,ffmpeg,zydis} \
  mingw-w64-clang-x86_64-{spirv-tools,spirv-headers,spirv-cross}
```

magic_enum, miniz and xbyak (not packaged by MSYS2) are submodules under `gpu/third_party/`.
Then, in the repository:

```bash
git submodule update --init --recursive
bash build.sh                      # out/tsuki-probe.exe, out/tsuki-gpu-capabilities.exe
TSUKI_GAME_DIR=F:/path/to/CUSA47999 python run.py
python launcher/tsuki_launcher_win.py
bash packaging/windows/package.sh  # dist/tsuki-windows/ and .zip: Tsukihime.exe + DLLs
```

`run.py` is the Windows `run.sh` (same variables; works from a plain Windows Python too).
The package freezes the launcher with PyInstaller (a private venv in `out/pyenv`, from a
python.org Python) into `Tsukihime.exe`, which also runs `run.py` (`--run`), the preparation
scripts (`--script`) and the game without the window (`--play`): players need no Python.
The venv also receives `iced-x86`, which `scripts/isa_rewrite.py` uses for CPUs without MOVBE,
BMI1 or LZCNT.
`Play Tsukihime.exe` (`packaging/windows/play.c`) is a small native program that runs
`Tsukihime.exe --play` from its folder and waits for it, for players who want to start the
game directly with the saved settings.
From a source tree it adds `C:\msys64\clang64\bin` (or `$MSYS2_ROOT`) to `PATH` for the DLLs.

The launcher texts are written in English with the Russian beside them; the other languages
are in `launcher/tsuki_lang.py` (one list per language in the order of `KEYS`; a missing or
empty text falls back to English). The icon (`launcher/tsuki.ico`/`.png`) is drawn by
`packaging/windows/make_icon.py` and built into `Tsukihime.exe`, `tsuki-probe.exe` (the game
window) and `tsuki-gpu-capabilities.exe`.

## What is different from Linux

| Linux | Windows |
|---|---|
| `mov rax, fs:[0]` rewritten to `gs:[0]`, GS base set with `arch_prctl` | GS is the TEB and cannot move. The loader rewrites the displacement to `gs:[0x1480 + 8*slot]` (the TEB's `TlsSlots[slot]`, same 9-byte instruction) and each guest thread keeps its TCB in that TLS slot. The slot is claimed first thing in `main` (Vulkan drivers take many). |
| One memfd for direct + flexible memory, `mmap(MAP_FIXED)` views | A pagefile-backed section; the PS4 range 64 GiB–1 TiB is reserved as one placeholder and views replace placeholders (`VirtualAlloc2`/`MapViewOfFile3`, as in shadPS4). Partial unmaps split views and restore page protections (GPU tracking). |
| `-no-pie`, heap below 1 TiB | `--disable-high-entropy-va`: heaps and thread stacks stay low and out of the PS4 range. |
| SIGSEGV handler, `sigsetjmp` recovery | Vectored exception handler; `tsuki_setjmp`/`tsuki_longjmp` (`src/compat_win.c`) restore all Win64 callee-saved registers without SEH unwinding (guest frames have no unwind data; clang's `__builtin_longjmp` restores a wrong frame pointer on Win64). |
| Guest thread stacks from `runtime_low_map` | Windows allocates them, fully committed (`pthread_attr_setstacksize`): guest code has no stack probes and would skip the guard page. The main thread stack is 16 MiB committed. |
| glibc `nanosleep`, monotonic condition variables | High-resolution waitable timers (`compat_sleep_ns`; winpthreads' sleep rounds to 15.6 ms); semaphore deadlines use `CLOCK_REALTIME` (winpthreads rejects monotonic condition variables). |
| `libtsukigpu.so` | `libtsukigpu.a`, linked into `tsuki-probe.exe` by CMake (`gpu/CMakeLists.txt`), with a manifest for UTF-8 paths and long paths. |
| Mod overlay with symlinks | Symlinks when allowed (Developer Mode), else junctions for folders and hard links for files. |

Diagnostics added on the way: `TSUKI_FRAME_DUMP_TRIGGER=<file>` writes the guest display buffer
and the presented image of the next frame to `TSUKI_DUMP_DIR` (raw 32-bit pixels), which also
works for menus and movies.

Not ported: the watchdog/`--timeout` thread dumps (Linux signals), userfaultfd tracking
(`TSUKI_UFFD`), MangoHud, PGO (GCC flags) and
the `tests/` programs (several use Linux-only APIs).
