# A focused change for U-V2 (25 Nov 2026)

*Proposed, for the Product Owner and the Solution Architect. Nothing here is ruled.* Docs only, on main `81c0f770`. No code was edited, nothing was built, and the lint was not run. The A5 counts below come from reading the lint's rules (`src/Gui/tests/architecture_lint_test.cpp` 47–60, 359–430 and 555–595) and match the allowlist's own counts (`src/Gui/tests/architecture_allowlist.txt:55-56`).

**C-V1 is not covered here.** The Product Owner ruled that the calibration QML moves all or nothing, so C-V1's deferral (`issues/cv1.md`) stands, and the coordinator dropped it from this page's scope.

## Summary

| Item | The focused change | Removes the P11 breach? | Approval | Hours | Recommendation |
|---|---|---|---|---|---|
| U-12, the work-ons' stale-session scan | Move the scan, reading exactly what it reads now, into the athlete's repository (`src/Export`). The composition root injects it into the controller, the way P-3b did | Yes for the reads (A5 7 → 2). 7 → 0 if the three path helpers' callers are re-pointed as well | **Re-rule Q4** (the Solution Architect): a new option (c), as an interim. Status edits to the IM and the lifecycle model | **3–4.5 h**, against the ruled (a)'s 3.5 h planned and 5–7 h likely | **Take**, if the Solution Architect rules (c) |
| U-11, the condition history | **Don't do the "pure move".** It reads the other sessions' `diagnostics.json` from a store, which DEC-195 and R-DER-3 forbid in so many words. The lint would go green while the breach stayed, now hidden in a ring the lint doesn't read | No: it hides the breach rather than removing it | It would need an IM change (C6 and C11: a second exception to R-DER-3) and Q3 re-ruled | 2–3 h for the move; **3.5–4.5 h** for Q3 (a) as ruled | **Keep U-11 deferred** under U-V2's trigger, or build Q3 (a) as ruled now. Not the store move |

**The premise needs correcting.** The brief says a read "behind the owner's door" needs no file-format change, so the Q3 summary block becomes "a later speed-up, not a compliance fix". That holds for U-12 but not for U-11. For U-11 the AthleteRepository is not the owner's door. The file belongs to each other session's `SessionDiagnostics`, and the one ruled route for an athlete-level read of judged rows says "never `diagnostics.json` itself or a store". The summary block *is* the compliance fix. Also, needing no format change does not make a change free of the information model: P-3b itself needed PR-21 and an IM edit.

---

## 1. The rules these changes are judged by

- **P11** (`governance/artefacts/information_model.md`, through `tools/method/im_excerpt.py P11`): "Only the owning object, through its repository, reads or writes its files. Views, rules and other objects reach its facts through the object, never its files or its repository."
- **Lint A5** has two parts. (a) Any `QFile`, `QDir`, `QFileInfo`, … access line outside `src/Export` and the Platform ring counts (lint 47–60, 382–384, 575–590). (b) An `#include` of a repository header from outside `src/Export` and `src/DomainModel` counts, and this includes `DomainModel/athlete_repository` and `session_repository` (lint 387–402, 567–569; the test at 816–819: "a view reaches the object"). **So the presenter cannot call an AthleteRepository method itself.** It must reach it through an object, or through a function the composition root injects.
- **The precedent, P-3b (`c8d387ec`)**, has exactly that shape. The Gui files include only `DomainModel/athlete.h` and `session_roll_up.h` (`gate_count_index.h:30`, `gate_counts.h:42`). `main.cpp:1075-1087` injects a lambda that calls `athleteStore.readIndexWithSummaries(o)` on the worker. That method uses "a session reader of its own" (`src/Export/athlete_store.cpp:324-334`), as `readActivity` does (316–322). `main.cpp` keeps its one `# kept (composition root)` A5 line (allowlist:85, ×8). A lambda that calls a store method is no file access, so that count does not move.
- **R-DER-3 / DEC-195** (the IM): the athlete level "reads session summaries, never raw shots". Its one exception is the scoped views, which read judged rows "through that session's own `SessionDiagnostics` … **never the file, never a store**".
- **FILE-SESS-03** (`diagnostics.json`): its owner is `SessionDiagnostics` and its repository is `SessionRepository`. **FILE-SESS-04** (`club_baseline.json`) already names this reader: "`WorkOnsController` reads it there too". **FILE-SESS-05** (`work_ons.json`, the summary) is read by the athlete level through its `SessionIndex` summaries.

