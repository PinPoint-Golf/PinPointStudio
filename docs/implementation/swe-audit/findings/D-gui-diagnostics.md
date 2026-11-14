# D · Gui diagnostics — SessionDiagnosticsModel, WorkOnsController and their QML

Reviewer D, at `37289e33`. Area: `src/Gui/diagnostics/` (C++ and QML), plus
`src/Gui/shot/PpSessionDiagnosticsWindow.qml`. Read-only review. Every runtime defect not reproduced is marked
**suspected**, with the scenario that would show it.

## 1. Summary

The arithmetic is in good shape. Reductions, outcome composition, factors and link grading live in pure,
header-only Coaching functions, and the model calls them. The **ownership around that arithmetic is the
structural fault**, and it is worse than `01-information-model.md` says:

1. **One class does four jobs**, chosen by flags. `SessionDiagnosticsModel` is the live panel, the cast, the
   work-ons catch-up and the report's reader, told apart by `baselineAuthority`, `persistEnabled` and
   `reviewing`. There are **four** instances, not three. Each writes the shared session and athlete files from
   its own memory.
2. **Golfer input can be lost on disk (suspected).** With the stage panel and a persistent cast both up, both
   write the whole `diagnostics.json` after every shot, and whichever writes last wins. A correction, a shot
   intent or a declared miss made on one is written over by the other. Only the catch-up re-reads anything
   before it writes, and it re-reads three of the six golfer-owned fields.
3. **Live grading exists only while a view does.** Without the panel open, a live shot is never graded until
   the session ends. Every consumer that reaches the model through the nullable last-wins seam
   `SessionMode.sessionDiagnostics` quietly does nothing: carousel pips, Charts refresh, regrade on rewrite,
   "open condition".
4. **All three diagnostic levels and the view state live in one 6.6k-line class.** That covers shot detection,
   session reduction, athlete-level facts (club baseline freeze, fault profile, history across sessions, tuning
   log), view state (selection, open detail, measure tab, cadence) and every view-model map. Nine functions run
   over 150 lines (`conditionDetail` is 461).
5. **The contracts are untyped.** There are 43 QVariant shapes (29 on the model, 14 on the controller),
   described only in comments. The C++ report reads the QML view-model (`spreadOf`), and the QML parses counts
   back out of model-written sentences with a regex.

The fault-profile write path (`closeSession`) has no caller in the app, so `fault_profile.json` is never
written. `session_spread.h` (domain arithmetic) sits in Gui and is included upward by Coaching.

## 2. How the area is put together

**Objects and owners**

| Object | Created by | Lifetime | Writes |
|---|---|---|---|
| `SessionDiagnosticsModel` #1 (stage panel) | `PpSessionDiagnosticsPanel.qml:90` | the panel's: rebuilt on every Capture↔Replay flip (`:247-251`) | `diagnostics.json`, `club_baseline.json` ("live"), tuning log |
| #2 (cast) | the panel inside `PpSessionDiagnosticsWindow.qml:129`, created by `Main.qml:326-347`; the "window" pop is created per shot (`:414-418`, `:434-439`) | the window's | same as #1 |
| #3 (catch-up) | `WorkOnsController::model()` (`work_ons_controller.cpp:348-371`), "always", reviewing | app (child of the `workOns` global, `main.cpp:364`) | `diagnostics.json`, `club_baseline.json`, plus `work_ons.json` via the controller |
| #4 (report) | `SessionReportModel::model()` (`session_report_model.cpp:298-311`), persist off, "never" | app (child of the `sessionReport` global, `main.cpp:395`); exposed to QML as `printModel` (`PpReportPrintHost.qml:97`) | nothing |
| `WorkOnsController` | `main.cpp:364`, re-pointed by `repoint` (`:366-386`) | app | `work_ons.json` per session; `swing_themes*.json`, `tendencies.json` per athlete |
| `WristDiagnosticsModel` | nobody (registered in `cmake/PinPointQmlModule.cmake:721`) | — | — |

**Data flow in:** `ShotProcessor::shotProcessed` goes to every panel's `ingestShot`
(`PpSessionDiagnosticsPanel.qml:323-328`). A worker runs `detectShot` (`cpp:945-1291`), and the result comes
back queued to `applyIngested` then `settleBatch`, which calls `rebuild()` and `persist()`. On activation,
`activateSession` (`cpp:1403-1624`) does the following on the GUI thread:

- reads the file;
- reconciles it against the `swing_*` folders;
- reads the athlete's `fault_profile.json`;
- freezes `club_baseline.json` from up to ten predecessor ledgers;
- queues the back-fill and the regrades;
- starts an off-thread read of every sibling session's ledger (`scanHistory`).

**Data flow out:**

- 18 QVariant properties and 11 QVariant invokables to QML.
- Typed C++ reads used by #3 and #4: `sessionWorkOns()`, `outcomeEvidence()`, `shotRecords()`, `ledgerRows()`,
  `corridorOf()`.
- Files on disk that other readers parse themselves: `session_series_model.cpp:796`,
  `charts_posture_presenter.cpp:714`, `work_ons_controller.cpp:196,1396,1670`.

**Responsibilities of `SessionDiagnosticsModel`, by level and kind**

| Responsibility | Code | Level | Kind |
|---|---|---|---|
| swing.json → condition rows, signal rows, peak times, LM readings, integrity exclusion | `detectShot` `cpp:945-1291` (345 lines) | Shot | domain (producer-side) |
| rows checked against the session's flight | `checkRowsAgainstFlight` `cpp:1771-1775` | Shot ← Session context | domain |
| ledger, session-scope ledgers, coverage, graph, links, rails, stage ratchet, explanation, ranking | `rebuild` `cpp:2589-2653`, `appendSessionScope`, `buildGraph`, `refreshExplanation`, `rankScores` | Session | domain (delegates to Coaching) |
| composed outcome per shot | `ensureOutcomes` `cpp:1827-1831` | Shot | domain |
| reasons tally, trend, cost; shot mix; carry today; club factor mix; outcome evidence | `ensureOutcomes` `cpp:1833-2082` (295 lines) | Session | domain plus wording |
| diagnostics.json read, write, reconcile, staleness | `activateSession`, `envelope`, `persist` | Session | persistence |
| golfer intent: focus, declared miss, corrections, shot intent, screens | `declare*`, `correctOutcome`, `setShotIntent`, `setIntentFrom`, `recordScreenResult` | Session | state |
| frozen club baseline from predecessors | `prepareClubBaseline` `cpp:2504-2585` | **Athlete** (snapshot stored in the session) | domain plus I/O |
| fault profile load and update; Cold expectations | `loadProfile`, `updateProfile` `cpp:3140-3245` | **Athlete** | domain plus I/O |
| tuning log append | `appendTuningLine` `cpp:2375-2386` | **Athlete** | I/O |
| readings across sessions | `readHistory`, `historyFor` `cpp:4306-4632` | **Athlete** | I/O plus view-model |
| selection, open detail and measure tab, cadence/quiet, after-shot delta | `m_selected*`, `m_detail*`, `m_cadence` | View | view state |
| card, node, link, rival, spread, history, detail, readout, pips and outcome maps | `cardMap` (207), `spreadFor` (314), `conditionDetail` (461), `shotReadout` (188), `shotOutcome*`, `pipsFor` … | View | view-model |
| worker pool, batch coalescing, activation generation | `queueDetect`, `settleBatch`, `m_activationGen` | — | infrastructure |

