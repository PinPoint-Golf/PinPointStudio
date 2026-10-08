# FLIR camera settings — how PPS manages them

Brief, 8 Oct 2026. Covers the FLIR (Chameleon3 / Blackfly S) machine-vision
cameras through both backends: Spinnaker (Windows, the cabin) and Aravis
(macOS). Written after a cabin incident (§6); the fix is §5, implemented
8 Oct 2026.

## 1. The rule that matters

**A FLIR camera keeps its node values for as long as it has power.** They
survive `DeInit()`, a PPS restart, and a different application opening the
camera. Whatever PPS leaves on the camera, the next connection inherits, and
so does Swing Catalyst or SpinView.

So PPS follows one principle: **every node PPS ever writes is written on every
connect**, either to the requested value or to a known neutral (camera-auto)
value. "Not requested" never means "inherit whatever was last there".

## 2. The settings model

### 2.1 Persisted settings

All per-camera settings live in `AppSettings` (`src/Gui/app/app_settings.h`),
each a `QVariantMap` keyed by the camera key `description|serial`:

| Property | QSettings key | Meaning | Reaches the camera? |
|---|---|---|---|
| `cameraPerspective` | `camera/perspective` | role: None, Face-on, DTL, Impact, Other. Impact is exclusive (`CameraManager::assignPerspective` strips it from every other camera) | decides which of the rows below are applied |
| `cameraRoi` | `camera/roi` | normalised crop `{x,y,w,h}` | yes, every role (hardware ROI) |
| `cameraTargetFps` | `camera/targetFps` | frame rate | **Impact only** (see §2.3) |
| `cameraExposureUs` | `camera/exposureUs` | locked exposure, µs | Impact only |
| `cameraTuning` | `camera/tuning` | `{gainDb, gamma, strobe, viewGain, note}` | `gainDb`, `gamma`, `strobe`: Impact only. `viewGain` is a display stretch on the tile and replay, `note` is stamped into the clip; neither touches the camera |
| `cameraTriggerMode` | `camera/triggerMode` | free-run / trigger | no local backend reads it |
| `cameraExcluded`, `cameraAlias`, `cameraIsMirrored` | | housekeeping | no |

Impact defaults are `CameraInstance::kImpactDefault*`
(`src/Gui/cameras/camera_instance.h`): exposure 70 µs, gain 12 dB, gamma 0.7,
view gain 1.0. `CamerasPanel.qml` mirrors them for its chips.

### 2.2 The Settings → Cameras controls

| Control | Visible for | Writes | Takes effect |
|---|---|---|---|
| VIEW (role) combo | every camera | `cameraPerspective`. First assignment to Impact also seeds the default exposure and the recommended impact mode | next `CameraInstance` (§3.2) |
| Crop editor | every camera | `cameraRoi` | next connect |
| FRAME RATE chips | non-Impact cameras | `cameraTargetFps` | PPCP phones only (resolved to a declared capture profile in `CameraManager::setTargetFps`); a no-op for Spinnaker/Aravis |
| IMPACT MODE chips | Impact | `cameraRoi` + `cameraTargetFps` together (crop × rate from the enumerate-time probe) | next connect |
| EXPOSURE chips | Impact | `cameraExposureUs` | live if streaming (`applyLiveTuning`), else next connect |
| GAIN, GAMMA | Impact | `cameraTuning.gainDb/gamma` | live if streaming, else next connect |
| VIEW gain, NOTE | Impact | `cameraTuning.viewGain/note` | display / clip metadata only |
| STROBE | Impact | `cameraTuning.strobe` | next connect |

There is deliberately no exposure, gain or gamma control for a non-Impact
camera: those run on camera auto (§5).

### 2.3 What the backend is handed

`CameraInstance`'s constructor (`camera_instance.cpp`) freezes the values for
the instance's lifetime:

- The crop is loaded for every instance (a settings preview instance reads it
  for the crop editor, but only buffer-backed instances apply it).
