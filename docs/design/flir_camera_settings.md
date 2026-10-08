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

All before `BeginAcquisition()` (the device nodes are read-only once
streaming). Steps 3–9 are `src/Video/spinnaker_settings.cpp` — `applyRoi()`
and `applyConnectSettings()` — which the app and the hardware probe (§7)
share, so what the probe measures is what the app does.

| # | Node(s) | Impact | Every other role |
|---|---|---|---|
| 1 | `AcquisitionMode` | Continuous | Continuous |
| 2 | `PixelFormat` | first available of BayerRG8, BayerBG8, BayerGR8, BayerGB8, BGR8, RGB8Packed, Mono8 | same |
| 3 | `OffsetX/Y`, `Width/Height` | the crop, snapped down to the increments | the saved crop, or full sensor |
| 4 | `ExposureAuto`, `ExposureTime` | Off, the locked value (default 70 µs) | Continuous |
| 5 | `GainAuto`, `Gain` / `AutoGainUpperLimit` | Off, the locked value (default 12 dB) | Continuous, upper limit = the sensor's max (18.06 dB on the Chameleon3) |
| 6 | `GammaEnable(d)`, `Gamma` | on, the value | off (or `Gamma` 1.0 where there is no enable). **Absent on the Chameleon3 in raw Bayer** |
| 7 | `LineSelector=Line1` → `LineMode`, `LineSource` | Output, ExposureActive if the strobe is on, else released | released: `LineMode=Input`, else `LineSource=Off`, else `LineSource=UserOutput1` with `UserOutputValue` false (the Chameleon3) |
| 8 | `AcquisitionFrameRateEnable(d)` → `AcquisitionFrameRateAuto` → `AcquisitionFrameRate` | true, Off, the requested rate clamped to the max **at this ROI** (691 → 611.7 at 640×240) | true, Off, the max at this ROI (150.7 full frame) |
| 9 | `AutoExposureTimeUpperLimit` | — | the frame period (6,574 µs at 150.7 fps) |
| 10 | `StreamBufferCountMode/Manual` (**TL stream** node map) | Manual, 40 | same |
| 11 | `ChunkModeActive`, `ChunkSelector=ExposureTime`, `ChunkEnable` | on: each frame carries the exposure actually applied | same |
| 12 | `ExposureAuto` (read) | cached as `m_exposureAuto` (0 = locked, 1 = auto), tagged on every frame | same |
| 13 | `TimestampLatch` | best of `kClockLatchBrackets` host/camera clock brackets | same |

The role changes **values, not the procedure**: every node in steps 3–9 is
written on every connect. One log line per connect names them all, e.g.

```
[VideoInputSpinnaker] settings: camera auto ExposureAuto=Continuous GainAuto=Continuous AutoGainUpperLimit=18.1dB Gamma=absent Line1=UserOutput1(low) FrameRate=150.7fps(max, max 150.7) AutoExposureTimeUpperLimit=6573.6us
[VideoInputSpinnaker] settings: impact locked ExposureTime=70.2us Gain=12.0dB Gamma=absent Line1=UserOutput1(low) FrameRate=611.7fps(requested 691.0, max 611.7)
```

and about a second into streaming a `settled:` line reports what the camera
actually chose (chunk exposure, `Gain`, `AcquisitionFrameRate`). A node the
writer could not set is a `ppWarn`.

**⚠ ORDER: ROI → exposure → gain → gamma → Line1 → rate → auto-exposure
limit**, with `InvalidateNodes()` between (`impact_camera_design.md` §3.1):

- the rate's max depends on the ROI, but a `Width/Height` write does not
  invalidate its cache;
- `ExposureTime` is read-only until `ExposureAuto` is Off, and its access mode
  is cached the same way;
- a rate a locked exposure cannot fit silently clamps the exposure down, so
  the exposure goes before the rate;
- the auto-exposure limit is the frame period, so it goes after the rate;
- **on the Chameleon3 `AcquisitionFrameRateAuto` is read-only while
  `AcquisitionFrameRateEnabled` is false**, so the enable goes first. Written
  the other way round, a camera another application left on auto rate kept it
  (found by the probe, §7).

**⚠ The rate is never auto.** On the Chameleon3 an auto rate lets auto
exposure stretch the frame period: 19 fps with a ~50 ms exposure in the
cabin on 8 Oct, where the camera should run 150.7.

### 3.4 While streaming — `applyLiveTuning()`

`CamerasPanel` calls `CameraInstance::applyLiveTuning(exposureUs, gainDb,
gamma)` when an Impact knob turns on a streaming camera. The instance records
the value first (the next connect primes what the operator last saw), then
`VideoInputSpinnaker::applyLiveTuning` runs `writeTuningNodes()`, which writes
the same nodes as steps 4–6 for the members given. A non-positive exposure,
negative gain or non-positive gamma is skipped (a live re-tune changes one
knob; the full set is written on the next connect). The rate, crop and
strobe are connect-only.