## 3. Findings

### D-01 · Two live instances each write the whole diagnostics.json from their own memory; golfer input on one is overwritten by the other
- Category: 1 Ownership/lifetime, 8 Concurrency, 9 Persistence — Severity: **High** (Critical if confirmed; **suspected**)
- Evidence:
  - `PpSessionDiagnosticsWindow.qml:29-35` — `TWO MODELS, ON PURPOSE FOR NOW … two models ingesting the same shots independently — wasteful, and correct … both reduce the same rows to the same surface.`
  - `PpSessionDiagnosticsPanel.qml:323-327` — every panel instance runs `diagModel.ingestShot(shotId, swingDir)`, and each settle persists (`cpp:1382-1383` — `rebuild(); persist();`).
  - `cpp:3045-3106` — the envelope is written whole from this instance's members: `intent` (focus, `declaredMiss`), `screens`, `shotIntent`, `intentFrom`, `corrections`.
  - `cpp:3116-3131` — the only merge is for the catch-up: `if (m_baselineAuthority == BaselineAuthority::Always) { … m_shotIntent = … m_corrections = … m_intentFromShot … }`. A live panel or cast ("live") re-reads nothing.
  - `cpp:2388-2433` — `correctOutcome` changes only this instance's `m_corrections`, then calls `persist()`. `declareMiss` (`cpp:1677-1684`) is the same, and has no cross-surface propagation at all. Only the focus goes through `SessionController` (`PpSessionDiagnosticsBody.qml:157-163`).
- What goes wrong (scenario): `postShotDisplayMode` is "panel" or "kiosk" with a second screen, and the stage
  diagnostics panel is on.
  1. On the stage panel the golfer corrects shot 5's label (`PpShotOutcomeHead` → `correctOutcome`). The stage
     model writes the file with the correction, and appends a line to `outcome_corrections.jsonl`.
  2. Shot 6 lands. Both models detect it on their own pools and each settles and persists. If the cast settles
     second, its envelope, which has no correction, replaces the file.
  3. The auto-replay flips the stage to Replay. That builds a new stage model, whose `activateSession` reads
     the file, and the correction is gone from the screen.

  The tuning log still says it was made. The same happens in the other direction (the persistent cast is
  interactive, `PpSessionDiagnosticsWindow.qml:89`), and to a declared miss and to shot intents. Even before a
  write, the two surfaces disagree on screen about the same shot.
- Fix: one session-scoped `SessionDiagnostics` owner per open session (D-05). Both views read that owner, and
  there is exactly one writer of `diagnostics.json`. Until then, a stopgap is to share the panel's model with
  the cast rather than instantiate a second one.
- Effort: M (the stopgap) · L (the owner) — Related: D-03, D-04, D-05

### D-02 · Session and shot diagnostics exist only while a view does, and are rebuilt with it
- Category: 1 Ownership/lifetime, 4 One source — Severity: **High**
- Evidence:
  - `session_diagnostics_model.h:65-71` — `INSTANTIATED BY THE PANEL, not registered in main.cpp … A golfer who never opens the panel should not be paying for any of it`.
  - Only the panel ingests live shots (`PpSessionDiagnosticsPanel.qml:323-327`). The catch-up is paused while a session runs (`main.cpp:383-385` — `workOns.setPaused(sessionController.activeSessionType() >= 0)`; `work_ons_controller.cpp:562`).
  - `PpSessionDiagnosticsPanel.qml:247-251` — `THE PANEL DOES NOT SURVIVE A MODE SWITCH. PpModeStage rebuilds its panels on every Capture↔Replay flip — twice per auto-replayed shot`.
  - Each rebuild re-parses and merges the characteristic pack (`cpp:702` `m_packProv(makeCharacteristicPackProvider())`; `merged_pack_provider.cpp:293-305` has no cache).
  - Each rebuild also re-hashes the serialised pack and norms (`cpp:929-943`, from `cpp:1425`), re-reads the athlete profile, and runs `prepareClubBaseline`. Then `scanHistory()` reads every sibling session's `diagnostics.json` (`cpp:1616`, `cpp:4319-4337`).
  - `cpp:726-728` — `The deliveries still queued die with this object, as they always did, and the next activation re-does them.` The destructor blocks the GUI thread on `m_pool.waitForDone()` (`cpp:721`).
- What goes wrong:
  - With the panel off during a live session, no row is graded for any new shot, so `diagnostics.json` does not
    grow. The carousel has no pips (`PpShotCard.qml:274`), and Charts never gets the diagnostics refresh
    (`PpChartsPanel.qml:223-225` targets a null seam). The grading arrives only after the session ends, through
    the catch-up. This is the fault the owner found, and it still holds at HEAD.
  - With the panel on, each auto-replayed shot pays, twice: a pack parse, a SHA-1 over the whole pack and norm
    JSON, a reconcile, and a full read of the golfer's other ledgers.
  - **Suspected:** a detection in flight when the outgoing panel is destroyed is dropped, and is graded again by
    the incoming panel. Scenario: auto-replay on, a shot lands, and the flip to Replay comes before the ~1 s
    detection finishes. The GUI thread then waits in the destructor, and the same swing's document is parsed
    twice.
- Fix: create the session diagnostics owner with the session — at Start for live, at `loadSession` for review —
  and destroy it at End or Close. Hold it on the session owner, not in a view. To keep "pay only if used", the
  owner can still defer the reduction while nobody reads it. What must not happen is that the existence of the
  rows depends on a view. Share one pack provider (an app-level `shared_ptr<const CharacteristicPack>`) across
  instances.
- Effort: L — Related: D-04, D-05, D-06

