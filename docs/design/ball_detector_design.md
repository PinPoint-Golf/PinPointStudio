# Ball Detector v2 — Implementation Plan

**A state-of-the-art, high-performance camera-only golf-ball detector for PinPoint Studio.**

Implements the **vision modality** of [`shotdetection.md`](shotdetection.md) (which frames shot
detection as a multi-modal problem — acoustic + inertial + vision fusion). Here: replace the
classical Hough/blob `BallDetector` with a **CNN (ONNX YOLO) + Kalman tracking-by-detection** loop
running on **small patches**, keep the cheap classical detector for stationary acquisition, and add
an **offline trajectory-refinement** pass over the frozen `SwingWindow`. No IR.

Status: **plan / not started.** This doc is the build spec — file paths, signatures, throttle/threading
contracts, CMake recipe, phasing, and tests are all grounded in the current code (file:line below).

---

## 0. Goals & non-goals

**Goals**
- **High performance:** patch-based inference (tiny model input regardless of frame size), GPU
  execution provider (CUDA here), Kalman prediction so we never blind-search the full frame.
- **State of the art accuracy:** single-class nano-YOLO in a tracking-by-detection loop (live);
  heavier model + multi-frame/motion cues for impact-window recall (offline).
- **Drop-in:** preserve the `BallDetection{found,x,y,radius,detectMs}` contract, the `FrameThrottle`
  protocol, the QML overlay bindings, and the `ballPresent` hysteresis — zero churn downstream.
- **De-risked rollout:** ship the hard infrastructure (Kalman, patch loop, state machine) *before*
  the ML model exists, by using the existing Hough/blob as the in-patch detector first.

**Non-goals (separate efforts)**
- **Club / clubhead detection** — open R&D, tracked in [`shotdetection.md`](shotdetection.md) §2.3/§8. Not here.
- **Ball-based shot triggering** — kept signal-only for now (see §8); the detector will *expose* the
  hooks but won't arm shots in v2.
- **Launch-monitor-grade ball-flight physics** (spin, true 3D launch) — out of scope; we produce a
  2D image-plane trajectory + basic launch descriptors.

---

## 1. Current state (what we're replacing)

| Concern | Today | Ref |
|---|---|---|
| Detector | OpenCV `HOUGH_GRADIENT_ALT` on a white-HSV mask + `SimpleBlobDetector` fallback | `src/Pose/ball_detector.cpp:39-144` |
| Threading | own `QThread m_ballThread`, slot-driven, created in `setupPipeline()` for buffer-backed instances only | `src/Gui/camera_instance.cpp:347-368` |
| Frame in | `VideoPreprocessorOpenCV::framePreprocessed(cv::Mat)` — **BGR `CV_8UC3`, full frame, owns its buffer** | `src/Video/video_preprocessor_opencv.h:54`, `.cpp:63-78` |
| Throttle | shared `FrameThrottle`, `consumerCount=2` (pose + ball), `skipFactor=2`; ball must emit exactly one `clearBusy`-bound signal per frame | `src/Gui/camera_instance.cpp:331-358`, `src/Video/frame_throttle.cpp:58-75` |
| Output | `ballDetected(BallDetection)` → `onBallDetected` → `ballX/Y/Radius/ballDetected` + `ballPresent` (50-frame window, 30% threshold, "ting") | `ball_detector.h:28-35`, `camera_instance.cpp:782-836` |
| ROI | runtime-only `m_roi` (hitting area), user drag-select in `PpCameraFrame.qml`, **not persisted** (distinct from the persisted crop ROI `m_cropRoi`) | `camera_instance.cpp:363-366,695-724` |
| Shot | **signal-only** — `ballPresentChanged` has no shot consumer; `ShotController::Source::Ball` exists but is never emitted | `shot_controller.h:43,66`, `camera_manager.cpp:655` |

The Hough detector is fine for a **stationary teed ball** but collapses on the moving/blurred ball at
and after impact, and white-on-white backgrounds. That is exactly the regime CNN+Kalman fixes.

---

## 2. Architecture overview

Two cooperating detectors, one shared `BallDetection` contract:

