# Shaft Detection — Skeleton-Aware Enhancement (Implementation Plan)

> **Status:** **K0–K4b BUILT & PUSHED** (commits `69fbedf..4046832`, all dark behind `shaft.*`
> flags — shipped behaviour byte-identical) · **K5 corpus validation + flag-flips PENDING**
> (data-gated) · **R4 + all K4 deferrals deferred to post-K5** (see the *Deferred work register*
> at the bottom). · **Date:** 2026-06-19 · **Grounded against:** `main` @ `4046832` ·
> **Implements:**
> [`docs/design/shaft_detection_skeleton_design.md`](../design/shaft_detection_skeleton_design.md)
> (gaps G1–G6, revisions R1–R8).
>
> **As-built (K0–K4b):** analyzer-tests **28/28** green (incl. new R1/R2/R5/R6/R8 cases +
> `shaft_kinematics_test` + the `predicted` swing-doc round-trip); app + `swinglab_run` build
> clean; headless offscreen QML smoke clean. **Not yet validated:** anything needing a real
> captured swing — all `shaft.*` flags stay OFF until K5.
>
> **Extends, does not replace.** The shipped ShaftTracker (stages **S0–S4**, see
> [`shaft_tracker_impl.md`](shaft_tracker_impl.md) and `shot_analyzer_design.md` addendum
> B.1–B.10) stays the baseline. Everything here is a **delta** on the existing offline analyzer:
> `pose_runner` → `shaft_tracker_math::detectShaft` → `ShaftTracker::track` /
> `ShaftTrackAssembly` → `wrist_analyzer` persist → `PpCameraFrame` replay overlay. No live
> 60 Hz path, no EventBuffer producer, no capture code is touched.
>
> **Verification stance (same as S0–S5).** A phase is *landable* when its standalone synthetic
> tests are green, the app build is clean (zero new warnings), and headless offscreen startup is
> clean. It is *validated* (and its flag flipped to default-on) only after a **SwingLab corpus
> A/B** on real swings — that gate is **K5**, deferred behind a clean corpus, exactly as the
> original S5 deferred hardware verification.

---

## What ships

The detector learns to exploit the skeleton it already anchors on. Four behavioural changes,
each independently landable behind a `shaft.*` tuning override (default OFF → byte-identical to
today until flipped):

1. **Scale the search from the arm, not the body silhouette** (R1) — robust when golf framing
   crops the lower body.
2. **Predict the club angle from the lead-arm angle via the double-pendulum wrist-cock model**
   (R6), and use it three ways: sharpen the directional prior (R2/R6), gate kinematically
   impossible candidates (R6 envelope, G5), and bridge gaps (R6 fallback).
3. **Reject the "impossibly small shaft"** (R5) — a detection shorter than one arm scores zero.
4. **Switch to blur-first detection in the delivery→impact zone** (R8) — predict the motion-blur
   fan from `ω̂·t_exp` and integrate it, instead of hunting a straight ridge that isn't there.

Plus the instrumentation that makes the above measurable and self-checking:

5. **Dual output** (R7) — emit a `predicted` (pure model) series alongside the existing `actual`
   (detector-inferred) series, a `modelVisionResidualDeg` health metric, and an opt-in overlay.

```
SwingWindow (frozen, face-on)                        [ existing S0–S2, UNCHANGED ]
  └─ S0  pose_runner ─────────► PoseTrack2D (17 kp + hands)
  └─ S1  detectShaft ─────────► per-frame candidates
  └─ S2  ShaftTracker/Assembly ► ShaftTrack2D

                                  ▼  this plan inserts/adjusts:

  K1  arm geometry      armPx, armHangDir, φ_arm  ──► R1 R_max · R2 prior · R5 len-gate
  K2  shaft_kinematics  (s, branch) → φ_club_pred,σ_β ──► R6 prior mean · envelope · R4 seed
  K3  dual output       ShaftTrack2D.predicted + residual ──► R7 overlay + persistence
  K4  blur-first mode   ω̂·t_exp fan inside the R6 envelope ──► R8-T1
  K5  corpus A/B        SwingLab: validate, calibrate β̂, flip defaults
  K6  deferred          R8-T2 temporal-difference · DTL · 3-D β̂
```

---

## Contracts that bind every phase

- **Frozen-window const reader.** All work runs inside `analyze()` on the QtConcurrent worker;
  reads finish before return; `payloadOf().data` null-checked per frame (exporter discipline).
  No new producer, no buffer-state change.
- **Quaternions-only.** IMU orientation stays `QQuaternion` end-to-end. The new angles
  (`φ_arm`, `φ_club_pred`, `β`, `θ`) are genuine **2-D image-plane scalars** — explicitly
  permitted (the rule bans Euler *intermediates for 3-D rotation*, not image angles). Nothing
  here introduces a stored Euler 3-D representation.