### D-03 · One class plays four roles, selected by flags; three of the roles can hold the same session at once
- Category: 1 Ownership, 6 Cohesion, 9 Persistence — Severity: **High**
- Evidence:
  - `session_diagnostics_model.h:258-263` — `WHO MAY WRITE club_baseline.json — set BEFORE activateSession (Contract A): "always" WorkOnsController's private model … "live" a live panel … "never" review panels and the session report`.
  - `h:275-280` — `setPersistEnabled … The session report reads a session this way`.
  - `work_ons_controller.cpp:356-364` — the catch-up sets `setReviewing(true)` to keep "panel-only work off the catch-up".
  - `session_report_model.cpp:304-308` — the report does the same, and turns persist off.
  - `PpSessionDiagnosticsPanel.qml:155-158` — the authority is set imperatively because `a binding's order isn't pinned, so one could race the activate`.
  - The catch-up resumes the moment a session ends (`work_ons_controller.cpp:430-431` — `Unpausing is a session ending`), while the stage panel stays pointed at that same folder (`PpSessionDiagnosticsPanel.qml:133-139`, falling back to `liveSessionDir`).
  - The catch-up also runs during review: the pause is only "a session is running".
  - The catch-up's merge-before-write (`cpp:3116-3131`) re-reads `shotIntent`, `corrections` and `intentFrom`, but not `intent.focusConditionId`, `focusFromShot`, `declaredMiss` or `screens` (`cpp:3075-3082`).
- What goes wrong (**suspected**): the golfer ends a session while some predecessor lacks a current ledger, so
  the live panel could not freeze a baseline and its rows are `|provisional` (`cpp:2488`, `cpp:2573`).
  1. The catch-up activates the session (Always), freezes `club_baseline.json`, and regrades every row.
     Detection takes seconds.
  2. Meanwhile the golfer, still on the stage panel, declares a miss.
  3. The catch-up settles and writes its envelope with the old `declaredMiss`.
  4. The next stage rebuild reads the file, and the declaration is gone.

  In the opposite order, the stage panel's next write (any correction, or a trash through `forgetShot`) puts
  its provisional rows back over the catch-up's frozen ones. Home (from `work_ons.json`) and the panel then
  describe that session from different gradings until the next activation regrades again.
- Fix: separate the roles into types. A reader for the report needs no ingest, no persistence and no
  baseline. The catch-up uses the session owner of D-05 when one is live for that folder, and otherwise a
  short-lived owner with the same single writer. Make the "who may write" contract a property of who owns the
  session, not a flag set before a call.
- Effort: L — Related: D-01, D-05, D-15

### D-04 · The nullable last-wins seam `SessionMode.sessionDiagnostics` makes every consumer silently partial
- Category: 1 Ownership, 11 Missing values, 13 Comments — Severity: **High**
- Evidence:
  - `SessionMode.qml:54` — `property var sessionDiagnostics: null`.
  - `PpSessionDiagnosticsPanel.qml:225-230` — `CLAIMED UNCONDITIONALLY BY THE MOST RECENT PANEL … the last panel to come up is the one whose ledger the cards draw`.
  - `:241-243` — released only `if (SessionMode.sessionDiagnostics === diagModel)`. Nothing reclaims the seam when the claimant leaves.
  - Consumers:
    - `PpShotCard.qml:274, 296-305` (pips);
    - `PpChartsPanel.qml:223-225` and `PpConsistencyView.qml:146-148` (refresh on `busy` false);
    - `PpShotCarousel.qml:748-755` (`_swingRewritten`, regrade on LM late data `:771` and re-analysis `:791`);
    - `PpChartsBody.qml:137-138` (`SessionMode.sessionDiagnostics.openDetail(id)`).
- What goes wrong:
  - Only the claimant regrades a rewritten swing. With the stage panel and a cast both up, a Foresight Premier
    late club reading (`:771`) regrades one model, and the other keeps quoting the old metrics on screen and
    writes them back (D-01).
  - Charts' "open condition" opens the detail on whichever model claimed last. That can be the wall cast, not
    the panel the user is looking at.
  - **Suspected:** with `postShotDisplayMode` "window", a fresh cast is created per shot (`Main.qml:326-327`)
    and claims the seam. When its dwell ends it is destroyed and nulls the seam (`:241-243`) while the stage
    panel is still up. From then until the next mode flip rebuilds the stage panel, the carousel shows no pips,
    and Charts, the consistency view and the regrade-on-rewrite all do nothing. Scenario: auto-replay off (or
    the dwell outlasting the replay), one shot.
  - Every consumer is written defensively (`ignoreUnknownSignals`, null checks), and none says that a feature
    is absent. That runs against the product rule "show with a warning, never hide".
- Fix: replace the seam with the session-scoped owner (D-02/D-05), reachable from the session object every
  view already resolves. The owner is non-null for as long as a session is open, and consumers never
  null-check a diagnostics model that exists for that session.
- Effort: M (once D-05 exists) — Related: D-01, D-02, D-14

### D-05 · SessionDiagnosticsModel mixes all three diagnostic levels, view state and view-models in one 6.6k-line class
- Category: 6 Size/cohesion, 3 Layering — Severity: **High**
- Evidence:
  - `session_diagnostics_model.cpp` is 6,575 lines and the `.h` is 1,039.
  - Functions over 150 lines: `conditionDetail` `cpp:6060` (461), `detectShot` `:945` (345), `spreadFor` `:3672` (314), `ensureOutcomes` `:1788` (295), `historyFor` `:4405` (224), `activateSession` `:1403` (221), `cardMap` `:5054` (207), `shotReadout` `:5800` (188), `buildCards` `:4875` (174).
  - The responsibility table in §2 covers Shot, Session and Athlete facts, view state and view-models.
  - The header says `It is a JOIN and an ORCHESTRATOR, and deliberately not a second place where diagnostics arithmetic lives` (`h:53-54`). Yet the reasons trend is computed here: the last third against the first two, with a ±0.15 threshold (`cpp:1911-1932` — `if (lateShare - earlyShare <= -0.15)`). So is the reason tally, and so is the 3-session prevalence trend for the profile (`cpp:3219-3229`).
  - The open detail is view state stored in the model (`h:961-966`, `m_detailConditionId`). Selection changes trigger a full re-reduction (`cpp:767-773` — `setSelectedShotId … rebuild()`).
- What goes wrong:
  - A change to any level touches this file and its whole re-reduction.
  - Session facts such as the reasons and their trend exist only as QVariantMaps, so the report and the
    work-ons cannot read them as data (they read `shotRecords()` and `ledgerRows()` and re-derive).
  - The view state's lifetime is tied to the reduction's. That is why the panel needs a save/restore protocol
    (D-14).
