# GC Quad under Foresight Premier — a fourth launch-monitor connector

**Audience**: whoever builds the Premier connector, and whoever runs the investigation session (§6)
**Code**: none yet. Would sit beside `src/LaunchMonitor/gcquad_monitor.{h,cpp}` and `gcquad_csv_parser.{h,cpp}`
**Source**: direct inspection of the capture PC on 2026-10-23, `liver` account, after one Premier
session of 91 shots with a 4-hybrid. Background: [`foresight_gcquad_data_sources.md`](../reference/foresight_gcquad_data_sources.md)
**Status**: 🔍 **PROPOSED**. The findings are from files left on disk after the session.
Nothing has been watched live yet, and §6 is the plan for doing that before any code is written.
**Written**: 2026-10-23

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
| `SessionUploadQueue.<userId>.json` | Every shot of the **current, not-yet-synced** session, ball + club | **yes, full** | UTC, 100 ns | §2.1. The best source. It's a queue, so its contents get drained |
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
- **`CreatedAtUtc` is ball-launch time** to within a few ms. `Player.log` logs the same shot
  at 14:14:54.249 local (BST, UTC+1). The queue says 13:14:54.253 UTC.
- **There is a real UTC timestamp per shot.** This is a material improvement on `LastShot.CSV`,
  which has none. `ShotPairing`'s arm-and-claim rule exists *because* the CSV can't say when
  a row was made (§4.3).
- **The queue drains.** `ShotReplay\history.json` reaches back to 12:55 UTC, an earlier
  Premier run (`Player-prev.log`, 72 shots). The queue holds **only** the session that started
  at 13:14. So the earlier session left the queue, almost certainly on upload. The current
  session is still present with `EndedAtSynced: false`. **The queue can't be the archive.**
  The connector must take each shot as it appears and never assume an earlier one is still
  there. §6 Block 6 confirms when entries leave.

Unknown until watched (§6): whether a shot is written at ball launch and amended when
`ClubData` lands, or written once complete. Also: whether the file is rewritten whole or in
place, how the write is locked, and the sign conventions of `AzimuthDeg`, `SideSpinRpm`,
`ClubPathDeg`, `FaceToTargetDeg`, `FAxisDeg` and `FaceImpactHorizontalMm`.

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

## 3. Comparison with the FSX2020 path

|  | FSX2020 (`LastShot.CSV`) | Premier (upload queue) |
|---|---|---|
| Club data | speed, AoA, path only (rest sentinel) | **all fields** (this session) |
| Per-shot timestamp | none | **UTC, at ball launch** |
| History | one row, overwritten | current session, until uploaded |
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
- **Polling, not `QFileSystemWatcher`**, for the reasons `gcquad_monitor.h` gives. Re-parsing
  ~100 KB of JSON every 250 ms is cheap but not free. Gate the parse on size+mtime *changing*,
  which is safe here as it was not for the CSV, because the queue only grows within a session.
  A plain byte compare also works.
- **Identity**: keep a set of `LocalShotId`s already emitted. On `start()`, prime the set from
  whatever is in the queue **without emitting** (the base-class rule). A shot is new when its
  id is not in the set.
- **Completion**: if §6 shows shots are written at launch and amended with `ClubData` later,
  hold a new shot until `ClubData` appears **or** a settle time passes with no change. The
  settle time comes from §6; the expectation is about 3 s, given club data at ~1.7 s. Emit then.
  A shot that settles without `ClubData` is emitted ball-only (the drain case in §2.1).
- **Hidden shots**: `Hidden: true` (the user deleted the shot in Premier) → not emitted. If it
  flips after emission, log it and do nothing; we don't retract readings.
- **Drain tolerance**: entries leaving the queue (upload), the session list emptying, or the
  file vanishing are all normal. None of them is an error state.
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
| `AzimuthDeg` | `launchDirection` | °, **sign TBD** |
| `BackSpinRpm` / `SideSpinRpm` / `TotalSpinRpm` | `backSpin` / `sideSpin` / `spinRate` | rpm, side **sign TBD** |
| derived | `spinAxis` | `atan2(side, back)` as today |
| `ClubData.ClubSpeedMs` | `clubheadSpeed` | × 2.236936 → mph |
| `ClubData.Efficiency` | `smashFactor` | ratio (cross-check against ball/club) |
| `ClubData.AngleOfAttackDeg` | `attackAngle` | ° |
| `ClubData.ClubPathDeg` | `clubPath` | °, **sign TBD** |
| `ClubData.FaceToTargetDeg` | `faceAngle` | °, **sign TBD** |
| derived | `faceToPath` | face − path, once signs are pinned |
| `ClubData.LoftDeg` | `dynamicLoft` | ° |
| `ClubData.LieDeg` | `lieAngle` | ° |
| `ClubData.ClosureRateDegs` | `closureRate` | °/s |
| `ClubData.FaceImpactHorizontalMm` | `strikeLocation` | mm, heel(−)/toe(+) **TBD** |
| `ClubData.FaceImpactVerticalMm` | `strikeHeight` | mm |
| `ClubData.ClubSpeedAtImpactMs`, `FAxisDeg` | — | not mapped yet (meaning unconfirmed) |

