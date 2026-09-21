# Shaft fusion — the club in three dimensions from two uncalibrated cameras

*2026-09-21. Status: **built**, graded on 24 two-camera swings of one golfer, not
committed when this was written. Step 4 of the down-the-line thread
(`dtl_shaft_tracker_design.md` §5.10: "Fusion is a later document" — this is it).*

## 0. Summary

The face-on tracker publishes an image angle θ_F; the down-the-line tracker publishes
θ_D on the frames where it can see a shaft. Each angle confines the shaft to a plane
through that camera's view ray. **The shaft is where the two planes cross.** That needs
neither projected length, no calibration and no new vision — a few microseconds of
arithmetic on two tracks that were already in `swing.json`.

What it gave, on 07-04 (taped 7-iron, 15 swings) and 06-11 (bare wedge, 9 swings):

| | |
|---|---|
| Downswing shaft plane, 7-iron (s4–15) | **60.3° to the ground, sd 0.7°**, scatter off the plane 0.6–1.3° rms |
| …wedge (06-11) | 62.8°, sd 1.7°, scatter 1.3–4.8° rms |
| Sensitivity to the camera we did not calibrate | inclination moves **≤ 1°** over yaw ±15°, pitch 0–15°; the plane's **heading moves 1:1 with yaw** |
| The face-on ellipse plane it replaces in the club's de-projection | node bearing −57…+63°, ratio 0.65–0.98 swing to swing; fused: ratio 0.84–0.90, node level ±15° |
| Club node timing, 07-04 s4–15 | lead before impact sd **19.6 → 9.5 ms**; one swing (s14) moved from 0 ms — a peak on the domain's edge — to 59 ms, in family |
| Club peak rate, same swings | CV 7.4 % → 6.2 % (mean 1831 → 1877 °/s) |
| Cost | 1 of 24 swings LOST its club node (06-11 s1: σ_t 14 → 59 ms, verdict partial → unresolved) |
| Parity against the fusion-off control | 24/24: only `club3d`, `versions.shaftFusion`, `kinematicSequence` and the `clubAngularSpeed` series differ |

And what it **found**, which is the other reason it was built:

1. **07-04 swings 1–3: the DTL tracker's mid-backswing band is mirrored.** It publishes
   the shaft on the ball side of the hands (θ_D ≈ 295°) where swings 4–15 read ≈ 245°.
   That is the second root of the two-valued depth sign (`dtl_shaft_tracker_design.md`
   §4.1 (c)). The fused backswing reads 84–89° "plane" with 17–23° of scatter against
   51–62° and 5–11°. The stage says so: `backIncoherent`.
2. **Face-on is coasting through impact on 15–25 % of the frames DTL publishes there**
   — exactly where DTL is sharp. Those frames are fused against the face-on synth and
   marked `bridged`; they are checked against the plane and never fitted into it.
3. **The DTL projected length is not a measurement away from address**: 0.2–0.35 short of
   what the fused direction predicts in every band but the first (p90 |error| 0.38).
4. 07-04 s14: 7 frames where the two views disagree on which way the head points.

## 1. Geometry

Frame — **the cameras', not the golfer's**: X = face-on image-right, Z = up, Y = the
face-on view ray. The DTL camera looks along +X (image-right = −Y), then is yawed about
Z and pitched down by two config angles (default 0, 0). Nothing knows a handedness; for
a right-hander +X is the target.

```
n_F = d_F × (cos θ_F e_rF + sin θ_F e_dF)        n_D likewise
u   = ± (n_F × n_D) / |n_F × n_D|                 cond = |n_F × n_D|
```

The sign is taken from the view that sees more of the shaft; the other view votes, and a
lost vote is the `signDisagree` flag. `cond < 0.26` (view planes within 15°) is
`illConditioned`. The predicted projections ρ_F, ρ_D are written per frame so the
lengths stay available as a CHECK (identity ρ_F² + ρ_D² = 1 + u_z²).

**Time.** DTL frames lead face-on by ~3.2 ms, so θ_F is interpolated (unwrapped) to each
DTL instant, never indexed. A bracket wider than 12 ms is not a bracket.

**Planes.** Backswing (takeaway → top) and downswing (top → impact + 20 ms), each the
smallest eigenvector of the direction scatter matrix, fitted on frames that are
face-on-MEASURED on both sides and carry no flag. A plane is *offered* downstream only
when ≥ 8 frames and ≤ 5° rms. Only the downswing is asked to be planar: `offPlane` is a
per-frame downswing flag (> max(3·rms, 6°)); the backswing's scatter is a swing-level
finding.

