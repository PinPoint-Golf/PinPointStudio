# Pinpoint Swing Export — Developer Guide

**Audience**: Developers working on or integrating with the Pinpoint application
**Location**: `src/Export/` (plus integration in `src/Gui/shot/shot_processor.{h,cpp}`)
**Language**: C++20, Qt 6
**Status**: Production — video + IMU + ball streams (codec h264/h265, container mp4/mov/mkv, export-time downscale, optional raw-payload sidecar, IMU json/csv/binary, pattern-based session-folder naming), impact thumbnail, extensive capture provenance, and a bulk zip export. Pose streams are serialised **if present on the job** but still have no producer; metrics/launch-monitor streams are schema-ready but not produced

> **⚠ Ownership moved (2026-06), and this guide predates several later changes.**
> The swing machinery described below as living in `CameraManager` (window
> capture, replay, `startSwingSave()`, `buildSwingExportJob()`,
> `maybeFinishSwing()`, the resume gating) **migrated to `ShotProcessor`**
> (`src/Gui/shot/shot_processor.{h,cpp}`), which drives it from
> `ShotController::shotDetected` and runs a per-session-type `ShotAnalyzer`
> **before** the export (the two heavy workers are sequenced, not overlapped —
> see the shot-analyzer guide §11). The join is now `maybeJoin()`/`finishShot()`;
> the teardown stop-barrier is `ShotProcessor::finishNowBlocking()`; resume
> gating keys off `EventBuffer::swingWindowLive()`. Read `CameraManager` as
> `ShotProcessor` wherever the text below touches window lifetime, and
> "5 s window" as **4 s**.
>
> Since then the exporter has also gained: the impact thumbnail (`thumb.jpg`, via
> `QImage::save` — see CLAUDE.md for the libjpeg-clash rationale), NV12/YUV420P
> handling, a **shared decode path** (`frame_decode.h`, §3), the **ball stream**
> and its persisted empty-mat baseline, the **capture provenance block**, and
> **`SwingZipExporter`** for bulk export. Those are covered in §3 and §6 below.
> The core invariants — values-in jobs, borrowed window, zero-copy reads,
> one-writer-per-file — are unchanged.

---

## Contents

