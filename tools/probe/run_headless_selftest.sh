#!/bin/bash
# run_headless_selftest.sh — proves tools/probe/run_headless.sh (tooling 5.3d) with a stand-in binary. It NEVER
# launches PinPoint Studio: every --app below is a stand-in compiled here from a few lines of C (a shell script will not
# do on macOS: ps -E shows no environment for /bin/sh, an Apple platform binary). The stand-in records its pid, its
# arguments and its environment in the marker file PINPOINT_STANDIN_MARKER names, prints to stderr, sleeps
# "sleep=<s>" and exits "rc=<n>".
#
#   run_headless_selftest.sh [<base dir>]     works in <base>/run_headless_selftest.work, made afresh and removed on a
#                                             pass; the base must be beneath <repo>/build/, /mnt/swingdata/scratch/ or
#                                             /private/tmp/claude-<uid>/. Default base: <repo>/build.
#
# It proves: every refusal (exit 2, and the stand-in never ran); the kill path (exit 3 within about a second, the
# stand-in gone) for a launch without the switch and for a wrong HOME, both through the test hooks; the happy path (the
# app's exact environment, the --probe argument, the exit code passed through, the stderr log); and the timeout (124).
# Exit 0 when all pass, 1 otherwise. ctest: run_headless_selftest (src/Gui/tests/CMakeLists.txt).