- **Flat source dirs + `tests/`.** New code lands flat in `src/Analysis/*.{h,cpp}`; standalone
  tests in `src/Analysis/tests/`. The design-doc `src/Analysis/<layer>/` paths are logical.
- **No new dependencies.** Core OpenCV `imgproc` only (no `ximgproc`/contrib); `shaft_kinematics.h`
  is pure `<cmath>` (header-only, like `impact_detector.h`). No new third-party libs.
- **Default-OFF flag discipline + a defined flip.** Every behaviour is gated by a `shaft.*` /
  `assembly.*` tuning override defaulting to *current behaviour*. A phase lands dark; its flag
  flips to default-on **only** when K5's corpus A/B shows the documented metric win. The
  rollout table at the end lists every flag and its flip gate.
- **Additive schema only.** `swing.json` gains additive blocks/fields; existing fields and
  readers (SwingLab, PpDataViewer) are untouched and tolerate the additions. The
  `swing_doc` round-trip test must cover every new field.
- **Never fabricate (layer-4).** Plausibility gates (R5 length, R6 angle) and blur mode only
  ever *reject* or *flag-and-bridge*; when no real signal exists the detector still emits empty
  and the filter coasts on IMU/model with the honest flag. A predicted/bridged sample is always
  flagged distinctly from a measured one.

---

## Data-model & config deltas (defined once; phases reference this)

### `src/Analysis/swing_analysis.h`

```cpp
enum ShaftSampleFlags : uint8_t {
    ShaftMeasured          = 0x01,
    ShaftImuBridged        = 0x02,
    ShaftCoasted           = 0x04,
    ShaftWedge             = 0x08,
    ShaftHeadProjected     = 0x10,
    ShaftKinematicPredicted= 0x20,   // NEW (R6 fallback / R7 predicted series)
};

struct ShaftTrack2D {
    // ... existing fields unchanged ...
    std::vector<ShaftSample2D> samples;        // ACTUAL  — detector-inferred (unchanged)
    std::vector<ShaftSample2D> predicted;      // NEW (R7) — pure R6 model, every frame
    float modelVisionResidualDeg = -1.f;       // NEW (R7) — RMS|actual − predicted| over prior-free measured frames
};
```

### `src/Analysis/shaft_tracker_math.h`

```cpp
struct ShaftDetectConfig {            // additions only; existing fields keep their defaults
    // R1/R5
    float minShaftLenPx   = 30.f;     // = max(minVisibleLenPx, minLenFracOfArm·armPx), caller-computed
    // R6
    bool  hasEnvelope     = false;    // R6 angle guardrail active this frame
    float envelopeKSigma  = 3.0f;     // soft-penalty arc half-width = k·σ_β
    float envelopeHardK   = 4.0f;     // hard-reject only beyond this AND wrong side
    // R8
    bool  blurMode        = false;    // caller sets per frame from ω̂·t_exp / phase
    float predFanHalfRad  = 0.f;      // = 0.5·ω̂·t_exp (predicted wedge half-width)
    float blurThreshScale = 0.5f;     // threshold multiplier inside the envelope in blur mode
};

struct AnchorPrior {                  // additions only
    // ... existing gripPx, interHand, elbow mask ...
    bool  hasKinematicDir = false;    // R6: replaces the inter-hand soft bump when present
    float kinematicDirRad = 0.f, kinematicSigmaRad = 0.f;   // φ_club_pred ± σ_β
    int   armSide         = 0;        // +1 club expected trail-side of arm, −1 lead-side (envelope sidedness)
};
```

### `src/Analysis/shaft_kinematics.h` *(new, pure header-only — no Qt)*

```cpp
namespace pinpoint::analysis::kinematics {
enum class Branch { Backswing, Downthrough };          // resolves the double-valued β̂(s)
struct ClubAnglePrediction { float betaRad; float sigmaRad; int side; };  // |β̂|, σ_β, ±side
// Seed table (R6) keyed on swing-progress s∈[0,1]; linear-interp between knots.
ClubAnglePrediction predictWristCock(float s, Branch b);
// φ_club_pred = φ_arm + side·β̂  (caller composes with the measured arm angle).
float predictClubAngle(float phiArmRad, float s, Branch b, ClubAnglePrediction *out = nullptr);
}
```

> **Why pure/standalone:** `shaft_kinematics.h` takes `s` (a `float`) and a local `Branch`, **not**
> a `Phase` or `Segmentation` — so it pulls no Qt and is unit-tested with zero link deps (the
> `impact_detector.h` pattern). The `Segmentation`/`Phase` → `(s, Branch)` mapping is Qt-side, in
> `shaft_tracker.cpp` (`swingProgressAt`, K2).

