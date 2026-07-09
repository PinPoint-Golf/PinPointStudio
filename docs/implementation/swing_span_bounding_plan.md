# Swing-span bounding v2 — true-onset segmentation + two-pass pose

**Status: IMPLEMENTED** (2026-07-09, same day as proposed). Stages A + B +
telemetry all landed; §7.2's corpus gate ran against the pre-change tracker
as baseline: **G1 passed 6/6** (onset→impact 917–1017 ms, was ~550 ms),
measured-tier θ elsewhere byte-stable (p90 ≤ 1.3°), recovered zone converts
`pred`→ray on 5/6 swings. In-app re-analyse now follows the same
span-bounded two-pass path as live (`fullWindow` was the workaround for the
bs0 lag this plan fixed; it survives only as a SwingLab/debug escape hatch).
Open items: coverage dipped on swing_0004 (−0.056; wider honest span +
271 posed frames — remedy is pose budget, not boundary); live studio
verification of the < 20 s budget via the new telemetry; the §7 SwingLab
ball-lane gap (ball-on-reanalyse, deferred). Python parity is retired —
the C++ tracker is the development reference (`shaft_parity_test` deleted).

Originally proposed 2026-07-09, camera-only Wrist swings, from the
investigation on the six fresh captures in
`/mnt/swingdata/Mark-Liversedge/2026-07-09_Mark-Liversedge_Wrist_01`
(ball data, taped 7-iron, band centres recorded).

## 1. Problem

The analysis span is derived from `segmentPhases()`
(`src/Analysis/shaft_track_assembly.cpp`): `bs0` = first frame of the
biggest sustained run of smoothed grip speed above `swSpd` = 8 px/frame.
That is a *single high threshold* on a signal that ramps up slowly — the
club rotates about the wrists for hundreds of ms before the grip
translates fast — so bs0 systematically lands mid-takeaway, not at it.
Everything downstream inherits the error:

- **Phase labels are wrong** over the recovered zone: real early-takeaway
  frames are labelled `Addr`, so the DP runs them with the address |Δθ|
  ceiling (`wmaxAddr` 3°/f instead of `wmaxBackswing` 9°/f), without the
  backswing sign restriction, and (before the 2026-07-08 exemption) with
  ray publication suppressed.
- **The evidence span starts late.** `spanLo = bs0 − addressCollarUs`;
  the 400 ms collar added on 2026-07-08 papers over the lag but is a fixed
  guess layered on a wrong boundary.
- **The reported Address landmark** (`phasesToSegmentation`, and the swing
  band shown in Review) is bs0 — visibly late.
- **The v3.4 ball-anchor tk0 search is capped at bs0**, so its "find the
  real takeaway" exploit can only search inside an already-truncated
  domain — the chicken-and-egg Mark described: the ball pass that could
  fix address runs *after* the evidence that needs it.

Meanwhile the whole camera-only analysis takes ~45 s on the dev box
(~1 min in-app); the benchmark is **< 20 s**.

## 2. Evidence (2026-07-09 corpus, n = 6)

Camera: 150.7 fps, 1280×1024 BayerRG8, 747 frames / 5 s window.
`bs0` reproduced offline from the recorded pose2d bit-exactly matches the
recorded Address events.

| swing | impactUs | bs0 (=Addr evt) | true onset¹ | bs0 lag | onset→impact | bs0→impact |
|-------|---------:|----------------:|------------:|--------:|-------------:|-----------:|
| 0001  | 3458 ms  | 2905 ms | 2483 ms | 422 ms | 975 ms  | 553 ms |
| 0002  | 3477 ms  | 2922 ms | 2573 ms | 348 ms | 904 ms  | 555 ms |
| 0003  | 3425 ms  | 2875 ms | 2485 ms | 390 ms | 940 ms  | 550 ms |
| 0004  | 3453 ms  | 2825 ms | 2444 ms | 380 ms | 1008 ms | 628 ms |
| 0005  | 3455 ms  | 2910 ms | 2575 ms | 335 ms | 880 ms  | 545 ms |
| 0006  | 3445 ms  | 2641 ms | 2514 ms | 127 ms | 931 ms  | 804 ms |

¹ true onset = walk back from bs0 to the first smoothed-speed frame below
1.5 px/f (see §4). A lead-forearm-φ onset detector independently lands
within ~150 ms of it on all six.

