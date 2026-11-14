# E · Athlete → session → shot plumbing and the selected shot

Reviewer E. Commit `37289e33`. Read-only review of `src/Gui/shot`, `src/Gui/review`, `src/Gui/session`,
`src/Gui/home` (QML), `src/Gui/athlete/athlete_controller`, and the wiring in `src/Gui/main.cpp` and
`src/Gui/shell/Main.qml`. Where a chain left the area (SwingPaths, SwingDocReader, the two session models'
identity helpers), I followed it as far as the identity question needed and no further.

## 1. Summary

The area has no Athlete, Session or Shot object, and that one absence explains most of what follows.
- **The athlete** is a name string. The library is keyed by the sanitised name, while the app's own
  settings are keyed by uuid. Rename an athlete and their history is cut off. Two athletes whose names
  sanitise alike share one folder, and every fully non-Latin name becomes `unknown`.
- **The session** is a folder string. Four panels and `main.cpp` each work it out from three sources, and
  their rules differ. Its lifecycle is a four-call sequence repeated in QML in four places.
  `ShotProcessor` re-resolves the folder on every shot, so a change of athlete, of name or of day in the
  middle of a session silently starts a new folder.
- **The selected shot** is held in five places with four different definitions. Nothing clears it when
  its session ends. A stale selection is then resolved by folder *name* into another session's shot.
- **"Shot N"** means three different things: the carousel's swing number, the diagnostics ledger's
  position and Charts' rank among analysed shots.

The five things that matter most:
1. **E-01:** a session can be split across two athletes. Creating, or renaming, an athlete mid-session is
   not guarded.
2. **E-03:** `currentAthleteChanged` fires on every edit to an athlete record. Every app-wide listener
   then behaves as though the athlete had switched, including reloading the live carousel mid-session.
3. **E-04 / E-05:** the selection has no owner, and it leaks across sessions.
4. **E-09 / E-10 / E-11:** `ShotProcessor` (2,737 lines) mixes capture, job building, serialisation, the
   document write, carousel rows, a replay player and session-folder ownership. It has three parallel
   shapes of `analysisDetail` and two swing.json header writers. The two headers already disagree:
   `athlete.heightM` is lost on every video swing.
5. **E-14:** panels are rebuilt on every switch between Capture and Replay. This is the visible symptom of
   session-level models being owned by views.

The well-built parts are real: swing-number high-water marks, `shot_outcome.h`, `QmlPayload`, the
model-agnostic re-analysis funnel, Home's per-uuid view state, and the delete-releases-the-folder hooks.
See §4.

## 2. How the area is put together

**Composition root (`main.cpp:280-560`).** The objects in this area are all stack objects of `main()`,
exposed as context properties (42 in total, `main.cpp:1030-1297`). The athlete- and session-scoped ones
are listed below.
- `AthleteController` holds the athlete records in QSettings (by uuid) plus the "current" uuid and name.
- `SessionController` holds the clock, type, active club and focus.
- `ShotListModel shotModel` holds the live session's rows.
- `SessionReviewController` holds the reviewed session's directory, its own private `ShotListModel`,
  and the sessions list.
- `ShotProcessor` holds the live pipeline and, through `SwingPaths`, the running session's folder.
- `ShotReplayController shotReplay` is the disk replay of one swing.
- `CurrentSwing` is a string that ScreenWrist writes.
- `ReanalysisController`, `WorkOnsController`, `SessionReportModel` and `SessionReportListModel` complete
  the set.

When the athlete changes, four connections re-point these objects (`main.cpp:380`, `:422`, `:459-460`,
`:461-465`). The library folder for an athlete is root + `SwingPaths::sanitise(name)`, written out six
times (E-02).

**Selection and mode (QML singletons).**
- `SessionMode` holds the mode (Capture, Replay or Analyse), `focusedShotId` and `focusedSwingDir`, a
  nullable `sessionDiagnostics` slot ("last panel wins"), and two parked reader-place maps
  (`diagnosticsNav`, `chartsNav`).
- `ViewLayout` holds the per-mode layout.
- `PpModeStage` builds each panel's delegate from `ViewLayout` for the current mode.

**Data flow of a shot.**
1. `ShotController.shotDetected` calls `ShotProcessor.onShotDetected`.
2. The processor runs post-roll, gather, window, segmentation, then analysis followed by export.
3. `maybeJoin` runs the integrity checks and the swing.json write on a worker.
4. `onSwingDocWritten` calls `shotModel.addShot` and emits `shotProcessed(modelId, swingDir)`.

From there:
- `Main.qml` calls `SessionMode.enterReplay`, which starts `shotReplay` (it re-reads the document).
- Each panel's own model ingests the shot from the same signal (diagnostics, series, consistency).
- The cast window builds its own panel and model.

**Reading a shot.** Every consumer fetches its own copy (E-04, E-10):
- the action bar: `shotSummary(id)`;
- the cards: folder → ledger id → `pipsFor`;
- Diagnostics: `selectedSwingDir` → `shotReadout` / `shotOutcome`;
- Charts: `shotReplay.analysisDetailRef`;
- the data viewer: a fresh `SwingStore::load`;
- the LM board: the row map, falling back to a disk read.

## 3. Findings