### 3.5 Read-backs and provenance

- Per frame: the chunk `ExposureTime` (µs) and the `m_exposureAuto` flag ride
  on every `RawVideoFrame`, and the exposure midpoint corrects the frame
  timestamp.
- Per connect: the `settings:` line, and ~1 s in the `settled:` line (§3.3).
- Per clip: `ShotProcessor` records the Impact camera's gain and gamma as
  `appliedGainDb()` / `appliedGamma()` (what the camera held after the clamp),
  falling back to the requested value, plus the strobe flag, view gain and
  note from `cameraTuning`. On a Chameleon3 the applied gamma is 0: the node
  is absent in raw Bayer, so the GAMMA chips change nothing on that camera.
- `queryCapabilities()` on a live camera reads nodes only, apart from the same
  brief `AcquisitionFrameRateEnable` toggle as the enumerate probe.

### 3.6 Disconnect — `stop()`

`EndAcquisition()`, join the capture loop, then — only if no other handle is
streaming the device — restore the ROI to full frame and `DeInit()`. The ROI
restore exists because a stale crop poisons the `Width/Height` `GetMax()`
reads of the next capability query. **Nothing else is restored on
disconnect**: every connect writes every node (§5), which also covers a
crash, a pulled cable, or another application.

### 3.7 Aravis — `VideoInputAravis::start()` (macOS)

The same procedure through the Aravis API, in the same order:

1. Region: offsets to 0, then Width, Height, OffsetX, OffsetY (crop snapped
   down, or full sensor). `stop()` restores full frame.
2. Exposure auto Off + value, or Continuous; gain auto Off + value, or
   Continuous with `AutoGainUpperLimit` / `AutoExposureGainUpperLimit` at the
   gain maximum; gamma enable + value, or off; Line1 strobe or released
   (Input, Off, or `UserOutput1` low); the rate — requested, or the maximum
   from `arv_camera_get_frame_rate_bounds` (it used to be a fixed 60 fps);
   the auto-exposure limit at the frame period. One `settings:` log line.
3. Pixel format Mono8; 10 stream buffers.

`applyLiveTuning()` runs `writeTuning()`, as on Spinnaker. **Not
hardware-verified**: no FLIR camera has been on the Mac since this was
written (2026-10-08), and the probe (§7) is Spinnaker-only.

## 4. Node spellings by firmware

Every write is guarded by `IsAvailable/IsWritable` (Aravis: a `GError`), so a
node a camera lacks is skipped silently — and that is how a write to the wrong
spelling hides. PPS writes both spellings wherever they differ. The
Chameleon3 column is read back by the probe (§7) on both studio cameras.

| Concern | Blackfly S (SFNC) | Chameleon3 (CM3-U3-13Y3C, fw 1.13.3.00) |
|---|---|---|
| Manual frame rate | `AcquisitionFrameRateEnable` | `AcquisitionFrameRateEnabled` + `AcquisitionFrameRateAuto` (no SFNC node). `…Auto` is read-only while `…Enabled` is false |
| Auto-exposure limit | `AutoExposureExposureTimeUpperLimit` | `AutoExposureTimeUpperLimit` |
| Auto-gain limit | `AutoExposureGainUpperLimit` | `AutoGainUpperLimit` (max 18.06 dB) |
| Gamma | `GammaEnable`, `Gamma` | **absent** (`GammaEnable`, `GammaEnabled`, `Gamma`) in BayerRG8 |
| `ExposureTime` | writable once `ExposureAuto` = Off | the same; the access mode is cached until `InvalidateNodes()` |
| Line1 | opto-isolated output | output only: `LineMode` reads Output with no Input entry; `LineSource` is one of ExposureActive, ExternalTriggerActive, UserOutput1 — no Off |
| Stream buffer count | TL stream node map | TL stream node map |
| Clock latch value | `TimestampLatchValue` | `Timestamp` |

## 5. The rule as implemented

**Every connect writes every tuning node PPS owns, from the request alone.**
There is no "leave alone" and no separate reset pass: the Impact camera and
every other camera go through the same writer (§3.3), and the role decides
only the values.

| Node | Impact | Every other role | Why that value |
|---|---|---|---|
| Exposure | locked (70 µs default) | auto, at most the frame period | the camera chooses within the rate PPS needs |
| Gain | locked (12 dB default) | auto, up to the sensor max | a dim scene gets gain, never a dropped rate |
| Gamma | on at the value | off | linear unless asked |
| Line1 | strobe if enabled, else released | released | a strobe output never outlives the Impact role |
| Rate | requested, clamped to the ROI max | the ROI max | capture needs the camera's full rate; never auto (§3.3) |

Measured (probe, 8 Oct 2026, cabin lights on, nobody in the cabin):

- A non-Impact Chameleon3 at full frame runs **149.3 fps delivered** (150.7
  advertised), exposure **6,574 µs** (the whole frame period), gain **18.06
  dB** (its max). Loading the camera's own `Default` user set gives exactly
  the same three numbers and the same picture: the camera's factory behaviour
  and PPS's agree.