HERE=$(cd "$(dirname "$0")" && pwd -P)
LAUNCH="$HERE/run_headless.sh"
REPO_TOP=$(git -C "$HERE" rev-parse --show-toplevel 2>/dev/null || (cd "$HERE/../.." && pwd -P))
BASE="${1:-$REPO_TOP/build}"
[ -d "$BASE" ] && BASE=$(cd "$BASE" && pwd -P) || { echo "FAIL: the base '$BASE' is not an existing folder"; exit 1; }
BASE_OK=0
for r in "$REPO_TOP/build" "$(cd "$(dirname "$(git -C "$HERE" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || echo "$REPO_TOP/.git")")" && pwd -P)/build" \
         /mnt/swingdata/scratch "/private/tmp/claude-$(id -u)" "/tmp/claude-$(id -u)"; do
    case "$BASE/" in "$r"/*) BASE_OK=1 ;; esac
done
[ $BASE_OK = 1 ] || { echo "FAIL: the base '$BASE' is not beneath <repo>/build/, /mnt/swingdata/scratch/ or the session scratchpad"; exit 1; }
WORK="$BASE/run_headless_selftest.work"

rm -rf "$WORK" && mkdir -p "$WORK/bin" "$WORK/cfg/PinPointStudio" "$WORK/lib" "$WORK/home" || { echo "cannot make $WORK"; exit 1; }
WORK=$(cd "$WORK" && pwd -P)
MARK="$WORK/standin.marker"
FAILS=0 PASSES=0
pass() { PASSES=$((PASSES + 1)); echo "  ok   $*"; }
fail() { FAILS=$((FAILS + 1)); echo "  FAIL $*"; }
now_ms() { perl -MTime::HiRes=time -e 'printf "%d\n", time * 1000'; }

# ---- the stand-in ------------------------------------------------------------------------------------------------
cat > "$WORK/standin.c" <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern char **environ;
#ifdef CARRY_SWITCH_TEXT
__attribute__((used)) static const char switch_text[] = "[HeadlessProbe] not started: the pose model";
#endif
int main(int argc, char **argv) {
    int secs = 1, rc = 0;
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "sleep=", 6) == 0) secs = atoi(argv[i] + 6);
        if (strncmp(argv[i], "rc=", 3) == 0) rc = atoi(argv[i] + 3);
    }
    const char *m = getenv("PINPOINT_STANDIN_MARKER");
    FILE *f = m ? fopen(m, "w") : NULL;
    if (f) {
        fprintf(f, "pid=%d\n", (int)getpid());
        for (int i = 0; i < argc; ++i) fprintf(f, "arg=%s\n", argv[i]);
        for (char **e = environ; *e; ++e) fprintf(f, "env=%s\n", *e);
        fclose(f);
    }
    fprintf(stderr, "standin: started\n");
    sleep(secs);
    fprintf(stderr, "standin: done rc=%d\n", rc);
    return rc;
}
EOF
CC_BIN=$(command -v cc || command -v clang || command -v gcc)
[ -n "$CC_BIN" ] || { echo "FAIL: no C compiler (cc) to build the stand-in"; exit 1; }
"$CC_BIN" -o "$WORK/bin/standin" "$WORK/standin.c" || { echo "FAIL: the stand-in did not compile"; exit 1; }
"$CC_BIN" -DCARRY_SWITCH_TEXT -o "$WORK/bin/standin_with_switch_text" "$WORK/standin.c" || { echo "FAIL: compile"; exit 1; }
cp "$WORK/bin/standin" "$WORK/bin/PinPointStudio"     # the real app's name, without the switch's text
STANDIN="$WORK/bin/standin"

# ---- settings files ----------------------------------------------------------------------------------------------
INI="$WORK/cfg/PinPointStudio/PinPointStudio.ini"
printf '[%%General]\nathleteLibraryPath=%s\ncheckForUpdates=false\n' "$WORK/lib" > "$INI"
ini() {   # ini <name> <body>: a settings file at <WORK>/bad/<name>/PinPointStudio/PinPointStudio.ini
    mkdir -p "$WORK/bad/$1/PinPointStudio"
    printf '%b' "$2" > "$WORK/bad/$1/PinPointStudio/PinPointStudio.ini"
    printf '%s\n' "$WORK/bad/$1/PinPointStudio/PinPointStudio.ini"
}
mkdir -p "$WORK/athlete_folder" && echo '{}' > "$WORK/athlete_folder/athlete.json"
ln -s "$HOME" "$WORK/link_to_real_home"

# run <expected rc> <name> <launcher args...>: runs the launcher, checks its exit code.
run() {
    local want="$1" name="$2"; shift 2
    rm -f "$MARK"
    OUT=$("$LAUNCH" "$@" 2>&1); RC=$?
    if [ "$RC" = "$want" ]; then pass "$name (exit $RC)"; else fail "$name: exit $RC, wanted $want"; printf '%s\n' "$OUT" | sed 's/^/         /'; fi
}
# refused <name> <launcher args...>: exit 2, with words, and the stand-in never ran.
refused() {
    local name="$1"; shift
    run 2 "refuses: $name" "$@"
    [ -e "$MARK" ] && fail "refuses: $name: the stand-in RAN"
    printf '%s\n' "$OUT" | grep -q 'REFUSED, nothing launched' || fail "refuses: $name: no 'REFUSED' words"
}
MK="--env PINPOINT_STANDIN_MARKER=$MARK"
OK_HOME="$WORK/home"

echo "run_headless_selftest: $WORK"
echo "-- refusals (exit 2, nothing launched)"
refused "no --home"                         --app "$STANDIN" --settings "$INI" $MK -- sleep=0
refused "no --settings"                     --app "$STANDIN" --home "$OK_HOME" $MK -- sleep=0
refused "no --app"                          --home "$OK_HOME" --settings "$INI" $MK -- sleep=0
refused "--home is the real home"           --app "$STANDIN" --home "$HOME" --settings "$INI" $MK -- sleep=0
refused "--home beneath the real home"      --app "$STANDIN" --home "$HOME/pps-probe-home" --settings "$INI" $MK -- sleep=0
refused "--settings is the live ini"        --app "$STANDIN" --home "$OK_HOME" --settings "$HOME/.config/PinPointStudio/PinPointStudio.ini" $MK -- sleep=0
refused "--home on the live share"          --app "$STANDIN" --home /mnt/swingdata/athletes --settings "$INI" $MK -- sleep=0
refused "--home is /mnt/swingdata"          --app "$STANDIN" --home /mnt/swingdata --settings "$INI" $MK -- sleep=0
refused "--home is a scratch root itself"   --app "$STANDIN" --home /mnt/swingdata/scratch --settings "$INI" $MK -- sleep=0
refused "--home next to scratch (scratchX)" --app "$STANDIN" --home /mnt/swingdata/scratchX/h --settings "$INI" $MK -- sleep=0
refused "--home with a '..' traversal"      --app "$STANDIN" --home "$WORK/../../../../../../tmp/h" --settings "$INI" $MK -- sleep=0
refused "--home through a symlink out"      --app "$STANDIN" --home "$WORK/link_to_real_home/pps-probe" --settings "$INI" $MK -- sleep=0
refused "--home with whitespace"            --app "$STANDIN" --home "$WORK/a b" --settings "$INI" $MK -- sleep=0
mkdir -p "$WORK/bad/shape" && cp "$INI" "$WORK/bad/shape/settings.ini"
refused "--settings not <dir>/PinPointStudio/PinPointStudio.ini" \
                                            --app "$STANDIN" --home "$OK_HOME" --settings "$WORK/bad/shape/settings.ini" $MK -- sleep=0
refused "--settings missing"                --app "$STANDIN" --home "$OK_HOME" --settings "$WORK/none/PinPointStudio/PinPointStudio.ini" $MK -- sleep=0
refused "library is /mnt/swingdata"         --app "$STANDIN" --home "$OK_HOME" --settings "$(ini share '[%General]\nathleteLibraryPath=/mnt/swingdata\n')" $MK -- sleep=0
refused "library is an athlete on the share" --app "$STANDIN" --home "$OK_HOME" --settings "$(ini athlete '[%General]\nathleteLibraryPath=/mnt/swingdata/Mark\n')" $MK -- sleep=0
refused "library is the scratch root"       --app "$STANDIN" --home "$OK_HOME" --settings "$(ini root '[%General]\nathleteLibraryPath=/mnt/swingdata/scratch\n')" $MK -- sleep=0
refused "library is the real home"          --app "$STANDIN" --home "$OK_HOME" --settings "$(ini realhome '[%General]\nathleteLibraryPath='"$HOME"'\n')" $MK -- sleep=0
refused "library is an athlete folder"      --app "$STANDIN" --home "$OK_HOME" --settings "$(ini athfolder '[%General]\nathleteLibraryPath='"$WORK"'/athlete_folder\n')" $MK -- sleep=0
refused "library quoted, on the share"      --app "$STANDIN" --home "$OK_HOME" --settings "$(ini quoted '[%General]\nathleteLibraryPath="/mnt/swingdata"\n')" $MK -- sleep=0
refused "library only under [General]"      --app "$STANDIN" --home "$OK_HOME" --settings "$(ini general '[General]\nathleteLibraryPath='"$WORK"'/lib\n')" $MK -- sleep=0
refused "library key missing"               --app "$STANDIN" --home "$OK_HOME" --settings "$(ini nokey '[%General]\ncheckForUpdates=false\n')" $MK -- sleep=0
refused "library empty"                     --app "$STANDIN" --home "$OK_HOME" --settings "$(ini empty '[%General]\nathleteLibraryPath=\n')" $MK -- sleep=0
refused "a second library line, live"       --app "$STANDIN" --home "$OK_HOME" --settings "$(ini second '[%General]\nathleteLibraryPath='"$WORK"'/lib\n[old]\nathleteLibraryPath=/Users/x/lib\n')" $MK -- sleep=0
refused "another key into the live share"   --app "$STANDIN" --home "$OK_HOME" --settings "$(ini otherkey '[%General]\nathleteLibraryPath='"$WORK"'/lib\n[ui]\nwristReferenceSwingDir=/mnt/swingdata/Mark/s1\n')" $MK -- sleep=0
refused "--log outside scratch"             --app "$STANDIN" --home "$OK_HOME" --settings "$INI" --log "$HOME/run_headless.log" $MK -- sleep=0
refused "--env a non-PINPOINT name"         --app "$STANDIN" --home "$OK_HOME" --settings "$INI" --env DYLD_INSERT_LIBRARIES=/x $MK -- sleep=0
refused "--env overriding the switch"       --app "$STANDIN" --home "$OK_HOME" --settings "$INI" --env PINPOINT_HEADLESS_PROBE=0 $MK -- sleep=0
refused "--env a path into the live share"  --app "$STANDIN" --home "$OK_HOME" --settings "$INI" --env PINPOINT_WORKONS_DIR=/mnt/swingdata/Mark $MK -- sleep=0
refused "--timeout not a number"            --app "$STANDIN" --home "$OK_HOME" --settings "$INI" --timeout 5s $MK -- sleep=0
refused "an app named PinPointStudio without the switch's text" \
                                            --app "$WORK/bin/PinPointStudio" --home "$OK_HOME" --settings "$INI" $MK -- sleep=0
refused "a test hook on a binary named PinPointStudio" \
                                            --app "$WORK/bin/PinPointStudio" --home "$OK_HOME" --settings "$INI" --_test_drop_switch $MK -- sleep=0
refused "a test hook on a binary carrying the switch" \
                                            --app "$WORK/bin/standin_with_switch_text" --home "$OK_HOME" --settings "$INI" --_test_drop_switch $MK -- sleep=0
[ -e "$HOME/pps-probe-home" ] && fail "a refusal made $HOME/pps-probe-home"
[ -e "$HOME/pps-probe" ] && fail "a refusal made $HOME/pps-probe"

# killed <name> <hook>: exit 3 within about a second, the stand-in gone.
killed() {
    local name="$1" hook="$2" words="$3" t0 t1 pid
    t0=$(now_ms)
    run 3 "kills: $name" --app "$STANDIN" --home "$OK_HOME" --settings "$INI" "$hook" $MK -- sleep=30
    t1=$(now_ms)
    printf '%s\n' "$OUT" | grep -q "KILLED: pid [0-9]* $words" || fail "kills: $name: no 'KILLED: ... $words' words"
    [ $((t1 - t0)) -lt 1500 ] && pass "kills: $name within $((t1 - t0)) ms" || fail "kills: $name took $((t1 - t0)) ms"
    pid=$(printf '%s\n' "$OUT" | sed -n 's/.*KILLED: pid \([0-9]*\) .*/\1/p')
    if [ -z "$pid" ]; then fail "kills: $name: the KILLED line names no pid"
    elif kill -0 "$pid" 2>/dev/null; then fail "kills: $name: the stand-in (pid $pid) is still running"; kill -KILL "$pid"
    else pass "kills: $name: the stand-in (pid $pid) is gone"; fi
}
echo "-- the kill path (exit 3)"
killed "a launch without PINPOINT_HEADLESS_PROBE" --_test_drop_switch "is running WITHOUT PINPOINT_HEADLESS_PROBE=1"
killed "a launch with the wrong HOME" --_test_wrong_home "is running with a HOME other than"