### E-01 · A running session can be split across two athletes: "New athlete" and rename are unguarded, and the processor re-resolves the folder on every shot
- Category: 1 Ownership and lifetime · 2 Identity · 3 Layering (domain rule in a view)      - Severity: High (**suspected**; Critical if reproduced: swings saved under the wrong athlete)
- Evidence:
  - `ScreenAthletePicker.qml:33-35`: `// switched (or deleted) until it ends. The rail has no session lock; this is the one guard.` The guard is in the view. `selectAthlete` itself refuses nothing (`athlete_controller.cpp:826-832`).
  - `ScreenAthletePicker.qml:216-218`: `label: qsTr("＋ New athlete")` / `onClicked: root.newAthleteRequested()`. Not gated on `sessionRunning`. Nor is Home's add button (`ScreenHome.qml:232`) or the Home avatar's edit (`ScreenHome.qml:338`, `Main.qml` `onEditCurrentAthleteRequested`).
  - `athlete_controller.cpp:385-386`: `if (creating) s.setValue(kCurrentUuid, targetUuid);   // auto-select new athlete`, followed by `reload()`, which emits `currentAthleteChanged` (`:227`).
  - `swing_paths.cpp:199-201`: `if (m_cachedSessionDir.isEmpty() || m_cachedRoot != r.root || m_cachedAthleteUuid != athleteUuid || m_cachedDate != r.today || m_cachedBase != r.base)` allocates a new session folder. `r.base` contains the name token (`:167-176`), so a **rename** re-allocates too.
  - `shot_processor.cpp:1584-1589` (export job): `job.athleteName = m_athlete->currentName(); job.athleteUuid = m_athlete->currentUuid();` is read at save time, 12–37 s after impact. `:1267`, `:1296` read the analysis job's handedness, height and club record from the *current* athlete.
  - The new folder is never announced. `activeSessionDir()` is `m_swingPaths.currentSessionDir()` (`shot_processor.h:143`), and `activeSessionDirChanged` is emitted only at `shot_processor.cpp:2066,2072`.
  - `session_diagnostics_model.cpp:830-849` `ingestShot` does not check that the swing lies under `m_sessionDir`. `regradeShot` (`:851-856`) and `SessionSeriesModel::ingestShot` (`session_series_model.cpp:485`) do check.
- What goes wrong (scenario):
  1. Mid-session, the coach opens Athletes, then ＋ New athlete, and saves.
  2. The new athlete becomes current. `main.cpp:461-465` reloads the live carousel from the new athlete's (empty) library, so the carousel empties mid-session.
  3. The next shot is analysed with the new athlete's handedness, height and clubs, and saved in a new session folder under the new athlete's directory.
  4. The diagnostics panel stays pointed at the old folder (no NOTIFY), and its ledger ingests the new folder's swings into the old session's `diagnostics.json`. Charts ignore them.

  Renaming the current athlete mid-session (Home avatar → form) has the same effect, without the
  analysis change. A session that runs past midnight also re-allocates (a new day), with the same silent
  split.
- Fix:
  - Introduce a **Session** object created by `begin()`. It captures an immutable athlete snapshot (uuid, folder, handedness, height, bag) and the resolved session folder. `ShotProcessor` takes its paths and athlete facts from the Session, never from `AthleteController`, and `SwingPaths` never re-resolves within a session.
  - Make "no athlete switch while a session runs" a rule in `AthleteController`: `selectAthlete` and the auto-select in `saveAthlete` refuse while a session is running. Drop the QML-only guard.
  - Make `ingestShot` check the parent folder, as its siblings do.
- Effort: M      - Related: E-02, E-03, E-12, E-13

### E-02 · Athlete identity: the library is keyed by sanitised name while a uuid exists and is persisted. Rename orphans history, and look-alike names merge
- Category: 2 Identity · 5 Duplication · design drift      - Severity: High
- Evidence:
  - `swing_paths.cpp:97-98`: `static const QRegularExpression hostile(QStringLiteral("[^A-Za-z0-9._-]+")); s.replace(hostile, QStringLiteral("-"));`. Any name with no ASCII letters returns `"unknown"` (`:111-112`). "José" becomes `Jos`, and "Chris Smith" twice is one folder.
  - The six derivations of the athlete folder disagree with each other:
    - `swing_paths.cpp:160-162`: `sanitise(!athleteName.trimmed().isEmpty() ? athleteName : athleteUuid)`, with a fallback to `AppDataLocation/swings` when the root is empty (`:153-158`). This is the writer.
    - `swing_doc.cpp:2523-2525` (`latestSessionDir`) and `:2542-2544` (`sessionDirs`): `if (libraryRoot.isEmpty() || athleteName.isEmpty()) return {};`. These readers have no uuid fallback and no AppData fallback.
    - `main.cpp:366-375`, `:404-411` and `:415-419`. The third copy, the picker's activity resolver, does not honour `PINPOINT_WORKONS_DIR` as the first two do.
  - `session_report_model.cpp:335-341` maps in reverse: `if (!n.isEmpty() && pinpoint::SwingPaths::sanitise(n) == folder)`, and `:378`: `// … the first whose sanitised name is the folder's wins`. The collision is acknowledged in a comment.
  - `athlete_controller.cpp:204`: `refreshActivity();          // a rename moves the folder`. No code moves a folder on rename. A repo-wide search finds no athlete-folder rename.
  - `docs/reference/swing_folder_layout.md:31`: *"the folder name is a display convenience and is **not** the identity key (the `athlete.uuid` inside each swing document is)"*. The code keys every lookup on the folder name.
  - The app's own settings are keyed by uuid: the club bag (`athlete_controller.cpp:717-...`), the club-length prior (`shot_processor.cpp:1315`: `m_lengthPriorKey = m_athlete->currentUuid() + '|' + club + '|' + faceOnKey`) and Home's view state (`hm_view_store.js`). So the athlete has two identities.
- What goes wrong:
  - **Rename** an athlete, for example to fix a typo. The carousel reload, the sessions drawer, the work-ons, tendencies, club distances and session reports all compute `<root>/<newName>`, which does not exist, so the golfer's whole history disappears from the app. The next session starts a second folder.
  - **Delete and re-create** an athlete with the same name: the new athlete inherits the old history.
  - **Two golfers named alike** share sessions, tendencies and `fault_profile.json`.
  - With an empty library path, swings go to AppData while every reader looks nowhere.
- Fix:
  - Store the athlete's library folder on the athlete record, set once at creation (uuid-based, or the sanitised name plus a uuid suffix), and migrate existing folders.
  - Resolve the folder in **one** place: an `Athlete` value or object handed to every consumer, replacing the six formulas and the reverse map.
  - Correct the comment at `athlete_controller.cpp:204`.
- Effort: M (plus a one-off migration)      - Related: E-01, E-13

