# H · Persistence — every file the analysis/diagnostics stack reads or writes

Reviewer H, cross-cutting. Code cited at `37289e33`. Read-only review; nothing built or run.

## 1. Summary

The **per-shot document** is in better shape than the rest. `SwingStore` is the one way in to a swing's file. Every in-tree writer goes through `SwingDocWriter` → `saveDocument` → `SwingStore::save`. libppswing writes it properly: temp file, fsync, rename, then a directory fsync. Each change is logged in a dated schema reference. The weak point is turning the document into something the screen can use. The `analysis` block is converted by four separate hand-written functions, and they have already drifted apart. The swing header is also built twice, and the two copies have drifted: re-analysis reads `athlete.heightM / sex / dob`, but no normal capture writes them.

The **session and athlete files** are weaker:

- **`diagnostics.json` has no single owner.** Two panel models, the work-ons catch-up model and a CLI tool all write it, and only one of them merges what is on disk before writing. Charts reads the file as a message from the panel. During a live session the file is written only if a panel is open.
- **User-authored facts share a file with a regenerable cache.** The shot intents, outcome corrections, focus, declared miss and screen answers sit in the same file as the regenerable ledger rows. The documented regrade, a load that fails to parse, or two panels open at once can each drop them. The helper that writes these files (`atomicWrite`) removes the old file and then renames, so it is not atomic.
- **Athlete facts are not stored with the athlete.** The profile (dob, sex, height, bag) lives in per-machine QSettings. `fault_profile.json` has a reader but no reachable writer.
- **Schema tags are decorative.** No code reads `pinpoint.swing/2` or `pinpoint.analysis/3`. Each reader carries its own tolerance for older shapes, and the replay's copy has already got the `/3` score wrong.
- **Reads and writes on the GUI thread are still whole-document.** Every star, note, launch-monitor reading and re-analysis write-back does a full load and re-encode of the swing file on the GUI thread. Some of that work feeds a carousel role that no QML reads.

## 2. How the area is put together

| Format | Level | Writer(s) | Reader(s) | Version | Atomic |
|---|---|---|---|---|---|
| `swing.ppsw` (legacy `swing.json`, read-only) | shot | `SwingDocWriter::{writeSwingJson, updateReview, updateShotAnnotations, updateLaunchMonitor, refreshLaunchMonitorRows, updateStreamOrigin, writeDeviceOnlySwing}` → `saveDocument` → `SwingStore::save`. Callers: live join (worker thread), re-analysis write-back, ShotListModel, LaunchMonitorController, PpcpClipFiler, swinglab, lm_repair. Outside that chain: `session_archiver` stub (`SwingStore::save` direct), `raw_reencode` tool. | `SwingStore::{load, loadAnalysis, loadParts, loadSummaryBlock}`; above them `SwingDocReader::readSwingJson`, `SwingDiskLoader::load`, `DiskReplaySource::load`, `SwingDataSource::reload`, `LiveMeasureSource`, `recorded_products.cpp`, `PoseRunner/BallRunner::from*Json`. | `schema` strings (never read); per-stage `analysis.versions` (`analysis_versions.h`, read for reuse). | yes (libppswing `writeFileAtomic`) |
| `summary` block inside `.ppsw` / `swing_summary.json` (JSON-era sidecar) | shot cache | `saveDocument` (every write) / `writeSummaryFile` (QSaveFile) | `readSwingSummary` | `pinpoint.swingsummary/4`, checked | yes |
| `swing_phasegrid.json` | shot cache | `measure_sample.cpp` (QSaveFile) | phase-grid reader, charts | `kPhaseGridSchemaVersion` 10, checked; `sameDocumentStamp` guard | yes |
| `diagnostics.json` | session | `SessionDiagnosticsModel::persist` — the instances are the stage panel's, the cast window's, `WorkOnsController`'s private model, `SessionReportModel`'s (persist off) and `regrade_ledger` | the model itself; `SessionSeriesModel`, `ChartsPosturePresenter`, `WorkOnsController` (tendencies, marks), `AthleteController` (mtime only) | rows `kDiagSchemaVersion` 15 (not checked); envelope `kEnvelopeVersion` 1 (never read) | `atomicWrite` (no — remove + rename) |
| `club_baseline.json` | session (frozen athlete snapshot) | `SessionDiagnosticsModel` (Always/Live authority) | the model, `WorkOnsController` | `kClubBaselineSchemaVersion` 4, strictly checked | `atomicWrite` |
| `work_ons.json` | session | `WorkOnsController::finishDerive` | `WorkOnsController`, `SessionReportModel`, `SessionReportListModel` | `kWorkOnSchemaVersion` + fingerprint | `atomicWrite` |
| `report/report_meta.json`, PDF | session | `report_meta.h` (QSaveFile) | session list | `kReportMetaSchema`, checked | yes |
| `archived.json` | session | `session_archiver` (QSaveFile) | `isArchived` | `kMarkerSchema` | yes |
| `tendencies.json`, `swing_themes.*.json` | athlete | `WorkOnsController` | `WorkOnsController` (fingerprint); `AthleteController` (mtime vs newest ledger) | `kTendency*Version` in fingerprint | `atomicWrite` |
| `fault_profile.json` | athlete | `updateProfile()` ← `closeSession()`, which has **no caller** | `loadProfile()` at every activation | stamped with `kEnvelopeVersion`; not checked | `atomicWrite` |
| `outcome_corrections.jsonl` | athlete | `appendTuningLine` (append) | tuning tools | none | append |
| Athlete profile | athlete | `AthleteController` → `ppSettings()` (QSettings, per machine) | app only | none | n/a |