```
LIVE (per frame, on m_ballThread, real-time)
┌──────────────────────────────────────────────────────────────────────────┐
│  BallDetectorCnn  (drop-in replacement for BallDetector)                   │
│                                                                            │
│   state machine:  SEARCHING ──acquire──► TRACKING ──miss×N──► COASTING     │
│                       ▲                                          │          │
│                       └──────────────miss×K─────────────────────┘          │
│                                                                            │
│   SEARCHING : cheap acquisition over the ROI (Hough/HSV, or coarse CNN)    │
│   TRACKING  : Kalman predict → crop ~320² native patch → CNN refine →      │
│               Kalman update → map patch coords back to full-frame norm*    │
│   COASTING  : Kalman predicts through a miss, search window grows           │
│                                                                            │
│   emits ballDetected(BallDetection{found,x,y,radius,detectMs})  (unchanged)│
└──────────────────────────────────────────────────────────────────────────┘
            │ same signals/slots, same throttle contract as BallDetector
            ▼
   CameraInstance::onBallDetected  →  QML overlay + ballPresent (UNCHANGED)

OFFLINE (after shot, QtConcurrent worker over the frozen SwingWindow)
┌──────────────────────────────────────────────────────────────────────────┐
│  BallTrajectoryWorker  (3rd ShotProcessor worker, alongside Analyzer/Export│
│   walk face-on (then DTL) frames → decode (shared demosaic) → heavier CNN  │
│   + multi-frame/motion cues → forward-backward Kalman (RTS) smoothing →    │
│   write additive "ballTrajectory" block to swing.json                      │
└──────────────────────────────────────────────────────────────────────────┘
```

\* "full-frame normalized" = the existing convention: `x,y` are the ball centre in `[0,1]` over the
**whole** frame; `radius` is normalized to **frame width**. Preserve it exactly.

---

## 3. Live detector: `BallDetectorCnn`

New files: `src/Pose/ball_detector_cnn.h` / `.cpp` (sit beside the pose estimators — they share the
ONNX + OpenCV pattern). Guarded `#if defined(HAVE_OPENCV) && defined(HAVE_ONNXRUNTIME)` (the YOLO
model is *optional* — see §3.4, the loop runs on the classical in-patch detector without it).

### 3.1 Public surface — a true drop-in

Mirror `BallDetector` exactly so `camera_instance.cpp` wiring changes by one type name:

```cpp
class BallDetectorCnn : public QObject {
    Q_OBJECT
public:
    explicit BallDetectorCnn(QObject *parent = nullptr);
    ~BallDetectorCnn() override;

    void setEnabled(bool on) { m_enabled.store(on, std::memory_order_relaxed); }   // atomic, any thread
    bool isEnabled() const   { return m_enabled.load(std::memory_order_relaxed); }

public slots:
    void detect(const cv::Mat &frame);     // BGR CV_8UC3, full frame
    void setRoi(QRectF roi);
    void setParams(double conf, int /*reserved*/);
    void load();                           // init ORT on this thread (queued-invoked after moveToThread)

signals:
    void ballDetected(const BallDetection &result);   // unchanged struct
    void detectionSkipped();                          // disabled path → clearBusy, no ball-lost
    void ballBackendReady(const QString &label);      // "", "CUDA", "CoreML" (parity w/ pose)
    void ballStatsUpdated(double avgMs, double fps);   // optional, parity w/ pose
    // FUTURE (dormant in v2): launch event for ball-based shot arming (see §8)
    void ballLaunched(qint64 timestampUs);
};
```

**Throttle contract (non-negotiable):** for every `detect()` call, emit **exactly one** of
`ballDetected` / `detectionSkipped` on **every** code path (found / not-found / disabled / empty-ROI /
exception). Both connect to `FrameThrottle::clearBusy` (`camera_instance.cpp:356,358`); the
`consumerCount=2` throttle deadlocks if zero fire and runs ahead if two fire. Use `detectionSkipped`
for the disabled path so a spurious `found=false` doesn't trigger a ball-lost transition
(`ball_detector.h:57-59`).

### 3.2 ONNX session — mirror MoveNet

Copy the `OrtState` PIMPL + `load()` + EP-cascade verbatim from `pose_estimator_movenet.cpp:39-198`:
- `OrtState` holds `Ort::Env/SessionOptions/RunOptions/Allocator/Session`, input/output names — keeps
  `onnxruntime_cxx_api.h` out of the header.
- EP cascade: CoreML → CUDA (`/proc/driver/nvidia/version` probe on Linux) → CPU. DirectML branch is
  dead (cmake never defines `WITH_DIRECTML`) — omit it.