- Fix (the split):
  1. **`detectShot` → a pure function in `src/Diagnostics`**: `ShotRecord diagnoseSwing(swingDir, pack, norms, policy, baseline)`. It is shared with `tools/diagnostics/regrade_ledger.cpp`, has no QObject, and its inputs are passed in — that removes the races in D-12.
  2. **`SessionDiagnostics`** (Coaching-side data plus a thin Qt owner per open session) holds:
     - rows and golfer intent;
     - the single worker queue;
     - one `DiagnosticsFile` reader/writer (D-08);
     - the reduction: ledgers, links, rails, explanation, composed outcomes, factors, reasons, mixes, carry today, as typed structs, with change signals.
  3. **`AthleteDiagnostics`** (the data half of `WorkOnsController`) owns:
     - club baselines (the freeze);
     - the fault profile and tuning log;
     - the readings across sessions;
     - tendencies, work-ons and marks.

     The session level receives what it needs (`usualFor`, profile bias) as inputs.
  4. **View-models per view**, cheap and free of I/O: a panel presenter that projects cards, detail, readout and pips from (SessionDiagnostics, selection, open detail, cadence). The pips, Charts and the report read the owner directly.
- Effort: L — Related: D-02, D-06, D-10, D-13

### D-06 · Athlete-level facts are computed by the session view-model, per activation, by whichever instance holds authority
- Category: 4 One source, 3 Layering (domain I/O in Gui) — Severity: **Medium**
- Evidence:
  - The club baseline is frozen from "the newest ten" predecessor ledgers, read synchronously in `activateSession` on the GUI thread: `cpp:2528-2572` (`QFile f(sessionFolder.filePath(kDiagnosticsFile)); … fromJson(ledgerObj)`).
  - The "live" authority returns without freezing when any predecessor is not current (`cpp:2573`), so it re-reads the same ledgers on the next activation, which is the next mode flip.
  - The fault profile lives in the athlete folder and is read and written here (`cpp:3034-3043`, `3140-3245`).
  - The tuning log is athlete-scoped (`cpp:3025-3032`).
  - History across sessions: `readHistory` reads every sibling's ledger (`cpp:4319-4337`), and this runs in all four instances, including the catch-up's model for each session it derives (`cpp:1616` is unconditional). That is N×(N−1) ledger reads per full catch-up, for a model nobody draws.
  - The athlete level reads the same ledgers again, three more times (`work_ons_controller.cpp:1396` marks, `:1670` themes, `:184-198` tendencies).
- What goes wrong:
  - A golfer's "usual" numbers and history are recomputed in up to four places, on their own schedules.
  - In a live session that cannot freeze (a never-opened predecessor with swings), every Capture↔Replay flip
    re-reads up to ten ledgers on the GUI thread, and every row stays `|provisional`. That is **suspected**:
    the stall is not measured.
  - The catch-up's history scans compete with its own derivations on disk (a network share in studios).
- Fix: `AthleteDiagnostics` (D-05) owns the baselines and history and hands a session owner its frozen
  baseline and profile bias. Readings across sessions are kept incrementally on the athlete owner (it already
  reads every ledger for the marks), and the detail asks it.
- Effort: M — Related: D-05, D-07, D-15

### D-07 · The fault-profile write path is dead: `closeSession()` has no caller in the app
- Category: 14 Dead code, 9 Persistence (design drift) — Severity: **Medium**
- Evidence:
  - `git grep closeSession` at `37289e33` finds calls only in tests (`src/Diagnostics/tests/session_diagnostics_model_test.cpp:2414…`) and a tool comment (`tools/diagnostics/regrade_ledger.cpp:17`).
  - No QML or C++ caller exists for `SessionDiagnosticsModel::closeSession` (`cpp:1626-1642`), the only caller of `updateProfile()` (`cpp:3195`).
  - `h:104-105` — `fault_profile.json … is read at activation and written at close.`
  - `docs/design/session_diagnostics_design.md:640` — `per-athlete fault_profile.json; updated at close`.
- What goes wrong: no app path writes `fault_profile.json`. Cold-state `expectations`
  (`PpDiagReplayCold.qml`, `PpExpectations.qml`) and the `profileBias` tiebreak (`cpp:2992`) read a file that
  only predates the four-modes refactor, or never exists. The feature is inert, and nothing says so. Because
  no session is ever "closed", `closed` and the Closing bookends appear only through `reviewing`.
- Fix: decide with Mark. Either wire the close into the session owner's End (D-02 makes that one call), or
  retire the profile, the expectations block and `rankScores`' profile term together.
- Effort: S — Related: D-02, D-06

### D-08 · The diagnostics.json envelope has no single reader/writer type; a domain invariant lives only in this Gui class
- Category: 9 Persistence, 3 Layering, 5 Duplication — Severity: **Medium**
- Evidence:
  - Parse sites of the same envelope, each by string key:
    - inside this model: `cpp:1463-1530` (activate), `cpp:3116-3131` (catch-up merge), `cpp:2561-2570` (predecessors), `cpp:4337-4341` (history);
    - elsewhere: `work_ons_controller.cpp:156-166, 196, 1396, 1670`, `session_series_model.cpp:796`, `charts_posture_presenter.cpp:709-714`, `athlete_controller.cpp:743`.
  - File-name constants are duplicated in anonymous namespaces: `cpp:87` `kDiagnosticsFile`, `work_ons_controller.cpp:70` `kLedgerFile`, `session_series_model.cpp:56` `kLedgerFile`; `kBaselineFile` at `cpp:89` and `work_ons_controller.cpp:71`.
  - `kEnvelopeVersion` is written (`cpp:3085`) and never read on load. It is also reused as the version of a different file, `fault_profile.json` (`cpp:3240`).
  - The flight check (`checkStrikesAgainstFlight`) is applied only here (`cpp:1773`). Coaching readers rely on the rows on disk already being checked: `club_distances.h:33` (`read off the ledger's CHECKED rows`), `outcome_scope.h:59`, `work_ons.h:90`.
- What goes wrong: a new envelope field, or a migration, has to be made in up to ten places, and the version
  stamp cannot drive a migration because nobody reads it. A ledger that reaches the Coaching readers without
  passing through this model (a tool, an older file, a copy from another host before the catch-up regrades
  it) is read unchecked, and nothing on that path can know.
