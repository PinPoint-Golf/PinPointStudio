# Swing document schema reference (`swing.ppsw`, legacy `swing.json`)

> **Date:** 2026-11-10 · **Applies to:** the per-shot swing document in each swing directory — `swing.ppsw` (written since 2026-09-23), or a legacy `swing.json` (read, never written) for a swing not yet converted · **Schema:** top-level `pinpoint.swing/2`, embedded `analysis` block `pinpoint.analysis/3`, embedded `summary` block `pinpoint.swingsummary/4`

Every captured shot produces one directory (`<session>/swing_<NNNN>/`) holding the media and one swing document. The document is the same JSON-model tree whichever file holds it: `swing.ppsw` is a binary container for it, and `swing.json` is the old text form. This page documents the tree field by field; the section "On disk" says how the two files relate and how to read either. Snippets are from a real swing — `2026-07-05_Mark-Liversedge_Wrist_02/swing_0006` (a camera-only Wrist shot; "s06" in the shaft-lab corpus) — with IMU-specific blocks taken from an IMU swing, and the down-the-line and 3-D blocks from a two-camera swing, where noted. The snippets are shown as JSON because that is what every reader hands back (`ppsw dump`, `ppswing.load_swing()`, `SwingStore::load()`).

## Source of truth

| Concern | File |
|---|---|
| The one door to a swing's document on disk: which file exists, load, save, convert | `src/Export/swing_store.{h,cpp}` (`pinpoint::SwingStore`) |
| Qt JSON ↔ libppswing tree (integers stay integers; ndarrays and tables expand to plain arrays) | `src/Export/ppsw_qt.{h,cpp}` |
| The binary format itself | libppswing (sister repo, MIT): `docs/specification/ppsw-format.md`; built into the app by the top-level `CMakeLists.txt` |
| Raw manifest — `clock`, `window`, `swing`, `athlete`, `session`, `thumbnail`, `capture`, `streams` | `src/Export/swing_exporter.cpp` (`SwingExporter` export, `captureBlock`, stream serialisation). Returned to the GUI thread, not written. |
| Header for a shot whose export failed but whose analysis worked | `src/Gui/shot/shot_processor.cpp` `buildSynthManifest()` |
| `imuIntegrity`, `captureIntegrity` | `src/Export/swing_doc.cpp` `imuIntegrityJson`, `captureIntegrityJson`; set by `shot_processor.cpp` (live) and `applyImuIntegrity` / `applyCaptureIntegrity` (re-analysis, `src/Gui/shot/reanalysis_controller.cpp`) |
| The one write: stamps the schema, seeds `review.club`, replaces `analysis`, carries the `lm.` rows | `swing_doc.cpp` `SwingDocWriter::writeSwingJson()` |
| Every write ends in `saveDocument()`, which rebuilds `summary` and calls `SwingStore::save()` | `SwingDocWriter::writeSwingJson` (end of shot, re-analysis, PPCP clip with no document), `updateReview`, `updateShotAnnotations`, `updateLaunchMonitor`, `updateStreamOrigin`, `writeDeviceOnlySwing` — all `swing_doc.cpp` |
| `analysis` block | `swing_doc.cpp` `serializeAnalysis()`, plus the write-time additions in `writeSwingJson()` |
| `summary` block | `swing_doc.cpp` `summaryFromRoot()` → `summaryJson()`, called by `saveDocument()` |
| `review` | `swing_doc.cpp` `updateReview()`, `updateShotAnnotations()`; seeded by `writeSwingJson()` |
| `launchMonitor` (and a follow-club rewrite of `capture.club`) | `swing_doc.cpp` `updateLaunchMonitor()`, `lmRawBlock()`; field table `src/LaunchMonitor/launch_monitor_reading.cpp` |
| Device-only document | `swing_doc.cpp` `writeDeviceOnlySwing()` |
| Phone clip `streams[]` element (`origin`) | `swing_doc.cpp` `updateStreamOrigin()`; caller `src/Gui/shot/ppcp_clip_filer.cpp` |
| `archive` block (stubbed swings) | `src/Export/session_archiver.cpp` `stubOf()` |
| JSON-era → `.ppsw` conversion | `SwingStore::convertDir` via `SwingDocWriter::convertToPpsw`; in-app `src/Gui/settings/library_converter.cpp`, CLI `tools/storage/pps_convert_library.cpp` |
| Readers | `SwingDocReader::readSwingJson` / `readSwingSummary` / `readLaunchMonitorDetail` (`swing_doc.cpp`), `SwingDiskLoader::load` (`src/Analysis/swing_reanalyzer.cpp`), `disk_replay_source.cpp`, `swing_data_source.cpp`; the charts and report read through `SwingStore::loadAnalysis`, the 3-D panel through `SwingStore::loadParts` (`swing_rig_driver.cpp`) |
| Enums (Phase, SegmentRole, ReconstructionTier, ShaftSampleFlags) | `src/Analysis/swing_analysis.h` |

> The writer is still called `writeSwingJson` and the reader `readSwingJson`. The names predate the switch; both go through `SwingStore` and only `swing.ppsw` is written.

**How it is written.** The export worker builds the *raw manifest* (`SwingExporter`) and writes the media; it does not write the document. On the GUI side the integrity checks add `imuIntegrity` / `captureIntegrity`, then `SwingDocWriter::writeSwingJson()` sets `schema` to `pinpoint.swing/2`, seeds `review.club`, merges in the analyzer's output as `analysis`, and hands the tree to `saveDocument()`. That drops any old `summary`, builds a new one from the tree being written, and calls `SwingStore::save()`. `save()` writes `swing.ppsw` and then deletes any `swing.json` and `swing_summary.json` in the same directory, so the first rewrite of a JSON-era swing converts it.

**Re-analysis** loads the whole document and passes it back as the manifest (`reanalysis_controller.cpp`). It replaces `analysis`, `imuIntegrity` and `captureIntegrity`, and keeps everything else — `capture`, `streams`, `clock`, `window`, `review`, `launchMonitor` — as it was. It carries the `lm.` metric rows across and derives the launch-monitor-derived rows again from the raw `launchMonitor` block. The review, annotation, launch-monitor and stream-origin updates each load the document, change their own block and write the whole thing back atomically in the same way. With **Storage → Save pose keypoints** off, `writeSwingJson(…, savePose=false)` leaves out `analysis.pose2d` and `analysis.poseDtl` and their version stamps.

**Additive by contract.** Readers ignore unknown keys, and every `analysis` sub-block is optional — a swing captured with analysis skipped (corpus capture) has no `analysis` object at all; a camera-only swing has no IMU streams and no `bindings`; an older file may lack newer blocks. Consumers must tolerate absence.

## On disk: `swing.ppsw`, and the legacy `swing.json`

A swing directory holds its document in one of two files (`swing_store.h`):

| File | Status |
|---|---|
| `swing.ppsw` | The binary format (libppswing). The only document the app has written since 2026-09-23 (commit `4d8ba169`). |
| `swing.json` | The old text format. Read for as long as one exists; never written into a swing directory again. |

**Which one wins.** If `swing.ppsw` exists, it is the document; `swing.json` is read only when there is no `swing.ppsw` (`SwingStore::info`). The Python reader follows the same rule (`libppswing/python/ppswing/reader.py`, `tools/pp_swingdoc.py`). A directory holding both is a conversion that did not finish. `convertDir` treats the JSON as the original there: it starts again from the JSON and overwrites the `.ppsw`.

**The same document in both.** `swing.ppsw` stores the same tree `swing.json` would hold: the same blocks, keys and `schema` ids. The format knows nothing about the swing schema (spec §1). Conversion is checked for semantic equality before the JSON is deleted. Integers stay integers and doubles come back bit-exact (spec §1.1; `ppsw_qt.cpp`). What differs:

- **`summary`.** A `swing.ppsw` written by the app has an extra top-level `summary` block (see "`summary`" below). A `swing.json` never has one; its equivalent is the `swing_summary.json` sidecar.
- **`archive`.** A stub left by archiving has an extra top-level `archive` block (see "`archive`" below). Only `swing.ppsw` stubs exist.
- **Int vs double in mixed arrays.** A numeric array that mixes integers and doubles reads back as all doubles (spec §5.2 rule N3). Qt writes a whole double such as `37.0` as `37`, so a real-valued column usually is such a mix. Python therefore sees `37.0` where `json.load` saw `37`. The values are equal; a reader should not branch on the type.

**How it is stored.** A reader using the libraries does not need this, but it explains the lazy reads (design notes: `docs/implementation/swing_storage_impl.md`):

- A 40-byte header, then zstd-compressed CBOR chunks, then a table of contents. Header, TOC and each chunk carry a CRC (spec §2).
- Any subtree whose encoding is over 64 KiB goes into its own chunk, named by its JSON Pointer, and its parent holds a ref (`include/ppswing/ppswing.h`; spec §2.3). On a typical Wrist swing the chunks are the root, `/analysis/club`, `/analysis/metrics`, `/analysis/pose2d/frames`, `/analysis/pose2d/smoothed` and `/analysis/pose2d/synth` (`ppsw info` on `2026-10-20_Mark-Liversedge_Wrist_01/swing_0001`: 1.39 MB on disk, 4.78 MB decoded). Small swings fit in the root chunk.
- Numeric arrays become typed ndarrays in the narrowest exact dtype, byte-shuffled; integer time bases are delta-coded. Arrays of objects become column tables (spec §5.1–§5.3). Each reader expands both back to plain arrays and objects (`ppsw_qt.cpp`; Python `to_plain`).
- Writes are lossless (no precision hints) and atomic: a temporary file, then a rename (spec §2.5).

So the root chunk alone (session, athlete, capture, review, launch monitor, `summary`) can be read without decompressing any pose. `SwingStore::loadSummaryBlock`, `loadAnalysis` (root + `/analysis/metrics`, with the per-frame tracks removed) and `loadParts` rely on this. On a `swing.json`, `loadAnalysis` and `loadParts` parse the whole file and return the same shape.

### Reading a swing outside the app

libppswing is the sister repository `github.com/PinPoint-Golf/libppswing` (public; MIT licence, `LICENSE`; the vendored zstd is BSD-3-Clause). The build uses a checkout at `../libppswing` beside PinPointStudio if there is one, and fetches `main` from GitHub otherwise. The app build does not include the CLI (`PPSW_BUILD_TOOLS OFF`).

| What | Where in libppswing |
|---|---|
| Format specification (1.0, draft) | `docs/specification/ppsw-format.md` |
| Design notes | `docs/design.md` |
| C++17 library | `include/ppswing/`, `src/` |
| CLI `ppsw` | `tools/ppsw.cpp` |
| Pure-Python reader (package `ppswing`, Python ≥ 3.10; stdlib zstd on 3.14+, else `pip install zstandard`; numpy optional) | `python/ppswing/reader.py`, `python/pyproject.toml` |

**Command line.** Build libppswing on its own (the CLI is built only when libppswing is the top-level project):

    cmake -S libppswing -B libppswing/build -DCMAKE_BUILD_TYPE=Release
    cmake --build libppswing/build -j8

    libppswing/build/ppsw dump   swing_0001/swing.ppsw --indent > swing.json   # the whole document as JSON
    libppswing/build/ppsw info   swing_0001/swing.ppsw                         # header + chunk table
    libppswing/build/ppsw verify swing.json swing.ppsw                          # lossless check; exit 1 on a difference
    libppswing/build/ppsw convert swing.json -o swing.ppsw                      # never overwrites its input

The usage text is at the top of `tools/ppsw.cpp`.

**Python.**

    import sys; sys.path.insert(0, "libppswing/python")     # or: pip install libppswing/python
    import ppswing
    doc = ppswing.load_swing("…/swing_0001")    # a swing dir, a swing.ppsw or a swing.json; .ppsw wins
    r = ppswing.Reader("…/swing_0001/swing.ppsw")
    root = r.root()                              # root chunk only; split subtrees come back as ppswing.Ref
    metrics = ppswing.to_plain(r.chunk("/analysis/metrics"))

`load_swing` returns plain dicts and lists, the same shape `json.load` gives. `Reader.root()`/`chunk()` leave refs unresolved and arrays typed. `Reader.load()` resolves everything. Inside this repository, tools import `tools/pp_swingdoc.py`. It finds the sibling checkout, re-exports `load_swing`, `has_swing`, `document_path` and `find_swing_dirs`, and maps a `<dir>/swing.json` path to the `swing.ppsw` next to it.

**Zip export.** The session zip export still contains a `swing.json`, built from the document: `SwingStore::load` → `SwingStore::toJsonText` (compact), in `swing_zip_exporter.cpp`. That JSON includes the document's `summary` block. A JSON-era swing's own `swing.json` is copied as it is. `swing.ppsw` itself is not in the zip.

**Nothing writes `swing.json` into the library.** No app code writes it: every write goes through `SwingStore::save` (`swing_doc.cpp`, `session_archiver.cpp`) or `SwingStore::writeFile` to a path outside a swing directory (`tools/swinglab/src/raw_reencode.cpp`). The only `swing.json` text that is still produced: the zip member above; `swinglab_run --out`'s `result.json`, a swing-shaped document with `summary` removed (`tools/swinglab/src/swinglab_run.cpp`); and some Python research and test tools that write `swing.json` into their own output or temp folders (`tools/diagnostics/diag_uncertainty_prep.py`, `tools/trunk/trunk_hw.py`, `tools/swinglab/montage_*.py`).

## Timestamp domains — read this first

All `t_us` values are **microseconds, window-relative** (0-based; `window.start_us` is `0`), matching `capture.impactUs`, the stream frame times, and the window bounds. The **one** absolute value is `clock.t0_us` — the EventBuffer clock instant of the window start. To recover an absolute timestamp: `absolute = clock.t0_us + t_us`.

> **Legacy caveat.** `analysis` timestamps written before 2026-07-07 may be **absolute** (a live capture wrote the EventBuffer clock domain directly; re-analysis always wrote relative). The writer now normalises all `analysis` `t_us` to window-relative regardless of source, and the review readers are domain-aware (`t >= t0 ? t - t0 : t`) so both old and new files render. New files are uniformly window-relative.

Exceptions, each described in its own section:

- **Device-only swings** have `clock.t0_us = 0` and a zero-length window: there is no buffer.
- **Phone clips** (`frames.clock: "clip"`) count `frames.t_us` from the clip's own first frame, not from the window start.
- **HackMotion `device.history`** intervals (`delivered`, `gaps`, `selfRecordingGap`, `clockFit.anchorHostUs`) are on the host buffer clock — the `clock.t0_us` domain — not window-relative.
- **`captureIntegrity.firstHoleUs`** is in whichever domain the check ran in (absolute on a live write, window-relative after re-analysis). Use `firstHoleMs`.
- **Durations from impact.** Gate-record `evidence` times and `kinematicSequence.nodes[].probable` times are ms from impact, + after. `kinematicSequence.nodes[].beforeImpactMs` and gate `bound` are ms *before* impact.
- **`skeleton3d.frames[].t`** is window-relative µs under the name `t`, not `t_us`.

## Top-level structure

```json
{
  "schema":  "pinpoint.swing/2",
  "clock":   { … },         // time base
  "window":  { … },         // captured span (window-relative bounds)
  "swing":   { … },         // id / index
  "session": { … },         // owning session folder
  "athlete": { … },         // who
  "capture": { … },         // shot setup + provenance (+ club geometry)
  "streams": [ … ],         // video, IMU, ball (and phone clips)
  "thumbnail": { … },       // impact still
  "imuIntegrity":     { … },  // OPTIONAL — IMU re-fusion check
  "captureIntegrity": { … },  // OPTIONAL — camera frame holes and coverage
  "review":  { … },         // OPTIONAL — club, rating, note, board annotations
  "launchMonitor": { … },   // OPTIONAL — a launch monitor's reading
  "analysis": { … },        // OPTIONAL — the analysed swing (pinpoint.analysis/3)
  "summary": { … },         // swing.ppsw only — the list index (pinpoint.swingsummary/4)
  "archive": { … }          // ONLY on the stub left by archiving a session
}
```

| Block | Present | Purpose |
|---|---|---|
| `schema` | always | `pinpoint.swing/2`. |
| `clock` | capture, analysis-only, device-only | Absolute anchor (`t0_us`) + wallclock. |
| `window` | capture, analysis-only, device-only | Window-relative capture span. |
| `swing` | capture, analysis-only, device-only | Folder name + index in the session. |
| `session` | capture, analysis-only, device-only | Owning session folder. |
| `athlete` | capture, analysis-only, device-only | Who, and handedness. |
| `capture` | capture, analysis-only, device-only | Session type, shot source, impact, latencies, host, club. |
| `streams` | always (may be `[]`) | One entry per camera, IMU lane, ball track or phone clip. |
| `thumbnail` | when a JPEG was written | Impact-frame still. Absent on analysis-only, device-only and phone-only documents, and when no camera gave a frame. |
| `imuIntegrity` | when an IMU lane could be checked | Re-fusion parity verdict. |
| `captureIntegrity` | when a camera lane could be checked | Frame holes and coverage verdict. |
| `review` | when a club is known, or the user edited the shot | Club, stars, note, board annotations. |
| `launchMonitor` | when a reading was paired to the shot | What the device said. |
| `analysis` | when analysis ran | Absent for analysis-skipped captures and phone-only documents. |
| `summary` | every `swing.ppsw` | The session-list scalars, rebuilt on every write (`saveDocument`). Never in a `swing.json`. Derived from the rest of the document: analysis code should not read it. |
| `archive` | archived stubs only | `{ location, archivedAt }`; the full document is at `location`. |

The document kinds, by writer:

| Kind | Writer | What it has |
|---|---|---|
| Capture | exporter + `writeSwingJson` | Everything above that applies. |
| Analysis-only | `buildSynthManifest` + `writeSwingJson` (export failed, analysis worked) | Same header, `streams: []`, no `thumbnail`. |
| Device-only | `writeDeviceOnlySwing` | See "Device-only swings". |
| Phone-only | `ppcp_clip_filer.cpp` `ensureSwingDocument` | `schema`, `streams` (the phone clip), `summary` — nothing else. Written when a phone clip lands for a shot whose pipeline wrote no document. |

**Schema id.** Every document on disk says `pinpoint.swing/2`. The exporter's raw manifest and the phone path's minimal manifest say `pinpoint.swing/1`, but neither is ever saved as it is: both go through `writeSwingJson`, which overwrites the id with `/2` before saving. `writeDeviceOnlySwing` writes `/2` itself. The `/1` strings are dead values in an in-memory object; a reader never sees them.

---

## `clock`, `window`, `swing`, `session`, `athlete`

```json
"clock":   { "t0_us": 176400665083, "unit": "us", "wallclock": "2026-07-05T11:32:34.072Z" },
"window":  { "start_us": 0, "end_us": 5000000 },
"swing":   { "id": "swing_0006", "index": 6 },
"session": { "dir": "2026-07-05_Mark-Liversedge_Wrist_02" },
"athlete": { "handedness": "Right", "name": "Mark Liversedge",
             "uuid": "52ff45d8-37a6-4474-bdc6-5a4184f78387" }
```

| Field | Type | Notes |
|---|---|---|
| `clock.t0_us` | int µs | **Absolute** EventBuffer instant of the window start — the only absolute time in the document. `0` on a device-only swing (there is no buffer). |
| `clock.unit` | str | `"us"`. Not written on a device-only swing. |
| `clock.wallclock` | ISO-8601 ms | UTC wallclock of the **window start**: the UTC instant frozen with the window's end, minus the window's length (`swing_exporter.cpp`, `shot_processor.cpp`). On a device-only swing, when the reading was read. |
| `window.start_us` / `end_us` | int µs | Window-relative span; `start_us` is always `0`. ⚠ A device-only swing spells these `startUs` / `endUs` (both `0`). |
| `swing.id` / `index` | str / int | Folder name and index within the session. |
| `session.dir` | str | Session folder name. ⚠ A device-only swing writes `session.id` instead. |
| `athlete.name` / `uuid` | str | Athlete record. |
| `athlete.handedness` | `"Right"` \| `"Left"` | Drives lead-arm sign throughout analysis. Not written on a device-only swing. |
| `athlete.heightM` | float m, optional | **Added 2026-09-26.** Athlete height, so re-analysis scales the 3-D skeleton as the live shot did. Written only by the analysis-only header (`buildSynthManifest`); the exporter's `athlete` block does not write it, so a normal capture lacks it. |

`athlete.sex` is read (the club set for the launch-monitor arc radius; re-analysis club standards) but no writer puts it in a document; every reader takes its absence.

---

## `capture`

Shot setup and provenance. Built by `SwingExporter::captureBlock`, the same function the analysis-only header uses.

```json
"capture": {
  "sessionType": 1,                 // SessionController::Type — 1 (Wrist) on every new swing
  "shotSource": 4,                  // ShotController::Source: 0 Manual·1 Imu·2 Pose·3 Ball·4 Acoustic·5 Ppcp
  "impactUs": 3481388,              // window-relative impact (-1 = unknown)
  "swingDetectionSensitivity": "Medium",
  "motionCaptureQuality": "High",
  "latencyUs": { "imuBle": 30000, "audioDevice": 0, "micTravel": 2915 },
  "host": { "app": "PinPointStudio", "version": "0.1.10007", "gitSha": "cb5c646",
            "hostname": "GOLFSIMPC", "platform": "Windows 11 Version 25H2", "poseBackend": "CUDA" },
  "club": { "lengthMm": 940, "shaftType": "steel", "bandCentersMm": [308, 362, 560, 758, 808, 854],
            "hoselFromButtMm": 0, "shaftLengthMm": 0, "handsEndMm": 0, "name": "7 IRON",
            "lengthPrior": { "px": 372.4, "varPx": 96.1, "n": 5 } }
}
```

