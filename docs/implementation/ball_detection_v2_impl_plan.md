# Ball Detection v2 — Implementation Plan (V0–V5)

**Grounded, file-referenced execution plan for the self-calibrating temporal matched filter.**
Companion to the design [`docs/design/ball_detection_v2.md`](../design/ball_detection_v2.md) (the
*what* and *why*); this is the *how*, tied to the current tree. Handoff table is design §11; this
doc expands each phase with real integration points, the delete-vs-rework map of the v1 footprint,
the stance corridor (design §4.1a) threaded through V0–V2, and the ground-truth route.

Status: **planned (2026-07-07). Not started.** Corpus: `/mnt/swingdata/Mark-Liversedge` — 44 swings,
4 non-empty sessions (06-11, 07-03, 07-04, 07-05_Wrist_02), 3 lighting regimes. `capture.impactUs`
present in every swing.json (independent launch truth). Python env: `/home/markl/venv/pinpoint/bin/python3`
(cv2 4.13, numpy 2.4). Run OpenCV corpus jobs **one at a time** (16 GB box — see the standing rule).

---

## 0. Guiding constraints

- **Promotion strategy (agreed 2026-07-07).** V0 acquisition is solid corpus-wide, so promote it to
  the app to refine in anger — but *properly*: **V1 parity port → scoped V2 (presence-first) → V4
  (arbiter-gated launch), deferring the v1-calibration deletion (V3)**. Keep `ball_state_machine.py`
  as the **regression oracle** after promotion: when a live issue is algorithmic, reproduce and fix
  it in the harness (seconds/iteration) and re-parity — never hand-tune the C++ against one live
  swing. The rough edges (weak-contrast position, launch latency) are safe to mature live because the
  arbiter cannot self-commit on a lone Ball candidate, and the live user-drawn ROI + live pose are
  strictly better than the harness's offline proxies.
- **V0 (the python acceptance harness) is the executable spec and the C++ parity oracle.** No C++ is
  written or tuned before the harness passes the §9.1 gates on the real corpus (**DONE**). Same
  discipline as the shaft-tracker port. Port reference: the bible
  [`../design/ball_detection_v2_exemplar_explained.md`](../design/ball_detection_v2_exemplar_explained.md).
- **The stance corridor (design §4.1a) is an additive robustness layer, not a hard dependency.** The
  detector must still lock via the static ROI band when pose is unavailable. It is used only in
  SEARCH/CANDIDATE; once LOCKED the tracker monitors the frozen spot.
- **Ball detection is signal-only** to the buffer (`ballPresentChanged` never pauses/resumes/replays;
  CLAUDE.md capture-intent contract). `ballLaunched` feeds the shot arbiter as a *candidate* only
  (conf 0.6 < the 0.8 single-modality floor — it cannot self-commit).
- **Throttle contract**: exactly one `ballDetected`/`detectionSkipped` per `detect()` on every path,
  `consumerCount = 2` (pose + ball), both `clearBusy`/`clearRawBusy` wired
  (`camera_instance.cpp:347-348, 386-393`).

---

## 1. Prerequisite — ground-truth ball centres (via the in-app markup tool) — DONE (2026-07-07)

The §9.1 position gate ("locked position within 3 px of the human-verified ball centre") needs
labelled ball centres. **Built + labelled:** a single-point, per-swing ball marker was added to the
in-app markup tool (`PpMarkupPanel` + `src/Gui/markup/`), writing each swing's `truth.json`
`"ball":[px,py]` (commit 5b64a92). The ball is stationary, so it is a per-swing constant (one click),
not a per-frame track like the shaft — this doubles as a product feature (ground truth for
SwingLab/re-analysis) and removes any throwaway labelling harness. The harness (`tools/balllab/`)
reads `truth.json` ball centres where present.

**All 44 corpus swings are now labelled** (verified sane: nx ∈ [0.51, 0.58], ny ∈ [0.93, 0.99] —
every ball low and roughly centred, no outliers, consistent across all four lighting regimes
including the saturated 06-11 "dark"). This measured distribution is itself corroboration for the
stance-corridor prior (§4.1a): the ball sits centred and low, between/below the stance.

**Stream-selection gotcha for V0:** these Wrist captures carry TWO video streams (Down-the-Line +
Face-On). Ball detection is Face-On only — any swing.json-driven stream/geometry selection MUST pick
the Face-On stream by `setup.perspective == 2` (else alias "Face"), NOT the first video stream, or it
normalizes against the wrong resolution (DTL is 512–576 px wide vs Face-On 720–1280). `balllab` opens
`Face-On.mp4` directly so it is unaffected, but the C++ detector and any harness reading swing.json
geometry must select correctly (mirror `markup_truth::readFaceOn`).

