# 04 · The refactor plan

**Status (14 Nov 2026):**
- Mark has agreed the overall architecture in `03-target-architecture.md` (draft 2.1); its elements will be refined
  step by step.
- This page is how we get there. It replaces the first remediation plan.
- Stage 0, local defect fixes, is partly done (below). Nothing structural starts until Mark approves the first
  step's brief.

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
- Its allowlist entries are gone.
- The docs that describe the area are current.

**Library and corpus re-runs** run on GOLFSIMPC, started and watched by the lead over ssh (the cabin-PC sweep
workflow), with a dry-run first. They are grouped: after steps 1 and 6, plus a regrade after step 3 if the stamps
change. That is about two to three re-runs in the whole refactor. Each one is reported to Mark before its results
are written back.

**HARD FREEZE (Mark, 14 Nov 2026):** no new functionality, anywhere, unless it is explicitly stated in this plan —
"even if I beg". Bug fixes go to Stage 0 or to the step that owns the area. A new idea is written down (a GitHub
issue labelled for after the refactor) and waits. When a request comes close to new functionality, the lead names
the plan line that covers it, or says that none does and holds it. The freeze ends when step 7 is done.

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
| 0.1 | Golfer input never lost from `diagnostics.json` (H-04) | **Keep** (Critical) | Implemented; reviewer's 3 changes in progress |
| 0.2 | `atomicWrite` on `QSaveFile` (H-05) | **Keep** | **Signed off** |
| 0.4 | A saved corridor edit reaches the app (B-01, part of B-03) | **Keep** | Implemented; reviewer's 5 changes in progress |
| 0.4b | Follow-up to 0.4: `SessionDiagnosticsModel` re-takes the norms at activation; `LmSessionModel` at its grade pass | **Keep**: closes 0.4's definition of done | Queued after 0.1 and 0.4 |
| 0.5 | The report honours the board's exclusions and club overrides (F-03) | **Keep** | **Signed off** |
| 0.3 | "Club assumed" demotion (B-07), and `ingestShot` checks its session | **Keep**: a grading defect, small, lasting | Queued |
| 0.6 | One stored-score reader; the replay's overall score (H-01) | **Keep**: small, lasting | Queued |
| 0.12 | One held rule for Your swing and Work-ons (C-06) | **Keep**: Mark's decision, small | Queued |
| 0.8g | **Guard only:** no athlete switch or auto-select while capture runs. The live carousel reloads only when the athlete's uuid actually changes. | **Keep the guard** (data can be saved under the wrong athlete today) | Queued; its brief is a cut-down 0.7 |
| 0.7 | The full split between "athlete changed" and "record edited", with listeners moved | **Fold into step 2** (Athlete objects replaced on switch) | — |
| 0.9 | The selection cleared on session change; no basename fallback (E-05) | **Fold into step 4** (`AppState` selection) | — |
| 0.10 | Re-analysis uses the live athlete facts (H-02, A-01) | **Fold into step 1** (the Recording contract) | — |
| 0.11 | The score pill | **Design question**, filed as an issue | — |

**Order:** 0.1 and 0.4 finish, then 0.4b, 0.3, 0.6, 0.12 and 0.8g, two at a time on disjoint files. Stage 0 ends
with one list of change sets for Mark to test and commit.

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

### Step 1 · The Recording contract (~1 day + a library re-analysis)

- **One builder** produces the Recording, with its analysis context (athlete facts, club and length, rig
  reference), for live capture and for re-analysis alike.
- **The analysis service takes only a Recording.** `ShotProcessor`'s analysis-job half moves behind it.
- **Folds in 0.10:** one header builder; `athlete.heightM`, the cohort and the standard club length recorded.
- **Closes:** H-02, A-01, E-11, the parity half of E-09, A-09 (one write-back path).
- **Test:** live job == re-analysis job, from the same Recording.
- **Re-run:** a stage-version bump, then one library re-analysis on GOLFSIMPC (run by the lead).

### Step 2 · Identity and repositories (~1 day)

- **Identity types:** `AthleteId`, `SessionId`, `ShotRef`, `SessionIdentity` (no folder-name parsing anywhere
  else).
- **Repositories:**
  - `AthleteRepository` (`athlete.json`, the folder stored on the record);
  - `SessionRepository`, an identity map (`session_input.json`, `diagnostics.json`, `club_baseline.json`);
  - `SessionFiles`/`AthleteFiles`.
- **One-off migrations,** each with a dry-run report and backups on the share:
  - the athlete folder recorded on the record;
  - QSettings → `athlete.json`;
  - golfer input out of `diagnostics.json`, into the shot `review` block and `session_input.json`.
