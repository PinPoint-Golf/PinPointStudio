#!/bin/zsh
# cabin_run.sh — runs an item's full test suite on the studio PC (the cabin, GOLFSIMPC) from the Mac, one cabin job at a
# time, and prints a short summary. The Mac builds the item and runs its own suites and the lints; the full suite, both
# passes, runs here on the cabin's 24 cores (tools/studio/studio-build.cmd's "tests" mode, jom -j16).
#
#   tools/method/cabin_run.sh <worktree> [mode M] [jobs N] [--wait]
#                                     M = tests (default: build\tests-vs18 and both ctest passes), build (that tree's
#                                     build only, no ctest) or release (the Release link check: build\Release-Installer's
#                                     PinPointStudio and swinglab_run, nothing deployed or installed); studio-build.cmd's
#                                     modes of those names. N = the cabin's jom job count, 1 to 24, default 16
#   tools/method/cabin_run.sh --self-test                     proves the runner against a fake cabin in a temporary
#                                                             directory; never the real share, ssh or cabin
#
# e.g. tools/method/cabin_run.sh build/wt-5b
#
# What it does, in order:
#  1. Base: fetches origin main and refuses (exit 21) unless the worktree's HEAD is on origin/main, since the cabin
#     pulls main and the patch is taken against HEAD (a local commit would be left out). The patch is made by the
#     WORKTREE's own tools/studio/make-wip-patch.sh (it finds its repo from its own path). A worktree that matches
#     HEAD has no patch, and main itself is tested.
#  2. Lock: one cabin job at a time across every lane. The lock is a folder made with mkdir (atomic) on the share,
#     /mnt/swingdata/scratch/cabin.lock, whose file "holder" names the holder's lane, pid, host, start time (the Mac's
#     clock, epoch seconds), its ceiling, a token and, once launched, its run. The claimer re-reads the holder after
#     writing it and holds the lock only if the token is its own. A waiter polls every 60 s and says so. A lock older
#     than its ceiling (2 h 40 min: the run's 2 h, a 30 min wait for a foreign build, and 10 min) is stale, and a lock
#     kept for a run that is over (its rc.txt is there, or its wrapper never started) is reclaimable: it is renamed
#     away and deleted only if what was moved is the very lock judged (the same token, or for a holder-less one the
#     same mtime and inode); a fresh lock moved by a race with another waiter is put back. A line says which. The
#     holder removes the lock on exit, interrupt, hang-up or termination, only while the token is its own, and never
#     while a run it launched may still be live (after the launch, until that run's rc.txt has been read: exit 22
#     after the launch, 23, or an interrupt keep the lock for that run).
#  3. Pre-flight, over ssh (tasklist, and one encoded PowerShell query of cmd.exe command lines): BUSY when
#     PinPointStudio.exe, or any image whose name holds Foresight, FSX or GSPro, runs (the script's own idle guard,
#     checked here too so a busy studio is not even pulled). While a jom.exe, a ctest.exe, or a cmd.exe running
#     studio-build.cmd or a .cmd from the scratch share (any lane's wrapper) runs there, it waits, polling every 60 s,
#     for up to 30 min (then exit 24), and looks once more a poll after they have gone. A query that fails or gives no
#     count is exit 22 (fail closed). It never kills anything.
#  4. Run: a fresh run dir /mnt/swingdata/scratch/cabin-<lane>-<stamp>/ (on the cabin C:\PinPointStudio\scratch\...)
#     holds wip.patch and run.cmd, a CRLF ASCII wrapper that pulls the checkout fast-forward FIRST (when clean), then
#     calls "studio-build.cmd <mode> nopull patch <wip.patch> jobs N". Pulling first means studio-build.cmd never
#     pulls a new copy of itself underneath its own run (cmd reads a running batch file from disk). The wrapper's
#     console goes to console.log and its exit code to rc.txt. It is launched detached, through Win32_Process Create
#     (powershell -EncodedCommand), so a dropped ssh does not kill it. The Mac polls console.log and rc.txt through
#     the share every 60 s, echoing the script's new lines, for up to 2 h (then exit 23; the cabin run is left alone).
#  5. Report: the exit code and its meaning; the cabin's HEAD and the patch's sha; the build time (from the script's
#     "jom" and "build exit" lines); pass 1's and pass 2's counts; each failing test with its first failure line
#     ([FAIL], FAIL!, FAIL, ASSERT; or, for a crash or a timeout, its last output); the real-store lines; whether the
#     checkout was left clean. A pass is read only when this run's console shows that pass's "exit" line, since the
#     studio-build log folder keeps the last run's logs. The logs this run wrote are copied into <run dir>/logs.
#  6. Busy studio: exit 20, BUSY, never a tight retry. --wait retries every 10 min, the lock released while it waits,
#     for up to 4 h. An exit 10 for an unset APPDATA or TEMP is not BUSY and is never retried.
#  7. Clean up: on exit 0 the run dir is deleted; on any other exit it is kept for the item's NOTES. The summary is
#     always written to <worktree>/build/cabin-runs/<run>.txt. The cabin checkout is restored by the script.
#
# Exit: the cabin script's own codes, passed through: 0 all passed (or, for build and release, built), 3 checkout
# dirty, 4 pull failed, 5 the release mode's cmake or build failed, 8 patch or arguments refused, 9 restore incomplete, 10 refused (APPDATA/TEMP unset), 11 test build failed, 12 a test failed,
# 13 a real store changed, 14 a test hit its timeout, 15 a changed settings file could not be kept. The runner's own:
# 2 usage, 20 BUSY, 21 refused on the Mac (base not on origin/main, local changes to tools/studio/studio-build.cmd
# (the cabin always runs main's copy, so they could not be tested), no share), 22 ssh or launch failed,
# 23 the cabin run did not end within 2 h, 24 a foreign build still ran on the cabin after 30 min; 129/130/143 a
# hang-up, interrupt or termination of the Mac side.
#
# Test hooks, for the self-test only, given first: --_test_share <dir> (the share's Mac path), --_test_ssh <cmd> (a
# fake ssh), --_test_poll <s>, --_test_wait <s>, --_test_run_max <s>, --_test_pre_max <s>, --_test_grace <s> (the
# launch grace, 600 s), --_test_before_mv <cmd> (run just before a reclaim's rename). They are flags, not
# environment variables, so nothing a shell inherits can redirect a real run; --_test_share refuses /mnt/swingdata.
# ctest: cabin_run_selftest (src/Gui/tests/CMakeLists.txt). Its samples are in tools/method/cabin_run_samples/, real
# cabin output of 24 Nov: console_fail.txt is B0.3's own run (whole), tests_qml_ui.txt its pass 2 and tests_ctest.txt
# another lane's pass 1 (both trimmed). console_pass.txt and console_busy.txt are short, in the script's own formats.

SELF=${0:A}
HERE=${SELF:h}
setopt pipefail

HOST=developer@GOLFSIMPC.local
REAL_SHARE=/mnt/swingdata
WSHARE='C:\PinPointStudio'
WREPO='C:\Users\developer\Projects\PinPointStudio'
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=20 -o ServerAliveInterval=15 -o ServerAliveCountMax=4)
SHARE=$REAL_SHARE
POLL=60 WAIT_INT=600 WAIT_MAX=14400 PRE_MAX=1800 RUN_MAX=7200 LAUNCH_GRACE=600 TESTING=0 TEST_BEFORE_MV= MODE=tests

say()  { print -r -- "cabin_run: $*"; }
die()  { local rc=$1; shift; print -u2 -r -- "cabin_run: $*"; exit $rc; }
usage() { print -u2 "usage: cabin_run.sh <worktree> [mode tests|build|release] [jobs N] [--wait] | --self-test"; }
now()  { date +%s; }
fmt_dur() { local s=${1%.*}; (( s < 0 )) && s=0
    if (( s >= 3600 )); then printf '%d h %02d min' $(( s / 3600 )) $(( s % 3600 / 60 ))
    elif (( s >= 60 )); then printf '%d min %d s' $(( s / 60 )) $(( s % 60 ))
    else printf '%d s' $s; fi; }