Signs follow [`pinpoint_sign_conventions.md`](pinpoint_sign_conventions.md). They come from
deliberate shots in §6 Block 8, not from guesswork. The range checks and the `dropped[]`
bookkeeping the CSV parser applies (`DroppedField`) apply here too, so a Premier reading
explains its absences the same way.

### 4.3 Pairing

Phase 1 keeps `ShotPairing` as it is: arm on shot detected, claim the next reading. It already
handles a reading arriving before or after the swing directory exists, and nothing about
Premier breaks it.

Phase 2, *optional and only if §6 supports it*: use `CreatedAtUtc` to check the claim. Reject a
reading whose launch time is not within a window of our own detected impact time. This would
end the one failure mode the arm rule can't see: a shot we missed whose reading is then given
to the next swing. That needs the PC clock and Premier's clock to agree, which is the same
machine, so they should. §6 measures the offset.

### 4.4 Tests

The CSV parser has `gcquad_csv_parser_test.cpp`. The Premier parser gets the same:
- a fixture cut from the real queue file (sanitised: user id in the file name, backend ids
  zeroed)
- cases for the missing-`ClubData`, hidden-shot, drained-queue, multiple-session and
  changed-schema paths
- a monitor test driving `pollNow()` across a sequence of file versions captured in §6. The
  watcher saves every version, which makes this possible.

### 4.5 Risks

- **Undocumented internal format.** It is the app's own upload queue, not an export, and can change
  in any update. Mitigations: schema guard, fixtures, a clear error state. Reading a plain
  JSON file the app leaves on disk needs no decoding. Still, the Premier EULA should be read for
  anything on reverse engineering or data access before shipping, as was done for GSPro.
- **Upload timing.** If the queue drains mid-session, for example when a shot group closes,
  shots could leave between polls. At 250 ms polling against seconds between shots this is
  unlikely, but §6 Block 6 settles it.
- **Multiple Premier users** on one PC: several queue files. Take the most recently modified
  one, or all of them, keyed by `LocalShotId` either way.

## 5. Implementation phases

1. **Investigate** (§6). Output: answers to the TBDs above, plus a folder of captured file
   versions to use as test fixtures.
2. **Parser + tests**: `premier_queue_parser.{h,cpp}`, pure, no Qt objects beyond JSON.
3. **Monitor**: `ForesightPremierMonitor`, `Kind::Premier`, factory, settings token, settings
   panel choice with the default path.
4. **Cabin test** with the real Quad: 20 shots, every swing gets its reading, values match
   Premier's screen.
5. *(Optional)* carry from `history.json`; timestamp-checked pairing.

## 6. Investigation session — you hit, I watch

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
| 0 | Start Premier, open the range, hit nothing for 1 min | What Premier writes at startup and while idle; queue state for a fresh session |
| 1 | **5 shots**, one club (e.g. 7i) | Per-shot write sequence: when the shot appears in each file, whether `ClubData` arrives later as an amendment, whole-file rewrite vs append. Latency of each source vs the SDK's ball-launch line |
| 2 | **5 shots**, a different club (driver), changing club in Premier first | `ClubName` / `ClubType` / tags / new `ShotGroup`. Whether full club data comes through for a second club (the "dots on club face" question) |
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

1. Is a shot written once complete, or written at launch and amended with `ClubData`? (Block 1)
2. When exactly does the queue drain, and can it happen mid-session? (Block 6)
3. Is full club data a Premier property, or did this hybrid happen to be marked? (Block 2)
4. Sign conventions of the direction, spin and impact fields. (Block 8)
5. Can SDK verbose logging be turned on from Premier, and does it give values plus the contact
   offset? (Block 7)
6. Does `history.json`'s `id` match the queue's `LocalShotId`? (Block 1 snapshots)
7. What does the Premier EULA say about reading its local files? (desk check, before shipping)
