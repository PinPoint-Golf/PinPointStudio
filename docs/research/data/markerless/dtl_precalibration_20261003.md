# Before the calibration: the four §3.2a items — built, gated on the DTL corpus, and what each turned out to be

**Status:** results, 3 Oct 2026 (unattended session), committed 1 Oct 2026 after Mark read it; A and the
synthetic line turned on by his decision (§0a, §3.4).
Implements `docs/design/dtl_continuous_track_design_update.md` §3.2a items A–D in that order. The
protocol session (`docs/validation/two_camera_capture_protocol.md`) **has not been recorded**: nothing
below claims a heading; every angle that depends on the DTL camera's yaw is said to.

Every number is reproducible from a run root under `/mnt/swingdata/scratch/dtl-precalib-20261003/`
with the commands in §7 (`build/run-me/dtl-precalib-grade.sh` runs them all). The per-swing grader
output for every gate is in the repo beside this file (`dtl_precalibration_20261003_grade_{off,A,C,D}.md`);
the run trees themselves (five × 24 swings, ~4 GB) were deleted at the end of the session once this
document held the numbers, leaving `in/` (the ids and params) and the `grade_*.md` copies on the share;
§7's studio recipe regenerates them in ~15 minutes.

## 0. Summary

| item | what it is | one swing (07-04 s7) | corpus gate | default |
|---|---|---|---|---|
| **A** η(t), the out-of-plane curve | one smooth curve per swing through the fused frames' `oopDeg`; the DTL synth de-projects the face-on angle η off the plane instead of on it | the curve follows the frames to 1.5° rms; leave-one-band-out prediction ≈ in-plane (the prior); band-edge hold-out 2× better; a slope across the top is 2× WORSE | §3 — LOBO gate **not met** on 24 swings (mid-backswing 6.8 → 7.7° p50, delivery 1.3 → 1.4°), and not for want of neighbours: 49 of 72 gaps are inside the prior's reach. η is not continuous across an end-on gap on this golfer. Under data the curve is 2–12× nearer the track; OFF is byte-identical; planes and metrics unchanged | **on, by Mark's decision** (`shaft.fusion.eta.enabled`, with the synthetic line `shaft.dtl.synth3d.enabled`), to be judged in the app |
| **B** the skeleton's camera as the fusion seed | cross-check first | skeleton DTL yaw 12.6° (07-04 median) vs the stick probe 9.0–9.6° at the skeleton's own focal length: **+3.6° apart, ranges disjoint**; 06-11 has no usable stick | §2 — **stopped at the cross-check**, no code | none |
| **C** the DTL anchor at impact | on a bridged frame, the DTL view plane ∩ the phase plane | **degenerate**: down the line the DTL view plane at impact IS the swing plane (cond 0.06–0.08); where the anchor is even possible (2 of 15 frames) it is 33° from the bridge and the bridge is nearer the neighbours | §4 — 243 bridged frames, 167 refused; within ±60 ms of impact 2 of 71 anchored; where anchored the bridge is nearer the neighbours 69 : 7 | **off** (`shaft.fusion.dtlAnchor.enabled`); closed |
| **D** the mirrored backswing band re-read | when the backswing fit is incoherent, reflect non-address DTL bands about the image vertical, greedily, keep only if coherent and facing the downswing plane's way | s7 coherent → untouched; s2 rms 20.1 → 10.7°, s3 18.8 → 4.6° (Python probe on the corpus_v11 runs), s1 stays incoherent → unchanged | §5 — **met**: 07-04 s2 20.9 → 12.0° (82.7 → 54.8°), s3 18.0 → 4.4° (87.3 → 57.9°), headings within 1° of the downswing plane's; s1 untouched; 0 of 21 coherent swings changed; DTL track untouched. The first run reflected the wrong (mirror-image) band on s2 — fixed by the downswing plane's side | **on** (`shaft.fusion.reflectBands.enabled`) |