Two structural regularities the fix leans on:

- **onset→impact = 940 ± 45 ms** across all six swings (my tempo with a
  7-iron). bs0→impact ≈ 550 ms — bs0 recovers barely half the backswing.
  Takeaway→Top should be ~0.7–0.9 s; the detected Addr→Top is ~300 ms.
- **Grip speed between true onset and bs0 never drops below 1.6 px/f**
  (smoothed) — the motion is continuous, so a low-threshold walk-back is
  stable. The address *hold* is NOT quiet (pose jitter: p95 3.2–4.6 px/f,
  peaks to 9 px/f), so the low threshold alone is not sufficient — the
  impact-anchored clamp in §4 is the safety rail.

Phase-label damage (swing_0001 trace): 434/745 frames `Addr`, only 43
`Backswing` (~285 ms — half the real backswing is mislabelled Addr).

**Timing breakdown** — measured on both boxes (2026-07-09):

| stage | dev box (Debug, CPU) | studio (GOLFSIMPC, Release, RTX 5080) | notes |
|---|---:|---:|---|
| full analysis | 44.9 s | — (no fresh log) | `swinglab_run`, camera-only |
| — pose pass | 34.4 s (111 ms/f × 310) | **5.5 s (18 ms/f × 310, CUDA EP active** — app log 2026-06-11**)** | **unbounded** in camera-only (the G3 bound needs IMU segmentation conf > 0, `wrist_analyzer.cpp`) |
| — of which decode | ~0.06 s | ~0.06 s | EA demosaic measured **0.2 ms/frame** (system OpenCV, release lib either way) — decode is noise; the pose cost is ORT inference and applies identically live (the ring stores raw Bayer; live decodes too) |
| — everything else (v3 evidence + DP + median) | 10.5 s | est. 3–6 s (untimed — see telemetry action) | 196 ridge frames + 94 scene-median decodes; Linux-Release parity benchmark ≈ 3 s |

**Live vs re-analyse (who owns the 20 s budget).** The reported
"1 min" decomposes as: dev-box *re-analysis* = 44.9 s analysis
+ ~20 s window rebuild + CIFS reads ≈ 65 s — that path is allowed to be
slow (Mark, 2026-07-09). The **live** post-shot pipeline on the studio
box is the budget owner. Forensics on today's session: every swing's
raw/mp4/thumb/swing.json share one mtime second and shots landed every
~52 s with SHOT re-arming in between, so the live tail (postroll 0.5 s
+ pose 5.5 s + shaft v3 + export) is very roughly 12–20 s today — near
budget but with the shaft-v3 stage **unmeasured on that box** (the v3
`[ShaftTracker]` log line dropped the old wall-time suffix).

**Telemetry action (do first, trivial):** re-add wall-clock ms to the
v3 ShaftTracker log line, and record per-stage times (pose / ball /
shaft / total) into `swing.json` analysis provenance + runmeta so every
live shot self-reports the budget. Without this the live number stays
anecdotal.

The pose pass is 76 % of dev-CPU wall time and is the only stage with no
span bound at all on the camera-only path. The shaft evidence span is
*accidentally* about right today (late bs0 + 400 ms collar), so fixing
correctness adds only ~10 % to the heavy-frame count.

## 3. Root cause, stated once

