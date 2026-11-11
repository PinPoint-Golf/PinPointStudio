# Swing library folder layout reference

> **Date:** 2026-11-10 · **Applies to:** the on-disk layout under the athlete library path (`AppSettings::athleteLibraryPath`) · **Companion to** [`swing_json_schema.md`](swing_json_schema.md), which documents the swing document (`swing.ppsw`, legacy `swing.json`) field by field.

This document describes the **directory structure** — the athlete, session, and swing folders and every file that lives in them. For the contents of the swing document itself, see [`swing_json_schema.md`](swing_json_schema.md).

## The three-level tree

```
<athleteLibraryPath>/                         ← Settings → athlete library path (e.g. /mnt/swingdata)
  <Athlete-Name-sanitised>/                   ← one folder per athlete
    <session-folder>/                         ← one folder per session (a day's worth of shots)
      swing_0001/                             ← one folder per captured shot
        swing.ppsw                            ← the swing document (legacy swings: swing.json + swing_summary.json)
        Face-On.mp4
        thumb.jpg
        …
      swing_0002/
      …
      diagnostics.json  work_ons.json  club_baseline.json  report/   ← session files
    tendencies.json  swing_themes*.json  …                           ← athlete files
  .pinpoint-trash/                            ← only on a volume with no system trash (the share)
```

Each level is created lazily: the athlete folder on first save for that athlete, the session folder at session start (or on the first save), and a `swing_NNNN/` folder per accepted shot. Source of truth for allocation and naming: `src/Export/swing_paths.{h,cpp}` (`SwingPaths`).

---

## Athlete level

`<athleteLibraryPath>/<Athlete-Name>/` — the athlete name is sanitised for the filesystem (`SwingPaths::sanitise`, e.g. spaces → `-`, so *Mark Liversedge* → `Mark-Liversedge`). The athlete's identity/UUID lives in QSettings, not in a file here; the folder name is a display convenience and is **not** the identity key (the `athlete.uuid` inside each swing document is).

The app writes a few athlete-level files beside the session folders: `tendencies.json`, `swing_themes*.json`, `fault_profile.json` and `outcome_corrections.jsonl` (see [`swing_json_schema.md`](swing_json_schema.md) § "Files beside the swings"). (Any `corpus.json`, `*.json.prenorm`, `*.json.prebackfill`, or similar you find are **tooling/corpus artifacts** — swinglab manifests and migration backups — not written by the app.)

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
- The type token (`Swing` / `Wrist` / `GRF` / `Coach`) is the human label for `SessionController::Type`. Session types are gone from the app; every session has started as `Wrist` since 2026-10-27, so new folders carry `Wrist`.

### Files at the session level

