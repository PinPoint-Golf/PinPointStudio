#!/bin/zsh
# tone_ab.sh - #183's analysis A/B: the Mac side (plan section 6 step 5). The studio PC's build lock, the idle
# check, the detached launch of tone_ab_studio.cmd and the polling, each with its own ceiling.
#
#   tools/swinglab/tone_ab.sh lock [max_wait_s]     wait for and take the studio PC's lock (lane i183-ab); default 8 h
#   tools/swinglab/tone_ab.sh unlock                drop it, only while the token is ours
#   tools/swinglab/tone_ab.sh preflight             0 idle; 20 BUSY (PinPoint Studio, Foresight, FSX, GSPro);
#                                                   24 a jom/ctest/studio-build or scratch .cmd runs; 22 ssh failed
#   tools/swinglab/tone_ab.sh tools                 copy the runner (tone_ab_run.py, tone_ab_studio.cmd) to _tools/
#   tools/swinglab/tone_ab.sh launch <phase>        launch tone_ab_studio.cmd <phase> detached on the studio PC
#   tools/swinglab/tone_ab.sh wait <phase> [max_s]  poll <phase>.rc.txt every 60 s, echoing new console lines
#
# Nothing private is a default: the machine, the share and the studio PC's paths come from the environment, and
# a command that needs one that is unset stops with exit 2.
#   TONE_AB_HOST    the studio PC's ssh target (user@host)
#   TONE_AB_SHARE   the share's mount point on the Mac; the run's root is $TONE_AB_SHARE/scratch/i183-ab
#   TONE_AB_WSHARE  the same share's root as the studio PC sees it
#   TONE_AB_WREPO   the studio PC's checkout;  TONE_AB_WPY  its swinglab venv's python.exe
#   TONE_AB_WQT     Qt's bin folder on the studio PC;  TONE_AB_ONE_ID  the one-swing step's swing (launch one)
#
# The lock is cabin_run.sh's (tools/method/cabin_run.sh, step 2): a folder made with mkdir on the share,
# <share>/scratch/cabin.lock, whose "holder" names lane, pid, host, start (epoch s), started, ceiling and
# token; the claimer re-reads the holder and holds the lock only if the token is its own. This holder writes
# a 12 h ceiling (the A/B is about 4 to 5 h, plus the build and the one-swing step) and no run= line, so a waiter
# judges it only by its ceiling. A waiter here reclaims a lock older than its own ceiling, or one kept for a
# cabin_run.sh run that is over (its rc.txt is there and older than 10 min, or its holder pid is dead on this
# host), exactly as cabin_run.sh does; otherwise it waits, polling every 60 s. Plan debt 3: this re-implements
# that protocol because cabin_run.sh is not a library.
setopt pipefail
need() { local v; for v in "$@"; do [[ -n ${(P)v} ]] || { print -u2 "tone_ab: set $v (see the header)"; exit 2; }; done; }
HOST=${TONE_AB_HOST:-}
SHARE=${TONE_AB_SHARE:-}
LOCK=$SHARE/scratch/cabin.lock
ROOT=$SHARE/scratch/i183-ab
WSHARE=${TONE_AB_WSHARE:-}
WROOT="$WSHARE\\scratch\\i183-ab"
HERE=${0:A:h}
STATE=$HERE/../../build/platform/A/i183ab
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=20 -o ServerAliveInterval=15 -o ServerAliveCountMax=4)
LANE=i183-ab CEIL=43200 GRACE=600
say() { print -r -- "tone_ab: $(date '+%H:%M:%S') $*"; }
now() { date +%s; }
field() { sed -n "s/^$1=//p" "$2" 2>/dev/null | head -1 | tr -d '\r'; }
mkdir -p $STATE

