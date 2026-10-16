#!/bin/zsh
# render_stage.sh — the stage audit: render each REAL stage panel (no overlay) at each layout and theme,
# with the timeline and carousel on, and grab the window and the panel's stage card. Docs: README.md.
#
#   render_stage.sh [--slot base] [--screen normal|tall|mark|<file.json>] <tag> <mode analyse|replay> \
#                   <themes 12,7> <layouts full,half,third> <panels camera,charts,…> [settle_ms=3000] [timeout_s=900]
#
#   panels: camera sessionDiagnostics swing3d launchMonitor charts table markup
# Output: $TYPESET_OUT/<tag>_<mode>_<layout>_<aesthetic>_<light|dark>_<panel>_{window,panel}.png
H=${0:A:h}; source $H/lib.sh
SLOT=base SCREEN=normal
while [[ $1 == --* ]]; do
  case $1 in
    --slot) SLOT=$2; shift 2;;  --screen) SCREEN=$2; shift 2;;
    *) echo "render_stage.sh: unknown option $1" >&2; exit 2;;
  esac
done
(( $# >= 5 )) || { sed -n '2,9p' $0; exit 2; }
[[ -x $PPS_APP ]] || { echo "render_stage.sh: no app at $PPS_APP (set PPS_APP)" >&2; exit 2; }
W=$(ensure_slot $SLOT) || { echo "render_stage.sh: could not make slot $SLOT" >&2; exit 2; }
mkdir -p $TYPESET_OUT/logs
LOG=$TYPESET_OUT/logs/stage_${1}_$2.log
XDG_CONFIG_HOME=$W/cfg QML_DISABLE_DISK_CACHE=1 QT_QPA_PLATFORM="offscreen:configfile=$(screen_json $SCREEN)" \
  $H/guard.sh timeout ${7:-900} "$PPS_APP" --probe-qml $H/render_stage.qml \
  --probe-session $W/session/${TYPESET_SESSION:t} --probe-ordinal 8 --probe-mode $2 \
  --probe-themes $3 --probe-layouts $4 --probe-panels $5 --probe-settle ${6:-3000} \
  --probe-out $TYPESET_OUT --probe-tag $1 > $LOG 2>&1
RC=$?
grep -E "$LOG_FILTER" $LOG | grep -v 'saved ' | head -150
echo "saved $(grep -c 'PROBE: saved ' $LOG) panels → $TYPESET_OUT   exit=$RC"
exit $RC
