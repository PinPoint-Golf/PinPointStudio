# Pinpoint Shot Detector — Developer Guide

**Audience**: Developers working on or integrating with the Pinpoint application  
**Location**: `src/IMU/impact_detector.h`, `src/Audio/onset_detector.h` + `acoustic_shot_detector.{h,cpp}`, `src/Gui/shot_arbiter.h` + `shot_controller.{h,cpp}`  
**Language**: C++17 (detector math headers) / C++20 (app integration)  
**Status**: P1–P3 implemented and unit-tested. P4 (per-source latency auto-calibration) pending; on-hardware field tuning pending.

---

## Contents

1. [What Shot Detection Is](#1-what-shot-detection-is)
2. [Where It Fits in Pinpoint](#2-where-it-fits-in-pinpoint)
3. [Core Concepts](#3-core-concepts)
4. [The Timestamp Model — Latency-Aware Back-Dating](#4-the-timestamp-model--latency-aware-back-dating)
5. [The IMU Impact Detector](#5-the-imu-impact-detector)
6. [The Acoustic Onset Detector](#6-the-acoustic-onset-detector)
7. [The Arbiter — Candidate → Hold → Fuse → Commit](#7-the-arbiter--candidate--hold--fuse--commit)
8. [ShotController Integration](#8-shotcontroller-integration)
9. [Getting Started — Adding a New Detector Modality](#9-getting-started--adding-a-new-detector-modality)
10. [Configuration and Settings](#10-configuration-and-settings)
11. [Internals — Design Decisions Explained](#11-internals--design-decisions-explained)
12. [Testing](#12-testing)
13. [Common Mistakes](#13-common-mistakes)
14. [File Map](#14-file-map)

---

## 1. What Shot Detection Is

Shot detection answers one question, hands-free: **"a golf ball was just struck —
at exactly what instant?"** The answer drives the entire post-shot pipeline: the
shot marker written into the EventBuffer, the frozen `SwingWindow`, analysis,
export, and the on-screen replay all align to the committed impact timestamp.

It is a **multi-modal** problem by design (`docs/design/shotdetection.md`): no single
sensor is reliable enough alone. The implementation fuses three independent
detectors behind one funnel:

| Modality | Detector | Strength | Weakness |
|---|---|---|---|
| **Acoustic** | `OnsetDetector` (impact "click") | Sample-accurate pinpoint; small, stable capture latency | Fires on any sharp sound near the mic |
| **IMU** | `ImpactDetector` (shaft shock + swing energy) | Strong evidence a *swing* happened | ±5 ms sampling at 200 Hz; variable BLE latency; ±16 g clipping |
| **Vision** | ball launch (future — see §9 note) | Confirms a ball actually left | Frame-rate coarse; detector latency |

Each detector emits a *candidate* — an estimated true-impact instant plus a
confidence. The **arbiter** collects candidates in a short hold window and
commits at most one shot: when two modalities agree, or when one is decisively
strong. The manual SHOT button bypasses all of this and commits directly.

Shot detection is **not** swing analysis. It decides *that* and *when* a shot
happened; everything downstream of `shotDetected` (window capture, metrics,
scoring) belongs to the shot analyzer — see
`docs/developer/shot_analyzer_developer_guide.md`.

---

## 2. Where It Fits in Pinpoint

```
┌────────────────────────────────────────────────────────────────────────────┐
│  Pinpoint Application                                                      │
│                                                                            │
│  [WT9011DCL_BLE] ─quaternionUpdated─► [ImuInstance]                        │
│                                        ImpactDetector (math)               │
│                                        │ impactDetected(est_t, conf)       │
│                                        ▼                                   │
│                                    [ImuManager] ──────────┐                │
│                                                           │ (gated on      │
│  [AudioInput] ──audioDataReady──► [AcousticShotDetector]  │  autoDetect-   │
│   (audio thread)                   OnsetDetector (math)   │  Swing)        │
│                                    │ impactDetected       │                │
│                                    ▼                      ▼                │
│                         [TranscriptionController] ─► [ShotController]      │
│                                                       reportCandidate()    │
│                                                       ┌──────────────┐     │
│  [SHOT button] ────triggerShot()──────────────────────►  ShotArbiter │     │
│   (manual — bypasses the hold)                        │  hold/fuse   │     │
│                                                       └──────┬───────┘     │
│                                                              │ commitShot  │
│                                              writeShotMarker │             │
│                                       ┌──────────────┐      ▼             │
│                                       │ EventBuffer  │  shotDetected ───►  │
│                                       │ shot_marker  │  [ShotProcessor]    │
│                                       └──────────────┘  post-roll → freeze │
│                                                          → analyse ∥ export│
└────────────────────────────────────────────────────────────────────────────┘
```

### The detection workflow

1. **Capture running**: the user presses Capture; the EventBuffer is `Capturing`
   and `ShotController::armed` is true.
2. **Swing happens**: the IMU on the club shaft sees a gyro-energy ramp through
   the downswing, then an accelerometer shock at impact; the microphone hears
   the impact click a few milliseconds of latency later.
3. **Candidates**: each detector independently back-dates its arrival stamp by
   its own latency and calls `ShotController::reportCandidate(source, est_t, conf)`.
4. **Hold**: the first candidate opens a 200 ms arbiter window. Other modalities'
   candidates for the same strike land inside it.
5. **Commit**: at the deadline the arbiter fuses: two agreeing modalities (or one
   strong one) → `commitShot()` writes the `shot_marker_v1` entry with the
   **authoritative** timestamp (acoustic when present) and emits `shotDetected`.
6. **Pipeline**: `ShotProcessor` runs post-roll → pause → `captureSwingWindow()`
   → analysis ∥ export → replay. Its `busy` state disarms the trigger for the
   whole pipeline, and the arbiter's own refractory absorbs echoes around the
   edges.

---

## 3. Core Concepts

### Modality

An independent physical evidence channel: IMU shock, acoustic onset, vision
launch. The arbiter only counts *distinct* modalities as corroboration — two
acoustic onsets are one voice, not two.

### Candidate

`{source, est_t_us, confidence}` — a detector's claim that an impact occurred at
`est_t_us` (already back-dated, see §4) with gate strength `confidence` in
[0, 1]. Candidates are cheap; commits are expensive. Detectors should report
freely within their own gates and let the arbiter decide.

### The armed gate

`ShotController::armed()` is true only while the buffer is Capturing, the
`ShotProcessor` is idle, and session review is not active. Every path —
`triggerShot()`, `reportCandidate()`, and the final `commitShot()` — checks it,
so all sources inherit the gate and a candidate that arrives mid-pipeline is
dropped, not queued.

### The hold window

The arbiter is a tiny state machine: **Idle → Collecting → Decide**. The first
candidate opens the window (`holdMs` = 200 ms, driven by a single-shot `QTimer`
in `ShotController`); the decision happens once, at the deadline, never earlier.
Latency cost: a real shot commits ~200 ms after the first detector fires —
invisible in practice, because the `ShotProcessor` post-roll waits 500 ms anyway.

### Authoritative timestamp

When modalities agree, their `est_t` values differ by a few ms. The committed
timestamp comes from the highest-authority agreeing modality — **Acoustic >
Imu > Ball** — because audio pinpoints (sample-accurate onset, stable latency)
while IMU/vision confirm (sampling-rate and transport-latency coarse). The
`ArbSource` enum order *is* the priority order.

### Refractory

Three layers prevent double-fires: each detector has its own refractory
(IMU 200 ms, acoustic 35 ms — tuned to its physics), the arbiter has a
1.5 s post-commit refractory, and `ShotProcessor::busy` disarms everything for
the pipeline's duration. They overlap deliberately: the busy gate has edges
(post-roll start, replay end) the refractory covers.

### The shot marker

Every commit writes a 16-byte `ShotMarker` into the EventBuffer (source
`"shot_controller"`, schema `"shot_marker_v1"`) **before** emitting
`shotDetected`, with the entry's `timestamp_us` set to the impact instant
itself. The frozen `SwingWindow` then locates impact via `entriesFor(markerId)`
without payload parsing. Back-dated timestamps are safe here: per-source
monotonicity cannot be violated at shot cadence.

---

## 4. The Timestamp Model — Latency-Aware Back-Dating

This is the linchpin of the whole design. **Every EventBuffer source is stamped
at host-arrival time on one `steady_clock` (`EventBuffer::nowMicros()`), with no
latency compensation in the buffer itself.** A camera frame, an IMU packet and
an audio buffer that describe the same physical instant arrive at different
times — each delayed by its own capture chain.

Detectors therefore emit an *estimated true-impact instant*:

```
est_t_us = arrival_stamp_us − per_source_latency_us
```

| Source | Arrival stamp | Latency constant | Where it lives |
|---|---|---|---|
| IMU | `nowMicros()` at the top of the GUI-thread packet handler | `ImuInstance::kImuBleLatencyUs` (30 ms) | `imu_instance.h` |
| Acoustic | `nowMicros()` at buffer **receipt** on the audio thread, minus `samplesAfterOnset/rate` back to the onset sample | `AppSettings::audioDeviceLatencyUs` (20 ms default, persisted) | `app_settings.h` |

The acoustic path is the precision channel: the onset sample index is exact
(the truth-table test demonstrates 0-sample error), so its only error is the
device-latency constant. The IMU's BLE connection-interval jitter makes its
constant softer — which is exactly why the arbiter's match tolerance is ±40 ms
and the acoustic timestamp wins when both agree.

**Until P4**, both constants are fixed estimates. P4 replaces them with
cross-correlation auto-calibration (peak-function alignment of simultaneous
IMU/audio observations of the same strikes). The constants are deliberately
*passed into* the detector configs, never hard-coded in the math, so P4 is a
plumbing change only.

**Rules:**
- Stamp arrival time **first**, before any processing, on the thread the data
  arrives on.
- Compute `est_t` **before** any thread hop — a queued connection adds
  scheduling jitter that must not contaminate the estimate.
- Use only `EventBuffer::nowMicros()`. The committed timestamp must live on the
  same timeline as the ring entries it will be matched against.

---

## 5. The IMU Impact Detector

**Math**: `src/IMU/impact_detector.h` — `pinpoint::ImpactDetector`, header-only,
no Qt, no allocation in `push()`. **Live hook**: `ImuInstance` (GUI thread).

### The gates

A sample stream of `{accelMag (g), gyroMag (°/s), clubVertical, t_us}` is pushed
per IMU packet. An impact fires only when **all** gates pass:

| Gate | Default | Rejects |
|---|---|---|
| **Local max** — strict rise in, level-or-fall out (`>=` lets a clipped ±16 g plateau fire on its first sample) | — | mid-rise samples |
| **Adaptive threshold** — peak ≥ max(`accelFloorG` 4 g, `accelAdaptive` 3 × slow EMA of \|a\|) | 4 g floor | normal swing accelerations |
| **Jerk gate** — rise rate into the peak ≥ `jerkMinGps` (100 g/s) | 100 g/s | slow swells that crest above threshold without a strike's sharp edge |
| **Swing-energy gate** — in the `gyroWindowMs` (400 ms) window ending at the peak: max \|gyro\| ≥ `gyroPeakMinDps` (300 °/s) AND mean ≥ `gyroMeanMinDps` (80 °/s); requires ≥ half a window of history (no firing right after connect) | 300/80 °/s | mat/ground taps and address knocks — an accel spike with a flat gyro is not a swing |
| **Club-orientation gate** — `clubVertical` ≥ `clubVerticalMin` (0.35); the weakest-evidenced gate, permissive and disableable (`orientationGate=false`) while tuning | 0.35 | strikes claimed at implausible shaft attitudes |
| **Refractory** — `refractoryMs` (200 ms) since the last accepted impact | 200 ms | follow-through double-hits |

All windows are **milliseconds evaluated against sample timestamps**, never
sample counts — switching the sensor between 100 Hz and 200 Hz does not change
behaviour (test G proves it).

**Confidence** derives from swing energy (gyro peak over its threshold: 0.5 at
the gate, 1.0 at 2×), **never from peak g** — the WT9011's ±16 g full scale
clips real strikes, so amplitude is a binary "over threshold", not a magnitude.

### The live hook

The detector is driven from the existing GUI-thread `quaternionUpdated` handler
in `imu_instance.cpp` — the last signal per packet, so the raw accel/gyro caches
and the fused quaternion are all current:

- `gyroMag` comes from `m_imu->gyroData()` (raw, synchronous) — **not**
  `angularVelocityDps`, which is quaternion-derived, queued, and coarse.
- `clubVertical = |z|` of the fused quaternion rotating the sensor long axis
  (+Y, see `docs/design/imu_frame_contract.md`) into the +Z-up world frame.
- The hook adds **no new EventBuffer producer** — the producer/stop-barrier
  contract (CLAUDE.md) is untouched. The whole BLE chain runs on the GUI
  thread, so `impactDetected` → `ImuManager` → `main.cpp` → `ShotController`
  is a plain same-thread call chain.

### Rate

`ImuManager::createInstance()` defaults new devices to **200 Hz** (`Hz_200`)
for sharper detection (±10 ms timing at 100 Hz risks attenuating the sub-ms
shock between samples). Persisted per-device rates still win. 200 Hz BLE
throughput stability is an open hardware-verification item: watch `dataRateHz`
≈ 200 and a flat `gimbalDropCount`.

---

## 6. The Acoustic Onset Detector

**Math**: `src/Audio/onset_detector.h` — `pinpoint::OnsetDetector`, header-only,
no Qt. **Wrapper**: `AcousticShotDetector` (thin QObject), owned by
`TranscriptionController`, living on its **audio thread**.

### The per-sample pipeline (time-domain only)

```
mono float ─► one-pole high-pass (1 kHz)   impact energy is high-frequency;
        │                                  rejects rumble + speech fundamentals
        ▼
   instant-attack / exponential-release    "how loud right now", collapses
   envelope (10 ms release)                within ms after a click
        ▼
   adaptive threshold                       8 × noise-floor EMA (τ 500 ms,
   crossed FROM BELOW (attack-only)         frozen while tracking a candidate)
        ▼
   exponential-decay gate                   at +30 ms the envelope must have
   (decayRatioMax 0.5)                      dropped below half its peak —
        ▼                                   speech and tones fail this
   refractory 35 ms                         min inter-onset spacing
```

Confirmation is delayed by `decayWindowMs` (30 ms), but the reported onset is
the **first threshold-crossing sample** — that is what makes audio the
sample-accurate pinpoint modality. A second hit inside the decay window folds
into the same candidate (peak update) rather than double-firing.

The **attack-only crossing** rule (`m_wasBelow`) is load-bearing: without it,
the abrupt *end* of a sustained tone re-candidates while the envelope is still
high, and then passes the decay gate — because the envelope genuinely does
collapse at a cutoff. Only a rise from below the threshold can open a
candidate. See test B ("tone CUTOFF rejected").

### The wrapper and threading

`AcousticShotDetector` is the **second consumer** of
`AudioInputBase::audioDataReady` — the `AudioStreamSaver` pattern. It runs at
the device's **native rate** (44.1/48 kHz; never STT's 16 kHz downmix, which
would shave off the click's high-frequency content), in parallel with STT, and
adding it never disturbs the STT pipeline (the signal is a fan-out; STT's
silence gating does not throttle the raw stream).

`onAudioData()`:
1. Stamps `recvNow = nowMicros()` **first** (it runs on the audio thread via a
   same-thread direct connection — this stamp is the precision anchor).
2. On format change, `reset(sampleRate)` re-derives the per-sample coefficients.
3. Converts channel 0 of each frame to float (Int16/Int32/UInt8/Float
   supported) and pushes through the detector.
4. After the whole buffer: for each confirmed onset,
   `est_t = estimateImpactUs(recvNow, samplesFromOnsetToBufferEnd, rate, latency)`
   and `emit impactDetected(est_t, conf)`.

The emit happens **on the audio thread** with `est_t` already computed.
`TranscriptionController` forwards it signal-to-signal (still the audio
thread — documented on the signal); the main.cpp connection's `&shotController`
context provides the queued hop onto the GUI thread. The latency constant is a
`std::atomic` so the GUI thread can update it from `AppSettings` while the
audio thread reads it.

---

## 7. The Arbiter — Candidate → Hold → Fuse → Commit

**Math**: `src/Gui/shot_arbiter.h` — `pinpoint::ShotArbiter`, header-only, no
Qt, time injected (`nowUs` parameters) so it is fully unit-testable.
`ShotController` owns one and supplies the `QTimer`.

```cpp
// First report opens the window (returns true → owner starts its timer):
bool opened = arbiter.report({ArbSource::Imu, est_t, conf}, nowUs);

// At the deadline (and only then):
ShotArbiter::Decision d = arbiter.decide(nowUs);
if (d.commit)  commitShot(fromArbSource(d.src), d.t_us);
```

### The decision policy (`decide()`)

1. Reduce to the **highest-confidence candidate per modality** (at most 3).
2. **Agreement pass**, in authority order (Acoustic, Imu, Ball): for each
   present modality, count the other modalities whose best candidate lies
   within `matchTolMs` (±40 ms) of it. The first with ≥2 agreeing wins —
   commit with **its** timestamp, `conf` = max over the agreeing set.
3. **Lone-strong pass**: no agreement → the highest-authority candidate with
   `conf ≥ strongConf` (0.8) commits alone. Its detector's own gates already
   passed decisively; this is the "1 strong + gated" rule.
4. Otherwise: no commit. The window simply evaporates.

Either way the candidate set is cleared (`decide()` always returns to Idle) and
a commit arms the **refractory** (`refractoryMs` 1.5 s): candidates reported
inside it are dropped at `report()` time — they never even open a window.

### Interaction with the rest of ShotController

- **Manual bypass**: `triggerShot()` (the SHOT button) stops the hold timer,
  cancels any pending window, calls `noteCommit(now)` so the refractory still
  covers auto candidates around the manual shot, then commits directly.
- **Disarm voids the window**: `reevaluateArmed()` on a transition to disarmed
  (capture stopped, processor busy, review entered) stops the timer and
  `cancel()`s the arbiter — a window opened while armed must not commit after
  the world changed.
- **Re-check at commit**: `commitShot()` re-checks `armed()`; the processor may
  have gone busy mid-hold.

Everything runs on the GUI thread — no locks anywhere in the chain.

---

## 8. ShotController Integration

`ShotController` (QML context property `shotController`) is the single funnel.
Its public surface, after P3:

```cpp
// Direct commit — manual path. Bypasses the hold; timestampUs −1 = "now".
Q_INVOKABLE void triggerShot(Source source = Source::Manual, qint64 timestampUs = -1);

// Auto-detector funnel — candidates flow through the arbiter.
void reportCandidate(Source source, qint64 estImpactUs, float confidence);

// The one output everything downstream hangs off:
signals: void shotDetected(Source source, qint64 timestampUs, int sessionType);
```

`Source` is `{Manual, Imu, Pose, Ball, Acoustic}`. The session type is captured
at the moment of commit (`SessionController::activeSessionType()`), carried in
both the signal and the marker payload, and consumed by the analyzer factory.

The main.cpp wiring (one lambda per modality, both gated on the
`autoDetectSwing` setting):

```cpp
QObject::connect(&imuManager, &ImuManager::impactDetected, &shotController,
                 [&](qint64 estImpactUs, float conf) {
    if (appSettings.autoDetectSwing())
        shotController.reportCandidate(ShotController::Source::Imu, estImpactUs, conf);
});
```

`ShotProcessor` applies a per-source **post-roll** before freezing the buffer
(`postRollMsFor(Source)`, all 500 ms today) so the follow-through lands in the
ring — a new source must be added to that switch.

---

## 9. Getting Started — Adding a New Detector Modality

The intended fourth modality is vision: a `ballLaunched(qint64 timestampUs)`
signal from a Kalman-tracked ball detector (`docs/design/ball_detector_design.md` §8).
**It does not exist yet** — today's `CameraInstance::ballPresentChanged` is
smoothed over a 50-frame window, seconds too coarse for a ±40 ms match
tolerance. Do not wire it as a candidate source. When the tracked detector
lands, the full integration is:

```cpp
// 1. Detector math: a pure header under the owning subsystem
//    (src/Pose/launch_detector.h), tested standalone. Emit an estimated
//    true-impact instant: arrival nowMicros() − frame/detector latency,
//    and a confidence that is NOT saturated (the arbiter's lone-strong
//    threshold is 0.8 — reserve >0.8 for genuinely decisive evidence).

// 2. Surface a signal from the owning controller (the ImuInstance /
//    TranscriptionController precedent): compute est_t on the thread the
//    data arrives on, BEFORE any queued hop.

// 3. Wire it in main.cpp behind the same setting gate:
QObject::connect(&cameraManager, &CameraManager::ballLaunched, &shotController,
                 [&](qint64 estImpactUs, float conf) {
    if (appSettings.autoDetectSwing())
        shotController.reportCandidate(ShotController::Source::Ball, estImpactUs, conf);
});

// 4. The arbiter already understands it: Source::Ball maps to ArbSource::Ball
//    (lowest timestamp authority — vision corroborates, never pinpoints).
//    A genuinely NEW modality instead needs: an ArbSource enum entry (enum
//    order = authority order), a Source enum entry + toArbSource/fromArbSource
//    cases (shot_controller.cpp), a postRollMsFor case (shot_processor.cpp),
//    and arbiter_test cases for its authority position.

// 5. Add truth-table tests for the detector math (see §12) and field-verify
//    before shipping it with any weight.
```

What you do **not** do: pause/resume the buffer, write EventBuffer entries,
debounce, or check `armed` in the detector — `ShotController` owns all of that.
A detector's entire job is `(est_t, conf)` candidates within its own gates.

---

## 10. Configuration and Settings

### User-facing (`AppSettings`, Settings → General)

| Setting | Key | Default | Effect |
|---|---|---|---|
| Auto-detect swing | `General/autoDetectSwing` | **ON** (since P3) | Master gate on both auto wirings in main.cpp. OFF = manual SHOT only. |
| Swing detection sensitivity | `General/swingDetectionSensitivity` | "Medium" | Low/Medium/High → IMU `thresholdScale` 1.5/1.0/0.7 (`ImuManager::impactScaleFor`, live-updated). >1 = less sensitive. |
| Audio device latency | `General/audioDeviceLatencyUs` | 20000 | Acoustic back-dating constant; forwarded to the detector atomically, live-updated. |

The default flipped OFF→ON at P3: a single modality auto-trigger was judged too
false-positive-prone to default on, but with cross-modal confirmation (or a
decisively-gated lone detector) it is acceptable. If field tuning proves
otherwise, flip `app_settings.h` line ~142 back.

### Detector constants (code)

`ImpactDetectorConfig`, `OnsetDetectorConfig` and `ArbiterConfig` are plain
structs with inline defaults — see §5/§6/§7 tables. They are constructed with
defaults in their owners; there is deliberately no settings plumbing for the
inner thresholds yet (they need field data before they deserve UI). The two
latency constants and the sensitivity scale are the only externally-fed values.

---

## 11. Internals — Design Decisions Explained

### Why the detectors are header-only pure math

Every gate decision is testable without BLE hardware, an audio device, Qt
signals, or the app build. The standalone suites (§12) run the full truth
tables in milliseconds. The live hooks (`ImuInstance`, `AcousticShotDetector`)
contain *no* detection logic — only sampling, frame conversion, and timestamp
plumbing. Keep it that way: a tuning change must be provable in a test before
it touches a device.

### Why the IMU decision is delayed by exactly one sample

A local maximum needs one sample of lookahead (`peak > prev && peak >= cur`).
At 200 Hz that is 5 ms — irrelevant against the 200 ms arbiter hold. The
alternative (fire on threshold crossing) triggers on the rising edge of slow
swells; the jerk gate plus local-max together encode "a strike is a sharp edge,
not a crest".

### Why the IMU noise floor is an EMA, and the acoustic floor freezes

The IMU adaptive threshold tracks a slow EMA (τ 1 s) of |a| — a one-sample 16 g
spike at 200 Hz moves it by ~0.075 g, negligible, so no freeze is needed. The
acoustic floor has the opposite problem: a 30 ms impact burst is *thousands* of
loud samples at 48 kHz, enough to inflate a 500 ms-τ EMA and raise the
detector's own threshold mid-candidate — so the floor is frozen while tracking.

### Why the arbiter takes `nowUs` as a parameter

Time injection makes `decide()`/`report()`/refractory behaviour deterministic
in tests (no sleeps, no mock clocks). `ShotController` passes
`EventBuffer::nowMicros()` — the same clock as every candidate timestamp, so
refractory arithmetic and hold deadlines live on the one timeline.

### Why candidates carry confidence but commits mostly ignore it

Confidence has exactly two jobs: the lone-strong threshold (0.8) and tie
metadata in the decision log. Agreement between independent modalities is far
stronger evidence than any single detector's self-assessed confidence, so the
fuse rule is structural (count distinct agreeing modalities), not a weighted
confidence sum. Resist the urge to "improve" it into one — that reintroduces
single-modality false positives through the back door.

### Threading map

| Piece | Thread | Why |
|---|---|---|
| `ImpactDetector::push` | GUI | BLE chain originates there (`QLowEnergyController` parented, no `moveToThread`) |
| `OnsetDetector::push` | Audio | data arrives there; receipt stamp must be taken there |
| `ShotArbiter`, `ShotController`, hold timer | GUI | single-threaded by construction — no locks |
| `est_t` computation | producer thread | before any queued hop (jitter must not contaminate the estimate) |

---

## 12. Testing

Three standalone CTest suites — the `src/Analysis/tests` convention (own
`main()`, printf CHECK macros, not in the root build):

```bash
# IMU impact detector truth table
cmake -S src/IMU/tests -B build/imu-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
cmake --build build/imu-tests -j && ctest --test-dir build/imu-tests --output-on-failure

# Acoustic onset detector truth table
cmake -S src/Audio/tests -B build/audio-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
cmake --build build/audio-tests -j && ctest --test-dir build/audio-tests --output-on-failure

# Arbiter decision table (lives in the analyzer suite — Gui-header precedent)
cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
cmake --build build/analyzer-tests -j && ctest --test-dir build/analyzer-tests --output-on-failure
```

The truth tables **are** the subsystem's value — every gate exists to kill a
specific false-positive class, and each class has a named test:

| Suite | Cases |
|---|---|
| `impact_detector_test` | strike fires exactly once with back-dated `est_t`; mat tap (spike, flat gyro) rejected; waggle rejected; slow swell rejected (jerk gate); refractory collapses double-hits; orientation gate on/off; 100 Hz ≡ 200 Hz; startup guard; sensitivity scale accepts/rejects a weak swing |
| `acoustic_shot_detector_test` | click fires once, sample-accurate (0-sample error); sustained tone rejected at start (decay gate) AND at cutoff (attack-only rule); speech-like bursts rejected; ambient noise rejected; 20 ms double-click → one onset, 100 ms → two; `estimateImpactUs` math |
| `arbiter_test` | 2-modal agree → one commit, acoustic timestamp; lone-weak reject; lone-strong commit; Imu > Ball authority; disagreement beyond ±40 ms; refractory drops echoes; manual `noteCommit` arms refractory; `cancel()` voids a window |

### Hardware verification (pending — the field checklist)

- Real strikes fire with `autoDetectSwing` on; mat taps and waggles do not.
- 200 Hz BLE stability: `dataRateHz` ≈ 200, `gimbalDropCount` flat.
- Acoustic latency: mic by the impact, compare the committed acoustic timestamp
  against the simultaneous IMU spike, tune `audioDeviceLatencyUs`.
- The real acceptance metric: false positives counted over a full range
  session.

A headless middle tier also exists: replay a recorded BLE trace
(`beginRawDump`/`endRawDump`) into `ImuInstance` under
`QT_QPA_PLATFORM=offscreen` and assert exactly one `shot_marker_v1` with
`source == Imu` via `SwingWindow::entriesFor`.

---

## 13. Common Mistakes

### Wiring a detector to `triggerShot()` instead of `reportCandidate()`

`triggerShot()` is the manual path: it bypasses the hold, cancels pending
windows and commits immediately. An auto detector wired there reintroduces
single-modality false positives and silently discards other modalities'
corroboration. P3 re-pointed the P1/P2 connections for exactly this reason.

### Computing `est_t` after a queued hop

A `Qt::QueuedConnection` adds event-loop scheduling jitter (ms-scale under
load). The estimate must be fixed on the producer thread; only the already-
computed number may cross threads.

### Deriving IMU confidence from peak acceleration

The ±16 g full scale clips real strikes — a clean strike and a mishit can both
read 16 g. Amplitude is a gate, not a magnitude. Confidence comes from swing
energy.

### Gating inference at the FrameThrottle / pausing the buffer from a detector

Detection is **signal-only**. Detectors never pause, resume, or replay the
buffer — buffer state is owned exclusively by the user capture intent
(CLAUDE.md "Capture intent"), and the post-shot freeze belongs to
`ShotProcessor`.

### Adding a hot-path mutex or a new EventBuffer producer to a detector hook

The IMU hook deliberately rides the existing GUI-thread display handler, not
the `Qt::DirectConnection` ring-write lambda — the EventBuffer producer
contract (stop barriers, no mutexes on `acquireWriteSlot`) stays untouched. A
detector needs no ring access at all.

### Expressing detector windows in sample counts

`refractorySamples = 20` means 200 ms at 100 Hz and 100 ms at 200 Hz — a silent
behaviour change on a rate switch. Every window in this subsystem is
milliseconds against sample timestamps; keep new ones that way (the
rate-independence test will catch you).

### Trusting `ballPresentChanged` as a launch signal

It is smoothed over a 50-frame window for UI stability — present→absent lags
the strike by seconds. Inside a ±40 ms match tolerance that is worse than no
candidate at all (it can only miss the window or, worse, match a *different*
strike).

### Re-using the acoustic detector at STT's 16 kHz

The impact click's energy is high-frequency; the 16 kHz STT feed (and its
silence gating) destroys both the detection SNR and the sample-accurate
timing. The detector subscribes to the raw native-rate `audioDataReady` fan-out
for a reason.

---

## 14. File Map

```
src/IMU/
├── impact_detector.h           ImpactDetector — pure gate math (P1)
└── tests/
    ├── CMakeLists.txt          Standalone CTest (build/imu-tests)
    └── impact_detector_test.cpp

src/Audio/
├── onset_detector.h            OnsetDetector + estimateImpactUs — pure math (P2)
├── acoustic_shot_detector.h    Thin QObject wrapper (audio thread)
├── acoustic_shot_detector.cpp  Receipt stamping, format conversion, emit
└── tests/
    ├── CMakeLists.txt          Standalone CTest (build/audio-tests)
    └── acoustic_shot_detector_test.cpp

src/Gui/
├── shot_arbiter.h              ShotArbiter — hold/fuse/commit math (P3)
├── shot_controller.h           ShotController — armed gate, triggerShot,
├── shot_controller.cpp           reportCandidate, commitShot, shot marker
├── imu_instance.{h,cpp}        IMU live hook + impactDetected + kImuBleLatencyUs
├── imu_manager.{h,cpp}         Signal forward + sensitivity mapping + 200 Hz default
├── transcription_controller.*  Owns AcousticShotDetector on the audio thread
├── app_settings.h              autoDetectSwing, swingDetectionSensitivity,
│                                 audioDeviceLatencyUs
└── main.cpp                    The two gated reportCandidate wirings

src/Analysis/tests/
└── arbiter_test.cpp            Arbiter decision table (in the analyzer suite)
```

---

*For the research survey behind the multi-modal approach see
`docs/design/shotdetection.md`; for the design this implements (including the P4
roadmap: latency auto-calibration, optional audio-in-ring, ML for jerk-less
sensor placements) see `docs/implementation/shot_detection_impl.md`. Downstream of
`shotDetected`, see `docs/developer/shot_analyzer_developer_guide.md`.*