### E-03 · `currentAthleteChanged` fires on every athlete-record edit, so app-wide listeners reload the live carousel and drop the review
- Category: 8 State and signal cascades · 1 Lifetime      - Severity: High
- Evidence:
  - `athlete_controller.cpp:203-227`: `reload()` always ends with `emit currentAthleteChanged();`. It runs from `saveAthlete` (`:391`), `updateAthlete` (`:465-484`), `setClubRecord` / `removeClubRecord` (`:792-808`), `setPhotosApproved`, `selectAthlete` and `deleteAthlete`.
  - `ScreenAthleteForm.qml:177-204`: one Save calls `saveAthlete` and then `updateAthlete` four times, plus `setPhotosApproved`, which makes 5–6 emissions. `AthleteClubsSection.qml:161` commits each club field via `setClubRecord`.
  - `main.cpp:461-465`: `shotModel.loadSessionDir(pinpoint::SwingDocReader::latestSessionDir(…))` is not guarded by `running`. That contradicts `shot_list_model.h:183-186`: *"never during live capture (guarded in QML)"*.
  - `session_review_controller.cpp:284-289`: `onAthleteChanged()`: `if (m_reviewActive) resumeLive(); refresh();`. `refresh()` walks every session folder of the athlete on the GUI thread (`:202-217`).
  - Not guarded either: `WorkOnsController::setAthleteDir` and the report list are guarded by `if (m_athleteDir == dir) return;` (`work_ons_controller.cpp:375`), so they are harmless here. The two unguarded listeners are the ones that matter.
- What goes wrong (**suspected**). During a running session, the coach taps the Home avatar, changes one club length and saves.
  - The live carousel is cleared and reloaded through the cheap path, five times. Each row loses its `analysisDetail` (`shot_list_model.cpp:231-246` loads `QVariantMap{}`).
  - In-memory-only rows disappear: a failed shot has no `swingDir`, contrary to "show with a warning, never hide".
  - Every row gets a new id, so `SessionMode.focusedShotId` names no row: the action bar disappears and the card highlight is lost while the stage keeps playing. A launch-monitor reading for the last failed shot then calls `attachSwingDir(lastShotId, …)` for an id that no longer exists (`launch_monitor_controller.cpp:467-471`).
  - Editing any athlete while reviewing a session drops the review.
- Fix:
  - Emit `currentAthleteChanged` only when the current uuid changes. Add `currentRecordChanged` for edits.
  - Longer term, create athlete-scoped objects per athlete (E-13) so that "switch" means "replace", and an edit means nothing to them.
- Effort: S (the signal split) / M (the guards)      - Related: E-01, E-13, E-04

### E-04 · "The selected shot" is held five times under four definitions, with no model object, and each consumer fetches its own copy
- Category: 4 One source per derived fact · 2 Identity      - Severity: High
- Evidence (the five holders):
  1. `SessionMode.qml:48-49`: `property int focusedShotId` / `property string focusedSwingDir`.
  2. `PpShotCarousel.qml:112-113`: `property int selectedShotId` / `property Item selectedCard`. This copy is set only by `_toggleShot` and `_refocusAway`, not by `SessionMode.pickShot` (`SessionMode.qml:104-112`) and not by the post-shot `enterReplay` (`Main.qml:263-267`).
  3. `ScreenWrist.qml:53-55`: `shotReplay.swingDir !== "" ? shotReplay.swingDir : (wristCarousel.selectedCard ? wristCarousel.selectedCard.swingDir : "")`. This feeds the data viewer, markup and 3-D (`:175`, `:193`, `:219`) and is published to…
  4. …`CurrentSwing.swingDir` (`ScreenWrist.qml:69-74`, `current_swing.h`), read by the Diagnostic Model screen in Settings.
  5. `shotReplay.shotId` / `swingDir` (`shot_replay_controller.h:43-48`). Charts reads only this (`PpChartsPanel.qml:114-115`: `_swingDir = shotReplay.active ? shotReplay.swingDir : ""`). The camera gates read it in Replay and SessionMode otherwise (`PpCameraFrame.qml:88`).

  Consumers using `SessionMode.focusedSwingDir`: Diagnostics (`PpSessionDiagnosticsPanel.qml:113`), Consistency (`PpConsistencyView.qml:157`) and annotations (`AnnotationTool.qml:78`). The action bar, edit popover and filter use `activeModel.shotSummary(SessionMode.focusedShotId)` (`PpShotCarousel.qml:176`). The LM board uses `focusedShotId` (`PpLaunchMonitorPanel.qml:66`).
- What goes wrong (**suspected**; each step is the code as written):
  1. An auto-replayed shot plays to its end, and `Main.qml:276-279` calls `SessionMode.enterCapture()`, which stops the replay but keeps the focus (`SessionMode.qml:120-129`).
  2. With Charts and Diagnostics both on the Capture stage, Diagnostics' *This shot* shows the new shot, Charts' *This shot* is empty, and the data viewer, 3-D and `currentSwing` show whichever card was last clicked (or nothing).
  3. Picking a dot on a chart (`PpReaderPlace.pickShot` → `SessionMode.pickShot`) for a shot with no playable video leaves `shotReplay` empty (`shot_replay_controller.cpp:72-79`), so ScreenWrist falls back to the *previously clicked* card's swing.
- Fix:
  - Create one C++ `SelectedShot` (or `ShotRef`), owned by the session view context. It holds the session, the swing number and folder, the row, the ledger id (resolved once) and a handle to the one detail payload (E-10), and it clears itself when its session is replaced.
  - Have SessionMode, the carousel, ScreenWrist, `CurrentSwing` and the replay all write and read that one object, so that "playing" becomes a property of it rather than a different definition of "selected".
- Effort: M      - Related: E-05, E-06, E-10

### E-05 · The selection outlives its session and is resolved by folder *name* into another session's shot
- Category: 1 Lifetime · 2 Identity · 11 Fallbacks that mask a fault      - Severity: High (**suspected**; wrong shot shown)
- Evidence:
  - Only the carousel clears the focus (`PpShotCarousel.qml:211`, `:259`, `:718`), plus SessionMode's own review transitions. Neither session start (`Main.qml:682-693`; `PpSessionToolbar.qml:188-193`), nor session end (`PpSessionToolbar.qml:426-437`), nor the athlete reload (`main.cpp:461-465`) clears it.
  - `session_diagnostics_model.cpp:792-811`, `shotIdForSwingDir`: after an exact `cleanPath` miss, `if (QFileInfo(QDir::cleanPath(it.value())).fileName() == name …) return it.key();`, and then `name.mid(name.lastIndexOf('_') + 1).toInt()`. There is no check that the swing belongs to this session.
  - `PpReaderPlace.qml:372-376` has the same basename fallback: `have.substring(have.lastIndexOf("/") + 1) === name`.
  - `SessionMode.qml:197-202`: `_loadFocused` restarts `shotReplay` on the stale (id, dir) pair whenever Replay or Analyse is entered.