`kShaftFusionStageVersion` 4 → 5 (once). `club3d` gains `eta`, `items`, and the (C)/(D) summary keys;
frames gain `etaDeg` / `anchorCond` / `uBridged` only where an item wrote them, so a run with every
item off writes the frames the previous version wrote, key for key (§3.1 of this file's gate).
`tools/shaftlab/fusion_geom.py` is the fusion's geometry in numpy — the grader re-fits η
leave-one-band-out from a run root with it, and the C++ knots agree with it to 0.0000°.

### 0a. Per item: is it live, what proves it, what resolves it

| item | live? | what the measurements prove | what would resolve it |
|---|---|---|---|
| **A** η(t) | **Yes** — on by Mark's decision after reading this, together with the synthetic line (`shaft.fusion.eta.enabled`, `shaft.dtl.synth3d.enabled`); the gate below was NOT met | The fit is right (C++ = numpy re-fit to 0.0000° on 24 swings) and under its own frames it is 2–12× nearer the track than in-plane (address 4.4 → 0.3°, mid-backswing 6.8 → 2.9°). The design's gate — predicting a held-out band from the others — fails on every family (mid 6.8 → 7.7°, delivery 1.3 → 1.4°) even where a neighbour is within reach, and a slope across the top is 2–3× worse: η is not continuous across an end-on gap on this golfer. OFF is byte-identical; planes, metrics, truth unchanged. | The gate as written cannot pass on this geometry, so the decision was Mark's, not a parameter's: on, to be judged as the drawn line in the app on the re-analysed 4 July session (the synthetic line is a PREVIEW through the assumed-zero camera until the protocol session; its heading is off by the unknown yaw). The clean session's flat/steep backswings say whether the reversal through the top is universal. |
| **B** skeleton camera | **No** — stopped at the cross-check, no code | Skeleton DTL yaw 12.6° median (11.4–14.4°) on 07-04 s4–15; the stick probe at the skeleton's own focal length 9.0–9.9°; Δ +3.6°, ranges disjoint. 06-11 has no usable stick. Conventions agree, so the gap is real. | The protocol session's stick + card clips seen by BOTH cameras (`camera_pose_sticks.h`, proven to machine precision on a synthetic bay): they fix the face-on squareness, the principal point and the yaw together, to ≤ 1°, and say which instrument is wrong. Nothing on the existing footage can. |
| **C** DTL anchor at impact | **No** — built as a diagnostic, off, closed | Geometry: a down-the-line camera looks along the swing plane, so the DTL view plane at impact IS the swing plane and their intersection is no direction. Corpus: within ±60 ms of impact 2 of 71 coasted frames can be anchored at all (conditioning p50 0.056); over the 76 frames anchored anywhere, the existing bridge is nearer the neighbouring measured frames 69 : 7. | Nothing inside the fusion. An impact direction owing the face-on bridge nothing would come from the 3-D ball (the head is at the ball at impact: skeleton grip → ball) or from the calibrated camera lifting the view ray off the plane by the true yaw (~9–12°, enough for a poor anchor, not a good one). Both are separate work. |
| **D** reflected backswing band | **Yes** — on by default (`shaft.fusion.reflectBands.enabled`) | 07-04 s2 20.9 → 12.0° rms (82.7 → 54.8°), s3 18.0 → 4.4° (87.3 → 57.9°), headings within 1° of each swing's own downswing plane; s1 cannot be repaired and is left exactly as it was; 0 of 21 coherent swings changed; every downswing plane identical; the DTL track untouched. Caveat: s2 lands at 11.98° against a 12° threshold. | Commit, then re-analyse the 4 July library session under the version gate (s2/s3 gain the backswing `swingPlane` half). The reflected band's absolute angle stays unproven until DTL truth beyond address exists (protocol §4 item 2); the tracker's sign table is where the mirror should eventually be prevented rather than repaired. |

## 1. One swing first: 07-04 s7, from the last gate's run root

Before any code, the questions were put to `dtl-continuous-20261002/c_on/…swing_0007` (the §3.1 build,
pinned poses) with the scratch probes that became the grader's `--items` code. What the fused frames say
(`club3d.frames`, `oopDeg` = the signed angle off the phase plane; the backswing plane is 60.6° with
5.1° rms, the downswing 60.8° with 3.2° rms over 9 frames):

| band | frames fused | oopDeg over the band | note |
|---|---|---|---|
| address → P1.8 | 40 (9 bridged at the hold) | +6.2 → −0.2 | drifts as the club leaves address: the address plane is not the backswing plane |
| P2.3 → P3.7 | 27 (last 2 bridged) | +4.7 → −1.6 → **+10.6 → +13.5** | the lift: the backswing is not one plane, and it is steepening at the band's end |
| P4.7 → P5.7 | 10 (last bridged) | −0.75 → −0.2 → (+8.7 one frame) → −2.5 | on the downswing plane to ~2° |
| P6.4 → P7.8 | 4, all bridged | −1.7 → +2.3 | the impact band: face-on coasts, DTL measures |

The gap between the mid-backswing band's end (3.06 s, η = +13°) and the delivery band's start (3.36 s,
η = −0.75°) is 300 ms of end-on: nothing in this swing's DTL data predicts one from the other.

## 2. (B) The skeleton's camera — stopped at the cross-check

The brief: put skeleton3d's per-swing DTL yaw and its session median beside `dtl_yaw_probe.py`'s range
for 07-04 s4–15 and 06-11 s1–9; agreement within ~3° makes the skeleton the fusion's seed, disagreement
stops B. The skeleton's `psiD` and the fusion's `dtlYawDeg` share a convention (checked in
`skeleton3d_fit.cpp` `camFrame` against `shaft_fusion.h` `dtlCamera`: yaw > 0 turns the view ray from +X
toward +Y, pitch > 0 looks down, roll > 0 turns the image clockwise), so the two numbers are comparable
as they stand.

The stick probe reports a vanishing column and leaves the focal length to the reader (its table runs
4.4–9.4° over an assumed 2–4 m). The skeleton fits its own focal length and distance, so the fair
comparison evaluates the probe **at the skeleton's f_D** (both assume the principal point at the ROI's
centre, 256 of 512 px; the probe's x_vp at the 40 % horizon row):

| swing | skeleton yaw ψ_D | pitch | roll | f_D px | camera distance m | stick x_vp | probe yaw at f_D | Δ |
|---|---|---|---|---|---|---|---|---|
| 07-04 s4 | 12.5 | 0.9 | 0.1 | 724 | 2.20 | 371 | 9.0 | +3.5 |
| 07-04 s5 | 12.3 | 1.9 | −6.0 | 704 | 2.11 | 371 | 9.3 | +3.0 |
| 07-04 s6 | 11.4 | 0.7 | −1.2 | 713 | 2.14 | 375 | 9.5 | +1.9 |
| 07-04 s7 | 12.2 | 0.9 | 1.9 | 692 | 2.15 | 373 | 9.6 | +2.6 |
| 07-04 s8 | 12.4 | 0.3 | 2.0 | 715 | 2.16 | 371 | 9.1 | +3.2 |
| 07-04 s9 | 12.4 | 1.9 | −4.5 | 720 | 2.13 | 371 | 9.1 | +3.4 |
| 07-04 s10 | 14.4 | 0.3 | 2.0 | 704 | 2.07 | 370 | 9.2 | +5.2 |
| 07-04 s11 | 13.1 | −0.2 | 4.8 | 696 | 2.11 | 369 | 9.2 | +3.8 |
| 07-04 s12 | 13.5 | 3.0 | −10.5 | 681 | 2.04 | 371 | 9.6 | +3.9 |
| 07-04 s13 | 13.9 | 0.3 | 0.5 | 702 | 2.08 | 371 | 9.3 | +4.6 |
| 07-04 s14 | 13.8 | 2.7 | −8.3 | 712 | 2.11 | 372 | 9.3 | +4.6 |
| 07-04 s15 | 12.7 | 0.5 | 5.5 | 718 | 2.17 | 370 | 9.0 | +3.7 |
| **07-04 s4–15** | **median 12.6 (11.4–14.4)** | | | 708 | | 365–379 | **9.0–9.9 at f 690–724** | **median +3.6 (+1.9…+5.2)** |
| 06-11 s1–9 | median 14.5 (12.5–16.1) | −0.4…3.0 | −7.0…1.8 | 783–904 | 2.09–2.44 | the probe finds a stick on 4 of 9, all > 5° off vertical (tilts +21, −10, +11, +9°: not the same line), summary range 96–445 px | — | — |

Skeleton numbers: `analysis.skeleton3d.camera` of the `dtl-continuous-20261002/c_on` runs
(`kSkeleton3DStageVersion` 3, the version in this build); probe: `tools/shaftlab/dtl_yaw_probe.py` on
`corpus/swings/2026-07-04_…` (the tool now also accepts 06-11's `Down-the-Line.mp4` name; that is the
one line changed in it).

**Verdict: disagreement.** The two ranges do not overlap on 07-04 (skeleton 11.4–14.4°, probe
8.9–9.9° at the skeleton's f; 6.7–8.7° at the probe's own 2.0–2.5 m), the median gap is 3.6°, and 06-11
offers no stick to check against at all. Per the brief B stops here: no code reads the skeleton's camera,
`ShotAnalysisJob::dtlCameraCalib` keeps its one producer (`--dtl-calib`, measured), and nothing here
seeds the fusion.

What the disagreement is a finding about — three candidates, none settled by this data:

1. **They do not measure the same angle.** The probe measures the optical axis against the STICK (the
   target line on the mat); the skeleton measures the DTL view ray against +X, the face-on camera's
   image-right. They coincide only if the face-on camera is square to the target line. The stick runs
   horizontally in the face-on frame to the eye, not to a degree; a 3° face-on yaw is not visible there.
2. **The skeleton's yaw is entangled with its other camera terms.** Its pitch/roll trade against the
   face-on pitch by up to 11° (`skeleton3d_pool.h` note), its focal lengths scatter ±3–10 %, its lengths
   are frozen to the height because perspective is what its pinhole model knows least
   (`fitLengths` note). A yaw with an unstated uncertainty is what the cross-check was for; ±1.5° swing
   to swing is what it shows on one rig that did not move.
3. **The probe's principal point.** Both assume cx at the ROI centre; a 40 px ROI offset is 3° at
   f ≈ 700. Neither the clips nor the pose files record the ROI.

What settles it: the protocol's stick and card clips seen by BOTH cameras (`camera_pose_sticks.h`
recovers the pose to machine precision on a synthetic bay, `dtl_continuous_20261002.md` §4), which fix
the face-on squareness, the principal point and the DTL yaw at once, to the ≤ 1° the design asks for.

