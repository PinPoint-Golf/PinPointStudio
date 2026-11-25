# Platform refactor, #224-U: eleven questions on the presenter residue

**Status: open, 25 Nov 2026; every question ruled the same day[^u-ruled-25nov], and the lever pass then deferred the three big headers' typing (U-V1)[^u-v1]. The history read and the stale-session scan are built here (U-V2 withdrawn)[^u-v2].** Nothing here is built. Each row has an empty **Ruled** line: the coordinator writes
the ruling, the role and the date there, and the briefs that wait read it. This page is item U-0 of
`build/platform/U/PLAN-U.md`.

**Why this page exists.** #224-U is the 28 allowlist lines (115 findings) that the domain refactor's step 5 left on
`src/Gui`'s presenters[^g24]. Twenty of its 33 items make code match the information model as it stands and need
no ruling. Twelve wait on one of the questions below (the last item is the close): where a line's own reason says "an ownership change", where
the door a reason names does not exist, or where a file use is no object's persistence.

**How to read it.** Counts were taken on main at `2ec638d2`, each with its command, run from the repo root. Classes
follow IM §1.2 (read through `python3 tools/method/im_excerpt.py P9 P10 P11 R-DER-3 DEC-74 DEC-195 FILE-SESS-05
FILE-SESS-08 OBJ-SESSION-SERIES`). "When in doubt, it is a change", so a doubtful one is classed HARD. Every
recommendation is **(proposed, for the role named)**. "If nothing is ruled" is always the same: today's behaviour
stays, the allowlist line stays with its count, and the item is not started.

| # | Question | Class | IM §1.2 | Recommended | Items that wait |
|---|---|---|---|---|---|
| Q1 | Who makes the Charts and Consistency presenters | **HARD: the Solution Architect** | C10 | (a) a C++ door by session and surface | U-18a |
| Q2 | Who makes the trust views' and the data viewer's presenters | **HARD: the Solution Architect** | C10, C11 | (b) ruled view-local | U-18b |
| Q3 | Where a condition's history across sessions is read from | **HARD: the Solution Architect** | C3 | (a) a block in the session summary | U-11 |
| Q4 | Where the work-ons' stale-session scan lives | Coordinator's call; re-ruled by the Solution Architect | C6 | (a) the athlete level answers; the scan goes. Re-ruled: (c), the scan as it is in the athlete's repository, as an interim | U-12 |
| Q5 | How the mailer reaches the made report | Coordinator's call | none | (a) a read on the Session | U-6b |
| Q6 | How the 3-D view reads a whole session | Coordinator's call | none | (a) the Session's shots, each through its own source | U-15c |
| Q7 | File uses that are no object's persistence | **HARD: the Solution Architect** | C11 (lint A5's scope) | (a) two named Platform helpers | U-6b, U-16b, U-17 |
| Q8 | The data viewer's Score row | **HARD: the Design Lead** | none (a changed look) | (a) the row goes, as in #186 | U-17 |
| Q9 | The review's session load and the bulk export | **HARD: the Solution Architect** | C8, C4 | (a) a Session command that indexes its shots | U-13 |
| Q10 | Closing #224-U one line short until Stage D | Product Owner | none | (a) close at 27 of 28 | U-10c, U-19 |
| Q11 | A shot document's stamp, read through the Shot | **HARD: the Solution Architect** | C5 | (a) one read on the Shot | U-15b, U-15c, U-16a[^u12-q11] |

**By class:** the Solution Architect 6 (Q1, Q2, Q3, Q7, Q9, Q11); the Design Lead 1 (Q8); the coordinator 3 (Q4,
Q5, Q6); the Product Owner 1 (Q10). **Q11 and Q7 free the most:** three items each.

## Items that can start with no ruling