- `modelPath()` resolves `applicationDirPath()/models/<file>` (`../Resources/models/` on macOS),
  mirroring `pose_estimator_movenet.cpp:61-78`, using `QStringLiteral(YOLO_BALL_MODEL_FILE)`.
- `setIntraOpNumThreads(1)`, `ORT_ENABLE_ALL` — same as pose.
- **Export the model FP16** (`half=True`) — cheap, portable speed win on the CUDA EP. The engine is
  ORT throughout; **TensorRT is deliberately not used** — see §3.7 for the decision and rationale.
- Emit `ballBackendReady(epLabel)` at the end of `load()`.

### 3.3 The tracking-by-detection loop (the core)

```cpp
enum class TrackState { Searching, Tracking, Coasting };

// cv::KalmanFilter, constant-velocity (4 state: x,y,vx,vy; 2 meas: x,y) in PIXEL space.
// Bump process noise on a velocity spike (impact) to track the launch without lag.
cv::KalmanFilter m_kf{4, 2, 0, CV_32F};
TrackState m_track = TrackState::Searching;
int m_misses = 0;
```

Per `detect(frame)` (after enabled/empty/ROI guards that emit the right throttle signal):

1. **SEARCHING** — acquire. Run the **acquisition detector** (§3.4) over the ROI (downscaled or, if
   small, native). On a hit: seed the Kalman state, `m_track = Tracking`, emit the detection.
   On a miss: emit `ballDetected{found=false,...}`.
2. **TRACKING** — `predict()` → crop a fixed **~320×320 native-resolution patch** centred on the
   prediction, clamped to frame bounds → run the **patch detector** (§3.4) on the patch →
   - hit: `correct()` with the measurement, `m_misses=0`, map patch-local px → full-frame
     normalized (`x=(x0+px)/fw`, `radius=r_px/fw`), emit the detection.
   - miss: `m_track = Coasting`, `m_misses=1`, emit using the *predicted* position with a confidence
     flag (or `found=false` — see §3.5).
3. **COASTING** — keep `predict()`-ing; grow the patch each miss to widen the search; on a hit return
   to TRACKING; after `kMaxCoast` (≈5) misses → `m_track = Searching`, emit `found=false`.

**Coordinate mapping** (single source of truth, unit-tested in §9):
`full_norm_x = (patch_x0 + det_px_x) / frame.cols`, `radius_norm = det_px_r / frame.cols` — identical
convention to the current detector (`ball_detector.cpp:102-104`).

### 3.4 Pluggable in-patch detector — the de-risking seam

Define a tiny strategy interface so the **loop is testable and shippable without a model**:

```cpp
struct PatchDetection { bool found; cv::Point2f centerPx; float radiusPx; float conf; };
struct IPatchDetector { virtual PatchDetection run(const cv::Mat &patchBgr) = 0; };
```

- **`ClassicalPatchDetector`** — the existing Hough/HSV/blob logic (`ball_detector.cpp:65-143`)
  refactored to operate on a patch. Ships in **Phase 1** with no ML dependency.
- **`YoloPatchDetector`** — ONNX nano-YOLO (§3.2 + §4). Ships in **Phase 2**.

