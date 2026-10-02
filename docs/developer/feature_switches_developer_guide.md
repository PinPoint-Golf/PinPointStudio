# Pinpoint Feature Switches — Developer Guide

**Audience**: Developers changing analysis behaviour, running SwingLab gates, or wondering why a feature "does nothing"
**Location**: `src/Core/pp_tuned_constants.h` (frozen defaults), per-module `*Config` structs in `src/Analysis` / `src/Pose` (their `fromOverrides`), `src/Analysis/analysis_tuning.h` (the override mechanism), `src/Gui/app/app_settings.h`, `CMakeLists.txt`
**Status**: Snapshot at `a7380850` (2026-10-01). Every default below was read at that commit; history comes from `git log -S` plus the commit bodies and gate documents cited. When a switch flips, update its row here in the same commit.

**Vocabulary**: **LIVE** = on by default, runs in production. **DARK** = built, off by default, OFF path byte-identical to "not built". **mode** = a numeric/enum constant where a sentinel (usually `0`) disables the feature. **oracle** = a switch that exists only to reproduce an older or reference behaviour for A/B; it is never meant to flip.

---

## Contents

1. [How switches work](#1-how-switches-work)
2. [Everything that is DARK, and why](#2-everything-that-is-dark-and-why)
3. [Analysis switches by area](#3-analysis-switches-by-area)
   - 3.1 IMU orientation filter · 3.2 HackMotion frame · 3.3 Scoring & assessment rules · 3.4 Offline pose · 3.5 Head · 3.6 Ball · 3.7 Shaft onset & takeaway · 3.8 Shaft tracker (seg, snap, address, phase, hands) · 3.9 Impact boundary, impact geometry & follow-through · 3.10 P-positions & blur wedge · 3.11 Synthesis tiers · 3.12 DTL continuous track · 3.13 Shaft fusion · 3.14 Skeleton3D · 3.15 Event refinement & timeline fusion · 3.16 Kinematic sequence · 3.17 Kinematics, tempo, ball position, shaft plane · 3.18 Shaft uncertainty
4. [Switches outside the analysis pipeline](#4-switches-outside-the-analysis-pipeline)
   - 4.1 Environment variables · 4.2 Build options · 4.3 App settings · 4.4 PPCP / transport · 4.5 Video, IMU, launch monitor · 4.6 Export & storage · 4.7 Metric presentation
5. [Known stale comments and inert switches](#5-known-stale-comments-and-inert-switches)
6. [Adding or flipping a switch](#6-adding-or-flipping-a-switch)

---

## 1. How switches work

### 1.1 Three layers

1. **`pp_tuned_constants.h`, the frozen default.** This header is the single source of truth for parameters that the validation programme tunes and then freezes (`docs/validation/pipeline_validation_and_tuning.md` §2.4). It holds only literals. `tuned_constants_parity_test` guards that the indirection stays byte-identical.
2. **The module's `*Config` struct.** Each struct defaults its fields from `tuned::…`, or from its own literal for switches that never went through the header. Examples: most `shaft.*`, `shaft.dtl.*`, `shaft.fusion.*` and `skeleton3d.*` switches live only in their struct.
3. **The dotted-key override.** `analysis_tuning.h` holds no keys. It provides the typed `tuning::apply(ov, "<area>.<field>", field)` overloads. Each consumer's `fromOverrides()` applies the keys it owns. The main ones are `ShaftV3Config::fromOverrides` (`shaft_track_assembly.cpp`), `DtlShaftConfig` (`dtl_shaft_config.h:404–442`), `shaft_fusion_json.h:46–72`, `skeleton3d_json.h` and `wrist_analyzer.cpp`.

**Production passes an empty override map**, so the struct default *is* the shipping behaviour. SwingLab sets keys with `--params <file>` (nested or flat JSON) or `--set`. **Unknown keys are logged and ignored**, so check `runner.log` when a knob seems to do nothing.

### 1.2 The "dark idiom" and the freeze

Almost every analysis feature since June 2026 has followed the same lifecycle:

1. **Built DARK.** The default is off, and the OFF path is proven byte-identical over the corpus (typically "dark run 61/61 byte-identical" on the pose2 corpus).
2. **Gated.** The keys-off vs keys-on run uses the same binary, against truth marks or a launch monitor. Typical gates: the 17-swing truth set for Address/Takeaway, the 61-swing pose2 corpus for coverage, the 11 truth-marked swings for P-positions, the six 08-18 launch-monitor pairs for clubhead speed, and 283 hand-marked frames for impact lean.
3. **Flipped by Mark's call.** The commit title usually says `FREEZE … ON` or "flipped", and the body carries the gate numbers. A few switches are ON **by Mark's decision even though the written gate failed**: DTL HELD, η and synth3d (§3.12, §3.13). Their rows say so.

Score and rule constants (`score.*`, `rules.*`) are **frozen until labels exist**. SwingLab refuses to sweep them without `--allow-frozen`.

### 1.3 Flipping a default does not reach the stored library by itself

Re-analysis reuses recorded stage outputs when the producer version matches (`src/Analysis/analysis_versions.h`). The shaft stage is reused only when pose and ball are reused **and the override map is empty**. When it is reused, the recorded **ladder comes back with it**, and EventRefine, PositionsLadder and TimelineFusion are skipped (`ctx.job.ladderPreloaded`).

| You flip… | Reaches re-analysed swings? |
|---|---|
| A pose / ball / shaft-stage default (`shaft.*`, `positions.*`, `impactBoundary`, `emitTakeaway`, `topRepair`…) | **Only if you bump** `kPoseStageVersion` / `kBallStageVersion` / `kShaftStageVersion` |
| A ladder default (`refine.*`, `positionsLadder`, `fusion`, `fusionP1`) | **Only if you bump `kShaftStageVersion`**, because the ladder rides with the track |
| A DTL / fusion / skeleton3d default | Bump `kDtlShaftStageVersion` / `kShaftFusionStageVersion` / `kSkeleton3DStageVersion` |
| A downstream unversioned stage (`sequence.*`, `tempo`, `ballpos`, `kinematics.*`, `shaftPlane`) | Yes, these always re-run |
| A synth-tier or follow-through default | Yes: synth is always re-synthesised, and follow-through demotion is applied to a reloaded track (df044ff2) |
| Anything, in a SwingLab sweep | Yes, because any override defeats shaft/ladder reuse |

Precedents for flip + bump in one commit: b419d646 (markerless stack ON, shaft v1→2) and b82de8a3 (η / reflectBands ON, fusion → v5). Every switch flipped before 2026-09-09 predates versioning and has no paired bump.

---

## 2. Everything that is DARK, and why

This is the list to read first. Each row is off in production today.

| Switch | Key | Why it is dark | What would flip it |
|---|---|---|---|
| Re-fusion from raw IMU | `filter.refuse`, `filter.adaptive` | Lab-only levers for the IMU filter study (023ade2c, 2026-06-26). No gate pass is recorded. | Validation §2.4 C1→C2→C3: per-phase imu_vision_corr up, post-impact FE/RUD/PS inside the HackMotion limits of agreement, one schedule generalises |
| Grip from smoothed hands | `pose.gripFromSmoothedHands` | WB4, "built DARK … awaits its own corpus evaluation" (dc17cd38). Hands at address are the most occluded landmarks. | Its own corpus gate (never run) |
| IMU-less pose wrist assessment | `pose.wristAngles.enabled` | WB4. Against HackMotion the camera-plane geometry explains only 31–47% of lead-wrist variance, and the fitted scale ranges −0.09…−0.32 per swing: "no stable correction" (1293ccd1, 619d53d5) | A correction that is stable across swings |
| Filtered trail-wrist curve | `pose.wristAngles.filterCurve` | It gains only +0.03 agreement but moves ~23% of graded trail-wrist readings between bands (31% at P4) (619d53d5) | **A criterion instrument on the TRAIL wrist**. A bigger corpus will not settle it. |
| Physical-core ORT threads | `pose.intraOpThreads = -1` | Determinism is unproven on other topologies | A thread-count A/B on no-SMT, hybrid P/E and >16-logical hardware |
| Chin in head centroid | `head.chinConfWeight` (0) | "Face channels may be noisy" (dc17cd38). No gate is recorded. | — |
| tk0 overwrites Address | `ball.tk0AddressOverride` | **Oracle.** Fired on the first fidget (w2s4 −0.134 → −1.533 s). Turning it off took Address median 0.564 → 0.060 s (7b8098a3) | Never; tk0 should become the Takeaway instant |
| Hand-axis θ prior | `shaft.handAxisPrior.enabled` | WB4, "dark until its own corpus gate flips it" | Its gate (never run) |
| Head from segment terminus | `shaft.seg.placeHead` | 73 px vs hand truth against 34–48 px for the head pass it would displace (e319d148) | — |
| Wrist-cock table v2 | `shaft.wedge.kinModelV2` | Residual 74.3° → 20.9°, but it cost 38% of WEDGE stamps and 8–15 P-emissions, because the table also drives the trigger (bfb03bd3) | Separate the wedge **trigger** from its **centre** (`docs/research/wrist_cock_model.md`) |
| Sub-frame impact retime | `shaft.impactGeom.retime` | De-biases (\|err\| 18.3 → 15.9 ms) but scatters individual swings −20…+19 ms (02cf2b0f) | "Until the geometry earns the extra ~15 ms" |
| Two-sided impact override | `shaft.impactGeom.overrideLater` | **Oracle.** One-sided since 02e04113 (0703_0007 +254 → −7 ms) | Never |
| Follow-through forearm test | `shaft.followThrough.minShaftForearmDeg` (0) | On a full finish the folded club and the forearm align face-on (21–26°); it demoted 25 good samples (df044ff2) | Not recorded |
| Ball as a P7 reading | `synth.ballAnchorSigmaDeg` (0) | σ 3°: P7–P8 p90 13.5 → 8.5° **but** P6–P7 8.2 → 12.5°, and P7 speed moves ~9 mph with no LM to arbitrate (6a8b0fd7) | "A well-lit session with LM speed shows it helps" |
| DTL anchor on bridged frames | `shaft.fusion.dtlAnchor.enabled` | **Closed as degenerate.** Down the line, the view plane at impact *is* the swing plane (cond 0.06–0.08); only 2 of 71 frames are anchorable (b82de8a3) | An independent impact witness: the 3-D ball or a calibrated camera |
| Calibrated fusion | `shaft.fusion.calibrated` | An input flag, not policy: set only by a measured `--dtl-calib` | The two-camera protocol session |
| Fit bone lengths | `skeleton3d.fitLengths` | Freed lengths absorb camera-model error (corpus: 1.96× spine, 0.2× head), though they recover to 2% on synthetic data (d9b466f7) | — |
| Lean clavicles | `skeleton3d.leanClavicles` | Failed its gates: face-on-only 19.1° (p90 50°), DTL reprojection 6.02 px (3edbcba3, design §13.5–13.7) | — |
| Pool golfer values | `skeleton3d.pool{Club,Scale,Sym,Grip}` | The golfer re-grips every swing (pooled grip → p90 55°); the pooled club reads short. Pooling **cameras** pays, and that part is live. | — |
| Thorax on the FO+DTL pair route | `sequence.pairTrunk.thoraxPlacement` | 9/10 placements >1200 °/s or >80 ms ahead of the lead arm: the face-on shoulder line crosses square 160–190 ms before impact (840e37eb) | "A face-on shoulder that survives its own square-up" |
| Address/P1 arbitration | `refine.fusionP1` | Address is the reference instant for tempo, every Address-referenced pose metric and the replay trim; on the IMU path Address and club P1 sit ~97 ms apart (ff27b061) | Phase-2 gate in `docs/design/timeline-fusion.md` §4.4/§9.1, plus a rule that Address may not cross Takeaway |
| Truth-only DTL | `shaft.dtl.truthOnly` | **Instrument**: grades the band-lock coupling | Never |
| Python oracles | `shaft.rasterC2` (dark); `shaft.spanBound=false`, `shaft.psiRail=false` | **Oracle** (v3.0-r1 port, 58156dc7) | Never |
| Skeleton test hook | `skeleton3d.debugForceMirror` | Test hook | Never |

`refine.positionsLadder` is nominally LIVE but **inert**. Its stage requires `refine.fusion == false`, and fusion has been ON since 2026-08-19.

---

## 3. Analysis switches by area

Line numbers are at `a7380850`. "PTC" means `src/Core/pp_tuned_constants.h`.

### 3.1 IMU orientation filter

| Switch | Key | Default | What it does |
|---|---|---|---|
| `tuningWantsRefusion()` (`orientation_refuse_tuning.h:44`) | `filter.refuse` | **DARK** | Re-derives orientation offline from raw accel+gyro and feeds that quaternion into `ImuVisionFuser`, so filter tuning moves the wrist metric. |
| `RefuseConfig::adaptive` (`src/IMU/orientation_refuser.h:82`) | `filter.adaptive` | **DARK** | Phase-adaptive Madgwick (validation §5.3.1): dynamics gate, ±16 g saturation reject, gyro-only blanking around impact. Only matters with `filter.refuse` on. |
| `ImpactDetectorConfig::orientationGate` (`src/IMU/impact_detector.h:59`) | — | LIVE | "Kept permissive and disableable while tuning" (12241100). Nothing outside the tests overrides it. |

Both refusion switches arrived in 023ade2c (2026-06-26), "production path byte-identical (no … filter block unless the lab opts in)". Neither has ever flipped.

### 3.2 HackMotion frame

| Switch | Key | Default | What it does |
|---|---|---|---|
| `hmframe::kCandidate` (PTC:133) | `hmframe.candidate`, documented but **not read by any code** | **mode = 1 (C2, Ry(+90°))** | Picks one of four rotations from the HackMotion anatomical frame to ours. `-1` = no anatomical frame, and the lane drives nothing. |

Set directly to 1 in 17b0c9ce (2026-08-17) by `tools/hm_frame_select.py`, on the *sign* of a known bow and a known ulnar deviation. All four candidates score exactly 0 on cross-talk, so cross-talk could not decide. Three captures on one mounting each selected C2. This is a **mounting** constant (`wg3-mount1`): move the strap and you must re-select it. The ~17–18° residual is deliberately not corrected, so that one golfer's anatomy is not baked in.

### 3.3 Scoring & assessment rules

| Switch | Key | Default | What it does |
|---|---|---|---|
| `rules::kStrengthsRequireAdjacentFault` (PTC:199) | `rules.strengthsRequireAdjacentFault` | LIVE | A Good finding is shown only when the swing also has a live fault for it to protect against. |
| `scoring::bands::k*OneSided` (PTC:150–156) | — | **mode** (+1/0/−1) | Direction of each provisional wrist band. FlexExt +1 penalises cupping. RadUln and Pronation are two-sided. ArmFlexion −1 penalises a bent lead arm. |

The strengths policy dates from 56d9eab7 (2026-06-15) and moved into the header in 023ade2c. The rationale is the design rule in `docs/implementation/wrist_assessment.md:219`. It is frozen until labels exist. The band directions are PROVISIONAL and wait on Corpus 2 against HackMotion.

### 3.4 Offline pose

| Switch | Key | Default | What it does |
|---|---|---|---|
| `pose::crop::kEnabled` (PTC:236) | `pose.crop.enabled` | LIVE | Static person crop before ViTPose, falling back to the full frame. |
| `pose::decode::kDark` (PTC:242) | `pose.decode.dark` | LIVE | DARK sub-pixel heatmap decode (the name is the algorithm, not "dark launch"). Off = argmax ±0.25. |
| `pose::kIntraOpThreads` (PTC:233) | `pose.intraOpThreads` | **mode = 0** | `0` = clamp(hw/2,1,8); `-1` = physical-core auto (**opt-in**); `>0` = pinned. |
| `pose::grip::kFromSmoothedHands` (PTC:258) | `pose.gripFromSmoothedHands` | **DARK** | Grip anchor from the RTS-smoothed hands. |
| `pose::wristAngles::kEnabled` (PTC:261) | `pose.wristAngles.enabled` | **DARK** | Gates only the IMU-less `PoseAssessmentStage`. The trail-wrist series runs regardless. |
| `pose::wristAngles::kFeLimitDeg` (PTC:279) | `pose.wristAngles.feLimitDeg` | **mode** 120 (≤0 off) | Refuses frames whose \|apparent FE\| exceeds the limit (detector failures). |
| `pose::wristAngles::kFcHz` (PTC:292) | `pose.wristAngles.fcHz` | **mode** 6 Hz (≤0 off) | Low-pass for the σ noise estimate. |
| `pose::wristAngles::kFilterCurve` (PTC:313) | `pose.wristAngles.filterCurve` | **DARK** | Emits the filtered curve instead of the raw one. |
| `smoother::kLegsSigmaScale` / `kLegsJerkScale` (PTC:343) | `poseSmooth.legs*Scale` | **mode** 1.0 (inert) | Static σ scales on keypoints 11–16. |
| `smoother::adapt::kMode` (PTC:378) | `poseSmooth.adapt.mode` | **"accel"** | Motion-adaptive RTS window: `off` \| `accel` \| `innov`. `off` is the exact parity switch. |
| `smoother::adapt::kGroup` (PTC:379) | `poseSmooth.adapt.group` | "legs" | Which keypoints adapt (the WholeBody tail never does). |
| `smoother::kReacquireRun` / `kReacquireConfMin` | `poseSmooth.reacquireRun` / `.reacquireConfMin` | **LIVE** 3 / 0.7 (2026-10-02) | Re-acquisition: after 3 consecutive gate-rejected detections at confidence ≥ 0.7 the next such detection is accepted, so a joint the filter coasted off is taken back. `reacquireRun` 0 is the parity switch. `kShaftStageVersion` 7. Record: `kinematic_sequence_design.md` §8. |
| `PoseRunOptions::twoPass` (`pose_runner.h:85`) | — | false, **set true at runtime** | Two-pass pose. `wrist_analyzer.cpp:438` sets it on camera-only jobs unless `fullWindow` (bbbe8340). |

History:
- **crop + DARK decode** — dc17cd38 (2026-07-16, WB1). 16-swing subset: corpus mean 69.8 → 72.7, lead-wrist jitter −12%.
- **feLimitDeg / fcHz** — 1293ccd1 (2026-08-20). 83 swings: 681 frames refused; swings holding a >180° step 40 → 2; median curve moved 0.00°. 120° sits in the gap between the real lobe and a 167–180° failure lobe. 6 Hz because the HackMotion lead wrist holds 99.9% of its energy below 3.7 Hz.
- **legs scales** — 9cb276b3 (2026-09-05), "swept and not promoted". Jitter fell, but P7 samples moved 3–4σ: "a global scale cannot buy jitter without biasing the impact reading".
- **adapt.mode** — built `off` in 25588a59, promoted to `accel` in acb9a616 (both 2026-09-05) on the C15 bake-off: 17 settings × 11 swings, 6 passed every criterion. Winner: ΔP4 ≤0.34σ, ΔP7 ≤0.38σ, still-address jitter −29% sway / −48% plumb bob, 0 fallbacks. `innov` lost on margin (pelvisLift P7 moved 1.5σ, no divergence guard). The full-corpus confirmation is still owed.

### 3.5 Head

| Switch | Key | Default | What it does |
|---|---|---|---|
| `head::kChinConfWeight` (PTC:486) | `head.chinConfWeight` | **mode 0 → DARK** | Weight of the chin keypoint (31) in the head centroid. |

### 3.6 Ball

| Switch | Key | Default | What it does |
|---|---|---|---|
| `ball::corridor::kUseFeet` (PTC:501) | `ball.corridor.useFeet` | LIVE | Stance corridor v2 from the WholeBody foot keypoints. Falls back to the v1 ankle corridor. |
| `ball::activity::kClubActivity` (PTC:526) | `ball.clubActivity` | LIVE | Club-corridor activity signal around the ball. Feeds the P1 club-quiet mask and EventRefine's Tier-B at-ball gate. |
| `ball::kTk0AddressOverride` (PTC:539) | `ball.tk0AddressOverride` | **DARK (oracle)** | Lets the earliest ball-departure tk0 overwrite Address. |
| `ImpactAnchorConfig::enabled` (`impact_anchor.h:52`) | — | LIVE | Finds the address ball by its departure. Impact lean and low point read from it. |

- **clubActivity** — dark in a2a38e6e, FROZEN ON in 18fddda1 (2026-07-18, with EventRefine). 17 truth swings: worst \|P1 err\| 0.577 → 0.145 s, within 100 ms 12 → 14, zero regressions. Cost: ball stage +207 ms median.
- **tk0AddressOverride** — flipped off in 7b8098a3 (2026-07-17, "FREEZE fidget-proof Address/Takeaway ON"). Address error median 0.564 → 0.060 s; within 100 ms 4/17 → 12/17.
- **impactAnchor** — 3c2f23db (2026-09-29). Ball found 31/32; lean error sd 10.2 → 5.7°.

### 3.7 Shaft onset & takeaway

| Switch | Key | Default | What it does |
|---|---|---|---|
| `shaft::kOnsetReturnBoxPx` (PTC:982) | `shaft.onsetReturnBoxPx` | **mode** 7 (0 = off) | "No-return" onset veto. It can only push onset later, past fidgets. |
| `shaft::kOnsetRunBridgeFrames` (PTC:990) | `shaft.onsetRunBridgeFrames` | **mode** 10 (0 = off) | Merges fragmented fast runs so that a slow backswing competes as one run. |
| `shaft::kOnsetBridgeMinNetFrac` (PTC:1003) | `shaft.onsetBridgeMinNetFrac` | **mode** 0.2 (0 = off) | "m3gate": a bridged chain of ≥3 runs needs net ≥0.2 of its path. Kills grip-anchor flap. |
| `shaft::kEmitTakeaway` (PTC:1004) | `shaft.emitTakeaway` | LIVE | Vision Takeaway event at bs0. |

- **Veto, bridge, emitTakeaway** — dark in a2a38e6e, redesigned in 8c9575a5, FROZEN ON in 7b8098a3 (2026-07-17, after in-app review). Box 7 is robust across a 5/9/12 sweep.
- **m3gate** — 0 in 60e552e9, flipped to 0.2 in 32dfde10 (2026-07-18). Net/path separation is 25× (flap 0.013 vs ≥0.34 for every legitimate run). 19/61 swings moved the right way, 0 score changes.

### 3.8 Shaft tracker: seg, snap, address, phase, hands

| Switch | Where | Default | What it does |
|---|---|---|---|
| `seg.enabled` | `shaft_tracker_math.h:136` | LIVE | Markerless steel-segment lock (grip end → hosel scale). |
| `seg.placeHead` | :179 | **DARK** | Head placed from the segment terminus. |
| `seg.probeStill` | :181 | LIVE | Probes the still frames outside the evidence span along the nearest DP direction (25c0d925). |
| `snap.enabled` / `skipAddr` / `skipBlur` | `shaft_track_assembly.h:70/88/96` | LIVE | Layer A ridge re-registration, skipped on Address and Impact/Thru frames. |
| `head.enabled` / `head.projPrior` | `clubhead_track.h:69/143` | LIVE | Measured clubhead pass and its projected-reach prior. |
| `handAxisPrior.enabled` | `shaft_track_assembly.h:110` | **DARK** | WB4 hand-axis θ prior. |
| `topRepair.enabled` / `onsetReseed` | :294 / :321 | LIVE | Re-derives a collapsed Top; then re-runs onset from the lost backswing run. |
| `addr.ballWell` / `trailArmVeto` / `refuse` / `decoyCheck` | AddrConfig :359–371 | LIVE | Ball θ-well on still address frames; trail-arm veto; refuse (club metrics show "-") on a P1/length conflict with the ball; drop a decoy ball. |
| `phase.retry` | :389 | LIVE | Re-segments a suspect phase model at swSpd×0.75, at most twice. |
| `hands.enabled` | :400 | LIVE | Hand-pair / glitch cleaning, used only when the raw-hands track is invalid. |
| `fusion.enabled` | `club_length_fusion.h:81` | LIVE | Multi-estimator club-length fusion (a81887f3, 07-10). |
| `spanBound`, `psiRail` / `rasterC2` | :180, :329 / :327 | LIVE / **DARK** | Oracles for the Python port. |

- **Markerless flip** — b419d646 (2026-09-10) turned seg, snap and projPrior ON together. Gate on the unmarked 6-iron (7 swings, 38 marks): direction p50 5.5 → 1.6°, head p50 39 → 21 px. At address the snap re-registered onto the leg, hence `skipAddr`. Report: `docs/research/data/markerless/p6_flip_gate_20260910.md`. `skipBlur` came from e319d148: the snap worsened through-swing agreement 9.6 → 12.4°.
- **topRepair** — f7a1d159 dark, fda26779 ON (2026-08-10): 15 swings repaired, P6 recall 46 → 59, BLOCKED_PHASE 131 → 40. **onsetReseed** — 22471162: 11 near-edge-pinned swings converted, tempo census 56 → 60/61.
- **addr.* / phase.retry / hands** — af0f3c93 (2026-09-30), against the two 29 Sept failure families. 21 ball-visible swings 19/17 → 21/21; 15 Sept W02 3/13 → 13/13; pinned corpus 68/68 metrics identical. The retry signature marks 5/6 broken runs and 0/36 clean ones.
- **head.enabled** — cbe68cda dark, df76fe9d ON (2026-07-09): 9/10 swings ≤5% high-confidence-bad, measured-tier median ≤2.4 px.

### 3.9 Impact boundary, impact geometry & follow-through

| Switch | Key | Default | What it does |
|---|---|---|---|
| `impactBoundary::kEnabled` (PTC:1025) | `shaft.impactBoundary.enabled` | LIVE | Impact as a rate discontinuity: one-sided IN/OUT rates about P7. |
| `ImpactBoundaryConfig::floorInSlope` (`shaft_track_assembly.h:460`) | `shaft.impactBoundary.floorInSlope` | LIVE | Monotone rate into P7. |
| `ImpactGeomConfig::enabled` (`impact_geom.h:69`) | `shaft.impactGeom.enabled` | LIVE | The θ=θ_ball crossing arbitrates the acoustic impact anchor. |
| `retime` (:74) / `overrideLater` (:87) | `shaft.impactGeom.*` | **DARK** / **DARK (oracle)** | Sub-frame retime / the two-sided override. |
| `followThrough::kEnabled` (PTC:1043) | `shaft.followThrough.enabled` | LIVE | Demotes a post-impact sample faster than the swing's own pre-impact peak (floored at 1200 °/s) to a coast. |
| `followThrough::kMinShaftForearmDeg` (PTC:1046) | `shaft.followThrough.minShaftForearmDeg` | **mode 0 → DARK** | Forearm-alignment demotion. |

- **impactBoundary** — 3c201a58 (2026-09-06), with `kinematics.composed` and `synth.curveRate`. On the six 08-18 LM pairs the speed peak moved −17…−19 ms → −1…−9 ms and the pre-impact SD went 3.3 → 1.8 mph. Two deeper probes were rejected and are not in the tree: splitting the φ Gaussian at impact, and DP prior ×0.25 across contact (SD 1.9 → 5.2 mph).
- **impactGeom** — 7ee515cd dark, 02cf2b0f ON (2026-08-10): fixed 8 clamp-corrupted impacts (0703_0002 +234 → −10 ms). One-sided override from 02e04113 (2026-09-11).
- **followThrough** — df044ff2 (2026-09-17), triggered by 15 Sept pitch shots where the tracker captured the lead arm (a 190° flip in 80 ms). Sample angles and timing were unchanged elsewhere; 28/55 swings touched, a median of 2 samples each.

### 3.10 P-positions & blur wedge

| Switch | Where | Default | What it does |
|---|---|---|---|
| `PositionsConfig::enabled` | `shaft_positions.h:109` | LIVE | Report-only P1–P8 `positions[]`. |
| `PositionFitConfig::fitEnabled` / `useFusedLen` | :79 / :101 | LIVE | B2 milestone fitter; draw fits at the fused club length. |
| `skipMeasuredConf` / `ballBonus` | PositionFitConfig | **mode** 0.5 / 0.6 (0 = off) | Never-degrade guard; head-near-ball reward. |
| `p6LastCrossing` | :147 | LIVE | P6 = the LAST horizontal crossing in (P4, P7). |
| `wedge.enabled` / `kinCone` | `shaft_wedge.h:52/67` | LIVE | R8 blur-wedge fan sweep + WEDGE tier; off-envelope penalty. |
| `wedge.kinModelV2` | :83 | **DARK** | Corpus-fitted wrist-cock table. |
| `wedge.leadEdge` | :104 | LIVE | Reads the blur's leading edge, not its centroid. |

- **positions** — b3b8432f / bdb84ff0 dark, 45caf240 ON (2026-07-11, B4). Every P at or better than the B1 baseline; P7 direction 176.7° → 22.8°. `useFusedLen` (8589f20b): measured-tier P2/P4 had got worse (8.4 → 11.6 px).
- **p6LastCrossing + wedge** — d0c9ff27 (2026-08-10). P6 truth 11/11 within 5 ms (was 7/11); P6 emission 19 → 46, P5 11 → 46; phase-blocked measures 431 → 131. kinCone converted 3 truth swings for −1 valid. Gate: `docs/implementation/shaft_wedge_p6_impl.md`.
- **leadEdge** — a0391fa3 (2026-09-29). Impact lean +12.5° → −0.4° median on 32 hand-marked P7s; the old +13° was the blur's *trailing* ridge.

### 3.11 Synthesis tiers

| Switch | Where | Default | What it does |
|---|---|---|---|
| `synth.enabled` | `shaft_synthesis.h:102` | LIVE | Layer C 240 Hz visualisation tier, also the input to `lowPointAhead` (4826501a dark → 2e03926f ON, 07-16). |
| `synth.curveRate` | :108 | LIVE | Analytic Hermite θ̇ (3c201a58). |
| `synth.measuredEndAfterImpact` | :129 | LIVE | A post-P7 bracket is bridged only into a measured anchor (df044ff2). |
| `synth.fitEvidence` | :161 | LIVE | Fits every reading under a θ̈ plausibility term, σ_a 5000 °/s² (a0391fa3: tuned on 283 marked frames, P7–P8 \|median\| 9.5 → 6.5°). |
| `synth.ballAnchorSigmaDeg` | :177 | **mode 0 → DARK** | Hands→ball line as a soft P7 reading (see §2). |
| `poseSynth.enabled` | `pose_synthesis.h:55` | LIVE | 240 Hz skeleton viz tier; no metric reads it. |

### 3.12 DTL continuous track (`shaft.dtl.*`, `dtl_shaft_config.h`)

| Switch | Line | Default | What it does |
|---|---|---|---|
| `enabled` | 53 | LIVE | Master gate for the DTL tracker. |
| `truthOnly` | 57 | **DARK (instrument)** | Band lock only, no face-on witness. |
| `schedule.enabled` | 77 | LIVE | Face-on visibility schedule ρ̂_D (ablation switch). |
| `held.enabled` | 105 | LIVE | **Rule 1, HELD**: fills ≤6-frame RAY-gate holes bounded inside one band. |
| `endOnFirst` | 113 | LIVE | Rule 2: classify END_ON before the OCCLUDED quarantine. |
| `quarantineCause` | 118 | LIVE | Rule 3: OCCLUDED → OCCLUDED_WRIST / OCCLUDED_ROW. |
| `edge.enabled` | 128 | LIVE | Rule 4: short runs beside a band (or ρ̂ ≥0.70) become bands. |
| `lenSchedule` | 138 | LIVE | Rule 5: one drawn length, ρ̂_D·L̂_D. |
| `refuseLateEscape` | 142 | LIVE | Rule 6: a post-P8 corridor escape is not published. |
| `corridor.enabled` | 146 | LIVE | D5 face-on corridor cost. |
| `synth3d.enabled` / `useEta` | `dtl_shaft_synth3d.h:73–74` | LIVE (preview) | 3-D synth shaft projected into the DTL tile, rotated by η(t). |
| `dtlPosture.enabled` | `dtl_posture.h:92` | LIVE | DTL posture metrics (379bbfbe, 09-21). |

The base tracker came in 20fefcc8 (2026-09-20, swinglab only) and went in-app in 2764eef9 (09-21). **The six rules** arrived in 3109778d (2026-09-30), each with a bit-identical OFF (21/21). Gate: `docs/research/data/markerless/dtl_continuous_20261002.md` §3.4. Drawn coverage went 0.69 → 0.80 mid-backswing and 0.66 → 0.75 delivery. **The ≥0.85 target was not met**; the residual is band-edge tails, and the ceiling is structural. Truth pairs unchanged, planes moved 0.00°.
- **HELD is ON by Mark's decision (1 Oct)**, to be judged in the app on the re-analysed 4 July session. The gate as written would have left it off.
- The other five passed their own gates: END_ON only relabels absences; edge earned 18 measured frames; lenSchedule cut flicker 14 → 5 on s7; refuseLateEscape refused 10 frames, all post-P8.
- **synth3d** was dark from 3109778d and turned ON in b82de8a3 by Mark's decision. It remains a preview through the assumed-zero camera until the calibration session.

### 3.13 Shaft fusion (`shaft.fusion.*`, `shaft_fusion.h`)

| Switch | Line | Default | What it does |
|---|---|---|---|
| `enabled` | 115 | LIVE | FO+DTL view planes → 3-D shaft and plane (b49a9dea, 09-21). |
| `calibrated` | 126 | false (input) | Set only by a measured `--dtl-calib`. |
| `eta.enabled` | 149 | LIVE | (A) η(t) out-of-plane curve; feeds only the DTL synth. |
| `dtlAnchor.enabled` | 162 | **DARK, closed** | (C) DTL view-plane ∩ phase-plane on bridged frames. |
| `reflectBands.enabled` | 172 | LIVE | (D) Re-reads a mirrored backswing DTL band when the backswing fit is incoherent. |

All from b82de8a3 (2026-10-01); gate in `dtl_precalibration_20261003.md`.
- **A (η)** is ON **by Mark's decision; its leave-one-band-out gate failed** (mid-backswing 6.8 → 7.7° p50), because η is not continuous across an end-on gap on this golfer.
- **C** is off and closed (§2).
- **D** is ON by its gate: 07-04 s2/s3 rms 20.9/18.0° → 12.0/4.4°, 0 of 21 coherent swings changed.
- **B** (skeleton camera as fusion seed) stopped at the cross-check, so no switch exists: skeleton 12.6° vs stick 9.0–9.9°, disjoint ranges.

### 3.14 Skeleton3D (`skeleton3d.*`)

| Switch | Where | Default | What it does |
|---|---|---|---|
| `enabled` | `skeleton3d_fit.h:70` | LIVE | Batch 3-D skeleton fit. |
| `useDtl/useLimits/useContact/useShaft/useGrip/useClubhead/useSmooth/useImu/useHm` | :71–79 | LIVE | Per-residual ablation switches. |
| `fitCameras` / `fitDtlRoll` | :80–81 | LIVE | Fit camera geometry / the DTL roll. |
| `fitLengths` | :87 | **DARK** | Fit bone-length scales. |
| `labelSwap` | :88 | LIVE | Test the mirrored L/R assignment per view and frame. |
| `splineBasis` / `leanRig` | :100 / :103 | LIVE | B-spline trajectories; shared spine (48 → 42 angles). |
| `leanClavicles` | :104 | **DARK** | Clavicles follow the upper arm. |
| `usePlane` / `useCataloguePlane` / `branchPass` | :166–171 | LIVE | Swing-plane prior on DTL-blind frames, the catalogue fallback, and the mirror-branch pass. |
| `debugForceMirror` | :187 | DARK (test) | Test hook. |
| `pool{Club,Scale,Sym,Grip}` | `skeleton3d_pool.h:59–62` | **DARK** | Hold the golfer's pooled values fixed. |
| `ReanalyzeOptions::useSessionPool` | `swing_reanalyzer.h:103` | LIVE | Use the session's pooled cameras. Forced off in SwingLab pass 1. |
| `pelvisYawAccRad` | `skeleton3d_fit.h` (FitConfig) | LIVE (200) | The pelvis yaw's own acceleration σ, rad/s², never loosened through the downswing (0 = the general σ). |
| `footToeLiftM` / `footHeelLiftM` | `skeleton3d_fit.h` (FitConfig) | LIVE (0.06 / 0.03) | The shod foot: the toe and heel keypoint markers lifted along the foot's up axis (0 = bare-sole priors). |

- **Base fit** — d9b466f7 (2026-09-26).
- **Plane / branch** — 2a7e57a9 (09-28): P8 off-plane 51.9° → 6.0°; face-on-only 50° → 15.8°.
- **Lean + splines + pool** — 3edbcba3 (09-28), graded in `docs/design/swing_3d_viz_design.md` §13.5–13.7. Spine-only lean + splines was best of four (face-on-only 15.8 → 13.45°). Pooling cameras only gives 11.15°.
- The doc records that leaving `leanClavicles` off departs from Mark's answer to q1, on the numbers.
- **pelvisYawAccRad** — 5bef75f3 (2026-10-02), `kSkeleton3DStageVersion` 4. With the general σ (about 1800 rad/s² in the fast window), the pelvis stopped and restarted at the ball on 15/15 07-04 swings.
  - At 200: implied acceleration 11 700 → 3 700 °/s²; reprojection, slip and limit holds unchanged; `hip_stall`'s P6→P7 rate 160 → 225 °/s, and three false stalls cleared.
  - At 100 the result is the same; at 400 a shallow dip remains.
  - `{"skeleton3d.pelvisYawAccRad": 0}` is the control.
  - The record is `docs/research/data/kinematic_sequence/skeleton_rate_k0_20261002.md` §10.
  - A debug term ledger sits beside it, behind the env var `PINPOINT_SKEL_TERMS=<path>`.

### 3.15 Event refinement & timeline fusion (`refine.*`)

| Switch | Key | Default | What it does |
|---|---|---|---|
| `refine::kEnabled` (PTC:1070) | `refine.enabled` | LIVE | Master gate for EventRefineStage: retimes Takeaway/Address from the finished shaft, ball and pose products. Never touches Impact; camera ladder only. |
| `kTakeaway` / `kAddress` (PTC:1071–1072) | `refine.takeaway` / `refine.address` | LIVE | Per-event switches. |
| `kImpactResidual` (PTC:1073) | `refine.impactResidual` | LIVE | Log-only launch−impact telemetry. |
| `kPositionsLadder` (PTC:1084) | `refine.positionsLadder` | LIVE but **inert** | Promotes club P2/P3/P5/P6/P8; runs only when `refine.fusion` is off. |
| `kFusion` (PTC:1110) | `refine.fusion` | LIVE | TimelineFusionStage: a measurement displaces a proxy, by measurement class and estimand ownership. |
| `kFusionP1` (PTC:1112) | `refine.fusionP1` | **DARK** | Address/P1 arbitration. |

- **refine** — e8ee1c4d (master OFF), FROZEN ON in 18fddda1 (2026-07-18, with `ball.clubActivity` and minConf 0.5 → 0.8). Median held at 0.052 s, worst 0.577 → 0.145 s; 61-swing corpus: 3 movers, 0 score changes.
- **positionsLadder** — 5d4ab4d1 dark, f056e1e8 ON (2026-08-09). 61/61 byte-identical dark, +289 resolved rows, ladder order clean on all 11 dual-carry swings.
- **fusion** — ff27b061 dark, 896d0c34 ON (2026-08-19); gate in `docs/implementation/timeline_fusion_impl.md` (GOLFSIMPC). OFF parity 61/61. On the truth-marked eleven: P6 −39 → +6 ms, P8 +96 → +7 ms, P10 +1686 → 0 ms; retained slots moved exactly 0 ms. Gate 6's human eyeball was not done; flipped "on Mark's call".

### 3.16 Kinematic sequence (`sequence.*`, `segment_rates.h`)

| Switch | Key | Default | What it does |
|---|---|---|---|
| `sequence::kEnabled` (PTC:754) | `sequence.enabled` | LIVE | Master gate for the kinematic-sequence producer. |
| `kFaceOnTrunkPlacement` (PTC:794) | `sequence.faceOnTrunkPlacement` | LIVE | Face-on pelvis/thorax nodes may be placed, but only inside the sighted band (\|turn\| ≥20°); elsewhere they are BOUNDS. |
| `kPairTrunkEnabled` / `kPairTrunkPlacement` (PTC:821–822) | `sequence.pairTrunk.*` | LIVE | Paired FO+DTL trunk route (between the IMU and face-on rungs) and its own placement gate. |
| `kPairTrunkThoraxPlacement` (PTC:865) | `sequence.pairTrunk.thoraxPlacement` | **DARK** | Thorax node placement on the pair route. |
| `kSkel3dLeadArm` | `sequence.skel3d.leadArm` | LIVE | The lead arm from the two-camera skeleton (`faceOn+dtl3d`), below a lead-arm IMU and above the face-on arm. |
| `kSkel3dMinUsableFrac` | `sequence.skel3d.minUsableFrac` | LIVE (0.8) | Below this usable fraction of the domain a skeleton rung steps aside. |
| `kSkel3dTrunk` / `kSkel3dPlacement` | `sequence.skel3d.trunk` / `.placement` | LIVE | The pelvis/thorax from the two-camera skeleton, **below the pair** and above the face-on span, with its own placement gate. |
| `kSkel3dThoraxPlacement` | `sequence.skel3d.thoraxPlacement` | **DARK** | Thorax node placement on the skeleton route. |
| — | `sequence.skel3d.scaleFrac` | LIVE (0.10) | The assumed camera's gain on a skeleton rung's peak σ; 0.03 once skeleton3d is calibrated. |
| `kPeakTimes` | `sequence.peakTimes` | LIVE | Emit `pelvisPeakTime` / `thoraxPeakTime` (placed nodes only) — what `sequence_order` reads. |

- **sequence.enabled** — ON from birth in 3b8dc071 (2026-09-17).
- **faceOnTrunkPlacement** — born OFF in 3b8dc071: the nodes were spikes from the address *reference*. ON in c1b7c995 (09-18) after the square-up reference replaced it. Pelvis 0 placed / 45 bounded; thorax 2 / 42. Design §12.4.
- **skel3d.leadArm** — bfa63ced (2026-10-02). On 07-04 the rung fired on 14/15 swings:
  - the node moved from a median 100.5 to 107.3 ms before impact, and σ_t from 18.7 to 14.3 ms;
  - s10 went from unresolved to partial;
  - it agrees with the face-on arm to a median 6.7 ms.

  Mark: "a material difference in stability and plausibility". The record is
  `skeleton_arm_g3_20261002.md`. With `false`, the output is byte-identical apart from timings.
- **skel3d.trunk** — built dark, then measured on 07-04 (`skeleton_rate_k0_20261002.md` §11).
  - Ranked above the pair, it held on 3/15 swings and placed nothing, saying what the pair said
    with 3–6× the timing σ.
  - So it was moved BELOW the pair and turned ON (Mark: "if it does no harm we may see
    benefits"). There it replaces the blind-banded span wherever the pair cannot produce.
  - The confirmation sweep, on vs off, 15 swings: routes, placements, instants and verdicts
    identical. On 07-04 it fires on none of them.
  - `{"sequence.skel3d.trunk": false}` is the control.
- **pairTrunk** — 840e37eb (2026-09-20), measured on 21 two-camera swings. The pelvis went from an 84 ms face-on bound to "did not peak before impact" on 20/21. **No trunk node is placed**, because on this golfer both trunk rates are still rising at impact, so placement "cannot be validated until a swing that peaks the trunk in the downswing, or rotation truth, exists". `{"sequence.pairTrunk.enabled": false}` is the named face-on control.

### 3.17 Kinematics, tempo, ball position, shaft plane, club delivery

| Switch | Key | Default | What it does |
|---|---|---|---|
| `kinematics::kEnabled` (PTC:1152) | `kinematics.enabled` | LIVE | Unscored clubhead speed, hand speed and lag curves (a2723181, ON at birth "by product decision: display-only, unscored, additive"). |
| `kinematics::kComposed` (PTC:1165) | `kinematics.composed` | LIVE | Clubhead speed = \|v_grip + L·θ̇·n̂\| from the track's own rate, masked past P7 (3c201a58: 1.004±0.030 → 0.959±0.022 of LM; the old figure was two errors cancelling). |
| `tempo::kEnabled` (PTC:931) | `tempo.enabled` | LIVE | `tempoBackswing`, `tempoRatio` (cbbf6f0a, 07-21). |
| `ballpos::kEnabled` (PTC:950) | `ballpos.enabled` | LIVE | Ball position at address on the heel-to-heel line (cbbf6f0a). |
| `shaftPlane::kEnabled` (PTC:1182) | `shaftPlane.enabled` | LIVE | Face-on transition-plane delta (ad29b01c, 08-11; placeholder corridor μ0 σ25, "normless was tried first and was worse"). |

cbbf6f0a records that its corpus gates are "still owed" (OFF-parity, Top error, corridor re-centre); no later record of them was found. `clubDelivery::` has no switches, only thresholds. `diagnostic_ledger.h` has none either: `warmUpWeight = 1.0` disables warm-up handling but is a weight, not a switch.

### 3.18 Shaft uncertainty (`uncertainty.*`, `uncertainty_config.h`)

Design: `docs/design/shaft_uncertainty_propagation_design.md`. Calibration and gate verdicts: `docs/research/data/uncertainty/calibration_20261001.md`. The σ is computed after the values and is never fed back into them, except where a row below says it is a value change.

| Switch | Key | Default | What it does |
|---|---|---|---|
| `uncertainty::kEnabled` | `uncertainty.enabled` | LIVE | The master switch. Off ⇒ the pinned corpus is byte-identical to 27e1e96d (90 of 90 swings, 1 Oct, version stamps and wall-clock timings excluded). On ⇒ every shaft sample carries σθ / pGross / tier, P-positions carry σ_t, and the shaft metrics carry a per-reading σ, sigmaKind and grossRisk. |
| (config) | `uncertainty.shaftTable` | LIVE | U1: the tier × phase-group σθ / pGross table (`kSigBaseDeg`, `kPGross`, generated by `calibrate_sigma.py --emit-cpp`). |
| (config) | `uncertainty.synthPosterior` | LIVE | U3: the synth curve's posterior σ and Monte Carlo draws. Scaled by `uncertainty.synthSigmaScale` (`kSynthSigmaScale` = 0.40, fitted on held-out marks). |
| `kSynthSoftAnchors` | `uncertainty.synthSoftAnchors` | DARK | U3: the P-anchors become soft constraints weighted by their σ. **A value change**: the synth curve moves. |
| `kOneImpact` | `uncertainty.oneImpact` | DARK | U4: lean, the speed mask and the peak lead read the ladder Impact instead of the trigger or the P7 knot. **A value change.** |
| `kFbPosterior` | `uncertainty.fbPosterior` | DARK | U5: forward–backward marginals replace the U1 table on in-span frames. It failed its gate (held-out ±2σ 76–80 % vs the table's 96 %; Spearman 0.59 vs 0.67). It stays a trace diagnostic (`fb_sigma`, `fb_palt` at five temperatures under `--trace`). |
| (config) | `uncertainty.planeBootstrap` | LIVE | U6: block-bootstrap σ for the fused planes, the address plane and the face-on conic (with `pNeedle`). |
| `kSequenceSigma` | `uncertainty.sequenceSigma` | DARK | The kinematic sequence's club node reads the per-sample σθ instead of `shaftThetaSigmaRad / conf`. **A value change**: the wider σ un-places the club node on some swings. Mark's decision, not a gate's; the moved-swing list is in the calibration report. |
| (knobs) | `uncertainty.mcDraws`, `.seed`, `.synthKappa`, `.synthSigmaScale`, `.fbTemperature`, `.rho`, `.bootstrapN`, `.bootstrapBlock` | 200, fixed, 0.33, 0.40, 1, 0.84, 200, 5 | Sweep knobs. The seed makes every σ reproducible (`det_rng.h`). |

Version stamps bumped with this: `kShaftStageVersion` 6, `kDtlShaftStageVersion` 3, `kShaftFusionStageVersion` 6. A library re-analysis is needed for stored swings to gain σ (§1.3).

**Diagnostics** (`tuned::diagUncertainty`, `session_diagnostics_design.md` §A8). These are compile-time constants, with the report tool's per-run overrides through `SessionDiagnosticsModel::setUncertaintyModes`:

| Switch | Default | What it does |
|---|---|---|
| `kEnabled` | LIVE | Readings carry their measurement σ, and every finding carries P(fire), gross risk and `quantified`. Verdict-identical: gate G1, 0 mismatches over 7,260 rows. |
| `kSoftTier` | LIVE | The Wilson bound takes expected counts, and each condition gets P(Pattern) from 200 deterministic draws. Gate G3: the 3 tier moves on the library all rested on borderline shots. |
| `kPosteriorRank` | LIVE, pending Mark's review of the rank-shift report | Roots rank by P(c \| evidence) × Σ q·s, with negative evidence, and carry a stability word (firm / likely / fragile). |

---

## 4. Switches outside the analysis pipeline

### 4.1 Environment variables

| Variable | Where | Default | What it does / why it exists |
|---|---|---|---|
| `PINPOINT_ENABLE_LLM` | `llm_controller.cpp:84` | unset → **DARK** | AI Coach gate (any value but `0`). 5adc5fa5 (2026-08-20): every launch was downloading Phi-4-mini (~4.9 GB) for a hatch the UI can't open. Runtime, not `#ifdef`, so the code doesn't rot. |
| `PINPOINT_PPCP_WIRED` | `ppcp_wired_link.cpp:257` | unset → **LIVE** | Only `=0` disables the USB wired link. Opt-in in c243aa2a, default ON in f3945312 (2026-08-29) once the cable took over an idle WiFi link instead of racing it. |
| `PINPOINT_PPCP_ALLOW_RESUME` | `ppcp_transport.cpp:493` | unset → resumption **OFF** | Repro only. A resumed TLS handshake resolves no PSK identity: 6 dials gave 3 links with no pairing id (d780efca). |
| `PINPOINT_PPCP_ACCEPT_ALL` | `shot_controller.cpp:480` | off | Bench override of the 50 ms corroboration rule (12/15 desk shots were refused). **Never in a real session** (c4fc1e4f; the default is tested by 89477de4). |
| `PINPOINT_PPCP_MAX_PREVIEWS` | `VideoInputPpcp.cpp:780` | no cap | Diagnostic cap on preview consumers (06e92fed). Note: disabling a camera does not suppress its preview. |
| `PINPOINT_PPCP_PRE_MS` / `_POST_MS` | `ppcp_host_service.cpp:1181` | 2000 / 1000 | Clip interval around t0 (b833c367). |
| `PINPOINT_SYNC_TRACE` | `ppcp_host_service.h:1181` | off | 1 Hz clock-convergence trace (e1766bed). |
| `PINPOINT_LOG_STDERR` | `pp_debug.cpp:309` | off | Echo the app log to stderr (12a8a56b). |
| `PINPOINT_BLE_TRACE` | `pp_debug.cpp:68` | off | `qt.bluetooth*` to stderr; needs `QT_LOGGING_RULES` (d4822b72). |
| `PINPOINT_CORE_{PACK,NORMS,CONTEXTS,SCREENS,DRILLS,REFERENCES}` | `src/Diagnostics/*` | Qt resource | Point the providers at the repo JSON. ctest sets them per target; **bare test binaries without them fake-fail**. `regrade_ledger` refuses to run without `PINPOINT_CORE_PACK`. |
| `PINPOINT_SPINNAKER_ROOT` / `SPINNAKER_ROOT` | `spinnaker_runtime.cpp:51` | Program Files search | Windows Spinnaker SDK location (7839dbd3). |
| `APPIMAGE` | `appimage_update.cpp:47` | set by runtime | Linux in-app update only. |
| `PINPOINT_ADDRESS_MARKS_DUMP`, `SK3D_BRANCH_DEBUG` (+ test-only `SK3D_GEN_DEBUG`, `SK3D_LEAN_EXPERIMENT`, `PINPOINT_PLANE_CORPUS/GOLDEN`, `PP_COVERAGE_DUMP`, `PP_STORE_TIMING_DIR`, `PPCP_ADVERTISE_HOLD_S`, `PPCP_BS_PROBE/RELAY`) | various | off | Debug dumps and live-endpoint probes. |

### 4.2 Build options

| Option | Default | What it does / why |
|---|---|---|
| `PP_SHIPPING_BUILD` | OFF | Asserts Release, refuses the harness options, makes `AppInfo::devBuild` false (which kills `--probe-qml`). Set by every packaging script and release CI. c2a3c79d: an embedded libwrist had shipped unoptimised. |
| `PP_ALLOW_NO_PPCP` | OFF | Without it, a shipping build lacking OpenSSL/libppcp fails to configure. 44038894: v0.1-beta1 shipped with no pairing and was pulled. |
| `PP_PPCP_PLAINTEXT_HARNESS` / `PP_PPCP_RV6_HARNESS` | OFF | Conformance listener / pairing-confirm harness; fatal with SHIPPING. |
| `PINPOINT_INSTALLED` | OFF | Packaged-build marker; the update engines stay inert without it. |
| `PINPOINT_DEBUG_LEVEL` | 1 | `ppDebug` compiles away below 3. |
| `PINPOINT_PROFILE_BASELINE` / `PINPOINT_PROFILE` | ON / OFF | Baseline profiling ships ("flip OFF for GA"); the deep tier also needs the runtime toggle. |
| `WITH_CUDA` / `WITH_COREML` / `WITH_VITPOSE` | ON | ORT providers and the ViTPose-B download. |
| `WITH_MEDIAPIPE` | **OFF** | BlazePose, optional and out of the UI since 9bf81163. |
| `WITH_ORTGENAI` | ON | Local LLM; forced OFF on Intel macOS. Still gated at runtime by `PINPOINT_ENABLE_LLM`. |
| `PP_LIB*_LOCAL` / `PP_LIBPPCP_FETCH` | ON / OFF | Prefer sibling checkouts. |
| `PP_OPENCV_NO_PIN` | OFF | 6f1d2bb0: OpenCV was choosing which Qt the app loaded. |
| `PINPOINT_BUILD_TOOLS` / `_LM_REPAIR` / `_STORAGE_TOOLS` | OFF / OFF / ON | SwingLab, lm_repair, pps_convert_library. |
| `PINPOINT_ENABLE_{ASAN,UBSAN,TSAN}` | OFF | Sanitizers. |

`HAVE_*` defines (OPENCV, PPCP, SPINNAKER, ONNXRUNTIME, SPARKLE, …) are capability detection, not policy.

### 4.3 App settings that act as switches (`app_settings.h`)

| Key | Default | Notes |
|---|---|---|
| `launchmonitor/enabled` | true | Off without losing config; default true so GCQuad users aren't cut off (62f9f58a). |
| `launchmonitor/standaloneShots` | **false** | Record a shot from an LM reading nothing else saw. Off because a capturing session would otherwise record every ball hit on the sim (e153740c). The gate is a pure function because it twice misbehaved. |
| `hackmotion/enabled` | true | wG3 discovery (49a346af). |
| `General/autoDetectSwing`, `acousticShotDetectionEnabled`, `checkForUpdates` | true | |
| `General/cloudFallback{Stt,Tts,Llm}` | false | Force the cloud engines. |
| `ui/sessionDiagnosticsCadence` | "bandwidth" | Surfacing only; the ledger is identical either way (81314b58). |
| `ui/diagnosticsGradePolicy` / `diagnosticsNormSetsOff` / `metricsHidePlanned` | standard / [] / false | |
| `ui/autoReplayAfterCapture` / `replayTrimToSwing` | true / false | |
| `storage/*` | see §4.6 | |

Non-persisted: `CameraManager::livePoseEnabled` (true); `CameraInstance::kPoseEnabled` (constexpr true, a manual flip for capture-rate diagnostics); `PpCameraFrame.qml` `showPredictedShaft` / `showPredictedEnvelope` (**DARK** R7 dev overlays, never set); `--probe-qml` (dev builds only, 67eca962).

### 4.4 PPCP / transport

| Switch | Where | Default | Notes |
|---|---|---|---|
| Wired link | `ppcp_wired_link.cpp` | LIVE | §4.1 |
| TLS resumption | `ppcp_transport.cpp:493` | OFF | §4.1 |
| Corroboration window | `shot_controller.h:206` | 50 ms | A phone shot needs a host detector within 50 ms. The value is CORE §5.10's proposal, not a measurement (e949cfec). |
| Ingest fps floor | `ppcp_ingest_policy.h:80` | 120 fps | Host policy outside the protocol (63eb71b0). `acceptProfileWithNoDeclaredRate` = true. |

### 4.5 Video, IMU, launch monitor

- **macOS fps floor** — `kMinUsefulCameraFps = 120` (`video_input_factory.cpp:73`): hides confirmed <120 fps AVFoundation cameras; an unknown rate is not filtered (9843e653).
- **Spinnaker timestamps** — no switch. Since 304b5567, the camera clock is mapped via `DeviceClockMapper`, falling back to host arrival when `deviceNs ≤ 0`.
- **Impact-camera CRF** — `kImpactClipCrf = 12`, applied as min(job, 12) (da5dcc17).
- **IMU** — `orientationGate` (§3.1).

### 4.6 Export & storage

| Switch | Default | Notes |
|---|---|---|
| Document format (`swing_store.h`) | writes **.ppsw**; reads both, .ppsw wins | 4d8ba169 (2026-09-23) |
| `storage/videoQuality` | "low" = **CRF 28** | 53977035: 2.5 vs 7.1 MB per swing per camera for ~0.5 px pose error. `SwingExportJob`'s own default is still 23, but the app always overrides it. |
| `storage/videoCodec` / `videoResolutionMode` | h264 / native | "4k"/"1080p" retired 2026-09-23 |
| `storage/saveRawFrames` / `skipAnalysisForRawCapture` | false | corpus capture (f82766e4) |
| `storage/savePoseKeypoints` / `saveImuStreams` | true | |
| `archiveAfterDays` / `archiveFloorGb` / `trashRetentionDays` | 0 = **all auto behaviour OFF** | "Moving a golfer's sessions off the library is never something an upgrade starts doing" (edcc9934). |

### 4.7 Metric presentation

- `MetricCardSpec::drawsCurve()` (`metric_descriptor.h:346`) is derived, not set: an instant-only card draws no curve and no legend chip (7d098ad7).
- `windowedMean` (40 ms) is false only for impactShaftLean and shaftLie, because lean turns ~2°/ms through impact (045cb989).
- `mergeInto` squeezes shaftLie onto the lean card (14fb44d1).
- `planned` is derived from routes and filtered by `ui/metricsHidePlanned`.

---

## 5. Known stale comments and inert switches

Found while compiling this guide. These are worth fixing the next time the file is touched:

- `shaft_track_assembly.h:445` and `:450` say `synth` and `impactBoundary` are "enabled=false by default (dark)". Both are LIVE.
- `kinematic_series.h:57` comments `kinematics.enabled` as "(dark)", and so does `docs/validation/tunable_parameters_reference.md:61`. It is LIVE.
- `ShaftPlaneConfig` (`wrist_analyzer.cpp`) says "(dark)". The PTC comment says "normless", but the measure ships with a placeholder corridor.
- `hmframe.candidate` is documented as an override key, but no code reads it.
- **UI toggles with no consumer**: `General/sendDiagnostics`, `display/hardwareAcceleration`, `camera/syncEnabled`, `General/aiCoachingOnSessionEnd` (which defaults true while the coach is dark), and `imu/saveCalibrationToFlash`. Only their settings panels read them. `display/postShotMirror` / `postShotContent` are kept only so stored profiles load.

---

## 6. Adding or flipping a switch

1. **Build it dark.** Put the default in `pp_tuned_constants.h` if validation will tune and freeze it; otherwise in the module's `*Config`. Name the dotted key in the comment and apply it in `fromOverrides()`. A disable sentinel (`0`) is fine for numeric modes. Say so in the comment.
2. **Prove the OFF path byte-identical** over the corpus before any gate.
3. **Gate with the same binary**, keys off vs keys on, against truth and with a control. Record the numbers in the commit body and the gate doc.
4. **Flip on Mark's call.** Put the gate numbers in the flip commit, update the constant's comment from "dark" (§5 shows what happens if you don't), and update this guide's row.
5. **Bump the producer version** in `analysis_versions.h` if the switch lives in a reused stage (§1.3); otherwise the library keeps the old output.
6. If a switch is ON against a failed gate, or OFF for good (degenerate, oracle), **say so in the comment**. §2 depends on it.
