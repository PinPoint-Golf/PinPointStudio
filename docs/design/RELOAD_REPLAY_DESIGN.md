# Reload & Replay — Design & Plan

> Picks up the two deferred M1 items (`SHOT_ANALYZER_M1_WRIST.md §11 D/E`): make
> **reloaded shots fully usable** — rating/note that survive a restart — and **replay a shot
> loaded from disk** (which has MP4s + `swing.json` but no in-memory `SwingWindow`). This doc
> is self-contained so a fresh session can start implementing without re-discovering the M1
> code. Companion: `SHOT_ANALYZER_DESIGN.md` (as-built notes), `SHOT_ANALYZER_M1_WRIST.md`.

## 1. Goal & scope
- **Reload completion:** rating/note persist into `swing.json` and reload; (minor) an
  export-fail degraded header so analysis-only shots still persist.
- **Replay-from-disk:** a reloaded shot can be replayed/scrubbed with the metric graph synced
  — the same scrub-with-video UX as fresh shots, but sourced from MP4 instead of the live ring.
- **Out of scope:** trimming/editing, external sharing, re-analysis of reloaded shots.

## 2. Current state (built in M1)

### Reload — read-only, works
- `SwingDocReader` (`src/Export/swing_doc.{h,cpp}`): `readSwingJson(swingDir) →
  PersistedShot {ok, swingDir, ordinal, timestampLabel, club, hasVideo, thumbnailPath, score,
  metrics, analysisDetail}`; `findSwingDirs(sessionDir)`; `latestSessionDir(root, athlete)`.
- `ShotListModel::addPersistedShot(...)` prepends a reloaded shot; the `Shot` struct carries a
  **`swingDir`** field — the on-disk folder, the key to everything below.
- `main.cpp` startup reloads the **current athlete's most recent session** into the carousel.
- **Gaps:** rating/note come back **cleared** (not persisted); reloaded shots **can't replay**.

### Replay — works for FRESH shots only
- `ShotProcessor::startReplay()` drives a **¼× linear** replay off the **live frozen
  `SwingWindow`** (ring slice). `onReplayTick` (~60 Hz) sets
  `m_replayPositionUs = m_replayWindowStartUs + virtualTimeUs` (**absolute** EventBuffer µs).
- Published: `isReplaying`, `replayPositionUs/StartUs/EndUs/ImpactUs`, `replayAnalysisDetail`.
- Video: `CameraInstance::displayReplayFrame()` pushes window frames to the live tiles.
- Graph: `PpMetricGraph.playheadUs` binds to `replayPositionUs`; both the review-panel graph
  and the `ScreenWrist` in-replay graph use it.
- **Trigger chain:** `PpShotPanel.replayRequested(mode)` → `PpShotCarousel.replayRequested(
  shotId, mode)` → wired in `Main.qml`/`VideoPage.qml` → `ShotProcessor`. **A reloaded shot has
  no `SwingWindow`, so this path cannot serve it.**

## 3. Key insights to carry forward (don't re-discover)

### `swing.json` schema (`pinpoint.swing/2`) — written once at the analyzer∥exporter join
```
{ schema:"pinpoint.swing/2",
  swing:{index,id}, athlete:{name,uuid,handedness}, session:{dir},
  clock:{ t0_us, unit:"us", wallclock:ISO },            // t0_us = ABSOLUTE window start
  window:{ start_us:0, end_us:durationUs },
  thumbnail:{ file:"thumb.jpg", t_us },                 // window-relative
  streams:[
    { kind:"video", alias, file:"<alias>.mp4",
      source:{serial,pixelFormat,width,height}, capture:{fps_num,fps_den},
      playback:{fps:30}, processing:{demosaic,restorer},
      frames:{ count, t_us:[...] } },                   // t_us WINDOW-RELATIVE (t0 subtracted)
    { kind:"imu", alias, schema, source, units, samples:{count,t_us,data} } ],
  analysis:{ schema:"pinpoint.analysis/2", tier, score,
    metrics:[{key,label,unit, t_us:[...], value:[...],  // value = NEUTRAL/absolute posture
              phaseSamples:[{phase,t_us,value,band}]}],
    phases:[{phase,t_us,conf}] } }
```
Directory: `<athleteLibraryPath>/<athlete-sanitised>/<YYYY-MM-DD_session-NN>/swing_NNNN/
{ swing.json, <alias>.mp4 (one per camera), thumb.jpg }` (see `SwingPaths`).