Acquisition (SEARCHING) uses `ClassicalPatchDetector` over the ROI even in Phase 2 (cheap, great on
the static teed ball — exactly the report's "keep Hough for teed/stationary"). The CNN runs in the
patch during TRACKING/COASTING where it earns its cost.

### 3.5 `ballPresent` — leave it, then enrich

Keep the 50-frame / 30% hysteresis in `onBallDetected` (`camera_instance.cpp:782-836`) untouched in
Phase 1 — the `found` boolean drives it as before. In a later phase, optionally derive presence from
**track confidence** (Tracking/Coasting = present) for snappier, less flickery behaviour; that is a
one-line change localized to `onBallDetected` and explicitly out of the drop-in scope.

### 3.6 Performance notes

- **Patch keeps inference O(patch²), not O(frame²).** A 320² patch on the CUDA EP is sub-millisecond
  for a nano model — the report's "tiny-fast" regime (~hundreds of fps).
- **`skipFactor=2`** (`camera_instance.cpp:331`) currently halves *both* pose and ball cadence. The
  golf ball at launch crosses the frame in 1–3 frames at 30–60 fps, so **live flight tracking is
  inherently limited** — set expectations: live = robust stationary detection + smoothed overlay +
  launch *event*; full trajectory is the offline job (§5), ideally on high-fps capture.
  *Optimization (later):* give the ball its own throttle/cadence independent of pose so it can run
  every frame during a shot. Noted, not in v2.
- One ORT session, reused; no per-frame allocation beyond the patch Mat.

### 3.7 Inference engine — decision: ORT + CUDA EP + FP16 (TensorRT **not** taken forward)

**Decision (2026-06): the engine is ONNX Runtime with its existing EP cascade (CoreML → CUDA → CPU)
plus FP16 weights. TensorRT was evaluated and is _dismissed_ — multiplatform support makes it more
trouble than it's worth.**

*Considered, because* TensorRT is the fastest engine on NVIDIA (layer fusion, FP16/INT8, kernel
auto-tuning — Ultralytics cites ~5× GPU speedup) and Ultralytics exports to it directly
(`model.export(format="engine")`). Two routes exist: a standalone TensorRT SDK with native `.engine`
files, or ONNX Runtime's TensorRT *execution provider* (a shared provider lib that auto-falls-back
when TensorRT isn't installed).

*Dismissed, because:*
- **Multiplatform support is too problematic — the deciding factor.** PinPoint ships
  Linux/macOS/Windows; TensorRT is NVIDIA-only (macOS = CoreML/no-CUDA, Windows-without-NVIDIA = CPU).
  It could never be the *sole* engine, so we'd carry the full ORT/CoreML/CPU path regardless — TensorRT
  only ever *adds* a second, platform-specific path on top of what we already maintain.
- **`.engine` files aren't portable** (built per GPU-arch + TensorRT-version + CUDA-version), which
  breaks the clean "download one `.onnx`, copy to `models/`" recipe — forcing per-machine engine
  build/cache plus a TensorRT runtime dependency.
- **The patch design makes the speedup largely moot for the live path.** A nano-YOLO on a ~320² patch
  is already sub-millisecond on the CUDA EP with FP16; the live bottleneck is camera cadence + the
  shared `FrameThrottle` (`skipFactor=2`), not inference — TensorRT would be optimizing a rounding error.

**FP16 is adopted regardless** — it's the cheap, portable share of the speedup with zero TensorRT
dependency.

**Revisit trigger (not now):** the only place TensorRT might pay off is the **offline** heavy-model
pass over the whole `SwingWindow` (many frames × a bigger model) or constrained hardware (e.g. Jetson).
If offline processing time ever becomes a bottleneck, reconsider the **ORT TensorRT EP** route
specifically (keeps the single `.onnx` + auto-fallback) — never the standalone SDK. Until then, out of
scope.

---

## 4. Model strategy

- **Family:** modern **nano/small YOLO** (YOLOv8n / YOLOv11n class), **single class `golf_ball`**,
  exported to ONNX. (The report names YOLOv3/v4 as *categories*; use the current nano equivalent.)
- **Input:** square `letterbox` to the patch size (320 or 416), NCHW float32, `0–1` scale (YOLOv8
  export default). Match `inputSize` to the patch so TRACKING feeds 1:1.
- **Output decode:** YOLOv8 ONNX emits `[1, 4+nc, N]`; single class → take `argmax` over `conf`,
  threshold, (NMS unnecessary for one ball — pick the max). Convert `xywh`→centre+radius
  (`radius ≈ max(w,h)/2`). Small inline decoder, unit-tested (§9).
- **Sourcing the weights — verified 2026-06 (we do NOT train from scratch):**
  - **Data is abundant (the easy part).** Public golf-ball datasets, directly exportable to
    YOLOv8/v11 format: Roboflow Universe `anna-gaming/golfball` (~17k imgs), `aerobotics/golf-ball-external-2`
    (~2.5k), `sai-gon-university/golf-ball-and-hole-detection-1k7` (~1.2k), plus the canonical paper's
    **MIT-licensed** labeled set (`github.com/rucv/golf_ball`, dataset on Google Drive).
  - **Off-the-shelf weights exist but are domain-mismatched — warm-start, don't ship as-is:**
    - `github.com/RyanShihabi/Golf-Ball-Broadcast-Model` — **YOLOv8n**, reported **96% precision / 94%
      recall**, weights committed under `/weights`. **But** trained on **PGA *broadcast* footage**
      (wide-angle, ball crossing frame, SAHI-sliced + optical-flow) and **has NO license** → wrong
      domain (we need teed / close-up / impact) and not safely shippable.
    - `rucv/golf_ball` — the paper's **Faster R-CNN** (two-stage, heavier), MIT, but published as code +
      dataset; trained weights aren't clearly distributed.
    - Several Roboflow Universe "pre-trained model + API" entries exist (quality/license vary per project).
  - **→ The path is FINE-TUNE, not create-from-scratch.** Fine-tune a nano-YOLO (YOLOv8n/v11n) on the
    public sets **+ our own captured swing windows** (our exact camera angles, teed ball, white-on-white
    at impact). Optionally warm-start from the broadcast weights *iff* a license is obtained. This is
    hours of GPU on one class, not a research project.
  - **Re-benchmark on golf** — the report's cross-sport caveat: tennis/volleyball numbers don't transfer
    1:1, and even the golf *broadcast* model is the wrong sub-domain. Measure recall in the **impact window**.
  - **Host the resulting `.onnx`** (HuggingFace, like the MoveNet/ViTPose models) for CMake `file(DOWNLOAD)`.
- **Accuracy levers if impact recall is poor** (report §2.5, apply in priority order, mostly offline):
  tighter patch crop → multi-frame/heatmap input (TrackNet-style 3-frame) → motion channel
  (training-dependent — can *drop* recall if done wrong) → a P2 high-resolution detection head.

Until the model exists, Phase 1 runs `ClassicalPatchDetector` and `HAVE_YOLO_BALL` is simply undefined.

---

## 5. Offline trajectory refinement (`ShotProcessor` 3rd worker)

Add a concurrent worker in the **Processing** state, beside `ShotAnalyzer ∥ SwingExporter`
(`shot_processor.cpp:334-369`), reading the same frozen const window (zero-copy, safe for ≥3 readers
— `shot_processor.cpp:275-276`).

**Value types** (mirror `ShotAnalysisJob`/`Result`, `src/Analysis/shot_analyzer.h:35-66`):
```cpp
struct BallTrajectoryJob {
    pinpoint::SourceId faceOnSourceId, downTheLineSourceId;
    qint64 impactUs; QString swingDir;     // resolved on the UI thread
};
struct BallPoint { qint64 t_us; float x, y, r; float conf; };
struct BallTrajectoryResult { bool ok=false; QString error; std::vector<BallPoint> points; };
```

**Worker** (UI-thread-free; runs on `QtConcurrent::run`):
- Pick face-on (`CameraInstance::FaceOn==2`) else DTL (`shot_processor.cpp:289-294`).
- Walk frames: `entriesFor(sourceId)` → `payloadOf(entry)` (zero-copy `ReadHandle{data,bytes}`) →
  decode to BGR (`swing_window.h:57,64,66`).
- **Refactor first:** extract the exporter's `demosaicPlanFor` + decode (`swing_exporter.cpp:67-83,
  336-356`) into a shared header (`src/Buffer/frame_decode.h` or `src/Export/frame_decode.h`) so the
  exporter and this worker share one decoder. (Today it's private to the exporter.)
- Run the **heavier** model with multi-frame/motion cues; because the whole window is in hand, apply a
  **forward-backward (RTS) Kalman smoother** — strictly better than the online filter.
- Find impact frame = entry nearest `impactUs` (`swing_exporter.cpp:171-174`).

**Output & join:**
- Fold an **additive `"ballTrajectory"` block** into `swing.json` (same additive pattern as the
  `"thumbnail"` block — CLAUDE.md ShotProcessor notes). Optionally draw the trace on the exported MP4.
- **Additive / non-blocking:** failures degrade gracefully (no trajectory), never gate the ¼× replay
  (which stays gated on analysis+export only). But the worker **must** be joined before window
  destruction — extend `maybeJoin()` (`shot_processor.cpp:542`) to await all three, and ensure
  `finishNowBlocking()` joins it too (teardown stop-barrier). Treat its outcome as non-fatal.

---

## 6. Build / CMake

Mirror the **ViTPose block** (`CMakeLists.txt:1233-1277`) — the cleanest single-model template.

1. **Option** (near `CMakeLists.txt:508-512`): `option(WITH_YOLO_BALL "ONNX YOLO ball detector" ON)`.
2. **Download + define + copy** (configure-time `file(DOWNLOAD)` → `${CMAKE_BINARY_DIR}/_deps/yolo_ball/`,
   then POST_BUILD `copy_if_different` to `$<TARGET_FILE_DIR:PinPointStudio>/models/` and macOS
   `Resources/models/`). Define `HAVE_YOLO_BALL` + `YOLO_BALL_MODEL_FILE="yolo_ball.onnx"`. (No hash
   check today — consider adding `EXPECTED_HASH`; the existing models don't.)
3. **Register sources** (after `CMakeLists.txt:1609-1634`):
   ```cmake
   target_sources(PinPointStudio PRIVATE
       src/Pose/ball_detector_cnn.h
       src/Pose/ball_detector_cnn.cpp)
   ```
   List both `.h` and `.cpp` (AUTOMOC scans headers). Keep the classical loop compiled unconditionally;
   gate only the `YoloPatchDetector` translation unit on `EXISTS "${_yolo_ball_model_path}"` if desired.
4. **OpenCV / KalmanFilter:** **nothing to do** — `opencv_video` is already linked via `${OpenCV_LIBS}`
   (`find_package(OpenCV REQUIRED)`, no COMPONENTS, `CMakeLists.txt:1594`). Just
   `#include <opencv2/video/tracking.hpp>`. *(Optional hardening: switch to
   `find_package(OpenCV REQUIRED COMPONENTS core imgproc video)` for portability.)*
