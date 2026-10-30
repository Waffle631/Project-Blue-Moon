#!/usr/bin/env bash
# tools/compare_captures.sh a.png b.png: pixel-exact comparison with ffmpeg's psnr filter, the oracle
# for the before/after captures of Specs 1b and 6 (docs/specs/2026-10-14-hito-primer-fotograma.md, H3).
# Prints "idéntico" (exit 0) when the average PSNR is inf, i.e. every pixel matches; otherwise
# "diferente: PSNR=<dB>" (exit 1). No tolerance: a one-pixel change is a difference. Usage or input
# errors (missing file, not an image, different sizes) go to stderr and exit 2.
set -u
if (($# != 2)); then echo "uso: $0 a.png b.png" >&2; exit 2; fi
for tool in ffmpeg ffprobe; do
    command -v "$tool" > /dev/null || { echo "error: falta $tool" >&2; exit 2; }
done
for f in "$1" "$2"; do [[ -f $f ]] || { echo "error: no existe $f" >&2; exit 2; }; done
# file: keeps names that start with '-' or contain ':' from being read as options or protocols.
size() { ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x "file:$1"; }
a=$(size "$1") && [[ -n $a ]] || { echo "error: $1 no es una imagen" >&2; exit 2; }
b=$(size "$2") && [[ -n $b ]] || { echo "error: $2 no es una imagen" >&2; exit 2; }
# The capture size follows the window (TSUKI_FULLSCREEN=1 for reference captures), and psnr needs both equal.
[[ $a == "$b" ]] || { echo "error: tamaños distintos ($a y $b)" >&2; exit 2; }
out=$(ffmpeg -hide_banner -nostats -i "file:$1" -i "file:$2" -lavfi psnr -f null - 2>&1) ||
    { echo "error: ffmpeg falló:" >&2; tail -n 5 <<< "$out" >&2; exit 2; }
avg=$(grep -o 'average:[^ ]*' <<< "$out" | tail -n 1); avg=${avg#average:}
[[ -n $avg ]] || { echo "error: ffmpeg no dio PSNR" >&2; tail -n 5 <<< "$out" >&2; exit 2; }
if [[ $avg == inf ]]; then echo "idéntico"; exit 0; fi
echo "diferente: PSNR=$avg"; exit 1