code_text() {
    case $1 in
        0) case $MODE in build) print "the test tree built (no ctest pass run)" ;; release) print "the Release app and swinglab_run linked (nothing installed)" ;; *) print "all tests passed" ;; esac ;;
        3) print "the cabin checkout was not clean; nothing was run" ;;
        4) print "the cabin's git pull failed; nothing was run" ;;
        5) print "the cabin's Release build (cmake or jom) failed" ;;
        8) print "the cabin refused the patch or the arguments" ;;
        9) print "the cabin checkout was NOT restored clean" ;;
        10) print "refused by the cabin script (see its REFUSED line)" ;;
        11) print "the cabin's test build (cmake or jom) failed" ;;
        12) print "a test failed" ;;
        13) print "a REAL STORE changed on the cabin" ;;
        14) print "a test hit its timeout (it may hang)" ;;
        15) print "a changed settings file could not be kept; nothing was put back" ;;
        20) print "BUSY: the studio is in use (PinPoint Studio, Foresight, FSX or GSPro)" ;;
        21) print "refused on the Mac" ;;
        22) print "ssh or the detached launch failed" ;;
        23) print "the cabin run did not end within its ceiling; it was left running" ;;
        24) print "a foreign jom or ctest still ran on the cabin" ;;
        *) print "unknown exit code" ;;
    esac
}