### Tuning overrides (all default to current behaviour)

`shaft.armScaleRatio`, `shaft.minLenFracOfArm`, `shaft.armAxisSigmaDeg`, `shaft.sectorHalfWidthDeg`,
`shaft.useKinematicPrior`, `shaft.envelopeKSigma`, `shaft.envelopeHardK`, `shaft.kinematicFallback`,
`shaft.blurOmegaTrigger`, `shaft.expExposureUs`, `shaft.blurThreshScale`, `shaft.emitPredicted`.
Applied in `ShaftTracker::track` exactly like the existing `shaft.*` block (`shaft_tracker.cpp:155-180`).

---

## Dependency graph & ordering

```
K0 scaffolding ─► K1 arm geometry ─►  K2 kinematics ─►  K3 dual output
                    (R1,R2,R5-base)     (R6,R3,R4)         (R7)
                         └───────────►  K4 blur mode ◄────┘   (R8-T1 needs R6 angle+ω̂ and R7 residual feedback)
                                              │
K5 corpus A/B (validates K1–K4, calibrates β̂, flips defaults) ─► K6 deferred (R8-T2, DTL, 3-D)
```

K1 is the foundation (arm geometry feeds everything). K2 needs K1's `φ_arm`. K3 and K4 both need
K2; K4 additionally consumes K3's per-swing residual to bound its aggressiveness (below). K5 is
the cross-cutting validation/flip gate. Land K0→K1→K2→K3→K4; K5 runs continuously from K1.

---

## The per-frame loop after K1–K4 (the integration heart)

`ShaftTracker::track` (`shaft_tracker.cpp`) gains the ordered steps below inside its existing
`for (entry : entries)` loop. Steps marked *(have)* exist today.

```
1. anchorAt(pose, …)            grip g, elbow dirs, inter-hand     (have) + φ_arm, armPx, armHangDir   [K1]
2. swingProgressAt(seg, t_us)   → (s, Branch)                                                          [K2]
3. predictClubAngle(φ_arm,s,b)  → φ_club_pred, σ_β, side                                               [K2]
4. ω̂ = clubRate(...)            IMU θ̇ ▸ Δφ_club_pred/Δt ▸ Δθ_meas    ; blur = ω̂·t_exp > trigger        [K4]
5. build AnchorPrior            grip, elbow mask (have), kinematicDir=φ_club_pred±σ_β, armSide         [K2]
                                cfg.blurMode/predFanHalfRad/minShaftLenPx set                          [K1,K4]
6. detectShaft(luma,cfg,prior)  buildThetaWeights → scanAllAnchors → pickPeaks → estimateTheta        (have, branched)
      • buildThetaWeights: envelope-centred bump replaces inter-hand bump when hasKinematicDir  [K2/R6]
      • scanAllAnchors:    blurMode ⇒ integrate within envelope, lowered threshold, proximal ρ  [K4/R8]
      • pickPeaks:         gate visLen ≥ minShaftLenPx (relaxed in blurMode); envelope hard-reject [K1/R5,K2/R6]
7. record predicted[i]          φ_club_pred sample, flag ShaftKinematicPredicted                       [K3]
8. push ShaftFrameObs           candidates + qHand (have)
   … after loop: ShaftTrackAssembly::assemble (have) + residual + predicted passthrough               [K3]
```

---

## K0 — Scaffolding & gating

**Deliverable:** the config/flag plumbing and test scaffolding so every later phase lands dark
and isolated. No behavioural change.

**Files**
- `shaft_tracker_math.h` — add the `ShaftDetectConfig`/`AnchorPrior` fields above (defaults =
  current behaviour, so `detectShaft` is unchanged).
- `swing_analysis.h` — add `ShaftKinematicPredicted`, `ShaftTrack2D::predicted`,
  `modelVisionResidualDeg`.
- `shaft_tracker.cpp` — read the new `shaft.*` overrides next to the existing block; thread the
  (still-defaulted) values through.
- `src/Analysis/tests/` — add empty `shaft_kinematics_test.cpp` target to the standalone CMake
  list (system OpenCV not needed — pure header).

**Acceptance:** app + all standalone tests build clean; `swing_doc` round-trip still green (new
fields default-empty, serialise to nothing); zero behavioural diff (a captured-swing analysis
logs byte-identical `ShaftTrack2D` to pre-K0).

---

## K1 — Arm geometry foundation (R1 scale · R2 prior · R5 length gate)

