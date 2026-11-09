# GC Quad under GSPro v4 — reading the shot record GSPro keeps

**Audience**: whoever runs the investigation session (§6), and whoever builds the connector afterwards
**Code**: none yet. Would sit beside `src/LaunchMonitor/premier_monitor.{h,cpp}`
**Source**: direct inspection of the capture PC on 2026-11-09, `liver` account, GSPro closed, working on copies of the files. Latest play on them was 2026-11-07
**Status**: 🔍 **INVESTIGATING.** §4 is provisional until §6 has run. Nothing is built
**Use**: personal use for now. See §1.1 before anything ships
**Written**: 2026-11-09

Related: [`gspro_gcquad_connector.md`](gspro_gcquad_connector.md) (the ball-only `ConnectDebug.txt` connector, declined 2026-09-26), [`foresight_premier_connector.md`](foresight_premier_connector.md) (the template for this work), [`foresight_gcquad_data_sources.md`](../reference/foresight_gcquad_data_sources.md).

---

## 1. Why

**The goal: play golf in GSPro and do nothing else.** PinPoint watches, captures each swing, and attaches the launch monitor numbers and the club that was hit. A round has jeopardy on every shot, which range practice does not.

PinPoint already captures swings under GSPro, but those swings have no launch monitor data. The September decision accepted that, because the only readable source was `ConnectDebug.txt`, which is ball-only. It named what would reopen the question: a route to **club** data. GSPro's own database may be that route.

### 1.1 Scope and limits

**Personal use for now. Talk to GSPro before this goes into any release.** That conversation must happen before v1.0.

Limits for this work, whatever the answer:

- **Never decrypt** `Round.RoundData` or `Round.RoundSettings`.
- **Never decompile** GSPro or GSPconnect. Everything here comes from observing files.
- **Never touch the private GSPconnect↔GSPro channel on TCP 9050.** It stays excluded, as in the reference doc.
- **Never disturb the game** (§4.3).

## 2. What GSPro leaves on disk

Version on 2026-11-09: `public-beta-v.4.1.2` (`C:\GSProV1\GSProV1.dat`), GSPconnect `public-beta-connect-v3.2.53` (`GSProV1Connect.dat`). ⚠ The running game is in `C:\GSProV1\Core\GSProPublicBeta`. `Core\GSP` is the older build, and its `GSPro.exe` is dated 2025-09-04.

Data root (Unity `persistentDataPath`, per user): `C:\Users\<user>\AppData\LocalLow\GSPro\GSPro\`

| File | Content | Club metrics | Clock | Notes |
|---|---|---|---|---|
| `GSPro.db` → `LastShot` | One row: the latest shot on the course, as JSON | fields exist; **never yet seen filled** | local, 1 s | §2.1. **The candidate live source** |
| `GSPro.db` → `DrivingRangeShot` | One row per range shot, as JSON | **yes, seen filled** | local, 1 s | §2.1. Rows are gone once you leave the range |
| `currentRound.dat` | Plain JSON, one record per stroke of the round | no, `ClubIndex` only | none per shot | §2.2 |
| `C:\GSProV1\Core\GSPC\ConnectDebug.txt` | Ball data, three lines per shot | no | local, ms | §2.3. About 0.5 s after contact |
| `Player.log`, `Player-prev.log` | Unity log | no | — | No shot values worth using |
| `Round.RoundData`, `RoundSettings` | Encrypted | — | — | Out of bounds (§1.1) |
| `Settings.vgs` | .NET binary serialisation | — | — | Not needed |

`PlayerBag` is empty on this PC.

### 2.1 `ShotData`: the two tables

`LastShot` and `DrivingRangeShot` have the same schema: `ID integer primary key autoincrement, DateCreated datetime, ShotData varchar`. A real range row, 2026-10-27 16:55:35:

```json
{"Carry":103.371918,"TotalDistance":109.788307,"BallSpeed":84.8549957,"BackSpin":8349.0,
 "SideSpin":43.0,"HLA":-1.9915,"VLA":30.7330971,"Decent":51.60407,"PeakHeight":82.76796,
 "Offline":-3.37440515,"rawSpinAxis":0.295088828,"rawCarryGame":103.4397,"rawCarryLM":0.0,
 "DistanceToPin":291.681061,"club":"GW","ClubSpeed":75.9207153,"Path":-2.11854172,
 "AoA":-3.749028,"FaceToTarget":-3.66667724,"FaceToPath":1.54813552,"Lie":0.534377635,
 "Loft":45.64891,"DynamicLoft":49.42592,"CR":3181.43652,"HI":4.912209,"VI":-9.14646,
 "SmashFactor":1.11767912}
