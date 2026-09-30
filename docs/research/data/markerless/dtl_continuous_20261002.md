# The down-the-line track, layer 1: holes inside the sighted bands — built, gated, and what it cannot reach

**Status:** results, 1 Oct 2026 (the doc is dated for the 2 Oct filing). Implements
`docs/design/dtl_continuous_track_design_update.md` §3.1 in full, and builds the calibration and
projection machinery §3.2 needs, behind flags, ahead of the protocol session
(`docs/validation/two_camera_capture_protocol.md`), which **has not been recorded**.

Every number below is reproducible from a run root under `/mnt/swingdata/scratch/dtl-continuous-20261002/`
with the commands in §7 (`build/run-me/dtl-continuous-grade.sh` runs them). The `c_off` tree, proven
byte-identical to `c_base`, is already deleted; `c_base`, `c_on` and the `s7_*` roots (1.4 GB) stay
until Mark has read this, then go.

## 0. Summary

| | |
|---|---|
| Built | six §3.1 rules in `dtl_shaft_decide` / `dtl_shaft_post`, each a `shaft.dtl.*` switch with a bit-identical OFF; three new tiers persisted in `clubDtl`; `kDtlShaftStageVersion` 1 → 2; the DTL tile's staleness window tied to the tier |
| Gate, 21 DTL corpus swings, studio Release, pinned poses | see §3 — the coverage threshold the brief set (≥ 0.85 on mid-backswing and delivery) is **not reached**; measured coverage is unchanged by construction, drawn coverage rises ~+0.10; no truth frame worse; `publishedInEndOn` 0; fused planes moved 0.00°; face-on θ and metrics identical on 21/21 |
| Defaults | all six rules on — the HELD tier by Mark's decision after reading the gate, to be judged in the app (§3.4) |
| Step 2 | the stick-clip camera solve, `camera_pose_sticks.h` + `tools/shaftlab/dtl_calib_solve.py`, recovers a synthetic two-camera bay to machine precision; fusion reads yaw/pitch/**roll**/offset/`calibrated`; `swinglab_run --dtl-calib`. **No real clips exist to run it on** |
| Step 3 | the 3-D synthetic shaft into the DTL tile, `dtl_shaft_synth3d.h`, dark by default, unit-tested on synthetic planes (θ_D to 0.003°), previewed on 07-04 s7 with the uncalibrated camera and labelled so |

## 1. What the corpus said before anything was built

Measured on the existing `robust-20261001/corpus_v11` runs (live DTL pose), all 21 swings, before
any code changed — this is what set the expectation for the HELD tier and is why the brief's 0.85
was never going to be reached by §3.1 alone:

| band family | bands | frames | measured | cov | fillable by a bounded HELD (≤ 6 frames, both sides measured) | cov if filled | leading UNSEEN | trailing UNSEEN |
|---|---|---|---|---|---|---|---|---|
| address | 21 | 2656 | 2655 | 1.00 | 1 | 1.00 | 0 | 0 |
| mid-backswing | 24 | 895 | 624 | 0.70 | 101 | 0.81 | 2 | **168** |
| delivery | 29 | 469 | 311 | 0.66 | 36 | 0.74 | **107** | 6 |
| impact | 20 | 283 | 262 | 0.93 | 6 | 0.95 | 1 | 14 |

The holes a HELD tier can honestly fill (between two measurements of one band) are a third of the
deficit. The rest is **tails**: the mid-backswing band's last 6–17 frames, where the clubhead leaves
the top of the frame toward P3.7 (design §1.4) and the run is refused as too short or as the sweep's
floor; and the delivery band's first 3–10 frames, at ρ̂_D 0.5–0.6, where the snap is skipped
(`snapRhoMin` 0.60) so the length is `rend` and `rend` sits at its 98 px floor. Neither is a hole
between measurements; coasting into them is the PRED-at-impact failure the tracker design refused,
and this build refuses it too. Also counted there: 1,078 of 2,063 OCCLUDED frames were end-on by
the schedule, 506 length-source switches and 713 jumps > 40 px between consecutive published
frames, and exactly three post-P8 published corridor escapes (07-04 s4, θ ≈ 2π).

