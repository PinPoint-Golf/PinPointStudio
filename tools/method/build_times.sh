#!/bin/zsh
# build_times.sh — times the test tree's builds on the studio PC (the cabin, GOLFSIMPC) from the Mac, so a stage can
# say "build times: no worse" by timing the same steps before and after on the same machine (platform refactor B1.3;
# the Product Owner ruled, 24 Nov, that these timings are taken on the cabin only).
#
#   tools/method/build_times.sh <worktree> [--no-clean] [--header <src path>]... [--cpp <src path>] [--repeat]
#                                          [jobs N] [--dry-run]
#
# e.g. tools/method/build_times.sh build/wt-37c --repeat
#
# What it times, in build\tests-vs18 (Debug, NMake JOM, the tests/ umbrella; no compiler cache there: its CMakeCache
# has no compiler launcher), each step a run of tools/studio/studio-build.cmd's "tests" mode, at jom -jN (default 16):
#  1. clean:  "jom clean" in the tree, then the tests mode's build. The cabin keeps two trees and never a third, so this
#             is a clean of the existing tree, not a fresh configure: CMake's cache and autogen's outputs are kept.
#             --no-clean leaves it out.
#  2. header: each header in turn has its time stamp touched (copy /b <file> +,,: the content is unchanged, so the
#             checkout stays clean), then the build. Defaults: src/Core/pp_debug.h (included almost everywhere) and
#             src/Vocabulary/Analysis/swing_analysis.h. --header replaces the defaults (give it once per header).
#  3. link:   one test's own .cpp is touched (default src/Analysis/tests/session_summary_test.cpp, in one target only),
#             then the build: one compile and one link. NMake gives no per-step times, so the row is the whole jom run.
# --repeat runs the last header step a second time and prints the drift from the first (the check: within 10%).
# The time of each row is jom's own, from studio-build.cmd's "jom -jN" and "build exit" %TIME% stamps; the compiles
# and links are the "Building C/CXX object" and "Linking" lines of that run's tests_build.log. Peak memory is not
# measured on the cabin ("-"). The tests mode runs both ctest passes after every build, as its header says it does;
# their result is listed but is not what this tool measures (studio-build.cmd has no build-only mode, and this tool
# never edits it).
#
# How it runs:
#  - Lock: it holds the cabin lock (/mnt/swingdata/scratch/cabin.lock, tools/method/cabin_run.sh's lock and holder
#    format, lane build-times-<worktree>) for all its steps, with a ceiling of 5 h, and never writes a run= line, so a
#    waiting cabin_run.sh does not take the lock between two steps. It waits while another lane holds it, and reclaims
#    a lock over its ceiling, or one kept for a cabin run that is over, as cabin_run.sh does. It keeps the lock on any
#    exit while a cabin run it launched may still be live (then a waiter reclaims it at the ceiling).
#  - Before each step, the idle pre-flight: BUSY (exit 20) while PinPointStudio.exe, Foresight, FSX or GSPro runs;
#    waits while a jom.exe, a ctest.exe or a cmd.exe running studio-build.cmd or a scratch .cmd runs (up to 30 min,
#    then exit 24).
#  - Each step's wrapper (run.cmd, ASCII, CRLF) is written into a run dir on the share (/mnt/swingdata/scratch/
#    build-times-<stamp>/<step>/), launched detached through Win32_Process Create, and polled through the share every
#    60 s. The first step pulls the checkout fast-forward (when clean); every later one does not pull; if the cabin's
#    HEAD has moved all the same, that step's row is dropped and the tool stops (exit 25). Nothing is built with a patch: the cabin builds origin/main's tip.
#  - Output: one table, printed and written to <worktree>/build/build-times/<sha>.txt (<sha>: the cabin's HEAD). The
#    run dir is deleted when every step ends with a build exit 0; otherwise it is kept and named.
#
# Comparing two runs: run it at the commit before the change and at the commit after, with the same options and job
# count, at a time the cabin is otherwise idle, then diff the two <sha>.txt files row by row. A row is comparable only
# when its compiles count is the same or the change explains the difference. The repeat row's drift is the noise.
#
# --dry-run prints the wrappers it would launch and the steps, and touches nothing (no lock, no ssh, no share).
#
# Exit: 0 when every step's build exited 0; 11 a build (or the clean) failed; 20 BUSY; 21 refused on the Mac (no share,
# bad options, the worktree not on origin/main); 22 ssh or a launch failed; 23 a step did not end within 2 h (left
# running, the lock kept); 24 a foreign build still ran after 30 min; 25 the cabin's HEAD moved between steps.

