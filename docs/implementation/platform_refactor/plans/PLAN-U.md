# #224-U, the step-5 presenter residue: the plan (25 Nov 2026)

From `docs/implementation/platform_refactor_assessment.md`, "#224 in three groups" and the Rulings of 24 Nov[^po24], and #224's "U" list. Premises checked on main `2ec638d2`: `NOTES-U-plan.md`. A plan only: no briefs yet, nothing built. Every brief will read the standing block (#402) and its own IM excerpt (`python3 tools/method/im_excerpt.py <ids>`). Lane 1 is `build/wt-37c`, lane 2 `build/wt-5b`.

**Four things the rough figure gets wrong** (NOTES §2).
1. **It is 28 lines and 115 findings, not 26 and 113** (B1.T's two re-tags): A2 6 lines and 11 findings, A5 12 and 43, A6 10 and 61.
2. **Three headers are half the work.** The diagnostics adapter, the work-ons and the report hand QML up to 663 distinct map keys (374, 117, 172: an upper bound, the inventories give the real ones), read in 33 and 9 QML files (14,200 lines) and in the report's 29 (7,255 lines; how many read the payload is U-3i's to say). Typing them is 8 runs, with 3 inventories and 2 pins first.
3. **Ten of the 61 A6 findings have no reader in the product's QML** and leave the QML API (U-4). One of them, `spreadOf`, is read by a test probe (`tests/probes/session_spread.qml`), which U-4 re-points or leaves to U-8c.
4. **Three of the doors the A5 reasons name don't exist** (the document's stamp through the Shot, the made report, a Session-level read), so nine of the eleven A5 items wait for a ruling, as the six A2 lines do ("an ownership change", by their own reason). **One line finishes after Stage D** (the report's payload carries the board's `graphics` map, SD-10c).

**The 28 lines, and what clears each.** (U-n is the item; "door" = read through the owning object, P11.)

| File (under `src/Gui/`) | Rule × n | Cleared by | What clears it |
|---|---|---|---|
| `charts/PpChartsPanel.qml` · `diagnostics/PpConsistencyView.qml` | A2 ×5 · ×2 | U-18a | the seven presenters handed out by a C++ door per session and surface, as `diagnosticsAdapters.of` is (Q1) |
| `trust/PpTrustInlineFor.qml` · `PpTrustPanel.qml` · `PpTrustSummaryFold.qml` · `review/PpDataViewer.qml` | A2 ×1 each | U-18b | the same door, or a ruling that a presenter holding only its view's picked shot is view-local (Q2) |
| `diagnostics/session_diagnostics_model.cpp` | A5 ×4 | U-11 | the history read goes through session results, not the other sessions' ledgers and folders (Q3) |
| `diagnostics/work_ons_controller.cpp` | A5 ×7 | U-12 | the scan moves, reading what it reads today, into the athlete's repository (`src/Export`), handed to the controller by the composition root; the path helpers' callers re-pointed (Q4 (c), interim) |
| `markup/markup_truth.cpp` | A5 ×2 | U-15a | the overlay's three document loads go through the review views' door |
| `review/session_review_controller.cpp` | A5 ×4 | U-13 | the review load and the bulk export read through the Session (Q9) |
| `shot/disk_replay_source.cpp` | A5 ×2 | U-14a | the load's streams come from the Shot's `RecordingSource` |
| `charts/charts_delivery_presenter.cpp` | A5 ×2 | U-15b | the strip reads through the Shot, its cache key from the Shot's document stamp (Q11); the test seam takes a parsed document |
| `report/report_mailer.cpp` | A5 ×4 | U-6b | a Session door for the made report (Q5); reveal-in-folder by Q7 |
| `report/report_meta.h` · `report/session_report_model.cpp` | A5 ×3 · ×5 | U-16a | the fingerprint (Q11) and the report read the Session's shots |
| `report/report_stills.cpp` | A5 ×3 | U-16b | the video through the Shot's `RecordingSource`; the stills cache by Q7 |
| `review/swing_data_source.cpp` | A5 ×4 | U-17 | the CSV export by Q7; the stored score's rule leaves the store, or its row goes (Q8) |
| `swing3d/swing_rig_driver.cpp` | A5 ×3 | U-15c | a Session-level read for the session view (Q6); the test seams take parsed documents |
| `diagnostics/session_diagnostics_model.h` | A6 ×29 | U-4 (8), U-8a–c (21) | 8 members with no QML reader leave the QML API; 21 typed |
| `diagnostics/work_ons_controller.h` | A6 ×14 | U-4 (1), U-9a–b (13) | `clubDistance` (tests only) leaves the QML API; 13 typed |
| `print/pdf_document_writer.h` | A6 ×1 | U-4 | `lastPageInfo` (tests only) leaves the QML API |
| `report/report_mailer.h` | A6 ×4 | U-6a | one result value for `compose`, `setPassword`, `send`, `sendTest` |
| `report/session_report_list_model.h` | A6 ×3 | U-7 | `reportCounts`, `clubCounts`, `rowFor` typed |
| `report/session_report_model.h` | A6 ×6 | U-10a (4), U-10b–c (2) | `preflight`, `state` and the options typed; then the payload of `prepared` and `printRequested` |
| `review/chart_metrics.h` | A6 ×1 | U-14b | the kinematic-sequence value typed at its producer |
| `review/session_review_controller.h` · `shot/reanalysis_controller.h` · `shot/shot_list_model.h` | A6 ×1 each | U-5 | two `QStringList`s and one shot-summary value (11 keys) |