## 2. U-12: the work-ons' stale-session scan

### 2.1 What the code does today
- `WorkOnsController::scan` (`src/Gui/diagnostics/work_ons_controller.cpp:566-605`) and `summaryFingerprintFor` (544–563) run on the controller's pool (526–531). For each subfolder of the athlete folder, the scan reads five things:
  - the swing folders (576), and each swing document's exists, size and modified time (`SwingStore::info`, 552);
  - the frozen club baseline (560, 585–586);
  - the session's day and place (571, 587);
  - the folder's modified time (583);
  - `work_ons.json`'s size and time (591–594), then the summary itself, read strictly (596–601).
- **The A5 ×7** (U-2i §8.1, re-checked by reading the file): the four includes at 48–50 and 52 (`swing_store.h`, `session_diagnostics_files.h`, `session_files.h`, `athlete_files.h`), then 567 (`QDir(athleteDir).entryInfoList`), 568 (`const QFileInfo &fi`) and 591 (`const QFileInfo summaryFile`).
- **What the includes serve outside the scan** (`grep -n 'SessionDiagnosticsFiles::\|SwingStore::\|SessionFiles::\|AthleteFiles::'`):
  - `swing_store.h` and `session_diagnostics_files.h` serve only the scan and `summaryFingerprintFor` (552, 560, 571, 585–587, 596).
  - `session_files.h` (226) and `athlete_files.h` (232–250) also serve the path helpers `recordPath`, `themesPath` and `tendenciesPath` (h:481–487). Their only callers are `work_ons_controller_test.cpp`, about 30 sites, and `tools/diagnostics/athlete_dump.cpp:514-515, 573, 684, 706` (`git grep`).
- `main.cpp:705-748` already hands the controller a catch-up door (`setCatchUp({...})`, with a `cohortFacts` hook at 737–741). That door is the place to inject a new hook.

### 2.2 The change, in plain English
Move the scan, unchanged in what it reads and in the rows it returns, out of the screen-side controller and into the athlete's repository. It becomes a worker-safe read beside `readActivity` and `readIndexWithSummaries`, for example `AthleteRepository::readSessionFreshness(owner, rulesStamp, cohort)`, returning today's `Scanned` rows (*proposed name*). `AthleteStore` implements it in `src/Export` with today's code, using the same folder-keyed `SessionDiagnosticsFiles` and `SwingStore` reads. Those are FILE-SESS-04's and -05's own file door and the Shot's store, so each file is still read through its owner's door. The composition root passes the read to the controller through a new hook on the catch-up door, as P-3b passed the gate count's read. The controller keeps `applyScan` and every duty in U-2i §8.3: the catch-up's order, the baseline-only asks, newer-build sessions never asked, outside writes heard through the summary stamps, and folder-to-path spelling. It just stops reading files.

### 2.3 Does it meet P11 and A5 as worded?
- **A5:** 7 → 2 by reading the lint. The three access lines and the two scan-only includes go. `session_files.h` and `athlete_files.h` stay while the path helpers stay. To reach **7 → 0**, re-point about 35 call sites (in the test and `athlete_dump`) to `SessionFiles::workOnsFile` and `AthleteFiles::themesFile` / `tendenciesFile` directly, and delete the helpers. They spell paths and read nothing, so this is a lint item, not a P11 one. Allowlist line 56 then goes.
- **P11:** the presenter no longer reads another object's files or includes a repository. The reads sit in the athlete's repository, through the session's and shot's own file doors. That is the same level as PR-21's approval of `readIndexWithSummaries`: a session reader of its own, not the owning object instance. Every fact it reads is one the athlete level may read today:
  - summaries, under R-DER-3;
  - the club baseline, which FILE-SESS-04 names and R-DER-3 makes the athlete level's to compute;
  - the shot's document stamp, which `AthleteDiagnostics` already computes through `SessionDiagnostics::summaryFingerprintNow` (`src/DomainModel/athlete_diagnostics.cpp:540-556`);
  - file times, which are no IM fact (U-2i §8.2, R3 and R9).

  No ruling's words forbid it.