5. **AUTOMOC:** automatic via `qt_standard_project_setup` (`CMakeLists.txt:29`).
6. **ONNX runtime:** already present (`HAVE_ONNXRUNTIME`, GPU build on Linux). Linux relies on RPATH
   for `libonnxruntime.so` (no extra copy needed — same as the existing pose models).
7. **(Optional) install rule:** add `install(FILES "${_yolo_ball_model_path}" DESTINATION bin/models)`
   if shipping via `cmake --install` (pose models currently have none; only Whisper does).

C++ guards: live class `#if defined(HAVE_OPENCV) && defined(HAVE_ONNXRUNTIME)`; the YOLO patch detector
additionally `#ifdef HAVE_YOLO_BALL`.

---

## 7. Integration & migration

- **Wiring change** in `camera_instance.cpp:347-368` is a type swap (`BallDetector` →
  `BallDetectorCnn`) plus a queued `load()` invoke after `moveToThread` (mirror how MoveNet's `load()`
  is triggered). All existing connections (`ballDetected→onBallDetected`, `→clearBusy`,
  `detectionSkipped→clearBusy`, `roiChanged→setRoi`, `setBallEnabled→setEnabled`) stay identical.
- **A/B safety net:** keep `BallDetector` in the tree and add a runtime selector
  (`AppSettings` flag, e.g. `ballBackend: classical|cnn`) so we can compare side-by-side in the field
  before deleting the old path. Cheap insurance given the cross-sport accuracy caveat.