- What goes wrong (scenario):
  1. With *Auto-replay after capture* off, in session A select shot 3, then press Capture (live focus is kept), End, and Start a new session.
  2. Hit three shots. The focus is still `A/swing_0003`.
  3. Switch to Replay: the stage plays session A's swing 3 over session B's carousel, and the Diagnostics *This shot* shows **B's shot 3** (basename match). Two different swings are presented as the selected one.

  The same happens after an athlete switch while not reviewing.
- Fix:
  - Scope the selection to its session (E-04): replacing the session clears it.
  - Give the swing identity a session component (session id + swing number).
  - Restrict the basename fallback to "same session, different mount", or remove it.
- Effort: S (clear on session change) / M (scoped identity)      - Related: E-04, E-07

### E-06 · Three user-visible meanings of "Shot N" (carousel swing number, ledger position, chart rank)
- Category: 2 Identity · 4 One source      - Severity: High (**suspected**; numbers disagree on screen)
- Evidence:
  - Carousel, action bar and notifications: `ordinal` = the document's `swing.index` (`shot_list_model.cpp:149-158`; `swing_doc.cpp:1146`; `PpShotActionBar.qml:140` `"#" + root.summary.ordinal`; `shot_processor.cpp:2474`). The header states the intent: `shot_list_model.h:271-275`: *"A notification saying "Shot 7" must mean the row the user can see."*
  - Diagnostics: `PpTypeset.qml:218` `shotNo(readout)` = `readout.shotIndex + 1`, where `shotIndex` is the position in the ledger (`session_diagnostics_model.cpp:5950-5952`). This feeds 22 call sites.
  - Charts: `session_series_model.cpp:856` `const int ordinal = snap->count() + 1;`. That is the rank among *analysed* shots, since unanalysed ones are skipped (`session_series_model.h:108-110`). `charts_delivery_presenter.cpp:618` and `charts_consistency_presenter.cpp:586-588` then print `"Shot %1 of %2"`.
- What goes wrong: in a session of five swings, trash swing 3 (or have one launch-monitor-only or unanalysed swing). Swing 4 is then "#4" on its card, "shot 3 of 4" in Diagnostics and "Shot 3 of 4" in Charts.
- Fix:
  - Put one display number on the shot, the document's swing number, carried by the Shot object.
  - Express position only as "of M" or as an index. The presenters print the shot's number, not their rank.
- Effort: S–M      - Related: E-07, E-04

### E-07 · Internal shot identity: two machine numberings, six folder-number parsers, and an `ingestShot(shotId, …)` parameter that carries the wrong number and is ignored
- Category: 2 Identity · 5 Duplication · 7 Contracts      - Severity: Medium
- Evidence:
  - The numberings:
    - `ShotListModel` id: `m_nextId` is never reset (`shot_list_model.h:304`; `clear()` resets only `m_nextOrdinal`, `.cpp:333-342`). The live and review instances both count from 1.
    - The ledger id is the **folder number**: `session_diagnostics_model.cpp:839` `if (const int fromFolder = folderShotId(swingDir); fromFolder >= 0) shotId = fromFolder;`.
  - The folder-number parsers: `swing_paths.cpp:68` `swingNumber`; `session_diagnostics_model.cpp:650` `folderShotId`; `session_series_model.cpp:199` `SessionSeriesModel::folderShotId`; `session_diagnostics_model.cpp:857-859` (inline in `regradeShot`); `:806-809` (inline in `shotIdForSwingDir`); `launch_monitor_controller.cpp:445-448` `// "swing_0007" → 7`.
  - Model id → folder: `shotSummary(id).swingDir` (`PpShotCarousel.qml:263`) and `swingDirsForIds`. Folder → model id: an `Instantiator` scan over `activeShots` (`PpReaderPlace.qml:355-379`). Ledger id → folder: `PpBrush.dirOf` (`PpBrush.qml:51`). Model id → ordinal: `ordinalForId`.
  - `PpSessionDiagnosticsPanel.qml:325-326`, `PpChartsPanel.qml:210` and `PpConsistencyView.qml:132` pass `shotProcessed(shotId, …)`, which is the **carousel model's id**, into `ingestShot(shotId, swingDir)`. The diagnostics model overrides it from the folder (`:837-839`). The series model queues it (`session_series_model.cpp:486`) and then takes `folderShotId(swingDir)` (`:234`).
  - Two past bugs of exactly this kind are recorded at `PpShotCard.qml:294-297` and `PpSessionDiagnosticsPanel.qml:109-112`. I found **no remaining site that passes the model id where a ledger id is read**. The `ingestShot` parameter is the live trap: it is typed `int shotId` and it is the wrong id.
- What goes wrong: every new consumer must know which of the two numbers it holds. The signature invites the mistake that has been made twice already.
- Fix:
  - Remove the `shotId` parameter from both `ingestShot`s.
  - Use one `swingNumberOf(dir)` in Export and delete the five other copies.
  - A Shot object carrying (session, swing number) makes the carousel id an internal row handle that never leaves the model.
- Effort: S      - Related: E-05, E-06