## 3. (A) The out-of-plane curve η(t)

### 3.1 Built

- `shaft_fusion.h`: `Config::Eta` (`shaft.fusion.eta.*`: `enabled` false, `knotMs` 30, `lambda` 4,
  `priorReachMs` 150, `priorFar` 1, `priorNear` 1e-3, `maxAbsDeg` 25); `fitEtaCurve` — knot values on a
  uniform grid minimising the data term (piecewise-linear hat functions over the face-on-measured,
  unflagged frames with a phase plane), a second-difference smoothness penalty λ and a zero prior on every
  knot with no frame within the reach; one dense Cholesky solve (36–60 knots); `EtaFit::at` evaluates a
  Catmull-Rom spline through the knot values (C¹, constant beyond the ends). The grid runs one reach past
  the fusion window (impact + 20 ms) so the curve decays to in-plane over the held-plane frames the synth
  still draws after impact. `deproject` (moved here from `dtl_shaft_synth3d.h`, same operations, same
  bits) and `deprojectEta`: the direction in the camera's view plane that sits η off the plane —
  `sin φ = sin η / cond`, refused where |sin η| > cond.
- `dtl_shaft_synth3d.h`: `synthesize(…, const fusion::EtaFit *eta)`; `Config::useEta`
  (`shaft.dtl.synth3d.useEta`, true); samples carry `etaDeg`. A null curve is the pre-η synth bit for bit.
