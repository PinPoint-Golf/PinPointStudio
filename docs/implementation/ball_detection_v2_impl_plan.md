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

- **V0 (the python acceptance harness) is the executable spec and the C++ parity oracle.** No C++ is
  written or tuned before the harness passes the §9.1 gates on the real corpus. Same discipline as
  the shaft-tracker port.
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

## 1. Prerequisite — ground-truth ball centres (via the in-app markup tool)

The §9.1 position gate ("locked position within 3 px of the human-verified ball centre") needs
labelled ball centres; the corpus has none today (`ballDetection` blocks are empty `{}`). **Route:
add a single-point, per-swing ball marker to the existing in-app markup tool** (`PpMarkupPanel` +
`src/Gui/markup/`), written into each swing's `truth.json`. The ball is stationary, so it is a
per-swing constant (one click), not a per-frame track like the shaft. This doubles as a product
feature (ground truth for SwingLab/re-analysis) and removes the need for a throwaway labelling
harness. The harness (`tools/balllab/`) reads `truth.json` ball centres where present.

**Launch truth** (`capture.impactUs`) already exists — no labelling needed for the launch-edge gate.

---

## 2. Phases

### V0 — Executable spec & acceptance harness  ·  python, `tools/balllab/`  ·  risk: low
The keystone. Turn the two evidence scripts into the state machine + acceptance gates.

- Reuse `dog()`/`band()` from `corpus_separation.py`. New `ball_state_machine.py`: the full
  SEARCH → CANDIDATE → LOCKED → VANISHED machine (design §4), in scene-noise σ units (robust MAD),
  response-baseline EMA (change-gated, hole under lock), at-spot collapse edge, sub-pixel quadratic
  centre, scale parabola.
- New `acceptance.py`: run every corpus swing at native ~149 fps and enforce the §9.1 gates —
  lock before impact−1 s in ≥95 % of `satFrac ≤ 0.25` swings; locked position within 3 px of the
  `truth.json` ball centre; **zero** address-phase launches corpus-wide; launch edge within ≤3
  frames of `capture.impactUs`; the 06-11 session runs with `exposureWarning` asserted and no false
  launches.
- **Stance corridor validation** (design §4.1a): confirm the between-feet × below-ankle box contains
  the labelled ball on every swing, and measure the win (search-area shrink + spurious-peak
  reduction vs the static band). Corpus is Wrist sessions; derive ankles from stored pose if present
  in swing.json, else from a couple of labelled ankle points per swing (cheap geometric check —
  live pose is not required offline).
- **Gate**: all §9.1 criteria pass (or a documented, understood miss like 07-03/swing_0002). This
  file becomes the parity reference for V1.

### V1 — C++ core  ·  `src/Pose/ball_temporal.h` + tests  ·  risk: low (pure, testable)
- Header-only, OpenCV-only, **no-Qt** (mirrors `ball_model.h`; test is a `NO_QT` target). Pure
  functions (DoG response, robust noise, baseline EMA, novelty, NMS peak-find, sub-pixel fit, scale
  parabola) + a `TemporalBallTracker` struct owning the state machine. `TemporalBallTracker` takes
  **optional stance bounds** as a SEARCH input; with none supplied it uses the static band, keeping
  the core pose-free and testable. Move `kBallDiameterMm` (currently `ball_model.h:56`) here.
- Unit tests (design §9.2) → `src/Pose/tests/ball_temporal_test.cpp`, new
  `pp_add_test(ball_temporal_test … NO_QT)` mirroring `ball_model_test` (`src/Pose/tests/CMakeLists.txt:24-28`):
  synthetic appear/persist/vanish over flat/noisy/gradient/moving-shadow, saturation ramp, baseline
  line-absorption, nudge-vs-launch, scale-space edge rejection, sub-pixel jitter bound, corridor gate.
- **Numeric parity** with V0 on 3 golden swings, modelled on `src/Analysis/tests/shaft_parity_test.cpp`:
  env-var fixture (`PP_BALL_PARITY_*`), SKIP (exit 0) when absent, tolerance gate on locked position /
  launch frame / at-spot response. Regenerate the python reference **on the dev box, same host**
  (cross-host float differs — shaft-port lesson).
- **Gate**: parity `|Δpos| < ~0.5 px`, launch-frame agreement, unit tests green.

### V2 — Detector rework  ·  `src/Pose/ball_detector.{h,cpp}` + timestamp/corridor plumbing  ·  risk: med (frame path)
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
- **Gate**: contract test green; live app builds; presence shows on a corpus replay.

### V3 — Calibration retirement  ·  deletion  ·  risk: low
Delete (v1 core + UI, fully mapped):
- `src/Pose/ball_model.h`, `ball_calibration_logic.h`, `ball_calibration_store.h`.
- `src/Gui/calibration/ball_calibration_controller.{h,cpp}`, `BallCalibrationFlow.qml`.
- Tests `src/Pose/tests/ball_model_test.cpp` + `ball_calibration_test.cpp` and their CMake entries
  (`src/Pose/tests/CMakeLists.txt:24-37`).
- `CamerasPanel.qml:1796-1835` (keep the ROI editor `:1755-1780`; add an exposure-health line).
- Wizard BallCal step + its `SummaryRow` (`ScreenSessionWizard.qml:1136-1232, 1907-1916`) and the
  `ballCalDone`/`ballFaceOnInst.ballCalibrated` state (`:102-111`).
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
