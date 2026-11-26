# 04 · The refactor plan

**Status (14 Nov 2026):**
- The Solution Architect has agreed the overall architecture in `03-target-architecture.md` (draft 2.1, and decisions 1–30: 19–23 his
  14 Nov decisions in draft 2.2, 24–30 step 1's information-model decisions in draft 2.3).
- **The information model is `governance/artefacts/information_model.md` (the reference), version 1.7: canonical**; 03 is
  its frozen rationale (Solution Architect, 14 Nov 2026; reference DEC-40). Decisions are cited here by the reference's ids
  (DEC-nn, register §13) and open questions by Q-nn (§14). The Solution Architect's decisions on the information-model audit (14 Nov
  2026, its D1–D8 = DEC-40…DEC-47) are applied to the steps below: the session file and `SessionId` (DEC-41, steps 2
  and 3), P3's wording (DEC-42), the tuning log (DEC-43, step 3), the shot kind (DEC-44, step 1), every written file
  listed (DEC-45, steps 2, 3 and 5), the club baseline (DEC-46, step 3); the lead's clarifications are ratified
  (DEC-47).
- This page is how we get there. It replaces the first remediation plan.
- Stage 0, local defect fixes, is committed (below). Nothing structural starts until the Product Owner approves the first
  step's brief.
- The Product Owner's decisions of 14 Nov 2026 are applied here: step 1's brief answered (`build/refactor/step-1/BRIEF.md` §9;
  03 decisions 24–30); the triage of the unscheduled core findings approved as proposed
  (`build/refactor/TRIAGE-PROPOSALS.md`), each footnoted below as the triage's; a new step 8 splits the
  long analysis and coaching functions; the gate catalogues stay code; the metric manifest becomes data in step 7;
  the later review of capture is now called "the platform refactor (capture)" everywhere, and is listed in
  `05-deferred-worklist.md`.

## 1. How the work runs

**For every step:**
1. **A design brief, approved by the Product Owner before anything is built.** It is short and carries the ownership section
   of the reference, §10: its seven questions (the kind of fact; the owner and level; lifetime and identity; inputs,
   rule version and invalidating events; persistence and the one writer; the ring of each new file; how a view
   reaches it), the ids it touches, and whether it is an information-model change as §1.2 defines it.

   It states which findings the step closes, what it deletes, and which allowlist entries go.
2. **Implementation by Opus agents**: one task each, on disjoint files, with an independent reviewer per piece
   who signs off or sends it back. The lead holds the contracts, reviews every diff and runs the builds: at most
   2–3 per piece, bounded at `-j 6`, tests through ctest, only the targets touched.
3. **The parity run** (step G) against the baseline. Every difference is either explained in the step's report
   and accepted by the Product Owner, or treated as a regression.
4. **The Product Owner tests the UI-visible changes** in the running app, from a short list of what to try.
5. **One commit per fix or step, pre-approved for the refactor**[^m14nov]. It is committed and pushed by the
   lead only when all of these hold:
   - the reviewer has signed off, for a step whose Review column says yes;
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
change. That is about two to three re-runs in the whole refactor. Each one is reported to the Product Owner before its results
are written back.

**HARD FREEZE:**[^m14nov] no new functionality, anywhere, unless it is explicitly stated in this plan —
"even if I beg". Bug fixes go to Stage 0 or to the step that owns the area. A new idea is written down (a GitHub
issue labelled for after the refactor) and waits. When a request comes close to new functionality, the lead names
the plan line that covers it, or says that none does and holds it. The freeze ends when step 7 is done[^m14nov-ext][^po23nov].

### Running unattended (proposed 14 Nov, for the Product Owner to agree)

- **The dashboard.** `build/refactor/DASHBOARD.md` is the living status. It opens with a one-page summary:
  - phase and progress;
  - what is running, and what is next;
  - decisions waiting for the Product Owner, each with its options, consequences and a recommendation;
  - change sets awaiting his test and commit;
  - blockers; the freeze; any library or corpus writes.

  The detail and a log follow below it. It is updated and re-opened on his screen at every material change. A
  fresh session resumes from it.
- **Session limits.** The lead cannot see the usage limit from inside a session. So all state lives in files (the
  plan, the dashboard, each fix's brief, report and reviews), and work is cut into units that survive being stopped
  mid-way. Long jobs run on GOLFSIMPC under a watchdog.
- **Reversible decisions** (uncommitted code, scratch copies, docs): take the safest option, record it on the
  dashboard with its consequences, and continue with independent work.
- **Irreversible actions always wait for the Product Owner:**
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
  - the session summary when the Product Owner is back.

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
| 0.12 | One held rule for Your swing and Work-ons (C-06) | **Keep**: small[^m-012] | Committed (ac5840b2) |
| 0.8g | **Guard only:** no athlete switch or auto-select while capture runs. The live carousel reloads only when the athlete's uuid actually changes. | **Keep the guard** (data can be saved under the wrong athlete today) | Committed (0531235b) |
| 0.7 | The full split between "athlete changed" and "record edited", with listeners moved | **Fold into step 2** (Athlete objects replaced on switch) | — |
| 0.9 | The selection cleared on session change; no basename fallback (E-05) | **Fold into step 4** (`AppState` selection) | — |
| 0.10 | Re-analysis uses the live athlete facts (H-02, A-01) | **Fold into step 1** (the Recording contract) | — |
| 0.11 | The score pill | **Design question**, filed as an issue | — |
| 0.13 | Frequency words never claim more than the share; one rule for Your swing and Tendencies | **Keep**[^m14nov] | Committed (e28eff09) |
| 0.14 | Your swing's words say what listed a habit ("…in your latest session"); a set-up can be praised by the same rule as a fault, and for being consistent using the app's existing consistency grade | **Keep**[^m14nov-014] | Committed (9d2ac92d) |
| 0.4c | When a session's diagnostics cannot be saved (unopenable file, failed set-aside), say so in the app through the existing notifications, not only in the log (show with a warning, never hide)[^mrule-warn] | **Keep** (a defect 0.4b exposed) | Committed (0a019783) |

**Stage 0 is committed.** The Product Owner tests the change sets from the dashboard's "to try" list.

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
3. **`governance/artefacts/information_model.md`**, written from 03 §0–§9 (canonical since 14 Nov 2026[^dec40]; 03 is its frozen rationale).
4. **The CLAUDE.md information-model rule and the reviewer's ownership check** (03 §14).

**Done when:** the baseline is on the share, the lint is green, and the rule is in CLAUDE.md.

### Step 1 · The Recording contract (3–4 days + the library re-analysis)

The agreed scope is the design brief, `build/refactor/step-1/BRIEF.md`, with the Solution Architect's answers in its §9[^m14nov].
In this step "D1"…"D9" are that brief's choices[^dec31-39], not the information-model audit's.
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
the Product Owner reads its dry-run report[^dec34]; the library re-analysis; and **the shot kind**[^dec44], which no work item
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
  (its writer contract is set in step 2: after capture, only the `Shot` writes its folder)[^dec86];
  every writer in brief §3.4 goes through it, and the `SwingDocWriter` write functions become private to the
  adapter. Capture writes the Recording in stages (`create`, then `completeCapture` for late LM readings and phone
  clips); until step 3 the later writers are today's callers through the repository, not the `Session` (IM-4, IM-5).
- **The input stamp, not a stage-version bump** (D6): `analysis.versions.inputs` (the effective club, its length and
  source, height, club set, handedness[^dec75]; `kAnalysisInputsVersion = 1`). No `k*StageVersion` moves, because no stage's code changes.
  The reuse gate reuses the shaft track, the ladder and the DTL club track only when the stamp's club inputs match.
  Pose is re-run only when its stage version changes, and is reused whatever pose model file the machine has
  (item 1.6)[^dec64]. The stamp includes handedness (item 1.6)[^dec75]. Whether it also names the session type is the reference's RAT-06, for the Solution Architect.
- **The shot kind** (added to step 1 with item 1.5)[^dec44]: an LM-only shot is a Recording with no
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
- **The backfill** (D4)[^m14nov]: height and club set written once into the old library's swings from the
  athlete record, so every camera swing's 3-D skeleton is scaled by height. A dry-run report goes to the Product Owner first (per
  swing: what would be written); backups on the share; its own pass, diffed apart from the refactor's parity run.
