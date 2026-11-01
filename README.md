# Project Blue Moon

**English** | [Español](README.es.md)

A native PC port of **TSUKIHIME -A piece of blue glass moon-** (PlayStation 4, CUSA47999, version 01.00) for
Linux and Windows. It needs your own dump of the game: no game files are included.

The CPU side is native: the game's original executable runs directly on the PC's processor (the PS4 uses the
same x86-64 architecture), and a runtime written for this port replaces the PS4 system libraries, so there is
no CPU emulator. The graphics are translated: the [shadPS4](https://github.com/shadps4-emu/shadPS4) video core
converts the PS4 GPU commands and shaders to Vulkan. See [Credits](#credits) for the projects this one comes from.

> **No game files are included.** You need a dump of your own copy of the game.
> This project is not affiliated with TYPE-MOON, Aniplex or Sony Interactive Entertainment.
> TSUKIHIME is a work of TYPE-MOON; the PlayStation 4 edition is published by Aniplex.

Progress is tracked (in Spanish) in [docs/tsuki/ROADMAP.md](docs/tsuki/ROADMAP.md).

## Alpha status

This is an **Alpha**: the game is playable from the start up to the **title of chapter 1**, in English.

- Logos, the opening and the prologue videos (H.264), with sound.
- Text, backgrounds, sprites and transitions.
- Audio: music, voices and effects (ATRAC9 and HEVAG).
- Mouse, controller (DualSense, Xbox and others, through SDL3) and keyboard, at the same time.
- Saving and loading. The launcher backs the saves up before every start.

Only English is tested. The launcher's **Game language** also offers Japanese and Chinese, the game's other
languages, but nobody has played them with this port. Routes past chapter 1 have not been checked yet.

**Platforms**
- **Linux:** tested on Bazzite 44 (Fedora-based), KDE Plasma 6 on Wayland, AMD Radeon RX 6650 XT (RADV),
  Ryzen 5 7600.
- **Windows 10 and 11:** built from the same sources. So far it has been tested under Wine 11 on Linux, where the
  launcher recognises the game and the game reaches the title menu; it has **not been tested on a real Windows PC
  yet**. If you try it, please open an issue with your `last_run.log` (see below).

## Requirements

- Linux, or Windows 10 (1903 or newer) / Windows 11, 64-bit.
- A 64-bit processor with AVX: Intel Sandy Bridge (2011) or newer, AMD FX or newer. On CPUs without MOVBE, BMI1
  or LZCNT (Sandy/Ivy Bridge, AMD FX) the game's instructions that use them are rewritten to emulated stubs and
  the log shows an `ISA:` line; speed on those CPUs has not been measured. Older CPUs stop at start with a
  `STOP: this CPU lacks` line in the log.
- A graphics card with **Vulkan 1.3** and an up-to-date driver.
- Your dump of TSUKIHIME -A piece of blue glass moon- (**CUSA47999, version 01.00**), decrypted: a folder that
  contains `eboot.bin`, `sce_module` and `sce_sys`. The launcher checks the serial and refuses any other game.

## Download and play

Download the package for your system from the [Releases page](https://github.com/Waffle631/Project-Blue-Moon/releases).

### Windows

1. Download `project-blue-moon-windows.zip` and extract it to a folder you can write to, for example
   `C:\Games\project-blue-moon-windows`. Do not run it from inside the zip, and avoid `Program Files`: saves and
   settings are written next to the program.
2. Open **`Project Blue Moon.exe`**. If Windows shows "Windows protected your PC", click **More info**, then
   **Run anyway**: the program is not digitally signed.
3. Click **Choose the game folder**. It takes you to the **Game** page: next to **Game folder**, click
   **Browse…** and select the folder with `eboot.bin`. A folder named `CUSA47999` next to the extracted folder is
   found on its own.
4. Back on **Home**, the game's cover and "CUSA47999 · version 01.00" appear. Click **PLAY**. The first start
   takes a little longer while the game image is prepared.

**Next time:** **`Play Project Blue Moon.exe`** starts the game straight away with your saved settings.
**Advanced** has buttons for a desktop shortcut and for adding the game to Steam (close Steam first).

**Files:** saves, the shader cache and `last_run.log` are in the `user` folder, and the game's settings in
`tsuki.ini`, both next to `Project Blue Moon.exe`. Keep them when you update. The launcher's own settings are in
`%APPDATA%\tsuki-launcher`.

### Linux

1. Download `project-blue-moon-x86_64.AppImage`, make it executable (`chmod +x`) and run it.
2. Click **Choose the game folder**, select the folder with `eboot.bin`, then click **PLAY**.

`project-blue-moon-x86_64.AppImage --play` starts the game with the saved settings and no launcher window.

- **Files:** saves, settings, the shader cache and the log are in `~/.local/share/tsuki` (`last_run.log` is in
  its `user` folder); the launcher's own settings in `~/.config/tsuki-launcher`.
- **Menu entry:** the AppImage writes `~/.local/share/applications/tsuki.desktop` when it starts, so the
  launcher and the game window show their icon; **Advanced → Linux** turns it off and removes the entry.
- **Advanced → Linux** also has the window system (automatic, Wayland or X11), GameMode and MangoHud. Only the
  tools installed on the system can be turned on.
- **Steam:** **Advanced → Add to Steam** adds Project Blue Moon as a non-Steam game, with its pictures made from
  the dump. Close Steam first (it rewrites its shortcuts when it exits).

### Before the first start

With no save, the game opens with the prologue video (about 6 and a half minutes), which buttons do not skip.
After the first save, the game starts at the title menu.

### If something goes wrong

- Black screen at start: **Advanced → Clear shader cache**, then start again.
- The game does not open, or stops: look at the **Log** section on **Advanced**, and attach `last_run.log` to
  your report on the [Issues page](https://github.com/Waffle631/Project-Blue-Moon/issues).

## While playing

- **Insert** on the keyboard, or **R3 + L2** on the controller, opens the port's menu, with three tabs:
  - **Image:** display mode, VSync, FSR 1 scaling and RCAS sharpening, with the game image and window sizes.
  - **Performance:** frames queued and background shader compiling.
  - **Overlay:** a performance overlay with FPS, frame time, GPU (name and use), CPU, RAM and VRAM, each
    optional, with font size, corner, background opacity and layout. The numbers are the game's own use, not the
    whole system's. **Graphics → Show performance overlay** in the launcher turns it on too.
- In the menu: L1/R1 or Q/E switch tabs.
- **Mouse:** the game's own cursor follows the pointer.

### Keyboard controls

Defaults; change them on the launcher's **Game → Controls**.

| Key | Button |
|---|---|
| WASD | left stick |
| Arrows | right stick |
| Space | Cross |
| Left Shift | Circle |
| E | Square |
| Q | Triangle |
| 1 and 3 | L1 and R1 |
| R and F | L2 and R2 |
| Z and C | L3 and R3 |
| I, K, J, L | d-pad |
| Enter | Options |
| Tab | touchpad click |
| Insert | the port's menu (R3+L2 on the controller) |

## Building from source

The main branch is `tsuki/main`. Clone with the submodules:

```bash
git clone --recursive https://github.com/Waffle631/Project-Blue-Moon
```

### Linux

Dependencies through pkg-config: Vulkan, SDL3 and FFmpeg (with an H.264 decoder: the videos need it), plus
CMake, Ninja, clang, Python 3 with Tk and Pillow. The full Fedora package list is in
[docs/ENTORNO.md](docs/ENTORNO.md) (Spanish); on NixOS `shell.nix` provides them.

```bash
bash build.sh                                         # out/gpu/libtsukigpu.so and out/tsuki-probe
TSUKI_GAME_DIR=/path/to/CUSA47999 python3 run.py      # the game, without the launcher
python3 launcher/tsuki_launcher_win.py                # the launcher
bash packaging/linux/build.sh                         # dist/project-blue-moon-x86_64.AppImage
```

The AppImage needs PyInstaller, iced-x86, patchelf and `file` ([docs/ENTORNO.md](docs/ENTORNO.md) §9).

### Windows

In the **MSYS2 CLANG64** shell, with the packages listed in
[packaging/windows/README.md](packaging/windows/README.md):

```bash
bash build.sh                       # out/tsuki-probe.exe, out/tsuki-gpu-capabilities.exe, out/tsuki-play.exe
bash packaging/windows/package.sh   # dist/project-blue-moon-windows/ and its .zip
```

### Windows, cross-built from Linux

`bash build.sh --windows` downloads [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) and the MSYS2 CLANG64
packages into `out/cross/` (pinned versions with their SHA256) and builds the same three `.exe` files.
`bash packaging/windows/package.sh` then makes the `.zip` on Linux too: it installs a Windows Python into a Wine
prefix and freezes the launcher there. Details in [packaging/windows/README.md](packaging/windows/README.md).

## Tests

```bash
python3 -m unittest discover -s tests
bash build.sh --test        # Linux: the C tests, then tools/check_rename.sh
ninja -C out/gpu graphics-settings-test && out/gpu/graphics-settings-test
```

The C++ tests are targets in [gpu/CMakeLists.txt](gpu/CMakeLists.txt). To check that every system function the
game uses is implemented, without starting it:

```bash
out/tsuki-probe out/boot-linked.bin --app0 /path/to/CUSA47999 --check-imports
```

It lists three functions on purpose (`scePthreadCancel`, `sceKernelGetModuleInfoForUnwind`,
`sceKernelInternalMemoryGetModuleSegmentInfo`): they would stop the game with a `STOP:` line if it called them,
and it has not on the tested route.

## How it works

| Part | Where | What it does |
|---|---|---|
| Preparation | `scripts/` | Reads `eboot.bin`, applies its relocations and links the game's own `libc` and `libSceFios2` into one image |
| Loader | `src/probe.c` | Loads the image below 1 TiB, resolves every PS4 import and starts the game's original code. An import with no implementation stops the game with a `STOP:` line |
| Runtime | `src/runtime_*.c` | Reimplements the PS4 system functions: memory, threads, files, audio decoding, controller, mouse, saves and services, as an offline single-user console |
| GPU | `gpu/shadps4/` | The shadPS4 video core: GPU commands and shaders to Vulkan, and the video player (`sceAvPlayer`) |
| In-game menu | `gpu/shim/tsuki_overlay.cpp` | The Dear ImGui menu and performance overlay; settings in `tsuki.ini` |
| Launcher | `launcher/tsuki_launcher_win.py` | Game folder, settings, saves, controls and the play button |

More documentation (in Spanish): [audio decoding](docs/audiodec.md), [video player](docs/avplayer.md),
[build environment](docs/ENTORNO.md) and the [roadmap](docs/tsuki/ROADMAP.md).

## Credits

- **deadinside28**: [bloodborne_pc](https://github.com/deadinside28/bloodborne_pc), the original native port.
- **Supermedo** (Mohammed Albarghouthi): [bloodborne_pc for Windows](https://github.com/Supermedo/bloodborne_pc).
- **Felipe Prado**: [the God of War III Remastered port](https://github.com/FelipePrado0/gow3_pc), built on Supermedo's, from which this port was retargeted. <!-- rename-check: ok -->
- **shadPS4 team**: the [shadPS4](https://github.com/shadps4-emu/shadPS4) video core, which does all the GPU and
  shader translation and the video playback.
- **cuesta4**: the shadPS4 rendering fixes from [the `eltutz` branch](https://github.com/cuesta4/shadPS4/tree/eltutz)
  (GNM blend rewrite, depth/stencil state, image aliasing), which the vendored core keeps.
- [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9) (ATRAC9 audio) and [vgmstream](https://github.com/vgmstream/vgmstream)
  (the HEVAG decoder in `third_party/vgmstream_hevag`).

Also used: [FFmpeg](https://ffmpeg.org), [SDL3](https://github.com/libsdl-org/SDL),
[Dear ImGui](https://github.com/ocornut/imgui), [sirit](https://github.com/shadps4-emu/sirit),
[magic_enum](https://github.com/Neargye/magic_enum), [miniz](https://github.com/richgel999/miniz),
[xbyak](https://github.com/herumi/xbyak),
[Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator),
[MSYS2](https://www.msys2.org), [LLVM](https://llvm.org), [llvm-mingw](https://github.com/mstorsjo/llvm-mingw),
[PyInstaller](https://pyinstaller.org) and [Pillow](https://python-pillow.org).

## License

GNU GPL v2 or later ([LICENSE](LICENSE)). Third-party components keep their own licenses.