- **But it is not Q4 as ruled, and it is not a way round the lint only if it is ruled.** Q4 ruled (a), that the athlete level says which sessions are stale and "its own scan … goes". It rejected (b) because (b) "only moves the file reads to a ring where A5 still counts them". This change is (b) moved into the one ring A5 doesn't read. That is exactly the move Q7 warns becomes "a way round the lint" unless it is a named, ruled door. U-2i's five missing facts (§8.2) had already sent Q4's stop to the Solution Architect.
- **So the ask is:** the Solution Architect re-rules Q4 to add **(c): the scan as it is, as a worker-safe read of the athlete's repository, injected by the composition root, as an interim**, with (a) kept as the end state under U-V2's trigger. "When in doubt, it is a change", so treat it as C6: the athlete's repository reads session- and shot-level facts for a view. It is approved the way PR-21 was, with the IM's reader columns corrected in the same commit (*proposed, for the Solution Architect*).

### 2.4 Lifecycle model
- APP-T4 and ATH-T8 (`governance/artefacts/lifecycle_model.md:58, 479`) name "the work-ons scan" and cite `src/Gui/main.cpp:705-748`. Their wording and line citations need a status edit. The transitions themselves don't change.
- GU-26 (666: the catch-up writes only derived files) is unaffected, because no write moves.
- The duty `athlete_diagnostics.h:71-72` relies on ("a summary written outside this process … the adapter's scan says so") is kept: R9's stamps still reach `applyScan`.
- No gap row in the lifecycle model names the scan, the summary stamp or the history (`grep -n -i` on "gap N" rows for scan, summary, stale, catch, history: none). #285 places no gap on #224-U (LEVERS §1.1).

