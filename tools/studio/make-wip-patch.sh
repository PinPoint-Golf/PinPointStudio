#!/bin/bash
# make-wip-patch.sh <run-dir> - write <run-dir>/wip.patch from the current tree, for a patched
# build on the cabin PC (tools/studio/studio-build.cmd ... patch C:\...\wip.patch).
#
# The patch holds every tracked change against HEAD (staged or not, never build/) plus the new
# untracked, not-ignored files under src/ docs/ tools/ cmake/ and CMakeLists.txt. New files are
# made intent-to-add in a SCRATCH index (GIT_INDEX_FILE), so your real index is never touched, and
# the patch is `git diff --binary --no-renames HEAD` against that index: new files are "new file"
# hunks and nothing is a rename, which is what studio-build.cmd expects when it lists the files a
# patch creates. tools/studio/studio-build.cmd itself is always left out: cmd reads a running batch
# file from disk, so the cabin refuses a patch that touches it.
#
# Use a run dir on the share so the cabin sees it: /mnt/swingdata/scratch/<run> on the Mac is
# C:\PinPointStudio\scratch\<run> on the cabin PC.
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 <run-dir>" >&2
    exit 2
fi
run_dir=$1
repo=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
mkdir -p "$run_dir"
run_dir=$(cd "$run_dir" && pwd -P)
out=$run_dir/wip.patch

new_paths=(src docs tools cmake CMakeLists.txt)
self=tools/studio/studio-build.cmd
diff_spec=(. ":(exclude)build" ":(exclude)$self")

tmp_index=$(mktemp "${TMPDIR:-/tmp}/wip-index.XXXXXX")
trap 'rm -f "$tmp_index"' EXIT
cp "$(git -C "$repo" rev-parse --absolute-git-dir)/index" "$tmp_index"

export GIT_INDEX_FILE=$tmp_index
git -C "$repo" ls-files -z --others --exclude-standard -- "${new_paths[@]}" \
    | xargs -0 git -C "$repo" add -N --

git -C "$repo" diff --binary --no-renames --no-ext-diff --no-color HEAD -- "${diff_spec[@]}" > "$out"

if [ ! -s "$out" ]; then
    rm -f "$out"
    echo "make-wip-patch: nothing to patch - the tree matches HEAD; refusing to write an empty patch" >&2
    exit 1
fi

echo "== files (A new, M modified, D deleted)"
git -C "$repo" diff --name-status --no-renames HEAD -- "${diff_spec[@]}"
if ! git -C "$repo" diff --quiet HEAD -- "$self"; then
    echo "NOTE: $self has local changes; they are NOT in the patch (commit it to change the cabin's script)"
fi
skipped=$(git -C "$repo" ls-files --others --exclude-standard -- . ":(exclude)build" \
    | grep -v -E '^(src|docs|tools|cmake)/|^CMakeLists\.txt$' || true)
if [ -n "$skipped" ]; then
    echo "NOTE: untracked files outside src/ docs/ tools/ cmake/ CMakeLists.txt left out:"
    echo "$skipped" | sed 's/^/  /'
fi

sha=$(shasum -a 256 "$out" | awk '{print $1}')
echo "== patch"
echo "$out"
echo "sha256 $sha"
echo "base   $(git -C "$repo" rev-parse --short HEAD)"
case "$out" in
    /mnt/swingdata/*)
        win="C:\\PinPointStudio\\$(echo "${out#/mnt/swingdata/}" | tr '/' '\\')"
        echo "== cabin PC"
        echo "ssh developer@GOLFSIMPC.local \"cmd /c C:\\Users\\developer\\Projects\\PinPointStudio\\tools\\studio\\studio-build.cmd both nopull patch $win\""
        ;;
esac