# ── the summary parse ─────────────────────────────────────────────────────────────────────────────────────────────
# pass_report <ctest log> <label>: the counts line, then one line per failing test with its first failure line.
pass_report() {
    LC_ALL=C awk -v label="$2" '
    { sub(/\r$/, ""); L[NR] = $0 }
    function isbound(s) { return s ~ /^ *[0-9]+\/[0-9]+ Test +#[0-9]+: / || s ~ /^ +Start +[0-9]+: / || s ~ /^[0-9]+% tests passed/ }
    function cut(s) { gsub(/\t/, " ", s); sub(/^ +/, "", s); return length(s) > 170 ? substr(s, 1, 167) "..." : s }
    END {
        nf = 0; nr = 0; summ = ""
        for (i = 1; i <= NR; i++) {
            s = L[i]
            if (match(s, /^ *[0-9]+\/[0-9]+ Test +#[0-9]+: /)) {
                rest = substr(s, RLENGTH + 1); split(rest, a, " "); at[a[1]] = i
                st = rest; sub(/^[^ ]+ */, "", st); sub(/^\.+ */, "", st); sub(/^\*+/, "", st)
                sub(/ +[0-9.]+ sec$/, "", st); status[a[1]] = st
            }
            if (s ~ /^[0-9]+% tests passed, [0-9]+ tests? failed out of [0-9]+/) summ = s
            if (s ~ /^The following tests did not run:/) { mode = "nr"; continue }
            if (s ~ /^The following tests FAILED:/) { mode = "f"; continue }
            if (mode == "nr") { if (s ~ /^\t/) { nr++; continue } else mode = "" }
            if (mode == "f") {
                if (s ~ /^\t/) { t = s; sub(/^\t *[0-9]+ - /, "", t); sub(/ \(.*$/, "", t); fl[++nf] = t; continue }
                if (s ~ /^\)$/) continue
                mode = ""
            }
        }
        if (summ == "") { print label ": no ctest summary line (the pass did not finish)"; }
        else {
            nw = split(summ, w, " "); failed = w[4]; total = w[nw]
            line = label ": " (total - failed) " passed, " failed " failed of " total
            if (nr) line = line "; " nr " not run (skipped or disabled)"
            print line
        }
        for (k = 1; k <= nf; k++) {
            n = fl[k]; i = at[n]; first = ""; last = ""; weak = ""
            if (i) {
                for (j = i + 1; j <= NR && !isbound(L[j]); j++) {
                    if (L[j] ~ /^\*\*\*Exception:/ && j == i + 1) continue
                    if (L[j] ~ /[^ \t]/) last = L[j]
                    if (first == "" && (L[j] ~ /\[FAIL\]/ || L[j] ~ /^FAIL!/ || L[j] ~ /^ASSERT/ || L[j] ~ /Assertion failed/)) first = L[j]
                    # A bare "FAIL " line is only a fallback: some suites name a section "FAIL (pool) - ..." whose
                    # checks all pass, so the clear markers above win when the test has one.
                    if (weak == "" && L[j] ~ /^FAIL[ :]/) weak = L[j]
                }
                if (first == "") first = weak
            }
            st = (n in status) ? status[n] : "failed"
            if (first != "") msg = cut(first); else if (last != "") msg = "last output: " cut(last); else msg = "no output"
            print "  FAIL " label " " n " (" st "): " msg
        }
    }' "$1"
}

# build_time <console>: the jom start and the build exit, both the script's %TIME% stamps.
build_time() {
    LC_ALL=C awk '
    function secs(t,  p) { gsub(/^ +/, "", t); split(t, p, /[:.]/); return p[1] * 3600 + p[2] * 60 + p[3] }
    { sub(/\r$/, "") }
    /^(tests|release): .*; jom -j[0-9]+ / { jobs = $0; sub(/^.*jom -j/, "", jobs); sub(/ .*$/, "", jobs); t0 = secs($NF); h0 = $NF }
    /^(tests|release): build exit [0-9]+ / { t1 = secs($NF); h1 = $NF; brc = $4 }
    END {
        if (h0 == "") { print "build: not started"; exit }
        if (h1 == "") { print "build: jom -j" jobs " started " h0 ", no end line"; exit }
        d = t1 - t0; if (d < 0) d += 86400
        printf "build: jom -j%s, exit %s, %d min %d s (%s to %s)\n", jobs, brc, int(d / 60), d % 60, h0, h1
    }' "$1"
}

# ── the lock ──────────────────────────────────────────────────────────────────────────────────────────────────────
HAVE_LOCK=0 LIVE=0
field() { sed -n "s/^$1=//p" "$2" 2>/dev/null | head -1 | tr -d '\r'; }
lockid() { stat -f '%m %i' "$1" 2>/dev/null; }
holder_text() {
    print -r -- "lane=$LANE
pid=$$
host=$(hostname -s)
start=$LOCK_START
started=$LOCK_STARTED
ceiling=$LOCK_CEIL
token=$TOKEN${1:+
run=$1}"
}
# lock_claim: after our mkdir, write the holder and re-read it. 0: ours. 1: another runner's, or the lock is gone (go
# round again). A holder-less lock we made is never given up: the write is retried while the lock is there.
lock_claim() {
    local t i
    LOCK_START=$(now) LOCK_STARTED=$(date '+%Y-%m-%d %H:%M:%S')
    for i in {1..30}; do
        holder_text > "$LOCK/holder" 2>/dev/null
        t=$(field token "$LOCK/holder")
        [[ $t == "$TOKEN" ]] && return 0
        [[ -n $t ]] && return 1
        [[ -e $LOCK ]] || return 1
        sleep 1
    done
    return 1
}
# run_over <run> <host> <pid>: true when a kept lock's cabin run is over: its rc.txt is there and the holder has died on
# this host or the rc.txt is older than the launch grace; or its wrapper never started (no console.log a launch grace
# after its run dir was made). The pre-flight that follows still waits for any live wrapper.
run_over() {
    local d=$SHARE/scratch/$1 f gone=0
    [[ $2 == "$(hostname -s)" && $3 == <-> ]] && ! kill -0 $3 2>/dev/null && gone=1
    if [[ -s $d/rc.txt ]]; then
        (( gone || $(now) - $(stat -f %m "$d/rc.txt" 2>/dev/null || now) > ${LAUNCH_GRACE%.*} ))
    elif [[ -d $d && ! -e $d/console.log ]]; then
        (( $(now) - $(stat -f %m "$d" 2>/dev/null || now) > ${LAUNCH_GRACE%.*} ))   # its wrapper never started
    else
        return 1
    fi
}
lock_take() {
    local lane pid host start started ceil age moved tok id run why
    while true; do
        if mkdir "$LOCK" 2>/dev/null; then
            if lock_claim; then HAVE_LOCK=1; say "took the cabin lock ($LOCK)"; return 0; fi
            continue
        fi
        id=$(lockid "$LOCK")
        lane=$(field lane "$LOCK/holder"); pid=$(field pid "$LOCK/holder"); host=$(field host "$LOCK/holder")
        start=$(field start "$LOCK/holder"); started=$(field started "$LOCK/holder"); ceil=$(field ceiling "$LOCK/holder")
        tok=$(field token "$LOCK/holder"); run=$(field run "$LOCK/holder")
        [[ $start == <-> ]] || start=$(stat -f %m "$LOCK" 2>/dev/null || now)
        [[ $ceil == <-> ]] || ceil=$LOCK_CEIL
        age=$(( $(now) - start )) why=
        if (( age > ceil )); then why="STALE: $(fmt_dur $age) old, over its $(fmt_dur $ceil) ceiling"
        elif [[ -n $tok && -n $run ]] && run_over "$run" "$host" "$pid"; then why="kept for its cabin run $run, which is over"
        fi
        if [[ -n $why ]]; then
            [[ -n $TEST_BEFORE_MV ]] && "$TEST_BEFORE_MV"
            moved="$LOCK.stale.$TOKEN"
            if mv "$LOCK" "$moved" 2>/dev/null; then
                # The rename is atomic, but another waiter may have reclaimed the judged lock and made a fresh one in
                # between, so what was moved is deleted only if it is the very lock judged: the same token, or, for a
                # holder-less one, still holder-less with the same mtime and inode.
                if { [[ -n $tok ]] && [[ $(field token "$moved/holder") == "$tok" ]]; } ||
                   { [[ -z $tok ]] && [[ ! -e $moved/holder && $(lockid "$moved") == "$id" ]]; }; then
                    rm -rf "$moved"
                    say "reclaimed the cabin lock ($why): lane ${lane:-?}, pid ${pid:-?} on ${host:-?}, since ${started:-?}"
                elif [[ ! -e $LOCK ]] && mv "$moved" "$LOCK" 2>/dev/null; then
                    say "a fresh cabin lock was moved by a race with another waiter; it is put back"
                else
                    say "WARNING: a live cabin lock (lane $(field lane "$moved/holder")) was moved aside by a race and its name is taken; it is at $moved"
                fi
            fi
            # Every way round waits a second: a refused rename, and a put-back too, since the share may give the put-back
            # dir a new inode, so a holder-less lock could be judged afresh at once and spin.
            sleep 1
            continue
        fi
        say "waiting for the cabin lock: held by lane ${lane:-?} (pid ${pid:-?} on ${host:-?}) since ${started:-?}${run:+, for cabin run $run}; looking again in ${POLL} s"
        sleep $POLL
    done
}
# lock_note <run>: records the launched run in our holder, so a later waiter can tell when that run is over.
lock_note() {
    [[ $(field token "$LOCK/holder") == "$TOKEN" ]] && holder_text "$1" > "$LOCK/holder"
}
# lock_drop: removes the lock while it is ours, unless a cabin run we launched may still be live (LIVE): then the lock
# is kept for it, and a later waiter reclaims it once that run's rc.txt is there (run_over), or at its ceiling.
lock_drop() {
    (( HAVE_LOCK )) || return 0
    HAVE_LOCK=0
    if (( LIVE )); then
        say "the cabin run $RUN may still be live, so the cabin lock is KEPT for it; the next runner takes it once that run's rc.txt is there"
        return 0
    fi
    if [[ $(field token "$LOCK/holder") == "$TOKEN" ]]; then rm -rf "$LOCK"; fi
}

# ── the cabin, over ssh ───────────────────────────────────────────────────────────────────────────────────────────
# images: the image names the cabin's tasklist lists, one per line; fails when ssh fails or lists nothing.
images() {
    local out
    out=$("${SSH[@]}" "$HOST" 'cmd /c tasklist /fo csv /nh' 2>/dev/null) || return 1
    out=$(print -r -- "$out" | tr -d '\r' | sed -n 's/^"\([^"]*\)".*/\1/p')
    [[ -n $out ]] || return 1
    print -r -- "$out"
}
# cabin_cmds: how many cmd.exe on the cabin run studio-build.cmd or a .cmd from the scratch share (a cabin_run.sh
# wrapper, or any lane's hand-made one). A run is live for all of its life in one of these, where jom and ctest are not
# (its pull, patch, cmake, store checks, restore). One encoded ssh call; it fails (closed) on anything but a count.
cabin_cmds() {
    local ps b64 out
    ps="\$n = @(Get-CimInstance Win32_Process -Filter \"Name='cmd.exe'\" | Where-Object { \$_.CommandLine -match 'studio-build\\.cmd|\\\\scratch\\\\[^ ]+\\.cmd' }).Count; 'CMDCOUNT=' + \$n"
    b64=$(print -rn -- "$ps" | iconv -f UTF-8 -t UTF-16LE | base64 | tr -d '\n')
    out=$("${SSH[@]}" "$HOST" "powershell -NoProfile -NonInteractive -EncodedCommand $b64" 2>/dev/null | tr -d '\r' | sed -n 's/^CMDCOUNT=//p' | head -1)
    [[ $out == <-> ]] || return 1
    print -r -- $out
}
# preflight: 0 idle, 20 busy (BUSY_WHAT set), 22 ssh failed, 24 a foreign build outlasted the wait.
preflight() {
    local imgs builds ncmd t0=$(now) seen=0
    while true; do
        imgs=$(images) || { say "ssh to the cabin failed, or its tasklist listed nothing"; return 22; }
        BUSY_WHAT=$(print -r -- "$imgs" | grep -i -E '^pinpointstudio\.exe$|foresight|fsx|gspro' | sort -u | paste -sd ' ' -)
        [[ -n $BUSY_WHAT ]] && return 20
        builds=$(print -r -- "$imgs" | grep -i -x -E 'jom\.exe|ctest\.exe' | sort | uniq -c | awk '{print $2 " x" $1}' | paste -sd ' ' -)
        ncmd=$(cabin_cmds) || { say "the cabin's cmd.exe query failed or gave no count"; return 22; }
        (( ncmd > 0 )) && builds="${builds:+$builds }studio-build.cmd/run.cmd x$ncmd"
        if [[ -z $builds ]]; then
            (( seen )) || return 0
            # A foreign run has just ended: its script still restores the checkout, so look once more a poll later.
            seen=0; say "the cabin's build or test has ended; looking once more in ${POLL} s, while its script cleans up"
            sleep $POLL; continue
        fi
        seen=1
        if (( $(now) - t0 >= PRE_MAX )); then say "still running on the cabin after $(fmt_dur $PRE_MAX): $builds"; return 24; fi
        say "waiting: the cabin is already building or testing ($builds), not ours; looking again in ${POLL} s"
        sleep $POLL
    done
}

# write_wrapper <file> <run> <has-patch>: the CRLF, ASCII run.cmd.
write_wrapper() {
    local f=$1 run=$2 haspatch=$3 wrun="$WSHARE\\scratch\\$2" pargs=""
    (( haspatch )) && pargs=" patch \"%RUN%\\wip.patch\""
    print -r -- "@echo off
rem cabin_run.sh's wrapper for $run, written on the Mac (tools/method/cabin_run.sh). ASCII, CRLF.
rem It pulls first, so studio-build.cmd never pulls a new copy of itself underneath its own run.
setlocal
set \"RUN=$wrun\"
set \"REPO=$WREPO\"
> \"%RUN%\\console.log\" echo cabin_run: wrapper start %DATE% %TIME%
>> \"%RUN%\\console.log\" echo cabin_run: APPDATA=%APPDATA% LOCALAPPDATA=%LOCALAPPDATA% TEMP=%TEMP%
cd /d \"%REPO%\"
set DIRTY=
for /f %%i in ('git status --short ^| find /c /v \"\"') do set DIRTY=%%i
if not \"%DIRTY%\"==\"0\" goto nopull
>> \"%RUN%\\console.log\" echo cabin_run: git pull --ff-only
git pull --ff-only >> \"%RUN%\\console.log\" 2>&1
if errorlevel 1 goto pullfail
goto pulled
:nopull
>> \"%RUN%\\console.log\" echo cabin_run: the checkout is not clean, so it is not pulled; studio-build.cmd refuses it
:pulled
for /f %%i in ('git rev-parse --short HEAD') do >> \"%RUN%\\console.log\" echo cabin_run: HEAD %%i
call \"%REPO%\\tools\\studio\\studio-build.cmd\" $MODE nopull$pargs jobs $JOBS >> \"%RUN%\\console.log\" 2>&1
set RC=%ERRORLEVEL%
goto done
:pullfail
>> \"%RUN%\\console.log\" echo cabin_run: PULL FAILED
set RC=4
:done
>> \"%RUN%\\console.log\" echo cabin_run: wrapper end %DATE% %TIME% - exit %RC%
> \"%RUN%\\rc.tmp\" echo %RC%
move /y \"%RUN%\\rc.tmp\" \"%RUN%\\rc.txt\" >nul
exit /b %RC%" | sed 's/$/\r/' > "$f"
}

launch() {
    local run=$1 wrun="$WSHARE\\scratch\\$1" ps b64 out
    ps="\$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = 'cmd.exe /c $wrun\\run.cmd'; CurrentDirectory = '$wrun' }; 'CIM ReturnValue=' + \$r.ReturnValue + ' ProcessId=' + \$r.ProcessId"
    b64=$(print -rn -- "$ps" | iconv -f UTF-8 -t UTF-16LE | base64 | tr -d '\n')
    out=$("${SSH[@]}" "$HOST" "powershell -NoProfile -NonInteractive -EncodedCommand $b64" 2>"$TMPD/launch.err" | tr -d '\r')
    if [[ $out == *"CIM ReturnValue=0 "* ]]; then
        say "launched detached on the cabin (process ${${out##*ProcessId=}%%[^0-9]*}: cmd.exe /c $wrun\\run.cmd)"; return 0
    fi
    out="$out $(tr -d '\r' < "$TMPD/launch.err" | grep -v -E 'CLIXML|<Objs|</Objs>' | head -5)"
    say "the detached launch failed: $out"
    return 1
}

# ── the summary ───────────────────────────────────────────────────────────────────────────────────────────────────
# clean_console <console> <out>: CRs dropped, and git apply's warnings dropped (the lines between "PATCH: N new
# file(s)" and "PATCH: applied"). Those warnings echo the patch's own lines, so a patch holding console samples (this
# tool's own) would otherwise be read as this run's console.
clean_console() {
    tr -d '\r' < "$1" | awk '/^PATCH: [0-9]+ new file/ { print; skip = 1; next }
        skip && /^PATCH: (applied|git apply FAILED)/ { skip = 0 }
        !skip { print }' > "$2"
}

# report <rc> <console> <logs dir> <wall s>
report() {
    local rc=$1 con=$2 logs=$3 wall=$4 head psha
    print -r -- "== cabin run $RUN (mode $MODE): exit $rc, $(code_text $rc)"
    print -r -- "worktree $WT at $(git -C "$WT" rev-parse --short HEAD); patch ${PATCH_DESC}"
    [[ -f $con ]] || { print -r -- "no console log: the wrapper never started"; return; }
    clean_console "$con" "$con.clean"; con=$con.clean
    head=$(sed -n 's/^cabin_run: HEAD //p' "$con" | head -1)
    psha=$(tr -d '\r' < "$con" | sed -n 's/^PATCHED BUILD .* sha256 \([0-9a-fA-F]*\) over .*/\1/p' | tail -1)
    print -r -- "cabin HEAD ${head:-?}${psha:+, patch sha256 ${psha[1,16]}...}; wall time $(fmt_dur $wall)"
    (( BEHIND > 0 )) && print -r -- "  NOTE: the worktree's HEAD was $BEHIND commit(s) behind origin/main; the cabin tested the patch on its tip"
    tr -d '\r' < "$con" | grep -E '^tests: (REFUSED|BUSY|TIMEOUT|REAL STORE CHANGED|settings|the changed settings are kept at|there was no settings file before|the qml_ui label NOT run)|^(PATCH:|TREE NOT CLEAN|PULL FAILED|RESTORE INCOMPLETE|USAGE|JOBS:|cabin_run: (PULL FAILED|the checkout is not clean))' \
        | grep -v '^PATCH: \(.*sha256\|applied\|restoring\|[0-9]* new file\)' | sed 's/^/  /'
    build_time "$con"
    if (( rc == 11 || rc == 5 )); then
        tr -d '\r' < "$con" | grep -E ': (fatal )?error|cmake FAILED' | head -10 | sed 's/^/  /'
    fi
    if [[ $MODE != tests ]]; then print -r -- "ctest: not run (mode $MODE)"
    else
        if grep -q '^tests: ctest pass 1 exit' "$con"; then pass_report "$logs/tests_ctest.log" "pass 1"
        else print -r -- "pass 1: not run"; fi
        if grep -q '^tests: ctest pass 2 exit' "$con"; then pass_report "$logs/tests_qml_ui.log" "pass 2"
        else print -r -- "pass 2: not run"; fi
    fi
    if grep -q '^tests: stores after' "$con"; then
        if grep -q 'REAL STORE CHANGED' "$con"; then print -r -- "stores: CHANGED (lines above)"
        else print -r -- "stores: unchanged (settings, Recycle Bin, Credential Manager)"; fi
    fi
    if grep -q '^== status after' "$con"; then
        local left=$(tr -d '\r' < "$con" | awk '/^== status after/{f=1;next} f&&/^(PATCHED BUILD|== end)/{exit} f&&NF{print}')
        if [[ -z $left ]]; then print -r -- "cabin checkout: restored clean"
        else print -r -- "cabin checkout: NOT clean:"; print -r -- "$left" | sed 's/^/  /'; fi
    elif [[ $PATCH_DESC == none* ]]; then print -r -- "cabin checkout: no patch applied"
    fi
    rm -f "$con"
}

# copy_logs <console> <dest>: the studio-build logs this run wrote (by the stages its console shows), never older ones.
copy_logs() {
    local dst=$2 src=$SHARE/scratch/studio-build f con=$2.console
    mkdir -p "$dst"
    [[ -f $1 ]] && clean_console "$1" "$con"
    grep -q '^tests: start' "$con" 2>/dev/null && cp "$src/tests_summary.log" "$dst/" 2>/dev/null
    grep -q '^tests: cmake' "$con" 2>/dev/null && for f in tests_cmake.log tests_list.log tests_list_run.log tests_list_qml.log; do cp "$src/$f" "$dst/" 2>/dev/null; done
    grep -q '; jom -j' "$con" 2>/dev/null && cp "$src/tests_build.log" "$dst/" 2>/dev/null
    grep -q '^tests: ctest pass 1 exit' "$con" 2>/dev/null && cp "$src/tests_ctest.log" "$dst/" 2>/dev/null
    grep -q '^tests: ctest pass 2 exit' "$con" 2>/dev/null && cp "$src/tests_qml_ui.log" "$dst/" 2>/dev/null
    grep -q '^release: cmake' "$con" 2>/dev/null && cp "$src/release_cmake.log" "$dst/" 2>/dev/null
    grep -q '^release: .*; jom -j' "$con" 2>/dev/null && cp "$src/release_build.log" "$dst/" 2>/dev/null
    grep -q '^PATCH: ' "$con" 2>/dev/null && cp "$src/patch_created.txt" "$dst/" 2>/dev/null
    rm -f "$con"
    return 0
}

# one_run: one attempt. Sets RC; returns 0 when the run ended (any RC), 1 when it should be retried (BUSY with --wait).
one_run() {
    local stamp con rcf t0 seen=0 n rc
    lock_take
    preflight; rc=$?
    if (( rc == 20 )); then say "BUSY: $BUSY_WHAT is running on the cabin; nothing launched"; lock_drop; RC=20; return 1; fi
    if (( rc != 0 )); then lock_drop; RC=$rc; return 0; fi
    stamp=$(date '+%Y%m%d-%H%M%S')
    RUN="cabin-$LANE-$stamp"
    RUNDIR=$SHARE/scratch/$RUN
    while [[ -e $RUNDIR ]]; do sleep 1; stamp=$(date '+%Y%m%d-%H%M%S'); RUN="cabin-$LANE-$stamp"; RUNDIR=$SHARE/scratch/$RUN; done
    mkdir "$RUNDIR" || { lock_drop; RC=21; say "cannot make $RUNDIR"; return 0; }
    (( HAS_PATCH )) && cp "$PATCH" "$RUNDIR/wip.patch"
    write_wrapper "$RUNDIR/run.cmd" "$RUN" $HAS_PATCH
    con=$RUNDIR/console.log rcf=$RUNDIR/rc.txt
    say "run dir $RUNDIR (on the cabin $WSHARE\\scratch\\$RUN)"
    t0=$(now)
    # From here the cabin run may be live, whatever the Mac side sees: the lock is kept for it on any exit until its
    # rc.txt has been read (lock_drop), and the holder names the run so a later waiter can tell when it is over.
    lock_note "$RUN"; LIVE=1
    launch "$RUN" || { say "the launch may still have started it: the lock is kept"; RC=22; return 0; }
    while true; do
        if [[ -f $con ]]; then
            n=$(wc -l < "$con" | tr -d ' ')
            if (( n > seen )); then tail -n +$(( seen + 1 )) "$con" | head -n $(( n - seen )) | tr -d '\r' | sed 's/^/  cabin| /'; seen=$n; fi
        fi
        [[ -s $rcf ]] && break
        if [[ ! -f $con ]] && (( $(now) - t0 > ${LAUNCH_GRACE%.*} )); then
            say "no console.log $(fmt_dur $(( $(now) - t0 ))) after the launch: the wrapper did not start"
            lock_drop; RC=22; return 0
        fi
        if (( $(now) - t0 > RUN_MAX )); then
            say "the cabin run has not ended after $(fmt_dur $RUN_MAX); left running, its run dir kept"
            copy_logs "$con" "$RUNDIR/logs"; lock_drop; RC=23; WALL=$(( $(now) - t0 )); return 0
        fi
        sleep $POLL
    done
    sleep 1
    n=$(wc -l < "$con" | tr -d ' ')
    (( n > seen )) && tail -n +$(( seen + 1 )) "$con" | tr -d '\r' | sed 's/^/  cabin| /'
    WALL=$(( $(now) - t0 ))
    RC=$(tr -dc '0-9' < "$rcf")
    [[ -n $RC ]] || RC=22
    LIVE=0
    copy_logs "$con" "$RUNDIR/logs"
    lock_drop
    if (( RC == 10 )) && clean_console "$con" /dev/stdout | grep -q -E '^tests: (BUSY|REFUSED - the studio is busy|REFUSED before ctest - the studio became busy|the qml_ui label NOT run - the studio became busy)'; then
        say "BUSY: the cabin script found the studio in use"
        report 20 "$con" "$RUNDIR/logs" $WALL > "$RUNDIR/summary.txt"
        RC=20; return 1
    fi
    return 0
}

# ── the self-test ─────────────────────────────────────────────────────────────────────────────────────────────────
self_test() {
    local T fails=0 out rc
    T=$(mktemp -d "${TMPDIR:-/tmp}/cabin_run_selftest.XXXXXX") || { print "FAIL - cannot make a temporary directory"; return 1; }
    T=${T:A}
    local S=$T/share SAMPLES=$HERE/cabin_run_samples
    ok()  { print -r -- "ok   - $1"; }
    bad() { print -r -- "FAIL - $1"; (( fails++ )); }
    mkdir -p $S/scratch/studio-build $T/bin
    # A stub ssh first on PATH: the real one must never be reached.
    print -r -- "#!/bin/sh
touch '$T/REAL-SSH-CALLED'; exit 255" > $T/bin/ssh; chmod +x $T/bin/ssh
    path=($T/bin $path)

    # The fake cabin: tasklist from tasklist.seq (each call takes the first line, the last one sticks); a launch
    # decodes the -EncodedCommand, finds scratch\<run>\run.cmd, and plays the scenario from scenario.seq in the
    # background: it writes console.log from a sample, the studio-build logs, then rc.txt.
    print -r -- "#!/bin/zsh
T='$T'; S='$S'; SAMPLES='$SAMPLES'" > $T/fake_ssh
    cat >> $T/fake_ssh <<'EOF'
print -r -- "$(date +%s) $*" >> $T/ssh.log
crlf() { sed 's/$/\r/' "$1"; }   # the samples are stored LF (a CRLF file can show modified under autocrlf); the cabin writes CRLF
pop() { local f=$1 l; l=$(head -1 $f); (( $(wc -l < $f) > 1 )) && sed -i '' 1d $f; print -r -- $l; }
cmd=$2
case $cmd in
  *tasklist*)
    case $(pop $T/tasklist.seq) in
      idle) print '"System Idle Process","0","Services","0","8 K"\r'; print '"explorer.exe","4100","Console","1","90,000 K"\r' ;;
      busy) print '"explorer.exe","4100","Console","1","90,000 K"\r'; print '"Foresight.Premier.exe","5100","Console","1","900,000 K"\r' ;;
      jom)  print '"explorer.exe","4100","Console","1","90,000 K"\r'; print '"jom.exe","6100","Services","0","20,000 K"\r' ;;
      none) ;;
    esac ;;
  *-EncodedCommand*)
    ps=$(print -r -- ${cmd##* } | base64 -D | iconv -f UTF-16LE -t UTF-8)
    if [[ $ps == *Get-CimInstance* ]]; then
      print -r -- "$ps" > $T/cimquery.txt
      n=$(pop $T/cmd.seq); [[ $n == <-> ]] && print "CMDCOUNT=$n\r" || print "Get-CimInstance : Access denied\r"
      exit 0
    fi
    run=${ps#*\\scratch\\}; run=${run%%\\run.cmd*}
    [[ $run != *[\\\ ]* && -f $S/scratch/$run/run.cmd ]] || { print "no run.cmd in: $ps"; exit 1; }
    sc=$(pop $T/scenario.seq)
    [[ $sc == nolaunch ]] && { print 'CIM ReturnValue=8 ProcessId=\r'; exit 0; }
    (
      R=$S/scratch/$run; B=$S/scratch/studio-build
      print -r -- "start $run" >> $T/cabin.log
      case $sc in
        fail) crlf $SAMPLES/console_fail.txt > $R/console.log; cp $SAMPLES/tests_ctest.txt $B/tests_ctest.log; cp $SAMPLES/tests_qml_ui.txt $B/tests_qml_ui.log; rc=12 ;;
        pass) crlf $SAMPLES/console_pass.txt > $R/console.log; rc=0
              print '100% tests passed, 0 tests failed out of 288' > $B/tests_ctest.log
              print '100% tests passed, 0 tests failed out of 39' > $B/tests_qml_ui.log ;;
        busy) crlf $SAMPLES/console_busy.txt > $R/console.log; rc=10 ;;
        slow) crlf $SAMPLES/console_pass.txt > $R/console.log; rc=0; sleep 2 ;;
        release|releasefail)
              if [[ $sc == release ]]; then end='release: build exit 0  9:12:41.50'; fin='release: linked, nothing deployed or installed - end Sat 28/11/2026  9:12:42.00 - exit 0'; rc=0
              else end='release: build exit 2  9:03:10.00'; fin='src/x.cpp(12): error C2065: undeclared identifier'; rc=5; fi
              printf '%s\n' 'cabin_run: HEAD a5089848' 'tests: start Sat 28/11/2026  9:00:00.00' \
                'release: the Release link check - PinPointStudio and swinglab_run are built in build\Release-Installer, nothing is deployed or installed' \
                'tests: studio idle - 120 processes, none PinPoint Studio, Foresight, FSX or GSPro' 'release: cmake  9:00:01.00' \
                'release: PinPointStudio and swinglab_run; jom -j16  9:00:40.00' "$end" "$fin" '== status after' '== end' \
                | sed 's/$/\r/' > $R/console.log
              print 'built' > $B/release_build.log ;;
      esac
      sleep 1.2
      print -r -- "end $run" >> $T/cabin.log
      print "$rc\r" > $R/rc.txt
    ) > /dev/null 2>&1 &
    print 'CIM ReturnValue=0 ProcessId=4242\r' ;;
  *) exit 255 ;;