## 2. What is claimed, and what is not

- **Claimed:** the plane's inclination to the ground; the 3-D direction per fused frame;
  the disagreement list.
- **Not claimed:** the plane's heading (swing direction). It is written, with
  `camera.calibrated: false`, and it is only as good as `shaft.fusion.dtlYawDeg`. The
  pair route measured the two cameras at 75–84°, not 90°.
- **Not done:** a full 3-D *track*. DTL is end-on at P2, the top and P6, so fused frames
  come in bands (9–19 in the downswing). The continuous product is the face-on angle
  de-projected through the fused plane — which is exactly what the sequence now does.
- **One direction only, still.** The stage reads both trackers and feeds neither.

## 3. As built

| File | |
|---|---|
| `src/Analysis/shaft_fusion.h` | NEW — pure std: cameras, `fuseOne`, `fitPlane`, `fuseTracks` |
| `src/Analysis/shaft_fusion_json.h` | NEW — `pinpoint.club3d/1`, `shaft.fusion.*` overrides |
| `src/Analysis/wrist_analyzer.cpp` | `ShaftFusionStage` after `DtlShaft`, both profiles; hands the offered plane to `KinematicSequence` |
| `src/Analysis/segment_rates.{h,cpp}` | `fusedClubPlane` input; the CLUB de-projects through it, route `faceOn+dtl`; the lead arm is not moved |
| `src/Analysis/swing_analysis.h`, `analysis_versions.h`, `src/Export/swing_doc.cpp` | `shaft3d`, `kShaftFusionStageVersion = 1`, `analysis.club3d` + `versions.shaftFusion` (absent on single-camera swings) |
| `src/Gui/review/PpChartPlot.qml` | an unmeasured run that reaches a series' end is drawn faint-solid, not dashed (§4) |
| tests | `shaft_fusion_test` (26 checks, NO_QT), `segment_rates_test` §10, `tst_chart_presets` test_017 |
| tools | `tools/shaftlab/fuse_probe.py` (K0, reads swing.json), `tools/shaftlab/fuse_grade.py` (run vs control) |

Numbers: `docs/research/data/fusion/fuse_grade_20260921.md`. The C++ stage and the
Python probe agree to 0.1° on all 15 swings of the 07-04 review copy.

## 4. The chart: outside is not a dash

The sequence producer masks every sample outside transition → impact, and the plot drew
every masked run dashed. So the whole backswing and follow-through of every curve wore
the stroke that means "bridged, not measured", and the chart read as mostly untrusted.
A run that reaches the first or last sample joins nothing; it is now a dimmed, full-width solid
line. Dashes are left for interior bridges. Probed offscreen on 07-04 s8: every series
is `outside / solid / outside`, no dashed run. `PpSegmentBrush` still mirrors the old
rule and was left alone.

## 5. Owed

1. **Fix the mirrored band in the DTL tracker** (swings 1–3). The fusion detects it; the
   sign table of §4.1 (c) is where it belongs.
2. **Measure the DTL camera's yaw**, so the heading can be claimed — from the new video's
   placement, or from the calibration thread.
3. 06-11 s1's lost club node: why σ_t quadrupled through the fused plane.
4. The lead arm still de-projects through the face-on ellipse. It does not swing on the
   shaft's plane; its own plane wants its own measurement (DTL pose).
5. No rate truth exists. Repeatability improved; accuracy is not graded.
6. The backswing trunk curves outside the window are pose noise drawn faintly, and the
   club's P1→P2 ramp is synth. Both are still drawn.

## 6. Later the same day: the address plane, `swingPlane`, and the camera's yaw

- The fusion now also reads the **address shaft plane** from the DTL view alone (median over
  the DTL frames published up to address; `club3d.address`, `deliveryVsAddressDeg`; stage
  version 2). 07-04: 54.0–55.5° at address, delivered **+6.0° ± 1.2** above it; 06-11 +3.2°.
  Published as the `swingPlane` series — `dtl_posture_design.md` §1.
- Owed item 2 has a first answer: the alignment stick puts the 07-04 DTL camera 4–9° turned
  toward the golfer depending on its distance, and shows it was MOVED between s3 and s4 —
  `dtl_posture_design.md` §4.

