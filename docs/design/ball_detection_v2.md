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

Three pillars, each earning its keep against a measured failure mode:

1. **Scale-matched band-pass (spatial)** — DoG response `G(σ₁) − G(σ₂)` with σ₁ = r/1.6,
   σ₂ = 3.2·σ₁. Insensitive to absolute luma and to any structure much larger than the ball
   (shadows, light pools, gain drift). This is the only per-pixel image operation in the design.
2. **Novelty against a static-scene baseline (what v1's background model becomes)** — a slowly
   adapted per-pixel baseline *of the response map itself*, not of the image. The painted line,
   pool edge and mat speckles live in the baseline and cancel; a newly placed ball is novel.
   No capture protocol: the baseline is learned from the live stream and freezes under the locked
   ball's footprint.
3. **Stability lock + at-spot monitoring (temporal)** — a novel candidate must hold position
   within ±2 px with sustained response before it *locks*. Once locked, presence is a cheap
   monitor of one 5×5 neighbourhood of the response map; the launch event is the collapse edge.
   Waggles, hands, feet and the club can't lock (they move); distractors can't lock (not novel).

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

- **SEARCH** (per frame): find local maxima of `N` above `k_appear = 5` with ball-scale support
  (non-max suppression radius 2r̂). Track up to K=3 peaks frame-to-frame.
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

| Distractor | Killed by |
|---|---|
| Painted line / pool edge / mat speckle | in baseline `B` → novelty ≈ 0 |
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
  scale responses. Reject a lock whose scale response is monotone at the edges of a widened scan
  (that's an edge/corner, not a disc).
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

### 4.6 Optional verification cue (lock time only)

NCC of the locked patch against the frozen self-template each ~1 s. Cheap; catches the
"static bright spot locked during a lighting event" residual. Never gates the launch edge
(the edge is time-critical); only demotes a stale lock back to SEARCH. Colour is deliberately
NOT a cue anywhere — range balls are yellow/orange, and the 06-11 regime proves luma-chroma
cues can vanish entirely.

## 5. What the corpus prototype maps to

| Prototype (offline, python) | Product (live, C++) |
|---|---|
| `medP − |medA|` self-location | not needed — placement novelty + stability lock does it causally |
| present/absent frame sets around impactUs | LOCKED at-spot monitor before/after the collapse edge |
| 43/44 clean separation | presence + launch reliability claim, per §9 gates |
| band prior y ∈ [890, h] | the existing user hitting-area ROI (`setRoi`, unchanged) |

The offline form remains valuable as-is: `swing_reanalyzer`/SwingLab can locate the ball and
launch frame at 149 fps precision in recorded windows (low-point metric, export overlays) using
the identical response math — same header, two entry points (live incremental, windowed batch).

## 6. Exposure QA — the required companion (not optional)

The 06-11 regime (mat fully clipped, per-frame response intermittently 0) is **unfixable in
software** — information is destroyed at the sensor. v2 therefore ships with a capture-side guard:

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
  `ball_calibration_store.h`, `BallCalibrationController`, `BallCalibrationFlow.qml`, the wizard
  confirmation row and the Settings calibration section are **retired** (the Settings section
  shrinks to the ROI editor + exposure health line). Keep `kBallDiameterMm` (move to the new
  header). The `setProfile`/`clearProfile`/`beginCalibCapture` slots and `calibFrame`/
  `calibCaptureDone`/`environmentDrift` signals go with them (`environmentDrift` →
  `exposureWarning`).
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
  clock. This is the one change outside `src/Pose/`.
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
- **Open**: should `ballLocked` position feed the hitting-area ROI auto-placement (draw the ROI
  around where the ball repeatedly appears)? Deferred — UX question, not detection.

## 11. Implementation plan (handoff)

| Phase | Deliverable | Size | Risk |
|---|---|---|---|
| **V0 — harness first** | Extend `tools/balllab/` with the full state-machine replay + acceptance gates of §9.1, runnable on the existing corpus. *This is the spec executable.* | M | Low |
| **V1 — core** | `src/Pose/ball_temporal.h` (response, baseline, state machine, scale/sub-pixel, launch edge — pure functions + a small `TemporalBallTracker` struct); unit tests §9.2. Numeric parity with V0 on 3 golden swings (project parity-test convention) | M | Low — pure, testable |
| **V2 — detector rework** | `BallDetector` swap to the new core; timestamp plumbing through `framePreprocessed`; new/retired signals (§7); throttle test §9.3 | M | Med — touches the frame path |
| **V3 — calibration retirement** | Delete v1 core/controller/flow/store + Settings & wizard surfaces; Settings keeps ROI editor + exposure health; provenance block v2 (`positionSource:"auto"`, `satFracAtCapture`) | M | Low — deletion |
| **V4 — Source::Ball** | main.cpp wiring to `reportCandidate`, DetectDot flash + amber-exposure tier, `autoDetectSwing` gating | S | Low — pattern exists (IMU/acoustic) |
| **V5 — field validation** | Studio session per §9.4; corpus grows a tee'd-driver session; presence-window shortening decision | S | hardware-gated |

Sequencing note for the implementer: V0 before V1 is deliberate — every algorithmic decision in
§4 was reached by measuring this corpus, and the acceptance harness is how you know your C++
matches the evidence. Do not skip to C++ and tune live.