**Launch truth** (`capture.impactUs`) already exists — no labelling needed for the launch-edge gate.

---

## 2. Phases

### V0 — Executable spec & acceptance harness — DONE (2026-07-07) · python, `tools/balllab/`
The keystone, and now the **parity oracle**. Built `ball_state_machine.py` (the causal state machine)
+ `acceptance.py` (§9.1 gates, `--root`/`--no-roi` parameterized; runs here and on the studio PC).
The algorithm settled through three iterations the harness caught *before* any C++:

- **Static-scene accumulation** (fast-EMA `A`; SEARCH on accumulated novelty `N_acc = (A−B)/σ`;
  monitoring per-frame) — recovered weak-contrast 07-03 from **0/10 → 10/10** acquired.
  **Acquisition-only**, so the live present/absent + 2-frame launch stay real-time (no trade-off).
- **2nd-moment blob shape-gate** on `D = A − B` (amplitude-invariant) — passes a weak round ball,
  rejects the line/shaft/normalization-spike; replaced an amplitude-sensitive ring test.
- **Padded-DoG crop** (no GaussianBlur crop-edge artifact) + **hitting-area ROI** (per-session ball
  cluster proxy — the live ROI is the user's `setRoi`) — fixed the ~120 px bare-mat mislocks.
- **Full corpus (accumulation + ROI):** acquisition works across ALL lighting regimes incl. the
  fully-saturated 06-11 (5–6 px); G1 15/15 good-exposure; G3 (no false launches) essentially clean.
- **Reference for the port:** the algorithm is documented in
  [`../design/ball_detection_v2_exemplar_explained.md`](../design/ball_detection_v2_exemplar_explained.md)
  — the "bible" the C++ port follows.
- **Open refinements (do NOT enshrine in the port as correct — §11 bible):** position ~9 px on weak
  contrast (adjacent grounded clubhead pulls the accumulated centroid); launch +3–4 frame
  ball-departure latency (→ `kBallLaunchLatencyUs`) + collapse completeness; `satFrac`-over-ROI
  over-flags → add a ball-vs-mat contrast/SNR health signal; swing.json should record the ROI.

### V1 — C++ core  ·  `src/Pose/ball_temporal.h` + tests  ·  risk: low (pure, testable)  ·  **NEXT**
- Header-only, OpenCV-only, **no-Qt** (mirrors `ball_model.h`; test is a `NO_QT` target). Port the
  **settled** exemplar per the bible's §12 "must-preserve" list: DoG on a **padded** crop; robust-MAD
  noise; the **fast-EMA accumulator `A`** + accumulated novelty `N_acc`; the 2nd-moment `is_blob`
  shape gate on `D`; the K=3 candidate state machine (±2 px matching, top-K-by-hold); the per-frame
  launch cliff. Pure functions + a `TemporalBallTracker` struct; the tracker consumes a precomputed
  `R` (caller owns ROI/padding). Takes an optional ROI / stance bounds as a SEARCH input. Move
  `kBallDiameterMm` (currently `ball_model.h:56`) here. **The algorithm is fixed — do not re-tune it
  in C++; iterate in the python harness and re-parity (bible §12).**
- Unit tests (design §9.2) → `src/Pose/tests/ball_temporal_test.cpp`, new
  `pp_add_test(ball_temporal_test … NO_QT)` mirroring `ball_model_test` (`src/Pose/tests/CMakeLists.txt:24-28`):
  synthetic appear/persist/vanish over flat/noisy/gradient/moving-shadow, saturation ramp, baseline
  line-absorption, nudge-vs-launch, scale-space edge rejection, sub-pixel jitter bound, corridor gate.
- **Numeric parity** with V0 on 3 golden swings, modelled on `src/Analysis/tests/shaft_parity_test.cpp`:
  env-var fixture (`PP_BALL_PARITY_*`), SKIP (exit 0) when absent, tolerance gate on locked position /
  launch frame / at-spot response. Regenerate the python reference **on the dev box, same host**
  (cross-host float differs — shaft-port lesson).
- **Gate**: parity `|Δpos| < ~0.5 px`, launch-frame agreement, unit tests green.

### V2 — Detector rework (scoped: presence-first) · `src/Pose/ball_detector.{h,cpp}` + plumbing · risk: med (frame path)
**Promotion scope (agreed):** ship v2 acquisition as the live **presence** feature first, and wire
the launch as an **arbiter-gated** candidate (V4) — the two rough edges (weak-contrast position,
launch latency/completeness) are *safe* to mature live because the arbiter cannot self-commit on a
lone Ball candidate. **Defer the v1-calibration deletion (V3)** — leave it dormant so this step
changes less and can roll back. Keep `ball_state_machine.py` as the regression oracle after promotion.
- Swap the `ball_model.h` include for `ball_temporal.h`; `BallDetector` holds one
  `TemporalBallTracker`. `detect()` feeds ROI + timestamp (+ latest stance bounds) to the tracker,
  maps its output to `BallDetection` (`score` = novelty `N`). Retire `setProfile`/`clearProfile`/
  `beginCalibCapture`/`cancelCalibCapture`/`environmentDrift`/`calibFrame`/`calibCaptureDone`
  (`ball_detector.h:87-98, 109-113`). Add `ballLocked(float x, float y, float radiusNorm)`,
  `ballLaunched(qint64 estImpactUs, float x, float y)`, `exposureWarning(double satFrac)`.
- **Timestamp plumbing** (design §4.5, one of two changes outside `src/Pose/`): add `qint64
  timestampUs` to `VideoPreprocessorOpenCV::framePreprocessed` (`video_preprocessor_opencv.h:54`;
  emit sites `.cpp:95` preprocessed, `.cpp:157` raw) and the raw path. **Source it by stamping
  `EventBuffer::nowMicros()` at `FrameThrottle::offer()`/`offerRaw()` (`camera_instance.cpp:606/641`)
  and carrying it through the throttle** — closest point to capture that already owns the clock (same
  clock as the ring publish at `camera_instance.cpp:1760`); avoids a second, later clock read on the
  preprocessor thread. `RawVideoFrame` (`raw_video_frame.h:27-41`) has no timestamp field today.
- **Stance-corridor input** (design §4.1a, the other outside-`src/Pose/` change): a queued setter
  `setStanceBounds(QPointF leftAnkleN, QPointF rightAnkleN, float conf)` modelled on `setRoi`
  (`ball_detector.h:83`, pushed from `camera_instance.cpp:397-400`). Feed it from the pose estimator's
  latest ankle keypoints (COCO 15/16) already computed on the same instance, with a staleness/conf
  guard and static-band fallback.
- Update `ball_detector_contract_test.cpp` for the new signal paths (throttle contract §9.3, LOCKED
  fast path included). `consumerCount = 2` + `clearBusy`/`clearRawBusy` wiring unchanged.
- **Presence consumers**: the new presence / `ballLocked` + `exposureWarning` signals drive BOTH the
  toolbar ball DetectDot (V4) AND the **wizard live "ball detected / not detected" badge** (the
  repurposed calibration step, design §7 / V3). Expose a simple live `ballDetectedOk` flag on the
  face-on `CameraInstance` for the wizard and DetectDot to bind — this is the wizard's ROI/lighting
  setup loop, so wire it in V2 even though the wizard QML lands with V3's repurpose.
- **Gate**: contract test green; live app builds; presence shows on a corpus replay.

### V3 — Calibration retirement (DEFERRED until v2 is proven live)  ·  deletion  ·  risk: low
Do this *after* V2/V4 ship and presence is proven in anger — leaving the dormant v1 stack in place
until then keeps the promotion small and reversible. Delete (v1 core + UI, fully mapped):
- `src/Pose/ball_model.h`, `ball_calibration_logic.h`, `ball_calibration_store.h`.
- `src/Gui/calibration/ball_calibration_controller.{h,cpp}`, `BallCalibrationFlow.qml`.
- Tests `src/Pose/tests/ball_model_test.cpp` + `ball_calibration_test.cpp` and their CMake entries
  (`src/Pose/tests/CMakeLists.txt:24-37`).
- `CamerasPanel.qml:1796-1835` (keep the ROI editor `:1755-1780`; add an exposure-health line).
- **Repurpose (do NOT delete) the wizard ball step** (`ScreenSessionWizard.qml:1136-1232`): remove
  the `BallCalibrationFlow`/controller wiring (the place/remove/validate protocol) and replace with a
  live **"ball detected / not detected"** badge over the editable ROI + exposure/contrast health
  (design §7) — so the user dials in the ROI and lighting before the session. The `SummaryRow`
  (`:1907-1916`) becomes "Ball: detected / not detected"; `ballCalDone`/`ballFaceOnInst.ballCalibrated`
  (`:102-111`) → a live `ballDetectedOk` flag from the detector's presence.
- `CameraManager::ballCalibrationFor` (`camera_manager.cpp:669-689`) + the `loadProfile`/profile
  restore path (`camera_manager.cpp:800-829`).
- `CameraInstance` `applyBallCalProfile`/`clearBallCalProfile`/`ballCalibrated`/drift
  (`.cpp:844-898`); keep presence smoothing (`onBallDetected`, `.cpp:902-957`) and derive the
  `ballLocked` position from the new signal.

**Provenance v2** — `swing_exporter.cpp:416-434` + `CamRecord` (`swing_exporter.h:49-62`) + the
population at `shot_processor.cpp:716-724`: set `positionSource:"auto"`, add `satFracAtCapture`, keep
`center`/`radiusNorm`; drop `calibrated`/`margin`/`driftAtCapture`. (The ball-centre *ground truth*
lives separately in `truth.json` via the markup tool — §1.)

### V4 — `Source::Ball` trigger  ·  `main.cpp` + `PpDetectCluster.qml`  ·  risk: low (pattern exists)
- Mirror the IMU wiring (`main.cpp:342-347`) at the reserved comment (`main.cpp:407-411`):
  `connect(ballLaunched → shotController.reportCandidate(Source::Ball, estImpactUs, conf))` gated on
  `appSettings.autoDetectSwing()`. `Source::Ball` + its 1250 ms post-roll already exist
  (`shot_controller.h:48`, `shot_processor.cpp:63`).
- `PpDetectCluster.qml`: replace the calibration/drift `baseColor` logic (`:113-115`) with an
  exposure-warning amber tier; wire the ball dot's `triggerFlash()` on `ballLaunched` (mirror the
  IMU/acoustic `Connections`, `:139-146`) — it has none today. `exposureWarning` → amber hint.

