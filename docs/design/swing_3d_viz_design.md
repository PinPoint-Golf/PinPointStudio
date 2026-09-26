# 3-D swing viz — a rigid, jointed skeleton fitted to every sensor, drawn from any side

*2026-09-26. Status: **BUILT and graded the same day** (an unattended run) — §12 says what was
built, what changed from §3–§6 and why, and what the synthetic suite and the 24 two-camera
corpus swings reported. §1–§11 are kept as the design that was approved; where the build
departed from them, §12.2 is the authority and the section carries a ⚠ pointer.*

*Originally written after surveying what already exists (`BodyVizView.qml`,
`BodyPoseAdapter`, `tools/extract_body_segments.py`, the shaft fusion stage, the kinematic
sequence's pair route and the DTL posture stage).*

*Revised the same day, after Mark's review:*
- **(1) No overlap with the calibration views.** The new view shares no QML, no C++ and no
  resource files with `BodyVizView`, `ArmVizView`, `ImuVizView`, `CapturePage` or the
  wizard (§6.1).
- **(2) Anatomical constraints are the estimator, not a post-check.** The first draft
  triangulated joints and then only *flagged* bones that changed length. Now the skeleton
  itself is the unknown. Bones have fixed lengths and joints have their real degrees of
  freedom and ranges. That skeleton is fitted, in one batch over the whole swing, to both
  cameras, the IMUs, the shaft and the ground (§3). §4 shrinks because orientation now
  comes out of the fit.

## 0. Summary

A new session panel, **"3-D swing"**, shows the focused shot as the Y-bot plus a club and a
ball. It follows the replay playhead and can be orbited or snapped to presets (face-on,
down-the-line, top, target-side, behind). It is built in three parts:

1. **An analysis stage, `skeleton3d`** (`src/Analysis/skeleton3d/`). It fits a **rigid,
   jointed skeleton** to everything measured about the swing. The skeleton has the Y-bot's
   topology, scaled to the golfer, with anatomical joint limits. The measurements are the
   face-on and DTL 2-D keypoints, IMU and HackMotion orientations where worn, the fused
   shaft, and the feet's contact with the floor. The fit is one sparse, robust nonlinear
   least-squares problem over the whole swing window. The segment lengths, camera geometry
   and sensor mounting offsets are shared unknowns solved alongside every frame's pose.
   **Bones cannot stretch, because length is not a per-frame quantity.** Knees and elbows
   cannot twist, because they are hinges. The output is the fitted state: lengths, camera
   parameters, per-frame joint rotations and positions, and a per-joint σ from the fit.
2. **A rig driver, `SwingRigDriver`** (C++, `QML_ELEMENT`). It samples that state at the
   playhead and hands the view bone-local quaternions. Because the fitted skeleton *is* the
   Y-bot hierarchy, this is a lookup and an interpolation, not a reconstruction.
3. **A view, `SwingViz3DView.qml`**. It is one `View3D` with its own copy of the Y-bot
   segments, the club, the ball, the ground and optional 3-D traces.

## 1. What exists, and what it cannot do

| Asset | What it is | Why it is not already the answer |
|---|---|---|
| `src/Resources/body/*.glb` + `ybot.glb` | Y-bot split into bone-local meshes by `tools/extract_body_segments.py` | Used by the calibration views. **Not reused** (§6.1): the new view extracts its own copy |
| `BodyVizView.qml`, `BodyPoseAdapter` | The chain and a live 2-D driver | Calibration-side. Planar (`zRot`) by construction. Not touched and not shared |
| Pose, face-on + DTL (`analysis.pose2d`, `analysis.poseDtl`) | 133-pt WholeBody per frame with `kp/tier/sigma`, plus `.smoothed` and a 240 Hz `.synth` | 2-D only. Hand keypoints are unreliable (confidence lies) |
| `club3d` (`shaft_fusion.h`) | Fused 3-D shaft **direction** in bands, plus the downswing plane (7i: 60.3° ± 0.7) | Direction only, and banded. Heading only as good as `dtlYawDeg` |
| Pair route (`kinematic_sequence_design.md` §13) | Trunk turn from signed separations. `r = s_D/s_F` is measured at address (1.12–1.25); γ was fitted offline at 75–84° | Pelvis and thorax only. Its camera model is the starting point for §3.3 |
| IMU (Witmotion) segments, HackMotion wG3 | Orientation and rate on the segment worn | Orientation only. Mount offset and heading drift unknown |
| Ball | Face-on `ballPx`; DTL ball (from its shadow on 06-11) | 2-D per view |
| Athlete profile | Height (`athlete_controller.h`) | The scale prior |
| Eigen | Already a dependency, including `Eigen/Sparse` | The solver needs nothing new |

## 2. Frames

- **Analysis frame** (as `club3d`): **X** = face-on image-right, **Y** = the face-on view
  ray, **Z** = up. Metres.
- **Golfer frame** (display): origin = the ball at address, dropped to the ground.
  **Stance axis** = the line through both heels at address, on the ground. ⚠ It is not the
  target line: heading is uncalibrated, so the top view says "square to your stance at
  address".
- **Qt Quick 3D**: `qt = (X, Z, −Y)`. The determinant is +1, so this is a proper rotation.
  A mirrored face-on source is un-mirrored in the stage, once.
- **Handedness** is never flipped in the geometry. Only the view presets use `rightHanded`.

## 3. The `skeleton3d` stage — fit the body, don't correct the points

### 3.1 Why "triangulate, then constrain" is the wrong order

If joints are triangulated first and bone lengths enforced afterwards, every error in the
points is kept, and the length correction only moves it to the ends of each bone. Three
problems stay:

- **Frames seen by only one camera have nothing to triangulate.** The trail arm at the top
  in face-on is one example; the whole DTL-less library is another.
- **Each bone is corrected on its own**, so fixing the forearm moves the wrist, and that
  quietly breaks the hand.
- **The camera geometry the triangulation needed** (γ, r, distance) had to be assumed or
  measured at address, which is exactly where the pair route found it hard.

A fixed-length, jointed skeleton turns this round. The *skeleton* is the state, and every
sensor is an observation of it. A bone cannot change length because there is one length for
the whole swing. A knee cannot twist because it has one axis. And across hundreds of frames,
a body that is known to be rigid in this way **constrains the cameras too** (§3.5).

### 3.2 The model

> ⚠ Built differently in four places (§12.2): lengths are **frozen** at the Y-bot × height by
> default, marker offsets are **fixed** priors (plus one symmetric width/height offset for the
> shoulder and hip pairs), the head turns at the **neck base**, and the camera model gained a
> DTL **roll**.

**Topology = the Y-bot hierarchy**: Hips → Spine → Spine1 → Spine2 → Neck → Head;
Spine2 → Shoulder → Arm → ForeArm → Hand (each side); Hips → UpLeg → Leg → Foot (each
side). The rest offsets and rest rotations are extracted from `ybot.glb` into a generated,
Qt-GUI-free header (`skeleton3d/ybot_rig.h`). Analysis owns the numbers and the view reads
the same header, so the fitted rotations map one-to-one onto the drawn bones.

**Shared unknowns (one value per swing, or per session when pooled — §3.4):**

| Unknown | Count | Prior |
|---|---|---|
| Segment lengths ℓ | 12 (left = right enforced: pelvis width, lumbar, thoracic, neck, head, clavicle, upper arm, forearm, hand-to-grip, thigh, shank, foot) | Winter's anthropometric ratios × athlete height, σ = 5 % |
| Marker offsets | 1 per keypoint used, a 3-vector in its segment's frame | Zero for joint-centre keypoints; anatomical defaults for eyes, ears and toes; σ = 2 cm |
| Camera geometry | γ, r, a distance to the golfer per view, a 2-D image offset per view | γ ≈ 80°, r from the address extent, distance from focal length and the height in pixels |
| IMU mount offset | 3 per worn IMU (rotation) + a heading-drift slope | Mount from the calibration record where one exists |

"Marker offsets" is the step that stops the fit trusting the pose model more than it
should. A COCO hip keypoint is not the hip joint centre. It sits on the surface, at a
roughly constant place in the pelvis frame. That offset is modelled explicitly instead of
being absorbed as a false bone length, the way OpenSim-style marker models do it.

**Per-frame unknowns** (~43): root position (3) and orientation (3); Spine, Spine1 and
Spine2 at 3 each, with the twist split held near 1/3 each by a soft prior; Neck + Head 3;
Shoulder (clavicle) 2; Arm 3; **elbow 1 (flexion) + forearm pronation 1**; **wrist 2**
(flexion/extension, radial/ulnar); UpLeg 3; **knee 1**; ankle 2. Rotations are stored as
rotation vectors about each joint's rest frame.

**Joint limits** are soft barriers (quadratic outside the range, zero inside), from clinical
ranges widened for golf. Examples: knee 0–150°, elbow 0–150°, pronation −90…+90°, lead
wrist flexion (bow) to 45°, lumbar axial twist ±15° per segment. A limit that the fit keeps
leaning on is reported. It is either a real extreme or a sign the model is wrong, and it is
worth knowing which.

### 3.3 Observations — the residual terms

Every residual is weighted by the observation's own σ, and the image-space terms go through
a robust loss (Cauchy), so one mislabelled keypoint cannot drag the body.

| Term | What it compares | Notes |
|---|---|---|
| **r_2D** | each keypoint's marker, projected through that view's camera model, against the detected pixel | Per view and per frame. Low-confidence or missing keypoints contribute nothing, and the rigid model carries those joints. Camera model: perspective with a fitted distance and a known or nominal focal length (the first draft's weak-perspective-plus-correction is now just the initialiser) |
| **r_IMU** | the segment's fitted orientation × mount offset, against the IMU's orientation, **and** the fitted angular rate against the gyro | Rate is where the IMU is strongest and does not drift, so it gets the higher weight. Orientation is weighted down by the drift model |
| **r_HM** | the fitted lead wrist's 2 angles against the wG3 | HackMotion is the criterion. The viz fit uses it where present. The *graded* fit is a separate, vision-only run (§8.2), so vision is still graded against HackMotion and never the reverse |
| **r_shaft** | the lead hand's grip axis against the shaft direction (`club3d` fused frame, else the face-on angle through the fused plane) | This is the forearm-roll witness, and it works through the straight-arm band where the elbow hinge gives nothing |
| **r_grip** | the trail hand's grip point against the shaft line, below the lead | Keeps both hands on one club. Switched off once the trail hand is seen to leave in the finish |
| **r_contact** | heel and toe markers: height = 0, and velocity = 0 while in contact | Contact comes from the existing foot metrics (heel lift). The feet are the best-seen, least-moving part of a golfer, so this pins the root far better than the pelvis keypoints can |
| **r_smooth** | second differences of every joint angle and of the root | ⚠ Weighted by phase: light through transition → impact, where arm and club rates reach ~2000 °/s, so the prior cannot shave the peaks the kinematic sequence reads |
| **r_limit** | the barriers above | — |
| **r_prior** | lengths, offsets and camera parameters against their priors | — |

**Label swaps** are handled inside the fit, not before it. When the face-on shoulder/hip
labels flip at the top, the swapped assignment for that view and frame is tried as well,
and the lower-cost one is kept and flagged `labelSwap`. A rigid body makes the wrong
labelling expensive, which a per-view heuristic cannot do.

### 3.4 The solve

> ⚠ Built as a block-banded Cholesky (48×48 blocks, bandwidth 2) with a Schur complement on
> the shared block, and an exact geometric Jacobian instead of AutoDiff (§12.2).

- **Initialise.** (a) Weak-perspective two-view triangulation of the joints (the first
  draft's §3.1: z from both views, x from face-on, y from DTL). (b) Lengths = the median
  over frames where both views are sharp. (c) Per-frame inverse kinematics onto the rig.
- **Batch.** Levenberg–Marquardt over **all frames at once** (it is offline, and the whole
  window is available). The Jacobian is sparse. Frame blocks are coupled only by the
  smoothness band, plus a thin border for the shared unknowns. Solve each step with Eigen's
  `SimplicialLDLT`, or with a Schur complement on the shared block. At ~600 frames × ~43 +
  ~120 shared unknowns that is ~26 k unknowns: an ordinary size for a sparse solver. Budget
  ≤ 1 s per swing on the Mac. **To be measured, not assumed.**
- **Pooling.** Lengths and marker offsets are properties of the golfer, and camera geometry
  is a property of the session. When a session has several swings, those can be solved once
  across all of them and then held fixed per swing. That is phase 3, not version 1.
- **Uncertainty.** A per-joint σ comes from the frame's block of the final Gauss–Newton
  Hessian, ignoring cross-frame terms, which is conservative. Tiers are thresholds on it
  (§3.7).

### 3.5 What the anatomy buys — and what it can't

This is the part to measure (§8), not to believe. What each constraint *should* do:

| Constraint | Should give | How it will be shown |
|---|---|---|
| Fixed lengths | No stretching; depth *magnitude* for any bone seen by only one camera | Raw triangulation's per-bone length CV, reported as the thing being corrected |
| Fixed lengths over many poses | **Self-calibration of γ, r and the camera distances** (articulated structure-from-motion) | §8.1 (c): a synthetic swing recovers the true γ/r/distance from a wrong start. If it doesn't, the camera parameters stay fixed at the pair-route values, and the doc is updated to say so |
| Hinge knees and elbows | Thigh and upper-arm roll observable from positions alone | Synthetic roll error |
| Joint limits + smoothness | The *sign* of the depth for one-view bones (the mirror pose is usually anatomically impossible, or a jump in time) | Face-on-only refit of two-camera swings, graded against the two-camera fit (§8.2) |
| Grip + shaft | Forearm pronation and wrist angles with an arm straight | Against HackMotion, where worn |
| Ground contact | A stable root; ends foot skating | Foot slip, mm, reported |

**What it cannot fix:**
- **Keypoint bias the pose model shares across frames.** Marker offsets absorb a constant
  bias, but not one that changes with pose (e.g. the shoulder keypoint sliding over the
  deltoid as the arm lifts).
- **Heading.** The rigid body is the same in any rotation about Z, so without calibration
  the swing direction is still not claimed.
- **Real motion the smoothness prior mistakes for noise.** That is the risk at impact, and
  why its weight is phase-gated and ablated in §8.

### 3.6 Face-on only

A swing without DTL runs **the same estimator with one camera**. Lengths, limits,
smoothness, ground contact and (where present) the shaft plane and IMUs are then the only
source of depth. That makes the old ad-hoc "depth from bone length" rule a special case of
the same fit, and its quality a number (§8.2) rather than a hope.

### 3.7 Tiers and persistence

Per joint, per frame, from σ and from which terms contributed:

| Tier | Rule (initial thresholds, set by §8) | Drawn as |
|---|---|---|
| `measured` | σ ≤ 2 cm and both views contributed | Solid |
| `constrained` | σ ≤ 5 cm | Solid, with a "constrained" chip on the panel |
| `inferred` | σ > 5 cm | Ghosted, 40 % |
| `absent` | no term within 100 ms | Not drawn |

**Persisted**, as `analysis.skeleton3d`, schema `pinpoint.skeleton3d/1`:
- **Shared block:** frame string, lengths, marker offsets, camera parameters, IMU mounts,
  final cost, and per-term residual medians.
- **Per frame:** `t_us`, root pose, joint rotation vectors (~43 floats), and a per-joint
  σ + tier for the 26 drawn joints.
- **Flags:** `labelSwap`, `limitHeld`, `footSlip`.

That is ~350–500 KB of JSON at source cadence over a ~2.5 s window, against a document
already at 35–65 MB. The version is `kSkeleton3DStageVersion`. Inputs are `pose2d`,
`poseDtl`, `club3d`, the IMU and HackMotion blocks and the config, so a metrics-only
re-analysis reuses it under the version gate (no `--full-window` on library write-back).

**It feeds no metric in version 1.** Once §8.2 has graded it, it is the obvious producer
for 3-D spine angle, pelvis turn and the rest, but that needs a separate document and a
separate decision.

## 4. Segment orientation

Orientation is no longer reconstructed from points. It is the fitted joint state itself:

- **Direction** of every bone: fixed by the 2-D terms through the rigid chain.
- **Roll**, per segment:

| Segment | Where its roll comes from in the fit |
|---|---|
| Pelvis, thorax | Both hips / both shoulders in two views; the IMU where worn |
| Thigh, upper arm | The hinge at the next joint (knee, elbow) — a hinge only bends one way, so the child's direction fixes the parent's roll |
| Forearm, hand | The shaft and grip terms; HackMotion where worn |
| Head | Eyes, ears and nose as markers |
| Foot | Heel and toe markers plus ground contact |

The only straight-arm hole the first draft had to patch (a lead elbow with no bend and no
roll) is covered by `r_shaft`. Where no shaft is seen, the smoothness prior carries roll
through it, and σ rises, which the tier shows.

The driver converts a joint's rotation vector to the Y-bot bone's local quaternion through
the rest rotation from `ybot_rig.h`. Unit test (§8.1 a): a zero state reproduces the rest
pose exactly.

## 5. The club and the ball

**Shaft direction**, best first:
1. `club3d` fused frame — measured.
2. Face-on angle de-projected through the fused plane — estimated.
3. Face-on angle through the catalogue plane for the club — inferred, dashed.
4. Hidden.

**Shaft position.** The grip sits on the **fitted** lead hand. `r_grip` and `r_shaft` have
already pulled the hand onto the club, so no gap is left to hide. The residual of those two
terms is the honest measure of how well the club and the body agree.

**Clubhead.** Neutral and symmetric, because face angle is not measured. Length comes from
the catalogue for the club.

**Ball.** At address, face-on x/z plus DTL y, else `ballBodyDistance`. Hidden when the ball
tracker loses it. With a launch-monitor read, `lm_flight_path` in 3-D relative to the stance
axis (phase 5).

**Scrubbing.** The driver interpolates the fitted state (slerp per joint) to the playhead,
exactly at time. There is no timer-driven easing: the same instant always gives the same
pose.

## 6. The view

### 6.1 No overlap with calibration

| Calibration side (untouched) | Swing side (new) |
|---|---|
| `src/Gui/viz/BodyVizView.qml`, `ArmVizView.qml`, `ImuVizView.qml`, `body_pose_adapter.*` | `src/Gui/swing3d/SwingViz3DView.qml`, `swing_rig_driver.*` |
| `src/Resources/body/*.glb` | `src/Resources/swing3d/*.glb` (~3 MB, extracted separately) |
| `tools/extract_body_segments.py` | `tools/extract_swing3d_rig.py` — writes the GLBs **and** `ybot_rig.h` from `ybot.glb` |
| `CapturePage`, the session wizard | Session-mode stage panel only |

The two sides share no file, component or type. The one cost is ~3 MB of duplicated
meshes, accepted in exchange for a change on either side never being able to reach the
other. There is also no parity test between them, because the point is that they may
diverge.

### 6.2 Structure

> ⚠ The View3D is created ONCE per screen and REPARENTED between panel slots
> (`SwingViz3DHost.qml`), never destroyed when the panel hides — see §12.2.

```
SwingViz3DView.qml
  SwingRigDriver            C++ — skeleton3d handle + playhead → bone quaternions, root, shaft, ball, tiers
  View3D                    one instance; created only while the panel is on AND the screen is active
    OrbitCameraController   presets animate the camera, never the scene
    ground grid, stance line (labelled "stance", not "target"), ball
    Swing3DRig              the 19 segment Nodes, offsets from ybot_rig.h scaled by the fitted lengths
    ClubNode                shaft + neutral head
    Traces (optional)       hand path, clubhead arc, fused downswing plane as a translucent quad
```

Bone scaling uses the **fitted lengths**. Each segment Node's offset is scaled, and each
mesh is scaled along its own +Y by (fitted ÷ Y-bot) length. The drawn body therefore has the
golfer's proportions, and the club lands in the hands with no fudge.

### 6.3 Presets, wiring, honesty

- **Presets:** Face-on · Down-the-line · Top · Target-side · Behind · Free. These are
  in-panel chips (no menus). They orbit the mid-hip at address, animate over
  `Theme.durationNormal` (none under `reduceMotion`), and are remembered per session mode.
- **Wiring:** key `swing3d`, label "3-D swing", in `PpViewPanel.panelMeta` and
  `PpModeStage._defs`, with a delegate in `ScreenSessionMode.qml` keyed to the focused
  swing (`shotReplay.swingDir`, else the carousel's selection).
- **Data handoff:** the replay source exposes a typed, immutable, C++-owned
  `Skeleton3DTrack` handle. **Never** as a QVariantMap property: every QML read of one is a
  deep copy.
- **Gate:** the panel is offered when `skeleton3d` exists (data-gated, never on
  `sessionType`). Otherwise it shows a muted placeholder with the reason.
- **Honesty:** a tier chip for the current frame; ghosted `inferred` segments; the top view
  labelled "square to your stance at address".
- **View3D risk:** the watch on 3-D views going blank still applies, so the panel ships
  after a 30-minute scrub-and-preset soak on Mac and GOLFSIMPC, checked through the app log.
  There is one instance, destroyed when hidden.

## 7. Performance

- **Stage:** dominated by the batch solve, ≤ 1 s per swing targeted and measured in phase 2.
  That is small beside pose inference, and paid once per analysis.
- **Driver:** ~20 slerps per playhead tick.
- **View:** the render.

## 8. Verification

### 8.1 Synthetic (`src/Analysis/tests/skeleton3d_test.cpp`)

A generator poses the Y-bot procedurally: address → top → impact → finish, with known
lengths, a 45° X-factor, forearm pronation, lead-wrist bow, feet planted and a heel lift.
It projects the markers (with known offsets) through two perspective cameras at γ = 80°,
1.75 m, and adds σ = 2 px noise, 2 % dropouts and one face-on label swap at the top.

- **(a) Rest round trip.** A zero state gives the rest pose, and every bone quaternion is
  identity to 1e-4.
- **(b) Accuracy.** Bone direction ≤ 3° p90; roll ≤ 8° p90 on every segment, including the
  lead forearm through the straight-arm band; the label swap is found.
- **(c) Identifiability.** Start γ at 90°, r at 1.0 and lengths at +10 %. The true values
  must be recovered within 2°, 2 % and 1 cm. If they are not, the camera parameters are
  frozen at the pair-route values in version 1, and §3.5 is rewritten to say so.
- **(d) Ablation.** Rerun with each term switched off in turn (lengths free per frame, no
  limits, no contact, no shaft, no smoothness). Each constraint's contribution is a number
  in a table, not a claim.
- **(e) Face-on only.** The same swing with the DTL view removed, error reported per bone.

### 8.2 Corpus (two-camera: 07-04 s4–15, 06-11)

Judged per swing, by count, never by one score. Mark judges the table before anything is
committed.

| Check | Against | Pass |
|---|---|---|
| Reprojection residual, each view | its own 2-D pose | ≤ 1.5 σ median per joint |
| Raw-triangulation length CV per bone | — | reported: this is what the rigidity is correcting |
| Pelvis and thorax turn | the pair route | ≤ 5° rms, address → impact |
| Spine bend, knee flex at address | `dtl_posture` | inside its published sd |
| Shaft plane from the fitted hands + shaft | `club3d` 60.3° ± 0.7 (7i) | ≤ 1.5° |
| Fitted γ, r | pair route's 75–84°, 1.12–1.25 | inside those ranges |
| Face-on-only refit | the two-camera fit, same swing | per-bone error table: **this sets whether face-on-only swings get the panel** |
| Lead wrist angles, vision-only fit | HackMotion wG3, where worn | reported against the criterion; no gate in version 1 |
| `labelSwap`, `limitHeld`, `footSlip` | — | counted per swing; outliers looked at by hand |

### 8.3 UI

`--probe-qml` offscreen checks: the panel is instantiated, the rig has 19 models, the
presets move the camera, and the tier chip text is right. Then a paused P4 in all five
presets, for Mark to judge.

## 9. When camera calibration lands

The camera parameters in §3.2 stop being unknowns: they become fixed inputs from
`camera_calibration_design.md`. The heading becomes real, so the top view can draw the
target line. The schema does not change; `camera.calibrated` flips to true.

## 10. Build order

| Phase | Deliverable | Done when |
|---|---|---|
| 1 | `ybot_rig.h` + `tools/extract_swing3d_rig.py` + forward kinematics + the synthetic generator | §8.1 (a) passes |
| 2 | The estimator with r_2D, lengths, markers, limits, smoothness, contact; swinglab output | §8.1 (b)–(e) pass; §8.2 table filled and judged by Mark |
| 3 | r_shaft, r_grip, r_IMU, r_HM; session pooling of lengths and camera parameters | lead-forearm roll through the straight-arm band passes §8.1 (b) *with* the shaft term and is shown to fail without it |
| 4 | `SwingRigDriver`, `SwingViz3DView`, panel wiring, presets, club, ball | §8.3 passes; soak clean on Mac and GOLFSIMPC |
| 5 | Traces, 3-D LM flight, optional X-bot skin | shown on a swing carrying the data |
| 6 | Calibrated cameras (§9) | — |

Phases 1–3 carry all the risk. The panel is deliberately last, because a picture of an
ungraded fit would be judged by eye, and by eye is exactly how a plausible wrong skeleton
gets through.

## 11. Open questions for Mark

1. **"Z-bot":** did you mean Mixamo's **X-bot**? It is not in the repo. The Y-bot is enough.
2. **Face-on-only swings:** decided by the §8.2 face-on-only row rather than now. Agreed?
3. **ScreenWrist:** should the wrist screen get the panel too? With HackMotion in the fit it
   may be most useful there.

## 12. Built and graded (26 September 2026)

### 12.1 What was built

| Piece | Where |
|---|---|
| The Y-bot, extracted for this view alone: 19 bone-local segment GLBs + the rig header | `tools/extract_swing3d_rig.py` → `src/Resources/swing3d/seg_*.glb` (1.0 MB), `src/Analysis/skeleton3d/ybot_rig.h` |
| The rig: 48 scalar DoFs, one forward kinematics shared by the fit and the view | `src/Analysis/skeleton3d/skeleton3d_rig.h` (plain C++) |
| The estimator | `src/Analysis/skeleton3d/skeleton3d_fit.{h,cpp}` (Eigen) |
| JSON (`pinpoint.skeleton3d/1`) writer, tuning keys (`skeleton3d.*`), the view's reader | `src/Analysis/skeleton3d/skeleton3d_json.h` |
| The stage (after KinematicSequence, both profiles; feeds nothing) | `Skeleton3DStage`, `wrist_analyzer.cpp`; `kSkeleton3DStageVersion = 1`; `analysis.skeleton3d` |
| Athlete height into the job (live: the profile; re-analysis: `athlete.heightM` now written into swing.json) | `ShotAnalysisJob::athleteHeightM`, `shot_processor.cpp`, `swing_reanalyzer.cpp`; swinglab `--height-m` |
| The panel | `src/Gui/swing3d/swing_rig_driver.{h,cpp}` (`SwingRigDriver`), `SwingViz3DView.qml`, `SwingViz3DHost.qml`; key `swing3d` in `PpModeStage` / `PpViewPanel` / `ScreenSessionMode` |
| Tests | `src/Analysis/tests/skeleton3d_test.cpp`, `src/Gui/tests/swing_rig_driver_test.cpp`, `tools/probes/swing3d_panel.qml` |
| Corpus rig | `tools/swinglab/skeleton3d_run.sh`, `tools/swinglab/skeleton3d_grade.py`; results `docs/research/data/skeleton3d/grade_20260926.{csv,md}` |

Nothing in `src/Gui/viz/`, `src/Resources/body/`, `CapturePage.qml`, the session wizard or
`tools/extract_body_segments.py` was touched (§6.1).

### 12.2 What changed from §3–§6, and why — each found by the synthetic suite or the corpus

1. **Marker offsets are fixed priors, not fitted.** Fitting a free 3-D offset per keypoint is a
   *gauge*: a constant shift of a rigid segment's markers images exactly like a constant rotation
   of the segment. The synthetic swing reprojected to the noise floor with the head tilted 30°,
   the lead foot pitched 46° and the pelvis turned 30°. The shoulder and hip keypoints ride the
   clavicle and the pelvis as surface points (acromion, trochanter) with ONE symmetric width and
   height offset per pair — a symmetric shift is not a rotation.
2. **Bone lengths are frozen at the Y-bot's proportions × height by default**
   (`skeleton3d.fitLengths` turns fitting on). ~10 shared scales against ~7 000 keypoint
   residuals means the length prior carries no weight at all, and any camera-model error is
   soaked up as bone length: the corpus fit read a 1.96× spine and a 0.2× head. A 2 % prior on the
   overall scale did not hold it either (the head shrank to pay for it). On synthetic data — no
   model error — fitted lengths recover to 2 % (test (c)); on the corpus, the `lengthsFitted`
   ablation moves the body 21° p90 from the frozen fit. **The claim in §0 that "bone lengths are
   one value per swing, solved" is true of the mechanism and not of the data: until the cameras
   are calibrated (§9), the height decides the lengths.**
3. **The head turns at the neck base** (the Y-bot's Neck joint), not its Head joint 10 cm
   higher: address is a flexed neck, and pivoting at the skull base made the corpus fit shrink the
   head to a fifth of its size to get the face down to the ball.
4. **The image scale is read from the legs**, not the eye-to-ankle height: at address the golfer
   is bent, and reading that height as standing height under-read the scale by 20–30 %.
5. **The camera model gained a DTL roll**; the face-on roll is fixed at 0 and its pitch has a 4°
   prior. Without gravity nothing measures "up": the face-on camera being near level is the
   convention (club3d makes the same one), and a face-on camera pitched *p* tilts the fitted world
   by about *p*/2. The corpus DTL camera is rolled 2.4–6.5° (median 5.2°) on its mount.
6. **A planted foot is a fixed 3-D anchor per marker, with no horizontal floor.** "All three foot
   markers at their flat-foot heights" is a claim the rig's ankles cannot always meet, and with
   the DTL free to roll it tilted the whole world (synthetic: 6.5 cm root-relative error → 1.0 cm
   without it). The floor is read off the anchors afterwards.
7. **The measured clubhead is an observation, and the club length a shared unknown** (the club
   record is its prior, σ 8 cm). A shaft *direction* fixes only two of the hand's three rotations —
   the hand can roll about the shaft without changing it — so §4's "the shaft is the roll witness"
   was half true. The head, ~0.9 m down the shaft, sees the rest.
8. **The grip term joins once the rest has settled.** Switched on from the stage-1 start, the
   two-arms-through-the-club coupling pulled the lead forearm into a rolled minimum (108° p90
   against 20° with it staged).
9. **Posture priors where the cameras cannot see:** the three spine segments coupled (4°) and a
   flexion prior (12°) — two hip keypoints cannot see pelvis tilt, so tilt, hip flexion and spine
   flexion otherwise trade freely; clavicles (10°); humeral rotation (35°) — a STRAIGHT arm's
   humeral rotation and pronation spin about the same line; wrist (30°) and pronation (45°).
10. **An IMU with an unknown mount pins how a segment moves, not where it points** — a constant
    rotation of the segment is indistinguishable from a different mount. With the mount from a
    calibration record it holds the lead forearm's roll to 5.5° p90 (synthetic).
11. **The solver** is a block-banded Cholesky with a Schur complement, and the Jacobian is exact
    and geometric (verified against central differences to < 1e-7) — no AutoDiff, no sparse solver.
12. **The View3D is never destroyed** (the design said "destroyed when hidden"): one instance per
    screen in `SwingViz3DHost`, lent to whichever slot is showing.

### 12.3 Synthetic (§8.1) — `skeleton3d_test`, 234 frames at 120 Hz, σ = 2 px

| | bone direction p90 (body / all) | roll p90 | joint pos p90 (root-rel.) | lead-forearm roll p90 |
|---|---|---|---|---|
| (b) two views | **2.17° / 4.41°** | **7.6°** | **1.0 cm** | 23.4° |
| (b-HM) + HackMotion on the lead wrist | 2.16° / 4.40° | 5.2° | 1.0 cm | 11.8° |
| (b-IMU) + forearm & pelvis IMUs, mount unknown | 2.12° / 4.35° | 9.4° | 1.0 cm | 10.8° |
| (b-IMU) …mount from a calibration record | 2.12° / 4.32° | 5.0° | 1.0 cm | **5.5°** |
| (c) from a wrong start (lengths fitted) | 2.76° / 5.18° | 8.8° | 1.3 cm | 20.4° |
| (e) face-on only | 6.57° / 17.6° | 25.8° | 4.2 cm | 36.3° |

"Body" excludes the hands' own direction, which rests on the grip prior. The rest-pose round trip
is exact to 5e-7 m; the face-on label swap at the top is found; (c) recovers the upper arm 1.063
(truth 1.07), shank 0.940 (0.94), forearm 1.040 (1.04) and γ 78.0° (78.0°). Design targets: bone
direction ≤ 3° **met for the body, missed with the hands**; roll ≤ 8° **met**; lead-forearm roll
≤ 8° **missed without an instrument** (met only with a calibrated forearm IMU). Ablation (d):
limits and smoothness carry the most (off: 12.9° and 14.9° bone p90); the shaft halves the
lead-forearm roll error (45.5° → 23.4°). 468 frames solve in 2.7 s.

### 12.4 The corpus (§8.2) — the 24 two-camera swings, 26 September

All 24 fitted; **parity 24/24 identical** (the stage changes nothing else in the document).
Medians over the 24 (per-swing table: `docs/research/data/skeleton3d/grade_20260926.md`):

| Check | Against | Median (p10–p90) | Design gate |
|---|---|---|---|
| Solve | — | 154 frames, 1.68 s (1.40–1.77) | ≤ 1 s — **missed** |
| Reprojection, all markers | own 2-D pose | face-on 6.9 px, DTL 4.9 px | ≤ 1.5 σ — **missed** (σ ≈ 2–4 px) |
| Raw triangulation length CV | — | upper arm 0.19, forearm 0.24, thigh 0.06, shank 0.11 | reported (what rigidity corrects) |
| γ, r | pair route 75–84°, 1.12–1.25 | 77.9° (76.4–82.4), 1.11 (1.09–1.20) | inside — **met** |
| Downswing plane | club3d down plane | +1.8° (−1.6…+4.4) | ≤ 1.5° — **borderline** |
| Spine bend at address | `spineForwardBend` | +4.8° (3.3–6.2) | inside its sd — **missed (a bias)** |
| Lead / trail knee flex at address | `lead/trailKneeFlexion` | +5.0° / +8.0° | inside its sd — **missed (a bias)** |
| Pelvis turn rate | `pelvisAngularSpeed` (pair route) | corr 0.51, rms 88 °/s | ≤ 5° rms of the turn — not comparable (rates only are persisted) |
| Thorax turn rate | `thoraxAngularSpeed` | corr 0.35, rms 324 °/s | **poor** — which route is wrong is not known |
| Face-on only vs two views | the full fit | body bone p90 33° | — this is Mark's §11 q2 number |
| DTL roll | — | 5.2° (2.4–6.5) | — |
| Frames at a joint limit / foot slip p90 | — | 16 / 22 mm | — |

Ablations on the corpus (body bone direction p90 against the full fit): cameras frozen 26°,
lengths fitted 21°, no smoothness 13.5°, no limits 9.5°, no contact 7.8°, no grip 1.8°, no shaft
1.0°, no clubhead 0.9°; vision-only 0° (no swing here carries an IMU or HackMotion stream).
Worst residual keypoint: the ankles (face-on lead ankle on 15 of 24 swings).

**Tiers.** On the two-camera swings 99 % of joint-frames tier `measured` ("seen by both
cameras") and on face-on-only 100 % `constrained`. ⚠ The per-joint σ behind the tiers is the
fit's own posterior: it knows the keypoint noise, not the camera-model error the residuals show
(~7 px ≈ 2 cm at the golfer). The chip therefore says *which cameras saw it*, and claims no
accuracy.

**Soak (§6.5, on screen, Mac).** 30 minutes on screen (`tools/probes/swing3d_panel.qml --probe-soak 30 --probe-grab`): the playhead scrubbed at 30 Hz (54 000 steps), the preset changed every 3 s and the view moved to a NEW slot every 10 s (180 reparentings). The one view survived throughout (same object, available), the app log shows no error, and all eight grabs (every 5 min + the end) rendered the rig — none blank. Mac only; GOLFSIMPC owed (§12.5).

### 12.5 Owed, and open

- **Camera calibration (§9) is what the remaining misfit is waiting for**: the 7 px face-on
  residual with DTL (2.9 px face-on alone), the knee/spine biases, the ball's placement (a single
  face-on ray meeting the fitted floor — ~10 cm on the rendered frames), and trustworthy fitted
  lengths.
- **The thorax rate** disagrees with the pair route (corr 0.35). Neither is graded against truth.
- **GOLFSIMPC**: the per-file `-O2` for the solver is not available under MSVC's `/RTC1`, so a
  Debug studio build runs the solver unoptimised — expect several seconds per shot there. The
  GOLFSIMPC soak is owed.
- **HackMotion mapping signs** (`skeleton3d.hmFlexSign/hmRadSign`) are unvalidated; no corpus swing
  carries a wG3 stream alongside two cameras.
- **Library re-analysis** has not been run: existing swings get `analysis.skeleton3d` the next
  time they are re-analysed (the stage is version-stamped, never reused).
- §11 q1 (X-bot) — not done; q3 (ScreenWrist) — not done.