```

What the data shows, and how strong each claim is:

| Claim | Evidence | Strength |
|---|---|---|
| Ball values are the Quad's | `LastShot` matches `ConnectDebug.txt` to every printed digit (2026-11-07) | Strong, one shot |
| `ClubSpeed` is in mph | `SmashFactor` = `BallSpeed` / `ClubSpeed` (84.855 / 75.921 = 1.1177) | Strong |
| Club data reaches the range table | 14 stale `DrivingRangeShot` rows recovered from free pages, IDs 21065–21459, all GW | Solid, but **one club, before 4.1.2** (27 Oct, 6 Nov) |
| Club data reaches `LastShot` | ⚠ The only row seen is an 11 mph LW chip with **every club field 0.0** | **Unknown. This is the main question for §6** |
| 4.1.2 still writes club data | No row with club data was written by this build | **Unknown** |
| Written about when the ball stops | Range: 7.2–8.8 s after the `ConnectDebug` line (14 shots). Chip: 3.5 s. 1 s resolution | Weak, inferred |

Things to design around:

- **Missing club data is `0.0`, not `null`.** That is how the chip looks, and one range row has `ClubSpeed`, `Path`, `AoA` and `SmashFactor` set with the other eight fields `0.0`. That is the same split as a one-sticker club in Premier (§2.6.2 there). GW has a full sticker set, so the stickers are the likely reason the LW chip was empty. A genuine `0.0` cannot be told from "not measured" (§4.4).
- **`FaceToPath` has the opposite sign to ours.** It is `Path − FaceToTarget` (−2.119 − (−3.667) = +1.548). PinPoint's `faceToPath` is face − path, so compute it rather than copy it.
- **`rawCarryLM` was 0.0** on every row. `Carry` is GSPro's own flight model, the numbers the game played.
- **The sequence counters are large:** `LastShot` 9,243 and `DrivingRangeShot` 21,470. These tables predate 4.1, so the source is probably not new, we just never looked. It also suggests each shot inserts a new row, so `ID` can identify a shot.

### 2.2 `currentRound.dat`

Plain JSON array, 2.5 MB after 17 holes. Each record has `ShotID` (GUID), `RoundID`, `Hole`, `HoleShot`, `HolePar`, `GlobalShotNumber`, `ClubIndex`, `BallSpeed`, `DistanceToPin`, `TotalDistance`, `StartingSurface`, `EndingSurface`, `CourseKey`, `ShotResult`, `HasSyncedOnline`, and the ball's full trajectory (`GhostData`, `activeShot`). `GhostData` also repeats the ball launch values (`lmfSP`, `lmfEL`, …). There are no club metrics and no per-shot wall-clock time.

The 2026-11-07 round has 88 records and 57 `Sending Shot` lines:

- **45 records**: ordinary strokes.
- **11 records with `ShotResult = 1`**: retakes. They share a `GlobalShotNumber` with the stroke they replay but have their own ball speed. **Each is a real swing**, so a shot cannot be identified by `GlobalShotNumber`.
- **32 records with `ClubIndex` 26**, about two per hole: they copy the previous record's ball speed and have **no launch monitor shot behind them**. Probably the putter, and possibly auto-putts or gimmes (§6 Block 5).
- `HasSyncedOnline` is false on all 88. No upload queue or portal file was found, so how the portal is fed remains unknown.
- 57 sends against 56 non-putter records. One difference is not explained.

### 2.3 `ConnectDebug.txt`

Unchanged in GSPconnect 3.2.53: the same three lines per shot as documented in [`gspro_gcquad_connector.md`](gspro_gcquad_connector.md) §2, written within 2 ms of the spin message, median 536 ms after contact. Its value here is the **time**: it is the only source that says when a shot happened to better than a second.

## 3. Comparison

|  | `ConnectDebug.txt` alone (declined) | `GSPro.db` (this doc) | Premier snapshot |
|---|---|---|---|
| Club metrics | none | **yes on the range; course unproven** | yes |
| Club name | none | **yes (`club`)** | yes |
| Carry, total, offline, peak, descent | none | **yes** | carry only |
| Available | ~0.5 s | ~4–9 s, likely flight-dependent | ~0.7 s, club a shot later |
| Time per shot | ms | 1 s, and late | UTC, 100 ns |
| Format | log | internal table, undocumented | internal file, undocumented |

## 4. Proposed design (provisional)

### 4.1 Shape

A new `LaunchMonitorBase` subclass, `GcQuadGsProMonitor`, `Kind::GcQuadGsPro`, token `"gcquad-gspro"`, label "GC Quad — GSPro", short label "GC Quad". It is **not** `Kind::GsPro`, which is the Open Connect server where PinPoint plays the part of GSPro. Same run-under-`liver` rule as Premier: the default folder is this account's `LocalLow\GSPro\GSPro`.

**One reading per shot, emitted when the row lands.** Premier needed a ball-first, club-later pair because its club data came a shot late. Here the row is complete when written, so a single `readingAvailable` is enough. The cost is that a reading arrives several seconds after contact. That does not matter for collection: analysis runs for 12–37 s, and `ShotPairing` already parks an early reading until the swing reaches disk.

Per poll, every 250 ms:

1. Read the 100-byte SQLite header. Stop if the file change counter (offset 24) has not moved.
2. Read the newest row of `LastShot` and of `DrivingRangeShot`. Stop if neither `ID` is new.
3. Parse `ShotData`. Apply the ghost gate (10 mph, as Premier), but see §6 Block 7: GSPro may filter first.
4. Find the matching `ConnectDebug.txt` line (§4.5) and set `contactAtMs`.
5. Emit. `deviceShotId` is `gspro-course-<ID>` or `gspro-range-<ID>`.

On `start()`, prime both IDs without emitting (the base-class rule). A schema mismatch goes to `State::Error` with a plain message, because beta builds will change this.

### 4.2 Why not the other files

`currentRound.dat` has no club metrics and exists only on the course. It is the natural later source for hole, par, lie and course, but the reading has no place for those today, so it stays out of phase 1.

### 4.3 Reading a live database without disturbing the game

**This is a hard requirement.** `GSPro.db` is in rollback-journal mode (header bytes 18–19 are 1, 1). An ordinary SQLite reader takes a shared lock, and a game write that meets it can fail with `SQLITE_BUSY`. Premier's upload failed in exactly this way when a watcher was too eager.

| Option | Lock taken | New dependency | Risk |
|---|---|---|---|
| **A. Read pages directly** with full share flags, and parse just these tables | **none** | none | A write can be caught half-done. Guard by re-reading the change counter and retrying. Needs a small, tested B-tree reader |
| B. SQLite read-only (`Qt6::Sql` or the amalgamation) | shared, for ms | yes; PinPoint links no SQLite today | Could block a game write, however rarely |
| C. Copy the 40 MB file per shot, then query it | none | as B | Heavy, and the copy can be torn |

**Provisional choice: A**, because it cannot affect the game by construction. §6 Block 12 tests B deliberately, at the end of the session. If B is shown to be harmless, it is less code to own.

Rules whichever option wins: no handle held between polls, no read at all while the change counter is still, and any failed read means "try next tick", never an error.

### 4.4 Field mapping to `LaunchMonitorReading`

Signs and several units are **unconfirmed** until §6 Block 8.

| `ShotData` | Reading field | Conversion | Status |
|---|---|---|---|
| `BallSpeed` | `ballSpeed` | mph | Confirmed |
| `VLA` / `HLA` | `launchAngle` / `launchDirection` | ° | Sign of HLA to check |
| `BackSpin` / `SideSpin` | `backSpin` / `sideSpin` | rpm | Sign to check |
| `rawSpinAxis` | `spinAxis` | ° | Sign to check |
| `sqrt(back² + side²)` | `spinRate` | rpm, derived: `ShotData` has no total | Matches `ConnectDebug` index 4 on the one shot compared (1771.07) |
| `Carry` / `TotalDistance` | `carryDistance` / `totalDistance` | yd assumed. Does it follow GSPro's unit setting? | To check |
| `Offline` | `offline` | yd assumed | Sign to check |
| `PeakHeight` | `peakHeight` | ft assumed (82.8 for a 103-yd wedge) | To check |
| `Decent` [sic] | `descentAngle` | ° | Likely |
| `club` | `deviceClub` | as written | §4.6 |
| `ClubSpeed` | `clubheadSpeed` | mph | Confirmed |
| `SmashFactor` | `smashFactor` | ratio | Confirmed |
| `AoA` | `attackAngle` | ° | Sign to check |
| `Path` | `clubPath` | ° | Sign to check |
| `FaceToTarget` | `faceAngle` | ° | Sign to check |
| derived | `faceToPath` | **`FaceToTarget − Path`**. Do not copy `FaceToPath`, its sign is reversed | Confirmed from data |
| `DynamicLoft` | `dynamicLoft` | ° | Check against the screen. `Loft` is unmapped |
| `Lie` | `lieAngle` | ° | Check |
| `CR` | `closureRate` | °/s assumed | Check |
| `HI` / `VI` | `strikeLocation` / `strikeHeight` | mm assumed. Sign of `HI` must be measured (Premier needed negating) | To check |

**Zero means not measured for club fields.** `ClubSpeed == 0` means no club data at all, so every club field becomes a `noData` drop. With `ClubSpeed` set, any of the other zero fields becomes a `noData` drop when all eight of the face, loft, lie and impact group are zero together (the one-sticker pattern). A real `0.0` in just one of them is kept. The same care applies to spin, as `ConnectDebug` already showed.

### 4.5 Pairing

`ShotPairing` works as it stands. The reading has `contactAtMs`, so it gets the timestamp check:

- Scan the last 16 KB of `ConnectDebug.txt` for the line whose first number equals `BallSpeed` (float32 printed round-trip, so the match is exact) and whose time is before `DateCreated`.
- `contactAtMs` = line time − 540 ms, the median spin latency. p99 is about +600 ms from that, inside the ±1.5 s window.
- One short open-read-close per **shot**, so none of the rollover logic designed for the tail in the declined doc is needed. A shot across midnight loses only its time check.
- No line found, for example a different launch monitor: no `contactAtMs`, and pairing falls back to order.

A dropped ball can't jam this. The reading describes the swing that was armed, or is rejected by time.

### 4.6 The club

**The `club` field names what GSPro thinks is being hit, and since the game depends on it, it should be right.** Follow it by default, unlike Premier where following is opt-in.

- Seen: `GW`, `LW`. libgspro's Open Connect codes are `DR`, `I7`, `PT` and so on, so `I7` is the likely iron form. `premierClubToVocabulary()` handles `GW` and `LW` but not `I7`, `W3` or `H4`, so this needs a sibling, `gsProClubToVocabulary()`, in a new header. §6 Block 3 records every code from your bag.
- **The club is known only after the shot.** Analysis starts about 1.25 s after detection, and the reading arrives much later. The reuse of Premier §4.8's "when the analysis won the race" path is the right mechanism: replace `capture.club`, re-analyse. But on a course the club changes almost every shot, so this path becomes the **normal** one. Block 11 measures whether that is acceptable while GSPro is rendering.
- A cheaper route would be a file that changes when you pick a club, before the swing. Block 3b looks for one. Nothing suggests there is one.
- A club PinPoint has no name for leaves the club alone, logged once, as for Premier.
- A putt with no launch monitor shot gets no reading and no club. §7 Q5.

### 4.7 "Just play": what hands-off needs

| Need | Today | Work |
|---|---|---|
| Captures with no input during a round | Session must be started in PinPoint | Phase 1: start a session once. Phase 2: auto-start from a new round (Block 9 provides the signal) |
| Readings attach with no input | §4.1–4.5 | This connector |
| Club follows the game | Premier path exists | §4.6 |
| Shot the mic missed still recorded | Standalone shots exist (`standalone_gate`) | Confirm they work with this connector's later reading |
| PinPoint stays out of the way | Needs checking | No focus theft, no sounds, no replay over the game. Check in Block 11 |
| Game unaffected | §4.3 | Block 12 and Block 11 |

### 4.8 Tests

As for Premier: a pure parser with `*_test.cpp`, fixtures cut from §6 captures and sanitised (`UserGuid` and `PlayerName` removed). Cases: full, one-sticker and no-sticker rows; a chip; a retake; a torn read; a change counter that moves with no new row; a changed schema; `ConnectDebug` line missing. A monitor test drives `pollNow()` over captured database versions.

### 4.9 Risks

- **The GSPro conversation** (§1.1) has not happened yet.
- **Beta builds change.** A format break should look like a clear error, not a quiet loss of data.
- **Course club data may simply not be there.** Then this comes down to range sessions only, and the plan needs rethinking.
- **Full re-analysis per shot** on a PC also running the game (§4.6).
- **Stickers.** Clubs without them give ball data only, as with every other Quad path. A round will be mixed.
- **Only proven on a GC Quad.** `ShotData` is probably the same for other monitors, but the sensor decides which fields are filled.

## 5. Phases

1. **Investigate** (§6). Go or no-go.
2. **Parser and tests.** Pure, no I/O.
3. **Monitor**, `Kind`, factory, settings choice.
4. **Club follow** and the mapping header.
5. **Cabin test**: a real round, cameras on.
6. **Auto-start of the session** from GSPro's round signal, if Block 9 supports it.
7. **Before v1.0: the GSPro conversation** (§1.1). No answer, no release of this connector.

## 6. Investigation session — you play, I watch

**Goal**: answer every open question in §2–§4 from live observation, and keep every version of every file as fixtures. About **70 balls plus 4–5 holes, 75–90 min**. GSPro runs in the `liver` account as you normally play. I run a watcher from the developer account. PinPoint is not needed until Block 11.

### Before the session (me)

A watcher script in my scratchpad, not in the repo, tested first against a closed GSPro and then an idle one. **It must be unable to disturb the game:**

- **Never opens `GSPro.db` through SQLite** (until Block 12). It opens the file with full share flags, reads the 100-byte header every **100 ms**, and only when the change counter moves reads the whole file, extracts every `ShotData` row with its `ID`, and logs the difference. Torn reads are retried. A copy is kept per change for fixtures.
- Stats `currentRound.dat`, and copies each new version, **stat first, open only on change**.
- Tails `ConnectDebug.txt`, `Player.log` and `FSS_SDK_MANAGED_LOG.txt` by open-seek-read-close, never holding a handle, so log4net's rollover is safe.
- Stats everything else under the data root, for files we haven't thought of (`-journal`, new queue or portal files).
- Excludes `gsp.lic`, and doesn't read `Round.RoundData` or `RoundSettings`.

The result is one merged timeline with high-resolution times, plus a numbered snapshot folder. I report back after each block. You don't need to read any of it.

**Also before you start, please note:** which clubs have full stickers, one sticker, or none; how you normally putt in GSPro (Quad, auto-putt, or gimmes); and whether there is a data-sharing or portal setting in 4.1.

### Blocks

Leave about 20 s between shots unless the block says otherwise. Call out anything odd as it happens, and I'll mark the time.

| # | What you do | What it answers |
|---|---|---|
| 0 | Start GSPro, sit idle in the menu for 1 min, then open the range and hit nothing for 1 min | What is written at startup and idle; how often the change counter moves when nothing is happening |
| 1 | **Range: 5 shots with the GW** (full stickers) | The control: does 4.1.2 still write club data to `DrivingRangeShot`? Exact write time against the `ConnectDebug` line. Is `LastShot` written on the range too? |
| 2 | **Range: 3 shots** one-sticker club, then **3 shots** no-sticker club | How partial and absent club data look in 4.1.2; confirms the zero rule (§4.4) |
| 3 | **Range: 1 shot with every club in the bag**, driver to LW, setting the club in GSPro first | The `club` code for each club, for the mapping. Latency against flight time (driver versus wedge) |
| 3b | **Change club 4 times without hitting** | Does anything on disk change when a club is picked? |
| 4 | **Course: play 3 holes as normal, and hit at least two full shots with the GW** | **The main test.** Is `LastShot` written for every shot? Does it have club data on the course? Timing on the course. `currentRound.dat` writes |
| 5 | Keep playing and deliberately get: a **retake** (OB, water, or a mulligan), a **chip**, a **gimme**, and **real putts** the way you normally putt | What `ShotResult = 1` means; whether retakes and putts write a row; what `ClubIndex` 26 is |
| 6 | **Range: 4 shots quickly**, as soon as GSPro will accept the next one | Are rows lost when shots overlap with the write? Does `DateCreated` stay in order? |
| 7 | One **practice swing** with no ball; a **knocked ball** at setup; a **top** | What GSPro filters, whether a row appears; sets the ghost gate |
| 8 | **Sign shots with the GW** on the range: a clear push, pull, fade and draw, then a toe and a heel strike (impact spray helps). Tell me what the screen says | Sign of `HLA`, `SideSpin`, `rawSpinAxis`, `Offline`, `Path`, `FaceToTarget`, `HI`. Units of carry, peak height and `CR` against the screen |
| 9 | **Start a new round, play one hole, finish or abandon it**, and then open another; quit GSPro mid-round; reopen | The signal for a round starting and ending (for auto-start). Does `LastShot` survive? Does `currentRound.dat`? `HasSyncedOnline` after a finished round: the portal question. `Player.log` rotation |
| 10 | Switch GSPro's distance units once, **hit 2 range shots**, and switch back | Does `ShotData` follow the display setting? |
| 11 | Start **PinPoint with cameras and the LM set to none**, play **1–2 holes**, as you would for real | PinPoint impact time against the `ConnectDebug` line (the pairing offset). Does anything steal focus or make noise? Does the game's frame rate suffer during analysis? How long does analysis take beside the game? |
| 12 | Last block, optional: I switch the watcher to a **real read-only SQLite query at 1 Hz** and you hit **5 shots** | Whether design option B is safe. Watch for any error from GSPro. Done last so that a problem costs nothing |

### After the session (me)

- Fill §2 with what was measured, mark each §4.4 row confirmed or corrected, and resolve §4.3.
- Add a section to `foresight_gcquad_data_sources.md`.
- Keep the snapshots as fixtures for §4.8, sanitised before anything is committed.
- Bring back a **go/no-go** before writing code. **No-go if `LastShot` has no club data on the course and nothing else does**, since ball-only under GSPro is the case already declined.

## 7. Open questions

1. Does `LastShot` carry club data for a full, stickered shot on the course? (Block 4)
2. Is `LastShot` written for every launch monitor shot, including retakes? (Blocks 4, 5)
3. When exactly is the row written: ball stopped, or a fixed time after contact? (Blocks 1, 3, 4)
4. Is the file change counter a reliable per-shot trigger? (Blocks 0, 1)
5. What is `ClubIndex` 26, and do putts ever produce a reading? If not, what should PinPoint do with a captured putt: no club, or `PUTTER` inferred from a later `currentRound.dat`? (Block 5)
6. Is `club` always one of the Open Connect codes? (Block 3)
7. Can a club selection be seen before the swing? (Block 3b)
8. Do `ShotData` values follow the display units? (Block 10)
9. How is the portal fed, and does `HasSyncedOnline` ever change? (Block 9)
10. What does a full re-analysis per shot cost beside the game? (Block 11)
11. Can PinPoint read the database through SQLite safely? (Block 12)
12. What does GSPro say about this use? (§1.1, your conversation, before v1.0)