- The rate, exposure, gain, gamma and strobe are loaded **only** when the
  instance has a buffer **and** `cameraPerspective == Impact`. Otherwise they
  stay at the "not requested" sentinels: rate `0`, exposure `0`, gain `-1`,
  gamma `0`, strobe `false`. A settings preview gets none of them, by design:
  the crop editor should show a bright full-sensor picture
  (`impact_camera_design.md` §10.2–10.3).

`primeBackend()` pushes them into the `VideoInputBase` setters
(`setCropRegion`, `setCaptureRate`, `setExposureUs`, `setGainDb`, `setGamma`,
`setStrobeOutput`) on the backend thread, immediately before `start()`. Every
sentinel means **camera auto**: the backend writes the neutral value of §5.

## 3. Lifecycle and code flow

### 3.1 Enumeration — `VideoInputFactory` (`video_input_factory.cpp`)

At enumerate time each Spinnaker camera is opened briefly (`Init` / `DeInit`,
no acquisition) to fill `CameraCapabilities`:

- `AcquisitionFrameRateEnable(d)` is set true for the reads, if it was false,
  and restored afterwards; with it false the rate max is exposure-limited.
- The ROI is reset to the full sensor (it survives app runs), then
  `InvalidateNodes()`, before reading the full-frame rate max (a stale crop
  reads 611.7 instead of 150.7).
- The impact crops (640×240, 640×320, 1280×240, 320×240, 640×480) are each
  written and the advertised rate max read; they become
  `extensions["impact.modes"]` and the IMPACT MODE chips. The ROI is left at
  full frame.

Nothing else is written. Exposure, gain and gamma from a previous owner are
still on the camera at this point; `start()` deals with them.

### 3.2 Instance creation and role changes

`CameraManager::setSelected` creates a buffer-backed `CameraInstance`
(`createController`) when a camera is selected, and destroys it when it is
deselected. `assignPerspective` only changes the **live instance's role
label**: the tuning members were frozen by the constructor. A new role reaches
the camera's nodes when the next instance is constructed and connects
(deselect and reselect the camera, or restart PPS).

### 3.3 Connect — `VideoInputSpinnaker::start()`

Order (all before `BeginAcquisition()`; the device nodes are read-only once
streaming):

| # | Node(s) | When | Value |
|---|---|---|---|
| 1 | `AcquisitionMode` | always | Continuous |
| 2 | `PixelFormat` | always | first available of BayerRG8, BayerBG8, BayerGR8, BayerGB8, BGR8, RGB8Packed, Mono8 |
| 3 | `OffsetX/Y`, `Width/Height` (`applySpinnakerRoi`) | always | crop snapped down to the increments, or full sensor |
| 4 | **camera-auto block** (§5) | per node, when not requested | `ExposureAuto`, `GainAuto`, gamma enable, Line1, frame-rate auto → neutral; then `InvalidateNodes()` and one `tuning:` log line |
| 5 | `ExposureAuto`, `ExposureTime` | `m_exposureUs > 0` | Off, `InvalidateNodes()`, value clamped to the node range |
| 6 | `GainAuto`, `Gain` (`writeTuningNodes`) | `m_gainDb >= 0` | Off, value; read back into `m_appliedGainDb` |
| 7 | `GammaEnable(d)`, `Gamma` (`writeTuningNodes`) | `m_gamma > 0` | true, value; read back into `m_appliedGamma` |
| 8 | `LineSelector`, `LineMode`, `LineSource` | `m_strobe` | Line1, Output, ExposureActive |
| 9 | `AcquisitionFrameRateAuto`, `AcquisitionFrameRateEnable(d)`, `AcquisitionFrameRate` | `m_captureFps > 0` | Off, true, `InvalidateNodes()`, value clamped to the max **at this ROI** |
| 10 | `StreamBufferCountMode/Manual` (**TL stream** node map) | always | Manual, 40 |
| 11 | `ChunkModeActive`, `ChunkSelector=ExposureTime`, `ChunkEnable` | always | on: each frame carries the exposure actually applied |
| 12 | `ExposureAuto` (read) | always | cached as `m_exposureAuto` (0 = locked, 1 = auto) and tagged on every frame |
| 13 | `TimestampLatch` | always | best of `kClockLatchBrackets` host/camera clock brackets, seeds the frame-time mapping |

