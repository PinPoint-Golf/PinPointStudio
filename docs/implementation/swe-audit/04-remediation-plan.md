# 04 · The refactor plan

**Status (14 Nov 2026):**
- Mark has agreed the overall architecture in `03-target-architecture.md` (draft 2.1, and decisions 1–30: 19–23 his
  14 Nov decisions in draft 2.2, 24–30 step 1's information-model decisions in draft 2.3).
- **The information model is `docs/developer/information_model.md` (the reference), version 1.7: canonical**; 03 is
  its frozen rationale (Mark, 14 Nov 2026; reference DEC-40). Decisions are cited here by the reference's ids
  (DEC-nn, register §13) and open questions by Q-nn (§14). Mark's decisions on the information-model audit (14 Nov
  2026, its D1–D8 = DEC-40…DEC-47) are applied to the steps below: the session file and `SessionId` (DEC-41, steps 2
  and 3), P3's wording (DEC-42), the tuning log (DEC-43, step 3), the shot kind (DEC-44, step 1), every written file
  listed (DEC-45, steps 2, 3 and 5), the club baseline (DEC-46, step 3); the lead's clarifications are ratified
  (DEC-47).
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
   of the reference, §10: its seven questions (the kind of fact; the owner and level; lifetime and identity; inputs,
   rule version and invalidating events; persistence and the one writer; the ring of each new file; how a view
   reaches it), the ids it touches, and whether it is an information-model change as §1.2 defines it.

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
workflow), with a dry-run first. They are grouped: after steps 1 and 6, plus a re-fire of the findings after step 3 if the stamps
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
3. **`docs/developer/information_model.md`**, written from 03 §0–§9 (canonical since 14 Nov 2026, DEC-40; 03 is its frozen rationale).
4. **The CLAUDE.md information-model rule and the reviewer's ownership check** (03 §14).

**Done when:** the baseline is on the share, the lint is green, and the rule is in CLAUDE.md.

### Step 1 · The Recording contract (3–4 days + the library re-analysis)

The agreed scope is the design brief, `build/refactor/step-1/BRIEF.md`, with Mark's answers in its §9 (14 Nov 2026).
In this step "D1"…"D9" are that brief's choices (reference DEC-31…DEC-39), not the information-model audit's.
Step 1 makes a shot's analysis a function of one thing, the **Recording**: a shot analysed live and the same shot
re-analysed a year later are built from the same job and cannot disagree; a shot whose club the golfer corrected on
the LM board is re-analysed with the club they really hit; and a swing file has one door (`RecordingRepository`), one
header writer and one serialiser per analysis block. It is cut into work items 1.0–1.7 (brief §6) that each build,
test and pass parity on their own.

**Status (14 Nov 2026, item 1.7):** items 1.0–1.6 are committed (`1751cc96`, `3586cd1b`, `d086ceaf`, `186dbf51`,
`108e7858`, `77ff74a7`, `d07ccebb`, `363925e6`); 1.7 closes the code (the loose ends, the backfill tool, the docs). The
Mac half of the parity run at `363925e6` agrees with `stageG-aa5aee15` exactly (the diagnostics and athlete dumps); the
cabin half (the analysis digests) is run separately. Still to do before the step is done: the cabin parity half; the
live-equals-re-analysis check on three real shots (§5.2 step 2: needs Mark's shots); the backfill's real run, after
Mark reads its dry-run report (DEC-34); the library re-analysis; and **the shot kind (DEC-44)**, which no work item
built (reference §11.1, R-REC-8).

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
- **`RecordingRepository`** (brief §4.8): the domain's interface for a shot's swing file, implemented by `SwingStore`
  (its writer contract is set in step 2: after capture, only the `Shot` writes its folder, DEC-86);
  every writer in brief §3.4 goes through it, and the `SwingDocWriter` write functions become private to the
  adapter. Capture writes the Recording in stages (`create`, then `completeCapture` for late LM readings and phone
  clips); until step 3 the later writers are today's callers through the repository, not the `Session` (IM-4, IM-5).
- **The input stamp, not a stage-version bump** (D6): `analysis.versions.inputs` (the effective club, its length and
  source, height, club set, handedness (DEC-75); `kAnalysisInputsVersion = 1`). No `k*StageVersion` moves, because no stage's code changes.
  The reuse gate reuses the shaft track, the ladder and the DTL club track only when the stamp's club inputs match.
  Pose is re-run only when its stage version changes, and is reused whatever pose model file the machine has (Mark,
  14 Nov 2026; DEC-64; item 1.6). The stamp includes handedness (Mark, 14 Nov 2026; DEC-75;
  item 1.6). Whether it also names the session type is the reference's RAT-06, for Mark.
- **The shot kind** (Mark, 14 Nov 2026; DEC-44, added to step 1 with item 1.5): an LM-only shot is a Recording with no
  streams; the Recording gains a shot-kind field (captured or device-only), written by the one header writer; a
  document without it is classified once, in the repository's reader (today's `isDeviceOnly`).