Besides the `swing_NNNN/` subfolders the app writes `diagnostics.json` (the session's diagnostic ledger), `club_baseline.json`, `work_ons.json`, `report/` (the report PDF and `report_meta.json`) and, on an archived session, `archived.json`. Schema versions and writers are in [`swing_json_schema.md`](swing_json_schema.md) § "Files beside the swings". An empty session folder (started but captured nothing) is trashed on exit. Recency is tracked by folder **mtime**, not by an index file (`SwingDocReader::sessionDirs` / `latestSessionDir`).

---

## Swing level (`swing_NNNN/`)

One folder per accepted shot, `swing_0001`, `swing_0002`, … (1-based, zero-padded to 4). The index also appears inside the manifest as `swing.index` / `swing.id`.

| File | Written by | When |
|---|---|---|
| `swing.ppsw` | `SwingStore::save` (via `SwingDocWriter::writeSwingJson` and the other updaters) | **Always**, since 2026-09-23. The per-shot swing document (raw capture + `analysis` + an embedded `summary` block for the session list), in the libppswing binary format; schema `pinpoint.swing/2`. Rewritten whole and atomically by re-analysis and by review, launch-monitor and clip updates. See [`swing_json_schema.md`](swing_json_schema.md) § "On disk" for how to read it outside the app. |
| `swing.json` | — (legacy) | Only in a swing captured before 2026-09-23 and not yet converted. Read, never written; the first rewrite (or a library conversion) replaces it with `swing.ppsw` and deletes it. |
| `swing_summary.json` | `SwingDocReader::writeSwingSummary` | **JSON-era swings only** — the list cache beside a legacy `swing.json` (`pinpoint.swingsummary/4`). Deleted with the `swing.json` on conversion. **Pure cache — safe to delete.** |
| `swing_phasegrid.json` | `savePhaseGrid` (`measure_sample.cpp`) | A rebuildable cache of the per-phase metric grid used by diagnostics and charts. Safe to delete. |
| `<alias>.mp4` | `SwingExporter` | **Always** — one encoded H.264 clip per camera. `<alias>` is the camera's display name / stream stem (e.g. `Face-On.mp4`), matching `streams[].alias` / `streams[].file`. |
| `thumb.jpg` | `SwingExporter` | **Always** — the impact-nearest still (≤480 px, face-on camera preferred). Referenced by `thumbnail.file`. Written via `QImage::save` (not `cv::imwrite` — see the exporter notes in CLAUDE.md). |
| `<alias>.raw` | `SwingExporter` | When raw-frame saving is on — the undecoded sensor frames as a single concatenated blob (GB-scale). Layout described by `streams[].raw`. Used by re-analysis for a pixel-exact re-decode. |
| `<alias>.ballbase.f32` | `SwingExporter` | When the face-on ball detector had a learned empty-mat baseline at capture. Raw row-major `float32`, `w*h*4` bytes over the search ROI. Referenced by `streams[].setup.ballDetection.baseline`; loaded by `BallRunner` on re-analysis. |
| `imu_<alias>.csv` / `imu_<alias>.bin` | — (retired) | No longer written since 2026-09-23; IMU samples are always inline in `streams[]`. Older swings may still hold them. |
| `truth.json` / `truth_dtl.json` | Markup lab (`markup_truth.cpp`) | Hand-marked ground truth for the face-on and DTL views — **not** written by the capture pipeline. Separate schema ([`truth_json_schema.md`](truth_json_schema.md)). |

A phone clip filed over PPCP lands as `<alias>.mp4` / `.mov` / `.mkv` beside the others. An **archived** swing keeps only its `swing.ppsw` stub and thumbnail. The full file table is in [`swing_json_schema.md`](swing_json_schema.md) § "Companion files".

### Size profile (why the document is chunked)

The document is dominated by the offline `analysis` blocks — on a Wrist swing, `analysis.pose2d` (per-frame whole-body keypoints) alone was ~13 MB as JSON, and a whole `swing.json` 30 MB+. `swing.ppsw` compresses it (a Wrist swing is about 1.4 MB on disk) and stores the big subtrees as separate chunks, so the session list reads only the root chunk's `summary` block and never decompresses the pose. The heavy media (`.mp4`, `.raw`) is only touched during replay/re-analysis, and `.raw` is streamed from disk, never held whole in RAM.

---

## What travels where

| Consumer | Reads |
|---|---|
| Session picker / "choose a session" drawer | The `summary` block in `swing.ppsw` (root chunk only); for a legacy swing, `swing_summary.json`, self-healed from `swing.json` — `SessionReviewController`, `SwingDocReader::readSwingSummary`. |
| Live carousel reload at startup / session entry | The swing document per swing of one session — `ShotListModel::loadSessionDir`. |
| Replay | The swing document (`analysis` overlays) + `<alias>.mp4` (or `<alias>.raw`) — `disk_replay_source.cpp`. |
| Re-analysis | The swing document + `<alias>.raw` (or `.mp4`) + `.ballbase.f32` — `swing_reanalyzer.cpp` (`SwingDiskLoader`). |
| Session **zip export** (carousel ⋯ menu) | Per swing: a `swing.json` built from the document, `thumb.jpg`, selected `<alias>.mp4`, and any old `imu_*` sidecars — **never** `.raw` (GB-scale), `swing.ppsw` itself, or the caches. Selection: `src/Export/swing_zip_exporter.cpp` (`includeMember`). |

---

## Deletion / recovery

- **Trashing a swing or a session** moves the whole folder to the OS trash (`QFile::moveToTrash`), recoverable from there — not a hard delete. On a volume with no system trash (the network share) it is renamed into `<library>/.pinpoint-trash/<athlete>/` instead (`SwingPaths`, `swing_paths.cpp`). See `SessionReviewController::trashSession` and the carousel's per-shot trash.
- **`swing_summary.json` and `swing_phasegrid.json` are disposable** — deleting one costs a re-derivation on next access, nothing else.
- **Their guard survives a copy.** Both record the document's size and mtime when they were built and are rebuilt when the document no longer matches. Every reader compares through `SwingStore::sameDocumentStamp` (`swing_store.h`): the same size and the same mtime, or — when the document's mtime has no milliseconds, as after a copy with macOS's `rsync -a` (openrsync, which truncates mtimes to the second) — the same second. A copied or restored swing keeps its sidecars; a copy that resets mtimes (plain `cp`) rebuilds them (issue #174). The session's `diagnostics.json` ledger stamps each row with the document it was graded from (`gradedFrom`) and compares by the same rule, so a copied session is not regraded on its first open.
- An empty session folder (no swings captured) is trashed automatically on session end.