echo "-- the happy path"
echo 'import QtQuick' > "$WORK/probe.qml"
run 7 "runs the stand-in and passes its exit code" --app "$STANDIN" --home "$OK_HOME" --settings "$INI" \
    --probe "$WORK/probe.qml" --log "$WORK/happy.log" $MK -- sleep=3 rc=7
printf '%s\n' "$OUT" | grep -q "checked: pid .* PINPOINT_HEADLESS_PROBE=1" && pass "said the switch was checked" || fail "no 'checked' line"
printf '%s\n' "$OUT" | grep -q "log: $WORK/happy.log" && pass "printed the log path" || fail "did not print the log path"
grep -q 'standin: started' "$WORK/happy.log" && grep -q 'standin: done rc=7' "$WORK/happy.log" &&
    pass "teed the app's stderr to the log" || fail "the log lacks the stand-in's stderr"
GOT_ENV=$(sed -n 's/^env=//p' "$MARK" | sort)
WANT_ENV=$(printf '%s\n' "PINPOINT_HEADLESS_PROBE=1" "QT_QPA_PLATFORM=offscreen" "PINPOINT_LOG_STDERR=1" \
    "HOME=$OK_HOME" "CFFIXED_USER_HOME=$OK_HOME" "XDG_CONFIG_HOME=$WORK/cfg" "PINPOINT_DIAGNOSTICS_DIR=$OK_HOME/diagnostics" "PINPOINT_RIG_DIR=$OK_HOME/rig" \
    "TMPDIR=$OK_HOME/tmp" "PATH=/usr/bin:/bin:/usr/sbin:/sbin" "LANG=en_US.UTF-8" "PINPOINT_STANDIN_MARKER=$MARK" | sort)