### E-08 · "The current session" is re-derived from strings in five places with differing rules, and "the live session when not running" alternates between *latest* and *today*
- Category: 4 One source · 5 Duplication · 2 Identity      - Severity: High
- Evidence:
  - The same three-way fallback chain appears four times:
    - `PpSessionDiagnosticsPanel.qml:133-139`: `(sessionReviewController.reviewActive && sessionReviewController.activeSessionId !== "") ? … : shotProcessor.activeSessionDir !== "" ? shotProcessor.activeSessionDir : sessionReviewController.liveSessionDir`;
    - copied verbatim at `PpChartsPanel.qml:95-98`, `PpConsistencyView.qml:108-111` and `PpTrustPanel.qml:40-43`.
  - `liveSessionDir` is derived from the first row that has a folder: `session_review_controller.cpp:83-95` `return QFileInfo(sd).absolutePath();`.
  - Two loaders give different answers for the live carousel:
    - `main.cpp:338-339` and `:461-465` use `latestSessionDir`, the most recently *modified* folder of any date and type (`swing_doc.cpp:2530-2537`).
    - `ScreenWrist.qml:80-85` and `PpSessionToolbar.qml:435-436` use `todaySessionDir(type)`.

    So the carousel, and everything that follows `liveSessionDir`, shows yesterday's session at launch and an empty one once the session screen is visited.
  - `main.cpp:431-433`: the archiver's "running session" is `sessionReviewController.liveSessionDir()`, which is empty until the first shot lands, not `shotProcessor.activeSessionDir()`.
  - `SessionMode.sameFolder` exists because the root is never normalised: `app_settings.h:394-400` `normaliseLibraryPath` keeps a trailing slash, and `swing_paths.cpp:161-162` concatenates `root + '/'`. Folder comparisons then come in four styles:
    - `SessionMode.folderKey` / `sameFolder` (18 QML uses);
    - `QDir::cleanPath` (44 in Gui C++);
    - `QFileInfo::absoluteFilePath` (`session_review_controller.cpp:162,212,267,354`);
    - exact `==` (`shot_list_model.cpp:256,634,647`; `ReanalysisController::holds`, `reanalysis_controller.h`; `Main.qml:429,433` `shotReplay.swingDir !== swingDir`).
- What goes wrong:
  - Every new panel must copy the chain, and a copy that drifts points a panel at another session.
  - The "live" fallback changes meaning with navigation: Home's report row, the archiver exemption and the diagnostics fallback all follow whichever loader ran last.
  - Any comparison made with `==` between a processor-spelled path and a carousel-spelled one misses on a library path ending in `/`.
- Fix:
  - Create a `Session` object (dir canonicalised once at creation) and one `currentSession` holder (live or reviewed) that panels bind to. This deletes the chain from four panels and the fallback logic from `main.cpp`.
  - Normalise the library root once in `AppSettings`.
  - Choose one rule for "today's live session when not running".
- Effort: M      - Related: E-01, E-12, E-14

### E-09 · `ShotProcessor` is a god class mixing capture orchestration, job resolution, serialisation, document writing, carousel rows, a replay player and session-folder ownership; it has no direct test
- Category: 6 Size and cohesion · 12 Testability · 3 Layering      - Severity: High
- Evidence (`shot_processor.cpp`, 2,737 lines; `shot_processor.h`, 401):
  - **Serialisation helpers** in an anonymous namespace, `:71-698` (~630 lines). `toAnalysisDetail` alone runs `:338-~680`, about 342 lines.
  - **Capture orchestration**: post-roll and gather `:807-946`. `finishGatherAndLaunch` (`:948-1148`, 200 lines) does deferred IMU stitching, window construction, the ball-snapshot freeze and the segmentation launch.
  - **Job resolution from live app state**: `buildAnalysisJob` (`:1170-1484`, 314 lines) and `buildSwingExportJob` (`:1563-2030`, 467 lines). Both read `AppSettings`, `AthleteController`, `SessionController`, `DeviceEnumerator::instance()` and the camera instances.
  - **Persisted athlete state**: the club-length prior is written into AppSettings (`:2134-2163`).
  - **Document write and integrity checks**: `maybeJoin` (`:2190-2347`, 157 lines, on a worker capturing `this`).
  - **Carousel row and announcements**: `onSwingDocWritten` (`:2350-2516`, 166 lines) calls `m_shotModel->addShot(…)` (`:2430`).
  - **The in-window ¼× replay player**: `:2521-2700`.
  - **Session-folder ownership**: `todaySessionDir` / `beginSessionFolder` / `endSessionFolder` (`:2036-2073`) through `m_swingPaths` (`.h:400`).
  - **An unrelated UI-stall watchdog** in the constructor (`:736-752`).
  - **No test links `shot_processor.cpp`.** Only the extracted `shot_outcome.h` is tested (`src/Analysis/tests/CMakeLists.txt:517`). `shot_outcome.h:27-32` explains why: *"⚠ THIS IS A SEAM, NOT A FIX … linking a test against the decision logic meant linking ONNX, OpenCV and FFmpeg."*
- What goes wrong:
  - Any change to what a shot records (a new athlete fact, a new detail block) is made in a file that cannot be tested and that also owns the trigger state machine.
  - The athlete and session facts are read from live controllers at three different moments (impact, analysis start, save), which is how E-01 arises.
  - The session folder belongs to an object whose lifetime is the app's, not the session's.
- Fix: split along the seams the file already names.
  1. `WindowGatherer` (pause, gather, stitch, window).
  2. A pure `ShotJobBuilder` taking a **`ShotContext` value**: an athlete snapshot, session (folder, type, club), a settings snapshot, the camera and device facts frozen at the pause. This is testable without Qt Multimedia or ONNX.
  3. The pipeline state machine.
  4. `ShotDocumentWriter` (integrity, write).
  5. `AnalysisDetailMapper`, shared with the disk path (E-10).
  6. `WindowReplayPlayer`.

  Move folder ownership to the Session (E-12), and move the stall watchdog out.
- Effort: L      - Related: E-01, E-10, E-11, E-12; June "god objects" theme

