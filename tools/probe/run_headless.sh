#!/bin/bash
# run_headless.sh — THE ONE WAY AN AGENT LAUNCHES PINPOINT STUDIO FOR A PROBE (tooling 5.3d).
#
# WHY. A probe launch once had its environment assignments passed to the shell as one word: the switch never reached
# the app, which started for real (windowed, the real home and settings, the LIVE library), and its start-up catch-up
# rewrote real files. Nothing checked that the switch had taken effect. This launcher refuses a real home, settings
# file or library before it starts anything, builds the app's environment itself, and kills the app within a second if
# the running process does not carry the switch.
#
#   run_headless.sh --app <PinPointStudio binary> --home <scratch dir> --settings <scratch ini>
#                   [--probe <qml>] [--timeout <s>] [--log <file>] [--env PINPOINT_NAME=value]... -- [app args]
#
#   --app       the binary (a macOS build: .../PinPointStudio.app/Contents/MacOS/PinPointStudio).
#   --home      the scratch home: HOME and CFFIXED_USER_HOME. Made if missing.
#   --settings  the scratch settings file. The app reads QSettings' IniFormat user file, which is
#               $XDG_CONFIG_HOME/PinPointStudio/PinPointStudio.ini, and no variable names the file itself, so it must be
#               <dir>/PinPointStudio/PinPointStudio.ini; XDG_CONFIG_HOME is set to <dir>. Its [%General] section must
#               hold athleteLibraryPath (AppSettings' "General/athleteLibraryPath") pointing at a scratch library.
#   --probe     a probe QML file: passed as --probe-qml <absolute path>.
#   --timeout   seconds before the app is stopped (SIGTERM, then SIGKILL 3 s later). Default 120.
#   --log       where the app's stderr is teed. Default <home>/run_headless-<time>-<pid>.log. Printed at the start.
#   --env       one more variable for the app, PINPOINT_* only (a probe seam: PINPOINT_WORKONS_DIR,
#               PINPOINT_PROBE_REANALYSE, PINPOINT_CORE_*, ...). A value that is an absolute path must be under a scratch
#               root or inside this repository. It cannot set the variables below.
#
# SCRATCH ROOTS. --home, --settings, --log and the library must be strictly beneath one of: /mnt/swingdata/scratch/,
# this repository's build/ (the worktree's and the main tree's), or the agent session scratchpad /private/tmp/claude-<uid>/
# (Linux: /tmp/claude-<uid>/). Paths are compared after resolving symlinks; a ".." component or any whitespace is refused.
#
# THE APP'S ENVIRONMENT is exactly this, through /usr/bin/env -i, each one a separate argument:
#   PINPOINT_HEADLESS_PROBE=1 QT_QPA_PLATFORM=offscreen PINPOINT_LOG_STDERR=1 HOME CFFIXED_USER_HOME XDG_CONFIG_HOME
#   PINPOINT_DIAGNOSTICS_DIR=<home>/diagnostics TMPDIR=<home>/tmp PATH=/usr/bin:/bin:/usr/sbin:/sbin LANG=en_US.UTF-8
#   plus each --env.
#
# THE CHECK. Within 1 s of the launch the running process's own environment is read (macOS: ps -E; Linux:
# /proc/<pid>/environ). Unless it holds PINPOINT_HEADLESS_PROBE=1 and HOME=<the scratch home> as whole entries, the
# process is killed at once (SIGKILL). An environment that cannot be read in that second (a hardened binary that hides
# it) is killed too: the check fails closed.
#
# EXIT. 2: refused, nothing launched. 3: the check failed, the process was killed. 124: the timeout stopped the app.
# Otherwise the app's own exit code (128+n for a signal). Every exit says why on stderr, prefixed "run_headless:".
#
# TEST HOOKS (run_headless_selftest.sh only): --_test_drop_switch leaves PINPOINT_HEADLESS_PROBE out, and
# --_test_wrong_home gives the app HOME=<home>/elsewhere, so the kill path can be proved. They are refused for any binary
# named PinPointStudio or carrying the switch's log text: a test hook never launches the real app without the switch.

PROG="run_headless"
say()    { printf '%s: %s\n' "$PROG" "$*" >&2; }
refuse() { say "REFUSED, nothing launched: $*"; exit 2; }

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd -P)
REPO_TOP=$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null)
REPO_COMMON=$(git -C "$SCRIPT_DIR" rev-parse --path-format=absolute --git-common-dir 2>/dev/null)
[ -n "$REPO_TOP" ] || REPO_TOP=$(cd "$SCRIPT_DIR/../.." && pwd -P)

