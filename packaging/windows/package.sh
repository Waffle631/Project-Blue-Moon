#!/usr/bin/env bash
# Builds dist/project-blue-moon-windows/ (and its .zip): Project Blue Moon.exe (the launcher, frozen
# with PyInstaller so players need no Python), tsuki-probe.exe with the CLANG64 DLLs it needs,
# the preparation scripts and run.py. Freezing uses a Windows Python 3.10+ (python.org; WINPYTHON
# overrides) and a private venv in out/pyenv with PyInstaller and Pillow (the game icon and the Steam
# pictures need it). Two ways to run it:
#  - an MSYS2 CLANG64 shell after `bash build.sh`;
#  - Linux after `bash build.sh --windows` (the cross build, docs/ENTORNO.md §8): the Windows Python runs
#    under Wine in out/wine-prefix (WINEPREFIX overrides), installed there on the first run. Wine is `wine`
#    when it is on the PATH, else the org.winehq.Wine Flatpak; WINE names another command.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
[[ -f out/tsuki-probe.exe && -f out/tsuki-gpu-capabilities.exe && -f out/tsuki-play.exe ]] || {
    echo 'Build first: bash build.sh (bash build.sh --windows on Linux)' >&2; exit 1; }

if [[ $(uname -s) == Linux ]]; then
    cross=${TSUKI_CROSS_DIR:-out/cross}
    tools=$cross/llvm-mingw/bin
    export WINEPREFIX=${WINEPREFIX:-$PWD/out/wine-prefix} WINEDEBUG=${WINEDEBUG:--all}
    if [[ -n ${WINE:-} ]]; then
        read -ra wine <<<"$WINE"
    elif command -v wine >/dev/null; then
        wine=(wine)
    else  # the Flatpak sees only the repository, and takes the prefix and the folder on the command line
        wine=(flatpak run --filesystem="$PWD" --env=WINEPREFIX="$WINEPREFIX" --env=WINEDEBUG="$WINEDEBUG"
              --cwd="$PWD" --command=wine org.winehq.Wine)
    fi
    winpath() { local p; p=$(realpath -m -- "$1"); printf 'Z:%s' "${p//\//\\}"; }
    windows() { "${wine[@]}" "$@"; }
    mkdir -p "$WINEPREFIX"
    python=${WINPYTHON:-$WINEPREFIX/drive_c/Python314/python.exe}
    if [[ ! -f $python ]]; then
        # python.org publishes sigstore signatures, not hashes: this is the SHA256 of the file it served.
        version=3.14.10 sha256=92480239264744fe0386a1bd8275786fefa126e3129e56a68e22b52c73329965
        installer=$cross/downloads/python-$version-amd64.exe
        if [[ ! -f $installer ]]; then
            mkdir -p "$cross/downloads"
            curl -fL -o "$installer.part" "https://www.python.org/ftp/python/$version/python-$version-amd64.exe"
            mv -f "$installer.part" "$installer"
        fi
        echo "$sha256  $installer" | sha256sum -c --quiet
        windows "$installer" /quiet InstallAllUsers=0 'TargetDir=C:\Python314' Include_test=0 Include_launcher=0 \
            Shortcuts=0 AssociateFiles=0 PrependPath=0
        [[ -f $python ]] || { echo "Python did not install into $python" >&2; exit 1; }
    fi
else
    source ./msys2-env.sh
    tools=$(dirname -- "$(command -v llvm-strip)")
    winpath() { cygpath -w "$1"; }
    windows() { "$@"; }
    # A Windows Python (not MSYS2's) for PyInstaller.
    python=${WINPYTHON:-}
    if [[ -z $python ]]; then
        for candidate in /c/Python3*/python.exe "${LOCALAPPDATA:-/c/Users/$USER/AppData/Local}"/Programs/Python/Python3*/python.exe; do
            [[ -x $candidate ]] && python=$candidate
        done
    fi
    [[ -n $python ]] || { echo 'Need a Windows Python 3 (python.org) or WINPYTHON=path\to\python.exe' >&2; exit 1; }
    # Windows Python needs USERPROFILE (some MSYS2 shells start without it).
    export USERPROFILE=${USERPROFILE:-$(cygpath -w "/c/Users/$(id -un)")}
fi
if [[ ! -f out/pyenv/Scripts/python.exe ]]; then
    windows "$python" -m venv "$(winpath out/pyenv)"
fi
windows out/pyenv/Scripts/python.exe -m pip install -q --disable-pip-version-check pyinstaller pillow iced-x86==1.21.0
# The scripts run inside Project Blue Moon.exe (--script): the standard modules they import come along.
hidden=()
for module in argparse base64 collections hashlib json re shutil struct tempfile xml.etree.ElementTree \
              urllib.request ctypes.wintypes iced_x86; do
    hidden+=(--hidden-import "$module")