**⚠ ORDER: ROI → exposure → rate**, with `InvalidateNodes()` between
(`impact_camera_design.md` §3.1). The rate's max depends on the ROI but a
`Width/Height` write does not invalidate its cache; `ExposureTime` is
read-only until `ExposureAuto` is Off and its access mode is cached the same
way; and a rate the exposure cannot fit silently clamps the exposure down, so
exposure goes first. The camera-auto block sits after the ROI and before step
5; it only touches nodes whose locked value is absent, so it never fights
steps 5–9.

### 3.4 While streaming — `applyLiveTuning()`

`CamerasPanel` calls `CameraInstance::applyLiveTuning(exposureUs, gainDb,
gamma)` when an Impact knob turns on a streaming camera. The instance records
the value first (the next connect primes what the operator last saw), then
`VideoInputSpinnaker::applyLiveTuning` runs `writeTuningNodes()` — the same
code as steps 5–7, so a connect and a knob turn agree by construction. A
non-positive exposure, negative gain or non-positive gamma is skipped.

### 3.5 Read-backs and provenance

- Per frame: the chunk `ExposureTime` (µs) and the `m_exposureAuto` flag ride
  on every `RawVideoFrame`, and the exposure midpoint corrects the frame
  timestamp.
- Per clip: `ShotProcessor` records the Impact camera's gain and gamma as
  `appliedGainDb()` / `appliedGamma()` (what the camera held after the clamp),
  falling back to the requested value, plus the strobe flag, view gain and
  note from `cameraTuning`.
- `queryCapabilities()` on a live camera reads nodes only, apart from the same
  brief `AcquisitionFrameRateEnable` toggle as the enumerate probe.

### 3.6 Disconnect — `stop()`

`EndAcquisition()`, join the capture loop, then — only if no other handle is
streaming the device — restore the ROI to full frame and `DeInit()`. The ROI
restore exists because a stale crop poisons the `Width/Height` `GetMax()`
reads of the next capability query. **Nothing else is restored on
disconnect**: the reset lives in `start()` (§5), which also covers a crash, a
pulled cable, or another application.

### 3.7 Aravis — `VideoInputAravis::start()` (macOS)

The same model through the Aravis API:

1. Region: offsets to 0, then Width, Height, OffsetX, OffsetY (crop snapped
   down, or full sensor). `stop()` restores full frame.
2. Camera-auto block (§5), with a `tuning:` log line.
3. Exposure (`arv_camera_set_exposure_time_auto(OFF)` + value), then gain and
   gamma (`writeTuning`), then the strobe, when requested.
4. Frame rate: always written — the requested rate, or **60 fps**.
5. Pixel format Mono8; 10 stream buffers.

`applyLiveTuning()` runs `writeTuning()`, as on Spinnaker. Not
hardware-verified (source comment, 2026-09-15).

## 4. Node spellings by firmware

Every write is guarded by `IsAvailable/IsWritable` (Aravis: a `GError`), so a
node a camera lacks is skipped silently — and that is how a write to the wrong
spelling hides. PPS writes both spellings wherever they differ.

| Concern | Blackfly S (SFNC) | Chameleon3 (CM3-U3-13Y3C, fw 1.13.3.00) |
|---|---|---|
| Manual frame rate | `AcquisitionFrameRateEnable` | `AcquisitionFrameRateEnabled` + `AcquisitionFrameRateAuto` (no SFNC node at all) |
| Gamma enable | `GammaEnable` | `GammaEnabled` |
| `ExposureTime` | writable once `ExposureAuto` = Off | the same; the access mode is cached until `InvalidateNodes()` |
| Line1 | opto-isolated output | dedicated output (no `Input` mode entry) |
| Stream buffer count | TL stream node map | TL stream node map |
| Clock latch value | `TimestampLatchValue` | `Timestamp` |

## 5. Neutral values on connect (the fix)

