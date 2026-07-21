# Swing library folder layout reference

> **Date:** 2026-07-21 · **Applies to:** the on-disk layout under the athlete library path (`AppSettings::athleteLibraryPath`) · **Companion to** [`swing_json_schema.md`](swing_json_schema.md), which documents the `swing.json` manifest field-by-field.

This document describes the **directory structure** — the athlete, session, and swing folders and every file that lives in them. For the contents of the `swing.json` manifest itself, see [`swing_json_schema.md`](swing_json_schema.md).

## The three-level tree

```
<athleteLibraryPath>/                         ← Settings → athlete library path (e.g. /mnt/swingdata)
  <Athlete-Name-sanitised>/                   ← one folder per athlete
    <session-folder>/                         ← one folder per session (a day's worth of shots)
      swing_0001/                             ← one folder per captured shot
        swing.json
        swing_summary.json
        Face-On.mp4
        thumb.jpg
        …
      swing_0002/
      …
```

Each level is created lazily: the athlete folder on first save for that athlete, the session folder at session start (or on the first save), and a `swing_NNNN/` folder per accepted shot. Source of truth for allocation and naming: `src/Export/swing_paths.{h,cpp}` (`SwingPaths`).

---

## Athlete level

`<athleteLibraryPath>/<Athlete-Name>/` — the athlete name is sanitised for the filesystem (`SwingPaths::sanitise`, e.g. spaces → `-`, so *Mark Liversedge* → `Mark-Liversedge`). The athlete's identity/UUID lives in QSettings, not in a file here; the folder name is a display convenience and is **not** the identity key (the `athlete.uuid` inside each `swing.json` is).

There are normally **no files** directly at the athlete level — only session subfolders. (Any `corpus.json`, `*.json.prenorm`, `*.json.prebackfill`, or similar you find are **tooling/corpus artifacts** — swinglab manifests and migration backups — not written by the app.)

---

## Session level

One folder per session. A session is "a sitting" — typically a day's shots for one session type; all swings captured during one app run share one session folder.

### Folder name

Composed from the `storage/sessionNamingPattern` setting (default `date-name-type`) plus a per-day uniqueness counter `_NN`:

| Pattern | Example |
|---|---|
| `date-name-type` (default) | `2026-07-10_Mark-Liversedge_Wrist_02` |
| `date-type-name` | `2026-07-10_Wrist_Mark-Liversedge_02` |
| `name-date-type` | `Mark-Liversedge_2026-07-10_Wrist_02` |
| `date-only` | `2026-07-10_02` |

- The leading `yyyy-MM-dd` (in every pattern except where the name comes first) is the session **date**, and the session-list picker reads it directly to label an as-yet-unindexed row.
- The trailing `_NN` disambiguates multiple sessions of the same base on one day (e.g. two Wrist sessions → `_01`, `_02`). "Extend today's session" reuses the latest matching folder; "New session" allocates the next `_NN`.
- The type token (`Swing` / `Wrist` / `GRF` / `Coach`) is the human label for `SessionController::Type`.

### Files at the session level

**None written by the app** — a session folder contains only `swing_NNNN/` subfolders. An empty session folder (started but captured nothing) is trashed on exit. Recency is tracked by folder **mtime**, not by an index file (`SwingDocReader::sessionDirs` / `latestSessionDir`).

---

## Swing level (`swing_NNNN/`)

One folder per accepted shot, `swing_0001`, `swing_0002`, … (1-based, zero-padded to 4). The index also appears inside the manifest as `swing.index` / `swing.id`.

