# Improving FLIR image clarity

17 Oct 2026. Covers the two cabin Chameleon3s (CM3-U3-13Y3C: 17453937 face-on, 18277032 DTL)
and every step a frame takes from the sensor to the screen and the saved clip. It was prompted by
Mark's observation that Swing Catalyst (SC), run on the cabin PC with the same cameras, looked
"much sharper focus" than PPS. Companion to `flir_camera_settings.md` (how PPS sets the cameras)
and `camera_calibration_design.md` §5 (focus by capability class).

This document has two halves. §1–§4 are what we know now, from reading both applications and
the cameras. §5–§8 are the test plan: Mark records as the subject, and we measure how clarity
moves as each lever changes. §9 is left for the results.

## 1. It is not focus

The Chameleon3 lenses are C-mount with a mechanical focus ring. No software can move them —
Spinnaker has no focus node on this camera (`camera_calibration_design.md` §5.1, class B). SC and
PPS saw the same lens at the same ring position, so **focus cannot be the difference between them**.

That does not prove the lens is *well* focused, only that both applications share whatever focus
it has. §6.1 measures that directly, because SC's processing (§2) would hide a mild defocus.

## 2. What Swing Catalyst does

Read from `C:\ProgramData\Swing Catalyst\settings\VideoCaptureSettings.xml` and its log
(`C:\ProgramData\Swing Catalyst\logs\log.txt`) on GOLFSIMPC, 17 Oct. It is the same for both cameras:

| Stage | SC setting |
|---|---|
| Capture | full sensor, 1280 × 1024, `Bayer8_RGGB`, 149.3 fps measured |
| Exposure / gain | **not stored** (`SettingsSpinnaker` is empty), so most likely camera auto (unverified, §6.2) |
| Demosaic | on the GPU (`VideoFilterChainGpu`, D3D11) |
| Denoise | **on**: window radius 3, Gaussian σ 3, lerp 0.8, noise level 0.15 |
| Sharpen | **unsharp mask on**: luma amount 1.0, 5 × 5 kernel; chroma 0 |
| Gamma | **1.25** |
| Lens correction | off |
| Encode | `h264_nvenc`, profile "Balanced", no resize: ≈ **0.064 bits per pixel** (7.9 MB for 746 face-on frames) |

Denoise followed by an unsharp mask is the standard recipe for video that "looks in focus". The
unsharp mask raises edge contrast, which the eye reads as focus. Removing noise first means the
sharpening has no grain to amplify, and the encoder spends its bits on real detail.

## 3. What PPS does