**The items** (one agent run each). "B" = Stage B closed, both parts, and B1.20 not running.

| # | Item | After | Lane | Tier | Accept | h |
|---|---|---|---|---|---|---|
| U-0 | The proposals, Q1–Q11 (docs), each with counts and a recommendation | now | docs | coordinator reads | 11 questions; each has "if nothing is ruled" | 1.5 |
| U-1i | Inventory: the adapter's 29 members, every key and its QML readers | now | docs | coordinator, line by line | 374 keys placed; unread keys listed | 2 |
| U-2i | Inventory: the work-ons' 14 members and keys; the scan's reads | now | docs | the same | 117 keys placed; each read has its owner | 1.5 |
| U-3i | Inventory: the report's payload, the print tree's reads, the board's part | now | docs | the same | 172 keys placed; SD-10c's part marked | 1.5 |
| U-P1 | ~~Pin: every live member of the adapter and the work-ons, as a golden shape on the rich fixture (C++)~~ **out of scope: U-V1, #289** | B, U-1i, U-2i | 2 | independent | 2 suites in ctest; two runs equal | 2.5 |
| U-P2 | Pin: the report model's `preflight`, `state` and payload; the review load (neither has a test that builds it) | B, U-3i | 1 | independent | 2 new suites; two runs equal | 3 |
| U-4 | The ten members no QML reads leave the QML API | B | 2 | independent | A6: adapter 29 → 21, work-ons 14 → 13, PDF writer 1 → 0 | 1.5 |
| U-5 | The three one-finding headers typed (7 QML call sites) | B | 1 | independent | 3 lines → 0 | 2.5 |
| U-6a | The mailer's results typed (12 uses, 3 files) | B; not beside SE-7a | 1 | independent | A6 4 → 0 | 1.5 |
| U-7 | The report list typed (15 uses, 3 files) | B; not beside SE-2d | 1 | independent | A6 3 → 0 | 2 |
| U-8a | ~~The adapter typed: this shot (8 members)~~ **out of scope: U-V1, #289** | U-P1, U-4 | 2 | independent | A6 21 → 13; pin equal | 3.5 |
| U-8b | ~~The adapter typed: the session surface (12 members)~~ **out of scope: U-V1, #289** | U-8a | 2 | independent | A6 13 → 1; pin equal | 3.5 |
| U-8c | ~~The adapter typed: `detail`, with its spread and history~~ **out of scope: U-V1, #289** | U-8b | 2 | independent | A6 1 → 0; `tst_session_diagnostics_*` green | 3.5 |
| U-9a | ~~The work-ons typed: the priorities and the summary (7 members)~~ **out of scope: U-V1, #289** | U-P1, U-4 | 2 | independent | A6 13 → 6; pin equal | 3 |
| U-9b | ~~The work-ons typed: tendencies, club scopes, the filter (6 members)~~ **out of scope: U-V1, #289** | U-9a | 2 | independent | A6 6 → 0 | 3 |
| U-10a | ~~The report model typed: `preflight`, `state`, the make options~~ **out of scope: U-V1, #289** | U-P2 | 1 | independent | A6 6 → 2 | 2.5 |
| U-10b | ~~The report's payload typed, but for the board's part: its own sections, and the diagnostics and Charts sections as their typed values~~ **out of scope: U-V1, #289** | U-10a, U-8c | 1 | independent | maps left in the payload: 1 (the board's); the report prints the same | 3.5 |
| U-10c | ~~The board's part of the payload typed~~ **out of scope: U-V1, #289** | U-10b, **SD-10c** | 1 | independent | A6 2 → 0; the report prints the same | 2 |
| U-11 | The history read through session results: a history block in the session summary (FILE-SESS-05), `resultsVersion` 5; "not yet summarised" replaces the panel's unread count (Q3 (a); U-V2 withdrawn) | **Q3**, B | 2 | independent | A5 4 → 0; the same history from a fixture's summaries as from its ledgers today; a summary without the block counted as not yet summarised; the round trip at version 5; **a dry-run count on a scratch copy of the library before any summary is rewritten** | 3.5–4.5 |
| U-12 | The work-ons scan, as it is, into the athlete's repository as a worker-safe read, injected by the composition root through the catch-up's door; the three path helpers' ~35 callers re-pointed and the helpers deleted (Q4 (c), interim; U-V2 withdrawn; `docs/implementation/platform_refactor/rulings/FOCUSED-U-V2-C-V1.md` §2) | **Q4 (c)**, B | 2 | independent | a pin written first: the rows today's scan returns, field for field, equal after the move; a folder with swings but no `session.json`; a summary written outside the process; A5 7 → 0 (allowlist line 56 goes) | 3–4.5 |
| U-13 | The review load and the bulk export through the Session | **Q9**, U-P2, U-5 | 1 | independent | A5 4 → 0 | 3.5 |
| U-14a | The replay's load through the `RecordingSource` | B; not beside SD-8f | 1 | independent | A5 2 → 0; replay tests green | 3 |
| U-14b | The kinematic-sequence value typed | U-14a | 1 | independent | `chart_metrics.h` A6 1 → 0 | 2 |
| U-15a | Markup's overlay through the review views' door (`shot_data_door.h`) | B | 2 | independent | A5 2 → 0 | 2 |
| U-15b | The delivery strip through the Shot | **Q11** | 2 | independent | A5 2 → 0 | 2 |
| U-15c | The 3-D rig's session view through the Session | **Q6, Q11** | 2 | independent | A5 3 → 0 | 3 |
| U-16a | The report's fingerprint and reads through the Session's shots | **Q11**, U-P2, U-10a | 1 | independent | A5 3 → 0 and 5 → 0; same fingerprint | 3.5 |
| U-16b | The stills' video read; the cache | **Q7**, U-16a | 1 | independent | A5 3 → 0; same pixels | 2.5 |
| U-6b | The mailer's PDF through the Session's door | **Q5, Q7**, U-6a | 1 | independent | A5 4 → 0 | 2 |
| U-17 | The data viewer's export and score | **Q7, Q8** | 1 | independent | A5 4 → 0 | 2 |
| U-18a | The Charts and Consistency presenters from a door | **Q1** | 2 | independent | A2 7 → 0; `tst_charts_panel` green | 3 |
| U-18b | The trust views' and the data viewer's presenters | **Q2**, U-18a | 2 | independent | A2 4 → 0 | 1–1.5 (U-V0) |
| U-8q | **New (U-V1):** Q13's readout built in C++ as a typed value, with its own pin; Q12's `expectations` retired | U-4 | 2 | independent | the readout read, not assembled, by the view; `expectations` gone; no untyped member added | 2–4 |
| U-19 | The close: counts re-taken by an agent that built none of it; #224's list; IM §11 | all | alone | n/a | the Accept row, each figure with its command | 2 |