lock_take() {
    local max=${1:-28800} t0=$(now) tok lane pid host start ceil run age d why moved
    TOKEN="$$-$RANDOM-$(now)"
    while true; do
        if mkdir "$LOCK" 2>/dev/null; then
            print -r -- "lane=$LANE
pid=$$
host=$(hostname -s)
start=$(now)
started=$(date '+%Y-%m-%d %H:%M:%S')
ceiling=$CEIL
token=$TOKEN" > "$LOCK/holder"
            if [[ $(field token "$LOCK/holder") == "$TOKEN" ]]; then
                print -r -- "$TOKEN" > $STATE/lock.token
                say "took the studio PC's lock (lane $LANE, ceiling ${CEIL} s)"; return 0
            fi
            continue
        fi
        lane=$(field lane "$LOCK/holder"); pid=$(field pid "$LOCK/holder"); host=$(field host "$LOCK/holder")
        start=$(field start "$LOCK/holder"); ceil=$(field ceiling "$LOCK/holder"); tok=$(field token "$LOCK/holder")
        run=$(field run "$LOCK/holder")
        [[ $start == <-> ]] || start=$(stat -f %m "$LOCK" 2>/dev/null || now)
        [[ $ceil == <-> ]] || ceil=9600
        age=$(( $(now) - start )) why=
        if (( age > ceil )); then why="stale: ${age} s old, over its ${ceil} s ceiling"
        elif [[ -n $tok && -n $run ]]; then
            d=$SHARE/scratch/$run
            if [[ -s $d/rc.txt ]]; then
                if [[ $host == "$(hostname -s)" && $pid == <-> ]] && ! kill -0 $pid 2>/dev/null; then why="its run $run is over and its holder is gone"
                elif (( $(now) - $(stat -f %m "$d/rc.txt") > GRACE )); then why="its run $run is over (rc.txt older than ${GRACE} s)"; fi
            fi
        fi
        if [[ -n $why ]]; then
            moved="$LOCK.stale.$TOKEN"
            if mv "$LOCK" "$moved" 2>/dev/null; then
                if [[ -n $tok && $(field token "$moved/holder") == "$tok" ]]; then rm -rf "$moved"; say "reclaimed the studio PC's lock ($why): lane $lane"
                elif [[ ! -e $LOCK ]] && mv "$moved" "$LOCK" 2>/dev/null; then say "a fresh lock was moved by a race; put back"
                else say "WARNING: a moved lock could not be put back; it is at $moved"; fi
            fi
            sleep 1; continue
        fi
        if (( $(now) - t0 > max )); then say "gave up waiting after ${max} s: held by lane ${lane:-?}"; return 1; fi
        say "waiting for the studio PC's lock: held by lane ${lane:-?} (pid ${pid:-?} on ${host:-?})${run:+, run $run}; again in 60 s"
        sleep 60
    done
}

lock_drop() {
    local tok=$(cat $STATE/lock.token 2>/dev/null)
    if [[ -n $tok && $(field token "$LOCK/holder") == "$tok" ]]; then rm -rf "$LOCK"; rm -f $STATE/lock.token; say "dropped the studio PC's lock"
    else say "the studio PC's lock is not ours (holder token $(field token "$LOCK/holder")); left alone"; return 1; fi
}