## 2. One swing first: 07-04 s7 (Mac, pinned poses, `s7_off` vs `s7_on`)

With the pinned DTL pose s7 differs from the corpus_v11 run in one important way: the impact band
(`P6.4→P7.8`, 12 frames there) does not exist. It is two five-frame sighted runs at ρ̂_D 0.73–0.97,
separated by three END_ON frames of schedule flicker, and the six-frame minimum threw both away
(`s7_off`: "sighted run of 5 frames is shorter than 6"). That is the case the design's "shorter
minimum run near band edges" is for, and it fixed the reading of the rule: ground (b) of
`dtl_shaft_bands.h` admits a short run whose **own** ρ̂_D median clears 0.70 — a fully sighted club
whose neighbours flickered is not a flicker at the threshold.

| | s7 OFF | s7 ON |
|---|---|---|
| bands | addr→P1.8, P2.3→P3.7, P4.7→P5.7, P9.8→P9.9 | + P6.4→P6.9 (edge), P8.0→P8.1 (edge), P9.7→P9.8 (edge) |
| mid-backswing cov measured / drawn | 0.71 / 0.71 | 0.71 / 0.79 (3 held) |
| delivery cov measured / drawn | 0.59 / 0.59 | 0.59 / 0.76 (3 held) |
| impact | none | 5/5 published; θ 60.0→56.0° against grip→ball 59.3→55.4° (≤ 0.7° off the ball line every frame) |
| after | 7/16 | 13/16 |
| OCCLUDED → END_ON by the schedule | – | 92 |
| OCCLUDED split | 87 OCCLUDED | 85 OCCLUDED_ROW, 2 OCCLUDED_WRIST |
| length jumps > 40 px between consecutive drawn frames | 14 | 5 |
| measured θ / grip on measured frames | identical | identical |
| fused planes back / down | 60.6° / 60.8° | 60.6° / 60.8° |

The held frames carry the band's Viterbi θ and sit between their neighbours (e.g. 4.206 → 4.215,
4.198, 4.224 → 4.232 rad). One admitted edge run publishes a single doubtful frame (P9.7→P9.8, θ 347°,
run 92 px) — the ladder judged it, no truth exists there, and it is reported rather than hidden.

## 3. The corpus gate (studio Release, 21 swings, pinned pose3 + pose2_dtl, base exe = main at 1fa5edf8)

Three runs of the same 21 swings: `c_base` (the main exe), `c_off` (this build, every rule off),
`c_on` (this build, every rule on). Base and this build differ only by the code in this package.

### 3.1 The OFF gate — bit-identical frames

`dtl_continuous_grade.py c_base c_off --identical`:

All 21 swings **OK**: `frames[]`, `bands[]` (minus the new `edge` key) and `summary` (minus
`configHash` and the new `continuous` block) byte-equal to the base exe's; face-on θ identical;
fused planes identical. The producer's version and hash differ by construction (`stageVersion` 2, the
nine new fields in the hash), and nothing else does.

### 3.2 The ON gate

`dtl_continuous_grade.py c_base c_on --truth /mnt/swingdata/corpus/dtl_heldout_truth`:

Per band family over the 21 swings (the per-swing table is `grade_on.md` in the run root, 90 rows):

| family | bands base→new | frames | measured base→new | cov base→new | held | drawn cov |
|---|---|---|---|---|---|---|
| address | 21→21 | 2656 | 2656→2656 | 1.00→1.00 | 0 | 1.00 |
| mid-backswing | 24→24 | 895 | 621→621 | 0.69→0.69 | 97 | 0.80 |
| delivery | 30→30 | 468 | 308→308 | 0.66→0.66 | 43 | 0.75 |
| impact | 20→21 | 286 | 268→270 | 0.94→0.94 | 3 | 0.95 |
| after | 6→15 | 79 | 36→47 | 0.46→0.59 | 1 | 0.61 |


