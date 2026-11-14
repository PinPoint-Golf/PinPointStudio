# 01 · The information model as it is

At commit `37289e33`. This page describes what the code does, not what it should do: the target is in
`03-target-architecture.md`. Every claim cites the line that shows it. Reviewers were told this page may be
wrong and to correct it; corrections are folded in and marked *(corrected)*.

## The model as the owner states it

```
Athlete ──owns──▶ Session ──owns──▶ Shot (swing_NNNN/ + swing.json) ──▶ Analysis ──▶ LM reading
   │                 │                 │
   ▼                 ▼                 ▼
Athlete diagnostics  Session diagnostics  Shot diagnostics
(tendencies, work-ons, (ledger, links/tiers,  (condition rows, outcome,
 club baselines,       flight usuals, club    three factors, carry)
 fault_profile, norms) mixes, carry today,
                       patterns, report)
   └── context flows down: athlete baselines → session grading → a shot's Sky / strike check ──┘
```

## What the code has instead

### 1. There is no Athlete object and no Session object

| Domain object | What represents it in the code | Evidence |
|---|---|---|
| **Athlete** | A *name string*: `AthleteController::currentName`. The athlete's library folder is worked out from that name in **six** places, and they differ *(corrected, E)*:<br>• three lambdas in `main.cpp`;<br>• `latestSessionDir` / `sessionDirs` in `swing_doc.cpp`;<br>• `SwingPaths`, the writer, which alone falls back to the uuid and to AppData;<br>• plus a reverse name-to-folder map in the report.<br>The uuid keys the app's own settings (record, bag, club-length prior, Home view state) and is written into every swing, but the **library is keyed by sanitised name**. So a rename orphans the athlete's history, and look-alike names share one folder (E-02). `currentAthleteChanged` fires on every record edit, not only on a switch (E-03). | `src/Gui/athlete/athlete_controller.h:34-37`, `athlete_controller.cpp:227`; `src/Gui/main.cpp:366`, `:404`, `:415`; `src/Export/swing_paths.cpp:97-112`, `:160-162` |
| **Session (live)** | A *folder path* held in three places: `ShotProcessor::activeSessionDir`, `SessionReviewController::liveSessionDir`, and the app-global `shotModel`'s contents. `activeSessionDir` is `SwingPaths`' allocation cache: it is re-allocated **silently, mid-session**, if the athlete's uuid, the athlete's name or the date changes, and no change signal is emitted (`swing_paths.cpp:199-212`) *(corrected, E; see E-01)*. Two of these spell the same folder differently, so a helper compares them as folders (`SessionMode.sameFolder`). | `src/Gui/shot/shot_processor.h:98,143`; `src/Gui/review/session_review_controller.h:73`; `src/Gui/session/SessionMode.qml:99`; `PpSessionDiagnosticsPanel.qml:149-152` |
| **Session (reviewed)** | A *folder path* (`SessionReviewController::activeSessionId`, which is the directory) plus a second, private `ShotListModel` (`sessionReviewController.shots`). | `session_review_controller.h:54,60,66` |
| **Session (running state)** | `SessionController`: clock, type, club, focus condition. It holds no folder, no shots and no diagnostics. | `src/Gui/session/session_controller.h:52-67` |
| **Shot** | A row in one of two `ShotListModel`s (`ShotListModel::Shot`: id, ordinal, club, metrics map, the analysis payload as a QML object, swingDir, LM device, annotations), plus the folder on disk. | `src/Gui/shot/shot_list_model.h:248-269` |

Consequence: "the current session" is not an object anyone can hold. Each consumer works it out from
whichever strings it can reach (e.g. `PpSessionDiagnosticsPanel.qml:133-139`: reviewed session, else the
running session's folder, else the carousel's fallback). When the athlete changes, each holder has to be
re-pointed by its own connection (`main.cpp:380`, `:422`, `:459-466`). The comment at `main.cpp:456`
records a bug where this was missed.

### 2. The shot has at least three numberings, and the selection is a pair of strings

| Key | Meaning | Where |
|---|---|---|
| `ShotListModel` **id** | A monotonic counter per model instance. The live and reviewed models count separately. | `shot_list_model.h:272-276` |
| `ShotListModel` **ordinal** | The number the carousel shows (per session). It diverges from the id once a shot is trashed. | same |
| Ledger **shotId** (`SessionDiagnosticsModel`) | The swing folder's number (`swing_0007` → 7), on every path in (`folderShotId`). `shotIdForSwingDir` exists for paths spelled differently by different hosts. *(corrected, D)* The header's "three numberings" comment is stale (D-16). | `session_diagnostics_model.cpp:643-658`, `:791`; `PpShotCard.qml:296-297` |
| **swingDir** | The folder path: the only identity shared by both sides. 881 uses in 120 files. | `data/identity_inventory.md` |