### 2.5 Files
- `src/DomainModel/athlete_repository.h`: one virtual, with an empty default like `readIndexWithSummaries`.
- `src/Export/athlete_store.{h,cpp}`: the moved `scan` and `summaryFingerprintFor`, and the `Scanned` value moved to a value header both sides can include.
- `src/Export/athlete_cache_repository.h`: the stub.
- `src/Gui/main.cpp`: the hook. This is the one-lane-at-a-time file (PLAN-U "Stage E").
- `src/Gui/diagnostics/work_ons_controller.{h,cpp}`.
- `src/Diagnostics/tests/work_ons_controller_test.cpp`; `src/Diagnostics/tests/diag_stage_panel_test.cpp:307` (the mirror's comment); `tools/diagnostics/athlete_dump.cpp` (only for 7 → 0).
- `src/Gui/tests/architecture_allowlist.txt`: line 56 goes.
- The IM's FILE-SESS-04 and FILE-SESS-05 reader columns: status edits under the ruling, together with Q14's correction to FILE-SESS-05.
- The lifecycle model's APP-T4 and ATH-T8.

### 2.6 Hours
**3–4.5 h**: about 3 h for the move, the hook and the re-pointed tests, plus 0.5–1 h for the path helpers' 35 call sites if 7 → 0 is wanted. This is an estimate from the shape. Compare PLAN-U's 3.5 h for U-12 as ruled, which the lever pass puts at 5–7 h likely (LEVERS §1.1 rank 21; U-V2's 8.5–10.5 h in all).

### 2.7 Tests
- **A new pin, written before the move:** on `work_ons_controller_test`'s fixtures, the rows today's `scan` returns are recorded. After the move, the repository read must return the same rows field for field: fingerprint, `fresh`, `newer`, `hasBaseline`, summary size and time, folder time, day and place.
- **Two fixture cases U-2i flags:** a folder with swings but no `session.json` (R2: the listing stays inside the read, so it must still be found), and a summary written outside the process (R9: `markSummaryStale` still fires).
- **The suites that run:** `work_ons_controller_test` (re-pointed, `setSynchronous` kept), `diag_stage_panel_test`, `architecture_lint_test` (A5 for the file 7 → 0, or 7 → 2 with a reason), `store_isolation_lint_test`. Through `ctest`, under the build lock.

### 2.8 What stays deferred
Q4 (a), "one freshness rule, the athlete's", stays deferred. Today's two predicates both stay: the scan's "fingerprint moved" test and `notSummedUp()`'s `!(read && hasFacts)` (`athlete_diagnostics.cpp:428`; U-2i R10). Trigger: U-V2's existing one, the next change to the athlete level's freshness check (`issues/uv2.md`).

## 3. U-11: the condition history

### 3.1 What the code does today
`SessionDiagnosticsModel::readHistory` (`src/Gui/diagnostics/session_diagnostics_model.cpp:3119-3185`) runs off the screen's thread (`scanHistory`, 3187–3208):
- it lists the athlete folder's subfolders (3138);
- it reads each other session's `diagnostics.json` through `SessionDiagnosticsFiles::readDiagnostics` (3147);
- it parses the ledger with `fromJson` and `rowReadings` (`src/Coaching/diagnostic_ledger.h:2433, 662`);
- it keeps four fields per reading (3177; `HistoryReading`, h:1019).

**The A5 ×4:** the include at 35, plus 3130 (`QFileInfo self`), 3133 (`QDir athlete`) and 3141 (`QFileInfo fi`). Every `SessionDiagnosticsFiles::` use is inside `readHistory` (3136, 3137, 3147, 3163), so the include could go.

### 3.2 The "pure move" and why it is not the fix
Mechanically it works. `readHistory` moves into `AthleteStore` and is injected like P-3b's read. The parsers are header-only in Coaching, and the two structs move to a value header. A5 would go 4 → 0, in about 2–3 h. **But the move reads each other session's FILE-SESS-03 judged rows from a store.** R-DER-3's only exception for an athlete-level read of judged rows says "through that session's own `SessionDiagnostics` … never the file, never a store" (DEC-195). So the change would:
- **break a ruling's words**. That is an IM change, C6 (a level reading another level's judged rows by a new route) and C11 (a second exception to R-DER-3), and it needs Q3 re-ruled. It is neither Q3 (a) nor (b): (b) at least went through each Session's object;
- **hide the breach from the lint** (`src/Export` is exempt from A5), where today it is a visible, reasoned allowlist line. That is the outcome Q7 says a door must not be.

Whatever a door looks like, it reads the same file by the same route, so it isn't behind the owner's door in P11's sense.

### 3.3 The smallest change that does remove it: Q3 (a), as ruled
The approved design is a history block in each session's summary (FILE-SESS-05) with the same four fields per shot and condition. The adapter reads those through the athlete level's summaries, and "not yet summarised" replaces today's "%1 sessions not yet read by the panel" caption (3450–3452). The ruling's cost is `resultsVersion` 5, so the catch-up rewrites every summary once. That is a rewrite of derived files only, and GU-26 holds, but U-11's brief takes a dry-run count on a scratch copy first.
- **A P-3b-style "no bump" variant doesn't work.** P-3b avoided its bump by falling back to the old read for sessions without the block. Here that fallback is the breach, so the line would stay. Without a fallback, older sessions drop out of the history until something re-summarises them.
- **Files:**
  - the summary's writer, `SessionDiagnostics` (`src/DomainModel/session_diagnostics.cpp`);
  - `src/Coaching/session_results.h` (`kSessionResultsVersion`, 510 and 617);
  - the summary's reader in `SessionIndexSummary`, through `deferredSummary`;
  - `session_diagnostics_model.{h,cpp}`;
  - the allowlist's line 55;
  - the IM's FILE-SESS-05, a C3 change already approved by Q3's ruling.
- **Hours:** PLAN-U's 3.5 h (U-11), plus up to 1 h for the dry run and the caption, so **3.5–4.5 h**.
- **Tests:**
  - `session_diagnostics_model_test` (15 history mentions): the same history from a fixture's summaries as from its ledgers today, which is PLAN-U's own check, "the same history on a fixture";
  - a summary without the block, counted as "not yet summarised";
  - the summary's round trip at version 5;
  - `architecture_lint_test` (A5 4 → 0);
  - the dry-run count, before anything is written.
- **Approval:** none new. Q3 (a) is ruled and it is C3 as approved. The caption's new words may want the Design Lead's lighter route (UI-20); that is not checked here.

### 3.4 What stays deferred if U-11 isn't built now
Line 55 (A5 ×4) stays as a visible exception to P11, with U-V2's trigger: the next `resultsVersion` bump, which rewrites the summaries anyway (`issues/uv2.md`).

## 4. Recommendation
- **U-12: take the focused change (3–4.5 h)** if the Solution Architect rules Q4 (c) as an interim (*proposed, for the Solution Architect*). It removes the screen's file reads with no format change, and it is cheaper than (a) at 5–7 h likely.
- **U-11: don't take the pure move.** Keep it deferred under U-V2's trigger, or, if the Product Owner wants the breach gone now, build Q3 (a) as ruled (3.5–4.5 h, with a library-wide summary rewrite after a dry run). Between those two, the deferral is the better value unless a `resultsVersion` bump is already coming.
- **If both happen:** U-V2 recovers 3–4.5 h of the 7 h, and the 8.5–10.5 h with growth becomes 3–4.5 h now plus 3.5–4.5 h at the trigger.