Data flow for one shot:

1. Analyzer ∥ exporter → `ShotProcessor::maybeJoin`. A worker thread builds the integrity blocks and calls `writeSwingJson` (the one off-GUI write). `onSwingDocWritten` then builds the live detail (`toAnalysisDetail`) for the replay and the carousel row.
2. Afterwards, on the GUI thread, each change is a whole-document read-modify-write: a launch-monitor reading, a PPCP clip, a user review, a re-analysis write-back.
3. Diagnostics read the documents on worker pools and write the session ledger. Derived session and athlete caches (work-ons, tendencies, themes) are rebuilt from those ledgers by `WorkOnsController` at launch, on an athlete change and when a session ends.

## 3. Findings

### H-01 · Four hand-written converters for `analysis`, already disagreeing
- Category: 5 Duplication · 4 One source per derived fact · 7 Contracts      - Severity: High
- Evidence:
  - Writer to disk: `src/Export/swing_doc.cpp:270` `QJsonObject serializeAnalysis(...)` (628 lines, size_table).
  - Live QML twin: `src/Gui/shot/shot_processor.cpp:338` `QVariantMap toAnalysisDetail(...)` (342 lines). Same blocks, re-typed as QVariant, in the absolute time domain.
  - Reload twin: `src/Gui/shot/disk_replay_source.cpp:143` `DiskReplaySource::load` (482 lines) re-converts the stored JSON back into the live shape.
  - Fourth shape: `src/Export/swing_doc.cpp:2266-2321` `readSwingJson` passes raw JSON blocks through (`an["club"].toObject().toVariantMap()`), window-relative.
  - Drift, disk has it and the live map does not:
    - `phases[].timing` (`swing_doc.cpp:440-441`, absent from `shot_processor.cpp:395-399`)
    - `segmentation.fusion` (`swing_doc.cpp:547-557`)
    - per-sample shaft σ, `insertShaftSigma(so, s)` (`swing_doc.cpp:635,670`, absent at `shot_processor.cpp:472-488`)
    - `positions[].timing` / `sigmaTUs` (`swing_doc.cpp:715-716` vs `:524-536`)
    - plane σ / `pNeedle` (`swing_doc.cpp:749-761`). `toPlaneChannelDetail` (`shot_processor.cpp:203-226`) lacks them, although its comment at `:197-202` says "identical shape in both parity writers".
    - `addressBallSigmaPx`, `synthKappa` (`swing_doc.cpp:782-785`)
    - ball `act` and `launch` (`:852-853, 863-865`)
    - `clubDtl` written "whenever the stage RAN, valid or not" (`:876`), while the live map writes it only `a.shaftDtl.valid ?` (`shot_processor.cpp:646`).
  - Drift, the reload gets it wrong: `disk_replay_source.cpp:344` `{ "overall", an["score"].toInt() }`. Since `/3` the score is an object (`swing_doc.cpp:350`), so this always gives 0. `summaryFromRoot` handles both forms (`swing_doc.cpp:1179-1182`). Reload phases also drop `segment` (`disk_replay_source.cpp:337-340`).
- What goes wrong: the same shot has a different detail depending on the path that built it: live, replay from disk, or a reloaded row. Today no QML reader was found for the keys that differ (`overall` has no reader; the σ, timing and fusion keys are read only by the reuse loader). So the divergence is latent. The next overlay or chart that reads one of these keys will work on live shots and silently not on reloaded ones, or the reverse. "Mirrors swing_doc.cpp" comments are the only guard against that, and three of them are already false.
- Fix: one serialiser per block, owned beside its producer. The pattern already exists: `poseTrackToJson`, `impactTrackJson`, `dtlShaftTrackToJson`, `kinematicSequenceToJson`, `skeleton3dToJson`. Extend it to club, ball, phases and segmentation, and have the live path build `QJsonObject` through the same functions with `windowT0 = 0`. Also have the reload path re-time one tree instead of rebuilding it. Add a parity test: serialise → reload → compare against the live map's keys.
- Effort: M        - Related: H-06, H-07; data/cpp_duplicates.md rows `swing_doc.cpp:679/728/763/823` ↔ `shot_processor.cpp:179/206/538/600`

