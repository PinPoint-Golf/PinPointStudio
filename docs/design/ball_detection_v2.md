# Ball Detection v2 — Self-Calibrating Temporal Matched Filter

**Robust stationary-ball identification with zero user calibration, validated against the
real swing corpus before a line of C++ exists.**

Replaces the runtime core *and* the user-in-the-loop calibration protocol of
[`ball_detection_calibration.md`](ball_detection_calibration.md) (B0–B5, 2026-06). The CNN/Kalman
flight-tracking plan ([`ball_detector_design.md`](ball_detector_design.md)) is unaffected; this
becomes its acquisition layer. Part of the vision modality of `shotdetection.md` — v2 finally
delivers the `Source::Ball` trigger candidate (§11 of the calibration doc, dormant until now).

Status: **design + corpus-validated prototype (2026-07-07). Not implemented.**
Prototype/validation harness: [`tools/balllab/`](../../tools/balllab/) (`corpus_separation.py`,
`launch_trace.py`) — run against `/mnt/swingdata/Mark-Liversedge` (44 swings, 4 sessions, 3
distinct lighting regimes).

---

## 1. Why the calibrated method is not reliable

The v1 design (background model + multi-cue scorer + derived threshold θ) is sound *in its own
frame of reference*, but four structural problems showed up the moment real corpus data existed:

1. **It is never calibrated in practice.** All 44 corpus swings carry
   `"ballDetection": {"calibrated": false}` in their swing.json provenance. The place/remove/
   validate protocol is enough friction that it doesn't get done, and an uncalibrated v1 detector
   emits nothing at all (legacy path retired). A detector whose precondition is a 2-minute chore
   the user skips is, operationally, a detector that doesn't run. **Reliability begins with
   removing the user from the loop.**

2. **A frozen background model meets a scene that never stops changing.** The corpus shows the
   player's shadow sweeping the entire hitting area during address and swing, feet entering the
   ROI margin, and the club resting adjacent to the ball. Ground truthing the corpus by
   `median(pre-impact) − median(post-impact)` frame differencing located *the stance shadow*, not
   the ball, in most swings — the shadow delta dwarfs the 19 px ball by two orders of magnitude in
   changed-pixel mass. v1's per-pixel σ-diff against a calibration-time background faces exactly
   that: the diff mask lights up with shadow/feet/club blobs, and its single global gain
   compensation cannot describe a *spatially varying* illumination change.

3. **Absolute-luma cues die under saturation, and the hitting area is the brightest spot in the
   studio.** Measured mat luma directly around the ball: 06-11 session ≥ 250 (fully clipped —
   the ball is literally white-on-white, invisible except for its contact shadow), 07-03/07-05
   partially clipped (200–250), 07-04 healthy (120–195). v1's contrast cue (`mean |ball − bg|`,
   weight 0.35, plus a hard `kContrastWindowLo` gate) reads ≈ 0 in the clipped regime however
   real the ball is.

4. **The profile is a claim about one environment instant.** Lighting drifts, mat wear changes,
   ROI edits invalidate — every drift path ends in "recalibrate?" friction, which goes back to
   problem 1.

## 2. What the corpus actually shows (measured evidence)

Face-On.mp4, 1280×1024 (one session 720×1024), ~149 fps within the 5 s swing window.