Takeaway detection uses one threshold that answers "when is the grip
moving *fast*?" when the question is "when did it *start* moving?".
Every existing mitigation (addressCollarUs, the spanLo ray-publication
exemption, tk0's adaptive departure threshold) compensates downstream
instead of fixing the boundary.

## 4. Stage A — true-onset segmentation (correctness)

All inside `segmentPhases()` / `decideTrack()`; no new passes, no new
compute (the signals already exist per coverage frame).

**A1. Dual-threshold hysteresis onset.** Keep the existing high-threshold
run detection to *find* the swing (robust). Then walk `bs0` back to the
first frame where `spdSmoothed < swLow` (default **1.5 px/f**, sweepable
`shaft.swLow`). The median5+gauss2 smoothing already removes single-frame
dips, and on the corpus the takeaway ramp never dips below 1.6 px/f.

**A2. φ-onset witness (cheap, optional but recommended).** The physical
statement "the club rotates about the wrists while the grip is still"
implies the lead-forearm direction φ (already computed per frame) moves
before the grip does. Compute the first sustained |dφ/f| > ~0.25°/f
before bs0 the same walk-back way; take `onset = min(onset_spd,
onset_phi)`. On the corpus this moves onset ≤ 150 ms earlier — a few
extra frames, never later.

**A3. Impact-anchored clamp (the safety rail).** `impactUs` is always
known at analysis time (shot trigger; ball `launchTUs` − ~40 ms where a
live ball lane exists, and the two agree to ±75 ms on the corpus). Clamp:

```
onset ∈ [impact − bsMaxUs, impact − bsMinUs]   default 1600 ms / 550 ms
```

If the walk-back exits the clamp (noisy hold, waggle running into
takeaway, pose dropout) pin to the violated edge and log. This bounds the
damage of any onset failure to a known-wide-enough span — the prior alone
already covers every corpus swing with margin. Sweepable
`shaft.bsMinBeforeImpactUs` / `shaft.bsMaxBeforeImpactUs`.

**A4. `bs0 := onset`.** Consequences, all simplifications:

- Phase labels: `Backswing` starts at the real takeaway → correct DP
  ceilings and sign restriction over the recovered zone; real evidence
  (the corpus shows ray/band measurables there) replaces `pred` rows.
- `addressCollarUs` 400 ms → **100 ms** (symmetric with the finish
  collar; it now covers measurement pad, not a known bias).
- The 2026-07-08 spanLo ray-publication exemption in `decideTrack()`
  becomes dead weight (those frames are now labelled Backswing) — retire
  it after the corpus gate confirms.
- `phasesToSegmentation()` Address event and `swingStartUs` become
  honest; the Review band starts where the swing starts.
- tk0's search domain `[0, bs0)` now ends at the true takeaway, so the
  v3.4 ball anchor refines *address discrimination* instead of doing the
  takeaway detector's job. The tk0-override-of-Address path stays as the
  post-hoc refinement it was designed to be (§9.6 additive-only safety
  intact — this is why I am NOT proposing to move the ball anchor ahead
  of the DP: with the onset fixed, nothing upstream needs θ_ball).

**Rejected alternative — ball-launch-derived span start.** The ball says
*impact* precisely but nothing about takeaway (it sits still through
address AND backswing); a launch-anchored start is just the §A3 prior
with extra steps. It stays as the clamp + impact cross-check.

## 5. Stage B — two-pass pose (compute, the < 20 s path)

Today the camera-only pose pass decodes+poses the whole 5 s window
because the span comes from grip speed, which comes from pose — the
second chicken-and-egg. Break it with a cheap coarse pass:

**Pass 1 — coarse, full window.** Stride ~12 (**63 frames** ≈ 80 ms
grid). Purpose: grip track for §4 segmentation, address-hold coverage
(this *subsumes* `addressScanPadUs`/`addressStride` — same cost, same
role), body scale. Verified offline: coarse-grid onset lands within
±30 ms of full-res on 5/6 swings, +133 ms worst → covered by a 150 ms
span pre-pad.

**Pass 2 — fill, span only.** `[onset − 150 ms, fin0 + 150 ms]`
(≈ 1.6 s ≈ 240 frames here):

- dense zone retuned to `[impact − 500 ms, impact + 250 ms]` stride 1
  (transition + downswing + impact blur zone; today's 800 ms pre reaches
  back into the mid-backswing where per-frame pose adds nothing —
  the shaft DP interpolates grip between pose samples anyway);
- stride 2 (75 Hz) over the rest of the span;
- pass-1 frames reused, not re-posed.

Posed frames: **310 → ~215**. Shaft evidence span: 196 → ~215 heavy
frames (correctness costs ~10 %; unchanged machinery, it is already
span-bounded). `PoseRunner` already has all the plumbing
(`scanStartUs/EndUs`, strides) — pass 2 is a second `run()` over
different bounds, or one call taking the pass-1 track to skip.

**Budget model.** Live (studio, CUDA pose measured 18 ms/f):

| configuration | pose | shaft+rest | live analysis total |
|---|---:|---:|---:|
| today (310 frames, unbounded) | 5.5 s | 3–6 s (unmeasured — telemetry first) | ~9–12 s |
| + Stage B (~215 frames) | ~3.9 s | ~3–6 s | **~7–10 s** |

Re-analyse (dev box CPU, Debug; explicitly allowed to be slower):
34.4 s pose → ~24 s with Stage B (~18 s with dense-zone stride 2 —
an accuracy question for the corpus gate), + 10.5 s shaft Debug
(~3 s Release). Smaller optional levers, in order: scene-median
stride 8 → 16 (−47 decodes), one-time ViTPose session reuse across
shots, decide-core windowing (keep emitting pred rows — porting
invariant).

Note the export stage (933 MB raw copy + x264 + thumb) sits *after*
analysis in the live pipeline and inside the SHOT-disarmed window; if
the perceived live latency target includes it, it needs its own
measurement — it is untouched by this plan.

## 6. New tuning keys

| key | default | meaning |
|---|---|---|
| `shaft.swLow` | 1.5 px/f | hysteresis low threshold (walk-back stop) |
| `shaft.phiOnsetDegPerFrame` | 0.25 | φ-witness sustained-motion threshold (0 = off) |
| `shaft.bsMinBeforeImpactUs` | 550 000 | onset clamp, near edge |
| `shaft.bsMaxBeforeImpactUs` | 1 600 000 | onset clamp, far edge |
| `shaft.addressCollarUs` | 400 000 → **100 000** | shrinks: bias fixed at source |
| `pose.coarseStride` | 12 | pass-1 stride |
| `pose.densePreMs` / `densePostMs` | 800/300 → **500/250** | retuned dense zone |
| `pose.denseStride` | 1 | dense-zone stride (2 = the ~21 s row) |

## 7. Verification & acceptance gates

1. **Unit** (`src/Analysis/tests`): synthetic grip tracks — clean ramp,
   noisy hold, waggle-with-gap, waggle-into-takeaway, degenerate
   (no run ⇒ bs0 = 0 path — note the pre-existing bs0=0 quirk found
   during v3.4), clamp violations both sides.
2. **Corpus gate** (the 6 swings + s01, via `swinglab_run`):
   - onset→impact within [850, 1150] ms on all 2026-07-09 swings;
   - phase-label sanity: Backswing span ≥ 2× today's, Addr count drops
     by the recovered zone;
   - coverage non-regression (≥ today's 0.81–0.96), FLIPS = 0;
   - θ parity outside the recovered zone (the zone itself is *expected*
     to change from pred → measured — that is the point);
   - wall-clock per stage recorded (runmeta) — Stage B target met.
3. **Live check**: one fresh studio capture, replay overlay start-of-band
   visually at the real takeaway.

SwingLab gap found while measuring: `swinglab_run` does not feed the
recorded ball lane (`streams[kind=ball]`) into `job.ballTrack`, so local
reruns lose the launch anchor the live app had (`measuredClubLenPx = −1`
vs 391.8 recorded live for the same swing). Worth fixing alongside —
the impact cross-check in §A3 wants it. (Also: session notes said 7
swings this morning; the directory holds 6 — swing_0007 never hit disk.)

## 8. Risks / edge cases

- **Waggle flowing into takeaway with no quiet gap** — walk-back treats
  it as swing; clamp caps the reach-back at 1.6 s; arguably the waggle
  IS part of the motion. Acceptable; corpus-verify.
- **Noisy address hold** (pose jitter to 9 px/f smoothed on swing_0001)
  — walk-back stops at first sub-threshold frame so it cannot run away;
  clamp bounds the worst case.
- **Practice swing inside the 5 s ring** — the two-biggest-runs picker
  can latch onto it; the impact clamp (trigger/ball launch) rejects an
  onset > 1.6 s before impact. Pre-existing exposure, now bounded.
- **Different tempos/clubs** — 940 ms is *this* golfer with a 7-iron;
  the clamp is deliberately wide [550, 1600] ms and sweepable; the
  primary detector is measurement, not prior.
- **No ball lane, no trigger timestamp** — manual SHOT always provides
  impactUs; the clamp degrades to trigger-only accuracy (±40 ms observed
  vs launch), which is ample for a span bound.

## 9. Out of scope (this iteration)

IMU-fused onset (better than vision when present — later, per "camera
accuracy first"); moving the ball anchor pre-DP (unneeded once onset is
fixed, and it would forfeit the §9.6 additive-only guarantee); untaped
passive-regime span questions (separate v3.3+ track).