- **After a club correction: stale only** (D9): re-analysed when the golfer asks or by the library sweep.
- **New directories** (D7, IM-6): `src/Recording` (Vocabulary) for the contract's types, `src/DomainModel` for the
  repository interface.
- **`shot_processor`'s two allowlist lines** (D8): the analysis-job half moves out and the lines shrink; what remains
  is re-tagged: the device and ring includes → the platform refactor (capture), the carousel → step 3, the session
  folder → step 2.
- **Not in step 1:** the JSON and `QVariant` re-parsers in Charts, Diagnostics and the LM board stay for steps 5–6
  (brief §4.6); the norm cohort (sex, age band) is not recorded, and whether firing findings reads it from the Recording is
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

- **Before this step's brief** Mark decided the reference's open questions MD-01…MD-12 (§14), from the final re-audit
  (15 Nov 2026; reference v1.8, DEC-130…DEC-140); MD-05 (resume: DEC-134), MD-07 (the index entry: DEC-136) and MD-08
  (the purge: DEC-137) shape this step's repositories.
- **Identity types:** `AthleteId`, `SessionId`, `ShotRef` (in a new `src/Identity/`, Vocabulary: Mark, 15 Nov 2026; DEC-148),
  `SessionIdentity` (no folder-name parsing anywhere else). `RecordingRepository` is re-keyed from `swingDir` to `ShotRef`.
- **The session file** (Mark, 14 Nov 2026; DEC-41): `SessionId` is a uuid minted at Start and stored, with the
  session's status and start/end times, in a session file in its folder, `session.json` (Mark, 15 Nov 2026; DEC-149); the
  folder becomes a property only. `SessionRepository` writes it. A one-off migration (below) gives every existing session a `SessionId`, a
  name-based (v5) uuid of its athlete's id and folder name (Mark, 15 Nov 2026; DEC-150), and records its start and end times. `SessionRepository` and `AthleteRepository` carry
  the identities the step-3 events carry: an event holds only the id of what changed (Mark, 14 Nov 2026; DEC-69).
  Extending today's session (`SwingPaths::beginSession`'s `extendExisting`) keeps the same `SessionId` (Mark, 14 Nov
  2026; DEC-77): `Athlete::resumeSession`, for a session started today and of the same type only (Mark, 15 Nov 2026; DEC-134).
  The session file holds the session type (DEC-88).
- **Deleting an athlete** (Mark, 14 Nov 2026; DEC-79): `Athlete::delete` moves the whole athlete folder, its
  sessions included, to the recoverable trash through `AthleteRepository`, instead of today's `deleteAthlete`, which
  removes only the settings group and orphans the folder.
- **The other files of the reference's §8 go through their repository in this step:** the markup truth files
  (FILE-SHOT-12, shot-owned source facts: Mark, 14 Nov 2026; DEC-83) through `RecordingRepository`; the archive stub
  (FILE-SHOT-10), which today still writes with the public `SwingStore::save`; `regrade_ledger`'s seed and backups
  (FILE-SESS-03, FILE-SESS-12) through `SessionRepository`.
- **The wrist view's reference shot** (SET-17, today a folder path in settings; the reference's Q-21): on the athlete
  record, as a `ShotRef`, one per athlete (Mark, 15 Nov 2026; DEC-152).
- **The `Athlete` aggregate** (Mark, 14 Nov 2026: a named deliverable): the athlete record as a C++ domain object in
  `src/DomainModel`, one instance per `AthleteId` (`AthleteRepository` is an identity map), replaced on an athlete
  switch; it replaces the athlete facts spread over `AthleteController` and QSettings.
