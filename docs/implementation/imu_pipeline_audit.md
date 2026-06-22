# IMU Pipeline Audit — Discovery / Connect / Stream

> **Status:** AUDIT COMPLETE · remediation IMPLEMENTED (R0–R4) — see [Remediation status](#remediation-status) ·
> builds clean, IMU CTest suite (incl. new ESKF test) green · **studio/hardware + SwingLab-replay validation still pending**
> **Date:** 2026-06-20 · **Scope:** the WT901BLE67 path end-to-end — BLE discovery →
> GATT connect/init → notification parse → host fusion → EventBuffer publish, plus the
> threading/lifecycle that binds them.
> **Method:** four parallel read-only sub-agent audits (discovery, connect, stream/parse,
> threading/lifecycle), cross-referenced and de-duplicated, then implemented phase-by-phase
> against the live tree with an incremental build after each phase.

## Remediation status

Implemented 2026-06-20 (working tree; not yet committed). Each phase build-verified; the
standalone IMU CTest suite (`build/imu-tests`: `impact_detector_test`, `imu_io_worker_test`,
and the new `eskf_gyro_units_test`) passes 3/3.

| Phase | Items | State |
|-------|-------|-------|
| **R0** Connection robustness | R0-1 watchdog · R0-2 failConnection+dedupe · R0-3 retry rework | ✅ done |
| **R1** Lifecycle & shutdown   | R1-1 quit stop-barrier · R1-2 rapid-toggle guard · R1-3 rescan race · R1-4 stop() watchdog | ✅ R1-1/2/3 done · R1-4 deferred (needs Windows wedge evidence) |
| **R2** Streaming integrity    | R2-1 nominal-dt · R2-2 frame resync · R2-3 0x71 gate · R2-4 stillness · R2-5 EMA · R2-6 clamp · R2-7 ESKF test | ✅ done (SwingLab-replay validation still pending) |
| **R3** Discovery UX/hygiene   | R3-1 surface BT errors · R3-2 refresh handle · R3-3 prune · R3-4 dead code | ✅ done (R3-3 + R3-4 landed 2026-06-22) |
| **R4** Documentation          | R4-1 CLAUDE.md · R4-2 cross-link/status | ✅ done |

**Deferred (with rationale):** R1-4 (bounded stop() watchdog — only warranted with an observed
WinRT shutdown wedge).

**Landed 2026-06-22 (the previously-deferred R3 items):**
- **R3-4 dead-code deletion** — removed the unused `BleImuTransport::scan()`/`stopScan()`
  public surface and the `deviceDiscovered`/`rawDeviceFound`/`scanFinished` signals + their
  `WT9011DCL_BLE` forwarders. The shared scanner (`m_scanner`/`ensureScannerCreated()`) and the
  internal `stopScan()` helper are kept — the Linux connect-phase scan reuses them. The audit's
  "triplicated WT901 accept-filter" was inaccurate: the only discovery filter is in
  `ImuBleScanner` (`device_enumerator.cpp`); `matchesPendingDevice()` is a generic
  selected-device matcher, not a discovery filter (comment corrected, nothing to factor).
- **R3-3 device pruning** — the connection-state coupling that made this safe now exists.
  `DeviceEnumerator` carries a scan-generation counter; each device records the generation it
  was last seen in, and the just-completed generation is published. `ImuManager` derives a
  per-device `present` flag (`seen-in-latest-completed-scan OR currently selected`) so a
  connected IMU that stops advertising is never dropped. Absent devices are hidden in the
  CapturePage chip row and dimmed in the Settings / toolbar device lists. Behaviour validation
  on real hardware (power-off → rescan → chip drops) remains an open gate.

**Validation gates still open:** TSAN re-run for the R1 threading changes; a SwingLab replay
pass over recorded swings to confirm the R2 fidelity changes reduce jitter without a
steady-state regression; hardware bring-up for the connect/retry/quit paths.

> Below is the original audit + plan. `file:line` references were captured pre-edit and have
> since moved; the implemented behaviour is the source of truth.

---

## Executive summary

The IMU subsystem is **well-architected at its core**. The I/O-thread migration (`W0–W3`,
see [`imu_io_thread_impl.md`](imu_io_thread_impl.md)) is sound: `QLowEnergyController`/discovery-agent
thread affinity is correct, the EventBuffer ring stays lock-free, and the producer-stop
barrier **is** honored on the normal deselect path — via the `ImuIoWorker` producer mutex +
severing the `quaternionUpdated` connection (not the `rawPacketReady` mechanism CLAUDE.md
still describes; the doc is stale — see [§ Documentation drift](#documentation-drift)).

The problems cluster in four themes, in priority order:

1. **No watchdog timeouts** on connect/discovery → a flaky BLE link can wedge the UI in
   "Connecting…" indefinitely with no recovery.
2. **The app-quit path bypasses the entire IMU stop sequence** → currently safe only by
   accident; a latent use-after-free / fatal-abort surface.
3. **Streaming data integrity during the swing** → fusion `dt` is incoherent with the
   published sample timestamp and mishandles BLE bursts; the checksum-less protocol resyncs
   weakly, so a dropped notification can inject garbage into **both** fusion and the
   shot-impact detector.
4. **Discovery / connection lifecycle fragility** → a scan-restart race that can clobber
   thread bookkeeping (abort on quit), single-shot retry logic, and silent failure when
   Bluetooth is off.

What's **sound** (verified, not padding): the threading model and same-thread hot-path
`DirectConnection`; atomic calibration swap and `gimbalDropCount`; the lock-free ring;
`main.cpp` destruction ordering; the producer stop-barrier on the **deselect** path; and the
endianness/scaling math in the parser. The fixes below are edge cases (timeouts, shutdown,
bursts, mis-framing), not a broken foundation.

---

## Findings

Severity is normalized across the four audits. Each finding has a stable ID used by the task
plan. `file:line` are as reported by the audits against the current tree.

### P1 — Fix first

#### P1-1 · No timeout on the connect / service-discovery phase (UI hangs forever)
`src/IMU/ble_imu_transport.cpp:198-232` (`doConnect`), `:279-282`, `:349`
The only timeout on the whole path is the Linux pre-connect *scan*. Once `connectToDevice()`
runs, nothing watches the HCI connect, `discoverServices()`, `discoverDetails()`, or the CCCD
`descriptorWritten`. `m_connectTimer` is a `QElapsedTimer` used only for log messages. If the
link establishes but a later step never completes *and* never emits `errorOccurred` (a real
BLE failure mode on BlueZ/WinRT), the transport sits in `Connecting`/`DiscoveringServices`
forever — `m_busy` stuck true, auto-retry never fires. The CCCD write is the single most
common hang point and has no fallback.
**Fix:** single-shot `QTimer` (~15–20 s) started in `doConnect()`, stopped only on reaching
`Ready`; on fire → `teardownController()` + `setState(Error)` + `emit errorOccurred("…timed out")`.

#### P1-2 · App-quit bypasses the IMU stop sequence (latent UAF / abort)
`src/Gui/main.cpp:417-420` (`aboutToQuit`) + `src/Gui/imu/imu_manager.cpp:104-128` (`~ImuManager`)
`aboutToQuit` calls `eventBuffer.stop()`, moving the buffer to `Idle`. `~ImuManager` then runs
during unwind, but its teardown is **state-gated**: it pauses only `if Capturing` and
deregisters only `if Paused`. With the buffer `Idle`, **both blocks are skipped** —
`instance->stop()` (the producer stop-barrier + clean BLE disconnect) and
`deregisterFromBuffer()` never run, then `delete entry.instance` is synchronous while the I/O
thread is still live. UAF on the ring is avoided only incidentally (the worker's
`isCapturing()` write-guard); the BLE link is torn down by `~BleImuTransport` rather than a
clean `disconnectFromDevice()`. Any future reorder or unguarded ring write makes this a real
UAF.
**Fix:** tear IMUs down explicitly in `aboutToQuit` *before* `eventBuffer.stop()`
(`imuManager.disconnectAll()`), **or** make `~ImuManager` unconditionally call
`instance->stop()` for every live instance regardless of buffer state (the barrier is valid in
any state). Keep deregistration gated; the producer barrier must not be.

#### P1-3 · Fusion `dt` incoherent with sample timestamp + mishandles BLE bursts (swing jitter)
`src/IMU/imu_base.cpp:53-57` + `src/Gui/imu/imu_instance.cpp:101`
`fuseRawImu` derives `dt` from its own `nowMicros()` at parse time, while `ImuInstance` reads a
*second, later* `nowMicros()` for the published/detector timestamp — two clock reads per
packet. On the first sample after any reconnect/filter-swap, `dt` silently defaults to 10 ms
regardless of rate (2× over-integration at 200 Hz). Worse, `dt` is from **host arrival time**,
which is bursty: BLE batches packets per connection interval, so packets <1 ms apart clamp to
the 0.5 ms floor while their true spacing is 5–10 ms — under-integrating gyro within a burst,
over-integrating across the gap, i.e. injecting jitter during fast motion (the swing).
**Fix:** compute one `nowUs` per packet at the parse site, thread it into both
`fuseRawImu(…, nowUs)` and the published sample. Prefer a nominal `dt` from the configured
output rate (1/`m_outputRateHz`) over host-arrival deltas, or divide accumulated arrival time
by burst count.

#### P1-4 · No frame validation on the checksum-less live stream (garbage into fusion *and* shot trigger)
`src/IMU/wt9011dcl_base.cpp:170-202`
Live `0x61`/`0x71` frames carry no checksum, and `processBuffer` resyncs by scanning for the
next `0x55` — which legitimately appears inside int16 payloads. On a dropped/split notification
the parser can lock onto a payload byte; if the next byte is `0x61`/`0x71` it consumes 20 bytes
of misaligned data as a "valid" frame, feeding nonsense accel/gyro into fusion **and the
`ImpactDetector`** with nothing to reject it. The legacy 11-byte path checks a checksum; the
live path has no equivalent. Can produce bad orientation *and* false shot triggers into the
arbiter.
**Fix:** add a cheap sanity gate before dispatching a combined frame — reject implausible
magnitudes (e.g. |a| > ~20 g) or out-of-range euler bytes — and on rejection advance by
**1 byte** (like the 11-byte path) rather than 20.

#### P1-5 · `rescanImu()` during the scan-finish window leaks the thread and clobbers bookkeeping
`src/Core/device_enumerator.cpp:262-277`
The `finished` lambda sets `m_imuScanActive = false` then `quit()`s — but `quit()` only posts
the request; the thread keeps running and `m_imuScanThread` is nulled only later on
`QThread::finished`. In that window a `rescanImu()` (reachable from CapturePage, ImusPanel, the
wizard, PpImuPanel, the resource monitor) passes the `if (m_imuScanActive) return;` guard and
overwrites `m_imuScanThread` with a new thread — leaking the old thread/scanner; then the *old*
thread's `finished` lambda nulls the pointer to the *new* running thread, after which
`aboutToQuit` can't stop it (sees null) → the "destroying a running QThread = fatal abort" the
code tries to avoid.
**Fix:** hold `m_imuScanActive = true` until the thread has fully finished (reset it in the
`QThread::finished` slot alongside the null-out), and capture the specific `QThread*` by value
so the finished lambda only nulls the pointer if it still points at itself. Or gate
`scanImu()` on `m_imuScanThread != nullptr`.

### P2 — Should fix

#### P2-1 · Connection error paths never call `teardownController()` — controller/service leak + stale lambda
`src/IMU/ble_imu_transport.cpp:284-295, 313-321, 334-338, 357-380, 420-424`
Every error exit only `setState(Error)` + emits; none tear down. After a CCCD/service/discovery
failure the HCI link is still held, `m_service` still alive, and the `descriptorWritten` lambda
stays armed (self-disconnects only on success) and could fire against a half-torn-down state. A
`ConnectionError` not accompanied by `disconnected()` leaks with no cleanup.
**Fix:** factor `failConnection(msg)` (`teardownController(); setState(Error); emit errorOccurred(msg);`)
and call it from every Error transition.

#### P2-2 · Auto-retry is single-shot and can be silently defeated by signal ordering
`src/Gui/imu/imu_instance.cpp:767-788` (+ `:690-718`)
`kMaxRetries = 1` after a 30 s delay is thin for a flaky link, and the retry only arms if
`m_inConnectPhase` is true when Error arrives. On BlueZ a failed connect emits *both*
`errorOccurred` and `disconnected`; if `Disconnected` (which sets `m_inConnectPhase = false`)
lands first, the Error handler skips the retry entirely. The two handlers also disagree on
`busy`.
**Fix:** drive retry off a single explicit state field set in `start()` (not `m_inConnectPhase`,
which both handlers mutate); shorten the first retry (2–3 s, backing off); add a test for the
Disconnected-before-Error ordering.

#### P2-3 · Rapid select→deselect→select overlaps teardown with a fresh connect
`src/Gui/imu/imu_manager.cpp:293-354` + `imu_instance.cpp:343-346`
Deselect schedules the old `ImuInstance` via `QTimer::singleShot(0, … deleteLater)`. A fast
re-select creates a new instance and `start()`s it while the old one is mid-teardown — both
briefly share the I/O thread and, on Linux, possibly the *same* HCI adapter (round-robin
`nextAdapter()`), racing the old controller's `disconnectFromDevice()`.
**Fix:** track a per-id "tearing down" flag (cleared in the `deleteLater` lambda) and
ignore/debounce `setSelected` for a device whose previous instance is still pending deletion.

#### P2-4 · `0x71` read-response has no echo validation → spurious `zeroingConfirmed`
`src/IMU/wt9011dcl_base.cpp:349-352` (+ `:358-366`)
`dispatchReadResponse` never verifies the echoed start register matches an outstanding request.
With the weak resync (P1-4), a mis-framed `0x55 0x71 …` can emit a bogus battery reading or —
worse — a spurious `zeroingConfirmed`, prematurely satisfying the calibration-zeroing fence in
`ImuInstance` (`imu_instance.cpp:144`).
**Fix:** track the address of the outstanding `readRegisters` request and ignore non-matching
`0x71` responses; gate `zeroingConfirmed` on an in-flight-zeroing flag, not just
`startReg == RegCalSw`.

#### P2-5 · First-sample `initFromAccel` seeds from one possibly-moving reading
`src/IMU/imu_base.cpp:59-60` + `orientation_filter.h:63-80`
The filter is seeded from a single accelerometer sample on first packet/reconnect/swap. If that
sample lands during motion, gravity is wrong and Madgwick (β=0.05) takes seconds to converge.
It won't NaN (degenerate cases are guarded) but will visibly mis-orient.
**Fix:** defer seeding until a sample with |a|≈1 g and |gyro|≈0 arrives (timeout fallback to
first sample).

#### P2-6 · Bluetooth-off / no-adapter fails silently — indistinguishable from "no IMUs found"
`src/IMU/ble_adapter_pool.cpp:39-40` + `src/Core/device_enumerator.cpp:50-56, 90-97`
With no adapter the scan still creates a default agent, which errors; the error is
`ppWarn`-logged and `imuScanFinished` fires with zero devices. The user gets no actionable
feedback.
**Fix:** propagate a distinct discovery-error signal carrying the
`QBluetoothDeviceDiscoveryAgent::Error`, surfaced by `ImuManager` so panels can show "Bluetooth
is off / no adapter."

#### P2-7 · `~ImuManager` deletes instances synchronously (vs `deleteLater` everywhere else)
`src/Gui/imu/imu_manager.cpp:119-120`
Synchronous `delete entry.instance` while the I/O thread is still running and (per P1-2) the
queued I/O→GUI `impactDetected` may still target it — a deleted-receiver hazard, masked today
only because the GUI loop is already stopping. **Folds into the P1-2 fix:** once
`instance->stop()` runs unconditionally first (severing the connection), the hazard goes away;
match the `deleteLater`-then-process pattern of the deselect path if touched further.

#### P2-8 · Stale `QBluetoothDeviceInfo` never refreshed across scans
`src/Core/device_enumerator.cpp:200-214`
`registerImuDevice()` early-returns on a known `id`, so a re-scan never updates the cached
handle; `ImuInstance::start()` then connects on first-seen advertisement data. Matters most on
macOS/Windows (Linux re-warms via the connect-phase scan).
**Fix:** update `platformHandle` on re-discovery instead of dropping the newer info.

### P3 — Polish / hygiene

- **P3-1 · Devices never removed from the enumerator** (`device_enumerator.cpp:161-214`): a
  powered-off IMU lingers as a selectable chip forever. Add "last seen"/prune or a
  `deviceRemoved` path. (Connect degrades gracefully → low.)
- **P3-2 · ESKF gyro unit round-trip fragile** (`eskf_orientation_filter.cpp:66-76`):
  rad/s→deg/s→rad/s relies on an undocumented internal multiply in vendored code; a library
  update could silently scale orientation 57×. Add a unit test pinning the assumption.
- **P3-3 · Impact-detector EMA freezes after a stall** (`impact_detector.h:111-117`): the
  `dtS < 0.5` guard skips the noise-floor update on any ≥500 ms gap, so the first post-stall
  jerk is judged against a stale baseline. Reset the EMA toward current |a| on a long gap.
- **P3-4 · Back-dated impact timestamp unclamped** (`impact_detector.h:180`,
  `imu_instance.h:153`): `peak_t_us − bleLatencyUs` isn't clamped to the ring-window floor;
  relevant once P4 auto-calibration grows the latency. Clamp where the marker is written.
- **P3-5 · Double `disconnected()` emit** (`ble_imu_transport.cpp:244-249`): emitted
  unconditionally even when already disconnected; guard on prior state like `setState` does.
- **P3-6 · `stop()` blocking-invoke wedge on shutdown** (`imu_instance.cpp:360-363`): acceptable
  for deselect; for shutdown, ensure it runs while the I/O loop still spins (the P1-2 fix
  arranges this) and consider a bounded watchdog if studio testing surfaces WinRT wedges.
- **P3-7 · Dead discovery code + stale comment** (`ble_imu_transport.cpp:100-153`,
  `wt9011dcl_base.cpp:62-68`, comment at `device_enumerator.cpp:34-35/67` referencing the
  deleted `WT9011DCL_BLE::onDeviceDiscovered()`): the WT901 accept-filter is duplicated; delete
  the unused `scan()` surface or factor one shared helper.

### Documentation drift

CLAUDE.md's IMU sections describe a structure that no longer exists and will mislead future
work:
- It says `WT9011DCL_BLE` owns the `QBluetoothDeviceDiscoveryAgent`/`QLowEnergyController` and
  that `descriptorWritten` triggers `initializeDevice()`. That machinery now lives in
  **`BleImuTransport`**, with discovery owned by `DeviceEnumerator::ImuBleScanner`.
- It says the producer-stop barrier is `disconnect(rawPacketReady, …)` before
  `disconnectFromDevice()`. The real barrier is **severing `quaternionUpdated` + the
  `ImuIoWorker` producer mutex / `detachBuffer()`** — `rawPacketReady` isn't on the worker
  path. The mechanism is correct, just not as documented.

---

## Remediation task plan (`R0–R4`)

Five phases, each a coherent PR-sized unit. Phases are ordered by risk/value; **R0 and R1 are
the P1 safety work and should land first.** Each task lists the finding ID, the files, the
change, and its acceptance check. Check boxes are for tracking during review/implementation.

**Cross-cutting validation** (run per phase as relevant): the `imu-tests` suite (and
`-DIMU_TESTS_TSAN=ON` for the threading phases), the standalone Buffer suite, the headless app
start (`QT_QPA_PLATFORM=offscreen`), and — for R2 — a SwingLab replay/soak pass over recorded
swings to confirm no orientation/detection regression.

### R0 — Connection robustness (timeouts & error cleanup)
*Covers P1-1, P2-1, P2-2, P3-5. No buffer/threading interaction — lowest blast radius.*

- [ ] **R0-1 (P1-1)** Add a connect/discovery **watchdog `QTimer`** in `BleImuTransport`:
  start in `doConnect()`, stop on `Ready`; on fire → `failConnection("connect/discovery timed
  out")`. Must span the CCCD write (the common hang point). *Accept:* a peripheral that connects
  but never finishes service discovery surfaces an Error within the timeout and the existing
  retry engages (add a fault-injection/unit test or a documented manual repro).
- [ ] **R0-2 (P2-1, P3-5)** Introduce `failConnection(msg)`
  (`teardownController(); setState(Error); emit errorOccurred(msg);`) and call it from **every**
  error exit (`onControllerError`, `setupService`/`enableNotifications` failures, `onServiceError`).
  Guard `disconnected()` emission on prior state so it can't double-fire. *Accept:* after any
  injected discovery/CCCD failure, `m_controller`/`m_service` are null and no `descriptorWritten`
  lambda remains armed; no duplicate `disconnected()`.
- [ ] **R0-3 (P2-2)** Rework the retry state machine: a single explicit `connectAttempt` field
  set in `start()` (drop the reliance on `m_inConnectPhase`), a small backoff (e.g. 2 s → 4 s →
  8 s) with `kMaxRetries` raised from 1, and consistent `busy` handling between the Error and
  Disconnected handlers. *Accept:* new unit test drives **Disconnected-before-Error** and
  **Error-before-Disconnected** orderings and both retry correctly; `busy` ends false on
  exhaustion.

### R1 — Lifecycle & shutdown safety
*Covers P1-2, P2-7, P2-3, P1-5, P3-6. Touches teardown ordering — run with TSAN.*

- [ ] **R1-1 (P1-2, P2-7)** Make IMU shutdown honor the stop-barrier: either
  `imuManager.disconnectAll()` in `aboutToQuit` **before** `eventBuffer.stop()`, or make
  `~ImuManager` call `instance->stop()` for every live instance **unconditionally** (regardless of
  buffer state), then delete. Keep deregistration state-gated. *Accept:* on quit with N IMUs
  connected, each runs its full `stop()` sequence (logged), the BLE link is cleanly
  disconnected, and TSAN reports no race on shutdown.
- [ ] **R1-2 (P2-3)** Add a per-id "tearing down" guard in `ImuManager` (set on deselect, cleared
  in the `deleteLater` lambda); `setSelected(enable)` ignores/debounces a device whose previous
  instance is still pending deletion. *Accept:* a scripted rapid select→deselect→select on one
  device produces exactly one live instance and no overlapping connect on the same adapter
  (TSAN-clean).
- [ ] **R1-3 (P1-5)** Fix the `rescanImu()` scan-finish race in `DeviceEnumerator`: keep
  `m_imuScanActive` true until `QThread::finished`, reset it and null `m_imuScanThread` together,
  and null only if the finishing thread is still the current one (capture `QThread*` by value).
  *Accept:* a tight `rescanImu()`-spam test (or stress harness) never leaks a thread and
  `aboutToQuit` always finds a stoppable thread; no abort.
- [ ] **R1-4 (P3-6)** *(optional, gate on R1-1)* Consider a bounded watchdog around
  `ImuInstance::stop()`'s blocking invoke so a wedged WinRT BLE stack can't stall shutdown
  indefinitely. *Accept:* documented; implement only if studio/Windows testing surfaces a wedge.

### R2 — Streaming data integrity
*Covers P1-3, P1-4, P2-4, P2-5, P3-3, P3-4, P3-2. The fidelity-critical phase — validate with SwingLab replay.*

- [ ] **R2-1 (P1-3)** Single per-packet timestamp: capture `nowUs` once at the parse site and
  thread it into both `fuseRawImu(…, nowUs)` and the published sample; derive fusion `dt` from
  the configured output rate (or burst-count-averaged arrival) rather than raw host deltas; fix
  the 10 ms first-sample default. *Accept:* `imu-tests` gains a burst/first-sample dt case;
  SwingLab replay of a known swing shows reduced orientation jitter vs baseline (no regression in
  steady state).
- [ ] **R2-2 (P1-4)** Harden `processBuffer` resync: add a combined-frame sanity gate (accel
  magnitude / euler range) before dispatch and advance by **1 byte** on rejection. *Accept:*
  unit test feeds a corrupted/split notification stream and confirms mis-framed frames are
  rejected (no garbage reaches fusion or the impact detector) while valid frames still parse.
- [ ] **R2-3 (P2-4)** Validate `0x71` responses: track the outstanding `readRegisters` address
  and ignore non-matching responses; gate `zeroingConfirmed` on an in-flight-zeroing flag.
  *Accept:* unit test shows a spurious/mis-framed `0x71` no longer fires `zeroingConfirmed` or a
  bogus battery update.
- [ ] **R2-4 (P2-5)** Stillness gate on `initFromAccel`: defer filter seeding until |a|≈1 g and
  |gyro|≈0 (timeout fallback to first sample). *Accept:* seeding during simulated motion is
  deferred; convergence from a still seed is unchanged.
- [ ] **R2-5 (P3-3)** Impact-detector EMA: on a ≥500 ms gap, reset the noise floor toward current
  |a| (or 1 g) instead of skipping the update. *Accept:* `impact_detector` test covers a stall
  followed by a strike and the threshold tracks reality.
- [ ] **R2-6 (P3-4)** Clamp the back-dated impact timestamp to the ring-window floor (and
  document the monotonicity assumption) where the marker is written. *Accept:* a back-date larger
  than the available window can't place the marker before captured data.
- [ ] **R2-7 (P3-2)** Add an ESKF gyro-unit regression test driving a known rate through
  `EskfOrientationFilter` and asserting the rotation rate, pinning the rad/s↔deg/s assumption.
  *Accept:* test fails if the vendored internal conversion changes.

### R3 — Discovery UX & hygiene
*Covers P2-6, P2-8, P3-1, P3-7. User-facing clarity + dead-code cleanup.*

- [ ] **R3-1 (P2-6)** Surface discovery errors: a distinct `DeviceEnumerator` signal carrying the
  `QBluetoothDeviceDiscoveryAgent::Error`, exposed via `ImuManager`, so IMU panels can show
  "Bluetooth is off / no adapter" instead of an empty list. *Accept:* with Bluetooth disabled the
  IMU panel shows an actionable message, not "no IMUs found".
- [ ] **R3-2 (P2-8)** On re-discovery of a known `id`, update the stored `platformHandle`
  (`QBluetoothDeviceInfo`) instead of early-returning. *Accept:* a power-cycled device connects on
  fresh advertisement data on macOS/Windows.
- [x] **R3-3 (P3-1)** Mark devices "last seen" and prune/grey entries absent from the most recent
  completed scan (or add a `deviceRemoved` path). *Accept:* a powered-off IMU drops out of the
  chip row after a re-scan. *Done 2026-06-22 via a scan-generation counter + derived `present`
  flag coupled to ImuManager selection state; chip rows hide / Settings rows dim absent devices.
  Hardware behaviour validation still pending.*
- [x] **R3-4 (P3-7)** Remove the unused `BleImuTransport::scan()`/`deviceDiscovered` surface and
  the `WT9011DCL_BLE` forwarders (keep the Linux connect-phase scan, which is used), or factor the
  WT901 accept-filter into one shared helper; fix the stale comment referencing the deleted
  `WT9011DCL_BLE::onDeviceDiscovered()`. *Accept:* one discovery filter implementation; no dead
  symbols; builds clean. *Done 2026-06-22; no accept-filter to factor (only one exists) — comment
  corrected instead. Umbrella test suite (incl. imu_driver_frame_test, which compiles the driver
  stack) green.*

### R4 — Documentation
*Covers the documentation-drift items + landing this audit.*

- [ ] **R4-1** Update CLAUDE.md's IMU sections: `BleImuTransport` owns the controller/discovery
  agent (discovery via `DeviceEnumerator::ImuBleScanner`); the producer-stop barrier is the
  `quaternionUpdated`-disconnect + `ImuIoWorker` mutex / `detachBuffer()`, not `rawPacketReady`.
- [ ] **R4-2** Cross-link this audit from [`imu_io_thread_impl.md`](imu_io_thread_impl.md) and mark
  remediation status as phases land.

---

## Suggested sequencing

1. **R0** + **R1-1/R1-3** — the connect-timeout, quit-path teardown, and rescan race are small,
   self-contained, high-value safety fixes; ship together.
2. **R2** — the data-fidelity phase; its own change with a SwingLab replay gate against recorded
   swings.
3. **R1-2/R1-4**, then **R3**, then **R4** — reliability polish, UX, and docs.

## Risks & notes

- **R1 touches the teardown barrier** — the single highest-risk area (see the `W3` risk notes in
  `imu_io_thread_impl.md`). Run the threading phases under TSAN and re-run the Buffer suite.
- **R2-1/R2-2 are fidelity-sensitive** — validate against recorded swings (SwingLab) so a "fix"
  doesn't shift orientation or detection timing in steady state; the goal is *less* jitter, not a
  different bias.
- All P1 `file:line` references should be re-confirmed against the tree at implementation time —
  they were captured by read-only audits and the tree moves.
- Nothing in this plan adds a mutex to the EventBuffer hot write path (forbidden by the producer
  contract); R1's safety work is all in the stop-ordering, not the ring.