SELF=${0:A}
HERE=${SELF:h}
setopt pipefail

HOST=developer@GOLFSIMPC.local
SHARE=/mnt/swingdata
WSHARE='C:\PinPointStudio'
WREPO='C:\Users\developer\Projects\PinPointStudio'
VCVARS='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=20 -o ServerAliveInterval=15 -o ServerAliveCountMax=4)
POLL=60 PRE_MAX=1800 RUN_MAX=7200 LAUNCH_GRACE=600 LOCK_CEIL=18000

say()  { print -r -- "build_times: $*"; }
die()  { local rc=$1; shift; print -u2 -r -- "build_times: $*"; exit $rc; }
now()  { date +%s; }
usage() { print -u2 "usage: build_times.sh <worktree> [--no-clean] [--header <src path>]... [--cpp <src path>] [--repeat] [jobs N] [--dry-run]"; }

# ── options ───────────────────────────────────────────────────────────────────────────────────────────────────────
WT= CLEAN=1 REPEAT=0 DRY=0 JOBS=16 CPP=src/Analysis/tests/session_summary_test.cpp
HEADERS=() DEF_HEADERS=(src/Core/pp_debug.h src/Vocabulary/Analysis/swing_analysis.h)
while (( $# )); do
    case $1 in
        --no-clean) CLEAN=0 ;;
        --repeat) REPEAT=1 ;;
        --dry-run) DRY=1 ;;
        --header) [[ -n $2 ]] || { usage; exit 21; }; HEADERS+=($2); shift ;;
        --cpp) [[ -n $2 ]] || { usage; exit 21; }; CPP=$2; shift ;;
        jobs) [[ $2 == <1-24> ]] || { usage; die 21 "jobs takes 1 to 24"; }; JOBS=$2; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) usage; exit 21 ;;
        *) [[ -z $WT ]] || { usage; exit 21; }; WT=$1 ;;
    esac
    shift