**What Stages B to E do to these files first** (NOTES §3).
- **B1.16** renames `src/Export`: it rewrites the repository include in all twelve A5 files, and the lint's table. **B1.6** and **B1.19** rewrite the adapter's, the work-ons' and the report's includes; **B1.15b** rewrites the charts presenters' and leaves `session_delivery` and `session_posture` in `src/Gui` (U-15b reads the first). **B1.20** (namespaces, 511 files, alone) holds the same slot as this plan: no code item here runs across one of its runs. **B2.16** changes how `qml_ui_test` lists `Gui`'s sources.
- **Stage C** moves or splits none of them. No QML item here runs across a scripted QML rewrite (C-2b, SD-12d, SE-5h, SE-9b).
- **Stage D:** SD-10c types the board's `graphics` map, which the report's payload carries: U-10b follows it. SD-8f moves the live replay's hand-off: U-14a is not beside it, and the second of the two re-checks.
- **Stage E:** `main.cpp` names these presenters on 103 lines (it builds and wires them) and is one lane at a time: U-6b, U-11, U-12, U-13, U-15c, U-16a and U-18a never run beside SE-2a–d, C-4, C-11b.1 or C-14. SE-2d (#234) re-points the report list (U-7); SE-7a takes the mail password (U-6a).

**Lanes.** Lane 1 is the report, review and replay (35.5 h); lane 2 is diagnostics, work-ons, charts, trust, markup and the 3-D rig (40 h); docs 6.5 h, which can start today. Straight after Stage B both lanes also carry Stages C and E, so these are hours of lane, not dates.

| When | Lane 1 (then ready) | Lane 2 (then ready) |
|---|---|---|
| Stage B closed | U-5 (U-6a, U-7, U-14a) | U-4 (U-15a; U-P1 once its inventories are read) |
| The pins | U-P2, then U-10a (U-6a, U-7, U-14a) | U-P1, then U-8a (U-15a, U-9a) |
| The typing | U-14a → U-14b → U-10b (U-6a, U-7) | U-8b → U-8c → U-9a → U-9b (U-15a) |
| After U-0's ruling | U-13, U-16a, U-16b, U-6b, U-17 | U-11, U-12, U-15b, U-15c, U-18a, U-18b |
| After SD-10c | U-10c, then U-19 | — |

Lane 2 has U-4 and U-15a ready at the start and nothing else until U-P1 lands; if U-1i is late it takes U-14a or U-7 from lane 1.

**Review tier.** Independent for every code item: each is a hand edit to a golfer-facing presenter, a cut-over or a deletion; none is a scripted move. The coordinator reads U-0 and checks the three inventories line by line before the typing starts. **Tests first:** U-P1 and U-P2, in C++ (no new `tst_*.qml`). `session_report_model`, `session_review_controller` and `TrustPresenter` have no test that builds them today.

**Accept** (U-19 re-takes each). #224-U's lines 28 → 0 and findings 115 → 0 (A2 11, A5 43, A6 61). Lines tagged `platform refactor (#224)`: 32 at Stage B's close → 4 (Stage D's 3, #224-H's 1). If Stage D has not reached SD-10c when the rest is done: 28 → 1 (`session_report_model.h`, A6 ×2), named on #224. No allowlist line added; a `# kept` re-tag only by Q2's or Q7's ruling. Forecast lever 10 (D-6 here) would add SD-10b's 3 h and Stage D's three lines.

**Effort: about 84 h (59–113 h) for 33 items, against the rough 8 h (8–24).** From scope: 6 A2 lines are 2 runs once ruled; 12 A5 files are 11 runs, one per owner's door (43 findings, of which 19 are repository includes that go only when the last read behind them does); the A6 lines are 1 deletion run (10 findings), 4 small runs (11 findings, 35 call sites) and 8 typing runs for the three big headers (40 findings, up to 663 keys, 71 QML files), which need 3 inventories and 2 pins first because a mistyped key fails silently in QML. At the measured 2.4 h an item it is 79 h. Step 5 typed and re-pointed presenters at about 410,000 tokens a run (13 runs, `build/refactor/LEDGER.csv`), the same size as an item here. Two lanes: **about 42 h of running, 3 days** at 14–16 h a day if both lanes were free, plus the waits for U-0's ruling and SD-10c. **The whole refactor: 392 h → about 468 h.** **Levers:** Q2 ruled "view-local" saves 1.5 h; Q7 ruled "`# kept`" for the three non-domain file uses saves about 3 h; leaving the three big headers typed only where a reader is touched saves 35 h and leaves 3 lines and 40 findings.

**Not yet ruled** (U-0 is written: `docs/implementation/platform_u_presenters.md`, which classes each: 6 HARD for the Solution Architect (Q1, Q2, Q3, Q7, Q9, Q11), 1 HARD for the Design Lead (Q8), 3 coordinator's calls (Q4, Q5, Q6), 1 for the Product Owner (Q10). Where it differs from the list below, it stands.)
- **Information-model changes, for the Solution Architect:** Q1 and Q2, who makes a presenter (C10; the Product Owner too, by the lines' own reasons); Q3, the history through the session summary (growing FILE-SESS-05 beyond DEC-74's fields, C3) or through each session's own diagnostics as DEC-195 does for the scoped views (C6); Q4, the scan's home (C9); Q5, Q6 and Q9, three new Session doors (the made report, the session's shots for the 3-D view, the review load); Q11, the document's stamp (exists, size, modified) read through the Shot, which three items need; Q8, the stored score's rule out of the store (C9).
- **A lint exception, for the Solution Architect:** Q7, the file uses that are no object's persistence (the CSV export to the home folder, the stills cache, reveal-in-folder, test seams): a Platform helper, or `# kept`.
- **For the Product Owner:** Q10, closing at 27 of 28 until SD-10c.
- **Not a change (the coordinator's reading, confirmed in U-0):** typing to P9 (U-4 to U-10, U-14b) and reading through a door that exists (U-14a: `Shot::recordingSource`; U-15a: `shot_data_door.h`) make code match the page.
- **Design system, for the Design Lead:** one, Q8's other half: whether the data viewer's Score row goes, as the three score displays did in #186 (a changed look). Nothing else is planned: typing changes no look. An item whose QML would change a look or need a new component stops and asks the Design Lead; every QML brief carries that rule.

**Ownership (IM §10).** Nothing persisted changes its file, writer or level, unless Q3 grows FILE-SESS-05.

| Thing (ids) | Kind | Owner, level | Persisted; one writer | Ring | How a view reaches it |
|---|---|---|---|---|---|
| The Charts, Consistency and Trust presenters, the data viewer's (P9, lint A2) | view state over derived rows | the Session (proposed, Q1, Q2) | none | Adapters | a door on the session, by surface |
| A condition's history across sessions (DEC-74) | derived | the athlete level, from each Session's results | FILE-SESS-05 if Q3 grows it; the Session | Domain model | through the adapter, typed |
| The work-ons scan and its stamps (J-59) | application state | a read of the athlete's repository (Q4 (c), interim; (a), the athlete level's own freshness rule, is the end state) | none | Adapters (`src/Export`) | the composition root hands the read to `app.workOns` |
| The made report's PDF (FILE-SESS-08) | derived product | the Session; `makeReport` its one writer (unchanged) | as today | Domain model | a Session door (proposed, Q5) |
| The typed values (P9) | view state | their presenter | none | Adapters | properties and list models |

**Cabin.** Every code item runs the full suite through `tools/method/cabin_run.sh` (no hands). No visit: nothing here touches capture, devices or the Windows build's shape. The report items compare a printed PDF before and after on the Mac.

[^po24]: The Product Owner, 24 Nov 2026: #224 in three groups, #224-U a plan of its own straight after Stage B (`docs/implementation/platform_refactor_assessment.md`, "Rulings of 24 Nov"; §9 Q16).

**Added by the coordinator (U-3i's F2):** the session list calls `state` BY NAME through the meta-object (`QMetaObject::invokeMethod` or `property("state")`). Typing `state` in U-10a breaks it silently at run time, and no compiler error shows it. Re-point that call in the same change, with a test that fails if the name is wrong.

**Added 25 Nov (Solution Architect, #273):** the session type steers nothing. Each item that rewrites a file reading the session type removes that file's uses as part of its own work, with no behaviour change, and a parity run shows 0 differences. Session folder names keep their `_Wrist_` word.

**The lever pass of 25 Nov 2026** (the Product Owner; the Solution Architect for the re-tags; `docs/implementation/platform_refactor/rulings/LEVERS-U-C-D.md` §1). **This plan's scope is now:**
- **Out of scope:** U-P1, U-8a, U-8b, U-8c, U-9a, U-9b, U-10a, U-10b, U-10c (U-V1: typing the three big headers when a screen that reads them next changes; 23–25 h; #289). Their three allowlist lines (`session_diagnostics_model.h` A6, `work_ons_controller.h` A6, `session_report_model.h` A6) are re-tagged to #289 at U-19. U-10c no longer waits for SD-10c, so Q10's "close at 27 of 28" no longer applies.
- **Re-sized:** U-18b at 1–1.5 h (U-V0: Q2 ruled view-local; no code moves).
- **Added:** U-8q (2–4 h), the slice of U-8a that keeps Q12's and Q13's rulings in this plan.
- **Kept:** U-15b, U-15c, U-16a (U-V3 not taken: the Shot's stamp read and its three users are built here).
- **Kept, U-V2 withdrawn (the Product Owner and the Solution Architect, 25 Nov 21:56):** U-11 as Q3 (a) ruled, with the dry run (3.5–4.5 h); U-12 as Q4's interim (c) (3–4.5 h). A pure move of U-11 into a store was rejected (it breaks DEC-195 / R-DER-3). U-11 no longer waits for U-8c, nor U-12 for U-9b or Q11 (out of scope, or not needed by (c)). U-12 edits `main.cpp` (one lane at a time). Its commit carries the IM's FILE-SESS-04 and FILE-SESS-05 reader-column edits and the lifecycle model's APP-T4 and ATH-T8. #290 closed as not planned.
- **Accept, re-set:** lines 28 → 3 (the three on #289; every P11 line closed); findings 115 → about 39. No allowlist line added.
- **Effort:** about 84 h → about 58.5–61 h (U-V0 and U-V1 net; U-11 and U-12 at FOCUSED's 6.5–9 h in place of 7 h).
