# FLIR camera settings — how PPS manages them

8 Oct 2026. Covers the FLIR (Chameleon3 / Blackfly S) machine-vision cameras
through both backends: Spinnaker (Windows, the cabin) and Aravis (macOS).
Written after a cabin incident (§6) and rebuilt the same day from
measurements on both studio Chameleon3s (§5, §7).

## 1. The rule that matters

**A FLIR camera keeps its node values for as long as it has power.** They
survive `DeInit()`, a PPS restart, and a different application opening the
camera. Whatever PPS leaves on the camera, the next connection inherits, and
so does Swing Catalyst or SpinView.

So every connect starts from first principles:

1. **The camera's own factory set** (`UserSetSelector=Default`, `UserSetLoad`,
   ~25 ms). Every node goes back to the value it left the factory with —
   including the ones PPS does not know about, and the per-camera calibration
   (the two studio cameras' black levels differ: 2.83 % and 5.37 %).
2. **PPS's departures from it, all written every time.** The role decides the
   values, never the procedure: one writer for every role (§3.3).

"Not requested" never means "inherit whatever was last there".

## 2. The settings model

### 2.1 Persisted settings

All per-camera settings live in `AppSettings` (`src/Gui/app/app_settings.h`),
each a `QVariantMap` keyed by the camera key `description|serial`:

| Property | QSettings key | Meaning | Reaches the camera? |
|---|---|---|---|
| `cameraPerspective` | `camera/perspective` | role: None, DownTheLine, FaceOn, Other, Impact. Impact is exclusive (`CameraManager::assignPerspective` strips it from every other camera) | decides which values the writer uses |
| `cameraRoi` | `camera/roi` | normalised crop `{x,y,w,h}` **per role**: `<key>` for every non-Impact role, `<key>#impact` for the impact strip (`src/Gui/cameras/camera_roi.h`) | yes (hardware ROI) |
| `cameraTargetFps` | `camera/targetFps` | frame rate | **Impact only** (§2.3) |
| `cameraExposureUs` | `camera/exposureUs` | locked exposure, µs | Impact only |
| `cameraTuning` | `camera/tuning` | `{gainDb, strobe, viewGain, note}` | `gainDb`, `strobe`: Impact only. `viewGain` is a display stretch on the tile and replay, `note` is stamped into the clip; neither touches the camera. A `gamma` member from older versions is ignored |
| `cameraTriggerMode` | `camera/triggerMode` | free-run / trigger | no local backend reads it |
| `cameraExcluded`, `cameraAlias`, `cameraIsMirrored` | | housekeeping | no |

**The crop is per role.** It used to be one crop per camera, and choosing an
impact mode wrote the strip over it: a camera moved out of Impact came back
as a 640×240 strip at 611 fps, and one moved into Impact that already had a
rate stored kept its full-height crop at 150 fps (§7.2 found both). Every
C++ reader goes through `pp_camroi::cropFor()`; the QML spells the same key
inline so its bindings stay on `appSettings.cameraRoi`. An Impact camera with
no strip stored gets the recommended impact mode centred (never full frame).
`AppSettings::migrateImpactRoi()` moves an existing impact camera's strip to
`<key>#impact` once (`camera/impactRoiMigrated`).

Impact defaults are `CameraInstance::kImpact*` (`camera_instance.h`):
exposure 70 µs, gain **18 dB**, black-level lift **4 %**, view gain 1.0
(§5 for why). `CamerasPanel.qml` mirrors the gain and exposure for its chips.

### 2.2 The Settings → Cameras controls

| Control | Visible for | Writes | Takes effect |
|---|---|---|---|
| VIEW (role) combo | every camera | `cameraPerspective`. Moving to Impact also seeds the default exposure and the recommended impact mode when the camera has no impact strip yet | **at once**: a connected camera that moves into or out of Impact is reconnected (§3.2) |
| Crop editor | every camera | the crop of the role the camera is in. It previews the full sensor in colour (a raw-Bayer camera through the same GPU demosaic as the tiles — it used to show the mosaic as greyscale), in a rectangle of the sensor's aspect so the crop box sits on the picture; `src/Gui/tests/probes/crop_editor_colour.qml` checks both on a real camera | next connect |
| FRAME RATE chips | non-Impact cameras | `cameraTargetFps` | PPCP phones only (resolved to a declared capture profile in `CameraManager::setTargetFps`); a no-op for Spinnaker/Aravis |
| IMPACT MODE chips | Impact | the impact strip + `cameraTargetFps` (crop × rate from the enumerate-time probe) | live crop if streaming as Impact, else next connect |
| EXPOSURE chips | Impact | `cameraExposureUs` | live if streaming (`applyLiveTuning`), else next connect |
| GAIN chips | Impact | `cameraTuning.gainDb` | live if streaming, else next connect |
| VIEW gain, NOTE | Impact | `cameraTuning.viewGain/note` | display / clip metadata only |
| STROBE | Impact | `cameraTuning.strobe` | next connect |

There is deliberately no exposure or gain control for a non-Impact camera:
those run on camera auto. There is no gamma control at all (§5).

### 2.3 What the backend is handed

`CameraInstance`'s constructor (`camera_instance.cpp`) freezes the values for
the instance's lifetime:

- The crop of the camera's role, for every instance (a settings preview
  instance reads it for the crop editor; only buffer-backed instances apply
  it).
