#!/bin/sh
# Prints the numbers behind docs/design/swing_3d_session_synthesis_design.md for one session.
# Read-only. Usage: tools/swinglab/session_synth/run.sh <session folder name under Mark-Liversedge>
set -e
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../../.." && pwd)"
out="$repo/build/session_synth"
mkdir -p "$out"
c++ -std=c++17 -O1 -I"$repo/src/Skeleton3d" "$here/dumprig.cpp" -o "$out/dumprig"
"$out/dumprig" > "$out/rig.json"
python3 "$here/synth_prototype.py" "$1" "$out/rig.json"
