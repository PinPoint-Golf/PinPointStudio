#!/bin/zsh
# guard.sh <command…> — runs one PinPoint Studio app under two guards. Every harness run goes through it.
#  1. At most 2 app instances at once, machine-wide, across all agents (mkdir lock slots in
#     $TYPESET_LOCKDIR, default /tmp/pps-typeset-locks). A slot whose owner died is reclaimed.
#  2. A memory watchdog: the app is killed when its footprint passes $GUARD_GB (default 6 GB).
#     On 11 Oct 2026 a runaway QML mock (a layout loop creating objects) reached 97 GB and filled swap.
#     Prints "GUARD: KILLED …" when it fires.
# See tools/typeset-harness/README.md.
LOCKDIR=${TYPESET_LOCKDIR:-/tmp/pps-typeset-locks}; mkdir -p $LOCKDIR
LIMIT=${GUARD_GB:-6}
SLOT=""; WAITED=""
while [[ -z $SLOT ]]; do
  for s in 1 2; do if mkdir $LOCKDIR/slot$s 2>/dev/null; then SLOT=$LOCKDIR/slot$s; break; fi; done
  # reclaim a slot whose owner died
  for s in 1 2; do p=$(cat $LOCKDIR/slot$s/pid 2>/dev/null); [[ -n $p ]] && ! kill -0 $p 2>/dev/null && rm -rf $LOCKDIR/slot$s; done
  if [[ -z $SLOT ]]; then
    [[ -z $WAITED ]] && { echo "GUARD: two app instances already running; waiting for a slot…" >&2; WAITED=1; }
    sleep 3
  fi
done
echo $$ > $SLOT/pid
trap 'rm -rf $SLOT' EXIT INT TERM
"$@" &
APP=$!
while kill -0 $APP 2>/dev/null; do
  sleep 3
  for c in $(pgrep -P $APP) $APP; do
    # top prints e.g. "812M", "6.2G" or "7G+"; strip the trend marker before comparing.
    m=$(top -l 1 -pid $c -stats mem 2>/dev/null | tail -1 | tr -d ' +-')
    case $m in
      *G) v=${m%G}; if (( ${v%.*} >= LIMIT )); then echo "GUARD: KILLED pid $c at $m (limit ${LIMIT} GB) — a runaway allocation in the QML (look for a binding/layout loop that creates objects)"; kill -9 $c $APP 2>/dev/null; fi;;
    esac
  done
done
wait $APP