- Fix: one `DiagnosticsFile` type in Coaching (`load(dir) → {rows checked against flight, intent, corrections,
  meta}`, `save`) that applies the flight check on load and owns the version. Every reader goes through it.
  Delete the duplicated constants.
- Effort: M — Related: D-05, D-17

### D-09 · `session_spread.h` is domain arithmetic filed under Gui and included upward by Coaching
- Category: 3 Layering — Severity: **Medium**
- Evidence:
  - `src/Coaching/session_report.h:61` — `#include "../Gui/diagnostics/session_spread.h"`.
  - `session_spread.h:21-24` includes `../typeset/box_stats.h` (Gui/typeset). Coaching therefore depends on two Gui folders.
  - `data/include_graph.md:9` lists it as the Coaching → Gui edge.
  - The file's own description: `PURE AND HEADER-ONLY … the axis, the bands, the stacking and the clipping are the only things about these pictures that CAN be tested` (`session_spread.h:40-43`).
- What goes wrong: Coaching cannot be built or tested without the Gui tree, and the corridor rules (bands,
  `spreadPositionWords`) look like a Gui concern when they are the house's grading geometry.
- Fix: move `session_spread.h`, and the box statistics it needs, to Coaching (or Diagnostics). Gui includes
  them downward.
- Effort: S — Related: D-11

### D-10 · 43 QVariant contracts are defined only in comments; the C++ report consumes the view-model, and QML parses numbers back out of sentences
- Category: 7 Contracts — Severity: **Medium**
- Evidence:
  - 18 `Q_PROPERTY(QVariant…)` and 11 `Q_INVOKABLE QVariant…` on the model, and 10 and 4 on the controller. No `Q_GADGET` exists in either header; the house has the pattern in `variability_facade.h:61-71`.
  - Shapes are given as prose, e.g. `h:526-546` (`spreadOf`, 20 keys) and `h:432-440` (`shotOutcome`).
  - `session_report_model.cpp:695` — `r.spread = x.model->spreadOf(id);` — C++ reads the QML view-model map.
  - `PpDiagRoles.qml:136-139` — `const m = String(rec || "").match(/(\d+)\s+of\s+(\d+)/)`, and `:154` `.replace(" measurable shots", "")`. The ordering of Watching is sorted on that parsed rate (`PpDiagReplay.qml:108`, `PpDiagAnalyse.qml:91` — `ro.rate(b) - ro.rate(a)`).
  - The body duck-types its source: `typeof source.declareFocus === "function"` (`PpSessionDiagnosticsBody.qml:139`).
- What goes wrong: renaming a key, or rewording "8 of 12 measurable shots", fails silently at runtime. The
  regex fallback reads 0 and the Watching order changes, with no compile or test signal unless a QML test
  happens to cover it. The model has become the report's data API by accident.
- Fix: typed gadgets for the shapes read in more than one place (spread, card, outcome, readout). Publish
  counts as numbers beside their sentence (`recurrenceFired`/`recurrenceOf` already exist on rows; make them
  universal and remove the regex path). Give the report typed accessors on the session owner (D-05).
- Effort: M — Related: D-05, D-11

### D-11 · QML re-derives facts the model publishes, including the pattern C21 retired
- Category: 4 One source, 5 Duplication, 3 Domain rules in QML — Severity: **Medium**
- Evidence:
  - `h:612-614` — `the QML used to rebuild both from the band words ("good|watch" is a pass edge) … and got a one-sided corridor wrong`.
  - `PpDiagAnalyse.qml:137-158` still rebuilds the pass edge and the nearest fault line from band words (`b.grade === "ideal" || b.grade === "good" || b.grade === "open"`). It applies its own "rounds to nothing is at the fault line" rule, which its comment says mirrors `session_spread.h spreadPositionWords` (`session_spread.h:457-478`).
  - `PpDiagRoles.qml:229-247` counts fired, clean, not-assessable and pattern conditions and writes the standfirst sentences in QML. That contradicts `h:62-63`: `no wording in the QML that this file could have written`.
  - `PpQuestions.qml:65-68` decides the session's "thirds" for the getting-worse question.
- What goes wrong: two implementations of "how far past the fault line" (C++ for the hover readout, QML for
  the key figures) can name different distances on one-sided corridors, which is the exact bug class C21 fixed.
  Wording moves between QML and C++ with no rule about which owns it.
- Fix: publish the travel figures (`passDistance`, `faultDistance`, `atFaultLine`) on the spread map from
  `spreadPositionWords`' arithmetic, and the standfirst counts or sentences from the model. Then delete the QML
  copies. No design-system change is involved: the same components draw the same strings.
- Effort: S–M — Related: D-09, D-10

### D-12 · The worker reads members the GUI thread can write, and a header comment says it does not
- Category: 8 State/concurrency, 13 Comments — Severity: **Medium** (**suspected** data race)
- Evidence:
  - `h:661-663` — `Worker-thread half … Touches no member except the pack and the norm provider`.
  - `detectShot` also reads `m_policyName` (`cpp:1009` `gradePolicyByName(m_policyName)`), `m_withSigma` (`:975`), `m_clubBaseline` and `m_baselineClubs` (`:979-982`, `:1244` `usualFor`), and `m_baselineFrozen`/`m_contentStamp` (`:955` `rowContentStamp()`).
  - `setGradePolicy` (`cpp:747-757`) assigns `m_policyName` with no wait. It is bound to `appSettings.diagnosticsGradePolicy` (`PpSessionDiagnosticsPanel.qml:98`).
  - The `mutable` outcome caches (`h:1002-1012`) are filled from `const` getters (`shotLog()` etc., `h:331-341`). They are GUI-thread only, which is safe today but only by convention.
  - Signal-order contracts: `busyChanged` is emitted before the row is placed and written (`cpp:1295-1300`). `WorkOnsController` connects queued for that reason (`work_ons_controller.cpp:365-368`), and `SessionSeriesModel` posts its refresh for the same reason (`session_series_model.cpp:512-516`).
- What goes wrong:
  - **Suspected:** the grade policy is changed in Settings while a shot is being detected. A `QString` is
    assigned on the GUI thread while the worker reads it, which is undefined behaviour (rare, possibly a
    crash).
  - The false comment invites the next edit to read another mutable member on the worker.
  - Any consumer of `busy` that connects directly reads one shot short.
- Fix: snapshot the worker's inputs into the queued lambda (policy, sigma flag, baseline `shared_ptr`, stamp),
  and make `detectShot` a free function of them (D-05 step 1). Emit a dedicated `settled()` after the
  persist, instead of overloading `busyChanged`.
- Effort: S — Related: D-05