- JSON: `club3d.eta {fitted, knotsUs, values, near, n, rmsDeg, knotMs, lambda, priorReachMs, priorFar,
  priorNear, maxAbsDeg}`, `club3d.frames[].etaDeg`, `clubDtl.synth3d[].etaDeg`.
- Tests: `shaft_fusion_test` §A — a swing leaving its plane by a 6° sinusoid, imaged by both cameras:
  the curve follows the frames' out-of-plane angle to 0.32° worst; the face-on angle rotated by the
  frame's own η IS the fused direction (155 frames to 0.0000°); η = 0 is `deproject` bit for bit; with a
  120 ms and a 400 ms gap the curve still follows inside the bands (0.32°) and reads −0.02° in the middle
  of the 400 ms gap where the true direction is 8.2° off the fitted plane — the prior, not a slope; OFF
  leaves no curve and the same direction bits. `dtl_shaft_synth3d_test` §5 — an out-of-plane swing (±7°)
  through a yawed, pitched, rolled DTL camera: told the curve, θ_D is reproduced to 0.28° (the spline's
  own interpolation); in-plane the same swing is off by 25.6° at worst; `useEta` off is the in-plane
  synth bit for bit.

### 3.2 One swing (07-04 s7, Mac, `m_off` vs `m_A`)

`dtl_continuous_grade.py m_off m_A --items`. The curve: 65 frames, 36 knots, 1.54° rms; the C++ knots
against `fusion_geom.fit_eta`'s refit: 0.0000° at the knots and on the frames. |Δθ_D| on the tracker's
MEASURED frames, the face-on angle de-projected through the phase plane, p50 / p90 (°):

| family | frames | in-plane | η leave-one-band-out (reach 150 ms) | η LOBO, reach 400 ms | η full fit (in-band) | edge frames | edge in-plane | edge η |
|---|---|---|---|---|---|---|---|---|
| address | 31 | 4.75 / 6.31 | 4.74 / 6.32 | 1.22 / 7.29 | 0.19 / 0.67 | 12 | 4.53 / 6.52 | 2.97 / 3.66 |
| mid-backswing | 25 | 5.17 / 12.36 | 5.40 / 12.30 | 9.91 / 17.40 | 1.39 / 3.67 | 12 | 9.18 / 14.07 | 4.49 / 7.32 |
| delivery | 9 | 1.04 / 4.08 | 0.84 / 4.07 | 5.55 / 6.99 | 0.75 / 3.82 | 9 | 1.04 / 4.08 | 0.84 / 4.07 |

Reading it:

- **Leave-one-band-out ≈ in-plane.** With a band held out, no other band lies within the prior's reach of
  most of its frames (the gaps are 130 and 300 ms), so the prediction is the zero prior — the in-plane
  answer — and the gate's "beats in-plane" cannot be met on this geometry. That is the design's own
  choice ("pulled to zero … so a gap coasts to in-plane rather than extrapolating a slope") and the
  reach-400 column says why it is the right one: letting the curve carry a slope across the top is 2×
  worse on the mid-backswing (9.9° vs 5.2°) and 5× worse on delivery (5.6° vs 1.0°). The mid-backswing
  band ends steepening at +13° and the delivery band starts at −1°: the lift does not continue through
  the top, it reverses.
- **Where the curve has data it is the track.** The in-band fit reads 0.2–1.4° p50 against 1.0–5.2°
  in-plane; the synth3d line on the measured frames (below) tells the same story. That is not a hold-out,
  it is what the tile would draw next to the measured line — and the tile does not draw the synth where
  the tracker measured.
- **The band edges are the real question**, because that is where the synth is drawn: the last/first
  six frames of each band predicted from the rest read 2× better with the curve on the address and
  mid-backswing bands (4.5 → 3.0°, 9.2 → 4.5°) and the same on delivery. A synth continuing a band by
  40 ms is nearer the tracker with η than without.

The synth3d line against the tracker on MEASURED frames, base (in-plane) → new (η), p50 / p90:
address 5.64 / 6.52 → 0.18 / 0.49; mid-backswing 5.34 / 14.49 → 1.40 / 4.67; delivery 1.64 / 3.67 →
1.40 / 3.23; impact (4 bridged frames, not in the fit) 1.97 / 2.34 → 2.29 / 3.66; after P8 (held plane)
1.93 / 6.52 → 3.10 / 4.76. The two families the curve does not fit on are a little worse: the curve's
value there is its end-of-window value decaying to zero, and this s7 run predates the one-reach
extension of the grid (§3.1); the corpus run has it.

### 3.3 The corpus gate (studio, Release, 24 swings: the 21 + 07-04 s1–3 for (D), pinned pose3 + pose2_dtl)

Base exe = main at b53d36f9 rebuilt on the studio (`build_main.cmd`); `p_off` = this build with every
item off; `p_A` = this build with `shaft.fusion.eta.enabled`. Every run has `shaft.dtl.synth3d.enabled`
so the synth line exists to compare.

**The OFF gate** (`dtl_continuous_grade.py p_base p_off --identical`): **24/24 OK** — `clubDtl`
frames, bands and summary, `club3d` frames (minus the §3.2a keys, none of which an OFF run writes),
planes and face-on θ byte-equal to the base exe's.