### E-10 · Three producers of a shot's `analysisDetail`, in different shapes and time domains; parity is held by comments, and each consumer re-reads the document
- Category: 5 Duplication · 7 Contracts · 4 One source      - Severity: High
- Evidence:
  - The three producers:
    - **Live:** `shot_processor.cpp:338-680` `toAnalysisDetail(const SwingAnalysis &)`. Times are absolute (`t_us`), and phases carry `"segment"` (`:393-397`).
    - **Replay:** `disk_replay_source.cpp:264-470`. Times are window-relative (`relUs(t, t0)`); phases have no `segment`; there is no `resemblance`, `interval` or `pattern`. Phase numbers are hard-coded: `:327-332` `if (phase == 0) … else if (phase == 5) … else if (phase == 7)`.
    - **Review rows:** `swing_doc.cpp:2265-2330` `readSwingJson`. It passes raw JSON arrays through and adds `perRegion`, `perPhase`, `segmentation`, `bindings` and `timings`.
  - A fourth shape: rows from `loadSessionDir` carry **no** detail (`shot_list_model.cpp:231-246`, `QVariantMap{}`). `lm_session_model.cpp:1011-1020` compensates with a disk read.
  - The parity is maintained by comments, not by code: `shot_processor.cpp:363` *"Mirrored in disk_replay_source.cpp: a live shot and its reloaded self must agree"*; `disk_replay_source.cpp:303,309,322` *"ShotProcessor's twin"*.
  - The same selected shot is parsed again by `SwingDataSource` (`swing_data_source.cpp:316-317`, `SwingStore::load`), by the series model, and by the diagnostics model. The live shot is *re-parsed* by the replay right after it is written (`disk_replay_source.cpp:152`, `takeJustWritten` avoids only the file read).
- What goes wrong:
  - A consumer that reads `detail.phases[i].segment`, `detail.interval` or `detail.perPhase` gets it from one source and not from another: live row against replay against review row.
  - `ShotListModel::previousAnalysisDetail` answers a full map on a fresh live row and `{}` on a reloaded one (`shot_list_model.cpp:629-640`).
  - Each new block must be added in three places, and the comments record that it has been missed before (`shot_processor.cpp:307-312`: *"neither bridge forwarded it, so the overlay's dense body mode had never once run"*).
- Fix: one mapper (doc → typed `ShotDetail`, plus `SwingAnalysis` → the same type) in one time domain, held once per shot by the Shot / SelectedShot object. The replay applies its window offset at the view boundary.
- Effort: M–L      - Related: E-04, E-09, E-11

### E-11 · The swing.json header is built twice and the copies already disagree: `athlete.heightM` is never written for a normal capture (June item: **open, now shown to diverge**)
- Category: 5 Duplication · 9 Persistence      - Severity: High
- Evidence:
  - The exporter header: `swing_exporter.cpp:917-944`, `root["athlete"] = { name, uuid, handedness }`.
  - The synthesised header: `shot_processor.cpp:2075-2117`, *"Mirrors the exporter's header exactly (swing_exporter.cpp)"*, which then adds `heightM` (`:2095-2100`).
  - No file in `src/Export/*.cpp` writes `heightM`. `swing_exporter.h:353` claims `// … 0 = unknown → athlete.heightM`.
  - The reader: `swing_reanalyzer.cpp:787-788` `job.athleteHeightM = root["athlete"].toObject()["heightM"].toDouble(0.0);`. The consumer: `analysis_pipeline.cpp:3366` `in.heightM = ctx.job.athleteHeightM;` (the skeleton scale prior). The live path uses `m_athlete->currentHeightM()` (`shot_processor.cpp:1267`).
  - `docs/reference/swing_json_schema.md:194` concedes: *"the exporter's `athlete` block does not write it, so a normal capture lacks it."*
- What goes wrong: every swing with video is re-analysed with height 0 (unknown), while its live analysis used the profile height, so the re-analysis does not reproduce the live 3-D scaling. This contradicts the field's stated purpose (`shot_processor.cpp:2093-2094`). It is a defect visible from the code; I have not run it.
- Fix: one header builder used by both paths (`captureBlock` is already shared; extend that to the whole header). A test that both headers serialise the same athlete block.
- Effort: S      - Related: June [H] "header built twice"; E-09

### E-12 · The session lifecycle is a four-call QML choreography repeated in four places, with an order contract that `main.cpp` depends on
- Category: 3 Layering (domain rule in QML) · 5 Duplication      - Severity: Medium
- Evidence:
  - Wizard start, `Main.qml:682-693`: `shotProcessor.beginSessionFolder(...); shotModel.loadSessionDir(""); sessionController.start(...); cameraManager.startCapture()`.
  - Toolbar start, `PpSessionToolbar.qml:188-193`, the same with `extend`.
  - Toolbar End, `PpSessionToolbar.qml:426-437`: `stopCapture; endSession; endSessionFolder; loadSessionDir(todaySessionDir)`.
  - Close confirm, `Main.qml:190-200`: the same, but **without** the carousel reload.
  - `main.cpp:381-386`: *"Queued: both end-session paths call sessionController.endSession() and THEN shotProcessor.endSessionFolder()"*. C++ correctness depends on the order of QML statements.
- What goes wrong: a fifth entry point, or a re-ordering, changes when work-ons unpause, whether an empty folder is trashed, and which session the carousel shows. Nothing enforces the sequence.
- Fix: `SessionController::begin(extend)` / `end()` (or the Session object of E-01) owns the folder, the live shot list, capture intent and focus, and emits one `sessionChanged`.
- Effort: M      - Related: E-01, E-08

### E-13 · App-lifetime globals hold athlete-scoped state and are re-pointed and cleared by hand
- Category: 1 Ownership and lifetime      - Severity: Medium
- Evidence:
  - The four re-point connections at `main.cpp:380`, `:422`, `:459-460` and `:461-465`. The comment at `:456-458` records the bug that came from missing one: *"switching athlete left BOTH showing the previous athlete's shots"*.
  - `work_ons_controller.cpp:373-409` `setAthleteDir` clears about 20 members by hand: `m_records`, `m_queue`, themes, tendencies, marks, scope, filter, summary. `clearTendencies()` is called twice (`:399`, `:406`). Every new athlete-scoped member is a leak until someone adds a line here.
- What goes wrong: state from the previous athlete survives any member that the hand-written reset misses. The redundant emissions of E-03 also run each reset path.
- Fix: an `AthleteContext` object, created when an athlete is selected and destroyed on switch. It owns the work-ons, the report list, the sessions list and the athlete-level models. QML binds to `athleteContext.*`.
- Effort: L      - Related: E-02, E-03, E-14

