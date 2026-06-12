# Shot Detection — Implementation Plan

Operationalizes the design in [`shotdetection.md`](../design/shotdetection.md): build the **multi-modal shot
trigger** — inertial (IMU) + acoustic (microphone) detectors with vision corroboration — behind the
existing `ShotController`, with a **candidate→arbitrate→commit** layer and **latency-aware
timestamping**. The vision modality's detector is specified separately in
[`ball_detector_design.md`](../design/ball_detector_design.md); this doc covers the inertial + acoustic detectors,
the arbiter, and the timing model.

Status: **plan / not started.** Every file path, signature, threading and contract claim below is
grounded in the current code (file:line).

---

## 0. Goals & non-goals

**Goals**
- A robust, low-false-positive shot trigger that fires `ShotController::triggerShot(...)` at the
  **true impact instant**, fusing IMU + acoustic (+ vision corroboration).
- **Sample-accurate impact timestamp** to anchor the EventBuffer shot marker (and thus the SwingWindow).
- **Drop-in to the existing funnel** — `ShotController`, the `shot_marker_v1` source, the `armed` gate,
  and `ShotProcessor` are reused unchanged except for an added arbitration layer and a `Source::Acoustic`.

**Non-goals**
- Vision *detector* internals (→ `ball_detector_design.md`); here vision is only a corroboration/veto input.
- Ball-flight physics / launch metrics.
- Replacing the manual SHOT button — it stays as an immediate, definitive trigger that bypasses arbitration.

---

## 1. The one fact that shapes the whole design — the clock model

`EventBuffer::nowMicros()` is a **single monotonic `steady_clock` in µs**, shared by *every* source
(`src/Buffer/event_buffer.cpp:313-316`). Every source stamps its `IndexEntry.timestamp_us` at
**host-arrival time** (`acquireWriteSlot`/`publish`), **not** capture time, and there is **no per-source
latency compensation** anywhere (camera `camera_instance.cpp:1485,1517`, IMU `imu_instance.cpp:277`,
marker `shot_controller.cpp:138`). Implications:

1. **Cross-modal timestamps are directly comparable** — fusion can compare instants without clock sync.
   The "few-hundred-ms audio/IMU offset" from the literature is not a clock skew here; it's each
   modality's **pipeline latency** baked into the arrival stamp.
2. **Each modality's arrival stamp lags the true impact** by its own latency: **IMU by the BLE
   transmission delay (tens–hundreds of ms, variable)**; **audio by the QAudioSource buffer period (tens
   of ms, stable)**; camera by ~frame latency. So "back-date the marker to the IMU sample's timestamp"
   is itself wrong by the BLE lag.
3. **Therefore: audio pinpoints, IMU/vision confirm.** Audio's host latency is small and stable, so —
   stamped at receipt and corrected for the in-buffer sample offset + a one-time device-latency constant
   — it gives the best estimate of the true impact instant. This is "confirm-then-pinpoint" made concrete.
4. **Back-dating the marker is safe** — the merger enforces only *per-source* monotonicity
   (`event_buffer.cpp` `enforceMonotonicity`), the marker source is sporadic, and no asserts guard a
   back-dated value. `*slot.timestamp_us = impactUs` already works (`shot_controller.cpp:138`).
5. **Window alignment is a nearest-`timestamp_us` scan** across sources (`swing_exporter.cpp:171-174`,
   `swing_window.cpp`), valid because all sources share the domain.

**Design consequence:** introduce a small **per-source latency calibration** (constants, later
measurable) and have each detector emit an *estimated true-impact instant* = `arrival_now − latency`.
The arbiter compares those and commits the **audio-derived** instant when available.

---

## 2. Architecture — the arbiter is the new core

```
 IMU impact detector (ImuInstance, GUI thread)
   |accel|>τ & swing-energy & club-orient ──► reportCandidate(Imu,  est_t, conf)
 Acoustic onset detector (audio-thread consumer of audioDataReady)
   envelope-peak + decay/HFC gate ─────────► reportCandidate(Acoustic, est_t*, conf)   [pinpoint]
 Vision (ball_detector_design.md)
   ballLaunched / ball-was-present ────────► reportCandidate(Ball, est_t, conf)         [confirm/veto]
                         │
                         ▼
         ┌──────────────────────────────────────────────────────────────┐
         │  ShotController ARBITER  (new)                                 │
         │  • armed gate (capturing && !busy && !review) — already exists │
         │  • first candidate opens a HOLD window (~150–250 ms wall)      │
         │  • collect candidates; match by est-impact within ±tol         │
         │  • decide: ≥2 modalities agree  OR  1 strong + gates           │
         │  • choose authoritative timestamp: Acoustic > Imu > Ball       │
         │  • commit ONCE → writeShotMarker(t*) + emit shotDetected       │
         │  • refractory after commit (busy gate covers most)            │
         └──────────────────────────────────────────────────────────────┘
                         │
                         ▼   (unchanged)
              ShotProcessor: postroll → freeze → analyse ∥ export → replay
```