### D-13 · `rebuild()` is both "reduce" and "present"; a selection click re-reduces the session and every card re-pulls its pips
- Category: 6 Cohesion, 4 One source — Severity: **Medium**
- Evidence:
  - `cpp:767-773`, `:759-765` and `:734-745` — `setSelectedShotId`, `setReviewing` and `setCadence` each call `rebuild()`. `rebuild()` (`cpp:2589-2653`) runs `conditionLedgers`, `gradeLinks`, `extractChains` and every builder, plus `conditionDetail` when a detail is open.
  - Links are graded three times per rebuild over different node sets: `m_links` (`cpp:2602`), `m_outcomeLinks` in `ensureOutcomes` (`cpp:1819`), and the detail's own (`cpp:6097`).
  - Every `surfaceChanged` makes every carousel card re-run `shotIdForSwingDir` (an O(n) scan with `QDir::cleanPath` per entry, `cpp:792-811`) and `pipsFor` (`PpShotCard.qml:296-305`).
- What goes wrong: a carousel click on a 50-shot session repeats the whole reduction, then n O(n) id
  resolutions, then n pip arrays. Answering "how is link A→B graded" in three places relies on `gradeLinks`
  being local per edge, which is true today but is not stated as a contract.
- Fix: split `reduce()` (rows or intent changed) from `project()` (selection, cadence, review, detail
  changed). Grade the links once over the union neighbourhood and look them up. Publish a `swingDir → id` map,
  or carry the ledger id on the carousel row.
- Effort: M — Related: D-05, D-16

