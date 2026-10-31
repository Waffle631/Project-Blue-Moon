#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prepares the game image and starts tsuki-probe (Windows and Linux; run.sh wraps it on Linux).

Environment variables (TSUKI_GAME_DIR, TSUKI_DATA_DIR, TSUKI_PATCHES_DIR, TSUKI_MODS_*,
TSUKI_USER_DIR, ...). Extra arguments go to tsuki-probe.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys

WINDOWS = sys.platform == 'win32'
EXE_SUFFIX = '.exe' if WINDOWS else ''
if WINDOWS:
    import ctypes

PORT = Path(__file__).resolve().parent


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(1)


def no_console():
    """Started without a console (the launcher): console programs must not open their own."""
    if not WINDOWS:
        return 0
    return 0 if ctypes.windll.kernel32.GetConsoleWindow() else subprocess.CREATE_NO_WINDOW


def parse_cpu_features(output):
    """Instruction sets `tsuki-probe --cpu-features` reports missing ([] when complete)."""
    for line in output.splitlines():
        if line.startswith('CPU: missing '):
            return [name.strip() for name in line[len('CPU: missing '):].split(',') if name.strip()]
    return []


def isa_rewrite_wanted(missing, env):
    """TSUKI_ISA_REWRITE=1 forces the ISA rewrite table, 0 disables it; otherwise the CPU decides."""
    policy = env.get('TSUKI_ISA_REWRITE')
    if policy in ('0', '1'):
        return policy == '1'
    return bool(missing)


def run_script(name, *args, capture=False, check=True):
    # Inside the packaged launcher executable (PyInstaller) the executable runs scripts itself.
    script = ['--script'] if getattr(sys, 'frozen', False) else []
    command = [sys.executable, *script, str(PORT / 'scripts' / name), *map(str, args)]
    env = {**os.environ, 'PYTHONIOENCODING': 'utf-8'}
    if capture:
        result = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, text=True,
                                encoding='utf-8', env=env, creationflags=no_console())
        if not check:
            return result.returncode, result.stdout
        if result.returncode:
            sys.exit(result.returncode)
        return result.stdout
    # stdin given: the output handles are passed explicitly (a windowed launcher child would
    # get none otherwise).
    result = subprocess.run(command, stdin=subprocess.DEVNULL, env=env, creationflags=no_console())
    if result.returncode:
        sys.exit(result.returncode)
    return None


def launch_prefix(env):
    """Linux integrations chosen in the launcher: MangoHud is its Vulkan layer (an environment
    variable), GameMode a command that starts the game."""
    if WINDOWS:
        return []
    if env.get('TSUKI_MANGOHUD') == '1':
        env.setdefault('MANGOHUD', '1')
    if env.get('TSUKI_GAMEMODE') == '1':
        if gamemode := shutil.which('gamemoderun'):
            return [gamemode]
        print('GameMode: gamemoderun not found, starting without it', flush=True)
    return []


def find_executable(name):
    """bin/ (packaged), else out/ (built with build.sh)."""
    for candidate in (PORT / 'bin' / name, PORT / 'out' / name):
        if candidate.is_file():
            return candidate
    return None


