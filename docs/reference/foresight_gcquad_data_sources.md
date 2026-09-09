# Foresight GCQuad — Data Source Reference

**Authority:** Direct inspection of the installed software and its logs on the capture PC —
`C:\GSProV1` (GSPro V1), `C:\Program Files (x86)\Foresight Sports Experience` (FSX),
`C:\ProgramData\Foresight` (Foresight SDK logs).
**Device observed:** GCQuad `#01796`, Ethernet, `192.168.1.124:9970`, built 12MAY2018, HW A1.
**Versions at time of writing:** GSPro build 2025-09-04 · GSPconnect 2025-08-05 ·
`DeviceAPIx64.dll` 3.5.2.9 (managed, GSPro) · `DeviceAPI_v141x64.dll` 3.5.3.3 (native, FSX).

This document covers **where GCQuad shot data can be read from disk**, what each source
contains, and how soon after ball contact it appears. It deliberately does not cover the
private GSPconnect↔GSPro channel — see [Excluded](#excluded).

---

## Summary

Four files carry shot data. Which are live depends on which application is driving the Quad —
only one application can hold the device at a time.

| Source | Written by | Live when | Content | First data |
|---|---|---|---|---|
| `FSS_SDK_NATIVE_LOG.txt` | `FSX.exe` | FSX runs | Ball + spin + club + ball position, contact offsets | **~111 ms** |
| `LastShot.CSV` | `FSX.exe` | FSX runs | Full precision + computed ball flight | after shot resolves |
| `ConnectDebug.txt` | `GSPconnect.exe` | GSPro runs | Ball metrics only | ~500 ms |
| `FSS_SDK_MANAGED_LOG.txt` | `DeviceAPIx64.dll` | GSPro runs | Timing and shot numbering, **no metrics** | ~111 ms |

**For an FSX-based workflow:** use the native log as the trigger and `LastShot.CSV` as the
source of record. They are complementary, not competing.

**For a GSPro-based workflow:** `ConnectDebug.txt` is the only live metric source, and it
carries ball data only.

---

## Topology

Only one host application can hold the Quad. The two application stacks use *different*
Foresight SDK builds, which is why their logs are mutually exclusive.

```
                    ┌─ FSX.exe ──────────── DeviceAPI_v141x64.dll (native)
GCQuad #01796 ──────┤                          └─ FSS_SDK_NATIVE_LOG.txt
Ethernet :9970      │                          └─ LastShot.CSV
                    └─ GSPconnect.exe ────── DeviceAPIx64.dll (managed)
                         │                     └─ FSS_SDK_MANAGED_LOG.txt
                         ├─ ConnectDebug.txt
                         └─ TCP 127.0.0.1:9050 ──> GSPro.exe
```

GSPconnect sets `LicenseAPI.ManagedLicensing = true` and takes the managed path, so it never
writes the native log. Conversely FSX never writes the managed log.

### GSPro launch chain

1. `GSPLauncher.exe` checks `gsp.lic`, reconciles `Core\GSP` and `Core\GSPC` against the CDN,
   then starts `Core\GSP\GSpro.exe` with `-show-screen-selector` and exits. It verifies
   `GSPconnect.exe` exists but **never launches it**.
2. `GSPro.exe` binds `127.0.0.1:9050` (Unity `TCPserver` MonoBehaviour; the port is a
   serialized inspector field, not configurable from a file).
3. `GSPro.exe` spawns `..\..\..\GSPC\GSPconnect.exe`. The connector is a child of the
   simulator — killing GSPro stops the connector and its log.

### Ports

| Port | Bound by | Purpose |
|---|---|---|
| 9970 | GCQuad | Device command/data channel (ECDH key exchange, then AES) |
| 239.0.0.100 (UDP) | — | Device discovery multicast (`HostAliveMessage` / `DeviceAliveMessage`) |
| 9050 | `GSPro.exe`, loopback only | GSPconnect → GSPro |
| 921 | `GSPconnect.exe` | GSPro Connect Open API — inbound from *third-party* LM software |
| 12495 / 12321 / 12485 | `GSPconnect.exe` | Same Open API, rebranded (Nova/OpenLaunch, ProTee, VTrack) |

Port 921 is inbound-from-LM, not an output. The GCQuad never uses it.

### Device message IDs

Seen in both SDK logs; useful for interpreting log lines.

| ID | Message |
|---|---|
| `0x0001` | StatusChanged (ball presence, handedness, ball positions) |
| `0x0002` | BallLaunched |
| `0x0003` | SpinData |
| `0x0005` | ClubData |
| `0x0037` | HostPublicKeyRequest (ECDH handshake) |
| `0x0104` | SetHandedness |
| `0x0111` | SetPuttingMode |
| `0x0206` | GetHitZone |

---

## Source 1 — `FSS_SDK_NATIVE_LOG.txt`

`C:\ProgramData\Foresight\FSS_SDK_NATIVE_LOG.txt`

Written by `FSX.exe` via `DeviceAPI_v141x64.dll`. The richest on-disk source, and the earliest.

### Per-shot blocks

A single swing produces three blocks, each preceded by a timestamped `dataReceiveHandler`
line. Real capture, shot 93:

```
2026-08-18 19:43:45.074 6 Received ball launch from GCQuad #01796 Ethernet 192.168.1.124:9970
Time contact:     111 ms	Shot Number:     93
Ball Speed:     100.5 mph	Launch Angle:    22.7 deg	Azimuth Angle:   -1.1 deg
Backspin:      3500.0 rpm	Sidespin:         0.0 rpm	LaunchFlags:      1
WorldStartXmm: -261.7 mm	WorldStartYmm:   30.2 mm	WorldStartZmm: -445.1 mm

2026-08-18 19:43:45.563 6 Received spin data from GCQuad #01796 Ethernet 192.168.1.124:9970
Time since contact: 601 ms
Shot Number:        93
Backspin:           5620.00 rpm
Sidespin:           710.00 rpm
RifleAxisDeg:       0.00 Deg
SpinIsValid:        1

2026-08-18 19:43:46.854 6 Received club data from GCQuad #01796 Ethernet 192.168.1.124:9970
Time Contact:    1907 ms      Shot number     93
Club Data is not valid - may need dots on club face
```

Note the three different spellings of the contact-offset label — `Time contact:`,
`Time since contact:`, `Time Contact:`. Match case-insensitively.

### Timing

| Block | Since contact |
|---|---|
| Ball launch | ~111 ms |
| Spin | ~601 ms |
| Club | ~1907 ms |

**The spin values in the ball-launch block are placeholders**, not measurements:
`Backspin 3500.0 / Sidespin 0.0 / LaunchFlags 1` is emitted for every shot. Real spin only
exists once the spin block lands. Do not read spin from the launch block.

**Recovering the contact instant:** subtract the block's printed contact offset from the
timestamp on its `Received ...` line. For shot 93 that is
`19:43:45.074 − 111 ms = 19:43:44.963`. This is the only source that makes the contact
instant recoverable, and it is what a video/IMU alignment needs.

### Club data ceiling

Club fields that the device could not measure are written as `FLT_MAX`
(`340282346638528859811704183484516925440.0`). In practice only three fields come through:

| Field | Available |
|---|---|
| Club Speed | yes |
| Angle of Attack | yes |
| Club Path | yes |
| At Impact, Face to Target, Lie, Loft, Impact X, Impact Y, Face Axis, Effective Loft, Closing Rate | `FLT_MAX` |

The log states the cause: *"Club Data is not valid - may need dots on club face."* This is
consistent across the archive (invalid on 20/26, 8/119, 49/233 and 31/191 shots in the four
retained logs), so it is the club-marking requirement rather than a per-session fault.

### Rotation — destructive

`C:\ProgramData\Foresight\LogSettings.xml` **does not exist** on this machine, so SDK defaults
apply, as stated in the log's own header:

- 1.00 MB per file — on exceeding it, the current file is renamed to
  `FSS_SDK_NATIVE_LOG_<YYYY-MM-DD>_<HH-MM-SS>.txt` and a new one started.
- 100.00 MB total across all native logs. Beyond that, the header warns files
  **"WILL BE DELETED in order (by name)"** until under the cap.

Observed rate is roughly 127 KB per 45 minutes of play, so a roll occurs every five or six
hours and can happen mid-session. Any tail must detect the rename and reopen.

---

## Source 2 — `LastShot.CSV`

`C:\Program Files (x86)\Foresight Sports Experience\System\LastShot.CSV`

Single-shot file, rewritten each shot, header row plus one data row. **Metric units.**

```
Shot ID, Club, Club head Speed (m/s), Ball Speed (m/s), Launch Angle (deg), Azimuth (deg),
Side Spin (rpm), Back Spin (rpm), Total Spin (rpm), Descent Angle (deg), Carry (m),
Total Distance (m), Offline (m), Peak Height (m), Distance to Pin (m), Vert Path (deg),
Horiz Path (deg), Face to Path (deg), Face to Target (deg), Lie (deg), Loft (deg),
Closure Rate (deg/s), Horiz Impact (mm), Vert Impact (mm),
```

Unmeasured fields use the sentinel `16777215.000000` (`0xFFFFFF`) — the CSV equivalent of the
log's `FLT_MAX`. The same club-data ceiling applies.

`LastShotOptions.xml` alongside it controls only which tiles FSX displays, not the CSV content.

### Native log vs. LastShot.CSV

|  | Native log | LastShot.CSV |
|---|---|---|
| First data available | ~111 ms after contact | after the shot resolves |
| Explicit contact offset | **yes**, per block | no |
| Ball position (WorldStart X/Y/Z, mm) | **yes** | no |
| Numeric precision | 1 dp — `100.5 mph` | 6 dp — `45.894768 m/s` |
| Computed flight (carry, total, descent, peak, offline, distance to pin) | no | **yes** |
| Club name (e.g. `1w`) | no | **yes** |
| Units | mph, deg, rpm, mm | m/s, m, deg, rpm, mm |

Use the log to trigger and timestamp; use the CSV for authoritative values.

---

## Source 3 — `ConnectDebug.txt`

`C:\GSProV1\Core\GSPC\ConnectDebug.txt` — written by GSPconnect via log4net.
The only live metric source while **GSPro** is driving the Quad.

Three lines per shot, always in this order:

```
2026-09-06 18:47:07,078 [1] INFO  VGPconnect.ForesightForm [(null)] - Sending Shot
2026-09-06 18:47:07,080 [1] INFO  VGPconnect.ForesightForm [(null)] - Logging ball data IMMEDIATELY before sending to GSPro
2026-09-06 18:47:07,176 [1] INFO  VGPconnect.ForesightForm [(null)] - [65.4664459,29.970499,-3.88416982,-507.0,7274.689,7257.0,-3.99639463,0.0]
```

Parse the third line — a plain JSON array. The middle line is a reliable one-line lookahead.

### Field order

| # | Field | Unit |
|---|---|---|
| 0 | Ball speed | mph |
| 1 | Vertical launch angle | deg |
| 2 | Horizontal launch angle | deg |
| 3 | Side spin | rpm |
| 4 | Total spin | rpm |
| 5 | Back spin | rpm |
| 6 | Spin axis | deg |
| 7 | Carry | **always 0** |

Indices 3–6 are internally consistent, which pins the order:
`√(7257² + 507²) = 7274.7` and `atan2(−507, 7257) = −4.00°`.
Index 7 is hardcoded to zero on the Foresight path — ignore it.

### Ball only

Club data **is** computed and sent to GSPro, but the club send path contains no logging call,
so it never reaches disk. The log level is already `ALL`; raising verbosity would not surface
it. Path, face angle, lie, loft, closure rate and face impact are unavailable from this source.

### Timing

GSPconnect waits for ball *and* spin before logging, so the line carries measured spin but
its timestamp is roughly 500 ms after contact — and the offset varies per shot. Unlike the
native log, no contact offset is printed, so the true contact instant is not recoverable from
this file alone.

### Rotation — non-destructive

Live file is always `ConnectDebug.txt`; previous days become `ConnectDebug.txt.YYYY-MM-DD`,
created only on days the connector ran. The config's `maxSizeRollBackups=5` applies to
size-triggered rolls, not dated ones, so **nothing is pruned**. As of writing: 198 files back
to 2024-11-17 holding **29,747 shots** — usable as a corpus in its own right.

### Tailing

- log4net's default locking model holds the file open but permits readers. Open with read
  sharing, not exclusive access.
- On the first shot of a new day the file is renamed out from under you and a fresh
  `ConnectDebug.txt` appears. Detect and reopen or shots are silently lost.
- The appender does not buffer; lines hit disk as written.
- The file only exists while GSPro is running, since GSPconnect is its child process.

---

## Source 4 — `FSS_SDK_MANAGED_LOG.txt`

`C:\ProgramData\Foresight\FSS_SDK_MANAGED_LOG.txt` — written by `DeviceAPIx64.dll` while
GSPconnect drives the Quad.

**Carries no metrics.** `BallLaunch.ToString()` prints speed, launch angle, azimuth, spin,
launch flags and world coordinates only when the SDK's `g_LogDebug` flag is set; GSPconnect
never sets it and `ForesightSDK.xml` does not expose it. What it does carry:

- Shot number and `TimeSinceContact` for each of the three messages
- Device identity and connection type
- Ball positions on the sensor, hit-zone geometry
- Status transitions (`NoBalls` → `OneOrMoreBalls` → `LockedOnBall` → `BallLaunched`)

**Its value is timing.** Pairing it with `ConnectDebug.txt` recovers the contact instant that
the connector log alone cannot give — the same role the native log plays for FSX.

Rotates near 1 MB into `FSS_SDK_MANAGED_LOG_<YYYY-MM-DD>_<HH-MM-SS>.txt`.

---

## Recommended integration

**FSX workflow (preferred — earliest and richest):**

1. Tail `FSS_SDK_NATIVE_LOG.txt`. On `Received ball launch`, parse the block and compute
   contact time as `line timestamp − Time contact`. Fire the capture trigger here (~111 ms).
2. Wait for the matching `Received spin data` block (same `Shot Number`) for measured spin.
3. Read `LastShot.CSV` for full-precision values and computed flight once it updates.

**GSPro workflow:**

1. Tail `ConnectDebug.txt` for the eight ball floats.
2. Optionally correlate against `FSS_SDK_MANAGED_LOG.txt` by shot number to recover contact
   timing. Accept that club data is unavailable.

---

## To verify

- **Native log flush behaviour — blocking issue.** Whether `DeviceAPI_v141x64.dll` flushes per
  line or block-buffers cannot be determined by reading the file, and block buffering would
  eliminate the ~111 ms latency advantage entirely. Test: tail the file during a live shot and
  observe whether the block appears immediately or arrives in a burst with later output.
  **Do this before building against it.**
- **`LogSettings.xml` schema.** The file is absent and defaults apply. If the 1 MB roll proves
  disruptive, the header implies the size caps are configurable there, but the schema has not
  been confirmed.
- **FSX Play / FSX Pro.** Both are installed but ship no `DeviceAPI*.dll` of their own; only
  Foresight Sports Experience does. Whether they write the same native log is untested.

---

## Excluded

The GSPconnect → GSPro channel on `127.0.0.1:9050` is a private interface between two GSPro
components. Its payload is obfuscated, and decoding it is prohibited by the GSPro EULA.
It is **not** a supported integration route and is deliberately not documented here.
The four log files above are ordinary application output and require no such decoding.

For a supported *input* path into GSPro — feeding it as a launch monitor rather than reading
from it — the documented interface is the Open Connect API on port 921, which accepts JSON
`ShotData` over a raw TCP socket.