| Field | Type | Notes |
|---|---|---|
| `sessionType` | int | `SessionController::Type`: `-1` None · `0` Swing · `1` Wrist · `2` GRF · `3` Coach. **No longer selects anything.** Session types left the UX with the session-setup refactor (Oct 2026); since 2026-10-27 every session starts as Wrist (`SessionController::defaultType`), so new swings say `1`. One analysis pipeline runs for every swing (`analysis_pipeline.cpp`). Re-analysis reads the value into the job for the profiler's run record only (`analysis_profiling.cpp`). Its one remaining effect is on **old** swings: an IMU stream with no `device.role` falls back to the Wrist slot map only when `sessionType` is `1` (`swing_analysis.h`). Older folders also carry it in their `_<Type>_` name. |
| `shotSource` | int, or the string `"launchMonitor"` | What fired the shot — `ShotController::Source` (`shot_controller.h`): `0` Manual · `1` Imu · `2` Pose · `3` Ball · `4` Acoustic · **`5` Ppcp** (added 2026-08-27: a shot arbitrated over the phone protocol — a label, not an authority). The ordinals are persisted: `Ppcp` was appended so it could not be taken for `Acoustic`, because re-analysis applies the legacy microphone de-bias only to `4`. A device-only swing writes the **string** `"launchMonitor"`; an integer read of it (`toInt(-1)`) gives `-1`. |
| `impactUs` | int µs | Window-relative impact — the re-analysis impact reference, present even on analysis-skipped captures. `-1` = unknown (always `-1` on a device-only swing). |
| `swingDetectionSensitivity` | str | `Low` / `Medium` / `High`. |
| `motionCaptureQuality` | str | **Added 2026-07-13.** Offline pose-model tier, `Medium` / `High`. Re-analysis replays it rather than reading the host's setting (`High` → ViTPose++-L where present, else B); absent ⇒ ViTPose-B. |
| `latencyUs.imuBle` / `audioDevice` | int µs | Detector back-dating constants at capture. |
| `latencyUs.micTravel` | int µs | **Added 2026-08-10.** Acoustic travel, hitting strip → mic, already subtracted from the anchor at capture. Its **presence** says the anchor is travel-corrected; absent marks a legacy acoustic anchor, which re-analysis corrects. |
| `host.*` | str | App/build/machine provenance. `poseBackend` = CUDA / CoreML / CPU. |
| `club` | object, optional | The club the session was on, with its geometry, from the athlete's bag. Written when any of length, bands or name is known. Every key below is written whenever the block is; `0` / `""` / `[]` mean unknown. Added 2026-07-07, so re-analysis can recover the club's retro-band geometry. |
| `club.lengthMm` | number mm | Club length; sizes the shaft-tracker head extrapolation. |
| `club.shaftType` | str | `steel` / `graphite` / `""`. |
| `club.bandCentersMm` | number[] mm | Retro-band centres from the butt. **Empty ⇒ untaped** → the shaft tracker runs ray evidence only. |
| `club.hoselFromButtMm` | number mm | **Added 2026-07-09.** Hosel offset from the butt (`0` = unknown). |
| `club.shaftLengthMm` | number mm | **Added 2026-09-08 (markerless P4).** Exposed shaft, bottom of grip → top of hosel (`0` ⇒ the tracker assumes a 265 mm grip). Grip end from the butt = `hoselFromButtMm − shaftLengthMm`. |
| `club.handsEndMm` | number mm | **Added 2026-09-08 (markerless P4).** Where the hands end on the grip, from the butt (`0` ⇒ 180). The steel-segment lock's proximal landmark. |
| `club.name` | str | **Added 2026-07-10.** Canonical club id — half the club-length prior key (`athleteUuid\|clubName\|cameraKey`), and step 2 of the club resolver (after `review.club`; `club_vocabulary.h`). |
| `club.lengthPrior` | object, optional | **Added 2026-07-10.** The club-length prior the live fuse used for this shot (before this shot's update): `px`, `varPx` (px²), `n`. Re-analysis replays it, never AppSettings — deterministic, cross-host. Omitted when the shot ran without one. |

**`capture.club` can change after capture.** With *follow Premier's club* on (`launchmonitor/premierFollowClub`), when the Premier reading arrives after the shot was analysed under a different club, `updateLaunchMonitor` **replaces the whole `capture.club`** with the device-named club's bag record — the same keys, **without `lengthPrior`** — and moves a seeded `review.club` with it; it never overrides a hand-set `review.club`, and the swing is then re-analysed (`swing_doc.cpp`, `launch_monitor_controller.cpp`). **Added 2026-11-03.** No other writer touches `capture.club`; a user's re-label goes to `review.club`.

---

## `streams`

An array with one entry per camera, IMU lane, ball track or phone clip. `kind` discriminates (`video`, `imu`, `ball`, `pose`); readers skip kinds they do not know.

### Video stream (`kind: "video"`)

```json
{
  "kind": "video", "alias": "Face-On", "file": "Face-On.mp4",
  "encoded": { "width": 1280, "height": 1024 },
  "source":  { "serial": "17453937", "width": 1280, "height": 1024, "pixelFormat": "BayerRG8" },
  "raw": { "file": "Face-On.raw", "count": 746, "frameBytes": 1310720,
           "stride": 1280, "width": 1280, "height": 1024, "pixelFormat": "BayerRG8" },
  "frames": { "count": 746, "t_us": [2720, 9402, 16126, … ] },
  "capture": { "fps_num": 150713, "fps_den": 1000, "measuredFps": 150.7,
               "exposureUs": 6573.56, "exposureSource": "measured", "exposureAuto": true,
               "timestampSource": "device", "clockMap": { … } },
  "playback": { "fps": 30 },
  "processing": { "demosaic": "EA", "restorer": "none" },
  "setup": { "perspective": 2, "perspectiveName": "FaceOn", "mirrored": false,
             "fixedInPlace": true,
             "ballDetection": { "calibrated": false, "margin": 0, "driftAtCapture": 0,
                                "calibratedAt": null, "searchRoi": [0.36, 0.72, 0.30, 0.24] } }
}
```

| Field | Type | Notes |
|---|---|---|
| `alias` | str | Human/UI name; also the media filename stem. |
| `file` | str | Encoded video in the swing folder. ⚠ **A video element with no `file` is not video** — it is a phone clip that is owed (see "Phone video stream"); `hasVideo`, the replay and the disk loader all skip it. |
| `encoded.width/height` | int | Encoded dimensions. |
| `source.*` | — | Sensor native dims + `pixelFormat` + camera `serial`. |
| `raw` | obj | **Optional** undecoded sensor sidecar (`<alias>.raw`, a single concatenated blob). `frameBytes`/`stride`/`pixelFormat` describe its layout. Present when raw-frame saving is on. |
| `frames.t_us` | int[] µs | Window-relative per-frame times — the replay master clock and the domain analysis samples align to. `count` = frame total. |
| `frames.clock` | str, optional | **Added 2026-10-31.** The clock `t_us` runs on when it is **not** the window's. Only value: `"clip"` — a phone clip timed from its own first frame. **Absent means the window's clock.** Read by `SwingStore::streamOnWindowClock`; a reader that needs window time leaves a `"clip"` stream out. |
| `capture.fps_num/den` | int | Nominal frame rate (`fps_num/fps_den`) — what the camera was asked for or reported. |
| `capture.measuredFps` | float, optional | **Added 2026-09-15.** The rate the frames arrived at: `1e6 /` the median inter-frame interval, to 0.1 fps; written with ≥ 8 frames. The truth when the nominal rate is in doubt (a frame-rate node has reported 30 for a camera delivering 591). |
| `capture.exposureUs` | float µs, optional | Exposure — feeds the shaft tracker's blur model. Written when known. |
| `capture.exposureSource` | str, optional | `measured` / `derived` / `unknown` (`format_descriptor.h`); written with `exposureUs`. |
| `capture.exposureAuto` | bool, optional | Measured streams only: the exposure was auto (and may drift). |
| `capture.timestampSource` | str, optional | Which clock stamped `frames.t_us`: `device` (the camera's own timestamp mapped onto the host clock), `devicePts` (a platform instant already on it — AVFoundation), `hostArrival` (stamped when the frame reached the app). **Absent means `hostArrival`** — every swing before 2026-09-16. ⚠ Arrival-stamped frames are late by the delivery path and jitter with it, so an older swing's timings and a newer one's are not directly comparable (`event_buffer_design.md` §9). |
| `capture.clockMap` | object, optional | How the camera-clock mapping did on this stream: `method` (`latch` / `envelope` / `devicePts`), `skewPpm`, `fitSpanS`, `residualP50Us`/`P99Us`, `lagP50Us`/`P99Us`/`MaxUs` (what the delivery path cost), `frames`, `droppedFrames` (gaps in the camera's own frame counter), and when a latch pinned the constant `latchRttUs`, `minLatencyUs`. Flags only when set: `shortBaseline`, `degenerate`, `implausibleRate`, `clampedMonotonic`, `clampedToArrival`, `streams`. |
| `capture.gainDb`, `capture.gainSource` | float dB, str, optional | **Added 2026-09-15.** Sensor gain (`impact_camera_design.md` §10.3). `gainSource` `applied` = read back from the camera after the write; `requested` = only the request is known. Absent when gain was never written. |
| `capture.blackLevelLift` | float %, optional | **Added 2026-10-08.** Percent the black level was raised over the camera's factory calibration (`flir_camera_settings.md` §5); moves every recorded level up ~2.5 per percent. Written only when > 0. Replaced `gamma`. |
| `capture.gamma` | float, optional | **Retired 2026-10-08.** Present only on impact clips captured 2026-09-15 … 2026-10-08. |
| `capture.strobe`, `capture.viewGain` | bool, float | Impact stream only (`setup.perspective` 4): whether Line1 carried ExposureActive, and the display stretch the operator had on the tile (replay applies the same; 1 = none). Never in the pixels. |
| `capture.note` | str, optional | The operator's free text for the camera (lens, aperture, light), from Settings → Cameras. |
| `playback.fps` | number | Container playback rate. `30` on every exported stream — casual scrub speed, **not** the analysis rate. A phone clip carries its measured file rate here. |
| `clip` | object, optional | `{ start_us, end_us }` window-relative: impact camera only, whose export is trimmed to this band around `capture.impactUs`. Says the short `frames.t_us` is deliberate; replay loops such a stream. |
| `processing.demosaic` | str | `EA` (edge-aware) / `bilinear` / `none`. |
| `processing.restorer` | str | Always `none`. |
| `setup.perspective` | int | `0` None · `1` DownTheLine · `2` FaceOn · `3` Other · `4` Impact. `perspectiveName` is the label (also the session picker's device tag: `swingDeviceTokens`). May be `null` on older exports. |
| `setup.mirrored` | bool | Webcam mirror flag (affects pose/shaft chirality). |
| `setup.fixedInPlace` | bool | Camera-fixed calibration state. |

**`setup.ballDetection`** — ball-detector provenance plus the search box offline re-analysis uses.

| Field | Type | Notes |
|---|---|---|
| `calibrated` | bool | Whether a v1 calibration profile was active. The v1 calibration stack is retired (temporal v2 detector), so now always `false`. |
| `margin` / `driftAtCapture` | float | Legacy v1 calibration diagnostics (`0` on the v2 path). |
| `calibratedAt` | ISO \| null | v1 calibration timestamp (`null` on v2). |
| `center` / `radiusNorm` / `positionSource` | float[2] / float / str | **Optional** — a stable calibrated ball position (full-frame normalized; radius to width). Present only when a calibrated position exists (`positionSource: "calibrated"`). |
| `searchRoi` | float[4] | **Added 2026-07-08.** The hitting-area box `[x, y, w, h]`, full-frame normalized — the region the live ball detector searched. Offline re-analysis (`BallRunner`) searches this box instead of the pose-derived stance corridor, so it matches live detection and skips out-of-box distractors (feet/shoes). **Omitted** when no hitting area is set; swings captured before 2026-07-08 lack it (re-analysis falls back to the stance corridor). |
| `baseline` | obj | **Added 2026-07-10.** The live detector's learned empty-mat baseline, snapshotted at seed time so offline re-analysis (`BallRunner`) reconstructs the exact baseline the session learned instead of self-seeding over the swing's opening frames (where the ball already sits, which bakes it into the subtracted baseline). `{ "file": "<alias>.ballbase.f32", "w": int, "h": int, "roi": [x,y,w,h], "rHat": float, "fps": float, "noise0": float }`. `file` names the raw float32 sidecar (see "Companion files"); `w`/`h` are its ROI pixel dims; `roi` is the full-frame-normalized box `B` covers; `rHat` is the seed-time radius; `noise0` is the robust-noise fallback. `fps` is **provenance only** — the offline tracker re-measures its own pushed-frame rate. On re-analysis a valid baseline makes the `BallRunner` re-run authoritative: the recorded `ball` stream (known wrong-time-base) is dropped so it can't shadow the re-run. **Omitted** for swings captured before 2026-07-10, or when the ROI was reset mid-seed. |

### Phone video stream (`origin`)

A clip filed from a phone over PPCP is a `kind: "video"` element written by `SwingDocWriter::updateStreamOrigin`, not by the exporter. It is created when the clip is **asked for**, before any bytes exist, and updated in place as the transfer moves on. **Added 2026-09-01.**

```json
{
  "kind": "video", "alias": "iPhone",
  "origin": { "transport": "ppcp", "peerId": "…", "sessionId": "…", "captureId": "…",
              "streamId": "…", "transfer": "complete", "completeness": "complete",
              "committedAt": "2026-11-08T14:02:11.507Z" },
  "file": "iPhone.mov",
  "frames": { "count": 719, "t_us": [0, 4167, 8334, …], "clock": "clip" },
  "playback": { "fps": 239.8 }
}
```

| Field | Type | Notes |
|---|---|---|
| `origin.transport` | str | `"ppcp"`. |
| `origin.peerId` / `sessionId` / `captureId` / `streamId` | str | The protocol identity of the Capture — the link to the PPCP import ledger. `captureId` is empty until the phone answers; `streamId` omitted when empty. Provenance only: analysis never reads `origin`. |
| `origin.transfer` | str | **This host's** view: `requested` · `arriving` · `complete` · `absent` · `timeout` · `failed`. |
| `origin.completeness` | str, optional | **The phone's** word about the Capture, carried verbatim and never inferred: `complete` · `partial` · `absent`. |
| `origin.reason` | str, optional | Only on an absent Capture, e.g. `outside_buffer`. |
| `origin.committedAt` | ISO-8601, optional | When the bytes were durably written. |
| `file` | str, optional | **Absent until the bytes land** — an owed clip is inert to every reader. |
| `frames.t_us` | int[] µs | **Relative to the clip's own first frame, not to `clock.t0_us`**, and marked so by `frames.clock: "clip"` (since 2026-10-31; clips filed before then have the same timing without the marker). Required for the clip to play. |
| `playback.fps` | float | The file's own rate, measured from its frame times. |

There is no `source`, `encoded`, `capture` or `setup` on this element, so the picker tags it `camera`.

### IMU stream (`kind: "imu"`)

One element per IMU **lane**. A Witmotion is one lane. A HackMotion wG3 is one device with **two lanes** — lower arm and palm — each its own stream (`hm_instance.cpp`).

```json
{
  "kind": "imu", "alias": "Green Sensor", "schema": "imu_sample_v2",
  "source": { "serial": "C6:05:20:9D:04:76" },
  "device": { "outputRateHz": 200, "fusionMode": "9axis", "orientationFilter": "Madgwick",
              "role": 5, "roleName": "LeadForearm",
              "alignA": [1, 0, 0, 0], "mountM": [1, 0, 0, 0], "calibrated": true, "anatCalibrated": true,
              "mountDeviationDeg": 2.1, "mountGravityErrorDeg": 0.8, "calibAgeSec": 412.0,
              "calibratedAt": "2026-11-08T13:55:02.114Z" },
  "units": { "accel": "g", "gyro": "deg/s", "quat": "wxyz" },
  "samples": {
    "count": 492,
    "t_us": [3878, 6587, 9308, … ],
    "data": [ [-0.984, -0.013, -0.102,  11.108, 2.746, -2.624,  0.474, 0.342, 0.615, -0.529], … ]
  }
}
```

| Field | Type | Notes |
|---|---|---|
| `schema` | str | `imu_sample_v2` — names the 40-byte record **layout**, shared byte-for-byte by Witmotion and HackMotion lanes. What the numbers mean differs; that is carried by `instrument` and `units`. |
| `source.serial` | str | Witmotion: the device serial/MAC (stable across runs; binding key). wG3: `"<deviceId>#lowerArm"` or `"<deviceId>#palm"` (`hm_unit_id.h`) — the one spelling, also the placement key. |
| `alias` | str | Witmotion: the user's alias (else the serial). wG3: `"<alias or device id> · Lower arm"` / `"… · Palm"`. |
| `instrument` | str, optional | **Added 2026-08-17.** `"hackmotion"` on a wG3 lane; absent on Witmotion. The machine-checkable discriminator — do not parse `units`. |
| `units` | obj | Witmotion: `accel "g"`, `gyro "deg/s"`, `quat "wxyz"`. wG3 (since 2026-08-17): `accel` = `"g (linear, gravity-removed — NOT comparable to a Witmotion accel channel)"`, `gyro "deg/s"`, `quat` = `"wxyz, world->body (conjugate of the pipeline's convention — unreconciled, Phase D)"`. |
| `samples.t_us` | int[] µs | Window-relative sample times. |
| `samples.data` | float[][10] | Per sample `[ax,ay,az, gx,gy,gz, qw,qx,qy,qz]`. **Witmotion:** raw accelerometer (g, includes gravity — 1 g at rest), gyro (deg/s), and the **host-fused** world orientation PinPoint owns (Madgwick/ESKF over raw accel+gyro, not the device's on-board Euler output — see `docs/design/imu_frame_contract.md`). **wG3:** gravity-removed linear acceleration (g, ≈ 0 at rest; from the raw counts at 1 mg/LSB), the library's scaled gyro (deg/s), and the **device's own on-board orientation**, stored verbatim as it arrives — world→body, the conjugate of the pipeline's convention, and after the device's own calibration transform when the unit was calibrated (`hm_sample_convert.h`). It is not host-fused and cannot be re-fused. |
| `samples` | — | Always inline. The CSV/binary sidecar options were retired 2026-09-23. |
| `device` | obj, optional | Device state at capture. Written when the lane's device was known to the capture. Keys below. Absent `device` on older files → role falls back to the placement map. |

**`device` — every lane:**

| Field | Type | Notes |
|---|---|---|
| `outputRateHz` | int | Witmotion: the live configured rate (settings fallback 200). wG3: the **measured average of an adaptive rate** (25 Hz at rest, 100 in motion, bursts to ~799), rounded — not a period. |
| `fusionMode` / `orientationFilter` | str | Witmotion: device 6/9-axis mode, host filter. wG3: `""` — it fuses on the device. |
| `role` / `roleName` | int / str | `SegmentRole` from role-keyed placement (`segment_role.h`), e.g. `5` / `"LeadForearm"`. wG3 lanes: `0` / `""` — the role travels in `analysis.bindings`, and re-analysis takes it from the unit (`hm_binding_recon.h`). |
| `placementSlot` | str | Legacy `"A"/"B"/"C"`, written up to 2026-10-05 only. Readers still accept it. |

**`device` — Witmotion calibration snapshot** (present when the live instance was found; added 2026-06-24). Lets an analysis-skipped swing be re-analysed: re-analysis builds a binding from a 4-element `alignA` + `mountM` pair when `analysis.bindings` is absent. Never written on a wG3 lane, on purpose.

| Field | Type | Notes |
|---|---|---|
| `alignA` / `mountM` | float[4] `[w,x,y,z]` | Fusion-world → anatomical-world (A); anatomical-body → sensor-body (M). |
| `calibrated` / `anatCalibrated` | bool | Composite gate / anatomical step done. |
| `mountDeviationDeg` / `mountGravityErrorDeg` | float ° | Mount checks. |
| `calibAgeSec` | float s | Age of the calibration at the shot; `-1` = never. |
| `calibratedAt` | ISO-8601, optional | When it was calibrated. |
| `calibrationSource` | str, optional | **Added 2026-10-26.** `"trunkStandTall"` when A/M came from the hands-free trunk solve; absent = the arm routines (`trunk_calib_provenance.h`). |
| `trunkSolve` | object, optional | **Added 2026-10-26**, with `calibrationSource`: `deltaTier` (`δ0`/`δ1`/`δ2`), `sigmaDeltaDeg`, `sampleFraction`, `maxAbsGyroDps`, `gravityErrPct`, `longAxisFromUpDeg`, `faceFromHorizontalDeg`. |

**`device` — HackMotion wG3 capture provenance** (wG3 lanes only; added 2026-08-17, `history` 2026-08-18). ⚠ Every key is **omitted when not measured** rather than written as 0 or −1 — an absent count means nobody looked; `0` is a positive claim.

| Field | Type | Notes |
|---|---|---|
| `skewUs` | float µs, optional | Palm-minus-lower-arm skew, the **session median** (~920 µs measured). The two units are not sampled together; recorded, not subtracted, because its physical meaning is unresolved. |
| `calibrationStateAtStart` / `AtEnd` | int, optional | The device's calibration state at the window's start and end: `0` unknown · `1` uncalibrated · `2` calibrated · `3` lost (libwrist `sample.h`). Uncalibrated readings carry an 11–15° mounting offset that cannot be removed later. Re-analysis refuses a wG3 binding on it. |
| `calibrationSpansTransition` | bool, optional | Only when `true`: the state changed inside the window. |
| `configBits` | int, optional | The `a0 01 <cfg>` byte behind these samples (sets the gyro divisor). |
| `pinnedSamples` | int, optional | Samples in this window with a saturated channel, **this unit**. Written once a sample was seen. A clipped peak looks like a plausible flat top. |
| `quatNormSuspect` | int, optional | Records whose quaternion norm was out of tolerance (counted on both units). |
| `provenanceDropped` | int, optional | Only when > 0: pinning outran the ring, so `pinnedSamples` is a **floor**. |
| `noFitSkippedSession` | int, optional | Only when > 0. **Session** total of samples skipped for having no host time — a symptom; should be 0. |
| `history` | object, optional | **Only when a deferred-history pull was attempted** — a purely live lane has none. Describes the device's one stream, so it is the same on both lanes except the stitch counts. |

**`device.history`** (wG3):

| Field | Type | Notes |
|---|---|---|
| `status` | int | `wr_history_status`: `0` complete · `1` short · `2` holed · `3` timed out · `4` cancelled · `5` refused (alignment) · `6` evicted · `7` no stream · `8` link lost · `9` not alignable · `10` error (libwrist `history.h`). |
| `attempts` | int | Requests issued. |
| `coverageOverflowed` | bool, optional | Only when `true` — and then `coverageFraction`, `density` and `delivered` are optimistic bounds. |
| `coverageFraction` / `density` / `achievedHz` | float, optional | Of what was asked for; 1 / median delivered index step; average delivered rate. |
| `largestGapUs` | int µs, optional | The figure that decides whether impact survived. |
| `liveOverlapSamples` / `liveOverlapMismatches` | int, optional | **Both or neither**. Zero mismatches over zero samples is no evidence. |
| `selfRecordingGap` | `{startUs, endUs}`, optional | The hole the pull itself caused, which falls after the requested window. |
| `stitchedFromLive` / `stitchedFromDeferred` | int, optional | This lane's composition. A stitched lane changes rate partway through, and nothing in `samples` records that. |
| `delivered[]` | `{startUs, endUs}` | Half-open host-time intervals that arrived. |
| `gaps[]` | `{startUs, endUs, kind}` | `kind`: `0` not recorded · `1` not delivered · `2` fit-blind (libwrist `history.h`). The kinds may overlap. |
| `clockFit` | object, optional | The clock fit **these samples were dated by** (it re-anchors at every pull): `flags`, `observations`, `fittedRateHz` (~799.2), `anchorHostUs`, `anchorIndex`, `slopeUsPerIndex`, `offsetUs`, `spanUs`, `residualMedianUs` / `P90Us` / `MaxUs`, `driftUsPerS`. |

`delivered`, `gaps`, `selfRecordingGap` and `clockFit.anchorHostUs` are on the host clock that stamps the samples in the EventBuffer — the `clock.t0_us` domain — **not** window-relative.

### Ball stream (`kind: "ball"`)

The face-on live ball detector's per-frame output for the window (`swing_exporter.cpp`, built in `shot_processor.cpp`). **Added 2026-07-08.** Re-analysis reads it into the ball track when it does not re-run the ball. It is the offline re-analysis input, distinct from the analysed `analysis.ball` block; consumers that read `analysis.*` can ignore it.

```json
{ "kind": "ball", "alias": "Face-On", "schema": "ball_v2", "source": { "serial": "17453937" },
  "layout": "found,x,y,r,conf",
  "frames": { "count": 746, "t_us": [ … ], "data": [ [1, 0.512, 0.803, 0.011, 0.92], … ] },
  "launch": { "t_us": 3484100, "x": 0.512, "y": 0.803 } }
```

| Field | Type | Notes |
|---|---|---|
| `schema` / `layout` | str | `ball_v2`; `found,x,y,r,conf`. |
| `frames.t_us` | int[] µs | Window-relative. |
| `frames.data` | float[][5] | `found` (0/1), centre `x`,`y` and radius `r` (normalised 0..1), `conf`. |
| `launch` | object, optional | The detected launch: window-relative `t_us` and position. Absent when no launch was seen. |

### Pose stream (`kind: "pose"`)

Reserved and not written today: the exporter serialises `poseStreams` (`pose_movenet_v1`, layout `coco17:y,x,score`, 51 floats a frame), but nothing fills them. The pose lives in `analysis.pose2d`.

---

## `thumbnail`, `review`

```json
"thumbnail": { "file": "thumb.jpg", "t_us": 3479416 },
"review":    { "club": "7 IRON", "rating": 0, "note": "", "excluded": true, "clubOverride": "6 IRON" }
```

| Field | Type | Notes |
|---|---|---|
| `thumbnail.file` / `t_us` | str / int µs | Impact-nearest JPEG (≤ 480 px) + its window-relative frame time. Absent when none was written. |
| `review.club` | str | The club the shot is graded as — step 1 of the club resolver (`review.club`, else `capture.club.name`, else the `DRIVER` stub; `club_vocabulary.h`). Distinct from `capture.club`. **Seeded at capture since 2026-08-09**: `writeSwingJson` fills it when the block has no club, with the session's active club, else the athlete's preferred club, else the stub — so on a live capture it is not necessarily a human pick. Overwritten by `updateReview` (the swing-edit popover) and by a follow-Premier club rewrite when it was not hand-set. Device-only swings seed it with the session's club. |
| `review.rating` | int 0–5 | 0 = unrated. Seeded `0`. |
| `review.note` | str | Free text. Seeded `""`. |
| `review.excluded` | bool, optional | **Added 2026-10-13.** The launch monitor board's *Leave out*: left out of the board's means, spreads, grades and ellipses. Written only when `true`; clearing removes the key (`updateShotAnnotations`). |
| `review.clubOverride` | str, optional | **Added 2026-10-13.** The club the **board** treats the shot as; absent = the default (the club evidence's suggestion, else the recorded club). Not read by the club resolver. Written only when set. |

The whole block is optional. `updateReview` **merges** rating, note and club into it and leaves the board's annotations alone; a block left with nothing in it is removed. No other review key is written.

---

## `launchMonitor`

Written by `SwingDocWriter::updateLaunchMonitor` when a reading is paired to the shot, from `lmRawBlock` — the one builder, shared with device-only swings. **Late and repeatable**: the document is normally on disk before the reading lands, and a later, fuller reading of the same shot (Premier's club data a shot later, Premier's carry) **replaces the whole block** and its `lm.` rows. Added 2026-08-04.

```json
"launchMonitor": {
  "kind": "premier",
  "deviceShotId": "0f3c9a2e-…",
  "deviceClub": "7I",
  "sourcePath": "…/SessionLiveSnapshot.1234.json",
  "readAtMs": 1786600000000,
  "contactAtMs": 1786599999420,
  "clubheadSpeed": 87.19, "ballSpeed": 111.68, "smashFactor": 1.281, "carryDistance": 150.4, …,
  "dropped": [ { "key": "lm.faceAngle", "code": "noData" } ]
}
```

| Field | Type | Notes |
|---|---|---|
| `kind` | str | The connector, set by the connector itself: `gcquad` (FSX `LastShot.CSV`, 2026-08-04) · `gspro` (GSPro Open Connect, 2026-09-10) · `premier` (GC Quad under Foresight Premier, 2026-11-02) (`launch_monitor_base.cpp`). Recorded, not assumed. |
| `deviceShotId` | str | The device's own shot id (a counter on FSX; a GUID on Premier). Not a PinPoint id, and never treated as one; it tells a fresh row from the same row restated. |
| `deviceClub` | str | The club the device believed in. Provenance, except that with *follow Premier's club* on, a Premier code that maps to the vocabulary sets the session's club and may rewrite `capture.club` (see `capture`). |
| `sourcePath` | str | The file it came from (GSPro: the connector's description). |
| `readAtMs` | int epoch ms | When PinPoint first read it. A fuller re-read of the same shot keeps the first time. |
| `contactAtMs` | int epoch ms, optional | **Added 2026-11-02.** The device's own contact stamp — Premier's `CreatedAtUtc`, ~0.58 s after contact. Absent for FSX and GSPro. The pairing checks a claim against it. |
| _readings_ | number | One key per value the device reported, in catalogue units; absent = not reported, never zero. See the table below. |
| `dropped[]` | array | **Added 2026-10-16.** What the import left out and why: `key` (the `lm.` key), `code` (`noData` · `outOfRange` · `noInputs`), and when known `raw` (the file's own value), `value` (converted), `lo`/`hi` (the range it was held to) and `unit` (`lm_gates.h`). **Written even when empty**, so `[]` = nothing dropped and an absent key = imported before the list was kept. Turned into `analysis.gates` records on every write. GSPro drops nothing. A Premier shot that never got club data has every club field as a `noData` drop. |

**Readings** (`launch_monitor_reading.cpp` — the single list; the `lm.`-prefixed metric key is `lm.<name>`):

| Group | Key | Unit | Notes |
|---|---|---|---|
| Club | `clubheadSpeed` | mph | Pairs with our bare `clubheadSpeed`. |
| Club | `attackAngle` | ° | Pairs with our `attackAngle`. |
| Club | `clubPath` | ° | |
| Club | `lowPointAhead` | in | + = ahead of the ball. No connector fills it yet. |
| Club | `faceAngle`, `faceToPath`, `dynamicLoft`, `lieAngle` | ° | |
| Club | `spinLoft` | ° | Derived by the parser when not stated: dynamicLoft − attackAngle. |
| Club | `closureRate` | °/s | |
| Launch | `ballSpeed` | mph | |
| Launch | `launchAngle`, `launchDirection` | ° | |
| Strike | `smashFactor` | ratio | Derived when not stated: ballSpeed / clubheadSpeed. |
| Strike | `strikeLocation` | mm | heel − / toe +. |
| Strike | `strikeHeight` | mm | low − / high +. |
| Spin | `spinRate`, `backSpin`, `sideSpin` | rpm | |
| Spin | `spinAxis` | ° | Derived when not stated: atan2(sideSpin, backSpin). |
| Flight | `carryDistance`, `totalDistance`, `distanceToPin` | yd | Premier's carry comes from `ShotReplay/history.json` ~0.8 s after the shot and arrives in a later rewrite of this block, or from the End Session sweep (`premier_monitor.h`). |
| Flight | `offline` | yd | left − / right +. |
| Flight | `peakHeight` | ft | |
| Flight | `descentAngle` | ° | |

**In `analysis.metrics[]`.** Every reading also appears as an `lm.`-keyed entry — an empty curve with one `phaseSamples` entry at Impact, `t_us` = `capture.impactUs` (0 when unknown). The `lm.` prefix means "the device said this": our own optical estimates keep their bare keys beside it, because a launch monitor is a reference instrument to check our estimates against. Two rows are **ours, derived from the reading**, and so carry bare keys: `compoundMiss` (ratio) and `lowPointFromAttack` (in, with an assumed-input σ). The metric rows are written only when the document has an `analysis` block; the raw block is written regardless, so a shot whose analysis failed still keeps what the device said. Re-analysis carries the `lm.` rows forward unchanged and derives the two bare rows again from this block, so this block is an input, not only a record.

**Not in the document:** the pairing's own evidence (the contact Δ against our impact, the window it was judged in) is logged, not stored (`launch_monitor_controller.cpp`). A reader can recompute the Δ from `contactAtMs` and `clock.wallclock` + `capture.impactUs`.

### Device-only swings

When a reading has no capture to go into, it makes the whole document (`writeDeviceOnlySwing`): either a **standalone** shot (`launchmonitor/standaloneShots` on, a library, an athlete, a session running and capture switched on — `standalone_gate.cpp`), or a reading claimed by a shot whose pipeline wrote no document (`launch_monitor_controller.cpp`). It is not a degraded capture — it is a different kind of document, and it says so rather than leaving blanks that read as failure:

| Block | In a device-only swing |
|---|---|
| `schema` | `pinpoint.swing/2`. |
| `clock` | `{ t0_us: 0, wallclock }` — no `unit`; `wallclock` = the reading's `readAtMs`. |
| `window` | `{ startUs: 0, endUs: 0 }` — zero-length, and ⚠ spelled `startUs`/`endUs`, not `start_us`/`end_us`. There was nothing to capture, and a fabricated span would put a scrubber on a shot with no frames. |
| `swing` | `{ index, id }`. |
| `session` | ⚠ `{ id: <session folder> }`, not `dir`. |
| `athlete` | `{ uuid, name }` — no handedness. |
| `capture` | `{ sessionType, shotSource: "launchMonitor", impactUs: -1 }` — nothing else (no host, no club). The device says a ball was struck, never when. |
| `streams` | **`[]`** — the fact, not a failure. `hasVideo` derives from it. |
| `thumbnail` | **absent** — an empty block would point at a file never written. |
| `review` | `{ rating: 0, note: "", club }` when the session had a club. |
| `launchMonitor` | `lmRawBlock(reading)`, as above. |
| `analysis` | `schema`, `tier` (`0`), `metrics[]` (the `lm.` readings and the two derived rows), `phases[]` (one Impact entry), `gatesVersion` and `gates` (the import's drops) — **nothing else**: no `score`, `pose2d`, `club`, `bindings`. Omitting them is what stops this being mistaken for an analysed swing whose analysis came out empty. |

**The Impact phase event is manufactured, and it has to be.** Every `lm.*` measure reduces `at p7`, and `buildPhaseGrid` returns an empty grid when `phases[]` is empty — *"unsegmented: no phase to read anything at, so nothing is producible"* — so without it the readings would persist and then resolve to nothing at all. Its `conf` is `1.0` because the **event** is certain: a ball was struck and the device measured it. What is unknown is the *instant*, and that is carried by `capture.impactUs = -1`, not by pretending the timestamp is good.

---

## `imuIntegrity`, `captureIntegrity`

Two verdicts about the **recording**, written beside the manifest blocks on a live capture (`shot_processor.cpp`) and **replaced on every re-analysis** — removed when re-analysis could not reach a verdict (`applyImuIntegrity` / `applyCaptureIntegrity` in `swing_doc.cpp`). Each is omitted when nothing could be checked, which is a no-claim, not a pass. They drive the shot's ⚠ and its exclusion from the session assessment (`dataWarningDetailFrom`), and become `shot.*` gate records in `analysis.gates`.

```json
"imuIntegrity": { "refusionOk": true, "worstMaxDeg": 0.02, "sourcesChecked": 1,
                  "thresholdDeg": 0.5, "filter": "madgwick" },
"captureIntegrity": { "ok": true, "camerasChecked": 2, "holes": 0, "framesLost": 0, "worstHoleMs": 0,
                      "firstHoleUs": -1, "preImpact": false, "postImpact": false, "holePeriods": 3,
                      "edgeToleranceMs": 50, "lateStart": false, "earlyEnd": false,
                      "startsAfterImpact": false, "windowMs": 5000, "missingStartMs": 3.1,
                      "missingEndMs": 6.2, "framesBeforeImpact": 512,
                      "cameras": [ { "serial": "17453937", "firstFrameMs": 3.1, "lastFrameMs": 4993.8,
                                     "missingStartMs": 3.1, "missingEndMs": 6.2,
                                     "framesBeforeImpact": 512 } ] }
```

**`imuIntegrity`** — **added 2026-06-26.** Re-fuses each IMU lane from its recorded raw accel+gyro and checks it reproduces the stored quaternion; a failure means the shot cannot be re-analysed offline.

| Field | Type | Notes |
|---|---|---|
| `refusionOk` | bool | `false` when a checked lane exceeded the threshold. |
| `worstMaxDeg` | float ° | Worst disagreement. |
| `sourcesChecked` | int | Lanes checked. wG3 lanes are skipped (they stream their own orientation), so a wG3-only window writes no block. |
| `thresholdDeg` | float ° | `0.5`. |
| `filter` | str | `"madgwick"` — of the lanes checked. Not written live when the orientation filter is ESKF. |

**`captureIntegrity`** — **added 2026-09-06**; coverage keys 2026-10-31 (issue #53); `firstHoleMs` 2026-10-16.

| Field | Type | Notes |
|---|---|---|
| `ok` | bool | No hole, and (when coverage was judged) a frame before impact, on every checked lane. `false` withholds the shot from the session assessment. |
| `camerasChecked` | int | Camera lanes with enough frames to judge. |
| `holes` / `framesLost` / `worstHoleMs` | int / int / float ms | Gaps longer than `holePeriods` frame periods, the frames they swallowed, the longest. |
| `firstHoleUs` | float µs | Where the first hole opens (−1 = none), ⚠ in the domain the check ran in — absolute buffer clock on a live write, window-relative after re-analysis. Use `firstHoleMs`. |
| `firstHoleMs` | float ms, optional | The same instant in ms from impact (+ after). Only when there is a hole and impact was known. |
| `preImpact` / `postImpact` | bool | A hole opened at/before impact (or impact unknown) / after it. |
| `holePeriods` | float | The hole boundary used, in frame periods (`3`). |
| `edgeToleranceMs` | float ms | How far inside the window a lane may start or stop (`50`). |
| `lateStart` / `earlyEnd` | bool | A lane starts late / stops early — a ⚠ that leaves `ok` true. |
| `startsAfterImpact` | bool | A lane has no frame at or before impact — fails `ok`. |
| `windowMs`, `missingStartMs`, `missingEndMs` | float ms, optional | Window length; the worst lane's shortfall at each end. Unknowns are left out, never 0. |
| `framesBeforeImpact` | int, optional | The fewest on any lane (absent when impact is unknown). |
| `cameras[]` | array | Per judged lane: `serial`, `firstFrameMs`, `lastFrameMs` (from the window start), `missingStartMs`, `missingEndMs`, `framesBeforeImpact`. |

The coverage keys are present only when coverage was judged; a block written before 2026-10-31 has none, which reads as "not judged". A phone clip (`frames.clock: "clip"`) is never held to the window.

---

## `summary` — the list index inside the document

**Added 2026-09-23** with `swing.ppsw`. The session list needs a few scalars from every swing, and a document runs to megabytes once decoded. So every write puts those scalars into the document as the top-level `summary` block, rebuilt from the document on **every** save and never carried over (`saveDocument`). The block lands in the root chunk, so `SwingStore::loadSummaryBlock` reads it in about a millisecond without touching a pose chunk (`swing_storage_impl.md` Stage 1 timing). It is written in the same atomic save as the rest of the document, so it cannot go stale and has no freshness check. A `swing.json` never has one; its sidecar `swing_summary.json` has the same fields plus a `source` guard (see below).

```json
"summary": {
  "schema": "pinpoint.swingsummary/4",
  "ordinal": 10, "timestampLabel": "17:45:37", "wallclockMs": 1793641537657,
  "club": "7 IRON", "hasVideo": false, "thumbnailFile": "", "score": 0,
  "dataWarning": false, "dataWarningDetail": {},
  "metrics": { "<key>": { "label": "…", "value": "+12°" } },
  "rating": 0, "note": "", "lmDeviceKind": "premier",
  "excluded": false, "clubOverride": "",
  "devices": ["lm:premier"], "handedness": ""
}
```

(From `/mnt/swingdata/Mark-Liversedge/2026-11-02_Mark-Liversedge_Wrist_02/swing_0010`, a launch-monitor-only swing. `metrics` is shown schematically.)

| Field | Type | Notes |
|---|---|---|
| `schema` | str | `pinpoint.swingsummary/4` (`/4` since 2026-09-16). Any other value (or no block) ⇒ the list reader derives the summary from the full document and does **not** write it back. The next real write refreshes it. |
| `ordinal` | int | `swing.index`. |
| `timestampLabel` | str | Display `hh:mm:ss`. Readers re-derive it from `wallclockMs` in local time, so a library carried across timezones shows local times. |
| `wallclockMs` | int | Epoch ms from `clock.wallclock` — the absolute window-start instant. `0` = unknown. |
| `club` | str | The resolved club: `review.club`, else `capture.club.name`, else the stub (`swingDocClub`). |
| `hasVideo` | bool | Any `streams[]` video element that has a `file`. An owed phone clip (origin only) does not count. |
| `thumbnailFile` | str | `thumbnail.file` as a bare file name; empty when there is no `thumbnail` block. |
| `score` | int | `analysis.score.overall` (/3), or the bare int (/2). |
| `dataWarning` / `dataWarningDetail` | bool / obj | The shot's data warning and the facts behind it — from the integrity blocks and the club/DTL facts. `{}` when there is none. |
| `metrics` | obj | `key → { label, value }`: each `analysis.metrics[]` entry's value at Impact, already formatted with its unit (`impactMetricsFromAnalysis`) — the carousel chips. A display string, not a number. |
| `rating` / `note` | int / str | From `review`. |
| `lmDeviceKind` | str | `launchMonitor.kind`. Empty when there is no launch monitor block. |
| `excluded` / `clubOverride` | bool / str | `review` annotations from the launch-monitor board. Added to /4 on 2026-10-13 without a bump. An older block without them is correct as it is. |
| `devices` | str[] | What recorded the swing: `faceOn`, `dtl`, `impact`, `camera`, `hackmotion`, `imu`, `lm:<kind>` (`swingDeviceTokens`). Added to /4 on 2026-10-20 without a bump. If a block lacks it, the reader derives it from the root chunk's `streams`. |
| `handedness` | str | `athlete.handedness`. Added to /4 on 2026-11-09 without a bump. If a block lacks it, it is derived the same way. |

The block is derived data. Analysis and research code should read the blocks it summarises, not `summary`. `swinglab_run` strips it from `result.json`.

### Legacy: `swing_summary.json` (JSON-era swings only)

A `swing.json` swing caches the same fields in `<swing>/swing_summary.json`. It uses the same schema id and the same `summaryJson()` spelling, plus a `source: { size, mtime_ms }` guard on the `swing.json`. The sidecar counts as fresh only when the schema matches, `devices` and `handedness` are both present, and size and mtime both match a fresh `stat()`. Otherwise it is re-derived and rewritten. It is a cache: always safe to delete. It is never written for a `.ppsw` swing. `SwingStore::save` and `convertDir` delete it along with the `swing.json`. It is never archived and never zipped.

## `archive` — an archived swing's stub

**Added 2026-09-23.** Archiving a session copies it to `<archiveRoot>/<athlete>/<session>/`, a plain folder rather than a zip (`session_archiver.h`). It checks every copy byte for byte, then thins each swing to a stub `swing.ppsw` (`SessionArchiver::archive`, `stubOf()`). A JSON-era swing is converted first. The stub keeps:

- every top-level block, including `summary`;
- in `analysis`, only `schema`, `tier`, `score`, `metrics`, `phases`, `segmentation` and `versions`;
- in each `streams[]` element, only `kind`, `alias`, `file`, `setup`, `device`, `origin`, `playback`, `encoded` and `clip`, so no frame stamps or samples.

It gains:

```json
"archive": { "location": "<archiveRoot>/<athlete>/<session>/swing_0001", "archivedAt": "2026-…Z" }
```

`location` is the swing's folder in the archive; `archivedAt` is ISO-8601 UTC. Only `swing.ppsw` and the thumbnail stay in the swing folder. The session folder gets an `archived.json` marker (see "Files beside the swings"). Restoring copies the full documents back over the stubs (so `archive` disappears) and re-applies any rating, note or club changed on the stub while the session was archived.

---

## `analysis` (`pinpoint.analysis/3`)

The analysed swing. Re-analysis rebuilds this whole block. Every sub-block is optional except `schema`, `tier`, `score`, `metrics` and `phases`, which are always written (a device-only swing has no `score`; see "Device-only swings"). Many blocks appear only when their stage ran or found something. Absent means "did not run" or "nothing to record", never zero.

```json
"analysis": {
  "schema": "pinpoint.analysis/3",
  "path":     { … },        // OPTIONAL: which way each decision went on this run
  "versions": { … },        // OPTIONAL: producer versions; they gate re-analysis reuse
  "tier": 0,
  "score":    { … },
  "metrics":  [ … ],
  "kinematicSequence": { … },        // OPTIONAL
  "gatesVersion": 2, "gates": [ … ], // OPTIONAL as a pair
  "phases":   [ … ],
  "segmentation": { … },  // OPTIONAL
  "pose2d":   { … },      // OPTIONAL: face-on pose
  "poseDtl":  { … },      // OPTIONAL: down-the-line pose, same shape as pose2d
  "club":     { … },      // OPTIONAL: face-on shaft track
  "clubDtl":  { … },      // OPTIONAL: down-the-line shaft track, pinpoint.clubDtl/1
  "club3d":   { … },      // OPTIONAL: fused 3-D shaft, pinpoint.club3d/1
  "ball":     { … },      // OPTIONAL: face-on ball track
  "impact":   { … },      // OPTIONAL: impact-camera track
  "skeleton3d": { … },    // OPTIONAL: fitted 3-D skeleton, pinpoint.skeleton3d/1
  "addressMarks": { … },  // OPTIONAL: body edges at address, pinpoint.addressMarks/2
  "dtlRuler": { … },      // OPTIONAL: cm per DTL pixel, pinpoint.dtlRuler/1
  "bindings": [ … ],      // OPTIONAL: IMU calibration snapshot
  "assessment": { … },    // OPTIONAL: wrist coach feed
  "filter":   { … },      // OPTIONAL: offline re-fusion diagnostic
  "timings":  { … }       // OPTIONAL: stage wall times
}
```

`tier` is a `ReconstructionTier`: `0` Angles2D · `1` Mono3DPlusImu · `2` Stereo3D · `3` ClubInstrumented. Only `0` and `1` are ever written: `1` when the shot had IMU streams, otherwise `0`. A device-only swing writes `0`. Nothing writes `2` or `3` yet.

**Write-time additions.** After `serializeAnalysis()` has run, `writeSwingJson()` does four things:

- appends the whole-shot gate records (`shot.*`);
- removes the pose tracks when pose saving is off;
- carries the `lm.*` rows across, and re-derives the rows computed from the launch-monitor reading;
- rebuilds the launch monitor's gate records (`lm.*`).

> **Two-camera blocks.** `poseDtl`, `clubDtl`, `club3d`, `skeleton3d`, `addressMarks` and `dtlRuler` come from the down-the-line (DTL) camera, or from both cameras together. A single-camera swing has none of the other five and none of their `analysis.versions` keys. `addressMarks` is the exception: it is also written on a face-on-only swing, with `dtl: {"found": false}`, and stamps `versions.addressMarks` there. Every block except `poseDtl` is written **whenever its stage ran, valid or not**. The block's own `valid` / `found` / `reason` keys say why there is no result. Each block except `poseDtl` also carries its own `schema` and `stageVersion`, so it can be read without the rest of the document. In `swing.ppsw`, `poseDtl`, `clubDtl`, `club3d` and `skeleton3d` are among the per-frame tracks that `SwingStore::loadAnalysis()` leaves out. A full document read returns them.

### `path` — how this run got its answer (optional)

Added 2026-11-06. This is telemetry for people: nothing reads it back to choose a path. `versions` says what produced a track. `path` says whether this run computed that track or loaded it, and why. It is written when either `pose` or `ladder` is known. Any string field that would be empty is left out.

```json
"path": { "origin": "reanalysis", "forced": false, "fullWindow": false, "overrides": [],
          "frames": { "faceOn": "raw", "dtl": "mp4" },
          "reuse": { "pose": "reused", "ball": "reused", "shaft": "version 11, want 12",
                     "ladder": "version 11, want 12", "poseDtl": "no down-the-line camera" },
          "pose": "recorded", "ladder": "vision", "witness": "traced", "trackerSpan": "bounded" }
```

| Field | Type | Notes |
|---|---|---|
| `origin` | str | `live` (the capture pipeline) · `reanalysis` (the app's re-analyse) · `replay` (SwingLab off disk, with no reuse gate). |
| `forced` | bool | SwingLab `--force-rerun`. |
| `fullWindow` | bool | The run analysed the whole capture window, not the swing span. |
| `overrides` | str[] | Tuning keys in effect. Empty in production. |
| `frames.faceOn` / `frames.dtl` | str | Where each camera's frames came from: `live`, `raw` (the `.raw` sidecar) or `mp4`. A camera with no frame source has no key. |
| `reuse` | obj | Re-analysis only. One key each for `pose`, `ball`, `shaft`, `ladder` and `poseDtl`. The value is `reused`, `forced`, or the first reason the recorded track was not reused, for example `no record`, `version 1, want 2`, `model changed`, `scope span, want full`, `tuning override`, `needs a reloaded pose`, `needs a reloaded ball` or `no down-the-line camera` (`swing_reanalyzer.cpp`). Omitted when empty. |
| `pose` | str | `recorded` (reloaded), `injected` (a pose file was handed in), or how PoseRunner picked its frames: `bounded`, `twoPass`, `twoPassWindow` or `window`. |
| `ladder` | str | Where the phase ladder came from: `recorded`, `sensors` (the IMU ladder), `vision`, `sensorClamp` (the IMU clamp fallback) or `none` (`seg_resolve.h`). |
| `witness` | str | `traced` or `flags`. Absent when no face-on witness was built. |
| `trackerSpan` | str | `bounded` (the swing span), `window` (the whole window) or `recorded` (a reloaded track). Absent when there is no shaft track. |

### `versions` — producer versions (optional)

Added 2026-09-09 for `pose`, `ball` and `shaft`. The block is written only when the face-on pose stage stamped a version (`stamped()` means `pose > 0`, `analysis_versions.h`). A swing with no face-on pose therefore has no `versions` at all. Absent on swings analysed before 2026-09-09 ⇒ one full re-run on the next re-analysis, which stamps it. Re-analysis reuses a recorded track only when its version matches the code that would run now (see `path.reuse`).

| Key | Shape | Block it stamps | Current code | Re-analysis |
|---|---|---|---|---|
| `pose` | `{code, model, scope}` | `pose2d` | 2 | `model` is the ViTPose file as `name@bytes`. `scope` is `span` or `full`, and a `full` record covers a `span` request. Reused when code, model and scope all match. |
| `ball` | `{code}` | `ball` | 2 | Reused only on top of a reused pose. `0` means the stage did not run. |
| `shaft` | `{code}` | `club` | 12 | Reused only on top of a reused ball, with a matching code and no tuning override. The phase ladder goes with it. `0` means the stage did not run. |
| `impact` | `{code}` | `impact` | 1 | Added 2026-09-15. Always present in the block. `0` means no impact camera ran. |
| `poseDtl` | `{code, model}` | `poseDtl` | 3 | Added 2026-09-21. Present only when the DTL pose ran. **Reused** when `code` and `model` match the code that would run now, no `pose.` / `ball.` / `shaft.addressScan` / `shaft.addressStride` override is set, the record holds a `poseDtl` block and the job has a DTL camera — independently of whether the face-on pose was reused. |
| `shaftDtl` | `{code}` | `clubDtl` | 6 | Added 2026-09-21. Present only when the stage ran. Never reused: the block is recomputed every time from the poses and face-on shaft. |
| `shaftFusion` | `{code}` | `club3d` | 9 | Added 2026-09-21. Present only when the stage ran. Never reused. |
| `skeleton3d` | `{code}` | `skeleton3d` | 17 | Added 2026-09-26. Present only when the stage ran. Never reused. |
| `addressMarks` | `{code}` | `addressMarks` | 4 | Added 2026-10-01. Present only when the stage ran. Never reused. |
| `dtlPosture` | `{code}` | `dtlRuler` (and the DTL posture series) | 2 | Added 2026-10-23. Present only when the stage ran. Never reused. The block's own key is named `dtlRuler`, but its version key is `dtlPosture`. |
| `shaftDirection` | `{code}` | the `shaftDirection` series (no block of its own) | 1 | Added 2026-11-12. Present only when `ShaftDirectionStage` ran (a DTL camera, and a fused shaft or a two-camera skeleton fit). Never reused: the series is recomputed from `club3d` and `skeleton3d` every time. |
| `skeleton3dBody` | `{code}` | the 3-D body series (no block of their own): `shoulderPlaneAngle3d`, `shoulderLineYaw`, the 3-D `leadKneeFlexion`, `trailKneeFlexion` and `spineSideBend`, `thighTurn3dLead`, `thighTurn3dTrail` | 3 | Added 2026-11-12. Present only when `Skeleton3dBodyStage` ran (a valid two-camera skeleton fit). Never reused: the series are recomputed from the fit every time. 2 (2026-11-13): the 3-D `shoulderPlaneAngle3d` is computed beside a chest sensor's, as its fallback; a swing with no chest sensor writes the same series. 3 (2026-11-13): a 3-D `trailKneeFlexion`, written only where there is no down-the-line one while the cameras are uncalibrated (every swing today; with calibrated cameras it would replace the down-the-line one where the fit reads the trail knee at address AND the top), and the 3-D `spineSideBend` is computed beside the trunk sensors', as its fallback. |
| `hipInternalRotation` | `{code}` | the `trailHipInternalRotation` / `leadHipInternalRotation` series (no block of their own) | 1 | Added 2026-11-12. Present only when `HipInternalRotationStage` ran (a `pelvisRotationSigned` from the pelvis sensor, `routeId` `pelvisImu`). Never reused: the series are recomputed from the pelvis series and the fit every time. |
| `shoulderTiltVsBend` | `{code}` | the `shoulderTiltVsBend` series (no block of its own) | 1 | Added 2026-11-13. Present only when the reading was written: the step after the trunk-sensor precedence in `runProfile` (`shoulder_tilt_vs_bend.h`) found a shoulder plane at the top and a forward bend at address. Not a stage. Never reused: it is the difference of two readings already in the document. |

The current codes are the `k*StageVersion` constants in `src/Analysis/analysis_versions.h`, and each bump has a comment there. When **Storage → Save pose keypoints** is off, `pose2d`, `poseDtl`, `versions.pose` and `versions.poseDtl` are all removed at write (since 2026-09-23). A version that promises a pose not in the file would send re-analysis looking for it.

### `score` (ScoreBreakdown, /3)

```json
"score": { "kind": "resemblance", "overall": 0, "pattern": "unknown", "blended": false,
           "resemblance": { "bowed": 12, "neutral": 40, "cupped": 8 },
           "interval": { "halfWidth": 9, "lo": 31, "hi": 49 } }
```

| Field | Type | Notes |
|---|---|---|
| `kind` | str | `resemblance` (the wrist estimand) or `adherence`. |
| `overall` | int 0–100 | Adherence: a weighted geometric mean. Resemblance: the largest resemblance value. |
| `resemblance` / `pattern` / `blended` | obj / str / bool | Resemblance only. Separate 0–100 values for `bowed`, `neutral` and `cupped`. They are **not** normalised to sum to 100. |
| `interval` | obj | `{halfWidth, lo, hi}`: the measurement uncertainty on `overall`. Written only once computed. |
| `perRegion` / `perPhase` | obj | **Added 2026-07-21.** Adherence contribution maps, for example `rotation` or `wrist` per region. Written only when not empty, so a resemblance score never has them. |

> **Version note.** In `pinpoint.analysis/2`, `score` was a bare integer (e.g. `"score": 6`). `/3` promotes it to this object (design §B.0a/§B.7). Readers accept both. A device-only swing has no `score`.

### `metrics[]`

One entry per metric the run produced. A curve is a per-frame series plus readings at phases. A scalar is a single reading.

```json
{ "key": "impactShaftLean", "label": "Shaft lean", "unit": "°",
  "t_us":  [2720, 9402, 16126, … ],
  "value": [26.0, 26.0, 26.0, … ],
  "phaseSamples": [ { "phase": 5, "t_us": 3479416, "value": 32.0, "band": "",
                      "sigma": 1.4, "sigmaKind": 1 } ],
  "sigma": 1.4, "sigmaKind": 1, "lineAngle": true }
```

| Field | Type | Notes |
|---|---|---|
| `key` / `label` / `unit` | str | Metric id, display label and unit. Always written. See **Which keys** below. |
| `t_us` / `value` | int[] / float[] | Parallel arrays. Times are window-relative. **Both are empty for a scalar metric**. See the rules below. |
| `phaseSamples[]` | obj[] | The value read at each phase: `phase` (a `Phase` int), `t_us`, `value` and `band`. `band` is `green`, `yellow` or `red` at a scored phase, otherwise `""`. Optional per reading (**added 2026-10-01**): `sigma` (1σ in the metric's unit), `sigmaKind` and `grossRisk` (0–1, the chance the reading rests on a gross error such as the tracker following the wrong structure; it is given beside σ, never folded into it). Each is written only when set (`uncertainty_json.h`). |
| `sigma` | float | Optional, added 2026-07-21. 1σ *measurement* uncertainty for the whole series, in the metric's own unit. **Absent means "not characterised", never "zero error".** It is separate from the coaching band: low confidence widens σ and never moves `value`. |
| `sigmaKind` | int | Optional, **added 2026-10-01**. What backs `sigma`: `1` Propagated (from calibrated inputs; no truth data of its own), `2` Calibrated (propagated, and its held-out coverage checked), `3` AssumedInput (one input was assumed rather than measured, so σ is wide on purpose). `0` Unspecified is never written. |
| `valid` | int[] | Optional, added 2026-09-04 (`metric_presentation_honesty.md` §5.1). Per-sample 0/1, parallel to `t_us`. `0` marks a sample bridged across a gated or absent stretch: the number in `value[i]` was filled in from its neighbours and is not a measurement. Skip it. Written only when at least one sample is `0`. No `phaseSamples` entry is ever taken at an invalid sample. |
| `routeId` / `routeDirect` | str / bool | Optional, **added 2026-10-15**. The producer route, using the catalogue's route ids (`pelvisImu`, `faceOn+dtl`, `faceOn`, …), and whether that route is Direct. Two peaks from different Estimated routes are on two different scales, so their ratio is not a reading. Written only when stated (`routeDirect` only when `true`). |
| `withheld` | str | Optional, **added 2026-10-15**. Drawn but not judged: the reason no diagnostic verdict may be drawn from this reading. The value and σ still stand and are charted. |
| `lineAngle` | bool | Optional, **added 2026-10-31** (issue #116). The series is the angle of a line, for example shaft lean. A single reading taken from it is folded into (−180°, 180°]. The curve itself is left unwrapped so it stays continuous through the top. |

Not written: the coaching band limits (`bandLo`/`bandHi`) and the stored-sign polarity flag.

**Which keys.** The metric catalogue is the source for every key PinPoint reads, with its label, unit, group, description and sign meaning. It is `src/Metrics/metric_catalogue_manifest.cpp`: one `cat.addDescriptor({…})` per metric, whose `.key` equals `metrics[].key`, plus `.unit`, `.group`, `.signPositive`, `.signNegative`, `.description` and `.howToRead`. To get the list:

- **As a table:** `docs/developer/metric_catalogue_developer_guide.md` §1A, "Every metric, and what its sign means". It is generated from the manifest by `python3 tools/metrics/gen_sign_table.py`, with columns key, unit, status (live / planned / device), what positive means and what negative means. Re-run the script first: the table goes stale whenever a descriptor changes.
- **In code:** `MetricCatalogue::all()` and `descriptor(key)` (`metric_catalogue.h`).
- **Sign frames** (target line, lead side, ISB): `docs/design/pinpoint_sign_conventions.md`.

No `--list-metrics` or JSON-dump tool exists.

**Rules a reader needs.**

1. **Two shapes, one contract.**
   - A *curve* has parallel `t_us`/`value` arrays plus `phaseSamples`.
   - A *scalar* has **empty** `t_us` and `value` and exactly one `phaseSamples` entry. That entry's `phase` says what the reading describes: `0` Address for setup readings such as stance and ball position; `5` Impact for whole-swing summaries (tempo) and every launch-monitor row; another phase where the reading belongs to it, for example `transitionPlaneDelta` at `3` Transition.
   - Loop over `t_us`/`value` and `phaseSamples` independently, and never assume the curve is not empty.
2. **Read the unit; never assume it.** Units are per key and come from the producer. A unit can change between versions. For example, `stanceWidth` has been `% shoulder width` since 2026-07-25, and the millimetre reading moved to its own key, `stanceWidthMm` (`foot_metrics.cpp`). Older files may carry `stanceWidth` in `mm` or `×frame`.
3. **Prefixes name the instrument.**
   - `lm.<key>`: the launch monitor said this. It is written by `updateLaunchMonitor`, sits beside our own estimate under the bare key, and is carried across re-analysis.
   - `hm.<key>`: a HackMotion wG3 measured this (`hm.leadWristFlexExt`, `hm.leadWristRadUln`, `hm.forearmRotation`; **added 2026-08-19**). It is written beside the bare key, never over it. `findSeriesByLadder` prefers it.
   - Values *we derive* from a launch-monitor reading take bare keys: `compoundMiss` (added 2026-08-12) and `lowPointFromAttack` (added 2026-10-21). They carry `sigma` with `sigmaKind: 3`.
4. **`sigma` absent = not characterised.** Many producers write σ now: club, body, kinematic, tempo, DTL and derived-LM. Never read a missing σ as zero.
5. **Expect keys you do not know.** A document can carry series with no catalogue descriptor. For example, `swingPlaneIotaBack` and `swingPlaneIotaDown` are kept for research only, and so are `thighTurn3dLead` and `thighTurn3dTrail` (each thigh's turn from address off the 3-D fit, lead-relative like `pelvisRotationSigned`, since 2026-11-12). Ignore unknown keys, and never assume a fixed key set.

### `kinematicSequence` — the ordered segment peaks (optional)

Added 2026-09-17 (`docs/design/kinematic_sequence_design.md`). The kinematic sequence over the four segment angular-speed series that also sit in `metrics[]` — `pelvisAngularSpeed`, `thoraxAngularSpeed`, `leadArmAngularSpeed`, `clubAngularSpeed` (unit `°/s`; the pelvis and thorax are SIGNED, opening toward the lead side positive; the arm and club are unsigned magnitudes of the long axis's swing about the swing-plane normal — see `pinpoint_sign_conventions.md`). **Absent when nothing produced a node** (no downswing ladder, no pose / shaft / segment IMU), so a swing without it serialises exactly as before the object existed. Written by `src/Analysis/kinematic_sequence_json.h`, the ONE helper the live `analysisDetail` map and the reload also go through.

> **Amended 2026-09-20.** `faceOn+dtl` is no longer planned: the uncalibrated face-on + down-the-line pair produces the **pelvis and thorax** nodes on a swing that has a down-the-line pose (`kinematic_sequence_design.md` §5.2, §13). Its `quality` is `estimated`. The two bound fields `peakNoEarlierThanMs` / `peakNoLaterThanMs` have been emitted since 2026-09-18, and the pair's `peakNoEarlierThanMs: 0` has a reading of its own.

```json
{
  "impactUs": 3479416,
  "nodes": [
    { "segment": "pelvis", "placed": true,  "tPeakUs": 3392000, "beforeImpactMs": 87.4,
      "peakDps": 463.0, "tSigmaMs": 11.0, "peakSigmaDps": 31.0, "routeId": "faceOn", "quality": "estimated" },
    { "segment": "club",   "placed": true,  "tPeakUs": 3475000, "beforeImpactMs": 4.4,
      "peakDps": 2110.0, "tSigmaMs": 3.0, "peakSigmaDps": 60.0, "routeId": "faceOnClub", "quality": "estimated" }
  ],
  "order": ["pelvis", "club"], "gapsMs": [83.0], "gainsDps": [1647.0],
  "orderResolved": true, "verdict": "partial", "routeSummary": "estimated", "pelvisDecelerates": true
}
```

| Field | Type | Notes |
|---|---|---|
| `impactUs` | int | The Impact instant the nodes are referenced to. **Window-relative**, like every other `analysis` `t_us`. |
| `nodes[]` | obj[] | One per PRODUCED segment, in pelvis → thorax → leadArm → club order. Segments no route produced are simply absent. |
| `nodes[].segment` | str | `pelvis` \| `thorax` \| `leadArm` \| `club`. |
| `nodes[].placed` | bool | `false` = the route produced a rate curve but its timing σ exceeded the placement threshold (`sequence.maxPlaceSigmaMs`); the timing fields then carry the attempt and the node is excluded from `order`. |
| `nodes[].tPeakUs` | int | Peak instant, **window-relative**. The only other instant (as opposed to a duration) in the object. |
| `nodes[].beforeImpactMs` | float | `impactUs − tPeakUs`, ms; positive = peaked before the ball. A duration, not re-timed. |
| `nodes[].peakDps` | float | Peak angular speed, °/s, in the segment's sign convention. |
| `nodes[].tSigmaMs` / `nodes[].peakSigmaDps` | float | 1σ on the peak instant and the peak value, propagated by `angular_rate.h`. |
| `nodes[].routeId` | str | `pelvisImu` \| `thoraxImu` \| `leadArmImus` \| `clubSensorFused` \| **`faceOn+dtl`** \| `faceOn` \| `faceOnClub`. |
| `nodes[].quality` | str | `direct` (an IMU) or `estimated` (a single face-on camera, or the uncalibrated face-on + down-the-line pair). |
| `nodes[].peakNoEarlierThanMs` | float | **Optional**, and only on an unplaced node. A lower bound on the peak, in ms **before impact**: "this segment had not peaked by *N* ms before the ball". A positive value is the face-on span rung's bound — the edge of the sighted band, "it peaked somewhere after this, where the camera stopped being able to see the segment turn". **`0` is the pair route's bound and means something different: the segment was watched all the way to impact and had not peaked when the club arrived.** Nothing went out of sight. |
| `nodes[].peakNoLaterThanMs` | float | **Optional**, and only on an unplaced node. The symmetric upper bound, for a peak at the far edge of a blind band (the rate was still falling when sight returned). |
| `nodes[].probable` | obj | **Optional, added 2026-10-25** (design §12.6). Only on an **unplaced** node whose curve the route lost for a short gap at impact. It holds the peak that gap hides, bridged by a Gaussian process. `placed` stays `false`, so `order`, `gapsMs` and `verdict` ignore it. Fields: `peakMs`, `tSigmaMs`, `peakDps`, `peakSigmaDps`, and `how` (`bridge` = the fit's peak inside the gap; `rising` / `fell` = the earliest / latest the peak can be; `seen` = no gap to bridge, the highest rate the camera saw). `bridge {tMs[], mean[], sd[]}` is the fit every ms across the gap, empty for `seen`. Optional `earlierMs` / `earlierDps` give a taller measured value earlier in the downswing. ⚠ **These times are ms from impact, + after**, the opposite sign to `beforeImpactMs` (`kinematic_sequence_json.h`, `kinematic_sequence.h`). |
| `order[]` | str[] | The PLACED segments, ascending `tPeakUs`. |
| `gapsMs[]` / `gainsDps[]` | float[] | Between adjacent entries of `order`: the timing gap and `peak(n+1) − peak(n)`. `order.length − 1` entries. |
| `orderResolved` | bool | Every adjacent gap exceeds `sequence.sigmaK · sqrt(σₙ² + σₙ₊₁²)`. When false the verdict is withheld. |
| `verdict` | str | `unresolved` \| `partial` \| `proximalToDistal` \| `armBeforeThorax` \| `other` — see `kinematic_sequence.h`. |
| `routeSummary` | str | `direct` \| `mixed` \| `estimated` over the placed nodes; `""` when none placed. |
| `pelvisDecelerates` | bool | **Optional** — present only when the pelvis node is placed: its peak sits before impact by more than its own σ. |

### `gates`, `gatesVersion` — why readings were refused, bounded or withheld (optional)

Since 2026-10-16, every analysis stamps `gatesVersion`, so the pair is present on every newly analysed swing. Device-only swings are stamped too. If both keys are absent, the swing was analysed before records existed, so its reasons are "not recorded". An empty `gates: []` means no gate fired. One record per gate firing (`src/Analysis/gate_record.h`; `docs/design/trust_and_gates_design.md` §4.2, §6).

`gatesVersion`: `1` = the kinematic sequence's gates only. `2` (current) = every per-swing gate.

Records come from three places, all merged into this one array:

- the stages;
- the whole-shot `shot.*` records, re-derived at every write from `captureIntegrity` / `imuIntegrity` (`manifest_gates.h`);
- the launch monitor's `lm.*` records, rebuilt at every write from the drop list in the `launchMonitor` block (`lm_gates.h`).

| Field | Type | Meaning |
|---|---|---|
| `gate` | str | Catalogue id such as `sequence.sightedBand`, `club.trackRefused` or `shot.captureHoles`. The catalogue is `src/Metrics/gate_catalogue.h` plus `gate_catalogue_{body,club,lm,rules,shot}.h`. |
| `subject` | str | What the gate acted on: `ks.pelvis`, `ks.thorax`, `ks.leadArm` or `ks.club`; `route.faceOn+dtl`; `track.club`, `track.clubDtl` or `track.impact`; a metric key (for example `pelvisRotation` or `ballPosition`); `shot`; or an `lm.` key. |
| `effect` | str | `refused` (no value) · `bounded` (a range instead) · `withheld` (shown, not judged) · `downgraded` (kept at lower trust) · `masked` (some samples out of sight; the reading survives). |
| `evidence` | obj | The numbers the gate compared, with the unit in the key name. Short codes, never prose. **Times are ms from impact, + after** — not window-relative, so a record needs no re-timing. A mask's evidence is `frames`, `ofFrames`, `frac`, `fromMs` and `toMs`. Omitted when empty. |
| `bound` | obj | Only when `effect` is `bounded`: `noEarlierThanMs` / `noLaterThanMs`, in ms **before** impact (the kinematic sequence's convention). Omitted when empty. |
| `blocks` | str[] | Reserved: what the record prevented downstream (design §4.4). Omitted when empty. |
| `covers` | str[] | **Added 2026-10-16.** A cascade is written as one record. A gate that refuses a whole track lists the metric keys it took down here, rather than writing one record per metric. To find the reasons for a metric, take the records whose `subject` is that key **or** whose `covers` contains it. Omitted when empty. |

The words are not stored. Every surface fills the catalogue's templates from `evidence`.

### `phases[]` — the phase ladder

```json
"phases": [ { "phase": 0,  "t_us": 2809565, "conf": 0.5, "segment": 0 },   // Address  (P1)
            { "phase": 1,  "t_us": 2909565, "conf": 0.5, "segment": 0 },   // Takeaway
            { "phase": 12, "t_us": 3010210, "conf": 0.8, "segment": 9 },   // P2, from the club
            { "phase": 2,  "t_us": 3238327, "conf": 0.5, "segment": 0 },   // Top      (P4)
            { "phase": 5,  "t_us": 3479416, "conf": 0.5, "segment": 0 },   // Impact   (P7)
            { "phase": 7,  "t_us": 3767410, "conf": 0.5, "segment": 0 } ]  // Finish   (P10)
```

| Field | Type | Notes |
|---|---|---|
| `phase` | int (`Phase`) | See below. |
| `t_us` | int µs | Window-relative event time. |
| `conf` | float 0–1 | Producer-local display confidence. Low values fade in the UI. **Do not compare `conf` across producers**: use `timing`. |
| `segment` | int (`SegmentRole`) | Provenance: the segment the event was measured from. `9` Club for events inserted from the club's positions; `0` Unknown for anchors such as Impact. |
| `timing` | int (`TimingClass`) | **Added 2026-08-19.** How the instant was obtained: `0` Measured · `1` Anchor (for example acoustic impact; never displaced) · `2` Proxy (from a stand-in) · `3` Fallback (a placeholder). **Written only when `segmentation.version ≥ 5`.** Absent means Measured. |

**`Phase`** (append-only), with the coaching P-number:

`0` Address P1 · `1` Takeaway · `2` Top P4 · `3` Transition · `4` Downswing · `5` Impact P7 · `6` Release · `7` Finish P10 · `8` MidBackswing P3 · `9` Delivery P6 · `10` MaxSpeed · `11` FollowThrough P9 · `12` ShaftParallelBack P2 · `13` ArmParallelDown P5 · `14` ShaftParallelThrough P8.

`4` and `6` are no longer emitted. The same instants go out as `13` and `14` (`phase_segmenter.cpp`). They can still appear in older files.

What a new ladder contains depends on the source (`path.ladder`):

| Source | Events |
|---|---|
| IMU (`sensors`) | Address, Takeaway, MidBackswing, Transition, Top, ArmParallelDown, Delivery, MaxSpeed, Impact, ShaftParallelThrough, FollowThrough, Finish. Each is included when found, and any event that breaks time order is dropped. `14` is a forearm proxy: `conf 0.35`, `timing` Proxy. |
| IMU clamp (`sensorClamp`) | Address (`conf 0.3`, Fallback), Impact (`1.0`, Anchor), Finish (`0.2`, Fallback) only. `segmentation.conf` is `0`. |
| Camera (`vision`) | The shaft tracker's Address, Takeaway, Top, Impact and Finish (Takeaway on by default), at the tracker's own `conf`. Then the **positions ladder** (on by default, `positions_ladder.h`) inserts the club's located P2/P3/P5/P6/P8 as `12`/`8`/`13`/`9`/`14` with `segment: 9`, where each lands strictly inside its anchor window. It abstains otherwise. |

Timeline fusion can then replace or insert a slot from the club track. When it does, `segmentation.version` becomes 5 and `fusion[]` records the decision.

A device-only swing has a single manufactured Impact event (see "Device-only swings").

### `segmentation` (optional)

```json
"segmentation": { "swingStartUs": 2709565, "swingEndUs": 3867410, "conf": 0.5, "version": 5,
                  "fusion": [ { "phase": 9, "winner": 9, "loser": 6, "dtUs": -4200, "reason": 3 } ] }
```

Consumers truncate to these swing bounds: the replay span, metric grids and the heavy-stage scan. Written only when `swingEndUs > swingStartUs`. An absent block means full-window bounds.

| Field | Type | Notes |
|---|---|---|
| `swingStartUs` / `swingEndUs` | int µs | Window-relative. Address − pad and Finish + pad, clamped to coverage. |
| `conf` | float | The minimum over Address, Top, Impact and Finish. `0` means "the bounds are just the window". |
| `version` | int | `2` base ladder · `3` the shaft refinement ran · `4` the positions ladder inserted club events · `5` the ladder was **fusion-arbitrated**. `phases[].timing` is written only at `5`. |
| `fusion[]` | obj[] | **Added 2026-08-19.** The audit trail of the arbitration: one entry per contested P-slot, **including slots that kept their event**. Present only when a fusion pass emitted something. `phase` is a `Phase` int (not a P-number). `winner` and `loser` are `SegmentRole` ints, with `loser: 0` for an uncontested insert. `dtUs` is winner − loser. `reason` is a `FusionReason`: `0` Inserted · `1` ClassBeat · `2` ClassHeld · `3` OwnerBeat · `4` OwnerHeld · `5` AnchorHeld · `6` TieHeld · `7` Disputed · `8` GuardWindow · `9` GuardNeighbour. |

### `pose2d`

Offline pose keypoints, normalized 0..1. **Widened 2026-07-13 (WB0, additive):** `kp` grew `float[51]` → `float[399]` — 133 COCO-WholeBody keypoints. Indices 0–16 are the **unchanged** COCO body joints (identical values to a pre-WB0 file); the tail is purely additive: **17–22 feet** (L bigtoe/smalltoe/heel, R bigtoe/smalltoe/heel), **23–90 face** (68-pt contour), **91–111 left hand** / **112–132 right hand** (21 each: wrist root, then 4 per finger thumb→pinky). `smoothed[].kp`/`tier`/`sigma` widened identically; new `keypointCount` field. Readers use bounded loops, so old 51-float files and new 399-float files load interchangeably (old files leave the tail defaulted).

The block is written by `poseTrackToJson` in `swing_doc.cpp`, the same serialiser `poseDtl` uses. When **Save pose keypoints** is off, `pose2d` is left out of the file, along with `versions.pose` (see `versions`).

```json
"pose2d": {
  "camera": 0,
  "keypointCount": 133,
  "decode": "dark",                                                   // OPTIONAL — WB1 provenance
  "cropRect": { "x": 0.34, "y": 0.11, "w": 0.33, "h": 0.78 },         // OPTIONAL — WB1 person crop
  "frames": [ {
    "t_us": 2720,
    "kp":   [0.5469, 0.2930, 0.9349,  0.5573, 0.2852, 0.9591,  … ],   // 133×(x,y,conf) = 399
    "lead":  [0.5599, 0.6012], "trail": [0.5399, 0.6031],
    "handConf": 0.7535
  }, … ],
  "smoothed": [ {                                                     // OPTIONAL — motion-overlay track
    "t_us": 2720,
    "kp":    [0.5471, 0.2931, 0.9349,  0.5574, 0.2853, 0.9591,  … ],  // 133×(x,y,conf) = 399
    "tier":  [2, 2, 2,  1, 0,  … ],                                   // PoseTier per kp (133)
    "sigma": [1.8, 1.8, 2.4,  3.1, 0.0,  … ]                          // posterior σ px (133)
  }, … ],
  "synth": [ { "t_us": 2720, "kp": [ … ] }, … ],                      // OPTIONAL — 240 Hz display upsample
  "adaptFallbacks": 2                                                 // OPTIONAL — only when > 0
}
```

| Field | Type | Notes |
|---|---|---|
| `camera` | int | Source id of the pose camera. |
| `keypointCount` | int | **Added 2026-07-13 (WB0).** Explicit `kp` width: `133` (COCO-WholeBody). Absent on older files ⇒ `17`. Provenance, not a parse contract — readers stay bounded-loop. |
| `decode` | string | **Optional — added 2026-07-13 (WB1).** Sub-pixel decode used for the offline pose pass: `"dark"` (DARK distribution-aware refinement). Written **only when non-legacy** — an argmax (`±0.25`-shift) run **omits** the field, so absent ⇒ `"argmax"`. Provenance only. |
| `cropRect` | obj | **Optional — added 2026-07-13 (WB1).** The swing-level person crop the pose pass ran inference on, `{x, y, w, h}` in **full-frame normalized** coords. **Absent** ⇒ the full-frame fallback ran (bbox too sparse / no resolution gain / crop disabled). Provenance/inspection only — all persisted `kp` are already full-frame normalized (back-projected through this crop). |
| `frames[].t_us` | int µs | Window-relative; pose is adaptively sampled (dense near impact), so frames < camera frames. |
| `frames[].kp` | float[399] | 133 COCO-WholeBody keypoints as flat `[x, y, conf] × 133`, normalized. Indices 0–16 = the unchanged COCO body joints; tail = feet/face/hands per the layout above. `float[51]` (17 kp) before 2026-07-13. |
| `frames[].lead` / `trail` | float[2] | Lead/trail hand centroids (COCO wrists on fallback), normalized. Unchanged by WB0 — still the derived grip anchors all consumers use. |
| `frames[].handConf` | float | 0 when wrist-fallback. |
| `smoothed[]` | obj[] | **Optional — added 2026-07-12** (motion-overlay fan/trace track). De-jittered companion to `frames`, produced by one offline RTS pass (`pose_smoother.{h,cpp}`, a per-axis 3-state `[p,v,a]` KF) in `PoseSmoothStage`. Written **only when non-empty** (absent on swings analysed before the smoother existed ⇒ old swings must be re-analysed to gain fan/trace; `frame`-mode overlays fall back to raw `frames`). Parallel to `frames` on the **same `t_us` grid**; **no `lead`/`trail`/`handConf`** — the hands are not smoothed. |
| `smoothed[].t_us` | int µs | Window-relative (matches the aligned `frames` entry). |
| `smoothed[].kp` | float[399] | Smoothed 133 COCO-WholeBody keypoints, flat `[x, y, conf] × 133`, normalized (widened from `[51]` 2026-07-13). `conf` carries the overlay **render-alpha** contract, not the raw detector score. |
| `smoothed[].tier` | int[133] | Per-keypoint honesty `PoseTier`: **`0` Off** (raw passthrough — don't paint as confident) · **`1` Pred** (coasted/bridged estimate) · **`2` Meas** (measured & smoothed). Widened from `[17]` 2026-07-13. |
| `smoothed[].sigma` | float[133] | Per-keypoint posterior σ in **pixels**; `0` ⇒ no smoothed value for that keypoint that frame (the `kp` entry is a raw passthrough). Widened from `[17]` 2026-07-13. |
| `synth[]` | obj[] | **Optional, added 2026-07-16.** A dense visualisation-only C¹ upsample of `smoothed` onto a fixed 240 Hz grid, so the replay skeleton scrubs smoothly. Each entry is `{t_us, kp[399]}`, with no `tier`, `sigma` or hands. Joints with `conf 0` are off. **Metrics never read it.** Written only when not empty. |
| `adaptFallbacks` | int | **Optional — added 2026-09-05 (phase 5, motion-adaptive smoother window `poseSmooth.adapt.*`).** Count of keypoints whose adaptive second pass was **rejected** by the divergence guard — it would have changed the segmentation (`accepted[]`/`hasSmoothed[]`), so that keypoint kept its **unadapted** output. Written **only when > 0**, so any run where nothing fell back serialises **byte-identically** (nothing did on the 11 swings the window was promoted on; `poseSmooth.adapt.mode=off` is the parity switch). Diagnostic only — nothing reads it back; it exists so a parameter sweep can refuse a setting that would have removed a sample. |

### `poseDtl` — the down-the-line pose

Added 2026-09-21. The DTL camera's 2-D pose track. Its shape is **exactly `pose2d`'s**: both come from the same serialiser, `poseTrackToJson()`. That means the same `camera` / `keypointCount` / `decode` / `cropRect` / `frames[]` / `smoothed[]` / `synth[]` / `adaptFallbacks` keys, 133 COCO-WholeBody keypoints normalised 0..1 to the **DTL** frame, and window-relative `t_us`. It is produced by `DtlPoseStage` (`src/Analysis/analysis_pipeline.cpp`) and is **absent when no DTL pose ran**: no DTL camera, the stream has no frames in the window, there is no face-on pose to pair with, or both of its consumers are switched off. When **Save pose keypoints** is off it is dropped with `versions.poseDtl`.

Snippet from `/mnt/swingdata/Mark-Liversedge/2026-10-07_Mark-Liversedge_Wrist_01/swing_0002`, a face-on plus down-the-line swing on the current stage versions (poseDtl 3, shaftDtl 6, shaftFusion 9, skeleton3d 17, addressMarks 4, dtlPosture 2); the snippets for the other two-camera blocks come from the same swing. Long arrays are cut short (`…`).

```json
"poseDtl": {
  "camera": 0, "keypointCount": 133, "decode": "dark",
  "cropRect": { "x": 0.0, "y": 0.1147, "w": 0.7737, "h": 0.8069 },
  "frames":   [ { "t_us": 459153, "kp": [0.4381, 0.3353, 0.9314, … ],   // 133×(x,y,conf)
                  "lead": [0.3053, 0.6057], "trail": [0.3057, 0.6074], "handConf": 0.8626 }, … ],  // 231 frames
  "smoothed": [ { "t_us": 459153, "kp": [ … ], "tier": [2, 2, 2, … ], "sigma": [8.71, 8.69, … ] }, … ],
  "synth":    [ { "t_us": 459153, "kp": [ … ] }, … ]                        // 240 Hz, 660 entries
}
```

How it differs from `pose2d`:

| | `pose2d` (face-on) | `poseDtl` |
|---|---|---|
| `camera` | The face-on SourceId. | The DTL SourceId (`ctx.job.dtlSource`). This is a runtime id, **not** a `streams[]` index: on the swing above it is `0` while `pose2d.camera` is `1`. To find the stream, use `clubDtl.stream.alias`, or `setup.perspective` 1 (DownTheLine). |
| Time base | Its own two-pass span. | **Inherited, never re-discovered.** `twoPass` is false, and the scan runs from swing span −1 s to +0.3 s, or from impact −2.5 s to +0.8 s when there is no span. |
| Frame schedule | Adaptive stride. | **Schedule B** (on by default, `pose.dtlSchedule`): every frame from P2 − 50 ms to P8 + 150 ms; every 2nd frame from P1 − 100 ms to P2 − 50 ms; every 4th frame elsewhere (`src/Pose/pose_schedule.h`). Missing P2 falls back to P1 + 250 ms, and missing P8 to impact + 150 ms. With no P1 the schedule is refused and every frame is posed. On the swing above the gaps between frames are 6.7 ms, 13.4 ms and 26.8 ms at 149 fps: 231 frames instead of about 420. The P-instants come from the face-on ladder. |
| Smoothing | `PoseSmoothStage`, which also rewrites the face-on grip anchors. | Smoothed in its own stage with the same keys, but `lead` / `trail` stay the **raw** frames'. |

`frames[].t_us` stays window-relative on the shared clock. A DTL instant matches a face-on instant only after adding `clubDtl.clockOffsetUs`, which is `0` on a shared clock.

Readers. The DTL replay tile, the report stills and the delivery picture read `frames` (and `smoothed` where present) (`src/Gui/shot/disk_replay_source.cpp`, `src/Gui/report/report_stills_pure.cpp`, `src/Gui/charts/delivery_picture.cpp`). Re-analysis reloads it by passing the block to `PoseRunner::fromJsonObject` as if it were `pose2d`, then overwrites `camera` with the live DTL SourceId and re-runs the smoother. Every reader is bounded-loop, as for `pose2d`.

### `club` — the face-on shaft track (`ShaftTrack2D`)

The block is written when the track is **valid**, and also when it was **refused** (`valid: false` plus `refused`). A refused track keeps its samples for the lab. A track that only failed the coverage gate is still omitted. **Consumers must check `valid`**: a refused track draws nothing, and every club metric shows "-". `grip` and `head` are normalised by `frameWidth` / `frameHeight`.

```json
"club": {
  "camera": 0, "valid": true, "coverage": 0.910, "frameWidth": 1280, "frameHeight": 1024,
  "imuVisionCorr": 0, "modelVisionResidualDeg": -1, "measuredClubLenPx": 361.2,
  "lengths": {
    "ballPx": 361.2, "bandPx": 377.8, "headP95Px": -1, "posePx": 248.5, "priorPx": 372.4,
    "fusedPx": 371.9, "fusedSigmaPx": 9.8, "fusedConf": 0.71,
    "fusedInstantPx": 370.2, "fusedInstantConf": 0.63,
    "ladderRung": 0, "ladderLenPx": 371.9, "nEstimators": 2, "priorN": 5, "headMeasN": 0
  },
  "plane": { … }, "diag": { … },
  "samples": [ { "t_us": 2720, "grip": [0.5499, 0.6021], "head": [0.4421, 0.8784],
                 "theta": 2.0246, "thetaDot": 0.0, "lenPx": 0, "conf": 0.30,
                 "headConf": 0.0, "headSigma": -1, "flags": 20, "sigTheta": 2.1, "pGross": 0.02, "tier": 0 }, … ],
  "predicted": [],
  "positions": [ … ], "synth": [ … ], "wedgeObs": [ … ],
  "addressBall": [0.548, 0.807], "addressBallSigmaPx": 1.2, "synthKappa": 1.6,
  "filter": { … }
}
```

| Field | Type | Notes |
|---|---|---|
| `camera` | int | SourceId of the face-on camera. |
| `valid` | bool | The coverage gate, and `false` on a refused track. Consumers must check it before using the track. |
| `refused` | str | **Added 2026-09-30.** Present only on a refused track. The tracker's own witnesses contradicted it: `p1BallConflict` (P1 disagrees with the ball direction) · `phaseSuspect` (a manufactured-address signature that no retry cleared) · `lengthConflict` (the address club length disagrees with grip→ball) · `handsUnusable` (the pose's hands were inconsistent on more than half the frames). It also raises the shot's data warning. |
| `coverage` | float | Fraction of span frames with a direct measurement. |
| `frameWidth` / `frameHeight` | int | Camera dimensions. Multiply `grip` and `head` by these to get pixels. |
| `imuVisionCorr` | float | Always `0` since the vision-only v3 tracker. Kept for old readers. |
| `modelVisionResidualDeg` | float | `-1` = unset. |
| `measuredClubLenPx` | float | Added 2026-07-08. Grip→ball at address in px, used as a scale floor. `-1` = no ball anchor. |
| `samples[]` | obj[] | The per-frame track: `t_us`, `grip`, `head`, `theta` (rad, grip→head, image atan2 convention), `thetaDot` (rad/s), `lenPx`, `conf`, `headConf` / `headSigma` (`-1` = head pass off), `flags`. Optional per-sample fields: `lineConf` (`-1` = absent) and, **added 2026-10-01**, `sigTheta` (1σ of θ, degrees), `pGross` (chance of a gross error) and `tier` (the tier that earned θ: `0` pred · `1` ray · `2` band · `3` recon · `4` wedge · `5` seg). Each is written only when assessed (`uncertainty_json.h`). |
| `predicted[]` | obj[] | Always `[]`. The v3 tracker has no kinematic-model series. |
| `synth[]` | obj[] | Layer C visualisation series between P-anchors, every entry flagged `0x100`. Same fields as `samples` except `lineConf`, and including the optional σ fields. Excluded from scoring and estimands, **but the club-path metrics read it on purpose** (clubhead speed, hand speed, lag, attack angle, low point, club angular speed). Written only when not empty. |
| `positions[]` | obj[] | Coaching P-positions: `p` is **1–8, or 10 (Finish)**; P9 is reserved. Each has `t_us`, `grip` and `head` (normalised here), `theta`, `lenPx`, `conf`, `sigmaThetaDeg` and `sigmaLenPx` (`-1` = not fitted), `stackN`, and `source` (`0` TrackSample · `1` MilestoneFit). Since **2026-09-17** each also has `timing` (a `TimingClass`: `0` Measured when the crossing sat on a real vision sample, `2` Proxy otherwise). Since **2026-10-01** each has an optional `sigmaTUs` (1σ of `t_us`, µs). P2/P6/P8 are image-plane shaft-parallel crossings; P3/P5 are lead-arm-parallel. Written only when not empty. |
| `lengths` | obj | Multi-estimator club-length fusion (`club_length_fusion.h`), added 2026-07-10. Component estimates in px (`ballPx` grip→ball, `bandPx` band-scale, `headP95Px` Stage-2 head p95, `posePx` pose rung — sanity bound only, never fused; `-1` = estimator absent); `priorPx` = recorded prior; `fusedPx`/`fusedSigmaPx`/`fusedConf` = with-prior posterior; `fusedInstantPx`/`fusedInstantConf` = prior-free variant (the only value folded back into the prior — no self-reinforcement); `ladderRung`/`ladderLenPx` = what the length ladder actually used (rung 0 = fused); `nEstimators`/`priorN`/`headMeasN` = support counts. **Always written** inside the block (parity writers keep it even on abstain, `fusedPx < 0` ⇒ absent). |
| `plane` | obj | **Added 2026-08-11.** The face-on swing-plane transition delta. Always written, even when nothing fitted. `valid`, `channel` (`-1` none · `0` measured · `1` synth), and one object each for `measured` and `synth` with: `fitted`, `iotaBackDeg` / `iotaDownDeg` (larger = flatter), `deltaDeg` (positive = steepened in transition), `nodeBackDeg` / `nodeDownDeg`, `nBack` / `nDown`, `conicResidBack` / `conicResidDown`, `ratioBack` / `ratioDown`, `splitHalfBackDeg` / `splitHalfDownDeg` (**measured channel only**), `anchorsBack` / `anchorsDown` and `anchorConfMin` (**synth channel only**), and `rejectBack` / `rejectDown` (`ConicReject`: `0` None … `8` IllConditioned, `shaft_plane.h`). Optional, added 2026-10-01: `sigmaIotaBackDeg`, `sigmaIotaDownDeg`, `sigmaDeltaDeg` and `pNeedle`. Never compare quality across the two channels. |
| `diag` | obj | **Added 2026-09-30.** Tracker self-checks: `onsetRule` (`0` walk-back · `1` top-repair reseed · `2` near-edge pin · `3` far-edge pin, `shaft_track_assembly.h`), `phaseRetries`, `phaseSuspect`, `p1BallDeltaDeg`, `lenBallRatio` (`-1` = not computable), `onsetTUs` / `topTUs` (window-relative; `-1` = unset), `handPairFixed`, `handGlitchFixed`, `ballSuspect` and `handsRetried`. |
| `wedgeObs[]` | arr[] | **Added 2026-09-29.** Timed edge readings off motion-blurred frames, as `[t_us, theta, kind, sigmaDeg]`. `kind` is `0` trail (exposure start) · `1` mid · `2` lead (exposure end, the frame time). Written only when not empty. |
| `addressBall` | float[2] | **Added 2026-09-29.** The address ball, normalised. Present only when the ball anchored impact: P7 is then the line from the hands to it. |
| `addressBallSigmaPx` / `synthKappa` | float | **Added 2026-10-01.** 1σ of `addressBall` in px, and the synth posterior's reading-weight inflation. Each written only when assessed. |
| `filter` | obj | **Added 2026-10-24.** The club rate filter's diagnostic, for developers only. Written only when the filter ran. Fields: `version`, `nEvidence`, `nGated`, `nDownweighted`, `nisMean` (about 1 when σθ is honest), `residualRmsDeg`, `rateWobble`, `humpsRaw` and `humpsFiltered` (`club_rate_filter.h`). |

**`ShaftSampleFlags`** (bitwise, `swing_analysis.h`):

| Bit | Name | Notes |
|---|---|---|
| `0x01` | Measured | |
| `0x02` | ImuBridged | No producer since v3; still read. |
| `0x04` | Coasted | |
| `0x08` | Wedge | Never co-set with Measured. |
| `0x10` | HeadProjected | |
| `0x20` | KinematicPredicted | No producer in v3. |
| `0x40` | BallAnchored | |
| `0x80` | HeadOffFrame | |
| `0x100` | Synthesized | `synth[]` only. |
| `0x200` | **Implausible** | Added 2026-09-17. The follow-through plausibility pass demoted this sample. Always co-set with Coasted and HeadProjected. |
| `0x400` | **BallSeen** | Added 2026-09-30. The ball detector had the ball on this frame near the address hold. BallAnchored implies BallSeen. |
| `0x800` | **RateFiltered** | Added 2026-10-24. The club rate filter set this synth tick's θ and θ̇. Synth ticks only. |

E.g. `20` = `0x14` = HeadProjected|Coasted (a `pred`-tier frame); `17` = `0x11` = HeadProjected|Measured (a `ray`-tier frame); `1` = Measured with a real head (a `band`-tier frame). JSON has always carried `flags` as an int, so widening the C++ enum never broke a reader.

### `clubDtl` — the down-the-line shaft track (`pinpoint.clubDtl/1`)

Added 2026-09-21. The shaft as the DTL camera sees it, one entry per DTL pose frame in the swing span, **absences included**. It is produced by `DtlShaftStage` (`analysis_pipeline.cpp`) and written by `dtlShaftTrackToJson()` (`src/Club/dtl_shaft_json.h`). This is the one builder SwingLab also uses for `club_dtl.json`, so the two files have the same bytes. Written **whenever the stage ran** (`versions.shaftDtl > 0`), valid or not. Absent when it did not run: no DTL pose, no valid face-on shaft track, or `shaft.dtl.enabled` off.

There is **no `valid` key**: the in-memory track's validity flag is not persisted. Readers judge the track from its per-frame `tier`. Absent numbers are JSON **`null`**, never `0` or `−1`.

```json
"clubDtl": {
  "schema": "pinpoint.clubDtl/1", "stageVersion": 6,
  "stream": { "alias": "DTL", "file": "DTL.mp4" },
  "frameWidth": 654, "frameHeight": 1020, "clockOffsetUs": 0,
  "config": { "enabled": true, "heldEnabled": true, … },          // 70 resolved tuning scalars
  "frames": [
    { "t_us": 1135719, "tier": "RAY", "grip": [0.3323, 0.6131], "head": [0.5682, 0.8805],
      "theta": 1.0559, "lenPx": 313.3, "lenSrc": "schedule", "runPx": 328, "evSrc": "ev",
      "conf": 0.55, "rhoPred": 0.9935, "rhoSrc": "bound", "corridor": null, "escape": false,
      "band": 0, "reason": "", "sigTheta": 0.50, "pGross": 0.0012 },
    { "t_us": 1698398, "tier": "END_ON", "grip": [ … ], "head": null, "theta": null, "lenPx": null,
      "band": -1, "reason": "end-on ρ̂=0.49", … },
    { "t_us": 1885951, "tier": "HELD", "corridor": { "centres": [-2.279, -0.863], "half": 0.436, "on": true },
      "band": 1, "reason": "held: 3-frame hole between measured frames (was: no evidence …)", … }
  ],
  "bands": [ { "lo_us": 867765, "hi_us": 1691698, "name": "addr→P1.7", "edge": false }, … ],
  "truth": [],
  "summary": {
    "sightedFrac": 0.754, "coverageByBand": { "addr→P1.7": 1.0, "P2.3→P5.8": 0.396, … },
    "publishedInEndOn": 0,
    "ball": { "found": true, "x": 391.7, "y": 882.6, "source": "contrast", "radiusPx": 9.02,
              "reason": "contrast cue: …", "shadowX": null, "shadowY": null, "launchRise": null, "nCandidates": 0 },
    "lFullPx": 315.3, "lFullSource": "ball", "rowFitA": 1.3165, "rowFitB": -230.76,
    "clubAwayWindow": "P2P5", "configHash": "c7389e81d3645723",
    "continuous": { "held": 12, "heldByBand": { … }, "coverageDrawnByBand": { … },
                    "lateEscapesRefused": 0, "endOnBeforeQuarantine": 5,
                    "occludedWrist": 18, "occludedRow": 67, "edgeBands": 0 },
    "topOutOfView": { "flag": false, "handsFromTopPx": 179.2, "clubPx": 315.3, "measuredFrames": 2 },
    "synth3d": { "n": 149, "preview": true,
                 "camera": { "calibrated": false, "yawDeg": 0, "pitchDeg": 0, "rollDeg": 0 },
                 "note": "PREVIEW: projected through the assumed-zero DTL camera; …" }
  },
  "synth3d": [ { "t_us": 1564428, "grip": [0.3361, 0.6137], "head": [0.5447, 0.8666], "theta": 1.0843,
                 "lenPx": 291.8, "rhoD": 0.9253, "u": [-0.379, -0.433, -0.818], "plane": 2,
                 "anchorSrc": 1, "etaDeg": 6.70, "flags": "synthesized" }, … ]
}
```

| Field | Type | Notes |
|---|---|---|
| `stream.alias` / `stream.file` | str | The DTL stream as `streams[]` names it, matched on the recorded serial. Empty when no serial was recorded. |
| `frameWidth` / `frameHeight` | int | DTL frame size. Multiply `grip` / `head` by these to get pixels. |
| `clockOffsetUs` | num µs | DTL `t_us` − face-on `t_us`, `0` on a shared clock. Add it to a face-on instant to land on the DTL clock. |
| `config` | obj | The resolved DTL tracker settings the track ran under (`src/Club/dtl_shaft_json.cpp`), hashed into `summary.configHash`. |
| `frames[].t_us` | int µs | Window-relative. |
| `frames[].tier` | str | `UNSEEN` · `END_ON` (the shaft points at the camera, ρ̂ too short to see) · `OCCLUDED` · `OCCLUDED_WRIST` · `OCCLUDED_ROW` (anchor quarantined: no grip / no confident wrist / disagrees with face-on on the image row) · `RAY` · `SEG` · `BAND` (**measured**) · `HELD` (drawn inside a band between two measurements, not measured) (`src/Club/dtl_shaft_track.h`). Fusion, the P-position borrow and every "published" count read only `RAY` / `SEG` / `BAND`. |
| `frames[].grip` / `head` | float[2] \| null | Normalised to the DTL frame (x / width, y / height). `grip` is the DTL pose anchor. `head` is `null` when nothing was measured. |
| `frames[].theta` | float rad \| null | Image angle grip→head, atan2 convention (y down), **not unwrapped**. |
| `frames[].lenPx` | float px \| null | The **drawn** length along θ. `lenSrc` says where it came from: `rend` · `snapLine` · `latBand` (measured runs) · `schedule` (the visibility law's ρ̂·L̂, one length per band). When `lenSrc` is `schedule`, the measured run is in `runPx`. |
| `frames[].evSrc` | str | Which gate let a `RAY` frame publish: `ev` (evidence along the pose-grip ray) or `lineConf` (support under the snapped line). |
| `frames[].conf` | float 0–1 | Per-frame confidence. |
| `frames[].rhoPred` / `rhoSrc` | float \| null, str | The predicted visible fraction ρ̂_D = √(1 − u_x²) from the face-on witness. `rhoSrc`: `measured` (face-on length on this frame) · `bound` (ρ_F := 1, used at address and impact) · `none`. |
| `frames[].corridor` | obj \| null | The face-on corridor on this frame: `centres` [2] and `half`, in **radians**, plus `on`. `null` when it was never set. |
| `frames[].escape` | bool | Clean evidence won outside the corridor. |
| `frames[].band` | int | Index into `bands[]`. `−1` means the frame is in a gap. |
| `frames[].reason` | str | Why an absence is an absence. `""` when the frame published. |
| `frames[].sigTheta` / `pGross` | float | **Optional.** 1σ of θ in **degrees**, and the probability of a gross error. Written only where they were assessed. |
| `bands[]` | obj[] | Maximal runs of solvable frames, each solved independently: `lo_us` / `hi_us` (window-relative), `name` (for reports, never gated on), and `edge` (admitted by the band-edge rule; present only when the continuous-track rules ran). |
| `truth` | [] | Always empty in the document. DTL truth is a separate instrument. |
| `summary.sightedFrac` | float | Fraction of the swing span that was sighted at all. This is the denominator behind `coverageByBand`. |
| `summary.coverageByBand` | obj | Measured fraction per band name. `continuous.coverageDrawnByBand` is measured + held, never folded into this one. |
| `summary.publishedInEndOn` | int | Frames published where the schedule said end-on. Should be `0`. |
| `summary.ball` | obj | The DTL ball in **pixels** (not normalised). `source` is `bright` · `shadow` · `contrast` · `none`. The shadow cue's numbers are always echoed. |
| `summary.lFullPx` / `lFullSource` | float px, str | The full club length in this view, L̂_D, and where it came from: `ball` · `faceOnRowScale` · `none`. |
| `summary.rowFitA` / `rowFitB` | float | The fitted cross-view row scale and offset the quarantine checked against. |
| `summary.clubAwayWindow` | str | Which rule placed the club-away window: `P2P5` · `P4P7` · `P4+120` · `P1impact` · `none`. |
| `summary.continuous` | obj | **Optional**, present when any continuous-track rule was on. Counts only. |
| `summary.topOutOfView` | obj | **Optional**, present only when the face-on ladder had a P4. `flag` true means nothing was measured at P4 ± 80 ms **and** the hands were less than one club length below the top edge. The shot card's ⚠ reads it. |
| `synth3d[]` + `summary.synth3d` | obj[], obj | **Optional.** The fused 3-D shaft projected into the DTL tile. **Synthesised, never a measurement**, and read by no metric. `u` is butt→head in the `club3d` frame. `plane`: `0` none · `1` address · `2` back · `3` down · `4` down extrapolated. `anchorSrc`: `0` none · `1` tracker · `2` skeleton (`src/Club/dtl_shaft_synth3d.h`). `etaDeg` appears only where an η curve was applied. `summary.synth3d.preview` true means it was projected through the assumed-zero DTL camera, so the heading is off by the unknown yaw. |

Readers. `dtlOverlayDetail()` (`src/Gui/shot/dtl_overlay_payload.cpp`) needs `frames` non-empty and `frameWidth` / `frameHeight` > 0, otherwise it draws nothing. It keeps only drawn frames (`RAY` / `SEG` / `BAND` / `HELD`) whose `grip` and `head` are both `[x, y]` pairs. A `null` `theta` / `lenPx` / `conf` falls back to `0` / `−1` / `0`. It borrows a face-on P-position only onto a **measured** sample within one frame interval. Inside the analysis, `club3d`, `skeleton3d`, the DTL posture ruler and the shaft-lie stage read the in-memory track, not this block.

### `club3d` — the fused 3-D shaft (`pinpoint.club3d/1`)

Added 2026-09-21. The shaft direction in three dimensions. Each fused frame intersects two planes: the face-on view ray with the face-on image angle, and the DTL view ray with the DTL image angle. No projected length is needed (`src/Club/shaft_fusion.h`). Produced by `ShaftFusionStage` (`analysis_pipeline.cpp`) and written by `shaftTrack3dToJson()` (`src/Club/shaft_fusion_json.h`). Written **whenever the stage ran** (`versions.shaftFusion > 0`), with `valid` saying whether a track came out. Absent when the stage did not run: fusion off, no valid face-on track, no valid DTL track, or no Top or Impact.

**Coordinate frame: the cameras', not the golfer's** (echoed in `frame`). **+X** is face-on image-right, **+Y** is the face-on view ray (from the camera toward the golfer), **+Z** is up. This is right-handed. The DTL camera looks along +X, yawed about Z by `yawDeg` (+ = from +X toward +Y) and pitched down by `pitchDeg`. For a right-hander +X is the target. All directions are unit vectors, butt→head. NaN is written as `null`.

> ⚠ **Uncalibrated unless `camera.calibrated` is true.** With no measured calibration on the job, the DTL camera is the assumed-zero placement: yaw = pitch = roll = 0 and offset 0. A plane's **inclination** is robust to this (≤ 1° over ±15° of yaw). Its **heading** (`azimDeg`) moves one-for-one with the unknown yaw. Treat inclination as a measurement, and heading as only as good as `yawDeg`.

```json
"club3d": {
  "schema": "pinpoint.club3d/1", "stageVersion": 9, "valid": true,
  "frame": "cameras: X face-on image-right, Y face-on view ray, Z up",
  "camera": { "dtlYawDeg": 0, "dtlPitchDeg": 0, "calibrated": false,
              "yawDeg": 0, "pitchDeg": 0, "rollDeg": 0, "offset": [0, 0, 0] },
  "address": { "inclDeg": 60.0, "n": 103, "inclSigmaDeg": 0.5 },
  "deliveryVsAddressDeg": null,
  "planes": {
    "back": { "fitted": true, "n": 47, "offered": false, "normal": [-0.2191, -0.8789, 0.4238],
              "inclDeg": 64.93, "azimDeg": -104.0, "oopRmsDeg": 9.74, "oopP90Deg": 15.72,
              "foRatio": 0.8789, "foNodeDeg": 152.66, "inclSigmaDeg": 4.36 },
    "down": { "fitted": true, "n": 14, "offered": false, … }
  },
  "summary": { "nDtlPublished": 177, "nFused": 76, "nNoFaceOn": 101, "nBridged": 6,
               "nSignDisagree": 0, "nIllConditioned": 1, "nOffPlane": 1, "backIncoherent": false,
               "nDtlAnchored": 0, "nDtlAnchorRefused": 0, "reflectedBands": [],
               "backRmsBeforeReflectDeg": null },
  "eta": { "fitted": true, "knotsUs": [1555061, 1585061, … ], "values": [6.76, 6.48, … ],
           "near": [1, 1, … ], "n": 60, "rmsDeg": 4.72, "knotMs": 30, "lambda": 4,
           "priorReachMs": 150, "priorFar": 1, "priorNear": 0.001, "maxAbsDeg": 25 },
  "items": { "eta": true, "dtlAnchor": false, "reflectBands": true },
  "frames": [ { "t_us": 1906043, "u": [-0.3440, 0.2979, 0.8905], "cond": 0.9934, "foSrc": "measured",
                "flags": [], "dtlBand": 1, "thetaF": 4.3438, "thetaD": 4.3895,
                "rhoF": 0.9546, "rhoD": 0.9390, "oopDeg": 11.00, "etaDeg": 2.93, "sigmaDeg": 3.21 }, … ]
}
```

| Field | Type | Notes |
|---|---|---|
| `valid` | bool | The fusion produced a track. When `false` the `summary` counts are the record of why. |
| `camera` | obj | The DTL camera the fusion **used**. `dtlYawDeg` / `dtlPitchDeg` are the original keys. `yawDeg` / `pitchDeg` / `rollDeg` / `offset` (m, fusion frame) / `calibrated` were added at stage version 4 (2026-10-02), and are filled from a measured calibration when there is one. |
| `address.inclDeg` / `n` / `inclSigmaDeg` | float °, int, float ° | The address shaft plane's inclination to the ground, read **from the DTL view alone**: the median over DTL frames published up to address. `inclSigmaDeg` appears only when assessed. |
| `deliveryVsAddressDeg` | float ° \| null | Downswing plane minus address plane. **+ = delivered steeper.** `null` unless both exist and the down plane is offered. |
| `planes.back` / `planes.down` | obj | Backswing / downswing direction planes. With `fitted` false only `{fitted, n}` is written. Otherwise: `normal` (unit, z ≥ 0), `inclDeg` (90 = vertical), `azimDeg` (heading of the normal's ground projection, atan2(y, x) — **yaw-dependent**), `oopRmsDeg` / `oopP90Deg` (scatter of the frames about the plane), `foRatio` / `foNodeDeg` (the plane as the face-on de-projection wants it), optional `inclSigmaDeg` (bootstrap 1σ of the inclination, not the scatter), and `offered` (`oopRmsDeg ≤ shaft.fusion.planeOopMaxDeg`, default 5°). Downstream reads only an **offered** plane. |
| `summary` | obj | Counts: DTL frames published, fused frames, frames with no face-on bracket, bridged frames, sign disagreements, ill-conditioned (\|n_F × n_D\| < 0.26), off-plane. `backIncoherent` (backswing scatter > 12° — suspect a mirrored DTL band). The §3.2a records: `nDtlAnchored`, `nDtlAnchorRefused`, `reflectedBands` (DTL band indices read reflected), `backRmsBeforeReflectDeg`. |
| `eta` | obj | The out-of-plane curve η(t). With `fitted` false only `{fitted}` is written. Otherwise `knotsUs` (window-relative), `values` (°), and `near` (1 = a measured frame within reach of that knot), plus the fit settings. Catmull-Rom through the knots. |
| `items` | obj | Which of the three §3.2a items ran. |
| `frames[].t_us` | int µs | Window-relative. Only fused frames are listed (`frames.length == summary.nFused`). |
| `frames[].u` | float[3] | Unit shaft direction butt→head, cameras' frame. |
| `frames[].cond` | float 0–1 | \|n_F × n_D\|. `0` means the two view planes coincide and the direction is undefined. |
| `frames[].foSrc` | str | `measured`, or `bridged` (the face-on angle came from its synth track). |
| `frames[].flags` | str[] | `signDisagree` · `illConditioned` · `offPlane` · `dtlAnchored` · `reflected`. |
| `frames[].dtlBand` | int | The `clubDtl.bands[]` index the DTL angle came from. |
| `frames[].thetaF` / `thetaD` | float rad | The two input image angles as used. θ_F is unwrapped, so it can lie outside ±π. |
| `frames[].rhoF` / `rhoD` | float \| null | The projected-length fractions this direction **predicts** in each view. A check, not an input. |
| `frames[].oopDeg` | float ° \| null | Signed angle off this phase's fitted plane. `null` when there is no plane. |
| `frames[].etaDeg` / `anchorCond` / `sigmaDeg` / `uBridged` | | **Optional**, each only where its item produced it: η at this instant, conditioning of a DTL-anchor attempt, 1σ of the direction (°), and the bridged direction before anchoring (only on `dtlAnchored` frames). |

> **Relating `club3d` to `skeleton3d`.** The axes are named the same, but `club3d` assumes a **level** face-on camera, while `skeleton3d` fits the face-on pitch and roll. To put a `club3d` normal into the skeleton world, rotate it through `skeleton3d.camera.pFDeg` / `rFDeg` first, as the 3-D view does (`src/Gui/swing3d/swing_rig_driver.cpp`).

Readers. The 3-D swing view reads `planes.down` and draws it only when `offered` is true and `normal` has three entries. The fused plane also feeds the face-on de-projection of the club's rate inside the analysis.

### `ball` — the face-on ball track (`BallTrack2D`)

The face-on ball position on each frame, drawn by the replay ball overlay (it mirrors the live green ball circle). Added 2026-07-08. Coordinates are normalised 0..1 to the **full frame** (same convention as `pose2d`), and the radius is normalised to frame width. The track comes from the live accumulator, a recorded `ball` stream, or the offline `BallRunner`. Written when there are any samples. An absent block means no ball data.

```json
"ball": {
  "camera": 0, "valid": true, "launchTUs": 3481000, "launch": { "x": 0.5484, "y": 0.8072 },
  "samples": [
    { "t_us": 620068, "found": true,  "x": 0.5484, "y": 0.8072, "r": 0.0074, "conf": 1.0, "act": 0.4 },
    { "t_us": 626700, "found": false, "x": 0,      "y": 0,      "r": 0,      "conf": 0.0 }
  ]
}
```

| Field | Type | Notes |
|---|---|---|
| `camera` | int | SourceId of the face-on camera. |
| `valid` | bool | Always `true` when written. |
| `launchTUs` | int µs | Window-relative launch instant (the collapse cliff). `-1` = no launch observed. |
| `launch` | obj | **Added 2026-09-09.** `{x, y}`, the ball centre on the last frame before launch, normalised. The impact anchor reads it, so a reloaded track carries it. Written only when `launchTUs ≥ 0`. |
| `samples[].t_us` | int µs | Window-relative frame time. |
| `samples[].found` | bool | Whether the ball was detected on this frame. `found: false` frames keep their `t_us` as gap markers, with the position zeroed; the overlay draws nothing on them, so the circle vanishes at launch. |
| `samples[].x` / `y` / `r` / `conf` | float | Centre, radius and detection confidence. `0` when not found. |
| `samples[].act` | float | **Added 2026-07-17.** Club activity beside a still ball: high when the clubhead moves next to it. Read only by the address-hold quiet mask and EventRefine's at-ball gate. Written only when computed (`≥ 0`). |

> The same track can also appear as a raw `kind: "ball"` stream (schema `ball_v2`; see "Ball stream"). That is a re-analysis input, separate from this block.

### `impact` — the impact camera's track (`ImpactTrack2D`)

Per-frame ball and club in the **impact camera's clip** (`setup.perspective` 4, `impact_camera_design.md` §7, §10.3), produced by `ImpactRunner` from the clip alone (no pose) and written by `impactTrackJson` in `swing_doc.cpp`. Added 2026-09-15. Coordinates are normalised 0..1 to the **clip frame** (`x / width`, `y / height`, radii `/ width`) — the clip is a sensor crop with its own geometry, never the face-on frame's. Drawn by the impact tile's overlay at the clip's own looping playhead. Absent block ⇒ no impact camera ran.

| Field | Type | Notes |
|---|---|---|
| `camera` | int | SourceId of the impact stream. |
| `valid` | bool | Always true when the block is present. |
| `width` / `height` | int | The clip frame the coordinates are normalised to. |
| `mmPerPx` | float | Frame scale from the resting ball's detected diameter (42.67 mm); `0` when no resting ball was found. ⚠ **Uncalibrated, and not for speeds or angles.** On a dim ball the detected diameter moves ±10 % with the threshold, and on 2026-09-15 it put ball speed at 1.3–1.5 × a launch monitor. Metrics from this clip need the card calibration (`camera_calibration_design.md` §4.8); this field is a drawing scale. |
| `ballRest` | obj, optional | `{x, y, r}` — the resting ball in the clip's opening frames. |
| `ballLeaveTUs` | int µs, optional | Window-relative instant of the first frame the resting ball's spot had gone dark — the clip's own impact evidence (it has run 5–10 ms **before** `capture.impactUs` on every 2026-09-15 swing). |
| `path` | obj, optional | The **synthesised** clubhead path (`kind` `synth`), not a fit through detections: robust quadratics in time for the hosel and the shaft angle over the run (30 frames before departure to 3 after, edge frames predicted but never fitted, broken hosels dropped), plus the head as a rigid offset from the hosel in the club's own frame (`headAlongPx`, `headAcrossPx`), self-calibrated by a feedback loop that gathers weak foreground inside the predicted head disc on every frame (`headEvidence` frames; `headAssumed` true when fewer than three showed anything and the generic 30/35 mm prior stands). `arc` `{a, b, c, thetaAtX0, thetaSlopePerX}` is the space-domain model (normalised: hosel `y = a·x² + b·x + c`, shaft angle `θ = thetaAtX0 + thetaSlopePerX·x`) the head path is built on; `ribbon[]` `{x, y}` is the head along it, 64 samples over the run's span — what the tile draws. `points[]` are `{t_us, x, y, hx, hy, th}` — one per run frame: the synthesised head, the smoothed hosel it hangs off and the smoothed shaft angle. `halfWidth` (/ width) is the ribbon the tile draws (≈ 30 mm). `hoselFit`/`hoselDropped`/`hoselResRmsPx` say how the smooth fit went. Proven in `tools/impactlab` before the C++ port. |
| `samples[].t_us` | int µs | Window-relative frame time; one sample per clip frame. |
| `samples[].bx/by/br` | float, optional | Ball centre + radius when the ball was found this frame (at rest, then tracked after departure until it leaves the frame). |
| `samples[].hx/hy`, `samples[].hs` | float, bool, optional | The head as far as the light showed it: the centroid of the club's off-shaft pixels plus the sole/crown glints in a head-sized window beyond the hosel (`hs` true), else the hosel itself. The head body is at mat level under a 100 µs exposure and never appears in the difference image, so this is glints, not a silhouette, until the head is lit. |
| `samples[].sax/say/sbx/sby` | float, optional | The shaft segment, grip side → the hosel (`sbx/sby` is the shaft's low end). |
| `samples[].edge` | bool, optional | `true` when the club component touches the left, right or bottom of the frame (partly out of view); drawn, never fitted. |
| `samples[].poly` | float[], optional | The club's outline: the convex hull of everything the light showed of it (shaft, neck, head glints), as `[x, y, x, y, …]` normalised. |

### `skeleton3d` — the fitted 3-D skeleton (`pinpoint.skeleton3d/1`)

Added 2026-09-26. A rigid, jointed skeleton (the Mixamo **Y-bot** rig: 27 joints, 48 degrees of freedom) fitted in one batch to everything that saw the swing. That means both cameras' 2-D poses, both shaft angles and measured clubheads, the planted feet, the ball, and any IMUs or HackMotion. Bone lengths are one value per swing, and knees and elbows are hinges (`src/Skeleton3d/skeleton3d_fit.h`, `analysis_pipeline.cpp`). Written by `skeleton3dToJson()` (`src/Skeleton3d/skeleton3d_json.h`) **whenever the stage ran** (`versions.skeleton3d > 0`), valid or refused. A refused fit writes `valid: false`, a `reason` and `frames: []`.

The block is **absent** when the stage did not reach the fit: skeleton3d off, no face-on pose, no Impact on the ladder, an unreadable face-on format, or fewer than 10 grid frames.

**Two cameras or no skeleton** (stage version 15, 2026-10-19). A face-on-only swing is refused with `reason` "the 3-D skeleton needs the down-the-line camera; …". A DTL camera with no usable hips or scale at address is refused as "the down-the-line camera could not be used (…)". Other refusals: "no address legs or height extent in the face-on view", "no address hips in the face-on view", and "the solve did not converge to a finite cost" (`skeleton3d_fit.cpp`). Each refusal also adds a `skeleton.refused` record to `analysis.gates` with `evidence.code` ∈ `fewFrames` · `noScale` · `noHips` · `noConverge` · `noDtl` · `dtlDropped`.

What it feeds. The 3-D swing panel. It also feeds the two-camera `faceOn+dtl` rotation route (`pelvisRotation`, `thoraxRotation`, `xFactor`, … for any segment no IMU measured), using only joints of tier ≥ 2, and the kinematic sequence's skeleton rung.

```json
"skeleton3d": {
  "schema": "pinpoint.skeleton3d/1", "stageVersion": 17, "valid": true, "reason": "",
  "frame": "world: X face-on image-right, Y face-on view ray, Z up; rig: Y-bot (ybot_rig.h)",
  "dtl": true, "foMirrored": false,
  "camera": { "calibrated": false, "fF": 884.7, "pFDeg": -0.69, "rFDeg": 0,
              "cD": [-1.7587, 1.9165, -0.1021], "psiDDeg": 8.61, "pDDeg": -1.98, "rDDeg": -2.26,
              "fD": 774.3, "zG": -0.9583, "gammaDeg": 81.39, "rRatio": 1.1965 },
  "scaleSource": "default", "scale": 1.0,
  "lengths": { "pelvis": 1, "spine": 1, "head": 1, "clavicle": 1, "upperArm": 1,
               "forearm": 1, "hand": 1, "thigh": 1, "shank": 1, "foot": 1 },
  "grip": { "axis": [-0.8042, 0.4604, -0.3759], "offset": [0.0, 0.08, 0.02],
            "trailOffset": [0.0, 0.08, 0.02], "sym": [-0.01199, 0.0147, -0.02942, 0.01657],
            "calibFixed": false, "clubLengthM": 0.947 },
  "display": { "originWorld": [0.1376, 2.0124, -0.9583], "stanceYawDeg": 4.05,
               "ball": [0.1376, 2.0124, -0.9293] },
  "diagnostics": { "costInit": 9547827.3, "costFinal": 66142.5, "iterations": 61, "ms": 1834,
                   "reprojMedPxFo": 4.27, "reprojMedPxDtl": 4.01,
                   "rawLengthCv": { "forearm": 0.233, "shank": 0.106, "thigh": 0.053, "upperArm": 0.417 },
                   "nSwapFo": 0, "nSwapDtl": 0, "nLimitHeld": 19, "footSlipP90Mm": 22.6,
                   "gripLockP90Mm": 0.30, "gripLockMaxMm": 0.51, "nUnknowns": 3651,
                   "plane": { "back": { "valid": true, "source": "catalogue", "normal": [0.0646, -0.8662, 0.4955], "count": 14, "rmsDeg": null },
                              "down": { "valid": true, "source": "self", "normal": [ … ], "count": 14, "rmsDeg": 4.18 },
                              "catalogueInclDeg": 60.3, "catalogueUncalibrated": false,
                              "nPlaneFrames": 107, "nBranchRuns": 0, "nBranchKept": 0, "branchMs": 0 } },
  "dofNames":   ["root.x", "root.y", "root.z", "root.yaw", "root.pitch", "root.roll", "spine.flex", … ],  // 48
  "jointNames": ["Hips", "Spine", "Spine1", "Spine2", "Neck", "Head", … ],                                // 27
  "frames": [ { "t": 1421081,
                "d": [0.1740, 2.7600, -0.0120, … ],                   // 48 DoFs: rad, root x/y/z in m
                "p": [174, 2760, -12,  180, 2677, 44, … ],            // 27 × (x,y,z) mm, world
                "tier": "333333333333333333333333333",
                "g": [0.1979, 2.5532, -0.2655], "u": [-0.1561, -0.6139, -0.7738],
                "s": 2, "f": 4 }, … ]                                  // 113 frames, P1 − 150 ms … P8 + 25 ms
}
```

#### Reconstructing the figure

- **World frame** (`skeleton3d_rig.h`, `skeleton3d_fit.cpp`): **+X** face-on image-right, **+Y** along the face-on view ray (away from the face-on camera), **+Z** up. It is right-handed (X × Y = Z). **The origin is the face-on camera's centre**, so Z = 0 is camera height and the floor is at `camera.zG` (−0.958 m above). On that swing the hips sit about 2.7 m down +Y. The face-on camera is fixed at the origin and fitted for pitch `pFDeg` (+ = looking down) and roll `rFDeg` (+ = image clockwise). The DTL camera's centre is `cD` (m), its yaw is `psiDDeg` (0 = looking +X), and it has pitch `pDDeg` and roll `rDDeg`. Both are pinholes with focal length `fF` / `fD` in **pixels** and the principal point **assumed at the image centre**, with no lens distortion.
- **Joints** — `p` is 27 × (x, y, z) in **millimetres** (integers), world frame, in this fixed order (`src/Skeleton3d/ybot_rig.h`; parent in brackets): `0 Hips (root) · 1 Spine (0) · 2 Spine1 (1) · 3 Spine2 (2) · 4 Neck (3) · 5 Head (4) · 6 HeadTop_End (5) · 7 LeftShoulder (3) · 8 LeftArm (7) · 9 LeftForeArm (8) · 10 LeftHand (9) · 11 LeftHandMiddle1 (10) · 12 RightShoulder (3) · 13 RightArm (12) · 14 RightForeArm (13) · 15 RightHand (14) · 16 RightHandMiddle1 (15) · 17 LeftUpLeg (0) · 18 LeftLeg (17) · 19 LeftFoot (18) · 20 LeftToeBase (19) · 21 LeftToe_End (20) · 22 RightUpLeg (0) · 23 RightLeg (22) · 24 RightFoot (23) · 25 RightToeBase (24) · 26 RightToe_End (25)`. Draw bones from each joint to its parent. In Mixamo naming, *LeftArm* is the upper arm (shoulder joint), *LeftForeArm* is the elbow, *LeftHand* is the wrist, *LeftUpLeg* is the hip, *LeftLeg* is the knee and *LeftFoot* is the ankle. **Left/Right are the golfer's anatomical sides**, so for a right-hander the lead side is Left. At zero rotation the golfer faces the face-on camera: front −Y, their left at +X. `jointNames` repeats the order. A reader should check it has 27 entries.
- **DoFs** — `d` holds 48 values in `dofNames` order. Root `root.x/y/z` is the Hips position in **metres** (the same point as `p[0..2]` in mm). Every other value is a rotation in **radians** about an anatomical axis (`spine.flex`, `lArm.abd`, `rKnee.flex`, …), applied as `restQ · neutral · Π Rot(axis_k, θ_k)` by `forwardKinematics()` (`skeleton3d_rig.h`). Use `p` for positions. Use `d` only if you rebuild the rig's rotations.
- **Club** — `g` is the lead-hand grip point (butt end) in **metres**, world frame. `u` is the unit shaft direction butt→head. The clubhead is roughly at `g + grip.clubLengthM · u`. Note that `g` is in metres while `p` is in millimetres.
- **Ball and display frame** — `display.ball` is the solved 3-D ball (m), or `null`. `display.originWorld` is that ball dropped to the floor, or else the mid-heels. `display.stanceYawDeg` is the heading of trail heel → lead heel in world XY. The 3-D view subtracts `originWorld`, rotates about Z by −`stanceYawDeg`, and maps (x, y, z) → (x, z, −y) to get a Y-up scene (`swing_rig_driver.cpp`).
- **Time** — `t` (not `t_us`) is window-relative µs. The grid is the face-on pose instants from address − 450 ms to impact + 700 ms, thinned to ≤ 600, with the impact instant added as its own frame. Then **only address − 150 ms … P8 + 25 ms is kept** (stage version 16). So there are no Finish frames.

#### Caveats

> ⚠ **`camera.calibrated` is always `false`.** The cameras are **self-solved** from an assumed placement. The face-on camera starts `faceOnDistanceM` = 2.0 m from the golfer. The DTL camera starts `dtlDistanceM` = 2.2 m away, level, and looking ±X (which way is chosen from the toes). Focal lengths, pitches, rolls, the DTL position and the floor are then fitted to the keypoints. A measured two-camera calibration does not reach this fit yet, so every consumer treats it as an estimate.
>
> **Scale comes from the athlete's height** (`scaleSource: "height"`), else the Y-bot default (`"default"`, about 1.79 m to the head top). Size and distance are nearly one image: an unknown height means the whole figure, and its distance from the camera, scale together.
>
> **`lengths` are held, not fitted.** `skeleton3d.fitLengths` is off by default, so every group is the Y-bot's proportion × the global scale (all `1` above). Fitted lengths soak up camera-model error. `diagnostics.rawLengthCv` (CV of an unconstrained triangulation's lengths per group, written only where finite) shows how loose the raw evidence is.

| Field | Type | Notes |
|---|---|---|
| `valid` / `reason` | bool, str | `false` + reason on a refusal. `reason` is `""` when valid. |
| `dtl` | bool | The DTL view was used in the fit. |
| `foMirrored` | bool | The face-on source was mirrored (the golfer's left imaged left). Its keypoints and shaft were flipped before the fit. |
| `camera.fF` / `fD` | float px | Focal length of each pinhole. |
| `camera.pFDeg` / `rFDeg` | float ° | Face-on pitch (+ down) and roll. The face-on centre is the origin. |
| `camera.cD` | float[3] m | DTL camera centre, world. |
| `camera.psiDDeg` / `pDDeg` / `rDDeg` | float ° | DTL yaw (0 = looking +X), pitch, roll. |
| `camera.zG` | float m | Floor height (world Z). The reader's `floorZ`. |
| `camera.gammaDeg` | float ° \| null | Angle between the two views' ground-plane rays. |
| `camera.rRatio` | float \| null | DTL px-per-m ÷ face-on px-per-m at the golfer. |
| `scale` / `scaleSource` | float, str | Global body scale vs Y-bot, and `height` \| `default`. |
| `lengths.<group>` | float | Scale per length group: `pelvis` `spine` `head` `clavicle` `upperArm` `forearm` `hand` `thigh` `shank` `foot`. |
| `grip.axis` / `offset` / `trailOffset` | float[3] | The shaft axis and the lead / trail grip points, in the lead hand's local frame. |
| `grip.sym` | float[4] | The shoulder and hip keypoint pairs' shared width/height offsets (m). |
| `grip.calibFixed` | bool | Shared values came from a session pool (SwingLab only; the app never pools). |
| `grip.clubLengthM` | float m \| null | Club length (grip point → head). A recorded or standard club is held to σ 5 mm. |
| `display.*` | | See *Reconstructing the figure*. |
| `diagnostics.costInit` / `costFinal` / `iterations` / `ms` / `nUnknowns` | | Solver record. |
| `diagnostics.reprojMedPxFo` / `reprojMedPxDtl` | float px | Median reprojection error per view. This is the main fit-quality number. |
| `diagnostics.nSwapFo` / `nSwapDtl` / `nLimitHeld` | int | Frames with a left/right label swap per view, and frames where a joint limit held (beyond 2°). |
| `diagnostics.footSlipP90Mm` | float mm | p90 slip of planted foot markers. |
| `diagnostics.gripLockP90Mm` / `gripLockMaxMm` | float mm \| null | How far the fit left the trail grip from its locked place before the exact lock moved the arm. The stage warns above 10 mm. |
| `diagnostics.plane` | obj | The club's depth-branch planes (`back`, `down`: `valid`, `source` `self` \| `catalogue` \| `""`, `normal`, `count`, `rmsDeg`), the catalogue inclination, and what the branch pass tried and kept. |
| `frames[].tier` | str[27] | One digit per joint: `0` absent · `1` inferred · `2` constrained · `3` measured. **Measured** = seen in both views at this instant, with posterior σ ≤ 2 cm × scale. **Constrained** = σ ≤ 5 cm × scale. Per-joint σ itself is **not** persisted. |
| `frames[].s` | int | Shaft tier: `1` model only · `2` one view's shaft angle · `3` both. `0` is never written. |
| `frames[].f` | int bits | `1` face-on label swap · `2` DTL swap · `4` a joint limit held · `8` the branch pass put the club on its other depth branch. |

Rounding. Angles and grip vectors are rounded to 1e-4. Camera angles are rounded to 0.01°, focal lengths to 0.1 px, and `p` to whole millimetres.

Reader — `skeleton3dFromJson()` (`skeleton3d_json.h`). It accepts the block only when `schema` is exactly `pinpoint.skeleton3d/1`. DoFs are matched **by name**, so a document with a different DoF layout reads those DoFs as absent (zero), not wrong. Joint positions are read only when `jointNames` has 27 entries and `p` has 81. A `t` ≥ `clock.t0_us` is made window-relative. A missing `lengths.<group>` reads as 1.0 and a missing `clubLengthM` as 0.95. A `valid` track with fewer than two frames is demoted to invalid.

### `addressMarks` — the coach lines' silhouette marks (`pinpoint.addressMarks/2`)

Added 2026-10-01. The body's **outer edges** on each camera's address frame. The pose gives joint centres; these come from the person mask (u2netp) on the frame nearest Address (`src/Body/address_marks.h`). Face-on, they are the outside of each hip, the cue for sway. Down the line, they are the rear of the butt (the cue for moving off the line), TPI's spine-angle line (tailbone → back of the neck), and the head outline. Since `/2` they also include the DTL butt and back lines at every position P1–P7. Produced by `AddressMarksStage` (`analysis_pipeline.cpp`), written by `addressMarksToJson()` (`src/Body/address_marks_json.h`).

Written **whenever the stage ran** (`versions.addressMarks > 0`). `found: false` on a view is the record that that view gave nothing. The block appears on **face-on-only swings too**, with `dtl: {"found": false}`. It is absent when there is no Address, no pose on either camera, no segmenter in the build, or the segmenter model failed to load.

**Coordinates are normalised to that camera's frame**: x ÷ width, y ÷ height. The head radius `headR` is ÷ **height**. Absent numbers are `null`.

```json
"addressMarks": {
  "schema": "pinpoint.addressMarks/2", "stageVersion": 4,
  "source": "person mask (u2netp) on the frame nearest Address, read on the hip rows",
  "faceOn": { "found": true, "t_us": 1555061, "rowY": 0.5286, "hipLeftX": 0.5059, "hipRightX": 0.5940,
              "leftX": 0.4752, "rightX": 0.6120, "buttX": null, "side": 0, "rows": 31, "rowSource": "hips",
              "tailboneX": null, "tailboneY": null, "neckX": null, "neckY": null, "backWalks": 0,
              "headFound": true, "headCx": 0.5696, "headCy": 0.3325, "headR": 0.0454,
              "crownY": 0.2871, "headRowY": 0.3477, "headLeftX": 0.5153, "headRightX": 0.6238 },
  "dtl":    { "found": true, "t_us": 1551029, "rowY": 0.4890, "leftX": 0.0260, "rightX": 0.3578,
              "buttX": 0.0260, "side": -1, … ,
              "headFound": false, "headRefused": "the outline is wider than a head (a hood or a collar merges)" },
  "phaseNeckFrac": 1.0,
  "dtlPhases": [
    { "phase": 0,  "eventTUs": 1555061, "found": true, "t_us": 1551029, "buttX": 0.0260, "side": -1,
      "tailboneX": 0.0444, "tailboneY": 0.4356, "neckX": 0.3811, "neckY": 0.2834, "backWalks": 11, … },
    { "phase": 12, "eventTUs": 1769426, "found": true, … },
    { "phase": 8,  "eventTUs": 1933217, "found": true, "neckX": null, …,
      "refused": "the rear outline not found behind the trunk: no back line" }, …     // 7 entries, P1…P7
  ]
}
```

| Field | Type | Notes |
|---|---|---|
| `faceOn` / `dtl` | obj | One view's marks. With `found` false nothing else is written. |
| `.t_us` | int µs | Window-relative time of the frame the mask was read on. |
| `.rowY` | float | The hip row (normalised y) the edges were read along. |
| `.hipLeftX` / `hipRightX` | float | The pose's hip joints at that frame (for the record). |
| `.leftX` / `rightX` | float | The mask's outer edges on the hip rows. Face-on, these are the two hips. Down the line, they are the body's front and rear. |
| `.buttX` / `.side` | float, int | **DTL only**: the rear edge, and which side it is on (`−1` image-left, `+1` image-right, `0` face-on / undecided). |
| `.rows` / `.rowSource` | int, str | Rows in the medians. `rowSource` is always `hips` today; `wrists` is reserved. |
| `.tailboneX/Y`, `.neckX/Y`, `.backWalks` | float, int | **DTL only** (stage version 2, 2026-10-22): TPI's spine-angle line on the rear outline, walked at a right angle to the trunk axis. The coach line in `dtl` reads the neck point at the collar (½ of the way shoulder-midpoint → ears). `null` when not measured. |
| `.headFound` + `headCx`, `headCy`, `headR`, `crownY`, `headRowY`, `headLeftX`, `headRightX` | bool, float | **Optional** (stage version 4, 2026-10-23): the head circle off the silhouette. It touches the crown, and its diameter is the widest head row. `headCx` and the `X` keys ÷ W; `headCy`, `headR`, `crownY`, `headRowY` ÷ H. `headFound` is written only when found or refused. |
| `.headRefused` | str | Why there is no head circle. Readers then draw the face-point circle. |
| `phaseNeckFrac` | float | **Optional, with `dtlPhases`.** Where the P-frames' back line reads its upper point (1.0 = behind the ears; the coach line uses 0.5). |
| `dtlPhases[]` | obj[] | **Schema /2** (stage version 3, 2026-10-23): the DTL butt line and back line at P1…P7 in ladder order, `phase` = `0` Address · `12` ShaftParallelBack · `8` MidBackswing · `2` Top · `13` ArmParallelDown · `9` Delivery · `5` Impact. Each entry has the view keys above, plus `phase`, `eventTUs` (the position's ladder instant, window-relative; `0` when the phase is not on the ladder), `refused` (present when not found or when the back line failed), and `side` (the rear side **frozen from address**, written even on a refused entry). P1 reuses the `dtl` address mask, with its back line re-read at `phaseNeckFrac`. Absent when there is no DTL pose or `addressMarks.phases` is off. |

From these P-frames the stage also adds two sparse series to `metrics[]`, `spineBackLine` (°) and `hipDepthButt` (cm, through `dtlRuler`). Those are documented by the metric catalogue (see `metrics[]`).

Readers. `viewMarksPayload()` / `dtlPhaseMarksPayload()` (`address_marks_json.h`). Every numeric key missing from an older record reads as NaN. `headFound` counts only when `headR` is a number. A `/1` object (no `dtlPhases`) gives an empty list. The face-on tile reads `faceOn`, and the DTL tile reads `dtl` and `dtlPhases` (`disk_replay_source.cpp`, `src/Gui/charts/posture_picture.cpp`).

### `dtlRuler` — centimetres per down-the-line pixel (`pinpoint.dtlRuler/1`)

Added 2026-10-23. The ruler `DtlPostureStage` resolved: **cm per DTL pixel at the golfer**. This is the one scale `pelvisThrust`, `headRise`, `hipDepthButt` and the ball's distance were measured in, kept so a reader can draw centimetres on the DTL frame with the same scale (`src/Body/dtl_ruler_json.h`, `analysis_pipeline.cpp`). Written by `dtlRulerToJson()` **whenever the stage ran**, including when the posture was refused. Absent when it did not run: posture off, no DTL camera or pose, or no Address. Its version key is `versions.dtlPosture`.

```json
"dtlRuler": { "schema": "pinpoint.dtlRuler/1", "stageVersion": 2,
              "source": "faceOnBall", "cmPerPx": 0.2901, "toward": 1, "scaleRatio": 1.1685 }
```

| Field | Type | Notes |
|---|---|---|
| `source` | str | `dtlBall` (the bright DTL ball: 4.267 cm ÷ its diameter in px) · `faceOnBall` (the face-on ball ruler ÷ `scaleRatio`) · `none` (`src/Body/dtl_posture.h`). |
| `cmPerPx` | float cm/px \| null | **`null` when there is no ruler.** It is never `0`, so a reader never multiplies by zero. |
| `toward` | int | **Optional.** `+1` = the ball is image-right, `−1` image-left (toes are on the ball side of the heels). Omitted when undecided. |
| `scaleRatio` | float | **Optional.** DTL px ÷ face-on px at the golfer (nose-to-ankles extent at address). Omitted when not measured. |
| `refused` | str | **Optional.** A ruler that resolved but was dropped because the shoulder width it implied fell outside the adult range, e.g. "faceOnBall implied 54.0 cm of shoulders". Then `source` is `none` and `cmPerPx` is `null`. |
| `reason` | str | **Optional.** The posture stage's own refusal, when it stopped before or after the ruler. |

Reader. `dtlRulerPayload()` gives `{cmPerPx (NaN when null or ≤ 0), source (default "none"), toward (default 0)}` (`dtl_ruler_json.h`, `disk_replay_source.cpp`, `posture_picture.cpp`).

### `bindings[]` (IMU only)

A per-device calibration snapshot, keyed by serial, so offline re-analysis can re-fuse with the exact anatomical transforms the app used. **It takes precedence over each stream's `device` block on re-analysis.**

```json
"bindings": [ {
  "serial": "WT901-1234", "role": 6, "roleName": "LeadHand",
  "alignA": [1,0,0,0], "mountM": [0.5,-0.5,-0.5,-0.5],
  "calibrated": true, "anatCalibrated": true,
  "mountDeviationDeg": 3.2, "mountGravityErrorDeg": 5.1,
  "calibratedAt": "2026-06-11T09:12:00.123Z", "calibAgeSec": 412.5,
  "hackMotion": false
} ]
```

| Field | Type | Notes |
|---|---|---|
| `serial` | str | Device serial or MAC. |
| `role` / `roleName` | int / str | `SegmentRole`, append-only: `0` Unknown · `1` Pelvis · `2` Thorax · `3` T12 · `4` LeadUpperArm · `5` LeadForearm · `6` LeadHand · `7` TrailThigh · `8` LeadThigh · `9` Club (`segment_role.h`). `roleName` is the stable spelling (`"LeadHand"`). It is **not** the settings spelling (`"leadHand"`). |
| `alignA` / `mountM` | float[4] | wxyz quaternions: fusion world → anatomical world, and anatomical body → sensor body. |
| `calibrated`, `anatCalibrated`, `mountDeviationDeg`, `mountGravityErrorDeg`, `calibratedAt`, `calibAgeSec` | — | Calibration status at shot time. `calibAgeSec -1` with an empty `calibratedAt` means never calibrated. |
| `hackMotion` | bool | **Added 2026-08-19.** The device is a HackMotion wG3, whose raw quaternion must be conjugated before composition. ⚠ It matters on re-analysis: without it every wrist sign comes back inverted, and the result still looks plausible. |
| `calibrationSource` / `trunkSolve` | str / obj | **Added 2026-10-26.** Present only when the trunk stand-tall solve set A/M. `calibrationSource` is `"trunkStandTall"`, and `trunkSolve` is `{deltaTier ("δ0"/"δ1"/"δ2"), sigmaDeltaDeg, sampleFraction, maxAbsGyroDps, gravityErrPct, longAxisFromUpDeg, faceFromHorizontalDeg}`. Absent means an arm routine's calibration, or none (`trunk_calib_provenance.h`). |

### `assessment` (optional) / `filter` (optional)

`assessment` is the wrist coach feed. It is written whenever the assessment stage ran, which needs IMU streams and wrist series: live, on re-analysis and in SwingLab alike (#110). It has `scoreV2` (0–100, telemetry, **not** a score) and `findings[]`. Each finding is `{id, name, category, severity ("good"/"watch"/"fault"), magnitudeDeg, weight, confidence, lowConfidence, dofs[], positions[]}`:

- `dofs` are `PpJointDof` ints: `0` LeadWristFlexExt · `1` LeadWristRadUln · `2` LeadForearmRot · `3` LeadElbowFlex · higher values reserved (`wrist_assessment_types.h`).
- `positions` are `PpSwingPosition` ints, **0-based**: `0` = P1 … `7` = P8.

`filter` is `{impactStepDeg}`, the orientation-filter quality. It is present only when offline re-fusion drove the orientation.

### `timings` (optional)

Per-stage analyser wall times in ms, reported by the analyser itself so that every shot measures the pipeline budget (the same numbers are echoed in SwingLab's `runmeta.json`). Written only when the total was measured. A stage that did not run stays `-1`.

```json
"timings": { "poseMs": 5480, "ballMs": 120, "shaftMs": 2900, "impactMs": -1, "poseDtlMs": 4100, "totalMs": 8600,
             "pose": { "faceOn": { "sessionBuildMs": 0, "sessionWaitMs": 0, "decodeMs": 610.2, "preprocessMs": 220.4,
                                   "runMs": 4300.7, "heatmapDecodeMs": 180.1, "totalMs": 5480.0, "frames": 212 } },
             "stages": [ { "name": "Pose", "ran": true, "source": "computed", "ms": 5480.12,
                           "startMs": 3.1, "endMs": 5483.22, "thread": 1 }, … ] }
```

| Field | Meaning |
|---|---|
| `poseMs` | The face-on pose pass. `-1` when no camera ran. |
| `ballMs` / `shaftMs` | The ball-track and shaft-track stages. `-1` when the pose pass produced no frames. |
| `impactMs` | **Added 2026-09-15.** The impact camera's track. `-1` when it did not run. |
| `poseDtlMs` | **Added 2026-10-06.** The DTL pose pass, smoothing included. `-1` when it did not run. Always written inside the block. |
| `totalMs` | The whole analysis. The block is present only when this is `≥ 0`. |
| `pose.faceOn` / `pose.dtl` | **Added 2026-10-06.** Where each camera's pose pass spent its time: `sessionBuildMs`, `sessionWaitMs` (added 2026-10-07), `decodeMs`, `preprocessMs`, `runMs`, `heatmapDecodeMs`, `totalMs` (each to 0.1 ms) and `frames`. Present only for a camera actually posed on this run. A reused or loaded track has no split (`pose_timing.h`). |
| `stages[]` | **Added 2026-10-06.** Every stage of the profile, in authored order, whether it ran or was skipped: `name`, `ran`, `ms`, `startMs` / `endMs` (offset from the start of the analysis, to 0.01 ms), `thread` (`0` under the sequential loop), and optional `skipReason`. Optional `source` (**added 2026-11-06**) is `computed`, `loaded` (a track the stage did not produce) or `refused`. `tools/analysis/analysis_dag.py` draws the timeline from it. |

---

## Companion files in the swing directory

| File | Written by | When |
|---|---|---|
| `swing.ppsw` | `SwingStore::save` | The document. Always, once anything has been recorded (see "On disk"). |
| `swing.json` | — (legacy) | Only in a swing not yet converted. Deleted by the first rewrite or by a library conversion. |
| `<alias>.mp4` | `SwingExporter` | One H.264 clip per local camera. Always `.mp4` (`shot_processor.cpp`). |
| `<alias>.mp4` / `.mov` / `.mkv` | `PpcpClipFiler` | A phone clip filed by PPCP. Its extension comes from the clip's declared container (`ppcp_clip_filer.cpp`, `ppcp_import_sink.cpp`). Its `streams[]` element carries `frames.clock: "clip"`. If the shot had no document, a minimal one is created for the clip. |
| `<alias>.raw` | `SwingExporter` | When raw-frame saving is on: the undecoded sensor frames as one blob (see `streams[].raw`). |
| `thumb.jpg` | `SwingExporter` | The impact thumbnail. Not written for a launch-monitor-only swing. |
| `<alias>.ballbase.f32` | `SwingExporter` | When the face-on ball detector had a learned empty-mat baseline at export. Raw row-major `float32`, `w*h*4` bytes — the live detector's learned empty-mat DoG-response baseline `B` over the search ROI at seed time. Loaded by `BallRunner` on re-analysis; see `streams[].setup.ballDetection.baseline`. |
| `swing_phasegrid.json` | `measure_sample.cpp` (`savePhaseGrid`) | A rebuildable cache of the per-phase metric grid used by diagnostics and charts. `schema` is an int (`kPhaseGridSchemaVersion` = 10, `measure_sample.h`). Checked against the document's size and mtime and the reduction parameters. Safe to delete. |
| `swing_summary.json` | `SwingDocReader::writeSwingSummary` | **JSON-era swings only.** The legacy list cache (see "Legacy: `swing_summary.json`"). A `.ppsw` swing never has one. |
| `truth.json` / `truth_dtl.json` | Markup lab (`src/Gui/markup/markup_truth.cpp`) | Hand-marked ground truth for the face-on and DTL views. Separate schema ([`truth_json_schema.md`](truth_json_schema.md)). |

**No longer written:**

- `imu_<alias>.csv` / `.bin`. The CSV/binary IMU sidecar options were removed on 2026-09-23 (`6e9e2c1a`). IMU samples are always inline in `streams[].samples`. Older swings may still hold sidecars, and the zip export still includes them.

An **archived** swing keeps only `swing.ppsw` (a stub) and its thumbnail. Every other file is in the archive copy.

The **full per-swing / per-session folder layout** is in [`swing_folder_layout.md`](swing_folder_layout.md).

The zip export contains `thumb.jpg`, the selected videos, and, when data is included, `swing.json` and any old `imu_*` sidecars. It never contains `.raw`, `swing.ppsw` (sent as `swing.json`), `swing_summary.json`, `swing_phasegrid.json`, `.ballbase.f32` or `truth*.json` (`swing_zip_exporter.cpp`).

## Files beside the swings

These are not part of the swing document, but someone reading a library will find them. All are JSON written by the app, except the PDF and the `.jsonl`.

| Level | File | Schema / version key | Written by |
|---|---|---|---|
| session | `diagnostics.json` | envelope `schemaVersion` 1; nested `ledger.schemaVersion` 15 (the row shape; 15 added `notLiveStatus` on rows a non-live measure stopped, and `ungradedReadings` on a shot whose launch-monitor readings include one, #176) | `SessionDiagnosticsModel::persist` (`session_diagnostics_model.cpp`; `diagnostic_ledger.h`) |
| session | `club_baseline.json` | — | `SessionDiagnosticsModel`: the per-club baseline the session was graded against, frozen once written |
| session | `work_ons.json` | `schemaVersion` 3 | `WorkOnsController` (`work_ons_controller.cpp`; `work_ons.h`) |
| session | `report/report_meta.json` + `report/<date>_<golfer>_session-report.pdf` | `schema` int 1 | `writeReportMeta` (`src/Gui/report/report_meta.h`) |
| session | `archived.json` | `schema` `pinpoint.archive/1`; `location`, `archivedAt`, `swings`, `files`, `bytes`, `rawKept` | `SessionArchiver::archive` (`session_archiver.cpp`). Present only on an archived session. |
| session | `consistency.json` | — | **No longer written or read** (retired 2026-11-07, `src/Coaching/consistency_home.h`). Old copies stay in libraries. |
| athlete | `tendencies.json` | `schema` int 1 (`kTendencySchemaVersion`) | `WorkOnsController` (`work_ons_controller.cpp`; `tendencies.h`) |
| athlete | `swing_themes.json`, `swing_themes.<club>[.<filter>].json` | — | `WorkOnsController::themesPath` |
| athlete | `fault_profile.json`, `outcome_corrections.jsonl` | — | `SessionDiagnosticsModel` |
| library | `ppcp-ledger.json` | — | PPCP import ledger: maps a phone capture's identity to where its bytes were filed (`src/Ppcp/ppcp_host_service.cpp`) |
| library | `.pinpoint-trash/<athlete>/…` | — | Trashed swings and sessions, used only when the volume has no system trash, as on a network share. Otherwise they go to the system trash (`src/Export/swing_paths.cpp`). `SwingStore::swingDirsUnder` never enters it. |

## Schema version history

Current ids: `pinpoint.swing/2` (the document), `pinpoint.analysis/3` (the `analysis` block — `/3` promoted `score` from a bare int to a `ScoreBreakdown` object, design §B.0a/§B.7; `/2`, where `score` was a bare integer, is still read) and `pinpoint.swingsummary/4` (the `summary` block). The two-camera blocks carry their own ids (`pinpoint.clubDtl/1`, `pinpoint.club3d/1`, `pinpoint.skeleton3d/1`, `pinpoint.addressMarks/2`, `pinpoint.dtlRuler/1`). None of the changes below bumped `pinpoint.swing/2` or `pinpoint.analysis/3`.

| Date | Change |
|---|---|
| 2026-06-26 | `imuIntegrity` top-level block (IMU re-fusion parity). Additive. |
| 2026-07-07 | `capture.club` added; all `analysis` `t_us` normalised to window-relative (readers domain-aware for legacy absolute files). |
| 2026-07-08 | `analysis.ball` added (face-on ball track for the replay overlay); `setup.ballDetection.searchRoi` added (hitting-area box for offline re-analysis); the raw `kind: "ball"` stream (`ball_v2`). All additive — no schema-version bump. |
| 2026-07-09 | Clubhead Stage-2 head pass: `capture.club.hoselFromButtMm` + `analysis.club.samples[].headConf`/`headSigma` (measured-head confidence + posterior σ) added, and `ShaftSampleFlags` gained `0x80` `HeadOffFrame`. The head pass (`shaft.head.enabled`) defaults ON from this date, so new Wrist swings carry real values. Additive — no schema-version bump. |
| 2026-07-09 | `analysis.timings` added (per-stage analyzer wall times: `poseMs`/`ballMs`/`shaftMs`/`totalMs`). Additive — no schema-version bump. |
| 2026-07-10 | `setup.ballDetection.baseline` object + `<alias>.ballbase.f32` sidecar added (persisted live empty-mat ball baseline for offline re-analysis). Additive — no schema-version bump. |
| 2026-07-10 | Club-length fusion: `capture.club.name` + `capture.club.lengthPrior` (recorded prior for deterministic re-analysis) and `analysis.club.lengths` (fused length ± σ + confidence) added. Additive — no schema-version bump. |
| 2026-07-11 | Shaft Layer A snap: `analysis.club.samples[].lineConf` (ridge support under the drawn line) added. Written only on vision-tier samples when the snap pass ran (`absent ⇒ −1`), so a snap-off run stays byte-identical. Additive — no schema-version bump. |
| 2026-07-11 | Shaft Layer B positions: `analysis.club.positions[]` (coaching P-positions P1–P8, `shaft_position_first`) added — P2/P6/P8 are image-plane shaft-parallel crossings (accepted face-on coaching practice, not 3-D geometry). Written only when non-empty (extraction off ⇒ absent, byte-identical). Additive — no schema-version bump. |
| 2026-07-11 | Shaft Layer C synthesis: `analysis.club.synth[]` (VISUALIZATION-tier interpolated series between P-anchors, `shaft_position_first`) added, and `ShaftSampleFlags` gained `0x100` `Synthesized` (C++ enum widened `uint8_t → uint16_t`; JSON already int, no reader break). Synthesized samples are excluded from scoring by the flag. Written only when non-empty (synthesis off ⇒ absent, byte-identical). Additive — no schema-version bump. Also: `positions.enabled`/`fitEnabled` flipped default ON, so new Wrist swings carry `analysis.club.positions[]`. |
| 2026-07-12 | Motion overlay: `analysis.pose2d.smoothed[]` (de-jittered pose companion track for the fan/trace overlays, `pose_smoother.{h,cpp}`) added — parallel to `frames` with per-keypoint `tier`/`sigma` honesty. Written only when non-empty (absent ⇒ re-analyse to gain fan/trace). `Phase` enum gained `ShaftParallelBack=12`/`ArmParallelDown=13`/`ShaftParallelThrough=14` for the P-position system, but these were **not yet emitted** into `phases[]` at the time (see the 2026-08-09 row for `13`/`14`; `12` now comes from the positions ladder). Additive — no schema-version bump. |
| 2026-07-13 | `capture.motionCaptureQuality`. Additive. |
| 2026-07-13 | WB1 offline pose accuracy: `analysis.pose2d.decode` (`"dark"`) + `analysis.pose2d.cropRect` (`{x,y,w,h}` full-frame normalized swing-level person crop) added as provenance. Both written **only when non-legacy** (`decode` omitted for argmax, `cropRect` omitted on the full-frame fallback), so a flags-off run (`pose.crop.enabled=false`, `pose.decode.dark=false`) serialises **byte-identically** to the pre-WB1 tree. `kp` stay full-frame normalized (back-projected). Additive — no schema-version bump. |
| 2026-07-13 | WB2 head tracking: `analysis.metrics[]` gains `headSway`/`headLift` (`×frame` = fraction of frame width, isotropic — anisotropy of the separately-normalized x/y kp is corrected; Address-referenced) and `headTilt` (`°`, eye-line-angle Δ). Head position is a first-class golf metric. Derived from the (smoothed, else raw) face-on pose head keypoints (`head_track.{h,cpp}`); camera-only path too. UNSCORED (no reference bands yet — no `phaseSamples[].band`). Emitted only when the pose pass produced head-localizable frames. Additive — no schema-version bump; readers already iterate `metrics[]` key-agnostically. |
| 2026-07-13 | WB3 setup + footwork metrics: `analysis.metrics[]` gains `stanceWidth`/`leadFootFlare`/`trailFootFlare`/`toeLineAngle` (address-only scalars — **empty `t_us`/`value`, the single measurement in `phaseSamples[0]`**, the same generic `metrics[]` contract every reader already tolerates) and `leadHeelLift` (`×frame`, Address-referenced lead-heel-vs-toe elevation curve, `+` = heel lifts — a normal per-frame series like the head keys). Derived from the COCO-WholeBody foot keypoints (`foot_metrics.{h,cpp}`); camera-only path too; lead foot follows the athlete's handedness. UNSCORED. Emitted only when the pose pass produced foot-localizable frames (never on a legacy 17-kp track). Additive — no schema-version bump. Also: `streams[].setup.ballDetection` re-analysis (`BallRunner`) now derives its search corridor from these same foot keypoints when coverage allows (`ball.corridor.useFeet`, default on), falling back to the pre-WB3 ankle-based corridor otherwise — no document field changes, this only affects re-analysis geometry. |
| 2026-07-16 | `analysis.pose2d.synth[]`: 240 Hz visualisation upsample of `smoothed`; metrics never read it. |
| 2026-07-17 | `analysis.ball.samples[].act`: club activity beside a still ball. |
| 2026-07-21 | Tempo, stance-width mm, and ball position: `analysis.metrics[]` gains `tempoBackswing` (`s`, Address→Top) and `tempoRatio` (`:1`, `(Top−Address)/(Impact−Top)`) — both **Summary scalars** (empty `t_us`/`value`, single `phaseSamples[0]` at `phase: 5` **Impact**, since they describe the whole swing), emitted only when the ladder carries a confident Address+Top+Impact (the IMU `clampFallback` ladder has no Top ⇒ omitted, never estimated). Also `ballPosition` (`%`, ball projected onto the lead-heel→trail-heel line ÷ stance width; `phase: 0` Address scalar; **unclamped** — below 0 % means forward of the lead heel), emitted only when a ball is detected at address. `metrics[]` entries gain the **optional** `sigma` field. **Changed, not added:** `stanceWidth`'s unit became `mm` when the ball-diameter px→mm ruler resolves (`42.67 mm ÷ 2·radiusPx`, `src/Core/pp_physical_constants.h`), falling back to the previous `×frame` when no ball is detected — **readers must branch on `unit`, not assume it** (superseded on 2026-07-25, see below). `leadHeelLift` deliberately stays `×frame`. All three producers gate on `tempo.enabled` / `ballpos.enabled`; both off ⇒ byte-identical to the pre-change tree. Additive — no schema-version bump. |
| 2026-07-21 | `analysis.score.perRegion` / `perPhase`: adherence contribution maps, written only when not empty. |
| 2026-07-21 | Session-picker sidecar: a `swing_summary.json` companion file (`pinpoint.swingsummary/1`) is written beside each `swing.json` to cache the handful of scalars the "choose a session" drawer needs, so the picker never parses a multi-MB `swing.json` on the GUI thread. Pure regenerable cache, guarded by the source doc's size+mtime. **Does not touch the document schema** — separate file, separate schema id. See "Legacy: `swing_summary.json`" and [`swing_folder_layout.md`](swing_folder_layout.md). |
| 2026-07-25 | `stanceWidth` unit fixed at `% shoulder width`; millimetres move to the new key `stanceWidthMm`. Supersedes the 2026-07-21 unit switch. |
| 2026-08-04 | `launchMonitor` block (GC Quad / FSX) and `lm.*` rows in `analysis.metrics[]`. Additive. |
| 2026-08-09 | Phase ladder gains real P5/P8 anchors: the segmenter now emits `13` ArmParallelDown (P5, lead-forearm parallel crossing) and `14` ShaftParallelThrough (P8) into `phases[]` in place of the old `4` Downswing / `6` Release ticks. `14` is a forearm PROXY for shaft-parallel-through (not shaft-measured). `4`/`6` remain valid `Phase` values (still read from legacy files; readers must not assume they still appear in newly-written ladders). No schema-version bump. |
| 2026-08-09 | `review` seeded at capture with the session's club. |
| 2026-08-09 | `swing_summary.json` → `pinpoint.swingsummary/2`: `club` resolved as `review.club`, else `capture.club.name`, else the stub (`bcf03d8e`). Sidecar only; the document is unchanged. |
| 2026-08-10 | `capture.latencyUs.micTravel` (travel-corrected acoustic anchor). Additive. |
| 2026-08-11 | `analysis.club.plane`: face-on swing-plane transition delta, with measured and synth channels. |
| 2026-08-17 | wG3 lanes: two `imu` streams per device, `instrument: "hackmotion"`, honest `units`, `device.skewUs` and capture provenance. Same `imu_sample_v2` layout. |
| 2026-08-18 | wG3 `device.history` (deferred pull provenance and clock fit). |
| 2026-08-19 | Timeline fusion: `analysis.segmentation.fusion[]`, `segmentation.version` 5 and `phases[].timing` (only at version 5). Also `analysis.bindings[].hackMotion` and the `hm.*` metric keys. |
| 2026-08-27 | `capture.shotSource` `5` = Ppcp. |
| 2026-09-01 | Phone clip `streams[]` element with `origin`. |
| 2026-09-04 | Metric-series validity: `analysis.metrics[].valid` (**optional** int[] 0/1, parallel to `t_us`) added — `0` marks a sample **bridged across a gated or absent run**, where the geometry could not be resolved and the grid was filled from its neighbours to keep the curve continuous. Written only when at least one sample is invalid, so every metric with nothing to mark serialises byte-identically and every pre-existing file reads back as fully valid (`metric_presentation_honesty.md` §5.1; the mask, not `value[]`, carries the absence). Additive — no schema-version bump. |
| 2026-09-06 | `captureIntegrity` top-level block. |
| 2026-09-06 | `pinpoint.swingsummary/3`: + `dataWarning` (`85f7c290`). |
| 2026-09-09 | `analysis.versions` (`pose`, `ball`, `shaft`), which gates re-analysis reuse; `analysis.ball.launch`. Additive. |
| 2026-09-10 | `launchMonitor.kind` `gspro`. |
| 2026-09-15 | `analysis.impact` added (the impact camera's ball + club track and path, `ImpactRunner`), `analysis.versions.impact`, `analysis.timings.impactMs`; the impact stream's `capture` gained `gainDb`/`gainSource`/`gamma`/`strobe`/`viewGain`/`note`/`measuredFps`. Additive — no schema-version bump. |
| 2026-09-16 | `pinpoint.swingsummary/4`: + `metrics`, `rating`, `note`, `lmDeviceKind`, `dataWarningDetail`, so loading a session never parses a whole document (`26518731`). |
| 2026-09-17 | Kinematic sequence: `analysis.kinematicSequence` (**optional**, additive — absent when no route produced a node) holds the ordered peaks of four new `analysis.metrics[]` series `pelvisAngularSpeed` / `thoraxAngularSpeed` (`°/s`, SIGNED, opening toward the lead side positive) and `leadArmAngularSpeed` / `clubAngularSpeed` (`°/s`, unsigned magnitudes about the swing-plane normal), each carrying `sigma` and a downswing `valid` domain. Times inside the object (`impactUs`, `nodes[].tPeakUs`) are window-relative like every other `analysis` `t_us`. One serialisation helper (`kinematic_sequence_json.h`) is shared by the document writer, the live `analysisDetail` map and the reload. See `docs/design/kinematic_sequence_design.md`. |
| 2026-09-17 | `analysis.club.positions[].timing`; `ShaftSampleFlags` gains `0x200` Implausible. |
| 2026-09-21 | Down-the-line camera: `analysis.poseDtl` (the DTL 2-D pose, the same shape and serialiser as `pose2d`, written only when a DTL pose ran) and `analysis.clubDtl` (`pinpoint.clubDtl/1`, the DTL shaft track with every absence tiered and reasoned — the same bytes as SwingLab's `club_dtl.json` — written whenever the stage ran), plus `analysis.versions.poseDtl` `{code, model}` and `versions.shaftDtl`. Additive — a single-camera swing is byte-identical. |
| 2026-09-21 | `analysis.club3d` (`pinpoint.club3d/1`): the shaft in 3-D from the two cameras' image angles, in the cameras' frame (X face-on image-right, Y face-on view ray, Z up), with the backswing / downswing / address planes and per-frame disagreements, plus `versions.shaftFusion`. Written whenever the stage ran, valid or not. Uncalibrated: inclinations are measured, headings depend on the assumed DTL yaw. Additive. |
| 2026-09-23 | `swing.json` is written compact, no indentation (`173244f0`). Same tree; about 2.4× smaller. |
| 2026-09-23 | **Storage: `swing.ppsw` replaces `swing.json` (`4d8ba169`).** All writes now go through `SwingStore::save` and produce `swing.ppsw`, a lossless libppswing container for the same tree (format 1.0 draft). `swing.json` is read for as long as one exists, and `swing.ppsw` wins when both exist. Writing a JSON-era swing converts it and deletes `swing.json` and `swing_summary.json`. A new top-level **`summary`** block (`pinpoint.swingsummary/4`) is written into every `swing.ppsw`, in its root chunk, and replaces the `swing_summary.json` sidecar for converted swings. The zip export still contains `swing.json`, built from the `.ppsw`. The 170 swing documents on the share were converted and checked by both the C++ and the Python decoder (5.18 GB → 0.49 GB; `swing_storage_impl.md` Stage 3). No change to `pinpoint.swing/2` or `pinpoint.analysis/3`. |
| 2026-09-23 | IMU sidecars `imu_<alias>.csv` / `.bin` no longer written; samples are always inline in `streams[].samples` (`6e9e2c1a`). |
| 2026-09-23 | Session archiving (`edcc9934`): an archived swing becomes a stub `swing.ppsw` with a top-level `archive` block, and the session gets `archived.json` (`pinpoint.archive/1`). Additive. |
| 2026-09-23 | **Save pose keypoints** off: `pose2d`, `poseDtl`, `versions.pose` and `versions.poseDtl` are left out of the document. |
| 2026-09-26 | `analysis.skeleton3d` (`pinpoint.skeleton3d/1`): a 27-joint, 48-DoF Y-bot skeleton fitted to both cameras, the shafts, the feet and any IMUs. Joints in mm, world frame with its origin at the face-on camera, cameras self-solved (`camera.calibrated` always `false`). Plus `versions.skeleton3d`. Written whenever the stage ran; a refusal carries `valid: false` and `reason`. Additive. |
| 2026-09-26 | `athlete.heightM` (analysis-only header). |
| 2026-09-29 | `analysis.club.wedgeObs[]` and `analysis.club.addressBall`. |
| 2026-09-30 | Refused face-on shaft tracks are now written (`valid: false` + `refused`), with `club.diag`; `ShaftSampleFlags` gains `0x400` BallSeen. |
| 2026-09-30 | `analysis.clubDtl.synth3d[]` + `summary.synth3d`: the fused 3-D shaft projected into the DTL tile — synthesised, read by no metric, `preview` when the DTL camera was uncalibrated. Additive. |
| 2026-10-01 | `analysis.addressMarks` (`pinpoint.addressMarks/1`): the body's outer edges at hip height on each camera's address frame, from the person mask — the coach lines' input — plus `versions.addressMarks`. Written whenever the stage ran, `found: false` per view as the record. Also: `clubDtl.frames[].sigTheta` / `pGross` and `club3d` `frames[].sigmaDeg`, `planes.*.inclSigmaDeg`, `address.inclSigmaDeg` (shaft uncertainty), each written only where assessed; `club3d.eta`, `items` and `frames[].etaDeg` (the out-of-plane curve). Additive. |
| 2026-10-01 | Uncertainty: `metrics[].sigmaKind`; `phaseSamples[].sigma` / `sigmaKind` / `grossRisk`; `club.samples[].sigTheta` / `pGross` / `tier`; `club.positions[].sigmaTUs`; `club.addressBallSigmaPx`, `synthKappa`; the plane's σ fields. |
| 2026-10-02 | `clubDtl`: the HELD / OCCLUDED_WRIST / OCCLUDED_ROW tiers, `bands[].edge`, `frames[].runPx` and `summary.continuous` (present only when a continuous-track rule ran). `club3d.camera` gains `yawDeg` / `pitchDeg` / `rollDeg` / `offset` / `calibrated` beside the original `dtlYawDeg` / `dtlPitchDeg` (shaftFusion v4). Additive. |
| 2026-10-05 | `device.placementSlot` no longer written (role-keyed placement). |
| 2026-10-06 | `analysis.timings.poseDtlMs`, `timings.pose.{faceOn,dtl}` and `timings.stages[]`. |
| 2026-10-06 | `clubDtl.summary.topOutOfView` (only when the ladder has a P4): the DTL camera could not see the top of the swing. It raises a shot-card ⚠ without excluding the shot. `poseDtl` is posed on schedule B (dense P2 − 50 ms … P8 + 150 ms, backswing every 2nd frame, the rest every 4th; poseDtl v2), so DTL frames are no longer evenly spaced. |
| 2026-10-08 | Video `capture.blackLevelLift` added, `capture.gamma` retired. |
| 2026-10-13 | `review.excluded`, `review.clubOverride`; the same two fields in `summary` (`881f6cab`, additive to /4, no bump). |
| 2026-10-15 | `metrics[].routeId` / `routeDirect` / `withheld`. |
| 2026-10-16 | Gate records: `analysis.gates` + `analysis.gatesVersion` (**optional**, additive) — one record per gate firing (`gate`, `subject`, `effect`, `evidence`, `bound`, and `covers` for a cascade), times in evidence ms from impact. First written by the kinematic sequence (`gatesVersion` 1: sighted band, σ ceiling, spike guard, rising at the end, route switches, club floor, club withheld, two-camera refusals and withdrawals); `gatesVersion` 2, the same day, stamps every analysis and covers every per-swing gate, with the `shot.*` and `lm.*` records merged in at write. One helper (`gate_record.h`) for the document, the live map and the reload. See `docs/design/trust_and_gates_design.md`. |
| 2026-10-16 | `launchMonitor.dropped[]`; `captureIntegrity.firstHoleMs`. |
| 2026-10-19 | `skeleton3d` refuses without the DTL camera (v15: "two cameras or no skeleton"), keeps only address − 150 ms … P8 + 25 ms (v16), and gains `diagnostics.gripLockP90Mm` / `gripLockMaxMm`. Shape unchanged otherwise. |
| 2026-10-20 | `summary` + `devices` (`92407144`). Additive to /4; readers derive it for older blocks. |
| 2026-10-22 | `addressMarks.dtl` gains TPI's spine-angle line `tailboneX/Y`, `neckX/Y`, `backWalks` (addressMarks v2). `null` when not measured; an older record reads as NaN. Additive. |
| 2026-10-23 | `addressMarks` → `pinpoint.addressMarks/2`: `dtlPhases[]` (the DTL butt and back line at P1–P7, rear side frozen from address) and `phaseNeckFrac` (v3), plus the head outline `headFound` / `headCx` / `headCy` / `headR` / `crownY` / `headRowY` / `headLeftX` / `headRightX` / `headRefused` on each address view (v4). Readers take /1 and /2. Also `analysis.dtlRuler` (`pinpoint.dtlRuler/1`): cm per DTL pixel and its source, with `cmPerPx` `null` when there is no ruler; its version key is `versions.dtlPosture`. Additive. |
| 2026-10-24 | `analysis.club.filter` (the club rate filter's diagnostic); `ShaftSampleFlags` gains `0x800` RateFiltered. |
| 2026-10-25 | `kinematicSequence.nodes[].probable`: the chest's bridged peak across a gap at impact. |
| 2026-10-26 | Trunk stand-tall solve: `calibrationSource` / `trunkSolve` on the IMU stream's `device` block and on `analysis.bindings[]`. |
| 2026-10-27 | Every session starts as Wrist: `capture.sessionType` is `1` on new swings and selects nothing. |
| 2026-10-31 | `captureIntegrity` coverage keys; video `frames.clock: "clip"`. |
| 2026-10-31 | `metrics[].lineAngle`. |
| 2026-11-02 | `launchMonitor.kind` `premier`; `launchMonitor.contactAtMs`. |
| 2026-11-03 | Follow Premier's club: `updateLaunchMonitor` may replace `capture.club` and a seeded `review.club`. |
| 2026-11-06 | `analysis.path`; `timings.stages[].source`. |
| 2026-11-09 | `summary` + `handedness` (`b0444b72`). Additive to /4; readers derive it for older blocks. |
| 2026-11-12 | `versions.shaftDirection` and the `shaftDirection` metric series (route `faceOn+dtl`, σ Propagated, phase samples at P2 and the top) with its two trust records, `club3d.cameraAssumed` and `club3d.shaftDirectionFromFit`, in `analysis.gates`. Additive; a single-camera swing is byte-identical. |
| 2026-11-12 | `versions.skeleton3dBody` and the readings from the 3-D body, all route `faceOn+dtl`, σ Propagated on the series and every phase sample: `shoulderPlaneAngle3d` (read at the top), `shoulderLineYaw` (one phase sample at address, no curve; σ includes the cameras' assumed yaw) with the trust record `body3d.cameraAssumed` in `analysis.gates`, `body3d.fromFit` (Downgraded, evidence `code` / `route` / `replaces` / `replacedRecords`) on a reading the 3-D fit supplied where the single-camera route had none — the refusals that route left are moved off the reading and listed in `replacedRecords` — a 3-D `leadKneeFlexion` that replaces the down-the-line one where the fit reads through impact, a 3-D `spineSideBend` only where the face-on one is absent, and the research-only `thighTurn3dLead` / `thighTurn3dTrail`. Additive; a single-camera swing is byte-identical. |
| 2026-11-12 | The trunk sensors' readings (planned-metrics promotion W3): `spineForwardBend` (the chest sensor's long axis in the vertical plane through the ball) and `spineSideBend` (the chest's frontal tilt against the hips'), route `trunkImus`, `routeDirect` true, on the sensors' own samples, phase samples at every ladder phase, σ Propagated on the series and every phase sample, with the Downgraded records `body.chestBendSite` (forward bend) and `body.trunkMountYawAssumed` (both bends; evidence `deltaTier`, `sigmaDeltaDeg`, and with a side bend `thoraxAddrDeg`, `pelvisAddrDeg`). Where they exist they are the only series under those keys (the down-the-line, face-on and 3-D ones are not written, and the records that spoke for those are taken off the keys) — unless that sensor is probably worn face-in, or the sensor series lacks a phase reading where the camera series would be graded: then the camera series is written and the sensor bend is not. The IMU-tier turn series now carry `routeId` (`pelvisImu`, `thoraxImu`, `trunkImus`) and `routeDirect`. `versions.hipInternalRotation` and `trailHipInternalRotation` / `leadHipInternalRotation` (route `pelvisImu+faceOn+dtl` or `pelvisImu`, σ Propagated, phase samples at the top and impact), with the trust record `body.thighTurnAssumed` (Downgraded; evidence `code` `noFit` / `thighUnread`, `sigmaDeg`) in `analysis.gates`; `body.pelvisFaceIn` / `body.thoraxFaceIn` may now cover the bends and the hip rotations. Nothing was ever written under `hipInternalRotation`. Additive; a swing with no trunk sensor is byte-identical. |
| 2026-11-13 | Three more trunk-sensor readings (planned-metrics second pass W4), each under the camera reading's own key and sign, `routeDirect` true, on the sensors' own samples, phase samples at every ladder phase where the curve reads, σ Propagated on the series and every phase sample: `shoulderPlaneAngle3d` route `thoraxImu` (the chest's left-right axis against level, + the trail shoulder higher); `hipLineTilt` route `pelvisImu` (the hip sensor's left-right axis in the face-on plane, + the trail hip higher; `valid` 0 where the hip line has turned edge-on and after Impact); `secondaryAxisTilt` route `thoraxImu` (the chest's long axis in the face-on plane, + away from the target; `valid` 0 after Impact). The face-on plane's target line is square to that sensor's address front. New Downgraded record `body.chestShoulderSite` (subject and covers `shoulderPlaneAngle3d`, evidence `code` `upperBack`); `body.chestBendSite` may also cover `secondaryAxisTilt` (its subject the first of its covers); `body.trunkMountYawAssumed` may cover all three; `body.thoraxFaceIn` may cover `shoulderPlaneAngle3d` and `secondaryAxisTilt`, `body.pelvisFaceIn` `hipLineTilt`. As for the bends, where a sensor reading exists it is the only series under its key (the face-on and 3-D ones are not written, and the records that spoke for them — also a camera route's refusal when that route wrote no series — are taken off the key), unless that sensor is probably worn face-in or lacks a phase reading the camera series has. `versions.skeleton3dBody` 2. Additive; a swing with no trunk sensor writes the same series and records (on a two-camera swing only `versions.skeleton3dBody.code` changes, 1 → 2). |
| 2026-11-13 | The 3-D trail knee (planned-metrics second pass W5): `trailKneeFlexion` route `faceOn+dtl` (`routeDirect` false), 180° − the angle at the TRAIL knee between hip and ankle off the two-camera fit, + = more bend (the down-the-line series' definition and sign), phase samples at Address, Top and Impact, σ Propagated on the series and every phase sample. While the cameras are uncalibrated (every swing today) the down-the-line `trailKneeFlexion` (no route id) is written exactly as before whenever it exists, and the 3-D one is written only when there is none, with `body3d.fromFit` (`replaces` `dtl`). With calibrated cameras the 3-D one would REPLACE the down-the-line one when it has a reading at Address AND the Top: every trail-knee measure is a change from address, so both ends come from one route. The 3-D `spineSideBend` is now also computed beside the trunk sensors' (`trunkImus`); the precedence keeps one of them, so only one is written. `versions.skeleton3dBody` 3. A single-camera swing is byte-identical. A two-camera swing with a down-the-line trail knee writes the same series (only `versions.skeleton3dBody.code` changes, 2 → 3); one with none gains the fit's trail knee and its record. |
| 2026-11-13 | New Downgraded gate record `body.targetFromAddress` (planned-metrics second pass W4c), fired with the trunk sensors' readings whose target line or ball plane is that sensor's own front at address: covers whichever of `spineForwardBend`, `hipLineTilt`, `secondaryAxisTilt` were written (subject the first). Evidence `code` `squareAtAddress`, and `chestAddressHeadingDeg` / `hipAddressHeadingDeg` (°, lead-relative, + open: that sensor's heading at address against stand tall, drift included) while a reading from that sensor is covered — diagnostics only, never in the shown sentence, because they are not the set-up's aim. Withdrawn from a key with the sensor reading when the camera reading stays the answer. Additive; no series or version changed. |
| 2026-11-13 | New metric `shoulderTiltVsBend` (planned-metrics second pass W7, Mark's decision): `shoulderPlaneAngle3d` at the top − `spineForwardBend` at address, in degrees, + when the shoulders turned more steeply than the posture sets them up to. ONE phase sample (phase Top, at the top's ladder instant, or the tilt's labelled top when the ladder has none) and NO curve (`t_us` and `value` empty), as the setup readings are written. Each half is the series that stands after the trunk-sensor precedence, read as the diagnostics grade it (the ±15 ms median of the valid samples at the ladder instant, else the labelled phase sample); with either half missing nothing is written. `routeId` names the pair: `trunkImus` (both halves from the chest sensor), `thoraxImu+dtl`, `faceOn+dtl`, `trunkImus+faceOn+dtl`; `routeDirect` true on the first two. σ: the two halves' graded σ in quadrature, on the phase sample and the series, `sigmaKind` Propagated (AssumedInput when a half's is); when a half has only a noise-floor series σ, a series σ with no `sigmaKind` and none on the phase sample; no σ when a half has none. `withheld` copies a half's. Every Downgraded or Withheld record that covers a half also covers `shoulderTiltVsBend`. When a half is missing and nothing is written, every Refused, Bounded or Withheld record that covers the missing half also covers `shoulderTiltVsBend`, so the reading's absence carries that half's reason (e.g. `posture.address`). On two-camera swings the down-the-line bend states no σ, so the reading has none. New Downgraded record `body.tiltBendMixedSources` (subject and covers `shoulderTiltVsBend`; evidence `tiltFrom`, `bendFrom`: the halves' routes, `dtl` for a bend with none stated) when the halves came from different instruments. New `versions.shoulderTiltVsBend` 1. Additive: every other series and record is written as before, and a swing with no shoulder plane at the top or no forward bend at address is byte-identical. |