**Deliverable:** the lead-arm geometry computed once per frame and fed into scale, prior, and the
length gate. Independently useful; the base every later phase builds on.

**Files**
- `shaft_tracker.cpp`
  - `AnchorState` gains `float phiArmRad; float armPx; bool hasArm; cv::Point2f shoulderRef;`.
  - `anchorAt()` computes them from COCO shoulders (5/6) + the grip `g`: `armHangDir =
    atan2(g.y−sh.y, g.x−sh.x)`, `armPx = ‖sh−wrist‖` (lead arm preferred; else forearm×1.9; else
    none). Lead-arm selection: handedness when known, else the *straighter* arm.
  - `track()`: replace the silhouette `pxPerM` (`:175-180`) with the **R1 scale ladder** —
    arm `pxPerM = armPx/L_arm_nominal` (0.52 m), → `R_max = clamp(1.25·clubLengthM·pxPerM, …)`;
    fall back to the existing silhouette path, then half-frame. Compute
    `minShaftLenPx = max(minVisibleLenPx, minLenFracOfArm·armPx)` (R5).
  - Feed `AnchorPrior.kinematicDir` (interim: `armHangDir`, the R2 "naive" mean — superseded by
    K2) and `cfg.minShaftLenPx`.
- `shaft_tracker_math.cpp`
  - `buildThetaWeights`: when `prior.hasKinematicDir`, apply the soft bump toward
    `kinematicDirRad` (reusing the existing `priorBump`/floor machinery — the unused
    `predictedTheta` slot, `:180-181`).
  - `pickPeaks`: gate on `cfg.minShaftLenPx` instead of the bare `minVisibleLenPx` (`:307-309`).

**Approach notes**
- R1 keeps `clubLengthM` (so clubs scale) and only changes the *source* of `pxPerM`. The ladder
  is logged (`ppInfo`) so the corpus run can see which rung each swing used.
- R5 runs at candidate selection (S1) so a sub-arm ridge never reaches Viterbi — this is what
  kills the "obsession". A frame whose only ridge is sub-arm yields **no candidate** → bridged.
- **R5 must not fire in blur mode** — left as `blurMode==false` until K4 wires the relaxation;
  K1 ships with R5 gated to sharp frames only (`!cfg.blurMode`, which is always false pre-K4).

**Tests** (`shaft_tracker_test.cpp`, extend the existing synthetic renderer)
- R1: render the same shaft+pose at three framings that progressively crop legs; assert `R_max`
  stays within tolerance of `1.25·clubLen·px` (silhouette path would collapse to half-frame).
- R5: plant a short bright dense ridge (≈0.5·arm) beside a longer low-contrast true shaft →
  assert the short one produces **no candidate**, the true shaft still detected; multi-frame
  variant → assert no S2 lock-on.
- R2: assert the arm bump raises the true-θ column and lowers a planted off-axis line, never
  zeroing the true bin (floor-0.3 invariant).

**Acceptance:** tests green; detect time unchanged (±5%); captured-swing smoke shows R1 ladder
logged and no new warnings.

**Flip gate (K5):** R1 + R5 default-on once the corpus shows *no coverage regression* on
fully-framed clips and *coverage gain + zero sub-arm samples* on cropped clips.

---

## K2 — Kinematic model (R6 · R3 · R4)

**Deliverable:** `shaft_kinematics.h` + the `Segmentation→(s,Branch)` mapping, turning the K1
prior from "point at the arm" into "point at the *predicted club*", plus the angle guardrail.

**Files**
- `src/Analysis/shaft_kinematics.h` *(new, pure)* — the seed β̂/σ_β table (R6, keyed on the
  canonical phases; see table below), `predictWristCock(s,branch)`, `predictClubAngle(...)`,
  linear interpolation between knots, branch-aware sidedness.
- `src/Analysis/tests/shaft_kinematics_test.cpp` *(new, standalone, no OpenCV)*.
- `shaft_tracker.cpp`
  - `swingProgressAt(const Segmentation&, int64_t t_us) → {float s, Branch}`: builds the knot
    timeline from `Segmentation::events` (the enum already provides every knot —
    `Address, Takeaway, MidBackswing, Top, Transition, Delivery, MaxSpeed, Impact, Release,
    FollowThrough, Finish`), interpolates `s` by time between knots, and sets `Branch` from
    pre-/post-`Top` and `Impact` (or `sign(φ̇_arm)` when an event is missing).
  - In the loop: replace the K1 interim mean with `predictClubAngle(φ_arm, s, branch)` →
    `kinematicDir/Sigma/side`; set `cfg.hasEnvelope`, `envelopeKSigma`, `armSide`.
  - **R4:** seed the S2 first-Viterbi node / `ŝ_hand` initial guess from `φ_club_pred` at Address
    (pass into `AssemblyConfig` or `ShaftTrackAssembly::assemble`).
  - **R3 (optional):** `sectorHalfWidthDeg` — a *soft* floor outside the envelope, default off.
