# GC Quad under GSPro — ConnectDebug.txt connector

**Audience**: whoever builds the third launch-monitor connector
**Code**: none yet. Would sit beside `src/LaunchMonitor/gcquad_monitor.{h,cpp}` and `gcquad_csv_parser.{h,cpp}`
**Source**: [`foresight_gcquad_data_sources.md`](../reference/foresight_gcquad_data_sources.md) §Source 3 and §Source 4, plus direct measurement on the capture PC (§3)
**Status**: ⛔ **DECLINED 2026-09-26 — do not build, and do not re-propose without new facts** (see below)
**Written**: 2026-09-25

---

## ⛔ Decision: not pursued

**Read this before proposing GC Quad data under GSPro again. The question has been asked and
answered.**

**The goal was capture, not the launch monitor.** Capturing swings while playing sim golf
matters because every shot has jeopardy, which tests discipline under pressure in a way
range practice does not. Launch-monitor data was a nice-to-have on top of that.

**PinPoint already captures under GSPro.** Video, IMU and acoustic shot detection do not need
the launch monitor at all. What is lost is only the `lm.*` block, and that loss is **accepted**.
Under GSPro, swings simply have no launch-monitor data.

**Why this connector is not worth building:**

- **Ball data only.** GSPconnect never logs club data (§2), so the `lm.clubheadSpeed` and
  `lm.attackAngle` checks against our camera estimates, the main reason PinPoint reads a launch
  monitor at all, are unavailable under GSPro anyway.
- **The latency is not near-real-time.** About half a second after contact is the floor (§3).
- **The cost is real for that return.** It needs a third connector, a tail that must not block
  log4net's rollover, a change to `ShotPairing` because readings can now beat `shotDetected`
  (§5), and a second tail of the SDK log for contact time (§6).