`measured` is what the tracker MEASURED (RAY/BAND) and is the number every metric and the fusion
read; it cannot change by the HELD tier and did not. `drawn` adds the HELD frames. The impact and
after families gained measured frames from the band-EDGE rule (10 edge bands: one at impact,
06-11 s3 P6.6→P7.0, 2 of 3 frames published; nine after P8, publishing 0–5 frames each, 16 in all).

publishedInEndOn base 0 → new 0; lateEscapesRefused 10; endOnBeforeQuarantine 451; edgeBands 10
truth: paired 425→425, p50 0.25→0.25°, p90 0.65→0.65°, frames worse by >0.5°: 0
largest fused-plane move: 0.00°; face-on θ identical on 21/21 swings

Per band, drawn coverage on the bands of ten frames or more: mid-backswing 23 bands, min 0.42,
median 0.82, max 1.00, **6 at or above 0.85**; delivery 22 bands, min 0.00, median 0.81, max 1.00,
**8 at or above 0.85**.

The 425 truth pairs (07-04 s10–s15, address band) read p50 0.25°, p90 0.65° before and after, and
no frame moved by more than the grid's 0.5°. The truth is address-only, so "no truth frame worse"
constrains the one band no rule touches; the other bands' new frames are judged by construction
(the ladder's gates, unchanged) and, at impact, by the grip→ball line (§2).

### 3.3 Reading it against the brief

- **Coverage ≥ 0.85 on mid-backswing and delivery: NOT met.** Drawn coverage 0.69 → 0.80 and
  0.66 → 0.75 in aggregate; 6 of 23 and 8 of 22 bands reach 0.85. This is the §1 ceiling: a bounded
  HELD tier can fill only the holes between two measurements of one band, and the remaining 20–25 %
  of every mid band is its tail — the club out of the top of the frame toward P3.7 and the sweep's
  floor at ρ̂_D 0.5–0.6 coming out of the top. No rule in §3.1 addresses those, and the design says
  not to: they are the gaps that wait for the 3-D synthesis (§3.2), whose preview §5 shows drawing
  exactly there.
- **No band-truth frame worse: met** (0 of 425).
- **`publishedInEndOn` still 0: met** (21/21).
- **Fused-plane fits unchanged within 1°: met** (largest move 0.00°; the fusion reads measured
  frames only, and the edge bands' new frames fall on face-on-bridged instants that never enter a fit).
- **No face-on metric changed: met** (θ identical and metric counts equal on 21/21; `robust_compare.py`
  is the same test on the face-on side and reads the same result).
- Also measured: 451 frames relabelled END_ON that the old order called OCCLUDED (the whole top of
  the backswing on every swing), 10 post-P8 corridor escapes kept off the tile, the drawn length now
  one source on every drawn frame (s7: 14 → 5 length jumps > 40 px).

### 3.4 Defaults

Each rule by its own gate, as the brief asked:

| rule | key | gate | default |
|---|---|---|---|
| HELD tier | `shaft.dtl.held.enabled` | coverage ≥ 0.85 — **not met** (0.80 / 0.75); nothing worse | **on, by Mark's decision** (1 Oct, to be judged in the app on the re-analysed 4 July session; the gate as written would have left it off) |
| END_ON before quarantine | `shaft.dtl.endOnFirst` | relabels absences only; measured frames, truth, planes unchanged | on |
| quarantine cause in the tier | `shaft.dtl.quarantineCause` | same | on |
| band-edge runs | `shaft.dtl.edge.enabled` | 18 new measured frames earned by the unchanged ladder; truth, planes unchanged | on |
| one drawn length | `shaft.dtl.lenSchedule` | tier untouched, run kept as `runPx`; truth unchanged; flicker 14 → 5 on s7 | on |
| late escapes refused | `shaft.dtl.refuseLateEscape` | 10 refused, all post-P8; nothing measured lost | on |

The default configuration is therefore the `c_on` configuration exactly. Mark's decision, on reading
§3: turn HELD on and re-analyse the 4 July library session in place so the tier can be judged in the
app, and turn it back off from there if it does not earn its place. The tile's "current sample"
window is one and a half frame intervals while the tier is on.

## 4. Step 2 — the calibration solve (built, unit-tested, no data to run it on)

`src/Analysis/camera_pose_sticks.h` (pure std) solves a camera's bay pose from the protocol's B clips
the way `camera_calibration_design.md` §4.4 says to: rotation from stick **line directions** only
(world X from the two parallel floor sticks' back-projected plane normals, Y from the cross stick,
the vertical stick refining the one remaining angle by least squares), origin from the ball's image
point, scale from the stick ends and nothing else, signs from the declared target end. Then
`dtlRelativeToFaceOn` expresses the DTL camera in the fusion's frame as exactly the numbers
`fusion::dtlCamera(yaw, pitch, roll)` consumes, plus the offset. `tools/shaftlab/dtl_calib_solve.py`
is the same arithmetic in numpy for the offline solve on the clips.

Unit test `camera_pose_sticks_test` (synthetic bay: face-on on −Y at 2.0 m, DTL on −X at 2.2 m
displaced 0.7 m, yaws/pitches/rolls of 1.5–7°, ROI'd principal points, lines fitted through
projected stick points): both rotations recovered to < 1e-4°, both centres to < 1 µm, the vertical
stick's residual 4e-12°, the two scale witnesses agree to 4e-14; the DTL pose from the three floor
sticks alone (no vertical) is the same; a coincident stick pair, f = 0 and a missing scale witness
are refused. `dtl_calib_solve.py --selftest` prints the same numbers to four decimals (face-on yaw
87.6141, pitch 7.1189, roll 1.5000; DTL 6.0000 / 2.9836 / −2.0000; relative yaw 8.3859, pitch
2.9836, roll −2.0000, offset (−2.4604, 2.5998, −0.1000) m, inter-camera 81.614°).

Fusion (`shaft_fusion.h`): `dtlCamera(yaw, pitch, roll)`; `Config` gains `dtlRollDeg`, `dtlOffsetM`,
`calibrated`; `club3d.camera` now carries `calibrated, yawDeg, pitchDeg, rollDeg, offset` beside the
original `dtlYawDeg/dtlPitchDeg` keys (`kShaftFusionStageVersion` 3 → 4). `shaft_fusion_test` §R:
`dtlCamera(yaw, pitch)` ≡ `dtlCamera(yaw, pitch, 0)` bit for bit; a swing imaged by a DTL camera
rolled 6° fuses to a 60.0° plane when told the roll and is off by more than 1° when not. The record
enters through `ShotAnalysisJob::dtlCameraCalib` — `swinglab_run --dtl-calib calib.json` fills it;
**the app has no producer** (there is no session to calibrate). An explicit `shaft.fusion.*` override
still wins over the record.

**Unproven:** everything about real clips — the stick finder (`dtl_yaw_probe.py` finds one stick;
the solve wants four lines and the ball, hand-marked is acceptable), lens distortion (the solve is
pinhole; the card clips give the intrinsics), and whether the 07-04 corpus rig can be solved after
the fact (its clips have one stick and no card).

## 5. Step 3 — the 3-D synthetic shaft into the DTL tile (built, dark, previewed)

`src/Analysis/dtl_shaft_synth3d.h`: the face-on Layer C synth angle θ_F confines the shaft to the
face-on view plane; the phase's fused plane (address from the DTL view alone, backswing and
downswing from fusion) is the second constraint; the direction is their intersection (the same
`fuseOne` geometry with a plane in place of the second camera); projected through the DTL camera it
is a DTL line, drawn from the tracker's grip where that grip was not quarantined and from the
3-D skeleton's projected hands where it was. `DtlSynth3DStage` runs after `Skeleton3D`; the samples
are `clubDtl.synth3d` (flag `synthesized`, `summary.synth3d.preview` true when the camera was the
assumed-zero one), `club.synth3d` in the tile payload, drawn as a dim dashed line under the measured
one in `PpCameraFrame.qml`, read by nothing else. `shaft.dtl.synth3d.enabled` is **false**.

Unit test `dtl_shaft_synth3d_test`: a swing on a 50° backswing and 60° downswing plane imaged by a
DTL camera yawed 6°, pitched 3°, rolled −2° — told the planes and the camera, θ_D is reproduced to
0.003° and ρ_D to 5e-5 on 208 of 224 frames (the rest are past the held downswing plane or the
face-on track's end, by construction), including 12 frames where the true projection is end-on and
the tracker has nothing; blind to an 8° yaw the same swing is off by 12° at worst, which is what the
calibration buys; no plane / no anchor / a face-on gap / an ill-conditioned plane each give no sample.

Preview, 07-04 s7 (`s7_on_synth3d`, `--params in/params_on_synth3d.json`):
`s7_synth3d_preview.png` in the run root — P1..P8 tiles, measured line solid amber (grey where
HELD), the synthesis dashed cyan, a red banner that it is a PREVIEW through the uncalibrated camera.
189 of 528 frames synthesised (104 backswing plane, 49 downswing, 36 held downswing; anchors 172
tracker, 17 skeleton). At P3 and P5 the synthesis lies on the measured line; at P2, P4 and P6 it
draws the short stub the tracker cannot; at P1 it reads 61° against the measured 55°, i.e. the
unknown yaw of this rig, as expected. Two frames at P7/P8 have no sample: the face-on synth's
impact boundary leaves a bracket wider than 12 ms there — a known gap, not chased.

## 6. What is unproven, and what waits for the session

- The coverage threshold (§3.3). The tails are framing and the sweep floor, not holes; the design
  says to leave the gaps to §3.2, and this build does.
- Edge bands beyond the address band have no truth (the held-out truth is address-only). Their
  θ was checked against the grip→ball line at impact (§2) and against the neighbours; that is not
  a measurement of accuracy.
- The calibration solve on real clips; the app path for a calibration record; lens distortion.
- Everything in §3.2 that needs the camera: the preview's heading is wrong by the unknown yaw.
- The tile was probed by compile and payload only; the Held/synth3d drawing was not looked at live.

## 7. Reproduce

```
# the 21-swing gate (studio; the three run roots exist under the run root's c_* dirs)
python3 tools/shaftlab/dtl_continuous_grade.py /mnt/swingdata/scratch/dtl-continuous-20261002/c_base \
        /mnt/swingdata/scratch/dtl-continuous-20261002/c_off --identical
python3 tools/shaftlab/dtl_continuous_grade.py /mnt/swingdata/scratch/dtl-continuous-20261002/c_base \
        /mnt/swingdata/scratch/dtl-continuous-20261002/c_on --truth /mnt/swingdata/corpus/dtl_heldout_truth
# one swing (Mac): base rules off / on / on + synth3d preview
R=/mnt/swingdata/scratch/dtl-continuous-20261002; ID=2026-07-04_Mark-Liversedge_Wrist_01__swing_0007
build/tools-parity-ninja/swinglab_run /mnt/swingdata/corpus/swings/2026-07-04_Mark-Liversedge_Wrist_01/swing_0007 \
   --pose /mnt/swingdata/corpus/pose3/$ID.json --dtl-pose /mnt/swingdata/corpus/pose2_dtl/${ID}__dtl.json \
   --dtl --trace --params $R/in/params_on_synth3d.json --out $R/s7_on_synth3d
python3 tools/shaftlab/dtl_synth3d_preview.py /mnt/swingdata/corpus/swings/2026-07-04_Mark-Liversedge_Wrist_01/swing_0007 \
   $R/s7_on_synth3d --out $R/s7_synth3d_preview.png
python3 tools/shaftlab/dtl_calib_solve.py --selftest
ctest --test-dir build/tests -R "dtl_shaft_post_test|dtl_shaft_decide_test|dtl_shaft_bands_test|shaft_fusion_test|camera_pose_sticks_test|dtl_shaft_synth3d_test|swing_doc_test"
```
