# Reviewer F: `src/Gui/charts` and `src/Gui/report`

Commit `37289e33`. Area: `src/Gui/charts` (112 files, 27.8k C++ + 10.4k QML) and `src/Gui/report` (57 files, 9.5k C++ + 7.3k QML), tests excluded. Read-only review. Runtime defects I have not reproduced are marked **suspected** and come with the scenario that would show them.

## 1. Summary

Inside each file the code is careful. The pure session maths is tested, threading inside one object is sound, missing values are honest, and the QML follows the design system. The structure around it is weak in the same way as the fault the owner found, and in a bigger way:

- **No session owns its chart data.** Charts, the Consistency tab and the Trust views each build their own `SessionSeriesModel` inside a view. The report builds another inline. Only the Charts panel and the Consistency tab feed theirs live shots, and the only signal that the diagnostics changed comes through `SessionMode.sessionDiagnostics`, which is null unless the diagnostics panel is on. The design doc records the copies as a workaround: "because the diagnostics body has none" (F-01).
- **`SessionSnapshot` is a shadow session-diagnostics model.** It re-reads `diagnostics.json` and re-reduces tiers and the work-on order. It re-grades the wrist cells. Its Posture, Wrist, Club delivery and Consistency pages read the ledger file, while the rest of the same report reads a private diagnostics model that re-grades and does not save (F-02).
- **Facts derived twice, by rules that already differ:**
  - the report's ball figures ignore the golfer's LM exclusions and club overrides; its own appendix honours them (F-03);
  - the report grades causal links with its own 0–4 rule, separate from the panel's `LinkGrade` (F-04);
  - Charts names a shot's direction with `lmShapeClass`, while the shot's Direction factor comes from pack corridors (F-05).
- **Identity is spelt many ways:**
  - "earlier" and "session N of the day" follow folder-name order, while the Sessions list follows start time, which its own comment says must agree (F-06);
  - Charts numbers shots by rank, the carousel by document index and the report by its own index (F-07).
- **Copy-paste:**
  - six presenters clone one worker and scheduling scaffold, and the copies already differ (F-08);
  - five threading idioms, including the destructor wait that #173 removed from the presenters but left in the model (F-09);
  - a 1,011-line `build()` and two 400-line builders (F-10);
  - about 5,000 lines of domain rules filed under `Gui/charts` (F-11).

## 2. How the area is put together

- **Loader.** `SessionSeriesModel` (QObject, `QML_ELEMENT`) reads every `swing_*` through `SwingStore::loadAnalysis` on one private worker thread. It joins each swing to the session's `diagnostics.json` and publishes one immutable `SessionSnapshot` holding `ShotSeries` (metrics, phases, kinematic sequence, gates, ledger row, wrist profile), condition links, tiers and the work-on order. A process-wide shot cache, keyed by the last two activated sessions, makes rebuilds cheap. There is no app-level owner: every view that wants session series creates one (`PpChartsPanel.qml:102`, `PpConsistencyView.qml:128`, `PpTrustPanel.qml:48`, `PpTrustSummaryFold.qml:71`, `PpTrustInlineFor.qml:50`, and `readChartsPages`, `session_report_model.cpp:132`).
- **Presenters.** These are `ChartsTraces`, `Relations`, `Wrist`, `Posture`, `Delivery` and `Consistency` (all QObject, `QML_ELEMENT`). Each takes `model:` (typed `QObject*`) plus the reader's choices. It runs one job on the shared `PresenterPool`, turning snapshot and inputs into `QVariantMap` payloads that QML places verbatim. `PpChartsPanel` creates five of them, and `PpConsistencyView` creates a Relations and a Consistency presenter. The domain rules live in pure files beside them (`session_traces`, `session_warp`, `session_relations`, `session_consistency`, `consistency_grades`, `session_posture`, `session_delivery`, `session_wrist`, `session_sequence`, `variability_clubs`).
- **Inputs from the app.** The session folder is chosen in each QML view by the same review-or-live expression. `shotProcessor.shotProcessed` → `ingestShot`. `reanalysisController.reanalysed` → `regradeShot`. `SessionMode.swingTrashed` → `forgetShot`. `SessionMode.sessionDiagnostics.busyChanged` → `refreshDiagnostics`. The picked shot comes from `shotReplay`.
- **Report.** `SessionReportModel` is an app-lifetime context property (`main.cpp:1285`). Per make it:
  1. owns a private `SessionDiagnosticsModel`, put in reviewing mode with persist off;
  2. reads small blocks of each swing document on a worker;
  3. on a second worker, builds a private synchronous `SessionSeriesModel` and calls the presenters' static `*SessionReportPayload` functions;
  4. reads the same-day siblings' `work_ons.json` and the athlete's tendencies;
  5. builds an inline `ShotListModel` and `LmSessionModel` for the LM appendix;
  6. assembles one `QVariantMap` (`build()`, 1,011 lines);
  7. has `ReportStills` render frames on a static pool and hands the map to `PpReportPrintHost`.

  The page blocks (`report_posture.h`, `report_wrist.h`, `report_delivery.h`, `report_consistency.h`) cut presenter payloads and renumber shots. `SessionReportListModel` lists sessions with report state. Mail is `ReportMailer`, `SmtpClient` and the platform credential stores.

## 3. Findings