- `shaft_tracker_math.cpp`
  - `buildThetaWeights`: centre the soft bump on `kinematicDirRad` with width `kinematicSigmaRad`
    (the K1 arm bump becomes the `β≡0` special case).
  - `pickPeaks`: **R6 envelope guardrail** — soft-penalise candidates beyond `k·σ_β`;
    **hard-reject** only beyond `envelopeHardK·σ_β` *and* on the wrong `armSide`. Never reject
    near a turn point (caller widens `σ_β` there).

**Seed correlation table** (R6 — `predictWristCock`):

| Phase knot (enum)        |  s   | \|β̂\|  | side  | σ_β |
|--------------------------|------|--------|-------|-----|
| `Address`                | 0.00 | 5°     | lead  | 8°  |
| `Takeaway`               | 0.15 | 28°    | trail | 15° |
| `MidBackswing`           | 0.35 | 70°    | trail | 20° |
| `Top`                    | 0.50 | 92°    | trail | 22° |
| `Transition`             | 0.60 | 100°   | trail | 30° |
| `Delivery`               | 0.80 | 48°    | trail | 25° |
| `Impact`                 | 0.90 | 8°     | ≈line | 10° |
| `Release`                | 0.95 | 28°    | lead  | 20° |
| `FollowThrough`/`Finish` | 1.00 | 95°    | lead  | 30° |

> The enum knots and the table knots are 1:1 — no new phase detection needed; K2 only *consumes*
> the existing ladder. `MaxSpeed` (s≈0.85) is not a table knot but is K4's blur-window centre.

**Tests** (`shaft_kinematics_test.cpp`)
- `predictWristCock` reproduces the table at knots and interpolates monotonically between;
  the **side flips** at impact; `σ_β` widens at `Transition`/`Finish`.
- Envelope: brackets an on-model synthetic shaft at every knot; rejects a planted off-axis line
  except at the velocity-zero turn points (`Top`, `Transition`), where it widens not rejects.
- `swingProgressAt` (Qt-side, folded into `shaft_tracker_test.cpp`): monotone `s`, correct branch
  across `Top`/`Impact`, graceful degrade to `sign(φ̇_arm)` when a knot is absent.

**Acceptance:** standalone kinematics test green; with `useKinematicPrior=true` a captured-swing
analysis shows the prior centred ~90° off the arm at `Top` (the case K1 alone gets wrong) and the
envelope rejecting a planted alignment-stick line.

**Flip gate (K5):** default-on once corpus directional accuracy improves (esp. wrist-fallback
clips) **and** the envelope produces no measured-frame false rejections (track them).

---

## K3 — Dual output: predicted vs. actual (R7)

**Deliverable:** the two parallel series, the residual health metric, additive persistence, and
the opt-in developer overlay.

**Files**
- `shaft_tracker.cpp` `track()`: fill `ShaftTrack2D::predicted` for **every** frame
  (`thetaRad=φ_club_pred`, `headPx=grip+clubLenPx·dir`, `visibleLenPx=clubLenPx`, `conf` from
  `σ_β`, `flags=ShaftKinematicPredicted`) — independent of `useKinematicPrior` (the series is
  always produced; the flag only controls whether the prior *biases* `samples`). Compute
  `modelVisionResidualDeg` over prior-free `ShaftMeasured` frames only.
