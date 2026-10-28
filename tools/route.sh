#!/usr/bin/env bash
# tools/route.sh record | replay <name>: the recorded route of Spec 6 block A
# (docs/specs/2026-10-14-spec-6-2d-raton-saves.md A1, A4). Everything lives in out/route/ (not versioned):
#   save/       fixed copy of a save (save/savedata/...); every run starts from it in a fresh user/, so neither
#               the user's own user/ nor its user/savedata_backups/ is touched
#   gow3.ini    the route's settings: the defaults, except async_shaders=0 (a shader still compiling would skip a draw)
#   pad.txt     the recording (GOW3_PAD_RECORD, clocked in pad reads: src/runtime_pad.c)
#   frames.txt  how each frame is compared (tools/compare_route.sh); record writes it from the F12 captures
#   record/, <name>/, <name>.log   the captures and the log of each run
# record: the user plays (pad or keyboard, no mouse: GOW3_MOUSE=0) and presses F12 on each scene for the
# reference, then closes the window. replay <name>: plays pad.txt back, captures every frame of frames.txt into
# <name>/ and closes the game once the replay is over. Exit 0, 1 when the game stops (STOP:/Fault:) or a capture is
# missing, 2 on a usage error. Runs inside the distrobox (re-entered from the host when it is not).
set -u
PORT=$(cd -- "$(dirname -- "$0")/.." && pwd) && cd "$PORT" || exit 2
if [[ ! -e /run/.containerenv ]] && command -v distrobox > /dev/null; then
    exec distrobox enter ps4dev -- bash "$PORT/tools/route.sh" "$@"
fi
R=$PORT/out/route
usage() { echo "uso: $0 record | replay <nombre>" >&2; exit 2; }
case ${1:-} in
    record) (($# == 1)) || usage; name=record
            [[ ! -e $R/pad.txt && ! -e $R/record ]] || { echo "error: ya hay una grabación ($R/pad.txt o $R/record/); muévela antes" >&2; exit 2; } ;;
    replay) (($# == 2)) || usage; name=$2
            [[ $name =~ ^[A-Za-z0-9_-]+$ && $name != record && $name != save && $name != user ]] || { echo "error: nombre no válido: $name" >&2; exit 2; }
            [[ -f $R/pad.txt && -f $R/frames.txt ]] || { echo "error: falta $R/pad.txt o $R/frames.txt (primero: $0 record)" >&2; exit 2; } ;;
    *) usage ;;
esac
[[ -d $R/save ]] || { echo "error: no existe $R/save/: copia ahí la partida fija (cp -a user/savedata $R/save/)" >&2; exit 2; }
# frames.txt's frame numbers, comma-separated for GOW3_SCREENSHOT_AT.
frame_list() { awk '{ sub(/#.*/, ""); if (NF) { if ($1 !~ /^[0-9]+$/) exit 1; print $1 + 0 } }' "$R/frames.txt" | paste -sd,; }
missing() { local n; for n in ${1//,/ }; do compgen -G "$R/$name/frame_$(printf '%06d' "$n")_*.png" > /dev/null || echo "$n"; done; }

rm -rf "$R/user" && mkdir -p "$R/user" && cp -a "$R/save/." "$R/user/" || { echo "error: no se pudo restaurar $R/user/" >&2; exit 2; }
[[ -f $R/gow3.ini ]] || printf '%s\n' '# gow3 settings of the recorded route (tools/route.sh): the defaults, except async_shaders' \
    render_resolution=native engine_fps=120 deferred_readback=1 stale_readback=1 async_shaders=0 > "$R/gow3.ini"
log=$R/$name.log; extra=()
if [[ $name == record ]]; then
    extra=(GOW3_PAD_RECORD="$R/pad.txt" GOW3_SCREENSHOT_DIR="$R/record")
else
    frames=$(frame_list) || { echo "error: $R/frames.txt tiene un fotograma que no es un número" >&2; exit 2; }
    rm -rf "${R:?}/$name"; extra=(GOW3_PAD_REPLAY="$R/pad.txt" GOW3_SCREENSHOT_DIR="$R/$name" GOW3_SCREENSHOT_AT="$frames")
fi
# Nothing scripted from the caller's shell gets in: only this run's input reaches the game.
env -u GOW3_PAD_FILE -u GOW3_PAD_RECORD -u GOW3_PAD_REPLAY -u GOW3_SCREENSHOT_AT -u GOW3_SCREENSHOT_DIR \
    GOW3_GAME_DIR=Tsukihime/CUSA47999 GOW3_FULLSCREEN=1 GOW3_MOUSE=0 GOW3_USER_DIR="$R/user" GOW3_CONFIG="$R/gow3.ini" \
    GOW3_FILE_TRACE=1 "${extra[@]}" python3 run.py > "$log" 2>&1 &
game=$!
# SIGTERM is the window close (report and exit); never pkill -f, which would match this script too.
close() { pkill -TERM -x gow3-probe; wait "$game"; }
stopped() { grep -q -E 'STOP:|Fault:' "$log"; }
echo "route $name: log in $log"
if [[ $name == record ]]; then
    echo "route record: no pulses nada hasta el menú de título; F12 en cada escena de la referencia; no guardes partida; F9 para la grabación; cierra la ventana al acabar"
    while kill -0 "$game" 2> /dev/null; do stopped && { close; echo "route record: el juego paró (STOP:/Fault:), mira $log" >&2; exit 1; }; sleep 0.2; done
    wait "$game"; stopped && { echo "route record: el juego paró (STOP:/Fault:), mira $log" >&2; exit 1; }
    if [[ ! -e $R/frames.txt ]]; then
        { echo "# tools/route.sh record, $(date -I): exact by default; Task 3 classifies each frame"
          for f in "$R"/record/frame_*_*.png; do [[ -f $f ]] || continue; b=${f##*/frame_}; echo "$((10#${b%%_*})) exact"; done | sort -n
        } > "$R/frames.txt"
    fi
    echo "route record: $(($(wc -l 2> /dev/null < "$R/pad.txt" || echo 1) - 1)) muestras en $R/pad.txt, $(grep -c ' exact$' "$R/frames.txt") fotogramas en $R/frames.txt"
    exit 0
fi
until grep -q 'pad replay finished' "$log"; do
    stopped && { close; echo "route $name: el juego paró (STOP:/Fault:), mira $log" >&2; exit 1; }
    grep -q 'pad replay: .* has no' "$log" && { close; echo "route $name: $R/pad.txt no tiene la cabecera 'reads'" >&2; exit 1; }
    kill -0 "$game" 2> /dev/null || { wait "$game"; echo "route $name: el juego acabó antes que la repetición, mira $log" >&2; exit 1; }
    sleep 0.2
done
# The last captures may still be on their way to disk when the replay ends.
for ((i = 0; i < 50; i++)); do [[ -z $(missing "$frames") ]] && break; sleep 0.2; done
lost=$(missing "$frames" | paste -sd' '); close
stopped && { echo "route $name: el juego paró (STOP:/Fault:), mira $log" >&2; exit 1; }
[[ -z $lost ]] || { echo "route $name: faltan capturas de los fotogramas $lost" >&2; exit 1; }
echo "route $name: repetición completa, capturas en $R/$name/"