- The rate, exposure, gain, black-level lift and strobe **only** when the
  instance has a buffer **and** the role is Impact. Otherwise they stay at
  the "not requested" sentinels: rate `0`, exposure `0`, gain `-1`, lift `0`,
  strobe `false` — which the writer turns into camera auto, the factory black
  level and a released Line1. A settings preview gets none of them, so the
  crop editor shows a bright full-sensor picture.

`primeBackend()` pushes them into the `VideoInputBase` setters
(`setCropRegion`, `setCaptureRate`, `setExposureUs`, `setGainDb`,
`setBlackLevelLift`, `setStrobeOutput`) on the backend thread, immediately
before `start()`.

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

### 3.2 Instance creation and role changes

`CameraManager::setSelected` creates a buffer-backed `CameraInstance`
(`createController`) when a camera is selected and destroys it when it is
deselected. The instance freezes its role's settings — and sizes its ring
slot for the role's crop and rate — when it is built.

So **`assignPerspective` reconnects every connected camera whose
Impact-ness changed** (`setSelected(false)` then `setSelected(true)`), after
saving the new role: the camera moved, and one stripped of Impact because
another took it. The new instance reads the new role; the backend writes it
from the factory set up. Moves between the other roles change nothing on the
camera and reconnect nothing. Before this, a connected camera kept its old
role's settings until the next Disconnect/Connect — the dark ex-impact DTL of
§6. (`CameraManager::setPerspective(controller, …)` only relabels a live
instance; nothing in the UI calls it.)

### 3.3 Connect — `VideoInputSpinnaker::start()`

All before `BeginAcquisition()` (the device nodes are read-only once
streaming). Steps 0 and 3–9 are `src/Video/spinnaker_settings.cpp` —
`loadFactoryDefaults()`, `applyRoi()`, `applyConnectSettings()` — which the
app and the hardware probe (§7.1) share, so what the probe measures is what
the app does.

| # | Node(s) | Impact | Every other role |
|---|---|---|---|
| 0 | `UserSetSelector=Default`, `UserSetLoad` | the factory set | same |
| 1 | `AcquisitionMode` | Continuous | Continuous |
| 2 | `PixelFormat` | first available of BayerRG8, BayerBG8, BayerGR8, BayerGB8, BGR8, RGB8Packed, Mono8 | same |
| 3 | `OffsetX/Y`, `Width/Height` | the impact strip, snapped down to the increments | the role's crop, or full sensor |
| 4 | `ExposureAuto`, `ExposureTime` | Off, the locked value (default 70 µs) | Continuous |
| 5 | `GainAuto`, `Gain` / `AutoGainUpperLimit` | Off, the locked value (default 18 dB) | Continuous, upper limit = the sensor's max (18.06 dB) |
| 6 | `BlackLevel` | the factory value + 4 % | the factory value (step 0) |
| 7 | `LineSelector=Line1` → `LineInverter` (false), `LineMode`, `LineSource` | Output, ExposureActive if the strobe is on, else released | released: `LineMode=Input`, else `LineSource=Off`, else `LineSource=UserOutput1` with `UserOutputValue` false (the Chameleon3) |
| 8 | `AcquisitionFrameRateEnable(d)` → `AcquisitionFrameRateAuto` → `AcquisitionFrameRate` | true, Off, the requested rate clamped to the max **at this ROI** (691 → 611.7 at 640×240) | true, Off, the max at this ROI (150.7 full height) |
| 9 | `AutoExposureTimeUpperLimit` | — | the frame period (6,574 µs at 150.7 fps) |
| 10 | `StreamBufferCountMode/Manual` (**TL stream** node map) | Manual, 40 | same |
| 11 | `ChunkModeActive`, `ChunkSelector=ExposureTime`, `ChunkEnable` | on: each frame carries the exposure actually applied | same |
| 12 | `ExposureAuto` (read) | cached as `m_exposureAuto` (0 = locked, 1 = auto), tagged on every frame | same |
| 13 | `TimestampLatch` | best of `kClockLatchBrackets` host/camera clock brackets | same |