### V5 — Field validation  ·  hardware-gated  ·  risk: low
- Live studio session per design §9.4: presence dot through a normal session, launch flash on real
  shots, arbiter log shows Ball candidates *fusing* with IMU/acoustic (not committing alone).
- Grow the corpus with a **tee'd-driver Swing session** (corpus is Wrist-only; tee'd geometry
  unvalidated). Decide the presence-window shortening (50 → ~12 frames, `camera_instance.h:354`).

---

## 3. v1 footprint — delete vs rework (quick reference)

| Action | Files |
|---|---|
| **Delete** | `ball_model.h`, `ball_calibration_logic.h`, `ball_calibration_store.h`; `ball_calibration_controller.{h,cpp}`, `BallCalibrationFlow.qml`; `ball_model_test.cpp`, `ball_calibration_test.cpp` (+ CMake); calibration rows in `CamerasPanel.qml:1796-1835`; wizard BallCal step |
| **Rework** | `ball_detector.{h,cpp}` (bridge to `ball_temporal.h`); `camera_instance.{h,cpp}` (presence, threading, corridor input, provenance); `camera_manager.cpp` (drop store/restore); `swing_exporter` provenance + `shot_processor.cpp:716-724`; `main.cpp` funnel (add `Source::Ball`); `PpDetectCluster.qml` ball dot; `ball_detector_contract_test.cpp` |
| **New** | `src/Pose/ball_temporal.h` + `ball_temporal_test.cpp`; ball marker in `PpMarkupPanel`/`markup_truth`; `tools/balllab/ball_state_machine.py` + `acceptance.py` |