# macOS adds __CF_USER_TEXT_ENCODING to a process at exec; that one is not ours to control.
if [ "$(printf '%s\n' "$GOT_ENV" | grep -v '^__CF_USER_TEXT_ENCODING=')" = "$WANT_ENV" ]; then
    pass "the app's environment is exactly the launcher's list"
else
    fail "the app's environment differs:"; diff <(printf '%s\n' "$WANT_ENV") <(printf '%s\n' "$GOT_ENV") | sed 's/^/         /'
fi
printf '%s\n' "$GOT_ENV" | grep -qxF -e "HOME=$HOME" -e "CFFIXED_USER_HOME=$HOME" -e "XDG_CONFIG_HOME=$HOME/.config" && fail "the real home ($HOME) is in the app's environment" || pass "the real home is not in the app's environment"
[ "$(sed -n 's/^arg=//p' "$MARK" | sed -n 2,3p | tr '\n' ' ')" = "--probe-qml $WORK/probe.qml " ] &&
    pass "--probe became --probe-qml <absolute path>" || fail "--probe was not passed as --probe-qml: $(sed -n 's/^arg=//p' "$MARK" | tr '\n' ' ')"
run 0 "an exit 0 passes through" --app "$STANDIN" --home "$OK_HOME" --settings "$INI" $MK -- sleep=3 rc=0
[ -n "$(ls "$OK_HOME"/run_headless-*.log 2>/dev/null)" ] && pass "the default log is in the scratch home" || fail "no default log in $OK_HOME"

echo "-- the timeout (exit 124)"
T0=$(now_ms)
run 124 "stops an app that runs past --timeout" --app "$STANDIN" --home "$OK_HOME" --settings "$INI" --timeout 1 $MK -- sleep=30
T1=$(now_ms)
[ $((T1 - T0)) -lt 6000 ] && pass "stopped within $((T1 - T0)) ms" || fail "the timeout took $((T1 - T0)) ms"
PID=$(sed -n 's/^pid=//p' "$MARK")
if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then fail "the timed-out stand-in (pid $PID) is still running"; kill -KILL "$PID"; else pass "no stand-in left running"; fi

echo "run_headless_selftest: $PASSES passed, $FAILS failed"
[ "$FAILS" = 0 ] && rm -rf "$WORK"
[ "$FAILS" = 0 ]
