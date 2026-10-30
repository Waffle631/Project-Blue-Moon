#!/usr/bin/env bash
# tools/check_rename.sh [repo_dir]: lists every old gow3 name left in the tracked tree after the Spec 7 rename
# (docs/specs/2026-10-14-spec-7-renombrado.md §6), any case: each content match as "path:line:text", then each
# path with gow3 in it. Exceptions: docs/gow3/, docs/plans/ and docs/specs/ (history with the names of their time),
# the submodules, pgo/, this script and its test, lines marked "rename-check: ok" (a test that checks an old
# variable no longer works has to name it), and in CLAUDE.md and docs/tsuki/ROADMAP.md the branch `gow3` and the
# path docs/gow3/. Exit 0 with no output when nothing is left, 1 when something is, 2 on an error.
set -u
(($# <= 1)) || { echo "uso: $0 [repo_dir]" >&2; exit 2; }
repo=${1:-$(dirname -- "$(realpath -- "$0")")/..}
git -C "$repo" rev-parse --git-dir > /dev/null 2>&1 || { echo "error: $repo no es un repositorio git" >&2; exit 2; }
skip=(':!docs/gow3' ':!docs/plans' ':!docs/specs' ':!gpu/third_party' ':!pgo'
      ':!tools/check_rename.sh' ':!tests/test_check_rename.py')
# The branch phrases are removed before searching again, so another gow3 on the same line still counts.
branch='s/rama `gow3`//g; s/branch `gow3`//g; s/branch gow3//g; s|docs/gow3/||g'
found=$({
    git -C "$repo" -c core.quotePath=false grep -nIi --no-color gow3 -- . "${skip[@]}" ':!CLAUDE.md' ':!docs/tsuki/ROADMAP.md'
    git -C "$repo" -c core.quotePath=false grep -nIi --no-color gow3 -- CLAUDE.md docs/tsuki/ROADMAP.md |
        while IFS= read -r m; do
            rest=${m#*:}; rest=${rest#*:}
            sed "$branch" <<< "$rest" | grep -qi gow3 && echo "$m"
        done
    git -C "$repo" -c core.quotePath=false ls-files -- . "${skip[@]}" | grep -i gow3
} | grep -v 'rename-check: ok')
[[ -z $found ]] && exit 0
echo "$found"; exit 1
