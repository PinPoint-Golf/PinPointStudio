# 04 · The refactor plan

**Status (14 Nov 2026):**
- Mark has agreed the overall architecture in `03-target-architecture.md` (draft 2.1, and decisions 1–23). Draft 2.2
  adds his 14 Nov decisions (19–23) and the lead's clarifications for him to read; draft 2.3 adds step 1's
  information-model decisions (24–30). Its elements will be refined step by step.
- This page is how we get there. It replaces the first remediation plan.
- Stage 0, local defect fixes, is committed (below). Nothing structural starts until Mark approves the first
  step's brief.
- Mark's decisions of 14 Nov 2026 are applied here: step 1's brief answered (`build/refactor/step-1/BRIEF.md` §9;
  03 decisions 24–30); the triage of the unscheduled core findings approved as proposed
  (`build/refactor/TRIAGE-PROPOSALS.md`), each marked "(triage, Mark 14 Nov 2026)" below; a new step 8 splits the
  long analysis and coaching functions; the gate catalogues stay code; the metric manifest becomes data in step 7;
  the later review of capture is now called "the platform refactor (capture)" everywhere, and is listed in
  `05-deferred-worklist.md`.

## 1. How the work runs

**For every step:**
1. **A design brief, approved by Mark before anything is built.** It is short and carries the ownership section
   (03 §14a):
   - source, derived, application or view?
   - which owner, at which level?
   - lifetime and identity;
   - inputs, version and invalidating events;
   - persistence and the one writer;
   - the layer of each file.

   It states which findings the step closes, what it deletes, and which allowlist entries go.
2. **Implementation by Opus agents**: one task each, on disjoint files, with an independent reviewer per piece
   who signs off or sends it back. The lead holds the contracts, reviews every diff and runs the builds: at most
   2–3 per piece, bounded at `-j 6`, tests through ctest, only the targets touched.
3. **The parity run** (step G) against the baseline. Every difference is either explained in the step's report
   and accepted by Mark, or treated as a regression.
4. **Mark tests the UI-visible changes** in the running app, from a short list of what to try.
5. **One commit per fix or step, pre-approved for the refactor** (Mark, 14 Nov). It is committed and pushed by the
   lead only when all of these hold:
   - the reviewer has signed off;
   - the touched tests pass (and, from step G, the lint);
   - from step G, there is no unexplained parity difference;
   - only that change's files and hunks are staged;
   - the message is an "Area: …" title in Mark's voice, with no attribution.

   Never committed: library or corpus writes, migrations, allowlist additions, history rewrites (fix forward or
   revert). UI-visible changes are listed on the dashboard "to try". The harness is restored at the end
   (`build/refactor/RESTORE-HARNESS.md`).

**What "done" means for a step:**
- The old path it replaces is **deleted**, not left beside the new one.
- Its allowlist entries are gone. An entry whose code **moves** rather than goes (for example `shot_processor`'s
  device and ring includes, which stay with capture in step 1) is re-tagged, not removed: `# unplanned`, with a WHY
  naming the platform refactor (capture), or `# step N` for the later step that takes it (the lint accepts only
  `# step 1` … `# step 8`, `# unplanned` and `# kept`).
- The docs that describe the area are current.

**Library and corpus re-runs** run on GOLFSIMPC, started and watched by the lead over ssh (the cabin-PC sweep
workflow), with a dry-run first. They are grouped: after steps 1 and 6, plus a regrade after step 3 if the stamps
change. That is about two to three re-runs in the whole refactor. Each one is reported to Mark before its results
are written back.

**HARD FREEZE (Mark, 14 Nov 2026):** no new functionality, anywhere, unless it is explicitly stated in this plan —
"even if I beg". Bug fixes go to Stage 0 or to the step that owns the area. A new idea is written down (a GitHub
issue labelled for after the refactor) and waits. When a request comes close to new functionality, the lead names
the plan line that covers it, or says that none does and holds it. The freeze ends when step 8 is done (Mark extended it from step 7 on 14 Nov 2026, with the commit pre-approval).

### Running unattended (proposed 14 Nov, for Mark to agree)

- **The dashboard.** `build/refactor/DASHBOARD.md` is the living status. It opens with a one-page summary:
  - phase and progress;
  - what is running, and what is next;
  - decisions waiting for Mark, each with its options, consequences and a recommendation;
  - change sets awaiting his test and commit;
  - blockers; the freeze; any library or corpus writes.

  The detail and a log follow below it. It is updated and re-opened on his screen at every material change. A
  fresh session resumes from it.
