# PinPoint — Whole-Body Landmarks (COCO-WholeBody 133): Design Proposal

**Status:** Proposal (2026-07-13). **Built same day (WB0–WB3, uncommitted):** 133-kp
offline contract + swing.json widening (byte-parity gated), swing-level person crop +
DARK decode (corpus mean 69.8→72.7 on the 16-swing raw subset; one shaft-assembly
validity interaction escalated — /mnt/swingdata/runs-wb1gate/{TRIAGE,ESCALATION}.md),
head tracking (headSway/headLift/headTilt series), feet consumers (stance corridor v2,
stanceWidth/flare/toe-line/leadHeelLift, overlay foot edges). Low tier removed (Q2).
**WB4 built DARK** (defaults-off, byte-parity verified): `pose.gripFromSmoothedHands`,
`shaft.handAxisPrior.*` DP unary, `PoseWristAngleSource` (apparent angles, IMU-less
only), hand overlay edges (ViewLayout "hands":"off") — each awaits its own corpus
evaluation before defaulting on. Q3 avatar deferred; Q4 face-on-only enforced.
**Scope:** exploit the 133-keypoint COCO-WholeBody output of the ViTPose models
we already ship — feet, hands, and face channels — to (a) raise the effective
resolution of joint tracking on the offline analysis path, and (b) extend the
body-, shaft- and ball-estimation algorithms with the new landmark groups.
**Non-goals (v1):** changing the live 60 Hz path (MoveNet, 17 kp — stays as
is), 3D lifting / multi-view triangulation, clubface orientation, replacing the
IMU wrist pipeline.

---

## 1. Where we are — the models already do this

Both offline pose models are the **wholebody** ViTPose exports:

| Tier | Model file | Output | Delivery |
|---|---|---|---|
| Medium | `vitpose-b-wholebody.onnx` (~330 MB) | `[1,133,64,48]` heatmaps | bundled in `models/` |
| High | `vitpose-l-wholebody.onnx` (1.23 GB) | `[1,133,64,48]` heatmaps | on-demand download (`MotionCaptureProbe`) |

Input is `[1,3,256,192]` for both, so both variants flow through one decode
path (`src/Pose/pose_estimator_vitpose.cpp`). The model computes **all 133
channels on every frame today** — widening our decode adds *zero* inference
cost. What we currently do with them:

- **Body 0–16** — decoded (`kBodyJoints=17`, vitpose.cpp:50) into
  `PoseResult` / `PoseFrame2D`. This is everything downstream consumers see.
- **Hands 91–132** — decoded when `setDecodeHands(true)` (only `PoseRunner`
  enables it) into the sibling struct `WholeBodyHands` (21+21,
  `pose_estimator_vitpose.h:44-49`), then **collapsed to two centroids**
  (`leadHand`/`trailHand`, `pose_runner.cpp:280-302`) used as the grip anchor.
  The per-knuckle data is discarded.
- **Feet 17–22 and face 23–90** — computed by the model, never decoded.

So "add support for the broader landmark set" is a decode-loop and
data-contract change, not a model change.

### 1.1 COCO-WholeBody index layout (for reference)

```
0–16    body (COCO-17, identical indices to today — the tail is purely additive)
17–22   feet: 17 L-bigtoe, 18 L-smalltoe, 19 L-heel, 20 R-bigtoe, 21 R-smalltoe, 22 R-heel
23–90   face (68-pt: jaw 23–39 incl. chin 31, brows, nose, eyes, mouth)
91–111  left hand  (21: wrist root, then 4 per finger, thumb→pinky, MCP/PIP/DIP/tip)
112–132 right hand (21, same layout)
```

Because 0–16 are unchanged, every existing index constant
(`shaft_tracker.cpp:51 kBodyJoints[8]`, `body_pose_adapter.cpp:29-37`,
`ball_runner.cpp:61-63`, the overlay edge lists) remains valid verbatim after
the arrays widen.

---

## 2. What the new landmarks buy us (golf-specific)

Ranked by expected value per unit of work:

### 2.1 Feet (6 kp) — stance, ground line, footwork

- **Ball-detection stance corridor v2.** `stanceCorridor()`
  (`ball_runner.cpp:69-95`) currently spans the *ankles* + a margin and floors
  at the *ankle line* — but the ankle sits 8–12 cm above the ground. Toe/heel
  keypoints give the true toe-line span and a ground line at actual shoe
  level: a tighter SEARCH-phase ROI and a better "ball below the feet" prior.