The **manual SHOT button** keeps calling `triggerShot(Manual)` → immediate commit, bypassing the
arbiter (definitive user intent).

---

## 3. Component — IMU impact detector

**Where:** inside `ImuInstance`, hung off `WT9011DCL_Base::quaternionUpdated` (the last signal per
packet, so accel/gyro are cached and the fused quaternion is in hand — `wt9011dcl_base.cpp:216-255`,
`imu_instance.cpp:263-280`). The whole IMU stack is on the **GUI thread** (no `moveToThread`; BLE
notifications are marshaled there), so it can call `ShotController` directly. Keep the math in a
**header-only `src/IMU/impact_detector.h`** (a pure struct, unit-testable without Qt/BLE).

**Algorithm (per sample, 100 Hz):**
- `g = |accelData()|` (accel is in **g**, rotation-invariant so the live/remapped axes give the same
  magnitude). Track a short ring of recent samples.
- Candidate when `g` exceeds an adaptive threshold **and** is a local max (a jerk/derivative peak is the
  literature's choice — Ebner & Findling — and rejects slow swells), with a per-detector refractory
  (~200 ms) so follow-through taps don't double-fire.
- **Gates (cheap, in-handler):** require a preceding **swing-energy signature** (recent angular-velocity
  ramp into transition — reuses the swing-phase work) and a plausible **club orientation at impact**
  (from the fused quaternion, free here) — this is what separates a real strike from an address tap or
  a waggle, and mirrors commercial club-orientation gating (`shotdetection.md` §3).
- Emit `reportCandidate(Source::Imu, est_t, conf)` where `est_t = nowMicros()_at_handler_top −
  kImuBleLatencyUs`. (For the earliest raw stamp, optionally consume `rawPacketReady`'s pre-stamped
  `timestamp_us`, `wt9011dcl_base.cpp:156-157`.)

**Caveats baked into the plan:**
- **±16 g full scale** (`wt9011dcl_base.cpp:219-221`) will *clip* a hard strike — fine for *detection*
  (clipped = clearly over threshold), but means amplitude isn't a clean speed proxy.
- **100 Hz = ±10 ms timing** and risks attenuating the sub-ms spike between samples — good enough to say
  "impact happened," **not** to pin the instant. Hence audio pinpoints. **Recommend raising the active
  sensor to 200 Hz** (`setOutputRateHz`, `imu_instance.cpp:643-662`) to sharpen detection.
- **Contract:** connect with `this` as context so the existing teardown
  (`disconnect(m_imu, &…::quaternionUpdated, this, nullptr)`, `imu_instance.cpp:340`, before
  `deregisterFromBuffer()`) severs it automatically — no producer-contract violation, no hot-path mutex.

---

## 4. Component — acoustic onset detector

**Where:** a new `AcousticShotDetector` consuming `AudioInput::audioDataReady(QByteArray, QAudioFormat)`
**in parallel** with STT — the exact pattern of the (commented-out) `AudioStreamSaver` second consumer
(`transcription_controller.cpp:52-53`). `audioDataReady` is a fan-out signal; adding a consumer does not
disturb STT, and STT's silence-gating never throttles the raw stream
(`stt_processor.cpp:192-196` appends every buffer).

**Use the native-rate stream.** Capture is the device `preferredFormat()` — typically **44.1/48 kHz**
Float32/Int16, not STT's 16 kHz (which is a downstream resample, `AudioConverter.cpp:84-97`). Native
rate preserves the impact "click" high-frequency content and gives ~20 µs/sample timing.

**Algorithm (real-time, per buffer):**
- **High-pass pre-filter** (impact energy is high-frequency; rejects rumble/speech fundamentals).
- **Energy envelope** = max magnitude in a ~10 ms sliding window; **peak** above an adaptive threshold
  (e.g. halfway between the running average and max — ShotSpotter US11133023) → onset candidate.
- **Validate (false-positive rejection, `shotdetection.md` §2.2):** require **fast exponential decay**
  after the peak (rejects speech, which builds), **positive spectral-flux / HFC** delta, and **sudden
  amplitude vs the noise floor**; **refractory ~30–40 ms** (min inter-onset). Spectral-flux/HFC need a
  small STFT — start time-domain (envelope + decay) and add the spectral gate in a second pass.
- **Timestamp (the pinpoint):** stamp each buffer with `nowMicros()` **at receipt** (earliest point),
  then `est_t* = recv_now − ((samplesAfterOnsetToBufferEnd / rate) × 1e6) − kAudioDeviceLatencyUs`. The
  device-latency constant is a one-time calibration (QAudioSource buffer period isn't set today;
  `setBufferSize()` is uncalled).
- Emit `reportCandidate(Source::Acoustic, est_t*, conf)`. The detector runs on the audio/processor
  thread → marshal to `ShotController` (GUI thread) via `QMetaObject::invokeMethod(..., est_t*)`,
  computing `est_t*` **before** the hop so timing is captured at detection, not delivery.

**No new heavy dependency** — implement the onset math directly (~100–150 lines); `aubio` is the
reference but not worth a dependency for one detector.

---

## 5. Component — vision corroboration

Reuse the ball detector's `ballLaunched(timestampUs)` hook (`ball_detector_design.md` §8) and the
ball-present state. Vision is the **slowest** modality (camera fps + `FrameThrottle` `skipFactor=2`), so:
- `reportCandidate(Source::Ball, est_t, conf)` as a **confirmer** (a strike that coincides with a
  ball-launch is almost certainly real) and a **veto** for practice swings (no ball ever left the ROI).
- Never the pinpoint or the sole fast trigger. The exact impact *frame* is recovered offline from the
  frozen `SwingWindow` regardless.

---

## 6. The arbiter — extending `ShotController`

Add, alongside the existing `triggerShot`:

```cpp
// New: detectors report candidates here instead of committing directly.
void ShotController::reportCandidate(Source src, qint64 estImpactUs, float confidence);
```

**State machine (all on the GUI thread, so no locks):**
1. **Idle** → first credible candidate while `armed()` → store it, start a `QTimer` **hold window**
   (`kArbHoldMs`, ~150–250 ms wall — long enough to receive the slow/BLE-lagged modalities).
2. **Collecting** → fold in further candidates; group by est-impact within `±kMatchTolMs` (~40 ms).
3. **Decide** (on hold expiry, or early if a strong multi-modal agreement is already in):
   - **Accept** if ≥2 modalities agree within tolerance, **or** one modality is *strong* and passes its
     gates (degrade gracefully for IMU-only / camera-only rigs).
   - **Reject** otherwise (e.g. lone weak acoustic onset = ambient noise; lone IMU spike that failed the
     swing-energy gate).
4. **Commit** → pick the **authoritative timestamp** (`Acoustic > Imu > Ball`), call the existing
   `writeShotMarker(t*)` + `emit shotDetected(...)` **once**, then refractory (the `ShotProcessor::busy`
   gate already disarms for the whole pipeline — add only a small explicit min-interval for safety).

`triggerShot(Manual, …)` stays as an **immediate commit** (no hold). Refactor: the current
`triggerShot` body (marker write + emit) becomes a private `commitShot(src, t, sessionType)` the arbiter
and the manual path both call.

**Latency constants** live in one place (config/`AppSettings`): `kImuBleLatencyUs`,
`kAudioDeviceLatencyUs`, `kArbHoldMs`, `kMatchTolMs` — tunable, later auto-calibrated (§13).

---

## 7. Threading & contracts

| Component | Thread | Cross-thread to `ShotController` (GUI)? | Contract |
|---|---|---|---|
| IMU detector | GUI (BLE marshaled) | **Direct call** | connect with `this` context → severed by `ImuInstance::stop()` before deregister |
| Acoustic detector | audio/processor thread | `QMetaObject::invokeMethod` (queued), `est_t*` computed first | second `audioDataReady` consumer; no EventBuffer producer involvement |
| Vision (`ballLaunched`) | ball-detector thread | queued signal | already a QObject signal |
| Arbiter (`ShotController`) | GUI | — | single-threaded; `QTimer` hold; no locks |

No new EventBuffer producers are required for the core trigger, so the producer/stop-barrier contract is
untouched (the marker source already exists).

---

## 8. Optional — audio as an EventBuffer source (Phase 2.5)

Putting the audio waveform in the ring (new `DeviceKind::Audio` + a `SourceDescriptor`, a write path
stamping `nowMicros()` at receipt) would let the **frozen SwingWindow carry the audio**, enabling
**offline acoustic re-pinpointing/verification** and audio in the export. It is **not required** for the
live trigger (the detector passes a back-dated timestamp directly). Recommend as an enhancement after the
core works; respect the producer contract (stop/disconnect before deregister) as camera/IMU do.

---

## 9. Build / CMake

Minimal — no new external dependencies, no model downloads:
- New sources: `src/IMU/impact_detector.h` (header-only math), `src/Audio/acoustic_shot_detector.{h,cpp}`.
  Register via `target_sources(PinPointStudio PRIVATE …)`; AUTOMOC is automatic
  (`qt_standard_project_setup`, `CMakeLists.txt:29`).
- Spectral-flux/HFC gate (if added) needs a small FFT — Qt has none; either hand-roll a radix-2 FFT in
  the detector or start time-domain only (recommended for Phase 2).
- No OpenCV/ONNX/audio-library additions.

---

## 10. Phasing (lowest-risk first)

| Phase | Deliverable | Risk |
|---|---|---|
| **1 — IMU trigger** | `impact_detector.h` + `ImuInstance` hook → `triggerShot(Source::Imu)` directly (no arbiter yet); armed + swing-energy + club-orient + refractory gates; raise active sensor to 200 Hz | Low — cheapest, biggest single win; pure GUI-thread |
| **2 — Acoustic detector** | `AcousticShotDetector` on native-rate audio; envelope-peak + decay gate; receipt-stamped back-dated timestamp; `Source::Acoustic` | Med — device-latency calibration; onset tuning |
| **3 — Arbiter + vision** | `reportCandidate` + hold/fuse/commit in `ShotController`; route IMU+acoustic+`ballLaunched` through it; authoritative-timestamp = acoustic; cross-modal agreement | Med — timing/latency-constant tuning |
| **4 — Calibration + audio-in-ring + ML** | auto-calibrate per-source latency (cross-correlate peak functions, arXiv 1805.05456); optional audio EventBuffer source; ML IMU detector only if a placement lacks a clean jerk | Low/Med, incremental |

Phase 1 alone gives a working automatic trigger; Phases 2–3 are where false positives drop and the
timestamp becomes sample-accurate.

---

## 11. Testing

Standalone CTest harnesses (project convention, e.g. `src/IMU/tests/`, `src/Audio/tests/`):
1. **`impact_detector` unit test** — synthetic accel traces: real strike (sharp jerk) fires; mat/ground
   tap, waggle, address adjustment, slow swell do **not** (gate behaviour).
2. **Acoustic onset test** — synthetic impact "click" fires; speech/sustained tone/ambient rejected via
   the decay/HFC gate; refractory suppresses double-triggers; onset→timestamp offset math.
3. **Arbiter test** — candidate sequences → correct decisions: 2-modal agreement commits once; lone-weak
   rejects; lone-strong+gates commits; hold-window timeout; authoritative-timestamp selection; manual
   bypass; refractory.
4. **Latency/timestamp test** — arrival-stamp − latency = expected impact instant; nearest-frame
   alignment in a fixture SwingWindow.
5. **Headless/offscreen** end-to-end (project pattern) feeding synthetic IMU+audio → one back-dated marker.

---

## 12. Risks & open questions

- **IMU ±16 g clip + 100 Hz** — detection OK, timing/amplitude poor; mitigated by audio-pinpoint and a
  200 Hz active rate. Confirm 200 Hz BLE throughput is stable.
- **BLE latency variability** — undermines IMU as a timestamp anchor and forces a generous arbitration
  hold window; the audio-pinpoint design absorbs this, but the hold adds ~150–250 ms before the live
  "ANALYSING" indicator (impact alignment is unaffected — it's back-dated).
- **Audio device-latency calibration** — one-time constant per setup; `setBufferSize()` is currently
  unset, so the period is platform default; may need to set it for determinism.
- **Golf-specific tuning** — all thresholds (accel τ, onset threshold, gates) need field tuning; no golf
  benchmark exists (`shotdetection.md` caveats). Capture real strikes (we store full swing windows) to tune.
- **Open:** auto-calibrate per-source latency online (peak-function cross-correlation) vs a fixed
  measured constant? Put audio in the ring now or later? Single-modality accept thresholds (how strong is
  "strong enough" for IMU-only)?

---

## 13. Touch list (files)

**New:** `src/IMU/impact_detector.h` (math), `src/Audio/acoustic_shot_detector.{h,cpp}`, tests under
`src/IMU/tests/` + `src/Audio/tests/`.
**Edit:** `src/Gui/shot_controller.{h,cpp}` (add `Source::Acoustic`, `reportCandidate`, arbiter/hold,
`commitShot` refactor), `src/Gui/imu_instance.{h,cpp}` (impact-detector hook + 200 Hz), `src/Gui/main.cpp`
(construct + wire the acoustic detector and route candidates), `CMakeLists.txt` (new sources),
`src/Gui/app_settings.*` (latency/threshold constants).
**Unchanged (reused):** `ShotProcessor`, `shot_marker_v1` source, the `armed` gate, `EventBuffer`,
`AudioInput`/`audioDataReady`, the ball detector's `ballLaunched`.

---

*Design: [`shotdetection.md`](../design/shotdetection.md). Vision modality: [`ball_detector_design.md`](../design/ball_detector_design.md).*
