# Calibrated Ball Detection — Design & Implementation Plan

**Environment-calibrated stationary-ball detection with a user-in-the-loop calibration protocol.**

Replaces the fixed-threshold Hough/HSV detector (`src/Pose/ball_detector.cpp`) with a detector whose
parameters are *learned from the actual studio* at session setup: the user defines the hitting area,
places a ball so the system can learn what *this* ball looks like under *this* lighting, removes it so
the system can learn the empty background, and repeats until the measured separation between
"ball present" and "ball absent" is provably robust. No manual parameter tuning.

Relationship to [`ball_detector_design.md`](ball_detector_design.md) (CNN + Kalman, not started):
that plan targets the **moving/blurred ball in flight** and deliberately keeps a "cheap classical
detector for stationary acquisition" (§3.4 `ClassicalPatchDetector`). **This design replaces that
acquisition layer.** The CNN/Kalman plan remains valid for flight tracking and slots on top later;
nothing here blocks it. Part of the vision modality of [`shotdetection.md`](shotdetection.md).

Status: **design / not started.**

---

## 1. Problem

The current detector hard-codes the decision *"a ball is white"*:

| Stage | Today | Failure mode |
|---|---|---|
| Segmentation | `cv::inRange(hsv, (0,0,170), (180,satCeil,255))` — `ball_detector.cpp:74` | In a dim studio the ball's V is often 100–160 → **empty mask, nothing downstream can recover** |
| Geometry | `HOUGH_GRADIENT_ALT`, conf 0.7 on the 2× upsampled mask — `ball_detector.cpp:96-102` | Runs on a degraded mask; confidence threshold is a guess |
| Fallback | `SimpleBlobDetector` with fixed circularity/convexity/inertia — `ball_detector.cpp:116-127` | Same mask, same fate |
| Tuning | Two user sliders: `houghConf` [0.3–1.0], `whiteSatCeil` [20–120] | Manual fiddling per environment; users can't reason about either number |

The structural flaw: **absolute thresholds in a world of unknown illumination.** Every magic number
(V≥170, conf 0.7, circularity 0.55, …) encodes an assumption about an environment we haven't seen.

What we actually have in a golf studio is the *best possible* setting for learned detection:
- **Fixed camera** (`cameraFixedInPlace` already exists in AppSettings).
- **Fixed, user-defined ROI** (hitting area — drag-select already in `PpCameraFrame.qml`).
- **Constant artificial lighting** (no sun/clouds).
- **One known object** that appears/disappears at roughly one spot and one scale.
- **A cooperative user** at session setup who can place/remove a ball on request.

## 2. Core idea

Learn two models in-situ and derive the decision boundary from their measured separation:

```
CALIBRATION                                    RUNTIME
┌─────────────────────────────┐
│ empty-ROI frames (N≈30)     │──► BackgroundModel: per-pixel μ,σ (gain-normalised)
│                             │      + illumination envelope (median luma, range)
├─────────────────────────────┤
│ ball-present frames (N≈30,  │──► BallModel: radius r̂±σr (px), colour distribution
│ ≥2 ball positions)          │      (mean+cov in gain-normalised space),
│                             │      grayscale template patch, contrast stats
├─────────────────────────────┤
│ score all calib frames      │──► decision threshold θ = derived from the gap:
│ through the runtime scorer  │      require margin = min(ballScores) − max(emptyScores)
└─────────────────────────────┘      to exceed a robustness floor before PASS

RUNTIME (per frame, detector thread, unchanged throttle contract)
  gain-normalise ROI (median-luma ratio vs calibration)
  → background difference ≥ kσ per pixel  →  candidate blobs (contours)
  → per-candidate multi-cue score:
        size prior   N(r; r̂, σr)
        shape        circularity · convexity
        appearance   NCC vs template  (or colour Mahalanobis)
        diff support fraction of blob pixels ≥ kσ
  → best score ≥ θ  →  BallDetection{found=true, x, y, r}   (existing struct)
```

Key properties:

- **No absolute brightness anywhere.** Background difference is relative to learned per-pixel noise
  (σ-multiples); colour matching is relative to the learned ball distribution. A grey ball in a dark
  studio calibrates as readily as a white ball in daylight. Coloured balls (yellow/orange range
  balls) work for free — we learn whatever the user placed.
- **θ is computed, not guessed.** Calibration runs the *exact runtime scorer* over its captures and
  places θ in the measured gap. The "repeat with the user until we're happy" loop is literally
  "collect samples until the gap is wide enough".