1. **Docs, today:** U-0 (this page), U-1i (the diagnostics adapter's inventory), U-2i (the work-ons'), U-3i (the
   report's). None edits `src`.
2. **Once Stage B is closed:** U-4, U-5, U-6a, U-7, U-14a and U-15a, in any order.
3. **After their inventories:** the pins U-P1 and U-P2; then the typing, U-8a to U-8c, U-9a, U-9b, U-10a, U-10b
   and U-14b. U-P1 and the typing of the three big headers (U-8a to U-8c, U-9a, U-9b, U-10a to U-10c) are deferred
   by the lever pass; U-P2, U-14b and the new slice U-8q stay[^u-v1].

That is 20 of the 33 items and 49 of the 84 hours. Of the three coordinator's calls, only Q4, re-ruled, frees an
item alone (U-12)[^u12-q11]; U-15c also waits on Q11, and U-6b on Q7.

## The questions for the Solution Architect

### Q1 · Who makes the Charts and Consistency presenters
- **Today.** Two views create seven session-scoped presenters themselves: `PpChartsPanel.qml` five (lines 143–217)
  and `PpConsistencyView.qml` two (203, 296)
  (`grep -nE 'Charts\w*Presenter\s*\{' src/Gui/charts/PpChartsPanel.qml src/Gui/diagnostics/PpConsistencyView.qml`).
  The six presenter classes are `QML_ELEMENT` (`grep -n QML_ELEMENT src/Gui/charts/charts_*_presenter.h`). P9 names
  this as its violation, and the two lines' reasons call the fix "an ownership change".
- **The pattern exists twice.** The series the presenters read is handed out, never created:
  `ChartsSeries.of(app.session)`, 8 uses, its type `QML_UNCREATABLE` (`charts_work.h:68`). The diagnostics adapters
  are too: `diagnosticsAdapters.of(session, surface)`, 5 uses
  (`grep -rhoE 'diagnosticsAdapters\.of\(|ChartsSeries\.of\(' src/Gui --include='*.qml' | sort | uniq -c`).
- **Options.** (a) One holder in the Adapters ring, made in C++ with the others, which hands out a session's
  presenters by surface, `chartsPresenters.of(session, surface)`; the six classes become uncreatable; a presenter
  lives as long as its session's series. (b) The `Session` itself owns them. (c) Rule them view-local, as Q2
  recommends for the trust views.
- **Recommendation: (a).** It is the ruled shape for the diagnostics adapters[^dec165], and it adds no domain
  object: the holder is an adapter over OBJ-SESSION-SERIES, which stays the `Session`'s. (b) puts Qt presenters in
  the Domain model ring. (c) does not fit: these presenters run pooled work over the whole session and are made
  once per screen (`PpChartsPanel` in 2 places, `PpConsistencyView` in 2), so one per session and surface serves.
- **C10** (a session-scoped thing a view creates is removed). Also the Product Owner's, by the lines' own reasons.
  Waits: U-18a.
- **Ruled:** as recommended (25 Nov 2026)[^u-ruled-25nov].

### Q2 · Who makes the trust views' and the data viewer's presenters
- **Today.** `TrustPresenter` is created by three views, and those views are created in 22 places in 17 files:
  `PpTrustInlineFor` 12 places in 11 files, `PpTrustSummaryFold` 9 in 5, `PpTrustPanel` 1
  (`grep -rhE '\bPpTrustInlineFor\s*\{' src/Gui --include='*.qml' | wc -l`, and the same for the other two).
  It is 186 lines with five properties: the session's series (handed in, not owned), one picked swing, and its
  results (`wc -l src/Gui/trust/trust_presenter.{h,cpp}`; `grep -c Q_PROPERTY src/Gui/trust/trust_presenter.h`).
  `SwingDataSource` is created once, by `PpDataViewer.qml:90`, over a `Shot` it is handed.
- **Why a door fits badly.** Each trust view has its own picked shot[^e7], so a presenter per session can't serve
  them; a door would have to key by view instance, which is a view creating it under another name.
- **Options.** (a) The door of Q1, keyed by session, surface and picked shot. (b) Rule the two types view-local:
  each holds only its view's pick and a pointer to data its `Session` owns, as `ChartMetrics` and
  `ShotFilterProxyModel` are already classed in the lint's own list. The lint's A2 list loses the two names; the
  four allowlist lines go; no code moves. (c) Keep the lines, re-tagged `# kept` with this reason.
- **Recommendation: (b).** What P9 guards against is a view owning session data. Neither type does: the series is
  the `Session`'s and the shot is the `Session`'s. (a) is about 3 h for no change in who owns what. (b) changes
  what the page says a scoped type is, which is why it is HARD. If (a) is ruled, U-18b grows from 1 h to about 3 h.
- **C10, C11.** Waits: U-18b.
- **Ruled:** as recommended (25 Nov 2026)[^u-ruled-25nov]. U-18b then takes 1–1.5 h, not 2.5: it edits the lint's list and deletes four lines, and moves no code[^u-v0].

### Q3 · Where a condition's history across sessions is read from
- **Today.** The adapter's `readHistory` lists the golfer's other session folders itself and reads each one's
  `diagnostics.json` ledger, off the GUI thread (`session_diagnostics_model.cpp` 3120–3185; the A5 line's four
  findings). From each row it keeps four fields per reading: the measure, its value, the club and the context
  (`grep -n 'struct HistoryReading' src/Gui/diagnostics/session_diagnostics_model.h`). It is live: the detail's
  `history` key reads it (`grep -nw 'readHistory\|historyFor' src/Gui/diagnostics/session_diagnostics_model.cpp`).
- **What is ruled.** Charts' and the report's reads of other sessions go through session results, and the session
  summary may grow for them[^dec74]; it grew by `dayReadings` (`resultsVersion` 4). The athlete level reads
  summaries, with one exception, for the scoped views[^dec195]. Neither names this read.
- **Options.** (a) A block in the session summary (FILE-SESS-05): per shot and condition, the same four fields;
  `resultsVersion` 5. The adapter reads the athlete level's summaries, and "not yet summarised" replaces today's
  "no ledger yet" count. (b) Widen R-DER-3's exception: the athlete level reads each other session's judged rows
  through that session's own diagnostics, holding and releasing each `Session`. (c) The line is re-tagged `# kept`.
- **Recommendation: (a).** It is the route DEC-74 ruled for the same kind of read, and the summary's writer and
  owner do not change. (b) loads every other session's `Session` to draw one chart, and turns a named exception
  into the rule. The cost of (a): the catch-up rewrites every summary once (a derived file, which the stamp already
  makes stale); U-11's brief takes the dry-run count on a scratch copy of the library before anything is written.
- **C3** (a new block in a persisted file). Waits: U-11.
- **Ruled:** as recommended (25 Nov 2026)[^u-ruled-25nov]. It stands: U-11 builds (a) in #224-U, and takes the dry-run count on a scratch copy of the library before the library's summaries are rewritten[^u-v2].

### Q7 · File uses that are no object's persistence
- **Today.** Lint A5 counts every `QFile`, `QDir`, `QFileInfo` and `atomicWrite` outside the stores and the
  Platform ring. Three of #224-U's uses read or write nothing a domain object owns
  (`grep -nE 'QFile|QFileInfo|QDir\(|atomicWrite' <file>`):
  1. `review/swing_data_source.cpp` 387–392: the data viewer's CSV export, a new file in the home folder (3 of
     the line's 4 findings).
  2. `report/report_stills.cpp` 361: the Charts pictures' stills, written to their own cache.
  3. `report/report_mailer.cpp` 107–132: reveal-in-folder and copy, which ask the file manager about a path.
  Two more are test seams that open a fixture (`charts_delivery_presenter.cpp:1121`; the 3-D rig's `loadNow`).
- **Options.** (a) Two named Platform helpers, which the Platform ring's exemption already covers: one for a
  user's export (a new file in a folder the user owns, never overwriting) and for revealing a path; one for a
  cache file under the application's cache folder. No lint change and no allowlist line. (b) The three uses are
  re-tagged `# kept (not an object's persistence)`. (c) A5 learns the difference itself.
- **Recommendation: (a), with the helpers named on this page so that they are a ruled door, not a way round the
  lint.** P11 is about an object's files; a user's export and a cache are neither, and one place for each also
  gives the "never overwrite" rule one copy. (b) leaves three lines for ever. (c) is a rule the lint can't check
  from text. The test seams need no ruling: they take a parsed document, as the Charts presenters' seams do
  since step 5.5c.
- **C11** (what lint A5 and P11 are read to cover). Waits: U-6b, U-16b, U-17.
- **Ruled:** as recommended (25 Nov 2026)[^u-ruled-25nov].

### Q9 · The review's session load and the bulk export
- **Today.** `SessionReviewController` includes four repository headers
  (`grep -n 'session_archiver\|swing_doc\|recording_repository\|session_repository' src/Gui/review/session_review_controller.cpp`:
  lines 25, 38, 41, 42). Its load lists the session's swing folders, reads each summary on a pool thread and, for a
  JSON-era swing with none, parses the whole swing and **writes** its summary sidecar through the repository (lines
  327–342). It checks an archived folder by its marker (376), and lists the folders again for the bulk export
  (423). No test builds the controller.
- **Options.** (a) A `Session` command, "index your shots": the `Session` lists its own shots, each `Shot` reads
  its summary or makes it through its repository, on the pool as today; the archive state comes from the session
  list's entry; the bulk export asks the `Session` for its shots. (b) The same reads behind a door in `src/Gui`,
  as the review views have for one shot. (c) The line is re-tagged `# kept`.
- **Recommendation: (a).** The sidecar is the `Shot`'s file and the list is the `Session`'s collection (P10), so
  both belong behind the `Session`; (b) would keep a view deciding when a shot's file is written. The write, its
  file and its repository are the same; who starts it changes, which is why this is HARD. U-P2 pins the load
  first: a JSON-era fixture session must index to the same sidecars, byte for byte.
- **C8** (a new command on the `Session`), **C4** (who starts a file's write). Waits: U-13.
- **Ruled:** as recommended (25 Nov 2026)[^u-ruled-25nov].

### Q11 · A shot document's stamp, read through the Shot
- **Today.** Four presenters ask the store whether a swing document exists, how big it is and when it changed,
  and build a cache key or fingerprint from the answer: the delivery strip, the 3-D rig, the work-ons scan and
  the report's fingerprint. Two more lines ask only whether it exists
  (`grep -rn 'SwingStore::info\|SwingStore::hasDocument' src/Gui --include='*.cpp' --include='*.h' | grep -v /tests/`:
  4 and 2 in these files). The `Shot` and its reader offer no such read
  (`grep -rnE 'DocInfo|mtimeMs' src/DomainModel/{shot,session,recording_reader,recording_repository}.h`: nothing).
- **Options.** (a) One read on the `Shot`, answered by its reader: exists, size, modified. The four keys keep
  their values, so no cache or report goes stale. (b) Replace the keys with what the model already says makes a
  derived fact stale: the shot's events and its stage versions. (c) Each line is re-tagged `# kept`.
- **Recommendation: (a) now, with (b) named as the better end.** (a) is the smallest change that keeps every
  fingerprint byte for byte, which is what lets four items be checked by comparison. (b) is right by P5, but it
  changes when a report or a summary is remade, and that is behaviour for a plan of its own.
- **C5** (the reasons call this "the document's identity": telling a shot's state by its file's size and time).
  Waits: U-15b, U-15c, U-16a[^u12-q11].
- **Ruled:** as recommended (25 Nov 2026)[^u-ruled-25nov].

## The question for the Design Lead

### Q8 · The data viewer's Score row
- **Today.** The data viewer's Analysis group shows a "Score" row, the stored overall number, beside "Tier" and
  "Schema" (`sed -n '743,749p' src/Gui/review/swing_data_source.cpp`). To read it the presenter includes the
  store's header for one function, `overallScoreFromAnalysis`, which is the fourth finding on its A5 line
  (`grep -rn overallScoreFromAnalysis src --include='*.cpp' --include='*.h' | grep -v /tests/`: this one caller
  outside the store).
- **What is ruled.** The score is retired from view until the shot score is built[^pr32]; the coordinator applied
  that to three more displays, for the Design Lead's review[^pr37]; the named wrist score stays[^pr38]. #186 is
  open for the shot picker's chips only. Nobody has listed this row.
- **Options.** (a) The row goes. The table is otherwise unchanged, uses no new component and closes its own gap;
  the include goes with it. (b) The row stays, as a stored value shown raw like Tier and Schema, and its one rule
  moves out of the store to sit beside the analysis block's other readers (a placement the coordinator makes).
- **Recommendation: (a).** It is the same unnamed number the pills showed, and it reads 0 where nothing was
  scored, which is what the ruling was aimed at. The case for (b) is that this screen shows what is stored, not
  what is judged. **Before and after:** before, the Analysis group reads Score, Tier, Schema; after, Tier, Schema.
  U-17 delivers the annotated before-and-after sheet for the Design Lead's review before it is committed, and
  changes nothing else on the screen.
- **A changed look** (`governance/artefacts/design_system/README.md`: any changed look needs the Design Lead's approval before it
  is built). No information-model change: the score is still computed and stored. Waits: U-17.
- **Ruled:** (a), the row goes[^q8r].

## The coordinator's calls

Each makes code match the page as it stands (IM §1.2: not a change). Each has a stop: if the inventory or the
item's first check shows more than is written here, the item keeps today's behaviour and the question goes to the
Solution Architect.

### Q4 · Where the work-ons' stale-session scan lives
- **Today.** `WorkOnsController` lists the golfer's session folders and, for each, reads the summary's file
  stamp, the club baseline and every swing document's stamp, to decide which sessions the catch-up should redo
  (`sed -n '540,600p' src/Gui/diagnostics/work_ons_controller.cpp`; A5 ×7: four repository includes and three
  reads). The catch-up itself is already in `src/App` (`athlete_catch_up.h`), and the athlete level already has
  a freshness check over the same stamps. The line's reason says the scan "belongs to the catch-up"[^j59].
- **Options.** (a) The controller asks the athlete level which sessions are stale and hands that list to the
  catch-up; its own scan and its four includes go. (b) The scan moves as it is into `src/App`. (c), added on
  25 Nov: the scan, reading exactly what it reads today and returning the same rows, moves into the athlete's
  repository (`src/Export`) as a read the worker may call, beside `readActivity` and `readIndexWithSummaries`. The
  composition root hands it to the controller through the catch-up's door, as P-3b's gate counts are handed over.
  The controller keeps `applyScan` and every duty U-2i lists; it stops reading files, and its three path helpers'
  callers are re-pointed, so all seven findings go (`build/platform/LEVERS/FOCUSED-U-V2-C-V1.md` §2).
- **Call: (a).** (b) only moves the file reads to a ring where A5 still counts them. **Stop:** if U-2i finds a
  fact the scan reads that the athlete level does not hold, other than Q11's stamp. Waits: U-12[^u12-q11].
- **Ruled:** as recommended, (a): the athlete says which sessions are stale (25 Nov 2026)[^u-ruled-25nov].
  **Re-ruled the same night: (c), as an interim**, with (a) kept as the end state[^u-q4c]. Each fact the scan reads
  is still read through its owner's file door, and every one is a fact the athlete level may read today.

### Q5 · How the mailer reaches the made report
- **Today.** QML hands the mailer a path, and the mailer opens the PDF itself to attach it
  (`report_mailer.cpp` 459–475). The `Session` writes the report (`makeReport`) and says where the print goes
  (`reportPrintTarget`), but has no read of the made one
  (`grep -n 'reportPrintTarget\|makeReport\|placeReportPdf' src/DomainModel/session.h`).
- **Options.** (a) A read on the `Session` for its made report, its name and bytes, through the same repository
  that places it (FILE-SESS-08). (b) A door in `src/Gui`.
- **Call: (a).** The PDF is the `Session`'s file and gets its reader where its writer is (P11). No file, field
  or writer changes. **Stop:** if the read needs anything FILE-SESS-08's row does not already name. Waits: U-6b
  (with Q7, for reveal and copy).
- **Ruled:** as recommended (the coordinator's call, 25 Nov 2026; `build/platform/JUDGEMENT-CALLS.md` PR-55, for the Product Owner's review).

### Q6 · How the 3-D view reads a whole session
- **Today.** `SwingRigDriver::prepareSession` lists the session's swing folders and loads parts of each document
  by folder (`sed -n '779,812p' src/Gui/swing3d/swing_rig_driver.cpp`). One shot is already read through the
  review views' door.
- **Options.** (a) The driver takes the `Session`, walks `Session::shots()` and reads each through that `Shot`'s
  own recording source; its fingerprint uses Q11's stamp. (b) A new session-level read on the `Session`.
- **Call: (a).** P10 already says a session's shots are reached through it, one by one; no new door is needed.
  **Stop:** if the recording source can't load only the parts the view needs, so that a session of 30 shots
  would parse 30 whole documents. Waits: U-15c (with Q11).
- **Ruled:** as recommended (the coordinator's call, 25 Nov 2026; `build/platform/JUDGEMENT-CALLS.md` PR-55, for the Product Owner's review).

## The question for the Product Owner

### Q10 · Closing #224-U one line short until Stage D
- **Today.** `session_report_model.h` has six A6 findings. Two are the report's payload, which carries the launch
  monitor board's `graphics` map; Stage D types that map in SD-10c (`build/platform/DEF/PLAN-D.md`, line 53).
  #224-U is scheduled straight after Stage B[^po24], before Stage D.
- **Options.** (a) #224-U closes at 27 of 28 lines, the last (A6 ×2) named on #224 and finished by U-10c (2 h)
  after SD-10c. (b) #224-U stays open until Stage D reaches SD-10c. (c) Forecast lever 10 in reverse: the board's
  typing moves here from Stage D (about 6 h and Stage D's three lines).
- **Recommendation: (a).** The accept figure then reads: tagged lines 32 → 5, and 4 after U-10c. (b) holds a
  finished plan open for one finding pair; (c) brings Stage D's unruled owner question (D-6) into this plan.
- **Ruled:** as recommended (25 Nov 2026)[^u-ruled-25nov]. Overtaken by the lever pass: U-10c is deferred with the rest of the report's typing, so #224-U no longer waits for SD-10c, and its close no longer reads "27 of 28"[^u-v1].

[^g24]: `docs/implementation/platform_refactor_assessment.md`, "#224 in three groups"; #224's "U" list; the premises are in `build/platform/U/NOTES-U-plan.md`.
[^po24]: The Product Owner, 24 Nov 2026 (the assessment's "Rulings of 24 Nov"; §9 Q16).
[^dec165]: The Solution Architect, 15 Nov 2026 (DEC-165): thin adapters, created in C++.
[^dec74]: The Solution Architect, 14 Nov 2026 (DEC-74), and 23 Nov 2026 for the summary's growth ("You can grow the summary file to support this").
[^dec195]: The Solution Architect, 21 Nov 2026 (DEC-195; R-DER-3's one exception).
[^e7]: Refactor step 5.5b, E7 (`build/refactor/step-5/NOTES-5.5.md`, line 102).
[^j59]: The coordinator, 22 Nov 2026 (`build/refactor/step-3/JUDGEMENT-CALLS.md`, J-59).
[^pr32]: The Design Lead, 24 Nov 2026: "retire any score being shown until we have the shot score done" (`build/platform/JUDGEMENT-CALLS.md`, PR-32; #186).
[^pr37]: The coordinator's call, 24 Nov 2026, for the Design Lead's review (PR-37).
[^pr38]: The coordinator's call, 24 Nov 2026, for the Design Lead's review (PR-38).


## Added after U-1i (the inventory of the diagnostics adapter's keys, 25 Nov 2026)

### Q12 · `expectations`: retire it? (HARD: the Design Lead)
- **Today.** The adapter hands QML `expectations` (h:345), read in 3 files, but it is always empty (h:343–344). `build/platform/U/INVENTORY-U-1i.md`.
- **Recommendation.** Retire it, since nothing ever fills it, so no golfer sees a change (proposed, for the Design Lead).
- **Ruled:** as recommended (25 Nov 2026)[^u-ruled-25nov]. Built in U-8q, the one slice of the adapter's typing that #224-U keeps[^u-v1].

### Q13 · `PpDiagReplayState` builds its own "partial" readout in QML (HARD: the Solution Architect)
- **Today.** The view assembles a readout object of its own instead of reading one the session owns. That may be a view owning domain state (C10). `build/platform/U/NOTES-U-1i.md`.
- **Recommendation.** The readout is built by the session's presenter, and the view only reads it; U-8 moves it (proposed, for the Solution Architect).
- **Ruled:** as recommended (25 Nov 2026)[^u-ruled-25nov]. The readout is built in C++ as a typed value in U-8q, which #224-U keeps; the rest of U-8 is deferred[^u-v1].


### Q14 · FILE-SESS-05's row names `WorkOnsController` as a writer (HARD: the Solution Architect)
- **Today.** The IM's FILE-SESS-05 row lists `WorkOnsController` among the summary's writers; the code writes it through the session's one writer. Found by U-2i (F16).
- **Recommendation.** Correct the row to the one writer the code has, as a status edit, citing U-2i (proposed, for the Solution Architect).
- **Ruled:** as recommended (25 Nov 2026)[^u-ruled-25nov].

### Q15 · The report's `make` options are written whole into FILE-SESS-13: is typing them a format change? (HARD: the Solution Architect)
- **Today.** The report's `make` call takes an untyped options map and writes it whole into FILE-SESS-13 (U-3i, `build/platform/U/NOTES-U-3i.md` F3). Typing the options in U-10 changes what the file can hold, unless the type reproduces every key and value it writes today.
- **Recommendation.** A typed options struct that writes the same keys and values, byte for byte, pinned by a round-trip test on today's files. Then it isn't a format change (C3), since nothing in the file changes (proposed, for the Solution Architect).
- **Ruled:** as recommended (25 Nov 2026)[^u-ruled-25nov]. The typing this governs (U-10a) is deferred, so the ruling applies when #289 types the options[^u-v1].

[^q8r]: The Design Lead, 25 Nov 2026, in the platform refactor's coordination session.

[^u-ruled-25nov]: Ruled in the platform programme's coordination session, 25 Nov 2026, by the roles each question names (the Solution Architect: Q1–Q4, Q7, Q9, Q11, Q13–Q15; the Design Lead: Q12; the Product Owner: Q10), all as recommended. **The Product Owner's condition:** before #224-U starts, a lever pass like Stages D and E's ranks its items by value per hour, and the items that only clear a lint line come back to keep, drop, or leave as accepted exceptions with a reason.

[^u-v0]: The Product Owner, 25 Nov 2026 (the lever pass on #224-U, Stage C and Stage D, lever U-V0, as recommended; `build/platform/LEVERS/LEVERS-U-C-D.md` §1.3): Q2's view-local ruling applied to U-18b's estimate; 1–1.5 h dropped.
[^u-v2]: The Product Owner and the Solution Architect, 25 Nov 2026: lever U-V2 (deferring U-11 and U-12) withdrawn, so both reads are built in #224-U: U-11 as Q3 (a) ruled (3.5–4.5 h), U-12 as Q4's interim (c) (3–4.5 h). Moving U-11's read into a store as it is was rejected: the athlete level would then read other sessions' judged rows from a store, which DEC-195 and R-DER-3 forbid. #224-U then closes every line against P11; the three lines U-V1 leaves remain (#289). #290 was closed as not planned.
[^u-q4c]: The Solution Architect, 25 Nov 2026 (`build/platform/LEVERS/FOCUSED-U-V2-C-V1.md` §2; option (c) as an interim, an information-model change C6, approved as PR-21's `readIndexWithSummaries` was): the athlete's repository reads the session- and shot-level facts the scan needs, for the work-ons view. The IM's FILE-SESS-04 and FILE-SESS-05 reader columns, and the lifecycle model's APP-T4 and ATH-T8, are corrected in U-12's commit. (a), one freshness rule held by the athlete level, stays the end state; it is built the next time the athlete level's freshness check changes (#293), and until then today's two staleness tests both stay.
[^u12-q11]: The coordinator's call, 25 Nov 2026, logged for the Product Owner's review: U-12 builds Q4's interim (c), which reads each swing document's stamp through the store exactly as today, so it does not need Q11's read on the Shot. Undo: put Q11 back in U-12's waits (`build/platform/U/PLAN-U.md`, the lever pass's "Kept, U-V2 withdrawn"; `build/platform/LEVERS/NOTES-compliance.md`, "Still open after 78f09b93").
[^u-v1]: The Product Owner, 25 Nov 2026 (the lever pass, lever U-V1, as recommended; re-tagging the three allowlist lines is the Solution Architect's): typing the three big headers (the diagnostics adapter, the work-ons and the report model: U-P1, U-8a to U-8c, U-9a, U-9b, U-10a to U-10c, 27 h, less the 2–4 h U-8q slice kept) is **deferred, with a trigger**: each member is typed when a screen that reads it is next changed, or sooner if #284's freezes trace to the maps' copies. 23–25 h. Its home is #289, which names the three allowlist lines it leaves. U-8q (2–4 h) stays in #224-U: Q13's readout built in C++ as a typed value, and Q12's `expectations` retired.
