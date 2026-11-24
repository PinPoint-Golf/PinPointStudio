#!/bin/zsh
# one_at_a_time.sh — runs a command while holding the machine-wide heavy-work lock, so that only one build, ctest run,
# lint run or app probe runs at a time across every lane and worktree on this 16 GB Mac.
#
#   tools/method/one_at_a_time.sh <command...>    waits for the lock, runs the command, releases the lock; the
#                                                 command's exit code is passed through
#   tools/method/one_at_a_time.sh --print-lock    prints the lock's path and exits; takes nothing
#   tools/method/one_at_a_time.sh --self-test     proves the lock on a lock folder in a temporary directory; never
#                                                 touches the real lock
#
# e.g. tools/method/one_at_a_time.sh cmake --build build/wt-5b/build/tests -j 3
#
# The lock is a folder made with mkdir (atomic) at <main checkout>/build/.heavy.lock, holding the holder's pid in
# <lock>/pid. The main checkout is found from git's common dir, so a copy run from a worktree under build/ takes the
# same lock as the main checkout does, and the same lock as the old build/refactor/one_at_a_time.sh. A waiter polls
# every 15 s. A lock whose pid file names a process that no longer exists is stale: the waiter removes it and tries
# again. The holder removes the lock on exit, interrupt or termination.
#
# Test hooks, for the self-test only: --_test_lock <dir> and --_test_poll <s>, given first, replace the lock's path and
# the poll interval. They are flags, not environment variables, and the lock is found with GIT_DIR, GIT_COMMON_DIR
# and GIT_WORK_TREE unset, so nothing a shell inherits can redirect a real run; --_test_lock refuses the real lock's path.
#
# Exit: the command's own code; 2 for a usage error or when the main checkout cannot be found.
# ctest: one_at_a_time_selftest (src/Gui/tests/CMakeLists.txt).

SELF=${0:A}
HERE=${SELF:h}

real_lock() {
    local common
    common=$(env -u GIT_DIR -u GIT_COMMON_DIR -u GIT_WORK_TREE git -C "$HERE" rev-parse --path-format=absolute --git-common-dir 2>/dev/null) || return 1
    [[ -n $common && -d $common && ${common:t} == .git ]] || return 1
    print -r -- "${common:h}/build/.heavy.lock"
}

usage() {
    print -u2 "usage: one_at_a_time.sh <command...> | --print-lock | --self-test"
}