- Both auto loops are at their limits, so the full-frame picture is as bright
  as 150 fps allows in that light — dark room, lit mat — and matches frames
  from the 7 Oct session recordings. A brighter picture costs frame rate (the
  bright 19 fps auto-rate picture of 8 Oct) or needs more light.
- Impact at 640×240: 611.7 fps advertised, **591.1 fps delivered**, 70.2 µs,
  11.99 dB.

Design notes:

- **In `start()`, not `stop()`**, so a crash, a pulled cable or another
  application leaving the camera in a bad state is also covered.
- **A camera configured by hand in SpinView for a non-Impact role is
  overridden on connect.** PPS owns these nodes.

Rejected alternatives:

- *A neutral pass for unrequested nodes, then the Impact writes* (the first
  version of this fix, 8e4a53d3). Two procedures for one job, and its
  "neutral" rate was auto: 19 fps.
- *`UserSetSelector=Default` + `UserSetLoad` on every connect.* A complete
  reset, but it slows every connect, resets nodes PPS sets itself (pixel
  format, buffers, chunk data), and depends on `UserSetDefault` not pointing
  at a user set someone saved. The probe uses it only as a reference.

## 6. The defect that prompted this (cabin, early October 2026)

A Chameleon3 was moved from **Impact** to **Down-the-line** without a power
cycle. The DTL picture was nearly black, and no setting in PPS cleared it.
Opening Swing Catalyst, which resets the camera, fixed it.

Cause: of the Impact settings, only the ROI was ever undone. On the DTL
connection every other Impact setting was "leave alone", so the camera kept:

- `ExposureAuto = Off`, `ExposureTime ≈ 70 µs`. This is the darkness: a
  full-frame indoor exposure is in the milliseconds.
- `GainAuto = Off`, `Gain = 12 dB`. (Gamma 0.7 was requested too, but the
  Chameleon3 has no gamma node in raw Bayer, so it never reached the camera.)
- `AcquisitionFrameRateAuto = Off` with the rate enabled. 691 fps is
  unattainable at full frame, so the camera ran at its own ceiling.
- Line1 as the strobe output, if the strobe had been on.

PPS shows no exposure control for a DTL camera, so the user had no way to
recover from inside the app. Aravis had the same gap for exposure, gain, gamma
and strobe; its rate was already safe because it always writes 60 fps.

## 7. Hardware probe — `tools/camera/spinnaker_settings_probe`

A console program built from the app's own `spinnaker_settings.cpp`. It drives
each connected camera through the role changes **without a power cycle** and
measures every connect: delivered fps from the camera's frame timestamps,
chunk exposure (median, min, max), gain, raw-Bayer mean / saturated % / black
%, and a read-back of every node in §4.

| Step | What it is |
|---|---|
| factory | reference: `UserSetLoad Default`, nothing of PPS's |
| dtl1 | a non-Impact connect at full frame |
| impact1 | an Impact connect (640×240, 691 → 611.7 fps, 70 µs, 12 dB, gamma 0.7) |
| dtl2 | non-Impact after Impact — the original defect |
| dirty, dtl3 | another application leaves exposure Off at 70 µs, gain Off at min, the auto limits at min, auto rate, Line1 ExposureActive and a 640×240 ROI; then a non-Impact connect |
| dirty, impact2 | the same, then Impact |

It checks: non-Impact connects deliver at least 98 % of the full-frame max
with auto exposure and gain, exposure never above the frame period, full
frame, Line1 not a strobe, and the same brightness as dtl1 (±8 %) whatever
came before; Impact connects lock 70 µs and 12 dB, deliver at least 95 % of
their rate, crop to 640×240, and drive the strobe only when asked; every
connect leaves the rate manual and logs no writer warning (bar the
Chameleon3's absent gamma). `--snapshot DIR` saves each connect's last frame
as a half-resolution colour PNG, to compare with a recording.

```
cmake -S tools/camera/spinnaker_settings_probe -B build/spinnaker_settings_probe ^
      -G "NMake Makefiles JOM" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build build/spinnaker_settings_probe
set PATH=C:\Qt\6.11.0\msvc2022_64\bin;C:\Program Files\Teledyne\Spinnaker\bin64\vs2015;%PATH%
build\spinnaker_settings_probe\spinnaker_settings_probe.exe [--serial N] [--seconds 3] [--strobe] [--snapshot DIR]
```

PPS must be closed (a camera has one owner). 8 Oct 2026, both studio
Chameleon3s (17453937, 18277032): **all checks pass**. The first run found
the rate-enable ordering bug of §3.3: after the dirty step the rate stayed
auto (`AcquisitionFrameRate not writable`), and passed the fps check only
because the exposure happened to fit.

What the probe cannot see: the app's own path around the writer
(`CameraInstance` priming, the role change of §3.2) and the strobe's light.