preflight() {
    local out imgs hit builds ps b64 n
    out=$("${SSH[@]}" $HOST 'cmd /c tasklist /fo csv /nh' 2>/dev/null) || { say "ssh failed"; return 22; }
    imgs=$(print -r -- "$out" | tr -d '\r' | sed -n 's/^"\([^"]*\)".*/\1/p')
    [[ -n $imgs ]] || { say "tasklist listed nothing"; return 22; }
    hit=$(print -r -- "$imgs" | grep -i -E '^pinpointstudio\.exe$|foresight|fsx|gspro' | sort -u | paste -sd ' ' -)
    [[ -n $hit ]] && { say "BUSY: $hit"; return 20; }
    builds=$(print -r -- "$imgs" | grep -i -x -E 'jom\.exe|ctest\.exe|swinglab_run\.exe' | sort | uniq -c | awk '{print $2 " x" $1}' | paste -sd ' ' -)
    ps="\$n = @(Get-CimInstance Win32_Process -Filter \"Name='cmd.exe'\" | Where-Object { \$_.CommandLine -match 'studio-build\\.cmd|\\\\scratch\\\\[^ ]+\\.cmd' }).Count; 'CMDCOUNT=' + \$n"
    b64=$(print -rn -- "$ps" | iconv -f UTF-8 -t UTF-16LE | base64 | tr -d '\n')
    n=$("${SSH[@]}" $HOST "powershell -NoProfile -NonInteractive -EncodedCommand $b64" 2>/dev/null | tr -d '\r' | sed -n 's/^CMDCOUNT=//p' | head -1)
    [[ $n == <-> ]] || { say "cmd.exe query failed"; return 22; }
    (( n > 0 )) && builds="${builds:+$builds }cmd-wrappers x$n"
    [[ -n $builds ]] && { say "a build, test or run is live on the studio PC: $builds"; return 24; }
    say "the studio PC is idle"; return 0
}

tools_copy() {
    mkdir -p $ROOT/_tools
    cp $HERE/tone_ab_run.py $HERE/tone_ab_studio.cmd $ROOT/_tools/ && say "copied the runner to $ROOT/_tools"
}

launch() {
    local phase=$1 ps b64 out one=
    need TONE_AB_WREPO TONE_AB_WPY TONE_AB_WQT
    [[ $phase == one ]] && { need TONE_AB_ONE_ID; one=" $TONE_AB_ONE_ID"; }
    ps="\$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = 'cmd.exe /c $WROOT\\_tools\\tone_ab_studio.cmd $phase $WSHARE $TONE_AB_WREPO $TONE_AB_WPY $TONE_AB_WQT$one'; CurrentDirectory = '$WROOT' }; 'CIM ReturnValue=' + \$r.ReturnValue + ' ProcessId=' + \$r.ProcessId"
    b64=$(print -rn -- "$ps" | iconv -f UTF-8 -t UTF-16LE | base64 | tr -d '\n')
    out=$("${SSH[@]}" $HOST "powershell -NoProfile -NonInteractive -EncodedCommand $b64" 2>&1 | tr -d '\r' | grep 'CIM ReturnValue')
    [[ $out == *"CIM ReturnValue=0 "* ]] || { say "launch failed: $out"; return 22; }
    say "launched $phase detached: $out"
}

wait_phase() {
    local phase=$1 max=${2:-21600} t0=$(now) seen=0 n
    local con=$ROOT/$phase.console.log rc=$ROOT/$phase.rc.txt
    while true; do
        if [[ -f $con ]]; then
            n=$(wc -l < $con | tr -d ' ')
            (( n > seen )) && { sed -n "$((seen + 1)),${n}p" $con | tr -d '\r'; seen=$n; }
        fi
        if [[ -s $rc ]]; then say "$phase ended: exit $(tr -d '\r\n ' < $rc)"; return $(tr -d '\r\n ' < $rc); fi
        if (( $(now) - t0 > max )); then say "$phase did not end within ${max} s; left running"; return 23; fi
        sleep 60
    done
}

case $1 in
    lock|unlock|tools|wait) need TONE_AB_SHARE ;|
    preflight) need TONE_AB_HOST ;|
    launch) need TONE_AB_HOST TONE_AB_SHARE TONE_AB_WSHARE ;|
    lock) lock_take ${2:-28800} ;;
    unlock) lock_drop ;;
    preflight) preflight ;;
    tools) tools_copy ;;
    launch) launch $2 ;;
    wait) wait_phase $2 ${3:-21600} ;;
    *) print -u2 "usage: tone_ab.sh lock [max_s] | unlock | preflight | tools | launch <phase> | wait <phase> [max_s]"; exit 2 ;;
esac
