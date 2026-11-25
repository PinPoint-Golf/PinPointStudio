#!/bin/zsh
# Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com). GPL-2.0-or-later.
#
# make_fixture.sh — builds the quit pins' fixture library (SE-1b; the busy quit_pin.<point> ctests), once per machine.
#
#   zsh src/Gui/tests/quit_pins/make_fixture.sh [<fixture dir>]      default /mnt/swingdata/scratch/quit-pins-fixture
#
# It COPIES, read only, one golfer's athlete.json and one one-swing session from the live library into a scratch
# library, <fixture dir>/lib (the same route as tools/session-report-mock/fixtures.sh). Nothing in the live library is
# written. quit_pin.py copies <fixture dir>/lib afresh for every run, so a run never reads or writes this copy either.
# Without it the busy pins (re-analysis, catch-up) are reported as Not Run, never as passed.
set -u
LIVE=/mnt/swingdata
SRC_ATHLETE=Nick-Henn
SRC_SESSION=2026-10-13_Nick-Henn_Wrist_01      # one swing, about 7 MB: two videos, its swing document, its diagnostics
FIX=${1:-/mnt/swingdata/scratch/quit-pins-fixture}
[[ $FIX == /mnt/swingdata/scratch/?* && $FIX != *..* ]] || { print -u2 "REFUSED: $FIX is not under /mnt/swingdata/scratch/"; exit 2; }
[[ -f $LIVE/$SRC_ATHLETE/athlete.json && -d $LIVE/$SRC_ATHLETE/$SRC_SESSION ]] ||
    { print -u2 "REFUSED: the source session $LIVE/$SRC_ATHLETE/$SRC_SESSION is not there (is the share mounted?)"; exit 2; }
print "before: $(ls -d $FIX/lib/*/*(N) 2>/dev/null | wc -l | tr -d ' ') session folder(s) in $FIX/lib"
rm -rf $FIX/lib
mkdir -p $FIX/lib/$SRC_ATHLETE
cp -p $LIVE/$SRC_ATHLETE/athlete.json $FIX/lib/$SRC_ATHLETE/ || exit 1
cp -Rp $LIVE/$SRC_ATHLETE/$SRC_SESSION $FIX/lib/$SRC_ATHLETE/ || exit 1
rm -f $FIX/lib/$SRC_ATHLETE/$SRC_SESSION/*.pre-*(N)                 # earlier sweeps' backups: not the app's files
print "after: $(du -sh $FIX/lib | cut -f1) in $FIX/lib:"
find $FIX/lib -maxdepth 3 | sed "s|$FIX/lib/||" | sort