**Invariants with A on** (`p_off` vs `p_A --truth`): measured coverage unchanged in every family
(address 1.00, mid-backswing 0.68, delivery 0.67, impact 0.95, after 0.59); truth 425 pairs p50 0.25°,
p90 0.65°, frames worse 0; `publishedInEndOn` 0; largest fused-plane move 0.00°; face-on θ identical
and metric counts equal on 24/24. The curve fitted on all 24 (65–98 frames, 39–55 knots, rms 1.2–8.6°;
the 8.6° is 07-04 s1 whose backswing is incoherent); the C++ knots against `fusion_geom.fit_eta`'s refit:
0.0000° at the knots and on the frames, all 24.

|Δθ_D| on the tracker's MEASURED frames, the face-on angle de-projected through the phase plane,
p50 / p90 (°), 24 swings:

| family | bands | frames | in-plane | η leave-one-band-out (reach 150 ms) | η LOBO, reach 400 ms | η full fit (in-band) | edge frames | edge in-plane | edge η |
|---|---|---|---|---|---|---|---|---|---|
| address | 24 | 728 | 4.15 / 16.49 | 4.42 / 12.59 | 14.56 / 39.14 | 0.36 / 2.32 | 288 | 5.12 / 26.86 | **1.87 / 8.08** |
| mid-backswing | 28 | 675 | 6.76 / 24.23 | 7.73 / 28.53 | 11.41 / 35.48 | 2.90 / 12.36 | 318 | 11.94 / 40.97 | 11.42 / 35.16 |
| delivery | 25 | 295 | 1.25 / 6.07 | 1.44 / 9.33 | 6.35 / 23.73 | 1.15 / 5.44 | 274 | 1.22 / 5.52 | 1.51 / 7.32 |
| impact | 15 | 70 | 1.24 / 3.83 | 1.29 / 4.57 | 1.76 / 6.64 | 0.86 / 2.60 | 70 | 1.24 / 3.83 | 1.29 / 4.57 |

`gate (design §3.2a A): LOBO beats in-plane on mid-backswing: NO; on delivery: NO; no family worse: NO`

The synth3d line against the tracker on MEASURED frames, base (in-plane) → new (η), p50 / p90 (°):
address 4.35 / 18.30 → **0.34 / 2.18**; mid-backswing 6.82 / 24.56 → **2.86 / 12.60**; delivery
1.89 / 95.90 → 1.62 / 53.10; impact 1.60 / 5.10 → 1.42 / 4.88; after P8 4.13 / 119.46 → 4.73 / 118.52
(15 frames; the p90s there are the corridor escapes of the after-P8 bands, not the curve).

Reading it — and it is not only the prior this time. The gaps between consecutive fused bands on the 24
swings run 33 ms (p10) / 114 ms (median) / 301 ms (p90); 49 of 72 are inside the prior's 150 ms reach.
So most held-out bands DID have a neighbour informing the curve, and the prediction was still no
better than in-plane — a little worse on every family at the median (+0.05 to +1.0°), and the
extrapolating variant (reach 400 ms) 2–3× worse. **η is not continuous across an end-on gap on this
golfer's swings**: the mid-backswing band ends steepening (s7: +13°) and the delivery band begins near
the plane (−1°); the neighbouring band's out-of-plane angle predicts the next band's with the wrong sign
as often as the right one. The gate fails on the data, not on a parameter. The band-edge hold-out
repeats the s7 finding on the address band only (5.1 → 1.9° p50: the takeaway leaving the address plane
is a slow, smooth departure the curve continues well); on the mid-backswing and delivery edges it is a
wash (11.9 → 11.4°, 1.2 → 1.5°). Where the curve has data under it (the full-fit column, and the synth
line on measured frames) it is 2–12× nearer the track than in-plane, which is what the tile would show
beside the measured line if it drew there — and it does not.

### 3.4 Default