- **Session limits.** The lead cannot see the usage limit from inside a session. So all state lives in files (the
  plan, the dashboard, each fix's brief, report and reviews), and work is cut into units that survive being stopped
  mid-way. Long jobs run on GOLFSIMPC under a watchdog.
- **Reversible decisions** (uncommitted code, scratch copies, docs): take the safest option, record it on the
  dashboard with its consequences, and continue with independent work.
- **Irreversible actions always wait for Mark:**
  - writes into the live library or corpus;
  - migrations on the share;
  - commits and pushes;
  - anything that would breach the freeze.
- **Stop rules.** Stop that line of work, write it up, and move to independent work on:
  - an unexplained parity difference;
  - a failing test that is not ours;
  - a fix that fails review three rounds running;
  - twice the estimate;
  - disk, memory or build trouble.
- **Briefs approved ahead.** The lead drafts the next two or three step briefs early, so unattended time always has
  approved work queued.
- **Checkpoints:**
  - a dashboard line per fix or step;
  - a plain-language summary at each boundary (Stage 0 done, each step done): what changed, what to test, the
    consequences of each open decision, and the change sets awaiting commit;
  - the session summary when Mark is back.

## 2. Stage 0, adjusted

The rule for keeping a fix: keep the in-flight fixes and the cheap ones with lasting value. A fix that a
structural step will redo is folded into that step, except for a minimal guard where a defect can corrupt data
today.

| # | Fix | Decision | State |
|---|---|---|---|
| 0.1 | Golfer input never lost from `diagnostics.json` (H-04) | **Keep** (Critical) | Committed (5b878a1d) |
| 0.2 | `atomicWrite` on `QSaveFile` (H-05) | **Keep** | Committed (b397c975) |
| 0.4 | A saved corridor edit reaches the app (B-01, part of B-03) | **Keep** | Committed (e0180377) |
| 0.4b | Follow-up to 0.4: `SessionDiagnosticsModel` re-takes the norms at activation; `LmSessionModel` at its grade pass | **Keep**: closes 0.4's definition of done | Committed (d668bb55) |
| 0.5 | The report honours the board's exclusions and club overrides (F-03) | **Keep** | Committed (ab9dd6e0; 0.5b, one golfer name per reading: 9b04fb9b) |
| 0.3 | "Club assumed" demotion (B-07), and `ingestShot` checks its session | **Keep**: a grading defect, small, lasting | Committed (44641cdf) |
| 0.6 | One stored-score reader; the replay's overall score (H-01) | **Keep**: small, lasting | Committed (4fd746ee) |
| 0.12 | One held rule for Your swing and Work-ons (C-06) | **Keep**: Mark's decision, small | Committed (ac5840b2) |
| 0.8g | **Guard only:** no athlete switch or auto-select while capture runs. The live carousel reloads only when the athlete's uuid actually changes. | **Keep the guard** (data can be saved under the wrong athlete today) | Committed (0531235b) |
| 0.7 | The full split between "athlete changed" and "record edited", with listeners moved | **Fold into step 2** (Athlete objects replaced on switch) | — |
| 0.9 | The selection cleared on session change; no basename fallback (E-05) | **Fold into step 4** (`AppState` selection) | — |
| 0.10 | Re-analysis uses the live athlete facts (H-02, A-01) | **Fold into step 1** (the Recording contract) | — |
| 0.11 | The score pill | **Design question**, filed as an issue | — |
| 0.13 | Frequency words never claim more than the share; one rule for Your swing and Tendencies | **Keep** (Mark, 14 Nov) | Committed (e28eff09) |
| 0.14 | Your swing's words say what listed a habit ("…in your latest session"); a set-up can be praised by the same rule as a fault, and for being consistent using the app's existing consistency grade (Mark, 14 Nov: option A; "praise setup, esp. a consistent setup"; "we have consistency grading in the app") | **Keep** (Mark's decisions) | Committed (9d2ac92d) |
| 0.4c | When a session's diagnostics cannot be saved (unopenable file, failed set-aside), say so in the app through the existing notifications, not only in the log (Mark's rule: show with a warning, never hide) | **Keep** (a defect 0.4b exposed) | Committed (0a019783) |

**Stage 0 is committed.** Mark tests the change sets from the dashboard's "to try" list.

## 3. The steps

### Step G · Guardrails (~1 day)

1. **The parity baseline.** A fixed set of sessions from the blessed corpus (proposed: about 12, covering every
   instrument mix and Chris's failure cases). For each, capture today's outputs:
   - the analysis products (summary blocks and key series);
   - the diagnostics ledgers, outcomes and the three factors;
   - tendencies and work-ons;
   - the report figures;
   - key screens, through probes.

   The tools are swinglab, `regrade_ledger`, the report probe and `--probe-qml`. Run on GOLFSIMPC, per the sweeps
   rule. A `parity_diff` script compares any later run against it.
2. **`architecture_lint_test`** (03 §11, rules A1–A8), with today's violations allowlisted and each entry tagged
   with the step that removes it.
3. **`docs/developer/information_model.md`**, written from 03 §0–§9.
4. **The CLAUDE.md information-model rule and the reviewer's ownership check** (03 §14).

**Done when:** the baseline is on the share, the lint is green, and the rule is in CLAUDE.md.

### Step 1 · The Recording contract (3–4 days + the library re-analysis)

The agreed scope is the design brief, `build/refactor/step-1/BRIEF.md`, with Mark's answers in its §9 (14 Nov 2026).
Step 1 makes a shot's analysis a function of one thing, the **Recording**: a shot analysed live and the same shot
re-analysed a year later are built from the same job and cannot disagree; a shot whose club the golfer corrected on
the LM board is re-analysed with the club they really hit; and a swing file has one door (`RecordingRepository`), one
header writer and one serialiser per analysis block. It is cut into work items 1.0–1.7 (brief §6) that each build,
test and pass parity on their own.

- **The Recording** (brief §4.1): the frozen window, the sources by role (one perspective mapping, no magic ints), the
  capture facts, the ball inputs, the IMU bindings, the integrity inputs, each stream's setup, and the **analysis
  context**, built once at the window freeze from a capture-side snapshot. The export writes the same value, so it
  no longer re-reads the athlete and the session 12–37 s later (E-01's mechanism). No rig reference yet: Studio/Rig
  has no identity until the platform refactor (capture); step 1 keeps each stream's setup and leaves a slot.
- **The analysis context recorded** (brief §4.2; folds in Stage 0.10): one schema-tagged `analysisContext` block
  (D3): handedness, height, the **club set** (men / women / junior) as the cohort, the captured club with its geometry,
  length prior and standard length, and a snapshot of the bag's geometry (D2). `capture.club` stays, written from the
  same value, for provenance. A document without the block gets one built in the repository's reader.
- **The effective club** (brief §4.3): the correction is the LM board's override (`review.clubOverride`) only, until
  step 6 (D1); the popover's `review.club` stays the declared club. A corrected club takes its geometry from the bag
  snapshot and its standard length from the club set and height; undeclared stays undeclared, never the stub.
- **One job builder**, `buildAnalysisJob(recording, effective, options)`, pure, in the Rules ring; the job's header
  moves out of `shot_analyzer.h`, so the measurement rules stop including the analyzer interface.
- **One ball-track rule** on both paths: today's re-analysis rule (a valid empty-mat baseline → BallRunner); its live
  cost is measured on GOLFSIMPC (D5).
- **One time domain** (A-03): window-relative in the Recording and on disk (`analysis.timeBase`), rebased once at
  `PipelineAnalyzer::analyze`; old documents are guessed in one place, the repository's reader.
- **One serialiser per analysis block**, beside its producer; **one projection** for the live, replay and reload views
  (`toAnalysisDetail`, `DiskReplaySource`'s walk and `readSwingJson`'s projection deleted).
- **`AnalysisService` and one write-back** (A-09): `integrityFor(recording)` decides the integrity checks from the
  recording, not the current setting; `reanalyseAndPersist` honours "Save pose keypoints" (`PersistOptions`);
  `swinglab_run` calls the service, so its copies of the integrity wiring and the write-back (`--doc-blocks`,
  Step G.2) are deleted.
- **`RecordingRepository`** (brief §4.8): the domain's interface for a shot's swing file, implemented by `SwingStore`;
  every writer in brief §3.4 goes through it, and the `SwingDocWriter` write functions become private to the
  adapter. Capture writes the Recording in stages (`create`, then `completeCapture` for late LM readings and phone
  clips); until step 3 the later writers are today's callers through the repository, not the `Session` (IM-4, IM-5).
- **The input stamp, not a stage-version bump** (D6): `analysis.versions.inputs` (the effective club, its length and
  source, height, club set; `kAnalysisInputsVersion = 1`). No `k*StageVersion` moves, because no stage's code changes.
  The reuse gate reuses the shaft track, the ladder and the DTL club track only when the stamp's club inputs match.
- **After a club correction: stale only** (D9): re-analysed when the golfer asks or by the library sweep.
- **New directories** (D7, IM-6): `src/Recording` (Vocabulary) for the contract's types, `src/DomainModel` for the
  repository interface.
- **`shot_processor`'s two allowlist lines** (D8): the analysis-job half moves out and the lines shrink; what remains
  is re-tagged: the device and ring includes → the platform refactor (capture), the carousel → step 3, the session
  folder → step 2.
- **Not in step 1:** the JSON and `QVariant` re-parsers in Charts, Diagnostics and the LM board stay for steps 5–6
  (brief §4.6); the norm cohort (sex, age band) is not recorded, and whether grading reads it from the Recording is
  step 3's (IM-7); the archive stub moves with the archive in step 2; the "stale stages" view on the sessions screen
  (A-08) is new UI and waits for the freeze to end.
- **The backfill** (D4; Mark, 14 Nov 2026): height and club set written once into the old library's swings from the
  athlete record, so every camera swing's 3-D skeleton is scaled by height. A dry-run report goes to Mark first (per
  swing: what would be written); backups on the share; its own pass, diffed apart from the refactor's parity run.
- **Closes:** H-02, A-01, E-11, the parity half of E-09, A-09 (one write-back path), A-02 (its four converters; the
  re-parsers are steps 5–6's), H-01, E-10, A-03; A-05, A-08, A-14, A-16, A-17, A-18, H-08 (the swing-document half),
  H-13 (triage, Mark 14 Nov 2026).
- **Test:** same Recording + same effective context (the effective club included) → same job, for an uncorrected
  shot (live job == re-analysis job) and for a corrected one (re-analysis uses the corrected club):
  `recording_contract_test`, `recording_store_test`, `analysis_block_roundtrip_test`, `recording_repository_test`, the
  projection key-parity test and the reuse test (brief §6). A live-equals-re-analysis check on three real shots.
- **Re-run:** the parity run against `stageG-aa5aee15` (only the expected differences, brief §5.3); the backfill
  (D4); then one library re-analysis on GOLFSIMPC of every swing without an input stamp, span-bounded, with a dry-run
  list first and backups on the share, run by the lead; Mark judges the results before they are written back.

### Step 2 · Identity and repositories (~1 day)

- **Identity types:** `AthleteId`, `SessionId`, `ShotRef`, `SessionIdentity` (no folder-name parsing anywhere
  else).
- **Repositories:**
  - `AthleteRepository` (`athlete.json`, the folder stored on the record);
  - `SessionRepository`, an identity map (`session_input.json`, `diagnostics.json`, `club_baseline.json`,
    `work_ons.json`, `archived.json`, the report documents);
  - `SessionFiles`/`AthleteFiles`: the one place each session or athlete file name is spelled, used only by the two
    repositories (H-12, lint A4);
  - advisory lock files, so the CLI tools and the app never write one session or athlete folder at once;
  - **one atomic-write helper** (the lead's proposal; Mark agreed 14 Nov 2026): the repositories write through the shared
    `atomicWrite`, and the about 15 files that use `QSaveFile` directly, and the two that write a part file and
    rename, move onto it.
- **The archive as a `Session` command** (the lead's proposal; Mark agreed 14 Nov 2026): `archived.json` and the archive move are
  written through `SessionRepository` in this step (the archiver stops writing the folder itself), and through
  `Session::archive()` once the `Session` exists in step 3.
- **One-off migrations,** each with a dry-run report and backups on the share:
  - the athlete folder recorded on the record;
  - QSettings → `athlete.json`;
  - golfer input out of `diagnostics.json`, into the shot `review` block and `session_input.json`.
- **Folds in 0.7:** athletes are replaced on switch, and an edit is not a switch.
- **Removes the Stage 0.3 stopgap:** `ShotProcessor::startSwingSave` announcing a moved session folder
  (`activeSessionDirChanged`). The `Session` owns its location through the `SessionRepository`; capture asks the
  session where to write; a mid-session library re-point is an explicit application command on the session, emitting
  a domain event that the views bound to `app.session` follow.
- **Decision for Mark in this step's brief:** when the library is re-pointed mid-session (a dropped share), should the
  session continue in the new library (today's behaviour: one session split across two places), or end and start a
  new session there? And can a past intent run be edited after its session ends (decision 20 covers edits during
  the session only)? (triage, Mark 14 Nov 2026)
- **Also in this step** (triage, Mark 14 Nov 2026):
  - a failed `atomicWrite` is surfaced in the app, the way Stage 0.4c reports a blocked `diagnostics.json`;
  - the advisory lock list names the tuning log, `outcome_corrections.jsonl`, which the app and a CLI tool can
    append at once;
  - from step 1's brief (§7): the live club-length prior is learned under the captured club even when the shot is
    later corrected; it is athlete-level learned state held in per-machine settings, so this step's brief places it.
- **Closes:** E-02, E-03, F-06, F-07/E-06, E-07, H-09, H-12, H-04 (fully), H-14, E-01 (the folder half: the
  session owns its folder, so it is never re-resolved per shot), D-08 (one reader and writer of the `diagnostics.json`
  envelope, in `SessionRepository`), H-08 (the session and athlete files' half: schema checked on read); C-03, C-11,
  D-16, F-13, H-10, and the June review's schema string literal ×4 (triage, Mark 14 Nov 2026).

### Step 3 · Session and SessionDiagnostics, by parallel run (3–4 days)

1. **`Session`** (aggregate root, commands and events) and **`SessionDiagnostics`** arrive in
   `src/DomainModel`. **Grading** (Mark, 14 Nov): `SessionDiagnosticsModel::detectShot` becomes the pure
   `gradeShot()` in `src/Diagnostics`. `SessionDiagnostics` grades every shot of its session (each number against its
   corridor, with the session's club baseline and the effective club) and keeps the rows in `diagnostics.json`, one
   writer. The rows are shot-level facts, exposed through the shot (`ShotDiagnostics`, read-only). The analysis
   pipeline never grades; a corridor edit or a club correction regrades without re-analysis.
   - `work_ons.json` is the session summary, written by `SessionDiagnostics` (the lead's clarification, for Mark
     to read).
   - `outcome_corrections.jsonl` is appended by the athlete level on `LabelCorrected`, never by the session (Mark,
     14 Nov).
   - The club baseline is a recorded input: written once at first need, rewritten only when its format is older
     than the current rule (Mark, 14 Nov).
   - `fault_profile.json` is deleted, with its reader (H-11, D-07).
   - **Rule versions are checked:** a golden or parity test fails when a rule's output changes and its version did
     not (C-05).
2. **Parallel run:** behind a feature switch, the new owner computes alongside today's `SessionDiagnosticsModel`
   on every live and opened session, and a diff is logged. The parity run covers the corpus.
3. **Cut-over, view by view:**
   - the stage panel;
   - the cast window;
   - the card pips;
   - the report;
   - the catch-up (via `AthleteDiagnostics`).

   Each cut-over deletes a private model. `SessionMode.sessionDiagnostics` goes last.
4. **Always grade:** every live shot is graded, panel or not.
5. **Praise needs consistency** (Mark, 14 Nov): "What you do well" praises a condition only when it is in range
   AND its consistency grade is Repeatable or Grooved, for every praise, set-ups and faults alike. The grades are
   held by `AthleteDiagnostics` and read by the swing-themes reduction (Stage 0.14 could not reach them: today they
   are computed only in the Home controller, after the themes, and never stored).
6. **A norm's standing is a typed field** (Mark, 14 Nov 2026): provisional or decided, by whom and when, read by
   everyone instead of parsed from prose (B-05). The release dashboard's two misreads (`m_takeawayPlane`,
   `m_clubSlotTop`) are fixed and checked.
7. **Decisions for Mark in this step's brief:** the owner and repository of packs and norms (the user's pack and
   corridor edits, today `<AppData>/diagnostics/user.json` and `user.norms.json`); and where session status and
   start/end times are stored (today they are not persisted). Also for this step's brief: whether grading reads the
   norm cohort (sex, age band) from the Recording (step 1 does not record it; IM-7, Mark 14 Nov 2026); and, from the
   triage (Mark 14 Nov 2026), where `AthleteDiagnostics` caches the athlete baselines and distances (ownership
   question 5) and P3's wording for the derived owners that write files ("the root, or a derived owner inside its
   aggregate").
8. **Also in this step** (triage, Mark 14 Nov 2026):
   - the two-model race on a blocked `diagnostics.json` (the cast resolves the shared notice while the stage panel
     still drops input) goes with the single writer;
   - the work-ons catch-up's write blocks are reported through `AthleteDiagnostics`, not only logged;
   - flight norms and the metric catalogue become immutable snapshots with the packs (03 §8);
   - `Diagnostics/packs` joins lint A5 once its loader is a repository;
   - the pack loader's two long functions (`validatePack`, `loadPack`, allowlist A7) are split with B-11;
   - the pack and corridor editor (`Gui/diagnosticmodel/model_browser.h`, allowlist A6 ×73): it edits and saves the
     user pack and the user norm set (`user.json`, `user.norms.json`), so its writes go with the packs and norms
     owner and repository (item 7, B-02) and its QML API is typed here (the lead's call, 14 Nov 2026).

- **Closes:** D-01…D-07, D-13, H-03, H-11, C-05, G-03/C-01, B-02 (one pack snapshot), B-03 (the pack registries'
  lazy singletons go with the one snapshot; the norm set's half was Stage 0.4), the session half of E-13, B-05 (a norm's standing
  typed; Mark, 14 Nov 2026), and the follow-up noted in 0.5 (Tendencies, Consistency, the kicker and work-on facts honour the board's annotations);
  A-19, B-04, B-11, B-13, B-20, B-22, C-10, C-15, C-16, D-12, D-14, D-15, D-17, D-18, D-19, E-12, E-13 (the athlete
  half), G-08, G-09, G-10, G-14, H-06 (triage, Mark 14 Nov 2026).
- **Re-run:** a regrade, if the stamps change.

### Step 4 · AppState, the selection, the carousel summary (~½ day)

- **`AppState`:**
  - the current athlete;
  - the capture target;
  - the session on screen;
  - the mode;
  - the selections (stage and cast);
  - the replay.

  It lives in `src/App` (Mark, 14 Nov) and replaces `SessionMode`'s focus fields, `CurrentSwing` and the carousel's
  copy. With nothing being captured, the cast selection shows the session on screen (Mark, 14 Nov; 03 decision 22).
  **Decision for Mark in this step's brief:** which shot of that session the cast shows (the newest, or whatever the
  selection holds); decision 22 picks only the session.
- **Folds in 0.9.**
- **No athlete switch while capture runs** (03 §4, an application rule in `AppState`): it replaces Stage 0.8g's guard (E-01).
- **The carousel shot summary**, the request that started this work: strike · direction · flight · carry, for LM
  shots. Its look goes to Mark first if it needs anything new.
- **The QML APIs of the carousel, the replay and the shot controller** are typed with the selection (allowlist A6:
  `shot_list_model.h`, `shot_filter_proxy_model.h`, `shot_replay_controller.h`, `shot_controller.h`) (triage, Mark
  14 Nov 2026).
- **Closes:** E-04, E-05, E-08, E-14, E-01 (the switch half); E-15, H-07 (triage, Mark 14 Nov 2026).

### Step 5 · Charts and the report onto the session (~2 days; can overlap step 3's cut-over)

- **`SessionSeries` per session.** The view-owned `SessionSeriesModel`s go. `SessionSnapshot` reads the owner.
- **One presenter base and one threading idiom** for the six presenters.
- **Typed presenters**, one view at a time; the `QVariantMap` shapes go.
- **The report as a `Session` command** (the lead's proposal; Mark agreed 14 Nov 2026): the report documents are saved through a
  `Session` command and `SessionRepository`, not by the report model directly.
- **QML through `app`:** as each view moves (from step 3's cut-over on), the 42 context globals shrink to devices,
  settings, logging and `app`.
- **No domain rules in QML:** as each view moves to a typed presenter, its rules ("Clean" vs "Solid", shape by
  substring, distances from band words) move into the presenter's typed result.
- **Also in this step** (triage, Mark 14 Nov 2026):
  - **the review views** get typed presenters by the same mechanism: the data viewer stops creating its own
    `SwingDataSource` (allowlist A2), and `swing_data_source.h`, `chart_metrics.h`, `timeline_labels.h`,
    `metric_catalog.h`, `device_filter_model.h`, `session_review_controller.h`, `trust_words.h`,
    `body_pose_adapter.h` and `swing_rig_driver.h` lose their `QVariant` shapes (allowlist A6);
  - **the long report and traces functions are split** as they move onto the session (`SessionReportModel::build`
    1,011 lines; the traces builders 430 and 413);
  - **the report stills** are added to the persistence table, saved through the `Session` command with the report.
- **Closes:** F-01, F-02, F-04, F-08, F-09, D-10, F-12, G-11, D-11; A-02 (the Charts and Diagnostics re-parsers; step 1
  closes its converters, brief §4.6); F-10, F-14 (triage, Mark 14 Nov 2026).

### Step 6 · Measurements unified, flight-check overlay (~1½ days + a regrade)

- **One source-tagged set of measurements per shot.** The LM is a device like any other, and one precedence rule
  decides which source wins. The LM board becomes a presenter.
- **One club per shot:** the effective club (the golfer's correction, else the declared club) is the club for
  grading, the board, every presenter and re-analysis (step 1). The board's private club handling goes.
- **No DRIVER stub for an undeclared club** (the lead's proposal; Mark agreed 14 Nov 2026): capture records an undeclared club as
  undeclared, so it is graded "club assumed" (B-07, G-07), instead of the athlete's primary club or the `"DRIVER"`
  stub, which reads back as a declared driver.
- **The flight check becomes a session overlay** on unchanged rows; the stamps include their norms and thresholds.
- **One direction**, from the pack's corridors.
- **One handedness gloss.**
- **The LM board's print layout** (`src/Gui/session/PpLmGraphicsBody.qml`, embedded in the report's appendix)
  takes its reading names from the one words table (Stage 0.5b), not its own hand-typed list.
- **Closes:** A-04, A-10, F-05/G-01, G-02, G-04, G-06, G-07, C-02; A-02 (the LM board's re-parser; brief §4.6 of
  step 1); G-12, G-13 (triage, Mark 14 Nov 2026).

### Step 7 · Rules down, words, namespaces (1–2 days; can overlap step 5)

- **Domain code moves out of `src/Gui`**, about 5,000 lines (F-11, D-09, C-08).
- **File I/O leaves Coaching and Diagnostics;** norms are loaded by repositories (C-07, B-12, B-06).
- **One words module;** domain reductions persist ids and numbers (C-04, B-10). The report's own frequency words (`everyWords` in
  `report_words.h`) fold into the one rule in `frequency_words.h` (Stage 0.13).
- **One namespace per layer** (C-14, B-15, A-20). `src/Export` is renamed for the Recording contract.
- **Moves the analysis vocabulary down and points dependencies inward:** the stages' value types to the vocabulary,
  so producers stop including the orchestrator (A-11); the Coaching adapter out of Diagnostics, and the band enum
  out of the Wrist ↔ norms cycle (B-08); coach wording out of Metrics into the words module (A-15).
- **The metric catalogue becomes data** (A-06; Mark, 14 Nov 2026): `installMetricManifest` (5,460 lines of table
  written as code, allowlist A7) is replaced by catalogue data, so the three tools stop regex-scraping it, one of
  which builds the release dashboard. The gate catalogues (`appendBodyGates`, `appendClubGates`) stay code, `# kept`
  (Mark, 14 Nov 2026).
- **Also in this step** (triage, Mark 14 Nov 2026):
  - metric keys generated from the catalogue (A-07, with `metric_keys.h` and the `nearestIndex`/`phaseValue`
    duplicates);
  - presentation strings and "no value" conventions out of the rules into the one words module (A-12, B-19, C-13);
  - Diagnostics re-cut by layer (B-09); constants referenced from their rule, not restated (B-16); one phase →
    swing-position mapping in the vocabulary (B-17); one `CoachingBands` place (C-12); shared maths (π, `wrapPi`) in
    the vocabulary;
  - `layoutDag` (852 lines) moves to the UI ring with the pixel layout (B-10); it is split in step 8 in its new home
    (the lead's call, 14 Nov 2026);
  - today's long Gui functions that move down into the rules (for example `SessionDiagnosticsModel::conditionDetail`,
    461 lines) are planned for: split in step 8, or new allowlist lines asked of Mark.
- **Closes:** F-11/D-09/C-08, C-07/B-12/B-06, C-04/B-10, C-14/B-15/A-20, A-11, B-08, A-15; A-06 (Mark, 14 Nov 2026);
  A-07, A-12, B-09, B-16, B-17, B-19, C-12, C-13, and the June review's `metric_keys.h` and π / `wrapPi` rows (triage,
  Mark 14 Nov 2026).

### Step 8 · Split the long analysis and coaching functions (Mark, 14 Nov 2026)

After step 7, so each function is split in its final ring and namespace. Every function over 300 lines in the
Vocabulary and Rules rings (lint A7) is split into named parts, one at a time, **under parity: the diff against the
baseline must show 0 differences** (61 pinned-pose swings, `parity_diff`). It is a refactor of algorithm bodies,
not a change to them: no number, σ, gate, phase or label moves, and no stage version moves. Pose runs are
non-deterministic, so `PoseRunner::run`'s split needs a control run. About 13,000 lines of function body; the
estimate is made in its brief.

| File (allowlist A7, `# step 8`) | Functions and lengths (lines) |
|---|---|
| `Ball/ball_runner.cpp` | `BallRunner::run` 435 |
| `Ball/impact_runner.cpp` | `ImpactRunner::run` 574 |
| `Body/dtl_posture.h` | `buildDtlPosture` 439 |
| `Body/lower_body_metrics.cpp` | `trackLowerBody` 338 |
| `Body/upper_body_metrics.cpp` | `trackUpperBody` 630 |
| `Club/dtl_shaft_decide.cpp` | `dtlSolve` 787 |
| `Club/dtl_shaft_post.cpp` | `dtlPostSolve` 818 |
| `Club/shaft_track_assembly.cpp` | `decideTrack` 1,960, `segmentPhases` 464, `synthesizeLayerC` 306 |
| `Coaching/shot_outcomes.h` | `composeShotOutcome` 387 |
| `Coaching/tendencies.h` | `reduceTendencies` 434 |
| `Diagnostics/engine/characteristic_engine.cpp` | `evaluate` 353 |
| `Diagnostics/engine/dag_layout.cpp` (moved to the UI ring in step 7) | `layoutDag` 852 |
| `Diagnostics/engine/diagnostics_health.cpp` | `diagnosticsHealth` 448 |
| `Diagnostics/engine/relation_resolver.cpp` | `explain` 374 |
| `Diagnostics/measures/measure_sample.cpp` | `buildPhaseGrid` 319 |
| `Kinematics/segment_rates.cpp` | `buildSegmentRates` 761 |
| `Pose/pose_runner.cpp` | `PoseRunner::run` 722 |
| `Skeleton3d/skeleton3d_fit.cpp` | `fitSkeleton` 1,390, `frameResiduals` 646 |

18 lines, 21 functions. `layoutDag` is moved in step 7 (to the UI ring, with B-10) and split here, because B-18 is
the function, not its ring (the lead's call, 14 Nov 2026); once moved, A7 no longer counts it, so its split is held by this list,
not the lint. The other A7 lines are not step 8's: `Diagnostics/packs/characteristic_pack.cpp` is split with the
pack loader in step 3 (B-11); the metric manifest becomes data in step 7 (A-06); the two gate catalogues are kept as
code.

**C-09's multi-job file** (the lead's call, 14 Nov 2026): `Coaching/swing_themes.h` (2,681 lines: an eigensolver, a Jacobi SVD,
Hungarian matching, EM imputation, the athlete reduction, a view model and persistence) is split by job here, under
the same parity rule: the maths into the vocabulary's shared maths, the reduction kept in Coaching, the view model
and the persistence out to the layers steps 3, 5 and 7 give them (step 7's C-07 takes the file I/O out of Coaching).

- **Closes:** B-18 (`layoutDag`, `explain`, `evaluate`), C-09 (the two god functions and `swing_themes.h` split by
  job) (triage, Mark 14 Nov 2026: the triage proposed them for step 7 as a scope addition, with a step of their own as
  its recommendation, which Mark chose; `layoutDag` and `swing_themes.h` in step 8: the lead's call, 14 Nov 2026).

### Later · the platform refactor

The technical and platform-layer refactor that follows this one (Mark, 14 Nov 2026). Its worklist, with the
context for each item, is `05-deferred-worklist.md`. Its **capture** part is a review of transports, devices, the
ring buffer, connectors and calibration, against the Recording contract; Studio/Rig is designed there. The rest is
settings, monitoring and tool APIs, build and test infrastructure, and platform code hygiene.

## 4. Time

These are wall-clock estimates at agent pace, assuming Mark reviews each step within about a day.

| Step | Estimate |
|---|---|
| Stage 0 | committed |
| G | ~1 day |
| 1 | 3–4 days + re-analysis (step 1's brief) |
| 2 | ~1 day |
| 3 | 3–4 days |
| 4 | ~½ day |
| 5 | ~2 days |
| 6 | ~1½ days + regrade |
| 7 | 1–2 days |
| 8 | estimated in its brief |
| **Total** | **about 13–16 working days, plus step 8** |

The 14 Nov triage adds scope to steps 1–7 that these estimates do not yet include; each step's brief re-estimates.

**What limits the pace:** builds and tests on one machine; file overlap in step 3; the hours a GOLFSIMPC re-run
takes (machine time, run by the lead); review rounds; and Mark's testing. It is not the typing.

**How it could be compressed:** steps 5 and 7 can overlap step 3's cut-over, and Mark can review two steps at
once. That gives about 10–13 working days, plus step 8.

## 5. What Mark is asked for

| When | What |
|---|---|
| Now | §1 (how it runs), the hard freeze and the Stage 0 adjustment in §2 — agreed 14 Nov. |
| End of Stage 0 | Test and commit the Stage 0 change sets; commit the audit docs. |
| Each step | Approve its brief; accept or reject each parity difference; test; approve the commit. |
| After each re-run | Judge the re-run's results before they are written back (results before commit). |