done
[[ -n $WT && -d $WT ]] || { usage; exit 21; }
WT=${WT:A}
(( ${#HEADERS} )) || HEADERS=($DEF_HEADERS)
for f in $HEADERS $CPP; do
    [[ $f == src/* && $f != *..* && -f $WT/$f ]] || die 21 "$f is not a file under src/ in $WT"
done
LANE="build-times-${WT:t}"

# Steps: "kind|file|label". clean has no file.
STEPS=()
(( CLEAN )) && STEPS+=("clean||clean (jom clean, then build)")
for f in $HEADERS; do STEPS+=("touch|$f|header ${f#src/}"); done
STEPS+=("touch|$CPP|link (touch ${CPP:t})")
(( REPEAT )) && STEPS+=("touch|${HEADERS[-1]}|header ${HEADERS[-1]#src/} (repeat)")

# ── the wrapper ───────────────────────────────────────────────────────────────────────────────────────────────────
# write_wrapper <file> <wrun> <kind> <src path> <pull 0|1>: the CRLF, ASCII run.cmd of one step.
write_wrapper() {
    local f=$1 wrun=$2 kind=$3 src=$4 pull=$5 wdir wname action pullblock
    if [[ $kind == clean ]]; then
        action="call \"$VCVARS\" >nul
set PATH=%PATH%;C:\\Qt\\Tools\\QtCreator\\bin\\jom;C:\\Qt\\Tools\\CMake_64\\bin
cd /d \"%REPO%\\build\\tests-vs18\"
>> \"%RUN%\\console.log\" echo build_times: clean start %TIME%
jom clean > \"%RUN%\\clean.log\" 2>&1
set CRC=%ERRORLEVEL%
>> \"%RUN%\\console.log\" echo build_times: clean exit %CRC% %TIME%
cd /d \"%REPO%\"
if not \"%CRC%\"==\"0\" ( set \"RC=11\" & goto done )"
    else
        wdir=${${src:h}//\//\\} wname=${src:t}
        action="pushd \"%REPO%\\$wdir\"
copy /b $wname +,, >nul
set CRC=%ERRORLEVEL%
for %%f in ($wname) do >> \"%RUN%\\console.log\" echo build_times: touched $src at %%~tf, copy exit %CRC%
popd
if not \"%CRC%\"==\"0\" ( set \"RC=11\" & goto done )"
    fi
    if (( pull )); then
        pullblock="set DIRTY=
for /f %%i in ('git status --short ^| find /c /v \"\"') do set DIRTY=%%i
if not \"%DIRTY%\"==\"0\" goto nopull
>> \"%RUN%\\console.log\" echo build_times: git pull --ff-only
git pull --ff-only >> \"%RUN%\\console.log\" 2>&1
if errorlevel 1 ( >> \"%RUN%\\console.log\" echo build_times: PULL FAILED & set \"RC=4\" & goto done )
goto pulled
:nopull
>> \"%RUN%\\console.log\" echo build_times: the checkout is not clean, so it is not pulled; studio-build.cmd refuses it
:pulled"
    else
        pullblock="rem a later step: no pull, so every step builds the same commit"
    fi
    print -r -- "@echo off
rem build_times.sh's wrapper, written on the Mac (tools/method/build_times.sh). ASCII, CRLF.
setlocal
set \"RUN=$wrun\"
set \"REPO=$WREPO\"
set RC=0
> \"%RUN%\\console.log\" echo build_times: wrapper start %DATE% %TIME%
cd /d \"%REPO%\"
$pullblock
for /f %%i in ('git rev-parse --short HEAD') do >> \"%RUN%\\console.log\" echo build_times: HEAD %%i
$action
call \"%REPO%\\tools\\studio\\studio-build.cmd\" tests nopull jobs $JOBS >> \"%RUN%\\console.log\" 2>&1
set RC=%ERRORLEVEL%
:done
>> \"%RUN%\\console.log\" echo build_times: wrapper end %DATE% %TIME% - exit %RC%
> \"%RUN%\\rc.tmp\" echo %RC%
move /y \"%RUN%\\rc.tmp\" \"%RUN%\\rc.txt\" >nul
exit /b %RC%" | sed 's/$/\r/' > "$f"
}

if (( DRY )); then
    say "dry run: $WT, jom -j$JOBS, ${#STEPS} step(s); nothing is locked, launched or written to the share"
    T=$(mktemp -d "${TMPDIR:-/tmp}/build_times_dry.XXXXXX")
    i=0
    for s in $STEPS; do
        (( i++ ))
        say "step $i: ${s##*|}"
        write_wrapper $T/run.cmd "$WSHARE\\scratch\\build-times-DRY\\$i" "${s%%|*}" "${${s#*|}%%|*}" $(( i == 1 ))
        tr -d '\r' < $T/run.cmd | sed 's/^/    /'
    done
    rm -rf $T
    exit 0
fi

[[ -d $SHARE/scratch ]] || die 21 "the share is not mounted at $SHARE"
git -C "$WT" fetch -q origin main 2>/dev/null || die 21 "git fetch origin main failed in $WT"
git -C "$WT" merge-base --is-ancestor HEAD origin/main || die 21 "$WT's HEAD is not on origin/main"
MACSHA=$(git -C "$WT" rev-parse --short origin/main)

# ── the lock (cabin_run.sh's format; no run= line) ────────────────────────────────────────────────────────────────
LOCK=$SHARE/scratch/cabin.lock TOKEN="$$-$RANDOM$RANDOM-$(now)" HAVE_LOCK=0 LIVE=0
field() { sed -n "s/^$1=//p" "$2" 2>/dev/null | head -1 | tr -d '\r'; }
holder_text() {
    print -r -- "lane=$LANE
pid=$$
host=$(hostname -s)
start=$LOCK_START
started=$LOCK_STARTED
ceiling=$LOCK_CEIL
token=$TOKEN"
}
lock_take() {
    local i t tok start ceil run pid host lane started moved
    while true; do
        if mkdir "$LOCK" 2>/dev/null; then
            LOCK_START=$(now) LOCK_STARTED=$(date '+%Y-%m-%d %H:%M:%S')
            for i in {1..30}; do
                holder_text > "$LOCK/holder" 2>/dev/null
                t=$(field token "$LOCK/holder")
                [[ $t == "$TOKEN" ]] && { HAVE_LOCK=1; say "took the cabin lock ($LOCK)"; return 0; }
                [[ -n $t || ! -e $LOCK ]] && break
                sleep 1
            done
            continue
        fi
        lane=$(field lane "$LOCK/holder"); pid=$(field pid "$LOCK/holder"); host=$(field host "$LOCK/holder")
        start=$(field start "$LOCK/holder"); started=$(field started "$LOCK/holder"); ceil=$(field ceiling "$LOCK/holder")
        tok=$(field token "$LOCK/holder"); run=$(field run "$LOCK/holder")
        [[ $start == <-> ]] || start=$(stat -f %m "$LOCK" 2>/dev/null || now)
        [[ $ceil == <-> ]] || ceil=9600
        local why=
        if (( $(now) - start > ceil )); then why="over its ceiling"
        elif [[ -n $tok && -n $run && -s $SHARE/scratch/$run/rc.txt ]] &&
             { [[ $host == "$(hostname -s)" && $pid == <-> ]] && ! kill -0 $pid 2>/dev/null ||
               (( $(now) - $(stat -f %m "$SHARE/scratch/$run/rc.txt") > LAUNCH_GRACE )); }; then
            why="kept for its cabin run $run, which is over"
        fi
        if [[ -n $why && -n $tok ]]; then
            moved="$LOCK.stale.$TOKEN"
            if mv "$LOCK" "$moved" 2>/dev/null; then
                if [[ $(field token "$moved/holder") == "$tok" ]]; then
                    rm -rf "$moved"; say "reclaimed the cabin lock ($why): lane ${lane:-?}, since ${started:-?}"
                elif [[ ! -e $LOCK ]]; then mv "$moved" "$LOCK" 2>/dev/null
                fi
            fi
            sleep 1; continue
        fi
        say "waiting for the cabin lock: held by lane ${lane:-?} (pid ${pid:-?} on ${host:-?}) since ${started:-?}${run:+, for cabin run $run}; looking again in ${POLL} s"
        sleep $POLL
    done
}
lock_drop() {
    (( HAVE_LOCK )) || return 0
    if (( LIVE )); then say "a cabin build may still be live, so the cabin lock is KEPT (it is reclaimed at its ceiling)"; return 0; fi
    HAVE_LOCK=0
    [[ $(field token "$LOCK/holder") == "$TOKEN" ]] && rm -rf "$LOCK"
}
trap 'lock_drop' EXIT
trap 'lock_drop; exit 129' HUP
trap 'lock_drop; exit 130' INT
trap 'lock_drop; exit 143' TERM

# ── the cabin, over ssh ───────────────────────────────────────────────────────────────────────────────────────────
images() {
    local out
    out=$("${SSH[@]}" "$HOST" 'cmd /c tasklist /fo csv /nh' 2>/dev/null) || return 1
    out=$(print -r -- "$out" | tr -d '\r' | sed -n 's/^"\([^"]*\)".*/\1/p')
    [[ -n $out ]] || return 1
    print -r -- "$out"
}
cabin_cmds() {
    local ps b64 out
    ps="\$n = @(Get-CimInstance Win32_Process -Filter \"Name='cmd.exe'\" | Where-Object { \$_.CommandLine -match 'studio-build\\.cmd|\\\\scratch\\\\[^ ]+\\.cmd' }).Count; 'CMDCOUNT=' + \$n"
    b64=$(print -rn -- "$ps" | iconv -f UTF-8 -t UTF-16LE | base64 | tr -d '\n')
    out=$("${SSH[@]}" "$HOST" "powershell -NoProfile -NonInteractive -EncodedCommand $b64" 2>/dev/null | tr -d '\r' | sed -n 's/^CMDCOUNT=//p' | head -1)
    [[ $out == <-> ]] || return 1
    print -r -- $out
}
# preflight: 0 idle, 20 busy, 22 ssh failed, 24 a foreign build outlasted the wait.
preflight() {
    local imgs busy builds ncmd t0=$(now)
    while true; do
        imgs=$(images) || { say "ssh to the cabin failed, or its tasklist listed nothing"; return 22; }
        busy=$(print -r -- "$imgs" | grep -i -E '^pinpointstudio\.exe$|foresight|fsx|gspro' | sort -u | paste -sd ' ' -)
        [[ -n $busy ]] && { say "BUSY: $busy is running on the cabin"; return 20; }
        builds=$(print -r -- "$imgs" | grep -i -x -E 'jom\.exe|ctest\.exe' | sort | uniq -c | awk '{print $2 " x" $1}' | paste -sd ' ' -)
        ncmd=$(cabin_cmds) || { say "the cabin's cmd.exe query failed or gave no count"; return 22; }
        (( ncmd > 0 )) && builds="${builds:+$builds }studio-build.cmd/scratch .cmd x$ncmd"
        [[ -z $builds ]] && return 0
        (( $(now) - t0 >= PRE_MAX )) && { say "still running on the cabin after 30 min: $builds"; return 24; }
        say "waiting: the cabin is already building or testing ($builds); looking again in ${POLL} s"
        sleep $POLL
    done
}
launch() {
    local wrun=$1 ps b64 out
    ps="\$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = 'cmd.exe /c $wrun\\run.cmd'; CurrentDirectory = '$wrun' }; 'CIM ReturnValue=' + \$r.ReturnValue + ' ProcessId=' + \$r.ProcessId"
    b64=$(print -rn -- "$ps" | iconv -f UTF-8 -t UTF-16LE | base64 | tr -d '\n')
    out=$("${SSH[@]}" "$HOST" "powershell -NoProfile -NonInteractive -EncodedCommand $b64" 2>/dev/null | tr -d '\r')
    [[ $out == *"CIM ReturnValue=0 "* ]] && { say "launched detached on the cabin: cmd.exe /c $wrun\\run.cmd"; return 0; }
    say "the detached launch failed: $out"; return 1
}
# jom_secs <console>: "<seconds> <build rc>" from studio-build.cmd's jom and build exit stamps; empty when absent.
jom_secs() {
    LC_ALL=C awk '
    function secs(t,  p) { gsub(/^ +/, "", t); split(t, p, /[:.]/); return p[1] * 3600 + p[2] * 60 + p[3] + p[4] / 100 }
    { sub(/\r$/, "") }
    /^tests: .*; jom -j[0-9]+ / { t0 = secs($NF); h0 = 1 }
    /^tests: build exit [0-9]+ / { t1 = secs($NF); h1 = 1; brc = $4 }
    END { if (h0 && h1) { d = t1 - t0; if (d < 0) d += 86400; printf "%.0f %s\n", d, brc } }' "$1"
}

# ── the steps ─────────────────────────────────────────────────────────────────────────────────────────────────────
lock_take
STAMP=$(date '+%Y%m%d-%H%M%S')
RUNROOT=$SHARE/scratch/build-times-$STAMP
mkdir -p "$RUNROOT" || die 21 "cannot make $RUNROOT"
say "run dir $RUNROOT"
ROWS=() HEAD0= FINAL=0 i=0 firstsecs=
for s in $STEPS; do
    (( i++ ))
    kind=${s%%|*} rest=${s#*|} src=${rest%%|*} label=${rest#*|}
    preflight; rc=$?
    (( rc )) && { FINAL=$rc; say "step $i ($label) not started"; break; }
    d=$RUNROOT/$i; mkdir -p "$d"
    wrun="$WSHARE\\scratch\\build-times-$STAMP\\$i"
    write_wrapper "$d/run.cmd" "$wrun" "$kind" "$src" $(( i == 1 ))
    say "step $i: $label"
    LIVE=1
    launch "$wrun" || { FINAL=22; break; }
    t0=$(now) seen=0
    while true; do
        if [[ -f $d/console.log ]]; then
            n=$(wc -l < "$d/console.log" | tr -d ' ')
            if (( n > seen )); then sed -n "$(( seen + 1 )),${n}p" "$d/console.log" | tr -d '\r' | grep -E '^(build_times|tests): ' | sed 's/^/  cabin| /'; seen=$n; fi
        fi
        [[ -s $d/rc.txt ]] && break
        if [[ ! -f $d/console.log ]] && (( $(now) - t0 > LAUNCH_GRACE )); then say "the wrapper did not start"; LIVE=0; FINAL=22; break 2; fi
        (( $(now) - t0 > RUN_MAX )) && { say "step $i has not ended after 2 h; left running, the lock kept"; FINAL=23; break 2; }
        sleep $POLL
    done
    LIVE=0
    src_logs=$SHARE/scratch/studio-build
    cp "$src_logs/tests_build.log" "$d/" 2>/dev/null
    cp "$src_logs/tests_summary.log" "$d/" 2>/dev/null
    con=$d/console.log
    head=$(tr -d '\r' < "$con" | sed -n 's/^build_times: HEAD //p' | head -1)
    [[ -z $HEAD0 ]] && HEAD0=$head
    if [[ $head != "$HEAD0" ]]; then say "the cabin's HEAD moved from $HEAD0 to $head"; FINAL=25; break; fi
    js=($(jom_secs "$con"))
    comp=$(grep -c -E 'Building (C|CXX) object' "$d/tests_build.log" 2>/dev/null)
    links=$(grep -c -E 'Linking (C|CXX) ' "$d/tests_build.log" 2>/dev/null)
    cln=$(tr -d '\r' < "$con" | awk '/^build_times: clean start/{a=$NF} /^build_times: clean exit/{b=$NF; c=$4} END{if(a&&b){split(a,p,/[:.]/);split(b,q,/[:.]/);d=(q[1]*3600+q[2]*60+q[3])-(p[1]*3600+p[2]*60+p[3]); if(d<0)d+=86400; print d " " c}}')
    ctl=$(tr -d '\r' < "$con" | grep -E '^tests: ctest pass [12] exit' | awk '{print "pass " $4 " exit " $6}' | paste -sd ',' -)
    rcv=$(tr -d '\r\n ' < "$d/rc.txt")
    if [[ -z ${js[1]} || ${js[2]} != 0 ]]; then
        say "step $i: no successful build (wrapper exit $rcv); logs kept in $d"; FINAL=11
        ROWS+=("$label|-|${comp:--}|${links:--}|build failed, exit $rcv"); break
    fi
    note="studio-build exit $rcv${ctl:+ ($ctl)}"
    [[ -n $cln ]] && note="jom clean ${cln%% *} s exit ${cln##* }; $note"
    if [[ $label == *"(repeat)" && -n $firstsecs ]]; then
        note="drift $(awk -v a=$firstsecs -v b=${js[1]} 'BEGIN{printf "%+.1f%%", (b-a)*100/a}') from the first; $note"
    fi
    [[ $kind == touch && $src == ${HEADERS[-1]} && $label != *"(repeat)" ]] && firstsecs=${js[1]}
    ROWS+=("$label|${js[1]}|$comp|$links|$note")
    say "step $i: ${js[1]} s, $comp compiles, $links links"
done

# ── the table ─────────────────────────────────────────────────────────────────────────────────────────────────────
SHA=${HEAD0:-unknown}
OUTD=$WT/build/build-times; mkdir -p "$OUTD"; OUT=$OUTD/$SHA.txt
{
    print -r -- "build_times.sh: cabin GOLFSIMPC, build\\tests-vs18 (Debug, NMake JOM, tests/ umbrella), jom -j$JOBS, no compiler cache"
    print -r -- "commit $SHA (cabin HEAD; origin/main on the Mac at the start: ${MACSHA:-?}); run $(date '+%Y-%m-%d %H:%M'); run dir $RUNROOT; exit $FINAL"
    printf '%-52s %8s %9s %6s %8s  %s\n' step seconds compiles links "peak MB" notes
    for r in $ROWS; do
        local -a c; c=("${(@s:|:)r}")
        printf '%-52s %8s %9s %6s %8s  %s\n' "${c[1]}" "${c[2]}" "${c[3]}" "${c[4]}" - "${c[5]}"
    done
} | tee "$OUT"
say "table written to $OUT"
if (( FINAL == 0 )); then rm -rf "$RUNROOT"; say "run dir deleted"; else say "run dir kept: $RUNROOT"; fi
exit $FINAL
