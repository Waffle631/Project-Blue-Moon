#!/usr/bin/env bash
# tools/probe_movies.sh [movie dir] [csv]: ffprobe of every .mp4 the game plays through sceAvPlayer
# (Spec 5, plan task 5). One CSV row per file: tracks, video codec/profile/size/fps/frames, audio
# codec/channels/rate and duration. Then, per column that can change how AvPlayer behaves, its
# distinct values with their counts: that is what decides which videos the sweep
# (avplayer-test --movie-dir) must not sample away. Defaults: Tsukihime/CUSA47999/image/movie and
# out/movies-probe.csv. Exit 1 if ffprobe failed on some file (its row says so), 2 on usage errors.
set -u
dir=${1:-Tsukihime/CUSA47999/image/movie}
csv=${2:-out/movies-probe.csv}
command -v ffprobe > /dev/null || { echo "error: falta ffprobe" >&2; exit 2; }
[[ -d $dir ]] || { echo "error: no existe $dir" >&2; exit 2; }
shopt -s nullglob
files=("$dir"/*.mp4)
((${#files[@]} > 0)) || { echo "error: ningún .mp4 en $dir" >&2; exit 2; }
cols=(file duration_s v_tracks a_tracks other_tracks order v_codec v_profile width height pix_fmt
      r_fps avg_fps v_frames b_frames a_codec a_profile channels sample_rate a_frames)
# default=nw=1 prints key=value lines; csv output would follow ffprobe's field order, not ours.
# file: keeps names that start with '-' or contain ':' from being read as options or protocols.
entries() { ffprobe -v error "${@:2}" -of default=nw=1 "file:$1"; }
failed=0
{
    (IFS=,; echo "${cols[*]}")
    for f in "${files[@]}"; do
        declare -A row=([file]=${f##*/})
        if ! types=$(entries "$f" -show_entries stream=codec_type); then
            row[v_codec]=ffprobe-error; failed=1
        else
            row[v_tracks]=$(grep -c '^codec_type=video$' <<< "$types")
            row[a_tracks]=$(grep -c '^codec_type=audio$' <<< "$types")
            row[other_tracks]=$(grep -vc '^codec_type=\(video\|audio\)$' <<< "$types")
            row[order]=$(sed 's/^codec_type=//' <<< "$types" | paste -sd+)
            while IFS='=' read -r key value; do row[duration_s]=$value; done \
                < <(entries "$f" -show_entries format=duration)
            while IFS='=' read -r key value; do
                case $key in
                codec_name) row[v_codec]=$value ;; profile) row[v_profile]=$value ;;
                r_frame_rate) row[r_fps]=$value ;; avg_frame_rate) row[avg_fps]=$value ;;
                nb_frames) row[v_frames]=$value ;; has_b_frames) row[b_frames]=$value ;;
                *) row[$key]=$value ;;
                esac
            done < <(entries "$f" -select_streams v:0 -show_entries \
                stream=codec_name,profile,width,height,pix_fmt,r_frame_rate,avg_frame_rate,nb_frames,has_b_frames)
            while IFS='=' read -r key value; do
                case $key in
                codec_name) row[a_codec]=$value ;; profile) row[a_profile]=$value ;;
                nb_frames) row[a_frames]=$value ;; *) row[$key]=$value ;;
                esac
            done < <(entries "$f" -select_streams a:0 -show_entries \
                stream=codec_name,profile,channels,sample_rate,nb_frames)
        fi
        line=
        for c in "${cols[@]}"; do line+=${line:+,}${row[$c]:-}; done
        echo "$line"
        unset row
    done
} > "$csv"
echo "${#files[@]} vídeos → $csv"
# Distinct values of the columns AvPlayer cares about, e.g. "2× 1280x720".
summary() {
    local title=$1; shift
    echo "$title:"
    awk -F, -v cols="$*" 'NR == 1 { for (i = 1; i <= NF; i++) at[$i] = i; next }
        { n = split(cols, c, " "); v = ""; for (i = 1; i <= n; i++) v = v (i > 1 ? " " : "") $(at[c[i]]); print v }' \
        "$csv" | sort | uniq -c | sort -rn | awk '{ n = $1; $1 = ""; printf "  %d×%s\n", n, $0 }'
}
summary "pistas (vídeo audio otras, orden)" v_tracks a_tracks other_tracks order
summary "vídeo (códec perfil ancho alto píxel)" v_codec v_profile width height pix_fmt
summary "fps (r_frame_rate avg_frame_rate)" r_fps avg_fps
summary "fotogramas B (has_b_frames)" b_frames
summary "audio (códec perfil canales tasa)" a_codec a_profile channels sample_rate
exit $failed
