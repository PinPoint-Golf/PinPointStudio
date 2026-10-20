#!/bin/zsh
# dump-session.sh <session folder in the library> <out.json>
# Copies the golfer's sessions to the share's scratch (opening a session in the app regrades it and writes into
# it, so never the library itself; videos are linked, not copied), loads the copy in the app offscreen, and
# writes the SessionDiagnosticsModel surface the report is assembled from as one JSON map.
set -u
HERE=${0:A:h}; SESSION=${1%/}; OUT=${2:A}
ATHLETE=${SESSION:h}; SCR=/mnt/swingdata/scratch/session-report/lib/${ATHLETE:t}
mkdir -p $SCR && rsync -a --exclude '*.mp4' --exclude '*.f32' $ATHLETE/ $SCR/
( cd $ATHLETE && for f in */swing_*/*.mp4(N) */swing_*/*.f32(N); do ln -sf $ATHLETE/$f $SCR/$f; done )
LOG=${OUT:r}.log
$HERE/run_probe.sh $HERE/dump_model.qml $LOG --probe-session $SCR/${SESSION:t}
/opt/homebrew/bin/python3 - "$LOG" "$OUT" <<'PY'
import json, sys
d = {}
for line in open(sys.argv[1], errors="replace"):
    i = line.find("DUMP:")
    if i < 0: continue
    k, _, v = line[i + 5:].rstrip("\n").partition(":")
    try: d[k] = json.loads(v)
    except Exception: d[k] = None
json.dump(d, open(sys.argv[2], "w"))
print(f"wrote {sys.argv[2]}: {len(d)} keys, {d.get('shotCount')} swings, {len(d.get('cards') or [])} habits")
PY