| File | Written by | When |
|---|---|---|
| `swing.json` | `SwingDocWriter::writeSwingJson` | **Always.** The unified per-shot manifest (raw capture + `analysis`). Schema `pinpoint.swing/2`; see [`swing_json_schema.md`](swing_json_schema.md). Rewritten in place by re-analysis (replaces only `analysis`) and by review edits (replaces only `review`). |
| `swing_summary.json` | `SwingDocReader::writeSwingSummary` | **Effectively always** (written alongside every `swing.json` write, and self-healed on read). A ~few-hundred-byte cache of the session-picker scalars so the drawer never parses the big manifest. Schema `pinpoint.swingsummary/1`. **Pure cache — safe to delete**; regenerates on next write or read. Fields documented in [`swing_json_schema.md`](swing_json_schema.md) § "`swing_summary.json` — the picker sidecar". |
| `<alias>.mp4` | `SwingExporter` | **Always** — one encoded H.264 clip per camera. `<alias>` is the camera's display name / stream stem (e.g. `Face-On.mp4`), matching `streams[].alias` / `streams[].file`. |
| `thumb.jpg` | `SwingExporter` | **Always** — the impact-nearest still (≤480 px, face-on camera preferred). Referenced by `thumbnail.file`. Written via `QImage::save` (not `cv::imwrite` — see the exporter notes in CLAUDE.md). |
| `<alias>.raw` | `SwingExporter` | When raw-frame saving is on — the undecoded sensor frames as a single concatenated blob (GB-scale). Layout described by `streams[].raw`. Used by re-analysis for a pixel-exact re-decode. |
| `<alias>.ballbase.f32` | `SwingExporter` | When the face-on ball detector had a learned empty-mat baseline at capture. Raw row-major `float32`, `w*h*4` bytes over the search ROI. Referenced by `streams[].setup.ballDetection.baseline`; loaded by `BallRunner` on re-analysis. |
| `imu_<alias>.csv` / `imu_<alias>.bin` | `SwingExporter` | Only when `imuDataFormat` ≠ `json`. Otherwise IMU samples are inlined into `streams[]` and there is no sidecar. |
| `truth.json` | Markup / shaft-lab tooling | Ground-truth annotations for a labelled corpus swing — **not** written by the capture pipeline. Separate schema ([`truth_json_schema.md`](truth_json_schema.md)). |

### Size profile (why the sidecar exists)

`swing.json` is dominated by the offline `analysis` blocks — on a Wrist swing, `analysis.pose2d` (per-frame whole-body keypoints) alone can be ~13 MB, and a whole file 30 MB+. The session picker only needs five scalars per swing (score, video presence, time, club, thumbnail), so it reads `swing_summary.json` (~hundreds of bytes) instead. The heavy media (`.mp4`, `.raw`) is only touched during replay/re-analysis, and `.raw` is streamed from disk, never held whole in RAM.

---

## What travels where

| Consumer | Reads |
|---|---|
| Session picker / "choose a session" drawer | `swing_summary.json` (falls back to `swing.json` to self-heal a missing/stale sidecar) — `SessionReviewController`, `SwingDocReader::readSwingSummary`. |
| Live carousel reload at startup / session entry | `swing.json` per swing of one session — `ShotListModel::loadSessionDir`. |
| Replay | `swing.json` (`analysis` overlays) + `<alias>.mp4` (or `<alias>.raw`) — `disk_replay_source.cpp`. |
| Re-analysis | `swing.json` + `<alias>.raw` (or `.mp4`) + `.ballbase.f32` — `swing_reanalyzer.cpp` (`SwingDiskLoader`). |
| Session **zip export** (carousel ⋯ menu) | Per swing: `swing.json`, `thumb.jpg`, selected `<alias>.mp4`, and `imu_*.csv/.bin` — **never** `.raw` (GB-scale) and **never** `swing_summary.json` (cache). Selection: `src/Export/swing_zip_exporter.cpp` (`includeMember`). |

---

## Deletion / recovery

- **Trashing a swing or a session** moves the whole folder to the OS trash (`QFile::moveToTrash`), recoverable from there — not a hard delete. See `SessionReviewController::trashSession` and the carousel's per-shot trash.
- **`swing_summary.json` is disposable** — deleting it costs one re-derivation on next access, nothing else.
- An empty session folder (no swings captured) is trashed automatically on session end.