- **Contract preserved:** `BallDetection`, QML overlay bindings (`ballX/Y/Radius`, `PpCameraFrame.qml
  :373-393`), and `ballPresent` all unchanged → no downstream edits.

---

## 8. Shot arming (future hook, dormant in v2)

Ball stays **signal-only** (CLAUDE.md capture-intent contract). The detector exposes
`ballLaunched(timestampUs)` — fired when the Kalman track shows a sudden velocity spike / the ball
exits the ROI — but **nothing is connected to it in v2**. When we choose to arm shots from vision, the
wiring is a single connection:
```cpp
connect(cnn, &BallDetectorCnn::ballLaunched, shotController,
        [sc](qint64 ts){ sc->triggerShot(ShotController::Source::Ball, ts); });
```
`Source::Ball` and `kPostRollBallMs=500` are already in place (`shot_controller.h:43`,
`shot_processor.cpp:54`). The `armed` gate (`shot_controller.cpp:70-73`) makes this safe by
construction. Keep it dormant until validated to avoid false triggers from waggles/practice strikes.

---

## 9. Testing & validation

Per project convention (standalone CTest harnesses, e.g. `src/Analysis/tests/`):

1. **Kalman loop** (`ball_tracker_test`): synthetic constant-velocity + accelerating trajectories →
   assert predict/correct accuracy, coasting through N dropped detections, re-acquisition after K
   misses, impact velocity-spike handling.