- `src/Export/swing_doc.cpp` + `swing_exporter.cpp`: additive `predicted` array in the
  `analysis.club` block, behind `shaft.emitPredicted` (default off so normal exports don't grow).
- `src/Gui/shot/shot_processor.cpp` (`:148-171`): emit the normalized `predicted` series into the
  replay detail alongside the existing normalized `samples`.
- `src/Gui/cameras/PpCameraFrame.qml`: a `bool showPredictedShaft: false` property; when on,
  draw the `predicted` polyline as a **ghost** (dashed, ~40% alpha, distinct hue) *behind* the
  solid actual line, and optionally the `±k·σ_β` **envelope cone** from the grip. Wired from a
  diagnostic/developer toggle only (not end-user replay chrome — honour the subtle-chrome rule).
- `src/Export/tests/swing_doc_test.cpp`: round-trip the `predicted` block + residual.

**Approach notes**
- The residual must be computed **prior-free** to avoid circularity (the prior grading its own
  homework). K5's model-validation runs set `useKinematicPrior=false`; production leaves it on.
- The overlay is the cheapest, highest-value debugging artefact in this whole plan: where
  `predicted` and `actual` diverge with high vision conf you are *looking at* a detector error;
  where they coincide with no vision the `actual` is model-filled.

**Tests**
- Unit: `predicted` present on every frame incl. vision-empty ones; toggling `useKinematicPrior`
  changes `samples` but **not** `predicted`; residual accumulates only over prior-free measured
  frames.
- Overlay smoke (headless offscreen QML harness, per the visual-verification memory): enable the
  toggle, confirm the ghost line + cone render and the existing actual overlay/scrub are
  undisturbed; remove the temp harness before commit.

**Acceptance:** round-trip green; overlay smoke clean; `modelVisionResidualDeg` logged per shot.

**Flip gate:** `predicted` series + residual are **always on** (cheap, additive, no detection
effect); persistence + overlay stay **opt-in** indefinitely (dev/QA artefacts).

---

## K4 — Blur-first detection in the high-ω zone (R8-T1)

**Deliverable:** the mode switch that lets vision keep contributing through the delivery→impact
blur instead of coasting — predict the fan, integrate it, relax the line assumptions.

**Files**
- `shaft_tracker.cpp` `track()`:
  - `clubRate(...)` → `ω̂`: prefer IMU `θ̇` (from `qHand`/the fused stream); else Δ`φ_club_pred`/Δt
    across neighbour frames; else Δ`θ_meas`. Set `cfg.blurMode = ω̂·t_exp > kernelEquivRad`
    **or** phase ∈ `[Delivery … Release]` (around `MaxSpeed`); `cfg.predFanHalfRad = 0.5·ω̂·t_exp`.
  - `t_exp`: `shaft.expExposureUs` default (~3 ms); **two-pass refine** — pass 1 fits
    `t_exp = median(observedWedgeWidth / ω̂)` over moderate-ω frames, pass 2 uses it.
  - **R5 relaxation:** in `blurMode`, set `minShaftLenPx` to the proximal-crisp extent
    (`≈ shaftWidthPx / (ω̂·t_exp)`) rather than one arm.
  - **Bound aggressiveness by this swing's trust (R7 feedback):** if `modelVisionResidualDeg`
    from the sharp frames is large (model/IMU disagree on *this* swing), widen the envelope and
    raise `blurThreshScale` toward 1.0 (less aggressive) — so blur mode only leans on the model
    as far as the model has earned on the same swing.
- `shaft_tracker_math.cpp`:
  - `scanAllAnchors`/scoring: a **blur-mode branch** — within the predicted θ-envelope, replace
    the longest-supra-threshold-run score with an **integrated (mean-subtracted) response over
    the proximal ρ band**, at a lowered threshold (`blurThreshScale`). Outside the envelope,
    unchanged. Proximal-ρ weighting (B.4's "the proximal shaft barely blurs", now phase-gated).
  - Output a **wedge candidate**: `θ` = envelope-refined fan centroid, `σ = predFanHalfRad`,
    `wedge=true`. Skip the TLS line refit in blur mode (we are not fitting a line). Feeds B.5's
    KF unchanged (it already takes wedge width into `R`).

**Approach notes**
- The straight-line success criterion is *dropped* in blur mode (the user's "stop looking for a
  straight object"): success = "swept energy consistent with the predicted fan", not "a crisp
  ridge of length ≥ X".
- This is the only phase that meaningfully touches the detection core; keep it strictly
  envelope-bounded so the lowered threshold can never raise far-field false positives.
- **When even this drowns** (very fast + low light): emit nothing → R6/IMU bridge. R8-T1 widens
  the band where vision contributes; it never invents a shaft.

**Tests** (`shaft_tracker_test.cpp`)
- Render a faint, semi-transparent **wedge** (uniform sweep over `ω·t_exp`) with *no crisp ridge*:
  assert (a) the thin-ridge path returns nothing, (b) blur mode with the predicted angle+ω
  returns a `ShaftWedge` candidate, `θ` ≈ mid-exposure (tol ~2°), `σ ≈ ω·t_exp/2`, (c) **R5 does
  not reject it**. Below-noise wedge → asserts empty (never fabricate).
- Delivery→impact synthetic sequence: assert measured-frame coverage through the high-ω span
  improves vs. the reactive baseline.

**Acceptance:** tests green; detect time in blur mode within budget (envelope-bounded integration
is cheaper than the full run scan); no regression on sharp-frame tests.

**Flip gate (K5):** default-on once corpus *coverage through the delivery→impact span* rises with
**no rise** in off-axis false positives (R7 residual + G5 counter as guards).

---

## K5 — Corpus validation & table calibration (SwingLab)

**Deliverable:** the real-world gate. Turns the seed β̂ table into a measured one, validates
K1–K4 on recorded swings, and decides every default flip.

**How** (the `swinglab` skill: `lab.py` + `swinglab_run`, per the SwingLab status memory)
- **A/B sweeps** with the `shaft.*` overrides — each phase ON vs. OFF over the corpus. Headline
  metrics: coverage on lower-body-cropped clips (G1/R1), directional accuracy on wrist-fallback
  clips (G2/R2/R6), tiny-detection rate → target 0 (G4/R5), off-axis FP rate (G5/R6), coverage
  through the blur span (G6/R8).
- **β̂ table calibration (the real win).** On **IMU-equipped** clips, extract the measured wrist
  cock per phase from the lead-hand `qHand`, fit `β̂(s)`/`σ_β(s)` in image space, and replace the
  seed knots — reporting the residual spread per phase as the honest `σ_β`. Three signals align
  on one timeline: IMU wrist cock, vision shaft (R7 actual), model (R7 predicted).
- **Per-club / per-skill question:** start one global table; split only if `σ_β` stays high in
  transition (deferred, K6).

**Acceptance / flip:** each phase's documented metric win with no regression on currently-passing
clips → flip its default-on flag; otherwise keep dark and iterate. Record the corpus + commit in
the SwingLab status memory.

> **Dependency:** needs a trustworthy corpus (`/mnt/swingdata`, calibrated/perspective-tagged per
> the swing-capture-provenance memory). Until corpus v1 is clean this phase runs on synthetic +
> the few good real swings; defaults stay OFF.

---

## K6 — Deferred follow-ons

- **R8-T2 — temporal-difference / median-background blur detector.** The most sensitive
  faint-smear path; needs all decoded frames (S0 holds them) and care where the *body* also moves
  fast — **mask by the R6 envelope before differencing** so the diff image is club, not arm.
  Memory/perf: a streaming/approx median (not all-frames-in-RAM) per the large-export memory
  caution. Land only after K4-T1 is measured.
- **Per-subject `L_arm_nominal`** from the athlete profile (height→arm regression).
- **DTL extension** — the arm prior and β̂ generalise to down-the-line with a view-specific table
  and σ; tracked under `shot_analyzer_design.md` addendum B.10.
- **3-D β̂** — a genuine swing-plane angle (needs C1 intrinsics + plane estimate) makes the table
  view-independent; defer with B.10.
- **Exposure feedback** — write the R8-estimated `t_exp` back into the camera-capabilities block
  for next-shot priors.

---

## Risks & mitigations (consolidated)

| Risk | Mitigation |
|------|------------|
| R1 arm scale worse than silhouette in some framings | scale ladder + silhouette/half-frame fallbacks; A/B before flip; log the rung used |
| R5 rejects a real but heavily-occluded/blurred shaft | floor is the *arm* (~50% occlusion headroom); **suspended in blur mode** (K4); `minLenFracOfArm` knob |
| R6 seed table is a guess | seed→corpus/IMU calibration (K5); generous σ_β; gate to Swing session type; never hard-gate except gross-violation |
| R6 branch wrong near turn points (φ̇_arm≈0) | widen σ_β + fall to soft prior; never hard-reject there |
| R8 lowered threshold raises false positives | strictly envelope-bounded; aggressiveness scaled by *this swing's* R7 residual; G5 counter watched in K5 |
| R8-T2 memory/CPU | deferred; envelope-masked; streaming median |
| Schema drift breaks SwingLab/PpDataViewer | additive-only; `swing_doc` round-trip test; old readers ignore new fields |
| Overlay clutters end-user replay | default-off diagnostic toggle; ghost/subtle styling |
| CPU budget (offline 2–4 s) | K4 reuses existing response maps (no new heavy pass in T1); decode/pose already dominate; measure per phase |

---

## Rollout / default-flip summary

| Flag | Phase | Lands | Default-on when (K5) |
|------|-------|-------|----------------------|
| `shaft.armScaleRatio` (R1) | K1 | OFF | no regression framed / +coverage cropped |
| `shaft.minLenFracOfArm` (R5) | K1 | OFF | zero sub-arm samples, no coverage loss |
| `shaft.useKinematicPrior` (R2/R6) | K1→K2 | OFF | directional accuracy ↑, no envelope mis-rejects |
| `shaft.envelopeHardK` (R6 guardrail) | K2 | OFF | off-axis FP ↓, measured-frame rejects = 0 |
| `shaft.emitPredicted` + residual (R7) | K3 | **ON** | always (additive, no detection effect) |
| `predicted` persistence + overlay (R7) | K3 | OFF (opt-in) | stays opt-in (dev/QA) |
| `shaft.blurMode` family (R8-T1) | K4 | OFF | blur-span coverage ↑, off-axis FP flat |
| R8-T2, DTL, 3-D | K6 | — | deferred |

**Bottom line:** land K0→K4 dark behind flags, prove each on the SwingLab corpus in K5, then flip.
The detection core change is confined to K4 and is envelope-bounded; everything else is anchor
inputs, a pure header, additive output, and instrumentation.

---

## Implemented Refinements (2026-06-28)

The following refinements (originally deferred until post-K5) have been fully implemented and verified:

### D1 · Residual-bounded blur aggressiveness
- **Implementation:** Added a two-pass tracker architecture inside `ShaftTracker::track()`. Pass 1 performs prior-free detection and calculates the root-mean-square tracking residual `modelVisionResidualDeg`. In Pass 2, if the residual exceeds a threshold ($5^\circ$), we dynamically expand the kinematic envelope width (`envelopeKSigma` and `envelopeHardK`) and scale `blurThreshScale` towards 1.0 to limit matching bias.
- **Files/Surface:** `src/Analysis/shaft_tracker.cpp::track()`.

### D2 · Two-pass exposure estimation
- **Implementation:** Implemented dynamic shutter exposure calibration. In the Pass 1 pre-pass, observed motion-blur wedge widths (`sigmaThetaRad`) are divided by angular velocity (`omega`) on frames where $\omega \ge 5.0\text{ rad/s}$. The median of these values is fitted as `expExposureUs` (fallback default: 3 ms) and used to drive the Pass 2 matched-filter fan width `predFanHalfRad = 0.5 * omega * t_exp`.
- **Files/Surface:** `src/Analysis/shaft_tracker.cpp::track()`.

### D3 · R8-T2 — temporal-difference / median-background subtraction pass
- **Implementation:** Integrated a median background luma estimator (`computeMedianLuma`) that processes 15 evenly-spaced frames from the swing window. On each frame, the absolute difference map is generated. An image-space body mask (`drawBodyMask`) is drawn dynamically using skeletal keypoint coordinates interpolated to the frame timestamp (`lerpPoseFrame`), masking out the golfer's moving silhouette. The resulting motion-only difference matrix is cropped to the ROI and passed to `detectShaft` to accumulate swept energy inside the kinematic envelope.
- **Files/Surface:** `src/Analysis/shaft_tracker_math.h`, `src/Analysis/shaft_tracker_math.cpp`, `src/Analysis/shaft_tracker.cpp`.

### Chirality Lock (Automated Handedness & Mirror Detection)
- **Implementation:** Resolved chirality automatically at the start of `track()` by measuring horizontal hand displacement in the first half of the swing (specifically Address to Top events). Hand sequence coordinate consistency is validated, falling back to job configuration handedness if coordinate movement is too small.
- **Files/Surface:** `src/Analysis/shaft_tracker.cpp::track()`.

---

## Deferred work register — DO AFTER THE K5 CORPUS

### D0 · R4 — seed the S2 assembly from φ_club_pred at Address  *(task #10; lowest risk)*

- **What:** prime the track-assembly bootstrap with the kinematic prediction at Address instead of
  letting it cold-start from vision alone.
- **Why deferred (not why-blocked):** *codeable + synthetically testable now*, but its only real
  benefit is faster / more robust convergence on real swings — measure it in K5, don't guess.
- **Files / surface:** `src/Analysis/shaft_track_assembly.{h,cpp}` — `calibrateShaftInHand()` (seed
  the ŝ_hand initial guess to narrow the blind 800-point Fibonacci lattice, `shaft_track_assembly.cpp:~210`)
  and the first-Viterbi/KF node init (`~L433`, currently first vision measurement or IMU). Add an
  optional `seedThetaRad` (+ valid flag) to `AssemblyConfig`; thread `φ_club_pred@Address` from
  `ShaftTracker::track` into `assemble()`. `src/Analysis/tests/shaft_track_test.cpp` for the test.
- **Validate (synthetic, legitimate):** on the existing synthetic candidate sequences assert the
  seeded run **converges to the same θ track** (invariant — must not change the answer) in **fewer
  iterations / with a worse initial guess** than cold-start. Acceptance: identical converged track,
  measurable convergence speed-up; gated behind a flag (`assembly.seedFromAddress`, default off).

### Longer-horizon (already in K6 above, not blocked on K5 specifically)

Per-subject `L_arm_nominal` (athlete-profile height→arm), DTL extension (view-specific β̂ table +
σ), 3-D swing-plane β̂ (needs C1 intrinsics), and 3-D projection of β̂. These ride later subsystem work
(athlete profiles, C1 calibration, DTL) rather than the K5 corpus.