### ⚠ t_us DOMAIN MISMATCH — the single most important sync detail
- **Analysis `series.t_us` are ABSOLUTE** EventBuffer µs (from the `SwingWindow` grid; the same
  domain as the live `replayPositionUs`). Confirmed: `m_replayPositionUs = m_replayWindowStartUs
  + virtualTimeUs`, and frame stamps are converted with `... − m_replayWindowStartUs`.
- **Stream `frames.t_us` are WINDOW-RELATIVE** (exporter stores `e.timestamp_us − t0`).
- **Bridge = `clock.t0_us`** (absolute window start): `relative = absolute − t0_us`.
- **Recommendation:** on reload, **offset the analysis `series`/`phases` `t_us` to
  window-relative** (subtract `clock.t0_us`) so a reloaded shot lives in a clean **0-based µs**
  domain that the `QMediaPlayer` position maps to directly. Do it on **READ** (in
  `SwingDocReader`), leaving the live/fresh path absolute and untouched. `PpMetricGraph` is
  domain-agnostic (it only needs `startUs/endUs/playheadUs` in one consistent domain), so
  feeding relative t_us + a relative playhead just works.

### Reuse: `FilmController` already wraps `QMediaPlayer`
`src/Gui/film_controller.{h,cpp}` already has `#include <QMediaPlayer>`, a `position`
Q_PROPERTY + `positionChanged`, `Q_INVOKABLE play()`, and **`seekTo(positionMs)`**. This is the
pattern (and likely the reuse) for MP4 playback + scrub — the `QMediaPlayer.setPosition` the
§11 item referenced. **Study it before writing a new player.**

## 4. Design

### A. Reload completion (rating/note + export-fail)
1. **Additive `review` block:** `review:{ rating:int(0–5), note:string }` in `swing.json`.
2. **Write-through on edit:** `ShotListModel::setRating/setNote` already know the row's
   `swingDir`. Add `SwingDocWriter::updateReview(swingDir, rating, note)` — read `swing.json`,
   set/replace `review`, atomic rewrite (QSaveFile). Call it from the setters when `swingDir`
   is non-empty. (Confirm a fresh shot gets its `swingDir` into the model row at `addShot` —
   today `addShot` does not pass `swingDir`; `addPersistedShot` does. Likely need to thread
   `swingDir` into `addShot` too, or look it up from `ShotProcessor::m_swingDir`.)
3. **Read-back:** `readSwingJson` fills `PersistedShot.rating/note`; `addPersistedShot` sets them.
4. **Export-fail degraded header (minor):** when `exportOk==false && analysisOk`, synthesize a
   minimal raw header from the job (athlete/session/clock/window — `ShotProcessor` has them) +
   the analysis block, so analysis-only shots persist and reload. Extend `writeSwingJson` with a
   `synthHeader(job)` path.

### B. Replay-from-MP4 (the big piece)
**Trigger routing — fresh vs reloaded.** A shot is *fresh* iff it is the just-captured one
still holding a live `SwingWindow`; everything else (reloaded, or an older carousel entry) is
*disk-backed*. Route `replayRequested(shotId, mode)`:
- fresh shot → existing `ShotProcessor::startReplay()` (unchanged).
- disk-backed → new **MP4 replay path** keyed on the row's `swingDir`.

**MP4 replay component (new) — reuse `FilmController`/`QMediaPlayer`.**
- Per video stream in the shot's `swing.json`: a `QMediaPlayer` + `VideoOutput`,
  `source = file://<swingDir>/<alias>.mp4`.