The ledger shotId equals the carousel's `ordinal` for every swing `SwingPaths` writes, so there are two machine
numberings: the model id and the folder number. There are **three displayed** numberings *(corrected, E)*:
- the carousel's swing number;
- the diagnostics ledger's position + 1;
- Charts' rank among analysed shots.

They disagree after a trashed or unanalysed swing (E-06, F-07).

A shot also has **three club identities** *(corrected, G)*:
- the declared club;
- the "DRIVER" stub some readers apply;
- the launch monitor board's override, persisted as `review.clubOverride` and read only by the board.

The board keeps its own per-club statistics (`lm_session_model.cpp:354-364`) (G-07).

**The selected shot** is held in **five places under four definitions** *(corrected, E)*:
- `SessionMode.focusedShotId` / `focusedSwingDir` (`SessionMode.qml:48-49`);
- the carousel's own `selectedShotId` / `selectedCard`;
- `ScreenWrist`'s "active replay, else last-clicked card", published as `CurrentSwing.swingDir`;
- `shotReplay.shotId` / `swingDir`, which Charts reads exclusively.

Nothing clears the selection when its session ends. It is resolved into a new session by folder *basename*
(E-05). There is no selected-shot object. Every view fetches its own data:
- the action bar calls `shotSummary(id)`, a fresh copy of the row;
- the cards resolve the folder to a ledger id and call `pipsFor`;
- This shot calls `shotOutcome(id)`;
- Charts go through their own presenters.

### 3. The three diagnostic levels and who owns them

| Level | Computed by | Created by / owner | Lifetime | Persisted to |
|---|---|---|---|---|
| **Shot** (rows, outcome, factors, carry) | `SessionDiagnosticsModel` (`detectShot`, `ensureOutcomes`, `shotOutcome`), using `src/Coaching` functions. It is not held on the shot: it is recomputed inside the session model whenever that model is dirty. **The three factors are not on the shot surface at all.** `shotOutcome()` returns none (`session_diagnostics_model.cpp:2203-2218`). They are computed only inside the session club-mix tally (`:2060`) and again in the athlete reductions. The athlete level also recomposes every shot's outcome (`tendencies.h:551-560`, the outcome filter `work_ons_controller.cpp:1681`, the report), from link evidence persisted in `work_ons.json` for that purpose *(corrected, G)*. | **The view**: `PpSessionDiagnosticsPanel.qml:90` | That panel's. The panel is rebuilt on every Capture↔Replay switch, which happens twice per auto-replayed shot (`PpSessionDiagnosticsPanel.qml:167-169`, `:247-249`). Each rebuild runs `activateSession()` (`_pointAtSession` at `:153`). *(corrected, D)* That is more than a disk scan plus back-fill. It also covers:<br>• a fresh pack parse (`session_diagnostics_model.cpp:702`);<br>• a SHA-1 over the pack and norms (`:929-943`);<br>• the profile read;<br>• `prepareClubBaseline`, which reads up to ten predecessor ledgers (`:2528-2573`);<br>• a read of every sibling session's ledger (`:1616`).<br>A selection click re-reduces the whole session (`setSelectedShotId → rebuild()`, `:767-773`). | Rows: `<session>/diagnostics.json`. The flight-check verdict is persisted in the rows (`overruled`). Corrections and intents are persisted in the envelope. Composed outcomes and factors are not stored: they are recomposed in the model (`:1830`) and again in `tendencies.h:558` from `work_ons.json` evidence. *(corrected, D)* |
| **Session** (ledger, links/tiers, flight usuals, mixes, carry today) | The same `SessionDiagnosticsModel` instance. | The view, as above. **A second instance** lives in the cast window (`src/Gui/shot/PpSessionDiagnosticsWindow.qml:29`: "TWO MODELS, ON PURPOSE FOR NOW"). **A third**, without a panel, lives inside `WorkOnsController` to catch up stale sessions (`work_ons_controller.h:60`, `:647`; `work_ons_controller.cpp:351`). **A fourth** belongs to `SessionReportModel`, with persist off, while a report is built (`src/Gui/report/session_report_model.cpp:298-311`). *(corrected, D)* Four roles are chosen by flags (`baselineAuthority`, `persistEnabled`, `reviewing`) (D-03). | Per instance. With no panel open, no catch-up and no report job, no session-level model exists. The catch-up is paused while a session runs (`main.cpp:383-385`), so during a live session with the panel off, **no row is graded at all** until the session ends. | `<session>/diagnostics.json`, written by whichever instance holds the session ("the panel and the work-ons catch-up own the file", `session_diagnostics_model.h:275-278`). |
| **Athlete** (tendencies, work-ons, swing summary, club distances) | `WorkOnsController`, using `src/Coaching` (`tendencies.h`, `work_ons.h`, `swing_themes.h`, `club_distances.h`). | `main.cpp:364`: **an app-lifetime global**, re-pointed at a new athlete folder string when the athlete changes (`:366-386`). | The app's. The athlete is swapped underneath it. | `<athlete>/tendencies.json`, swing summary. Work-on records are per session (`<session>/work_ons.json`). *(corrected, D)* Two athlete files are written by the *session* model: `fault_profile.json` and `outcome_corrections.jsonl`. `fault_profile.json` is in fact never written, because `closeSession()` has no caller (D-07, H-11). |
| **Session report** (a session-level product) | `SessionReportModel` | `main.cpp:395`: an app-lifetime global | The app's | Report documents under the session |

