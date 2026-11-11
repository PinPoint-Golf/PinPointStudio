#!/bin/zsh
# fixtures.sh [dump|build|all] [tag …] — regenerates the session report's QML test data,
# src/Gui/tests/qml/data/session_report/<case>.json + fixtures.js (read by src/Gui/tests/qml/tst_session_report.qml).
#
#   one line:  zsh tools/session-report-mock/fixtures.sh          (= all: dump, then build)
#
#   dump   copies the source sessions below READ-ONLY from the live library into a scratch library on the share
#          (no ledger backups, no earlier reports; video only for the session reported), points a COPY of
#          the app's settings at that scratch library (opening a session regrades it, and the work-ons catch-up rewrites
#          the session records of the library the app is pointed at — so never the live one), and runs the CURRENT
#          SessionReportModel on each source session through dump_report.qml (the Debug app at
#          build/Qt_6_11_1_for_macOS_Debug, offscreen, under tools/typeset-harness/guard.sh). Each map is saved as
#          $SCR/dumps/<tag>.json. Needs the share mounted and a Debug app built from the tree being tested.
#   build  build_fixtures.py: every case from those dumps (its header says which case comes from where). Writes into
#          the repo's fixture folder only (the .json files, fixtures.js and the two placeholder stills in stills/).
#
# Scratch: $SCR (default /mnt/swingdata/scratch/session-report-fixtures). Delete $SCR/lib and $SCR/dumps when done;
# a re-run recreates both. A re-run is byte-identical while the source sessions in the library are unchanged; after one,
# re-record test_01's expectedPages in tst_session_report.qml and look at every count that moved.
#
# THE SOURCE SESSIONS (live library, read only). Each tag gets its own fresh scratch library ($SCR/lib/<tag>) holding
# the sessions listed for it below and nothing else: the reported session and the rest of its day ("Earlier today" reads
# the earlier sessions; Club delivery's plane -> path fit pools the golfer's day; Your tendencies reads the golfer's
# sessions present). Only the reported session's video is copied (the stills are cut from it). build_fixtures.py's header says which case each one feeds.
#   nick   Nick-Henn/2026-10-20_Nick-Henn_Wrist_03              the base of every stress case, and its Posture, Tendencies,
#                                                               Consistency (flagged swings) and launch monitor pages
#   jess3  Jess-Ratcliffe/2026-10-20_Jess-Ratcliffe_Wrist_03    the second golfer: "Linked, not proven", a held habit
#   nolm   Mark-Liversedge/2026-09-09_Mark-Liversedge_Wrist_01  no launch monitor (cameras only)
#   long   Mark-Liversedge/2026-10-07_Mark-Liversedge_Wrist_01  a long session: 50 swings, five work-ons, no launch monitor
#   rich   Mark-Liversedge/2026-08-18_Mark-Liversedge_Wrist_02  the wrist case: its own map, Posture and Wrist pages (6 swings,
#                                                               motion sensor + camera)
#   jess   Jess-Ratcliffe/2026-10-20_Jess-Ratcliffe_Wrist_01    the delivery case: its own map, Posture, Wrist and Club delivery
#                                                               (9 swings, the fit over her day)
#   mixed  Mark-Liversedge/2026-10-20_Mark-Liversedge_Wrist_01  the consistency-mixed-clubs case: its own map and Consistency
#                                                               page (19 swings, 7 iron and gap wedge)
#   clean  Mark-Liversedge/2026-11-02_Mark-Liversedge_Wrist_02  the library's clean report and
#   note   Nick-Henn/2026-10-13_Nick-Henn_Wrist_01              its one-swing note: read so the clean and note transforms
#                                                               are checked against the keys the model writes
set -u
REPO=${0:A:h:h:h}
HERE=${0:A:h}
APP=${PPS_APP:-$REPO/build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio}
LIVE=/mnt/swingdata
SCR=${SCR:-/mnt/swingdata/scratch/session-report-fixtures}
LIB=$SCR/lib
DUMPS=$SCR/dumps
PY=/opt/homebrew/bin/python3
typeset -A SES DAY
SES=(nick  Nick-Henn/2026-10-20_Nick-Henn_Wrist_03
     jess3 Jess-Ratcliffe/2026-10-20_Jess-Ratcliffe_Wrist_03
     rich  Mark-Liversedge/2026-08-18_Mark-Liversedge_Wrist_02
     jess  Jess-Ratcliffe/2026-10-20_Jess-Ratcliffe_Wrist_01
     mixed Mark-Liversedge/2026-10-20_Mark-Liversedge_Wrist_01
     clean Mark-Liversedge/2026-11-02_Mark-Liversedge_Wrist_02
     note  Nick-Henn/2026-10-13_Nick-Henn_Wrist_01
     nolm  Mark-Liversedge/2026-09-09_Mark-Liversedge_Wrist_01
     long  Mark-Liversedge/2026-10-07_Mark-Liversedge_Wrist_01)
JESSDAY="Jess-Ratcliffe/2026-10-20_Jess-Ratcliffe_Wrist_01 Jess-Ratcliffe/2026-10-20_Jess-Ratcliffe_Wrist_02 Jess-Ratcliffe/2026-10-20_Jess-Ratcliffe_Wrist_03"
DAY=(nick  "Nick-Henn/2026-10-20_Nick-Henn_Wrist_01 Nick-Henn/2026-10-20_Nick-Henn_Wrist_02 Nick-Henn/2026-10-20_Nick-Henn_Wrist_03"
     jess3 "$JESSDAY"
     rich  "Mark-Liversedge/2026-08-18_Mark-Liversedge_Wrist_01 Mark-Liversedge/2026-08-18_Mark-Liversedge_Wrist_02"
     jess  "$JESSDAY"
     mixed "Mark-Liversedge/2026-10-20_Mark-Liversedge_Wrist_01"
     clean "Mark-Liversedge/2026-11-02_Mark-Liversedge_Wrist_01 Mark-Liversedge/2026-11-02_Mark-Liversedge_Wrist_02"
     note  "Nick-Henn/2026-10-13_Nick-Henn_Wrist_01"
     nolm  "Mark-Liversedge/2026-09-09_Mark-Liversedge_Wrist_01"
     long  "Mark-Liversedge/2026-10-07_Mark-Liversedge_Wrist_01")