One log line per connect names every departure, e.g.

```
[VideoInputSpinnaker] factory set loaded
[VideoInputSpinnaker] settings: camera auto ExposureAuto=Continuous GainAuto=Continuous AutoGainUpperLimit=18.1dB Line1=UserOutput1(low) FrameRate=150.7fps(max, max 150.7) AutoExposureTimeUpperLimit=6573.6us
[VideoInputSpinnaker] settings: impact locked ExposureTime=70.2us Gain=18.0dB BlackLevel=6.84%(factory 2.83 + 4.00) Line1=UserOutput1(low) FrameRate=611.7fps(requested 691.0, max 611.7)
```

and about a second into streaming a `settled:` line reports what the camera
actually chose (chunk exposure, `Gain`, `AcquisitionFrameRate`). A node the
writer could not set is a `ppWarn`.

**⚠ ORDER: factory set → ROI → exposure → gain → black level → Line1 → rate
→ auto-exposure limit**, with `InvalidateNodes()` between
(`impact_camera_design.md` §3.1):

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
  (found by the probe, §7.1).

**⚠ The rate is never auto.** On the Chameleon3 an auto rate lets auto
exposure stretch the frame period: 19 fps with a ~50 ms exposure in the
cabin on 8 Oct, where the camera should run 150.7.

### 3.4 While streaming — `applyLiveTuning()`

`CamerasPanel` calls `CameraInstance::applyLiveTuning(exposureUs, gainDb)`
when an Impact knob turns on a streaming camera. The instance records the
value first (the next connect primes what the operator last saw), then
`VideoInputSpinnaker::applyLiveTuning` runs `writeTuningNodes()`, which writes
steps 4–5 for the members given (a non-positive exposure or negative gain is
skipped). The crop, rate, black level and strobe are connect-only.

### 3.5 Read-backs and provenance

- Per frame: the chunk `ExposureTime` (µs) and the `m_exposureAuto` flag ride
  on every `RawVideoFrame`, and the exposure midpoint corrects the frame
  timestamp.
- Per connect: the `settings:` line, and ~1 s in the `settled:` line.
- On demand: `CameraInstance::readBackSettings()` → `VideoInputBase::
  readBackSettings()` → `pinpoint::spinnaker::readBack()`, every node above as
  text, read on the backend thread (§7.2 uses it; so does the probe).
- Per clip: `ShotProcessor` records the Impact camera's gain as
  `appliedGainDb()` (what the camera held after the clamp), falling back to
  the requested value, the black-level lift (`capture.blackLevelLift`, which
  moves every recorded level up by ~2.5 per percent), and the strobe flag,
  view gain and note from `cameraTuning`. Clips before 8 Oct 2026 may carry a
  `capture.gamma` that the camera never applied (§5).
- `queryCapabilities()` on a live camera reads nodes only, apart from the same
  brief `AcquisitionFrameRateEnable` toggle as the enumerate probe.

### 3.6 Disconnect — `stop()`

`EndAcquisition()`, join the capture loop, then — only if no other handle is
streaming the device — restore the ROI to full frame and `DeInit()`. The ROI
restore exists because a stale crop poisons the `Width/Height` `GetMax()`
reads of the next capability query. **Nothing else is restored on
disconnect**: every connect starts from the factory set, which also covers a
crash, a pulled cable, or another application.

### 3.7 Aravis — `VideoInputAravis::start()` (macOS)