**Which way each level depends on the others, and whether that is visible:**
- **Session → shot.** Session flight usuals feed a shot's Sky and strike check (`sessionFlightUsuals(m_shots)` inside `outcomeSessionFacts()`, `session_diagnostics_model.cpp:1777-1780`; `checkStrikesAgainstFlight` rewrites a vector of shot records, `strike_from_flight.h:554`). The dependency is real but implicit: a shot's outcome can only be read through the session model that computed it.
- **Athlete → session.** The athlete's club baseline feeds session grading as a frozen input handed to
  detection (`session_diagnostics_model.cpp:979-982`, `:1244`). That athlete-level fact is computed by the
  session model itself, from up to ten predecessor ledgers (`prepareClubBaseline`, `:2504-2585`). It is
  stored in the session folder (`club_baseline.json`) by whichever instance has authority. *(corrected, D:
  `:2153` only formats the carry text.)*
- **Athlete level computes the session level itself.** The athlete level does not read session-level results from an owner. It reruns the session derivation in its own private `SessionDiagnosticsModel` when it decides a session is stale.

### 4. Views that own models (from `data/seam_inventory.md`)

QML creates 20 C++ types in 40 places. The ones that hold session data:

| Type | Instances | Note |
|---|---|---|
| `SessionDiagnosticsModel` | panel + cast window (+ private ones inside `WorkOnsController` and `SessionReportModel`) | as above |
| `SessionSeriesModel` | `PpChartsPanel`, `PpConsistencyView`, `PpTrustInlineFor`, `PpTrustPanel`, `PpTrustSummary…` | `session_series_model.h:51` says it was "INSTANTIATED BY THE PANEL … SessionDiagnosticsModel's decision": the pattern was copied on purpose |
| `Charts*Presenter` ×6, `ChartsRelationsPresenter` ×2 | `PpChartsPanel`, `PpConsistencyView` | each has its own worker thread |
| `LmSessionModel` | `PpLaunchMonitorPanel` | the session's LM readings, owned by a panel |
| `SwingDataSource` | `PpDataViewer` | a shot's series, owned by a panel |
| `ShotFilterProxyModel` | `PpShotCarousel` | a view-level filter: legitimately a view's |

Views reach the session diagnostics model through a nullable singleton slot that the most recently built
panel claims ("last wins", `PpSessionDiagnosticsPanel.qml:225-230`). It has five consumers *(corrected, D)*:
- the cards' pips;
- Charts' diagnostics refresh;
- the consistency view;
- the carousel's regrade on an LM late reading or a re-analysis (`PpShotCarousel.qml:748-755`);
- Charts' "open condition" (`PpChartsBody.qml:137-138`).

All of them silently do nothing while the slot is null.

### 5. App-lifetime globals (42 context properties, `main.cpp:1030-1297`)

Most are legitimately app-wide (devices, settings, logging, updates). The ones that hold athlete- or
session-scoped state are `athleteController` (the current athlete), `sessionController` (running
session), `sessionReviewController` (reviewed session + its shots), `shotModel` (live session's shots),
`shotProcessor` (live session folder), `workOns` (athlete diagnostics), `sessionReport` /
`sessionReportList` (athlete's reports), `currentSwing` (selection) and `launchMonitor`. Each is re-pointed
by hand when the athlete or session changes, rather than created and destroyed with it.

## In one paragraph

The domain's containment (athlete → session → shot) and its three diagnostic levels exist in the code
only as folder strings and naming conventions. No object represents an athlete, a session or a shot's
diagnostics.
- **Athlete level:** an app-global controller that is re-pointed when the athlete changes.
- **Session and shot levels:** computed together inside one 6.6k-line model. Views create it, up to three
  copies can exist at once, and it is rebuilt on mode switches.
- **The selection:** an id and a folder string, translated into three numberings.

Each consumer rebuilds "which athlete, which session, which shot" from whatever strings it can reach,
and the ⚠ comments across these files are the record of the cases where that went wrong.
