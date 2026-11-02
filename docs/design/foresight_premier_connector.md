# GC Quad under Foresight Premier — a fourth launch-monitor connector

**Audience**: whoever builds the Premier connector, and whoever runs the investigation session (§6)
**Code**: `src/LaunchMonitor/premier_session_parser.{h,cpp}` (pure parser), `premier_monitor.{h,cpp}`
(`ForesightPremierMonitor`, `Kind::Premier`, token `"premier"`), timed pairing in `shot_pairing.h`, the late
club update in `src/Gui/launchmonitor/launch_monitor_controller.cpp`. Tests: `src/LaunchMonitor/tests/premier_*`
over the captured write sequences in `tests/data/premier/`
**Source**: direct inspection of the capture PC on 2026-10-23, `liver` account, after one Premier
session of 91 shots with a 4-hybrid. Background: [`foresight_gcquad_data_sources.md`](../reference/foresight_gcquad_data_sources.md)
A second look on 2026-10-31 followed a 26-shot training drill (ladder, wedge) on Premier 1.2.0 (§2.5).
**Status**: ✅ **BUILT 2026-11-02, first cabin run passed** (§5 step 6, §4.7). Pairing against camera swings is still unmeasured. Investigated live the same day (§2.6):
Premier 1.2.1 sends live shots to `SessionLiveSnapshot.<userId>.json`, and the upload queue only sees
a session once it has ended. §4 reads the snapshot; §4.6 records where the build sharpened §4.
**Written**: 2026-10-23. **Updated**: 2026-11-01 (§2.5, queue drain corrected in §2.1);
2026-11-02 (§2.6 live session, §4 revised, §7 answered; built, §4.6; cabin run, §4.7)

---

## 1. Why

Foresight Premier is Foresight's current simulator, replacing FSX2020. It is the more modern
and more compelling program, and it drives the same GC Quad (`#01796`, Ethernet, `192.168.1.124:9970`).

PinPoint reads the Quad today only through FSX2020's `LastShot.CSV` (`Kind::GcQuad`,
`gcquad_monitor.cpp`). The goal is to **also** read it when Premier is the application holding
the device, **without** losing FSX2020 support. Only one application can hold the Quad at a
time, so the user picks the connector to match the program they are running.

Unlike the GSPro case ([`gspro_gcquad_connector.md`](gspro_gcquad_connector.md), declined),
Premier writes **club data** to disk. That data is what PinPoint reads a launch monitor
for: `lm.clubheadSpeed` and `lm.attackAngle` checked against our camera estimates.

## 2. What Premier leaves on disk

Install: `C:\Program Files\Foresight Premier`. Per-user data root (Unity `persistentDataPath`):

```
C:\Users\<user>\AppData\LocalLow\Foresight Sports\Foresight Premier\
```

| File | Content | Club data | Clock | Notes |
|---|---|---|---|---|
| `SessionLiveSnapshot.<userId>.json` | Every shot of the **range session in progress**, ball + club (club data one shot behind) | **yes, full** | UTC, 100 ns | §2.6. **The live source** on 1.2.1. Exists only while a range session is open |
| `SessionUploadQueue.<userId>.json` | **Ended** sessions not yet uploaded, ball + club | **yes, full** | UTC, 100 ns | §2.1. A session enters it when it ends and leaves it on a successful upload, seconds later |
| `ShotReplay\history.json` | Rolling last **100** shots | no | UTC | §2.2. Ball + computed carry |
| `Player.log` | Unity log; one `SimulateShotInternal` line per shot | no | local, ms | §2.3. Plain text, ball only |
| `C:\ProgramData\Foresight\FSS_SDK_MANAGED_LOG.txt` | SDK protocol trace | no values | local, ms | §2.4. Timing and shot number only |

Not opened, deliberately: `foresight_auth.journal`, `Users.json`, and FSX Play's `cred.dat`.
These hold account and sign-in details. A connector has no reason to touch them, and the
investigation watcher (§6) excludes them.

### 2.1 `SessionUploadQueue.<userId>.json`

The file name is suffixed with the signed-in user's id (`f9b7fb0a-…`). There is one queue per
Premier user, so the connector must glob `SessionUploadQueue.*.json`, not hard-code the name.

Shape (UTF-8 with BOM, pretty-printed, ~100 KB for 91 shots):

```jsonc
{ "Sessions": [ {
    "LocalSessionId": "7b5fd196-…", "BackendSessionId": "ad7c231e-…",
    "CreatedAtUtc": "2026-10-23T13:14:18.3858911Z",
    "EndedAtUtc":   "2026-10-23T15:12:03.4221854Z", "EndedAtSynced": false,
    "SessionType": 0, "TrainingDetails": null,
    "ShotGroups": [ {
        "LocalGroupId": "…", "BackendGroupId": "…", "ColorHex": "#FF4FC5C9",
        "Tags": [ "4h" ],
        "Shots": [ {
            "LocalShotId": "eda81ebb-…", "BackendShotId": "HtZCZ/BOJo9qk918vBFxhQ==",
            "ClubType": "HYBRID", "ClubName": "4h",
            "BallSpeedMs": 23.69312, "AzimuthDeg": 2.2, "ElevationDeg": 29.9,
            "BackSpinRpm": 3276.0, "SideSpinRpm": -768.0, "TotalSpinRpm": 3365.0,
            "Hidden": false, "CreatedAtUtc": "2026-10-23T13:14:54.2529763Z",
            "ClubData": {
                "ClubSpeedMs": 20.34032, "ClubSpeedAtImpactMs": 20.34032, "Efficiency": 1.16,
                "AngleOfAttackDeg": 5.0, "ClubPathDeg": 5.5, "FaceToTargetDeg": -5.3,
                "LieDeg": 8.9, "LoftDeg": 51.7, "FAxisDeg": -12.8,
                "FaceImpactHorizontalMm": -12.6, "FaceImpactVerticalMm": -9.1,
                "ClosureRateDegs": 3234.2 } } ] } ] } ] }
```

