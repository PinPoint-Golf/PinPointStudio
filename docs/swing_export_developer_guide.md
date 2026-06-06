# Pinpoint Swing Export — Developer Guide

**Audience**: Developers working on or integrating with the Pinpoint application
**Location**: `src/Export/` (plus integration in `src/Gui/camera_manager.{h,cpp}`)
**Language**: C++20, Qt 6
**Status**: v1 — video + IMU streams implemented; pose/metrics/launch-monitor streams are schema-ready but not produced

---

## Contents

1. [What Swing Export Is](#1-what-swing-export-is)
2. [Where It Fits in Pinpoint](#2-where-it-fits-in-pinpoint)
3. [Core Concepts](#3-core-concepts)
4. [The Export Pipeline — Frame by Frame](#4-the-export-pipeline--frame-by-frame)
5. [Resume Gating — the CameraManager Join](#5-resume-gating--the-cameramanager-join)
6. [The `swing.json` Sidecar](#6-the-swingjson-sidecar)
7. [On-Disk Layout — `SwingPaths`](#7-on-disk-layout--swingpaths)
8. [The Encoder — `IVideoEncoder` / `FfmpegH264Encoder`](#8-the-encoder--ivideoencoder--ffmpegh264encoder)
9. [Configuration — AppSettings Keys](#9-configuration--appsettings-keys)
10. [Threading Model](#10-threading-model)
11. [Build System — FFmpeg Detection](#11-build-system--ffmpeg-detection)
12. [Extending the Module](#12-extending-the-module)
13. [Common Mistakes](#13-common-mistakes)
14. [File Map](#14-file-map)

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
struct SwingExportCamera { SourceId sourceId; QString alias, fileName; };
struct SwingExportJob {
    QString swingDir;                          // absolute, already created
    QString swingId;       int swingIndex;
    std::vector<SwingExportCamera> cameras;    // aliases sanitised + deduped
    QHash<QString, QString> imuAliasBySerial;
    QString codec;  int crf;  bool saveImu;
    QString athleteName, athleteUuid, handedness, sessionId;
    QDateTime wallclockAnchorUtc;              // see §6, clock block
};
```

`SwingExporter::run(const SwingWindow&, const SwingExportJob&)` is a **stateless
static function** — all scratch is local, the result comes back as a
`SwingExportResult{ok, swingDir, error}` value through the `QFuture`.

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

`"h264"` → `FfmpegH264Encoder` when built with `HAVE_FFMPEG`; anything else (or a
no-FFmpeg build) → `nullptr`, and the save fails gracefully with a logged error.
`video_encoder.cpp` is the **only** file in the module with `HAVE_FFMPEG` ifdefs.

### The borrowed window

The worker receives the `SwingWindow` **by const reference** — it does not own it,
copy it, or extend its life. The contract is enforced entirely by `CameraManager`:
the window is destroyed only in `maybeFinishSwing()`, on the UI thread, strictly
after the worker has returned (guaranteed by `QFutureWatcher::finished` ordering —
see §10).

### Zero-copy reads

Per frame, the worker calls `window.payloadOf(entry)` and wraps the returned bytes
in a `cv::Mat` header **without copying**:

```cpp
const cv::Mat raw(srcH, srcW, plan.matType,
                  const_cast<std::byte*>(handle.data), stride);
cv::cvtColor(raw, bgr, plan.cvtCode);    // demosaic into the ONE reused scratch
```

While the buffer is `Paused` the producers are stopped, so the bytes are stable —
this is the same guarantee the replay path relies on.

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

Schema identifier: **`pinpoint.swing/1`**. Built with `QJsonDocument`, written
atomically with `QSaveFile`. All stream timestamps are **relative µs offsets from
`t0`** (small, exact); `t0_us` is the absolute capture-clock value. A new stream
type later is just another element of `streams[]` — readers must ignore unknown
`kind`s.

```json
{
  "schema": "pinpoint.swing/1",
  "swing":   { "index": 7, "id": "swing_0007" },
  "athlete": { "name": "Mark Liversedge", "uuid": "…", "handedness": "Right" },
  "session": { "dir": "2026-06-05_session-01" },
  "clock":   { "t0_us": 173456789012345, "unit": "us",
               "wallclock": "2026-06-05T14:22:01.234Z" },
  "window":  { "start_us": 0, "end_us": 4980000 },
  "streams": [
    {
      "kind": "video", "alias": "faceOn", "file": "faceOn.mp4",
      "source": { "serial": "…", "pixelFormat": "BayerRG8",
                  "width": 1936, "height": 1096 },
      "capture":  { "fps_num": 150, "fps_den": 1 },
      "playback": { "fps": 30 },
      "processing": { "demosaic": "EA", "restorer": "none" },
      "frames": { "count": 742, "t_us": [0, 6671, 13342] }
    },
    {
      "kind": "imu", "alias": "leadWrist", "schema": "imu_sample_v1",
      "source": { "serial": "…" },
      "units": { "accel": "g", "gyro": "deg/s", "quat": "wxyz" },
      "samples": { "count": 498, "t_us": [0, 5000, 10000],
                   "data": [[0.01, 0.99, 0.02, 1.2, 0.3, -0.1, 1, 0, 0, 0]] }
    }
  ]
}
```

### Field sources

| Field | Source |
|---|---|
| `t0_us` | `window.startTimestampUs()` — the monotonic capture clock (`EventBuffer::nowMicros()` epoch) |
| `wallclock` | Honest approximation: a UTC anchor is snapshotted on the UI thread right after window capture (when wallclock ≈ monotonic `endTimestampUs()`), then the window duration is subtracted. Accurate to milliseconds. |
| `window.end_us` | `endTimestampUs() − t0` |
| video `source.*`, `capture.*` | `CameraFormat` via `window.formatOf()` (`serial` from `FormatDescriptor::device_serial`) |
| video `frames.t_us` | Recorded in the encode loop — **written frames only**, so frame *i* in the MP4 corresponds to entry *i* here, always |
| imu `samples` | Decoded `ImuSample` payloads (40 bytes, schema `imu_sample_v1`) |
| `athlete`, `session`, `swing` | The `SwingExportJob` (resolved on the UI thread) |

The `frames.t_us` index is what lets any downstream tool map output frame *i* to
its true capture instant and cross-reference IMU samples — the MP4 itself carries
no real-time timing (it plays at a fixed 30 fps).

> **`device_serial` note**: `EventBuffer::registerSource()` normalises the
> registrar's `SourceDescriptor::identifier` (serial when present, else opaque
> device id) into `FormatDescriptor::device_serial`, so window readers can
> attribute data to a physical device without access to the descriptor. This was
> added for the exporter and applies to every source.

---

## 7. On-Disk Layout — `SwingPaths`

```
<athleteLibraryPath>/
  <athlete-name-sanitised>/            athlete level
    <YYYY-MM-DD_session-NN>/           session level — date + per-day index
      swing_0001/
        <faceOnAlias>.mp4
        <dtlAlias>.mp4
        swing.json
      swing_0002/
        …
```

### Session allocation policy

The session folder is allocated **lazily on the first save and cached for the
lifetime of the `SwingPaths` instance** — i.e. per app run. All swings recorded
today by the same athlete into the same library share one `session-NN`, across
Stop/Start cycles. The cache key is `(athleteUuid, todayISO, libraryRoot)`; a new
day, an athlete switch, or a library-root change reallocates. `NN` is
`max(existing for today) + 1`, zero-padded to two digits.

### Swing allocation

Count of existing `swing_*` directories + 1, formatted `swing_%04d`, probed upward
until unused (gaps and collisions are tolerated), then created with `mkpath`.

### Fallbacks

- Empty `athleteLibraryPath` → `<AppDataLocation>/swings`, with a `ppWarn`.
- Empty athlete name → uuid, then `"unknown"`.

### `sanitise()`

One shared rule for athlete folders and camera-alias filenames: trim → any char
outside `[A-Za-z0-9._-]` becomes `-` → collapse separator runs → strip
leading/trailing `-`/`.` → truncate to 64 → `"unknown"` if nothing survives.
Camera filename collisions are deduped `-2`, `-3`, … in `buildSwingExportJob()`.

---

## 8. The Encoder — `IVideoEncoder` / `FfmpegH264Encoder`

### Output contract

- Container MP4, codec **libx264**, `AV_PIX_FMT_YUV420P`, profile High.
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
`avcodec_find_encoder_by_name("libx264")` → `avformat_new_stream` →
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

`ffmpeg_h264_encoder.h` **forward-declares** the libav types (`AVFormatContext`
etc.) and includes no FFmpeg headers — only the `.cpp` does (inside
`extern "C"`). The header therefore parses in any TU regardless of whether FFmpeg
dev headers are installed.

---

## 9. Configuration — AppSettings Keys

The exporter reuses existing keys — **do not invent parallel settings**.

| Key | Used as |
|---|---|
| `general/athleteLibraryPath` | Library root for `SwingPaths` |
| `storage/videoCodec` | Factory key (`"h264"` → `FfmpegH264Encoder`) |
| `storage/videoQuality` | CRF: `low`=28, `medium`=23, `high`=18, `lossless`=0 |
| `storage/videoContainer` | `"mp4"` (informational in v1 — the encoder writes MP4) |
| `storage/videoResolutionMode` | `"native"` in v1 — encode at source resolution |
| `storage/saveImuStreams` | Gates the IMU streams in `swing.json` |
| `storage/saveRawFrames` | **Off and unhandled in v1** |
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
only `ffmpeg_h264_encoder.*` and the `HAVE_FFMPEG` define are conditional. Without
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

### Adding a new encoder backend (`h265`, ProRes, hardware)

1. Implement `IVideoEncoder` in a new `src/Export/<name>_encoder.{h,cpp}`.
2. Add its factory key in `video_encoder.cpp`.
3. Gate compilation in CMake the same way `ffmpeg_h264_encoder` is gated.
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

`saveRawFrames`, 12/16-bit Bayer, `MJPEG`/`H264_NAL` passthrough, wiring the
session-folder name to `storage/sessionNamingPattern`, any UI beyond the two
status signals.

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
one-to-one. The exporter's table mirrors the live-view mapping deliberately — if
exported colours ever diverge from the on-screen image, compare against
`camera_instance.cpp`'s PixelFormat→pattern switch before touching the exporter.

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
├── ffmpeg_h264_encoder.h       Concrete encoder; libav types forward-declared
├── ffmpeg_h264_encoder.cpp     libav call sequence, BT.709 sws, RAII cleanup,
│                               the pkt->duration fix (§8)
│
├── swing_paths.h               SwingPaths — session/swing dir allocation + cache
├── swing_paths.cpp             sanitise(), per-app-run session policy
│
├── swing_exporter.h            SwingExportJob/Camera/Result, SwingExporter::run
└── swing_exporter.cpp          Per-camera encode loop, DemosaicPlan table,
                                restorer hook, IMU streams, swing.json builder

src/Gui/
├── camera_manager.h            Export members, signals, join declarations
└── camera_manager.cpp          startSwingSave(), buildSwingExportJob(),
                                onSwingSaveFinished(), maybeFinishSwing(),
                                guards on resume/teardown paths (§5)

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

*For the EventBuffer contracts this module depends on — pause/resume semantics,
SwingWindow lifetime, zero-copy read safety — see
`docs/event_buffer_developer_guide.md`, in particular §9 (SwingWindow) and §14
(Common Mistakes).*
