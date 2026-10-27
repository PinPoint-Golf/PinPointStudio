#!/bin/zsh
# render.sh — render a stage panel in the real app, offscreen, fed the real data: either a mock laid
# exactly over the real panel, or (overlay "none") the real panel itself. Docs: README.md beside this file.
#
#   render.sh [options] <overlay.qml | none> <tag> <layout>
#
#   layout   full | stage | stagecam | stagediag | half | third | cast | print-portrait | print-landscape
#   --panel <profile|target>  panels/<profile>.env (default diagnostics), or a raw target:
#                             an objectName, or key:<stageKey> (e.g. key:charts)
#   --slot <name>             your own copy of the session and settings (default a; created on demand)
#   --themes 7,12             6 Vector light, 7 Vector dark, 12 Folio light, 13 Folio dark
#   --views v1,v2             default: the profile's views
#   --detail <id>             the condition "detail" opens (default: the profile's; diagnostics over_the_top)
#   --mode analyse|replay     default analyse
#   --states s1,s2            sets the mock's demoState before each grab; file names gain _<state>
#   --screen normal|tall|mark|<file.json>    1680×1050 | 1680×2600 | 2560×1560 (Mark's window)
#   --ordinal <n>             the shot (default 8)
#   --nav <screen>            the app screen: an AppScreen name (Session, WorkOns, …) or its number; default Session
#   --feed p1,p2  --tab-prop <p>  --target <t>   override the profile
#   --dump                    write the fed properties (and the detail) as JSON to $TYPESET_OUT/<tag>_data/, then quit
#   --timeout <s>             default 500
#
# Output: $TYPESET_OUT/<tag>_<layout>_<aesthetic>_<light|dark>_<view>[_<state>]_{window,panel}.png
#     or  $TYPESET_OUT/<tag>_print-<orient>_<aesthetic>_<light|dark>_p<N>.png   (A4 at 2×)
# The full app log is kept at $TYPESET_OUT/logs/<tag>_<layout>.log; the console shows probe lines and QML errors.
H=${0:A:h}; source $H/lib.sh
PROFILE=diagnostics SLOT=a THEMES=7,12 MODE=analyse STATES="" SCREEN=normal ORDINAL=8 TIMEOUT=500 DUMP="" NAV=Session
O_VIEWS="" O_DETAIL="" O_FEED="" O_TAB="" O_TARGET="" O_FEED_SET="" O_TAB_SET=""
while [[ $1 == --* ]]; do
  case $1 in
    --panel) PROFILE=$2; shift 2;;     --slot) SLOT=$2; shift 2;;      --themes) THEMES=$2; shift 2;;
    --views) O_VIEWS=$2; shift 2;;      --detail) O_DETAIL=$2; shift 2;; --mode) MODE=$2; shift 2;;
    --states) STATES=$2; shift 2;;      --screen) SCREEN=$2; shift 2;;   --ordinal) ORDINAL=$2; shift 2;;
    --feed) O_FEED=$2; O_FEED_SET=1; shift 2;;  --tab-prop) O_TAB=$2; O_TAB_SET=1; shift 2;;
    --target) O_TARGET=$2; shift 2;;    --dump) DUMP=1; shift;;          --timeout) TIMEOUT=$2; shift 2;;
    --nav) NAV=$2; shift 2;;
    *) echo "render.sh: unknown option $1" >&2; exit 2;;
  esac
done
(( $# == 3 )) || { sed -n '2,25p' $0; exit 2; }
OVERLAY=$1 TAG=$2 LAYOUT=$3
load_profile $PROFILE
[[ -n $O_TARGET ]] && TARGET=$O_TARGET
[[ -n $O_FEED_SET ]] && FEED=$O_FEED
[[ -n $O_TAB_SET ]] && TAB_PROP=$O_TAB
[[ -n $O_VIEWS ]] && VIEWS=$O_VIEWS
[[ -n $O_DETAIL ]] && DETAIL=$O_DETAIL
[[ $OVERLAY != none ]] && OVERLAY=${OVERLAY:A}
[[ $OVERLAY == none || -f $OVERLAY ]] || { echo "render.sh: no overlay file $OVERLAY" >&2; exit 2; }
[[ -x $PPS_APP ]] || { echo "render.sh: no app at $PPS_APP (set PPS_APP)" >&2; exit 2; }

PRINT=""
if [[ $LAYOUT == print-* ]]; then PRINT=${LAYOUT#print-}; LAYOUT=full; SCREEN=print; fi
SCREENFILE=$(screen_json $SCREEN)
W=$(ensure_slot $SLOT) || { echo "render.sh: could not make slot $SLOT" >&2; exit 2; }
mkdir -p $TYPESET_OUT/logs
LOG=$TYPESET_OUT/logs/${TAG}_${LAYOUT}${PRINT:+-$PRINT}.log

XDG_CONFIG_HOME=$W/cfg QML_DISABLE_DISK_CACHE=1 QT_QPA_PLATFORM="offscreen:configfile=$SCREENFILE" \
  $H/guard.sh timeout $TIMEOUT "$PPS_APP" --probe-qml $H/render_overlay.qml --probe-variant "$OVERLAY" \
  --probe-target "$TARGET" --probe-panel-key "$PANEL_KEY" --probe-feed "$FEED" --probe-tab-prop "$TAB_PROP" \
  --probe-session $W/session/${TYPESET_SESSION:t} --probe-ordinal $ORDINAL --probe-nav $NAV --probe-layout $LAYOUT --probe-print "$PRINT" \
  --probe-themes $THEMES --probe-views "$VIEWS" --probe-detail "${DETAIL:-none}" --probe-mode $MODE \
  --probe-states "$STATES" --probe-dump "$DUMP" --probe-out $TYPESET_OUT --probe-tag $TAG > $LOG 2>&1
RC=$?
grep -E "$LOG_FILTER" $LOG | grep -vE 'saved |DUMP ' | head -80
if [[ -n $DUMP ]]; then
  DD=$TYPESET_OUT/${TAG}_data; mkdir -p $DD
  DD=$DD perl -ne 'if (/DUMP (\S+) (.*)$/) { open my $f, ">", "$ENV{DD}/$1.json" or die; print $f "$2\n"; close $f }' $LOG
  echo "data → $DD: $(ls $DD | tr '\n' ' ')"
fi
echo "saved $(grep -c 'PROBE: saved ' $LOG) PNGs → $TYPESET_OUT   exit=$RC"
exit $RC
