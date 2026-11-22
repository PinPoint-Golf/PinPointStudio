#!/bin/zsh
# run_probe.sh <probe.qml> <log> [extra args] — the Debug app, offscreen, with a COPY of the settings, under the
# typeset harness's render guard (at most two app instances, memory watchdog) and a 10-minute timeout.
#
# Always a headless probe (PINPOINT_HEADLESS_PROBE=1, refactor 4-c): no device side, no permission prompts.
# PPS_APP=<app binary> overrides the Debug app (a worktree's build/app/…).
# PP_PROBE_LIBRARY=<a library under /mnt/swingdata/scratch/> points the settings copy at that library (as fixtures.sh
# does), so a session copied there is held by its Athlete and judged by its own owner, and nothing the app writes
# (a regrade, the work-ons catch-up) reaches the live library (refactor 5.3). Unset: the copy names the live library,
# for a probe that opens no session (print_mock.qml).
set -u
REPO=${0:A:h:h:h}
PROBE=${1:A}; LOG=${2:A}; shift 2
APP=${PPS_APP:-$REPO/build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio}
CFG=$(mktemp -d); mkdir -p "$CFG/PinPointStudio"
INI="$CFG/PinPointStudio/PinPointStudio.ini"
if [[ -n ${PP_PROBE_LIBRARY:-} ]]; then
  # As given, not resolved: the app matches a session's folder to its athlete's by path text, so the library must be
  # spelled as the --probe-session folder is (/mnt/swingdata/…, not its /System/Volumes/Data/… firmlink).
  LIB=${PP_PROBE_LIBRARY%/}
  [[ $LIB == /mnt/swingdata/scratch/* || $LIB == /System/Volumes/Data/mnt/swingdata/scratch/* ]] \
    || { echo "REFUSED: PP_PROBE_LIBRARY $LIB is not under /mnt/swingdata/scratch"; rm -rf "$CFG"; exit 9; }
  sed -e "s#^athleteLibraryPath=.*#athleteLibraryPath=$LIB/#" -e "s#^path=/System/Volumes/Data/mnt/swingdata/#path=$LIB/#" \
      ~/.config/PinPointStudio/PinPointStudio.ini > "$INI"
  grep -q "^athleteLibraryPath=$LIB/" "$INI" || { echo "REFUSED: library path rewrite failed"; rm -rf "$CFG"; exit 9; }
  grep -q "^athleteLibraryPath=.*/mnt/swingdata/$" "$INI" && { echo "REFUSED: settings still name the live library"; rm -rf "$CFG"; exit 9; }
else
  cp ~/.config/PinPointStudio/PinPointStudio.ini "$INI"
fi
echo '{"screens":[{"name":"s","x":0,"y":0,"width":1680,"height":1200,"logicalDpi":96,"logicalBaseDpi":96,"dpr":2}]}' > "$CFG/screen.json"
PINPOINT_HEADLESS_PROBE=1 XDG_CONFIG_HOME=$CFG QML_DISABLE_DISK_CACHE=1 QML_XHR_ALLOW_FILE_READ=1 \
  QT_QPA_PLATFORM="offscreen:configfile=$CFG/screen.json" \
  perl -e 'alarm 600; exec @ARGV' $REPO/tools/typeset-harness/guard.sh "$APP" --probe-qml "$PROBE" "$@" >"$LOG" 2>&1
RC=$?; rm -rf "$CFG"
# a timed-out guard can leave the app behind: stop any probe instance still running this probe
pkill -f -- "--probe-qml $PROBE" 2>/dev/null
echo "exit=$RC"