- **Folds in 0.7:** athletes are replaced on switch, and an edit is not a switch.
- **Closes:** E-02, E-03, F-06, F-07/E-06, E-07, H-09, H-12, H-04 (fully).

### Step 3 · Session and SessionDiagnostics, by parallel run (3–4 days)

1. **`Session`** (aggregate root, commands and events) and **`SessionDiagnostics`** arrive in
   `src/DomainModel`. `diagnoseSwing()` becomes a pure function in `src/Diagnostics`.
2. **Parallel run:** behind a feature switch, the new owner computes alongside today's `SessionDiagnosticsModel`
   on every live and opened session, and a diff is logged. The parity run covers the corpus.
3. **Cut-over, view by view:**
   - the stage panel;
   - the cast window;
   - the card pips;
   - the report;
   - the catch-up (via `AthleteDiagnostics`).

   Each cut-over deletes a private model. `SessionMode.sessionDiagnostics` goes last.
4. **Always detect:** every live shot is graded, panel or not.

- **Closes:** D-01…D-06, D-13, H-03, G-03/C-01, B-02 (one pack snapshot), the session half of E-13, and the
  follow-up noted in 0.5 (Tendencies, Consistency, the kicker and work-on facts honour the board's annotations).
- **Re-run:** a regrade, if the stamps change.

### Step 4 · AppState, the selection, the carousel summary (~½ day)

- **`AppState`:**
  - the current athlete;
  - the capture target;
  - the session on screen;
  - the mode;
  - the selections (stage and cast);
  - the replay.

  It replaces `SessionMode`'s focus fields, `CurrentSwing` and the carousel's copy.
- **Folds in 0.9.**
- **The carousel shot summary**, the request that started this work: strike · direction · flight · carry, for LM
  shots. Its look goes to Mark first if it needs anything new.
- **Closes:** E-04, E-05, E-08, E-14.

### Step 5 · Charts and the report onto the session (~2 days; can overlap step 3's cut-over)

- **`SessionSeries` per session.** The view-owned `SessionSeriesModel`s go. `SessionSnapshot` reads the owner.
- **One presenter base and one threading idiom** for the six presenters.
- **Typed presenters**, one view at a time; the `QVariantMap` shapes go.
- **Closes:** F-01, F-02, F-04, F-08, F-09, D-10, F-12.

### Step 6 · Measurements unified, flight-check overlay (~1½ days + a regrade)

- **One source-tagged set of measurements per shot.** The LM is a device like any other, and one precedence rule
  decides which source wins. The LM board becomes a presenter.
- **The flight check becomes a session overlay** on unchanged rows; the stamps include their norms and thresholds.
- **One direction**, from the pack's corridors.
- **One handedness gloss.**
- **Closes:** A-04, A-10, F-05/G-01, G-02, G-04, G-06, G-07, C-02.

### Step 7 · Rules down, words, namespaces (1–2 days; can overlap step 5)

- **Domain code moves out of `src/Gui`**, about 5,000 lines (F-11, D-09, C-08).
- **File I/O leaves Coaching and Diagnostics;** norms are loaded by repositories (C-07, B-12, B-06).
- **One words module;** domain reductions persist ids and numbers (C-04, B-10).
- **One namespace per layer** (C-14, B-15, A-20). `src/Export` is renamed for the Recording contract.
- **Closes:** most of themes 5 and 6.

### Later · The Acquisition session

A separate review of transports, devices, the ring buffer, connectors and calibration, against the Recording
contract. Studio/Rig is designed there.

## 4. Time

These are wall-clock estimates at agent pace, assuming Mark reviews each step within about a day.

| Step | Estimate |
|---|---|
| Stage 0 remainder | ~½ day |
| G | ~1 day |
| 1 | ~1 day + re-analysis |
| 2 | ~1 day |
| 3 | 3–4 days |
| 4 | ~½ day |
| 5 | ~2 days |
| 6 | ~1½ days + regrade |
| 7 | 1–2 days |
| **Total** | **about 11–13 working days** |

**What limits the pace:** builds and tests on one machine; file overlap in step 3; the hours a GOLFSIMPC re-run
takes (machine time, run by the lead); review rounds; and Mark's testing. It is not the typing.

**How it could be compressed:** steps 5 and 7 can overlap step 3's cut-over, and Mark can review two steps at
once. That gives about 8–10 working days.

## 5. What Mark is asked for

| When | What |
|---|---|
| Now | §1 (how it runs), the hard freeze and the Stage 0 adjustment in §2 — agreed 14 Nov. |
| End of Stage 0 | Test and commit the Stage 0 change sets; commit the audit docs. |
| Each step | Approve its brief; accept or reject each parity difference; test; approve the commit. |
| After each re-run | Judge the re-run's results before they are written back (results before commit). |
