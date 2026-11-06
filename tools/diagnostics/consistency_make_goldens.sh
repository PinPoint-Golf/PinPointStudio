#!/usr/bin/env bash
# Consistency: cut the two golden files the C++ tests read (docs/design/consistency_design.md, Appendix A).
#
#     tools/diagnostics/consistency_make_goldens.sh [out dir] [dump dir]
#
#   goldens_1005_consistency.json   the tally's `result` for a dump of session_1005.json itself, plus a `home` block
#                                   rolled up over Mark's five judged sessions
#   goldens_home_consistency.json   those five sessions' cells, newest first, for goldenHome in session_consistency_test
#
# out dir defaults to src/Gui/tests/data/charts. Point it at a scratch directory and diff to check the committed files.
# dump dir holds consistency_dump's output for Mark's sessions (Appendix A); 5 Oct there is the whole session, and the
# committed fixture's own dump stands in for it, so the goldens are cut from exactly what the tests load.
#
# Needs a built consistency_dump (ninja -C build/tests -j4 consistency_dump) and numpy. The dump runs on the PINPOINT_CORE_*
# files ctest gives session_consistency_test. The tally's --check has to pass over the five, so a stale dump stops here.
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUT=${1:-$REPO/src/Gui/tests/data/charts}
DUMPS=${2:-/mnt/swingdata/scratch/consistency/dump/Mark-Liversedge}
PYTHON=${PYTHON:-python3}
BUILD=$REPO/build/tests
DUMP=$BUILD/Gui/consistency_dump
TALLY=$REPO/tools/diagnostics/consistency_tally.py
OCT5=2026-10-05_Mark-Liversedge_Wrist_01

[ -x "$DUMP" ] || { echo "no $DUMP: build the consistency_dump target first" >&2; exit 1; }
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# ctest's own environment for the test the golden is read by, so the packs here are the packs there
eval "$(ctest --test-dir "$BUILD" --show-only=json-v1 | "$PYTHON" -c '
import json, shlex, sys
for t in json.load(sys.stdin)["tests"]:
    if t["name"] == "session_consistency_test":
        for p in t["properties"]:
            if p["name"] == "ENVIRONMENT":
                for e in p["value"]:
                    k, v = e.split("=", 1)
                    if k.startswith("PINPOINT_CORE_"): print("export %s=%s" % (k, shlex.quote(v)))
')"
[ -n "${PINPOINT_CORE_PACK:-}" ] || { echo "ctest has no session_consistency_test: configure build/tests first" >&2; exit 1; }

for s in 2026-07-04 2026-10-07 2026-10-08 2026-10-20; do
    cp "$DUMPS/${s}_Mark-Liversedge_Wrist_01.json" "$WORK/"
done
"$DUMP" "$REPO/src/Gui/tests/data/charts/session_1005.json" "$WORK/$OCT5.json"

mkdir -p "$OUT"
"$PYTHON" "$TALLY" "$WORK" "$WORK/unused" --check
"$PYTHON" "$TALLY" "$WORK" "$WORK/unused" --out-goldens "$OUT/goldens_1005_consistency.json" --golden-session "$OCT5" \
                                          --out-home-cells "$OUT/goldens_home_consistency.json"
echo "wrote $OUT/goldens_1005_consistency.json and $OUT/goldens_home_consistency.json"