esac
EOF
    chmod +x $T/fake_ssh
    # The stale studio-build logs a previous run left: a run that never reached pass 1 must not read them.
    print -r -- "100% tests passed, 0 tests failed out of 1" > $S/scratch/studio-build/tests_ctest.log

    # A worktree fixture: a bare origin, a clone on origin/main with the real make-wip-patch.sh, and a change.
    local O=$T/origin.git W=$T/wt-fix
    git init -q --bare -b main $O && git clone -q $O $W 2>/dev/null
    mkdir -p $W/tools/studio $W/src && cp $HERE/../studio/make-wip-patch.sh $W/tools/studio/ && print "int a;" > $W/src/a.cpp
    print "@echo off" > $W/tools/studio/studio-build.cmd
    git -C $W add -A && git -C $W -c user.name=t -c user.email=t@t commit -q -m base && git -C $W push -q origin main 2>/dev/null
    print "int b;" >> $W/src/a.cpp
    local H=(--_test_share $S --_test_ssh $T/fake_ssh --_test_poll 0.2 --_test_wait 0.3 --_test_run_max 30 --_test_pre_max 8 --_test_grace 3)
    setup() { print -r -- "$1" | tr ' ' '\n' > $T/tasklist.seq; print -r -- "$2" | tr ' ' '\n' > $T/scenario.seq
              print -r -- "${3:-0}" | tr ' ' '\n' > $T/cmd.seq; : > $T/cabin.log; }
    # waits up to $2 s for file $1 to hold $3
    within() { local i; for i in {1..$(( $2 * 20 ))}; do grep -q -- "$3" "$1" 2>/dev/null && return 0; sleep 0.05; done; return 1; }

    # 0. The hooks refuse the real share.
    "$SELF" --_test_share $REAL_SHARE --_test_ssh $T/fake_ssh $W > $T/o0 2>&1; rc=$?
    if [[ $rc == 2 && ! -e $T/ssh.log ]]; then ok "--_test_share refuses the real share ($REAL_SHARE), nothing run"
    else bad "--_test_share on the real share gave exit $rc"; fi

    # 1. A failing run: the summary of the real samples, the wrapper, the run dir kept.
    setup "idle" "fail"
    out=$("$SELF" $H $W 2>&1); rc=$?
    local run=$(ls $S/scratch | grep '^cabin-wt-fix-' | head -1)
    [[ $rc == 12 ]] && ok "a failing run passes the script's exit 12 through" || bad "a failing run gave exit $rc"
    local want
    for want in \
        "exit 12, a test failed" \
        "build: jom -j16, exit 0, 1 min 49 s (13:36:34.28 to 13:38:23.89)" \
        "pass 1: 278 passed, 10 failed of 288; 1 not run (skipped or disabled)" \
        'FAIL pass 1 tendencies_test (Failed): ASSERT: "size_t(d.size) <= MaxSize"' \
        "FAIL pass 1 dtl_posture_test (Exit code 0xc0000409): last output: [PASS] §9" \
        "FAIL pass 1 lm_session_model_test (Failed): [FAIL] N0 the user norm set goes in this run's own temporary directory" \
        "FAIL pass 1 parity_json_test (Failed): FAIL  run folders tokenised, the longest first, in lists too" \
        "FAIL pass 1 athlete_repository_test (Failed): FAIL!  : AthleteRepositoryTest::tuningLogAppendsUnderTheLock() Compared values are not the same" \
        "FAIL pass 1 shot_commands_test (Failed): [FAIL] C15e a failed save" \
        "pass 2: 36 passed, 2 failed of 38; 1 not run (skipped or disabled)" \
        "FAIL pass 2 qml_ui.tst_session_report (Failed): FAIL!  : qml_ui::SessionReport::test_01_everyFixtureHoldsTheLayoutContract(sixty-swings-three-clubs)" \
        "FAIL pass 2 qml_ui.tst_charts_panel (Failed): FAIL!  : qml_ui::ChartsPanel::test_15_the_open_row_survives_a_rebuild()" \
        "stores: unchanged" "cabin checkout: restored clean" "cabin HEAD a5089848, patch sha256 c6e374f31ec335f5" ; do
        [[ $out == *"$want"* ]] && ok "summary: $want" || bad "summary lacks: $want"
    done
    [[ $out != *"cabin HEAD 93592de3"* ]] && ok "git apply's warnings, which echo the patch's own sample lines, are not read as the console" \
        || bad "a sample line echoed by git apply was read as this run's HEAD"
    [[ -n $run && -f $S/scratch/$run/logs/tests_ctest.log && -f $S/scratch/$run/console.log && -f $S/scratch/$run/wip.patch ]] \
        && ok "a failed run keeps its run dir, with the logs" || bad "the failed run's dir or logs are missing ($run)"
    [[ -f $W/build/cabin-runs/$run.txt ]] && ok "the summary is written into the worktree's build/cabin-runs/" || bad "no summary in the worktree"
    local wr=$S/scratch/$run/run.cmd
    if [[ -f $wr ]] && [[ $(tr -d '\r' < $wr | grep -c .) == $(grep -c $'\r$' $wr) ]] && ! LC_ALL=C grep -q '[^ -~[:cntrl:]]' $wr; then ok "run.cmd is ASCII with CRLF on every line"
    else bad "run.cmd is not ASCII CRLF"; fi
    grep -q "studio-build.cmd\" tests nopull patch \"%RUN%\\\\wip.patch\" jobs 16 >>" $wr && grep -q "^git pull --ff-only" $wr \
        && grep -q "set \"RUN=C:\\\\PinPointStudio\\\\scratch\\\\$run\"" $wr \
        && ok "run.cmd pulls first, then runs studio-build.cmd tests nopull with the patch, jobs 16" || bad "run.cmd's command is wrong"
    [[ ! -e $S/scratch/cabin.lock ]] && ok "the lock is released after the run" || bad "the lock was left behind"

    # 2. Two runners at once: the second waits for the lock, and the cabin runs never overlap.
    setup "idle" "fail fail"
    "$SELF" $H $W > $T/o2a 2>&1 &
    local pa=$!
    sleep 0.3
    "$SELF" $H $W > $T/o2b 2>&1 &
    local pb=$!
    local i; for i in {1..300}; do kill -0 $pa 2>/dev/null || kill -0 $pb 2>/dev/null || break; sleep 0.1; done
    wait $pa; wait $pb
    if [[ $(tr '\n' ' ' < $T/cabin.log) == start\ *\ end\ *\ start\ *\ end\ * ]]; then ok "two runners: the cabin runs one after the other ($(grep -c start $T/cabin.log) runs)"
    else bad "the cabin runs overlapped: $(tr '\n' ' ' < $T/cabin.log) | A: $(tail -2 $T/o2a | tr '\n' ' ') | B: $(tail -2 $T/o2b | tr '\n' ' ')"; fi
    grep -q "waiting for the cabin lock: held by lane wt-fix" $T/o2a $T/o2b && ok "the second runner waits and says so" || bad "no waiting line"

    # 3. A stale lock is reclaimed, with a line.
    setup "idle" "pass"
    mkdir $S/scratch/cabin.lock && print -r -- "lane=wt-old