### H-02 · The swing header is built twice, has drifted, and readers rely on fields no normal writer writes
- Category: 5 Duplication · 9 Persistence · 11 Missing values      - Severity: High
- Evidence:
  - Two header builders: `src/Export/swing_exporter.cpp:917-944` (full document) and `src/Gui/shot/shot_processor.cpp:2075-2115` `buildSynthManifest` ("Mirrors the exporter's header exactly").
  - Only the synthetic one writes height: `shot_processor.cpp:2097-2100` `a.insert(QStringLiteral("heightM"), m_exportJob.athleteHeightM)`. The exporter's `athlete` block (`swing_exporter.cpp:922-926`) writes only name, uuid and handedness. Its own header still promises it: `swing_exporter.h:353` `double athleteHeightM = 0.0; // … → athlete.heightM`.
  - Re-analysis reads it: `src/Analysis/swing_reanalyzer.cpp:788` `job.athleteHeightM = root["athlete"]["heightM"].toDouble(0.0)`. That value feeds `analysis_pipeline.cpp:3366` `in.heightM = ctx.job.athleteHeightM`. The live job uses the profile instead: `shot_processor.cpp:1267` `job.athleteHeightM = m_athlete ? m_athlete->currentHeightM() : 0.0`.
  - Readers of keys no writer emits:
    - `swing_reanalyzer.cpp:828` `ath["dob"]` and `:838` `ath["sex"]` (the standard club length for the 3-D skeleton)
    - `swing_doc.cpp:1033-1035` `lmArcRadiusM` reads `athlete.sex` (the radius behind `lowPointFromAttack`)
    - The schema reference admits both: `docs/reference/swing_json_schema.md:194` "the exporter's `athlete` block does not write it, so a normal capture lacks it"; `:196` "`athlete.sex` is read … but no writer puts it in a document".
  - Schema tag drift (June [H], still open): `swing_exporter.cpp:917` and `ppcp_clip_filer.cpp:258` write `"pinpoint.swing/1"`, which `swing_doc.cpp:1585` and `:2095` overwrite with `"/2"`. The literal now appears four times.
- What goes wrong (**suspected**, not reproduced): any camera swing (the FullDocument path) re-analysed after capture gets a skeleton3d height prior of 0, while live used the profile height. A version bump triggers re-analysis of the whole library (the version-gated re-analysis rule), so after the bump the stored skeleton and its 3-D readings come from a different input than live did. The women's and junior club sets are unreachable for `lowPointFromAttack` and for the re-analysis standard length. A female athlete's low point is always computed with the men's club length.
  - To show it: re-analyse one swing captured with a profile height set, then compare `analysis.skeleton3d` and any height-scaled 3-D series before and after.
- Fix: one header builder, `SwingExporter::headerBlock(job)`, called by both paths. Record the athlete facts the analysis consumes (height, sex, dob or a resolved cohort/club set) in the document at capture, so re-analysis is reproducible without the per-machine profile (see H-11). Make the schema string one constant.
- Effort: S        - Related: H-11; June review Export [H] "header built twice" (now drifted, worse)

### H-03 · `diagnostics.json` is shared by several writers, and Charts reads the file as a message
- Category: 1 Ownership · 8 Concurrency · 9 Persistence      - Severity: High
- Evidence:
  - Writers of one session's file:
    - the stage panel's model (`src/Gui/diagnostics/PpSessionDiagnosticsPanel.qml:90` `SessionDiagnosticsModel { id: diagModel … }`)
    - a second one in the cast window (`src/Gui/shot/PpSessionDiagnosticsWindow.qml:29-36` "TWO MODELS, ON PURPOSE FOR NOW … two models ingesting the same shots independently")
    - the work-ons catch-up model (`src/Gui/diagnostics/work_ons_controller.cpp:351-355`, `BaselineAuthority::Always`)
    - `tools/diagnostics/regrade_ledger.cpp:95-99`
  - Only the Always-authority writer merges, and only three keys: `session_diagnostics_model.cpp:3116-3131` re-reads `shotIntent`, `corrections` and `intentFrom` from disk "at the last moment". Panel models default to `Live` (`session_diagnostics_model.h:990`), so they merge nothing. `intent.focusConditionId`, `declaredMiss` and `screens` are not merged by anyone, and every writer writes the whole envelope (`:3084-3105`).
  - Charts uses the file as the channel: `src/Gui/charts/PpChartsPanel.qml:225` `onBusyChanged() { if (SessionMode.sessionDiagnostics && !…busy) seriesModel.refreshDiagnostics() }`, then `session_series_model.cpp:766` stats and parses `diagnostics.json`. Its comment at `:512-516` explains the posted call is needed because the model "emits it a few statements BEFORE it writes diagnostics.json".
  - Live session with no panel: the catch-up is paused while a session runs (`src/Gui/main.cpp:384-385` `workOns.setPaused(sessionController.activeSessionType() >= 0)`). So no object writes the live session's ledger unless a panel instantiated one.