self_test() {
    local T fails=0 real
    T=$(mktemp -d "${TMPDIR:-/tmp}/one_at_a_time_selftest.XXXXXX") || { print "FAIL - cannot make a temporary directory"; return 1; }
    local L=$T/lock
    ok()  { print -r -- "ok   - $1"; }
    bad() { print -r -- "FAIL - $1"; (( fails++ )); }
    # waits up to $2 seconds for process $1 to end; 0 when it ended
    gone_within() { local i; for i in {1..$(( $2 * 20 ))}; do kill -0 $1 2>/dev/null || return 0; sleep 0.05; done; return 1; }

    # 0. The real path: <main checkout>/build/.heavy.lock, never the worktree's own build/.
    real=$(real_lock)
    local printed=$("$SELF" --print-lock)
    local top=$(git -C "$HERE" rev-parse --show-toplevel 2>/dev/null)
    local common=$(git -C "$HERE" rev-parse --path-format=absolute --git-common-dir 2>/dev/null)
    if [[ -n $printed && $printed == "${common:h}/build/.heavy.lock" ]]; then
        ok "--print-lock names the main checkout's build/.heavy.lock ($printed)"
    else
        bad "--print-lock printed '$printed', expected '${common:h}/build/.heavy.lock'"
    fi
    if [[ $top != ${common:h} && $printed == $top/build/.heavy.lock ]]; then
        bad "run from a worktree, the lock resolved to the worktree's own build/ ($printed)"
    fi

    # 1. The test hook refuses the real path (exit 2, the command never ran).
    "$SELF" --_test_lock "$real" --_test_poll 0.1 /bin/sh -c "touch '$T/real-ran'" 2>/dev/null
    local rc=$?
    if [[ $rc == 2 && ! -e $T/real-ran ]]; then ok "--_test_lock refuses the real lock's path"
    else bad "--_test_lock on the real path gave exit $rc (ran: $([[ -e $T/real-ran ]] && echo yes || echo no))"; fi

    # 2. Two holders: the second waits until the first has finished.
    local log=$T/order
    "$SELF" --_test_lock $L --_test_poll 0.1 /bin/sh -c \
        "echo A-start >> '$log'; while [ ! -e '$T/release-A' ]; do sleep 0.05; done; echo A-end >> '$log'" &
    local pa=$!
    local i; for i in {1..100}; do [[ -s $L/pid ]] && break; sleep 0.05; done
    if [[ $(cat $L/pid 2>/dev/null) == $pa ]]; then ok "the first holder takes the lock and records its pid"
    else bad "the lock's pid file holds '$(cat $L/pid 2>/dev/null)', expected the first holder's $pa"; fi
    "$SELF" --_test_lock $L --_test_poll 0.1 /bin/sh -c "echo B-start >> '$log'; echo B-end >> '$log'" &
    local pb=$!
    sleep 0.6                                     # six polls: a second holder that did not wait has run by now
    if kill -0 $pb 2>/dev/null && [[ $(<$log) == A-start ]]; then ok "the second holder waits while the first holds the lock"
    else bad "the second holder did not wait (order so far: $(tr '\n' ' ' < $log))"; fi
    touch $T/release-A
    gone_within $pa 10 && gone_within $pb 10 || bad "a holder was still running 10 s after the release"
    if [[ $(tr '\n' ' ' < $log) == "A-start A-end B-start B-end " ]]; then ok "the second ran only after the first had finished"
    else bad "the order was: $(tr '\n' ' ' < $log)"; fi
    if [[ ! -e $L ]]; then ok "the last holder removed the lock"; else bad "the lock was left behind"; fi

    # 3. A stale pid (a process that has ended) is reclaimed.
    /bin/sh -c 'exit 0' &
    local dead=$!
    wait $dead
    if kill -0 $dead 2>/dev/null; then
        bad "pid $dead was reused before the stale case could run; re-run the self-test"
    else
        mkdir $L && print $dead > $L/pid
        "$SELF" --_test_lock $L --_test_poll 0.1 /bin/sh -c "touch '$T/stale-ran'" &
        local ps=$!
        if gone_within $ps 10 && [[ -e $T/stale-ran ]]; then ok "a lock held by an ended process ($dead) is reclaimed"
        else bad "a lock held by an ended process was not reclaimed within 10 s"; kill $ps 2>/dev/null; fi
        [[ ! -e $L ]] || { bad "the lock was left behind after the stale case"; rm -rf $L; }
    fi

    # 4. The command's exit code is passed through, and the lock is released.
    "$SELF" --_test_lock $L --_test_poll 0.1 /bin/sh -c 'exit 7'
    rc=$?
    if [[ $rc == 7 && ! -e $L ]]; then ok "the command's exit code (7) is passed through and the lock released"
    else bad "exit $rc (expected 7); lock left: $([[ -e $L ]] && echo yes || echo no)"; fi

    rm -rf "$T"
    if (( fails == 0 )); then print "self-test: all cases hold"; return 0; fi
    print "self-test: $fails FAILED"; return 1
}

if [[ $1 == --self-test ]]; then self_test; exit $?; fi

LOCK= POLL=15 TESTING=0
while [[ $1 == --_test_* ]]; do
    case $1 in
        --_test_lock) LOCK=$2; TESTING=1; shift 2 ;;
        --_test_poll) POLL=$2; shift 2 ;;
        *) print -u2 "one_at_a_time.sh: unknown test hook $1"; exit 2 ;;
    esac
done
if (( TESTING )); then
    [[ -n $LOCK && ${LOCK:A} != "$(real_lock)" ]] || { print -u2 "one_at_a_time.sh: --_test_lock may not name the real lock"; exit 2; }
else
    LOCK=$(real_lock) || { print -u2 "one_at_a_time.sh: cannot find the main checkout from git; refusing to run"; exit 2; }
fi
if [[ $1 == --print-lock ]]; then print -r -- $LOCK; exit 0; fi
(( $# )) || { usage; exit 2; }

while ! mkdir $LOCK 2>/dev/null; do
    p=$(cat $LOCK/pid 2>/dev/null)
    if [[ -n $p ]] && ! kill -0 $p 2>/dev/null; then rm -rf $LOCK; continue; fi   # stale holder
    sleep $POLL
done
echo $$ > $LOCK/pid
trap 'rm -rf $LOCK' EXIT INT TERM
"$@"
