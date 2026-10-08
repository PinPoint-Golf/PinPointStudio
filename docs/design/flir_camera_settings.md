# FLIR camera settings — how PPS manages them

Brief, 8 Oct 2026. Covers the FLIR (Chameleon3 / Blackfly S) machine-vision
cameras through both backends: Spinnaker (Windows, the cabin) and Aravis
(macOS). Written after a cabin incident; the fix is §5, to be applied in a
separate session.

## 1. The rule that matters

**A FLIR camera keeps its node values for as long as it has power.** They
survive `DeInit()`, a PPS restart, and a different application opening the
camera. Whatever PPS leaves on the camera, the next connection inherits, and
so does Swing Catalyst or SpinView. Anything PPS does not write on connect is
whatever the last writer left there.

## 2. Where the settings come from

`CameraInstance` (`src/Gui/cameras/camera_instance.cpp`) reads per-camera
settings from `AppSettings`, keyed by `description|serial`, and hands them to
the backend in `primeBackend()` before `start()`:

| Value | Source | Impact camera | Every other camera |
|---|---|---|---|
| Crop (ROI) | `cameraRoi` | small crop | the saved crop, or full frame |
| Frame rate | `cameraTargetFps` | e.g. 691 fps | `0` = leave alone |
| Exposure | `cameraExposureUs` | default 70 µs | `0` = leave alone |
| Gain | `cameraTuning.gainDb` | default 12 dB | `-1` = leave alone |
| Gamma | `cameraTuning.gamma` | default 0.7 | `0` = leave alone |
| Strobe | `cameraTuning.strobe` | optional | `false` = leave alone |

The rate, exposure, gain, gamma and strobe values are loaded only when
`cameraPerspective == Impact` and the instance has a buffer. A settings preview
instance gets none of them. That is deliberate: the crop editor should show a
bright full-sensor picture. See `impact_camera_design.md` §10.2–10.3.

The exposure, gain and gamma controls in `CamerasPanel.qml` are visible
**only** while the camera is Impact (line ~970). A camera in any other role
has no PPS control over those nodes.

## 3. What each backend writes on connect

### Spinnaker — `VideoInputSpinnaker::start()`

The order is fixed (see the ⚠ ORDER note in the source). It is ROI, then
exposure, then rate, with `InvalidateNodes()` between them.

| Node(s) | Written when | Value |
|---|---|---|
| `AcquisitionMode` | always | Continuous |
| `PixelFormat` | always | first of BayerRG8/BG8/GR8/GB8, BGR8, RGB8Packed, Mono8 |
| `Width/Height/OffsetX/OffsetY` | always (`applySpinnakerRoi`) | crop, or full sensor |
| `ExposureAuto`, `ExposureTime` | `m_exposureUs > 0` | Off, value |
| `GainAuto`, `Gain` | `m_gainDb >= 0` (`writeTuningNodes`) | Off, value |
| `GammaEnable(d)`, `Gamma` | `m_gamma > 0` | true, value |
| `LineSelector/LineMode/LineSource` | `m_strobe` | Line1 / Output / ExposureActive |
| `AcquisitionFrameRateAuto`, `AcquisitionFrameRateEnable(d)`, `AcquisitionFrameRate` | `m_captureFps > 0` | Off, true, value clamped to the ROI's max |
| `StreamBufferCountMode/Manual` (TL stream map) | always | Manual, 40 |
| `ChunkModeActive`, `ChunkSelector=ExposureTime`, `ChunkEnable` | always | on |
| `TimestampLatch` | always | clock bracket before `BeginAcquisition()` |

`applyLiveTuning()` re-runs `writeTuningNodes()` (exposure, gain and gamma)
while the camera streams.

**On disconnect** (`stop()`), the ROI is restored to full frame before
`DeInit()`. Nothing else is restored. The ROI restore was added because a stale
crop poisons the `Width/Height` `GetMax()` reads made by the capability query.

### Aravis — `VideoInputAravis::start()`

- The region is always written (crop or full sensor).
- The frame rate is always written: the requested rate, or **60 fps** otherwise.
- The pixel format is always written (Mono8).
- Exposure, gain, gamma and strobe follow the Spinnaker rule: written only when
  requested, otherwise left alone.
- Not hardware-verified (see the source comment, 2026-09-15).

## 4. The defect (cabin, early October 2026)

A Chameleon3 was moved from **Impact** to **Down-the-line** without a power
cycle. The DTL picture was nearly black, and no setting in PPS cleared it.
Opening Swing Catalyst, which resets the camera, fixed it.

Cause: of the Impact settings, only the ROI is ever undone. On the DTL
connection, every other Impact setting was "leave alone", so the camera kept:

- `ExposureAuto = Off`, `ExposureTime ≈ 70 µs`. This is the darkness: a
  full-frame indoor exposure is in the milliseconds.
- `GainAuto = Off`, `Gain = 12 dB`, `Gamma = 0.7`.
- `AcquisitionFrameRateAuto = Off` with the rate enabled. 691 fps is
  unattainable at full frame, so the camera ran at its own ceiling.
- Line1 as the strobe output, if the strobe had been on.

PPS shows no exposure control for a DTL camera, so the user had no way to
recover from inside the app.

Aravis has the same gap for exposure, gain, gamma and strobe. Its rate is safe
because it always writes 60 fps.

## 5. Fix (to apply in a separate session)

The principle is that **every node PPS ever writes must be written on every
connect**: either to the requested value or to a known neutral default. "Leave
alone" must not mean "inherit whatever was last there".

Spinnaker, in `start()`, when the corresponding value is not requested:

| Node | Neutral value |
|---|---|
| `ExposureAuto` | Continuous |
| `GainAuto` | Continuous |
| `AcquisitionFrameRateAuto` | Continuous (Chameleon3), and `AcquisitionFrameRateEnable(d)` false |
| `GammaEnable(d)` | false (or leave Gamma at 1.0 if the enable is not writable) |
| `LineSelector=Line1` → `LineMode` | Input (only when `!m_strobe`) |

Aravis: the same for exposure (`arv_camera_set_exposure_time_auto(ARV_AUTO_CONTINUOUS)`),
gain, gamma and strobe. The rate is already handled.

Notes:

- **Do it in `start()`, not `stop()`.** That also covers a crash, a pulled
  cable, or another application leaving the camera in a bad state.
- **Write the neutral values first, then the Impact values.** This keeps the
  existing ORDER rule intact (ROI → exposure → rate, `InvalidateNodes()`
  between).
- Each write is guarded by `IsAvailable/IsWritable` like the existing code, so
  a camera missing a node (Blackfly S vs Chameleon3 spellings) skips it.
- Log one `ppInfo` line per connect naming the mode applied ("tuning: camera
  auto" vs "tuning: impact locked"), so the app log shows which state the
  camera was put in.

Rejected alternative: `UserSetSelector=Default` + `UserSetLoad` on every
connect. It is a complete reset, but it slows every connect and resets nodes
PPS sets itself (pixel format, buffers, chunk data). It also depends on
`UserSetDefault` not pointing at a user set someone saved.

### Cabin test

1. Put the Chameleon3 in Impact and stream it. Confirm the dark 70 µs picture
   and the high rate in the log.
2. Without power-cycling, switch it to DTL and reconnect. The picture must be
   normally exposed. The log should show `ExposureAuto` Continuous and the
   camera-auto tuning line.
3. Switch it back to Impact. The locked exposure, gain, gamma and rate must all
   be re-applied, and the strobe must work if it is enabled.
4. Optional: leave the camera in Impact, open and close SpinView, then
   reconnect in PPS. The settings must be the same as in step 3.