---

## 4. Decisions defaulted (flag to change)

- `ball_temporal.h` stays **Qt-free** → `NO_QT` test, like `ball_model.h`.
- Frame timestamp sourced at **`offer()`**, not re-read in the preprocessor (closest to capture,
  reuses the existing clock).
- Stance corridor is **SEARCH-phase only** and non-load-bearing (static-band fallback).
- Ground truth via the **in-app markup tool → `truth.json`**, not a throwaway harness.
- Tee'd/driver ball is **out of scope for v2.0 sign-off** (deferred to V5 corpus growth).
- **V0 strictly before V1.**

---

## 5. Build & test

```bash
# V0/acceptance (python, one job at a time):
/home/markl/venv/pinpoint/bin/python3 tools/balllab/acceptance.py

# Pose core + detector tests (standalone CTest, NOT the app build):
cmake -S src/Pose/tests -B build/pose-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
cmake --build build/pose-tests -j4 && ctest --test-dir build/pose-tests --output-on-failure

# Parity (needs a same-host python reference; SKIPs when the fixture is absent):
PP_BALL_PARITY_REF=... ctest --test-dir build/pose-tests -R ball_temporal_parity --output-on-failure

# App build (V2+): build/Desktop_Qt_6_11_0-Debug, --parallel 4 (OOM cap).
```

Related: design `docs/design/ball_detection_v2.md`; markup `docs/reference/truth_json_schema.md`,
`docs/implementation/shaft_markup_exemplar_impl.md`; shot arbiter `docs/design/shotdetection.md`.