### E-14 · Panels own session-level models, so every Capture↔Replay switch rebuilds them, and their state has to be parked on `SessionMode`
- Category: 1 Ownership (view-owned models)      - Severity: Medium (the structural cause is in diagnostics; this is the view side of it)
- Evidence:
  - `PpModeStage.qml:96-99`: `readonly property var active: _defs.filter(… ViewLayout.isPanelOn(SessionMode.mode, d.key) …)` is a new array on every mode change. The Repeaters take it as their model (`:283`, `:386`), so every card and its `Loader` is destroyed and rebuilt, even when the same panels are on in both modes.
  - `SessionMode.qml:56-70` parks `diagnosticsNav` and `chartsNav` *"because the panel does not survive a mode switch"*. `PpSessionDiagnosticsPanel.qml:244-262` explains the save-as-it-changes workaround.
  - The cast window: `Main.qml:326-327` `_openCast` destroys and recreates `PpSessionDiagnosticsWindow` on each pop (in window mode, after every shot), and with it a new `SessionDiagnosticsModel` that runs `activateSession()`. `PpSessionDiagnosticsWindow.qml:29-36`: *"TWO MODELS, ON PURPOSE FOR NOW"*.
- What goes wrong: each rebuild re-scans and back-fills the session from disk (twice per auto-replayed shot). Per-reader state is lost unless parked by hand. The cards' pips and the regrade go dark whenever no panel holds the `sessionDiagnostics` slot.
- Diagnosis: the stage is a per-mode *view* hosting *models*. The rebuilds are a symptom of having no session-scoped owner. Keying the Repeaters by panel key would only hide it.
- Fix: session-level models (diagnostics ledger, series) owned by the Session object (E-08/E-12) and read by panels. With that, the parked `*Nav` maps can become ordinary panel state, or stay as view state on purpose.
- Effort: M (on this side; the model move is diagnostics')      - Related: E-08, E-12

### E-15 · Dead and leftover surface in the shot and athlete layer
- Category: 14 Dead and leftover code · 13 Stale comments      - Severity: Low
- Evidence:
  - `ShotListModel::previousAnalysisDetail` and `analysisDetailForSwingDir` (`shot_list_model.h:210-221`, `.cpp:629-653`) have no production caller; only `shot_ledger_test.cpp:619-648` uses them. Their documented consumer, `WristDiagnosticsModel`, is registered as `QML_ELEMENT` (`wrist_diagnostics_model.h:44`; built at `cmake/PinPointQmlModule.cmake:721-722`) and is instantiated nowhere.
  - `ScreenAthletePicker.qml:221-222`: `label: qsTr("Import roster")` / `onClicked: console.log("Import roster pressed")`.
  - `AthleteController::recentSessions() const { return {}; }` (`athlete_controller.h:91`) is a property that is always empty.
  - The registry's `lastSessionAt` and `sessionCount` are written at creation and "nothing writes" them (`athlete_controller.h:68`).
  - Stale "four mode screens" text: `session_review_controller.h:33-36` (*"switching Swing↔Wrist↔GRF↔Coach"*), `shot_list_model.h:31-33` and `PpShotCarousel.qml:19-20`.
- Fix: delete, or wire.
- Effort: S

### E-16 · `PpModeStage` hand-draws its tab strip (PpMicro + Rectangle + MouseArea) while `PpTabs` and `PpTabButton` exist
- Category: 3 (CLAUDE.md UI rule) · 12 (lint gap)      - Severity: Low
- Evidence:
  - `PpModeStage.qml:456-490`: a `Row` of `PpMicro` labels, a 2 px `Rectangle` rule and a bare `MouseArea` with `onClicked: stage.tabIndex = tab.index`. It has no keyboard path. `PpTabs` provides arrow keys, a focus ring and accessibility (`docs/design/system/components/PpTabs.md`).
  - `design_system_lint_test` does not flag it, presumably because the label is a `Pp*` component rather than `Text`.
- Fix: decide whether `PpTabs` (a typeset register) or `PpTabButton` fits the stage heading. If neither does, it is a design-system change, which **needs Mark's approval before it is built**. Widen lint rule D1 to count `PpMicro` labels.
- Effort: S (once decided)

### E-17 · ShotProcessor hygiene: the settings-fallback idiom, a second session-type label table, and the open-coded device key
- Category: 5 · 11 · 15      - Severity: Low
- Evidence:
  - `AppSettings  fallback;` at `shot_processor.cpp:1098`, `:1565` and `:2146`. At `:2146` it is a *write* path: `s->setClubLenPrior(all)` lands on a temporary when `m_appSettings` is null. Repo-wide the idiom now appears **48** times in `src/Gui` (June: 32). The processor is never constructed without settings, so these guards protect nothing.
  - `sessionTypeLabel` at `shot_processor.cpp:686-696` duplicates `profiler_controller.cpp:75-81`. This label is the `_Wrist_` token in every session folder name.
  - `shot_processor.cpp:1815` `dev.description + QStringLiteral("|") + dev.id` (June item H: open).
- Fix: inject non-null dependencies (assert), put one `sessionTypeToken()` next to `SessionController::Type`, and use `Device::key()`.
- Effort: S      - Related: June [M] fallback idiom, June H device key

## 4. Done well

- **Swing numbers are never reissued.** `swing_paths.cpp:212-243` uses a recorded high-water mark plus an upward probe (*"A number is a swing's identity — the coaching ledger keys its rows on it"*). `ShotListModel::addShot` takes the number from the document, not from counting rows (`shot_list_model.cpp:131-158`). This is the right foundation for a Shot identity.
- **`shot_outcome.h`** extracts the launch and join decisions as pure `constexpr` functions with a contract test. It names the formerly-wedged path (`JoinArrival::LaunchPathDirectly`) and distinguishes `Skipped` from `Failed`. It is a model seam, honestly labelled.
- **`SessionReviewController::activeShots`** (`session_review_controller.h:55-60`) gives one answer to "which shots is the app showing", and the LM board and `PpReaderPlace` use it.
- **`ReanalysisController` is model-agnostic by folder** (`reanalysis_controller.h:30-36`), which avoids resolving ids against the wrong model.
- **Delete releases the folder first** (`main.cpp:546-558` hooks; `ShotListModel::setFolderReleaser` / `setFolderBusyCheck` / `setTrashFunction`). These are injected functions, testable without Windows.
- **Home** holds per-athlete view state keyed by **uuid** (`hm_view_store.js`), carries no domain arithmetic (no ranking or rate computation found in `src/Gui/home`), and the athlete header uses `athleteController` properties only.
- **`QmlPayload` / `analysisDetailMap`** fixed a real GUI-freeze class (copy per read) at its source, with the rule written where readers will see it (`shot_list_model.h:91-99`).
- **`SwingPaths::sanitise`** handles Windows reserved names and truncation order. This is careful work, even though the keying it serves is wrong (E-02).

## 5. June review status (area items)

| June item | Status | Evidence |
|---|---|---|
| swing.json header built twice (`swing_exporter` vs `shot_processor`) [H] | **Open, and the copies now diverge** | E-11: `heightM` only in `shot_processor.cpp:2095-2100` |
| `AppSettings *s = m ? m : &fallback` idiom 32× [M] | **Worse**: 48 in `src/Gui`; three in ShotProcessor, one on a write path | E-17 |
| Device key `"Model\|Id"` open-coded, incl. `shot_processor.cpp:672` [M] | **Open** (moved to `:1815`) | E-17 |
| `main.cpp` composition root has positional destruction-order coupling [L] | **Open**, still documented in comments (`main.cpp:488`: *"Declared after cameraManager so it is destroyed FIRST"*) | not raised as a separate finding |
| `club` hard-coded `"DRIVER"` on reload [M, Export] | Not re-checked (outside the area); the live row's club is now settled by the doc writer (`shot_processor.cpp:2240-2247`, `:2389`) | — |

## 6. Questions I could not settle from the code

1. Is "New athlete" or "Edit athlete" during a running session meant to be possible? The picker comment calls its Select/Delete check "the one guard" (E-01).
2. Is the latest-session restore at launch (`main.cpp:333-342`) meant to differ from ScreenWrist's today-scoped carousel, or is one of them left over (E-08)?
3. Charts' *This shot* reads only an **active replay** (`PpChartsPanel.qml:114`). Is a selected shot that is not playing deliberately blank in Charts (E-04)?
4. The basename fallback in `shotIdForSwingDir` cites "a ledger written on another host". Is cross-host reading of a session actually supported, or can the fallback be limited to the same session (E-05)?
5. Should a session that crosses midnight stay one session? `SwingPaths` currently re-allocates on a date change (E-01).

## Corrections to 01-information-model.md

1. **§2, the ledger `shotId` "is the diagnostics model's own numbering".** *Corrected:* it is the **swing folder number** (`swing_NNNN` → N; `session_diagnostics_model.cpp:650-658`, `:839`). For every swing `SwingPaths` writes, this equals the carousel's `ordinal` (the document's `swing.index`; `swing_doc.cpp:1146`). The machine numberings are two (the `ShotListModel` id and the folder number), not three. The **displayed** numbers are three: carousel swing number, diagnostics ledger position + 1 (`PpTypeset.qml:218`), and Charts' rank among analysed shots (`session_series_model.cpp:856`). See E-06 and E-07.
2. **§2, "`CurrentSwing.swingDir` as a third copy" of SessionMode's selection.** *Corrected:* it is a **different definition**: the active replay, else the carousel's last-clicked card (`ScreenWrist.qml:53-55`), not `SessionMode.focusedSwingDir`. There are two further holders: `PpShotCarousel.selectedShotId` / `selectedCard` (`:112-113`) and `shotReplay.shotId` / `swingDir`, which Charts read exclusively (`PpChartsPanel.qml:114-115`). Five holders, four definitions (E-04).
3. **§1, Athlete: "three separate copies of the same lambda".** *Corrected:* there are **six** derivations of root + sanitise(name), and they differ:
   - `main.cpp:366`, `:404` and `:415`; the third ignores `PINPOINT_WORKONS_DIR`;
   - `swing_doc.cpp:2525` and `:2544`;
   - `swing_paths.cpp:161`, which falls back to the uuid and to AppData;
   - plus the reverse map in `session_report_model.cpp:335-341` and `:378-383`.

   Also, the uuid is not unused: the app INI keys the athlete record, bag, club-length prior and Home view state by uuid. Only the library is keyed by name (E-02).