### D-14 · The ⚠ comments in PpSessionDiagnosticsPanel.qml, and the structural fault each one patches
- Category: 13 Comments as a symptom — Severity: **Medium**
- Evidence and diagnosis:

  | Comment | What it patches | Structural fault |
  |---|---|---|
  | `:164-200` "IN A LIVE SESSION THE FOCUS BELONGS TO SessionController … each model ADOPTS the controller's focus"; `:201-207` deferred by `Qt.callLater` "on a half-written pair" | the focus has two owners (SessionController and each model's file-backed copy); a new model comes up with an empty intent and writes it over the focus | session intent is not owned by a session object; it is copied into per-view models, and the writer is a pair of non-atomic properties |
  | `:217-224` "⚠ FETCHED HERE AS WELL AS FROM THE SIGNALS … The selectedShotIdChanged it emits is lost" | `shotReadout` is a pull invokable; the model resolves the pick synchronously inside `Component.onCompleted`, before the Connections exist | the selected shot is not an object; it is copied from `SessionMode` into each model, and the view acts as the cache invalidator |
  | `:225-243` "CLAIMED UNCONDITIONALLY BY THE MOST RECENT PANEL … released only if it is still OURS" | consumers need *a* model and there can be several | no owner for the session diagnostics (D-04) |
  | `:245-304` "THE PANEL DOES NOT SURVIVE A MODE SWITCH … SAVED AS IT CHANGES, NOT ON TEARDOWN: the stage builds the incoming panel BEFORE it destroys the outgoing one" | tab, filter, open detail and scroll are lost on every flip; the save on teardown lands after the restore | view state (and the open detail, in the model) has the lifetime of a view that is rebuilt per flip; the place is kept in a global (`SessionMode.diagnosticsNav`) stamped by folder |
  | `:127-139` "LIVE IS TODAY'S SESSION WHETHER OR NOT IT IS RUNNING" and `:149-152` "COMPARED AS FOLDERS … spell the same folder differently" | the panel works out "which session" from three folder strings | no session identity (01 §1) |
  | `:155-158` authority set "by hand before the load reads it (a binding's order isn't pinned)" | the role flag has to be set before activation | one class in four roles (D-03) |
  | `session_diagnostics_model.h:154-159` "three numbering schemes that only coincide by luck" and `cpp:643-649` (the live id that filed shot "91" beside folders 1–30) | ids disagree between the carousel and the ledger | the shot has no identity beyond its folder (D-16) |
- Fix: D-02, D-04 and D-05. Each comment disappears once the session and the selected shot are objects with
  owners.
- Effort: — (follows the others) — Related: D-02, D-03, D-04, D-16

### D-15 · WorkOnsController: the athlete switch is handled carefully, but an in-flight derivation is neither cancelled nor paused, and the catch-up overlaps open sessions
- Category: 1 Ownership, 8 Concurrency — Severity: **Medium**
- Evidence:
  - Sound part: `setAthleteDir` (`work_ons_controller.cpp:373-409`) clears records, queue, scope, filter, summary, tendencies and marks, and cancels jobs. Every job result is guarded by a generation (`:1591-1594`, `:1765`, `:1449`, `:2103`). `finishDerive` writes but does not adopt an old athlete's record (`:601-603`, `:626-629`).
  - Gaps:
    - `setPaused(true)` cancels themes, tendencies and marks (`:425-429`) but not the derivation in flight. `deriveNext` checks the pause only before the next one (`:562`), so a full regrade of an old session (dozens of whole-document parses on the model's worker) keeps running beside capture. That contradicts `h:70-71`: `never while a session is live`.
    - The private model keeps the previous session's rows until the queue drains (`:579-580`).
    - The pause is "a session is running" (`main.cpp:383-385`), so the catch-up also runs while the golfer reviews a session, and right after End on the session the stage panel still holds (D-03).
    - `main.cpp:366-375` is one of three copies of the athlete-folder lambda (`:404-411`, `:415-419`).
- What goes wrong: capture competes with a regrade started seconds before Start (**suspected**: a CPU and
  disk spike at the first shot of a session that follows a pack edit). Panel and catch-up both hold one
  session (D-03).
- Fix: cancel the in-flight derivation on pause (`activateSession(QString())` drops the queued work, and the
  generation drops the deliveries). Derive through the session owner when one is open for that folder. Make
  `athleteDir` come from an Athlete object, not from a recomputed string (01 §1).
- Effort: S (the pause) · M (the overlap) — Related: D-03, D-06

### D-16 · Identity: `ingestShot(shotId, …)` ignores its id, and the stale comment still describes three numberings
- Category: 2 Identity — Severity: **Low**
- Evidence:
  - `cpp:837-839` — `The folder's number, whatever the caller numbered it … if (const int fromFolder = folderShotId(swingDir); fromFolder >= 0) shotId = fromFolder;`.
  - `regradeShot` parses the folder number a second way (`cpp:858-861`), as does `activateSession` (`cpp:1586-1587`) and `shotIdForSwingDir` (`cpp:805-808`). That is four parsers of `swing_NNNN`.
  - `h:154-159` still says `this model's are the swing_NNN folder number (back-fill) or the processor's id (live)`.
  - A legacy-row migration runs on every load (`cpp:1494-1509`).
- What goes wrong: callers believe the id they pass matters, and readers of the header are told the opposite
  of what the code does. Four parsers can drift, for example on a folder such as `swing_0007_b`.
- Fix: `ingestShot(swingDir)`, with one `folderShotId` used everywhere. Fix the comment. Retire the migration
  once every library has been reconciled; the catch-up has done that by now.
- Effort: S — Related: D-13, D-14

### D-17 · Rules and serialisers duplicated between the model, WorkOnsController and Coaching
- Category: 5 Duplication — Severity: **Low**
- Evidence:
  - Shot intent is resolved twice: `SessionDiagnosticsModel::intentFor` (`cpp:1747-1752`) and `resolvedShotIntents` (`tendencies.h:190-198`). They agree today.
  - `TendencyOnBall` is marshalled twice with the same keys: to QVariantMap in `WorkOnsController::onBallFor` (`work_ons_controller.cpp:2283-2303`) and to JSON in `tendencies.h:1296-1310`. This is the `data/cpp_duplicates.md:66` clone. The model already uses the one-serialiser pattern (`shotMixBarToJson(…).toVariantMap()`, `cpp:2078`).
  - Each instance builds its own pack provider: four models, plus `WorkOnsController::m_packProv` (`:693`), plus `main.cpp`'s goal resolver, and two `ShotOutcomePack` marshallings (`cpp:712`, `work_ons_controller.cpp:695`).
- What goes wrong: a change to the intent rule, or to an on-ball key, has to be made twice. A user pack edited
  between two instances' construction gives two panels different packs.
- Fix: the model calls `resolvedShotIntents`; `onBallFor` returns `onBallToJson(b).toVariantMap()`; one shared
  immutable pack per app (reloaded explicitly).
- Effort: S — Related: D-08

### D-18 · Comments that now say the opposite of the code
- Category: 13 Comments — Severity: **Low**
- Evidence:
  - `session_report_model.cpp:509-513` — `There is no read-only switch on SessionDiagnosticsModel … that reconcile persists`. The same object calls `setPersistEnabled(false)` at `:308`; `session_report_model.h:48-51` says the same as `:509-513`.
  - `PpSessionDiagnosticsBody.qml:166-167` — `The model holds which detail is open (so it survives a stage rebuild)`. The model does not survive the rebuild; `PpReaderPlace` restores the detail (`PpSessionDiagnosticsPanel.qml:298-302`).
  - `h:124` — `The panel comes in a later phase`.
  - `PpSessionDiagnosticsWindow.qml:31-33` — `both reduce the same rows to the same surface` (false for golfer input and for regrades, D-01/D-04).
  - `work_ons_controller.cpp:464-466` — `the panel rewrites that file for reasons that change no verdict (a focus declared …)`. A declared focus changes link grades (`h:395-397`, `cpp:1816-1819` FocusSplit → `gradeLinks`). Those links are persisted as `outcomeEvidence` in `work_ons.json` (`:623`), so a focus declared in review leaves that evidence stale.
- What goes wrong: in this codebase the comments are the design record, so false ones mislead the next change.
  The last item is a small, real staleness (**suspected**).
- Fix: correct each comment. Include the focus in the work-ons fingerprint, or stop persisting links that
  depend on the focus.
- Effort: S — Related: D-01, D-03

### D-19 · Dead and unwired code
- Category: 14 Dead code — Severity: **Low**
- Evidence:
  - `WristDiagnosticsModel` (518 lines) is registered (`cmake/PinPointQmlModule.cmake:721-722`) but has no QML or C++ instantiation. `docs/design/wrist_motion_into_charts.md:318` — `nothing in the app uses it now, only the dump probe`, and no probe references it either. It still carries `loadDemo` (`wrist_diagnostics_model.h`).
  - `recordScreenResult` and `screens` are persisted (`cpp:3080-3082`) but have no UI (`PpSessionDiagnosticsPanel.qml:79-88` `DELIBERATELY UNCONNECTED`).
  - `closeSession`/`updateProfile`: see D-07.
- Fix: delete `WristDiagnosticsModel`, or name its remaining user. Leave the screens API in place, but say in
  the model header that it is unwired.
- Effort: S — Related: D-07

### D-20 · The cast window draws its own Pin button
- Category: QML rule (hand-drawn control) — Severity: **Low**
- Evidence: `PpSessionDiagnosticsWindow.qml:145-168` — `Rectangle { … radius: Theme.radius; color: win.pinned ? Theme.colorBg3 : "transparent"; border.color: … PpMicro { … } HoverHandler { … } TapHandler { onTapped: win.pinned = !win.pinned } }`. This is a toggle button built from Rectangle, text and a tap handler. The D1 lint misses it because it uses `TapHandler`, not `MouseArea`, and it is not in `design_system_allowlist.txt`.
- What goes wrong: a restyle of buttons does not reach it, and the design-system rule is bypassed by a
  handler type the lint does not know.
- Fix: candidates are `PpButton` with `size: "micro"`, a quiet or ghost `style` and `active: win.pinned`
  (`docs/design/system/components/PpButton.md`). Its "chromeless until hovered" look is not obviously an
  existing style, so whether one fits is **Mark's decision**. If none does, a new variant is a design-system
  change that needs his approval before it is built. Separately, the lint could learn `TapHandler`.
- Effort: S — Related: —

## 4. Done well

- **The arithmetic is delegated to pure, header-only Coaching code.** `rebuild()` (`cpp:2589-2653`) is calls
  into `diagnostic_ledger.h` (`conditionLedgers`, `gradeLinks`, `extractChains`, `sessionStage`,
  `hystereticOrder`). The outcome and factor reductions are `composeShotOutcome`/`shotFactors`
  (`shot_outcomes.h`, `shot_factors.h`), shared with `tendencies.h:558-559`, so the panel and Home compose a
  shot with one function.
- **Rows are persisted, never verdicts** (`h:93-102`), with a staleness stamp taken *before* the read
  (`cpp:949-955`) and a content stamp over pack, norms and schema (`cpp:929-943`). A re-analysed session
  regrades itself.
- **Stale deliveries are dropped by generation**: `m_activationGen` (`h:1029-1033`, `cpp:1304-1307`), and the
  controller's job generations (`work_ons_controller.cpp:1591-1594`, `:1765`, `:2103`). The athlete switch
  clears every athlete-scoped cache (`:373-409`).
- **Batch coalescing** (`h:84-91`, `cpp:1352-1385`) turned a 39 s drain into one reduction. It has a counter
  the test can assert (`persistCount`).
- **Every file is written atomically** (`cpp:3136`, `:2579`, `:3244`; `work_ons_controller.cpp:624`).
- **The fault profile is structurally limited to ranking**: `profileBias` is called only from `rankScores`
  (`cpp:2992`), as `h:104-110` promises. That holds, though the profile is currently never written (D-07).
- **Data-integrity exclusion as NotAssessable** with its reason (`cpp:1247-1285`), which keeps a broken
  recording visible and flagged rather than hidden.
- **Testable without the GUI**: `setSynchronous`, `src/Diagnostics/tests/session_diagnostics_model_test.cpp`
  and `work_ons_controller_test.cpp`. The panel/body split lets the body load offscreen with a fixture
  (`PpSessionDiagnosticsPanel.qml:23-29`).
- **`session_spread.h` is the right kind of code**: pure, header-only and tested. It is only in the wrong
  folder (D-09).

## 5. June review status

`docs/implementation/code_quality_review.md` (2026-06-20) predates this area: `session_diagnostics_model.cpp`
was added on 2026-08-07 (`81314b58`). The June review has no findings here to carry. Its cross-cutting theme A
(hand-written QThread teardown) does not apply: this area uses `QThreadPool`, drained in destructors
(`cpp:716-730`, `work_ons_controller.cpp:307-318`).

## 6. Corrections to 01-information-model.md

1. **Four instances, not three.** `SessionReportModel` owns a fourth private `SessionDiagnosticsModel`
   (`session_report_model.cpp:298-311`; persist off, authority "never"). It is app-lifetime through the
   `sessionReport` global (`main.cpp:395`) and is handed to QML as `printModel` (`PpReportPrintHost.qml:97`).
   §3's "With no panel open and no catch-up running, no session-level model exists" is therefore not quite
   true: a report job holds one while it runs (`:572` releases it).
2. **The seam has five consumers, not four.** Add `PpChartsBody.qml:137-138` (`openDetail` on whichever model
   claimed last). "The carousel's regrade" is `PpShotCarousel.qml:748-755`, triggered by LM late readings
   (`:771`) and by re-analysis (`:791`).
3. **A rebuild costs more than "a disk scan plus back-fill".** It also covers:
   - a fresh pack parse (`cpp:702`);
   - a SHA-1 over the serialised pack and norms (`cpp:929-943`, `:1425`);
   - the profile read;
   - `prepareClubBaseline`, which reads up to ten predecessor ledgers on the GUI thread until the session is
     frozen (`cpp:2528-2573`);
   - an off-thread read of every sibling session's ledger (`cpp:1616`, `:4319-4337`).

   The "window" cast is created anew per shot (`Main.qml:326-327`, `:414-418`), so it pays this on every shot.
4. **"Outcomes and factors: not persisted (recomputed)" is only partly true.**
   - Composed outcomes and factors are not stored.
   - The flight-check verdict is persisted *in the rows* (`overruled`; the check runs before every persist,
     `cpp:1338`, `:1383`).
   - Corrections and intents are persisted in the envelope (`cpp:3089-3100`).
   - The per-condition outcome evidence is persisted in `<session>/work_ons.json`
     (`work_ons_controller.cpp:623`).
   - Outcomes are recomposed in two places: the model (`cpp:1830`) and `tendencies.h:558`, the latter from
     the work_ons.json evidence.
5. **Athlete → session is not `usualFor(s.club)` at `cpp:2153`.** That line formats the carry text in
   `shotOutcome`. The grading input is the frozen club baseline handed to detection (`cpp:979-982`, `:1244`).
   It is computed *by the session model itself* from up to ten predecessor ledgers (`prepareClubBaseline`,
   `cpp:2504-2585`) and stored in the session folder (`club_baseline.json`), by whichever instance has
   authority ("always" or "live"). An athlete-level fact is computed at the session level.
6. **Athlete-level files are written by the session model too.** `fault_profile.json` (`cpp:3034-3043`,
   `:3195-3245`) and `outcome_corrections.jsonl` (`cpp:3025-3032`, `:2375-2386`) are athlete files written by
   `SessionDiagnosticsModel`. `fault_profile.json` is never written in the app, because `closeSession()` has
   no production caller (D-07). "Work-on records" are per session (`<session>/work_ons.json`,
   `work_ons_controller.cpp:320-323`), not per athlete.
7. **The writers of `diagnostics.json` include the reviewing panel.** `persistEnabled` defaults to on, and the
   "never" authority governs only `club_baseline.json` (`cpp:2524-2526`). Before writing, the catch-up re-reads
   three envelope fields (`cpp:3116-3131`); live instances re-read none (D-01).
8. **The ledger shotId is not "resolved from the folder by `shotIdForSwingDir`".** It *is* the swing folder's
   number on every path in (`folderShotId`, `cpp:643-658`, `:839`). `shotIdForSwingDir` exists for paths that
   differ between hosts. The header's three-numberings comment (`h:154-159`) is stale (D-16).
9. **"The athlete is swapped underneath it" is confirmed, with one nuance.** `setAthleteDir` clears every
   athlete-scoped cache and bumps generations (`work_ons_controller.cpp:373-409`). The derivation in flight is
   neither cancelled nor paused, and the private model keeps the previous session until it settles
   (`:595-631`, `:579-580`).
10. **The athlete-folder lambda is not cloned three times inside the athlete-to-WorkOns wiring.** Only one
    copy serves WorkOns (`main.cpp:366-375`). The other two serve the report list (`:404-411`) and
    `setAthleteDirResolver` (`:415-419`). Same fact, but the cited lines are those.

## 7. Questions

- Is `closeSession()` meant to be unwired, with the fault profile retired, or was it lost when the panel moved
  into `PpModeStage`? (D-07)
- With default settings, does the post-shot "window" cast's dwell usually end after the auto-replay returns to
  Capture? That decides whether D-04's null-seam window is common or rare.
- Is `PpModeStage`'s build-before-destroy order guaranteed? The panel's comment (`:253-255`) says it is. If it
  is, the double detection in D-02 is likely on every auto-replayed shot. It needs one probe run to confirm.
- Is the catch-up meant to run while a session is open for review, and in the minutes after End while the
  stage still shows that session? (D-03, D-15)
- `WristDiagnosticsModel`: the design doc names a dump probe as its last user, and no probe at HEAD references
  it. Can it be deleted? (D-19)
