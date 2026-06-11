# IMU I/O Thread — Implementation Plan (device interaction off the GUI thread)

> **Status:** W0 IMPLEMENTED (worker + validation tests, not yet wired) · W1–W4 planned ·
> **Date:** 2026-06-11 · **Grounded against:** the studio performance triage of the same day
>
> Moves all per-packet IMU work off the GUI thread onto one dedicated I/O thread.
> Motivation: with three WT901s connected (3 × 200 Hz = 600 packets/s), every packet
> was parsed, fused, impact-checked and ring-written **on the GUI thread** (the BLE
> driver is a child of `ImuInstance`), and on Windows the WinRT BLE stack adds heavy
> per-notification marshaling on top — visible as stuttery calibration screens and
> live video. The display-signal coalescing (`5d5b954`) removed the QML binding
> storms; this plan removes the remaining per-packet GUI work entirely. It is also a
> **detection-fidelity** fix: impact timestamps are backdated from host-arrival time,
> so GUI hitches currently inject jitter straight into shot-detection timing.
>
> Cameras and microphones need nothing: each `CameraInstance` already runs capture /
> preprocess / pose / ball threads with ring writes on the capture thread and a
> properly coalesced display drain; audio delivers off-thread into the processor
> thread. The IMU is the only device class on the GUI thread.

## Target architecture

```
ImuManager (GUI)
  └─ owns ONE QThread "ImuIoThread" (all IMU devices share it — Qt BLE is
     fully async; 600 pkt/s is nothing for one event loop)
       ├─ WT9011DCL_BLE per device   (QLowEnergyController created HERE —
       │                              controller affinity = creating thread)
       └─ ImuIoWorker per device     (src/IMU/imu_io_worker.{h,cpp}, W0 ✓)
             per packet (DirectConnection from the driver, same thread):
               fusion-consumer hot path: anatQuat (A·q·M) · angular velocity ·
               data rate · impact detector · EventBuffer ring write ·
               locked LiveSample snapshot
ImuInstance (GUI, unchanged QML API)
  └─ 60 Hz display tick (already exists) reads worker->snapshot(), emits the
     QML-facing signals when seq changed — ZERO per-packet GUI events
```

## Contracts that bind every stage

- **EventBuffer producer contract** (CLAUDE.md): the ring stays lock-free; the
  producer must be provably stopped before `deregisterFromBuffer()`. The worker's
  `detachBuffer()` is that stop barrier (mutex-serialised against the in-flight
  write) — validated by test #4.
- **Quaternions-only** unchanged; the worker reuses `imu_calibration::toAnatomical`
  (the single A·q·M source of truth).
- **`ppInfo()`/PpMessageLog for any diagnostics** — never stdout (user mandate).
- The worker stays **QML/log/settings-free** so it remains standalone-testable.

## W0 — `ImuIoWorker` + validation suite ✓ (this commit)

`src/IMU/imu_io_worker.{h,cpp}`: the per-packet hot path extracted verbatim from
`ImuInstance`'s packet lambdas, with two deliberate changes:
- **`processSample(nowUs, …)` takes the timestamp as a parameter** (driver passes
  `EventBuffer::nowMicros()` at arrival) — deterministic under test, identical live.
- **Burst-tolerant angular velocity:** sub-5 ms arrivals (Windows BLE batches
  packets per connection interval) keep the velocity baseline instead of resetting
  it, so the next qualifying pair measures real motion across the burst window.

`src/IMU/tests/imu_io_worker_test.cpp` (in the `imu-tests` suite; build with
`-DIMU_TESTS_TSAN=ON` for ThreadSanitizer — both configurations ALL PASS):
1. hot-path correctness — anat transform vs `toAnatomical`, display accel remap,
   velocity (incl. the burst case), data-rate window
2. snapshot coherence — 300k-sample hammering vs ~800k concurrent snapshots, no
   torn reads, seq monotonic
3. atomic calibration swap — 137k mid-stream A/M swaps; anatQuat is always one
   whole pair, never a blend
4. ring writes + **detachBuffer stop barrier** — writes frozen the instant detach
   returns, then pause + deregister with the producer still running