pid=1
host=elsewhere
start=$(( $(now) - 20000 ))
started=yesterday
ceiling=9600
token=old" > $S/scratch/cabin.lock/holder
    out=$("$SELF" $H $W 2>&1); rc=$?
    [[ $out == *"reclaimed the cabin lock (STALE: "*"): lane wt-old"* && $rc == 0 ]] && ok "a lock older than its ceiling is reclaimed, with a line" \
        || bad "the stale lock was not reclaimed (exit $rc): $(print -r -- $out | grep -m3 cabin_run: | tr '\n' ' ')"
    # and a passing run deletes its run dir, keeping the summary
    run=$(ls -t $W/build/cabin-runs | head -1); run=${run%.txt}
    [[ ! -e $S/scratch/$run && -f $W/build/cabin-runs/$run.txt && $out == *"exit 0, all tests passed"* ]] \
        && ok "a passing run deletes its run dir and keeps the summary" || bad "the passing run's dir was kept, or no summary"
    [[ $out == *"build: jom -j16, exit 0, 1 min 3 s (9:59:58.00 to 10:01:01.50)"* ]] && ok "a %TIME% with a leading space (before 10:00) is read" \
        || bad "the leading-space build time was misread"

    # 4. BUSY: before the launch (tasklist), nothing launched, exit 20; and with --wait it retries and runs.
    setup "busy" "pass"; : > $T/ssh.log
    out=$("$SELF" $H $W 2>&1); rc=$?
    [[ $rc == 20 && $out == *"BUSY: Foresight.Premier.exe"* ]] && ! grep -q EncodedCommand $T/ssh.log \
        && ok "a busy studio: BUSY, exit 20, nothing launched" || bad "busy gave exit $rc"
    [[ ! -e $S/scratch/cabin.lock ]] && ok "BUSY releases the lock" || bad "BUSY left the lock"
    setup "busy busy idle" "pass"
    out=$("$SELF" $H $W --wait 2>&1); rc=$?
    [[ $rc == 0 && $(grep -c 'retrying in' <<< $out) == 2 ]] && ok "--wait retries a busy studio and then runs (2 retries)" \
        || bad "--wait gave exit $rc"
    # the cabin script's own BUSY (exit 10 with a BUSY line) is BUSY too
    setup "idle" "busy"
    out=$("$SELF" $H $W 2>&1); rc=$?
    [[ $rc == 20 ]] && ok "the script's own exit 10 for a busy studio is BUSY, exit 20" || bad "the script's busy gave exit $rc"
    [[ $out == *"pass 1: not run"* ]] && ok "the last run's studio-build logs are not read when this run's console has no pass" \
        || bad "a stale pass-1 log was read"

    # 5. A foreign jom on the cabin: wait, then run.
    setup "jom jom idle" "pass"
    out=$("$SELF" $H $W 2>&1); rc=$?
    [[ $rc == 0 && $out == *"already building or testing (jom.exe x1)"* ]] && ok "a foreign jom.exe: it waits, then runs" || bad "foreign jom gave exit $rc"

    # 5b. TERM while it holds the lock (waiting on a foreign jom): the lock is released, exit 130.
    setup "jom" "pass"
    "$SELF" $H $W > $T/o5b 2>&1 &
    local pt=$!
    for i in {1..100}; do [[ -e $S/scratch/cabin.lock/holder ]] && break; sleep 0.05; done
    sleep 0.3; kill -TERM $pt; wait $pt; rc=$?
    [[ $rc == 143 && ! -e $S/scratch/cabin.lock ]] && ok "TERM while holding the lock (before a launch): released, exit 143" \
        || bad "TERM gave exit $rc; lock left: $([[ -e $S/scratch/cabin.lock ]] && echo yes || echo no)"
    rm -rf $S/scratch/cabin.lock
    setup "jom" "pass"
    "$SELF" $H $W > $T/o5c 2>&1 &
    pt=$!
    for i in {1..100}; do [[ -e $S/scratch/cabin.lock/holder ]] && break; sleep 0.05; done
    sleep 0.3; kill -HUP $pt; wait $pt; rc=$?
    [[ $rc == 129 && ! -e $S/scratch/cabin.lock ]] && ok "HUP while holding the lock (before a launch): released, exit 129" \
        || bad "HUP gave exit $rc; lock left: $([[ -e $S/scratch/cabin.lock ]] && echo yes || echo no)"
    rm -rf $S/scratch/cabin.lock

    # B1. A cabin run that may still be live keeps the lock; a running studio-build.cmd/run.cmd is waited for.
    setup "idle" "pass" "1 1 0"
    out=$("$SELF" $H $W 2>&1); rc=$?
    [[ $rc == 0 && $out == *"already building or testing (studio-build.cmd/run.cmd x1)"* ]] \
        && ok "a studio-build.cmd/run.cmd running with no jom or ctest: it waits, then runs" || bad "a running wrapper gave exit $rc"
    grep -q "Get-CimInstance Win32_Process -Filter \"Name='cmd.exe'\"" $T/cimquery.txt && grep -q 'studio-build\\.cmd|\\\\scratch\\\\\[^ \]+\\.cmd' $T/cimquery.txt \
        && ok "the cmd.exe query sent is the one checked on the cabin" || bad "the cmd.exe query is not the checked one: $(cat $T/cimquery.txt)"
    setup "idle" "pass" "junk"
    out=$("$SELF" $H $W 2>&1); rc=$?
    [[ $rc == 22 && ! -e $S/scratch/cabin.lock ]] && ok "a cmd.exe query with no count fails closed (22), nothing launched" || bad "a bad query gave exit $rc"
    setup "idle" "slow pass"
    "$SELF" $H $W > $T/o6a 2>&1 &
    pt=$!
    within $T/o6a 5 "launched detached" && sleep 0.3
    kill -TERM $pt; wait $pt; rc=$?
    local krun=$(field run $S/scratch/cabin.lock/holder)
    [[ $rc == 143 && -n $krun && $(<$T/o6a) == *"lock is KEPT"* ]] && ok "TERM after the launch: the lock is KEPT for the live run ($krun)" \
        || bad "TERM after the launch: exit $rc, kept run '$krun'"
    out=$("$SELF" $H $W 2>&1); rc=$?
    [[ $rc == 0 && $out == *"reclaimed the cabin lock (kept for its cabin run $krun, which is over)"* ]] \
        && ok "the next runner waits for that run's rc.txt, then reclaims the kept lock and runs" || bad "the kept lock gave exit $rc"
    [[ $(tr '\n' ' ' < $T/cabin.log) == "start $krun end $krun start "* ]] && ok "the two cabin runs did not overlap" \
        || bad "the runs overlapped: $(tr '\n' ' ' < $T/cabin.log)"
    setup "idle" "slow"
    out=$("$SELF" $H --_test_run_max 1 $W 2>&1); rc=$?
    [[ $rc == 23 && -n $(field run $S/scratch/cabin.lock/holder) ]] && ok "exit 23 (over its ceiling) keeps the lock for the live run" \
        || bad "exit 23 case gave exit $rc"
    within $T/cabin.log 6 "^end" ; rm -rf $S/scratch/cabin.lock
    setup "idle" "nolaunch pass"
    out=$("$SELF" $H $W 2>&1); rc=$?
    [[ $rc == 22 && -e $S/scratch/cabin.lock ]] && ok "a launch that reports failure (22) still keeps the lock: it may have started" || bad "nolaunch gave exit $rc"
    out=$("$SELF" $H $W 2>&1); rc=$?
    [[ $rc == 0 && $out == *"which is over"* ]] && ok "a kept lock whose wrapper never started is reclaimed after the launch grace" \
        || bad "the never-started lock gave exit $rc"

    # B2. A reclaim deletes only the lock it judged: a fresh lock swapped in before the rename is put back.
    local variant
    for variant in holderless token; do
        rm -rf $S/scratch/cabin.lock; mkdir $S/scratch/cabin.lock
        if [[ $variant == token ]]; then
            print -r -- "lane=wt-old