| Fact | Measurement |
|---|---|
| Ball size | r ≈ 9–10 px at 1280 px width (diameter ~19 px); consistent across sessions |
| Ball position | Bottom-centre, y ≈ 955–1010 (within ~60 px of the frame's bottom edge); per-session spread of placements ≈ ±20 px; per-session cluster centres differ by < 100 px across 4 sessions |
| Lighting regimes | 07-04: ball 255 on mat 120–195 (strong contrast) · 07-03/07-05: ball 255 on mat 200–250 (weak contrast + contact shadow) · 06-11: ball 255 on mat ≥ 250 (**zero luma contrast**; only the shadow crescent remains) |
| Static distractors in/near ROI | white painted target line across the mat, light-pool boundary, chrome club-head reflections (ball-scale!), white shoes, mat speckles |
| Launch signature | at-spot response collapses to < 10 % of its address level within **2 frames at 149 fps (~13 ms)**; motion-blur streak visible for 1–2 frames |
| Address stability | at-spot response never dips below ~85 % of median across the full ~3.4 s address (waggle, grounding the club, foot shuffles included) in healthy-exposure sessions |

**Prototype result** (`tools/balllab/corpus_separation.py` — scale-matched DoG response, median
response map over ball-present frames `medP` vs ball-absent frames `medA`, self-location by
`argmax(medP − |medA|)`, then per-frame response at the locked spot):

- **43/44 swings separate cleanly** (10th-percentile present response > 90th-percentile absent
  response with margin), including all nine fully-saturated 06-11 swings.
- Separation ratio present/absent at the spot: ~10–20× (07-04), ~5–8× (07-05), ~4–5× (07-03),
  ~4–10× but small absolute values (06-11).
- The one failure (07-03/swing_0002): ball nearly saturated, response only drops to ~50 % after
  launch — presence still readable, launch edge marginal.
- Per-frame trace (`launch_trace.py`): in the fully-clipped 06-11 regime the per-frame response
  intermittently reads 0 during address — **full saturation is not reliably detectable and must be
  prevented at capture time, not survived algorithmically** (§6).

Two negative results that shaped the design (both reproducible with the harness):

- A *global* "most ball-like peak in the ROI" search fails: static distractors (line, pool edge,
  club chrome) out-respond the ball in 2 of 4 sessions. Their tell: identical response with the
  ball present and absent. → identity must be *temporal novelty*, not per-frame appearance.
- Differencing raw luma before band-passing fails: shadow deltas dominate. Band-pass at ball scale
  *first*, then reason temporally — large-scale illumination structure never enters the pipeline.

## 3. Core idea

> **A golf ball is not "a white circle". It is the only ball-scale feature that *appears* in the
> hitting area, *persists perfectly motionless* for seconds, and *vanishes within two frames*.**

Four pillars, each earning its keep against a measured failure mode:

1. **Scale-matched band-pass (spatial)** — DoG response `G(σ₁) − G(σ₂)` with σ₁ = r/1.6,
   σ₂ = 3.2·σ₁. Insensitive to absolute luma and to any structure much larger than the ball
   (shadows, light pools, gain drift). This is the only per-pixel image operation in the design.
2. **Novelty against a static-scene baseline (what v1's background model becomes)** — a slowly
   adapted per-pixel baseline *of the response map itself*, not of the image. The painted line,
   pool edge and mat speckles live in the baseline and cancel; a newly placed ball is novel.
   No capture protocol: the baseline is learned from the live stream and freezes under the locked
   ball's footprint.
3. **Static-scene accumulation (temporal integration) — the acquisition engine.** The ball is
   *motionless for seconds*, so the novelty is integrated over a short acquisition window (a fast
   EMA of the response, ≈0.35 s): the ball, in the same pixels every frame, reinforces while
   per-frame texture and moving-shadow noise average out (≈√N SNR gain). **This is what makes a
   low-contrast ball detectable** — a white ball on an over-bright mat has near-zero *per-frame*
   contrast yet is unmistakable once integrated. Measured (V0): bringing accumulation into the
   causal tracker took the weak-contrast 07-03 session from **0/10 to 10/10 acquired**. Accumulation
   belongs to **acquisition only** — see pillar 4.
4. **Stability lock + per-frame monitoring (real-time).** A candidate must hold position within
   ±2 px with sustained *accumulated* novelty before it *locks*. Once locked, presence and the
   launch edge are read from the **instantaneous** response at the frozen 5×5 spot — no accumulation
   lag — so the live present/absent signal and the 2-frame launch collapse stay real-time. This
   split is the whole game: **accumulate to *find* the static ball (slow, robust); read per-frame to
   *watch* it (instant)** — so the low-contrast robustness costs nothing at monitoring time, and the
   existing live present/absent feature is retained. Waggles, hands, feet and the club can't lock
   (they move); distractors can't lock (not novel, and rejected by the shape gate §4.4).

Everything is expressed in **noise units of the scene itself** (robust σ of the response map),
never absolute luma — the same thresholds hold from the dim corner of a daylight studio to a
nearly clipped mat.

## 4. Algorithm specification

### 4.1 Definitions

For each analysed frame (ROI-cropped, grayscale float):

```
R(x,y)   = GaussianBlur(gray, σ₁) − GaussianBlur(gray, σ₁·3.2)   σ₁ = r̂/1.6
noise    = 1.4826 · MAD(R over ROI)                               (robust, per frame)
B(x,y)   = response baseline: EMA of R, τ ≈ 10 s, updated only where
           |R − B| < 4·noise (change-gated) and never inside a locked ball's 3r̂ disc
N(x,y)   = (R − B) / noise                                        "novelty", dimensionless
```

`r̂` is the current radius estimate (§4.4). Before any radius is known, run 3 scales
(r ∈ {5, 8, 12} px scaled by frameWidth/1280) and let the lock pick the winner.

**Acquisition accumulator (§3 pillar 3).** Because the ball is static, acquisition runs on a
temporally *integrated* novelty, not the per-frame `N`:

```
A(x,y)  = fast EMA of R, τ_acc ≈ 0.35 s        (the static ball reinforces; noise averages out)
D(x,y)  = A − B                                 accumulated novelty (raw)
N_acc   = D / (1.4826 · MAD(D))                 acquisition novelty, in scene-noise units
```

SEARCH/CANDIDATE (§4.2) operate on `N_acc`; the LOCKED monitor and the launch edge (§4.5) read the
**instantaneous** `R` at the locked spot (pillar 4). The accumulator is causal, so the same code runs
live and over a recorded window (§5). Validated in V0: with per-frame `N` the weak-contrast 07-03
session acquired 0/10; with `N_acc` it acquires 10/10.

### 4.1a Search-space prior — the hitting-area ROI and the stance corridor

Search is confined to the **hitting-area ROI** — the tight region the user frames around the ball
spot (the existing `setRoi`, unchanged from v1). This is the *primary* distractor cut: anything
outside it (a mat-texture patch, a second ball, the light-pool edge) is never searched, so only
features *inside* the hitting area — most notably the clubhead resting against the ball at address —
can compete, and those are handled by the shape gate (§4.4) and the temporal logic (§4.3). Measured
in V0: with a loose fallback band a bare-mat novelty artifact ~100 px from the ball won several
weak-contrast 07-03 locks; constraining to a per-session hitting-area ROI recovered the correct spot
on all of them. **The ROI is not recorded in swing.json today** (it lives in per-camera settings) —
v2 provenance should record it (§7) so re-analysis and this harness use the real one, not a proxy.

The ROI is *tightened further* by the stance corridor when pose is available. Golf geometry bounds
where a stationary ball can be, and PinPoint already measures it. The face-on
`CameraInstance` runs the pose estimator on the *same* frames as the ball detector (the
`FrameThrottle` consumerCount = 2 is pose + ball), so the COCO ankle keypoints (15 left, 16 right)
are in hand at **zero extra inference**. They define a per-frame **search corridor** `S`:

```
S = { x ∈ [min(ankleX) − m, max(ankleX) + m],  y ∈ [ankleLine, groundLine] }
    m ≈ 0.5·(stance width)      groundLine = ROI/frame bottom
```

The ball is *always* horizontally between the feet (front-to-back stance width — true even for a
driver, which sits inside the lead foot) and *always* below the ankle line (it rests on the ground;
the ankle joints project above it). Peak search (§4.2 SEARCH) is restricted to `S`. This is a
causal, per-golfer replacement for the static band prior (`y ∈ [890, h]`) the prototype used: it
moves with the golfer (no ROI re-aim), it is a fraction of the band's area (cheaper), and it
excludes whole distractor classes *before candidacy* rather than relying on them failing to lock
(§4.3).

`S` is an **additive robustness layer, not a hard dependency**. It is used only in SEARCH/CANDIDATE —
once LOCKED the tracker monitors the frozen spot, so corridor drift during the downswing (weight
transfer, trail-heel lift) is irrelevant: the ball was acquired at address, when the stance is
stillest. When pose is unavailable (inference off, low keypoint confidence, or the golfer has not
yet entered frame) the detector falls back to the static ROI band and loses only the distractor
pruning, never the ability to lock. Face-on only; the hitting-area ROI is already face-on.

**Clubhead at address** is the corollary: the club is presented to the ball at address, so a
freshly-placed ball normally has an adjacent bright, ball-scale feature (often chrome). The lock
must therefore *not* reject a candidate merely because something touches it — if anything the
adjacent head is weak corroboration at the lock instant. That same head becomes the §4.3
chrome-glint distractor once motion starts; the temporal logic already kills it (it moves, and the
ball's spot is baseline-frozen before the head swings back through).

### 4.2 State machine

```
            candidate appears            stable ≥ T_lock
  SEARCH ────────────────────► CANDIDATE ────────────────► LOCKED
    ▲     N > k_appear at a       │  moved > 2px /            │
    │     ball-scale peak         │  N < k_hold               │ response collapse (§4.5)
    │                             ▼                           ▼
    └────────────────────── back to SEARCH             VANISHED ──► ballLaunched
    ▲                                                     │  (re-appears within 3r̂ ≤ T_move:
    └─────────────────────────────────────────────────────┘   it was a nudge → re-CANDIDATE)
```

- **SEARCH** (per frame): find local maxima of the **accumulated novelty `N_acc`** (§4.1) above
  `k_appear` with ball-scale support (non-max suppression radius 2r̂), **within the hitting-area ROI**
  (tightened by the stance corridor `S` §4.1a when pose is available). Track up to K=3 peaks
  frame-to-frame; a peak becomes a candidate only if its accumulated response is a ball-scale disc
  (shape gate §4.4).
- **CANDIDATE → LOCKED**: same peak within ±2 px for `T_lock = 0.5 s` of detector frames, median
  `N ≥ k_lock = 5`. On lock: freeze sub-pixel centre (§4.4), radius, a small self-template
  (grayscale patch, for the optional verification cue), the locked response level
  `L₀ = median(at-spot response)`, and punch the baseline hole. Hand/club movement during
  placement naturally delays the lock until the scene settles — no stillness gate needed.
- **LOCKED** (per frame, cheap — a 5×5 read of `R` at the spot, full-map work optional at reduced
  cadence): maintain `L = EMA(at-spot max of R)`. Two exits:
  - **Occlusion dip**: `L < 0.4·L₀` sustained < `T_occl = 300 ms` then recovers → stay LOCKED
    (club passing on a practice swing, hand reaching in). Presence (`ballPresent`) is NOT dropped
    during a dip shorter than T_occl.
  - **Collapse** (§4.5) → VANISHED.
- **VANISHED**: report the launch candidate immediately (§4.5); then watch 3r̂ around the old spot
  for `T_move = 400 ms`. Re-appearance → the ball was nudged/replaced: re-candidate at the new
  spot, no state carried. No re-appearance → return to SEARCH (next ball placement).

### 4.3 Why each distractor fails to lock

Two layers reject distractors: the stance corridor `S` (§4.1a) removes everything outside the
between-feet, below-ankle region *before* it can become a candidate; the temporal logic below
handles what remains inside the corridor.

| Distractor | Killed by |
|---|---|
| Anything outside the between-feet / below-ankle region | not in corridor `S` (§4.1a) → never searched (the painted line's outer run, pool edge, distant chrome, both shoes) |
| Painted line / pool edge / mat speckle (the part inside `S`) | in baseline `B` → novelty ≈ 0 |
| Player shadow sweeping | large-scale → no DoG response at ball scale |
| White shoes | foot-scale (≫ 3σ₁) → weak response; and they move |
| Club-head chrome glint at address | moves with every waggle → never passes T_lock; and typically arrives after the ball is already LOCKED (its spot is baseline-frozen, not searched) |
| Motion-blur streak at impact | present for 1–2 frames → never a candidate |
| Lighting switch flipped (new static bright spot) | is novel and static — would lock. Rejected by the size/shape of the matched-filter peak (scale-space check §4.4) and, if genuinely ball-like, self-corrects when the baseline re-learns after T_baseline_forget; accepted residual risk (§10) |

### 4.4 Position, radius, scale

- **Sub-pixel centre**: 2-D quadratic fit over the 3×3 response neighbourhood of the peak.
  This is the px→mm anchor (`kBallDiameterMm = 42.67`) for the low-point metric — jitter target
  < 0.3 px over a 3 s address (achievable: the corpus response is essentially static).
- **Radius**: at lock, evaluate `R` at scales r̂·{0.8, 1.0, 1.25}; parabolic fit over the three
  scale responses.
- **Shape gate (candidacy, §3 pillar 4)**: a peak becomes a candidate only if the accumulated
  novelty `D` (= A − B) around it is an isotropic ball-scale disc — tested by the 2-D covariance of
  its positive lobe: eigenvalue ratio (elongation; disc ≈ 1, ridge ≫ 1) and √(smaller eigenvalue) ≈
  r/2 (a normalization spike has extent ≈ 0 and is rejected). This is **amplitude-invariant**, so it
  passes a *weak* round ball (07-03) that the earlier peak/ring ratio wrongly rejected, while still
  rejecting the painted line and the address shaft. It runs on `D` (background-subtracted), where the
  ball is a clean disc even when buried in a bright mat in the raw image.
- **Cross-swing prior**: per cameraKey, persist EMA of locked radius and spot (AppSettings,
  auto-written — this replaces the v1 profile store; ~4 numbers, no mats). Seeds the scale scan
  and a soft position prior (search everywhere, but a candidate inside the prior window gets
  T_lock halved — the ball reappears at the same worn spot dozens of times per session).

### 4.5 Launch event (`Source::Ball`)

Trigger condition, evaluated in LOCKED state:

```
at-spot response < 0.25·L₀   for  ≥ 2 consecutive frames,
with the preceding frame ≥ 0.8·L₀        (cliff, not a fade)
```

Corpus: the collapse is 84→39→−5 and 26→21→3 within 2 frames at 149 fps — the cliff shape
separates a launch from every occlusion dip observed (dips are partial and recover).

- **Report immediately** on the second collapse frame: `reportCandidate(Source::Ball,
  estImpactUs, conf = 0.6)`. Do *not* wait out T_move — a nudge that mimics a collapse produces a
  lone 0.6-confidence candidate which the arbiter will not commit (single-modality floor is 0.8),
  while a real shot gets IMU + acoustic agreement inside the 200 ms hold. That is precisely the
  arbiter's job; don't duplicate it in the detector.
- **Timestamp**: `estImpactUs = ts(first collapse frame) − framePeriod/2`. The frame timestamp
  must travel with the frame into `detect()` — today `framePreprocessed(cv::Mat)` carries none;
  the signal gains a `qint64 timestampUs` (capture timestamp from the source, same clock as the
  EventBuffer), NOT `nowMicros()` at detect time. At the current throttled detector cadence
  (~15–30 Hz) the residual error is ±½ frame ≈ 17–33 ms — inside the arbiter's 40 ms agreement
  window, but only because the timestamp is the *frame's*, not the handler's. A LOCKED-state
  fast path running the 5×5 spot probe unthrottled at full camera rate is the designed follow-up
  if field data shows the margin is too thin (§10).
- **Ball-departure latency (V0 measurement)**: the visible collapse lands a consistent **+3–4 frames
  (~20–27 ms) after** the acoustic/IMU `impactUs` — the ball is in contact/compression before it
  visibly departs. Treat this as a calibrated per-source constant (`kBallLaunchLatencyUs`, sibling of
  `kImuBleLatencyUs`) and back-date `estImpactUs` by it, rather than widening the fusion window.

### 4.6 Optional verification cue (lock time only)

NCC of the locked patch against the frozen self-template each ~1 s. Cheap; catches the
"static bright spot locked during a lighting event" residual. Never gates the launch edge
(the edge is time-critical); only demotes a stale lock back to SEARCH. Colour is deliberately
NOT a cue anywhere — range balls are yellow/orange, and the 06-11 regime proves luma-chroma
cues can vanish entirely.

## 5. What the corpus prototype maps to

| Prototype (offline, python) | Product (live, C++) |
|---|---|
| `medP` accumulation over static frames | **needed, and causal** — the fast-EMA accumulator `A` (§4.1). V0 proved a per-frame threshold misses low-contrast balls (07-03 0/10 → 10/10 with accumulation), so this is a load-bearing pillar, not an offline convenience |
| `medP − |medA|` self-location | the causal accumulated novelty `N_acc = (A − B)/σ`; placement + stability lock does it online |
| present/absent frame sets around impactUs | LOCKED at-spot monitor (per-frame `R`) before/after the collapse edge |
| 43/44 clean separation | presence + launch reliability claim, per §9 gates |
| band prior y ∈ [890, h] | the user hitting-area ROI (`setRoi`), tightened by the pose-derived stance corridor `S` (§4.1a) when pose is present |

The two "entry points" are the **same accumulator** fed online (live) or over a recorded window
(`swing_reanalyzer`/SwingLab, which can locate the ball and launch frame at 149 fps precision for the
low-point metric and export overlays). Same header, same code — only the frame source differs.

## 6. Exposure QA — the required companion (not optional)

**Two regimes — do not conflate them (V0 correction).** The original doc treated any bright mat as a
capture defect; V0 showed that is only half true:
- **Full saturation (06-11, satFrac ≈ 0.5–0.7)** — the mat is clipped at 255, information destroyed
  at the sensor. Unfixable in software; capture-side only. Presence may survive on the contact
  shadow, but the launch edge must not be trusted.
- **Bright mat / weak *per-frame* contrast (07-03 & 07-05, satFrac ≈ 0.15–0.35 over the ROI)** —
  information is NOT destroyed: the ball is plainly visible, only its *single-frame* luma contrast is
  low. **Accumulation recovers it** (V0: 07-03 went 0/10 → 10/10 acquired; 07-05 likewise). This is a
  machine-vision problem the design now solves, not a capture defect — better lighting still helps,
  but the detector no longer *needs* it.

The exposure guard therefore becomes a *quality* signal, not a go/no-go — the detector acquires
across both regimes, and the warning tells the user when the *launch edge* (not acquisition) is at
risk:

- Per frame (already in the detector's hands): `satFrac` = fraction of ROI pixels ≥ 250.
- `satFrac > 0.25` sustained → new signal `exposureWarning(double satFrac)` → amber hint on the
  camera panel / ball DetectDot ("hitting area over-exposed — reduce exposure or re-aim light").
  This replaces v1's gain-drift monitor as the environment-health signal (the drift monitor's
  job — "profile no longer matches" — has no meaning when nothing is pre-calibrated).
- Provenance: the swing.json `ballDetection` block (§7) records `satFracAtCapture` so SwingLab
  can stratify corpus quality, exactly like IMU calibration state.
- Fully-clipped scenes still *degrade gracefully* (the prototype separated all nine 06-11 swings
  on median statistics — the contact shadow carries ~5σ) — presence will usually still work;
  the launch edge must not be trusted there (`conf` drops to 0.4 when satFrac > 0.25 at the spot).
- **V0 note**: measured over the *tight* ROI, satFrac reads 0.32–0.35 on 07-03/07-05 — which now
  *acquire cleanly* — so a 0.25 warn threshold over the ROI is a launch-edge caution, not an
  acquisition gate. A complementary **ball-vs-mat contrast (SNR)** health signal is the better
  predictor of when acquisition itself is at risk; add it alongside satFrac.

## 7. Integration (PinPoint specifics)

**`BallDetector` keeps name, thread, and external contract** — `detect(cv::Mat)` slot (gains the
timestamp arg), `ballDetected`/`detectionSkipped` signals, `BallDetection{found,x,y,radius,
detectMs,score}` normalisation, one-signal-per-frame throttle rule (consumerCount=2 —
`clearBusy`/`clearRawBusy` both wired, CLAUDE.md), enabled/ROI early-outs (`detectionSkipped`,
never a spurious found=false). `score` becomes the novelty `N` at the reported peak (dimensionless,
still monotonically "how ball-like").

Changes:

- `ball_model.h` (v1 core) is replaced by a new header-only `ball_temporal.h` (same OpenCV-only,
  no-Qt convention; standalone-testable in `src/Pose/tests/`). `BallCalProfile`,
  `ball_calibration_store.h`, `BallCalibrationController`, `BallCalibrationFlow.qml` (the
  place/remove/validate *protocol*) and the Settings calibration section are **retired** (Settings
  shrinks to the ROI editor + exposure/contrast health line). **The wizard ball step is repurposed,
  not deleted** — see the live-preview bullet below. Keep `kBallDiameterMm` (move to the new header).
  The `setProfile`/`clearProfile`/`beginCalibCapture` slots and `calibFrame`/`calibCaptureDone`/
  `environmentDrift` signals go with them (`environmentDrift` → `exposureWarning`).
- **Wizard ball step → live "setup & verify" (replaces v1 calibration).** The start-session wizard's
  ball step (`ScreenSessionWizard.qml`) becomes a *live preview*, not a place/remove/validate chore.
  While the step is active it runs the v2 detector on the connected face-on camera and shows a plain
  **"ball detected / not detected"** badge (driven by the detector's presence / `ballLocked`), over
  the editable hitting-area ROI (`PpCameraFrame roiEditable`), plus the **exposure/contrast health**
  hint (`exposureWarning` + the ball-vs-mat contrast signal §6). The user drags the ROI and adjusts
  studio lighting until it reads *detected* steadily, then Continue — **that is the whole
  "calibration"**: no profile is saved; the detector self-calibrates at runtime (§3). The wizard
  `SummaryRow` reads "Ball: detected / not detected" instead of "Calibrated / Skipped". This is the
  primary purpose of the ROI here — get the hitting area framed and the lighting right so acquisition
  is reliable before the session starts.
- New signals: `ballLocked(float x, float y, float radiusNorm)` (feeds the swing.json
  `ballDetection.center/radiusNorm` provenance — `positionSource: "auto"`),
  `ballLaunched(qint64 estImpactUs, float x, float y)`, `exposureWarning(double satFrac)`.
- **`Source::Ball` wiring (main.cpp)**: `ballLaunched` → `ShotController::reportCandidate(
  Source::Ball, estImpactUs, conf)` behind the existing `autoDetectSwing` setting, mirroring the
  IMU/acoustic detector wiring; the toolbar ball DetectDot finally gets its green flash (v1 doc
  §8.3 table stands, minus the amber "uncalibrated" tier — there is no uncalibrated state
  anymore; amber now = exposure warning).
- **Presence smoothing** (`CameraInstance::onBallDetected`, 50-frame/30 % window,
  `kBallPresentThreshold`): keep the contract initially, but the detector's internal occlusion
  hysteresis (§4.2) means the found/lost stream is already debounced — shortening the window to
  ~12 frames is the planned follow-up once field-confirmed (unchanged from v1 doc §11).
- **Frame timestamps**: `VideoPreprocessorOpenCV::framePreprocessed` (and the raw-path
  equivalent) gain `qint64 timestampUs` sourced from the capture pipeline's existing EventBuffer
  clock. One of two changes outside `src/Pose/` (the other is the stance-corridor input below).
- **Stance-corridor input** (§4.1a): the pose estimator already running on the face-on
  `CameraInstance` pushes its latest ankle line to `BallDetector` via a cheap queued setter modelled
  on `setRoi` — e.g. `setStanceBounds(QPointF leftAnkleN, QPointF rightAnkleN, float conf)` in
  normalized frame coords — with a staleness/confidence guard and static-band fallback. No new
  inference; SEARCH-phase only. This is also what answers the v1 auto-ROI question (§10): the stance
  *is* the search region.
- Per-cameraKey persistence (radius/spot EMA — §4.4) goes through the single shared `AppSettings`
  instance (CLAUDE.md rule), auto-written, no UI.

Threading and cost: SEARCH does 2 Gaussian blurs + peak scan on the ROI (corpus ROI ≈ 700×130:
well under 1 ms); LOCKED does a 5×5 read (nothing). All state lives on the detector thread;
slots stay queued; no mutexes anywhere near the frame path.

## 8. Parameters (all dimensionless unless stated)

| Param | Value | Provenance |
|---|---|---|
| DoG σ₁ | r̂/1.6 | prototype (peak response at true r) |
| DoG σ₂/σ₁ | 3.2 | prototype |
| bootstrap radii | {5, 8, 12} px · frameWidth/1280 | corpus r ≈ 9–10 px @1280; ±40 % window |
| `k_appear` | 5 σ | corpus: ball 4–20 σ, empty-scene spot ≤ ~2 σ |
| `k_lock` | 5 σ median over T_lock | same |
| `T_lock` | 0.5 s (halved inside spot prior) | placement→address gap ≫ 1 s in corpus |
| lock stability | ±2 px | corpus response static to < 1 px |
| occlusion dip floor | 0.4·L₀, `T_occl` 300 ms | corpus: no address dip below 0.85·L₀ observed; 0.4 leaves 2× margin both ways |
| collapse | < 0.25·L₀ ×2 frames, from ≥ 0.8·L₀ | corpus: collapse to < 0.10·L₀ in 2 frames |
| `T_move` (nudge window) | 400 ms | hand re-placement speed; not latency-critical |
| baseline τ | 10 s, change-gated at 4 σ | slow vs placement (s), fast vs session drift (min) |
| `satFrac` warn | > 0.25 of ROI ≥ 250, sustained 2 s | 06-11 ≈ 0.5, 07-05 ≈ 0.15, 07-04 ≈ 0.06 |
| launch conf | 0.6 (0.4 if satFrac > 0.25 at spot) | arbiter single-modality floor is 0.8 — by design cannot self-commit |

Tuning philosophy unchanged from v1: these are fixed by design; only field evidence via the
corpus harness moves them. None is an absolute luma.

## 9. Validation & acceptance (before any live wiring)

The corpus is the gate — extend `tools/balllab/` into the acceptance harness (python, offline,
runs on every corpus session including future ones; same role SwingLab plays for the shaft):

1. **Replay acceptance** — implement the v2 state machine in the harness (or bind the C++ core
   once it exists) and run every corpus swing end-to-end at native 149 fps:
   - lock acquired before impact−1 s in ≥ 95 % of swings with satFrac ≤ 0.25;
   - locked position within 3 px of the human-verified ball centre (spot-check set);
   - **zero** launch events during address across the whole corpus (waggles, grounding, practice
     strokes are all in there);
   - launch edge within ≤ 3 frames (≤ 20 ms) of `capture.impactUs` on every swing where
     satFrac ≤ 0.25 (impactUs is IMU/acoustic-derived — independent truth);
   - the 06-11 session runs with the exposure warning asserted and no false launches (degraded
     but honest).
2. **Unit tests** (`src/Pose/tests/`, header-only core, project conventions): synthetic disc
   appear/persist/vanish sequences over flat/noisy/gradient/moving-shadow backgrounds; saturation
   ramp; baseline absorption of a static line; nudge-vs-launch; scale-space rejection of edges;
   sub-pixel jitter bound.
3. **Throttle contract test** — exactly one signal per `detect()` on every path (extends the
   existing rule; the state machine adds paths — LOCKED fast path included).
4. **Live studio session** — presence dot through a normal session, launch flash on real shots,
   arbiter log shows Ball candidates fusing with IMU/acoustic (not committing alone).

## 10. Risks & open questions

- **Static ball-like object placed in ROI during a lighting change** (§4.3 last row): residual;
  bounded by the NCC self-check + baseline re-learn. Accept.
- **Launch timestamp margin at throttled cadence** (§4.5): ±½ detector frame. If field data shows
  Ball candidates falling outside the 40 ms fusion window, add the unthrottled LOCKED-state spot
  probe (design allows it: 5×5 read, no allocation) — decide on evidence, not now.
- **Two balls in the ROI** (player drops the next ball early): second novel stable peak while
  LOCKED. Policy: track as shadow-candidate, ignore for presence/launch until the primary
  vanishes and T_move expires — then it's the natural next lock. Cheap, worth doing in v2.0.
- **Putter-pace nudges** that roll the ball slowly out of the 3r̂ re-acquire window: reads as
  launch candidate (conf 0.6, won't commit) + re-lock at the new resting spot via SEARCH. Fine.
- **Ball on a tee** (Swing sessions, driver): unchanged geometry ±; tee stalk is sub-scale. The
  corpus has no tee'd swings yet — add a session to the corpus before declaring Swing-type done.
- **Wrist/GRF/Coach sessions without a ball**: SEARCH simply never locks; `ballPresent` stays
  false; nothing fires. No per-session-type gating needed beyond the existing `ballEnabled`.
- **Pose availability for the stance corridor** (§4.1a): when pose inference is off or
  low-confidence the corridor is unavailable and the detector falls back to the static ROI band —
  correct, but with the full distractor surface. If a session runs ball detection *without* pose,
  the band must be a real (user or auto) ROI, not the whole frame. Acceptable; verify in field
  validation (§9.4).
- **Resolved (was open in v1)**: hitting-area ROI auto-placement — the stance corridor derives the
  search region causally, so the ROI can be auto-seeded from where the stance repeatedly sits
  rather than hand-drawn. Ship the manual ROI editor as the fallback; auto-seed is a follow-up UX
  polish, not a detection dependency.

## 11. Implementation plan (handoff)

| Phase | Deliverable | Size | Risk |
|---|---|---|---|
| **V0 — harness ✅ DONE** | `tools/balllab/ball_state_machine.py` + `acceptance.py`: full state machine (accumulation + moment shape-gate + ROI) + §9.1 gates. Acquisition solved corpus-wide incl. saturated 06-11. *The spec executable and parity oracle.* | M | Done |
| **V1 — core (NEXT)** | Port the SETTLED exemplar → `src/Pose/ball_temporal.h` (pure functions + `TemporalBallTracker`) per the **bible's §12 must-preserve list**; unit tests §9.2; numeric parity with V0 on 3 golden swings (`shaft_parity_test` convention). *Do not re-tune in C++.* | M | Low — pure, testable |
| **V2 — detector rework (scoped)** | `BallDetector` swap; timestamp + ROI/corridor plumbing; new signals (§7); throttle test §9.3. **Presence-first**; defer V3. | M | Med — frame path |
| **V4 — Source::Ball** | `ballLaunched → reportCandidate` (arbiter-gated); DetectDot flash + amber-exposure tier | S | Low — pattern exists |
| **V3 — calibration retirement (DEFERRED)** | Delete the dormant v1 core/controller/flow/store + Settings & wizard surfaces *after* v2 is proven live; provenance block v2 (`positionSource:"auto"`, `satFracAtCapture`, **`hittingAreaRoi`**) | M | Low — deletion |
| **V5 — field validation** | Refine in anger (position/latency/health) with the harness as oracle; tee'd-driver corpus; presence-window shortening | S | hardware-gated |

Sequencing note: V0 (DONE) before V1 was deliberate — every algorithmic decision was reached by
measuring the corpus, and the harness is how you know the C++ matches the evidence. **The full
exemplar, and exactly what the C++ port must preserve, is written up in the port bible
[`ball_detection_v2_exemplar_explained.md`](ball_detection_v2_exemplar_explained.md) §12.** Promotion
order is V1 → scoped V2 → V4, deferring V3; keep the python harness as the regression oracle and
never hand-tune the C++ against a single live swing.

The grounded, file-referenced execution plan (real integration points, delete-vs-rework of the v1
footprint, the stance corridor threaded through V0–V2, and the ground-truth labelling route) lives
in [`docs/implementation/ball_detection_v2_impl_plan.md`](../implementation/ball_detection_v2_impl_plan.md).
Ground-truth ball centres for the §9.1 position gate come from the in-app markup tool
(`PpMarkupPanel`): a single per-swing ball marker (the ball is stationary), exported to each
swing's `truth.json` — no separate labelling harness.
