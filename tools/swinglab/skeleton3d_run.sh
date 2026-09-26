#!/bin/zsh
# skeleton3d corpus pass (docs/design/swing_3d_viz_design.md §8.2) — the 24 two-camera swings
# (2026-07-04 s1–15, taped 7-iron; 2026-06-11 s1–9, bare wedge) through swinglab_run with the
# pinned face-on and DTL poses, 4 at a time. One CONFIG per call:
#
#   tools/swinglab/skeleton3d_run.sh <outroot> <config> [height-m]
#
#   full        production defaults
#   control     skeleton3d.enabled = false         (parity: nothing else may differ)
#   visionOnly  no IMU / HackMotion terms          (the grade against HackMotion)
#   faceOnly    skeleton3d.useDtl = false          (the face-on-only tier; the DTL pose is still loaded)
#   noLimits | noContact | noShaft | noClubhead | noGrip | noSmooth | lengthsFitted | camerasFrozen
#
# Outputs <outroot>/<config>/<id>/{result.json,runner.log}. Grade with skeleton3d_grade.py;
# DELETE the run trees once its CSVs are in docs/research/data/skeleton3d/.
set -u
REPO=${REPO:-$(cd "$(dirname "$0")/../.." && pwd)}
BIN=${SWINGLAB_BIN:-$REPO/build/tools-parity-ninja/swinglab_run}
C=${CORPUS:-/mnt/swingdata/corpus}
ROOT=$1
CFG=$2
HEIGHT=${3:-}
OUT=$ROOT/$CFG
mkdir -p $OUT

params=""
dtl=1
case $CFG in
  full)          ;;
  control)       params='{"skeleton3d.enabled": false}' ;;
  visionOnly)    params='{"skeleton3d.useImu": false, "skeleton3d.useHm": false}' ;;
  faceOnly)      params='{"skeleton3d.useDtl": false}' ;;
  noLimits)      params='{"skeleton3d.useLimits": false}' ;;
  noContact)     params='{"skeleton3d.useContact": false}' ;;
  noShaft)       params='{"skeleton3d.useShaft": false}' ;;
  noClubhead)    params='{"skeleton3d.useClubhead": false}' ;;
  noGrip)        params='{"skeleton3d.useGrip": false}' ;;
  noSmooth)      params='{"skeleton3d.useSmooth": false}' ;;
  lengthsFitted) params='{"skeleton3d.fitLengths": true}' ;;
  camerasFrozen) params='{"skeleton3d.fitCameras": false}' ;;
  *) echo "unknown config $CFG"; exit 2 ;;
esac
if [[ -n $params ]]; then echo $params > $OUT/params.json; fi

one() {
  id=$1; sess=${id%%__*}; sw=${id##*__}
  mkdir -p $OUT/$id
  args=(--pose $C/pose3/$id.json)
  (( dtl )) && args+=(--dtl --dtl-pose $REPO/build/dtl_keep/pose/${id}__dtl.json)
  [[ $sess == 2026-07-04* ]] && args+=(--bands 308,362,560,758,808,854 --club-length-mm 940 --hosel-mm 882)
  [[ -n $HEIGHT ]] && args+=(--height-m $HEIGHT)
  [[ -n $params ]] && args+=(--params $OUT/params.json)
  PINPOINT_LOG_STDERR=1 $BIN $C/swings/$sess/$sw --out $OUT/$id $args > $OUT/$id/runner.log 2>&1
  echo "$CFG $id exit=$?"
}
ids=()
for i in {1..15}; do ids+=(2026-07-04_Mark-Liversedge_Wrist_01__swing_$(printf %04d $i)); done
for i in {1..9};  do ids+=(2026-06-11_Mark-Liversedge_Wrist_01__swing_$(printf %04d $i)); done
n=0
for id in $ids; do one $id & n=$((n+1)); (( n % 4 == 0 )) && wait; done
wait