What the data shows:

- **Club data is complete.** Face to target, lie, loft, face impact and closure rate all come
  through. FSX2020 logged only speed, AoA and path, and wrote `FLT_MAX` for the rest
  ("may need dots on club face"). We don't yet know whether that is Premier or this
  particular club. §6 Block 2 tests it.
- **85 of 91 shots have `ClubData`.** The six missing ones line up with a Premier warning at
  15:53:21: *"Drained 6 stale pending club data entries; club data will be applied to most
  recent shot …"*. When club data falls behind, Premier drops it rather than attaching it to
  the wrong shot. A connector should treat a missing `ClubData` as normal (ball data only).
- **Units are SI**: m/s and degrees. Checks: `23.69312 m/s × 2.23694 = 53.0 mph`, which matches
  `Player.log` for the same shot. `Efficiency 1.16 = 53.0 / 45.5 mph`, so it is smash factor.
  `ElevationDeg` is vertical launch angle (29.9°, matches the log).
- **`CreatedAtUtc` is the `SimulateShotInternal` time** to within a few ms. `Player.log` logs the same shot
  at 14:14:54.249 local (BST, UTC+1). The queue says 13:14:54.253 UTC. **Correction (2026-11-02):**
  that is spin-arrival time, ~0.58 s after contact, not ball launch (§2.6.6).
- **There is a real UTC timestamp per shot.** This is a material improvement on `LastShot.CSV`,
  which has none. `ShotPairing`'s arm-and-claim rule exists *because* the CSV can't say when
  a row was made (§4.3).
- **The queue drains on a successful upload, and only then.** `ShotReplay\history.json` reaches
  back to 12:55 UTC, an earlier Premier run (`Player-prev.log`, 72 shots). The queue holds
  **only** the session that started at 13:14, so the earlier session left the queue. **Correction
  (2026-10-31):** that 13:14 session is *still* in the queue eight days later. Its upload fails
  at every launch: `[SessionUploadProcessor] ProcessPendingInternalAsync: Session upload failed
  for session 7b5fd196-…; will retry later. This session has already ended.` (logged 2026-10-27
  10:42:48 and 2026-10-31 12:34:19). The queue file's mtime is that retry (12:34:19.15), so
  **Premier rewrites the queue at startup even when no shot has been hit.** A failed upload
  keeps a session in the queue indefinitely. A successful one removes it. The connector still
  can't treat the queue as the archive. It must take each shot as it appears, never assume an
  earlier one is still there, and never assume an old one has gone. Priming on `start()` (§4.1)
  covers stale sessions. §6 Block 6 shows what a successful drain looks like.

Answered by the live session (§2.6): on 1.2.1 the queue receives a session only when it **ends**.
Live shots go to `SessionLiveSnapshot` (§2.6.1). The queue is rewritten whole, opened exclusively,
once per shot during an upload. Signs are in §2.6.5.

### 2.2 `ShotReplay\history.json`

`{"version":1,"maxHistorySize":100,"records":[…]}`, newest first. Per record: `id`, `created`
(UTC), `courseName` ("Shot Analysis" on the range), `ballSpeedMPH`, `launchAngleDeg`,
`azimuthDeg`, `backspinRPM`, `sideSpinRPM`, `totalSpinRPM`, `spinAxisTiltDeg`, `clubType`,
`clubName`, `carryDistanceMeters`, `totalDistanceMeters` (0.0 throughout, so not populated on
the range), plus ball/aim world positions.

There's no club data. Its value is **carry** and **spin axis**, which the queue doesn't carry,
and the fact that it survives upload. Whether `id` equals the queue's `LocalShotId` is
unknown: the ids sampled didn't obviously match. If they don't match, `created` (UTC) is the join key.

### 2.3 `Player.log`

One line per shot, plain text, local time:

```
[14:14:54.249][Info] ℹ️ [Gameplay] [DeviceRepository] SimulateShotInternal: ballSpeedMPH:53.0mph|launchAngleDeg:29.9°|backSpinRPM:3276rpm|sideSpinRPM:-768rpm|azimuthDeg:2.2°
```

`SimulateShotInternal` is the path real Quad shots take; the 91 lines match the 91 queued
shots. The log is ball-only and 1 dp. The time-of-day carries no date. On every Premier launch
the file is replaced and the old one is renamed `Player-prev.log`.

Also present: ghost-shot filtering (`OnSpinDataReceived: Ghost shot detected and filtered out`)
and the club-data drain warning above. Its use to us is a **fast trigger** and a diagnostic,
not a source of record. It is the Premier equivalent of FSX2020's native log.

### 2.4 `FSS_SDK_MANAGED_LOG.txt`

Premier uses the **managed** SDK (`DeviceAPIWrapper`), the same build GSPconnect uses. So this
log carries timing but no values (`BallLaunch ShotNumber= 38 TimeSinceContact= 115 ms`, spin at
~509 ms, club data at ~1676 ms). It rotates near 1 MB. A dated archive goes back to 2026-09-09.

Premier logs `SetVerboseLogging: FSS SDK verbose logging updated. isEnabled:False` at startup.
The reference doc notes that `BallLaunch.ToString()` prints values only under the SDK's debug
flag. **If Premier exposes a verbose-logging setting, this log may start carrying values with
an explicit contact offset.** That would be the only route to the true contact instant. §6 Block 7.

### 2.5 Training drills (2026-10-31): nothing reached the queue, no club data

The user ran a ladder drill (`ladder-rising-wedges`, Medium) on Premier **1.2.0** (released
2026-10-26, after §2.1–§2.4 were written; Unity 6000.3.22f1). It had 26 shots from 12:36:03 to
12:45:22 local, with the clock on UTC (GMT) by then. What happened:

- **The upload queue was not touched.** The last write was 12:34:19, the startup upload
  retry (§2.1). No drill shot appears in it.