**On, by Mark's decision** (1 Oct 2026, on reading §3.3), together with the synthetic line
(`shaft.dtl.synth3d.enabled`, which had been dark since §3.2's build). The gate as written (LOBO beats
in-plane on mid-backswing and delivery) is not met, and on this geometry it cannot be by a curve with the
design's zero prior; the curve's measurable value is inside and at the edges of the bands, which is a
different claim than the gate tests, and one only the tile can judge. With it on, the synth reads the
curve (`shaft.dtl.synth3d.useEta`) and nothing else does — the plane fits, `deliveryVsAddressDeg`, the
sequence and every metric are the OFF bits (§3.3's identity row). The synthetic line is drawn dim and
dashed under the measured one on the DTL tile and is a PREVIEW through the assumed-zero camera: its shape
is the curve's, its heading is off by the unknown yaw (§2) until the protocol session. The judgement is
Mark's, in the app, on the re-analysed 4 July session, as the HELD tier's was; the switch turns it back
off from there if it does not earn its place. Not built: the synthetic shaft drawn in the 3-D swing
panel (design §3.2 "into both tiles") — that panel still shows the skeleton's own club.

## 4. (C) The DTL anchor at impact — degenerate down the line

### 4.1 The geometry

The design's rule: on a bridged frame take the direction as the DTL view plane's intersection with the
phase's fitted plane. The one-swing probe returned the conditioning of that intersection, |n_plane ×
n_view|, on s7's bridged downswing frames: **0.063, 0.069, 0.078, 0.084** at impact (band P6.4 → P7.8)
and 0.073 at the delivery band's tail — the two planes are 3.6–4.8° apart. The fusion refuses a two-camera
intersection below 0.26 (15°); the synth below 0.15.

Why it is structural and not this swing: "down the line" means the DTL camera's view ray lies along the
swing plane's node line — the fused downswing normal has n_x = 0.06 on s7, i.e. the view ray is 3.5° out
of the plane. A DTL view plane contains the view ray by construction; two planes that share a direction
intersect along it. So the DTL angle de-projected through the swing plane is the view ray itself (end-on,
no direction) unless the two planes coincide, and at impact, where the shaft lies in the plane and
perpendicular to the view ray (ρ̂_D ≈ 0.95, "sharp"), they do coincide: the DTL view plane at impact IS
the swing plane. The DTL sees impact sharply because the shaft is broadside to it, and a broadside line
through the camera's axis constrains only the plane the shaft is in — its inclination — never the
direction within it. That is exactly the fact the address plane uses constructively (`addressPlaneNormal`:
the DTL view plane at address IS the address plane) and what `oopDeg` on bridged frames already reads
("checked against the view that sees impact sharply": s7's bridge is within −1.7…+2.3° of the plane at
impact). The direction along the plane at impact can only come from the face-on view or from time; the
bridge is the face-on synth intersected with the DTL view plane, and that is the best either camera can
do at that instant.

### 4.2 Built anyway, as the diagnostic

`shaft.fusion.dtlAnchor.enabled` (false): on every bridged frame with a phase plane, `deprojectEta(cd,
θ_D, n_plane, η(t))` with the fusion's `minCond`; `anchorCond` on the record either way; where it
succeeds the sample's `u` becomes the anchor, `uBridged` keeps the bridged value beside it, the flag
`dtlAnchored` is set, `OffPlane` (a statement about the bridge) is cleared, the sample stays out of every
fit; `summary.nDtlAnchored / nDtlAnchorRefused`. Test `shaft_fusion_test` §C: a coast through impact
with a bridge 4° wrong — camera on the node: 13 bridged, 0 anchored, 13 refused, conditioning ≤ 0.0000;
camera 25° off the node: 13 anchored to 0.005° of the truth where the bridge is ≥ 2.9° off, the bridged
value kept beside it bit for bit, the plane fit untouched.

### 4.3 One swing (07-04 s7, `m_off` vs `m_C`)

15 bridged frames in the phase windows: 2 anchored (cond 0.28 and 0.43, both on the delivery band's tail
where ρ̂_D is 0.57–0.70 and the shaft is not yet broadside), 13 refused (p50 cond 0.12, max 0.43). On the
two anchored frames the anchored direction is **33.2°** from the bridged one and the bridged one is the
nearer to the neighbouring measured fused frames on both. So where the anchor is even possible it is
worse, and the ill-conditioning is why: a 1° error in θ_D or the plane's normal moves a 0.3-conditioned
intersection by degrees.

### 4.4 The corpus gate

`p_off` vs `p_C --items`, 24 swings. Invariants: planes 0.00°, face-on θ identical 24/24, truth
unchanged, `clubDtl` untouched (the anchor changes `club3d` frames only).

Bridged frames inside the phase windows **243**; anchored **76**, refused **167** (conditioning below
0.26); conditioning p50 0.138, p90 0.554, max 0.990. Within ±60 ms of impact — the frames the item was
for — 71 bridged, **2 anchored**, conditioning p50 0.056, p90 0.152: the DTL view plane and the swing
plane are 3–9° apart there on every swing, as §4.1 says they must be. Where the anchor was possible it
was mostly on the delivery band's tail (ρ̂_D 0.5–0.7, the shaft not yet broadside) and on the three
07-04 s1–3 swings of the other rig epoch (conditioning 0.57–0.99: that rig was yawed further off the
node — the skeleton read 7.7° there against 12.6° after the move, §2 — and its backswing is the
mirrored one). On the 76 anchored frames the anchored direction is 1–35° from the bridged one (per-swing
p50 1.1–34.9°), and against the neighbouring measured fused frames the **bridged direction is nearer on
69 of 76, the anchored on 7** (four of the seven on 07-04 s15 where the two are 1° apart). The full
per-swing table is `grade_C.md` in the run root.

### 4.5 Default, and what would give impact a witness that owes the bridge nothing

Off; the item is closed as infeasible in the fusion's geometry, with the diagnostic on the record. Two
things would give the impact instant a direction the face-on synth does not own: the 3-D ball (the head
is AT the ball at impact — `skeleton3d.display.ball` and the skeleton's grip give a grip → ball direction
on that one frame), and the calibrated camera, which lifts the view ray off the node by the true yaw (the
skeleton says ~12°, the stick ~9°, §2) — enough conditioning, 0.15–0.2, for a poor anchor, not a good one.
Neither is this item.

## 5. (D) The mirrored backswing band, re-read inside the fusion

### 5.1 Built

`shaft.fusion.reflectBands.enabled` (false; `minGainDeg` 0.5). Only when the backswing fit is incoherent
(rms > `backIncoherentDeg`, 12°). Candidates: every DTL band with samples in the backswing window and
none at or before the address instant — the address band never is (the DTL ball anchors it, and
reflecting it is the mirror image of reflecting all the others). A band is reflected about the DTL image's
vertical, θ → π − θ: the corridor's two centres (tracker design §4.1 (c), `atan2(s, ∓q)`) are symmetric
about the vertical, so that IS "2·centre − θ" — checked on the corpus frames that carry a corridor (their
two centres sum to −π on every one), and it needs no corridor to be on. Greedy: the band whose reflection
lowers the refit's rms most is taken while the gain exceeds 0.5° and the band's own sign-disagreement
count does not rise; the set is kept only if the final rms is under 12°, else every sample is put back
exactly. Reflected samples are re-fused (`fuseOne` with the reflected θ_D), flagged `reflected`, and
count in the fits (the flag is a reading, not a disagreement). `summary.reflectedBands`,
`backRmsBeforeReflectDeg`. The DTL track (`clubDtl`) is untouched, byte for byte.

Test `shaft_fusion_test` §D: the §4 mirrored swing with band ids — rms 16.3° → 0.0°, inclination 79.2°
→ 50.0°, the reflected band is the mirrored one and only it, every sample of it flagged, its directions
the truth again, the downswing plane untouched; a coherent swing is not touched, bit for bit; a
half-mirrored band (every other frame) that no reflection can repair: rms 18.6° before and after the
search, nothing kept, every sample put back exactly.

### 5.2 One swing, and the three the fusion first saw it on

s7 (`m_off` vs `m_D`): coherent (5.1° rms) → untouched, `reflectedBands` empty, planes identical. The
Python probe (`fusion_geom` on the `robust-20261001/corpus_v11` runs, live pose, the ones the fusion
design's finding came from), greedy over the non-address backswing bands:

| swing | backswing as fused | reflect | after |
|---|---|---|---|
| 07-04 s1 | 20.0° rms, 54.7° (published 22.4° / 48.9°) | P2.8→P3.1 (12 fr) alone: 14.8°; no second band helps | **still incoherent — nothing kept** |
| 07-04 s2 | 20.1° rms, 82.2° | P2.2→P3.5 (22 fr) | **10.7° rms, 55.1°** |
| 07-04 s3 | 18.8° rms, 85.5° | P2.3→P3.1 (16 fr), then P3.1→P3.4 (3 fr) | **4.6° rms, 56.4°** |
| 07-04 s4 (coherent) | 6.2° rms, 59.0° | — | untouched |

s2 and s3 come back to 55–56°, within 4° of s4–15's 59–61° (the rig moved between s3 and s4, so equality
was never expected). s1 — the swing with the golfer clipped at the frame edge — has three short
mid-backswing bands and no reflection of any subset reaches 12°, so the rule leaves it as it was.

### 5.3 The corpus gate

`p_off` vs `p_D --items`, 24 swings (the run root's `p_D`; the first run, `p_D_v1`, is §5.3a).
Invariants: truth 425 pairs unchanged (0 worse); face-on θ identical and metric counts equal 24/24;
measured coverage per family identical; the downswing plane identical on 24/24 ("down plane: same");
`clubDtl` frames untouched.

| swing | backswing base → new | back rms base → new | back incl base → new | heading new (down plane) | bands reflected |
|---|---|---|---|---|---|
| 07-04 s1 | INCOHERENT → INCOHERENT | 24.6 → 24.6 | 66.5 → 66.5 | — | none kept |
| 07-04 s2 | INCOHERENT → **coherent** | 20.9 → **12.0** | 82.7 → **54.8** | −95.7° (−96.4°) | P2.2→P3.5 |
| 07-04 s3 | INCOHERENT → **coherent** | 18.0 → **4.4** | 87.3 → **57.9** | −98.7° (−98.3°) | P2.3→P3.5 |
| the other 21 | coherent → coherent | unchanged | unchanged | | none |

`incoherent swings 3, made coherent 2, with a reflection kept 2; coherent swings changed: 0 (gate: 0)`.
The wedge session (06-11), where the fusion design first suspected a mirror, fuses coherent on all nine
in this build (backswing rms 3.0–11.1°) and is not touched. The repaired planes' headings sit within 1°
of their own downswing planes' (a heading is only as good as the DTL yaw — §2 — but the AGREEMENT
between the two halves of one swing does not depend on it). Inclinations 54.8° / 57.9° against s4–15's
59–62°: the rig moved between s3 and s4, so "within a few degrees" is the most that can be asked.
s2 lands at 11.98°, a hair under the 12° threshold: a coherence claim by one frame's width, and said so.

#### 5.3a The first corpus run found the mirror-image trap

`p_D_v1` reflected s2's **address band** ("addr→P1.7") instead of its mid-backswing band: the same
scatter (12.0°), the same inclination (54.8°), a heading of **+137°** — the mirror image of the swing.
Reflecting one band or reflecting all the others are the same refit up to a mirror, and the time rule
("no sample at or before the address instant") did not protect the address band on s2 because the
face-on track there starts after the address event, so every fused frame of that band lies past it, and
the greedy took the lower band index on a tie. The fix (in `p_D`): a refit whose normal points away from
the downswing plane's (n · n_down < 0) is the mirror image and is refused — the downswing plane is
fitted from frames no reflection touches, so it says which way the swing faces. `shaft_fusion_test` §D
now covers it (the address band as the mirror-image candidate with no address window declared is refused
by the downswing plane's side).

### 5.4 Default

**On** (`shaft.fusion.reflectBands.enabled`, `Config::reflectBands = true`). The gate as written is met:
the incoherent backswings that a whole-band mirror explains become coherent (2 of 3; the third, s1 with
the golfer clipped at the frame edge and three short bands, no subset of reflections reaches coherence
and it is left exactly as it was), their inclination lands near the other epoch's, no coherent swing of
24 changes, the DTL track is untouched. The p_off run (every item explicitly off) is the OFF gate's
witness regardless of the default. Because the default moved and `kShaftFusionStageVersion` is 5, the 4
July library session's s1–3 will re-fuse on their next version-gated re-analysis — s2 and s3 gain a
backswing `swingPlane` half they never had (the metric is published only when the fit is coherent).

## 6. What is unproven

- **A across the gaps.** The curve has no information about the top or P6 gaps and says so (the prior).
  Whether the true η through the top is the reversal s7 shows or a hinge is the clean session's
  backswing-plane test (design §4 item 5).
- **A at the band edges** is a hold-out on the tracker's own frames, not truth: the held-out truth is
  address-only. The 2× at the edges is against the tracker.
- **B**: which of the two instruments is wrong, and by how much, is not decided here (§2's three
  candidates). Only the two-camera stick/card clips decide it.
- **C**: closed on geometry; the two anchored s7 frames are two frames. The corpus row (§4.4) is the
  count of how often the anchor is possible at all.
- **D** has no DTL truth beyond address; "coherent, 55–56°" is the plane's own scatter and its
  agreement with the rig's other epoch, not a measurement of the reflected band's angle. The reflected
  band's θ_D is still the tracker's angle mirrored; the tracker's sign table (design §4.1 (c), owed) is
  where the mirror should eventually be prevented rather than repaired.
- The tile was not looked at: the synth with η is a number in `clubDtl.synth3d[].etaDeg`; nothing draws
  differently until `shaft.fusion.eta.enabled` is on.
- Mac vs studio: the s7 numbers in §3.2 / §4.3 / §5.2 are Mac runs (184 DTL frames fused vs the studio's
  186); the gate is the studio, before and after on one platform.

## 7. Reproduce

```
# everything below: build/run-me/dtl-precalib-grade.sh
R=/mnt/swingdata/scratch/dtl-precalib-20261003; G=tools/shaftlab/dtl_continuous_grade.py
python3 $G $R/p_base $R/p_off --identical                                  # the OFF gate
python3 $G $R/p_off $R/p_A --truth /mnt/swingdata/corpus/dtl_heldout_truth --items   # (A)
python3 $G $R/p_off $R/p_C --truth /mnt/swingdata/corpus/dtl_heldout_truth --items   # (C)
python3 $G $R/p_off $R/p_D --truth /mnt/swingdata/corpus/dtl_heldout_truth --items   # (D)
python3 tools/shaftlab/dtl_yaw_probe.py /mnt/swingdata/corpus/swings/2026-07-04_Mark-Liversedge_Wrist_01   # (B)
# one swing (Mac): m_off / m_A / m_C / m_D are swinglab_run … --params $R/in/params_{off,A,C,D}.json
ctest --test-dir build/tests -R "shaft_fusion_test|dtl_shaft_synth3d_test"
```

Studio recipe (24 swings, five configs, three to five in parallel): `C:\Users\developer\wedge-patches\
build_pc.cmd` (file-copy build from `pc_files.tar`, restores the checkout with `git show HEAD:path`,
leaves `swinglab_run_pc.exe` beside the base `swinglab_run.exe` rebuilt from origin/main b53d36f9 by
`build_main.cmd`), `run_pc.ps1 -Cfg <base|off|A|C|D> -Exe <exe>` launched with the EncodedCommand
`Invoke-CimMethod Win32_Process Create` pattern; the `.done` sentinels and per-swing logs (UTF-16) are in
the run root.

## 8. The commits

1. `Analysis: before the calibration — the out-of-plane curve η(t), the DTL anchor's conditioning and
   the reflected backswing band inside the fusion, each switched (§3.2a A, C, D); kShaftFusionStageVersion 5`
   — `src/Analysis/{shaft_fusion.h, shaft_fusion_json.h, dtl_shaft_synth3d.h, dtl_shaft_track.h,
   dtl_shaft_json.h, wrist_analyzer.cpp, analysis_versions.h}`, tests
   `src/Analysis/tests/{shaft_fusion_test.cpp, dtl_shaft_synth3d_test.cpp}`.
2. `Tools: the fusion's geometry in numpy and the §3.2a per-item columns in the DTL grader; the yaw
   probe reads the 11 June clip name` — `tools/shaftlab/{fusion_geom.py, dtl_continuous_grade.py,
   dtl_yaw_probe.py}`.
3. `Docs: before the calibration — the four items gated; B stopped at its cross-check, C degenerate
   down the line, A dark, D on` — this file, the four `dtl_precalibration_20261003_grade_{off,A,C,D}.md`
   per-swing tables beside it, and the "Done" line in the design's §5 order of work.

Then: re-analyse the 4 July library session in place under the version gate (D moves s2/s3's backswing
plane; A and the synthetic line now write `club3d.eta` and `clubDtl.synth3d` on every two-camera swing),
and judge the drawn line in the app.