| Stage | PPS today | Evidence |
|---|---|---|
| Capture | software crop: face-on 1008 × 1024 on the sensor, 848 × 1024 saved; DTL 640 × 1020 | camera nodes, 17 Oct; swing 2026-10-07 #50 |
| Exposure | **auto, pinned at its ceiling**: 6 574 µs, the whole 150.7 fps frame period | `ExposureTime` = `AutoExposureUpperLimit`; `capture.exposureUs` |
| Gain | **auto, pinned at its ceiling**: 18.06 dB = `AutoGainUpperLimit` | camera nodes, 17 Oct |
| Demosaic, live tile | 3 × 3 bilinear in `bayer_demosaic.frag`, through a linear sampler (KB-30, fixed; §7.2: negligible) | `bayer_video_item.cpp:206` |
| Demosaic, saved clip | OpenCV edge-aware (`COLOR_BayerRG2BGR_EA`) | `frame_decode.cpp:28` |
| Denoise | none | — |
| Sharpen | none (the camera's `Sharpness` node does nothing to Bayer output) | — |
| Gamma | camera default on face-on and DTL | — |
| Encode | x264 CRF 28 ("low", the default 23 Sept – 17 Oct), `medium`, yuv420p: ≈ **0.026 bits per pixel**. **Now CRF 23** ("Standard") by default, ≈ 0.07 — Swing Catalyst's budget; CRF 28 stays selectable as the analysis floor | 676 kbit/s at 848 × 1024, 30 fps container |
| Display | the live tile and replay scale to the tile; no sharpening | — |

⚠ **Both the exposure and the gain are at their ceilings.** Auto exposure has used all of the frame
period and auto gain all of its range, so the camera is asking for more light than the bay gives it.
The frames are as noisy as this sensor gets (maximum gain).

⛔ **The long exposure is not a defect to fix — the shaft tracker depends on it.** At 6.6 ms a fast
club images as a fan, and the tracker reads three timed positions off every such frame: the trailing
edge (exposure start), the leading edge (exposure end, the frame time) and their midpoint
(`ShaftWedgeObs`, `shaft_wedge.h` `leadEdge`). Frames are 6.6 ms apart whatever the exposure, so a
shorter one turns three readings into one, dimmer, reading — exactly where evidence is scarcest, at
impact. Clarity has to come from light, processing and bits, never from a shorter exposure (§6.5).

SC most likely runs at the same ceilings, since it stores no exposure and runs the same frame rate.
If so, its frames carry the same blur and noise as ours, and its advantage is the processing alone.
§6.2 checks this.

## 4. The levers

Each lever is one of two kinds. **Capture levers** change the photons, so testing one needs a new
recording. **Processing levers** change only what is done with the raw frames, so they can be tested
offline: record once with `saveRawFrames` on, then rebuild the same frames many ways. Mark's time
is only needed for the capture levers.

| # | Lever | Kind | Changes the frames analysis reads? |
|---|---|---|---|
| L1 | Light on the golfer | capture | yes: less noise (auto gain falls) |
| L2 | Exposure locked at the frame period | capture | no change today; keeps the blur the wedge reads once light is added (§6.5) |
| L3 | Fixed gain | capture | yes: trades noise against brightness |
| L4 | Full-width capture (no software crop) | capture | yes: more of the scene; pixel scale unchanged |
| L5 | Demosaic method (bilinear / EA / VNG) | processing | yes, if used for the saved clip |
| L6 | Denoise (spatial or temporal) | processing | yes, if applied before encode |
| L7 | Unsharp mask | processing | **never**: display only (below) |
| L8 | Gamma | processing | display only |
| L9 | Encode quality (CRF 28 / 23 / 18) | processing | yes, on every re-analysis |
| L10 | Live tile: KB-30 fix (nearest sampler, `texelFetch`) | display | no |

⛔ **Sharpening and gamma go on the display path only**, like the impact tile's VIEW gain
(`viewgain.frag`), and are never applied to frames that analysis reads. An unsharp mask draws halos
beside every edge, and the shaft tracker, ball detector and pose model would all read them as structure.

⚠ **Denoise before encode is a real option, but it changes re-analysis inputs.** Re-analysis of a
default swing already runs on the mp4, not the raw frames (`swing_storage_impl.md` stage 4). Any L5,
L6 or L9 change to the saved clip must therefore pass the same raw-vs-mp4 gate that study used (§7.3).

## 5. What "clarity" is, in numbers

Clarity is judged by eye in the end, but it has three separate causes, and each needs its own measure:

| Measure | What it isolates | How |
|---|---|---|
| **MTF50**, cycles/pixel | focus + demosaic + sharpening + encode on a still subject | slanted-edge method (ISO 12233) on a printed target at the golfer's plane |
| **Noise σ**, DN | gain + light + denoise + encode | standard deviation over a flat patch of the same target |
| **Motion blur**, px | exposure | shaft edge spread *across* the shaft at P6, against the same edge at address |
| **Bits per pixel** | encode cost | file size ÷ (frames × pixels) |
| **Re-analysis delta** | harm to analysis | `raw_vs_mp4.py` against the raw run (§7.3) |

The target is one A4 sheet: a solid black square rotated 5° on white, mounted flat at the hitting
area facing each camera in turn. The ChArUco board (`docs/design/charuco-print/`) serves too, since
its square edges are slanted edges. Its job is to give every arm the same still subject, which a
golfer cannot.

Every arm also gets a **comparison sheet**: the same frame of each variant side by side, at 1:1
and with crops of the face, hands and club at address, P6 and impact. Mark judges from these.

## 6. Recording session (Mark as subject)

One cabin session of about 30 minutes, about 13 swings. Club: 7-iron throughout. Lights are fixed
for the whole session, apart from arms C2 and C3.

⛔ **Nobody touches a lens ring or moves a tripod** between the arms. That would change what
we are measuring.

### 6.1 Setup and focus check (no swings)

1. PPS: Settings → Storage → **save raw frames on**. Raw is ~900 MB a swing and goes to the library
   on the share; it is deleted once the CSVs are committed.
2. Target at the ball position, facing face-on, then facing DTL. Record 3 s of each with PPS (a
   manual capture, nobody in frame).
3. Measure MTF50 on the raw frames. **Gate: MTF50 ≥ 0.25 cycles/pixel on the raw green channel.**
   Below that, the lens itself is soft. Re-focusing is then a separate step before any other arm,
   and its guide is `camera_calibration_design.md` §5.3 and §5.6 (back-focus).

### 6.2 SC reference (3 swings)

1. Close PPS. Open SC, take 10 s of Mark at address, then 3 swings.
2. Close SC, and **before PPS opens the cameras**, dump every camera node read-only:
   `NodeMapInfo_v140.exe` (Spinnaker's installed sample, `bin64\vs2015`; run over ssh). The node
   values survive SC's exit, so this records SC's exposure, gain and ROI as the camera held them.
3. Copy SC's mp4s for the session to the share's scratch area (`/mnt/swingdata/scratch/clarity/sc/`).

### 6.3 PPS baseline (3 swings)

PPS as it is today (auto exposure, auto gain, CRF 28), raw frames on: 10 s at address, then 3 swings.
This is the raw source for every processing arm in §7.

### 6.4 Capture arms (7 swings)

Raw on, 10 s at address before each arm. No arm shortens the exposure (§6.5).

| Arm | Exposure | Gain | Light | Swings | Question |
|---|---|---|---|---|---|
| C1 | auto | auto | as baseline | 3 | full sensor width (L4), crop cleared: does the wider view cost anything? |
| C2 | **locked at the frame period** (Full frame) | auto | all the extra light available, on the golfer | 3 | how far does noise fall when gain is free to drop, with the wedge's blur held? |
| C3 | auto | auto | as C2 | 1 | does auto exposure shorten itself under the added light? Read `capture.exposureUs` |

**Built (17 Oct).** Settings → Cameras now has an "Exposure and gain" row on every non-Impact camera
whose backend can lock them (`tuning.lockable`, Spinnaker and Aravis): Auto or a locked exposure, Auto
or a locked gain, and the live levels in its summary line. The values live under `<key>#wide`
(`pp_camroi::tuningKey`), never the impact camera's slot. A lock reaches a streaming camera at once
(`applyLiveTuning`); a return to Auto reconnects the camera (`CameraManager::reconnectCamera`), because
only the connect writer puts the nodes back on auto. Each swing stamps `capture.exposureUs` (per-frame
chunk data) and, when locked, `gainDb`; the sheet reads the values from there, not from this table.

### 6.5 The shaft tracker gate

**Why no arm shortens the exposure.** Every corpus swing the shaft tracker and its blur wedge were
tuned on was recorded at auto, ~6.6 ms, and the code makes the blur evidence:

- **Three timed readings a frame.** The recorded exposure reaches `WedgeConfig::exposureUs`
  (`shaft_tracker.cpp:242`). A blurred frame yields a leading edge at the frame time and, when the
  edges sit |ω̂|·t_exp apart, a trailing edge and a midpoint as well (`ShaftWedgeObs`). The trail needs
  ≥ `edgeMinSepDeg` (4°) of separation: at 6.6 ms that is any club turning faster than ~600°/s; at
  2 ms only above ~2000°/s; at 1 ms never. Before the top the centroid well needs a ≥ 2° fan, the
  same story. A short exposure turns three readings into one.
- **Each reading dimmer.** The evidence floors are absolute (`evAbsFloor`) and gain is already at its
  18 dB ceiling, so 1 ms locked is 6.6× (−16 dB) less signal with nothing to make it up.
- **No gain to buy.** The one problem a shorter exposure would address — the impact shaft-lean smear —
  was fixed on 2026-09-29 by reading the leading edge (`analysis_pipeline.cpp`, P7 lean σ).

**Why the lock exists anyway: added light.** Auto exposure sits at the frame period today only because
the bay is dark. Light the golfer and auto may shorten the exposure on its own, quietly spending the
wedge's evidence to brighten a picture that gain could have brightened. Locked at the frame period
(the row's Full frame chip), the blur is held and the extra light goes to lower gain — less noise for
the eye and the evidence floors alike. C3 says whether auto would have drifted.

**Gate, C1 and C2 against the §6.3 baseline (face-on and DTL):** `track.valid`, coverage, WEDGE-tier
stamps, `ShaftWedgeObs` Trail/Mid counts, P-position emissions, P6/P7 against the acoustic impact, and
shaft lean at impact. No arm is recommended, and no default changes, unless it holds every one.

## 7. Offline processing arms (no Mark)

The processing levers L5, L6, L8 and L9 are built from the baseline raw (§6.3), one factor at a time:

| Arm | Demosaic | Denoise | Encode |
|---|---|---|---|
| P0 | EA | off | CRF 28 (today) |
| P1 | EA | off | CRF 23 |
| P2 | EA | off | CRF 18 |
| P3 | VNG | off | CRF 28 |
| P4 | EA | spatial (`fastNlMeansDenoisingColored`, h ≈ noise σ) | CRF 28 |
| P5 | EA | temporal (3-frame, motion-gated) | CRF 28 |
| P6 | best of P3–P5 | best of P3–P5 | best encode that meets §7.3 |

Then the display arms, each on P0 and P6:

| Arm | Display |
|---|---|
| D0 | as today |
| D1 | unsharp mask, luma amount 1.0, 5 × 5 (SC's setting) |
| D2 | D1 + gamma 1.25 (SC's whole display) |
| D3 | unsharp mask, amount 0.5 |

### 7.1 Tooling

- **Encode arms (P1, P2):** `tools/swinglab/src/raw_reencode` already rebuilds a swing from its raw
  through the production `SwingExporter` at any CRF.
- **Demosaic and denoise arms:** a `--demosaic` and a `--denoise` option on `raw_reencode`, through
  the same exporter, so the arm measures what the app would ship.
- **Measures and sheets:** `tools/camera/clarity/clarity_measure.py` reads a run root of variant
  mp4s and the raw. It writes MTF50, noise σ, shaft blur and bits per pixel to one CSV
  (`docs/research/data/clarity/clarity_arms.csv`), and writes the comparison sheets.
- **Display arms:** the unsharp mask and gamma as a shader after the demosaic, shared by the live
  tile and replay, with its strength as a setting. D1–D3 are renders of that shader on recorded
  frames, so the sheet shows exactly what the tile would.

### 7.2 Live tile (L10)

KB-30 is fixed: a nearest sampler, every read an exact sensor pixel, and the four demosaiced pixels
around each fragment blended after the demosaic. **Measured on 17 Oct, it makes almost no difference.**
One raw face-on frame (2026-07-05 W02 s1, frame 100) through the old and new renderer on the Mac's
GPU: mean change 0.04–0.07 of 255, under 0.3 % of pixels changed by more than 8 (all at hard
edges), at 0.6× and at 3×. The live tile's softness is not this. (Scratch harness, not kept.)

### 7.3 The analysis gate

Any arm that changes the saved clip (P1–P6) is re-analysed against the raw run with
`tools/swinglab/raw_vs_mp4.py`, as in `swing_storage_impl.md` stage 4.
**Gate: no worse than P0 on body keypoints (mean px), hand keypoints and club head px, and
`clubheadPeakLead`.** A clip that looks better but moves the numbers more does not ship.

## 8. Deliverables

- **This document**, with §9 filled from the CSV, each figure reproducible from a command against
  the named run root `/mnt/swingdata/scratch/clarity/`.
- **Tooling:** the `raw_reencode` options (§7.1) and `tools/camera/clarity/clarity_measure.py`.
- **App:**
  - before the session: exposure and gain on every camera role, with a Full frame lock (§6.4);
  - KB-30 fixed;
  - the display sharpen/gamma shader on the live tile and replay, with its setting;
  - any change of default (encode, denoise, exposure) that the results and §7.3 support. Mark
    judges these from the sheets before anything is committed.

## 9. Results

Not yet run.
