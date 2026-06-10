# Golf Shot Detection — Research & Design

**The problem:** automatically detect that a **golf shot (ball strike / impact) has occurred**, fix its
**impact instant** accurately enough to anchor a pre-roll/post-roll buffered capture, and do so with
**few false positives** (waggles, practice swings, address taps, mat/ground taps, club-body contact,
nearby players) and low enough latency to drive a live trigger.

This doc reframes and supersedes the earlier ball-only note (formerly `CLUBANDBALLDETECTION.md`, now
folded in here and into [`BALL_DETECTOR_DESIGN.md`](BALL_DETECTOR_DESIGN.md)): "ball present → ball
absent" is **one signal among several**, not the framing. Shot detection is a
**multi-modal sensing problem**, and the evidence says the robust answer is **sensor fusion**, not any
single method.

PinPoint already has the pieces: **audio capture** (`src/Audio`, `QAudioSource`), **multiple Bluetooth
IMUs** (~100 Hz, host-fused quaternions **+ raw accel/gyro**, wrist/club-mountable), **multi-camera
video**, and a single **`ShotController` funnel** — `triggerShot(Source, timestampUs)` — that already
supports a back-dated impact timestamp and an `armed` gate (capturing && processor idle). The job is to
feed that funnel from the right detectors, fused.