- **Repositories:**
  - `AthleteRepository` (`athlete.json`; the athlete's folder is where it is found, no `folder` key: Mark, 15 Nov 2026; DEC-151);
  - `SessionRepository`, an identity map (`session.json`, `session_input.json`, `diagnostics.json`,
    `club_baseline.json`, `work_ons.json`, `archived.json`; the report documents' names now and the report's meta read
    through it, their writes in step 5: Mark, 15 Nov 2026; DEC-156);
  - **`RecordingRepository`'s writer contract** (Mark, 14 Nov 2026; P10, DEC-86): keyed by `ShotRef`, written by capture
    (`create`) and, after it, only by that shot's `Shot` (through its commands); every other caller reads. Until
    step 3 the interim callers (R-REC-3) keep writing through it under this contract;
  - `SessionFiles`/`AthleteFiles`: the one place each session or athlete file name is spelled, used only by the two
    repositories (H-12, lint A4). They cover every file of the reference's §8 (DEC-45), the swing counter
    `.pinpoint-last-swing` (FILE-SESS-10) and the session file among them;
  - advisory lock files, so the CLI tools and the app never write one session or athlete folder at once: short locks
    per write, and a tool refuses a session whose `session.json` says InProgress (`--allow-in-progress` for one a crash
    left) (Mark, 15 Nov 2026; DEC-158);
  - **one atomic-write helper** (the lead's proposal; Mark agreed 14 Nov 2026): the repositories write through the shared
    `atomicWrite`, and the about 15 files that use `QSaveFile` directly, and the two that write a part file and
    rename, move onto it. libppswing's `writeFileAtomic` stays as the store's documented exception (Mark, 15 Nov 2026; DEC-155).
- **The archive as a `Session` command** (the lead's proposal; Mark agreed 14 Nov 2026): `archived.json` and the archive move are
  written through `SessionRepository` in this step (the archiver stops writing the folder itself), and through
  `Session::archive()` once the `Session` exists in step 3.
- **One-off migrations,** each with a dry-run report and backups on the share:
  - (no migration for the athlete folder: it is where `athlete.json` is found, DEC-151);
  - QSettings → `athlete.json` (every key of the reference's SET-01; `lastSessionAt` and `sessionCount` are derived
    from the `SessionIndex`, not carried over);
  - a `SessionId` (v5, DEC-150) and the session file for every existing session (DEC-41);
  - golfer input out of `diagnostics.json`, into the shot `review` block and `session_input.json`, its keys removed
    from `diagnostics.json` only after a check, field by field, that every one is present at its target.
- **Folds in 0.7:** athletes are replaced on switch, and an edit is not a switch.
- **Removes the Stage 0.3 stopgap:** `ShotProcessor::startSwingSave` announcing a moved session folder
  (`activeSessionDirChanged`). The `Session` owns its location through the `SessionRepository`; capture asks the
  session where to write; a mid-session library re-point ends the session and starts a new one (same type and club,
  declared focus carried), so no relocate command or event is needed (Mark, 15 Nov 2026; DEC-154).
- **Decided in this step's brief** (Mark, 15 Nov 2026): a library re-pointed mid-session (a dropped share) ends the session
  and a new one starts there (DEC-154); a past intent run is not edited after its session ends, except by resuming it
  (DEC-159). One session allocator: a standalone launch-monitor shot goes into the running session, and the LM
  controller's second allocator is deleted (DEC-157).
- **Also in this step** (triage, Mark 14 Nov 2026):
  - a failed `atomicWrite` is surfaced in the app, the way Stage 0.4c reports a blocked `diagnostics.json`;
  - the advisory lock list names the tuning log, `outcome_corrections.jsonl`, which the app and a CLI tool can
    append at once;
  - from step 1's brief (§7): the live club-length prior is learned under the captured club even when the shot is
    later corrected; placed by this step's brief as learned rig state, per machine, owner Studio/Rig (Mark, 15 Nov 2026; DEC-153).
- **Closes:** E-02, E-03, F-06, F-07/E-06, E-07, H-09, H-12, H-04 (fully), H-14, E-01 (the folder half: the
  session owns its folder, so it is never re-resolved per shot), D-08 (one reader and writer of the `diagnostics.json`
  envelope, in `SessionRepository`), H-08 (the session and athlete files' half: schema checked on read); C-03, C-11,
  D-16, F-13, H-10, and the June review's schema string literal ×4 (triage, Mark 14 Nov 2026).

### Step 3 · Session and SessionDiagnostics, by parallel run (3–4 days)

1. **`Session`** (aggregate root, commands and events), the **`Shot`** entity it owns (one per `ShotRef`: its
   Recording, review facts and the read-only `ShotDiagnostics`; it replaces the row structs in `ShotListModel` and the
   phone-clip filer as the source of shot facts — Mark, 14 Nov 2026: a named deliverable), **`SessionDiagnostics`** and
   **`AthleteDiagnostics`** (the athlete level's owner: tendencies, work-ons, club baselines and distances — a named
   deliverable of this step) arrive in
   `src/DomainModel`. **Findings** (Mark, 14 Nov 2026; DEC-100, superseding decision 19's "`SessionDiagnostics`
   grades every shot"): `SessionDiagnosticsModel::detectShot` becomes a pure function in `src/Diagnostics`, renamed
   for what it does (e.g. `fireFindings`). Each `Shot` fires its own findings, each number against its corridor, with
   the club baseline, norms and effective club its `Session` passes down. The findings are fired by the `Shot` and kept by its
   `Session` (Mark, 15 Nov 2026; DEC-135), exposed through `ShotDiagnostics` (read-only). `SessionDiagnostics` diagnoses the session from its shots' findings (tiers,
   patterns, links, reasons, per-club mixes, the summary), and may compute and hold the usuals, a session aggregate of
   the shots' readings that is not part of the diagnosis (Mark, 15 Nov 2026; DEC-130). The analysis pipeline never fires findings. A
   corridor or norm edit makes the `Session` ask each shot to re-fire; a club correction re-fires that shot; neither
   re-analyses. Each shot has five layers (the reference's M-SHOT-LAYERS; Mark, 14 Nov 2026; DEC-121): acquired → processed
   and analysed → shot derived data (the outcome, its three factors, carry and the flight-check overlay, computed for the
   shot with the session's usuals passed down) → assessment against the diagnostic graph (DEC-132) → findings. The derived data feeds the
   assessment and is not a finding. The `Shot` posts its derived data and findings, stamped, to its `Session`, which keeps them
   together per shot in `diagnostics.json`, recomputed when their inputs change; readers never recompose them (DEC-124,
   moved by Mark, 15 Nov 2026; DEC-135). In the reference, "assessment" names the shot's layer and the older cross-shot use is
   "diagnosis" (DEC-125).
   - **`WristDiagnosticsModel` retires** (Mark, 14 Nov 2026; DEC-123): there is no separate wrist model. The wrist-pattern
     identification is an analysis of the shot, and its result becomes part of the shot's findings through the one
     diagnostic model. The QML-created model, its own score, findings and strengths, and its view's private
     derivation go.
   - **The `Session` keeps its shots' findings** (Mark, 15 Nov 2026; DEC-135, superseding DEC-102 and moving DEC-124): the
     `Shot` fires its findings and computes its derived data and posts both, stamped; the `Session` keeps them per
     shot as its collection in `diagnostics.json` (FILE-SESS-03), as today. FILE-SHOT-13 retires, so today's rows are
     not moved into shot caches. **Golfer input in `diagnostics.json` must never be lost** (H-04, Stage 0.1): label
     corrections, shot intent and the session input (focus, declared miss, screen answers, intent runs) leave it by
     step 2's migration, to the shot's `review` block and `session_input.json`.
   - **The `Shot` is its own object** (Mark, 14 Nov 2026; P10, DEC-86, DEC-94, DEC-95): it has its own commands for
     its own facts (`rate`, `note`, `correctClub`, `excludeReading`, `correctLabel`, `setShotIntent`,
     `setDeclaredClub`, and the `fireFindings` its `Session` calls), and it is obtainable only from its `Session`. It
     is a QObject child of its `Session`, exposed to QML directly, and reads the snapshot (baseline, pack, norms)
     from its parent (Mark, 15 Nov 2026; DEC-141). `Session::addShot` allocates the swing number, creates the folder through
     `RecordingRepository::create` and returns the `Shot` (DEC-142); `Session::end` on an empty session asks
     `Athlete::trashSession`, and EVT-20 fires (DEC-143).
     After capture it is the only writer of its swing folder, through `RecordingRepository` (replacing "then only by
     its `Session`"). Its commands' events (EVT-02, 04…07, 21, 22) reach the `Session`, which hears them in command
     order and propagates them (re-firing, the summary). The `Session` keeps the collection (`addShot`, `trashShot`,
     swing numbers, order), its context and its session-wide results. Settled (Mark, 14 Nov 2026; DEC-103,
     DEC-104): the `Shot` saves and emits its own changes; `applyAnalysis`, `completeCapture` and the phase grid are
     the `Shot`'s; P3 reads "through their owner's commands". Today's interim writers (R-REC-3) move onto the
     `Shot`'s commands here.
   - `work_ons.json` is the session summary, written by `SessionDiagnostics` (ratified by Mark, 14 Nov 2026;
     DEC-55).
   - `outcome_corrections.jsonl` is a source fact owned by the `Athlete` and appended through the `Athlete` root on
     `LabelCorrected`, never by the session and never by the lazy `AthleteDiagnostics` (Mark, 14 Nov 2026; DEC-43).
   - The club baseline is a recorded input: `AthleteDiagnostics` computes it, the `Session` freezes it at first need,
     and it is rewritten only when the baseline rule's version is newer, as a recorded event naming the sessions it
     read (`ClubBaselineRewritten`, EVT-16) (Mark, 14 Nov 2026; DEC-46).
   - `Session.status` and its start and end times are read and written by the `Session`'s commands in the session
     file step 2 creates (DEC-41); the shot kind is the `Shot`'s, from its Recording (DEC-44).
   - The trash and the archive go through the roots (FILE-LIB-01, FILE-LIB-02, FILE-SHOT-10; DEC-45): a shot by
     `Session::trashShot`, a session by `Athlete::trashSession` (Mark, 14 Nov 2026; DEC-71), which replaces
     `SessionReviewController::trashSession`.
   - **Domain events** (Mark, 14 Nov 2026; DEC-69, DEC-70): delivered synchronously on the owner thread, in command
     order, carrying only the id of what changed. The set is the reference's EVT-01…EVT-23 (EVT-11 removed: DEC-154): `ShotAnnotated` is
     split into `ShotClubCorrected` and `ShotReadingExcluded`, and `SessionStarted`, `SessionArchived`,
     `SessionRestored`, `SessionTrashed`, `ShotDeclaredClubChanged` (marks the analysis stale and re-fires its findings) and
     `ShotCaptureCompleted` (a late LM reading, follow-club or a phone clip: re-fire) are added.
   - **A session left InProgress by a crash** stays InProgress until the golfer ends it or the next Start ends it
     first (Mark, 14 Nov 2026; DEC-73).
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
4. **Always fire:** every live shot fires its findings, panel or not (decision 2; DEC-100).
5. **Praise needs consistency** (Mark, 14 Nov): "What you do well" praises a condition only when it is in range
   AND its consistency grade is Repeatable or Grooved, for every praise, set-ups and faults alike. The grades are
   held by `AthleteDiagnostics` and read by the swing-themes reduction (Stage 0.14 could not reach them: today they
   are computed only in the Home controller, after the themes, and never stored).
6. **A norm's standing is a typed field** (Mark, 14 Nov 2026): provisional or decided, by whom and when, read by
   everyone instead of parsed from prose (B-05). The release dashboard's two misreads (`m_takeawayPlane`,
   `m_clubSlotTop`) are fixed and checked.
7. **Decisions for Mark in this step's brief:** the owner and repository of packs and norms (the user's pack,
   corridor and content edits, today `<AppData>/diagnostics/`, and the disabled norm sets; Q-02). Also for this
   step's brief: whether firing findings reads the norm cohort (sex, age band) from the Recording (step 1 does not record it;
   DEC-30; Q-05); and, from the triage (Mark 14 Nov 2026), where `AthleteDiagnostics` caches the athlete baselines
   and distances (Q-06). Settled since: where session status is stored (DEC-41), P3's wording for the derived
   owners (DEC-42), the event mechanics and the events (DEC-69, DEC-70), who trashes a session (DEC-71), and the
   crash rule (DEC-73). Also for this brief: how a re-fire on a worker reads the `Shot`'s snapshot as an immutable copy and posts
   its results to the GUI thread (the `Shot`'s form is settled: DEC-141), and the memory cost of always-running
   `SessionDiagnostics` in a sweep (Q-18), measured in the parallel run.
   - **Deliverables these decisions add** (Mark, 14 Nov 2026): `Athlete::startSession`, which starts every session and
     first ends any of the athlete's sessions still InProgress (DEC-78, DEC-73); `Athlete::resumeSession`, Ended →
     InProgress → Ended with the same `SessionId`, for a session started today and of the same type only (DEC-77;
     Mark, 15 Nov 2026; DEC-134); housekeeping as the one application service that purges, with one purge command and
     SET-14 off documented as a permanent delete of raw frames, shown with a warning (DEC-137) (the "purged" end state) and auto-archives, through the roots' commands, in place of
     `ArchiveController::runHousekeeping` and `emptyTrash` (DEC-80); the session summary as the one session-level
     product the report renders and the athlete level reads, with its typed result types (links, mixes, reasons: Q-19)
     as a deliverable of this step (DEC-81); the grade policy (SET-15) in the findings stamp, with packs and norms
     (Q-02).
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
- **Re-run:** a re-fire of the findings, if the stamps change.

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
  `Session` command and `SessionRepository`, not by the report model directly. The report renders the session
  summary step 3 defines; it computes none of its figures itself (Mark, 14 Nov 2026; DEC-81). `report_meta.json` mixes
  source facts (the coach's note and the send log: Mark, 15 Nov 2026; DEC-140, T5) with derived ones (the fingerprint,
  the PDF name); this step's brief decides whether to split the file.
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
  - **the report stills** (the reference's FILE-SESS-09, today in the cache location) are saved through the
    `Session` command with the report;
  - **day-level reads of other sessions go through session results** (Mark, 14 Nov 2026; DEC-74): the Charts
    delivery presenter and the report stop reading other sessions' `swing_phasegrid.json` and read their results
    through the `SessionIndex` instead.
- **Closes:** F-01, F-02, F-04, F-08, F-09, D-10, F-12, G-11, D-11; A-02 (the Charts and Diagnostics re-parsers; step 1
  closes its converters, brief §4.6); F-10, F-14 (triage, Mark 14 Nov 2026).

### Step 6 · Measurements unified, flight-check overlay (~1½ days + a re-fire of the findings)

- **One source-tagged set of measurements per shot.** The LM is a device like any other, and one precedence rule
  decides which source wins. The LM board becomes a presenter.
- **One club per shot:** the effective club (the golfer's correction, else the declared club) is the club for
  findings, the board, every presenter and re-analysis (step 1). The board's private club handling goes. Until then
  the analysis and findings can name different clubs when only the popover names one: a known interim, accepted
  until this step (Mark, 14 Nov 2026; DEC-72). When the seed goes, follow-club's move of a seeded `review.club` goes
  too, and follow-club stops overwriting the captured club: it stores the device's club beside it, and the
  effective-club rule prefers that club when follow-club is on (Mark, 15 Nov 2026; DEC-133, superseding DEC-82's rewrite).
- **No DRIVER stub for an undeclared club** (the lead's proposal; Mark agreed 14 Nov 2026): capture records an undeclared club as
  undeclared, so its findings are fired as "club assumed" (B-07, G-07), instead of the athlete's primary club or the `"DRIVER"`
  stub, which reads back as a declared driver.
- **The flight check becomes shot derived data** (Mark, 14 Nov 2026; DEC-122, superseding decision 6's "session-level
  overlay"): computed for the shot with the session's usuals passed down, like the outcome, it feeds the assessment
  that fires the shot's findings; the stamps include their norms and thresholds.
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

**Proposed for this list, for Mark** (the reference's Q-20): a machine-readable form of the information model and a
check that its §11 matches the code. The PPCP import files (FILE-LIB-04, FILE-LIB-05) and the trunk ceremony
recorder's pool-thread write (FILE-SESS-11) are capture's, here.

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
| 6 | ~1½ days + a re-fire of the findings |
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