- **No shot group was created.** As the drill started, Premier logged `[ShotGroupCreateDuplicatePreventionInterceptor]
  InterceptAsync: ShotGroupCreateCommand has invalid club name. Skipping creation.` (12:35:30).
  The shots are recorded with the generic club `clubType: "IRON"`, `clubName: "IRON"`. That
  probably explains the missing queue entries, since a queued shot lives inside a `ShotGroup`.
- **Club data never attached to any shot.** At the 25th shot Premier logged
  `Drained 24 stale pending club data entries; club data will be applied to most recent shot …`
  (12:45:03). This is the §2.1 drain, but for the whole drill rather than 6 of 91. **The likely
  cause is the club, not the mode.** The user hit a wedge with no fiducial stickers. The
  Quad can't measure the club without them, which is the same reason FSX2020 gives
  ("may need dots on club face"). The 2026-10-23 hybrid had stickers and got full club data. So
  the warning means "shots waiting for club data that never came", and this drill tells us nothing about
  club data in training mode.
- **`history.json` and `Player.log` did get every shot.** In `history.json` that means ball data
  and carry, with `courseName: "Shot Analysis"` and `holeNumber: -3`, newest first (confirmed).
  In `Player.log` it means one `SimulateShotInternal` line per shot, plus per-shot
  `PredictFlightCollisionAsync` (landing) and `OnBallStopDiagnosticsReceived` (ball at rest) lines.
- **Drill targets are not on disk.** `training-drills.<userId>.json` holds only
  `CustomDrills` and `BestScores` (`"ladder-rising-wedges|Medium": 26`). The drill definitions
  are compiled into the app.

What this means for the connector: club data needs a **stickered club**, which has always been
true of the Quad. The separate open question is whether a **training drill, or an unnamed
club, ever writes to the queue at all**. If not, the connector sees nothing in that mode even
with a stickered club. At best it could fall back to ball-only readings from `Player.log` or
`history.json`. §6 Block 2b separates the three variables: stickers, club name and mode.

Also seen in the startup flags (ConfigCat, logged in `Player.log`):
`"clubDataEntitlementEnabled": false`. Its meaning is unknown. Club data was present on
2026-10-23, so for now it gates nothing we use. A flag with this name could later put club data behind a
subscription tier, though. See §4.5.

`Settings.json` gives `"DistanceUnit": 36, "SpeedUnit": 21, "UnitSystem": 0`. These are enum
values with no labels. The files are SI regardless (§2.1, `carryDistanceMeters`), so
this only matters for comparing against Premier's screen.

### 2.6 Watched live (2026-11-02, Premier 1.2.1)