- **Auto-exposure drift handled.** Per-frame global gain `g = calibMedianLuma / currentMedianLuma`
  (median over background-classified pixels, robust to the player's shadow) compensates AE changes;
  drift beyond the learned envelope flags the profile as degraded (§6).
- **Hough is demoted, not deleted.** `HOUGH_GRADIENT_ALT` on the diff mask becomes one *candidate
  generator* feeding the scorer (cheap, occasionally finds what contours miss). It no longer gates.

### Why not jump straight to a CNN?

A nano-YOLO would also solve dim studios, but: it needs a trained model that doesn't exist yet (the
gating dependency called out in `ball_detector_design.md` §11), costs GPU per frame forever, and
generalises to environments we *don't* operate in at the price of being un-tunable in the one we
*do*. The calibrated classical detector is dependency-free, sub-millisecond on CPU, explainable
(every cue inspectable), and — because it learns the actual pixels it will judge — is, in a fixed
studio, more specific than any general model. The CNN remains the right tool for the *moving* ball.

---

## 3. Detection core (`src/Pose/ball_model.h`)

Header-only math, standalone-testable (project convention — flat dir, tests in `src/Pose/tests/`).
Pure functions over `cv::Mat`, no Qt signal dependencies, no globals.

```cpp
namespace pinpoint::ballcal {

struct BackgroundModel {
    cv::Mat meanBgr;        // CV_32FC3, ROI resolution, gain-normalised
    cv::Mat sigma;          // CV_32FC1, per-pixel luma noise (≥ floor, e.g. 2.0)
    double  calibMedianLuma = 0.0;
    // accumulate(frame) during capture; finalize() computes μ,σ
};

struct BallModel {
    double  radiusPx = 0, radiusSigma = 0;     // learned scale (and tolerance)
    cv::Vec3f colourMean;  cv::Matx33f colourCovInv;   // gain-normalised BGR
    cv::Mat template8u;     // grayscale patch (2r̂+margin)², gain-normalised, for NCC
    double  minContrast = 0;                   // ball-vs-surround luma delta seen at calib
};

struct CandidateScore {                        // every cue preserved for diagnostics/UI
    float size, shape, appearance, diffSupport;
    float total() const;                       // fixed weights — see below
};

struct Detection { bool found; cv::Point2f centerPx; float radiusPx; float score; };

// The runtime entry point — also used verbatim by calibration to score captures.
Detection detect(const cv::Mat &roiBgr, const BackgroundModel &bg, const BallModel &ball,
                 double theta);

// Calibration-side fitting + threshold derivation.
BallModel  fitBallModel(const std::vector<cv::Mat> &ballFrames, const BackgroundModel &bg);
struct ThresholdResult { double theta, margin; bool pass; };
ThresholdResult deriveThreshold(const std::vector<double> &ballScores,
                                const std::vector<double> &emptyScores);

} // namespace
```

Design decisions:

- **Score weights are fixed** (e.g. size .30, shape .25, appearance .30, diffSupport .15), **only θ
  is derived.** A 1-D learned threshold over a fixed scorer needs ~tens of samples to be reliable;
  jointly optimising weights from one session's captures would overfit and be untestable. Weights
  live in one constexpr table; SwingLab can sweep them offline later if evidence demands.
- **Threshold rule:** `θ = maxEmpty + 0.4·(minBall − maxEmpty)` — biased toward the empty side
  because a false "ball present" is worse than a slow acquisition (presence hysteresis already
  debounces brief misses). `pass = (minBall − maxEmpty) ≥ kMinMargin (0.15) && minBall/maxEmptyish
  sanity checks`. Empty frames with zero candidates contribute score 0.
- **Background σ floor** (≈2 luma levels) prevents a too-quiet calibration from making the diff
  threshold hair-trigger; **k=4σ** for the diff mask.
- **ROI resolution native** — the ROI is a small fraction of the frame; no downsampling, radius
  fidelity preserved. Memory: a 640×360 ROI background model is ~3.5 MB — fine.
- The existing **2× upsample trick is dropped**: it existed to help Hough vote on small masks; the
  contour+score path measures radius sub-pixel via `minEnclosingCircle` on the native diff mask.

## 4. Runtime detector (`BallDetector` rework)