2. **Coordinate mapping**: patch-local px ↔ full-frame normalized round-trip across patch positions
   and frame sizes (the single most bug-prone bit — mirror `ball_detector.cpp:102-104`).
3. **YOLO decode**: synthetic `[1,5,N]` tensor → `BallDetection` (argmax, threshold, xywh→centre/r).
4. **Throttle balance**: assert exactly one `clearBusy`-bound signal per `detect()` on all paths
   (found / not-found / disabled / empty-ROI / exception).
5. **Golf-specific benchmark**: labeled golf clips (teed, takeaway, impact, post-impact) →
   precision/recall, **separately reported for the impact window** (re-benchmark per the cross-sport
   caveat). Compare classical vs CNN backends.
6. **Visual verification**: offscreen `grabWindow` of `PpCameraFrame` overlay on a known clip
   (per the project's QML visual-verification harness pattern).
7. **Offline worker**: feed a captured `SwingWindow` fixture → assert a sane monotonic trajectory and
   a non-fatal degrade when frames are unsupported (MJPEG/H264).

---

## 10. Phased delivery

| Phase | Deliverable | Model needed? | Risk |
|---|---|---|---|
| **1 — Tracking infra** | `BallDetectorCnn` with Kalman + patch + state machine, using `ClassicalPatchDetector`; drop-in swap; A/B flag; tests 1,2,4,6 | No | Low — pure refactor of proven CV + new tracker |
| **2 — CNN in-patch** | `YoloPatchDetector` + ONNX session + CMake model block; tests 3,5 | **Yes** (train/host `yolo_ball.onnx`) | Med — depends on data/model quality |
| **3 — Offline trajectory** | shared `frame_decode` extraction; `BallTrajectoryWorker` as 3rd `ShotProcessor` worker; `swing.json` block; test 7 | Reuses Phase-2 model | Med — join/teardown correctness |
| **4 — Enrichments (opt.)** | track-confidence `ballPresent`; dedicated ball cadence; multi-frame/motion/P2; `ballLaunched`→shot arming | — | Low/Med, incremental |

Phase 1 is shippable immediately and delivers most of the *performance* win (patch + Kalman) and a
clean seam; Phase 2 delivers the *accuracy* win once the model lands.

---

## 11. Risks & open questions

- **Model & training data is the gating dependency.** Phase 1 (classical-in-patch) de-risks by
  decoupling the infrastructure from the ML.
- **Live flight is fundamentally brief** at 30–60 fps — manage expectations; trajectory belongs to the
  offline/high-fps path. What is our capture fps at impact, and do we need a high-fps mode?
- **Shared throttle `skipFactor=2`** caps live ball cadence; a dedicated ball cadence may be needed for
  responsive launch detection (Phase 4).
- **Cross-sport accuracy** won't transfer 1:1 (report caveat) — budget a golf re-benchmark.
- **Unsupported codecs offline:** exporter already can't decode MJPEG/H264_NAL/12–16-bit Bayer
  (CLAUDE.md) — the offline worker inherits this; degrade gracefully.
- **Open:** is full-frame coarse CNN acquisition ever worth it vs classical acquisition? (Default: no —
  classical acquisition is cheaper and strong on the static teed ball.)

---

## 12. Touch list (files)

**New:** `src/Pose/ball_detector_cnn.{h,cpp}`, `src/{Buffer,Export}/frame_decode.h` (shared decoder),
`src/Analysis/ball_trajectory_worker.{h,cpp}` (+ job/result types), tests under `src/Pose/tests/`.
**Edit:** `src/Gui/camera_instance.cpp` (type swap + `load()` invoke), `src/Gui/shot_processor.{h,cpp}`
(3rd worker + `maybeJoin`/`finishNowBlocking`), `src/Export/swing_exporter.cpp` (use shared decoder),
`CMakeLists.txt` (model block + sources), `src/Gui/app_settings.*` (A/B backend flag).
**Unchanged (contract preserved):** `PpCameraFrame.qml`, `onBallDetected` hysteresis,
`BallDetection`, `FrameThrottle`, `ShotController`.

---

*Part of the shot-detection design: [`shotdetection.md`](shotdetection.md) frames the multi-modal
problem (acoustic + inertial + vision fusion); this doc is the implementation plan for the **vision
modality's** ball detector, whose `ball-present` / `ballLaunched` outputs feed shot-detection's
corroboration/veto path. Club/clubhead detection remains an open R&D item.*
