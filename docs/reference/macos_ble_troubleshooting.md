# macOS BLE troubleshooting — silent connect stalls

> **Date:** 2026-06-12 · **Applies to:** IMU (WT901) BLE connections on macOS,
> Qt 6.11 / CoreBluetooth backend

## Symptom

- Clicking Connect on an IMU leaves the GUI at **"Connecting…" indefinitely** —
  no error, no timeout, no log entry beyond the connect attempt.
- The device LED keeps blinking. This is **advertising**, not a link: the OS
  never actually initiates the connection.
- The startup scan may still work (devices appear in the list), which makes the
  stall look exactly like a regression in whatever code changed last. It isn't.

## Root cause: a third-party USB Bluetooth dongle wedges bluetoothd

A USB BT dongle left plugged in (seen with an **ASUS USB-BT500**; fine on Linux,
no proper macOS support) wedges macOS `bluetoothd`: freshly created
`CBCentralManager` instances never receive their mandatory initial
`centralManagerDidUpdateState` callback, so
`QLowEnergyController::connectToDevice()` parks in `ConnectingState` forever.
Managers created **earlier** in the process lifetime (e.g. the app-startup scan)
may keep working, so the failure appears selective and code-shaped.

**Fix** — no app change needed:

1. Unplug the dongle.
2. `sudo pkill bluetoothd` (it restarts automatically; a reboot also works).
3. Reconnect.

## Diagnosing: get the CoreBluetooth trace

The app's message handler (`src/Core/pp_debug.cpp`) suppresses the entire
`qt.bluetooth` logging category by default — **including Qt-side BLE errors**.
To see what CoreBluetooth is actually doing:

```sh
PINPOINT_BLE_TRACE=1 QT_LOGGING_RULES="qt.bluetooth*=true" \
    ./PinPointStudio.app/Contents/MacOS/PinPointStudio
```

Healthy connect sequence on stderr:

```
[qt.bluetooth] ... setting state to QLowEnergyController::ConnectingState
[qt.bluetooth.darwin] trying to connect
[qt.bluetooth] ... setting state to QLowEnergyController::ConnectedState
[qt.bluetooth] ... setting state to QLowEnergyController::DiscoveringState
...
```

**Silence immediately after `"trying to connect"`** means the freshly allocated
`CBCentralManager` never got its state callback — bluetoothd is wedged
(environment), not app code. Check for the dongle and reset the daemon.

## Not a thread-affinity problem

Qt BLE objects on a plain worker `QThread` work fine on macOS. The IMU
I/O-thread architecture (BLE driver + `QLowEnergyController` living on
`ImuIoThread`, `docs/implementation/imu_io_thread_impl.md`) is
hardware-verified on macOS: connect, service discovery, streaming and clean
stop()/teardown all pass. Do **not** move BLE objects to the main thread
chasing this symptom — that misdiagnosis cost a day in June 2026.