token=old
start=$(( $(now) - 20000 ))
ceiling=9600" > $S/scratch/cabin.lock/holder
        else
            touch -t 202601010000 $S/scratch/cabin.lock
        fi
        print -r -- "#!/bin/sh
[ -e '$T/swapped' ] && exit 0; touch '$T/swapped'
rm -rf '$S/scratch/cabin.lock'; mkdir '$S/scratch/cabin.lock'
printf 'lane=wt-fresh\ntoken=fresh\nstart=%s\nceiling=9600\n' \$(date +%s) > '$S/scratch/cabin.lock/holder'" > $T/swap.sh; chmod +x $T/swap.sh; rm -f $T/swapped
        setup "idle" "pass"
        "$SELF" $H --_test_before_mv $T/swap.sh $W > $T/o7 2>&1 &
        pt=$!
        within $T/o7 5 "waiting for the cabin lock: held by lane wt-fresh"
        kill -TERM $pt; wait $pt
        [[ $(field token $S/scratch/cabin.lock/holder) == fresh && $(<$T/o7) == *"it is put back"* && ! -s $T/cabin.log ]] \
            && ok "B2 ($variant): a fresh lock swapped in before the rename is put back, not deleted, and waited for" \
            || bad "B2 ($variant): token now '$(field token $S/scratch/cabin.lock/holder)'; $(grep -m2 cabin_run: $T/o7 | tr '\n' ' ')"
    done
    rm -rf $S/scratch/cabin.lock

    # 8. The modes: a release run reaches the wrapper and is summarised as a link check; a bad mode is refused.
    setup "idle" "releasefail"
    out=$("$SELF" $H $W mode release jobs 12 2>&1); rc=$?
    run=$(ls -t $S/scratch | grep '^cabin-wt-fix-' | head -1)
    [[ $rc == 5 && $out == *"(mode release): exit 5, the cabin's Release build (cmake or jom) failed"* && $out == *"error C2065"* ]] \
        && ok "a failed release run passes exit 5 through, with its error line" || bad "a failed release run gave exit $rc"
    grep -q "studio-build.cmd\" release nopull patch \"%RUN%\\\\wip.patch\" jobs 12 >>" $S/scratch/$run/run.cmd \
        && ok "run.cmd runs studio-build.cmd release nopull with the patch, jobs 12" || bad "run.cmd's release command is wrong"
    [[ -f $S/scratch/$run/logs/release_build.log ]] && ok "a release run's build log is copied" || bad "no release_build.log in the run's logs"
    setup "idle" "release"
    out=$("$SELF" $H $W mode release 2>&1); rc=$?
    [[ $rc == 0 && $out == *"exit 0, the Release app and swinglab_run linked (nothing installed)"* \
        && $out == *"build: jom -j16, exit 0, 12 min 1 s"* && $out == *"ctest: not run (mode release)"* && $out != *"pass 1"* ]] \
        && ok "a passing release run: linked, its build time, no ctest pass" || bad "a passing release run gave exit $rc: $(print -r -- $out | grep -m2 '^==\|^build')"
    : > $T/ssh.log
    out=$("$SELF" $H $W mode bogus 2>&1); rc=$?
    [[ $rc == 2 && ! -s $T/ssh.log ]] && ok "an unknown mode is refused (2), the cabin untouched" || bad "mode bogus gave exit $rc"

    # 6. Refusals on the Mac: a local commit not on origin/main; and an ssh that lists nothing.
    git -C $W -c user.name=t -c user.email=t@t commit -q -am local
    setup "idle" "pass"; : > $T/ssh.log
    out=$("$SELF" $H $W 2>&1); rc=$?
    [[ $rc == 21 && ! -s $T/ssh.log ]] && ok "a HEAD not on origin/main is refused (21), the cabin untouched" || bad "a local commit gave exit $rc"
    git -C $W reset -q --soft HEAD~1
    print "rem changed" >> $W/tools/studio/studio-build.cmd; : > $T/ssh.log
    out=$("$SELF" $H $W 2>&1); rc=$?
    [[ $rc == 21 && ! -s $T/ssh.log && $out == *"studio-build.cmd has local changes"* ]] && ok "a local change to studio-build.cmd is refused (21), the cabin untouched" \
        || bad "a studio-build.cmd change gave exit $rc"
    git -C $W checkout -q -- tools/studio/studio-build.cmd
    setup "none" "pass"
    out=$("$SELF" $H $W 2>&1); rc=$?
    [[ $rc == 22 ]] && ok "a tasklist that lists nothing fails closed (22)" || bad "an empty tasklist gave exit $rc"

    # 7. Nothing touched the real share or ssh.
    [[ ! -e $T/REAL-SSH-CALLED ]] && ok "the real ssh was never called" || bad "the real ssh was called"
    ! grep -rq "$REAL_SHARE" $T/ssh.log 2>/dev/null && ok "no fake-cabin call named the real share" || bad "a call named $REAL_SHARE"

    rm -rf "$T"
    if (( fails == 0 )); then print "self-test: all cases hold"; return 0; fi
    print "self-test: $fails FAILED"; return 1
}

