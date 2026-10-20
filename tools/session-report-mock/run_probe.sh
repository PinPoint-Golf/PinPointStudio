#!/bin/zsh
# run_probe.sh <probe.qml> <log> [extra args] — the Debug app, offscreen, with a COPY of the settings, under the
# typeset harness's render guard (at most two app instances, memory watchdog) and a 10-minute timeout.
set -u
REPO=${0:A:h:h:h}
PROBE=${1:A}; LOG=${2:A}; shift 2
APP=$REPO/build/Qt_6_11_1_for_macOS_Debug/PinPointStudio.app/Contents/MacOS/PinPointStudio
CFG=$(mktemp -d); mkdir -p "$CFG/PinPointStudio" && cp ~/.config/PinPointStudio/PinPointStudio.ini "$CFG/PinPointStudio/"
echo '{"screens":[{"name":"s","x":0,"y":0,"width":1680,"height":1200,"logicalDpi":96,"logicalBaseDpi":96,"dpr":2}]}' > "$CFG/screen.json"
XDG_CONFIG_HOME=$CFG QML_DISABLE_DISK_CACHE=1 QML_XHR_ALLOW_FILE_READ=1 QT_QPA_PLATFORM="offscreen:configfile=$CFG/screen.json" \
  perl -e 'alarm 600; exec @ARGV' $REPO/tools/typeset-harness/guard.sh "$APP" --probe-qml "$PROBE" "$@" >"$LOG" 2>&1
RC=$?; rm -rf "$CFG"
# a timed-out guard can leave the app behind: stop any probe instance still running this probe
pkill -f -- "--probe-qml $PROBE" 2>/dev/null
echo "exit=$RC"