- **New setup metrics** (address, camera-first per the P-position rule):
  stance width (heel-to-heel px, convertible via the measured-club-length
  scale), foot flare angle per foot (heel→bigtoe direction), toe-line
  direction (alignment proxy on the face-on camera).
- **Footwork traces:** lead-heel lift (heel-y minus toe-y through the
  downswing) and trail-foot roll are classic coaching observables the Wrist/
  Swing analyzers currently cannot see at all.
- **Avatar feet.** `BodyVizView.qml` already has `leftFootNode`/`rightFootNode`
  meshes (lines 594/634) that are *hidden whenever a pose source is live*
  because no COCO keypoint drives them. heel→toe gives the foot direction;
  `BodyPoseAdapter`'s existing `zRot()` two-point quaternion pattern extends
  directly (quaternions only, per the project rule).

### 2.2 Hands (42 kp — already decoded, underused) — grip, wrist, shaft prior

- **Grip anchor v2.** Today's grip is a score-weighted centroid of each hand.
  The 21-point layout also gives a **hand axis** (wrist-root → middle-finger
  MCP) and a **knuckle line** (index→pinky MCP row). Two upgrades:
  1. a *grip direction prior* for the ShaftTracker DP — the hands grip the
     club, so the hand axis constrains shaft θ near the grip. This is an
     additional physics anchor of exactly the kind the untaped-club
     investigation needs (band locks currently carry wrap through the
     impact-blur gap; a per-frame hand-orientation prior is band-independent).
  2. a *split-hand sanity check* — lead/trail hand separation along the shaft
     direction validates handedness and grip detection.
- **Pose-based wrist angles.** The wrist assessment engine
  (`WristAssessmentEngine`, `wrist_assessment_contract.h:78`) consumes an
  `IWristAngleSource`; the only implementation is IMU-fed
  (`wrist_analysis_adapter.cpp`). A new `PoseWristAngleSource` — apparent
  flexion/extension from the forearm vector (elbow→wrist) vs the hand axis,
  apparent radial/ulnar from the knuckle line — gives:
  - an **IMU-less lesson mode** (no hardware on the club/arm), the top latent
    asset flagged in the lesson-model gap audit, and the missing corpus
    condition for the K5 vision-only shaft validation track;
  - a **cross-check channel** for IMU mount calibration drift.
  Honesty caveat: these are camera-plane *projected* angles, not anatomical
  3D angles. They are a resemblance/archetype signal (consistent with the
  per-archetype scoring estimand), not a replacement for the IMU DOFs; the
  source must carry appropriately lower confidence.
- **Hand smoothing.** The RTS smoother copies `leadHand/trailHand` through
  unsmoothed (`pose_smoother.h:56-59`). With hands as first-class keypoints
  they enter the same segmented-KF machinery, and the grip anchor (which
  drives ShaftTracker and span estimation) inherits the honesty tiers
  (Meas/Pred/Off) everything else already has.
- **Avatar hands.** `leftHandNode`/`rightHandNode` (BodyVizView.qml:429/535)
  are welded to the forearm today; the hand axis gives them a rotation.

### 2.3 Face (68 kp) — head stability (lowest priority)

Nose/eyes/ears (body 0–4) already support a head-sway trace, so the marginal
value of the face group is robustness (ears occluded on DTL views), a chin
point (jaw index 31) for chin-up/down, and a head-size scale. At our input
resolution the face occupies a handful of heatmap cells even after cropping
(§3), so expect noisy contours. **Recommendation:** decode and persist face
keypoints (uniform contract, §4) but build no face-specific algorithm in v1
beyond an optional head-orientation refinement for `headNode`/`neckNode`
(BodyVizView.qml:331-349). Revisit once crop-pass accuracy is measured.

---

## 3. The accuracy prerequisite: person crop + better sub-pixel decode

This section is the "higher resolution for joint tracking" half of the
proposal, and it **improves the existing 17 body joints too** — it should land
first and be validated independently of any new landmark consumer.

### 3.1 Problem

`PoseEstimatorViTPose` squashes the **full frame** to 192×256
(vitpose.cpp:304-322) — no person detector, no bbox crop, no letterbox.
ViTPose is a *top-down* model trained on person crops; feeding it full frames:

- wastes most of the 64×48 heatmap on background — in a studio face-on
  framing the golfer typically spans 40–60 % of the frame, so each joint's
  localisation grid is effectively ~2× coarser than the model was trained for;