- What goes wrong:
  - (a) With no diagnostics panel open, the live session has no ledger, and Charts shows no verdicts for it until the session ends and the catch-up runs.
  - (b) **Suspected** lost update, with the stage panel and the interactive cast window both up: correct an outcome or declare a miss in one; the next shot lands; the *other* model ingests it and persists its own in-memory envelope, which lacks the change. `diagnostics.json` no longer holds the correction until the first model persists again. If the app is quit first, it is gone.
  - (c) **Suspected**, the same against the catch-up: a session ends, the catch-up activates and regrades it, and the golfer declares a focus or miss in the still-open panel. When the catch-up persists, it writes the focus and miss it read at activation.
- Fix: one session-diagnostics object per reviewed/live session, owned by the session (not by a view or a controller). It is the only writer of the file and it publishes a change signal; panels, the cast, Charts and the report read from it. The catch-up and the tool construct the same type headless, but never while the app owns that session. Until then, persist should merge every user-owned envelope key from disk, not three.
- Effort: L        - Related: H-04, H-05; the brief's MVC fault (now patched by the catch-up, not removed)

### H-04 · User-authored facts share a file with a cache that is rebuilt by deleting it
- Category: 9 Persistence · 11 Errors/missing values · 1 Ownership      - Severity: High
- Evidence:
  - The envelope carries the only copy of golfer input: `session_diagnostics_model.cpp:3090-3100` (`shotIntent`, `intentFrom`, `corrections`), plus `intent` and `screens` (`:3086-3088`). They are written by `setShotIntent` (`:2436-2449`), `correctOutcome` (`:2388-2434`) and `declareFocus` / `declareMiss` (`:1647-1683`). For corrections the athlete's `outcome_corrections.jsonl` is only a log (`:2375-2386`).
  - The documented regrade throws the file away: `tools/diagnostics/regrade_ledger.cpp:86` `QFile::rename(ledger, backup)`, then `m.activateSession(dir)` (`:99`) back-fills and writes a fresh file. The header calls this "the only way to see what the new pack says" (`:7-9`); memory's "Corpus sweep is two halves — swinglab THEN regrade_ledger".
  - A load that fails to parse is read as no file: `session_diagnostics_model.cpp:1465` `const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();`. There is no error check. The reconcile then back-fills every swing (`:1540-1551`) and `persist()` overwrites the unreadable file.
  - Per-shot facts are split across two levels. Rating, note, club, `excluded` and `clubOverride` are in the shot's document (`swing_doc.cpp:1865-1906`). Intent and corrections for the same shot are in the session file, keyed by the folder-derived int (`shotIntent.insert(QString::number(it.key()), …)`, `:3093`).
- What goes wrong:
  - After any `regrade_ledger` run, the live `diagnostics.json` holds none of the session's intents, corrections, focus, declared miss or screen answers. They survive only in `diagnostics.json.<tag>`, which nothing reads. This follows from the code and was not reproduced.
  - **Suspected:** a truncated or corrupt ledger (for example from the non-atomic window in H-05, or a partial copy onto the share) is silently replaced by a regraded one with the user's input removed, and no warning is logged.
- Fix: store user-authored session/shot input in its own small file (or with the shot: `review.intent` and `review.corrections` in the swing document, beside `review.excluded`). Then `diagnostics.json` is a pure cache that can be deleted and regenerated. Until then, `loadDiagnostics` must check the parse and refuse to persist over a file it could not read, and `regrade_ledger` must carry the envelope across.
- Effort: M        - Related: H-03, H-05

### H-05 · Three ways to write a file atomically; the one guarding the session and athlete caches is not atomic
- Category: 9 Persistence · 5 Duplication · 13 Comments      - Severity: Medium
- Evidence:
  - `src/Diagnostics/engine/pack_io.cpp:142` `const QString tmpPath = path + QStringLiteral(".tmp");` (a fixed name), with no fsync, then `:165-166` `QFile::remove(path); if (!QFile::rename(tmpPath, path))`.
  - Its comment says the content "is still in the temporary beside it", but no reader ever looks at `.tmp`.
  - Callers that trust it: `session_diagnostics_model.cpp:3136` ("an interrupted write must not truncate the session's evidence"), `:3244` (fault profile), and `work_ons_controller.cpp:624, 1794, 2121` (work_ons, themes, tendencies).
  - The others: QSaveFile at `swing_doc.cpp:1290`, `measure_sample.cpp:1009`, `report_meta.h:199` and `session_archiver.cpp:248`; libppswing `writeFileAtomic` (`../libppswing/src/file.cpp:75-117`: temp + fsync + rename + dir fsync).