- **Closes:** H-02, A-01, E-11, the parity half of E-09, A-09 (one write-back path), A-02 (its four converters; the
  re-parsers are steps 5–6's), H-01, E-10, A-03; A-05, A-08, A-14, A-16, A-17, A-18, H-08 (the swing-document half),
  H-13[^m14nov-triage].
- **Test:** same Recording + same effective context (the effective club included) → same job, for an uncorrected
  shot (live job == re-analysis job) and for a corrected one (re-analysis uses the corrected club):
  `recording_contract_test`, `recording_store_test`, `analysis_block_roundtrip_test`, `recording_repository_test`, the
  projection key-parity test and the reuse test (brief §6). A live-equals-re-analysis check on three real shots.
- **Re-run:** the parity run against `stageG-aa5aee15` (only the expected differences, brief §5.3); the backfill
  (D4); then one library re-analysis on GOLFSIMPC of every swing without an input stamp, span-bounded, with a dry-run
  list first and backups on the share, run by the lead; the Product Owner judges the results before they are written back.

### Step 2 · Identity and repositories (~1 day)

**Status (15 Nov 2026, item 2.8b, the close):** items 2.0–2.7b and 2.8a are committed (`528aaaf1`, `d0292bba`,
`7efab713`, `640c1bf5`, `b5c0e6f8`, `1a5e6dfe`, `3c5da388`, `6a5b328c`, `45e32f36`, `78efbf74`); 2.8b closes the code
(the swing file's writes under the session's lock, the archive's per-swing re-check, one reader of `athlete.json`,
lint A4 over every session and athlete file name, J-18's inferred end, the report's athlete by its folder only, and
every writing tool refusing a session in progress unless `--allow-in-progress`: D11)[^dec158].
Parity, the Mac half (`build/refactor/step-2/parity-step2-vs-G.md`): run A (unmigrated copies) and run B (M1–M3
written on scratch copies first) both agree with `stageG-aa5aee15` and with step 1's closing run exactly; the expected
set of run B was empty. Still to do before the step is done: the cabin PC's dry run and export, the Solution Architect's id picks
(answered — each athlete keeps the id its swings record)[^j58] and the **real migrations** on the Mac and the cabin
PC, each after the Product Owner reads its dry run (`build/refactor/step-2/MIGRATION-DRY-RUN.md`); and J-55's
`PpTypeset.shotNo` sheets (a design-system change, waiting for the Design Lead).

- **Before this step's brief** the reference's open questions MD-01…MD-12 (§14), from the final re-audit, were
  decided[^dec130-140]; MD-05 (resume)[^dec134], MD-07 (the index entry)[^dec136] and MD-08
  (the purge)[^dec137] shape this step's repositories.
- **Identity types:** `AthleteId`, `SessionId`, `ShotRef` (in a new `src/Identity/`, Vocabulary)[^dec148],
  `SessionIdentity` (no folder-name parsing anywhere else). `RecordingRepository` is re-keyed from `swingDir` to `ShotRef`.
- **The session file**[^dec41]: `SessionId` is a uuid minted at Start and stored, with the
  session's status and start/end times, in a session file in its folder, `session.json`[^dec149]; the
  folder becomes a property only. `SessionRepository` writes it. A one-off migration (below) gives every existing session a `SessionId`, a
  name-based (v5) uuid of its athlete's id and folder name[^dec150], and records its start and end times. `SessionRepository` and `AthleteRepository` carry
  the identities the step-3 events carry: an event holds only the id of what changed[^dec69].
  Extending today's session (`SwingPaths::beginSession`'s `extendExisting`) keeps the same `SessionId`[^dec77]:
  `Athlete::resumeSession`, for a session started today and of the same type only[^dec134].
  The session file holds the session type[^dec88].
- **Deleting an athlete**[^dec79]: `Athlete::delete` moves the whole athlete folder, its
  sessions included, to the recoverable trash through `AthleteRepository`, instead of today's `deleteAthlete`, which
  removes only the settings group and orphans the folder.
- **The other files of the reference's §8 go through their repository in this step:** the markup truth files
  (FILE-SHOT-12, shot-owned source facts)[^dec83] through `RecordingRepository`; the archive stub
  (FILE-SHOT-10), which today still writes with the public `SwingStore::save`; `regrade_ledger`'s seed and backups
  (FILE-SESS-03, FILE-SESS-12) through `SessionRepository`.
- **The wrist view's reference shot** (SET-17, today a folder path in settings; the reference's Q-21): on the athlete
  record, as a `ShotRef`, one per athlete[^dec152].
- **The `Athlete` aggregate** (a named deliverable)[^m14nov]: the athlete record as a C++ domain object in
  `src/DomainModel`, one instance per `AthleteId` (`AthleteRepository` is an identity map), replaced on an athlete
  switch; it replaces the athlete facts spread over `AthleteController` and QSettings.
