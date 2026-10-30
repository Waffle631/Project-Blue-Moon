#!/usr/bin/env bash
# tools/check_rename.sh [repo_dir]: lists every old gow3 name left in the tracked tree after the Spec 7 rename
# (docs/specs/2026-10-14-spec-7-renombrado.md §6), any case: each content match as "path:line:text", then each
# path with gow3 in it. Since §11 it also lists the previous game's name ("god of war") and serials (CUSA01623,
# CUSA01715). Exceptions: docs/gow3/, docs/plans/ and docs/specs/ (history with the names of their time), the
# submodules, pgo/, this script and its test, the player READMEs (rewritten for the Alpha), lines marked
# "rename-check: ok" (a test that checks an old variable no longer works has to name it), "gow3 contributors" on a
# copyright line, and in CLAUDE.md and docs/tsuki/ROADMAP.md the branch `gow3` and the path docs/gow3/. Exit 0 with
# no output when nothing is left, 1 when something is, 2 on an error.
set -u
(($# <= 1)) || { echo "uso: $0 [repo_dir]" >&2; exit 2; }
repo=${1:-$(dirname -- "$(realpath -- "$0")")/..}
git -C "$repo" rev-parse --git-dir > /dev/null 2>&1 || { echo "error: $repo no es un repositorio git" >&2; exit 2; }
skip=(':!docs/gow3' ':!docs/plans' ':!docs/specs' ':!gpu/third_party' ':!pgo'
      ':!tools/check_rename.sh' ':!tests/test_check_rename.py'
      ':!README.md' ':!README.pt-BR.md' ':!packaging/windows/README-Windows.txt')
old='gow3|god of war|cusa01623|cusa01715'
# The allowed phrases are removed before searching again, so another old name on the same line still counts.
found=$({
    git -C "$repo" -c core.quotePath=false grep -nIi --no-color -E "$old" -- . "${skip[@]}" |
        awk -v old="$old" '{
            path = $0; sub(/:.*/, "", path); rest = $0; sub(/^[^:]*:[^:]*:/, "", rest); rest = tolower(rest)
            if (path == "CLAUDE.md" || path == "docs/tsuki/ROADMAP.md")
                gsub(/rama `gow3`|branch `gow3`|branch gow3|docs\/gow3\//, "", rest)
            if (rest ~ /copyright/) gsub(/gow3 contributors/, "", rest)
            if (rest ~ old) print }'
    git -C "$repo" -c core.quotePath=false ls-files -- . "${skip[@]}" | grep -iE "$old"
} | grep -v 'rename-check: ok')
[[ -z $found ]] && exit 0
echo "$found"; exit 1
