#!/usr/bin/env bash
# tools/compare_route.sh a/ b/ frames.txt: compares two capture folders of the recorded route (Spec 6 block A,
# docs/specs/2026-10-14-spec-6-2d-raton-saves.md A1) frame by frame with tools/compare_captures.sh.
# frames.txt has one line per frame, and what follows '#' is a comment (the scene's name):
#   <N> exact                         the whole image
#   <N> mask <x> <y> <w> <h> [...]    with those boxes filled black in both images (e.g. the "Saving…" icon)
#   <N> eye                           not compared (video, title menu background): the user judges it
# Prints "<N>: idéntico|diferente: PSNR=<dB>|a ojo". Exit 0 when every exact/mask frame is identical, 1 when one
# differs, 2 on an error: a missing capture, a malformed line, a box outside the image. The whole file is checked
# before any comparison, so a broken frames.txt prints nothing on stdout.
set -u
(($# == 3)) || { echo "uso: $0 a/ b/ frames.txt" >&2; exit 2; }
a=$1; b=$2; list=$3; here=$(dirname -- "$(realpath -- "$0")")
[[ -f $list ]] || { echo "error: no existe $list" >&2; exit 2; }
for tool in ffmpeg ffprobe; do command -v "$tool" > /dev/null || { echo "error: falta $tool" >&2; exit 2; }; done
# capture <folder> <N>: the one frame_<NNNNNN>_<w>x<h>.png of that frame (the size follows the window).
capture() {
    local found=("$1"/frame_"$(printf '%06d' "$2")"_*.png)
    [[ ${#found[@]} == 1 && -f ${found[0]} ]] || { echo "error: $1 no tiene una sola captura del fotograma $2" >&2; return 1; }
    echo "${found[0]}"
}
bad() { echo "error: $list línea $line_no: $1: $line" >&2; exit 2; }
frames=(); kinds=(); boxes=(); declare -A seen; line_no=0
while IFS= read -r line || [[ -n $line ]]; do
    ((++line_no)); read -ra f <<< "${line%%#*}"; ((${#f[@]})) || continue
    [[ ${f[0]} =~ ^[0-9]+$ ]] || bad "el fotograma no es un número"
    n=$((10#${f[0]})); [[ -z ${seen[$n]:-} ]] || bad "fotograma repetido"; seen[$n]=1
    case ${f[1]:-} in
        exact|eye) ((${#f[@]} == 2)) || bad "${f[1]} no lleva más campos" ;;
        mask) (((${#f[@]} - 2) % 4 == 0 && ${#f[@]} > 2)) || bad "mask lleva recuadros de 4 números"
              for v in "${f[@]:2}"; do [[ $v =~ ^[0-9]+$ ]] || bad "recuadro con un valor que no es un número natural"; done ;;
        *) bad "tipo desconocido (exact, mask o eye)" ;;
    esac
    frames+=("$n"); kinds+=("${f[1]}"); boxes+=("${f[*]:2}")
done < "$list"
# Every capture exists and every box fits before anything is printed: a half-written result is not a result.
filters=()
for i in "${!frames[@]}"; do
    n=${frames[i]}; x=$(capture "$a" "$n") && capture "$b" "$n" > /dev/null || exit 2; filters+=("")
    [[ ${kinds[i]} == mask ]] || continue
    size=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x "file:$x") &&
        [[ $size =~ ^([0-9]+)x([0-9]+)$ ]] || { echo "error: $x no es una imagen" >&2; exit 2; }
    width=${BASH_REMATCH[1]}; height=${BASH_REMATCH[2]}; read -ra v <<< "${boxes[i]}"; filter=format=rgb24
    for ((k=0; k<${#v[@]}; k+=4)); do
        bx=$((10#${v[k]})); by=$((10#${v[k+1]})); bw=$((10#${v[k+2]})); bh=$((10#${v[k+3]}))
        ((bw > 0 && bh > 0 && bx + bw <= width && by + bh <= height)) ||
            { echo "error: fotograma $n: recuadro $bx $by $bw $bh fuera de la imagen (${width}x$height)" >&2; exit 2; }
        filter+=,drawbox=x=$bx:y=$by:w=$bw:h=$bh:color=black:t=fill
    done
    filters[i]=$filter
done
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT; status=0
for i in "${!frames[@]}"; do
    n=${frames[i]}; x=$(capture "$a" "$n"); y=$(capture "$b" "$n")
    if [[ ${kinds[i]} == eye ]]; then echo "$n: a ojo"; continue; fi
    if [[ ${kinds[i]} == mask ]]; then
        # RGB all the way: a YUV round trip would blur a one-step difference next to a box.
        for s in x y; do
            ffmpeg -v error -y -i "file:${!s}" -vf "${filters[i]}" -pix_fmt rgb24 "$tmp/$s.png" || { echo "error: ffmpeg falló con ${!s}" >&2; exit 2; }
        done
        x=$tmp/x.png; y=$tmp/y.png
    fi
    result=$(bash "$here/compare_captures.sh" "$x" "$y"); code=$?
    ((code == 2)) && exit 2
    echo "$n: $result"; ((code == 1)) && status=1
done
exit $status