The same procedure through the Aravis API, in the same order: the factory set
(`UserSetSelector=Default`, `arv_camera_execute_command(UserSetLoad)`), the
region, exposure (Off + value, or Continuous), gain (Off + value, or
Continuous with the auto-gain limit at the gain maximum), the black-level
lift over the factory value, Line1 (strobe, or released), the rate (requested,
or the maximum from `arv_camera_get_frame_rate_bounds` — it used to be a fixed
60 fps), the auto-exposure limit at the frame period, one `settings:` line;
then pixel format Mono8 and 10 stream buffers. `applyLiveTuning()` runs
`writeTuning()`, as on Spinnaker. `readBackSettings()` is not implemented.
**Not hardware-verified**: no FLIR camera has been on the Mac since this was
written, and the probes (§7) are Spinnaker-only.

## 4. Node spellings by firmware

Every write is guarded by `IsAvailable/IsWritable` (Aravis: a `GError`), so a
node a camera lacks is skipped silently — and that is how a write to the wrong
spelling hides. PPS writes both spellings wherever they differ. The
Chameleon3 column is read back by the probe (§7.1) on both studio cameras.

| Concern | Blackfly S (SFNC) | Chameleon3 (CM3-U3-13Y3C, fw 1.13.3.00) |
|---|---|---|
| Factory set | `UserSetSelector=Default`, `UserSetLoad` | the same, 21–28 ms |
| Manual frame rate | `AcquisitionFrameRateEnable` | `AcquisitionFrameRateEnabled` + `AcquisitionFrameRateAuto` (no SFNC node). `…Auto` is read-only while `…Enabled` is false |
| Auto-exposure limit | `AutoExposureExposureTimeUpperLimit` | `AutoExposureTimeUpperLimit` |
| Auto-gain limit | `AutoExposureGainUpperLimit` | `AutoGainUpperLimit` (max 18.06 dB) |
| Black level | `BlackLevel` | `BlackLevel`, % (0–24.9), calibrated per camera: 2.832 (18277032), 5.371 (17453937) |
| Gamma | `GammaEnable`, `Gamma` | **absent** in BayerRG8 and BayerRG16; only `Mono8` (the ISP's processed mono) has `GammaEnabled` (rw) and `Gamma` (ro until enabled). RGB8Packed, BGR8 and YUV422Packed are not offered |
| Line1 | opto-isolated output | output only: `LineMode` reads Output with no Input entry; `LineSource` is one of ExposureActive, ExternalTriggerActive, UserOutput1 — no Off. The factory set drives it from ExposureActive (a strobe, on) |
| Line1 polarity | `LineInverter` | `LineInverter`, factory false: `LineStatus` idles at 1 and drops to 0 for each exposure |
| `ExposureTime` | writable once `ExposureAuto` = Off | the same; the access mode is cached until `InvalidateNodes()` |
| Stream buffer count | TL stream node map | TL stream node map |
| Clock latch value | `TimestampLatchValue` | `Timestamp` |

## 5. The values, and the measurements behind them

| Node | Impact | Every other role | Why that value |
|---|---|---|---|
| Baseline | factory set | factory set | everything PPS does not set is the factory's, never the last owner's |
| Exposure | locked (70 µs default) | auto, at most the frame period | the camera chooses within the rate PPS needs |
| Gain | locked (**18 dB** default) | auto, up to the sensor max | a dim scene gets gain, never a dropped rate |
| Black level | factory + 4 % | factory | keeps the impact shadows out of the clip at 18 dB |
| Line1 | strobe if enabled, else released | released | a strobe output never outlives the Impact role |
| Rate | requested, clamped to the ROI max | the ROI max | capture needs the camera's full rate; never auto |
| Crop | the impact strip | the role's crop | per role (§2.1) |

All measured 8 Oct 2026 on both studio Chameleon3s, cabin lights on, nobody
in the cabin (`spinnaker_settings_probe`, §7.1).

**Normal use.** At full frame: **149.3 fps delivered** (150.7 advertised),
exposure **6,574 µs** (the whole frame period), gain **18.06 dB** (its max).
The camera's own factory set gives exactly the same numbers and picture. The
face-on and DTL clips of 9 Sept, 5 Oct and 7 Oct recorded `exposureAuto:
true`, `exposureUs: 6573.56`, measured 149.3 fps (swing provenance via
libppswing's `ppsw dump`) — the state the analysis is built on — and frames
saved by the probe match frames from those recordings. Both auto loops are at
their limits in that light; a brighter picture costs frame rate (the bright
19 fps auto-rate picture) or needs more light.

**Impact gain.** At 70 µs the mat is unlit: with `BlackLevel` at 0, over 99 %
of the strip reads 0 at any gain. The "mat 5–8" of impact_camera_design.md
§10.3 — and of the 15 Sept clips — is the black pedestal, not light. Gain is
the one camera lever: a gain sweep (0 → 6 → 12 → 18 → 12 → 6 → 0 dB, same
scene, identical there and back) roughly doubles the bright end every 6 dB
(99.9th percentile 40 → 65 → 116 → 209 on 17453937). The default is 18 dB,
the Chameleon3's maximum (a camera with less range clamps it).

**Impact black level.** But the factory pedestal does not hold the noise above
0 at high gain: at 18 dB, 70 % and 41 % of the strip clipped to black on the
two cameras. A lift over each camera's own calibration (the cameras differ, so
an absolute value would not do):

| Lift | black % at 18 dB (18277032 / 17453937) | floor, 1st percentile | median |
|---|---|---|---|
| +0 | 74 / 42 | 0 / 1 | 4 / 6 |
| +2 | 1.8 / 0.25 | 5 / 7 | 9 / 11 |
| +3 | 0.10 / 0.02 | 8 / 9 | 12 / 14 |
| **+4** | **0.02 / 0.00** | **10 / 12** | 14 / 16 |
| +5 | 0 / 0 | 13 / 14 | 17 / 19 |

+4 % is the smallest lift that keeps clipping under 0.02 % on both, at a cost
of ~10 of 255 levels. The floor is the sensor's, not the room's, so it holds
in any light. With it the impact strip delivers 591 fps, 70.2 µs, 18.0 dB,
floor 10–12, nothing clipped, and the bright end doubled over 12 dB
(99.9th percentile 116 → 219 on 17453937).

**No gamma.** impact_camera_design.md §10.3's in-camera gamma 0.7 never
reached a Chameleon3: there is no gamma node in raw Bayer (only in `Mono8`,
the ISP's processed mono). The control was dropped (Mark, 8 Oct 2026); the
writer no longer asks for gamma, and the factory set decides it.

**Strobe.** With the strobe on, Line1 is at its active level for ~7 % of
`LineStatus` samples (a 70 µs exposure in a 1.69 ms period is 4 %, plus read
latency); released, it never leaves idle. `LineInverter` is reset if another
application set it.

**Still the light's, not the settings'.** §10.3's targets — mat under 40,
peak near 250, club body ~100 — need light at the ball (§10.3's floods or a
strobe); the camera has no lever left.

Design notes:

- **In `start()`, not `stop()`**, so a crash, a pulled cable or another
  application leaving the camera in a bad state is also covered.
- **A camera configured by hand in SpinView is reset on connect.** PPS owns
  the camera while it is connected.

Rejected alternatives:

- *A neutral pass for unrequested nodes, then the Impact writes* (the first
  version of this fix, 8e4a53d3). Two procedures for one job, and its
  "neutral" rate was auto: 19 fps.
- *Writing only the nodes PPS knows about, without the factory set* (8379281d).
  Every node PPS did not know about was still inherited: `BlackLevel` and
  `LineInverter` were found that way. The factory set was first rejected as
  slow without being measured; it costs 21–28 ms. It resets nodes PPS sets
  itself (pixel format, chunk data), which `start()` writes afterwards
  anyway, and it loads the read-only factory set by name, not
  `UserSetDefault`, so a user set someone saved cannot stand in for it.

## 6. The defects that prompted this (October 2026)

**The dark ex-impact camera (cabin, early October).** A Chameleon3 was moved
from **Impact** to **Down-the-line** without a power cycle. The DTL picture
was nearly black, and no setting in PPS cleared it. Opening Swing Catalyst,
which resets the camera, fixed it. Cause: of the Impact settings, only the
ROI was ever undone, and only on disconnect; on the DTL connection every other
Impact setting was "leave alone", so the camera kept `ExposureAuto = Off` at
~70 µs (the darkness), `GainAuto = Off` at 12 dB, the manual 691 fps (run at
the full-frame ceiling), and Line1 as the strobe output if it had been on. A
connected camera did not reconnect on the role change at all, and the crop it
came back with was the impact strip (§2.1). PPS shows no exposure control for
a DTL camera, so the user had no way to recover from inside the app.

**Found while fixing it (8 Oct), each by a probe rather than by eye:** an
auto frame rate ran a full-frame camera at 19 fps; a camera left on auto rate
by another application kept it (write order, §3.3); `LineInverter` and
`BlackLevel` were never written; in-camera gamma had never been applied; the
crop was shared between roles; and a role change on a connected camera
reached the camera only on the next Disconnect/Connect.

## 7. Tests on the real cameras

Two harnesses, both run on the studio PC with the cameras attached and nobody
needed at them.

### 7.1 Hardware probe — `tools/camera/spinnaker_settings_probe`

A console program built from the app's own `spinnaker_settings.cpp`. It
connects the way `start()` does (factory set, acquisition mode, pixel format,
chunk, buffers, ROI, the writer) and drives each camera through role changes
**without a power cycle**, measuring every connect: delivered fps from the
camera's frame timestamps, chunk exposure (median, min, max), gain, raw-Bayer
levels (mean, 1st / 50th / 99.9th percentile, clipped and black %),
`LineStatus` sampled while streaming, and a read-back of every node in §4.

| Step | What it is |
|---|---|
| factory | reference: `UserSetLoad Default`, nothing of PPS's, timed |
| dtl1 | a non-Impact connect at full frame |
| impact1 | an Impact connect (640×240, 691 → 611.7 fps, 70 µs, 18 dB, +4 %) |
| dtl2 | non-Impact after Impact — the original defect |
| dirty, dtl3 | another application leaves exposure Off at 70 µs, gain Off at min, the auto limits at min, auto rate, Line1 ExposureActive inverted, `BlackLevel` at max and a 640×240 ROI; then a non-Impact connect |
| dirty, impact2 | the same, then Impact |

It checks: non-Impact connects deliver at least 98 % of the full-frame max
with auto exposure and gain, exposure never above the frame period, full
frame, Line1 not a strobe, and the same brightness as dtl1 (±8 %) whatever
came before; Impact connects lock the exposure and gain (the request, clamped
to the camera's range), deliver at least 95 % of their rate and crop to
640×240; with `--strobe`, Line1 pulses during Impact and holds one level
everywhere else; `LineInverter` is false after every connect; every connect
loads the factory set, leaves the rate manual and logs no warning.

Other modes: `--gain-sweep` (Impact at 0/6/12/max/12/6/0 dB), `--lift-sweep`
(Impact at 12 dB and max over black-level lifts 0–8 %), `--factory-test`
(dirty, then the factory set, timed), `--snapshot DIR` (each connect's last
frame as a half-resolution colour PNG, to compare with a recording).

```
cmake -S tools/camera/spinnaker_settings_probe -B build/spinnaker_settings_probe ^
      -G "NMake Makefiles JOM" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.0/msvc2022_64
cmake --build build/spinnaker_settings_probe
set PATH=C:\Qt\6.11.0\msvc2022_64\bin;C:\Program Files\Teledyne\Spinnaker\bin64\vs2015;%PATH%
build\spinnaker_settings_probe\spinnaker_settings_probe.exe [--serial N] [--seconds 3] [--strobe] [--snapshot DIR]
```

PPS must be closed (a camera has one owner). 8 Oct 2026, both studio
Chameleon3s: **all checks pass**, with and without `--strobe`.

### 7.2 In-app role probe — `CameraRoleProbe`

The probe above cannot see the app's own path: the VIEW combo, `CameraManager`,
the per-role crop, `CameraInstance` priming, the reconnect. This one runs
inside the app:

```
set PINPOINT_PROBE_CAMERA_ROLES=<report file>
set PINPOINT_PROBE_CAMERA_SERIAL=<serial>      (optional: one camera)
build\…\PinPointStudio.exe
```

For each impact-capable camera it connects and captures, then moves the camera
original role → Impact → DownTheLine → Impact → original role through exactly
the calls Settings → Cameras makes (the VIEW combo's impact seeding, then
`assignPerspective`). After each move it waits 4.5 s and checks the device's
read-back and the delivered frames against the role: the instance was rebuilt
(a reconnect) and carries the role; Impact has exposure and gain locked, the
black level at factory + 4 %, the rate manual, the impact strip as its frames,
its rate delivered and the strobe as configured; the other roles have auto
exposure and gain, the factory black level, the rate manual at the ROI max
and delivered, no strobe, exposure within the frame period, and **their own
crop as their frames**. It restores the operator's camera settings,
connection and capture state, writes the report and quits.

8 Oct 2026, both studio Chameleon3s: **117 checks, all pass**, and the
settings file came back byte-identical. Its first run failed four checks and
found the shared crop (§2.1).

What neither probe can see: the strobe's light, and anything about the picture
beyond levels — that is still a look in the cabin.