# canon <path>: an absolute path with symlinks resolved, for a path that may not exist yet (its deepest existing
# ancestor is resolved and the rest appended). Prints nothing for a ".." component or whitespace.
canon() {
    local p="$1" head tail=""
    case "$p" in *[[:space:]]*) return 1 ;; esac
    case "/$p/" in */../*) return 1 ;; esac
    case "$p" in /*) ;; *) p="$PWD/$p" ;; esac
    head="$p"
    while [ ! -d "$head" ]; do
        tail="/$(basename "$head")$tail"
        head=$(dirname "$head")
    done
    head=$(cd "$head" 2>/dev/null && pwd -P) || return 1
    p="${head%/}$tail"
    # A trailing symlink to a file (the settings file, the log) is resolved too.
    if [ -L "$p" ]; then
        local t; t=$(readlink "$p")
        case "$t" in /*) ;; *) t="$(dirname "$p")/$t" ;; esac
        canon "$t"; return
    fi
    printf '%s\n' "$p"
}

ROOTS=()
add_root() { local r; r=$(canon "$1") && [ -n "$r" ] && ROOTS+=("$r"); }
add_root /mnt/swingdata/scratch
add_root "$REPO_TOP/build"
[ -n "$REPO_COMMON" ] && add_root "$(dirname "$REPO_COMMON")/build"
add_root "/private/tmp/claude-$(id -u)"
add_root "/tmp/claude-$(id -u)"

# under_scratch <canonical path>: strictly beneath a scratch root (never the root itself).
under_scratch() {
    local r
    for r in "${ROOTS[@]}"; do
        case "$1" in "$r"/?*) return 0 ;; esac
    done
    return 1
}
under_repo() { case "$1" in "$REPO_TOP"/?*) return 0 ;; esac; return 1; }

REAL_HOME=$(canon "${HOME:-/nonexistent}")
LIVE_INI="$REAL_HOME/.config/PinPointStudio/PinPointStudio.ini"
ROOT_WORDS="/mnt/swingdata/scratch/, $REPO_TOP/build/, or the session scratchpad /private/tmp/claude-$(id -u)/"

# must_scratch <what> <given path> -> prints the canonical path, or refuses.
must_scratch() {
    local what="$1" given="$2" c
    [ -n "$given" ] || refuse "$what is required"
    c=$(canon "$given") || refuse "$what '$given' has a '..' component or whitespace"
    [ -n "$c" ] || refuse "$what '$given' cannot be resolved"
    if [ "$c" = "$REAL_HOME" ]; then refuse "$what '$given' is the real home ($REAL_HOME)"; fi
    if [ "$c" = "$LIVE_INI" ]; then refuse "$what '$given' is the live settings file ($LIVE_INI)"; fi
    case "$c" in /mnt/swingdata|/mnt/swingdata/) refuse "$what '$given' is /mnt/swingdata itself, the live library share" ;; esac
    under_scratch "$c" || refuse "$what '$given' (resolved: $c) is not beneath a scratch root: $ROOT_WORDS"
    printf '%s\n' "$c"
}

# ---- arguments ---------------------------------------------------------------------------------------------------
APP="" HOME_ARG="" SETTINGS_ARG="" PROBE="" TIMEOUT=120 LOG_ARG="" DROP_SWITCH=0 WRONG_HOME=0
EXTRA_ENV=() APP_ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --app)      [ $# -ge 2 ] || refuse "--app needs a value";      APP="$2"; shift 2 ;;
        --home)     [ $# -ge 2 ] || refuse "--home needs a value";     HOME_ARG="$2"; shift 2 ;;
        --settings) [ $# -ge 2 ] || refuse "--settings needs a value"; SETTINGS_ARG="$2"; shift 2 ;;
        --probe)    [ $# -ge 2 ] || refuse "--probe needs a value";    PROBE="$2"; shift 2 ;;
        --timeout)  [ $# -ge 2 ] || refuse "--timeout needs a value";  TIMEOUT="$2"; shift 2 ;;
        --log)      [ $# -ge 2 ] || refuse "--log needs a value";      LOG_ARG="$2"; shift 2 ;;
        --env)      [ $# -ge 2 ] || refuse "--env needs a value";      EXTRA_ENV+=("$2"); shift 2 ;;
        --_test_drop_switch) DROP_SWITCH=1; shift ;;
        --_test_wrong_home)  WRONG_HOME=1; shift ;;
        --) shift; APP_ARGS=("$@"); break ;;
        -h|--help) sed -n '2,46p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) refuse "unknown argument '$1' (app arguments go after --)" ;;
    esac
done

# ---- the app -----------------------------------------------------------------------------------------------------
[ -n "$APP" ] || refuse "--app is required"
[ -f "$APP" ] && [ -x "$APP" ] || refuse "--app '$APP' is not an executable file"
case "$APP" in /*) ;; *) APP="$PWD/$APP" ;; esac
APP_NAME=$(basename "$APP")
IS_REAL_APP=0 HAS_SWITCH=0
LC_ALL=C grep -q -F '[HeadlessProbe] not started' "$APP" 2>/dev/null && HAS_SWITCH=1 IS_REAL_APP=1
case "$APP_NAME" in [Pp]in[Pp]oint[Ss]tudio*) IS_REAL_APP=1 ;; esac
if [ $IS_REAL_APP = 1 ] && [ $HAS_SWITCH = 0 ]; then
    refuse "--app '$APP' does not carry PINPOINT_HEADLESS_PROBE (built before refactor 4-c?): rebuild it"
fi
if [ $DROP_SWITCH = 1 ] || [ $WRONG_HOME = 1 ]; then
    [ $IS_REAL_APP = 0 ] || refuse "a --_test_* hook is for the self-test's stand-in only, never for the app ('$APP')"
fi

# ---- home, settings, library -------------------------------------------------------------------------------------
HOME_DIR=$(must_scratch "--home" "$HOME_ARG") || exit 2
SETTINGS=$(must_scratch "--settings" "$SETTINGS_ARG") || exit 2
[ -f "$SETTINGS" ] || refuse "--settings '$SETTINGS' does not exist"
[ "$(basename "$SETTINGS")" = "PinPointStudio.ini" ] && [ "$(basename "$(dirname "$SETTINGS")")" = "PinPointStudio" ] ||
    refuse "--settings must be <dir>/PinPointStudio/PinPointStudio.ini (the app finds it as \$XDG_CONFIG_HOME/PinPointStudio/PinPointStudio.ini); got '$SETTINGS'"
CONFIG_HOME=$(dirname "$(dirname "$SETTINGS")")

# The library: every athleteLibraryPath line, in any section, must be scratch, and [%General] must have one (Qt's ini
# keeps the "General/" group as [%General]; a plain [General] is the root group, which the app does not read for it).
LIB_GENERAL="" ; LIB_SEEN=0 ; SECTION=""
while IFS= read -r line || [ -n "$line" ]; do
    line="${line%$'\r'}"
    case "$line" in
        \[*\]*) SECTION="${line#[}"; SECTION="${SECTION%%]*}"; continue ;;
        \;*|\#*|"") continue ;;
    esac
    key="${line%%=*}"; [ "$key" = "$line" ] && continue
    val="${line#*=}"
    key=$(printf '%s' "$key" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
    val=$(printf '%s' "$val" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//; s/^"\(.*\)"$/\1/')
    # No setting may point into the live share.
    case "$val" in
        /mnt/swingdata/scratch/?*) ;;
        /mnt/swingdata|/mnt/swingdata/*) refuse "the settings file's [$SECTION] $key points into the live share ($val)" ;;
    esac
    case "$key" in
        athleteLibraryPath|General/athleteLibraryPath|General\\athleteLibraryPath) ;;
        *) continue ;;
    esac
    LIB_SEEN=1
    [ -n "$val" ] || refuse "the settings file's [$SECTION] athleteLibraryPath is empty"
    lib=$(canon "$val") || refuse "the settings file's athleteLibraryPath '$val' has a '..' component or whitespace"
    [ "$lib" = "$REAL_HOME" ] && refuse "the settings file's athleteLibraryPath is the real home"
    under_scratch "$lib" || refuse "the settings file's [$SECTION] athleteLibraryPath '$val' (resolved: $lib) is not beneath a scratch root: $ROOT_WORDS"
    [ -e "$lib/athlete.json" ] && refuse "the settings file's athleteLibraryPath '$val' is an athlete folder (it holds athlete.json), not a library"
    [ "$SECTION" = "%General" ] && LIB_GENERAL="$lib"
done < "$SETTINGS"
[ -n "$LIB_GENERAL" ] || refuse "the settings file has no athleteLibraryPath under [%General] (the key the app reads: General/athleteLibraryPath)$([ $LIB_SEEN = 1 ] && echo '; one is in another section, where the app does not read it')"

# ---- probe, timeout, log, extra environment ----------------------------------------------------------------------
if [ -n "$PROBE" ]; then
    [ -f "$PROBE" ] || refuse "--probe '$PROBE' is not a file"
    PROBE=$(canon "$PROBE") || refuse "--probe has a '..' component or whitespace"
    APP_ARGS=(--probe-qml "$PROBE" ${APP_ARGS[@]+"${APP_ARGS[@]}"})
fi
case "$TIMEOUT" in ''|*[!0-9]*) refuse "--timeout '$TIMEOUT' is not a whole number of seconds" ;; esac
[ "$TIMEOUT" -ge 1 ] || refuse "--timeout must be at least 1 s"
[ -n "$LOG_ARG" ] || LOG_ARG="$HOME_DIR/run_headless-$(date +%Y%m%d-%H%M%S)-$$.log"
LOG=$(must_scratch "--log" "$LOG_ARG") || exit 2

RESERVED=" PINPOINT_HEADLESS_PROBE PINPOINT_LOG_STDERR PINPOINT_DIAGNOSTICS_DIR QT_QPA_PLATFORM HOME CFFIXED_USER_HOME XDG_CONFIG_HOME TMPDIR PATH LANG "
for kv in ${EXTRA_ENV[@]+"${EXTRA_ENV[@]}"}; do
    name="${kv%%=*}"; value="${kv#*=}"
    [ "$name" != "$kv" ] || refuse "--env '$kv' is not NAME=value"
    printf '%s' "$name" | grep -Eq '^PINPOINT_[A-Z0-9_]+$' || refuse "--env '$name': only PINPOINT_* variables can be added"
    case "$RESERVED" in *" $name "*) refuse "--env '$name' is set by the launcher and cannot be overridden" ;; esac
    case "$value" in
        /*) c=$(canon "$value") || refuse "--env $name: '$value' has a '..' component or whitespace"
            under_scratch "$c" || under_repo "$c" ||
                refuse "--env $name: '$value' (resolved: $c) is neither beneath a scratch root nor inside $REPO_TOP" ;;
    esac
done

# ---- the environment ---------------------------------------------------------------------------------------------
mkdir -p "$HOME_DIR/diagnostics" "$HOME_DIR/tmp" "$(dirname "$LOG")" || refuse "cannot make the scratch folders under $HOME_DIR"
APP_HOME="$HOME_DIR"
[ $WRONG_HOME = 1 ] && APP_HOME="$HOME_DIR/elsewhere"
ENVV=()
[ $DROP_SWITCH = 1 ] || ENVV+=("PINPOINT_HEADLESS_PROBE=1")
ENVV+=("QT_QPA_PLATFORM=offscreen" "PINPOINT_LOG_STDERR=1"
       "HOME=$APP_HOME" "CFFIXED_USER_HOME=$APP_HOME" "XDG_CONFIG_HOME=$CONFIG_HOME"
       "PINPOINT_DIAGNOSTICS_DIR=$HOME_DIR/diagnostics" "TMPDIR=$HOME_DIR/tmp"
       "PATH=/usr/bin:/bin:/usr/sbin:/sbin" "LANG=en_US.UTF-8")
ENVV+=(${EXTRA_ENV[@]+"${EXTRA_ENV[@]}"})

say "log: $LOG"
say "app: $APP"
say "home: $HOME_DIR  settings: $SETTINGS  library: $LIB_GENERAL  timeout: ${TIMEOUT}s"

# ---- launch ------------------------------------------------------------------------------------------------------
# The check decides only once the pid's argv[0] is the app (read_env). The nonce is a second guard: env -i strips it,
# so an environment that still holds it is not the app's.
export PP_RUN_HEADLESS_NONCE="nonce-$$-$RANDOM$RANDOM"
FIFO_DIR=$(mktemp -d "$HOME_DIR/tmp/run_headless.XXXXXX") || refuse "cannot make a fifo folder"
FIFO="$FIFO_DIR/stderr"
mkfifo "$FIFO" || refuse "cannot make a fifo"
tee "$LOG" < "$FIFO" >&2 &
TEE_PID=$!
/usr/bin/env -i "${ENVV[@]}" "$APP" ${APP_ARGS[@]+"${APP_ARGS[@]}"} 2> "$FIFO" &
PID=$!
WATCH_PID=""

cleanup() {
    if [ -n "$WATCH_PID" ]; then pkill -P "$WATCH_PID" 2>/dev/null; kill "$WATCH_PID" 2>/dev/null; fi
    wait "$TEE_PID" 2>/dev/null
    rm -rf "$FIFO_DIR"
}
on_signal() { say "interrupted: killing the app (pid $PID)"; kill -KILL "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; cleanup; exit 130; }
trap on_signal INT TERM HUP

# read_env <pid>: the process's environment, one entry per line; nothing until the pid has exec'd the app (its argv[0]
# is $APP: before that it is this shell's fork or /usr/bin/env, whose environment is not the app's) or while it cannot
# be read.
read_env() {
    if [ -r "/proc/$1/environ" ]; then
        local argv0
        argv0=$(tr '\0' '\n' < "/proc/$1/cmdline" 2>/dev/null | head -n 1)
        [ "$argv0" = "$APP" ] || return
        tr '\0' '\n' < "/proc/$1/environ" 2>/dev/null
        return
    fi
    local full cmd rest
    full=$(ps -E -ww -o command= -p "$1" 2>/dev/null) || return
    cmd=$(ps -ww -o command= -p "$1" 2>/dev/null) || return
    case "$cmd" in "$APP"|"$APP "*) ;; *) return ;; esac
    rest="${full#"$cmd"}"
    [ "$rest" != "$full" ] || return                  # exec'd between the two reads: read again
    printf '%s\n' "$rest" | tr ' ' '\n'              # entries hold no whitespace: every path we set was checked
}

kill_now() { kill -KILL "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; cleanup; say "KILLED: $*"; exit 3; }

sleep 0.9 & DEADLINE_PID=$!
VERDICT=""
while :; do
    if ! kill -0 "$PID" 2>/dev/null; then VERDICT="gone"; break; fi
    ENV_NOW=$(read_env "$PID")
    if [ -n "$ENV_NOW" ] && ! printf '%s\n' "$ENV_NOW" | grep -qxF "PP_RUN_HEADLESS_NONCE=$PP_RUN_HEADLESS_NONCE"; then
        if ! printf '%s\n' "$ENV_NOW" | grep -qxF "PINPOINT_HEADLESS_PROBE=1"; then VERDICT="noswitch"
        elif ! printf '%s\n' "$ENV_NOW" | grep -qxF "HOME=$HOME_DIR"; then VERDICT="nohome"
        else VERDICT="ok"; fi
        break
    fi
    kill -0 "$DEADLINE_PID" 2>/dev/null || { VERDICT="unread"; break; }
    sleep 0.01
done
kill "$DEADLINE_PID" 2>/dev/null; wait "$DEADLINE_PID" 2>/dev/null
case "$VERDICT" in
    ok)       say "checked: pid $PID runs with PINPOINT_HEADLESS_PROBE=1 and HOME=$HOME_DIR" ;;
    noswitch) kill_now "pid $PID is running WITHOUT PINPOINT_HEADLESS_PROBE=1 in its environment" ;;
    nohome)   kill_now "pid $PID is running with a HOME other than $HOME_DIR ($(printf '%s\n' "$ENV_NOW" | grep '^HOME=' | head -1))" ;;
    unread)   kill_now "pid $PID's environment could not be read within 1 s, so the switch could not be confirmed" ;;
    gone)     wait "$PID"; RC=$?; cleanup
              say "FAILED: the app exited ($RC) before its environment could be checked"; exit 3 ;;
esac

# ---- run to the end or the timeout -------------------------------------------------------------------------------
TIMED_OUT="$FIFO_DIR/timed_out"
( sleep "$TIMEOUT"; : > "$TIMED_OUT"; kill -TERM "$PID" 2>/dev/null; sleep 3; kill -KILL "$PID" 2>/dev/null ) &
WATCH_PID=$!
wait "$PID"; RC=$?
if [ -e "$TIMED_OUT" ]; then
    cleanup
    say "TIMEOUT: the app ran past ${TIMEOUT}s and was stopped (log: $LOG)"
    exit 124
fi
cleanup
say "the app exited $RC (log: $LOG)"
exit "$RC"