- What goes wrong: a crash or power loss between `remove` and `rename` leaves the session with no `diagnostics.json` (and so H-04's back-fill and loss of user input). The window is wider than "microseconds" on the SMB share the studio library lives on. Two processes writing the same ledger (the app and `regrade_ledger`) share the fixed `.tmp` name and can interleave.
- Fix: delete `atomicWrite`'s body and implement it on `QSaveFile`, which replaces the file atomically on all three platforms and writes a uniquely named temp. Keep one helper.
- Effort: S        - Related: H-04; June review Export [M] "QSaveFile scaffolding ×3" (worse: now three mechanisms)

### H-06 · Whole-document read-modify-writes and fat parses still run on the GUI thread
- Category: 7 Contracts (read paths that deep-copy) · 8 Concurrency      - Severity: Medium
- Evidence:
  - Every update is a full load plus a full save. `swing_doc.cpp:1854` `QJsonObject root = SwingStore::load(swingDir, error);` in `updateReview`, and likewise at `:1884` (annotations), `:1923` (launch monitor) and `:1786` (stream origin). Each ends with `saveDocument` (a full re-encode and write).
  - Callers on the GUI thread:
    - `shot_list_model.cpp:356` (every star, note, club)
    - `launch_monitor_controller.cpp:537`, followed by `m_shotModel->refreshShot(swingDir)` at `:553`, which does `readSwingJson` (`shot_list_model.cpp:261`): a third full load plus `toVariantMap` of pose2d, club and ball (`swing_doc.cpp:2304-2312`)
    - `ppcp_clip_filer.cpp:125, 298, 424`
  - Re-analysis writes back on the GUI thread: `src/Gui/shot/reanalysis_controller.cpp:139` `QJsonObject manifest = pinpoint::SwingStore::load(dir);`, then `:163` `writeSwingJson` inside `onWorkerFinished` (a `QFutureWatcher` slot).
  - A full load just to classify: `reanalysis_controller.cpp:73` `isDeviceOnlySwing(pinpoint::SwingStore::load(dir))`, run in `startNext()` for each queued swing. A camera swing is parsed whole on the GUI thread, and then again on the worker.
  - Review load: `session_review_controller.cpp:243` `QtConcurrent::blockingMapped(... readSwingJson ...)` blocks the GUI until every swing of the session is parsed in full.
- What goes wrong (**suspected**, not measured): the stall the app is known for, moved rather than removed. One launch-monitor pairing in a live session costs three full passes over a document described as ~28 MB (JSON) or ~100 MB decoded (`swing_store.h:128`) on the GUI thread over the share. Each star click costs one load and one write. A re-analysis batch blocks the GUI twice per swing.
  - To show it: pair a reading with the Premier connector during a live session and read the `[ShotProcessor] join tail` / UI timings.
- Fix:
  - Make the small updates chunk-local: `review`, `launchMonitor`, `streams[].origin` and `summary` are in the root chunk. Add a libppswing "rewrite root chunk, copy the others byte-for-byte" path, or move the updates to a worker with a per-swing serial queue.
  - Answer `isDeviceOnlySwing` from `loadSummaryBlock`'s heads.
  - Move the re-analysis write-back to the worker.
  - Refresh a row from the summary block plus `readLaunchMonitorDetail`, not `readSwingJson` (see H-07).
- Effort: M        - Related: H-07, H-13; brief's "28 MB document" note

### H-07 · Each carousel row keeps a full analysis detail that no QML reads; the documented fallback is dead code
- Category: 14 Dead code · 13 Comments · 4 One source      - Severity: Medium
- Evidence:
  - The role is filled from the live map (`shot_processor.cpp:2438` `m_replayAnalysisDetail.map()`), from `readSwingJson` (`shot_list_model.cpp:270, 318`) and from the review load (`session_review_controller.cpp:258`).
  - QML consumers read `shotReplay.analysisDetail` / `shotProcessor.replayAnalysisDetail` instead (`PpCameraFrame.qml:62`, `PpMotionPanel.qml:80`, `PpChartsPanel.qml:115`, `PpTrustPanel.qml:58`). No QML file reads the row's `analysisDetail` role. `PpShotCard.qml:58` says "the card never read it".
  - The only C++ reader is `LmSessionModel::rebuild` (`lm_session_model.cpp:1010`), for `lm.` series and gates. `readLaunchMonitorDetail` already serves those cheaply (`swing_doc.cpp:2189-2213`).
  - `ShotListModel::analysisDetailForSwingDir` (`shot_list_model.cpp:642`) and `previousAnalysisDetail` (`:629`) have no caller in C++ or QML, yet `swing_doc.h:447-449` and `shot_list_model.cpp:235-236` describe the first as the consumer the cheap path relies on.
  - The review loader's reason for the fat parse is wrong: `session_review_controller.cpp:232` "analysisDetail carries the pose2d keypoint track the replay overlays read".
- What goes wrong: the cost in H-06 (review fat parse, `refreshShot`) and the memory of a whole pose track per live row are paid for one reader that needs a few KB. This is the fourth detail shape in H-01.
- Fix: replace the row's `analysisDetail` with the `lm.` slice (the `readLaunchMonitorDetail` shape) and delete the two dead invokables. Then review load can use `readSwingSummary` like `loadSessionDir` does.
- Effort: S        - Related: H-01, H-06

### H-08 · Schema tags are decorative; each reader handles older shapes itself
- Category: 9 Persistence/versioning      - Severity: Medium
- Evidence:
  - Nothing branches on the document's `schema`. The only reads are display rows (`swing_data_source.cpp:669, 676`). Tolerance for old shapes is repeated per reader:
    - score int-or-object in `summaryFromRoot` (`swing_doc.cpp:1179-1182`) and `readSwingJson` (`:2261-2262`), but not in `DiskReplaySource` (`disk_replay_source.cpp:344`, H-01)
    - the unit change on `stanceWidth` is left to readers (`swing_json_schema.md:811`)
  - The ledger reader `fromJson` (`src/Coaching/diagnostic_ledger.h:2396`) never checks `schemaVersion`. `kEnvelopeVersion` is written (`session_diagnostics_model.cpp:3085`) and never read.
  - `SessionReportModel` defeats the work-on version gate on read: `session_report_model.cpp:1260-1261` `root["schemaVersion"] = kWorkOnSchemaVersion; root["ruleVersion"] = kWorkOnRuleVersion;`.
- What goes wrong:
  - The first reader that forgets an old shape gets a silent 0 (`overall` already does).
  - **Suspected:** two studio machines on one share running different builds regrade each other's ledgers on every open. Any mismatch in the content stamp triggers a regrade and rewrite. An older build reads a newer ledger without complaint and writes it back at its own version.
  - A work-on record from an older schema is parsed as the current one.
- Fix:
  - One `migrateSwingDoc(root)` step in `SwingStore::load*` that normalises known older shapes (score → object; unit renames) and stamps the result, so readers see one shape.
  - Have the ledger and envelope readers reject (or read-only) a newer `schemaVersion`.
  - Replace the report model's version rewrite with an explicit "read evidence fields from an older record" function in `work_ons.h`.
- Effort: M        - Related: H-01, H-02; June review Export [H] (contradictory schema version — open)

### H-09 · Athlete facts are stored per machine, not with the athlete
- Category: 1 Ownership/level · 9 Persistence      - Severity: Medium
- Evidence:
  - `src/Gui/athlete/athlete_controller.cpp:137` `QSettings s = ppSettings();` / `:39` `kGroup = "athletes"`. dob, sex, height, handedness and the club bag with its geometry are read from QSettings (`:152-187`).
  - Derived athlete files live in the library's athlete folder, found by sanitised name: `faultProfilePath()` (`session_diagnostics_model.cpp:3034-3043`), `tendenciesPath` (`work_ons_controller.cpp:343`), `tuningLogPath` (`session_diagnostics_model.cpp:3025-3032`).
  - The swing document deliberately does not carry the cohort inputs (`swing_doc.h:365-369`), and H-02 shows what it does not carry by accident.
- What goes wrong (**suspected**): the library is shared and bulk re-analysis runs on another PC (GOLFSIMPC, per the workflow). That machine either has no profile for the athlete or a different one, so cohort, club set and height prior differ by machine for the same swing. Nothing records which profile a grade used.
- Fix: an athlete record file in the athlete folder (`athlete.json`, keyed by uuid) as the source of truth; QSettings holds only "current athlete". Snapshot the analysis-relevant facts into each swing at capture (H-02).
- Effort: M        - Related: H-02

### H-10 · Archive restore drops annotations made on the stub; the stub bypasses the document writer
- Category: 9 Persistence · 11 Errors      - Severity: Medium
- Evidence:
  - Restore puts back only three review fields. `src/Export/session_archiver.cpp:318-323` compares the whole stub review (`== stubReview`), then calls `SwingDocWriter::updateReview(sd, rating, note, club)`. `updateReview` writes only those three (`swing_doc.cpp:1866-1868`). `review.excluded` and `review.clubOverride`, which `updateShotAnnotations` writes on the stub, are never carried back.
  - The stub write skips `saveDocument`: `session_archiver.cpp:230` `SwingStore::save(sd, stubOf(doc, block), &err)`. The `summary` is not rebuilt and `clearJustWritten` is not called.
- What goes wrong (**suspected**): archive a session, then "Leave out" a shot or override its club on the launch monitor board, then restore. The restored document has the archive-time annotations, so the board's means and grades silently include the shot again.
- Fix: restore the whole stub `review` block (merge it over the restored document's), through one `SwingDocWriter::replaceReview`. Write the stub through `SwingDocWriter` so the summary and cache rules apply.
- Effort: S        - Related: H-12

### H-11 · `fault_profile.json`: a reader and an unreachable writer
- Category: 14 Dead code · 9 Persistence      - Severity: Low
- Evidence:
  - `SessionDiagnosticsModel::closeSession()` (`session_diagnostics_model.cpp:1626`) has no caller in `src/` (only `regrade_ledger.cpp:17` mentions it, to say it does not call it). It is the only caller of `updateProfile()` (`:1640`, `:3195`).
  - `loadProfile()` still runs on every activation (`:1533`) and feeds `profileBias` into ranking (`:2992`).
  - `updateProfile` stamps the file with the session envelope's `kEnvelopeVersion` (`:3240`) and ignores `m_persistEnabled`.
  - Design admits it: `docs/design/work_ons_design.md:16` "**Nothing calls it.** … no fault profile has ever been written."
- What goes wrong: a dead athlete-level store, its read path and a ranking input that is always 0. A later caller of `closeSession()` would revive an incremental tally that the design says "cannot survive a re-analysis without double counting" (`work_ons_design.md:17`).
- Fix: delete `fault_profile.json`, `loadProfile`, `updateProfile`, `profileBias` and `closeSession` (or make `closeSession` a no-op on the profile). The athlete-level history is `tendencies.json` / `work_ons.json`.
- Effort: S        - Related: H-03

### H-12 · File names and staleness rules are spelled per class
- Category: 5 Duplication · 15 Names      - Severity: Low
- Evidence:
  - `"diagnostics.json"` is spelled in five files: `session_diagnostics_model.cpp:87`, `session_series_model.cpp:56`, `work_ons_controller.cpp:71`, `athlete_controller.cpp:743`, `charts_posture_presenter.cpp:681,709`.
  - `"work_ons.json"` in three (`work_ons_controller.cpp:69`, `session_report_model.cpp:1254`, `session_report_list_model.cpp`). `"club_baseline.json"` and `"tendencies.json"` in two each.
  - Two staleness rules for `tendencies.json`: the fingerprint in `work_ons_controller.cpp:2066-2070`, and file mtime against the newest ledger mtime in `athlete_controller.cpp:741-747`. By the work-ons comment (`work_ons_controller.cpp:464-466`), the panel rewrites the ledger "for reasons that change no verdict", which trips the second rule.
- What goes wrong: a rename or move of one file needs edits in five places. The club-distance card says "reading…" after any focus declaration, although nothing it shows changed.
- Fix: one `SessionFiles` / `AthleteFiles` header with the paths, and one staleness predicate (the fingerprint).
- Effort: S        - Related: H-03

### H-13 · `takeJustWritten` is a process-wide one-entry cache that also keeps re-analysed documents
- Category: 8 State · 1 Lifetime      - Severity: Low
- Evidence:
  - `swing_doc.cpp:1221-1239`: global `g_justWrittenDir` / `g_justWrittenRoot`, set by every `writeSwingJson` (`:1711`). That includes the re-analysis write-back (`reanalysis_controller.cpp:163`), the PPCP stub (`ppcp_clip_filer.cpp:261`) and swinglab.
  - The archiver (H-10) and out-of-process writers cannot clear it.
- What goes wrong: after a re-analysis batch the last swing's full decoded tree stays in memory until that exact swing is replayed or something else is written. An external rewrite (swinglab `--write-back` on the share) of that one swing leaves a stale cached tree for the next replay. Both are narrow.
- Fix: set it only on the live path (pass the root to `DiskReplaySource` explicitly from `ShotProcessor`, instead of keeping a global), or compare the `DocInfo` stamp on take.
- Effort: S        - Related: H-06

### H-14 · Readers and writers in other processes race the app with no lock; a reader that sees a rename mid-read returns "no document"
- Category: 8 Concurrency      - Severity: Low
- Evidence:
  - The libppswing reader re-opens the file for each chunk read (`../libppswing/src/file.cpp:52-60` `readFileRange` opens an `ifstream` per call), so a rename between `Reader::open` (TOC) and `chunk()` gives a CRC or truncated-read exception.
  - `SwingStore::load` turns that into an empty object (`swing_store.cpp:148-150`). `updateReview` then returns false (`swing_doc.cpp:1855-1856`) and only warns (`shot_list_model.cpp:357`). The re-analysis write-back skips the swing as "document missing/unreadable" (`reanalysis_controller.cpp:145-151`).
  - Separate-process writers with no lock against the app: `swinglab_run.cpp:800`, `lm_repair.cpp:231`, `pps_convert_library.cpp:132`, `regrade_ledger.cpp:86-99`.
  - Within the app, document writes are serialised by convention: the live write precedes all others, and the rest run on the GUI thread. CRC checks make this fail loud rather than corrupt.
- What goes wrong (**suspected**): a star set while a worker or an external tool rewrites the same swing is shown on the row but not saved (log warning only). Two whole-document read-modify-writes in different processes lose one side's change with no error. For example, the app's `updateReview` around a swinglab write-back drops the re-analysis.
- Fix: have the reader take a snapshot (read the file once, or check the size and mtime at open and retry), and give the library a per-swing advisory lock (`swing.ppsw.lock`), honoured by the tools.
- Effort: M        - Related: H-06

## 4. Done well

- **One door to the swing document.** `SwingStore` (`swing_store.h:21-30`) hides the two formats. Every in-tree writer goes through `SwingDocWriter` → `saveDocument` (`swing_doc.cpp:1389-1394`), which rebuilds `summary` from the tree being written, so the index cannot be stale (`:908-911`). Exceptions are H-10's stub and one tool.
- **Atomic, verified writes for the document.** libppswing does temp, fsync, rename and dir fsync (`file.cpp:75-117`). JSON → ppsw conversion re-reads the new file with a fresh reader and checks semantic equality before deleting anything (`swing_store.cpp:338-429`).
- **Integrity blocks have one builder each.** Each has an `apply*` that removes the block when the verdict is unknown (`swing_doc.h:63-86`), so re-analysis can never inherit a stale ⚠.
- **The live write moved off the GUI thread and has one writer at the join** (`shot_processor.cpp:2218-2345`).
- **Launch-monitor rows survive re-analysis, and derived rows are re-derived from the raw block** (`swing_doc.cpp:1643-1700`). The update paths are idempotent.
- **Version discipline per stage.** `analysis_versions.h` dates every bump, and reuse is gated on them.
- **One staleness rule for document-stamped sidecars** (`SwingStore::sameDocumentStamp`, `swing_store.h:62-84`). Strict schema checks on `club_baseline.json` (`club_distances.h:483-499`), the phase grid (`measure_sample.cpp:803`), `report_meta.json` (`report_meta.h:167`) and the `/4` summary.
- **One ledger-row reader.** All five readers of `diagnostics.json` rows use `diagnostic_ledger.h fromJson`, and `kDiagSchemaVersion`'s history is written beside it.
- **Fingerprinted derived caches.** `work_ons.json`, `tendencies.json` and `swing_themes.*.json` carry `derivedFrom` fingerprints of their inputs.
- **The re-analysis reuse reader tracks the writer closely**, σ fields included (`recorded_products.cpp:41-190`). Its known losses are listed (`analysis_versions.h:36-38`).
- **`docs/reference/swing_json_schema.md`** is a thorough, dated, additive-by-contract reference. It even records H-02's gaps.

## 5. June review status (src/Export, persistence items)

| June item | Status at `37289e33` | Evidence |
|---|---|---|
| [H] Contradictory schema version: exporter `/1` overwritten to `/2`, literal ×3 | **Open, slightly worse** (×4: `ppcp_clip_filer.cpp:258` added) | `swing_exporter.cpp:917`, `swing_doc.cpp:1585, 2095` |
| [H] swing header built twice (`swing_exporter` vs `shot_processor`) | **Open, worse: drifted** (`athlete.heightM`) | H-02 |
| [M] QSaveFile open/write/commit scaffolding ×3 | **Worse**: three different atomic-write mechanisms, one not atomic | H-05 |
| [M] `club` hardcoded `"DRIVER"` on reload | **Fixed** (fallback chain review.club → capture.club.name → stub) | `club_vocabulary.h:128` `swingDocClub`; `swing_doc.cpp:1148` |
| [M] stubbed `restorer` / pose hooks as live-looking branches | **Open** | `swing_exporter.cpp:291` `// TODO(restorer)`, `:561` `"restorer","none"`; `poseStreams` "empty today" (`swing_exporter.h:322`, `shot_processor.cpp:1581`) |
| "Clean atomic-write/worker discipline" (Export verdict) | Still true for the swing document; **not** true for the session and athlete files | H-05 |

## 6. Questions

1. Is per-machine QSettings deliberately the athlete record's home (privacy, or a studio-local roster), or is it a leftover from before the library existed? H-09's severity depends on it.
2. Can the cast window (`PpSessionDiagnosticsWindow`) be interactive while the stage panel is also up in normal studio use? That decides whether H-03(b) is reachable or theoretical.
3. Does `regrade_ledger` ever run over the athlete library (not only `corpus/`)? If yes, H-04's loss is happening, not suspected.
4. `skeleton3d` with `heightM = 0`: what does the fit fall back to, and by how much does a re-analysed swing's 3-D output differ from its live self (H-02)? I could not settle this from the code without running it.
5. Is `club_baseline.json` meant to be a frozen per-session snapshot of an athlete-level fact (Contract A), with no athlete-level baseline file? The code reads that way, and I treated it as intended.