1. [What Swing Export Is](#1-what-swing-export-is)
2. [Where It Fits in Pinpoint](#2-where-it-fits-in-pinpoint)
3. [Core Concepts](#3-core-concepts)
4. [The Export Pipeline — Frame by Frame](#4-the-export-pipeline--frame-by-frame)
5. [Resume Gating — the CameraManager Join](#5-resume-gating--the-cameramanager-join)
6. [The `swing.json` Sidecar](#6-the-swingjson-sidecar)
7. [On-Disk Layout — `SwingPaths`, the session file and `SessionRepository`](#7-on-disk-layout--swingpaths-the-session-file-and-sessionrepository)
8. [The Encoder — `IVideoEncoder` / `FfmpegVideoEncoder`](#8-the-encoder--ivideoencoder--ffmpegvideoencoder)
9. [Configuration — AppSettings Keys](#9-configuration--appsettings-keys)
10. [Threading Model](#10-threading-model)
11. [Build System — FFmpeg Detection](#11-build-system--ffmpeg-detection)
12. [Extending the Module](#12-extending-the-module)
13. [Common Mistakes](#13-common-mistakes)
14. [File Map](#14-file-map)
15. [Testing](#15-testing)

Two sections carry most of what changed since this guide was first written:
**§3** (the job's provenance fields and the shared `frame_decode` path) and
**§6** (the ball stream, the baseline sidecar, and `swing_summary.json`).

---

## 1. What Swing Export Is

Swing Export persists a captured swing to disk the moment it is detected. For each
swing it writes one folder containing:

- **One H.264/MP4 per camera**, named by the camera alias. Every captured frame is
  kept; the container framerate is fixed at **30 fps**, so a 150 fps capture plays
  back as ≈5× slow motion with zero frames dropped or duplicated.
- **One `swing.json`** — an extensible sidecar holding video stream descriptors, a
  per-frame microsecond timestamp index, and the embedded IMU sample streams.

It is built around one hard constraint: **the video rings are GB-scale and must
never be duplicated in RAM.** The exporter streams frame-by-frame directly out of
the frozen `SwingWindow` via zero-copy reads. Peak extra memory is one reusable
BGR scratch frame plus the encoder's single YUV frame — never the ring.

It is not a general recording facility. It runs only while the `EventBuffer` is
`Paused` with a live `SwingWindow`, concurrently with the on-screen replay, and the
buffer cannot resume until it finishes.

---

## 2. Where It Fits in Pinpoint

```
┌────────────────────────────────────────────────────────────────────────┐
│  Ball lost (swing complete)                                            │
│       │                                                                │
│  EventBuffer::pause()  ──►  captureSwingWindow(5000ms)                 │
│       │                          │                                     │
│       │                    m_swingWindow (frozen, zero-copy)           │
│       │                     │              │                           │
│       │              [Replay ¼×]     [SwingExporter]                   │
│       │              (UI thread,     (QtConcurrent worker:             │
│       │               60Hz timer)     per-camera MP4 + swing.json)     │
│       │                     │              │                           │
│       │                     └──── join ────┘                           │
│       │                  maybeFinishSwing()                            │
│       │             (both done → window destroyed)                     │
│       ▼                          │                                     │
│  EventBuffer::resume()  ◄────────┘  (iff recording && ball present)    │
└────────────────────────────────────────────────────────────────────────┘
```

### The export workflow

1. **Ball lost**: `CameraManager::onCameraBallPresenceChanged(false)` pauses the
   buffer and calls `startReplay()`.
2. **Window capture**: `startReplay()` captures a trailing 5 s `SwingWindow`,
   builds the replay tracks, and emplaces the window into `m_swingWindow`.
3. **Job build (UI thread)**: `buildSwingExportJob()` resolves everything that
   touches QSettings or controllers — camera aliases, CRF, IMU alias map, athlete
   metadata, the output directory — into a value-type `SwingExportJob`.
4. **Worker launch**: `startSwingSave()` hands the job plus a pointer to the live
   window to `QtConcurrent::run`. The replay and the save now read the same frozen
   window concurrently.
5. **Encode**: the worker encodes each camera sequentially, then writes
   `swing.json` atomically via `QSaveFile`.
6. **Join**: replay end and save completion both funnel into
   `maybeFinishSwing()`. Whichever finishes **last** destroys the window and
   re-evaluates the resume condition.
7. **Resume**: the buffer resumes only if recording is active, a ball is present,
   and nothing (e.g. `stopAll()`) vetoed auto-resume.

The exporter is invisible to QML in v1. `CameraManager` emits
`swingSaved(QString path)` / `swingSaveFailed(QString error)`; diagnostics go
through `ppInfo()`/`ppWarn()`/`ppError()` (auto-captured by `PpMessageLog`).

---

## 3. Core Concepts

### SwingExportJob — values in, no controllers

The worker must never touch `QSettings`, `AppSettings`, or any controller — those
are UI-thread objects. Everything is resolved up-front into a `SwingExportJob`:

```cpp
struct SwingExportJob {
    QString swingDir;                          // absolute, already created
    QString swingId;       int swingIndex;

    std::vector<SwingExportCamera> cameras;    // aliases sanitised + deduped
    QHash<QString, QString>            imuAliasBySerial;
    QHash<QString, SwingImuDeviceInfo> imuDeviceBySerial;   // rate, fusion, A/M
    std::vector<SwingPoseStream> poseStreams;  // no producer yet
    std::vector<SwingBallStream> ballStreams;  // face-on ball track + launch step

    // Capture provenance — session context, detector constants, host/build
    int sessionType, shotSource;
    QString swingDetectionSensitivity, motionCaptureQuality;
    qint64 imuBleLatencyUs;  int audioDeviceLatencyUs;
    SwingHostInfo host;                        // appVersion, gitSha, hostname,
                                               // platform, poseBackend

    // Encode + serialisation settings
    QString codec;  int crf;  bool saveImu;
    QString resolutionMode;                    // native | half | 1080p | 4k
    bool    saveRaw;                           // "<alias>.raw" sidecars
    QString imuFormat;                         // json | csv | binary
    bool    savePose;

    QString athleteName, athleteUuid, handedness, sessionId;

    // Club geometry + the club-length prior, so re-analysis reproduces the fuse
    double clubLengthM, hoselFromButtMm;  QString shaftType, clubName;
    std::vector<double> bandCentersMm;
    double priorClubLenPx, priorClubLenVarPx;  int priorClubLenN;

    QDateTime wallclockAnchorUtc;              // see §6, clock block
    SourceId  thumbnailSourceId;  int64_t thumbnailTimestampUs;
    int64_t   impactUs;                        // WINDOW-RELATIVE µs
};
```

`SwingExporter::run(const SwingWindow&, const SwingExportJob&)` is a **stateless
static function** — all scratch is local, the result comes back as a
`SwingExportResult{ok, swingDir, error, thumbnailPath, manifest}` value through
the `QFuture`.

Note the last field: the exporter **returns** the raw `pinpoint.swing` manifest
tree rather than writing it. The unified `swing.json` (manifest + the analyzer's
`"analysis"` block) is written once, at the join, on the GUI thread — see the
shot-analyzer guide §9. The header is not built here: `run()` and the degraded
analysis-only path both call **the one header writer** (`src/Export/recording_header.h`,
step 1.2 — `recordingHeaderFor(job, t0, duration)` + `recordingHeaderJson(header,
streams)`); `SwingExporter::analysisOnlyHeader(job, t0, duration)` is that header with no
media, so the two headers are identical but for `streams` and `thumbnail`.

### Two provenance rules the job encodes

**`impactUs` is window-relative, and it is the re-analysis anchor.** A corpus
swing captured with analysis skipped has no `analysis.phases[Impact]`, so
`capture.impactUs` is the only record of when the ball was struck. Everything
under `analysis` is likewise normalised to window-relative µs on write — absolute
`nowMicros()` values are meaningless in a file that outlives the process.

**Everything re-analysis needs must be *in the file*.** The club geometry, the
club-length prior as it stood *before* this shot's update, the motion-capture
quality tier, the IMU A/M calibration snapshot, the hitting-area ROI, and the
learned empty-mat baseline are all persisted for one reason: offline re-analysis
must reproduce what the live shot did without reading `AppSettings`, which may
have changed since. If you add an input that affects analysis, persist it here.

### `SwingExportCamera` — per-stream setup, not just a filename

Each camera carries its capture-time setup into the stream's `"setup"` object:
perspective (None/DownTheLine/FaceOn/Other), mirroring, fixed-in-place, exposure
and its provenance, plus the ball-detection block — calibration state, the
calibrated ball position and radius (full-frame normalised, co-registered with
the shaft-track head samples), the hitting-area ROI, and the learned baseline
blob. The baseline is written as a sidecar; an empty blob omits both the sidecar
and its JSON key entirely, and re-analysis then self-seeds as before.

### IVideoEncoder — abstract base + factory

The codec backend is product-specific, so the encoder is an abstract interface
obtained through a factory:

```cpp
class IVideoEncoder {
    virtual bool open(const VideoEncoderConfig&) = 0;
    virtual bool writeBgr(const cv::Mat& bgr) = 0;  // encoder owns pts
    virtual bool finish() = 0;                      // flush + trailer
};
std::unique_ptr<IVideoEncoder> makeVideoEncoder(const std::string& codec);
```

`"h264"` → `FfmpegVideoEncoder("libx264")`, `"h265"` → `FfmpegVideoEncoder("libx265")`
when built with `HAVE_FFMPEG`; any other key falls back to H.264 (so a stale
`prores`/`raw` setting never loses a swing). A no-FFmpeg build → `nullptr`, and the
save fails gracefully with a logged error. `video_encoder.cpp` is the **only** file
in the module with `HAVE_FFMPEG` ifdefs.

### The borrowed window

The worker receives the `SwingWindow` **by const reference** — it does not own it,
copy it, or extend its life. The contract is enforced entirely by `CameraManager`:
the window is destroyed only in `maybeFinishSwing()`, on the UI thread, strictly
after the worker has returned (guaranteed by `QFutureWatcher::finished` ordering —
see §10).

### Zero-copy reads, through one shared decode path

Per frame, the worker calls `window.payloadOf(entry)` and wraps the returned bytes
in a `cv::Mat` header **without copying**:

```cpp
const cv::Mat raw(srcH, srcW, plan.matType,
                  const_cast<std::byte*>(handle.data), stride);
cv::cvtColor(raw, bgr, plan.cvtCode);    // demosaic into the ONE reused scratch
```

While the buffer is `Paused` the producers are stopped, so the bytes are stable —
this is the same guarantee the replay path relies on.

The demosaic table used to live inside `SwingExporter`. It is now
**`src/Export/frame_decode.h`** — pure functions, no Qt, no logging — because the
shot analyzer decodes the same frozen payloads and the two must not be able to
diverge. It exposes:

| Function | Purpose |
|---|---|
| `demosaicPlanFor(PixelFormat)` | `{supported, matType, cvtCode, tag, rowsNum/rowsDen}`. The `tag` is what lands in `swing.json` `processing.demosaic`. |
| `frameGeometry(CameraFormat&, stride, minBytes)` | Plane-0 stride and the minimum bytes of one frame. False for unsupported formats or degenerate dimensions. |
| `decodeFrame(...)` | One payload → BGR `CV_8UC3`. False on null/short payloads and unsupported formats. |

Two details in there that bite:

- The mapping deliberately **mirrors the live-view path**
  (`camera_instance.cpp` → `raw_video_frame.cpp`) so an exported frame's colour
  matches what was on screen. The edge-aware `_EA` variants share the pattern
  naming — better quality, and the cost is irrelevant off the hot path.
- Planar 4:2:0 payloads (NV12/I420) are **1.5× image-height rows tall**, which is
  what `rowsNum`/`rowsDen` encode. Treating them as `height` rows silently
  truncates the chroma planes.

Only 8-bit formats are handled; MJPEG, H264_NAL and 12/16-bit Bayer are
unsupported and skip the source with a warning.

---

## 4. The Export Pipeline — Frame by Frame

Per camera (cameras are encoded **sequentially** — lowest peak RAM; per-camera
parallelism is a possible future option):

1. `entriesFor(sourceId)` → ordered frame entries; `formatOf(sourceId)` →
   `CameraFormat` (dimensions, pixel format, capture fps, strides).
2. Pixel format is mapped to a `DemosaicPlan` (see below). Unsupported formats
   (`MJPEG`, `H264_NAL`, 12/16-bit Bayer) skip the **source** with a `ppWarn` —
   the remaining cameras still export.
3. Dimensions are **cropped** (never padded) to even values — libx264/yuv420p
   requires them, and padding would invent pixels.
4. `makeVideoEncoder(job.codec)` → `open()` with CRF from settings, preset
   `medium`, output 30 fps.
5. Per entry: `payloadOf(e)` — a null or short payload **skips the entry
   entirely**, which keeps the invariant *MP4 frame i == `frames.t_us[i]`*.
   Demosaic into the reused scratch, then:
   ```cpp
   // TODO(restorer): frame restoration hook
   ```
   This comment marks the **single** documented insertion point where a future
   denoise/sharpen stage will go. Nothing else sits between demosaic and encode.
6. `writeBgr(bgr(cropRect))`; record `e.timestamp_us - t0` for the JSON index.
7. `finish()` flushes the encoder and writes the MP4 trailer.

### Demosaic — matching the live view exactly

The OpenCV `COLOR_Bayer*` constants are offset from the sensor pattern naming —
getting this wrong swaps colour channels. The exporter's mapping **mirrors the
live-view path** (`camera_instance.cpp` PixelFormat→BayerPattern,
`raw_video_frame.cpp` pattern→cvtColor code) so exported colour is identical to
what the user saw on screen:

| `PixelFormat` | cvtColor code | JSON `demosaic` tag |
|---|---|---|
| `BayerRG8` | `COLOR_BayerRGGB2BGR_EA` | `"EA"` |
| `BayerBG8` | `COLOR_BayerBGGR2BGR_EA` | `"EA"` |
| `BayerGR8` | `COLOR_BayerGRBG2BGR_EA` | `"EA"` |
| `BayerGB8` | `COLOR_BayerGBRG2BGR_EA` | `"EA"` |
| `Mono8` | `COLOR_GRAY2BGR` | `"none"` |
| `BGR24` | passthrough (no copy) | `"none"` |
| `YUV422` / `YUYV` | `COLOR_YUV2BGR_YUYV` | `"none"` |
| `UYVY` | `COLOR_YUV2BGR_UYVY` | `"none"` |

The `_EA` (edge-aware) variants share the same pattern naming as the live view's
bilinear codes — identical colour mapping, better interpolation quality. The cost
difference is irrelevant off the hot path.

### IMU streams

When `saveImuStreams` is enabled, IMU sources are **discovered from the window
itself**: distinct `source_id`s in `window.entries()` whose `FormatDescriptor`
holds the `ImuFormat` variant. Each payload is `memcpy`d into a local `ImuSample`
(the alignment-safe equivalent of a reinterpret-cast — the payload is the decoded
fixed 40-byte sample) and emitted as parallel `t_us` + 10-float rows:
accel xyz (g), gyro xyz (deg/s), quat wxyz.

---

## 5. Resume Gating — the CameraManager Join

This is the part most likely to bite you when modifying `CameraManager`.
**The `SwingWindow` is valid only while the buffer is `Paused`. The save worker
reads ring memory through it. Therefore the buffer must not resume — and the
window must not be destroyed — until BOTH the replay and the save have finished.**

### State

```cpp
QFutureWatcher<SwingExportResult> m_swingSaveWatcher;  // finished → onSwingSaveFinished
bool m_swingSaveInFlight = false;   // worker still reading the window
bool m_swingAutoResume   = true;    // cleared by stopAll() / stopReplay(false)
```

All of this state lives on the UI thread. The worker communicates only through
`QFutureWatcher::finished` (delivered on the UI thread), so no mutexes exist.

### The join

```cpp
void CameraManager::maybeFinishSwing()
{
    if (m_replaying || m_swingSaveInFlight) return;   // wait for BOTH
    if (!m_swingWindow) return;                        // already finished
    m_swingWindow.reset();                             // worker has returned — safe
    if (m_swingAutoResume && m_recording && m_ballPresentCount > 0)
        resumeBuffer();
}
```

- `stopReplay()` no longer resets the window or resumes — it clears replay state
  and calls `maybeFinishSwing()`.
- `onSwingSaveFinished()` clears `m_swingSaveInFlight`, emits
  `swingSaved`/`swingSaveFailed`, and calls `maybeFinishSwing()`. **The failure
  path joins identically** — a failed save must still release the window and let
  the buffer resume.
- The ball condition is evaluated **at join time**, not at `stopReplay()` time, so
  a ball re-appearing (or leaving) during a long save is handled correctly.

### Every path that could violate the invariant, and its guard

| Path | Guard |
|---|---|
| QML / ball-handler calling `resumeBuffer()` mid-save | `resumeBuffer()` returns early while `m_swingWindow` is live — the hard backstop. The join re-checks ball presence, so a resume is never lost. |
| Second swing while a save is in flight | `startReplay()` returns early if `m_swingWindow` is live (a second `captureSwingWindow()` would assert in `EventBuffer`). |
| `stopAll()` mid-save | Sets `m_swingAutoResume = false` *before* `stopReplay(false)` so a save finishing later cannot resume a stopped session. `startReplay()` re-arms the flag for the next swing cycle. |
| Camera deselect mid-save (`setSelected`) | `deregisterSource()` asserts no live window, and the worker reads ring memory — so the path **blocks**: `stopReplay(false)` → `m_swingSaveWatcher.waitForFinished()` → `m_swingWindow.reset()`. (This also fixed a pre-existing deselect-during-replay assert.) |
| App exit (`~CameraManager`) | Waits on the watcher, then resets the window, before any ring teardown. Abandoning the worker would be a use-after-free. |
| Ball lost again while save in flight | `pauseBuffer()` is a no-op (already Paused) and `startReplay()`'s window guard returns — correct, because the buffer captured nothing new while Paused. |

### Rules

**You MUST:**
- Destroy `m_swingWindow` only inside `maybeFinishSwing()` (or after a blocking
  `waitForFinished()` in the teardown paths).
- Route every new resume path through `resumeBuffer()` so the window guard applies.
- Call `maybeFinishSwing()` after clearing any of the two completion flags.

**You MUST NOT:**
- Add a resume call that bypasses `resumeBuffer()`.
- Capture a new `SwingWindow` while one is live.
- Touch `m_swingWindow` from the worker — the worker sees only the const reference
  it was handed.

---

## 6. The `swing.json` Sidecar

Schema identifier: **`pinpoint.swing/2`** — the one schema constant `kSwingDocSchema`
(`recording_header.h`; before step 1.2 the raw manifest said `/1` in memory and the
writer restamped it). The header (`schema`, `shotKind`, `swing`, `athlete`, `session`, `clock`,
`window`, `thumbnail`, `capture`, `analysisContext`) comes from the one header writer. All stream timestamps are **relative µs offsets from
`t0`** (small, exact); `t0_us` is the absolute capture-clock value. A new stream
type later is just another element of `streams[]` — readers must ignore unknown
`kind`s.

> **This is the exporter's *raw* manifest.** At the analyzer→exporter join,
> the writer (`SwingDocWriter::writeDocument()`, reached through `RecordingRepository::create`
> — see [The one door](#the-one-door--recordingrepository-step-14) below) composes this tree with the analyzer's
> output into **one** `swing.json` — stamping the on-disk schema (`kSwingDocSchema`)
> and adding inline `analysis` (`pinpoint.analysis/2`: score, tier, metric series,
> phases) and `review` (user rating/note) blocks, plus a `thumbnail` block. There is
> no separate `analysis.json`. The raw `streams[]` shown below are preserved verbatim.

```json
{
  "schema": "pinpoint.swing/2",
  "swing":   { "index": 7, "id": "swing_0007" },
  "athlete": { "name": "Mark Liversedge", "uuid": "…", "handedness": "Right", "heightM": 1.80 },
  "session": { "dir": "2026-06-05_session-01" },
  "clock":   { "t0_us": 173456789012345, "unit": "us",
               "wallclock": "2026-06-05T14:22:01.234Z" },
  "window":  { "start_us": 0, "end_us": 4980000 },
  "capture": {
    "sessionType": 1, "shotSource": 1,
    "swingDetectionSensitivity": "Medium",
    "latencyUs": { "imuBle": 30000, "audioDevice": 20000 },
    "host": { "app": "PinPointStudio", "version": "0.1", "gitSha": "cab95fb",
              "hostname": "studio-pc", "platform": "Ubuntu 26.04",
              "poseBackend": "CUDA" }
  },
  "streams": [
    {
      "kind": "video", "alias": "faceOn", "file": "faceOn.mp4",
      "source": { "serial": "…", "pixelFormat": "BayerRG8",
                  "width": 1936, "height": 1096 },
      "capture":  { "fps_num": 150, "fps_den": 1,
                    "exposureUs": 250.0, "exposureAuto": false, "exposureSource": "measured" },
      "setup":    { "perspective": 2, "perspectiveName": "FaceOn",
                    "mirrored": false, "fixedInPlace": true,
                    "ballDetection": { "calibrated": true, "margin": 0.42,
                                       "driftAtCapture": 0.0,
                                       "calibratedAt": "2026-06-12T09:14:03Z",
                                       "center": [0.51, 0.78], "radiusNorm": 0.012,
                                       "positionSource": "calibrated" } },
      "playback": { "fps": 30 },
      "processing": { "demosaic": "EA", "restorer": "none" },
      "frames": { "count": 742, "t_us": [0, 6671, 13342] }
    },
    {
      "kind": "imu", "alias": "leadWrist", "schema": "imu_sample_v2",
      "source": { "serial": "…" },
      "device": { "outputRateHz": 200, "fusionMode": "6axis",
                  "orientationFilter": "Madgwick", "placementSlot": "A" },
      "units": { "accel": "g", "gyro": "deg/s", "quat": "wxyz" },
      "samples": { "count": 498, "t_us": [0, 5000, 10000],
                   "data": [[0.01, 0.99, 0.02, 1.2, 0.3, -0.1, 1, 0, 0, 0]] }
    }
  ]
}
```

### The one door — `RecordingRepository` (step 1.4; keyed by `ShotRef` since step 2.4)

Every write of a swing file — and its archive stub and its markup truth — goes through
**`RecordingRepository`** (`src/DomainModel/recording_repository.h`, the domain-model ring),
implemented on the store by **`SwingStoreRecordingRepository`** (`src/Export/swing_store_repository.*`).
`SwingDocWriter`'s write functions are **private**: the adapter is their friend.

**The key is the shot's `ShotRef`** (its session's `SessionId` + its swing number; IM-S2-2)[^dec160],
never its folder. The adapter is made over a `SessionRepository` and resolves each ShotRef's folder
through `SessionRepository::swingFolder` — the one method of the session store safe from any thread,
because it reads the folders the identity map *publishes* — so a worker (the live join, re-analysis)
writes by ShotRef without touching the map. A ShotRef it cannot resolve (null, or its session not
open) is never guessed at: every write is `Failed` ("not found"), every read empty. The reading door,
**`RecordingReader::load(ShotRef)`** (`SwingStoreRecordingReader`, `src/Export/recording_loader.*`), is
keyed and resolved the same way.

**The edge translation**[^dec90]. Whoever holds a *path* — a tool's argument, a picker, a carousel row
that still carries its folder until step 4 — turns it into the ShotRef once, where it comes in, with
`RecordingRepository::shotOfFolder(swingDir[, legacyAthlete])` (→ `SessionRepository::openSwingFolder`):
the swing number from the folder's name (`SwingPaths`), the session's id from its `session.json`, or —
**a session older than step 2, with no session file** — D3's derived id[^dec150] from the athlete who
**owns** the session's folder[^j39]: the athlete the caller names; else the
adapter's owner resolver (`setOwnerOf`: in the app, the current athlete when the folder is theirs — the
athlete the session list derives the same id from); else the athlete folder's `athlete.json`
(`AthleteFiles::read`); and only when no owner is found, the athlete the swing's own header records
(`athlete.uuid`, read from the document's root alone). The swings' uuids were minted on the capturing
machine and need not be the owner's (H-09), so the header is the last resort. A folder already held keeps the id it was listed with. Not a `swing_NNNN`
folder, or a legacy session whose athlete cannot be found: no ShotRef. Owner thread only (it reads the
session into the identity map). Where each caller translates:

| Caller | Its edge |
|---|---|
| the live join (`ShotProcessor` → `capture::writeLiveDocument`) | none: the allocation's ShotRef (`SessionRepository::allocateSwing`) |
| `ShotListModel` (every carousel) | none: a row is its Shot's, keyed by the Shot's `ShotRef`, with the Shot's folder |
| `PpcpClipFiler` | the folder `onSwingReady` announces |
| `LaunchMonitorController` | the folder each write is handed (its own allocation's, or a failed camera shot's) |
| `ReanalysisController` | each queued folder as it starts (GUI thread); the worker reads by the ShotRef |
| `SessionReviewController` (the JSON-era sidecar) | every swing of the session, on the GUI thread, before the pool threads write |
| `MarkupController` (the truth) | each queued folder, once |
| `LibraryConverter` | each folder it walks — over a `SessionStore` of the run's own (it runs on a worker, so it never touches the app's identity map) |
| `SessionArchiver` (behind `SessionRepository::archive` / `restore`, step 2.6) | each swing of the session it archives or restores — over its `SessionArchiveRun`'s own `SessionStore` (§7, "The archive") |
| `lm_repair`, `pps_convert_library`, `swinglab_run`, `backfill_analysis_context` | each folder they are given or find, in `main`'s own `SessionStore` |

`main.cpp` holds the one `SessionStore` and, over it, the one `SwingStoreRecordingRepository` and the
one `SwingStoreRecordingReader` (all three static, the store first, so the static phone-clip filer
never outlives them); it hands them to the callers above.

| Method | Does | Callers |
|---|---|---|
| `shotOfFolder(dir, legacyAthlete)`, `swingFolder(shot)` | the edge translation; the folder as a property | every caller that holds a path |
| `create(shot, NewRecording{header, analysis, reviewClub}, PersistOptions)` | the first write: header + streams (+ analysis), `review.club` seeded | the live join, the phone-clip placeholder |
| `create(shot, reading, DeviceOnlyMeta)` | a shot the launch monitor alone recorded; `shotKind: "deviceOnly"` only when `meta.standalone` (the rescue of a failed camera shot's folder records no kind: a phone clip may still join it) | `LaunchMonitorController` |
| `completeCapture(shot, reading, followedClub, &clubReplaced)` | a late LM reading (IM-4: capture writes in stages); follow-club may replace `capture.club` | `LaunchMonitorController`, `lm_repair` |
| `completeCapture(shot, StreamArrival)` | a phone clip's stream origin, then its file, frames and clock | `PpcpClipFiler` |
| `writeAnalysis(shot, AnalysisOutcome{analysis, imu, capture}, PersistOptions)` | the re-analysis write-back: the document read fresh (**the empty-document guard**: nothing read → `NoDocument`, nothing written), both verdicts set from this pass (`nullptr` withdraws one), `savePose` honoured. **No analysis** = the launch monitor rows derived again from the recorded reading (a launch-monitor-only shot; `lm_repair`, with `dryRun`) | `Shot::applyAnalysis` (the app's re-analysis write-back, refactor step 3.4b-ii; `persistReanalysis` hands it the outcome), swinglab `--write-back`, `lm_repair`; never `ReanalysisController` itself since step 5.1 (it writes back through the `Shot` only) |
| `editReview(shot, ReviewEdit)` | merges rating / note / club / `excluded` / `clubOverride` and — IM-S2-3 — `shotIntent` / `labelCorrections` into `review` (an unset field is left alone; the annotations and the two golfer-input keys are removed when set back to none — except with `keepEmptyGolferInput`, which keeps an empty value as the golfer's explicit "none" while `diagnostics.json`'s older copy holds an answer: step 2.5)[^j33] | `ShotListModel`, `SessionArchiver` (restore), `SessionDiagnosticsModel` (shot intent, label corrections: step 2.5) |
| `readSummary(shot, parseIfUnindexed)` | the shot's summary (`ShotSummary`: what a carousel row shows of it — its number, time, club, video, thumbnail, score, metrics, rating, note, device, warning and the board's annotations), by the cheap read (`SwingDocReader::readSwingSummary`; `parseIfUnindexed` is its `writeSidecar`) — refactor step 3.4b-ii | `Shot::summary` / `rereadSummary` (the carousel's rows over the `Shot`s) |
| `readReview(shot)` | the shot's golfer input on its review (`shotIntent`, `labelCorrections [{family, from, to}]`), from the document's root alone; a document without the keys reads as none, and `hasShotIntent` / `hasLabelCorrections` say whether the review holds each key (absent: the reader falls back to `diagnostics.json`'s older keys until M3) | `SessionDiagnosticsFiles::golferInput` (step 2.5): the diagnostics model, the club baseline's predecessors, the tendencies, the scratch tools |
| `convert(shot, deleteJson)` | JSON era → `swing.ppsw`, proven first | `LibraryConverter`, `pps_convert_library`, `SessionArchiver` |
| `writeSummarySidecar(shot, PersistedShot)` | the JSON-era `swing_summary.json` | `SessionReviewController` |
| `writeArchiveStub(shot, stub)` | the archive's stub (FILE-SHOT-10) written over the document as given (no summary rebuilt); a read-only document refused, never thinned | `SessionArchiver` |
| `writeTruth(shot, view, json)`, `truth(shot, view)` | the markup truth (FILE-SHOT-12)[^dec83]: `truth.json` (face-on) / `truth_dtl.json`, written whole through `atomicWrite` (indented, as the markup tool always wrote it; never into a folder that has gone) | `MarkupController` |
| `backfillAnalysisContext(shot, AthleteRecordFacts, PersistOptions)` | the step-1 backfill (a one-off migration)[^dec34]: writes `analysisContext` into a document with none, from its legacy fields and the record's height and club set; `Unchanged` when it already has a block or the facts add nothing; `dryRun` honoured | `tools/migrate/backfill_analysis_context` |
| `isDeviceOnly(shot)` | a read: the shot kind (step 1.8), the document's `shotKind` through `readShotKind()`, else, for a document older than the field, today's rule (no streams and a `launchMonitor` block) | `ReanalysisController`, `reanalyseAndPersist` |

Each returns a `WriteResult` (`Written`, `Unchanged`, `NoDocument`, `ReadOnly`, `Failed`, `Locked` +
the reason). `Locked`[^j22] is a folder another writer holds by its advisory lock past the wait — the
session store's writes and the swing allocation answer it, in the lock's own words ("The session … is
being changed by …"), and the live capture's "swing folder could not be created" notice carries those
words instead of "check the library path".

**The session's lock, per write (step 2.8b; brief §4.9, D11).**[^j36-47] Every write above takes the
advisory lock of the shot's **session** folder (`FolderLock`, `<session>/.pinpoint-lock`) for that one write
and lets it go after it[^j11]: a tool's write-back, the other machine, the archiver on another thread and
the app's own edits never write one session at once. A lock another writer holds past the wait (5 s)
refuses the write as `Locked`. The door takes it with `FolderLock::lockOrJoin`: when **this thread** already
holds the session's lock — the archiver, around one swing's stub, conversion or restored review — the write
joins that hold instead of waiting for itself; another thread's hold is never joined. A dry run
(`writeAnalysis`, `backfillAnalysisContext` with `dryRun`) writes nothing and takes no lock, so it leaves no
lock file in a folder it only read. A session folder where no lock file can be made at all (read-only,
vanished) is left to the write, which decides and reports — the allocation's rule. Measured on the share
(15 Nov 2026): a 1.5 MB `swing.ppsw` writes in about 30 ms, so a GUI-thread edit that meets a worker's
write in the same session waits that long, not seconds.

**The writer contract** (R-REC-1)[^dec86], written in the header: capture creates; until step 3 the
interim callers of R-REC-3 write on the shot's behalf; everything else reads.

**Every write refuses a read-only document.** A document saved by a newer build — its
`schema`, or its `analysisContext` block's, newer than this build reads
(`readAnalysisContext`, `recording_header.h`) — is never rewritten: `ReadOnly`, the bytes
untouched (`SwingDocWriter::refusesWrite`, asked by every write before it changes anything).

**The one re-analysis write-back** is `persistReanalysis(write, result, …)` /
`reanalyseAndPersist(write, source, deviceOnly, …)` (`src/Analysis/reanalyse_and_persist.*`, application
ring; refactor step 3.4b-ii): the one rule of what a re-analysis persists, handed to the shot's write —
in the app `Shot::applyAnalysis` (EVT-02, reached through `app`), in a tool its repository's
`writeAnalysis`; it names no repository. The app's `ReanalysisController` analyses on a worker
(`reanalyzeShot`, reading through the `Shot`'s `RecordingSource`) and persists on the GUI thread (so an
edit made meanwhile on the GUI thread is never lost to a write-back that read the document before it); a
shot no `Session` holds is not re-analysed and says so (step 5.1: the controller keeps no reader or write of its
own; the shot is found through the door, `app`, whichever athlete holds it). swinglab's `--write-back`
calls `reanalyseAndPersist` over its own repository and a `RecordingSource` over its own reader. **"Save pose keypoints" is honoured by re-analysis** as by the live save
(`PersistOptions::savePose`, read from the setting at each write; swinglab keeps the app's default, on).

**The whole-shot gate records** (`manifest_gates.h`) are appended once, in the writer, from
the document's integrity blocks on every write.

**Not a shot:** SwingLab's research outputs (its `result.json` and `--doc-blocks` scratch) are written
by the same writer into its own output folder through `SwingStoreRecordingRepository::writeOutsideLibrary`
(by path; not on the interface; never a library folder). `SwingStore::save` / `writeFile` stay public
for tools writing outside the library (`raw_reencode`) and for tests' fixtures; no production writer of
a library swing calls them past the door.

### Capture provenance (additive, 2026-06)

Recorded so SwingLab can filter a corpus by calibration/session provenance and
stop hardcoding assumptions; all additive (absent on legacy swings):

- **Top-level `capture`** — session context (`sessionType` = `SessionController::Type`,
  `shotSource` = `ShotController::Source`, detection sensitivity, the detector
  back-dating latencies) and host provenance (`version`/`gitSha` from the generated
  `pp_version.h`, hostname/platform from `QSysInfo`, `poseBackend` from the first
  camera's live pose-backend label). Built by `captureBlockJson()` inside the one
  header writer (`recording_header.cpp`) — the same call for the degraded
  analysis-only path (`ShotProcessor::buildSynthManifest()` →
  `SwingExporter::analysisOnlyHeader()`), so both record identical metadata.
- **Top-level `analysisContext`** (step 1.2, IM-1) — the analysis context recorded:
  handedness, height, club set, the captured club with its geometry, prior and
  standard length, and the bag's geometry. Written when `SwingExportJob::analysisContext`
  is set (`captureAnalysisContext()`); filling it from the live athlete is step 1.5's
  capture snapshot. Reference: `docs/reference/swing_json_schema.md` `analysisContext`.
- **Per-video-stream `setup`** — `perspective`/`perspectiveName` (CameraInstance
  enum: None 0, DownTheLine 1, FaceOn 2, Other 3), `mirrored`, `fixedInPlace`
  (the camera-side "calibrated" signal), and `ballDetection` — the calibrated
  ball detector's state at capture: `calibrated` (profile loaded), `margin`
  (the profile's validated ball/empty separation), `driftAtCapture`
  (illumination-drift severity, 0 when clean), `calibratedAt` (ISO 8601 UTC,
  null when never calibrated). SwingLab filters the corpus on these
  (`ballCalibrated`/`ballMargin` in corpus.json). When a valid calibration ball
  is present the block also carries the stable ball **position + scale** —
  `center` `[x,y]` and `radiusNorm` (full-frame normalized, radius to frame
  width) + `positionSource` — co-registered with `analysis.club.samples[].head`;
  this is the enabling data for the deferred low-point-ahead-of-ball metric
  (`docs/design/low_point_metric_design.md`), omitted on uncalibrated streams.
- **Per-video-stream `capture` exposure** (additive, 2026-07) — alongside
  `fps_num`/`fps_den`: `exposureUs` (per-stream exposure time, microseconds),
  `exposureSource` (`"measured"` = read from the frame's chunk data on industrial
  cameras, `"derived"` = `1/(2·fps)` fallback where the frame carries no exposure,
  e.g. webcams), and `exposureAuto` (bool, measured streams only — `true` when the
  camera ran auto-exposure so the value is frame-varying). Sourced from
  `CameraFormat::exposure_us`/`exposure_source`; keys omitted when unknown. Consumed
  by the shaft detector to gauge motion-blur/wedge width (`ω · t_exposure`); see
  [`shaft_detection_improvements.md`](../design/shaft_detection_improvements.md)
  (§5.3, `1/(2·fps)` fallback).
- **Per-IMU-stream `device`** — `outputRateHz` (live instance rate — authoritative
  over the registration-time `ImuFormat`), `fusionMode` (device 6/9-axis),
  `orientationFilter` (host fusion: Madgwick/ESKF), `placementSlot` (A/B/C).
- **`analysis.bindings[]` calibration status** — alongside `alignA`/`mountM`:
  `calibrated` (composite gate: `anatCalibrated` AND mount deviation ≤ 15° AND
  gravity error ≤ 25° — `ImuInstance::fullyCalibrated()`), the two gate angles,
  `calibratedAt` (ISO8601 UTC) and `calibAgeSec` at shot time (−1 / empty = never
  calibrated). **The A/M snapshot is *also* duplicated into each IMU stream's
  `device` block**, and that duplication is load-bearing: a swing captured with
  analysis skipped (corpus capture) has no `analysis.bindings` at all, so the
  disk loader rebuilds the IMU→segment bindings from `device` instead.
- **`capture.impactUs`** (window-relative µs) — the re-analysis impact anchor for
  an analysis-skipped swing, which has no `analysis.phases[Impact]` to read.
- **`capture.club`** — club length, shaft type, retro-band centres and hosel
  offset, plus the club-length prior as it stood *before* this shot's update, so
  re-analysis reproduces the exact length fuse the live shot ran.
- **`capture.motionCaptureQuality`** — the offline pose-model tier, so
  re-analysis picks the same model rather than the current setting.

### The ball stream and the baseline sidecar

The face-on camera contributes a `kind: "ball"` stream — deliberately
low-entropy: per-frame `found/x/y/r/conf` plus a single launch step
(`launchTUs`, `launchX`, `launchY`, window-relative). Empty means nothing is
written: ball detection may be off, or this may not be the camera running it.

Separately, each camera may carry its learned **empty-mat baseline** `B` — the
v2 temporal detector's self-calibrated reference (see the shot-detector guide
§7) — as a raw `float32` sidecar plus a JSON key holding its dimensions, ROI,
radius estimate, noise level and (provenance-only) fps. This exists so offline
re-analysis reconstructs the tracker from the baseline the live session actually
learned, instead of self-seeding over a window in which the ball is *already
placed* — which would subtract the ball into the baseline and then never detect
it. An absent blob omits both the sidecar and the key, and re-analysis
self-seeds as before.

### Timestamps in the `analysis` block are window-relative too

Everything under `analysis` — metric series `t_us`, phase events — is normalised
to window-relative µs on write, matching the stream timestamps. Absolute
`nowMicros()` values are meaningless in a file that outlives the process; the
reader re-bases them when reconstructing `analysisDetail`.

### `analysis.kinematicSequence` — one helper, three paths

The kinematic sequence (`docs/design/kinematic_sequence_design.md`, schema
`swing_json_schema.md` § `kinematicSequence`) is an **optional, additive** object
beside `metrics[]`: the ordered peaks over the four segment angular-speed series.
It is serialised by exactly ONE helper, `src/Kinematics/kinematic_sequence_json.h`
(`kinematicSequenceToJson` / `retimeKinematicSequence`), which the document
writer (`swing_doc_analysis.cpp`), the live `analysisDetail` map (`shot_processor.cpp`)
and the reload (`disk_replay_source.cpp`) all call — so a live shot, its
`swing.json` and its reloaded self expose the same shape, and the two absolute
instants inside it (`impactUs`, `nodes[].tPeakUs`) are re-based through the same
window-relative rule as every metric series. Absent when no route produced a
node. Do not add a fourth hand-written copy; extend the helper.

### `swing_summary.json` — a regenerable index sidecar

A Wrist swing's `swing.json` runs to tens of MB (`analysis.pose2d` alone is
~13 MB, retained for replay overlays), and the session picker needs about eight
scalars from each. Parsing every document to draw a list was a visible stall.

`swing_summary.json` (schema `pinpoint.swingsummary/1`, a few hundred bytes)
caches exactly those scalars, guarded by the source document's **size + mtime**
so any out-of-band rewrite — re-analysis, corpus tooling — is detected and the
sidecar regenerated. It is **pure cache**: always safe to delete, always
regenerable.

Two API details matter:

- `SwingDocReader::readSwingSummary(dir, writeSidecar)` prefers the sidecar and
  falls back to a full parse on a miss or stale guard. Pass `writeSidecar=false`
  on any GUI-thread path that must never fat-parse — the caller then gets
  `ok=false` for an un-indexed swing and renders it without detail, rather than
  stalling. The bool is two meanings at once ("may parse" and "may write the
  sidecar"); since refactor step 3.7-0 an overload takes a `SidecarPolicy`
  (`src/Core/sidecar_policy.h`) that names them apart: `CachedOnly` (the bool's
  false), `ParseAndWrite` (its true) and `ParseNoWrite` — parse a miss or a stale
  guard exactly as `ParseAndWrite` does, write nothing. `ParseNoWrite` is the
  parallel run's firing reader (`pinpoint::parallel::doors`), which must leave
  every file of a session to its one writer; `readPhaseGrid` /
  `readLivePhaseGrid` take the same policy. A `.ppsw`'s own summary block is never
  written back by any policy.
- Both the cheap and the full path extract through **one** function
  (`summaryFromRoot`), so they cannot disagree about a score shape, a club
  fallback or a thumbnail name. `SwingSummary::fromSidecar` is provenance the
  parity test uses to prove the cheap path was actually exercised — a bug that
  always fell back would otherwise pass silently while the stall crept back.

### Field sources

| Field | Source |
|---|---|
| `t0_us` | `window.startTimestampUs()` — the monotonic capture clock (`EventBuffer::nowMicros()` epoch) |
| `wallclock` | UTC of the window **start**: a UTC anchor is taken on the UI thread in the same breath as the window end is frozen (`ShotProcessor::beginGather`, so wallclock = monotonic `endTimestampUs()`), then the window duration is subtracted. Accurate to milliseconds. `clock.wallclock + capture.impactUs` is impact on the wall clock. Swings saved before 2026-11-02 anchored at save time instead (after analysis), so theirs are 2–37 s late. |
| `window.end_us` | `endTimestampUs() − t0` |
| video `source.*`, `capture.*` | `CameraFormat` via `window.formatOf()` (`serial` from `FormatDescriptor::device_serial`) |
| video `capture.exposureUs` / `exposureSource` / `exposureAuto` | `CameraFormat::exposure_us` / `exposure_source` — stamped from Spinnaker chunk data (`GetChunkData().GetExposureTime()`) or fps-derived; read back via `exposureSourceFromName()` |
| video `frames.t_us` | Recorded in the encode loop — **written frames only**, so frame *i* in the MP4 corresponds to entry *i* here, always |
| imu `samples` | Decoded `ImuSample` payloads (40 bytes / 10 floats, schema `imu_sample_v2` — accel, gyro, and quaternion all in the raw sensor frame; see [`imu_frame_contract.md`](../design/imu_frame_contract.md)) |
| `athlete`, `session`, `swing` | The `SwingExportJob` (resolved on the UI thread); `athlete.heightM` when known |
| `analysisContext` | `SwingExportJob::analysisContext` (unset = not written) |

The `frames.t_us` index is what lets any downstream tool map output frame *i* to
its true capture instant and cross-reference IMU samples — the MP4 itself carries
no real-time timing (it plays at a fixed 30 fps).

> **`device_serial` note**: `EventBuffer::registerSource()` normalises the
> registrar's `SourceDescriptor::identifier` (serial when present, else opaque
> device id) into `FormatDescriptor::device_serial`, so window readers can
> attribute data to a physical device without access to the descriptor. This was
> added for the exporter and applies to every source.

### Reload & replay — consumer contract

Two readers consume `swing.json` from disk: `SwingDocReader::readSwingJson()`
(carousel reload at startup) and `ShotReplayController` (disk-backed playback).
Both are deliberately **forward-compatible** — they select streams by `kind` and
ignore unknown keys — so the additive blocks below cause no breakage when absent
and no parsing when present-but-unconsumed:

- **Container** — replay feeds `QMediaPlayer` the `file` field verbatim, so
  `mp4`/`mov`/`mkv` all work. `errorOccurred`/`InvalidMedia` are surfaced
  (`replayFailed`) rather than rendering a silent black frame.
- **`encoded` block / downscale** — transparent: `VideoOutput` sizes from the
  decoded frame; no reader reads stream dimensions.
- **`raw` block + `<alias>.raw` sidecar** — **not consumed yet.** A future
  "view raw / reprocess" reader reconstructs frames from
  `pixelFormat`/`width`/`height`/`stride`/`frameBytes`/`count`.
- **IMU streams (`kind:"imu"`)** — **not consumed yet.** When an IMU-reload
  consumer is added it MUST branch on `samples.format`: inline `data` for
  `json`, else load the `imu_<alias>.csv`/`.bin` sidecar named by `file`
  (binary record = LE `i64 t_us` + `f32[10]` accel3/gyro3/quat4).
- **Pose streams (`kind:"pose"`, `pose_movenet_v1`)** — **not consumed yet.**
  The basis for a future skeleton-overlay-on-replay: per-frame `frames.data`
  holds 51 floats (`layout: coco17:y,x,score`, normalised 0..1) against
  `frames.t_us`. Empty today (no producer).

`latestSessionDir(athleteDir)` selects the most recent session by directory mtime, not by
name — session folder names now embed the naming-pattern tokens, so a name sort
no longer tracks recency. It and `sessionDirs(athleteDir)` take the athlete's folder
(`Athlete::folder()`, D4), never a name joined to the library (step 2.7a); `findSwingDirs` lists
through `SwingPaths::swingFolders`.

---

## 7. On-Disk Layout — `SwingPaths`, the session file and `SessionRepository`

```
<athleteLibraryPath>/
  <athlete-folder>/                    athlete level — the sanitised name, chosen ONCE (step 2.3)
    athlete.json                       the athlete's record and bag (step 2.3; FILE-ATH-01)
    <session-folder>_NN/               session level — named by the naming pattern + per-day _NN
      session.json                     the session file: id, athlete, type, status, times (step 2.2)
      .pinpoint-last-swing             the highest swing number ever issued (never reissued)
      .pinpoint-lock                   the advisory lock, only while a write holds it
      archived.json                    only while archived: the marker naming the archive mirror (step 2.6)
      swing_0001/
        <faceOnAlias>.mp4
        <dtlAlias>.mp4
        swing.json
      swing_0002/
        …
```

A folder's name is a property and provenance, never an identity: a session is a `SessionId`, a shot
a `ShotRef` (`src/Identity/identity.h`; information_model.md §5.2). Only two classes read a folder's
name (P7, lint A3): `SwingPaths` (composing names, a swing folder's number) and `SessionIdentity`
(`src/DomainModel/session_identity.*`: a session without a session file — its date, type and `_NN`
under every naming pattern, and D3's name-based `SessionId`).

### The names (static; refactor step 2.1)

| Function | Gives |
|---|---|
| `sessionFolderBase(pattern, day, athleteToken, typeLabel)` | the base before `_NN`: `date-name-type` (default, and any unknown key) `2026-06-05_Mark-Liversedge_Swing`; `date-type-name`; `name-date-type` `Mark-Liversedge_2026-06-05_Swing`; `date-only` `2026-06-05`. An empty type label is `session` |
| `sessionFolderName(base, n)` | `<base>_NN`, at least two digits |
| `nextSessionCounter(athleteDir, base)` | one past the highest `_NN` of that base under the athlete folder |
| `swingFolderName(n)` / `swingNumberOf(name)` | `swing_0007` ↔ 7 (0 for a name that is not a swing folder) |
| `swingFolders(sessionDir)` | the one `swing_*` listing of a session's swing folders |
| `trashPath(path[, where, useOsTrash])` | the OS trash, or the library-local `.pinpoint-trash` on a volume without one; `useOsTrash` false goes straight to the library-local one (the tests' seam, `SessionStore::setOsTrashForTest`) |

### The session file — `session.json` (FILE-SESS-01; step 2.2)

Written only by `SessionStore` (`src/Export/session_store.*`, the adapter of
`src/DomainModel/session_repository.h`), whole, through `atomicWrite`, under the session folder's
`FolderLock` held for that one write. The names are spelled once, in `SessionFiles`
(`src/Export/session_files.h`).

| Key | Meaning |
|---|---|
| `schema` | `"pinpoint.session/1"`, checked on read. A newer one is **read-only**: every write refuses it (`WriteResult::ReadOnly`); a file that cannot be parsed is never overwritten either |
| `sessionId` | a random (v4) uuid minted at Start; for a session older than step 2, D3's v5 id (`SessionIdentity::legacySessionId`) |
| `athleteId` | the athlete's uuid; absent when no athlete was selected. A file naming another athlete than the folder it sits in is logged, never re-owned |
| `type` | by name[^dec88]: `swing`, `wrist`, `grf`, `coach`; absent for none |
| `status` | `inProgress` / `ended` |
| `startedAt`, `endedAt` | ISO 8601 UTC with milliseconds (`2026-11-15T10:04:05.123Z`). `endedAt` is absent while in progress and cleared on resume |
| `endInferred` | `true` when `endedAt` was **inferred**, not observed[^j18]: a session a crash left in progress, ended by the athlete's next Start, gets the latest of its swings' capture ends (`captureEndUtcOf`, `recording_header.h`: `clock.wallclock` plus the window; read through the swing door's `captureEndOf`, which `main.cpp` hands the store); when none of its swings records a capture time it gets no `endedAt` at all. M2 (step 2.8) writes an old session's end by the same rule and marks it the same way. Absent when the end was observed (End) |
| `origin` | `{ "kind": "captured" }` at Start, or `{ "kind": "backfilled", "tool", "version", "at", "read" }` when the start had to be inferred (a session older than step 2 written for the first time by a resume: its start is its swing folders' earliest creation time; M2, step 2.8, writes the rest from the swings' capture times) |

Keys this build does not know are kept on every rewrite. **A read never writes**: a session without a
file is read from its folder's name in memory (`SessionIdentity::fromLegacyFolder`).

### The athlete file — `athlete.json` (FILE-ATH-01; step 2.3)

Written only by `AthleteStore` (`src/Export/athlete_store.*`, the adapter of
`src/DomainModel/athlete_repository.h`), whole, through `atomicWrite`, under the athlete folder's
`FolderLock` for that one write; its name is spelled once, in `AthleteFiles` (`src/Export/athlete_files.h`,
lint A4's owner). Keys: `schema` (`"pinpoint.athlete/1"`, checked on read; a newer one is read-only, an
unparseable one never listed nor overwritten), `athleteId`, `record` (SET-01's keys as stored today, the
bag as `clubs`, the Wrist ★ as `wristReference`, a `ShotRef` string; never `lastSessionAt` /
`sessionCount`, which come from the session index) and, from migration M1, `migratedFrom`. There is **no
`folder` key**: the athlete's folder is the one the file is found in[^dec151].

- **Created** with the athlete: the folder is the sanitised name, `-2`, `-3` … when a folder of that name
  exists or a settings athlete's derived folder would be it. A **rename changes the name only**: the
  folder, its sessions and the next session's folder stay.
- **An athlete created before step 2** has no `athlete.json` until M1 (step 2.8): it is read from this
  machine's settings and its edits go back there; its folder is still `library + sanitise(name)`. No
  read ever writes an `athlete.json` (nor the standard bag a record lacks). An `athlete.json` with the
  same id wins.
- **Deleted** (`Athlete::remove`): refused while one of its sessions is in progress; else the whole
  folder goes to the OS trash, or on a share to `<library>/.pinpoint-trash/<athlete>/<stamp>_<athlete>`
  (housekeeping's layout: one entry). A settings athlete's record is written into its folder first, so
  the trashed folder carries it.
- **The tuning log** `outcome_corrections.jsonl` is appended a line at a time under the same lock
  (`AthleteFiles::appendTuningLine`).
- **One reader** (step 2.8b; R-PERS-1)[^j40]: `AthleteFiles::read(athleteDir)` parses the file and checks its
  envelope (schema, `athleteId`); `AthleteStore` lists the records through it, and the legacy-owner lookup
  (the swing door's `legacyOwnerOf`, `SessionArchiveRun::openSession`)[^j39] reads the owner's id through it.
- The session store composes a session's folder from `Athlete::sessionOwner()`: the athlete folder's own
  name for an athlete with `athlete.json` (`capture::athleteIdentity`).

### The session lifecycle — `SessionRepository` (step 2.2)

| Call | Does | Caller (step 3.4a-ii: through the `Athlete` and its `Session`s, below) |
|---|---|---|
| `start(owner, type, pattern)` | ends any other in-progress session of the athlete (one per athlete), names a new folder (`SwingPaths`, SET-04, the counter past any name taken), makes it, writes the file in progress. A file that cannot be written leaves no folder | `Athlete::startSession`, asked by `ShotProcessor::beginSessionFolder` (QML: the wizard's Start, the toolbar's Capture) and its start on demand |
| `todaysSession(owner, type)` | the athlete's latest session **started today of that type** — a session-file session before a legacy one, then by start time, then its place in the day — the "extend" offer (MD-05)[^dec134] | `Athlete::todaysSession`, asked by `ShotProcessor::todaySessionDir` and the extend |
| `resume(id, type)` | that session in progress again with **the same `SessionId`**; refused for any other day or type; any other in-progress session of the athlete ended first. A refused or failed resume starts a new session instead, and says so (the notice `session.resume.failed`) | `Athlete::resumeSession`, asked by `beginSessionFolder(type, extend = true)` |
| `end(id)` | ended, with `endedAt` — **and nothing more** (step 3.4a-ii): the empty discard[^dec93] (unchanged: only a session this run's `start` made, started by the golfer and not on demand, while it holds no `swing_*` folder; a resumed session never) is `Session::end`'s, which ends the session here and then asks `Athlete::trashSession` (EVT-20)[^dec143] | `Session::end`, asked by `ShotProcessor::endSessionFolder` (a failed write is posted as the notice `session.file.failed`) |
| `enumerate(owner)` | one typed `SessionIndexEntry` per session folder (identity, status, start/end, `hasSessionFile`, `readOnly`, `archived`) with n-of-day by start time (MD-07)[^dec136]; a day with an untimed (legacy) session orders by folder name (step 2.7a). A folder is a session when it has a file, a name this app made, or a swing | `todaysSession` |
| `enumerateFolder(athleteDir, legacyAthlete)` (the store, not the interface; step 2.7a) | the same for a caller that holds an athlete's folder (the diagnostics model, the work-ons controller and their tools, through `SessionDiagnosticsFiles::athleteSessions` / `identityOf`): a legacy session's D3 id from `legacyAthlete` (J-39's owner: `SwingStoreRecordingRepository::legacyOwnerOf`); with none, it is still listed and placed by its name (`SessionIdentity::placeOfLegacyFolder`) with a null id, and never held | the day and place in the day the club baseline, club distances, tendencies and the history columns go by |
| `allocateSwing(id)` | the next swing number (below), under the session's lock; a `ShotRef` and the folder | `Session::addShot`[^dec142], asked by `ShotProcessor::buildSwingExportJob` and, for `LaunchMonitorController`'s standalone shot, by `ShotProcessor::addDeviceOnlyShot` — **one allocator** (D10)[^dec157], so a device-only shot goes into the running session, never a `…_session_NN` folder of its own |
| `archive(id, options, progress)`, `restore(id, progress)` | the whole session to its archive mirror and back (below). **Refused while the session file says in progress** — read afresh, so another computer's live session, or one a crash left open, is refused too (D11)[^dec158]; `archive` also refuses a newer build's session file (read-only) | `ArchiveController` (the Archiving tab, the review's "restore", housekeeping), each job in a `SessionArchiveRun` of its own |
| `trash(id, &where)` | the whole session folder to the OS trash, else the library's own `.pinpoint-trash` (FILE-LIB-01). Refused while its session file says in progress, or while another writer holds its lock (`Locked`; the lock is taken for a moment and let go **before** the move: a lock file must not travel with the folder, and Windows will not move a folder holding an open file). The identity map lets the session go | `SessionReviewController::trashSession` (the drawer's delete, the bulk delete): the row's folder translated once with `openFolder` (D3 from the current athlete for a legacy session) |
| `trashShot(ref, &where)` (step 3.4a)[^j24] | one shot's swing folder to the same trash: its number recorded as the high-water first (`.pinpoint-last-swing`), so it is never issued again[^dec91], then the folder moved (`SwingPaths::trashPath`), both under the session's lock (`Locked` when another writer holds it) | `Session::trashShot` (no app caller yet: the carousel moves to it in step 3.4b) |
| `readDiagnostics(id)` … `writeWorkOns(id, …)` (step 3.4a)[^j42] | the session's diagnostics files (`diagnostics.json`, its backups and set-aside copy, `session_input.json`, `club_baseline.json`, `work_ons.json`) by `SessionId`: the same file work as `SessionDiagnosticsFiles`' folder-keyed functions — both forward to `session_diagnostics_io.h` — on the held session's folder; a session not held reads empty and refuses every write | none yet (step 3.5's `SessionDiagnostics`) |
| `folderPresent(id)` (step 3.4a-ii) | whether the held session's folder is still on disk, whatever its session file's state; reads only | `ShotProcessor`'s D7: an old session's failed End is shown only while its folder is there[^dec140] |
| `shots(id)`, `swingFolder(ref)`, `open(id)`, `openFolder(path, athlete)` | the identity map's reads. A copied session keeps its id (D3, or a copied file): re-read from another folder, the map takes the fresh folder, so a library re-pointed to a copy never hands out the old library's folder — except the session this run started and is still running, which keeps its own | (2.4 onwards) |

**The objects over it (step 3.4a; information_model.md LC-SESSION, LC-SHOT).** The `Athlete` starts, resumes and
trashes its sessions (`startSession`, `todaysSession` / `resumeSession`, `trashSession`: the calls above, after
ending the athlete's session running here, EVT-09) and hands out one `Session` per `SessionId`
(`session(id)`, `sessions()`, and `sessionAt(folder)` — the edge translation `app.sessionAt` asks). A
`Session` (`src/DomainModel/session.*`) adds a shot with `addShot()` — `allocateSwing`, then the `Shot` — and
announces it (EVT-01) only when capture reports its document written (`shotDocumentWritten`); `trashShot`
goes through `trashShot` above (EVT-03); `end()` ends it and, for an empty session this run started, asks
its `Athlete` to trash it (EVT-20), the empty discard's new home. A `Shot` (`shot.*`) is reached only through
its `Session`; its `RecordingSource` reads the Recording through the one `RecordingReader`, which resolves
the folder only through `swingFolder`, so a source whose session is no longer held reads nothing.

**Capture through the objects (step 3.4a-ii).** `ShotProcessor`, the capture service, holds the `Session` it
captures into (`QPointer`; BRIEF §4.8): Start and the extend go through the current `Athlete`, which ends its
session running here first (never discarding); a swing outside a session starts one on demand
(`SessionStart::OnDemand`: never the empty discard's, whatever ends it); End is `Session::end`. Every camera
shot and every standalone launch-monitor shot is added by `Session::addShot(kind, &allocation)` — the
allocation handed back because capture still writes the media into the folder itself (the platform
refactor). EVT-01 is reported when the shot's first document lands: capture's live write
(`onSwingDocWritten`, an analysis-skipped shot's too) and the launch monitor's device-only write (the
`setDocumentWritten` callable, a standalone shot's or the rescue of a camera shot that wrote none); the
phone-clip placeholder (`PpcpClipFiler::ensureSwingDocument`) is not. **D7**[^dec154] — the library
re-pointed mid-session — is the running session's End (its empty discard included; a failed end of an old
session whose folder is still there — a held lock, a read-only or unreadable file; `folderPresent(id)` — is
posted as `session.file.failed`, a gone library is not) and a Start of the same type in the new library; the club is
`SessionController`'s and carries over; the declared focus is the ending `Session`'s, read from it before its End
and declared on the new one from its first shot; a restart that fails keeps it pending (`focusCarried`), and the session
the next swing opens takes it (`AppState::handOverFocus`; `src/App/session_focus.h`; refactor 4-d).
**NO GOLFER CHOSEN** (step 5.1; information_model.md §5.11)[^dec198]: the session is the library's **Unassigned
owner**'s, a full `Session` started, resumed, ended and allocated through its `Athlete` like any other
(`ShotProcessor::captureAthlete` falls back to the composition root's `unassigned` callable, whose record is written
at the first Start in `<library>/(unassigned)/`). A library re-pointed mid-session that does not list the golfer
starts under that library's Unassigned too[^j67]. The swing header records no golfer, as before.

**The folder is fixed for the session's life** (E-01): `allocateSwing` takes only the `SessionId`, so a
rename, midnight or a changed pattern or type cannot move it; nothing relocates (D7). The 0.3 stopgap
that announced a moved folder from `startSwingSave` is gone: `activeSessionDir` changes only when a
session starts or ends. A session whose folder could not be made at Start is started by its first swing
(as before), and a swing outside any session gets a session started on demand, kept while the athlete,
library, day and type stay the same and never discarded.

### The archive — `archive` / `restore` (FILE-SESS-06, FILE-LIB-02; step 2.6)

`SessionStore` checks the session and hands its folder to the file work, `SessionArchiver`
(`src/Export/session_archiver.*`, the store's `SessionArchiving`). An archive runs on a worker, so it
never uses the app's store: `SessionArchiveRun` composes a `SessionStore`, the swing door over it and the
archiver, and `openSession(folder)` is the edge translation — the session file's id, else D3's from the
folder's `athlete.json`, else from the athlete its swings record[^j39]. The store does not link the
archiver (the interface is virtual), so the targets that compile `session_store.cpp` need nothing more.

- **Archive**: every file but the caches (and the raw frames when SET-14 is off — a permanent delete)[^dec137] is copied to `<archive>/<athlete>/<session>/` beside its place as `<name>.partial`, read back
  byte for byte, and only then **renamed over** its place in one step — never remove-then-rename, so there
  is no moment without the file. Then each swing is thinned to its stub through
  `RecordingRepository::writeArchiveStub`. `archived.json` is written through `atomicWrite` **before the
  first swing is thinned** (so a stub never exists without a marker naming the archive that holds its full
  copy; the session reads as archived from then on, and archiving it again is refused) and made final — its
  counts — at the end. A run that stops part-way (a refused stub, another writer's lock) leaves the marker,
  and restoring brings the thinned swings back. **A stub is never copied over a full document**: a swing
  that is already a stub (a marker lost, or a run from before round 2) keeps the mirror's full document, and
  an archive whose stub has no full copy in the mirror is refused, the library untouched. The session file
  is copied with the rest and stays in the library.
- **`archived.json`**: `schema` `"pinpoint.archive/1"`, `location`, `archivedAt`, `swings`, `files`,
  `bytes`, `rawKept`. Read with its schema checked (`SessionArchiver::readMarker`): a newer schema is
  refused as read-only, a file that is not a marker is refused; `isArchived` (what the lists check) is its
  presence.
- **Restore**: each file copied back beside its place, proven, then renamed over the stub. The report
  folder and the session file are never thinned, so the library's copy is kept and the archive's is put
  back only where the library has none. A swing's review is merged three ways right after its document
  lands (step 1.4, H-10): the stub's review is read just before the rename, after the long copy and
  check, and the merge follows straight after. The marker goes last; the archive is kept.
- **The lock** (D11)[^dec158][^j11]: the session's `FolderLock` is taken around each write into the
  library — one conversion, one swing's stub (its read, the stub, the removals), one file put back (with
  its review read and merge), the marker — and let go after it; never across the run. A lock another
  writer holds past 5 s stops the run as `Locked`: an archive before any swing is thinned leaves the
  library as it was, a restore stays marked archived. The copies into the mirror take no lock. The
  swing file's own writes take the same lock since step 2.8b, so the app's review edits on other threads
  are shut out while a document lands and is merged (H-10 closed); the archiver's stub, conversion and
  merge are written inside its hold and join it (`lockOrJoin`).
- **A tool meets a live session** (D11; step 2.8b for the five below)[^dec158]: every tool that writes into a
  session asks `SessionStore::fileSaysInProgress(sessionFolder)` (its `session.json` read afresh; nothing written)
  and refuses one in progress — being recorded here or on the other machine — with
  `SessionStore::inProgressRefusal`, unless `--allow-in-progress` (for one a crash left open): `regrade_ledger`
  (the session skipped, exit 1), `swinglab_run --write-back`, `lm_repair`, `pps_convert_library` (the swing
  refused, the run goes on, exit 1), `backfill_analysis_context --write` and `pps_migrate_step2 --write`. A dry run
  or a read-only mode (`--out`) is never refused.
- **Re-checked per swing** (step 2.8b)[^j52]: under each swing's hold, before it is thinned, the archive
  reads its marker again (still there, still naming this archive) and asks the store again whether the
  session is live (its session file read afresh). A marker gone or naming another archive — another
  computer restored the session meanwhile — stops the archive at once, and the marker is not written again;
  a session resumed meanwhile stops it with the marker kept over the stubs made. A restore removes the
  marker last, under the lock, and only when no swing in the library is a stub; a swing thinned again by
  another run meanwhile keeps the marker, and the restore says so. So no swing is ever a stub without a
  marker.

### Swing allocation

One past the highest number the session has ever issued — the highest `swing_*` folder present or
the recorded `.pinpoint-last-swing`, whichever is higher — formatted by `swingFolderName`, probed
upward past any collision, then created with `mkpath` (`SessionStore::allocateSwing`, under the
session's `FolderLock`; a lock held by another writer past 5 s refuses the allocation). A trashed swing records its number on the way
out, so a deleted swing's number is never issued again (#56). The high-water file is written whole
through the one atomic write (`src/Core/atomic_write.h`, into the existing session folder only).

### Fallbacks

- Empty `athleteLibraryPath` → `<AppDataLocation>/swings`, with a `ppWarn`.
- Empty athlete name → uuid, then `"unknown"`.

### `sanitise()`

One shared rule for athlete folders and camera-alias filenames: trim → any char
outside `[A-Za-z0-9._-]` becomes `-` → collapse separator runs → strip
leading/trailing `-`/`.` → truncate to 64 → `"unknown"` if nothing survives.
Camera filename collisions are deduped `-2`, `-3`, … in `buildSwingExportJob()`.

---

## 8. The Encoder — `IVideoEncoder` / `FfmpegVideoEncoder`

### Output contract

- Container from the file extension (`mp4`/`mov`/`mkv`), codec **libx264** or
  **libx265** (selected by name at construction), `AV_PIX_FMT_YUV420P`. `profile
  High` is set for libx264 only (libx265 rejects that profile name).
- Colour tagged **BT.709, limited range** (`color_primaries`/`color_trc`/
  `colorspace = BT709`, `color_range = MPEG`), and the BGR→YUV conversion uses
  matching ITU-709 coefficients via `sws_setColorspaceDetails` — tags and pixels
  agree.
- `movflags +faststart` — the `moov` atom precedes `mdat`, so clips stream/scrub
  immediately.
- `time_base = {1, 30}`, **pts = sequential frame index** (0, 1, 2, …). This is
  the entire slow-motion mechanism: source frame count == output frame count, and
  the clip plays at 30 fps regardless of capture rate.
- CRF from settings, preset `medium`. Encode speed is deliberately not optimised —
  the save hides under the 20–30 s analysis/replay pause.

### Lifecycle

`open()` follows the canonical libav sequence (`avformat_alloc_output_context2` →
`avcodec_find_encoder_by_name(m_codecName)` → `avformat_new_stream` →
`avcodec_alloc_context3` → `avcodec_open2` → `avcodec_parameters_from_context` →
`avio_open` → `avformat_write_header`). One `AVFrame` and one `AVPacket` are
allocated once and reused for every frame. Every libav return code is checked; any
failure logs via `ppError` (with `av_strerror` — the `av_err2str` macro is C-only),
runs the idempotent `cleanup()`, and returns `false`. `cleanup()` is also the
destructor path, so a mid-encode failure can never leak contexts; the partial file
is left truncated and the caller treats the export as failed.

### ⚠ The packet-duration fix — do not remove

```cpp
// drainPackets():
m_pkt->duration = 1;     // one tick of time_base {1, out_fps}
av_packet_rescale_ts(m_pkt, m_enc->time_base, m_stream->time_base);
```

The libx264 wrapper emits packets with `duration = 0`. With B-frame reordering,
the mov muxer then computes the track's edit list ending at the **last DTS**
instead of last pts + duration. The final reordered frame falls outside the edit
list, gets the `AV_PKT_FLAG_DISCARD` flag on demux, and **every decoder silently
drops the last frame of every clip** — the file still reports the full
`nb_frames`, so nothing obvious fails. Found via
`ffprobe -count_frames` (90 packets, 89 decoded frames). Since every frame here is
exactly one time-base tick, stamping the duration explicitly is always correct.

### Header hygiene

`ffmpeg_video_encoder.h` **forward-declares** the libav types (`AVFormatContext`
etc.) and includes no FFmpeg headers — only the `.cpp` does (inside
`extern "C"`). The header therefore parses in any TU regardless of whether FFmpeg
dev headers are installed.

---

## 9. Configuration — AppSettings Keys

The exporter reuses existing keys — **do not invent parallel settings**.

| Key | Used as |
|---|---|
| `general/athleteLibraryPath` | Library root for `SwingPaths` |
| `storage/sessionNamingPattern` | Composes the session-folder name (`date`/athlete/session-type tokens) in `SwingPaths::sessionFolderBase` |
| `storage/videoCodec` | Factory key (`"h264"` → libx264, `"h265"` → libx265; other → h264 fallback). `FfmpegVideoEncoder` |
| `storage/videoQuality` | CRF: `low`=28 (**default** since 23 Sept 2026, shown as "Compact"), `medium`=23 ("Standard"), `high`=18, `lossless`=0 |
| `storage/videoContainer` | `mp4`/`mov`/`mkv` — sets the clip file extension; the muxer is guessed from it |
| `storage/videoResolutionMode` | `native`/`half`/`1080p`/`4k` — export-time downscale, never upscales |
| `storage/saveImuStreams` | Gates the IMU streams in `swing.json` |
| `storage/imuDataFormat` | `json` (inline) / `csv` / `binary` — csv/binary write an `imu_<alias>.<ext>` sidecar |
| `storage/saveRawFrames` | Dumps undecoded sensor payloads to an `<alias>.raw` sidecar (+ a `raw` block per video stream) |
| `storage/skipAnalysisForRawCapture` | With `saveRawFrames` on: skip analysis and the replay entirely, export frames only. Corpus-capture mode — each shot lands instantly and is re-analysed in bulk later (shot-analyzer guide §4/§9a) |
| `storage/savePoseKeypoints` | Gates serialising `kind:"pose"` streams — the exporter only *writes* pose carried on the job; no producer yet (empty today) |
| `general/motionCaptureQuality` | Recorded into `capture` so re-analysis picks the same offline pose model |
| `camera/alias` (`cameraAlias()` map) | MP4 filenames, keyed by `cameraKey()` |
| `imu/alias` (`imuAlias()` map) | IMU stream aliases, matched by serial/device-id |

All keys are read on the UI thread in `buildSwingExportJob()` via the single
shared `AppSettings` instance (see the AppSettings rule in `CLAUDE.md`).

---

## 10. Threading Model

**`QtConcurrent::run` + a `QFutureWatcher` value member** — not a `QThread`
worker. The save is a one-shot job returning a value, which is exactly QFuture's
shape; a `moveToThread` worker would add a class, lifetime management, and
queued-signal plumbing for zero benefit. (The `TtsController` QThread pattern is
for long-lived stateful services.)

Two properties of `QFutureWatcher` carry the safety argument:

1. `finished` is delivered **on the watcher's thread** (the UI thread), so all
   swing-lifecycle state is single-threaded and mutex-free.
2. `finished` cannot fire before the worker lambda has **fully returned**, so by
   the time `maybeFinishSwing()` destroys the window, no code can still be reading
   through it.

What runs where:

| UI thread | Worker thread |
|---|---|
| Window capture, replay | `SwingExporter::run()` |
| `buildSwingExportJob()` — all QSettings/controller access, alias resolution, directory creation | Zero-copy window reads, demosaic, encode, JSON build/write |
| Window destruction, resume decision | `ppInfo`/`ppWarn`/`ppError` (thread-safe) |

The worker uses only `const` methods of `SwingWindow` over frozen data, so it can
read concurrently with the replay's `payloadOf()` calls on the UI thread.

The job runs on the **global** QThreadPool. That is fine for one encode at a time;
if other QtConcurrent users ever appear, consider a dedicated single-thread pool.

---

## 11. Build System — FFmpeg Detection

FFmpeg is a **real link dependency** here (libavcodec, libavformat, libavutil,
libswscale via `pkg_check_modules(FFMPEG IMPORTED_TARGET …)`), distinct from the
dlsym-based log suppression in `pp_debug.cpp`, which targets Qt Multimedia's
*bundled* libavutil and needs no link. GPL builds of FFmpeg (libx264) are fine —
the project is GPLv2+.

- **macOS**: `brew --prefix ffmpeg` is prepended to `PKG_CONFIG_PATH` (mirrors the
  OpenCV handling).
- **Windows**: probes `C:/ffmpeg`, `C:/tools/ffmpeg`; override with
  `-DFFMPEG_DIR=`.
- **Linux**: distro dev packages (`libavcodec-dev libavformat-dev libavutil-dev
  libswscale-dev`).

`video_encoder.*`, `swing_paths.*`, `swing_exporter.*` are **always compiled**;
only `ffmpeg_video_encoder.*` and the `HAVE_FFMPEG` define are conditional. Without
FFmpeg the app builds and runs normally — `makeVideoEncoder()` returns `nullptr`
and each save fails fast with a logged "no encoder available".

### Coexistence with Qt's bundled FFmpeg

The process ends up with **two** FFmpeg generations loaded: ours from the system
(e.g. `libavutil.so.60`) and Qt Multimedia's bundled copy (e.g. `.so.59`, loaded
lazily by its media plugin). This is safe: the SONAMEs differ and FFmpeg exports
versioned symbols (`av_log@LIBAVUTIL_59` vs `…@LIBAVUTIL_60`), so the dynamic
linker keeps them apart. Two consequences:

- CMake's `PkgConfig::FFMPEG` imported target links by **absolute path** — never
  replace it with `-L`/`-l` flags, which could resolve against Qt's lib dir.
- `av_log_set_level()` must be called on **our** instance (the encoder does this
  in `open()`); the dlsym suppression in `pp_debug.cpp` only reaches Qt's copy.

---

## 12. Extending the Module

### Adding a new stream kind to `swing.json`

Append another object to `streams[]` with a new `kind` (e.g. `"pose"`,
`"metrics"`, `"launch"`). Keep the established shape: `kind`, `alias`, `source`,
a units/schema block, and parallel `t_us` + data arrays. Do **not** bump the
schema version for additive changes — readers ignore unknown kinds by contract.

### Bulk export — `SwingZipExporter`

The carousel's "export selected shots" action is a separate worker
(`swing_zip_exporter.{h,cpp}`, QML context property `swingExporter`), built on the
same discipline as `SwingExporter`: a self-contained value job, a stateless static
worker on `QtConcurrent`, the result delivered back on the UI thread via
`QFutureWatcher`.

- `camerasForShots(dirs)` reads each shot's `swing.json` and unions its
  `kind=="video"` streams, de-duped by file in first-seen order — that list is the
  model for the options sheet's camera checkboxes. An unreadable `swing.json` is
  skipped rather than fatal.
- `exportShots(dirs, selectedVideoFiles, includeJson)` builds `~/<session>.zip`.
  Extracting it reproduces the session: a top-level `<session>/` with one
  `swing_NNNN/` per shot. Each shot contributes `thumb.jpg` (always), the selected
  videos, and — only with the JSON toggle on — `swing.json` plus the IMU sidecars.

Two things it deliberately does *not* do. **Raw frame sidecars are never
included** — they are the largest artifact by far and useless outside this
machine. And **the archive is never held in memory**: it streams straight to the
on-disk file via `QZipWriter`'s filename constructor, so peak RAM is one member
at a time (bounded by one swing's MP4, since `*.raw` is excluded). A session can
be multiple GB; building the zip in a `QByteArray` would not survive it.

### Adding a new encoder backend (`h265`, ProRes, hardware)

1. Implement `IVideoEncoder` in a new `src/Export/<name>_encoder.{h,cpp}`.
2. Add its factory key in `video_encoder.cpp`.
3. Gate compilation in CMake the same way `ffmpeg_video_encoder` is gated.
The exporter and `CameraManager` need no changes — codec selection already flows
from `storage/videoCodec`.

### The frame-restoration hook

Denoise/sharpen goes in exactly one place — the marked hook between demosaic and
`writeBgr()` in `swing_exporter.cpp`. It receives the reused BGR scratch and must
write its result back into a BGR mat of the same dimensions. Record the stage in
`processing.restorer` (currently always `"none"`).

### Per-camera parallel encode

Possible future optimisation: the window is read-only and each camera writes its
own file, so cameras could encode in parallel at the cost of one BGR scratch + one
encoder per worker. Bound the pool and measure peak RSS first.

### Not yet handled (v1 scope)

12/16-bit Bayer, `MJPEG`/`H264_NAL` passthrough, an actual **pose producer**
(the exporter serialises `poseStreams` but nothing fills them yet), launch-monitor
streams, any UI beyond the two status signals.

---

## 13. Common Mistakes

### Resuming the buffer while a save is in flight

The worker reads ring memory zero-copy; `resume()` clears the rings — instant
use-after-free. All resume paths must go through `CameraManager::resumeBuffer()`,
which refuses while `m_swingWindow` is live. If you add a new resume path, route
it through `resumeBuffer()` — never call `m_eventBuffer->resume()` directly from
swing-adjacent code.

### Destroying `m_swingWindow` anywhere except the join

`stopReplay()` used to reset the window; it deliberately no longer does. If you
reintroduce a reset on the replay path, a save that outlives the replay will read
freed memory. The only legitimate places are `maybeFinishSwing()` and the
teardown paths that first call `m_swingSaveWatcher.waitForFinished()`.

### Recording a `t_us` entry for a skipped frame

If `payloadOf()` returns null data, skip the entry **entirely** — both the encode
and the timestamp. Recording the timestamp but not the frame (or vice versa)
breaks the *MP4 frame i == `t_us[i]`* invariant that all downstream tooling
relies on.

### Copying window payloads "to be safe"

The whole design exists to avoid this. The window is frozen — wrap the payload in
a `cv::Mat` header and demosaic straight into the reused scratch. If you find
yourself calling `.clone()` or building a `QByteArray` from a payload on this
path, stop.

### Touching `AppSettings` / controllers from the worker

Everything the worker needs is in the `SwingExportJob`. If a new feature needs
another setting, resolve it in `buildSwingExportJob()` on the UI thread and add a
field to the job struct.

### Removing the `pkt->duration` stamp

See §8 — without it every exported clip silently loses its final frame. The
failure is invisible to ffprobe's metadata (`nb_frames` still reports the full
count); only `-count_frames` or an actual decode reveals it.

### Re-deriving the Bayer mapping

The OpenCV `COLOR_Bayer*` constants do not match the sensor pattern names
one-to-one. The table in `frame_decode.h` mirrors the live-view mapping
deliberately — if exported colours ever diverge from the on-screen image, compare
against `camera_instance.cpp`'s PixelFormat→pattern switch before touching it.
And note the table is now **shared with the shot analyzer**: a change here
changes what analysis sees, not just what is written to disk. That is the point
of the extraction — the two can never diverge — but it means the blast radius is
wider than the file's name suggests.

### Adding an analysis input without persisting it

Offline re-analysis reads `swing.json` and never `AppSettings`, so anything that
affects analysis and lives only in settings makes a re-analysed swing disagree
with the live one — silently, with the same score field showing a different
number. Club geometry, the length prior, the quality tier, the IMU A/M snapshot,
the ball ROI and the empty-mat baseline are all in the file for exactly this
reason. Add yours to `SwingExportJob` and the manifest at the same time you add
it to `ShotAnalysisJob`.

### Blocking the UI thread on the watcher outside teardown

`waitForFinished()` is acceptable only where correctness demands it
(`setSelected()` deregistration, the destructor). Everywhere else the join is
asynchronous by design — a long save must not freeze the UI.

---

## 14. File Map

```
src/Export/
├── video_encoder.h             VideoEncoderConfig, IVideoEncoder (abstract),
│                               makeVideoEncoder() declaration. No libav includes.
├── video_encoder.cpp           Factory — the only file with HAVE_FFMPEG ifdefs
│
├── ffmpeg_video_encoder.h      Concrete encoder; libav types forward-declared
├── ffmpeg_video_encoder.cpp    libav call sequence, BT.709 sws, RAII cleanup,
│                               the pkt->duration fix (§8)
│
├── frame_decode.h              DemosaicPlan / frameGeometry / decodeFrame — the
├── frame_decode.cpp              ONE decode path, shared with the analyzer (§3)
│
├── swing_paths.h               SwingPaths — the library's names (static), the one
├── swing_paths.cpp               swing_* listing, sanitise(), trashPath()
│
├── session_store.h             SessionStore — SessionRepository's adapter: session.json,
├── session_store.cpp             Start / resume / End, D7, enumerate, the one allocator (§7)
├── session_files.h             SessionFiles — the session folder's own file names, and the
│                                 swing counter's reader and writer (header-only)
├── session_diagnostics_io.h    the diagnostics files' file work (diagnostics.json,
│                                 session_input.json, club_baseline.json, work_ons.json),
│                                 shared by SessionDiagnosticsFiles and the store's id-keyed
│                                 methods (header-only; step 3.4a, J-42)
├── session_archiver.h          SessionArchiver — the archive's file work behind
├── session_archiver.cpp          SessionRepository::archive / restore; SessionArchiveRun (§7)
│
├── swing_exporter.h            SwingExportJob/Camera/Result, the stream structs,
├── swing_exporter.cpp            SwingExporter::run + analysisOnlyHeader(); per-camera
│                                 encode loop, thumbnail, IMU/ball streams,
│                                 baseline sidecar, manifest builder
│
├── swing_doc.h                 SwingDocWriter/Reader — the unified swing.json
│                                 (the writes private since step 1.4), and the
│                                 regenerable swing_summary.json index sidecar (§6)
├── swing_doc_writer.cpp        SwingDocWriter, refusesWrite, saveDocument, the
│                                 launch monitor and integrity blocks
├── swing_store_repository.h/.cpp  THE ONE DOOR (step 1.4): RecordingRepository on
│                                 the store; the only friend of the writer
├── swing_doc_reader.cpp        SwingDocReader, the summary block and sidecar,
│                                 dataWarningDetailFrom & the other read helpers
├── swing_doc_analysis.cpp      the `analysis` block, composed from the per-block
│                                 serialisers beside their producers (serializeAnalysis,
│                                 analysisFromDocument, analysisReadOriginUs)
├── analysis_path_json.h        analysis.path's writer + typed reader
├── swing_doc_internal.h        the helpers the three files share (not public)
│
├── recording_header.h/.cpp     THE ONE HEADER WRITER (step 1.2): kSwingDocSchema, the
│                                 header blocks, captureBlockJson, the analysisContext
│                                 block's writer/reader, the legacy context, clubReviewOf
├── recording_loader.h/.cpp     the swing folder → the Recording (disk-backed window,
│                                 the Recording, the club review); moved out of
│                                 swing_reanalyzer.cpp in step 1.2
│
├── swing_zip_exporter.h        Bulk "export selected shots to a zip" (§12)
├── swing_zip_exporter.cpp
│
└── tests/
    ├── recording_repository_test.cpp  Every RecordingRepository method, by ShotRef;
    │                               savePose; the empty-document guard; every write
    │                               refuses a newer document; a legacy ShotRef
    │                               resolves; truth; the stub; ANALYSIS SUITE
    ├── swing_doc_test_writes.h  the tests' writes, through the repository, and
    │                               FolderShots (any test folder is a shot)
    ├── recording_store_test.cpp  The Recording through the disk, the one header,
    │                               the legacy context, read-only; ANALYSIS SUITE
    └── swing_doc_test.cpp      Document round-trip; BUILT BY THE ANALYSIS SUITE

src/DomainModel/
└── recording_repository.h      RecordingRepository — the interface (step 1.4)

src/Analysis/
└── reanalyse_and_persist.{h,cpp}  the one re-analysis write-back (step 1.4)

src/Gui/shot/
└── shot_processor.{h,cpp}      startSwingSave(), buildSwingExportJob(),
                                onSwingSaveFinished(), maybeJoin(), finishShot(),
                                finishNowBlocking() — the teardown stop-barrier

src/Buffer/
└── event_buffer.cpp            registerSource() normalises identifier →
                                FormatDescriptor::device_serial (§6 note)
```

### Verifying an export by hand

```bash
# Stream/colour/profile checks — expect h264 High, yuv420p, bt709×3, tv range, 30/1:
ffprobe -v error -show_streams <swing_dir>/<alias>.mp4 \
  | grep -E "codec_name|profile|pix_fmt|color_|r_frame_rate|nb_frames"

# The check that actually catches the last-frame bug — decoded count must equal
# nb_frames and the json frames.count:
ffprobe -v error -count_frames -select_streams v \
  -show_entries stream=nb_frames,nb_read_frames <swing_dir>/<alias>.mp4

# faststart: moov must precede mdat
python3 -c "d=open('<file>','rb').read(); print(d.find(b'moov') < d.find(b'mdat'))"
```

Cross-check `swing.json`: `frames.count` == decoded frame count per video stream,
`t_us` arrays non-negative and monotonic, IMU rows are 10 floats, and
`samples.count` == `imuSampleCount()` for each IMU source.

---

## 15. Testing

This module's automated coverage is the **`swing.json` document round-trip**. The
encoder and frame pipeline have no unit test — they need a live FFmpeg build and
real captured frames — and are validated by hand (see
[Verifying an export by hand](#verifying-an-export-by-hand) above).

### `swing_doc_test` — the unified document round-trip

`src/Export/tests/swing_doc_test.cpp` exercises `SwingDocWriter` / `SwingDocReader`
(§6) end to end against a temp directory. Its writes go through the repository
(`swing_doc_test_writes.h` keeps the call shapes), as every test's do since step 1.4:

- **Unified write** — `writeSwingJson(rawManifest, analysis)` composes the exporter's
  raw `pinpoint.swing/1` tree with a `SwingAnalysis` into one document, bumping the
  on-disk schema to **`pinpoint.swing/2`** and adding an inline `analysis` block
  (`pinpoint.analysis/2`: `score`, `tier`, metric `series` with `t_us`/`value`/
  `phaseSamples`, and `phases`). The raw `streams` / `swing` blocks survive untouched.
- **Reader** — `readSwingJson()` reconstructs a `PersistedShot` (ordinal, `hasVideo`,
  thumbnail path, `hh:mm:ss` from `clock.wallclock`, `score`, flat impact `metrics`,
  and `analysisDetail` for the replay graph) — the same shapes `ShotProcessor`
  produces live, so a reloaded shot is indistinguishable from a freshly captured one.
- **Review write-through** — `updateReview(rating, note)` lands an additive `review`
  block without disturbing the raw/analysis blocks, replaces (does not append) on
  rewrite, clamps the rating to 0–5, and fails harmlessly (returns `false`) when no
  `swing.json` exists.
- **Raw-only** — `writeSwingJson(…, nullptr)` omits the `analysis` block while still
  emitting `pinpoint.swing/2` (export-succeeded-but-analysis-failed degrades cleanly).

- **Summary sidecar** — `swing_summary.json` is written on a full read, reused on
  the next one, and **invalidated by the source document's size/mtime**
  (`SwingStore::sameDocumentStamp`: a copy that kept the mtime to the whole second still
  matches, issue #174). The test
  also asserts the cheap path was genuinely taken (`SwingSummary::fromSidecar`)
  and that an orphaned sidecar copied into a directory with no `swing.json` is
  rejected rather than trusted.

### `recording_repository_test` — the one door

`src/Export/tests/recording_repository_test.cpp` runs every `RecordingRepository` method on a
temp folder: `create` (the review seed), `savePose` on `create` and `writeAnalysis`, the
write-back (the analysis replaced, capture / streams / review / LM rows carried, both
verdicts from this pass), the empty-document guard, `editReview`'s merge, `completeCapture`
(follow-club, never over a hand-set club; a stream arrival), the launch-monitor-only shot
(rows current / stale / dry run), `convert` and the JSON-era sidecar, **every write refusing a
newer document** (document schema or `analysisContext` block; the bytes unchanged), and
`reanalyseAndPersist` with the analyzer stubbed. Keyed by `ShotRef` (step 2.4): a test's temp folder is
a shot through the tests' own `FolderShots` session repository (`swing_doc_test_writes.h`: any folder,
its own ShotRef). Then, through the **real** `SessionStore`: **a legacy session's ShotRef resolves**
(D3's id from the caller's athlete, and from the swing's own `athlete.uuid` in a fresh store — what a
tool does — writing no session file), a session-file session's allocation read back from its folder, a
non-swing folder with no ShotRef; **the markup truth round trip** (both views, identical, indented,
atomic, no folder re-made); **the stub through the door** (written as given; a newer document refused,
bytes unchanged; no document → `NoDocument`); **a legacy session's id from its owning athlete** (the
folder's `athlete.json` on the tool path, the resolver on the app path, the header only with no owner)[^j39];
**the two new review keys** (written, read back,
unchanged on repeat, removed when cleared; an old document reads none); and an unresolved shot
(`Failed` "not found", every read empty).

### `session_repository_test` — the session file and the one allocator (step 2.2)

`src/Export/tests/session_repository_test.cpp` runs `SessionStore` on temp libraries: Start writes
`session.json` with every key (a newer schema read-only and byte-identical after every refused write;
unknown keys kept); the naming patterns; allocation, and a number never reissued after deletes, a
restart or a trashed swing (these sections moved here from `swing_paths_test` with the allocator);
resume keeps the `SessionId`, only for today's session of the same type; a legacy folder read with
D3's id and never written by a read, its first write a backfilled file; one in-progress session per
athlete; enumerate's typed entries and n-of-day under `name-date-type`; End, which never discards (the
discard is `Session::end`'s: `session_test` S3, S12), the resumed morning session included; the folder
fixed past midnight and a rename; D7's store half (the old session ended where it is, or left in progress
when unreachable — `folderPresent` true while its folder is there, even with an unreadable session file:
the rule the capture service reports by — and
a new session of the same type in the new library); a held `FolderLock` refusing the write, as `Locked` in the lock's own words, for
End and for an allocation (step 2.4)[^j22]; and (step 2.4, S) a swing folder to its `ShotRef` and back — an
allocation's, a fresh store's read of a session file, a legacy swing's D3 id with no file written, a held
session keeping its listed id, no ShotRef for a non-swing folder — `swingFolder` read on a worker thread
while the owner thread re-lists the sessions, and a discarded session's folder no longer published; and
(step 2.8b, J)[^j18] a crash-left session ended by the next Start: `endedAt` the latest of its swings'
capture ends (a swing with none passed over), `endInferred` set and read back; resumed, then ended by End,
the mark gone; no capture time anywhere, no end and no mark.
`swing_paths_test` keeps the names, `sanitise` and the trash.

### `session_archiver_test` — the archive and the session trash (step 2.6)

`src/Export/tests/session_archiver_test.cpp` (Analysis suite) archives and restores temp sessions through
`SessionRepository` by `SessionId`, each in a `SessionArchiveRun`: every copy byte-identical, the stubs
still listing, a review made while archived and one made during the restore both kept, raw dropped only
when asked, a cancel leaving the library as it was; and (step 2.6) the session file copied, the library's
kept on restore, a lost one put back; `archived.json`'s schema written and checked (a newer one
read-only, a broken one refused); a session in progress neither archived nor trashed; the lock held
around each file put back and free between files, a held lock stopping an archive (nothing thinned) and
a restore (nothing put back) as `Locked`; (round 2) a newer build's swing 2 of 2 refused with the marker
still written and swing 1 restored whole; the marker present before the first swing is thinned; a lock taken
at swing 2 leaving the session marked archived, a re-archive refused, and — the marker lost — a re-archive
keeping the archive's full documents (to another archive: refused); every file put back by one rename with the file present the
moment before, and a failed rename leaving the stub byte for byte; the index's `archived`; the trash
refused under a held lock, then the whole folder moved and let go; (step 2.8b) an edit from another thread
while the restore puts a document back refused `Locked`, the restore's own merge joining its hold, an edit
with the session free written and one under a tool's lock refused[^j36-47]; the marker removed (a restore
elsewhere) before swing 2 stopping the archive without re-marking, a session resumed meanwhile stopping it
with the marker kept, and a swing thinned again during a restore keeping the marker[^j52]. (The trash case
puts one small temp folder in the macOS Trash, as `session_repository_test`'s discards do.)

### `athlete_repository_test` — the athlete file and its one door (step 2.3)

`src/Gui/tests/athlete_repository_test.cpp` (Gui suite) runs `AthleteStore`, the `Athlete` and
`AthleteController` on a temp library and a scratch INI: `athlete.json` at creation (no `folder` key, no
derived keys), the folder numbered when taken, one instance per id; a rename keeps the folder and its
sessions; a settings athlete read, never written by a read, its edits back to settings, an `athlete.json`
with its id winning; delete refused while a session is in progress, else the whole folder in the
library's trash in housekeeping's layout, a settings athlete's record carried; an edit is
`recordChanged`, never `currentAthleteChanged`; a newer file read-only, an unparseable one left alone;
the Wrist ★ on the record as a `ShotRef`; the tuning log's append and a held lock refusing it; the
picker's activity from the index.

There is **no Export test project**. `swing_doc_test.cpp` lives in
`src/Export/tests/` but is compiled by the **Analysis suite**
(`src/Analysis/tests/CMakeLists.txt`) because it links `swing_analysis.h`:

```bash
cmake -S src/Analysis/tests -B build/analysis-tests
cmake --build build/analysis-tests -j6
ctest --test-dir build/analysis-tests -R swing_doc_test --output-on-failure
```

It is self-contained (own `main()` + `check()`, no GoogleTest). See
`docs/developer/testing_developer_guide.md` for the umbrella build and the
per-suite conventions, and the EventBuffer suite for the borrowed-window contract
this module relies on.

---

*For the EventBuffer contracts this module depends on — pause/resume semantics,
SwingWindow lifetime, zero-copy read safety — see
`docs/developer/event_buffer_developer_guide.md`, in particular §9 (SwingWindow) and §14
(Common Mistakes).*

## Notes

[^dec160]: IM-S2-2, DEC-160.
[^dec90]: DEC-90.
[^dec150]: DEC-150.
[^j39]: J-39; confirmed by the Solution Architect, 15 Nov 2026.
[^j33]: J-33.
[^dec83]: DEC-83.
[^dec34]: DEC-34.
[^j22]: J-22.
[^j36-47]: J-36, J-47.
[^j11]: J-11.
[^dec86]: DEC-86.
[^dec88]: DEC-88.
[^j18]: J-18: Solution Architect, 15 Nov 2026; `build/refactor/step-2/JUDGEMENT-ANSWERS.md`.
[^dec151]: DEC-151.
[^j40]: J-40.
[^dec134]: MD-05: DEC-134.
[^dec93]: DEC-93.
[^dec143]: DEC-143.
[^dec136]: MD-07: DEC-136.
[^dec142]: DEC-142.
[^dec157]: D10: DEC-157.
[^dec158]: D11: DEC-158.
[^j24]: J-24.
[^dec91]: DEC-91.
[^j42]: J-42.
[^dec140]: DEC-140.
[^dec154]: DEC-154.
[^dec198]: DEC-198.
[^j67]: J-67.
[^dec137]: DEC-137.
[^j52]: J-52.