The §6 session, run as planned except where noted. Premier `1.2.1+288521.d0cd50d` (released
2026-10-29) in the `liver` account; a watcher in the developer account polled every 100 ms.
Every file version it saw, the merged timeline and the scripts are kept outside the repo in
`C:\PinPointStudio\scratch\premier_capture_20261102\` (not sanitised: user id in names, backend ids).
About 45 shots: gap wedge (full stickers), 5-wood and 7-iron (one sticker each), 4-iron (none).
The SDK log clock, `Player.log` and the UTC fields all read the same PC clock (GMT, so local = UTC).

#### 2.6.1 The live source is `SessionLiveSnapshot.<userId>.json`, not the queue

- **Created with the first shot of a range session.** It is not created when the session opens or a club is picked.
  It holds one session object with the same schema as a queue session, plus `Name`,
  `UnwritableGroupReplaced` and a group-level `CreatedAtUtc`. `BackendSessionId`/`BackendShotId`
  are `null` (nothing is uploaded yet). `EndedAtUtc` tracks the latest shot.
- **Rewritten whole once per shot**, ~0.1 s after `SimulateShotInternal`, which is ~0.46–0.69 s
  after contact (when spin arrives). The first shot of a session is slower to land: ~1.6 s after spin.
  No other event rewrites it. Club changes, deletions and club data arriving all wait for the next shot.
- **`ClubData` is one shot behind.** It arrives from the Quad 1.6–2.3 s after contact, but a shot
  is written with `ClubData: null` and its club data only reaches disk with the **next** shot's
  write. This held 25/25 times. The last shot of a session gets its club data:
  - **on exit to the menu** (session end flushes it into the queue), but
  - **not on quitting the app** mid-session: `SessionLiveSnapshotWriter … Quit flush wrote live
    session` rewrote the file **without** the last shot's club data, which had arrived 52 s
    earlier. That club data is lost.
- **Session end** (exit to menu): the session is appended to `SessionUploadQueue`, the snapshot is
  deleted ~0.6 s later, and the upload starts at once. It rewrites the queue once per shot as backend ids
  come back (~0.35 s apart), then removes the session: **"Session upload complete"**, and the
  queue is back to its prior bytes. 14 shots took ~11 s.
- **Abandoned session** (app quit mid-session): on the next launch Premier logs `Promoting
  abandoned live session … to the upload outbox`. It queues, uploads and deletes the snapshot
  within ~2 s of startup.
- **The upload queue opens its file exclusively.** A 100 ms reader caused `Session upload failed
  … Sharing violation on path …SessionUploadQueue…json`, leaving a half-uploaded session for a later retry.
  It retried successfully at the next session end. **A connector must not hold or poll-open these
  files blindly:** stat first, and open only when size or mtime has changed (§4.1). With that the
  violations stopped.
- The stuck 2026-10-23 session still fails at every startup and session end with
  `SESSION_ALREADY_ENDED` from the server's `CreateShot`. It is a server-side rejection and never
  drains. Premier rewrites the queue each time with identical bytes.

#### 2.6.2 Club data and stickers

| Club | Stickers | `ClubData` |
|---|---|---|
| Gap wedge | full set | every field |
| 5-wood, 7-iron | **one** | `ClubSpeedMs`, `Efficiency`, `AngleOfAttackDeg`, `ClubPathDeg` only; the other eight are JSON **`null`** |
| 4-iron | none | `null` (the whole object), even though the Quad sends a ClubData message |

So full club data is a sticker property, not a Premier one (answers §7 Q3). Missing fields are
per-field `null`s, not sentinels.

#### 2.6.3 Shots Premier drops, and ghost shots

- **Shots hit before Premier is ready are silently dropped.** In the rapid-fire block (§6 Block 3),
  shot #224 was hit ~6 s after the previous one, before Premier had reset the tee for the next shot.
  The SDK logged its ball, spin and club messages. Premier wrote no `SimulateShotInternal`, no snapshot entry and no
  history record. **PinPoint would capture that swing and get no reading for it**. Under
  arm-and-claim pairing, it would then be given the next shot's reading (§4.3).
- **Club data drain:** `Drained 3 stale pending club data entries; club data will be applied to
  most recent shot`. Unstickered shots and a 2.1 mph knock leave "pending" entries that
  are only cleared when the next valid club data arrives. Around the dropped shot it can't be
  shown whether the neighbouring shot carries its own club data or the dropped shot's.
- **A practice swing (no ball) sends nothing.** The Quad uses a shot number internally (#227)
  but emits no message.
- **A 2.1 mph, 0° launch, −30° azimuth event was recorded as a real shot** (probably a ball knocked
  while setting up). Premier does not filter it, so a connector should gate on ball speed.
- A thin/toe mishit is an ordinary shot.

#### 2.6.4 Deleting a shot

Deleting a shot in Premier's UI **removes it from the snapshot entirely** on the next write. It does not set
`Hidden: true`. `Hidden` was never seen `true`.

#### 2.6.5 Sign conventions (right-handed golfer, gap wedge, confirmed against Premier's screen)

| Field | Positive means | vs PinPoint |
|---|---|---|
| `AzimuthDeg` | ball starts right (pull read −10.1°) | same → `launchDirection` |
| `SideSpinRpm` | curves right (fade +1887, draw −1042) | same → `sideSpin` |
| `FAxisDeg` | spin axis tilted right (fade +13.2, draw −8.2) | same → `spinAxis` (prefer this over `atan2`) |
| `ClubPathDeg` | in-to-out / right (pull −8.7, fade −12.1) | same → `clubPath` |
| `FaceToTargetDeg` | open / right (screen "4.1 closed" = −4.1) | same → `faceAngle` |
| `FaceImpactHorizontalMm` | **heel** (screen "low toe" = −24.0; heel strike +6.5) | **opposite → negate** for `strikeLocation` |
| `FaceImpactVerticalMm` | high (low toe −5.1; heel +8.9) | — |

Left-handed is untested. All signs above are "right of target" in the world frame, so they should
hold for a left-hander too, except perhaps toe/heel.

#### 2.6.6 Timing

| Event | After contact |
|---|---|
| SDK `BallLaunch` received | 91–151 ms (`TimeSinceContact`; a 2 mph knock read 399 ms) |
| SDK `SpinData`, then `SimulateShotInternal` and `CreatedAtUtc` | **+529 to +626 ms** (`CreatedAtUtc` − contact, 5 shots) |
| Snapshot write | ~0.1 s after that (first shot of a session ~1.6 s) |
| SDK `ClubData` | 1.6–2.3 s |
| `history.json` write and `created` | ~0.8 s after `CreatedAtUtc` (ball landing) |

So `CreatedAtUtc` is **spin-arrival time, not launch** (corrects §2.1). Contact ≈ `CreatedAtUtc` −
0.58 s ± 0.05 s. The exact instant is in the SDK log: receive time minus `TimeSinceContact`.

**Block 9 found a PinPoint bug, not a Premier one.** PinPoint's swing `clock.wallclock` +
`capture.impactUs` came out **2.3–6.2 s later** than the Quad's contact on all 5 shots, and the gap
varied per shot. `ShotProcessor::buildSwingExportJob()` snapshots `wallclockAnchorUtc =
currentDateTimeUtc()` on the assumption that this is when the window was captured. In fact it runs from
`startSwingSave()` after analysis and the pose pass. Every saved swing's absolute time is late by
that shot's processing time. Fix: anchor = now − (`EventBuffer::nowMicros()` −
`window.endTimestampUs()`). **Fixed 2026-11-02:** `ShotProcessor::beginGather()` now takes the UTC
instant in the same breath as it freezes the window end, and the export anchors on that. Swings saved
before the fix keep their late anchor. **The PinPoint↔Premier offset must still be re-measured on the
cabin** (§5 step 6) to size the pairing window; pairing itself no longer depends on the anchor (§4.6).

#### 2.6.7 Other answers, and the verdict

- **Training drills write nothing to the snapshot or queue, stickers or not** (§7 Q8). The drill
  logs `ShotGroupCreateCommand has invalid club name` and creates no group, and the Quad's club data
  is discarded. `history.json` gets ball data, but **labels the shots with the last range club**
  (`5w`/`WOOD` for wedge shots). Its club name can't be trusted in a drill.
- The range will not start without a club, so "no club picked" can't happen there.
- `history.json` `id` ≠ `LocalShotId` (§7 Q6). The join is by time: `created` ≈ `CreatedAtUtc` + 0.8 s.
- No verbose or debug logging option exists in Premier's settings (§7 Q5). It is the remote ConfigCat flag
  `deviceSdkVerboseLoggingEnabled` (false). `clubDataEntitlementEnabled` stayed `false` all session
  and gated nothing.
- `Player.log` is rotated to `Player-prev.log` at every launch. Nothing else carries over.

**Verdict: GO, with the design changes in §4.** The snapshot is a clean, structured, per-shot source
with full club data and real timestamps. Its two flaws can be handled. The one-shot club-data lag is handled by
emitting the ball at once and amending with club data later, or by holding the shot. Dropped shots
are handled by timestamp-checked pairing, which needs PinPoint's wallclock fixed first.

## 3. Comparison with the FSX2020 path

|  | FSX2020 (`LastShot.CSV`) | Premier (`SessionLiveSnapshot`) |
|---|---|---|
| Club data | speed, AoA, path only (rest sentinel) | **all fields with a full sticker set**; speed/AoA/path with one sticker (§2.6.2); **one shot late** |
| Per-shot timestamp | none | **UTC, ~0.58 s after contact** |
| History | one row, overwritten | current range session; deleted at session end |
| Carry / flight | yes | no (history.json has carry) |
| Club name | yes | yes, plus `ClubType` and tags |
| Units | m/s, m | m/s, deg |
| Format stability | stable, documented export | **internal file, undocumented, may change with any update** |
| Location | `Program Files (x86)\…\System` (shareable) | per-user `LocalLow` (same account) |

## 4. Proposed design

### 4.1 Connector

A new `LaunchMonitorBase` subclass, `ForesightPremierMonitor`, with `Kind::Premier` and the settings
token `"premier"`. `GcQuadMonitor` and its CSV parser are untouched, and `Kind::GcQuad` keeps
meaning FSX2020. In the settings panel the GC Quad choice becomes two choices, *"GC Quad —
FSX2020"* and *"GC Quad — Foresight Premier"*. Existing settings keep working unchanged.

- **Source path**: a folder, like today. The default is
  `%USERPROFILE%\AppData\LocalLow\Foresight Sports\Foresight Premier`, resolved for the
  account PinPoint runs under. PinPoint and Premier run under the same account on the cabin
  PC (`liver`). Reading another account's profile is not a supported case.
- **Files** (revised 2026-11-02): read **`SessionLiveSnapshot.*.json`** for live shots. Also read
  **`SessionUploadQueue.*.json`** only to pick up club data for the last shot of a session that ends
  normally (the flush, §2.6.1). Treat both the same way: sessions → groups → shots, keyed by `LocalShotId`.
- **Polling, not `QFileSystemWatcher`**, for the reasons `gcquad_monitor.h` gives. **Stat first;
  open only when size or mtime has changed, read once, close.** Premier opens the queue
  exclusively, and a reader that opens it on every tick makes Premier's upload fail with a sharing violation
  (§2.6.1). Open with full share flags, and treat a failed open as "try next tick", never as an error.
  Both files are written whole and closed, so their stat is current. That isn't true of `Player.log`,
  which is held open.
- **Identity**: keep a set of `LocalShotId`s already emitted. On `start()`, prime the set from
  both files **without emitting** (the base-class rule). A shot is new when its id is not in the set.
- **Emit the ball data immediately, and send club data as a late amendment.** Club data reaches disk only
  with the *next* shot's write (§2.6.1). Holding a shot until then would hand its reading to the
  wrong swing (§4.3). So:
  1. A new shot is emitted at once as a ball-only `LaunchMonitorReading` (`ClubData` is always
     `null` on first sight).
  2. When a later write shows `ClubData` for an already-emitted `LocalShotId`, the connector emits
     a **club amendment** for that id (new signal, e.g. `readingAmended(deviceShotId, reading)`).
     The host keeps a small `deviceShotId → swingDir` map from its claims and merges the club
     fields into that swing's `lm` block.
  3. An amendment for an id that was never claimed (a dropped or missed shot) is logged and discarded.
  The last shot of a session is amended from the queue on a normal exit. If Premier quits mid-session,
  that club data is lost (§2.6.1). Report it, don't wait for it.
- **Deleted shots** disappear from the snapshot (`Hidden` is not used in practice, §2.6.4). Not
  emitted if they vanish first. If they vanish after emission, log and do nothing, because we don't retract.
  Still skip `Hidden: true` in case a future build uses it.
- **Ghost gate**: don't emit a shot below a ball-speed floor (e.g. 10 mph). Premier recorded a
  2.1 mph knock as a shot (§2.6.3).
- **Drain tolerance**: the snapshot vanishing (session end), sessions leaving the queue (upload) and
  stale sessions that never leave (server-rejected) are all normal. None of them is an error state.
- **Schema guard**: parse defensively. If the top-level shape or a required ball field is
  missing, go to `State::Error` with a clear message ("Premier's file format has changed")
  rather than emitting partial readings. Premier updates itself, so this will happen.
- **Carry (optional, phase 2)**: join `history.json` by UTC timestamp to fill
  `carryDistance`. It isn't needed for the validation use, so it's left out of phase 1.

### 4.2 Field mapping to `LaunchMonitorReading`

| Premier | Reading field | Conversion |
|---|---|---|
| `LocalShotId` | `deviceShotId` | as-is (globally unique, unlike FSX2020's counter) |
| `ClubName` (`ClubType`) | `deviceClub` | `"4h"` (recorded only) |
| `BallSpeedMs` | `ballSpeed` | × 2.236936 → mph |
| `ElevationDeg` | `launchAngle` | ° |
| `AzimuthDeg` | `launchDirection` | °, right-positive, **as-is** (§2.6.5) |
| `BackSpinRpm` / `SideSpinRpm` / `TotalSpinRpm` | `backSpin` / `sideSpin` / `spinRate` | rpm, side fade-positive, **as-is** |
| `ClubData.FAxisDeg` | `spinAxis` | °, right-tilt positive, **as-is**; fall back to `atan2(side, back)` when `ClubData` is absent (it is *in* `ClubData` although it is a ball quantity) |
| `ClubData.ClubSpeedMs` | `clubheadSpeed` | × 2.236936 → mph |
| `ClubData.Efficiency` | `smashFactor` | ratio (cross-check against ball/club) |
| `ClubData.AngleOfAttackDeg` | `attackAngle` | ° |
| `ClubData.ClubPathDeg` | `clubPath` | °, in-to-out positive, **as-is** |
| `ClubData.FaceToTargetDeg` | `faceAngle` | °, open positive, **as-is** |
| derived | `faceToPath` | face − path (fade read +12.3, draw −6.9) |
| `ClubData.LoftDeg` | `dynamicLoft` | ° |
| `ClubData.LieDeg` | `lieAngle` | ° |
| `ClubData.ClosureRateDegs` | `closureRate` | °/s |
| `ClubData.FaceImpactHorizontalMm` | `strikeLocation` | mm, **negated**: Premier is heel-positive, ours toe-positive (§2.6.5) |
| `ClubData.FaceImpactVerticalMm` | `strikeHeight` | mm, high-positive |
| `ClubData.ClubSpeedAtImpactMs` | — | not mapped (within ±2 % of `ClubSpeedMs`; meaning unconfirmed) |

Any `ClubData` field may be `null` on its own (one sticker, §2.6.2). Each `null` is a `dropped[]` entry.
Signs follow [`pinpoint_sign_conventions.md`](pinpoint_sign_conventions.md). They come from
deliberate shots checked against Premier's screen on 2026-11-02 (§2.6.5), right-handed only. The range checks and the `dropped[]`
bookkeeping the CSV parser applies (`DroppedField`) apply here too, so a Premier reading
explains its absences the same way.

### 4.3 Pairing

`ShotPairing` (arm on shot detected, the next reading claims it) still works for the ball reading,
because that is emitted ~0.6–0.7 s after contact, well inside the same shot. Club data does **not** go through
it. It is an amendment keyed by `deviceShotId` (§4.1).

**Timestamp-checked pairing is now required, not optional** (revised 2026-11-02). Premier
silently drops a shot hit before it is ready (§2.6.3). On its own that is harmless: PinPoint arms
for the swing, no reading comes, and the next detected swing displaces it. But a swing waits
indefinitely for its reading. If PinPoint then misses the *next* shot, which Premier does record,
that reading is claimed by the earlier swing, which is still armed. The two failures together, at
the fast cadence that causes the first one, give a wrong pairing that nothing flags. Check every claim with `CreatedAtUtc`:
contact ≈ `CreatedAtUtc` − 0.58 s (±0.05 s, §2.6.6). Reject a claim whose contact is not within a
window of our impact time, and disarm a swing whose window has passed.

~~**Blocker:**~~ PinPoint's saved absolute impact time was wrong by 2–6 s (§2.6.6), now fixed. The
pairing does not use it anyway: it takes our impact on the wall clock **at detection** (§4.6). The
window starts at **±1.5 s** and every timed claim logs its Δ, so the cabin test can tighten it. Both
clocks are the same PC, so ±0.5 s should prove ample.

### 4.4 Tests

The CSV parser has `gcquad_csv_parser_test.cpp`. The Premier parser gets the same:
- fixtures cut from the 2026-11-02 captures (`C:\PinPointStudio\scratch\premier_capture_20261102\snap`),
  sanitised: user id in the file name, backend ids zeroed
- cases for the per-field-`null` (one sticker), whole-`null` (no stickers), late-`ClubData`
  amendment, deleted-shot, ghost-gate, snapshot-vanishes, queue-flush, stuck-session,
  multiple-session and changed-schema paths
- a monitor test driving `pollNow()` across a sequence of file versions captured in §6. The
  watcher saves every version, which makes this possible.

### 4.5 Risks

- **Undocumented internal format.** It is the app's own upload queue, not an export, and can change
  in any update. Mitigations: schema guard, fixtures, a clear error state. Reading a plain
  JSON file the app leaves on disk needs no decoding. Still, the Premier EULA should be read for
  anything on reverse engineering or data access before shipping, as was done for GSPro.
- **Upload timing.** Settled (§2.6.1): the queue never drains mid-session, because live shots aren't in it.
  The live snapshot also changed between 1.2.0 and 1.2.1 with no notice, so expect the same again.
- **Interfering with Premier.** Reading too eagerly broke its upload once (§2.6.1). The
  stat-gated read in §4.1 is a requirement, not an optimisation.
- **Club data one shot late, and lost on quit** (§2.6.1). This is inherent to how Premier writes. The amendment path
  covers it except for the last shot before an app quit.
- **Multiple Premier users** on one PC: several queue files. Take the most recently modified
  one, or all of them, keyed by `LocalShotId` either way.
- **Mode-dependent.** Training drills write nothing to the snapshot or queue, even with a stickered
  club (§2.6.7). The connector should say so in its status ("no readings in training drills")
  rather than look broken. A ball-only fallback from `Player.log` is possible. `history.json` is not,
  because its club name is wrong in drills.
- **Entitlement flag.** `clubDataEntitlementEnabled` (§2.5) suggests club data could become
  tier-gated server-side. That would make the queue lose `ClubData` with no change on our side.
  The missing-`ClubData` path (ball-only) already covers it, but the status should report it.

### 4.6 As built (2026-11-02)

Where the build sharpened §4.1–§4.3:

1. **The club update is a full re-reading, not a merge.** When a later write shows a shot's
   `ClubData`, the snapshot holds that shot's ball *and* club data, so the connector re-parses it and
   emits `readingUpdated` with the same `deviceShotId` (first `readAtMs` kept). The host replaces the
   block through the existing `SwingDocWriter::updateLaunchMonitor`, which already re-derives the
   `lm.*` rows and gates. No merge code exists.
2. **A shot is final once it is no longer the newest in its session.** First sight: ball only, with
   **no** club drops (pending, not missing). A write later: with its club data, or with every club
   field a `noData` drop (no stickers). The newest shot is finalised from the queue's session-end
   flush. If it reaches the queue still without club data, Premier was quit mid-session *or* the club
   had no stickers. The file cannot say which, so the reading stays ball-only and the log says so.
3. **The queue is never a source of new shots,** and is opened only while a shot waits on it (stat
   first, open only on change, close at once). A test proves an unreadable queue goes unnoticed while
   nothing waits on it.
4. **Club updates are written only when the pipeline is idle.** An update lands ~0.7 s after the
   *next* shot's contact, exactly as that capture starts, and a whole-document rewrite on the GUI
   thread there is the known freeze. The controller holds updates while `ShotProcessor` is busy, and
   while the re-analysis queue still owes the folder a write. An update for a claim still parked
   replaces the parked reading in place. The carousel, diagnostics and an open replay re-read the
   swing on `readingUpdatedForSwing`, as they do after a re-analysis; no chime.
5. **Pairing takes our impact at detection.** `ShotController::shotDetected` carries `impactUs` on the
   buffer's steady clock; `main.cpp` converts it to the wall clock there and then (`now − (nowMicros −
   impactUs)`), so pairing never depended on the saved-anchor bug. A reading carries
   `contactAtMs = CreatedAtUtc − 580 ms` (written to the raw block as `contactAtMs`, additive). Δ within
   ±1.5 s claims; Δ later than the window disarms the swing (Premier dropped its shot) and the reading
   is unpaired; Δ earlier discards the reading and stays armed. FSX2020 and GSPro readings state no
   contact time and pair by order, as before.
6. **Premier has its own folder setting,** `launchmonitor/premierPath`; empty means Premier's data
   folder for this account. Choosing Premier never overwrites FSX2020's folder. The settings panel
   lists "GC Quad — FSX2020" and "GC Quad — Foresight Premier"; both short labels are "GC Quad", so
   the LM board, report and session summary name the device, not the program.
7. **Ghost gate** at 10 mph ball speed: the 2.1 mph knock is never emitted.

Verified on the desk (2026-11-02): parser, monitor and pairing tests over every captured write; the
running app replaying the session-2 capture into a folder (a reading per shot, the club data a shot
later, the knock ignored, the last shot finalised from the flush); and the controller driven end to
end against a real swing (Δ −40 ms claim, ball-only write at save, club update held while busy and
written when idle, 19 `lm.*` rows). The cabin test (step 6) is the first run against live Premier.

### 4.7 First cabin run (2026-11-02, 17:37–17:47)

One range session, a 4-hybrid, Premier and PinPoint in the `liver` account. Read back from the
swings it wrote (`2026-11-02_Mark-Liversedge_Wrist_02`):

- **Eight shots, eight readings, all with club data.** Cameras were off, so every swing is a
  launch-monitor-only document (no streams, `capture.impactUs` −1), written at first sight with ball data and rewritten
  when the club data landed. Shots 1 and 3 have club speed, attack angle and path but face, lie,
  loft, strike and closure as `noData`: the one-sticker pattern of §2.6.2, shown as "not measured".
- **The last shot was finalised at session end,** its folder rewritten two minutes after its read,
  when Premier flushed the session to the queue.
- **Read latency:** each reading reached PinPoint 596–804 ms after contact (`readAtMs − contactAtMs`),
  so 16–224 ms after Premier's `CreatedAtUtc`.
- **The microphone missed a few shots** (a sports broadcast on the cabin TV). Detected shots and
  missed ones (saved as standalone shots) give the same document here, so which is which, and
  every `contact Δ`, is only in that run's app log.
- **Two empty swing folders** (`swing_0007`, `swing_0009`, during the rapid-fire block) hold no
  document and are not in the library trash. Probably microphone triggers with no reading inside
  the window, most likely from the TV: the folder was made at detection and nothing claimed it.
- **A launch monitor panel bug, not the connector's:** Path, Loft and Impact were drawn only for a
  focused shot while the readings fall back to the latest one, so a capture with auto-replay off
  showed full club readings beside three blank figures. Fixed in 30cc1a89.

Not measured from the swings: the `contact Δ` spread that sizes the pairing window (still ±1.5 s)
is in the app log only, and the `clock.wallclock + capture.impactUs` check against Premier's contact
(§7 Q11) needs a capture with cameras. No club data was seen on the wrong shot (§7 Q12), but no dropped shot is known
in this run either.

## 5. Implementation phases

1. ✅ **Investigate** (§6, done 2026-11-02, §2.6). Fixtures captured.
2. ✅ **Fix PinPoint's swing wallclock anchor** (§2.6.6). Re-running §6 Block 9 to measure the
   PinPoint↔Quad offset moves into step 6.
3. ✅ **Parser + tests**: `premier_session_parser.{h,cpp}` (snapshot and queue share a schema), pure,
   no Qt objects beyond JSON.
4. ✅ **Monitor**: `ForesightPremierMonitor`, `Kind::Premier`, factory, settings token, settings
   panel choice with the default path, the club update signal and the host's write into the swing (§4.6).
5. ✅ **Timestamp-checked pairing** (§4.3, §4.6).
6. 🔧 **Cabin test**: first run passed 2026-11-02 (§4.7), cameras off. Still to do: the Δ spread
   from an app log, and the wallclock check from a run with cameras. The original brief: 20 shots including a rapid-fire run, every swing gets its
   reading and its club data, values match Premier's screen; the logged Δ sizes the pairing window
   (§4.3), and new swings' `clock.wallclock + capture.impactUs` agree with Premier's contact.
7. *(Optional)* carry from `history.json`.

## 6. Investigation session — you hit, I watch

> **Ran 2026-11-02; results in §2.6.** Changes from the plan below: Premier was already open, so
> Block 0 was read from its log afterwards. Block 2 used a one-sticker 5-wood rather than a driver.
> In Block 2b the "no club" case is impossible on the range. Block 7 found no option. A bonus block
> quit Premier mid-session. **Watcher lesson:** the 100 ms open-and-hash below broke Premier's queue upload
> (sharing violation). It was changed mid-session to stat first and open only on change. Keep that change
> for any re-run. The directory listing's size lags for `Player.log` (held open), so tail that one
> through a handle.

**Goal**: answer every TBD in §2–§4 from live observation, and capture every version of every
file Premier writes, as fixtures. About **60 balls, 45–60 min**. Premier runs in the `liver`
account. I run a watcher from the developer account. PinPoint doesn't need to be running,
except for Block 9.

### Before the session (me)

A watcher script in my scratchpad, not in the repo, that every **100 ms**:
- stats every file under the Premier data root and `C:\ProgramData\Foresight`, excluding the
  auth/credential files in §2. It records size, mtime and a hash to a timeline CSV with a
  high-resolution timestamp.
- copies each new version of `SessionUploadQueue.*.json` and `ShotReplay\history.json` to a
  numbered snapshot folder, opening with read-share so Premier is never blocked
- tails `Player.log` and `FSS_SDK_MANAGED_LOG.txt`, following renames, into the same timeline.

I check that it runs cleanly against a closed and then an idle Premier before you start hitting.
After each block I summarise what changed. You don't need to read any of it.

### Blocks

Leave ~20 s between shots unless the block says otherwise, so each shot's writes stand apart.
Call out anything odd as it happens (a mishit, a shot you deleted). I'll note the time.

| # | What you do | What it answers |
|---|---|---|
| 0 | Start Premier, open the range, hit nothing for 1 min | What Premier writes at startup and while idle; queue state for a fresh session. Expect a queue rewrite from the upload retry of the stuck 2026-10-23 session (§2.1). Note whether that upload now succeeds, and whether 1.2.0 changed the queue schema |
| 1 | **5 shots**, one club (e.g. 7i) | Per-shot write sequence: when the shot appears in each file, whether `ClubData` arrives later as an amendment, whole-file rewrite vs append. Latency of each source vs the SDK's ball-launch line |
| 2 | **5 shots**, a different club (driver), changing club in Premier first | `ClubName` / `ClubType` / tags / new `ShotGroup`. Whether full club data comes through for a second club (the "dots on club face" question) |
| 2b | All with a **stickered** club: **2 shots** on the range with no club picked in Premier, then **3 shots** in a training drill. Then **2 range shots** with an **unstickered** club | Whether a queue entry needs a named club, or the range rather than a drill (§2.5); whether drills get club data when the club has stickers; what an unstickered shot looks like in the queue (no `ClubData`, or no entry) |
| 3 | **5 shots quickly** (~6–8 s apart) | Whether club data falls behind and drains, as the 6 missing shots did; the settle time §4.1 needs |
| 4 | One swing with no ball; one heavy mishit or top | Ghost-shot filtering: does anything reach the queue? Does a top produce a ball-only entry? |
| 5 | Delete one shot in Premier's UI | Does `Hidden` flip, or does the entry disappear? |
| 6 | End the session (or switch mode), wait 2 min, then start a new one | When the queue drains, what `EndedAtSynced` does, whether a drained shot leaves while a session is still open |
| 7 | Look in Premier's settings for a debug/verbose/logging option; if there is one, enable it and hit **3 shots** | Whether the managed SDK log then carries values plus the contact offset (§2.4) |
| 8 | **Sign shots** with one club: a clear push, a clear pull, a fade, a draw; then a toe strike and a heel strike (impact spray or tape helps) | Signs of `AzimuthDeg`, `SideSpinRpm`, `ClubPathDeg`, `FaceToTargetDeg`, `FaceImpactHorizontalMm` against our conventions |
| 9 | *(Optional)* start PinPoint with LM set to none and hit **5 shots** | Offset between PinPoint's impact time and Premier's `CreatedAtUtc`, for the timestamp-checked pairing (§4.3) |
| 10 | Close Premier, reopen it, hit 1 shot | `Player.log` rotation; what survives a restart; whether the old queue persists |

### After the session (me)

- Update §2–§4 here with the answers, and replace each **TBD** with the measured value.
- Add a Premier section to `foresight_gcquad_data_sources.md`.
- Keep the snapshot folder as the test fixtures for §4.4, sanitised before anything is committed.
- Bring back a go/no-go on the connector, plus any change to the design above, before writing code.

## 7. Open questions

Answered 2026-11-02 (§2.6) unless marked open.

1. ✅ Written when spin arrives with `ClubData: null`; club data arrives with the **next** shot's write (§2.6.1).
2. ✅ Only after the session ends, never mid-session. Live shots aren't in the queue on 1.2.1 (§2.6.1).
3. ✅ A sticker property: full set → all fields, one sticker → speed/AoA/path, none → `null` (§2.6.2).
4. ✅ Right-positive throughout, except impact horizontal, which is heel-positive (§2.6.5). Left-handed untested.
5. ✅ No. It is the remote flag `deviceSdkVerboseLoggingEnabled`, not a user setting. The contact
   instant is still available: SDK receive time − `TimeSinceContact` (§2.6.6).
6. ✅ No. Join by time (`created` ≈ `CreatedAtUtc` + 0.8 s).
7. ✅ Checked 2026-11-02 against the EULA shipped with FSX2020 and Foresight's current web copy,
   which are the same text. The clause that matters is "Unauthorized Integrations": the System may
   be used only with Foresight's software and authorised third-party software. It is aimed at
   software that integrates with the device itself, which is what Foresight's Authorized
   Integration Licenses are sold for. PinPoint never talks to the Quad; it reads files Foresight's
   own software wrote, and that applies equally to the FSX2020 connector. PinPoint is personal and
   non-commercial. Never decompile Foresight's software, and never read its sign-in files.
8. ✅ Drills write nothing, stickered or not. The range requires a club (§2.6.7).
9. ✅ Server rejects it (`SESSION_ALREADY_ENDED` on `CreateShot`), so it never drains. Successful uploads drain at session end (§2.6.1).
10. **Open.** Stayed `false` and gated nothing observable.
11. **Open.** PinPoint↔Quad impact offset. The wallclock fix is in (§2.6.6); the first cabin run had
    cameras off, so nothing to measure it on (§4.7).
12. **Open (new).** Does a club-data amendment ever arrive for the wrong shot around a dropped
    shot (the drain warning, §2.6.3)? Not seen in the first cabin run, but no dropped shot is known there either (§4.7).
