#!/bin/zsh
# skeleton3d knob audit (swing_3d_viz_design.md §13.2 (D)): every prior width / model factor of the fit,
# one at a time, set NEUTRAL (a prior widened ×100 — effectively off; a factor set to 1), and the corpus
# rerun under the standard configs. A knob whose neutral setting moves no gate is a candidate for
# removal; one that moves a gate is kept and documented with what it prevents.
#
#   [BASE='"skeleton3d.x": v, …'] [CALIBDIR=<session pools>] tools/swinglab/skeleton3d_knob_audit.sh <outroot>
#
# Each knob's runs land in <outroot>/<knob>/<config>/…; grade each with
# skeleton3d_grade.py <outroot>/<knob> --branch (and the main mode), then compare against the baseline
# (<outroot>/baseline). Three knobs run at once (4 swings each). DELETE the trees once the CSV is kept.
set -u
REPO=${REPO:-$(cd "$(dirname "$0")/../.." && pwd)}
ROOT=$1
BASE=${BASE:-}
CONFIGS=(full dtlDropDown faceOnly)
# knob → its neutral value (from FitConfig's defaults: σ × 100, factors → 1). spineCoupleSigmaDeg is not
# here: the lean spine (§13.5) has no spine couples to hold together, so it is dead under the defaults.
typeset -A NEUTRAL
NEUTRAL=(
  baseline            ''
  cauchyC             '"skeleton3d.cauchyC": 1000'
  smoothAccRad        '"skeleton3d.smoothAccRad": 15000'
  smoothAccRootM      '"skeleton3d.smoothAccRootM": 400'
  fastFactor          '"skeleton3d.fastFactor": 1'
  limitSigmaDeg       '"skeleton3d.limitSigmaDeg": 200'
  gripSigmaM          '"skeleton3d.gripSigmaM": 2'
  contactSigmaM       '"skeleton3d.contactSigmaM": 1'
  spineFlexSigmaDeg   '"skeleton3d.spineFlexSigmaDeg": 700'
  pelvisTiltAccRad    '"skeleton3d.pelvisTiltAccRad": 150'
  wristSigmaDeg       '"skeleton3d.wristSigmaDeg": 3000'
  pronationSigmaDeg   '"skeleton3d.pronationSigmaDeg": 4500'
  clavicleSigmaDeg    '"skeleton3d.clavicleSigmaDeg": 1000'
  armRotSigmaDeg      '"skeleton3d.armRotSigmaDeg": 3500'
  planeSigmaDeg       '"skeleton3d.planeSigmaDeg": 1000'
  branchReleasePriorFactor '"skeleton3d.branchReleasePriorFactor": 1'
)
run_knob() {
  local k=$1 extra=$BASE
  [[ -n ${NEUTRAL[$k]} ]] && extra="${BASE:+$BASE, }${NEUTRAL[$k]}"
  for c in $CONFIGS; do
    EXTRA="$extra" zsh $REPO/tools/swinglab/skeleton3d_run.sh $ROOT/$k $c 1.8288 > $ROOT/$k.$c.log 2>&1
  done
  echo "knob $k done"
}
mkdir -p $ROOT
knobs=(${(k)NEUTRAL})
n=0
for k in $knobs; do
  run_knob $k &
  n=$((n+1)); (( n % 3 == 0 )) && wait
done
wait
echo AUDITDONE