5. impact detection across the thread boundary — strike fires exactly once,
   backdated by `bleLatencyUs` to the µs
6. 25 attach/hammer/detach/deregister churn cycles

## W1 — Thread hosting + data-path wiring

- `ImuManager` owns the `ImuIoThread` (created on first selection, joined in the
  destructor after all instances are gone).
- `ImuInstance` constructs the driver + worker WITHOUT parent, `moveToThread`s both
  before `start()`; `connectToDevice` is invoked on the I/O thread so the
  `QLowEnergyController` is created there (affinity rule).
- Driver `quaternionUpdated` → worker `processSample` via DirectConnection (same
  thread), passing accel/gyro from the driver's current packet (already set before
  the signal fires — same ordering guarantee the GUI lambda relied on).
- `ImuInstance`'s 60 Hz display tick switches from dirty-flag members to
  `worker->snapshot()`; the per-packet GUI lambdas (quat, accel, euler, ring write)
  are deleted. Display euler derives from the snapshot quaternion at tick time
  (CLAUDE.md: Euler is display-only, converted at the last moment) — the device's
  euler packets stop mattering on the live path.
- Low-rate driver signals (battery, state, diag, mag) stay queued to `ImuInstance`
  unchanged. `impactDetected`: worker → `ImuInstance` (queued) → existing
  ShotController wiring; timestamps are absolute, so the queued hop costs nothing.

## W2 — Control marshaling (GUI → I/O thread)

Every driver/worker mutation from the GUI becomes an `invokeMethod` onto the I/O
thread (or a worker any-thread call, which is already mutex-safe):
- calibration setters (`setNominalCalibration`, `refineMountAboutLongAxis`,
  `setCalibration`, `clearCalibration`) — compute A/M on the GUI as today, then
  `worker->setAnatomical(A, M, calibrated)` (atomic swap, test #3); the members
  `ImuInstance` keeps for persistence/QML stay GUI-side.
- `setOutputRateHz` / `zeroOrientation` / `requestBattery` / rawDump toggles —
  queued invokes to the driver on the I/O thread.
- impact sensitivity (`ImuManager` threshold scale) — `worker->impactConfig()`
  before streaming starts (documented pre-stream contract).

## W3 — Teardown ordering (the risk concentration)

`ImuInstance::stop()` becomes, in order:
1. blocking invoke on the I/O thread: disconnect driver→worker connection and
   `disconnectFromDevice()` (no new samples can be queued after it returns)
2. `worker->detachBuffer()` (stop barrier — any in-flight write completes)
3. buffer pause + `deregisterFromBuffer()` (existing manager sequence, unchanged)
4. destruction: workers/drivers `deleteLater` on the I/O thread; `ImuManager`
   quits + waits the thread after the last instance dies (mirror of the camera
   capture-thread barrier).

Re-run: `imu-tests` (+TSAN), the full Buffer suite, and the headless app start.

## W4 — Studio validation

- Calibration screens + live video smooth with 3 IMUs connected (the original
  complaint); CPU per-core profile sane in Task Manager.
- Calibrate flow end-to-end (the stillness gates read worker snapshots now).
- Shot detection: IMU impact triggers still fire; compare backdated timestamps
  against acoustic on a few shots (expect LESS jitter than before).
- End Session disconnect / reconnect cycles (wizard re-entry) — no hangs, no
  stalled-source warnings in the resource monitor.

## Risks

- **Teardown deadlock** (blocking invoke onto a thread being quit) — W3's strict
  ordering exists for this; churn test #6 covers the worker half, the W3 manual
  pass covers the driver half.
- **Qt BLE thread affinity quirks** (WinRT backend creating watchers on the wrong
  thread) — mitigated by creating the controller on the I/O thread from the start;
  the Linux/BlueZ path is verified here first, Windows in W4.
- **Calibration races** — closed by the worker's atomic-pair swap (test #3); the
  GUI-side copies are display/persistence only.

## Out of scope

Camera and microphone I/O (already threaded — see the audit at the top); moving
`DeviceEnumerator`'s BLE scan (low-rate, stays GUI); per-device threads (one shared
I/O thread is sufficient and simpler to tear down).