# ── main ──────────────────────────────────────────────────────────────────────────────────────────────────────────
# All of it in one function, read whole before it runs: zsh reads a script's top level as it goes, so an edit to this
# file during a long run would otherwise change what the rest of that run executes.
main() {
    if [[ $1 == --self-test ]]; then self_test; exit $?; fi

    while [[ $1 == --_test_* ]]; do
        case $1 in
            --_test_share) SHARE=${2:A}; TESTING=1; shift 2 ;;
            --_test_ssh) SSH=($2); shift 2 ;;
            --_test_poll) POLL=$2; shift 2 ;;
            --_test_wait) WAIT_INT=$2; shift 2 ;;
            --_test_run_max) RUN_MAX=$2; shift 2 ;;
            --_test_pre_max) PRE_MAX=$2; shift 2 ;;
            --_test_grace) LAUNCH_GRACE=$2; shift 2 ;;
            --_test_before_mv) TEST_BEFORE_MV=$2; shift 2 ;;
            *) die 2 "unknown test hook $1" ;;
        esac
    done
    if (( TESTING )); then
        [[ $SHARE != ${REAL_SHARE:A} && $SHARE != ${REAL_SHARE:A}/* ]] || die 2 "--_test_share may not name the real share"
        [[ ${SSH[1]} != ssh ]] || die 2 "--_test_share needs --_test_ssh: a test never reaches the real cabin"
    elif [[ ${SSH[1]} != ssh || -n $TEST_BEFORE_MV ]]; then
        die 2 "--_test_ssh and --_test_before_mv need --_test_share"
    fi

    WT= JOBS=16 WAIT=0 MODE=tests
    while (( $# )); do
        case $1 in
            --wait) WAIT=1; shift ;;
            jobs) [[ $2 == <-> ]] && (( $2 >= 1 && $2 <= 24 )) || die 2 "jobs: '$2' is not 1 to 24"; JOBS=$2; shift 2 ;;
            mode) [[ $2 == (tests|build|release) ]] || die 2 "mode: '$2' is not tests, build or release"; MODE=$2; shift 2 ;;
            -*) usage; exit 2 ;;
            *) [[ -z $WT ]] || { usage; exit 2; }; WT=$1; shift ;;
        esac
    done
    [[ -n $WT ]] || { usage; exit 2; }
    [[ -d $WT ]] || die 2 "no worktree at $WT"
    WT=$(git -C "$WT" rev-parse --show-toplevel 2>/dev/null) || die 2 "$WT is not a git checkout"
    [[ -x $WT/tools/studio/make-wip-patch.sh ]] || die 21 "$WT has no tools/studio/make-wip-patch.sh"
    [[ -d $SHARE/scratch && -w $SHARE/scratch ]] || die 21 "the share is not mounted: no writable $SHARE/scratch"
    LANE=${WT:t}; LANE=${LANE//[^A-Za-z0-9._-]/-}
    LOCK=$SHARE/scratch/cabin.lock
    LOCK_CEIL=$(( ${RUN_MAX%.*} + ${PRE_MAX%.*} + 600 ))
    TOKEN="$$-$RANDOM$RANDOM-$(now)"

    # 1. The base, and the patch, frozen now.
    git -C "$WT" fetch --quiet origin main 2>/dev/null || die 21 "cannot fetch origin main in $WT"
    git -C "$WT" merge-base --is-ancestor HEAD origin/main || \
        die 21 "HEAD $(git -C "$WT" rev-parse --short HEAD) is not on origin/main: the cabin pulls main, so commits not on it would be left out. Rebase onto origin/main, or push first."
    BEHIND=$(git -C "$WT" rev-list --count HEAD..origin/main)
    (( BEHIND > 0 )) && say "NOTE: HEAD is $BEHIND commit(s) behind origin/main; the cabin applies the patch on origin/main's tip"
    git -C "$WT" diff --quiet HEAD -- tools/studio/studio-build.cmd || die 21 "tools/studio/studio-build.cmd has local changes; the cabin always runs main's copy (cmd reads a running batch file from disk), so they would not be tested. Land it on main first."
    TMPD=$(mktemp -d "${TMPDIR:-/tmp}/cabin_run.XXXXXX") || die 21 "cannot make a temporary directory"
    PATCH=$TMPD/wip.patch HAS_PATCH=1
    pout=$("$WT/tools/studio/make-wip-patch.sh" "$TMPD" 2>&1); prc=$?
    if (( prc == 0 )); then
        print -r -- "$pout" | sed -n '/^== files/,/^== patch/p' | grep -v '^== patch' | sed 's/^/  /'
        print -r -- "$pout" | grep '^NOTE' | sed 's/^/  /'
        PATCH_DESC="$(grep -c '^diff --git' $PATCH) file(s), sha256 $(shasum -a 256 $PATCH | cut -c1-16)..."
    elif [[ $pout == *"nothing to patch"* ]]; then
        HAS_PATCH=0 PATCH_DESC="none (the worktree matches HEAD; origin/main itself is tested)"
        say "no patch: the worktree matches HEAD, so origin/main itself is tested"
    else
        die 21 "make-wip-patch.sh failed: $pout"
    fi

    # 2-6. Lock, pre-flight, run; BUSY retried only with --wait.
    T_START=$(now)
    while true; do
        WALL=0
        one_run && break
        if (( ! WAIT )); then break; fi
        if (( $(now) - T_START + ${WAIT_INT%.*} > WAIT_MAX )); then say "still BUSY after $(fmt_dur $(( $(now) - T_START ))); giving up"; break; fi
        say "BUSY: retrying in $(fmt_dur $WAIT_INT) (--wait, up to $(fmt_dur $WAIT_MAX))"
        [[ -n $RUNDIR && -d $RUNDIR ]] && rm -rf "$RUNDIR"
        RUNDIR=
        sleep $WAIT_INT
    done

    # 5-7. Report and clean up.
    mkdir -p "$WT/build/cabin-runs"
    if [[ -n $RUNDIR && -d $RUNDIR ]]; then
        SUMMARY=$WT/build/cabin-runs/$RUN.txt
        report $RC "$RUNDIR/console.log" "$RUNDIR/logs" $WALL > "$SUMMARY"
        if (( RC == 0 )); then rm -rf "$RUNDIR"; else cp "$SUMMARY" "$RUNDIR/summary.txt"; fi
        print
        cat "$SUMMARY"
        if (( RC == 0 )); then print -r -- "run dir deleted (a passing run); summary $SUMMARY"
        else print -r -- "run dir kept: $RUNDIR (logs in logs/); summary $SUMMARY"; fi
    else
        RUN=${RUN:-cabin-$LANE-$(date '+%Y%m%d-%H%M%S')}
        SUMMARY=$WT/build/cabin-runs/$RUN.txt
        { print -r -- "== cabin run: exit $RC, $(code_text $RC)"; [[ $RC == 20 ]] && print -r -- "busy: $BUSY_WHAT"; } > "$SUMMARY"
        print; cat "$SUMMARY"
    fi
    exit $RC
}

TMPD=
trap 'lock_drop; [[ -n $TMPD ]] && rm -rf "$TMPD"; true' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
main "$@"
exit $?