**Also considered and rejected:** a bridge on the private GSPconnect↔GSPro channel (TCP 9050)
to get club data. It is out of bounds. See the reference doc's
[Excluded](../reference/foresight_gcquad_data_sources.md#excluded) section. Do not re-propose it.

**What would reopen this:** a supported way to get **club** data from the Quad while GSPro
drives it, or launch-monitor data becoming central to sim-golf sessions. Latency alone is not
enough.

**For launch-monitor data, use FSX2020** with `GcQuadMonitor`, in practice sessions.

The rest of this document is the design as it stood when declined, kept so that the analysis
is not repeated.

---

## Contents

1. [Why this matters](#1-why-this-matters)
2. [What the file gives us, and what it does not](#2-what-the-file-gives-us-and-what-it-does-not)
3. [Latency, measured](#3-latency-measured)
4. [The design](#4-the-design)
5. [Pairing: the reading can now beat the shot](#5-pairing-the-reading-can-now-beat-the-shot)
6. [Contact time from the managed SDK log](#6-contact-time-from-the-managed-sdk-log)
7. [Testing](#7-testing)
8. [Phasing](#8-phasing)
9. [Definition of done](#9-definition-of-done)
10. [Open decisions](#10-open-decisions)

---

## 1. Why this matters

Today the only GC Quad path is `GcQuadMonitor`, which reads FSX2020's `LastShot.CSV`. It has
two costs:

- **It needs FSX2020 driving the Quad.** Only one application can hold the device, so it rules
  out GSPro. Shots captured on a practice range lack the jeopardy of a round of sim golf, which
  is the more realistic test of a swing.
- **It is slow.** FSX2020 writes the CSV about **5 s** after the shot.

`C:\GSProV1\Core\GSPC\ConnectDebug.txt` removes both. `GSPconnect.exe` writes it on every shot
while GSPro drives the Quad, **about half a second after contact** (§3). The file is ordinary
log4net output in plaintext. Reading it needs no decoding and no cooperation from GSPro.

This is also not a new *device*: it is the same GC Quad, reached through a different host
application. So it earns its own `Kind` for settings and provenance, while the reading shape,
the units and the pairing are all shared with the existing connector.

**Evidence the file is live under GSPro.** `GSPconnect.exe` is started only by `GSPro.exe`. The
launcher does not start it, and FSX uses a different SDK build. Recent files on the capture PC
hold 398, 200, 69, 182 and 302 shots for 24, 23, 20, 17 and 15 September. The archive goes back
to November 2024.

## 2. What the file gives us, and what it does not

Three lines per shot, always in this order:

```
2026-09-24 21:45:11,442 [1] INFO  VGPconnect.ForesightForm [(null)] - Sending Shot
2026-09-24 21:45:11,442 [1] INFO  VGPconnect.ForesightForm [(null)] - Logging ball data IMMEDIATELY before sending to GSPro
2026-09-24 21:45:11,443 [1] INFO  VGPconnect.ForesightForm [(null)] - [103.689224,19.2373333,-0.5078487,985.0,4859.8667,4759.0,11.6937485,0.0]
```

| # | Field | Unit | → `LaunchMonitorReading` |
|---|---|---|---|
| 0 | Ball speed | mph | `ballSpeed` |
| 1 | Vertical launch angle | ° | `launchAngle` |
| 2 | Horizontal launch angle | ° | `launchDirection` (sign to verify, §7.4) |
| 3 | Side spin | rpm | `sideSpin` |
| 4 | Total spin | rpm | `spinRate` |
| 5 | Back spin | rpm | `backSpin` |
| 6 | Spin axis | ° | `spinAxis` (sign to verify, §7.4) |
| 7 | Carry | always `0.0` | **ignored** — hard-coded on the Foresight path |

The values are float32 printed round-trip (`65.4664459`), so the parse is exact.

**What it does not carry:**

- **No club data.** GSPconnect sends club data to GSPro, but never logs it. So
  `lm.clubheadSpeed`, `lm.attackAngle`, `lm.clubPath`, `lm.faceAngle` and the rest stay empty
  under this connector. ⚠ `lm.clubheadSpeed` and `lm.attackAngle` are the two reference values
  our live camera estimates are checked against today. **Under GSPro that comparison is
  unavailable.** FSX2020 remains the route for club validation sessions. This connector does not
  retire `GcQuadMonitor`.
- **No shot number.** §6 recovers one from the managed SDK log.
- **No contact time.** The line is written when spin arrives, not at contact. §6 again.
- **No flight results.** GSPro computes carry and total, but they never reach this file.

**Spin can be absent, and then it is logged as zeros.** When the Quad reports
`SpinIsValid = 0`, GSPconnect writes `0.0` into indices 3–6. Real example, shot 401 on 24 Sep:
`[4.339472,0.934056461,9.550428,0.0,0.0,0.0,0.0,0.0]`. The mapping must therefore treat
**all four spin fields exactly zero** as "not measured" and leave them unset. Writing a zero
would be a value the device never reported, which the reading contract forbids.

**Shots GSPconnect rejects are never logged.** Its ghost-read, zero-spin, putt-placement and
<1 mph filters run *before* "Sending Shot". Those shots also never reach GSPro, so PinPoint
and the simulator agree on what counted.

## 3. Latency, measured

The managed SDK log records, for each shot, how long after contact each device message arrived.
Across **9,578 shots** in the retained logs:

| Message | p50 | p90 | p99 | max |
|---|---|---|---|---|
| Ball launch | 123 ms | 156 ms | — | 695 ms |
| **Spin** | **536 ms** | **671 ms** | **1131 ms** | 2197 ms |

GSPconnect holds the ball record until spin arrives. It then logs and sends in the same call.
Correlating the two logs on 24 Sep shows the ConnectDebug line landing **within 2 ms** of the
SpinData receipt:

| Shot | Spin since contact | SpinData received | ConnectDebug line |
|---|---|---|---|
| 398 | 717 ms | 21:44:33.106 | 21:44:33,107 |
| 399 | 667 ms | 21:44:53.831 | 21:44:53,833 |
| 400 | 554 ms | 21:45:11.441 | 21:45:11,443 |

So **contact-to-line ≈ spin latency: a median of about half a second**, against about 5 s for
the CSV. The log4net FileAppender is not buffered, so the line is on disk when written. What we
add on top is the poll interval (§4.2).

The Quad's own spin measurement is the floor. No reading of the GSPro side can beat it for
ball-plus-spin.

## 4. The design

### 4.1 Shape

A new connector, following the split the other two already use:

| Piece | Where | What |
|---|---|---|
| `Kind::GcQuadGsPro` | `launch_monitor_base.h/.cpp`, factory | Token `"gcquad-gspro"`. Label "Foresight GC Quad (GSPro)". Short label "GC Quad" |
| The parser | `connectdebug_parser.h/.cpp` | Pure: bytes in → complete shot records out, plus the unconsumed tail. No Qt I/O. The same split as `gcquad_csv_parser` |
| The mapping | inside the parser, or `connectdebug_reading.cpp` | Record → `LaunchMonitorReading`, including the all-zero-spin rule and field 7 dropped |
| The tail | `connectdebug_tail.h/.cpp` | Pure logic on (size, identity, bytes read). Decides offsets and rollover. Testable without a filesystem |
| The monitor | `gcquad_gspro_monitor.h/.cpp` | `LaunchMonitorBase` subclass: a `QTimer`, the tail, the parser |
| Settings | `app_settings.h`, `LaunchMonitorPanel.qml` | Folder (default `C:\GSProV1\Core\GSPC`) and poll interval. Its own keys, apart from the FSX folder |

`deviceKind = "gcquad-gspro"`, so provenance in `swing.json` records which host produced the
numbers. `deviceClub` is empty: the club selection travels on the private channel, not in the log.

### 4.2 Tailing — ⚠ never hold the file open

log4net's daily roll **renames** `ConnectDebug.txt` to `ConnectDebug.txt.YYYY-MM-DD` on the
first shot of a new day. On Windows, a rename fails if another process holds the file open
without `FILE_SHARE_DELETE`, and `QFile` does not ask for it. A tail that keeps a handle open
could therefore **break GSPconnect's rollover**. At best that would scramble the day's logs. At
worst it would make the appender fail on the shot being sent to GSPro.

So each poll does open → seek → read new bytes → close, and never holds a handle between
polls. The GCQuad monitor already works this way for its own reasons. The cost is one stat and
a small read every poll.

Per poll:

1. Stat `ConnectDebug.txt`. If it is absent, stay `Waiting`: GSPro has never run on this
   machine, or the folder is wrong.
2. **Rollover check.** Record the file's identity at the last read (creation time, plus the
   Windows file index where available). If the identity changed or the size shrank, the old
   file was rolled. First finish reading the **renamed** file from the old offset, to pick up
   lines written between our last poll and the rename. Then start the new file at offset 0.
   The renamed file is `ConnectDebug.txt.<date of the old file's last line>`.
3. Read from the offset to EOF. Append to a carry buffer and hand complete lines to the parser.
   A trailing partial line stays in the buffer, so a line caught mid-write is never parsed.
4. Emit one reading per complete three-line record.

**Start-up baseline.** On `start()`, set the offset to the current size and emit nothing. That
is the base-class contract: the file holds thousands of old shots.

**Poll interval.** Default **50 ms**. The cost is negligible, and it keeps the added latency
below one video frame at the lower capture rates. The GCQuad monitor's 250 ms suits a CSV that
arrives 5 s late. It is wrong here.

**Local only.** The folder may be an SMB share, as for the CSV, but the rollover identity check
is only reliable locally. Document it as supported only on the machine running GSPro, which is
the capture PC anyway.

### 4.3 Parsing

Match the **payload** after ` - `, not the fixed prefix: the thread id and padding can change.
The record is:

- `Sending Shot`, then
- `Logging ball data IMMEDIATELY before sending to GSPro`, then
- a line whose payload is a JSON array of exactly 8 numbers.

Rules:

- An array line not preceded by the marker is ignored. Other code paths might log arrays.
- A marker not followed by an array within the next 3 lines is dropped with a debug log, not
  an error.
- The timestamp is local wall-clock time with a comma before the milliseconds
  (`yyyy-MM-dd HH:mm:ss,zzz`). Keep it on the reading. §5 and §6 need it.
- Parse the numbers with the C locale. The file uses `.` whatever the Windows locale.
- Plausibility gates: ball speed 0–250 mph, VLA −10–90°. Outside those, drop the record and
  log it, the same way the CSV parser treats the `16777215` sentinel. Keep the gates loose:
  their job is to catch a corrupt line, not a bad shot.

## 5. Pairing: the reading can now beat the shot

`ShotPairing` assumes a reading arrives *after* `shotDetected`. With a 5 s CSV that is always
true. It is no longer guaranteed:

- `shotDetected` fires after the arbiter's 200 ms hold, plus whatever the winning detector
  needs. That is roughly 0.2–0.5 s after impact.
- The ConnectDebug line lands at a median of 536 ms, but the **minimum is 124 ms** (§3).

A reading that arrives while nothing is armed is **discarded**. So a fast spin read would lose
its reading silently. That is exactly the failure users would not notice.

**Change:** give `ShotPairing` a short **pre-arm slot**. The rule becomes:

```
reading arrives, nothing armed  ─► hold it as `early` (at most one), stamped with its arrival
shot detected                   ─► if `early` is present, arrived ≤ kEarlyGraceMs ago, and
                                   (when §6 supplies contact time) |contact − impact| ≤ kContactTolMs
                                       → claim it for this swing
                                   otherwise drop `early`, arm as today
```

- `kEarlyGraceMs = 1500`. That covers the arbiter hold plus the slowest detector, and is much
  shorter than any realistic gap between two shots.
- `kContactTolMs = 250` applies only once §6 is in. Before that, the grace window alone decides.
- The single-slot argument in `shot_pairing.h` still holds: the processor handles one shot at
  a time, so there is never more than one early reading worth keeping.
- The FSX CSV path cannot trigger this. Its readings always arrive after the shot, so its
  behaviour does not change. The new cases go into `gcquad_pairing_test.cpp`.

The processor-busy case is unchanged. A shot PinPoint did not commit arms nothing, so its
reading is held as `early`, expires and is dropped. That is still the honest outcome.

## 6. Contact time from the managed SDK log

`C:\ProgramData\Foresight\FSS_SDK_MANAGED_LOG.txt` is written by Foresight's own DLL during
GSPro play. It carries no metrics, but for every shot it records:

```
21:45:10.983 75 DataReceivedHandler START 0x0002:BallLaunchedMessage:44 size=50 dataSize=50
BallLaunch ShotNumber=400 TimeSinceContact= 103 ms
21:45:11.441 75 DataReceivedHandler START 0x0003:SpinDataMessage:20 size=26 dataSize=26
SpinData ShotNumber=400 TimeSinceContact= 554 ms
```

That gives us the two things ConnectDebug lacks:

- **The device shot number.** Use it as `deviceShotId`.
- **The contact instant**, as `BallLaunch receipt − its TimeSinceContact` (here
  10.983 − 0.103 = 21:45:10.880). If there is no BallLaunch block, fall back to the SpinData
  equivalent.

**Joining the logs.** A ConnectDebug record matches the SpinData block received within
**±20 ms** of the record's line time (observed: 1–2 ms). Both use the same PC clock. Take the
date from the ConnectDebug line: the managed log prints time only.

**Mapping to PinPoint's timeline.** `impactUs` is steady-clock time, and the logs use wall-clock
time. Sample the two clocks together each time a record is read, and convert with that offset.
The drift over a second is negligible.

Once the contact instant is on the reading, the pairing tolerance in §5 applies, and
`swing.json` can record the offset between device contact and our impact. That offset is worth
tracking as a detector-accuracy metric in its own right.

Tailing is the same as §4.2: open and close each poll, never hold the file. This log rolls at
about 1 MB into `FSS_SDK_MANAGED_LOG_<date>_<time>.txt` and may roll in the middle of a
session.

**Degradation.** If the managed log is missing or stops, readings still flow without shot
number or contact time, and pairing falls back to the grace window alone. Never block a
reading on this log.

**Future, not in scope.** The BallLaunch block lands about 123 ms after contact. That makes it
a strong candidate for a shot-detection source feeding the arbiter, and it would work the same
under FSX (native log) and GSPro (managed log). It gets its own design if pursued.

## 7. Testing

### 7.1 Parser, against the real corpus

The archive (`ConnectDebug.txt.*`, 198+ files, ~30,000 shots since 2024-11-17) is the test set.

- A test fixture is a trimmed excerpt: one normal day, one day with a mid-session rollover
  seam, and one file with a zero-spin shot. Commit it under `src/LaunchMonitor/tests/data/`.
- A corpus sweep, run manually rather than in CI, parses every archived file and asserts:
  - records = count of `Sending Shot` lines, with no marker left unmatched;
  - every record has 8 fields, and field 7 is 0.0;
  - `|√(BS² + SS²) − TS| < 0.5 rpm` and `|asin(SS/TS)·180/π − SA| < 0.05°` whenever spin is
    present. These internal identities catch any column slip;
  - all four spin fields are zero, or none are.

### 7.2 Tail logic, pure

Cover each of these in `connectdebug_tail` unit tests:

- partial line then completion
- several records in one read
- a record split across three polls
- the start-up baseline (no historic emission)
- rollover with unread lines left in the old file
- a shrink without a rename
- the file absent, then appearing

### 7.3 Tail against a real file, with a writer that renames

A test writes lines into a temporary `ConnectDebug.txt` in the same way log4net does, and runs
the monitor at 10 ms polls. It **renames the file mid-stream with `MoveFileEx`**, with no
retry. Assert:

- every rename succeeds, so the monitor never blocks rollover;
- no record is lost or duplicated across the seam.

This is the test that guards §4.2's ⚠. It is Windows-only.

### 7.4 Sign conventions — before any spin-axis or HLA metric is graded

`pinpoint_sign_conventions.md` fixes right (+) / left (−) for launch direction and spin axis.
GSPconnect computes `SA = asin(SS/TS)` from Foresight's side spin, whose sign we have not
confirmed. Check it on a live session: hit a deliberate push-fade and pull-draw pair, then
compare the logged signs against GSPro's shot shape and PinPoint's video. Record the result
in the reference doc. Until then, carry the same "sign unverified" note as `gspro_reading.cpp`.
Left-handed play also needs a check: GSPconnect negates club path and face for LH, but not the
ball fields.

### 7.5 Replay harness

`tools/launchmonitor/fake_connectdebug.py`, following the pattern of `fake_shot.py`, replays a
real archived day into a scratch folder with its original spacing. It can compress the gaps,
and it can force a rollover at a chosen shot. That lets the whole path, including
`LaunchMonitorPanel`, run on any machine without GSPro.

### 7.6 Pairing

New cases in `gcquad_pairing_test.cpp`:

- the reading arrives 124 ms after impact and before `shotDetected` → claimed;
- the reading arrives with nothing armed and no shot follows within the grace window →
  dropped;
- an early reading, then a shot 2 s later → dropped, and the shot waits for its own reading;
- with contact time: an early reading whose contact is 400 ms from impact → dropped.

### 7.7 Live acceptance on the capture PC

Play one session of at least 100 shots in GSPro with PinPoint capturing. Record:

- **Completeness:** ConnectDebug records = PinPoint readings emitted = swings with an
  `lm.ballSpeed`, apart from shots PinPoint itself did not commit. Each gap is explained by
  the log.
- **Latency:** contact (from §6) to `readingAvailable`, p50/p90. Expect about 540/680 ms. Flag
  anything more than 60 ms above the spin latency for the same shot.
- **Rollover:** run once across midnight, or with the system clock advanced, and see the
  first shot of the new day arrive.
- **Non-interference:** GSPro's shot count matches ConnectDebug's, and none of the dated files
  is missing.

## 8. Phasing

| Phase | Scope | Exit |
|---|---|---|
| 1 | Parser, tail, monitor, `Kind`, settings, the §5 pre-arm slot, the replay harness, tests §7.1–7.3 and §7.5–7.6 | The replayed day produces the same readings as the corpus sweep. Tests pass |
| 2 | Managed-log join (§6): shot number, contact time, contact tolerance in pairing | Live acceptance (§7.7), including latency |
| 3 | Sign verification (§7.4). Update the reference doc and `BUILDING.md`'s launch-monitor section | Signs recorded, and the "unverified" note removed or kept on evidence |

## 9. Definition of done

- The connector can be selected in Settings → Devices → Launch monitor. With GSPro running, a
  swing gets its ball data about half a second after contact.
- No shot is lost at the daily rollover, and GSPconnect's rollover is never blocked.
- An early reading is paired with its swing rather than discarded.
- The spin-invalid shot records no spin rather than zero spin.
- The reference doc's "GSPro workflow" section points here. `gspro_integration_brief.md` §1
  ("the GCQuad connector is not superseded") is updated to name both GC Quad routes and when to
  use each: GSPro for play and ball data, FSX2020 for club validation.

## 10. Open decisions

1. **Kind naming.** `gcquad-gspro` as a separate Kind, as proposed, or a "host" option under
   one GC Quad Kind? Separate is simpler for settings and provenance. One Kind is closer to
   "it's the same device".
2. **Poll interval.** 50 ms as proposed, or a directory change notification on the local
   folder with polling as a backstop? Polling is simpler and its added latency is already
   under 50 ms.
3. **Phase 2 in phase 1?** The managed-log join is what makes the pre-arm pairing safe rather
   than merely likely. Its cost is a second tail with the same code.
4. **Grace window.** Is 1500 ms right for sessions where the only detector is pose? Measure
   the commit latency of each detector before fixing the constant.