done
windows out/pyenv/Scripts/python.exe -m PyInstaller --noconfirm --clean --log-level WARN --windowed --copy-metadata pillow \
    --name "Project Blue Moon" --icon "$(winpath launcher/tsuki.ico)" --distpath "$(winpath out/pyi-dist)" \
    --workpath "$(winpath out/pyi-work)" --specpath "$(winpath out/pyi-work)" --paths "$(winpath scripts)" "${hidden[@]}" \
    "$(winpath launcher/tsuki_launcher_win.py)"

# The package is assembled in a fresh staging folder and zipped from there; dist/project-blue-moon-windows
# (a playable copy that may hold saves and settings) is only refreshed afterwards.
dest=out/stage/project-blue-moon-windows
rm -rf -- out/stage
mkdir -p "$dest/bin" "$dest/launcher"
cp -r "out/pyi-dist/Project Blue Moon/." "$dest/"
"$tools/llvm-strip" -o "$dest/Play Project Blue Moon.exe" out/tsuki-play.exe
cp launcher/tsuki.ico launcher/tsuki.png launcher/gowfont.ttf "$dest/launcher/"
# The executables without debug information (out/ keeps the symbols for crash reports).
for exe in tsuki-probe.exe tsuki-gpu-capabilities.exe; do
    "$tools/llvm-strip" --strip-debug -o "$dest/bin/$exe" "out/$exe"
done
# Every DLL the executables load from the CLANG64 tree (SDL3, FFmpeg, Vulkan loader, ...).
if [[ -n ${cross:-} ]]; then
    # No ldd for Windows programs here: each import table is followed through llvm-mingw (libc++,
    # libunwind, winpthreads, which the cross build links) and the sysroot. What neither has is Windows'.
    declare -A seen
    queue=("$dest/bin/tsuki-probe.exe" "$dest/bin/tsuki-gpu-capabilities.exe")
    while ((${#queue[@]})); do
        file=${queue[0]}
        queue=("${queue[@]:1}")
        while read -r dll; do
            [[ ${seen[${dll,,}]:-} ]] && continue
            seen[${dll,,}]=1
            for dir in "$cross/llvm-mingw/x86_64-w64-mingw32/bin" "$cross/sysroot/clang64/bin"; do
                [[ -f $dir/$dll ]] || continue
                cp "$dir/$dll" "$dest/bin/"
                queue+=("$dir/$dll")
                break
            done
        done < <("$tools/llvm-objdump" -p "$file" | awk '/DLL Name:/ {print $3}')
    done
else
    ldd "$dest/bin/tsuki-probe.exe" "$dest/bin/tsuki-gpu-capabilities.exe" |
        awk '/\/clang64\/bin\// {print $3}' | sort -u | while read -r dll; do
            cp -u "$dll" "$dest/bin/"
        done
fi
cp -r scripts "$dest/"
cp run.py LICENSE README.md packaging/windows/README-Windows.txt "$dest/"
find "$dest" -name __pycache__ -prune -exec rm -r {} +
# No game files and nothing a player made (saves, shader caches built from the game's shaders,
# settings, logs) may ship: stop when one is found.
forbidden=$(find "$dest" \( -iname eboot.bin -o -iname sce_module -o -iname sce_sys -o -iname savedata \
    -o -iname user -o -iname cache -o -iname '*.log' -o -iname tsuki.ini -o -iname settings.json \
    -o -iname mods.json -o -iname patches.json \
    -o -iname '*.spv' -o -iname '*.pkg' -o -iname '*.sprx' -o -iname '*.prx' \) -print)
if [[ -n $forbidden ]]; then
    echo "Package holds files that must not ship:" >&2
    echo "$forbidden" >&2
    exit 1
fi
mkdir -p dist
rm -f dist/project-blue-moon-windows.zip
if [[ -n ${cross:-} ]]; then
    python3 -c 'import shutil, sys; shutil.make_archive(sys.argv[1], "zip", "out/stage", "project-blue-moon-windows")' \
        dist/project-blue-moon-windows
else
    (cd out/stage && powershell -NoProfile -Command \
        "Compress-Archive -Path project-blue-moon-windows -DestinationPath ../../dist/project-blue-moon-windows.zip")
fi

# Refresh dist/project-blue-moon-windows, keeping what players create there (saves, settings, mods), and
# on Windows only while nothing runs from it: deleting a running launcher's files breaks it (on Linux,
# Wine keeps the files it has open).
play=dist/project-blue-moon-windows
running=0
if [[ -z ${cross:-} ]]; then
    running=$(powershell -NoProfile -Command \
        "@(Get-Process | Where-Object { \$_.Path -like '$(cygpath -w "$PWD/$play")\\*' }).Count" | tr -d '\r')
fi
if [[ ${running:-0} != 0 ]]; then
    echo "dist/project-blue-moon-windows is in use ($running processes): not refreshed; the zip is ready." >&2
else
    mkdir -p "$play"
    find "$play" -mindepth 1 -maxdepth 1 ! -name user ! -name mods ! -name tsuki.ini \
        ! -name mods.json ! -name patches.json -exec rm -rf {} +
    cp -r "$dest/." "$play/"
fi
du -sh "$dest" dist/project-blue-moon-windows.zip