`BallDetector` keeps its name, thread, and **exact external contract** — `detect(cv::Mat)` slot,
`ballDetected`/`detectionSkipped` signals, `BallDetection{found,x,y,radius,detectMs}` normalised
exactly as today (`x,y` ∈ [0,1] full-frame, `radius`/frame-width), the one-signal-per-frame
throttle rule (`consumerCount=2` — CLAUDE.md), and the enabled/ROI early-outs. Internally:

```
detect(frame):
  guards (enabled / roi / empty)            → detectionSkipped     (unchanged)
  if (m_profile.valid)                      → ballcal::detect(...) → emit
  else                                      → legacy Hough path    → emit   (today's behaviour)
```

- New slots (queued, like `setRoi`): `setProfile(BallCalProfile)` — swaps the learned models;
  `clearProfile()`.
- **Uncalibrated behaviour is bit-for-bit today's detector** — nothing regresses for users who skip
  calibration. The `houghConf`/`whiteSatCeil` sliders remain wired to the legacy path only and are
  hidden in the UI once a valid profile exists.
- Presence smoothing (`onBallDetected`, 50-frame window / 30 %) is untouched in this phase. With the
  calibrated detector's much cleaner found/not-found stream we can later shorten the window for
  snappier presence — deferred, one-line change, needs studio evidence first.
- The drift monitor (§6) runs here too (it sees every frame): cheap median-luma check per frame,
  `environmentDrift(double severity)` signal at most once per few seconds.

## 5. Calibration protocol — `BallCalibrationController`

New `src/Gui/ball_calibration_controller.h/.cpp`, QML-exposed per camera (created by
`CameraInstance`, property `ballCalibration`, mirroring how panels reach per-instance state today).
GUI-thread state machine; frame capture and model fitting run on the detector thread via queued
invokes (the detector already owns the frames — calibration adds capture slots to it; fitting ~30
frames is < 100 ms, fine on that thread between frames).

```
            ┌────────────────────────────────────────────────────────────────┐
            ▼                                                                │
 Idle → CheckRoi → CaptureEmpty → CaptureBall → Fit → Validate ──pass──► Done
                    "clear the      "place a            │   ▲
                     hitting         ball in the        │   │ VALIDATE = repeat rounds:
                     area"           hitting area"      │   │  "remove the ball"  → expect
                                                        │   │    no detection for ~3 s
                                                        ▼   │  "place it (vary the spot)"
                                                     fail / └─── → expect detection < 0.5 s
                                                     widen        each round adds samples,
                                                                  refits θ, updates margin
```

Phase details:

- **CheckRoi** — ROI must exist and be sane (≥ ~40 px min side at native res); `cameraFixedInPlace`
  recommended (warn if false: profile will not be persisted across sessions, only used live).
- **CaptureEmpty** — N≈30 frames (~2 s at the throttled cadence). Live sanity: if per-pixel variance
  is huge (someone walking through), auto-extend and tell the user. Accumulates `BackgroundModel`.