- **Repositories:**
  - `AthleteRepository` (`athlete.json`; the athlete's folder is where it is found, no `folder` key)[^dec151];
  - `SessionRepository`, an identity map (`session.json`, `session_input.json`, `diagnostics.json`,
    `club_baseline.json`, `work_ons.json`, `archived.json`; the report documents' names now and the report's meta read
    through it, their writes in step 5)[^dec156];
  - **`RecordingRepository`'s writer contract** (P10)[^dec86]: keyed by `ShotRef`, written by capture
    (`create`) and, after it, only by that shot's `Shot` (through its commands); every other caller reads. Until
    step 3 the interim callers (R-REC-3) keep writing through it under this contract;
  - `SessionFiles`/`AthleteFiles`: the one place each session or athlete file name is spelled, used only by the two
    repositories (H-12, lint A4). They cover every file of the reference's §8[^dec45], the swing counter
    `.pinpoint-last-swing` (FILE-SESS-10) and the session file among them;
  - advisory lock files, so the CLI tools and the app never write one session or athlete folder at once: short locks
    per write, and a tool refuses a session whose `session.json` says InProgress (`--allow-in-progress` for one a crash
    left)[^dec158];
  - **one atomic-write helper**[^lead-proposal-m14nov]: the repositories write through the shared
    `atomicWrite`, and the about 15 files that use `QSaveFile` directly, and the two that write a part file and
    rename, move onto it. libppswing's `writeFileAtomic` stays as the store's documented exception[^dec155].
- **The archive as a `Session` command**[^lead-proposal-m14nov]: `archived.json` and the archive move are
  written through `SessionRepository` in this step (the archiver stops writing the folder itself), and through
  `Session::archive()` once the `Session` exists in step 3.
- **One-off migrations,** each with a dry-run report and backups on the share:
  - (no migration for the athlete folder: it is where `athlete.json` is found)[^dec151];
  - QSettings → `athlete.json` (every key of the reference's SET-01; `lastSessionAt` and `sessionCount` are derived
    from the `SessionIndex`, not carried over);
  - a `SessionId` (v5)[^dec150] and the session file for every existing session[^dec41];
  - golfer input out of `diagnostics.json`, into the shot `review` block and `session_input.json`, its keys removed
    from `diagnostics.json` only after a check, field by field, that every one is present at its target.
- **Folds in 0.7:** athletes are replaced on switch, and an edit is not a switch.
- **Removes the Stage 0.3 stopgap:** `ShotProcessor::startSwingSave` announcing a moved session folder
  (`activeSessionDirChanged`). The `Session` owns its location through the `SessionRepository`; capture asks the
  session where to write; a mid-session library re-point ends the session and starts a new one (same type and club,
  declared focus carried), so no relocate command or event is needed[^dec154].
- **Decided in this step's brief**[^m15nov]: a library re-pointed mid-session (a dropped share) ends the session
  and a new one starts there[^dec154]; a past intent run is not edited after its session ends, except by resuming it[^dec159].
  One session allocator: a standalone launch-monitor shot goes into the running session, and the LM
  controller's second allocator is deleted[^dec157].
- **Also in this step**[^m14nov-triage]:
  - a failed `atomicWrite` is surfaced in the app, the way Stage 0.4c reports a blocked `diagnostics.json`;
  - the advisory lock list names the tuning log, `outcome_corrections.jsonl`, which the app and a CLI tool can
    append at once;
  - from step 1's brief (§7): the live club-length prior is learned under the captured club even when the shot is
    later corrected; placed by this step's brief as learned rig state, per machine, owner Studio/Rig[^dec153].
- **Closes:** E-02, E-03, F-06, F-07/E-06, E-07, H-09, H-12, H-04 (fully), H-14, E-01 (the folder half: the
  session owns its folder, so it is never re-resolved per shot), D-08 (one reader and writer of the `diagnostics.json`
  envelope, in `SessionRepository`), H-08 (the session and athlete files' half: schema checked on read); C-03, C-11,
  D-16, F-13, H-10, and the June review's schema string literal ×4[^m14nov-triage].

### Step 3 · Session and SessionDiagnostics, by parallel run (3–4 days)

**Status (22 Nov 2026, item 3.10, the close):** items 3.1–3.9 are committed: 3.1 `3632f991`, 3.2 `bb729ca7`, 3.3
`160ba131`, 3.4a `024f7c16` and `3ef287b4`, 3.4b `d60d24d7` and `381166ee`, 3.5 `e38ba7a1` and `7442d433`, 3.6 `a6220739`,
3.8-i `1da5fffe`, 3.9 `72f07b62`, `36ac2542` and `96fd6029`, the cut-overs 3.7-a/b `cc563466`, 3.7-d `f2a98a33`, 3.7-c
`9b6f3868`, 3.7-e `2aa5bc72` and 3.7-f `fef9bfa1`, and 3.8-ii `63bfb951`. 3.10 closes the step: the parallel run's app switch
deleted, the parity harness on the new owner with an athletes ini (a fresh baseline, `step3.10-baseline`; `regrade_ledger`'s
default stays the old engine, and its `--engine new` refuses to run without `--athletes-ini`: 3.10 review B1), the allowlist
re-tagged, the docs (B-04), and parity reports A and P (`build/refactor/step-3/3.10/`). Every finding below is closed or placed
(`build/refactor/step-3/3.10/INVENTORY.md` §2). **Re-placed, not done in step 3** (`build/refactor/step-3/JUDGEMENT-CALLS.md`)[^j59]:
the old engine in `SessionDiagnosticsModel` (`detectShot`, its reduction, stores, pools and baseline authorities),
which J-a3's folders (cabin sessions on the Mac), `regrade_ledger --engine old` and the tests still run, goes in step 5 after
migration M1 (step 5's list); E-12's QML half goes to step 4; D-14's four remaining ⚠ comments go with step 4's AppState. **Still to do before
the step is done:** step 3.0 (its gate: the real M1–M4 runs), the re-fire on GOLFSIMPC (not run: the app re-judges every stale
session on load, on the Product Owner's "try the app" list), and H-06's command rewrites (open, for the Solution Architect to place)[^dec183].

**Split in two**[^dec162]: **3A** = items 3.0–3.4b, **3B** = items 3.5–3.10, as numbered in step 3's
brief (`build/refactor/step-3/BRIEF.md` §6)[^brief3].

#### Step 3.0 · Migrated data only: the corpus and archive copies migrated, then the tool and the legacy-read paths deleted
"the only user is me, so the migration tool is a one off right now" … "yes, put it in the plan at the
start of step 3- so long as we know the migration works and no data is lost"[^m15nov].

**Entry gate — all must hold first, each shown to the Product Owner:**
1. The real M1–M4 runs on the Mac and on GOLFSIMPC are done, each after its dry-run report and the Product Owner's approval.
2. **The migration works:** each run's read-back check passes; the written counts match the dry run's; a second run writes
   nothing; the app opens every migrated session and the parity dumps of the live library (analysis, diagnostics, athlete)
   are identical before and after the migration.
3. **No data is lost:** every golfer key moved by M3 is present at its new home, field by field, before any is removed
   (H-04); every athlete record's fields are present in `athlete.json`; the backups (`step2-backup-<stamp>`) are kept on the
   share until the Product Owner says they can go.

**Then:**
- The corpus and the archive mirror are migrated the same way (dry run → the Product Owner's OK → write → the same checks).
- Deleted: `pps_migrate_step2` and its tests; the golfer-input fallback and carry-over[^j33]; reading athletes from settings[^j27];
  the legacy D3 id derivation and folder-name day/order for sessions without `session.json`[^j56] — each only once
  no folder anywhere still needs it (a check over the library, archive and corpus proves it). The JSON-era library
  converter (`Gui/settings/library_converter.cpp`, allowlist A5 ×5, P11) goes with the legacy-read paths[^m15nov-p11].
- Tests: the legacy-read tests go with their code; a guard test proves a folder without `session.json` is reported, not guessed.

#### Step 3 itself

1. **`Session`** (aggregate root, commands and events), the **`Shot`** entity it owns (one per `ShotRef`: its
   Recording, review facts and the read-only `ShotDiagnostics`; it replaces the row structs in `ShotListModel` and the
   phone-clip filer as the source of shot facts — a named deliverable)[^m14nov], **`SessionDiagnostics`** and
   **`AthleteDiagnostics`** (the athlete level's owner: tendencies, work-ons, club baselines and distances — a named
   deliverable of this step) arrive in
   `src/DomainModel`. **Findings**[^dec100]:
   `SessionDiagnosticsModel::detectShot` becomes a pure function in `src/Diagnostics`, renamed
   for what it does (e.g. `fireFindings`). Each `Shot` fires its own findings, each number against its corridor, with
   the club baseline, norms and effective club its `Session` passes down. The findings are fired by the `Shot` and kept by its
   `Session`[^dec135], exposed through `ShotDiagnostics` (read-only). `SessionDiagnostics` diagnoses the session from its shots' findings (tiers,
   patterns, links, reasons, per-club mixes, the summary), and may compute and hold the usuals, a session aggregate of
   the shots' readings that is not part of the diagnosis[^dec130]. The analysis pipeline never fires findings. A
   corridor or norm edit makes the `Session` ask each shot to re-fire; a club correction re-fires that shot; neither
   re-analyses. Each shot has five layers (the reference's M-SHOT-LAYERS)[^dec121]: acquired → processed
   and analysed → shot derived data (the outcome, its three factors, carry and the flight-check overlay, computed for the
   shot with the session's usuals passed down) → assessment against the diagnostic graph[^dec132] → findings. The derived data feeds the
   assessment and is not a finding. The `Shot` posts its derived data and findings, stamped, to its `Session`, which keeps them
   together per shot in `diagnostics.json`, recomputed when their inputs change; readers never recompose them[^dec124].
   In the reference, "assessment" names the shot's layer and the older cross-shot use is
   "diagnosis"[^dec125].
   - **`WristDiagnosticsModel` retires**[^dec123]: there is no separate wrist model. The wrist-pattern
     identification is an analysis of the shot, and its result becomes part of the shot's findings through the one
     diagnostic model. The QML-created model, its own score, findings and strengths, and its view's private
     derivation go.
   - **The `Session` keeps its shots' findings**[^dec135]: the
     `Shot` fires its findings and computes its derived data and posts both, stamped; the `Session` keeps them per
     shot as its collection in `diagnostics.json` (FILE-SESS-03), as today. FILE-SHOT-13 retires, so today's rows are
     not moved into shot caches. **Golfer input in `diagnostics.json` must never be lost** (H-04, Stage 0.1): label
     corrections, shot intent and the session input (focus, declared miss, screen answers, intent runs) leave it by
     step 2's migration, to the shot's `review` block and `session_input.json`.
   - **The `Shot` is its own object** (P10)[^dec86-94-95]: it has its own commands for
     its own facts (`rate`, `note`, `correctClub`, `excludeReading`, `correctLabel`, `setShotIntent`,
     `setDeclaredClub`, and the `fireFindings` its `Session` calls), and it is obtainable only from its `Session`. It
     is a QObject child of its `Session`, exposed to QML directly, and reads the snapshot (baseline, pack, norms)
     from its parent[^dec141]. `Session::addShot` allocates the swing number, creates the folder through
     `RecordingRepository::create` and returns the `Shot`[^dec142]; `Session::end` on an empty session asks
     `Athlete::trashSession`, and EVT-20 fires[^dec143].
     After capture it is the only writer of its swing folder, through `RecordingRepository` (replacing "then only by
     its `Session`"). Its commands' events (EVT-02, 04…07, 21, 22) reach the `Session`, which hears them in command
     order and propagates them (re-firing, the summary). The `Session` keeps the collection (`addShot`, `trashShot`,
     swing numbers, order), its context and its session-wide results. Settled[^dec103-104]:
     the `Shot` saves and emits its own changes; `applyAnalysis`, `completeCapture` and the phase grid are
     the `Shot`'s; P3 reads "through their owner's commands". Today's interim writers (R-REC-3) move onto the
     `Shot`'s commands here.
   - `work_ons.json` is the session summary, written by `SessionDiagnostics`[^dec55].
   - `outcome_corrections.jsonl` is a source fact owned by the `Athlete` and appended through the `Athlete` root on
     `LabelCorrected`, never by the session and never by the lazy `AthleteDiagnostics`[^dec43].
   - The club baseline is a recorded input: `AthleteDiagnostics` computes it, the `Session` freezes it at first need,
     and it is rewritten only when the baseline rule's version is newer, as a recorded event naming the sessions it
     read (`ClubBaselineRewritten`, EVT-16)[^dec46].
   - `Session.status` and its start and end times are read and written by the `Session`'s commands in the session
     file step 2 creates[^dec41]; the shot kind is the `Shot`'s, from its Recording[^dec44].
   - The trash and the archive go through the roots (FILE-LIB-01, FILE-LIB-02, FILE-SHOT-10)[^dec45]: a shot by
     `Session::trashShot`, a session by `Athlete::trashSession`[^dec71], which replaces
     `SessionReviewController::trashSession`.
   - **Domain events**[^dec69-70]: delivered synchronously on the owner thread, in command
     order, carrying only the id of what changed. The set is the reference's EVT-01…EVT-23 (EVT-11 removed)[^dec154]: `ShotAnnotated` is
     split into `ShotClubCorrected` and `ShotReadingExcluded`, and `SessionStarted`, `SessionArchived`,
     `SessionRestored`, `SessionTrashed`, `ShotDeclaredClubChanged` (marks the analysis stale and re-fires its findings) and
     `ShotCaptureCompleted` (a late LM reading, follow-club or a phone clip: re-fire) are added.
   - **A session left InProgress by a crash** stays InProgress until the golfer ends it or the next Start ends it
     first[^dec73].
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
4. **Always fire:** every live shot fires its findings, panel or not (decision 2)[^dec100].
5. **Praise needs consistency**[^m14nov]: "What you do well" praises a condition only when it is in range
   AND its consistency grade is Repeatable or Grooved, for every praise, set-ups and faults alike. The grades are
   held by `AthleteDiagnostics` and read by the swing-themes reduction (Stage 0.14 could not reach them: today they
   are computed only in the Home controller, after the themes, and never stored).
6. **A norm's standing is a typed field**[^m14nov]: provisional or decided, by whom and when, read by
   everyone instead of parsed from prose (B-05). The release dashboard's two misreads (`m_takeawayPlane`,
   `m_clubSlotTop`) are fixed and checked.
7. **Decisions for the Solution Architect in this step's brief:** the owner and repository of packs and norms (the user's pack,
   corridor and content edits, today `<AppData>/diagnostics/`, and the disabled norm sets; Q-02). Also for this
   step's brief: whether firing findings reads the norm cohort (sex, age band) from the Recording (step 1 does not record it;
   Q-05)[^dec30]; and, from the triage[^m14nov-triage], where `AthleteDiagnostics` caches the athlete baselines
   and distances (Q-06). Settled since: where session status is stored[^dec41], P3's wording for the derived
   owners[^dec42], the event mechanics and the events[^dec69-70], who trashes a session[^dec71], and the
   crash rule[^dec73]. Also for this brief: how a re-fire on a worker reads the `Shot`'s snapshot as an immutable copy and posts
   its results to the GUI thread (the `Shot`'s form is settled)[^dec141], and the memory cost of always-running
   `SessionDiagnostics` in a sweep (Q-18), measured in the parallel run.
   - **Deliverables these decisions add**[^m14nov]: `Athlete::startSession`, which starts every session and
     first ends any of the athlete's sessions still InProgress[^dec78][^dec73]; `Athlete::resumeSession`, Ended →
     InProgress → Ended with the same `SessionId`, for a session started today and of the same type only[^dec77][^dec134];
     housekeeping as the one application service that purges, with one purge command and
     SET-14 off documented as a permanent delete of raw frames, shown with a warning[^dec137] (the "purged" end state) and auto-archives, through the roots' commands, in place of
     `ArchiveController::runHousekeeping` and `emptyTrash`[^dec80]; the session summary as the one session-level
     product the report renders and the athlete level reads, with its typed result types (links, mixes, reasons: Q-19)
     as a deliverable of this step[^dec81]; the grade policy (SET-15) in the findings stamp, with packs and norms
     (Q-02).
8. **Also in this step**[^m14nov-triage]:
   - the two-model race on a blocked `diagnostics.json` (the cast resolves the shared notice while the stage panel
     still drops input) goes with the single writer;
   - the work-ons catch-up's write blocks are reported through `AthleteDiagnostics`, not only logged;
   - flight norms and the metric catalogue become immutable snapshots with the packs (03 §8);
   - the packs' lint A5 lines (`Diagnostics/packs`, in A5 since step 2's S1-g, and `Diagnostics/engine/pack_io`) go once
     their loader is a repository;
   - the analysis services read a shot's Recording through the `Shot`, not `RecordingReader` / the loader
     (`Analysis/analysis_service.h`, `swing_reanalyzer.h`, `swing_reanalyzer.cpp`, allowlist A5, P11)[^m15nov-p11];
   - the pack loader's two long functions (`validatePack`, `loadPack`, allowlist A7) are split with B-11;
   - the pack and corridor editor (`Gui/diagnosticmodel/model_browser.h`, allowlist A6 ×73): it edits and saves the
     user pack and the user norm set (`user.json`, `user.norms.json`), so its writes go with the packs and norms
     owner and repository (item 7, B-02) and its QML API is typed here[^dec177].

- **Closes:** D-01…D-07, D-13, H-03, H-11, C-05, G-03/C-01, B-02 (one pack snapshot), B-03 (the pack registries'
  lazy singletons go with the one snapshot; the norm set's half was Stage 0.4), the session half of E-13, B-05 (a norm's standing
  typed)[^m14nov], and the follow-up noted in 0.5 (Tendencies, Consistency, the kicker and work-on facts honour the board's annotations);
  A-19, B-04, B-11, B-13, B-20, B-22, C-10, C-15, C-16, D-12, D-14, D-15, D-17, D-18, D-19, E-12, E-13 (the athlete
  half), G-08, G-09, G-10, G-14[^m14nov-triage]; H-06 in part[^dec183]: the re-fires and
  the diagnosis leave the GUI thread, and the commands' whole-document rewrites stay on it, recorded open
  (`information_model.md` §11.1) for the Solution Architect to place.
- **Re-run:** a re-fire of the findings, if the stamps change.

### Step 4 · AppState, the selection, the carousel summary (~½ day)

- **`AppState`:**
  - the current athlete;
  - the capture target;
  - the session on screen;
  - the mode;
  - the selections (stage and cast);
  - the replay.

  It lives in `src/App`[^m14nov] and replaces `SessionMode`'s focus fields, `CurrentSwing` and the carousel's
  copy. With nothing being captured, the cast selection shows the session on screen (03 decision 22)[^m14nov].
  **Decision for the Solution Architect in this step's brief:** which shot of that session the cast shows (the newest, or whatever the
  selection holds); decision 22 picks only the session.
- **Folds in 0.9.**
- **No athlete switch while capture runs** (03 §4, an application rule in `AppState`): it replaces Stage 0.8g's guard (E-01).
- **The carousel shot summary**, the request that started this work: strike · direction · flight · carry, for LM
  shots. Its look goes to the Design Lead first if it needs anything new.
- **The QML APIs of the carousel, the replay and the shot controller** are typed with the selection (allowlist A6:
  `shot_list_model.h`, `shot_filter_proxy_model.h`, `shot_replay_controller.h`, `shot_controller.h`)[^m14nov-triage].
- **The athlete controller's QML API** (allowlist A6: `athlete_controller.h`) is typed when `app.athlete` arrives, not in
  step 3 (D12)[^dec176].
- **The allowlist lines tagged `# step 4` at step 3's close**[^j59]: `athlete_controller.cpp` (A5, with `app.athlete`),
  `session_review_controller.cpp` (A5: the Sessions list, with the session on screen), `archive_controller.cpp` (A5: the
  tab's by-hand archive and restore through the `Session`, with 3.8-i's F-4 answer).
- **E-12's QML half** (step 3 built the C++ half: `Athlete::startSession`, `resumeSession`, `Session::end`)[^n2]:
  the session lifecycle still runs as a choreography of calls in QML in several places; with `AppState`'s capture target and
  session on screen, each becomes one command on the `Athlete` or its `Session`.
- **Closes:** E-04, E-05, E-08, E-14, E-01 (the switch half); E-15, H-07[^m14nov-triage]; E-12's QML half (placed by step 3's close)[^n2].
- **Added during step 4**[^m22nov]:
  - **a build fix:** a C++ header edit no longer recompiles every compiled-QML unit (`ad9b1edc`);
  - **a headless start for probes:** `PINPOINT_HEADLESS_PROBE=1` (`858ba2a7`);
  - **the re-grade tool reads `athlete.json`,** needed after the 22 Nov migration (`858ba2a7`);
  - **`app.state.outcome`** (R-APP-7): the carousel summary shows the newest analysed shot, which wins over an older selection;
  - **4-d, the declared session focus reaches its `Session`:** golfer input was lost when no diagnostics panel or cast was on (`build/refactor/step-4/FOCUS-TRACE.md`).

### Step 5 · Charts, the report and every view onto the session (~3–4 days, re-estimated 22 Nov 2026)

- **`SessionSeries` per session.** The view-owned `SessionSeriesModel`s go. `SessionSnapshot` reads the owner.
- **One presenter base and one threading idiom** for the six presenters.
- **Typed presenters**, one view at a time; the `QVariantMap` shapes go.
- **The report as a `Session` command**[^lead-proposal-m14nov]: the report documents are saved through a
  `Session` command and `SessionRepository`, not by the report model directly. The report renders the session
  summary step 3 defines; it computes none of its figures itself[^dec81]. `report_meta.json` mixes
  source facts (the coach's note and the send log)[^dec140] with derived ones (the fingerprint,
  the PDF name); this step's brief decides whether to split the file.
- **QML through `app`:** as each view moves (from step 3's cut-over on), the 42 context globals shrink to devices,
  settings, logging and `app`.
- **No domain rules in QML:** as each view moves to a typed presenter, its rules ("Clean" vs "Solid", shape by
  substring, distances from band words) move into the presenter's typed result.
- **Also in this step**[^m14nov-triage]:
  - **the review views** get typed presenters by the same mechanism: the data viewer stops creating its own
    `SwingDataSource` (allowlist A2), and `swing_data_source.h`, `chart_metrics.h`, `timeline_labels.h`,
    `metric_catalog.h`, `device_filter_model.h`, `session_review_controller.h`, `trust_words.h`,
    `body_pose_adapter.h` and `swing_rig_driver.h` lose their `QVariant` shapes (allowlist A6);
  - **the long report and traces functions are split** as they move onto the session (`SessionReportModel::build`
    1,011 lines; the traces builders 430 and 413);
  - **the report stills** (the reference's FILE-SESS-09, today in the cache location) are saved through the
    `Session` command with the report;
  - **day-level reads of other sessions go through session results**[^dec74]: the Charts
    delivery presenter and the report stop reading other sessions' `swing_phasegrid.json` and read their results
    through the `SessionIndex` instead.
- **From step 3's close** (`build/refactor/step-3/JUDGEMENT-CALLS.md`, as amended after 3.10's review)[^j59]:
  - **the old engine in `SessionDiagnosticsModel` is deleted, once migration M1 (step 3.0) has run:** `detectShot`'s body,
    the model's own reduction, stores, three of its four pools and its baseline authorities; `regrade_ledger --engine old`
    (its default went to `new` in item 5.3; `new` reads each session's athlete from its athlete folder's `athlete.json`, or,
    for the corpus, from `athletes.json` beside the session)[^r53-athletes]; `post_shot_bench --path old` (removed in 5.3);
    and the ~8,900 lines of
    tests written against it (`session_diagnostics_model_test`, `work_ons_controller_test`'s engine stand-in,
    `golfer_input_test`, `shot_number_test`, `diag_parallel_live_test`'s old side, the engine cases of
    `diag_stage_panel_test`), re-pointed at the owners or deleted with their reason;
  - the session adapter's history read (`historyOf`) and its pool read the sibling sessions through session results;
  - the allowlist lines tagged `# step 5` at step 3's close: `session_diagnostics_model.cpp`/`.h` (A5),
    `work_ons_controller.cpp` (A5; its catch-up scan moves into `src/App`), `markup_controller.h`, `markup_truth.cpp`
    (A5; the review views over the shot).
- **Every view references the information model** ("the key outcome I was looking for after this
  refactor")[^m22nov]. The survey `build/refactor/step-4/LOCAL-COPIES.md` (at `4baa30dd`) found 14 components that keep their own
  copy of domain facts and refresh it by hand. The items above cover 7 of them; this step also takes the other 7. At the
  step's end, no component in `src/Gui` keeps a list or cache of domain facts of its own: each reads the owner and follows
  its events. The survey is re-run at the close to prove it.
  - **the carousel is a pure view over the session**[^m22nov]: `ShotListModel`, live and review, and the
    report's temporary copy show `app.session`'s shots and follow the `Session`'s events (shot added, changed, trashed).
    They no longer build or reload a list of their own, which is the cause of 4-a's review B3;
  - **sessions with no golfer get an "Unassigned" owner**[^m22nov-n1]. Capture with no
    golfer selected goes to an Unassigned athlete, whose sessions are full `Session`s with one code path. The five
    `# kept (N1 …)` A5 lines go: `shot_list_model.cpp`, `ppcp_clip_filer.cpp`, `reanalysis_controller.h`,
    `shot_processor.cpp`/`.h`. A no-golfer folder found on disk (none today) is held by Unassigned and can be moved to a
    golfer. This is an information-model change, approved[^im-approved]; the reference is updated before it is built;
  - **one session list:** the Sessions screen, the report list and the Archiving tab become one presenter over the
    `Athlete`'s `SessionIndex`, following its events, instead of three disk scans. Today a club correction or a trash
    shows late in all three;
  - **the review header's** club mix comes from the `Session`'s summary, and its "live" marker from `AppState`'s capture
    target;
  - **the rule packs:** the five holders that keep the first pack they were given (`WorkOnsController`, `LmSessionModel`,
    `SessionSeriesModel`, and the statics in `consistency_env.cpp` and `session_posture.cpp`) take `RuleData::current()`
    and follow EVT-13. Today a pack edit reaches them only after a
    relaunch. B-02 fixed the duplicate assemblies but not this;
  - **the athlete screen** lists the `Athlete`s, with activity from each `SessionIndex`; `athlete_controller.h`'s six
    remaining QVariant members are typed (from 4-a);
  - **the replay** reads its analysis slice through the `Shot`'s `RecordingSource` (`disk_replay_source.cpp`);
  - **the markup tool's** truth summaries come from the `Shot` (`markup_controller.h`, `markup_truth.cpp`: their reads and
    their copies).
- **Closes:** F-01, F-02, F-04, F-08, F-09, D-10, F-12, G-11, D-11; A-02 (the Charts and Diagnostics re-parsers; step 1
  closes its converters, brief §4.6); F-10, F-14[^m14nov-triage].

### Step 6 · Measurements unified, flight-check overlay (~1½ days + a re-fire of the findings)

- **One source-tagged set of measurements per shot.** The LM is a device like any other, and one precedence rule
  decides which source wins. The LM board becomes a presenter.
- **One club per shot:** the effective club (the golfer's correction, else the declared club) is the club for
  findings, the board, every presenter and re-analysis (step 1). The board's private club handling goes. Until then
  the analysis and findings can name different clubs when only the popover names one: a known interim, accepted
  until this step[^dec72]. When the seed goes, follow-club's move of a seeded `review.club` goes
  too, and follow-club stops overwriting the captured club: it stores the device's club beside it, and the
  effective-club rule prefers that club when follow-club is on[^dec133].
- **No DRIVER stub for an undeclared club**[^lead-proposal-m14nov]: capture records an undeclared club as
  undeclared, so its findings are fired as "club assumed" (B-07, G-07), instead of the athlete's primary club or the `"DRIVER"`
  stub, which reads back as a declared driver.
- **The flight check becomes shot derived data**[^dec122]:
  computed for the shot with the session's usuals passed down, like the outcome, it feeds the assessment
  that fires the shot's findings; the stamps include their norms and thresholds.
- **One direction**, from the pack's corridors.
- **One handedness gloss.**
- **The LM board's print layout** (`src/Gui/session/PpLmGraphicsBody.qml`, embedded in the report's appendix)
  takes its reading names from the one words table (Stage 0.5b), not its own hand-typed list.
- **Closes:** A-04, A-10, F-05/G-01, G-02, G-04, G-06, G-07, C-02; A-02 (the LM board's re-parser; brief §4.6 of
  step 1); G-12, G-13[^m14nov-triage].

### Step 7 · Rules down: dependencies inward and file I/O out of the rules (~1 day; can overlap step 6)

Step 7 covers the allowlist lines tagged `# step 7` under A1 and A5, and nothing else[^po23nov].

- **File I/O leaves Coaching and Diagnostics;** norms and packs are loaded by repositories (C-07, B-12, B-06). Ball and
  Pose too: their stages' pinned inputs and baselines and the pose model fingerprint are read outside the rules
  (`Ball/ball_baseline_io.h`, `Ball/ball_runner.cpp`, `Pose/pose_runner.cpp`, allowlist A5, P11)[^m15nov-p11].
  The diagnostics measure stops reading swing documents through `SwingStore` itself (`measure_sample.cpp`, A1 and A5).
- **Dependencies point inward** (A1): `SessionSpread` moves out of `src/Gui/diagnostics` so `Coaching/session_report.h`
  no longer reaches up; the wrist types and reference bands that `Metrics/metric_descriptor.h` and
  `Metrics/metric_provider.h` use move from `src/Wrist` into the vocabulary (B-08, the wrist half).
- **The rest of the original step 7 is #224:** domain code out of `src/Gui`, the one words module, one namespace per
  layer, the stages' value types, the metric catalogue as data, metric keys, the Diagnostics re-cut, and `layoutDag`'s
  move. Their allowlist lines are re-tagged to #224 at step 7's close.
- **Closes:** C-07/B-12/B-06, B-08 (the wrist half), and the A1 and A5 `# step 7` allowlist lines. The findings that
  moved with #224 are recorded there as deferred.

### Step 8 · moved out of the plan

The long-function splits (B-18, C-09) are GitHub #222, done after the refactor or when a later change has to touch the
function[^po23nov]. Their allowlist lines keep the tag `# step 8` until #222 re-tags them.

### Later · the platform refactor

The technical and platform-layer refactor that follows this one[^m14nov]. Its worklist, with the
context for each item, is `05-deferred-worklist.md`. Its **capture** part is a review of transports, devices, the
ring buffer, connectors and calibration, against the Recording contract; Studio/Rig is designed there. The rest is
settings, monitoring and tool APIs, build and test infrastructure, and platform code hygiene.

**Proposed for this list, for the Solution Architect** (the reference's Q-20): a machine-readable form of the information model and a
check that its §11 matches the code. The PPCP import files (FILE-LIB-04, FILE-LIB-05) and the trunk ceremony
recorder's pool-thread write (FILE-SESS-11) are capture's, here.

## 4. Time

These are wall-clock estimates at agent pace, assuming the Product Owner reviews each step within about a day.

| Step | Estimate |
|---|---|
| Stage 0 | committed |
| G | ~1 day |
| 1 | 3–4 days + re-analysis (step 1's brief) |
| 2 | ~1 day |
| 3 | 3–4 days |
| 4 | ~½ day |
| 5 | ~3–4 days (re-estimated 22 Nov 2026: the old engine, every view onto the session) |
| 6 | ~1½ days + a re-fire of the findings |
| 7 | ~1 day[^po23nov] |
| **Total** | **about 13–15 working days, ending at step 7** |

The 14 Nov triage adds scope to steps 1–7 that these estimates do not yet include; each step's brief re-estimates.

**What limits the pace:** builds and tests on one machine; file overlap in step 3; the hours a GOLFSIMPC re-run
takes (machine time, run by the lead); review rounds; and the Product Owner's testing. It is not the typing.

**How it could be compressed:** steps 5 and 7 can overlap step 3's cut-over, and the Product Owner can review two steps at
once. That gives about 10–12 working days.

## 5. What the Product Owner is asked for

| When | What |
|---|---|
| Now | §1 (how it runs), the hard freeze and the Stage 0 adjustment in §2 — agreed 14 Nov. |
| End of Stage 0 | Test and commit the Stage 0 change sets; commit the audit docs. |
| Each step | Approve its brief; accept or reject each parity difference; test; approve the commit. |
| After each re-run | Judge the re-run's results before they are written back (results before commit). |

## Notes

[^m14nov]: Product Owner, 14 Nov 2026.
[^m14nov-ext]: The Product Owner extended it from step 7 on 14 Nov 2026, with the commit pre-approval.
[^m-012]: The Product Owner's decision.
[^m14nov-014]: Coaching Lead, 14 Nov 2026: option A; "praise setup, esp. a consistent setup"; "we have consistency grading in the app".
[^mrule-warn]: The Design Lead's rule.
[^dec40]: DEC-40.
[^dec31-39]: Reference DEC-31…DEC-39.
[^dec34]: DEC-34.
[^dec44]: Solution Architect, 14 Nov 2026; DEC-44.
[^dec86]: Solution Architect, 14 Nov 2026; DEC-86.
[^dec75]: Solution Architect, 14 Nov 2026; DEC-75.
[^dec64]: Solution Architect, 14 Nov 2026; DEC-64.
[^m14nov-triage]: The triage (`build/refactor/TRIAGE-PROPOSALS.md`), approved by the Product Owner, 14 Nov 2026.
[^dec158]: Solution Architect, 15 Nov 2026; DEC-158.
[^j58]: J-58.
[^dec130-140]: Solution Architect, from the final re-audit (15 Nov 2026; reference v1.8, DEC-130…DEC-140).
[^dec134]: Solution Architect, 15 Nov 2026; DEC-134.
[^dec136]: DEC-136.
[^dec137]: DEC-137.
[^dec148]: Solution Architect, 15 Nov 2026; DEC-148.
[^dec41]: Solution Architect, 14 Nov 2026; DEC-41.
[^dec149]: Solution Architect, 15 Nov 2026; DEC-149.
[^dec150]: Solution Architect, 15 Nov 2026; DEC-150.
[^dec69]: Solution Architect, 14 Nov 2026; DEC-69.
[^dec77]: Solution Architect, 14 Nov 2026; DEC-77.
[^dec88]: DEC-88.
[^dec79]: Solution Architect, 14 Nov 2026; DEC-79.
[^dec83]: Solution Architect, 14 Nov 2026; DEC-83.
[^dec152]: Solution Architect, 15 Nov 2026; DEC-152.
[^dec151]: Solution Architect, 15 Nov 2026; DEC-151.
[^dec156]: Solution Architect, 15 Nov 2026; DEC-156.
[^dec45]: DEC-45.
[^lead-proposal-m14nov]: The lead's proposal; the Product Owner agreed 14 Nov 2026.
[^dec155]: Solution Architect, 15 Nov 2026; DEC-155.
[^dec154]: Solution Architect, 15 Nov 2026; DEC-154.
[^m15nov]: Product Owner, 15 Nov 2026.
[^dec159]: DEC-159.
[^dec157]: DEC-157.
[^dec153]: Solution Architect, 15 Nov 2026; DEC-153.
[^j59]: J-59 (`build/refactor/step-3/JUDGEMENT-CALLS.md`).
[^dec183]: Solution Architect, 15 Nov 2026; DEC-183.
[^dec162]: Product Owner, 15 Nov 2026; DEC-162.
[^brief3]: Approved 15 Nov 2026; its §8 answered as DEC-162…DEC-189.
[^j33]: J-33.
[^j27]: J-27.
[^j56]: J-56.
[^m15nov-p11]: Solution Architect, 15 Nov 2026: `build/refactor/step-2/JUDGEMENT-ANSWERS.md`, "P11 lint".
[^dec100]: Solution Architect, 14 Nov 2026; DEC-100, superseding decision 19's "`SessionDiagnostics` grades every shot".
[^dec135]: Solution Architect, 15 Nov 2026; DEC-135, superseding DEC-102 and moving DEC-124.
[^dec130]: Solution Architect, 15 Nov 2026; DEC-130.
[^dec121]: Solution Architect, 14 Nov 2026; DEC-121.
[^dec132]: DEC-132.
[^dec124]: DEC-124, moved by the Solution Architect, 15 Nov 2026 (DEC-135).
[^dec125]: DEC-125.
[^dec123]: Solution Architect, 14 Nov 2026; DEC-123.
[^dec86-94-95]: Solution Architect, 14 Nov 2026; DEC-86, DEC-94, DEC-95.
[^dec141]: Solution Architect, 15 Nov 2026; DEC-141.
[^dec142]: DEC-142.
[^dec143]: DEC-143.
[^dec103-104]: Solution Architect, 14 Nov 2026; DEC-103, DEC-104.
[^dec55]: Ratified by the Solution Architect, 14 Nov 2026; DEC-55.
[^dec43]: Solution Architect, 14 Nov 2026; DEC-43.
[^dec46]: Solution Architect, 14 Nov 2026; DEC-46.
[^dec71]: Solution Architect, 14 Nov 2026; DEC-71.
[^dec69-70]: Solution Architect, 14 Nov 2026; DEC-69, DEC-70.
[^dec73]: Solution Architect, 14 Nov 2026; DEC-73.
[^dec30]: DEC-30.
[^dec42]: DEC-42.
[^dec78]: DEC-78.
[^dec80]: DEC-80.
[^dec81]: Solution Architect, 14 Nov 2026; DEC-81.
[^dec177]: The lead's call, 14 Nov 2026; kept by the Product Owner against the brief's recommendation, 15 Nov 2026; DEC-177.
[^dec176]: Solution Architect, 15 Nov 2026; DEC-176, D12, reversing J-68.
[^n2]: Product Owner, N2 at 3.4a-ii.
[^m22nov]: Solution Architect, 22 Nov 2026.
[^dec140]: Solution Architect, 15 Nov 2026; DEC-140, T5.
[^dec74]: Solution Architect, 14 Nov 2026; DEC-74.
[^m22nov-n1]: Solution Architect, 22 Nov 2026, changing his N1 answer.
[^im-approved]: Approved by the Solution Architect.
[^dec72]: Solution Architect, 14 Nov 2026; DEC-72.
[^dec133]: Solution Architect, 15 Nov 2026; DEC-133, superseding DEC-82's rewrite.
[^dec122]: Solution Architect, 14 Nov 2026; DEC-122, superseding decision 6's "session-level overlay".
[^lead14nov]: The lead's call, 14 Nov 2026.
[^m14nov-triage-s8]: Triage, Product Owner 14 Nov 2026: the triage proposed them for step 7 as a scope addition, with a step of their own as its recommendation, which the Product Owner chose; `layoutDag` and `swing_themes.h` in step 8: the lead's call, 14 Nov 2026.
[^r53-athletes]: Since refactor 4-c (J-61) `--engine new` reads each athlete from the library's `athlete.json`, and `--athletes-ini` is only the fallback for an unmigrated library; the earlier wording ("needs `--athletes-ini`") dated from 3.10 review B1. The corpus's golfer file, `corpus/swings/athletes.json`: the Product Owner's answer 1 to step 5's questions, 22 Nov 2026.
[^po23nov]: Product Owner, 23 Nov 2026, to save tokens and time: step 8 left the plan for GitHub #222, and step 7 was reduced to its A1 and A5 allowlist lines, the rest moving to #224.