TAGS=(nick jess3 nolm long rich jess mixed clean note)
STEP=${1:-all}
(( $# > 1 )) && TAGS=(${@:2})     # dump only these tags: fixtures.sh dump mixed

dump() {
  [[ $SCR == /mnt/swingdata/scratch/* ]] || { echo "REFUSED: scratch $SCR is not under /mnt/swingdata/scratch"; exit 9; }
  [[ -x $APP ]] || { echo "REFUSED: no app at $APP"; exit 9; }
  mkdir -p $LIB $DUMPS
  local CFG=$(mktemp -d); mkdir -p $CFG/PinPointStudio
  for t in $TAGS; do
    # each tag its own fresh library holding ONLY its sessions: what the model reads beyond the session (the day, the
    # golfer's tendencies, the work-ons catch-up) is then the same whatever ran before
    local TL=$LIB/$t
    rm -rf $TL; mkdir -p $TL
    sed -e "s#^athleteLibraryPath=.*#athleteLibraryPath=$TL/#" -e "s#^path=/System/Volumes/Data/mnt/swingdata/#path=$TL/#" \
        ~/.config/PinPointStudio/PinPointStudio.ini > $CFG/PinPointStudio/PinPointStudio.ini
    grep -q "^athleteLibraryPath=$TL/" $CFG/PinPointStudio/PinPointStudio.ini || { echo "REFUSED: library path rewrite failed"; rm -rf $CFG; exit 9; }
    grep -q "^athleteLibraryPath=.*/mnt/swingdata/$" $CFG/PinPointStudio/PinPointStudio.ini && { echo "REFUSED: settings still name the live library"; rm -rf $CFG; exit 9; }
    for s in ${=DAY[$t]}; do
      mkdir -p $TL/${s:h}
      # the golfer's own files beside the sessions (themes, tendencies), then the session: no ledger backups, no
      # earlier reports, and video only for the session reported
      rsync -a --exclude '*/' --exclude '.DS_Store' $LIVE/${s:h}/ $TL/${s:h}/ || exit 2
      local VID=(--exclude '*.mp4'); [[ $s == ${SES[$t]} ]] && VID=()
      rsync -a $VID --exclude 'diagnostics.json.*' --exclude 'report/' --exclude '.DS_Store' $LIVE/$s/ $TL/$s/ || exit 2
      # rsync keeps whole-second times on the share; a swing's phase grid records its swing document's time to the
      # millisecond, so without this every copied grid reads as stale and the app rebuilds it while the report reads it
      $PY - $LIVE/$s $TL/$s <<'PY' || exit 2
import os, sys
src, dst = sys.argv[1], sys.argv[2]
for root, dirs, files in os.walk(dst):
    for f in files:
        d = os.path.join(root, f); s = os.path.join(src, os.path.relpath(d, dst))
        if os.path.exists(s):
            st = os.stat(s); os.utime(d, ns=(st.st_atime_ns, st.st_mtime_ns))
PY
    done
    local LOG=$DUMPS/$t.log
    XDG_CONFIG_HOME=$CFG QML_DISABLE_DISK_CACHE=1 QT_QPA_PLATFORM=offscreen PINPOINT_MAIL_DRYRUN=1 PINPOINT_LOG_STDERR=1 \
      $REPO/tools/typeset-harness/guard.sh perl -e 'alarm 180; exec @ARGV' $APP --probe-qml $HERE/dump_report.qml \
        --probe-session $TL/${SES[$t]} --probe-stills 1 > $LOG 2>&1
    echo "$t exit=$?"
    pkill -f -- "--probe-qml $HERE/dump_report.qml" 2>/dev/null
    $PY - $LOG $DUMPS/$t.json <<'PY' || { rm -rf $CFG; exit 3; }
import json, re, sys
parts = {}
for line in open(sys.argv[1], errors="replace"):
    m = re.search(r"REPORTMAP\[(\d+)/(\d+)\]:(.*)$", line.rstrip("\n"))
    if m: parts[int(m.group(1))] = (int(m.group(2)), m.group(3))
if not parts or len(parts) != next(iter(parts.values()))[0]:
    sys.exit("no complete REPORTMAP in " + sys.argv[1])
text = "".join(parts[i][1] for i in sorted(parts)).replace("\\n", "\n")
m = json.loads(text, strict=False)   # a newline inside a string came through the log escaped like the layout's
json.dump(m, open(sys.argv[2], "w"), indent=1, ensure_ascii=False)
print("  %s: kind %s, %d swings, blocks: %s" % (sys.argv[2], m.get("kind"), (m.get("meta") or {}).get("swings", 0),
      ", ".join(k for k in ("posture", "wrist", "delivery", "tendencies", "consistency", "lmAppendix") if m.get(k))))
PY
  done
  rm -rf $CFG
}

build() {
  $PY $HERE/build_fixtures.py --dumps $DUMPS --lib $LIB || exit 4
}

case $STEP in
  dump)  dump ;;
  build) build ;;
  all)   dump; build ;;
  *)     echo "usage: fixtures.sh [dump|build|all]"; exit 1 ;;
esac