def main():
    args = sys.argv[1:]
    # As run.sh: relative paths (TSUKI_GAME_DIR, TSUKI_DATA_DIR) are from the port.
    os.chdir(PORT)
    env = os.environ
    data = Path(env.get('TSUKI_DATA_DIR', PORT))
    out = data / 'out'
    out.mkdir(parents=True, exist_ok=True)
    env.setdefault('TSUKI_CONFIG', str(data / 'tsuki.ini'))
    # CPU reads of images the GPU wrote use the previous frame's copy instead of waiting (texture_cache.h).
    # Tsukihime makes none on the recorded route (Spec 8 §5), so there is no option for them.
    env.setdefault('TSUKI_DEFERRED_READBACK', '1')
    env.setdefault('TSUKI_STALE_READBACK', '1')
    sys.path.insert(0, str(PORT / 'scripts'))

    probe = Path(env['TSUKI_PROBE']) if env.get('TSUKI_PROBE') else find_executable('tsuki-probe' + EXE_SUFFIX)
    if not probe or not probe.is_file():
        fail(f'tsuki-probe{EXE_SUFFIX} not found: build it with build.sh or use a packaged build.')
    # Development builds run from the MSYS2 tree: its CLANG64 DLLs (SDL3, FFmpeg, ...).
    if WINDOWS and not (probe.parent / 'SDL3.dll').is_file():
        clang64 = Path(env.get('MSYS2_ROOT', r'C:\msys64')) / 'clang64' / 'bin'
        if clang64.is_dir():
            env['PATH'] = f'{clang64}{os.pathsep}{env.get("PATH", "")}'
    # Instruction sets this CPU lacks (MOVBE, BMI1, LZCNT): the ISA rewrite covers them; exit 20
    # is a set nothing covers.
    features = subprocess.run([str(probe), '--cpu-features'], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                              text=True, creationflags=no_console())
    if features.returncode == 20:
        print(features.stdout, end='', flush=True)
        sys.exit(20)
    missing = parse_cpu_features(features.stdout)

    game = Path(env.get('TSUKI_GAME_DIR', PORT / 'Tsukihime' / 'CUSA47999'))
    if not (game / 'eboot.bin').is_file():
        fail(f'No eboot.bin in {game} (set TSUKI_GAME_DIR).')
    update_view = None
    if env.get('TSUKI_PATCH_DIR'):
        import game_update
        try:
            update_view = game_update.create_view(game, env['TSUKI_PATCH_DIR'], out)
        except (ValueError, OSError) as error:
            fail(str(error))
        game = update_view
        print(f'Game and update combined from {env["TSUKI_PATCH_DIR"]}', flush=True)
    original_game = game.resolve()
    game = Path(run_script('mods.py', game, '--out', out,
                           '--mods-dir', env.get('TSUKI_MODS_DIR', data / 'mods'),
                           '--config', env.get('TSUKI_MODS_CONFIG', data / 'mods.json'),
                           '--enabled', env.get('TSUKI_MODS_ENABLED', '0'), capture=True).strip())
    mod_view = game if game.resolve() != original_game else None
    try:
        run_script('prepare.py', game, '--out', out)
        run_script('link_libc.py', game, '--out', out)
        run_script('link_modules.py', game, '--out', out)
        # A failed rewrite prints its reason and the game starts without the table.
        isa_args = []
        if isa_rewrite_wanted(missing, env):
            code, output = run_script('isa_rewrite.py', out / 'boot-linked.bin', '--out', out,
                                      capture=True, check=False)
            print(output, end='', flush=True)
            if code == 0:
                isa_args = ['--isa-rewrite', str(out / 'isa-rewrite.bin')]
        run_script('content_profile.py', game, '--out', out, '--sku', env.get('TSUKI_CONTENT_SKU', 'full'))

        # The window shows the game's icon (BMP: the format SDL loads without extra libraries); on
        # Linux the launcher's own icon (the AppImage ships the one chosen when it was built) and
        # an application id the desktop entry matches, so the shell shows the same icon.
        icon_file = game / 'sce_sys' / 'icon0.png'
        if not WINDOWS:
            env.setdefault('SDL_APP_ID', 'tsuki')
            if (PORT / 'launcher' / 'tsuki.png').is_file():
                icon_file = PORT / 'launcher' / 'tsuki.png'
        try:
            from PIL import Image
            with Image.open(icon_file) as icon:
                icon.convert('RGBA').resize((64, 64), Image.LANCZOS).save(out / 'window_icon.bmp')
            env.setdefault('TSUKI_WINDOW_ICON', str(out / 'window_icon.bmp'))
        except (ImportError, OSError):
            pass
        run_script('patches.py', '--out', out, '--game-dir', game,
                   '--patches-dir', env.get('TSUKI_PATCHES_DIR', data / 'patches'),
                   '--patches-config', env.get('TSUKI_PATCHES_CONFIG', data / 'patches.json'))
        env.setdefault('TSUKI_VBLANK_HZ', '60')

        # GPU caches (shaders, pipelines) beside the saves; read before main(), so set here.
        user_dir = Path(env.get('TSUKI_USER_DIR', data / 'user')).resolve()
        user_dir.mkdir(parents=True, exist_ok=True)
        env.setdefault('TSUKI_GPU_USER_DIR', str(user_dir))
        from save_backup import backup
        try:
            if made := backup(user_dir):
                print(f'Saves backed up to {made}', flush=True)
        except OSError as error:
            print(f'Save backup failed, starting anyway: {error}', flush=True)
        command = [*launch_prefix(env), str(probe), str(out / 'boot-linked.bin'), '--content-profile', str(out / 'content.bin'),
                   '--patches', str(out / 'patches.bin'), '--app0', str(game),
                   '--user', str(user_dir),
                   '--timeout', env.get('TSUKI_TIMEOUT', '0'), *isa_args, *args]
        # No stdin: an inherited pipe (shells such as Git Bash) cost the game its console output.
        return subprocess.run(command, stdin=subprocess.DEVNULL, creationflags=no_console()).returncode
    finally:
        if mod_view:
            shutil.rmtree(mod_view, ignore_errors=True)
        if update_view:
            game_update.remove_view(update_view)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