- **¼× + sync:** prefer a **timer-driven `setPosition`** (~60 Hz advancing position by `dt/4`),
  mirroring the live `onReplayTick`, so the graph playhead and video stay locked and scrubbing
  is exact. (`playbackRate = 0.25` is simpler but couples graph sync to the player's clock.)
- **Multi-camera sync:** N players (face-on + DTL); one is the master clock, others `setPosition`
  to match. Wrist = 1 camera (do this first).
- **Graph playhead sync:** `PpMetricGraph.playheadUs = position_ms*1000` in the **window-relative
  domain** (after the §3 reload offset); `startUs/endUs = 0..window.end_us`. Bidirectional —
  dragging the graph/timeline calls `seekTo(ms)`.
- **Render location:** a dedicated **replay view in the shot-review panel** (a `VideoOutput` +
  the metric graph), **not** the live `PpCameraFrame` tiles (those are bound to live
  `CameraInstance` sinks). Replaying a saved shot is a review action.

**Ownership.** A small `ShotReplayController` (or an extension of `FilmController`) that, given a
`swingDir` + the reload-offset `analysisDetail`, owns the player(s) and exposes
`position`/`isPlaying`/`durationUs`/`seekTo`; the review panel binds the graph + `VideoOutput`
to it. `ShotProcessor` stays the fresh/live path only.

## 5. Implementation plan (increments)
- **R1 — Rating/note persistence.** `review` block + `SwingDocWriter::updateReview` + model
  write-through + reader read-back (+ thread `swingDir` into `addShot`). Test: set → restart →
  survives. *(small, no UI)*
- **R2 — MP4 replay controller (single camera).** `ShotReplayController` over `QMediaPlayer`
  (reuse `FilmController`), window-relative domain, `position`/`seekTo`. Verify against a real
  `swing_NNNN/` dir.
- **R3 — Review-panel replay view.** `VideoOutput` + `PpMetricGraph` bound to R2; trigger
  routing (disk-backed → R2). The scrub-with-video for saved shots.
- **R4 — Multi-camera sync** (face-on + DTL): master/slave players.
- **R5 — Export-fail degraded header** (analysis-only persist). *(minor)*

R1 first (shots fully survive restart); R2–R4 are the replay core; each independently committable.
Add tests to the existing CTest suite (`src/Analysis/tests/CMakeLists.txt`) where pure (e.g. the
reload t_us offset, the `review` round-trip in `swing_doc_test`).

## 6. Open questions / decisions
- **Reload offset vs persist-relative** for analysis t_us — recommend reload-offset (§3). Confirm.
- **¼× via `playbackRate` vs timer `setPosition`** — recommend timer `setPosition` for exact
  graph sync + scrub.
- **Replay view location** — review panel (recommended) vs the rail screen's video area.
- **Which shots replay** — all disk-backed with `hasVideo`; gate on the MP4 existing on disk.
- **Container/codec playback** — the exporter writes **h264 MP4** (NV12/YUV420P, Bayer
  demosaiced; MJPEG/12-16-bit Bayer unsupported). Verify Qt's FFmpeg `QMediaPlayer` plays them.
- **`addShot` vs `addPersistedShot`** — they have diverged (swingDir/rating/note); consider
  unifying so fresh and reloaded rows are identical (R1 forces part of this).

## 7. File & symbol quick-reference
- Persistence: `src/Export/swing_doc.{h,cpp}` (`SwingDocWriter`, `SwingDocReader`,
  `PersistedShot`); `src/Export/swing_exporter.cpp` (manifest + MP4 + thumb, the `streams`
  block); `src/Export/swing_paths.{h,cpp}` (dir layout).
- Model: `src/Gui/shot_list_model.{h,cpp}` (`addShot`, `addPersistedShot`, `Shot.swingDir`,
  `setRating/setNote`).
- Replay (fresh): `src/Gui/shot_processor.{h,cpp}` (`startReplay`, `onReplayTick`,
  `replayPositionUs`, `replayAnalysisDetail`, `m_swingDir`).
- Player reuse: `src/Gui/film_controller.{h,cpp}` (`QMediaPlayer`, `seekTo(ms)`, `position`).
- Graph: `src/Gui/PpMetricGraph.qml` (`seriesList`, `playheadUs`, `startUs`/`endUs` — domain-agnostic).
- Trigger: `src/Gui/PpShotPanel.qml` (`replayRequested`) → `src/Gui/PpShotCarousel.qml` →
  wiring in `src/Gui/Main.qml` / `VideoPage.qml`.
- Reload entry: `src/Gui/main.cpp` (startup reload block).
- Tests: `src/Analysis/tests/CMakeLists.txt` (CTest suite); `src/Export/tests/swing_doc_test.cpp`.