- distorts aspect ratio (anisotropic squash), which the heatmap head was not
  trained to see;
- is fatal for the small structures: a hand at address is ~3×3 heatmap cells,
  a foot less, a face feature sub-cell.

### 3.2 Proposed design: swing-level static crop

Use the existing **two-pass architecture** in `PoseRunner`
(`pose_runner.cpp:321-408`): pass 1 already runs sparse full-frame inference
to estimate the swing span. Extend pass 1 to also accumulate the union bbox of
all body keypoints with conf ≥ threshold, expanded by a margin that covers
raised arms + club grip at the top of the backswing (the club head does NOT
need to be inside — pose only needs the person). Then:

- Pass 2 crops every frame with **one fixed, swing-level crop rect**, aspect
  locked to 3:4 (192×256), and back-projects heatmap peaks through the (now
  constant) affine transform.
- **Static, not per-frame, tracking crop** — deliberately. A per-frame bbox
  track couples crop jitter into keypoint coordinates and complicates
  determinism/re-analysis parity; a swing-level crop is one transform, zero
  added jitter, and captures most of the resolution gain because the golfer
  does not translate during a swing.
- **Fallback:** if pass-1 coverage is too sparse/low-confidence to trust a
  bbox, use the full frame exactly as today (bit-identical legacy behaviour).
- Persist the crop rect in swing.json (`pose2d.cropRect`) so re-analysis and
  the data viewer can reproduce/inspect it, and overlay painters can sanity-
  check coordinates.

Expected effect: ~1.5–2.5× effective pixel density per joint on typical studio
framing, with the biggest wins exactly where the new landmark groups need it
(hands, feet, face). This also directly benefits the shaft pipeline: grip
anchor and lead-arm φ noise (`phiRaw` gate, shaft_tracker.cpp:157-161) is the
declared enemy of the ψ wrist-rail work.

### 3.3 Sub-pixel decode: DARK refinement

`decodeHeatmapChannel` (vitpose.cpp:63-85) uses argmax + a fixed ±0.25-cell
shift toward the higher neighbour. Replace with DARK-style Taylor-expansion
refinement (log-heatmap gradient/Hessian at the peak, 3×3 neighbourhood):
deterministic, a few extra FLOPs per channel, and the published gain on
COCO-style heatmaps is several tenths of a heatmap cell — which at our 64×48
grid is a multiple-pixel improvement in frame space. Keep the ±0.25 path as a
compile-visible fallback for parity testing.

### 3.4 Confidence calibration note

Keypoint score is the raw heatmap peak (no sigmoid). The wholebody groups have
different peak-value distributions (face channels run hot, hand channels run
cold). Downstream thresholds are all group-agnostic constants today (0.25
overlay, 0.30 adapter/φ/ankle, 0.35 smoother, 0.5 data viewer). Introduce
**per-group threshold scale factors** as dotted-key tunables (additive — the
existing frozen constants keep their names and defaults for body joints).

---

## 4. Data-model and schema changes

### 4.1 Contracts

Two contracts exist; treat them differently:

- **Live contract — unchanged.** `PoseResult` stays `kNumKeypoints=17`
  (`pose_estimator_base.h:56`); the live path is MoveNet-only
  (camera_instance.cpp:330) and has no wholebody source. `BodyPoseAdapter`,
  the skeleton canvas, and the QVariantList export keep their shape. (The
  vitpose header's "never widened" note stays true.)
- **Offline contract — widen to 133.** In `swing_analysis.h`:

```cpp
inline constexpr int kWholeBodyJoints = 133;   // 0–16 body, 17–22 feet, 23–90 face, 91–132 hands
inline constexpr int kFootFirst = 17, kFaceFirst = 23, kLeftHandFirst = 91, kRightHandFirst = 112;

struct PoseFrame2D {
    int64_t t_us;
    std::array<QPointF,kWholeBodyJoints> kp;
    std::array<float,kWholeBodyJoints>  conf;
    QPointF leadHand, trailHand;   // kept: derived grip anchors, consumers unchanged
    float   handConf;
};
struct PoseKpAux {
    std::array<uint8_t,kWholeBodyJoints> tier;
    std::array<float,kWholeBodyJoints>   sigma;
};
```

  Rationale for one widened array rather than sibling `feet[]`/`face[]`
  structs (the `WholeBodyHands` pattern): COCO-WholeBody's tail is *additive*
  — indices 0–16 keep their exact meaning — so a single array preserves every
  existing index constant, keeps the smoother/serializer loops trivially
  generic (`for k < kWholeBodyJoints`), and avoids a second parallel indexing
  scheme. `WholeBodyHands` then becomes an internal estimator detail;
  `PoseRunner` copies hand channels into `kp[91..132]` and *derives*
  `leadHand/trailHand` from them as today.

- Memory/size: `PoseFrame2D` grows ~0.4 KB → ~2.7 KB; a 600-frame track is
  ~1.6 MB in RAM. Negligible against the video window.

### 4.2 swing.json (additive, no version bump needed)

Schema stays additive per the existing policy (readers key off presence, all
current readers use bounded loops — verified: `pose_runner.cpp:515`,
swinglab, disk replay, data-viewer models iterate `conf.size()`):

- `pose2d.frames[].kp` widens `float[51]` → `float[399]`, same
  `[x,y,conf]×N` layout, indices 0–16 unchanged. Old readers that loop
  `j<17` read exactly what they read today.
- `pose2d.smoothed[].kp/tier/sigma` widen identically.
- New: `pose2d.keypointCount` (17|133) for explicitness, `pose2d.cropRect`
  (§3.2), `pose2d.decode` (`"argmax+0.25"` | `"dark"`) for provenance.
- `lead`/`trail`/`handConf` kept as-is (now derived from smoothed hands).
- Size: +~2.5 KB/frame of JSON. For a 600-frame pose track that is ~1.5 MB —
  acceptable; if it bites, cap face precision at 3 decimals or add a
  `pose2d.groups` opt-out later. Writer changes are the two `for j<17` loops
  in `swing_doc.cpp:201,230`.
- **Re-analysis determinism:** `SwingReanalyzer` already replays the recorded
  `capture.motionCaptureQuality`; old swings (51-float kp) load fine and
  simply have no wholebody tail. Re-analysing an old swing with the new code
  regenerates pose from video, so it gains the tail — consistent with how
  re-analysis already upgrades old swings.

### 4.3 Smoother

Widen the driver loop (`pose_smoother.cpp:397`) to `kWholeBodyJoints` — the
`Kf3`/`smoothKeypoint` core is already per-scalar generic. 266 scalar filters
remain trivial CPU-wise. Additions:

- per-group `sigmaJerk`/`measSigBasePx` scale factors (hands move much faster
  than hips through impact; face barely moves) — additive tunables, body
  defaults untouched so existing corpus output is unchanged;
- after smoothing, recompute `leadHand/trailHand/handConf` from the smoothed
  hand keypoints (behind a flag — this changes ShaftTracker input and needs
  its own corpus gate).

### 4.4 Overlays

The two mirrored edge lists (`video_overlay_pose.cpp:81-87`,
`PpCameraFrame.qml:178-184`) gain optional wholebody edges (foot triangles,
hand skeletons) behind the existing View-menu per-element visibility pattern —
default OFF except feet, to respect the subtle-chrome preference. Face
contours are viewer/diagnostic only (Markup Lab), never a default overlay.

---

## 5. Algorithm extensions (per consumer)

| Consumer | Today | Proposed change |
|---|---|---|
| `BallRunner::stanceCorridor` (ball_runner.cpp:69) | ankle span + margin, floor at ankle line | toe/heel span, floor at ground (max toe/heel y); ankle fallback when foot conf < gate |
| `ShaftTracker` grip/φ (shaft_tracker.cpp:143-161) | grip = hand-centroid midpoint; φ = grip→lead-elbow | unchanged inputs, but sourced from smoothed hands; **new:** hand-axis θ-prior term in the DP unaries near the grip (off by default, `shaft.handAxisPrior.*` tunables) |
| P-positions (shaft_positions.h) | φ from elbow→grip | optionally refine φ with wrist→MCP hand direction where hand conf high |
| Wrist assessment | IMU-only `IWristAngleSource` | new `PoseWristAngleSource` (apparent flex/ext + RUD from hand axis/knuckle line), low confidence, camera-plane caveat; enables IMU-less lessons and IMU cross-checks |
| Metrics | — | stance width, foot flare, toe-line direction (address); lead-heel-lift trace; head-sway trace (from existing 0–4 + face chin) — surfaced via `MetricSeries`/`PpMetricChart` |
| BodyVizView / adapter | feet hidden, hands welded, head from nose | *offline/replay-driven only:* feet quaternions from heel→toe, hand rotation from hand axis, head refinement from face — requires the replay-time pose feed (see open question Q3) |
| Markup Lab / truth | 17-kp truth (`markup_truth.h:181`) | additive wholebody groups in truth schema for validating the new channels |

---

## 6. Phasing and gates

Each phase lands separately with its own gate; the corpus rules apply
(single-swing evidence is development-only; accuracy claims are corpus-scale
via SwingLab).

- **WB0 — decode + contract plumbing.** Decode all 133 in
  `PoseEstimatorViTPose`; widen `PoseFrame2D`/`PoseKpAux`/smoother loop/
  swing.json writer; `keypointCount` field. *Gate:* first-17 outputs
  **byte-identical** to pre-change on the parity corpus (same heatmaps, same
  decode math ⇒ identical by construction; the gate proves the plumbing).
  Cost: decode adds ~116 argmax passes over 64×48 floats/frame — sub-ms.
- **WB1 — crop pass + DARK decode.** §3.2/§3.3, `cropRect`/`decode`
  provenance fields. *Gate:* SwingLab corpus — grip/φ noise, shaft-track
  honesty %, P-position stability vs baseline; markup-truth px error on s01
  dense truth for body joints. Expect (and require) improvement; this changes
  bytes everywhere downstream, so it must precede any tuning built on top.
- **WB2 — feet consumers.** Stance corridor v2 + setup/footwork metrics +
  feet overlay. *Gate:* ball SEARCH acquisition rate on corpus ≥ baseline;
  stance metrics vs a small hand-measured truth set.
- **WB3 — hand consumers.** Smoothed hands → grip anchor (flagged), hand-axis
  shaft prior (flagged, evaluated on the untaped-corpus problem),
  `PoseWristAngleSource`. *Gate:* shaft corpus gate (down/thru %, FLIPS=0
  invariant) with flags on vs off; wrist source validated against IMU DOFs on
  calibrated swings (agreement bands, not equality).
- **WB4 — face/head + viz.** Head-sway metric, avatar feet/hands/head in
  replay. *Gate:* visual verification harness + head metric plausibility on
  corpus.

WB0/WB1 are the foundation and are valuable even if WB2–WB4 never ship: WB1
alone should measurably improve every existing pose-fed algorithm.

---

## 7. Costs and risks

- **Inference cost: zero** (model already computes 133 channels); decode and
  smoothing costs are sub-ms/frame. The 25 s re-analysis budget is untouched
  except by the crop pass, which is a `cv::warpAffine` per frame (~same cost
  as today's `cv::resize` it replaces).
- **swing.json growth** ~1.5 MB/swing worst case (§4.2). Mitigations listed;
  streaming writers unaffected (per-frame records).
- **Crop-pass regression risk:** a bad pass-1 bbox degrades everything. The
  full-frame fallback plus the persisted `cropRect` (inspectable in the data
  viewer) bound this; WB1's corpus gate is the arbiter.
- **Score-scale mismatch across groups** (§3.4) — mitigated by per-group
  threshold tunables; body thresholds frozen.
- **Hands through impact blur:** motion blur will hollow out hand heatmaps
  exactly when the shaft needs them most; the smoother's Pred tier and the
  DP's existing evidence weighting handle absence honestly — the hand-axis
  prior must be conf-gated so it vanishes rather than lies.
- **Projected wrist angles oversold** — guard with naming ("apparent"),
  low source confidence, and per-archetype (resemblance) scoring only.

## 8. Open questions

1. **Q1 — decode gating:** decode wholebody always, or only for
   Medium/High + a `pose.wholebody` tunable? (Proposal: always — cost is nil
   and provenance is simpler; the *consumers* are individually flagged.)
2. **Q2 — MoveNet-Low tier:** RESOLVED 2026-07-13 — the Low (MoveNet) offline
   tier was vestigial (the offline pose pass already only ever used ViTPose;
   Low and Medium were identical) and has been removed. The tier ladder is now
   Medium/High, both ViTPose, so this question no longer applies.
   (`vitpose-s-wholebody.onnx`, 97 MB, is also available upstream if a
   cheaper wholebody floor is ever wanted.)
3. **Q3 — replay-driven avatar:** animating feet/hands in `BodyVizView`
   needs a pose feed during replay (today the adapter is live-MoveNet-only).
   Feed the smoothed offline track through `BodyPoseAdapter` in replay, or
   defer avatar work until that plumbing exists?
4. **Q4 — DTL camera:** all algorithms above are face-on-first. Hands/feet on
   DTL are heavily self-occluded; do we gate the new consumers to
   `perspective === face-on` in v1? (Proposal: yes.)