4. **§1, "When the athlete changes, each holder has to be re-pointed (`main.cpp:380`, `:422`, `:459-466`)".** *Refined:* the signal also fires on **every edit** of any athlete record (`athlete_controller.cpp:227`, reached from `updateAthlete`, `setClubRecord` and `saveAthlete`). The two unguarded listeners then reload the live carousel and drop the review (E-03).
5. **§1, Session (live): `ShotProcessor::activeSessionDir` "only while a session runs".** *Refined:* it is `SwingPaths`' allocation cache. `allocateSwingDir` can **silently re-point** it mid-session, on a change of athlete uuid, name or date, with no `activeSessionDirChanged` (`swing_paths.cpp:199-212`; `shot_processor.cpp:2066,2072` are the only emits) (E-01).
6. **§1, Shot: a row "plus the analysis payload as a QML object".** *Refined:* a row has three shapes:
   - live rows carry the processor's detail;
   - rows reloaded through `loadSessionDir` carry **none** (`shot_list_model.cpp:231-246`);
   - review rows carry `readSwingJson`'s detail, a different shape again.

   The replay holds a fourth, window-relative copy (E-10).
7. **§1, Session fallback chain at `PpSessionDiagnosticsPanel.qml:133-139`.** *Confirmed,* and it is **copied verbatim** in `PpChartsPanel.qml:95-98`, `PpConsistencyView.qml:108-111` and `PpTrustPanel.qml:40-43` (E-08).
8. **§3 and §4, the panel rebuild "twice per auto-replayed shot".** *Confirmed* from `PpModeStage.qml:96-99`: `active` is recomputed per mode and is the Repeaters' model. Add: in the cast's window mode, the cast window and its model are destroyed and recreated on **every** shot (`Main.qml:326-327`) (E-14).
9. **§5, globals "re-pointed by hand".** *Confirmed.* Add that the guard against switching athlete during a session exists **only in QML** (`ScreenAthletePicker.qml:33-35,48`) and that creating a new athlete bypasses it (`athlete_controller.cpp:385-386`) (E-01).