### F-01 · Session chart data has no owner: one `SessionSeriesModel` per view, fed differently
- Category: 1 Ownership and lifetime    - Severity: High
- Evidence:
  - `src/Gui/charts/session_series_model.h:51` — `// INSTANTIATED BY THE PANEL, not registered in main.cpp — SessionDiagnosticsModel's decision for`
  - Instantiation sites: `PpChartsPanel.qml:102`, `diagnostics/PpConsistencyView.qml:128`, `trust/PpTrustPanel.qml:48`, `trust/PpTrustSummaryFold.qml:71` (itself inside Charts' `PpChartsSessionView.qml:136` and `PpChartsShotView.qml:274`), `trust/PpTrustInlineFor.qml:50` (inside 6 hosts), and `report/session_report_model.cpp:132` `pinpoint::charts::SessionSeriesModel series;`
  - Only `PpChartsPanel.qml:208-220` and `PpConsistencyView.qml:130-142` wire `ingestShot`, `regradeShot` and `forgetShot`. `PpTrustPanel.qml`, `PpTrustSummaryFold.qml` and `PpTrustInlineFor.qml` activate once and are never fed: `Component.onCompleted: activateSession(tf._sessionOf(tf.swingDir))` (`PpTrustInlineFor.qml:50`).
  - The diagnostics signal comes only through a view-owned object: `PpChartsPanel.qml:223` `target: SessionMode.sessionDiagnostics`. `SessionMode.qml:54` has `property var sessionDiagnostics: null`, which is set only by `PpSessionDiagnosticsPanel.qml:230`.
  - The session-folder rule is copied verbatim into four views: `PpChartsPanel.qml:95-98`, `PpConsistencyView.qml:108-111`, `PpTrustPanel.qml:40-43`, `PpSessionDiagnosticsPanel.qml:135-138`.
  - The design doc records the copy as a workaround: `docs/design/consistency_design.md:519` — "The tab owns a `SessionSeriesModel`, as `PpTrustPanel.qml` does, because the diagnostics body has none."
  - Each instance parses the catalogue on its own worker: `session_series_model.cpp:599` `if (!m_pack) m_pack = pinpoint::analysis::makeCharacteristicPackProvider();`
- What goes wrong:
  - A reviewed session with Charts, Session diagnostics (Analyse) and the Trust panel open holds at least four snapshots, four pack parses and four ledger parses of one session. They agree only by coincidence of timing.
  - **Suspected:** in a live session with the Charts Session view open, hit a shot. Charts' own model ingests it (N+1 shots), but the trust fold under the figure (`PpChartsSessionView.qml:136`, its own model) still holds N. One block mixes `sv.se.trustLines` from the first snapshot with `lines` from the second. The Trust panel's kicker "Shot %1 of %2" (`trust_presenter.cpp:85`) keeps the old total, and the new shot falls back to the detail map with no ordinal.
  - **Suspected:** with Session diagnostics off, nobody calls `refreshDiagnostics`, and nobody writes `diagnostics.json` until the work-ons catch-up. The Charts Wrist and Posture findings, which are "read off the model's snapshot" (`PpChartsPanel.qml:164-166`), carry no ledger rows for this session's new shots.
  - `PpChartsBody.qml:137` also hides "open in diagnostics" unless the panel is up.
  - The report's inline model pushes its session into the shared cache's two-session `recent` list (`session_series_model.cpp:620-622`) and evicts the Charts panel's sessions, so the panel's next rebuild re-reads everything.
- Fix:
  - One `SessionSeriesModel` (or the snapshot as part of a session-level owner) per open session, created and destroyed with the reviewed or live session in C++, beside the session's diagnostics owner, and exposed read-only.
  - It is fed once, from C++, by `shotProcessed`, `reanalysed` and `swingTrashed`, and by a "ledger changed" signal from the diagnostics owner. That signal must not depend on a panel being on.
  - Views take `model: session.series` and never create one. The report and other off-screen readers get a snapshot from the same owner, or build one through a factory that does not touch the shared cache's eviction list.
  - The session-folder rule becomes one property on that owner.
- Effort: L        - Related: F-02, F-09; the owner's SessionDiagnosticsModel fault; other reviewers' diagnostics findings

### F-02 · `SessionSnapshot` is a second session-diagnostics model, and the report reads both
- Category: 4 One source per derived fact    - Severity: High
- Evidence:
  - `session_series_model.cpp:56` `const QString kLedgerFile = QStringLiteral("diagnostics.json");`
  - Session tiers are re-reduced, at `:801-805`: `conditionLedgers(shots, opt)` → `m_ledgerTiers.insert(l.id, l.tier == Tier::Pattern ? QStringLiteral("pattern") …`
  - The work-on order is re-reduced, at `:810-811`: `sessionWorkOnsForPack(m_pack ? &m_pack->pack() : nullptr, shots, ledgers, wopt)`. The panel's copy is `session_diagnostics_model.cpp:3254` `sessionWorkOnsForPack(… m_shots, m_ledgers)`.
  - `session_series_types.h:183-197`: `tiers` and `workOnOrder` were "ADDED 15 Oct" and "ADDED 22 Oct" so that "the Wrist tab says what Session diagnostics says".
  - Wrist cells are graded a second time: `session_wrist.h:33-34` "so this profile is the one WristDiagnosticsModel draws for the same swing". This happens at `session_series_model.cpp:349` `shot->wrist = shotWristPayload(*shot, -1, …)`, and again per job in `charts_wrist_presenter.cpp:1858` `regradeWristPayload(*s, s->wrist, in.archetype)`.
  - The report's two ledgers:
    - The private model never saves: `session_report_model.cpp:308` `m_model->setPersistEnabled(false);`
    - Its four Charts pages come from `readChartsPages` (`:129-142`), whose `SessionSeriesModel` reads the ledger **file**.
    - Everything else (cover, grid, work-on pages, order) reads `m->shotRecords()` and `m->ledgerRows()` (`:1177-1179`), the private model's re-graded rows.
- What goes wrong:
  - The session's tiers, work-on order and wrist grades are computed in at least two places from two inputs: in-memory rows and the file.
  - **Suspected, in one PDF:** re-analyse a swing with Session diagnostics off, so `diagnostics.json` keeps the old row, then make the report. The private model re-grades the swing, but the file stays stale because persist is off. The work-on pages and grid show the new grade, while the Posture, Wrist and Consistency pages (tiers, `workOnOrder`, fired rows) show the old one.
  - The `.h` comment says the reconcile write "cannot be prevented" (`session_report_model.h:48-51`), and the `.cpp` repeats it (`:509-513`). Both contradict `:306-308`, which turns persisting off. The comment and the behaviour have drifted.
- Fix:
  - The snapshot carries the series. Every diagnostic fact (rows, tiers, work-on order, wrist cell grades) is read from the session's diagnostics owner, joined by shot id at read time or published by that owner as an immutable value.
  - Delete `readLedgerIfChanged`'s reductions, and grade wrist cells in one place (the diagnostics wrist domain).
  - The report hands its own session-diagnostics result to the page payload functions instead of letting a second loader read the file.
- Effort: L        - Related: F-01, F-04, F-11

### F-03 · Report ball figures ignore the golfer's LM exclusions and club overrides; the appendix in the same PDF honours them
- Category: 4 One source per derived fact    - Severity: High (suspected wrong data on the PDF)
- Evidence:
  - `readDocs` takes only `launchMonitor`, `athlete`, `clock` and `streams` (`session_report_model.cpp:153-166`), never `review`.
  - The exclusion and override live in `review` (`Export/swing_doc.cpp:1893-1897` `review[QStringLiteral("excluded")] = true; … review[QStringLiteral("clubOverride")] = ov;`).
  - `lmSeries` (`session_report_model.cpp:857-866`) takes every finite value: `const double d = lmValue(s.lm, key); if (std::isfinite(d)) v.push_back(d);`. It groups by `s.club = (*x.shots)[size_t(s.idx)].club` (`:1192`).
  - The reference swing uses the raw carry: `:1416` `for (const Ctx::Swing &s : x.swings) carry.push_back(lmValue(s.lm, "carryDistance"));`
  - The appendix uses the panel's model "so the figures… agree with the panel: the same effective club per shot, the same shots left out" (`:980-983`) via `LmSessionModel` (`:1923-1931`), which drops `excluded` rows (`launchmonitor/lm_session_model.cpp:548` `if (r->excluded || r->values.isEmpty()) continue;`).
  - The report keeps its own parallel LM column table (`:839-855`, heads, units and decimals beside `lm::fieldDefs()`) and a device sign assumption (`:893` `// GC Quad's path is + to the right of the target line`).
- What goes wrong: **suspected.** The golfer leaves a mis-read shot out on the LM board (or re-labels its club), then makes the report. The cover's ball tiles, the numbers table's "Typical" row, the swing grid's carry row and the choice of reference swing all include the excluded shot under its ledger club. The appendix's per-club statistics exclude it under the overridden club, so one PDF shows two medians for the same club.
- Fix: the report reads LM figures only through the LM session owner (`LmSessionModel` or its pure core), for the cover, numbers, grid and reference swing as well as the appendix. It never keeps a second column table.
- Effort: M        - Related: F-02, F-10

### F-04 · The report grades causal links with its own rule, beside the panel's `LinkGrade`
- Category: 4 One source per derived fact / 5 Duplication    - Severity: High
- Evidence:
  - `session_report_model.cpp:1650-1655`:
    ```
    if (ed.sharedEvidence)                grade = 4;   // the panel's cap: measured from one reading, untested
    else if (to->causedBy.contains(ed.from)) { grade = 0; n = t.c + t.d; gone = t.d; }
    else if (lf->rangeRestricted)      grade = 2;
    else if (linkTestable(*lf, t))      grade = 1;
    ```
  - Words for codes 0–4 are in `report_words.h:632-651`.
  - The panel grades the same edges through `gradeLink()` into `LinkGrade::MovedTogether / ConditionallyDependent / Coherent / PresentTogether / Unanchored` (`session_diagnostics_model.cpp:2852-2868`, `:5562-5600`). That uses a Fisher test (`"Fisher exact · %1 paired shots"`) and `minPairsForDependence`.
- What goes wrong: the same session's link between two listed habits is graded twice by different tests. **Suspected:** a pair the panel shows as "Coherent · tested · not dependent" can read in the PDF as "went away too … so it waits behind it" (code 0, from the work-on root-cause test), or the reverse. The comment "the panel's cap" shows the report is trying to mirror the panel by hand.
- Fix: the session's link evidence (grade, counts, flags) is a session-level fact owned by the diagnostics session object. The report maps its `LinkGrade` onto paper words and has no grading of its own.
- Effort: M        - Related: F-02

### F-05 · A shot's direction is named by two rules: Charts' LM classifier and the shot's Direction factor
- Category: 4 One source per derived fact    - Severity: High
- Evidence:
  - `charts/session_delivery.cpp:709-711` `pa::lmShapeClass(s.launchDir, s.spinAxis, none, …)` (fixed thresholds: `LaunchMonitor/lm_inferred_reads.h:62-66` `startT = 1.0`, `curveT = 2.0`, `severeAxisT = 10.0`).
  - The shot's Direction factor (`Coaching/shot_factors.h:25-29`) is composed from pack conditions graded against norm corridors: `Resources/diagnostics/core.json:5241` `"id": "sig_push", "test": "outsideCorridor", "measures": ["m_lmLaunchDirection"]`.
  - The comment at `session_delivery.cpp:708` aims only at agreeing with the LM card: "so this section and the launch monitor card name a shot alike".
  - The words are built twice: `deliveryShotShape` makes "Push, slight fade" (`:718-726`), and `lmFlightShape` makes "Pull–fade" (`lm_inferred_reads.h:312-327`).
- What goes wrong: **suspected.** For a launch direction between 1° and the club's corridor edge, the Club delivery section and the report's Delivery page say "Push", while the shot's Direction factor, the shot log and Tendencies say "Straight", or the reverse when the corridor is tighter than 1°. A coach sees two answers to "which way did it go".
- Fix: a shot's direction (start and curve) is one shot-level fact, produced once by the shot's outcome composer and read by Charts, the LM card and the report. If the coach wants the LM classifier's thresholds, that is a decision about the composer, not a second classifier in a chart section. That decision is the owner's.
- Effort: M        - Related: F-02, F-11

### F-06 · Session order and "the day" come from folder names; the Sessions list uses start time
- Category: 2 Identity    - Severity: High (suspected wrong words on the report)
- Evidence:
  - The report's "Session n of the day" and "Earlier today" are built in `session_report_model.cpp:1246-1248`:
    ```
    const QString prefix = QFileInfo(job.dir).fileName().left(10);
    … lib.entryInfoList(QStringList{ prefix + QStringLiteral("_*") }, …, QDir::Name)
    ```
    and "earlier" means name-before-self (`:1546` `if (s.self) { cols.push_back(&s); break; }`).
  - The Posture "earlier sessions" history: `charts_posture_presenter.cpp:707` `if (name >= me) continue;   // earlier sessions only`.
  - The Sessions list counts by first-swing wall clock and says the two must agree: `session_report_list_model.cpp:461-467` "the session of the day, counted in start order … — the cover's "session of the day" and this row must agree."
  - Folder names put the session type before the per-type counter: `Export/swing_paths.cpp:178` `r.base = r.today + '_' + nameTok + '_' + typeTok;`, then `_NN` per base (`:203-205`). The naming pattern can put the name first: `:173` `"name-date-type"`.
  - More date-from-name readers: `session_report_model.cpp:230` (`dateOfFolder`, used by `preflight`/`state` photo decisions at `:357`, `:421`), `charts_delivery_presenter.cpp:201`, `charts_posture_presenter.cpp:271`, `charts_wrist_presenter.cpp:377`, `charts_traces_presenter.cpp:544`.
- What goes wrong:
  - **Suspected:** a day with a Wrist session at 09:00 (`…_Wrist_01`) and a Swing session at 11:00 (`…_Swing_01`), both types present in today's libraries. In name order Swing sorts first. The 09:00 report's kicker says "Session 2 of 2", while the Sessions list row says Session 1. "Earlier today" lists the 11:00 session, and the Posture address history treats the later session as earlier.
  - With the "name-date-type" pattern, `dateOfFolder` is invalid, so `preflight()`/`state()` decide photos on an invalid date while `build()` falls back to the wall clock (`:1225`). Preflight and the made report can disagree.
- Fix: a session identity object (folder, athlete, start time, local day, type, n-of-day) computed once by the library, from the documents' wall clock, and read by the list, the report, Charts and the cross-session readers. No reader parses a folder name.
- Effort: M        - Related: F-07, F-13

### F-07 · Three shot numberings, shown to the user side by side
- Category: 2 Identity    - Severity: High
- Evidence:
  - Charts numbers by rank among analysed swings: `session_series_model.cpp:856` `const int ordinal = snap->count() + 1;`, labelled "Shot %1 of %2" in `charts_delivery_presenter.cpp:618`, `charts_posture_presenter.cpp:1395`, `charts_relations_presenter.cpp:1406`, `charts_consistency_presenter.cpp:586` and `trust/trust_presenter.cpp:85`.
  - The carousel numbers by the document's index, and records why: `shot/shot_list_model.cpp:132-142` "⚠ THE NUMBER COMES FROM THE DOCUMENT, NOT FROM COUNTING THE ROWS … the same swing was Shot 6 this afternoon and Shot 3 after a restart".
  - Charts' own reference label uses the folder number: `charts_traces_presenter.cpp:546` `parts << QStringLiteral("shot %1").arg(id);` (the same in `charts_posture_presenter.cpp:273` and `charts_wrist_presenter.cpp`).
  - The report numbers swings with a ledger row and renumbers presenter payloads by folder, falling back to the presenter's ordinal: `report_posture.h:155` `swingNoOfDir.value(dir, m.value(QStringLiteral("ordinal")).toInt())` (also `report_wrist.h:120`, `report_consistency.h:124`). It keeps presenter sentences verbatim, including the count: `charts_posture_presenter.cpp:1180` `QStringLiteral("%1 %2").arg(n)…`.
- What goes wrong:
  - **Suspected:** delete `swing_0003` of a session (numbers are never reissued, `swing_paths.h` allocateSwingDir doc), or have one unanalysed swing. The carousel's "Shot 7" is Charts' "Shot 6 of 14", and a click on Charts' "Shot 6" moves the carousel to its "Shot 7".
  - In the report, a swing with analysis but no ledger row keeps its presenter ordinal, which can equal another swing's report number. The Posture kicker counts 15 swings while the cover counts 14.
- Fix: one shot identity (the folder number, already `shotId`) used for every label. The rank, if needed at all, is "n of N" computed next to that identity, and never stands in for it. The report renumbers nothing because every source uses the same id.
- Effort: M        - Related: F-06

### F-08 · Six presenters clone one scaffold, and the copies have already drifted
- Category: 5 Duplication    - Severity: High
- Evidence:
  - `diff` of `charts_delivery_presenter.cpp:887-1110` against `charts_posture_presenter.cpp:1714-1948` (setters, `currentSnapshot`, `schedule`, `startJob`, `apply`, `flushIfSynchronous`, `makeStills`, `setSynchronous`, `waitForIdle`, `updateSelected`) differs only in names, two posture-only setters and one picture parser. The wrist (`:2075-2240`) and traces (`:3594-3720`) copies match the same shape.
  - The copies have drifted:
    - Snapshot precedence is reversed. `charts_delivery_presenter.cpp:889` `if (auto *sm = qobject_cast<SessionSeriesModel *>(m_model.data())) return sm->snapshot(); return m_directSnapshot;` (also wrist and traces), against `charts_relations_presenter.cpp:2724` `if (m_directSnapshot) return m_directSnapshot;` (also consistency).
    - Scheduling uses two idioms. Delivery, posture, wrist and traces coalesce through a queued `startJob` (`charts_delivery_presenter.cpp:950-956`). Relations and consistency start at once (`charts_relations_presenter.cpp:2729-2733` `if (m_running) { m_dirty = true; return; } start();`).
    - Stills use another pool again (delivery `:1027`, posture `:1864`; wrist none).
  - Helpers are copied: `cleanDir` 7 times (`charts_delivery_presenter.cpp:105`, `charts_posture_presenter.cpp:127`, `charts_consistency_presenter.cpp:84`, `charts_relations_presenter.cpp:163`, `charts_wrist_presenter.cpp:202`, `charts_traces_presenter.cpp:261`, `trust/trust_presenter.cpp:29`); `referenceLabelOf` 3 times; `pictureStillDir` twice (delivery `:539`, posture `:1346`).
  - QML copies: `PpConsistencyView.qml:79` and `report/PpConsistencyPage.qml:57` share the same `_fill`/`_cells`; `PpPosturePage.qml` and `PpWristPage.qml` share the same block (`cpp_duplicates.md`).
- What goes wrong: a fix to presenter lifetime or scheduling (#173 was one) has to be made six times, and has already been made unevenly. A test that sets both a direct snapshot and a model gets opposite answers from two presenters.
- Fix: one presenter base (or a template `SnapshotPresenter<Inputs, Result>`) owning model wiring, snapshot choice, coalesced scheduling, the poster, synchronous mode, `waitForIdle` and stills. Each presenter supplies `compute(const Inputs&, snapshot) → Result`. Shared helpers go in one header.
- Effort: M        - Related: F-09; June Theme A (thread boilerplate), same pattern in a new place

### F-09 · Five threading idioms; the model still has the destructor wait #173 removed from the presenters
- Category: 8 State and concurrency    - Severity: Medium
- Evidence:
  - `presenter_pool.h:22-26` "⚠ DESTROYING A PRESENTER NEVER WAITS FOR ITS JOB … PpModeStage rebuilds the chart panels on EVERY mode switch … a 1.4 s freeze".
  - The same model still waits:
    - `session_series_model.cpp:408-412` `~SessionSeriesModel() { … m_pool.waitForDone(); }`
    - `session_series_model.h:324` `QThreadPool m_pool;`
    - Cold activation takes seconds (header decision 3: "7.4 s for the 50-shot 7 Oct session").
  - Every instance's job holds the process-wide lock for its whole length, disk reads included: `session_series_model.cpp:594` `QMutexLocker sharedLock(&shared().mutex);` (comment at `:592-593`: "two panels alive at once … take turns").
  - Two pools are never joined at quit:
    - The still jobs run on the global pool (`charts_delivery_presenter.cpp:1027`, `charts_posture_presenter.cpp:1864` `QThreadPool::globalInstance()->start(…)`), against the rule `presenter_pool.h:28-29` states: "none may outlive ORT or the app log".
    - The report's still pool leaks: `report/report_stills.cpp:419-423` `static QThreadPool *pool = [] { auto *p = new QThreadPool; …`.
  - The report uses `QtConcurrent::run` on the global pool (`session_report_model.cpp:490`, `:502`), and builds a QObject model inside that job (`:132`).
  - QObject view-models act as libraries on worker threads: `charts_traces_presenter.cpp:118-119, 168-169` `std::unique_ptr<ChartMetrics> cm; … if (!cm) cm = std::make_unique<ChartMetrics>();` (`ChartMetrics` is `QML_ELEMENT`).
- What goes wrong:
  - **Suspected:** open a 50-shot session not yet cached and flip Capture↔Replay before the load ends. `~SessionSeriesModel` blocks the GUI thread until the job finishes, which is the freeze #173 fixed for presenters.
  - Five trust or inline models activating at once queue behind one lock, so the Charts panel waits for a Trust fold's read.
  - **Suspected:** quitting while a report is making stills leaves FFmpeg decodes running on an un-joined static pool past static teardown.
- Fix: one worker facility. The `PresenterPool` pattern (app-owned pool, poster, per-owner wait) also serves the series loader, the stills and the report. With one series owner (F-01) the shared cache and its global lock become unnecessary. Plain functions replace QObject helpers inside jobs.
- Effort: M        - Related: F-01, F-08; June Theme A

### F-10 · God functions: `SessionReportModel::build` (1,011 lines), the traces builders (430 and 413), the relations presenter (seven jobs)
- Category: 6 Size and cohesion    - Severity: Medium
- Evidence:
  - `session_report_model.cpp:1156-2167` `void SessionReportModel::build()` reads the pack, gate words and drills. It joins swings to ledger rows, reads same-day sibling `work_ons.json` from disk (`:1242-1276`), builds brand, meta, headline, cover, grid, today, order, links, checks, work-on pages, stills plans and numbers, and builds an inline `ShotListModel` and `LmSessionModel` (`:1923-1931`). It also resets a global cache (`:1173` `resetSharedGateWordSet();`).
  - The catalogue is parsed four times per make: `:499`, `:1163`, the private diagnostics model, and the inline series model `session_series_model.cpp:599`.
  - Traces: `charts_traces_presenter.cpp:2902` `buildSession` (430 lines) and `:1641` `buildThisShot` (413). The file is 3,845 lines.
  - Relations: `charts_relations_presenter.cpp` (2,938 lines) holds This shot, the sequence, Relations, Kinds, Replay Session, the Consistency tab's shared rows and moves-with (section heads at `:1251`, `:1512`, `:1603`, `:1834`, `:2352`, `:2580`, `:2868`). `PpConsistencyView.qml:196-209` instantiates a hidden `ChartsRelationsPresenter` just for an open row's curves, so every family is computed again over a second snapshot.
- What goes wrong: a change to one report page means editing a 1,000-line function that also does file I/O and global cache resets. Charts relations work runs twice on a reviewed session.
- Fix:
  - Split `build()` into one builder per map block (each a pure function of a `ReportInputs` value), with the I/O (siblings, LM, stills) gathered before it.
  - Split the relations presenter by view, and give the Consistency tab its row-curve function directly.
- Effort: M        - Related: F-03, F-11

### F-11 · About 5,000 lines of domain rules live under `Gui/charts`, and other layers include them from there
- Category: 3 Layering    - Severity: Medium
- Evidence:
  - The pure, QObject-free rule files are `session_warp` (921 lines), `session_traces` (672), `session_relations` (734), `session_consistency` (786), `consistency_grades` (279), `session_posture` (2,492), `session_delivery` (1,463), `session_sequence` (477), `session_bridge` (372) and `variability_clubs` (295). Each states it is "Pure" (e.g. `consistency_grades.h:45`).
  - Coaching names Gui as the home of its rules: `Coaching/consistency_home.h:20-23` "A cell's grade is the variability grader's (src/Gui/charts/consistency_grades.h) … The rules that fill the cells are src/Gui/charts/session_consistency.h".
  - Domain consumers reach into charts:
    - `Gui/diagnostics/session_diagnostics_model.cpp:38` `#include "../charts/variability_clubs.h"`
    - `Gui/variability/variability_ledger.h:42` (same)
    - `Gui/trust/trust_readings.cpp` includes `charts/session_sequence.h`.
  - There are Gui-internal cycles: charts → report (`posture_picture.cpp:25`, `delivery_picture.cpp:23` `#include "../report/report_stills_pure.h"`) and report → charts (`session_report_model.cpp:32-39`, which includes five presenter headers to reach static `*SessionReportPayload` functions). Charts → diagnostics (`charts_relations_presenter.cpp:38` `session_spread.h`) closes the `include_graph.md` cycle.
  - Presenters do cross-session file I/O: `charts_posture_presenter.cpp:706-714` reads every earlier session's `diagnostics.json` (an athlete-level address history), and `charts_delivery_presenter.cpp:196-260` reads the day's sibling `swing_phasegrid.json` files.
- What goes wrong: rules that define consistency grades, posture and delivery verdicts and athlete address history cannot be used by Diagnostics or Coaching without depending on Gui. They are already used that way upward. Athlete-level facts are computed by a chart presenter when it happens to run, and the report gets them by linking presenter QObjects.
- Fix:
  - Move the pure files to a domain library (e.g. `src/Session/` or `src/Diagnostics/session/`) with their tests. The payload builders the report needs move out of the presenter headers into that library.
  - The athlete address history and the day's fit pool become athlete- and session-level facts with an owner.
  - `box_stats.h` and `session_spread.h` follow (see other reviewers).
- Effort: L        - Related: F-02, F-05; include_graph cycles

### F-12 · Payload contracts live in comments and untracked files, with a struct → map → struct round trip
- Category: 7 Contracts    - Severity: Medium
- Evidence:
  - Schemas are cited from `build/`, which is git-ignored: `session_report_model.h:24` "none in QML: build/report-build/REPORT_MAP.md is the schema". 29 source files cite `build/report-build/…`, `build/charts-build/CONTRACT.md` or `build/wrist-tab/CONTRACT.md`. `git check-ignore` reports `.gitignore:44:Build/`, and `build/wrist-tab/CONTRACT.md` does not exist on disk.
  - The wrist profile is built as a `QVariantMap` whose shape is documented in a comment (`session_wrist.h:36-55`), stored on `ShotSeries::wrist`, then parsed back into a struct (`charts_wrist_presenter.cpp:238-262` `Profile parseProfile(const QVariantMap &p)`).
  - Tiers are strings rather than the enum: `session_series_types.h:186` `QHash<QString, QString> tiers;` ("clean" | "watching" | "pattern").
  - Grade words go over the C++ → QML seam as positional `%1..%7` templates, filled identically in two QML files (`PpConsistencyView.qml:79-83`, `PpConsistencyPage.qml:57-61`). This contradicts "every string made here, none in QML" (`session_report_model.h:21-24`) and the design rule "no JavaScript logic in QML" (`docs/design/charts_panel_traces_relations.md:228`).
  - `model` is typed `QObject*` and resolved by `qobject_cast` on every read (`charts_delivery_presenter.cpp:889`).
- What goes wrong: a key renamed in one presenter breaks a QML page or a report block silently. The only description of the shape is a comment or a file a fresh clone does not have.
- Fix:
  - Commit the schemas next to the code (e.g. `docs/design/contracts/`), or better, typed `Q_GADGET` payloads for the stable blocks.
  - Keep the wrist profile as a struct end to end.
  - Use the `Tier` enum.
  - Have C++ fill the vocabulary words, or give QML one shared function.
- Effort: M        - Related: F-08

### F-13 · Persistence: version gates bypassed, and file names spelt four times
- Category: 9 Persistence and versioning    - Severity: Medium
- Evidence:
  - The report stamps old records as current to get past the reader's gate (`session_report_model.cpp:1258-1262`):
    ```
    root[QStringLiteral("schemaVersion")] = kWorkOnSchemaVersion;
    root[QStringLiteral("ruleVersion")]   = kWorkOnRuleVersion;
    ```
  - The ledger file name is spelt per reader: `session_series_model.cpp:56` `kLedgerFile`, `charts_posture_presenter.cpp:709` `QStringLiteral("/diagnostics.json")`, `diagnostics/work_ons_controller.cpp:71`, `athlete/athlete_controller.cpp:743`. In this area `"work_ons.json"` (`session_report_model.cpp:1253`) and `"swing_phasegrid.json"` (`charts_delivery_presenter.cpp:226`, `:250`) are spelt the same way.
  - `SessionSeriesModel::extractShot` is a third reader of `analysis.metrics`, by its own description: `session_series_model.cpp:241` "The field-by-field twin of DiskReplaySource::load's series loop (and so of ShotProcessor's live map)".
- What goes wrong: a work-on schema change that moves a field silently mis-reads older siblings in "Earlier today", because the gate that would refuse them has been bypassed. A rename of `diagnostics.json` must be found by grep in four places. A field added to the metrics reader must be added three times, and the comment "character for character" is the only guard.
- Fix:
  - One reader per format, owning its file name and its migration. A relaxed `sessionWorkOnsEvidenceFromJson` that states which fields survive old schemas replaces the field rewrite.
  - One `analysis.metrics` → series deserialiser shared by the replay, the processor and the loader.
- Effort: M        - Related: F-02, F-06

### F-14 · Dead and test-only code
- Category: 14 Dead code / 12 Testability    - Severity: Low
- Evidence:
  - `SessionSeriesModel::wristArchetype` (Q_PROPERTY, `session_series_model.h:144`), `setWristArchetype` (`.cpp:396-405`) and `JobKind::Rearchetype` have no app caller. The only uses are `Gui/tests/charts_series_model_test.cpp:431,438`, and the wrist presenter re-grades on its own (`charts_wrist_presenter.cpp:1858`).
  - `PpChartsPanel.qml:302-303` guards `SessionMode.chartsNav !== undefined` for "build 1". The property exists (`SessionMode.qml:70`).
  - `session_wrist.h:27` "`engineScore` is kept only so the morning's before/after table can show the two side by side".
  - `Coaching/consistency_home.h:25-26` "the old per-session <session>/consistency.json files stay in the libraries, unread".
  - A settings migration runs in a view: `PpChartsPanel.qml:237-238` `p === "Club & speed" ? "Club delivery" : p`.
- What goes wrong: about 60 lines of model surface with a job kind that runs only in tests, plus guards and migrations that hide real behaviour.
- Fix: delete the archetype property and job kind, the `chartsNav` guard and `engineScore`. Move preference migrations to `AppSettings` load.
- Effort: S        - Related: F-01

### F-15 · Hygiene: normalisation rules, settings reads, a model built from a const getter, lint gaps
- Category: 2 / 8 / 15    - Severity: Low
- Evidence:
  - Three folder normalisations:
    - `session_series_model.cpp:60-63` `QDir::cleanPath(QFileInfo(p).absoluteFilePath())`
    - presenters' `QDir::cleanPath(d.trimmed())`
    - QML `SessionMode.qml:98` `folderKey` (slashes only; no case folding on Windows, the primary platform).
  - The grade policy bypasses `AppSettings`: `session_report_model.cpp:235` `ppSettings().value(QStringLiteral("ui/diagnosticsGradePolicy"), …)`.
  - `SessionReportModel::contentStamp() const` creates a `SessionDiagnosticsModel` through `const_cast` (`:315-318`). `state()`, which the list asks for every row, reaches it through `fingerprintOf`.
  - 49 `"black"`/`"white"` literal fallbacks in 24 Charts QML files (e.g. `PpChartsShotView.qml:156` `tone: sv.roles ? sv.roles.ink : "black"`). The D4 lint only matches hex (`design_system_lint_test.cpp:275`).
  - Test CMake lists the same sources twice in one target (`Gui/tests/charts_posture_tests.cmake:13-17` `pp_log_stream.cpp`, `PpMessageLog.cpp`), and there is no Charts library target, so each test hand-lists its closure.
- What goes wrong: path equality depends on which helper compared the two paths, which matters for "is this shot in the session". The rest is cost and noise.
- Fix:
  - One `canonicalSwingDir()` used by C++ and exposed to QML.
  - Read settings through `AppSettings`.
  - Compute the content stamp from the pack and norms without a model.
  - Whether D4 should also match colour names, and whether the fallbacks should become `Theme` tokens, is a design-system decision for Mark, not a fix to apply here.
  - A `pp_charts` static library for the tests.
- Effort: S        - Related: F-08

## 4. Done well

- **Immutable snapshots with pointer identity.** `SessionSnapshot` and `ShotSeries` are published as `shared_ptr<const>`, and a changed shot is a new object (`session_series_model.h:97-100`, `.cpp:866-881`). Readers can diff by pointer, and no reader ever sees a half-built session.
- **Honest missing values.** Missing values stay NaN, never 0 (`session_series_model.cpp:72-75` `num()`). Unanalysed shots are counted, never hidden (`skipped`, decision 8). The report has an explicit `kNA = "-"` and `kNull` convention (`session_report_model.cpp:105-108`).
- **`PresenterPool` is a sound fix.** It is app-owned, delivers through a `QPointer`-guarded poster, and waits per owner (`presenter_pool.h`). It should become the pattern for the whole area (F-09).
- **The classifier lives with the LM domain.** `lmShapeClass` lives in `LaunchMonitor`, and Charts calls it rather than keeping thresholds, which an earlier copy got wrong (`lm_inferred_reads.h:73-78`). The remaining problem is the second, pack-based rule (F-05).
- **Shared parsers and reductions where it counts.** The ledger is read with its own deserialiser (`session_series_model.cpp:794-796` `pinpoint::analysis::fromJson`), and tiers and work-ons use the Coaching reductions rather than re-written rules. The duplication is of computation, not of rule text (F-02).
- **Testability.** Every presenter and the model have a synchronous mode and standalone tests (`charts_*_presenter_test.cpp`, `charts_session_maths_test.cpp`, `session_posture_test.cpp`, `session_delivery_test.cpp`, `report_model_test.cpp`). The pure maths is testable without Qt Quick. The report page blocks (`report_posture.h` etc.) are pure and header-only.
- **QML discipline.**
  - `PpChartsPanel` is wiring only, and the look lives in `PpChartsBody`.
  - The typeset roles carry every colour and space (`g(n)`, `u(n)`).
  - The report pages' allowlist entries are reasoned print exceptions (`design_system_allowlist.txt:14-32`).
  - The one held raw input (`PpChartsReadingsView.qml:107`) is waiting on Mark's approval, as the rules require.
  - I found no Rectangle + Text + MouseArea controls outside the sanctioned typeset link style.
- **The report writes nothing to the session.** `setPersistEnabled(false)` and `BaselineAuthority::Never` (`session_report_model.cpp:308`, `:505`). Only the comments have not caught up (F-02).

## 5. June review status

The June review (2026-06-20) predates both directories. `session_series_model.h` was added on 2026-10-14 and `session_report_model.h` on 2026-10-21, so the review has no item specific to this area. Its cross-cutting themes apply:
- **Theme A (thread-lifecycle boilerplate, High): worse here, in a new form.** Six presenters copy one scheduling and worker scaffold, and the area has five threading idioms (F-08, F-09). `PresenterPool` is a good partial fix, applied to the presenters' compute jobs only.
- **Theme K (magic numbers) and "business logic in QML": partly repeated.** Report caps are named constants (`session_report_model.cpp:90-102`, good). Preference migrations and positional word filling sit in QML (F-12, F-14).
- **QML "missing reusable components": not repeated here.** Charts and report use shared typeset components, and the duplicated QML is small (F-08).

## 6. Questions

1. Should a shot's direction in the Club delivery section be the shot's Direction factor (pack corridors) or the LM classifier's thresholds (F-05)? Both are coaching judgements, so this is Mark's to rule. The code currently has both.
2. The report's Posture page picks a "typical swing" by median distance to the pattern (`charts_posture_presenter.cpp:1648-1668`), while the grid's reference swing comes from `R::referenceSwing` over the work-ons and carry (`session_report_model.cpp:1410-1418`). Are two different "typical" swings in one PDF intended?
3. Is the LM-only standalone path's session type token (`launch_monitor_controller.cpp:513-517`, empty → "session") meant to produce a separate session folder on the same day? It changes the name order F-06 depends on.
4. `SessionSeriesModel`'s shared cache keeps "the last two activated sessions". With one owner per open session (F-01), is the cache still needed, or was it only compensating for panel rebuilds (header decision 5)?
5. Is `build/charts-build/CONTRACT.md` and its siblings (F-12) meant to be kept? `build/wrist-tab/CONTRACT.md` is already gone while six source comments still cite it.
