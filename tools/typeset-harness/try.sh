#!/bin/zsh
# try.sh — the live try-out: the real app, windowed, with a candidate panel laid over the real one.
# Use the app normally (pick shots, switch Capture / Replay / Analyse, change View layouts and themes);
# the overlay re-attaches whenever the stage rebuilds the panel. Quit the app to finish. Docs: README.md.
#
#   try.sh [--selftest] [--slot try] <panel profile | target> <overlay.qml>
#
#   panel: a profile in panels/ (diagnostics, charts, wristMotion, …) or a raw target (objectName or key:<stageKey>)
#   --selftest   offscreen: drive every tab, both modes and the detail, print PASS/FAIL, exit 0 only on a pass.
#                Run it, and see it pass, before Mark is asked to look.
# Runs on a COPY of the fixture session and of your settings (the slot), never on your own. Every run goes
# through guard.sh with an 8 GB limit.
#
# For Mark, wrap it in a one-line build/run-me/try-<panel>-typeset.sh, e.g.
#   exec /Users/markliversedge/Projects/PinPointStudio/tools/typeset-harness/try.sh diagnostics <path>/Panel.qml
H=${0:A:h}; source $H/lib.sh
SELFTEST="" SLOT=try
while [[ $1 == --* ]]; do
  case $1 in
    --selftest) SELFTEST=1; shift;;  --slot) SLOT=$2; shift 2;;
    *) echo "try.sh: unknown option $1" >&2; exit 2;;
  esac
done
(( $# == 2 )) || { sed -n '2,16p' $0; exit 2; }
load_profile $1
OVERLAY=${2:A}
[[ -f $OVERLAY ]] || { echo "try.sh: no overlay file $OVERLAY" >&2; exit 2; }
[[ -x $PPS_APP ]] || { echo "try.sh: no app at $PPS_APP (set PPS_APP)" >&2; exit 2; }
W=$(ensure_slot $SLOT) || { echo "try.sh: could not make slot $SLOT" >&2; exit 2; }
ARGS=(--probe-qml $H/try_overlay.qml --probe-variant $OVERLAY --probe-session $W/session/${TYPESET_SESSION:t}
      --probe-target "$TARGET" --probe-panel-key "$PANEL_KEY" --probe-feed "$FEED" --probe-view-prop "$VIEW_PROP"
      --probe-tab-prop "$TAB_PROP" --probe-views "$VIEWS" --probe-detail "${DETAIL:-none}")
if [[ -n $SELFTEST ]]; then
  mkdir -p $TYPESET_OUT/logs; LOG=$TYPESET_OUT/logs/try_selftest_$1.log
  XDG_CONFIG_HOME=$W/cfg QML_DISABLE_DISK_CACHE=1 QT_QPA_PLATFORM="offscreen:configfile=$SCREENS/screen.json" GUARD_GB=8 \
    $H/guard.sh timeout 300 "$PPS_APP" $ARGS --probe-selftest > $LOG 2>&1
  RC=$?
  grep -E "$LOG_FILTER" $LOG | head -60
  echo "self-test exit=$RC   (log: $LOG)"
  exit $RC
fi
XDG_CONFIG_HOME=$W/cfg QML_DISABLE_DISK_CACHE=1 GUARD_GB=8 $H/guard.sh "$PPS_APP" $ARGS 2>&1 | grep --line-buffered -E "GUARD|TRY|\.qml:[0-9]+"