Implemented in both backends' `start()`, right after the ROI write and before
the Impact writes:

| Node | Written when | Neutral value |
|---|---|---|
| `ExposureAuto` | exposure not requested (`<= 0`) | Continuous |
| `GainAuto` | gain not requested (`< 0`) | Continuous |
| `GammaEnable` / `GammaEnabled` | gamma not requested (`<= 0`) | false. If neither enable is writable, `Gamma = 1.0` (Spinnaker only) |
| `LineSelector = Line1` → `LineMode` | strobe off | Input; where Line1 has no Input mode (Chameleon3), `LineSource = Off` instead |
| `AcquisitionFrameRateEnable(d)`, `AcquisitionFrameRateAuto` | rate not requested (`<= 0`) | false, Continuous. Spinnaker only: Aravis always writes a rate |

Then one `InvalidateNodes()` (the `ExposureAuto` change flips `ExposureTime`'s
cached access mode, and the rate nodes cache their range) and one log line per
connect naming the state the camera was put in and what was reset, e.g.:

```
[VideoInputSpinnaker] tuning: camera auto ExposureAuto=Continuous GainAuto=Continuous Gamma=off Line1=Off FrameRate=auto
[VideoInputSpinnaker] tuning: impact locked Line1=Off
```

`impact locked` means an exposure was requested; the list names only the
neutral writes that landed (an Impact camera without the strobe still has
Line1 released). `(nothing reset)` means every node was either locked or
absent.

Design notes:

- **In `start()`, not `stop()`**, so a crash, a pulled cable or another
  application leaving the camera in a bad state is also covered.
- **Neutral first, Impact values after.** The ORDER rule of §3.3 is
  untouched, and the two blocks never write the same node.
- **The neutral is camera auto, not a measured value.** For a full-frame
  Chameleon3 indoors this is the familiar ~150 fps with auto exposure up to
  the frame period. A camera configured by hand in SpinView for a non-Impact
  role is overridden on connect; PPS owns these nodes.

Rejected alternative: `UserSetSelector=Default` + `UserSetLoad` on every
connect. It is a complete reset, but it slows every connect and resets nodes
PPS sets itself (pixel format, buffers, chunk data). It also depends on
`UserSetDefault` not pointing at a user set someone saved.

### Cabin test

1. Put the Chameleon3 in Impact and stream it. The log shows
   `tuning: impact locked`, `Exposure locked: ~70 us`, the gain, gamma and
   the high frame rate; the picture is the dark impact strip.
2. Without power-cycling, switch it to DTL and reselect it (§3.2). The
   picture must be normally exposed. The log shows
   `tuning: camera auto ExposureAuto=Continuous GainAuto=Continuous ...
   FrameRate=auto` and no `Exposure locked` / `Frame rate` lines.
3. Switch it back to Impact. The locked exposure, gain, gamma and rate must
   all be re-applied, and the strobe must fire if it is enabled.
4. Optional: leave the camera in Impact, open and close SpinView, then
   reconnect in PPS. The settings must be the same as in step 3.

## 6. The defect that prompted this (cabin, early October 2026)

A Chameleon3 was moved from **Impact** to **Down-the-line** without a power
cycle. The DTL picture was nearly black, and no setting in PPS cleared it.
Opening Swing Catalyst, which resets the camera, fixed it.

Cause: of the Impact settings, only the ROI was ever undone. On the DTL
connection every other Impact setting was "leave alone", so the camera kept:

- `ExposureAuto = Off`, `ExposureTime ≈ 70 µs`. This is the darkness: a
  full-frame indoor exposure is in the milliseconds.
- `GainAuto = Off`, `Gain = 12 dB`, `Gamma = 0.7`.
- `AcquisitionFrameRateAuto = Off` with the rate enabled. 691 fps is
  unattainable at full frame, so the camera ran at its own ceiling.
- Line1 as the strobe output, if the strobe had been on.

PPS shows no exposure control for a DTL camera, so the user had no way to
recover from inside the app. Aravis had the same gap for exposure, gain, gamma
and strobe; its rate was already safe because it always writes 60 fps.
