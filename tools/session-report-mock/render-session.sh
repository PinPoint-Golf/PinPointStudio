#!/bin/zsh
# render-session.sh <name> <session folder in the library> <model dump json> [coach's note]
# Assembles the report map for ANY session and prints the mock register through the app's own PDF path, then
# extracts the pages and runs the folio-overflow check. Output (on the share, never the repo):
#   /mnt/swingdata/scratch/session-report/out/<name>/{report.json, stills/, session-report-<name>.pdf, pages/}
set -u
HERE=${0:A:h}; NAME=$1; SESSION=${2%/}; DUMP=${3:A}; NOTE=${4:-}
OUT=/mnt/swingdata/scratch/session-report/out/$NAME; mkdir -p $OUT
/opt/homebrew/bin/python3 $HERE/assemble.py --session $SESSION --dump $DUMP --note "$NOTE" --out $OUT/report.json || exit 1
$HERE/run_probe.sh $HERE/print_mock.qml $OUT/render.log --probe-register $HERE/SessionReportMock.qml \
    --probe-json $OUT/report.json --probe-out $OUT/session-report-$NAME.pdf --probe-dpi 300
grep "PROBE:" $OUT/render.log | sed 's/^WARNING: //' | tail -1
grep -iE "SessionReportMock.*(error|TypeError|undefined)" $OUT/render.log | head -5
rm -rf $OUT/pages && mkdir -p $OUT/pages && /opt/homebrew/bin/python3 $HERE/pdfimgs.py $OUT/session-report-$NAME.pdf $OUT/pages 1240
/opt/homebrew/bin/python3 -W ignore $HERE/overflow_check.py $OUT/pages