> **How to read this.** Claims carry a confidence (`high`/`medium`) and the adversarial-verification
> vote they survived (`3-0` unanimous; `2-1` one dissent on a nuance). Refuted claims are in
> [§9](#9-refuted-claims--do-not-cite). Sources in [§11](#11-sources).
>
> **Domain caveat (applies throughout).** The strongest empirical numbers come from **racquet sports**
> (table tennis / tennis), not golf — the literature notes table tennis is *harder* than golf for IMU
> impact detection, so the transfer is conservative, but **no golf-specific fusion benchmark was
> confirmed.** Patents describe *disclosed/optional* techniques, not validated deployed performance.

---

## 1. TL;DR — the recommendation

**Fuse three detectors behind the existing `ShotController`, each in the role it's good at:**

| Modality | Role | Why | Evidence |
|---|---|---|---|
| **Inertial (IMU)** | **Fast trigger** | Club/active-wrist impact spike is clean and unambiguous; cheap threshold/peak detection; ~real-time | `high, 3-0` |
| **Acoustic (mic)** | **Confirm + sample-accurate pinpoint** | Impact "click" is a textbook impulsive transient; high sample rate fixes the true instant | `high, 3-0` |
| **Vision (ball/club)** | **Corroborate / veto + post-hoc precise impact frame** | Ball-was-present-then-launched is strong *context*, but slow & under-evidenced as a live trigger | `under-evidenced` |

**The single most important finding:** *no single modality is robust.* A bare accelerometer spike **or**
a bare audio onset **cannot** tell a real ball strike from a mat/ground tap, club-body contact, a
practice swing, or a nearby player — these are demonstrated primary failure modes (`high, 3-0`). The
robustness comes from **layering transient detection with confirmation gates and cross-modal
agreement.**

**Timestamping:** use **"confirm-then-pinpoint"** — the higher-accuracy/lower-false-rate detector
(acoustic spectral or an ML swing model) *confirms* the event; a fast time-domain detector (accel-
derivative peak or audio energy-envelope peak) *fixes* the sample-accurate instant used to back-date the
`EventBuffer` shot marker.

**Mapping to PinPoint:** IMU fires the candidate `triggerShot(Source::Imu, …)` under the `armed` gate →
acoustic confirms within a small time window **and** supplies the precise `timestampUs` → vision
corroborates (ball present→launched) and yields the exact impact frame offline. Add a `Source::Audio`
to the existing enum; everything else (funnel, back-dated marker, armed gate) already exists.

---

## 2. Modalities

### 2.1 Inertial (IMU) — the fast trigger

- **Threshold/peak detection works when a clean impact spike is present.** Peak detection with
  thresholding + windowing on the **derivative of averaged accelerometer** data gives **98.28% (wrist) /
  99.44% (racket)** stroke-detection accuracy across 8 stroke types, with the two mount positions
  performing **similarly** (`high, 3-0`, Ebner & Findling, Aalto MoMM '19).
- **ML only earns its keep where the jerk is absent.** Silent Impact (UIST '24): on a *passive*-arm
  sensor lacking the impact jerk, threshold peak detection scored **37.6 F1** vs **MS-TCN 86.0**; but on
  the *dominant* arm where the jerk is present, threshold scored **90.1 F1** — higher than the ML
  passive-arm result. So a **club-mounted or active-wrist IMU → simple low-latency threshold**; passive
  placements need a learned model (`high, 3-0`). BLSTM/CNN roughly *halve* swing-event segmentation
  error vs handcrafted heuristics generally (`medium, 2-1`).
- **Impact timing is localizable to ~5–16 ms at the wrist** — "the peak of acceleration signal … is
  relatively clearly seen at impact," far better than address/finish (41–92 ms) (`high, 3-0`, Jang et
  al., *Sensors* 2020 20(16):4466). *(Stronger "5 ms precise-instant guarantee" phrasings were refuted —
  see §9; use the 5–16 ms range.)*
- **High-g needs the right sensor.** Golf impact transmits very high g (sources cite up to ~1000 g at
  the clubhead); commercial wearables use **dual-range accelerometers** (low-g high-resolution for swing
  dynamics + high-g low-resolution for the impact spike) to avoid saturation (`high, 2-1`; e.g. ST
  LSM6DSV80X/320X ship this).
- **The failure mode:** a bare accel/derivative threshold **cannot distinguish a ball strike from the
  club hitting the floor/mat or the body on follow-through** — *the* primary IMU false-positive source
  (`high, 3-0`). This is why the IMU is the *trigger*, not the *decision*.

**PinPoint fit:** raw accel is available per IMU (`ImuInstance::accelX/Y/Z`, `ImuBase::fuseRawImu`).
**Caveat — sample rate:** our IMUs default to **100 Hz** (10 ms/sample). The impact transient is
sub-millisecond, so 100 Hz can reliably say *"impact happened"* but pins the instant only to ±~1 sample
(~10 ms) — exactly why acoustic pinpointing matters. Consider raising the output rate (`setOutputRateHz`
/ `RRATE`) for the club/wrist sensor, and treat the IMU instant as coarse.

### 2.2 Acoustic (microphone) — the confirmer + sample-accurate pinpoint

- **The signature:** an impulsive impact event is **short duration, high intensity, sharp onset, fast
  (exponential) decay** (`high, 3-0`, US7234340).
- **Detection:** either split the signal into frequency bands and detect **per-frame power changes**, or
  extract a **time-condensed envelope** (max magnitudes in a ~10 ms sliding window) and find **peaks
  above an adaptive threshold** (e.g. halfway between the second's average and max), then validate with
  **STFT spectral flux / high-frequency-content (HFC)** (`high, 3-0`, US11133023 ShotSpotter). The
  Audio Peak Function in arXiv 1805.05456 is the same envelope-peak approach applied to ball strikes.
- **False-positive rejection (acoustic):** (a) verify **exponential energy decay** after onset (rejects
  speech, which builds rather than decays); (b) reject candidates with **negative spectral flux or
  negative ΔHFC** (sound getting quieter / losing highs); (c) require **sudden amplitude relative to the
  noise floor**; (d) **instrument-tuned high-pass pre-filter** before energy computation; (e) a
  **refractory/guard period** (~30 ms) to prevent double-triggers (`high, 3-0`; Rosão DAFx 2018 +
  patents). `aubio`'s min inter-onset-interval + silence threshold are the canonical real-time
  implementation.

**PinPoint fit:** audio is already captured for STT; an onset detector is a cheap parallel consumer of
the same PCM. High audio sample rate (44.1/48 kHz) is what enables sample-accurate impact timing.

### 2.3 Vision (ball / club) — corroborate, veto, and pinpoint the impact *frame*

- **Honest gap:** **no verified source surfaced a vision shot-detection technique or accuracy/latency.**
  Ball presence-then-absence, clubhead-ball contact-frame detection, and launch detection are plausible
  and are what PinPoint started from, but they're **under-evidenced as a real-time trigger** — and
  vision latency (camera fps + the shared `FrameThrottle`, `skipFactor=2`) makes it the *slowest*
  modality. The ball also leaves frame in 1–3 frames at 30–60 fps.
- **So its role is corroboration/veto and offline precision:** "a ball was present in the hitting area
  and then launched" is strong *context* to confirm an IMU/audio trigger and to **reject practice
  swings** (no ball ever left); and the captured `SwingWindow` gives the **exact impact frame** offline
  for metrics. It should generally *not* be the fast trigger.
- The **detector implementation** for this modality (CNN + Kalman patch tracking, replacing the Hough
  `BallDetector`) is specified in **[`BALL_DETECTOR_DESIGN.md`](BALL_DETECTOR_DESIGN.md)**; that doc's
  `ballLaunched(timestampUs)` hook is precisely the corroboration/veto signal referenced here.

### 2.4 Other approaches (radar/Doppler, optical gates, pressure mats) — named, unevidenced here

Doppler radar and photometric **camera** triggers are how commercial launch monitors detect/track the
shot, but those are full LM systems, not a trigger we can adopt standalone. Optical/photodiode gates and
pressure/strain mats were named in the question but **produced no surviving claims** — out of scope
unless we add dedicated hardware.

---

## 3. How commercial wearables / launch monitors reject false strikes

Inertial signal processing **plus contextual gates** (`high`, US8903521 / US10706273):
- **Vibration analysis** to detect impact, **combined with a club speed/acceleration threshold or range
  gate** — a "hit" only counts inside an acceptable speed/accel window (`3-0`).
- **Wavelet comparison** of the measured signature against a typical *valid golf swing* signature to
  eliminate false strikes / practice swings (`2-1`; **attribute to US8903521 only** — the same claim was
  *refuted* for US10706273, see §9).
- **Club-orientation gating** — only count an impact when the club is **oriented vertically at the moment
  of impact** (`2-1`, Blast Motion US10706273). PinPoint has this for free: the host-fused **quaternion**
  gives club orientation at the candidate instant.
- **Dual-range accelerometers** to capture both swing dynamics and the high-g spike without saturation
  (see §2.1).

---

## 4. False-positive rejection — the hard problem (the spine of the design)

No detector fires cleanly on its own. Robustness = **per-modality gates → contextual gates →
cross-modal agreement**:

1. **Per-modality gates** (§2): acoustic exp-decay/spectral-flux/HFC + refractory; IMU clean-spike
   threshold; vision ball-launch.
2. **Contextual gates** (mostly already in PinPoint):
   - **`armed` only** — capturing && processor idle (`ShotController` already enforces this).
   - **Session active / correct session type** (already in `ShotController`/`SessionController`).
   - **Swing-energy / orientation gate** — require the IMU swing signature (angular-velocity ramp into
     transition; club vertical at impact) before accepting an impact, so a **waggle or address tap**
     (low-energy, non-monotonic) is rejected. This reuses the **swing-phase / "No-event" waggle-rejection**
     work (GolfDB/SwingNet's down-weighted No-event class + temporal context; IMU swing-energy gate) from
     the prior research — see the phase notes folded into the analysis design.
   - **Refractory period** (≥40 ms, see §5) to suppress double-triggers and follow-through taps.
3. **Cross-modal arbitration** — require **≥2 modalities to agree within a small time window** where
   more than one is available (e.g. IMU spike **and** acoustic transient within ~50 ms). This is the
   single biggest false-positive reducer and directly rejects **nearby players** (their strike won't
   coincide with *this* player's IMU) and **mat/ground taps** (no acoustic ball-click signature, or no
   ball launch). Degrade gracefully: if only one strong detector is present, allow it to trigger but at a
   higher threshold.

---

## 5. Timestamp accuracy & latency — "confirm-then-pinpoint"

- **Spectral (frequency-domain) detectors are most accurate but temporally coarse** — a fixed STFT-frame
  floor of **≥5.8 ms** (256-sample window @ 44.1 kHz) plus an *unpredictable, sound-dependent* extra
  delay (`high, 3-0`, Rosão DAFx 2018).
- **Run a time-domain detector in parallel to pinpoint** the true onset; **coarse-to-fine** refinement
  reaches **sample-level** timing (fine search within ~20 ms of the coarse onset), with a configurable
  **minimum event separation (~40 ms)** (`high, 3-0`, Rosão + US11133023). Improves timing in ~half of
  detections, avg ~3 ms, max ~12 ms.
- **Cross-source clock alignment:** audio and the ~100 Hz IMU stream are **asynchronous with a variable
  offset of "typically a few 100 ms"** — *"imperative to synchronize before using in conjunction"*
  (`high, 3-0`, arXiv 1805.05456). The paper aligns by **cross-correlating the Audio Peak Function and
  IMU Peak Function** (MAE 32 ms). Detected shots themselves can serve as the alignment reference.

**PinPoint fit:** this maps straight onto the `EventBuffer` + `ShotController` marker. The funnel already
accepts a back-dated `timestampUs`; the recipe is: **IMU coarse instant → acoustic fine instant → set
the shot-marker `timestamp_us` to the acoustic-pinpointed impact.** Decide whether the few-100 ms
audio/IMU offset is handled by **online peak-function cross-correlation** or a **one-time calibration**
given the EventBuffer's existing per-source timestamps (open question — §10).

---

## 6. Recommended architecture for PinPoint

```
                 ┌───────────────── ARMED gate (capturing && processor idle) ─────────────────┐
                 │  + session active/type   + swing-energy/orientation gate   + refractory ≥40ms │
                 └───────────────────────────────────────────────────────────────────────────┘
   IMU (raw accel, club/active-wrist)
     accel-derivative peak > τ ──────────►  CANDIDATE  (Source::Imu, coarse t)  ── fast path
                                                  │
   Acoustic (existing audio PCM)                  ▼  within ~50 ms window?
     envelope-peak + exp-decay/HFC ──────►  CONFIRM  + sample-accurate t*  (Source::Audio)
                                                  │  back-date EventBuffer shot marker → t*
   Vision (BALL_DETECTOR_DESIGN.md)               ▼
     ball present → ballLaunched ────────►  CORROBORATE / VETO  + offline exact impact frame
                                                  │
                                                  ▼
                              ShotController::triggerShot(source, t*)  →  ShotProcessor pipeline
```

**Arbitration policy:** IMU fires the fast candidate; **require acoustic agreement within ~50 ms** to
accept (and to pinpoint the timestamp); vision corroborates/vetoes (ball actually launched) and supplies
the precise impact frame post-hoc. Where a modality is absent, raise the surviving detector's threshold
rather than trust it blindly.

**Code touch-points (all small, the funnel already exists):**
- `ShotController::Source` — add `Audio`; `Imu`, `Ball`, `Pose`, `Manual` already present.
- New `AcousticShotDetector` (parallel consumer of the audio PCM) → `triggerShot(Source::Audio, t*)`.
- IMU impact detector in `ImuInstance` (raw-accel peak) → `triggerShot(Source::Imu, t)`; consider a
  higher output rate for the active sensor.
- Arbitration/dedup lives in `ShotController` (it's already the single funnel) — collapse multiple
  near-simultaneous sources into one shot, choose the acoustic-pinpointed `timestampUs`.
- Vision `ballLaunched` (`BALL_DETECTOR_DESIGN.md`) wired as corroboration/veto, not the primary trigger.

**Phasing (lowest-risk first):**
1. **IMU threshold trigger** (club/active-wrist) under the existing `armed` + new swing-energy/refractory
   gates. Cheapest, biggest single win; no ML, no new sensors.
2. **Acoustic onset detector** on the existing audio + confirm-then-pinpoint timestamping.
3. **Cross-modal arbitration** (IMU∧audio within window) + **vision corroboration/veto**.
4. **ML** only where a clean jerk is unavailable (passive placement), per Silent Impact.

---

## 7. Caveats (read before acting)

- **Racquet-sport transfer.** 95.6% fusion / 98–99% IMU / MS-TCN figures are table-tennis/tennis; no
  golf-specific fusion benchmark confirmed. Treat as directional.
- **Patents are disclosed/optional embodiments** ("may"…), not measured deployed performance; the
  acoustic patents (US7234340, US11133023/ShotSpotter) are general impulsive-acoustic methods applied to
  golf by inference.
- **Vision shot-detection is under-evidenced** — keep it as corroborator/offline, not the trigger, until
  we benchmark it ourselves.
- **100 Hz IMU undersamples the impact peak** — fine for "impact happened," coarse (±~10 ms) for the
  instant; lean on acoustic for the precise timestamp, or raise the active sensor's rate.
- **Small single studies.** arXiv 1805.05456 is ~650 shots / 8 players; UIST '24 and Sensors 2020 are
  single, un-replicated on golf.

---

## 8. Vision-modality detail (folded from the old ball/club note)

The deep ball-detection research and the implementation plan now live in
**[`BALL_DETECTOR_DESIGN.md`](BALL_DETECTOR_DESIGN.md)** (CNN + Kalman patch tracking, model sourcing,
ORT engine decision). For shot detection specifically, the relevant outputs of that work are: the
**ball-present** state (corroboration), the **`ballLaunched`** event (veto practice swings; coarse
launch instant), and the **offline exact impact frame** from the frozen `SwingWindow`. **Club/clubhead
detection remains an open R&D item** (no verified golf-specific evidence) and is not on the shot-trigger
critical path.

---

## 9. Refuted claims — do NOT cite

- ✗ `1-2` — "dominant-arm IMU captures the impact jerk while a passive-arm IMU cannot" (placement
  dependence overstated). *Use the jerk-present vs jerk-absent framing from Silent Impact instead.*
- ✗ `0-3` — "a single wrist IMU locates all four swing points incl. impact at ~5 ms MAE."
- ✗ `1-2` — "wrist IMU impact at 5–16 ms is a *precise impact-instant guarantee* for triggering."
  *Use the conservative 5–16 ms range, and pinpoint with acoustic.*
- ✗ `0-3` — wavelet-signature false-strike rejection attributed to **US10706273**. *The validated
  wavelet-signature evidence is US8903521 only.*

---

## 10. Open questions

1. **Golf-specific** accuracy/false-positive performance of audio+IMU fusion, and whether **club- vs
   wrist-mount** materially changes impact-spike detectability for a golf swing.
2. **Vision shot-detection** SOTA latency/accuracy — can multi-camera confirm a strike fast enough for
   real-time arbitration, or only as a slower post-hoc verifier?
3. **Optimal fusion/arbitration policy** for PinPoint's exact mix (async audio + multiple ~100 Hz
   host-fused IMUs + multi-camera): which modality triggers, which confirms, voting thresholds, and the
   cross-modal agreement window that rejects nearby players.
4. **Clock reconciliation** — handle the few-100 ms audio/IMU offset + per-source drift to back-date the
   `EventBuffer` marker: online peak-function cross-correlation (arXiv 1805.05456) vs a one-time
   calibration, given the EventBuffer's existing per-source timestamps.

---

## 11. Sources

**Multimodal fusion (primary anchor)**
- Sharma et al., *Wearable Audio and IMU Based Shot Detection in Racquet Sports*, arXiv 1805.05456 —
  95.6% audio+IMU fusion; async sync via peak-function cross-correlation. https://arxiv.org/abs/1805.05456

**Inertial**
- Ebner & Findling, *Tennis Stroke Recognition* (ACM MoMM '19, Aalto) — accel-derivative peak detection
  98–99%; club/floor/body follow-through = primary FP source.
  https://ambientintelligence.aalto.fi/paper/Tennis_Stroke_Recognition.pdf
- Jang et al., *Sensors* 2020 20(16):4466 — IMU swing-phase segmentation; impact 5–16 ms.
  https://www.mdpi.com/1424-8220/20/16/4466 · https://pmc.ncbi.nlm.nih.gov/articles/PMC7472298/
- *Silent Impact* (UIST '24) — threshold vs MS-TCN; ML wins only without the jerk.
  https://dl.acm.org/doi/fullHtml/10.1145/3654777.3676403

**Acoustic**
- Rosão et al., *Hard real-time onset detection of percussive sounds* (DAFx 2018) — confirm-then-pinpoint,
  STFT frame floor, refractory.
  https://www.researchgate.net/publication/325541830_Hard_real-time_onset_detection_of_percussive_sounds
- US7234340 — impact-sound detection (band-power onset, exp-decay validation).
  https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/7234340
- US11133023 (ShotSpotter) — envelope-peak + adaptive threshold; spectral-flux/HFC rejection; sample-level
  coarse-to-fine. https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/11133023

**Commercial wearables / launch monitors**
- US8903521 — vibration + speed/range gate + wavelet valid-swing-signature.
  https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/8903521
- US10706273 (Blast Motion) — dual-range accel + club-vertical-at-impact gating.
  https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/10706273

**Vision (under-evidenced; see companion)**
- [`BALL_DETECTOR_DESIGN.md`](BALL_DETECTOR_DESIGN.md) — vision-modality detector implementation + the
  ball-detection research (arXiv 2012.09393 patch+Kalman, GolfDB/SwingNet waggle rejection).

---

### Research provenance
Two deep-research passes (multi-angle search → fetch → 3-vote adversarial verification → synthesis):
ball/swing-phase (folded into the vision section + `BALL_DETECTOR_DESIGN.md`) and this multi-modal
shot-detection pass (**21 confirmed / 4 refuted** of 25 verified, from 18 sources). Confidence/vote tags
reflect that process. Largest residual gaps: **golf-specific** fusion accuracy and **vision** trigger
latency — both flagged in §10.