- **CaptureBall** — wait for *stability*: the largest diff-blob must hold position/size for ~1 s
  (user's hand leaving), then capture N≈30. The blob segments the ball (no chicken-and-egg with the
  detector we're calibrating); `fitBallModel` learns scale/colour/template from it. Sanity gates:
  exactly one dominant blob, plausibly circular, ≥ 8 px radius — else explain and retry ("couldn't
  isolate the ball — check the area is clear and the ball is visible").
- **Fit** — score *all* captures through `ballcal::detect`, derive θ/margin.
- **Validate** — the user loop you asked for, made measurable. Each round: prompted removal (expect
  sustained not-found — catches false positives, the dangerous failure), prompted placement *at a
  varied spot within the ROI* (expect acquisition within ~0.5 s — catches false negatives and
  position overfit). Round samples are *appended* to the score sets, θ/margin recomputed.
  **Pass:** ≥ 2 consecutive clean rounds *and* margin ≥ floor. **Fail path:** a specific,
  actionable diagnosis — "the empty area scores too ball-like near the top-left (bright reflection);
  shrink the hitting area or move the mat" — derived from *where* the max-empty score occurred.
  User may also accept a marginal profile explicitly (stored with its margin; provenance records it).
- **Done** — profile assembled, pushed to the detector (`setProfile`), persisted (§7), QML
  `robustness` (0–1, from margin) exposed for the UI meter.

Cancellation at any phase reverts to the previous profile (or none). The controller never touches
buffer state (capture-intent contract — CLAUDE.md); it only consumes frames the detector already
receives. If the camera isn't running it asks the user to connect it first (wizard guarantees this
ordering anyway).

## 6. Drift monitoring & invalidation

A profile is a claim about an environment; the claim decays. Three layers:

1. **Hard invalidation** (profile dropped, recalibration required): ROI rect changed, camera
   resolution changed, `cameraFixedInPlace` switched off, profile schema version bump.
2. **Soft drift** (profile kept, flagged): per-frame median-luma gain outside the learned envelope
   (e.g. >±35 %) sustained for seconds → `environmentDrift` → UI badge "lighting changed —
   recalibrate?" on the camera panel / session toolbar. Detection keeps running gain-compensated;
   often still fine, which is why this is a prompt and not a stop.
3. **Age**: profile records `calibratedAt`; wizard shows age ("calibrated 3 weeks ago") and offers a
   one-round revalidate (a single Validate round, ~15 s) instead of full recalibration when a valid
   profile exists for this camera+ROI.

## 7. Persistence & provenance

**Profile storage** — per camera, *file-based* (a background-model image does not belong in
QSettings): `QStandardPaths::AppDataLocation`/`ballcal/<cameraKeyHash>/` containing `profile.json`
(version, ROI rect, radius/colour/θ/margin/envelope, timestamps, validation history) +
`background.png` + `template.png` (lossless). AppSettings holds only the pointer-ish bits
(`ballCalibrated` flag per cameraKey for cheap UI queries), via the single shared `AppSettings`
instance rule. Load at `CameraInstance` construction when `cameraFixedInPlace` and the stored ROI
matches; the **hitting-area ROI itself becomes persisted per camera** (today it is runtime-only —
this design makes it part of the profile, restored on connect, still editable which invalidates).

This construction-time auto-load is load-bearing for the UX model (§8): a user who configured ROI +
calibration in Settings and then **skips the start wizard entirely** (straight onto the session
rail) still gets the calibrated detector the moment the camera connects — no wizard participation
required.

**Provenance** — `swing.json` capture provenance (per the existing pattern, cb55419) gains, in the
camera stream's setup block: `"ballDetection": {"calibrated": bool, "margin": float,
"calibratedAt": iso8601, "driftAtCapture": float}`. SwingLab can then filter/correlate corpus
quality on ball-detection calibration the same way it does IMU calibration.

## 8. UX surfaces

**Principle: Settings is the home of ROI + calibration; the wizard only confirms.** A fixed face-on
camera's hitting area and ball calibration are environment properties, not session properties — they
belong with the other per-camera environment state (`cameraFixedInPlace`, mirrored, crop) in
Settings → Cameras. The wizard's job shrinks to *verifying* the claim, and users who skip the wizard
entirely (straight onto the session rail) lose nothing, because the profile + ROI auto-load at
`CameraInstance` construction (§7).

Follows the established calibration hosting pattern (`ImuCalibrationFlow.qml` real flow /
`CameraCalibrationFlow.qml` stub — both hosted by panels and the wizard with
`layoutMode`/`completed()`/`cancelled()`):

### 8.1 Settings → Cameras (primary home)

`CamerasPanel.qml` / `PpCameraPanel.qml` gain, for a connected camera with `cameraFixedInPlace` and
face-on perspective, a **Hitting Area & Ball Calibration** section:

- **ROI editor** — live `PpCameraFrame` with `roiEditable` (existing machinery), persisted on commit
  (§7 — B0). Editing the ROI invalidates any profile (§6) and the section says so before the drag.
- **`BallCalibrationFlow.qml`** (new) hosted inline — instruction text per phase, live thumbnail
  with the ROI overlay, capture progress, the robustness meter, pass/fail with the actionable
  diagnosis. Phase prompts are the state machine's QML-visible `phase` + `instruction` properties;
  no logic in QML. Requires the camera connected — same affordance as the IMU panel (Connect button
  right there).
- **Status line** — "✓ calibrated (margin 0.42, 3 days ago)" / "not calibrated — using generic
  detector", plus Recalibrate / Revalidate / Clear. The `houghConf`/`whiteSatCeil` sliders disappear
  when a profile is active (legacy-path-only controls).
- Gating: section hidden for non-fixed cameras (profile wouldn't survive — §5 CheckRoi warning
  becomes a hard precondition here, since Settings *is* the persistent path).

### 8.2 Session wizard (confirmation, skippable)

The Cameras step (Connect→Continue today) gains, for session types that use ball detection with a
face-on camera, a **confirmation row** after connect — three cases:

- **Valid profile exists** → collapsed card: "✓ Ball detection calibrated (margin 0.42, 3 days ago)
  — [Continue] [Revalidate] [Recalibrate]". Continue is the default; one click, no friction. This is
  the *managed-in-Settings* happy path.
- **No profile / invalidated** → "Ball detection not calibrated — [Calibrate now] [Skip]". Calibrate
  now runs the same `BallCalibrationFlow` inline (the wizard already hosts calibration flows this
  way); Skip proceeds on the legacy detector (provenance records it).
- **Soft-drift flagged** (§6) → the collapsed card carries the amber hint: "lighting has changed
  since calibration — [Revalidate] recommended".

Calibration is **encouraged, never mandatory** — Skip always proceeds.

### 8.3 Session toolbar — ball detector dot (`PpSessionToolbar.qml`)

The DETECT cluster already has one dot per modality with the ball dot reserved
(`DetectDot { id: ballDot; available: false }` — `PpSessionToolbar.qml:578`) and an established
visual language: brightness tiers (unavailable → dim → armed glow w/ breathing halo) + a green
flash on detection that decays over 2 s. The ball dot wires into that language rather than
inventing a new one, adding two ball-specific dimensions — **calibration state** and **presence**:

| State | Visual | Meaning |
|---|---|---|
| No face-on camera / ball detection off | even-more-dim (existing tier) | modality unavailable |
| Active, **uncalibrated** (legacy detector) | dim **amber tint** (`Theme.colorAttention`) | running on generic parameters — "calibrate me"; matches the cameras-pill amber "calibrate" hint convention |
| Active, **calibrated**, armed, no ball | accent glow + breathing halo (existing armed tier) | watching the hitting area |
| Ball **present** (calibrated, `ballPresent`) | core shifts to steady `Theme.colorGood` at moderate alpha | ball seen at the hitting area — the "ready to hit" cue |
| **Shot triggered from ball** (detected→lost transition committed/reported a candidate) | full green flash via `triggerFlash()` (existing mechanism + decay) | the vision modality fired |
| Soft drift flagged (§6) | amber tint over the current tier | lighting changed — recalibrate hint |

Wiring: `available` binds to (face-on instance connected && `ballEnabled`); the calibrated/amber
distinction binds to the profile-valid flag; presence binds to the existing
`CameraInstance::ballPresent`; the shot flash connects to the `Source::Ball` candidate/commit path
exactly as `imuDot`/`acDot` connect to their detectors' `impactDetected` (`PpSessionToolbar.qml:
663-668`) — that connection lands with the §11 `Source::Ball` follow-up; until then the dot has all
states except the flash. Steady-presence green and the flash compose naturally (the flash overlays
at opacity and decays to reveal the tier beneath — existing DetectDot behaviour).

## 9. Testing

Standalone CTest harnesses in `src/Pose/tests/` (project convention), Qt-Gui-only where possible:

1. **Model fitting** — synthetic ROI sequences (flat/noisy/gradient backgrounds; synthetic ball
   discs at varied luma incl. dim-studio levels V≈110) → assert learned radius/colour within
   tolerance, σ floor honoured.
2. **Threshold derivation** — score-set fixtures → θ placement, margin math, pass/fail floor,
   degenerate cases (overlapping distributions ⇒ fail, never a confident θ).
3. **Detection scoring** — golden frames: ball at calib spot / moved within ROI / absent / club-head
   intrusion / foot intrusion / ±30 % illumination → assert found/score behaviour; specifically
   assert the dim-studio case that defeats `V≥170` today.
4. **Gain compensation** — same scene at multiple synthetic exposures → detection invariance;
   envelope-exceeded flag fires.
5. **State machine** — drive `BallCalibrationController` with scripted frame feeds: happy path,
   walking-through-capture, hand-not-removed stability gate, failing validation rounds appending
   samples, cancellation restoring the prior profile.
6. **Throttle contract** — exactly one signal per `detect()` on every path incl. the new
   profile/legacy branches (extends the existing rule).
7. **Persistence round-trip** — profile save/load across a process boundary (the QSettings
   general-group lesson), schema-version rejection.
8. **Visual verification** — offscreen harness per the project pattern for the flow's phases.
9. **SwingLab** — once real studio captures exist: corpus runs comparing legacy vs calibrated
   detector presence streams (the `swinglab` skill's regression machinery).

## 10. Implementation plan

| Phase | Deliverable | Size | Risk |
|---|---|---|---|
| **B0 — ROI persistence** | Hitting-area ROI persisted per cameraKey (AppSettings), restored on connect when `cameraFixedInPlace`; wizard/panel unchanged | S | Low |
| **B1 — Detection core** | `src/Pose/ball_model.h` (+ `.cpp` if needed): background/ball models, scorer, θ derivation; tests 1–4. Pure math, no integration | M | Low — pure, testable |
| **B2 — Detector rework** | `BallDetector` profile path + legacy fallback, `setProfile`/`clearProfile`, drift monitor + signal, throttle test 6 | M | Low — contract preserved, legacy default |
| **B3 — Calibration controller** | `BallCalibrationController` + detector-thread capture slots; profile persistence (§7); tests 5, 7 | M/L | Med — threading + state machine |
| **B4 — UX** | `BallCalibrationFlow.qml`; Settings → Cameras section (ROI editor + flow hosting + status line, §8.1); wizard confirmation row (§8.2); ball `DetectDot` states (calibrated/amber, presence, drift — §8.3, flash excluded); robustness meter; slider retirement; test 8 | M/L | Low/Med — pattern exists |
| **B5 — Provenance + field validation** | swing.json `ballDetection` block, SwingLab corpus fields, real-studio validation incl. dim-light sessions; test 9 | S/M | Med — hardware-gated |

Each phase lands independently; B1+B2 already fix nothing-detected-in-dark-studio *if* a profile is
created via a temporary debug hook, but the feature is "done" at B4. B5's studio validation gates
declaring the legacy path deprecated.

**Touch list** — *New:* `src/Pose/ball_model.h`, `src/Gui/ball_calibration_controller.{h,cpp}`,
`src/Gui/BallCalibrationFlow.qml`, `src/Pose/tests/*`. *Edit:* `src/Pose/ball_detector.{h,cpp}`,
`src/Gui/camera_instance.{h,cpp}` (profile load, controller, ROI persistence),
`src/Gui/app_settings.{h,cpp}` (ballCalibrated, persisted ROI), `ScreenSessionWizard.qml`,
`PpCameraPanel.qml` + `CamerasPanel.qml` (§8.1), `PpSessionToolbar.qml` (ball DetectDot states,
§8.3), `swing_exporter.cpp` + `shot_processor.cpp` (provenance), `CMakeLists.txt`.
*Unchanged:* `FrameThrottle`, `BallDetection` struct, presence hysteresis, `ShotController`,
QML overlay bindings.

## 11. Follow-ups enabled (not in scope)

- **`Source::Ball` shot candidate** — a calibrated, position-locked stationary detector makes
  ball-disappearance a sharp event: presence locked at the calibrated spot, then a sudden
  score collapse within 1–2 frames. That is a credible `reportCandidate(Source::Ball, …)` feed into
  the existing arbiter (hold window 200 ms comfortably covers 1–2 frames at 30–60 fps) — far simpler
  than the Kalman `ballLaunched` route for the *trigger* use-case. Keep dormant until the detector
  is field-proven (same discipline as `ballLaunched` in the CNN plan §8). When it lands, it also
  completes the toolbar ball dot (§8.3): the green shot flash wires to this candidate path the same
  way the IMU/acoustic dots wire to their detectors.
- **Presence window shortening** (50→~12 frames) once the cleaner stream is confirmed in-studio.
- **CNN flight tracking** (`ball_detector_design.md`) — unchanged; its SEARCHING state should use
  this calibrated detector as the acquisition layer when a profile exists.
- **Score-weight sweeps in SwingLab** if studio evidence shows the fixed weights leaving margin on
  the table.

## 12. Risks & open questions

- **White/bright mats** (ball-coloured background) — the diff path still works (the ball casts
  shadow + occludes texture), but margin will be small; the calibration loop *detects and reports*
  this honestly instead of silently failing later. This is the main "environment unsuitable" case.
- **Player shadow over the ROI during play** — gain compensation uses background-classified median
  (robust), and the score is multi-cue; needs studio validation (B5).
- **Throttled cadence** (`skipFactor=2`) caps capture speed — calibration captures take ~2 s per
  block; acceptable. Not changing the throttle here.
- **Multiple balls in the ROI** (player drops a second ball) — scorer picks the best candidate;
  presence stays true. Fine for presence/trigger semantics; trajectory work is the CNN plan's job.
- **Open:** should Validate *require* a varied ball position (enforced via min displacement from the
  calibration spot), or merely encourage it? Default: encourage (prompt wording), don't block.
