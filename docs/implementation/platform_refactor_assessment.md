# Platform refactor: assessment

**Date:** 2026-11-24 (revision 3: revision 2, written for the close of steps 5–7, with the domain refactor's lessons, a second outside review and the 24 Nov rulings applied; the first version is 21 Nov 2026).
**Status:** assessment for the platform refactor, after step 7 of the current refactor (the refactor ends at step 7;
step 8 left the plan for #222[^po23-plan]). The first version was reviewed (`build/refactor/platform-review/REVIEW.md`,
21 Nov 2026) and revised; this revision folds in what steps 5–7 routed here and an outside review (§10).
**What it is:** input to the platform refactor's briefs. It is not a plan. The worklist of record is
`docs/implementation/swe-audit/05-deferred-worklist.md` (05); appendix A places every 05 row.
**Checked against:** the first version at HEAD `8cd42e44` (main, 21 Nov 2026), with `governance/artefacts/information_model.md`
(IM) v1.9.10, and v1.9.12 for DEC-193. This revision's new facts are read at `origin/main` `5946266d` (24 Nov 2026):
the J-calls (`build/refactor/step-3/JUDGEMENT-CALLS.md`), GitHub #222, #224, #229 and #230, the allowlist, the lint's
ring table and IM. Its counts are re-measured at the close, at `origin/main` `44ae9230` (24 Nov 2026), with IM v1.9.52[^c-sha]; revision 3's corrections are IM v1.9.53. Also
`docs/implementation/swe-audit/02-findings.md` (02), `04-remediation-plan.md` (04) and 05;
`docs/implementation/code_quality_review.md` (the June review); `src/Gui/tests/architecture_allowlist.txt` (the
allowlist); the open GitHub issues.

**How things are measured here.** Line counts are production code only: `.cpp`, `.h`, `.mm` and `.qml` files, with
every `tests/` directory left out, counted with `find <dir> \( -name '*.cpp' -o -name '*.h' -o -name '*.mm' -o -name
'*.qml' \) -not -path '*/tests/*' | xargs cat | wc -l`. Single files are `wc -l`. Allowlist entries are cited by path
and rule, not by line number, because line numbers go stale. Every count from the first version is at `8cd42e44`.
Counts in this revision are re-measured at `44ae9230`, the close of steps 5–7; each carries a footnote naming its source.
Historical figures tied to a named commit (for example the QML globals "18 after 5.8b") are given at that commit.


**Words used.** IMC: an information-model change, as IM §1.2 defines it (C1–C11). PPCP: the PinPoint Capture
Protocol, the phone link (`src/Ppcp`). Execution provider: the ONNX Runtime back-end a model runs on (CPU, CUDA,
DirectML, CoreML). A ring is a dependency ring of IM §4 (Platform, Vocabulary, Rules, Domain model, Application,
Adapters); this page uses the names, not numbers. "The lint" is `architecture_lint_test` and its rules A1–A8 (IM §1.4).
"The cabin PC" is GOLFSIMPC, the Windows studio machine. "The domain refactor" is 04's steps 1–7. "The close" is the
close of steps 5, 6 and 7 (`build/refactor/CLOSE-BRIEF.md`). "06" is the domain refactor's report
(`docs/implementation/swe-audit/06-refactor-report.md`; its Part B is the fact pack). A J-number is a judgement call
in the refactor's decision log, which is in the repository as `docs/implementation/swe-audit/06a-judgement-calls.md`.

---

## What changed since the 21 Nov version

- **The domain refactor ends at step 7.** Step 8 (the long-function splits) left the plan for #222, and step 7 was
  reduced to its `# step 7` A1 and A5 allowlist lines, with the rest moved to #224[^po23-plan]. Premise 3 and Stage B
  no longer assume that step 7 splits directories or renames namespaces.
- **A new section, "Mandatory carry-ins from the domain refactor"**: the one Vocabulary root (mandatory)[^sa23-vocab],
  #224's items, the QML globals question, and the items each J-call routed here.
- **#222 is out of the platform plan** (§8, and "Mandatory carry-ins" (ii)).
- **R-INS-5 (#229, #230)** is recorded as analysis work after the refactor, not a platform stage, with one ordering
  point against #224's metric catalogue as data.
- **DEC-194** (the pose inference thread)[^dec194] is now the starting point for Stage E's threading item and §9 Q9.
- **The outside review of 23 Nov** is folded in, point by point, with a verdict on each (§10).
- **Small corrections:** an approval marked with a person's name now names the role; the MediaPipe mentions go (`32bb5353` removed the
  code); `post_shot_trace` is in IM's RING-PLATFORM since v1.9.12; #192 and #193's "ask after step 3" become
  "re-check now that the freeze ends"; `archive_controller`'s lines are re-checked at the close; the PPCP clip filer's
  lines are placed (Stage F); Stage D carries two guardrails; Q2 is split in two.
- Counts re-measured at `44ae9230` at the close of steps 5–7.
- **The domain refactor's lessons are applied** (24 Nov, after an independent review of 06). A new section, "Lessons
  from the domain refactor, applied", maps 20 lessons to the edits they made: rules for every stage (§5); a Tier and
  an Accept row in each stage's table; entry conditions and lane rules (§7); when approvals and questions are ruled
  ("Approvals this page needs", §9); six more carry-ins that 06 §10 routes here ("Mandatory carry-ins" (iv)); and the
  outside review's build-cost point, now accepted with change (§10, row 2.1c). Every new rule is a proposal for the
  role named beside it.
- **A second outside review (24 Nov), checked line by line before it was applied**[^g24]. The 34 lines tagged
  `platform refactor (#224)` are split four ways, and 26 of them, the step-5 presenter residue, leave Stage B for a
  third group, #224-U ("Mandatory carry-ins" (ii)). The three stale `# step 1` and `# step 4` lines are placed, and
  re-analysis' A1 line is cleared before Analysis becomes a library (Stage B). `headless_probe.h`, `same_folder.h` and
  `version.h` stay in Platform, outside the Vocabulary move. `archive_controller.cpp`, #234 and #196 are in Stage E's
  rows, and #208 is in Stage F's. #203 and #228 are in Stage B's, #235's two halves are split between Stage A and
  Stage D, and §8 names all six. §5 gains rule 14, a cap on process documents.
- **The morning review's rulings (24 Nov)** are applied: the next section lists them, and the rows they settle say so.

---

## Rulings of 24 Nov

The morning review of J-101 to J-112 and of the second outside review settled these[^po24]. Each row it settles now
says "ruled" where it appears below.

| Ruling | By | Where it lands |
|---|---|---|
| #224 in three groups: #224-B in Stage B; #224-H and #224-U each a plan of its own, with #224-U scheduled straight after Stage B, so that step 5's leftovers have an owner and a date, not only a tag | Product Owner | "Mandatory carry-ins" (ii); §7; §9 Q16 |
| #222 stays out of the platform refactor (the outside review's report of 23 Nov, now confirmed) | Product Owner | §8 |
| GPU segmentation leaves the platform plan for its own issue, #240. Stage A keeps only the one-copy execution-provider cascade, which changes no output | Product Owner | Stage A; §9 Q10 |
| #235's half (2), an edit cleared before its write, is a Stage A bug fix (a golfer-input loss); half (1) stays with Stage D | Product Owner | Stage A, Stage D; "Mandatory carry-ins" (iv) |
| The attack-angle low point shows ± σ and no ⚠, as built in 6.4: ⚠ keeps its one meaning, "a gate touched this reading" (J-108) | Product Owner | none: as built |
| The 63 shots whose low point changed in 6.4 are accepted (J-108) | Product Owner | none: as built |
| Device-only recordings record handedness (R-REC-8) | Solution Architect | Stage D, with D-5 |
| The `hm.*` values are re-tagged as derived (R-INS-5), reversing 6.1a's J-2. 6.4's D-1 (capture writes them) is dropped from Stage D. The re-tag is #239, after the refactor | Solution Architect | not platform work |
| The pose model fingerprint becomes a direct call, `modelFileFingerprint` in `Core/pp_model_path.h`, replacing the hook (J-107's J-2) | Solution Architect | Stage B, Part 1 |
| Buffer's stream types, LaunchMonitor's reading types and `Export/frame_decode` move under `src/Vocabulary/` too, for one Vocabulary rule (J-109) | Solution Architect | Stage B (LaunchMonitor, `frame_decode`); Buffer's part withdrawn on 25 Nov: it stays[^lever-25nov] |
| P-3: a Shot-level reader, `RecordingReader::phaseGrid(shot)`, with the model browser's two library-wide scans moved to the athlete level's `SessionIndex` summaries, as Charts did in 5.5d. No library-level door (J-104) | Solution Architect | Stage B, Part 1 |
| Lint A5 counts `Export/rule_data_reads.cpp` as a store door, with one allowlist line cleared in Stage B (J-110) | Solution Architect | Stage B, Part 1 |
| A legacy `"DRIVER"` stub reads as undeclared everywhere (R-CLUB-4): its own domain item, #238 (J-106) | Solution Architect | not platform work |
| J-111's stale figures are corrected: IM §4.1's Analysis row (its shared types are in `src/Vocabulary/Analysis/` since 7.3), IM §11's PPCP build flags, and 02's "about 5,000 lines" (8,529 in F-11's ten files at the close). The other two were already corrected in this page's footnotes (`[^c-exe]`, `[^c-ppcptest]`) | Solution Architect | done (IM v1.9.53; 02's F-11 footnote) |
| The review's club popover shows "Not declared" for an undeclared shot, built with #233's fix (DL-4) | Design Lead | not platform work (#233) |
| Side-naming body conditions mirror for a left-hander, worded by the Coaching Lead (E-11): #236 | Coaching Lead | not platform work |
| The compound miss starts at 4° like pull/push (6.3b's D-1), and fade/draw follow the grade policy (D-3): #237. D-2 (the no-axis fallback) is accepted as intended | Coaching Lead | not platform work |
| **Q20, the entry condition:** the platform refactor starts now. The cabin visit, with the 3-shot live check, is assumed successful for planning, and runs at the first visit as that visit's first item; the `94b3b9a1` installer stays the baseline, so a fault it finds is traced to the domain refactor. Issues are closed only on evidence, so #105 and #53 are built now and closed at the visit | Product Owner | §7 |
| **Q4, Stage A's order:** as written: #184, #105 and #53's detection half first, then the refactors, with #235's half (2) in the stage. **#183 is held** until its comparison at the cabin, so that its settings change and its per-stream fields (a format change) are designed once, from evidence | Product Owner | Stage A |
| **Q21, the review tier:** as proposed. An independent review for capture timing, threads and teardown, settings and secrets, formats, golfer data, deletions and cut-overs; a self-check only for scripted moves. The tier is written in each brief | Product Owner | §5 rule 5 |
| **Q22, the Windows test lane:** one bounded build of the test tree and a `ctest` run on the cabin PC over ssh now, to show whether it works. After that it runs only when no session is on and never beside Foresight Premier, each run started from one script | Product Owner | Stage B |
| **Q15, the area names:** whole directories keep their names (`Vocabulary/Metrics`, `Recording`, `Identity`, `LaunchMonitor`, and `Buffer` in Stage D); `frame_decode` goes to `Vocabulary/Recording`; Core's seven vocabulary files go by subject, `club_standards` and `club_vocabulary` to `Vocabulary/Club` and the rest to `Vocabulary/Common` | Solution Architect | Stage B, Part 1 |
| **Q11, the studio profile:** `src/App/`, beside `AppState`, in the Application ring where its owner is. It keeps its own file (P11); Stage E gives it its repository | Solution Architect | Stage B, Part 1 |
| **Q25, the method's tools:** the IM excerpt tool, the build lock and the changelog merge helper move into `tools/`, each with a self-test, at the start of Stage B | Product Owner | Stage B |
| #224's four-way split of its 34 lines is posted on #224 (§5 rule 10) | Product Owner | done |
| **Q26, the pipeline and the domain model:** option 1, contracts, not owners. Every context reaches Domain + Application only through the Domain model's interfaces and value types, never the owner classes or the Application services; A9 checks it with a named set of contract headers (IM v1.10.1, R-CTX-1) | Solution Architect | Stage B |
| **Q27, shared Vocabulary:** option B. `Vocabulary/Club`, `Vocabulary/Common` and `Identity` are tagged Shared, usable by every context; a type goes there only when two contexts use it; `trunk_calib_provenance.h` moves to `Vocabulary/Common` (IM v1.10.1, §4.1) | Solution Architect | Stage B, Part 1 |
| **The context map: kept for later** (PR-9). Tested in `build/context-map/` (963 cross-context includes: 886 fit, 68 by a carve-out, 9 fit neither, the same 9 under either rule) and reviewed by Gemini. It is applied when the coaching track makes its first information-model change, or when Stage D lifts the capture deferral, because only then does it check what the order can't (Coaching never re-derives and never reads raw streams). Guards for then: no Acquisition → Coaching row (a play source implements a hub or Coaching interface; rehearsal swings reach Coaching as domain events); decide the pack machinery's home (`layered_set`, `pack_io`) and whether pure sensor maths is Shared; and decide whether each context offers `Gui` a named published surface (403 hub → context includes are unbounded today) | Product Owner | the coaching track, or Stage D |
| **The four contexts** (`docs/design/four_contexts.md`, L1–L8; PR-4 in `build/platform/JUDGEMENT-CALLS.md`): Acquisition → Diagnostics → Coaching, with Domain + Application the hub (Q26), with the direction rule R-CTX-1 and the results contract. Today's `src/Coaching` moves to `src/Diagnostics/derived/` and the folder becomes the Coaching context's; Diagnostics' priorities are split from Coaching's work-ons, and the panel says "Priorities" until the coaching track builds adoption (the Design Lead's wording). Lint A9 checks that no context includes a later one. Stage B's libraries stay by ring (L6). Three guards for Stages C and D. Merged into IM v1.10.0 | Solution Architect and Product Owner (L1–L8); Design Lead (the "Priorities" wording) | IM §2, §4, §4.1 and the glossary (done, v1.10.0); Stage B, Part 1 (the move, the renames, A9); Stage B, Part 2 (L6); Stages C and D (the guards) |

---

## Approvals this page needs

Two hard rules apply to everything below (`CLAUDE.md`):

- **Any information-model change needs the Solution Architect's approval before it is built** (IM §1.2, C1–C11).
- **Any design-system change needs the Design Lead's approval before it is built**: a new component, a new variant, a changed
  look or behaviour, a new or changed Theme token.

Each is marked "(proposed, for the Solution Architect)" or "(proposed, for the Design Lead)" where it appears, and
each stage lists its own. Scope and order calls are marked "(proposed, for the Product Owner)". Every stage brief
carries both rules and the IM §10 ownership section, as 04 §1 does for the current refactor. The ones this page
proposes:

| Proposal | Kind | Stage |
|---|---|---|
| A new `src/Platform/` directory, or `src/Core/` made Platform-only, and the file moves out of `Core` | IMC C9 (the precedent is DEC-148: a new `src/Identity` was C9) | B |
| The Vocabulary ring's one root, `src/Vocabulary/<area>/`, and `Metrics/`, `Recording/`, `Identity/` and Core's vocabulary files moved under it | IMC C9: approved, mandatory[^sa23-vocab] | B |
| LaunchMonitor's inferred reads moved to a Rules directory, apart from its connectors | IMC C9 | B |
| The area names inside `src/Vocabulary/`, and the Vocabulary files the first ruling did not name (Buffer's stream types, LaunchMonitor's reading types, `Export/frame_decode`) | IMC C9: the three files ruled in, 24 Nov[^po24]; the area names still to rule | B, D |
| Correct IM's Studio/Rig entry: `StudioProfile` is not a rig object | IMC C1, C2, C3: approved and applied[^dec193] | done (Premise 2) |
| The Rig's design: identity, store, writer, `RigChanged`, a rig reference in each Recording | IMC C1, C3, C4, C5, C7, C8 | C |
| SET-05 and SET-09's rig keys moving into the Rig | IMC C3 | C |
| A device key type | IMC C5 | C |
| The capture service's ring, and where its device half goes | IMC C9 | D |
| When in capture the shot is allocated (`Session::addShot` allocates it[^dec142]) | IMC C8, if the timing changes | D |
| A persisted shot-source type in the Vocabulary ring | IMC C3/C9 | D |
| D-6: the launch-monitor board as a presenter over `SessionSeries`, owned by the session | IMC C1 (an owner) | D |
| The composition root's ring (`AppComposition`) | IMC C9 (lint A1) | E |
| The settings split, where it moves keys that hold domain facts (SET-*) | IMC C3, C4 | E |
| Amending R-CON-5 to allow a helper for long-lived hardware and media threads (DEC-194 is its first instance[^dec194]) | IMC C11 | E |
| Wrapping the kept QML globals into `devices`, `settings` and `logging` objects, if it is done | IMC C1 | E (and C, D, F) |
| Narrowing lint A6 back to P9's wording | IMC (a change to a lint rule's scope)[^dec120] | E, any time |
| P-3: a read accessor for the phase grid on the Recording's reader, with the model browser's scans on `SessionIndex` summaries (ruled 24 Nov[^po24]; no library-level door) | for the Solution Architect: an IMC (C4) only if it changes FILE-SHOT-07's writer or repository, which IM already names as `RecordingRepository` | B (Part 1) |
| 6.3a's D-1: persist the flight-check verdict in the shot's `derived` block | IMC C3 | none of A–F (see "Mandatory carry-ins" (iv)) |
| J-107's J-2: the model fingerprint as a direct call in `Core/pp_model_path.h`, not a hook | approved, 24 Nov[^po24] | B |
| PPCP's files: retiring FILE-LIB-05, a PPCP repository, FILE-SHOT-11's owner | IMC C1, C3, C4 | F |
| The pairing key (`ppcp/pairings`, SET-09): any change of store | IMC C3; a question for the Solution Architect only, citing E56/E57 | none proposed |
| #105: a "not ready" state on a sensor's device card (`PpDeviceCard`) | design system | A |
| Notification coalescing (#79) in `PpNotificationHost`, `PpNotificationCard`, `PpToast` | design system | F |
| #53's coverage rule in `recording_integrity` / `capture_integrity_check` (Rules ring) | a rule change; an IMC if the integrity verdict is a recorded product (IM §7.2): for the Solution Architect | A |
| #183: recording gain, compensation, black level and gamma per stream per swing | IMC C3 (a field family in the swing file) | A |
| 6.4's D-1: the HackMotion `hm.*` values written by capture, not produced by analysis | dropped, 24 Nov: the values are re-tagged derived instead (#239)[^po24] | none |
| Handedness recorded in device-only recordings (R-REC-8) | IMC C3[^j101]: approved, 24 Nov[^po24] | D, with D-5 ("Mandatory carry-ins" (iv)) |

**When these are ruled (proposed, for the Product Owner).** One sitting per stage. Before a stage's first brief is
written, every row above that names the stage, and every §9 question that blocks it, goes to its role as one pack:
each item with a recommendation and with what happens if nothing is ruled, which is that today's behaviour stays. A
stage does not start on an unruled item it depends on. An item that finds a new information-model or design-system
change while it is being built stops that part, keeps today's behaviour, logs the call and carries on with the rest.
The domain refactor stood idle for about 8 hours on one blocking question, built one information-model change before
its approval, and ended with its calls from J-101 on still waiting[^l-calls].

---

## Mandatory carry-ins from the domain refactor

The domain refactor routes the items below here. The heading names the section; it does not make every row mandatory.
Each item says whether it is **mandatory** (a ruling makes it so), **carried** (placed here, but not required by a
ruling), or **open** (a question still to be answered).

### (i) One root for the Vocabulary ring: mandatory

**The ruling.** `src/Vocabulary/` is the one root for every Vocabulary-ring type, with an area folder inside it for
each functional area. `Metrics/`, `Recording/`, `Identity/` and Core's vocabulary files MUST move under
`src/Vocabulary/<area>/` in the platform refactor, and the lint ends with one Vocabulary rule. It is not
optional[^sa23-vocab].

**Where it stands.** Step 7.3 made the first area, `src/Vocabulary/Analysis/` (`45419616`): the stages' result types,
the shot's analysis record, and the files the lint already counted as Vocabulary inside `src/Analysis`. A-11 (the
stages' value types to the vocabulary) is done there, not in #224. At `origin/main`, the lint's ring table has a
`Vocabulary/` rule and no `Analysis/…` Vocabulary entry left.

**What moves, and where it lands.**
- `Metrics/`, `Recording/` and `Identity/` move whole, each to an area folder (for example `Vocabulary/Metrics/`,
  `Vocabulary/Recording/`, `Vocabulary/Identity/`; the names are proposed, for the Solution Architect).
- Core's vocabulary files ("the rest of `Core`" in IM's RING-VOCABULARY, which the lint reaches through its `Core/`
  rule) move to areas by what they are. This is the same work as Stage B's `Core` split: `Core` keeps the Platform
  files, the Vocabulary files leave for `src/Vocabulary/`, and the Adapters and Rules files leave for their rings.

**A gap in the ruling, for the Solution Architect.** "One Vocabulary rule" needs more than the four names the ruling
lists. At `origin/main`, the lint's ring table also places these files in the Vocabulary ring, one by one:
- Buffer's stream types: `types`, `format_descriptor`, `source_descriptor`, `swing_window`, `swing_payload_source`,
  `imu_sample`, `pixel_format_names`;
- LaunchMonitor's reading types: `launch_monitor_reading`, `lm_device`;
- `Export/frame_decode`.

The proposal (proposed, for the Solution Architect) is that these move too, each with the stage that splits its
directory:
- `frame_decode` moves with `Export`'s rename (#224, Stage B Part 1);
- the reading types move with Stage B;
- Buffer's stream types move in Stage D. `swing_window.h` and `swing_payload_source.h` carry the two A1 lines on
  `source_ring.h` that Stage D's handle value type closes. Moving them earlier would carry those lines to new paths.

So the lint reaches one Vocabulary rule at the end of Stage D, not of Stage B, unless the Solution Architect rules
otherwise.

**How.** Every includer of the moved headers is rewritten. That is the same include rewrite as Stage B's libraries, so
both are done in one scripted pass, as 7.3's move was. At the close, 268 files include one of those headers: 212 production files under `src` (26 of them inside the folders that move), 54 test files and 2 under `tools`, with 375 includes in all[^c-vocab].
The lint's Vocabulary entries collapse to the one `Vocabulary/` rule[^sa23-vocab]. Its `Core/` rule then changes from
Vocabulary to Platform, once `Core` holds only Platform files (proposed, for the Solution Architect).

**Stage:** B, Part 1, and the Buffer residue in D (proposed, for the Product Owner; the ruling says "in the platform
refactor").

### (ii) #224's items: carried

**Status.** #224 ("step 7's hygiene items, moved out of the plan", low priority) says its items are "done after the
refactor, or when a later change has to touch that code anyway"[^po23-plan]. The review reports that the Product Owner
agreed to pull #224 into the platform refactor. No other source records that decision, so it stays "reported by the
review; confirm"[^review]. The sources do place three parts of #224 here:
- the Vocabulary root move, recorded on #224 (item (i));
- P-3, the phase grid's Shot door, recorded on #224 (J-104);
- the allowlist lines that the close re-tags `platform refactor (#224)`[^j105].

**#224 does not deliver Stage B's precondition by itself.** Stage B needs the spanning directories split (Premise 3).
#224 takes the domain code out of `Gui`, renames `Export` and re-cuts `Diagnostics`. It does not split `Core`, `Buffer`,
`IMU`, `LaunchMonitor` or `Pose`. Those splits are Stage B's own work, or Stage D's for the capture halves.

**#224 in three groups (ruled 24 Nov[^po24]).**

| Group | Items | Stage |
|---|---|---|
| **#224-B: what Stage B depends on** | domain code out of `src/Gui`, about 5,000 lines by the audit's headline, which has no stated method; the ten rule files F-11 names are all still in `src/Gui/charts`, 8,529 lines at the close[^c-gui] (F-11, D-09, C-08); one namespace per layer and `src/Export` renamed (C-14, B-15, A-20); the Coaching adapter out of Diagnostics (B-08, its non-wrist half); Diagnostics re-cut by layer (B-09); `layoutDag` to the UI ring (B-10); P-3 (item (iv)); the Export store doors that A5 counts from the close (J-88) | B, Part 1 |
| **#224-H: hygiene that no platform stage depends on** | one words module, and domain reductions that persist ids and numbers (C-04, B-10); the report's `everyWords` folded into `frequency_words.h`; presentation strings and "no value" conventions out of the rules (A-12, B-19, C-13); coach wording out of Metrics (A-15); the metric catalogue as data (A-06, the A7 line on `metric_catalogue_manifest.cpp`) and metric keys generated from it (A-07); constants referenced from their rule (B-16); one phase to swing-position mapping (B-17); one `CoachingBands` place (C-12); shared maths (π, `wrapPi`); J-83's D-2 and D-3[^j95] | its own plan, or #224's "when a later change has to touch that code" |
| **#224-U: the step-5 presenter residue** | 26 allowlist lines (113 findings) where a Gui presenter still reads files itself or hands QML untyped shapes: the A2 lines of QML views that create their presenters (the Charts panel, the Consistency view, the three trust views, the data viewer); the A5 reads of the review, replay, report, mailer, charts strip, data viewer and 3-D rig, with the four "re-tag only" copies (item (iv)); the A6 lines of the diagnostics adapter (×29), work-ons (×14), the report model, list, mailer and PDF writer, and two review headers. Also the stale `# step 1` and `# step 4` A6 lines on `reanalysis_controller.h` and `shot_list_model.h` (item (iv))[^g24] | its own plan, scheduled straight after Stage B (ruled, 24 Nov[^po24]). No Stage B library depends on them, and narrowing A6 to P9 (§9 Q5) does not clear the A6 lines, because these are presenters |

**The 34 lines tagged `platform refactor (#224)` at the close, by where they go**[^g24]:

| Where | Lines | Findings | Which |
|---|---|---|---|
| Stage B, Part 1 (#224-B) | 4 | 9 | `measure_sample.cpp` A1 ×1 and A5 ×4 (P-3); `Export/session_list_reads.cpp` A5 ×1 and `Export/session_index_summary.cpp` A5 ×3 (J-88's store doors) |
| Stage D (D-5, D-6) | 3 | 4 | `launch_monitor_controller.cpp` A5 ×1 (the device-only create); `lm_club_art.h` A6 ×2 and `lm_session_model.h` A6 ×1 (the board) |
| #224-H | 1 | 1 | `Metrics/metric_catalogue_manifest.cpp` A7 ×1 (A-06) |
| #224-U | 26 | 113 | A2: 6 lines, 11 findings; A5: 12 lines, 43; A6: 8 lines, 59 (above) |

Why the split:
- 05's first rule is "Pick one group … Don't mix groups in one plan".
- #224-H is analysis and coaching hygiene. D-3 (the route erasers consult the one precedence rule) changes stage
  output, so it needs version bumps and a re-fire. Stage B promises no re-run to analysis (parity 0).
- `layoutDag`'s move takes its A7 line out of A7's scope, which covers the Vocabulary and Rules rings only. The split
  that #222 holds is then not needed to clear the line.

**The metric catalogue as data (A-06), and R-INS-5.** R-INS-5 makes a metric that no device reports an estimate from
every input, with σ, not a pick from one input[^rins5]. #230 inventories every derived metric against it. If the
catalogue becomes data before that inventory, its format would encode today's pick-one routes (`preferKeys` ladders,
the catalogue's route qualities) and be redone. So #230's inventory comes before A-06's format is fixed (proposed, for
the Solution Architect).

### (iii) The QML context globals: open

**Facts.** There were 42 QML context globals before step 5 (04 step 5). There were 18 after 5.8b (`048aca1d`). 5.8b
moved 23 globals under `app` (a new Adapters-ring `GuiApp`) and deleted `ppcpImport`, which had no reader. R-QML-1's
"devices, settings, logging and app" was read as four kinds of global, not four objects, because inventing `devices`,
`settings` and `logging` objects would be an IMC[^j92]. The final count is taken at the close: **18[^c-globals]**.

The 18 at `95997058` (the final report's fact pack, `build/refactor/report/FACTS.md` §8.5): `app`, `appLog`, `appSettings`, `bufferController`, `cameraManager`,
`controller` (the `TranscriptionController`), `cudaRuntime`, `imuManager`, `launchMonitor`, `liveWrist` (the
`LiveWristAngles` view of the IMU), `ppcpHost`, `ppcpOffers`, `profiler`, `resourceMonitor`, `secrets`,
`studioProfile`, `trunkCeremony`, `ttsController`. At the close (`44ae9230`) the list is the same 18 names[^c-globals].

**The question.** Should the kept globals be wrapped into `devices`, `settings` and `logging` objects? It is settled
after the refactor, and may fold into the platform refactor[^j93]. A second question rides with it: three kept
"devices" may not be devices: `launchMonitor`, `trunkCeremony` and `liveWrist`[^j92].

**Recommendation (proposed, for the Solution Architect).** Do not wrap for the count's sake. A wrapper moves a name,
not an owner, and each of the three objects would be an IMC (C1) with no domain gain. Instead, each kept global leaves
with the stage that gives its object a proper owner and a typed API:

| Globals | Stage | Why |
|---|---|---|
| `appSettings`, `secrets`, `studioProfile` | E | The settings split, the secrets store and the studio profile's repository. If a `settings` object comes, it comes from the split |
| `cameraManager`, `imuManager`, `bufferController`, `launchMonitor` | C, D | Devices and the capture seam. IM §4: "a launch monitor is a device like any other", so `launchMonitor` is a device |
| `trunkCeremony` | C | A calibration procedure, whose result belongs to the Rig (IM §2.3). It is not a device |
| `liveWrist` | D | A live view over the IMU's data: a presenter, not a device |
| `appLog`, `profiler`, `resourceMonitor`, `cudaRuntime` | E | Logging, monitoring and machine queries |
| `controller`, `ttsController` | E | Media tools |
| `ppcpHost`, `ppcpOffers` | F | The phone link |
| `app` | stays | The one QML door (R-QML-1) |

Check this table against 18[^c-globals] and the final list at the close. The final report states the before and
after counts, what is left and why, and where it goes next[^j93].

### (iv) The items each J-call routed here

| J-call | Item | Status | Stage | Approval |
|---|---|---|---|---|
| J-90, J-95 (a) | **D-6:** the launch-monitor board as a presenter over `SessionSeries`, owned by the session. Today `SessionReviewController` owns it (`lmBoard`) as an interim owner, because the board shows unsaved live rows and must say "No launch monitor connected" with no session. D-5, the device-only create's writer, is named beside it | carried[^j95] | D (proposed, for the Product Owner): the no-session state is device state, and the device-only create is the capture seam's | IMC C1, for the Solution Architect |
| J-90 | The board's `graphics` map and LmClubArt's anchors: not typed (A6), re-tagged at the close | carried | D, with D-6 | none (removals only) |
| J-92, J-93 | The QML globals | open | item (iii) | IMC C1 if wrapped |
| J-95 | 6.3a's **D-1:** persist the flight-check verdict in the `derived` block, with the ledger rows as fired | carried[^j95] | none of A–F. It is a derived-data format change, not platform work, and is best placed with #224-H or as its own item (proposed, for the Product Owner) | IMC C3, for the Solution Architect |
| J-95 (on J-83) | **D-2:** retire the 27 authored `preferKeys` ladders that equal the default order (a pack format change). **D-3:** the route erasers (`applyTrunkSensorPrecedence`, `applyLeadElbowSensorPrecedence`, the 3-D step-aside) consult the one precedence rule (version bumps and a re-fire) | carried to #224[^j95] | #224-H (item (ii)) | for the Solution Architect; the pack editor is the Design Lead's |
| J-100 | The Vocabulary root | mandatory[^sa23-vocab] | item (i) | approved |
| J-104, J-105 | **P-3:** the phase grid needs a Shot door. `measure_sample.cpp` reads swing documents through `SwingStore` and reads and writes the phase-grid sidecar (FILE-SHOT-07) itself: A1 ×1 and A5 ×4 (unchanged at the close, and now tagged `platform refactor (#224)`)[^c-allow]. It stays because the model browser's gate count and corridor scan read the phase grid folder by folder, with no `Shot` to ask. Proposal: `RecordingReader::phaseGrid(ShotRef, config, policy)`, and a library-level door for the two scans, or the scans moved to the athlete level's `SessionIndex` summaries, as Charts did in 5.5d | ruled 24 Nov[^po24]: the Shot-level reader, with the two scans on `SessionIndex` summaries and no library-level door | B, Part 1, with Diagnostics' re-cut | for the Solution Architect: an IMC (C4) only if FILE-SHOT-07's writer or repository changes |
| J-104 | The norm fallback goes through a hook: the rules declare a factory, and the composition root installs it (`installSharedNormSource`) | done in 7.1 | E: the composition root owns every hook install | none |
| J-105 | The lint accepts one new tag, `platform refactor (#224)` (and `#222` for long-function lines), and every line leaving the plan is re-tagged with it at the close | the close's call[^j105] | 34 lines and 127 findings at the close (A1 1 line and 1 finding, A2 6 and 11, A5 16 and 52, A6 10 and 62, A7 1 and 1)[^c-allow], split four ways in item (ii): Stage B 4, Stage D 3, #224-H 1, #224-U 26[^g24] | a tooling change, with no line added |
| J-88, J-95 | Lint A5 counts the Export store doors that 5.7a and 5.7a-P1 added (`session_list_reads`, `session_index_summary`), tagged `platform refactor (#224)` | approved[^j95] | B, Part 1, with `Export`'s rename | approved |
| J-107 (J-2) | The pose model fingerprint goes through an installed reader hook. The review of 7.2 recommends a direct call, `modelFileFingerprint` beside `modelFilePath` in `Core/pp_model_path.h` (Platform ring, so A5 exempts it), which removes the slot, its three install sites and the `@?` failure mode. The hook stays until ruled | ruled 24 Nov[^j107][^po24]: the direct call | B, Part 1 | for the Solution Architect |
| J-43 (DEC-194) | The pose inference thread is "the first instance of the platform assessment's long-lived-thread helper, private to `PoseRunner` for now" | done[^dec194] | E (threading) | approved |
| J-108 | 6.4's **D-1:** the HackMotion `hm.*` values written by capture. It was not built, and "would therefore move the producer out of the analysis". A question rides with it: tag the `hm.*` values derived, which would reverse 6.1a's J-2 | dropped 24 Nov[^j108][^po24]: the `hm.*` values are re-tagged derived instead (#239), so capture has nothing to write | none | IMC C3 or C4, for the Solution Architect |
| the close (06 §4.5) | **Four local copies left in `src/Gui`, owned "by re-tag only":** the diagnostics adapter's history read (survey row 4d), the work-ons scan and its stamps (9b), the 3-D rig's session view (10c) and markup's overlay read (14b). Their A5 lines are tagged `platform refactor (#224)`, but neither #224's text nor 05 names them[^close-res] | carried by the tag alone | not #224-B: no library depends on them. With #224-U, each named on #224 (proposed, for the Product Owner) | none known; each is answered in its own brief |
| J-110 | `Export/rule_data_reads.cpp` (7.1's reads) is a store door that lint A5 does not count | ruled 24 Nov[^j110][^po24]: A5 counts it | B, Part 1, with J-88's doors | for the Solution Architect (a lint-scope change) |
| J-101 | **P-6.3b-1:** record handedness in device-only recordings (R-REC-8). Today a left-hander's device-only shots take the athlete's hand | approved 24 Nov[^j101][^po24] | D, with D-5's device-only create | IMC C3, for the Solution Architect |
| the close (06 §4.5) | **#234:** the report list and Archiving take "the live session" from the carousel (`main.cpp`), not from `AppState` | open issue | E, part 1: the composition root rewrites those lines (proposed, for the Product Owner) | none: `AppState` already owns the live folder |
| the close (06 §4.5, C-4) | **Three stale tags the close left, outside its brief:** `Analysis/swing_reanalyzer.cpp` A1 ×1 (`# step 1`: it includes `recorded_products.h`, which the lint and IM's RING-ADAPTERS place in the Adapters ring although it sits in `src/Analysis`); `Gui/shot/reanalysis_controller.h` A6 ×1 (`# step 1`) and `Gui/shot/shot_list_model.h` A6 ×1 (`# step 4`). The fourth, `library_converter.cpp` (`# step 3.0`), is item 3.0 (below)[^g24] | carried by a stale tag alone | `swing_reanalyzer.cpp`: B, Part 1, cleared before Analysis is cut as a library that links only inward. The two A6 lines: #224-U (item (ii)). All three re-tagged and named on #224 in the stage's first commit (ruled with #224's three groups, 24 Nov[^po24]) | none (removals and re-tags) |
| 5.10's residue (#235) | (1) A card whose document never lands still loses its edits at End (`forgetCapture`). (2) `handCardEditsToShot` clears the card's copy before it forwards each edit, so a failed write loses that edit | open issue | (2): A, as a bug fix. It loses golfer input, which is always blocking, and the fix is the order (clear after the write succeeds). (1): D, because what a shot with no document is belongs to the capture seam (ruled 24 Nov[^po24]) | none known |

**Not carried here:**
- **J-106's legacy `"DRIVER"` stub** (read a legacy stub as undeclared everywhere, R-CLUB-4): a domain rule for the
  Solution Architect, not platform work[^j106]. Ruled 24 Nov: its own domain item, #238[^po24].
- **Item 3.0** (delete the library converter and the legacy-read paths): its gate was met on 22 Nov and it has not
  started (06 §3). It is domain work with no platform stage; it is named here because no "Next" list holds it.
- **#229 and #230 (R-INS-5).** The low-point estimator from every input (#229), and the audit of every derived metric
  against R-INS-5 (#230). Both are analysis work "after the refactor (freeze)", not a platform stage[^rins5]. The one
  link to this page is A-06's ordering (item (ii)).
- **#222.** See §8.

---

## Lessons from the domain refactor, applied

The domain refactor ran from 14 to 24 Nov 2026 and is reported in 06. An independent review of 06 drew 20 lessons
from it, ranked by how much each changes what the platform refactor proves or costs. This section maps each lesson to
the change it makes on this page. The review's working paper, with the evidence for each lesson and its
disagreements with 06 and with the outside review, is `build/refactor/report/LESSONS-FOR-PLATFORM.md` (a local file,
not in the repository).

**Standing.** Every rule this section adds is a proposal: "(proposed, for the Product Owner)" unless another role is
named. Nothing here changes a mandatory carry-in. The facts each lesson rests on are in the footnotes.

**What the first version of revision 2 lacked.** It placed work and measured it. It carried almost no method: no
entry condition, no review tier, no rule for checks on the cabin PC, no acceptance figures, no rule for lanes and no
rule for unattended calls. Of the 20 lessons it had applied 2, partly applied 10 and missed 8.

| # | Lesson, as a rule | The change on this page | Before this edit |
|---|---|---|---|
| 1 | **Do not build on an unproven base.** The domain refactor ended with its 3-shot live check not run and 17 of its last 24 items never reviewed[^l-live]. The live check runs, and its findings are fixed or filed, before any platform item that changes capture, devices, teardown or the Windows build is committed | §7: entry conditions; §5 rule 1; Stage A | missing |
| 2 | **Hardware evidence is needed; waiting on it is the mistake.** A gate on the Product Owner's sessions held step 3 for 92 hours, and those sessions then found a fault that stored-session parity had passed[^l-gate]. Every cabin check is named in its brief with who runs it, its procedure, its cost and what is built behind it | §5 rule 2; the Test rows of Stages A, C, D, E, F; §9 Q13 | partly |
| 3 | **Windows inside the loop.** Nothing in the domain refactor ran on a Windows test build, and the incremental-build fix is POSIX-only[^l-win]. A scripted test build and `ctest` on the cabin PC exist before Stage B starts | Stage B: precondition; §5 rule 3; §7; §9 Q22 | partly |
| 4 | **An aim is a count.** Step 5's aim was a sentence, measured once, at the end, with its verdict words redefined at the measuring; the survey still found a loss of golfer input that nothing else had[^l-aim]. Each stage states method, baseline, target and allowed exceptions before work, and re-counts at its close | an Accept row in each stage's table; §5 rule 4 | partly |
| 5 | **The review tier is set in the brief, by platform triggers.** The 23 Nov tier was chosen at the commit and applied loosely; where reviews ran they found 25 blocking findings in 21 items[^l-tier] | a Tier row in each stage's table; §5 rule 5; §9 Q21 | missing |
| 6 | **Build the cases the fixed data lacks; any loss is blocking.** The parity set had no left-hander and no skied shot, and a reviewer's constructed data found what tests could not[^l-loss] | §5 rule 6; the Test rows of Stages C, D, E, F | missing |
| 7 | **Rule the principles first, on something concrete.** R-INS-5 and the Vocabulary root were both missing, and both were found when a role saw a concrete result[^l-princ]. Questions are ruled per stage before its brief; a pilot goes to the role before a scripted roll-out; each ruling reaches the IM the same day | §9: the stage each question blocks; Stage B (the pilots) | partly |
| 8 | **A HARD rule holds when a tool enforces it.** The breaches that stopped were stopped by a guarded launcher and a lock, not by brief text[^l-hard]. Each brief lists the real stores and devices it could touch, with the guard | §5 rule 7; Stage B (#187 first); Stage E's Test row | partly |
| 9 | **No suite goes unrun.** `qml_ui` was left out of every "full run" for about 30 items, and 10 checks went red unseen[^l-qml] | §5 rule 8; Stage B's #90 bullet; Stage E | partly |
| 10 | **Approvals in one sitting per stage**[^l-calls] | "Approvals this page needs": when these are ruled; §9 | partly |
| 11 | **Unattended calls have a stated limit.** Applying a principle to a case its text did not name built the wrong thing once (J-96) and moved a plan target once (J-92)[^l-calls] | §5 rule 9 | missing |
| 12 | **Work that moves is listed where it lands.** 29 allowlist lines and four residues reached #224 by a tag alone[^l-scope] | "Mandatory carry-ins" (iv): six rows added; §5 rule 10 | partly |
| 13 | **An item is one reviewable change; inventory a class before splitting it.** Step 3 grew from 12 items to 23 by splitting to fit an agent's context[^l-items] | §5 rule 11; Stage D; §6 | missing |
| 14 | **Two lanes, one lock, and one lane only during a tree-wide rewrite**[^l-lanes] | §7; Stage B | partly |
| 15 | **A close record per row, and a ledger from the first item.** 9 of the audit's 33 Critical and High findings are in `main` with no close record[^l-record] | §5 rule 12; appendix A | partly |
| 16 | **Build cost is measured on both platforms.** 54% of step 3's compiles were QML-cache units, and nobody asked why for a week[^l-win] | Premise 3; Stage B's Test row; §10 row 2.1c | missing |
| 17 | **The method's tools and sources live in the repository.** The IM excerpt tool and the build lock are under `build/`, which git ignores[^l-tools] | Stage B's Tools bullet; "Words used" | missing |
| 18 | **Facts apart from analysis; every count with a method** | already this page's practice ("How things are measured here"); §5 rule 12 extends it to each stage's close | applied |
| 19 | **Keep what worked:** parity as the gate for moves, the ownership answers, approval by role | unchanged: each stage's Re-run and Ownership paragraphs | applied |
| 20 | **Short briefs, excerpts, fresh agents** | §5 rule 13 | missing |

---

## 1. Summary

Steps 1–7 of 04 fix the domain core: the information model, the aggregates, typed identities and the Recording
contract. They leave the platform layer as it is, by design (05 "Purpose").

What this assessment adds to 05:

1. **Premise 2 was the main finding, and it is now corrected.** IM filed the studio profile (FILE-APP-06) under
   Studio/Rig and mapped `StudioProfile` to OBJ-RIG, though it holds report and coach details only
   (`src/Core/studio_profile.h:21–34`). IM v1.9.12 applies the correction as
   DEC-193[^dec193]: OBJ-STUDIO-PROFILE, SET-20, and FILE-APP-06 re-classed.
2. **Build structure is under-weighted in 05** (one row, B-14). Ring-aligned CMake libraries are worth more than that
   row says, but only after the directories that span rings are split, and they enforce lint A1 only (Premise 3).
   Since step 7 was reduced, those splits are Stage B's own Part 1, with #224-B and the Vocabulary root move.
3. **June's concurrency, secrets and execution-provider items were not re-checked in 02.** This page re-checks them at
   HEAD (§4). Most are not shown to be bugs; they are listed as "suspected; check first", and become issues only
   where a failure is observed.
4. **Every 05 row is placed** (appendix A), and the stale 05 rows are named for whoever next updates 05
   (appendix B).
5. **The domain refactor's carry-ins are placed** ("Mandatory carry-ins"), the Vocabulary root first.
6. **The domain refactor's lessons are applied** ("Lessons from the domain refactor, applied"): how each stage is
   entered, reviewed, tested on the cabin PC and accepted (§5, §7), all as proposals.

---

## 2. Five premises, re-weighed

### Premise 1: agree with 05's grouping; run capture as its own plan

05 already splits the work into "platform refactor (capture)", "platform refactor", "(settings)" and "(build)"
(05 "Conventions", §1–§3), and its first step is "Pick one group … Don't mix groups in one plan" (05 "How to use it").
This page agrees, and proposes that capture (Stages C, D and F) runs as its own plan, with its hardware testing on the
cabin PC, apart from the build and settings work (Stages B and E) (proposed, for the Product Owner). The same rule
keeps #224-H out of Stage B ("Mandatory carry-ins" (ii)).

The size of each group, measured as above at `44ae9230` (re-measured at the close of steps 5–7):

| Group | Directories | Production lines | Rings (IM §4.1) |
|---|---|---:|---|
| `src/Core` | `src/Core` (43 files)[^c-lines] | 9,055[^c-lines] | Platform, Vocabulary, Rules (`kf3_smoother`), Adapters (`device_enumerator`, `pp_settings`, `studio_profile`, `app_info`) |
| Build | `CMakeLists.txt` | 3,943[^c-cmake] | none |
| Capture and devices | `Buffer`, `Video`, `IMU`, `LaunchMonitor`, `Gui/cameras`, `Gui/imu`, `Gui/calibration`, all of `Gui/shot` (Buffer 4,045, Video 8,946, IMU 10,219, LaunchMonitor 6,397, Gui/cameras 9,273, Gui/imu 7,605, Gui/calibration 3,978, Gui/shot 11,917)[^c-lines] | 62,380[^c-lines] | all six; IMU fusion and the LM's inferred reads are Rules, not capture |
| Phone link (PPCP) | `src/Ppcp` (47 files)[^c-lines] | 24,871[^c-lines] | Adapters |
| App host, settings, tools, media | `Gui/app`, `Gui/main.cpp`, `Gui/monitor`, `Gui/settings`, `Update`, `Secrets`, `Audio`, `LLM`, `STT`, `TTS` (Gui/app 3,012, Gui/main.cpp 2,101, Gui/monitor 5,135, Gui/settings 11,295, Update 2,678, Secrets 229, Audio 1,950, LLM 767, STT 1,567, TTS 1,380)[^c-lines] | 30,114[^c-lines] | Application, Adapters |

`src/Ppcp` also has a large test suite: 16,625 lines in 23 files, about 40% of its code[^c-ppcptest].
That is a sign of health. The production code of Buffer, Video, IMU and LaunchMonitor together is
29,607 lines[^c-lines], so PPCP
is not larger than those four combined.

### Premise 2: `StudioProfile` is not the Rig (right; corrected)

`StudioProfile` (`src/Core/studio_profile.h:21–34`) holds the studio's and coach's details for reports and emails:
name, logo, accent colour, address, phone, email, website, booking link, practice steps, email subject and body,
coach name, title, email, phone and photo. It holds nothing about cameras, sensors or calibration.

Yet IM classed FILE-APP-06 as "source (rig)", owned by Studio/Rig (§8.1), mapped `StudioProfile` to OBJ-RIG
(§11.2), and listed FILE-APP-06 among "where rig facts live today" (§2.3, §11.1). Because §2.3 says "No new place may
be added before the design (C3) (DEC-140)", the misclassification also constrained work. DEC-193 corrected it[^dec193].

A Recording's reference to the rig (IM §2.3, "each Recording references the rig state it was captured with") must
never go stale because a coach changed a phone number or a logo.

**What DEC-193 decided**[^dec193]:

- The studio profile is a source (the coach's entries), owned at the application level by `StudioProfile`
  (OBJ-STUDIO-PROFILE), held per machine: the Mac and the cabin PC each hold their own. It lives in SET-20 (`Studio/*`,
  new) and FILE-APP-06 (the copied logo and coach's photo). The report reads it.
- `ppcp/studioName` is the machine's own name as phones see it, and stays in SET-09 with the rig facts.
- OBJ-RIG is unchanged: which cameras and sensors, where they are, and their calibration, versioned over time (IM
  §2.3). FILE-APP-06 leaves its list of where rig facts live today, and SET-05 joins it.
- The two allowlist lines were re-worded[^m21nov]: `studio_profile.cpp`'s A5 line is now tagged
  `# platform refactor (settings)`, `studio_profile.h`'s A6 line stays `# unplanned`, and 05 lists both in §2. So
  they are Stage E's.

Only the profile's directory is still open (§9, Q11), with Stage B's `Core` split.

### Premise 3: build structure deserves more than B-14, once the spanning directories are split

05 has one build row, B-14 ("Diagnostics is not a library target"). The build at `44ae9230`:

- `CMakeLists.txt` is 3,943[^c-cmake] lines.
- Internal libraries: two[^c-libs],
  `pinpoint_buffer` (`src/Buffer/CMakeLists.txt`) and `pinpoint_swingstore` (`cmake/swingstore/CMakeLists.txt`).
  Everything else compiles into the `PinPointStudio` executable.
- 103 `.cpp` and `.mm` files[^c-exe] are listed in
  `qt_add_executable`, and 20[^c-exe] more
  `target_sources(PinPointStudio …)` calls add more: 271 `.cpp` and `.mm` names[^c-exe] in all
  (a parse of the calls' arguments).
- 50 include directories (49 source directories and the generated one)[^c-incdirs]
  are given to every file of the app.
- Test targets hand-list the sources they need, and engineers work around the missing libraries:
  `src/Core/atomic_write.h` says it is header-only "on purpose: there is no Core library and every test compiles
  the sources it needs".

**What libraries can enforce, and what they cannot.**

- They can enforce lint A1 (inward-only includes) through `PRIVATE` include directories and `target_link_libraries`,
  but only for a library whose directories each sit in one ring. The directories that span rings must be split first
  (IM §4.1, "Directories that span rings": `Export`, `Analysis`, `Core`, `LaunchMonitor`, `Buffer`, `IMU`, `Pose`,
  `Gui`). Step 7 no longer splits them[^po23-plan]: 7.3 moved `Analysis`'s Vocabulary files to
  `src/Vocabulary/Analysis/` (`45419616`), so `Analysis` loses its Vocabulary part, and the rest are still to split.
  At the close all eight still span by the lint's ring table: `Analysis` has lost only its Vocabulary part, and still mixes Rules, Application and Adapters files[^c-span].
- **Who splits which.** #224-B covers `Gui`'s domain code, `Export`'s rename and `Diagnostics`' re-cut. The Vocabulary
  root move (Mandatory carry-ins (i)) takes the Vocabulary files out of `Core`, `LaunchMonitor` and `Export`. The
  rest of `Core`, and `LaunchMonitor`'s inferred reads, are Stage B's own Part 1. The capture
  halves of `Buffer`, `IMU`, `LaunchMonitor` and `Gui/shot`, and `Pose`'s motion-capture probe, wait for Stage D
  (proposed, for the Solution Architect).
- They cannot enforce A2 to A8: QML creating a domain type, folder-name parsing, file names spelled outside their
  owner, file access outside a repository, the QML boundary, function length, and Analysis reaching past the
  Recording. Those stay with the lint.
- **Quoted includes that resolve only through the global include directories:**
  425, in 144 production files[^c-incs]
  (counted as an `#include "…"` whose path does not exist relative to the including file). Every one of them breaks
  when the include directories become `PRIVATE` to a library. That is the size of the job, and the Vocabulary root
  move rewrites many of the same lines.

**Recommendation.** Promote ring libraries above B-14, as high-leverage work, timed for after the domain refactor and
once the spanning directories are split (Stage B Part 1). Its estimate is made in its brief (Stage B). The case for
libraries is enforcement, not rebuild time (§10, row 2.1c). Build time is still measured before and after, on the Mac
and under Microsoft's compiler, because Stage B changes how everything links and the domain refactor's
incremental-build fix does nothing on a Windows host[^l-win] (Stage B's Test row; proposed, for the Product Owner).

### Premise 4: resolve 05's open decision on the lint scope

P9 forbids "a new `QVariantMap`/`QVariantList` in a presenter's or domain type's public API" (IM §3), and A6 is
written the same way (IM §1.4). In step G the lead widened A6 to every header (and, since the follow-up, slots and
signals), and that call is waiting for the Solution Architect (05 "Decisions waiting": "The lint-scope calls … A6 on every header, not
only presenters").

**Recommendation.** Ask the Solution Architect to narrow A6 back to P9's wording (proposed, for the Solution Architect; a lint-scope change is an IMC[^dec120]). That one decision settles most of 05 §2's A6 lines, instead of re-tagging them `# kept` one at a time. The
lines it would take out of A6's scope are settings, monitoring and tool objects, which are not presenters or domain
types: `app_settings.h`, `markup_controller.h`, `profiler_controller.h`, `resource_monitor_controller.h`,
`swing_zip_exporter.h`, `app_info.h`, `llm_controller.h`, `transcription_controller.h`, `LlmWorker.h`,
`archive_controller.h`, and `studio_profile.h` (in 05 §2 since DEC-193).
At the close each is unchanged: `app_settings.h` 26, `markup_controller.h` 11, `profiler_controller.h` 8, `resource_monitor_controller.h` 4, `swing_zip_exporter.h` 2, `app_info.h` 1, `llm_controller.h` 1, `transcription_controller.h` 1, `LlmWorker.h` 1, `archive_controller.h` 1, `studio_profile.h` 4[^c-allow].

Whatever the ruling, the shapes that carry rig and capture facts are worth typing, because they will hold domain facts
once the Rig exists: the camera and IMU device objects, the trunk ceremony recorder and the buffer controller (Stages
C and D), the PPCP host service (Stage F), and the rig keys inside `AppSettings` (Stage C) (proposed, for the Solution Architect).
Appendix A places every line.

### Premise 5: PPCP is in 05; June's concurrency, secrets and execution-provider items are not

PPCP is in 05 §1: its first row ("connectors"), its A5 row (seven files) and its A6 row (`ppcp_host_service.h`).
IM gives its files ids (FILE-LIB-03…05, FILE-SHOT-11), all homed in "platform refactor (capture)", and the pairing
store's file lines are capture's[^m15nov] (05 §1, citing
`build/refactor/step-2/JUDGEMENT-ANSWERS.md`). It is not missed.

The real gap is that 02's "The June review, re-checked" table leaves out June's concurrency, secrets and
execution-provider items. §4 re-checks them. They should be checked on hardware where needed, then filed as issues
only where a failure is observed.

---

## 3. What is sound: keep it

- **The Platform ring's new pieces**: the worker pool and its receiver (`src/Core/work_pool.*`, step 3.1), `atomicWrite`
  (`src/Core/atomic_write.h`), `FolderLock`, `post_shot_trace`, `cpu_topology`. They are domain-free and tested.
- **The pose inference thread** (`src/Pose/inference_thread.h`, DEC-194[^dec194]): one long-lived thread, the only
  caller of the cached ViTPose sessions, stopped before they are torn down at exit. It is pure C++ with no Qt
  dependency, so the tools use it too.
- **The ring buffer in `src/Buffer`.** Leave its internals alone. Two narrow changes only: the `fetch_add` in §4, and
  the frozen window's handle (Stage D).
- **The updater's back-end split is done.** `src/Update` has one back-end per platform behind `UpdateBackend`
  (`linux_appimage_backend`, `mac_sparkle_backend`, `win_sparkle_backend`, `inert_update_backend`), so the June
  review's Theme J is stale. What is left is its A1 lines: it includes `AppSettings` and `SessionController` (Stage E).
- **The secure credential store in `src/Gui/report/mail_credentials_{mac.mm,win.cpp,linux.cpp}`**: macOS Keychain,
  Windows Credential Manager, a Linux fallback and an in-memory test mode. It is a candidate home for the API keys
  (Stage E), not for the pairing key (§4).
- **Real-time capture loops keep their own threads**: the `EventBuffer` merger, the Spinnaker and Aravis frame
  callbacks, BLE I/O and the PPCP accept loop. They have latency and blocking needs a shared pool cannot meet. Do not
  force them onto the pool (Stage E).
- **N cameras from day one in the Rig** (#180 adds a third permanent perspective; #74 and #178 add per-bay
  calibration). This is the right constraint for the Rig (Stage C).
- **The measured map of the capture seam**: `ShotProcessor`'s A1 findings and `ShotController`'s
  (13 in the `.cpp` and 4 in the `.h`, and 3, unchanged at the close)[^c-allow],
  and `ShotProcessor`'s five jobs (Stage D). They are a good starting map for the capture brief.
- **The headless probe** (`tools/probe/run_headless.sh`, `src/Core/headless_probe.h`, `0b555ca7`): it guards the real
  homes and libraries and holds off Bluetooth scans and the pose warm-up. Stage E's probe harnesses and Stage B's
  test-isolation helper build on it rather than repeat its guards (§10, row 2.5b).
- **#187's cross-platform test-isolation helper.** The issue confirms the macOS leak; it is cheap (Stage B).

---

## 4. The June items and the exit crash, at HEAD

The project's rule is that a bug needs an observed failure. So each item is either "shown" (with the evidence) or
"suspected; check first". This table is at `44ae9230`; its line numbers are re-checked when each stage is planned.

| Item | June's claim | State at HEAD | What to do | Stage |
|---|---|---|---|---|
| `source_published_` (`src/Buffer/event_buffer.cpp:232`) | a non-atomic read-modify-write from several producer threads can stall the merger | The read-modify-write is real. The worst case is a missed wake, bounded by `merger_cold_timeout_us = 500` (`event_buffer_config.h:38`, used at `event_buffer.cpp:578`): at most 0.5 ms of latency, not a stall. Suspected, not observed | Make it `fetch_add`: cheap and plainly right | A (after the freeze) |
| `BleAdapterPool::m_index` (`src/IMU/ble_adapter_pool.cpp:56`) | a data race across per-IMU I/O threads | Not shown. Both callers run on the GUI thread before they hand off to the I/O thread (`src/Gui/imu/imu_instance.cpp:496`, `hm_instance.cpp:867`), and both are Linux-only | Check first; make it atomic only if a second thread is found | A (after the freeze) |
| `DeviceEnumerator::m_devices` (`src/Core/device_enumerator.cpp`) | mutated and iterated with no mutex | Not shown. BLE results reach it through a queued connection to the GUI thread (`device_enumerator.cpp:370–371`). The threads of the video and PPCP callers are not established | Check first; assert the GUI thread at the entry points | D |
| `PersonSegmenter` (`src/Pose/person_segmenter.cpp:42`) | runs CPU-only, with no execution-provider cascade | Shown in the code: it sets no execution provider, while the three pose estimators each carry their own cascade. Its session is cached for the process (`PersonSegmenter::acquireShared`), the pattern that leaked GPU memory in ViTPose when called from a new thread each shot (DEC-194)[^dec194]. On the CPU that leak cannot occur; on CUDA it is suspected, not observed | A refactor that changes analysis output: after the freeze, with a stage-version bump and a re-run (Stage A). If GPU segmentation is built (#240), check for the leak first | #240, out of this plan (ruled 24 Nov[^po24]) |
| `ppDebug()` (`src/Core/pp_debug.h:59`) | a dangling-`else` bug | The hazard is real: `if (false) PpLogStream(…)` captures a following `else`. A scan for `if (…) ppDebug() …; else` found no such site. Latent, not observed | Harden the macro | A (after the freeze) |
| Gemini key in the URL (`src/LLM/GeminiLlmEngine.cpp:66–70`) | key in the query string; default model id broken | The key in the URL is confirmed. No evidence is given that the default model id (`GeminiLlmEngine.h:44`) is broken | Move the key to a request header; check the model id before changing it | E |
| API keys in plaintext settings (`src/Secrets/SecretsManager.cpp:37–50`, seeding at `:60–84`) | Azure, Gemini and AssemblyAI keys in the settings file | Shown: `secrets/*` through `ppSettings()`, with a compile-time AssemblyAI default and environment overrides | Move the API keys to a keychain-backed store (Stage E) | E |
| The PPCP pairing root key | June did not raise it; the first draft of this page did | It is stored in settings through `ppSettings()` on purpose, "NOT `src/Secrets/SecretsManager`", and the reasons are recorded at `src/Ppcp/ppcp_pairing_store.cpp:19–70` (libppcp errata E56 and E57, commit `1616016d`): in the keychain, Windows and Linux could persist no pairing at all, and macOS prompted on every relink. The cost, a key readable by anything running as the user, was "judged and accepted" | Leave it where it is. A change is only a question for the Solution Architect, citing E56/E57 (§9, Q7) | none |
| The PPCP statics in `main.cpp` | exit-crash bugs | The crash was observed on 23 Aug and **is fixed**: `aboutToQuit` stops the host and drops the borrowed ledger while everything is alive (`src/Gui/main.cpp`, "Stopping here fixes it at the root"). What remains is design debt: function-local statics whose order of destruction matters, and (since steps 3–5) six manual teardown lines after `exec()` (Stage E) | Own them in the composition root (Stage E) | E |
| The pose warm-up at exit (`main.cpp`, `AbandonPoseWarmUpAtExit`) | — | A quit during the warm-up ends the process early, on purpose, and now stops the pose inference thread first (`PoseRunner::stopInferenceThread()`). The Windows path is "NOT TESTED" | Test a quit during the warm-up on the cabin PC before Stage E changes teardown | E |

---

## 5. The stages

The stages are named A–F so they do not collide with the IM's principles P1–P11. Each gives what it closes, deletes,
tests, documents and re-runs, as 04's steps do, and its ownership answers (IM §10) in brief, or says that they are
answered when the stage is planned.

Two facts apply to every stage:
- **There is no CI that runs tests.** `.github/workflows/release.yml` is the only workflow; it builds releases, is
  "authored, not yet run", and runs no `ctest`. Tests are run by hand, through `ctest`.
- **Windows is the primary platform.** Studios run Windows with an NVIDIA GPU. A stage that changes build,
  teardown, threading or devices is tested on the cabin PC, not only on the Mac.

Estimates are made in each stage's brief. What limits the pace is what 04 §4 names: builds on one machine, the hours a
GOLFSIMPC re-run takes, review rounds and the Product Owner's testing; and here, the cabin PC with real cameras, IMUs and phones.

**Rules for every stage** (all proposed, for the Product Owner; each comes from a lesson in "Lessons from the domain
refactor, applied", whose number is in brackets). A stage's brief carries them.

1. **Entry (1).** No item that changes capture, devices, teardown or the Windows build is committed until the
   domain refactor's live check has run on the cabin PC and its findings are fixed or filed. Work may be built and
   reviewed behind it.
2. **Checks on hardware (2).** Each check that needs the cabin PC, real devices or the Product Owner's hands is
   named in the brief with: who runs it (agents do not launch the app on real devices), a script or a written
   procedure, its cost in the Product Owner's time, and what is built behind it meanwhile. Checks are batched into
   one cabin visit per stage where possible. A fault a live run finds gets a test shaped like the live case.
3. **Windows (3).** A stage's Windows checks run from a script on the cabin PC. The cabin PC is the working studio
   and shares its GPU with the launch monitor's software, so its time is booked with the Product Owner.
4. **Acceptance (4).** The stage's Accept row is its aim as counts: the method, the baseline, the target and what
   is an allowed exception, fixed before work starts. The counts are re-taken at the stage's close by an agent that
   built none of it. Anything left gets an owner named in words on an issue or a plan; a tag alone is not an owner.
5. **Review tier (5).** The tier is written in the brief, not chosen at the commit. An independent review is
   required for: a write of source data, settings, secrets or pairings; a file-format change; a deletion or a split
   of a class; a change to teardown, thread lifetime or capture timing; anything a golfer sees; and anything parity
   cannot show. A coordinator's self-check is enough only for a scripted move that is shown by parity (0
   differences), both lints, and a clean build and the full test run on both platforms.
6. **Evidence and loss (6).** The brief lists the cases its fixed data lacks and builds them as scratch data.
   Anything that can lose a shot, golfer input, a setting, a pairing or a key on a supported platform is blocking,
   however unusual the path. Other edge cases are filed and parked.
7. **HARD rules and what the work can touch (8).** The brief carries one standing block: the two approval rules
   ("Approvals this page needs"); the app is launched only through `tools/probe/run_headless.sh`; no debugger; no
   question prompts in an unattended run; no allowlist line added to pass a lint. It also lists the real stores and
   devices the work could touch (the settings store, the keychain, the library, the Trash, Bluetooth, cameras,
   pairings), each with its guard. Where a guard can be a tool, it is a tool.
8. **Tests (9).** The brief names every suite that covers what the item touches, and the item runs them. A suite
   left out of an item's run has a stated reason and runs at the stage's close. An item that changes QML or a QML
   global runs the `tst_*.qml` files covering it. A stage's close runs every suite, both lints and the whole
   `qml_ui`, on the Mac and on the cabin PC.
9. **Unattended calls (11).** An agent may apply a stated rule to a case its text names. It may not stretch a rule
   to a case the text does not name, re-read an acceptance target, add scope, or add an allowlist line: those are
   logged for the role that owns them, and today's behaviour stays. Each logged call is short and gives a
   recommendation, what follows if the role disagrees, and how to undo it. One id series runs for the whole platform
   refactor. Calls a later stage depends on are cleared before that stage.
10. **Moved work (12).** Work that leaves a stage is listed by name on the issue or plan that receives it, in the
    same commit. Each stage's close checks for orphans: anything gated, deferred or re-tagged that no list names.
11. **Items (13).** One page of numbered deliverables; nothing added after the item starts. An item is one
    reviewable change, and is split only where its parts can be reviewed apart; a scripted mass move is one item.
    Before a class is split or deleted, its duties are inventoried with no code, and the behaviour to keep is pinned
    by tests. Tests are re-pointed, never deleted. Each item has a build budget, and gets one review and one delta
    check.
12. **Records (15, 18).** A ledger from the first item: tokens, builds, the review's outcome and the time the item
    waited on a role. At each stage's close, appendix A's rows for that stage are marked closed, moved or left, with
    the evidence, in a fact table written by an agent that did not run the stage.
13. **Reading (20).** A brief quotes what its agent must read. No agent reads IM or this page whole. A fresh agent
    per item.
14. **Process documents (06 §6 item 6).** A brief, a NOTES file and a review are a page each. At the stage's close they
    are archived out of the working tree, and only the ledger, the decision log, IM and this page stay as lasting
    records. The domain refactor wrote 753,000 words of process documents for 47,500 lines of product, which 06 calls
    "the clearest excess"[^g24] (proposed, for the Product Owner).

### Stage A · Bug fixes

04 §1 says "Bug fixes go to Stage 0 or to the step that owns the area". The freeze ends when step 7 is done (04 §1),
so the observed bugs below go first, and the rest follow as refactors (§9, Q4).

**Observed (on hardware or in the code):**
- **#184**: the swing-detection sensitivity subtitle says the opposite of what the setting does (`GeneralPanel.qml`,
  `SettingsIndex.qml`). Wording only.
- **#105**: a Witmotion can connect without its vertical-install setting, and nothing notices (`src/IMU/`). Log the
  read-back with `ppInfo`/`ppWarn`, retry the write, and show the sensor as not ready. Showing "not ready" on the
  device card is a design-system change (`PpDeviceCard`) (proposed, for the Design Lead).
- **#183**: the cameras pick their own brightness target. The issue's own first step comes before any change: compare
  the camera's settings after Swing Catalyst opens it and after PinPoint does, with the bay lit and a bag standing in.
  Then take control of exposure compensation on connect (`src/Video/spinnaker_settings.cpp`,
  `applyConnectSettings`). The issue says the 2 Nov darkness "has two causes mixed together" (one bay light was on),
  so the fix is not claimed to explain all of it. Recording gain, compensation, black level and gamma per stream per
  swing is a field family in the swing file: an IMC (C3) (proposed, for the Solution Architect), and a `swing_json_schema.md` change.
- **#53, the detection half**: a swing whose video starts after impact passes the capture check. Add a coverage check
  in `recording_integrity` / `capture_integrity_check` (Rules ring). The capture cause is unknown (the issue says
  "perhaps because …"); finding it is Stage D's.

**Refactors and unobserved items:**
- The `fetch_add`, the `BleAdapterPool` check and the `ppDebug()` macro (§4).
- **The execution-provider cascade, one copy for the three pose estimators.** It removes duplication and is expected
  to change no output; the parity run must show 0 differences, because the three copies may differ in their options
  or order.
- **GPU segmentation is out of this plan** (§9, Q10; ruled 24 Nov[^po24]). It changes segmentation output for a
  small gain, so it is its own issue, #240, with the long-lived thread, the cabin memory bench beside Foresight
  Premier and the re-run written there.

| | |
|---|---|
| Closes | #184, #105, #53 (its detection half); #183 (its settings half) once its comparison is in (held, 24 Nov[^po24]); #235's half (2), the edit cleared before its write ("Mandatory carry-ins" (iv)); June's `source_published_` and `ppDebug()` rows (the segmenter rows go with #240) |
| Deletes | the three copies of the execution-provider cascade |
| Test | #105 and #183 on the cabin PC's sensors and cameras; #53 against a swing known to start after impact; the cascade under parity (0 differences); `ctest` for the touched targets |
| Re-run | #53's coverage rule: the Product Owner decides whether existing swings are re-graded |
| Docs | `docs/design/flir_camera_settings.md` and `docs/reference/swing_json_schema.md` (#183) |
| Tier (proposed, for the Product Owner) | Independent review: #53's rule, #183's fields (a format change), #105's retry and the cascade. Self-check: #184 (wording), the `fetch_add`, the macro |
| Accept (proposed, for the Product Owner) | Each closed issue has its evidence from the cabin PC attached; the cascade's parity run shows 0 differences; copies of the cascade 3 → 1 |
| Cabin visit (proposed, for the Product Owner) | One visit carries the domain refactor's 3-shot live check (§7's entry condition), #183's comparison, and #105's and #183's checks. The Product Owner runs it from a written procedure; the refactors and Stage B's Part 1 are built behind it |

**Ownership.** #183's new per-stream fields: source facts of the Recording (shot level), written once by capture
through `RecordingRepository::create` (R-REC-2), Vocabulary types in `src/Recording` (by then
`src/Vocabulary/<area>/`), read by a view through the shot. #105's readiness: application state of the device object,
not persisted. #53's rule: answered in its brief, with whether the integrity verdict is a recorded product (IM §7.2).
Ids: FILE-SHOT-01 (the swing file's capture block), R-REC-2, RING-RULES.

### Stage B · Build libraries, the `Core` split and the Vocabulary root

After the domain refactor. Its measurements are redone then. It has two parts.

**Before Part 1 (all proposed, for the Product Owner).**
- **A Windows test lane.** A scripted build of the test tree and a `ctest` run on the cabin PC, with its result
  recorded as the baseline. This stage's Test row asks for the full `ctest` there, and no source read for this page
  says that has been done: 06 records that no Windows test build ran the domain refactor's one Windows-only fix[^l-win].
  If the cabin PC cannot host it, the brief says what stands in.
- **#187 first.** The test-isolation helper lands before anything else in this stage, and before any settings or
  secrets work in Stage E, because tests have already written into the real application data and the real Trash[^l-hard].
- **Build times.** A clean build, a one-header edit and a link are timed on the Mac and on the cabin PC.
- **The two pilots, for the Solution Architect.** The area names are ruled on one Vocabulary area's proposed layout,
  and the first library (Platform) is cut and linked under Microsoft's compiler, before the scripted pass runs over
  the rest. The Vocabulary move stays mandatory[^sa23-vocab]; only its names and its script wait on the pilot.
- **One lane.** While the scripted include rewrite runs, no other lane has code open under `src`. The script is kept
  and re-runnable, as 7.3's was for the other lane's rebase[^l-lanes].

**Part 1: the splits the libraries need.**
- **The Vocabulary root move** ("Mandatory carry-ins" (i)): `Metrics/`, `Recording/`, `Identity/` and Core's vocabulary
  files go under `src/Vocabulary/<area>/`; LaunchMonitor's reading types and `Export/frame_decode` go with them, and Buffer's stream
  types in Stage D (ruled 24 Nov[^po24]). The areas: `Vocabulary/Metrics`, `Recording` (with `frame_decode`), `Identity`,
  `LaunchMonitor`, `Buffer` (Stage D), `Club` (`club_standards`, `club_vocabulary`) and `Common` (Core's other
  vocabulary files). Three `Core/` headers are not vocabulary and stay in Platform, although the lint's
  `fileRings()` counts them as Vocabulary today because it was not updated when they were added: `headless_probe.h`
  (the process environment and which devices start, 4-c), `same_folder.h` (path spelling per operating system, 6.1a)
  and `version.h` (the release version and build number). `fileRings()` gains Platform entries for them before the
  move script runs, so the script does not carry them (proposed, for the Solution Architect; IM §4.1)[^g24].
- **Re-analysis' A1 line** (`Analysis/swing_reanalyzer.cpp`, stale `# step 1`; "Mandatory carry-ins" (iv)): cleared
  before Analysis is cut as a library, because a library that links only inward cannot include
  `recorded_products.h`, an Adapters-ring file inside `src/Analysis` (lint `fileRings()`; IM RING-ADAPTERS)[^g24].
- **#224-B** ("Mandatory carry-ins" (ii)): domain code out of `src/Gui`, one namespace per layer, `src/Export` renamed,
  the Coaching adapter out of Diagnostics, Diagnostics re-cut by layer, `layoutDag` to the UI ring, P-3 (a Shot-level
  `RecordingReader::phaseGrid(shot)`, with the model browser's two scans on `SessionIndex` summaries; ruled 24
  Nov[^po24]), and the Export store doors, with `rule_data_reads.cpp` now counted by A5 (J-110, ruled 24 Nov).
- **`Core` holds only the Platform ring** (proposed, for the Solution Architect; IMC C9), in `src/Core/` or a new
  `src/Platform/`. `kf3_smoother.h` moves to a Rules directory (it is already Rules by IM's RING-RULES);
  `pp_tuned_constants.h` and the other Vocabulary files go to `src/Vocabulary/`. The studio profile moves to `src/App/` (§9
  Q11, ruled 24 Nov); the device enumerator moves with Stage D, because it is capture work. `pp_model_path` gains
  `modelFileFingerprint`, replacing the pose stage's fingerprint hook (J-107's J-2, ruled 24 Nov[^po24]).
- **`LaunchMonitor`'s inferred reads** go to a Rules directory (proposed, for the Solution Architect; IMC C9). The
  connectors, the Buffer stream types, `IMU`'s device back-ends and `Pose`'s motion-capture probe wait for Stage D.
- **The four contexts' moves** (ruled 24 Nov, PR-4; IM v1.10.0). Today's `src/Coaching` files move to
  `src/Diagnostics/derived/` in the scripted move, and `src/Coaching` becomes the Coaching context's folder (IM §4).
  The priorities' types are renamed with them (for example `WorkOnClass` → `PriorityClass`; IM GL-71), and the
  panel's "Work-ons" heading becomes "Priorities" (the Design Lead's wording); `work_ons.json` keeps its name. Lint
  A9, no context includes a later one (IM §1.4, R-CTX-1), lands with the folder moves, reading IM §4.1's Context
  column, with no allowlist line; it places the rows that column marks "to place in Stage B". Before it lands, the
  includes it would reject today are cleared or ruled (counted on 24 Nov by grep; the review of the information-model
  merge, `build/platform/D/REVIEW-D1.md`): the pipeline's includes of the domain model (`Analysis/analysis_service.h`,
  `swing_reanalyzer.h`, `reanalyse_and_persist.h`), which A9 allows as contracts (§9 Q26, ruled 24 Nov); `measure_sample.cpp`'s
  store read, which P-3 removes; and the drill pack's Diagnostics includers, which keep it tagged Diagnostics until they
  move (IM §2, "The packs"). `Vocabulary/Club`, `Vocabulary/Common` and `Identity` are tagged Shared, usable by every
  context; `Recording` stays Acquisition; `trunk_calib_provenance.h` moves to `Vocabulary/Common` (§9 Q27, ruled 24 Nov; IM
  v1.10.1 §4.1). Four more must be cleared before A9 lands (found by the context-map include check, 24 Nov): **`Recording/recording.h`** includes
  `Vocabulary/Analysis/swing_analysis.h` for `BallTrack2D`, `BallBaselineRef` and `ImuSegmentBinding`: capture-time facts
  in the Recording contract, so they move to `Vocabulary/Recording` (Acquisition) when Part 1 splits `swing_analysis.h`, with `BallSample2D`,
  which `BallTrack2D` holds. `SegmentRole` (`segment_role.h`), which `ImuSegmentBinding` holds and `Gui/imu` also uses,
  goes to `Vocabulary/Common` (Shared, Q27's rule), and IM §4.1's RING-VOCABULARY text drops "the segment roles" from
  `Vocabulary/Analysis` with it. `TrunkCalibProvenance` is already going there (Q27); and
  **`Video/video_overlay_pose.*`** includes the pose estimator. It is compiled into the app (`CMakeLists.txt:2709`) but no
  code creates one: only comments name it, and `PpCameraFrame.qml` draws the skeleton itself. So it is deleted, with its
  CMake lines, and `video_overlay_base.*` with it (no other subclass or user), its comments re-pointed to
  `PpCameraFrame.qml`'s skeleton (a deletion: independent review). Two more follow from the placements A9 makes in Stage B:
  `IMU/hm_session_worker.cpp:28` includes `hm_frame.h` and uses nothing from it, so the include is deleted; and `Ppcp`'s
  three includes of `Core/pp_settings.h` (`ppcp_host_inventory.cpp:30`, `ppcp_host_service.cpp:47`,
  `ppcp_pairing_store.cpp:73`) are cleared by `pp_settings`' placement (Platform, as a `QSettings` handle; proposed, for
  the Solution Architect, IMC C9) or by injecting the two settings. When A9 is built, note that it checks direct includes
  only: `recording_reader.h`'s own includes reach every file that includes the contract.
  **The context map is kept for later** (the Product Owner, 24 Nov; PR-9): A9 is built as R-CTX-1's order, which rejects the
  same real problems today.

**Part 2: the libraries.**
- **The libraries stay by ring** (ruled 24 Nov, L6 of the four contexts): a library splits by context only where a
  clean seam already exists, such as the IMU's signal maths against its golf fusion. Lint A9 enforces the context
  boundary; the libraries don't have to.
- **Ring libraries** (proposed, for the Solution Architect): one static library per ring, starting with Platform, then Vocabulary,
  Rules (B-14 is the Diagnostics part of it), Domain model and the stores. Each links only inward; its include
  directories are `PRIVATE`. The quoted includes that rely on the global paths (Premise 3) are rewritten as each
  library is cut, in the same pass as the Vocabulary move's. Until Stage D, `Buffer`, `IMU` and `Pose` still span
  rings, so the Vocabulary and Rules libraries take only their single-ring files, the rest stays in the executable on
  the global include paths, and the lint keeps guarding A1 there (proposed, for the Solution Architect).
- **Tests link libraries** instead of hand-listing sources.
- **#187**: one test-isolation helper for every suite (`QStandardPaths::setTestModeEnabled` or a temporary
  directory), in place of `XDG_DATA_HOME`, which Qt ignores on macOS. It shares the headless probe's guard against the
  real homes (`src/Core/headless_probe.h`) rather than writing a second one (proposed, for the Solution Architect).
- **#90**: the stale `session_setup_ui` and `qml_ui` suites. Writing new UI tests waits for the Product Owner's
  say-so. Running the existing ones does not: that reading left `qml_ui` unrun for about 30 items of the domain
  refactor[^l-qml]. So an item here that changes QML runs the `tst_*.qml` files that cover it, and the stage's close
  runs the whole `qml_ui` (§5 rule 8; proposed, for the Product Owner). The domain refactor's close ran it once, and
  #231 (ChartsPanel test_15) is its one known red check.
- **Tools**: `swinglab_run --doc-blocks` exits non-zero when its document write fails; `parity_baseline.py
  --self-test` works with other arguments (05 §3). Both need a build or a tools change, which this stage has. Two
  tools the method depends on move into `tools/`, with a self-test each, first in the stage (ruled 24 Nov[^po24]; the changelog merge helper goes too): the IM
  excerpt tool (`build/refactor/im_excerpt.py`), which the "never read IM whole" rule relies on, and the build lock
  (`build/refactor/one_at_a_time.sh`). Both sit under `build/`, which git ignores[^l-tools].
- **The per-release re-run of the audit tools** (05 §3, 03 §14b) gets an owner: the release runbooks (proposed, for
  the Product Owner).

| | |
|---|---|
| Closes | the Vocabulary root (the lint's one Vocabulary rule, apart from Buffer's stream types, which close in Stage D); #224-B and the 4 allowlist lines tagged `platform refactor (#224)` that it covers ("Mandatory carry-ins" (ii)); `Export/rule_data_reads.cpp`'s A5 line, counted from this stage (J-110, ruled 24 Nov) and cleared in it, so 5 lines in all; `swing_reanalyzer.cpp`'s A1 line; B-14; 05 §3's tool rows; #187, with #203 beside it (probes that send scratch sessions to the real Trash); #228 first, so that the full `ctest` baseline is clean before the first library is cut; #90's suites (with the Product Owner's say-so) |
| Deletes | the lint's per-directory Vocabulary entries; the hand-listed source lists in test targets; the header-only workarounds such as `atomic_write.h`'s |
| Test | a clean build and the full `ctest`, with `qml_ui`, on the Mac and on the cabin PC (Microsoft's compiler, MSVC, links static libraries differently), run from the Windows test lane's script; the three build times against their baseline |
| Re-run | none to analysis expected; the parity run must show 0 differences |
| Docs | IM §4.1 (RING-VOCABULARY, RING-PLATFORM, the spanning directories, and the Context column's "to place" rows); IM §1.4 (A9 built); `docs/developer/testing_developer_guide.md` (the isolation helper) |
| Tier (proposed, for the Product Owner) | Self-check for the scripted moves, on the evidence of §5 rule 5. Independent review: P-3's accessor, #187's helper, the first library, and every CMake change that alters what a test links |
| Accept (proposed, for the Product Owner) | The lint's per-file Vocabulary entries: down to Buffer's stream types only. Directories that span rings: 8 → those Stage D owns, named in the brief. Quoted includes that resolve only through the global directories: 425 → the number left in the executable's own files, set in the brief. Allowlist lines tagged `platform refactor (#224)`: 34 → 30, with Stage B's 4 at 0 and the other 30 (Stage D 3, #224-H 1, #224-U 26) each named on #224. `Export/rule_data_reads.cpp`'s new A5 line: added and cleared in the stage, so Stage B clears 5 lines. `swing_reanalyzer.cpp`'s A1 line: gone. Test targets that hand-list sources: to 0. Build times: no worse on either platform, or accepted by the Product Owner |

**Ownership.** No domain data. Each moved file keeps its ring, except where the move is the point (C9: the new
directories, and the RING-* rows that name `Core` and the Vocabulary areas). P-3's accessor is answered in its brief
(FILE-SHOT-07, `RecordingRepository`, shot level). Ids: RING-PLATFORM, RING-VOCABULARY, RING-RULES, B-14,
FILE-SHOT-07.

### Stage C · The Rig and calibration

The first stage of the capture plan. The IM correction of Premise 2 is made[^dec193]; the Rig's design is an IMC
(C1, C3, C4, C5, C7, C8) for the Solution Architect before anything is built.

Where rig facts live today (IM §2.3): settings SET-09 (`camera/*`, `imu/*`, `ppcp/studioName`, `ppcp/pairings`);
SET-05 (`analysis/clubLenPrior`, learned rig state per machine[^dec153]); the trunk ceremony FILE-SESS-11; and
assumptions in code ("fixed cameras are square", the uncalibrated down-the-line yaw σ, `pp_tuned_constants.h`).
3,978[^c-calib] lines of calibration QML run state machines,
quaternion maths and pass/fail thresholds.

What to design (all proposed, for the Solution Architect):
- **OBJ-RIG with an identity, a versioned snapshot and one writer**, for N cameras and sensors (#180, #74, #178).
  Where it lives is two questions (§9, Q2): where the capture machine keeps the rig it captures with, and how a past
  Recording's rig is read on another machine.
- **A rig reference in each Recording.** Today only IM prose reserves it; the code says "There is no rig reference
  yet" (`src/Recording/recording.h`). Adding it is a swing-file format change, with libppswing's backward
  compatibility: a swing without it reads as "rig unknown".
- **`RigChanged` (EVT-15)**: a calibration change makes a new snapshot and never alters a past Recording.
- **What the Rig holds** is IM §2.3's: the cameras and sensors, where they are, and their calibration. Exposure and
  gain are per-stream capture facts in the Recording (Stage A, #183), not rig facts.
- **One device key type** in the Vocabulary ring (IMC C5), in place of the open-coded `description + "|" + id`
  (7 sites in 5 files[^c-key] by a grep for `"|"` beside a
  serial, description or id: `camera_instance.cpp`, `camera_manager.cpp`, `imu_role_map.h`,
  `resource_monitor_controller.cpp`, `shot_processor.cpp`).
- **One perspective type (A-13)**: `CameraInstance::Perspective` (`camera_instance.h`) and
  `kImpact = 4` (`app_settings.cpp`) give way to `recording::Perspective` (`recording.h`).
- **Calibration procedures in C++.** The procedure belongs to Acquisition and its result to the Rig (IM §2.3). Pull
  the thresholds, maths and step transitions out of `ImuCalibrationFlow.qml`, `TrunkRoutine.qml` and
  `HackMotionArmRoutine.qml` into tested C++ that produces a typed result and commits it to the Rig. The trunk
  ceremony recorder's file (FILE-SESS-11) and its pool-thread write move with it, and so does the `trunkCeremony`
  QML global, if the globals leave with their stages (§9, Q17). The brief reads `governance/artefacts/design_system/README.md` and the component pages
  before any QML is touched.
- **A guard from the four contexts** (ruled 24 Nov, PR-4; a constraint on the design, not new work): **a third
  kind of Recording.** External video uploads (`coaching_layer.md` ASY-R1) will be a Recording with declared
  capabilities and no rig, so the shot kind (`shotKind`, today `captured` or `deviceOnly`; R-REC-8) and the rig
  reference must not assume two values.
- **#74 is a programme in its own right.** `docs/design/camera_calibration_design.md` stages it, starting with "make
  the focus failure visible". The Rig gives its results a home; the stage does not absorb the programme.

| | |
|---|---|
| Closes | 05 §1's Studio/Rig, `RigChanged` and ceremony-file rows; A-13; 05 §4's device key; the A5 lines of `trunk_ceremony_recorder.cpp` and the A6 lines of `trunk_ceremony_recorder.h` (A5 ×7, A6 ×2, unchanged at the close)[^c-allow] |
| Deletes | `CameraInstance::Perspective`, `kImpact`, the open-coded device keys; the calibration logic in QML |
| Test | an equivalence test of each calibration procedure, old QML logic against new C++, on recorded inputs; the procedures on the real sensors on the cabin PC; a reader test for swings without a rig reference |
| Re-run | none unless calibration results start feeding analysis; then a stage-version bump and a re-run |
| Docs | IM §2.3, §8, §11; `docs/design/camera_calibration_design.md`; `docs/reference/swing_json_schema.md` |
| Tier (proposed, for the Product Owner) | Independent review for every item: a new owner, a format change and source data. A duties inventory of each calibration QML file before its logic moves |
| Accept (proposed, for the Product Owner) | Places that hold rig facts (SET-09's rig keys, SET-05, FILE-SESS-11, the assumptions in code): to one owner, counted in the brief. Open-coded device keys: 7 → 0. Perspective types: 2 → 1. Thresholds, maths and step transitions in calibration QML: to 0 (deferred to #291: unchanged in Stage C, pinned by the golden traces[^lever-25nov]). `trunk_ceremony_recorder`'s A5 ×7 and A6 ×2: to 0 |
| Scratch cases (proposed, for the Product Owner) | A swing with no rig reference; a third camera; two Recordings either side of a calibration change; a machine with no rig stored yet |
| Cabin visit (proposed, for the Product Owner) | The procedures on the real sensors: the Product Owner runs them from a written procedure in one visit; Stage E's part 1 is built behind the visit and behind the Rig's design approval (§7) |

**Ownership.** Answered when the stage is planned, from the Rig's design. Ids it touches:
OBJ-RIG, EVT-15, FILE-SESS-11, SET-05, SET-09, A-13, the Recording's stream setup (R-REC-*).

### Stage D · The capture seam

After Stage C, so the capture service writes rig references from the start.

- **The frozen window stops being a view onto the ring.** `swing_window.h` and `swing_payload_source.h` (Vocabulary
  ring) include `source_ring.h` (Adapters) for `SourceRing::ReadHandle`. A value type for the handle, in the
  Vocabulary ring, closes their two A1 lines (proposed, for the Solution Architect). Buffer's stream types then move
  under `src/Vocabulary/`, which completes the lint's one Vocabulary rule ("Mandatory carry-ins" (i)). Both are
  dropped from Stage D: the handle and the headers stay as they are[^lever-25nov].
- **Split `ShotProcessor`** (2,156 + 508 lines, 17 A1 at the close[^c-files]).
  Its five jobs today: post-roll and late-source gathering over the event buffer and the device managers; the export
  and the frozen window; session and shot bookkeeping; starting the analysis; live replay. IM puts the capture service
  in the Application ring (IM §4: "services that run work: capture, …"; RING-APPLICATION lists `shot_processor`), and
  says the capture services' device half leaves in this refactor (IM §4.1). So (proposed, for the Solution Architect;
  IMC C9): the capture service stays in Application, and its device half moves to an Adapters-ring collaborator behind
  an interface the inner ring defines. That removes the device and ring includes behind the A1 lines.
- **Before the split** (proposed, for the Product Owner): a duties inventory of `ShotProcessor`, written and
  checked with no code, as the domain refactor did before each deletion from 21 Nov[^l-items]; and tests that pin the
  two guardrails below, passing on today's code, before any of it moves. A note that says "keep" does not survive a
  split; a test does.
- **Two guardrails for the split** (proposed, for the Solution Architect):
  - the first-document-write boundary stays where R-REC-1 puts it: the allowlist's `# kept (R-REC-1)` lines
    (`Gui/shot/capture_context.cpp` A5 and `Gui/shot/shot_processor.h` A5) are kept, not "fixed";
  - the timing invariants marked ⚠ in `shot_processor.cpp` are kept: the short post-roll for PPCP and Manual
    (`postRollMsFor`), the two ordered deadlines (`kHistoryDeadlineUs` sits past `kGatherDeadlineMs` on purpose),
    "reserve at detection, collect at the gather", and the window bounds frozen at the pause.
- **The shot is allocated by `Session::addShot`** (allocate, then create through
  `RecordingRepository::create`, then capture writes)[^dec142], not by the capture service. The capture service binds the
  target session when the shot is triggered, so a shot in flight when End is pressed lands in that session (#188).
  Whether `addShot` is called at the freeze rather than after the export is a change of timing (C8) for the Solution
  Architect (§9, Q8). #188 is still open and has not been re-checked since step 3.4a-ii (the issue says so); check it
  first.
- **Late launch-monitor readings arrive through `Shot::completeCapture` only** (R-REC-2, EVT-22). #192 was found in
  step 3.4b-ii's probe and is still open after steps 3 and 4 closed; check whether it still reproduces before planning
  it here.
- **D-6: the launch-monitor board owned by the session** ("Mandatory carry-ins" (iv)): a presenter over
  `SessionSeries`, with the no-session "No launch monitor connected" state taken from the device, and D-5's writer for
  the device-only create (proposed, for the Solution Architect; IMC C1). The board's untyped `graphics` map and
  LmClubArt's anchors (A6) go with it.
- **#53's capture cause** is unknown; investigate it here, with the probe, on back-to-back swings and near End.
- **`ShotController`** (634 + 370 lines, 3 A1 at the close[^c-files])
  takes narrow interfaces instead of the event buffer and the session controller; one arbiter for all phones (#78:
  "today two phones make two shots in two sessions from one ball"); the shot source becomes an explicit persisted type
  instead of an enum whose order matters (`Source::Acoustic == 4`) (proposed, for the Solution Architect; IMC C3/C9).
- **`CameraInstance`** (2,186 lines at the close, as on 21 Nov, up from 1,721 in June[^c-files])
  gives its pipeline construction and its ball-presence monitor to collaborators, as June proposed; `CameraManager`
  and `ImuManager` (1,060 and 1,050 lines at the close[^c-files])
  share their device-selection and producer mechanics. Both dropped from Stage D: they close no lint line and no
  issue[^lever-25nov]. The `liveWrist` QML global, a view over the IMU, goes with
  them, if the globals leave with their stages (§9, Q17).
- **The device enumerator** moves out of `Core` into an Adapters capture directory, and its `Device` struct, which
  couples Video, IMU and Bluetooth types, is split (proposed, for the Solution Architect; IMC C9).
- **The motion-capture probe** (`Pose/motion_capture_probe.*`): its A1 line (it includes `SessionController`) and its
  A6 line go with the probe's move out of `src/Pose` and an injected "is a session running" question. #86's items
  (it benchmarks a different pose format; opening Settings runs it) are fixed with it.
- **Guards from the four contexts** (ruled 24 Nov, PR-4; constraints on the design, not new work). The capture seam
  is Acquisition's boundary with Domain + Application (IM §2).
  - **A third kind of Recording**, as in Stage C: the shot kind (R-REC-8) must not assume two values.
  - **Not every capture is a shot.** Rehearsal swings with no ball (`coaching_layer.md` PRA-R8) are an Acquisition
    event stream, so the capture interfaces must not assume that every trigger is a ball impact.
  - **A play source is an Acquisition adapter.** The course context that a play source supplies (WMP-R2's
    `PlaySource`) is one, beside the launch-monitor connectors.
  - **Live triggering across the boundary.** The capture code in `Gui` (`Gui/cameras`, `Gui/imu`, `Gui/shot`) is
    tagged Domain + Application until this stage splits it, because it includes Diagnostics headers today: the pose
    estimator and the ball detector for live triggering, and `Analysis`, `Club` and `Timeline` in the shot processor.
    This stage decides how live pose and ball triggering sit across the boundary (IM §4.1; the four contexts, AM6).
- **Video duplication**: the pixel-format name mapping appears in `video_input_factory.cpp`, `VideoInputAravis.cpp`
  and `VideoInputSpinnaker.cpp`; one function.

| | |
|---|---|
| Closes | 05 §1's transport, pipeline, `swing_payload_source.h`, `swing_window.h`, `shot_controller` and `ShotProcessor` rows; E-09's capture half; the A6 lines of the camera, IMU and buffer-controller headers; the probe's A1 and A6 lines; the last per-file Vocabulary entries (Buffer's stream types); D-6, with its 3 lines tagged `platform refactor (#224)` ("Mandatory carry-ins" (ii)); #188; #53's capture half; #192 if it still reproduces; #235's half (1), a card whose document never lands; #78's arbiter |
| Deletes | `ShotProcessor` as one class; the ring include in the window types; the copies of the pixel-format mapping; the board's interim owner in `SessionReviewController` |
| Test | a live capture probe on the cabin PC; #188: End pressed while a shot is in flight, the shot lands in that session; back-to-back swings and a swing just before End each save a full clip on every camera; the post-roll and gather deadlines unchanged; `ctest` |
| Re-run | the parity run (re-analysis must not change); the live-equals-re-analysis check on real shots (04 step 1's) |
| Docs | IM §4.1, §11; `docs/developer/event_buffer_developer_guide.md` (the window's handle) |
| Tier (proposed, for the Product Owner) | Independent review for every item: capture timing, source-data writes and the split of a class. No self-check tier in this stage |
| Accept (proposed, for the Product Owner) | `ShotProcessor`'s A1 findings: 17 → 0. `ShotController`'s: 3 → 0. The window types' A1 lines: 2 → 0. Per-file Vocabulary entries in the lint: to 0 (one Vocabulary rule). Copies of the pixel-format mapping: 3 → 1. `AppSettings fallback` declarations in the two managers: 20 → 0, if they are deleted here. The `# kept (R-REC-1)` lines: unchanged, by design. The timing tests written before the split: still passing, unedited |
| Scratch cases (proposed, for the Product Owner) | End pressed with a shot in flight; back-to-back swings; a swing just before End; two phones on one ball; a late launch-monitor reading; a device-only shot |
| Cabin visit (proposed, for the Product Owner) | The live capture probe needs real cameras, sensors and phones: the Product Owner runs it from a script. A shot not saved is blocking (§5 rule 6). Items that do not touch timing are built and reviewed behind the visit, and committed when it passes |

**Ownership.** Answered when the stage is planned. D-6: the board is a presenter, its owner the session (session
level), read by the view through the session (proposed, for the Solution Architect). Ids: RING-APPLICATION,
RING-ADAPTERS, LC-SHOT, ID-SWING-NUMBER, R-REC-1, R-REC-2, EVT-01, EVT-22, DEC-142, A-13, E-09.

### Stage E · App host, settings, threading and secrets

The second half of the non-capture plan (05 §2 and §4's settings-fallback row; proposed, for the Product Owner, as one plan,
because they are one change: inject the settings object from `app_settings.h`, and delete each local fallback). The
composition root and the updater's injection can start any time after Stage B. The settings split waits for Stages C
and D: most of the `AppSettings fallback` declarations are in `camera_manager.cpp` and `imu_manager.cpp`
(24, of them 11 and 9, at the close[^c-fb]),
which Stage D edits, and the rig keys leave `AppSettings` in Stage C. The alternative is to delete each manager's
fallbacks in Stage D, as that file is edited (for the Product Owner).

- **The composition root** (proposed, for the Solution Architect; IMC C9). One owning object for what `main.cpp`
  builds today (2,101 lines and 18 `setContextProperty` calls at the close[^c-files]), with
  members declared in the order they must be torn down, and no function-local static services. Since the first
  version, the case is stronger than the PPCP statics:
  - `main.cpp`'s tail after `exec()` now holds six manual teardown lines, each there because a static or a callback
    outlives a local of `main()`: `archiveController.stopJobs()`, `recordingRepository.setOwnerOf({})`,
    `SessionDiagnosticsModel::setShotCommands(nullptr)`, `diagLookup = {}`, `ppcpClipFiler.setShotCommands(nullptr)`
    and `pinpoint::charts::setNeighboursDoor({})` (at `origin/main`);
  - the warm-up's exit path stops the pose inference thread (`PoseRunner::stopInferenceThread()`);
  - the hooks the rules declare are installed here (`installSharedNormSource`, J-104; the fingerprint reader, J-107,
    unless it becomes a direct call);
  - 5.8b's `GuiApp` (Adapters ring) already gathers 23 former globals under `app`; the composition root owns it and
    the kept globals.

  The composition root cannot sit in `src/App`, which is the Application ring (RING-APPLICATION): a composition root
  includes adapters, so lint A1 would fail. It stays beside `main.cpp`, which is `# kept (composition root)` in the
  allowlist, unless the Solution Architect gives it a ring of its own. The probe harnesses (`PINPOINT_PROBE_*`) move
  to their own file and use the headless probe's guards (`src/Core/headless_probe.h`).
- **The updater** takes an injected "is a session running" question and its one setting, which deletes its A1 lines
  (`Update/linux_appimage_backend.cpp`, `mac_sparkle_update.mm`, `update_controller.cpp`, `win_sparkle_update.cpp`)
  (A1 ×1, ×2, ×2 and ×2, unchanged at the close)[^c-allow].
- **`AppSettings`** (1,958 and 163 lines at the close[^c-files]):
  implementations out of the header; split into groups backed by one injected settings object (proposed, for the
  Solution Architect); no stack-local `AppSettings fallback;`. The fallback idiom is counted by grep
  `AppSettings +\w*[fF]allback`, tests excluded, less the hits in comments
  (27 hits, 3 of them in comments, at the close[^c-fb]). Its setters write
  `ppSettings()` directly, so a write through a fallback persists; what is lost is the live object's cached value and
  its change signal. Where a group holds keys that are domain facts (IM §8.2), moving them is an IMC (C3, C4). If the
  globals leave with their stages (§9, Q17), the `appSettings`, `secrets` and `studioProfile` globals leave with
  this item.
- **Threading: what is left of F-09 after step 5, re-measured at the close.** Step 5 closes F-09 for the Charts
  presenters ("one presenter base and one threading idiom", 04 step 5), and step 3.7 moves the diagnostics owners
  onto the pool (IM §11.1, R-CON-5's row).
  At the close, the production lines naming each: `QThreadPool` 25, `QtConcurrent` 48, `WorkPool` 44, `InferenceThread` 28, `std::thread` 27, `new QThread` 13[^c-thr].
  What those steps do not reach:
  - **Long-lived `QThread` + `moveToThread` owners**, each with its own teardown: `CameraInstance` (four threads:
    capture, preprocess, pose, ball), `ImuInstance` (`ImuIoWorker` on the manager's I/O thread), `HmInstance`,
    `TtsController`, `TranscriptionController` (two threads), `LlmController`, `DeviceEnumerator` (the IMU scan
    thread) and `SttProcessor`. At the close, the same 8 owners and about 12 threads[^c-owners].
    Their workers need a Qt event loop (timers, queued signals, Bluetooth and network objects).
  - **Finite jobs on other idioms**: `MotionCaptureProbe` on its own `std::thread` and `TrunkCeremonyRecorder` on the
    global `QThreadPool`. These are pool work by R-CON-5 as it stands, so moving them is making code match the IM, not
    a change to it.
  - **The proposal: amend R-CON-5 to allow a helper for long-lived hardware and media threads** (proposed, for the
    Solution Architect; IMC C11). R-CON-5 says "One threading idiom … Never: a new pool, thread or idiom" (IM §9.1),
    and DEC-194 is its first exception[^dec194]. DEC-194 calls `InferenceThread` "the first instance" of this page's
    helper, "deliberately not that helper", private to `PoseRunner` for now, and says the platform refactor may move it
    to the Platform ring when it generalises the helper. There are two shapes of thread:
    1. **a pure C++ serial executor** (`InferenceThread`'s shape: a FIFO with `submit`/`call`, no Qt, used by the
       tools): promote it to the Platform ring if a second non-Qt caller needs it, the segmenter's GPU path (#240) being the
       candidate if it is built;
    2. **a Qt event-loop owner** (the eight owners above): one tested RAII owner of a `QThread`, which starts it,
       drains it and runs the `quit()`/`wait()` teardown that each owner hand-writes today.

    One rule with two tested forms, not one class: collapsing both into one class would pull Qt into the pure C++
    callers or leave the Qt workers without an event loop. The alternative is to keep one idiom and name the capture
    threads as exceptions (§9, Q9).
- **Secrets**: move `SecretsManager`'s API keys (Azure TTS and STT, Gemini) to a keychain-backed store generalised
  from `mail_credentials_*`, into `src/Secrets/` (proposed, for the Solution Architect); move the Gemini key to a
  header. The pairing key is not in this item (§4). #89's "no keychain prompt on a signed upgrade" check applies.
- **AssemblyAI, one decision** (for the Product Owner): keep it or delete it. `STTBackendAssemblyAI`
  (296 lines at the close[^c-files]) is referenced by nothing outside its own
  files, and `CMakeLists.txt` marks it "kept for reference, not used as fallback". Deleting it drops the compile-time
  AssemblyAI default (`SecretsManager.cpp`) and its key with it; keeping it moves its key with the others.
- **Media duplication**: the cloud-fallback pair (`applyCloudFallbackPref`, `refreshCloudAvailability`) appears in
  the TTS, transcription and LLM controllers; one shared piece.
- **The logging, monitoring and media globals** (`appLog`, `profiler`, `resourceMonitor`, `cudaRuntime`,
  `controller`, `ttsController`) get typed APIs here, or the lint-scope ruling takes them out of A6's scope, if the
  globals leave with their stages (§9, Q17).
- **#193** (the app's own trash always): new behaviour, held by the freeze, which overlaps step 3's housekeeping
  (SET-14)[^dec80]. Step 3 is done, so ask when the freeze ends (§9, Q6).

| | |
|---|---|
| Closes | 05 §2's updater A1 lines; 05 §2's studio-profile lines (`studio_profile.cpp` A5 with the profile's repository, `studio_profile.h` A6 unless the lint-scope ruling takes it out of scope)[^dec193]; 05 §4's settings-fallback row; F-09's remainder (as re-measured); the A6 lines the lint-scope ruling leaves in scope; June's secrets and Gemini rows; the QML globals this stage owns, if §9 Q17 is answered that way; `archive_controller.cpp`'s A5 ×3 (`# platform refactor (settings)`, unchanged at the close); #234 (part 1, the composition root); #196, with the worker pool sized from the cabin visit's numbers |
| Deletes | the function-local statics and the manual teardown lines after `exec()`; `AppSettings fallback;`; the hand-written thread teardowns (if the amendment is agreed); the compile-time key default |
| Test | quit at every point of start-up and during the pose warm-up, on the Mac and the cabin PC (the Windows path is "NOT TESTED" in `main.cpp`); the helper's own tests under ThreadSanitizer (TSan, the data-race checker); settings read and write tests; a signed upgrade on macOS with keys in the keychain |
| Re-run | none to analysis |
| Docs | IM §8.2, §9.1 (if R-CON-5 is amended), §11; the settings and secrets developer notes |
| Tier (proposed, for the Product Owner) | Independent review: teardown order, every thread-lifetime change, the secrets move, and any settings key that moves. Self-check: an implementation moved out of a header with no other change |
| Accept (proposed, for the Product Owner) | Manual teardown lines after `exec()`: 6 → 0. Function-local static services: to 0. `AppSettings fallback` declarations: 24 → 0. Hand-written thread teardowns: 8 owners → 0, if the amendment is agreed. The updater's A1 findings: 7 → 0. QML globals: 18 → the number §9 Q17's ruling gives, set before the item starts; the domain refactor's target of 4 became 18 on an unruled reading[^l-aim]. API keys in the settings file: to 0. `archive_controller.cpp`'s A5 findings: 3 → 0. Readers of "the live session" from the carousel in `main.cpp`: to 0 (#234) |
| Guards (proposed, for the Product Owner) | No test or agent run reads or writes the real settings store or the real keychain: #187's helper and the credential store's in-memory test mode are in place first. An item that moves a QML global runs the `tst_*.qml` files that cover it; the domain refactor's 5.8b rewrote 980 QML references without them[^l-qml] |
| Scratch cases (proposed, for the Product Owner) | A machine with no keychain entry; an upgrade from the settings layout before the split; a quit at each point of start-up |
| Cabin visit (proposed, for the Product Owner) | The quit tests and the quit during the pose warm-up on the cabin PC, and the signed upgrade on the Mac, are run by the Product Owner from a script in one visit; the settings split is built behind it |

**Ownership.** Answered when the stage is planned. Ids: R-CON-5, DEC-194, F-09, SET-* (those that move),
RING-APPLICATION, the composition root's allowlist entry (`Gui/main.cpp`, A5, `# kept`).

### Stage F · The phone link (PPCP)

The last stage of the capture plan, after Stage E's composition root, which takes PPCP's ownership.

- **PPCP's files** (all "design: the platform refactor (capture)" in IM §8.1): the import ledger FILE-LIB-03, the
  imported clips FILE-LIB-04, the legacy ledger FILE-LIB-05, and the annotations FILE-SHOT-11. Retire FILE-LIB-05 once
  it is shown unused, or migrate it once; one repository for the ledger; decide whether FILE-SHOT-11 joins the
  `Shot` (as FILE-SHOT-12 did[^dec187]) or is a peer cache (all proposed, for the Solution Architect; IMC C1, C3, C4).
- **The phone-clip filer** (`Gui/shot/ppcp_clip_filer.cpp`, A5, `# unplanned`): it translates the swing folder to its
  `ShotRef` (`shotOfFolder`) and writes the placeholder document for a phone shot through `RecordingRepository::create`.
  It goes with PPCP's repository here, and its ownership (today a static in `main.cpp`) moves to the composition root
  in Stage E (proposed, for the Solution Architect).
- **`PpcpHostService`** (3,709 + 1,191 lines at the close[^c-files]) splits
  into the headless engine it already builds on (`makeHostEngine`, `src/Ppcp/ppcp_host_peer.cpp`) and a typed
  presenter for QML (its A6 lines). The `ppcpHost` and `ppcpOffers` globals go with it, if the globals leave with
  their stages (§9, Q17).
- **#78 and #79's defects**: a disabled camera still gets a preview; the wired preview channel drops about every 50
  s; a cabled phone connects twice; MP4 timestamps not rebased; the misleading "still processing the previous shot"
  message (`shot_controller.cpp`). The shared arbiter is Stage D's.
- **Notification coalescing** (#79: "one unmounted share once produced 10 identical toasts"): a change to how
  `PpNotificationHost`, `PpNotificationCard` and `PpToast` behave, a design-system change (proposed, for the Design Lead).

| | |
|---|---|
| Closes | 05 §1's PPCP A5 row and `ppcp_host_service.h`'s A6 lines (A5 in seven files, A6 ×6, unchanged at the close)[^c-allow]; `ppcp_clip_filer.cpp`'s A5 line; #78 and #79's listed defects; #208 (a late clip leaves a card's video flag and thumbnail stale after End) |
| Deletes | FILE-LIB-05 (if retired); PPCP's own file access outside its repository |
| Test | the PPCP conformance suite (`docs/conformance/ppcp-conformance.md`); two phones on the cabin PC; a cabled phone |
| Re-run | none to analysis |
| Docs | IM §8.1, §11; the PPCP design docs |
| Tier (proposed, for the Product Owner) | Independent review for every item: file ownership, pairings and a split of a class. A duties inventory of `PpcpHostService` before its split |
| Accept (proposed, for the Product Owner) | PPCP files with A5 lines: 7 → 0. `ppcp_host_service.h`'s A6 findings: 6 → 0. `ppcp_clip_filer.cpp`'s A5 line: gone. The conformance suite: passing before and after. Stored pairings: still valid after the upgrade |
| Scratch cases (proposed, for the Product Owner) | A library that still holds the legacy ledger (FILE-LIB-05); a phone paired before the upgrade; a clip that arrives after End |
| Cabin visit (proposed, for the Product Owner) | Two phones and a cabled phone, run by the Product Owner from a written procedure in one visit. A lost pairing or a lost clip is blocking (§5 rule 6) |

**Ownership.** Answered when the stage is planned. Ids: FILE-LIB-03, FILE-LIB-04, FILE-LIB-05, FILE-SHOT-11, SET-09
(`ppcp/pairings`, unchanged).

---

## 6. What not to do

| Item | Recommendation | Why |
|---|---|---|
| Re-tagging single A6 lines `# kept` one at a time | Ask for the lint-scope ruling instead (Premise 4) | One decision settles them |
| Wrapping the kept QML globals into new objects to lower the count | Let each global leave with the stage that owns its object (proposed, for the Solution Architect) | A wrapper moves a name, not an owner ("Mandatory carry-ins" (iii)) |
| Putting #224-H in Stage B | Keep it on #224 or in its own plan | It is not a platform dependency, and D-3 needs a re-fire ("Mandatory carry-ins" (ii)) |
| Taking #222 into the platform plan | Leave it on #222 | No platform stage depends on it (§8) |
| A gate that stops all work until the Product Owner has run sessions | Name the check, its cost and who runs it, and build behind it (§5 rule 2; proposed, for the Product Owner) | Step 3 waited 92 hours on one[^l-gate] |
| Dropping a hardware check because the last one "found no defect" | Keep the check; drop only the waiting | The domain refactor's live sessions found a fault that stored-session parity had passed[^l-gate] |
| Splitting an item to fit an agent's context | Split only where the parts can be reviewed apart (§5 rule 11; proposed, for the Product Owner) | Step 3 went from 12 items to 23 that way, each with its own review and builds[^l-items] |
| Rewriting `src/Update` into a back-end split (June's Theme J) | Already done | §3 |
| Rewriting the ring buffer's internals | Leave them | §3 |
| Forcing real-time capture loops onto the pool | Do not | §3; Stage E |
| One threading class for both the pure C++ executor and the Qt event-loop owners | One R-CON-5 rule with two tested forms | Stage E |
| Rewriting the STT and TTS workers' parallelism | Leave it | Their payloads differ (the June review) |
| Moving the pairing key into a keychain | Do not, unless the Solution Architect reopens E56/E57 | §4 |
| Hardening the lint's A1 blind spots now | Wait for Stage B; then decide whether the A1 rows stay or go (§9, Q12) | Libraries cover A1 only for single-ring directories; A2, A4, A5 and A7's limits are not affected by libraries and stay as 05 §3's rows |
| A machine-readable IM (Q-20) | Reconsider, rather than defer indefinitely | The first draft said "unless doc drift becomes a recurring real failure". The 21 Nov review found drift in 05 (counts, line numbers), in IM (FILE-APP-06, since corrected by DEC-193; EXT-10; `post_shot_trace` missing from RING-PLATFORM, since fixed in v1.9.12) and in the first draft of this page. This revision found more: IM's spanning-directories table still lists `Analysis`'s shared types after 7.3. That is the failure the condition names (for the Solution Architect) |

---

## 7. Order

```
the domain refactor's steps 1–7 and the close; the freeze ends
   │
   ▼
entry conditions (proposed): the 3-shot live check on the cabin PC · the Windows test lane · Stage A's and B's approvals in one sitting
   │
   ▼
Stage A (observed bugs first, then the refactors)
Stage B, Part 1 · the Vocabulary root, #224-B, the Core split
Stage B, Part 2 · build libraries
                               │
          ┌────────────────────┴──────────────────────┐
          ▼                                           ▼
   capture plan                               non-capture plan
   Stage C · the Rig  ──────────┐             Stage E, part 1 · composition root, updater
                                │                     │
          ▼                     │                     │
   Stage D · the capture seam ──┴──────────▶  Stage E, part 2 · settings split, threading, secrets
     (and the Buffer vocabulary)                      │
          │                                           │
          └───────────────▶ Stage F · PPCP ◀──────────┘  (needs the composition root)

#224-U straight after Stage B, as a plan of its own (ruled 24 Nov)
#224-H, #222, #229, #230 and #240 run on their own tracks (not platform stages)
```

- **Entry conditions** (proposed, for the Product Owner). The domain refactor's code has had no live session since
  21 Nov, and 17 of its last 24 items had no review[^l-live]. So three things come first: the 3-shot live check of
  the cabin installer, with its findings fixed or filed; the Windows test lane (Stage B); and one sitting for Stage
  A's and Stage B's approvals and questions. The live check is a gate, and a short one: it is the only evidence of
  how steps 4–7 behave on real devices, and Stage A's wording fix and Stage B's Part 1 are built and reviewed behind
  it. If Stage B's 268-file rewrite landed first, a fault from steps 4–7 would have to be found across it.
  **Ruled (24 Nov):** the platform refactor starts now, with the cabin visit assumed successful for planning; the live
  check runs first at the next visit, against the `94b3b9a1` installer as the baseline, and #105 and #53 close on its
  evidence[^po24].
- **Lanes** (proposed, for the Product Owner): two at most, on disjoint files, with every build, test run and probe
  through one lock, as from 23 Nov[^l-lanes]. One lane only while a scripted rewrite crosses the tree. `main.cpp`,
  the root `CMakeLists.txt`, `AppSettings` and the allowlist belong to one lane at a time.
- **Stage E's part 1 early** (proposed, for the Product Owner). Stage C starts with a design that waits on the
  Solution Architect. The composition root and the updater's injection need only Stage B, so they are the work to
  build behind that wait. `main.cpp` changed in 14 of step 3's 22 code commits[^l-lanes]; an owning composition
  root makes every later stage's edits there smaller.
- Stage B's Part 1 before its Part 2: libraries need single-ring directories, and the Vocabulary move rewrites the same
  includes the libraries do.
- Stage C before the settings split: SET-05 and SET-09's rig keys leave `AppSettings` for the Rig, so splitting
  `AppSettings` first would be redone.
- Stage D before the settings split: `camera_manager.cpp` and `imu_manager.cpp` hold most of the fallbacks.
- Stage F after Stage E's composition root, which owns PPCP.
- The capture plan and the non-capture plan are not independent: they meet at `AppSettings`, the device managers
  and the composition root. Run them in this order, or plan the shared files once.
- #230's inventory before #224-H's metric catalogue as data (A-06) ("Mandatory carry-ins" (ii)).

---

## 8. Open issues this touches

| Issue | State (each is open on 24 Nov, by `gh issue list`) | Stage |
|---|---|---|
| #224 step 7's hygiene items, moved out of the plan | open, low priority; its Vocabulary root comment is mandatory[^sa23-vocab]; its P-3 comment is carried | B, Part 1 (#224-B); #224-U a plan of its own straight after Stage B; #224-H on its own track ("Mandatory carry-ins" (ii)) |
| #222 split the long analysis and coaching functions (was step 8) | open, low priority; moved out of the refactor plan as "code hygiene, not an architectural refactor"[^po23-plan]; its suggested approach is "one function at a time, preferably when a feature change has to touch that function anyway". No source places it in the platform plan; a Product Owner call to keep it out was reported by the review[^review] and confirmed on 24 Nov[^po24] | none: out of the platform plan |
| #229 the low point from every input (R-INS-5) | open; "After the refactor (freeze)" | none: analysis work |
| #230 audit every derived metric against R-INS-5 | open; after the refactor; "an inventory first, fixes as separate items" | none: analysis work; before A-06's format (§7) |
| #183 cameras pick their own brightness target (high) | open; "two causes mixed together" on 2 Nov; the issue's comparison comes first | A (settings), A (recording the values, an IMC) |
| #105 Witmotion connects without its vertical-install setting (high) | open | A |
| #53 a swing saved with video starting after impact passes the check | open; the capture cause is unknown ("perhaps because …") | A (detection), D (cause) |
| #184 the sensitivity subtitle is inverted | open, no labels; wording only | A |
| #188 a shot in flight at End goes to a stray folder | open; not re-checked since step 3.4a-ii (the issue) | D, after a re-check |
| #192 a live row's launch-monitor device flips until End | open after steps 3 and 4 closed; found in step 3.4b-ii's probe | D, if it still reproduces |
| #187 `model_browser_test` writes to the real macOS app data | open | B |
| #90 tests, corpus and library upkeep | open | B (the stale suites, with the Product Owner's say-so) |
| #231 the Charts QML harness needs a real Session | open; the close's one known red `qml_ui` check | not platform; noted for Stage B's `qml_ui` work |
| #31 3-D views go blank after a while | open; "The root cause is not known … Hiding is a strong lead but not a proven cause" | its own track, until a cause is found; no stage owns it |
| #78, #79 phone capture | open programmes; their listed defects | D (the arbiter), F (the rest, and coalescing) |
| #85 devices and session setup | open programme; "Calibrate mag" and "Save to flash" have no engine function | C |
| #86 performance | open programme; the motion-capture benchmark items; camera and IMU teardown joins | D (the probe), E (teardown) |
| #89 release, updates and platforms | open programme; a "no keychain prompt on a signed upgrade" check; write-back refusing older recordings with no session type | E (the keychain check); the write-back item is not platform |
| #74, #178, #180 calibration, set-up aim, a third perspective | open programmes; they set the Rig's requirements | C (the model), not the programmes |
| #193 the app's own trash always | open; new behaviour, held by the freeze | ask when the freeze ends |
| #203 headless probes send scratch sessions to the real macOS Trash | open, low priority | B, beside #187 |
| #228 two tests fail under a parallel full `ctest` | open, edge case | B, first: the full `ctest` baseline |
| #234 "the live session" read from the carousel, not `AppState` | open, edge case | E, part 1 |
| #196 Home's swing summary about 1 s slower on the one worker pool | open, low priority; "size the pool from live studio numbers" | E (threading), from the cabin visit's numbers |
| #208 a late phone clip leaves a card's video flag and thumbnail stale | open, low priority, edge case | F |
| #235 5.10's residue: a no-document card's edits | open, edge case | A for half (2), a golfer-input loss; D for half (1) ("Mandatory carry-ins" (iv)) |

**#222's touch points with this plan.** #222 is out, but three of its items meet platform work:
- `layoutDag` is moved by #224-B (B-10) and split by #222. The move alone takes its A7 line out of scope.
- `measure_sample.cpp` carries P-3's A1 and A5 lines (#224-B, Stage B) and an A7 line (`buildPhaseGrid`) that is
  #222's.
- `Coaching/swing_themes.h`'s maths goes to the vocabulary's shared maths under #222 (C-09). If that is done after
  Stage B, it lands in `src/Vocabulary/<area>/`.

The other open bugs are analysis, tracker, diagnostics or UI issues outside the platform layer.

---

## 9. Open questions

Answered: the first draft's Q1, "correct IM's Studio/Rig entry now?", is answered by DEC-193 (Premise 2)[^dec193]. Its number is kept free so the other questions keep theirs.

Kept from the first draft:
2. **The Rig's persistence, as two questions** (for the Solution Architect).
   (a) Where does the capture machine keep the rig it captures with: one writer, versioned on `RigChanged`, and
   per machine, as the DEC-193 studio profile is?
   (b) How does a past Recording's rig reach analysis on another machine: an immutable copy embedded in each
   Recording, or a versioned store in the library referenced by id from each Recording? Analysis runs on the Mac and
   the cabin PC. An embedded copy needs no second lookup and keeps each Recording self-contained. A library store
   works only where both machines read the same library.
3. **How active is phone capture over the next six months?** (for the Product Owner) If it is a main path, Stage F
   matters; if it is parked, only the composition root's ownership of the PPCP statics is needed (Stage E).
4. **Stage A's order** (for the Product Owner): #184 (wording), #105 and #183 (observed on hardware) and #53's
   detection half first, as soon as the freeze ends, then the refactors? **Ruled (24 Nov): as written, with #183 held
   for its comparison[^po24].**

Reframed:
5. **The lint-scope ruling** (05 "Decisions waiting"): narrow A6 back to P9's wording, a presenter's or domain type's
   public API? That takes 05 §2's settings, monitoring and tool lines out of scope (Premise 4).
6. **#193** (for the Product Owner): ask when the freeze ends; step 3 built housekeeping as the one purging service
   (SET-14)[^dec80].

Added in the first version:
7. **The pairing key**: keep it in settings, as E56/E57 decided (`ppcp_pairing_store.cpp:19–70`), or move it to a
   keychain, accepting a prompt on macOS and no store on Linux? This page recommends keeping it.
8. **The capture service**: does it stay in the Application ring with its device half in Adapters (IM §4.1), and does
   `Session::addShot`[^dec142] allocate the shot at the freeze or after the export?
9. **R-CON-5**: amend it to allow one helper rule with two tested forms (DEC-194's pure C++ executor, promoted to
   Platform when a second caller needs it; and a Qt event-loop owner for the long-lived hardware and media threads),
   or keep one idiom with the capture threads named as exceptions, as DEC-194 is?
10. **GPU segmentation**: is it wanted, given a stage-version bump, a library re-run, the shared 16 GB card and the
    suspected GPU memory growth (§4)? On the 6 Oct cabin bench the segmenter's inference took 0.55 s on 2 CPU threads,
    in a leaf stage off the critical path (`docs/research/data/analysis/analysis_dag_20261006.md`), so the gain is small.
    **Ruled 24 Nov: out of the platform plan, as #240**[^po24]. The review had reported that the Product Owner keeps it in Stage A[^review]. A second option (proposed, for
    the Product Owner): move it out of the platform plan to its own issue, as #222 was. It changes analysis output
    and needs a re-run, and 05's rule is "Don't mix groups in one plan".
11. **The studio profile's directory**: its owner is decided (`StudioProfile`, application level)[^dec193]; which
    directory does it move to, with Stage B's `Core` split? **Ruled (24 Nov): `src/App/`[^po24].**
12. **After Stage B**: keep the lint's A1 limits as 05 rows or retire them; and the pre-commit hook (05 "Decisions
    waiting") and the per-release audit re-run (05 §3)?
13. **Windows testing**: which cabin-PC tests does each stage need? This page names them per stage; the Product Owner sets the bar.
14. **Q-20**: given the drift the review found, plan the machine-readable IM after all (§6)?

Added in this revision:
15. **The Vocabulary root's residue**: do Buffer's stream types, LaunchMonitor's reading types and `Export/frame_decode`
    move under `src/Vocabulary/` too, so that the lint has one Vocabulary rule, with Buffer's in Stage D? And what are
    the area names ("Mandatory carry-ins" (i))? **The first half is ruled (24 Nov): all three move[^po24].** **The area
    names are ruled too: source names, with `Vocabulary/Club` and `Vocabulary/Common` for Core's files[^po24].**
16. **#224 in three groups** (for the Product Owner): #224-B in Stage B, #224-H and #224-U (the step-5 presenter
    residue, 26 allowlist lines) each on its own track? And is the whole of #224 in the platform refactor, as the
    review reports[^review]? **Ruled 24 Nov:** three groups; #224-H and #224-U are plans of their own, #224-U
    scheduled straight after Stage B[^po24].
17. **The QML globals**: wrap the kept globals into `devices`, `settings` and `logging` objects, or let each leave with
    its stage? Are `launchMonitor`, `trunkCeremony` and `liveWrist` devices ("Mandatory carry-ins" (iii))?
18. **D-6 and 6.3a's D-1**: D-6 in Stage D; 6.3a's D-1 (persisting the flight verdict) outside the platform stages?
19. **J-107's J-2**: the model fingerprint as a direct call in `Core/pp_model_path.h`, or the hook kept? **Ruled (24
    Nov): the direct call[^po24].**

Added with the lessons (24 Nov; each for the Product Owner unless it says otherwise):
20. **The entry conditions** (§7): is the live check a gate on capture, device, teardown and Windows build changes,
    with other work built behind it? **Ruled (24 Nov): start now; the visit is assumed successful and runs first at
    the next visit[^po24].**
21. **The review tier** (§5 rule 5): are those the triggers for an independent review, and is the self-check tier
    limited to scripted moves? 17 self-checked items of the domain refactor await their first live run[^l-live], so
    the lighter tier is not yet shown to be safe. **Ruled (24 Nov): as proposed[^po24].**
22. **The Windows test lane** (Stage B): can the cabin PC build the test tree and run `ctest` from a script, and
    when may it be used, given that it is the working studio? No source read for this page answers the first part. **Ruled (24 Nov): try it once
    now; then off-hours only, never beside Foresight Premier[^po24].**
23. **Stage E's part 1 behind Stage C's design wait** (§7)?
24. **The new carry-ins** ("Mandatory carry-ins" (iv)): #234 in Stage E, part 1? The rest is ruled (24 Nov)[^po24]: 6.4's
    D-1 is dropped (#239), R-REC-8's handedness is approved for Stage D, and the four residues are #224-U's.
25. **The method's tools** (Stage B): the IM excerpt tool and the build lock moved into `tools/`? **Ruled (24 Nov):
    yes, with the changelog merge helper[^po24].**

26. **The pipeline and the domain model** (for the Solution Architect; the four contexts' review): the analysis pipeline
    (Diagnostics) includes Domain-model headers (`analysis_service.h`, `swing_reanalyzer.h`, `reanalyse_and_persist.h`).
    Is a Domain-model interface R-CTX-1's "typed input from Domain + Application", so A9 allows it, or must the
    pipeline take plain values? **Ruled (24 Nov): option 1, contracts, not owners.** Every context may use the Domain model's
    interfaces and value types, never its owner classes or the Application services; R-CTX-1 is clarified (IM v1.10.1).
27. **Shared Vocabulary and A9** (for the Solution Architect): tag each shared Vocabulary area at the earliest context
    that includes it (Recording → Acquisition), which A9 passes as it stands, or mark shared areas usable by every
    context, which gives A9 a new meaning (C9)? **Ruled (24 Nov): option B.** `Club`, `Common` and `Identity` are tagged
    Shared; a type goes there only when two contexts use it. `trunk_calib_provenance.h` moves to `Vocabulary/Common` in Part 1
    (IM v1.10.1, §4.1).

**The stage each question blocks** (proposed, for the Product Owner). A question is ruled, with the stage's
approvals, in one sitting before that stage's first brief ("Approvals this page needs").

| Before | Questions |
|---|---|
| anything | none (20 and 22 ruled 24 Nov) |
| Stage A | none (4, 10 and 21 ruled 24 Nov) |
| Stage B | none (26 and 27 ruled 24 Nov; lint A9 built as R-CTX-1's order, the context map kept for later, PR-9; 11, 15, 16, 19 and 25 ruled 24 Nov) |
| Stage C | 2, 23 |
| Stage D | 8, 18 |
| Stage E | 5, 6, 7, 9, 17, 24's #234 |
| Stage F | 3 |
| each stage | 13 (its cabin checks, as §5 rule 2 names them) |
| after Stage B | 12, 14 |

---

## 10. The outside review (23 Nov), folded in

An outside model reviewed the first version (`build/refactor/inputs/platform_refactor_feedback_20261123.md`, checked
at `aa305dfb`). Each point, with its verdict. Its counts were taken at `aa305dfb`, before 5.8b and step 7, so none is
adopted; each count carries a re-measure mark here.

| Review point | Verdict | Reason, and where it lands |
|---|---|---|
| §1.1 Product Owner call: pull #224 into the platform plan | **accepted with change** | No source other than the review records the call: "reported by the review; confirm"[^review]. The sources place only the Vocabulary move, P-3 and the re-tagged lines here. #224 is split into #224-B (Stage B) and #224-H (its own track) ("Mandatory carry-ins" (ii)) |
| §1.2 Product Owner call: keep #222 out | **accepted** | #222 left the domain plan[^po23-plan], and nothing puts it in the platform plan. The explicit call is reported by the review; confirm[^review]. Its touch points are named (§8) |
| §1.3 Product Owner call: keep the segmenter's GPU work in Stage A and Q10 | **superseded (24 Nov)** | The first version had it in Stage A, and the review reported the call[^review]. The Product Owner ruled it out of the platform plan, to #240[^po24] |
| 2.1a #224 is "the deferred remainder … directory/ring splits" that Stage B depends on | **accepted with change** | Part of #224 (#224-B) is a Stage B dependency, but #224 does not split `Core`, `Buffer`, `IMU`, `LaunchMonitor` or `Pose`, so it does not deliver Stage B's precondition by itself. It also lists A-11, which 7.3 did (#224 comment) (Premise 3) |
| 2.1b "All eight ring-spanning directories still span rings" | **accepted with change** | True at `aa305dfb`; since 7.3 (`45419616`), `Analysis`'s Vocabulary files are in `src/Vocabulary/Analysis/`. Re-measured at the close (Premise 3) |
| 2.1c ccache and `pp_qml_cachegen_keep_unchanged` solved most POSIX rebuild pain | **accepted with change** (first "rejected as unneeded"; changed on 24 Nov with the lessons) | Premise 3 argues libraries for A1 enforcement, not for rebuild time, and that stands. But the review's other half matters to the plan: the fix is POSIX-only (`cmake/PinPointQmlModule.cmake`: "on Windows hosts it does nothing"), Windows is the primary platform, and Stage B changes how everything links. Build times are measured on both platforms before and after Stage B (Premise 3; Stage B; proposed, for the Product Owner). 06 gives the figure as about 415 compiles to 4[^l-win] |
| 2.1d Name the boundary with Stage D: `Core`, `Export`, `Analysis`, `Diagnostics` in Stage B, capture halves in Stage D | **accepted with change** | `Diagnostics` does not span rings in IM §4.1 (it is Rules); only `layoutDag` is in the wrong ring. `Core`'s device enumerator is Stage D's. Premise 3, "Who splits which" |
| 2.2a Separate the cascade's de-duplication from GPU segmentation | **accepted with change** | De-duplication is expected to change no output, but that is shown by a parity run (0 differences), not assumed: the three copies may differ (Stage A) |
| 2.2b Binding the segmenter to `InferenceThread`, and a cabin GPU memory bench beside Foresight Premier, are mandatory | **accepted with change** | The code facts check out (`PersonSegmenter::acquireShared` caches its session for the process; the analysis calls from a new thread each shot). The leak is suspected, not observed, because the segmenter runs on the CPU. Recorded as "check first" and (proposed, for the Solution Architect), not mandatory. DEC-194 keeps today's thread private to `PoseRunner` (§4, Stage A) |
| 2.2c Context for Q10: on the 6 Oct cabin bench, the segmenter's inference took 0.55 s on 2 CPU threads (plus 0.28 s fetching the address frames), and `AddressMarks` is a leaf off the critical path | **accepted** | Checked in `docs/research/data/analysis/analysis_dag_20261006.md`. It weighs against GPU segmentation's value (§9, Q10) |
| 2.3 Two thread shapes, "not one helper" | **accepted with change** | The two shapes are real (a pure C++ executor; a Qt event-loop owner). But DEC-194 names `InferenceThread` "the first instance" of this page's helper[^dec194], so the framing is one R-CON-5 rule with two tested forms, not two unrelated helpers (Stage E, §9 Q9) |
| 2.3 counts "already shifted" | **rejected** | Taken mid-step-5; re-measured at the close instead |
| 2.4 Q2: the live rig store and the historical rig are two questions | **accepted with change** | The split is right. It is written without presuming the answer to (a): the live store's place is part of the question (§9, Q2) |
| 2.5a `main.cpp`'s manual teardown lines strengthen the composition root's case | **accepted** | Checked at `origin/main`: six lines after `exec()` and the warm-up's `stopInferenceThread()`. Also added: the hook installs (J-104, J-107) and 5.8b's `GuiApp`, which the review predates (Stage E) |
| 2.5b The probe harnesses and #187 align with `run_headless.sh` / `headless_probe.h` | **accepted with change** | For the probe harnesses, yes. For #187, test binaries are not app launches: they share the guard against the real homes, not the launch script (Stage B) |
| 2.6a `ppcp_clip_filer.cpp` A5 (`# unplanned`) needs a place | **accepted** | Stage F with PPCP's repository; its ownership to the composition root in Stage E. The review's allowlist line numbers were already stale; cited by path and rule |
| 2.6b `archive_controller.cpp` A5 and `.h` A6: re-check after step 5, not step 3 | **accepted** | Re-checked at the close (appendix A) |
| 2.6c `# unplanned` is 26, not 25 | **accepted as a re-measure** | Marked for the close (appendix A, B) |
| 2.6d Stage D guardrails: the `# kept (R-REC-1)` boundary and the ⚠ timing invariants in `shot_processor.cpp` | **accepted** | Checked at `origin/main` (Stage D) |
| §3 "after step 8" becomes "after step 7" | **accepted** | Throughout |
| §3 the "checked against" line | **accepted with change** | A re-measure mark; the review's IM version is not adopted |
| §3 an approval marked with a person's name becomes the role | **accepted** | Also a gendered "say-so" in Stage B |
| §3 `Core` and `main.cpp` counts | **accepted as a re-measure** | Marked for the close |
| §3 the three MediaPipe mentions left after `32bb5353` | **accepted** | Deleted (Stage A) |
| §3 `post_shot_trace` is in RING-PLATFORM since v1.9.12 | **accepted** | Dropped from Stage B's Docs; §6 says "since fixed" |
| §3 #192: steps 3 and 4 are closed | **accepted** | #192 is still open; check whether it reproduces (Stage D, §8) |
| §3 AssemblyAI's default and its unused back-end as one decision | **accepted** | One decision for the Product Owner (Stage E) |
| §3 #193: ask when the freeze lifts | **accepted** | §9 Q6 |
| §3 Appendix A's report-figures row: update its status | **accepted as a re-measure** | No source read here gives its status; marked for the close |
| §3 Appendix B: markup truth's A5 lines are `# step 5` | **accepted** | Checked at `origin/main` (appendix B) |
| §4 apply at the close with one re-measurement | **accepted** | This revision is the draft; the close fills the marks |
| Not in the review: the Vocabulary root (J-100), the QML globals (J-92, J-93), D-6, 6.3a's D-1, P-3, J-107's J-2, R-INS-5 | **added** | "Mandatory carry-ins" (i)–(iv) |

---

## Appendix A. Every 05 row, placed

The counts (×n) in this table are the 21 Nov allowlist's.
At the close every ×n here still equals its allowlist line[^c-allow].
This table is also the platform refactor's close record (§5 rule 12; proposed, for the Product Owner): at each stage's
close, each of its rows is marked closed, moved or left, with the evidence. The domain refactor kept such a record
for steps 3 and 4 only[^l-record].

| 05 row | Placement |
|---|---|
| §1 Transports, devices, ring buffer, connectors, threading inside capture | Stages C, D, F |
| §1 The capture pipeline that produces a Recording | Stage D |
| §1 Studio/Rig | Stage C; the IM correction is made[^dec193] |
| §1 `RigChanged` | Stage C |
| §1 Studio and acquisition files | Stage C (ceremony); Stage F (PPCP ledger); the studio profile is now 05 §2's[^dec193] |
| §1 `swing_payload_source.h`, `swing_window.h` A1 | Stage D, then their move under `src/Vocabulary/` |
| §1 `shot_controller.cpp` A1 ×2, `.h` A1 ×1 | Stage D |
| §1 `ShotProcessor`'s capture half (A1 13 + 4 at `8cd42e44`) | Stage D |
| §1 E-09, the capture half | Stage D |
| §1 A-13 | Stage C |
| §1 `motion_capture_probe.h` A6 ×1 | Stage D |
| §2 `studio_profile.h` A6 ×4 (moved from §1)[^dec193] | Stage E; out of A6 scope if the lint-scope ruling narrows it |
| §1 `buffer_controller.h` ×1, `camera_instance.h` ×2, `camera_manager.h` ×2 A6 | Stage D |
| §1 `imu_instance.h` ×2, `imu_manager.h` ×3 A6 | Stage D; `trunk_ceremony_recorder.h` ×2: Stage C |
| §1 `ppcp_host_service.h` A6 ×6 | Stage F |
| §1 PPCP's A5 lines (seven files, FILE-LIB-03…05, FILE-SHOT-11, the pairing store) | Stage F; the pairing store's line with the repository design, its storage unchanged |
| §1 `trunk_ceremony_recorder.cpp` A5 ×7 | Stage C |
| §2 `studio_profile.cpp` A5 ×9 (moved from §1, `# platform refactor (settings)`)[^dec193] | Stage E, with the profile's repository |
| §1 `motion_capture_probe.cpp` A1 ×1 | Stage D |
| §2 `app_settings.h` A6 ×26 | the lint-scope ruling; the rig keys' shapes in Stage C; else Stage E |
| §2 `markup_controller.h` A6 ×11 | the lint-scope ruling (a tool); if A6 stays wide, Stage E |
| §2 `profiler_controller.h` ×8, `resource_monitor_controller.h` ×4 | the lint-scope ruling; proposed out of scope (single-consumer monitoring) |
| §2 `llm_controller.h` ×1, `transcription_controller.h` ×1, `LlmWorker.h` ×1 | the lint-scope ruling; proposed out of scope |
| §2 `app_info.h` ×1, `swing_zip_exporter.h` ×2 | the lint-scope ruling; proposed out of scope |
| §2 the updater's A1 lines | Stage E |
| §2 `archive_controller.h` A6 ×1 | re-checked at the close: unchanged, as is `archive_controller.cpp`'s A5 ×3 (`# platform refactor (settings)`). The `.cpp` line: Stage E; the `.h` line: the lint-scope ruling (§9 Q5) |
| §3 lint A1 reads only quoted includes | Stage B, then §9 Q12 |
| §3 lint A2 misses module-qualified creation | deferred: no case today; keep the row |
| §3 lint A4 misses concatenated file names | deferred: no case known; keep the row |
| §3 lint A5 misses `std::filesystem` and `QStandardPaths` | deferred; worth revisiting in Stage C and F, which move file access into repositories |
| §3 lint A7 misses trailing-return functions | deferred; keep the row |
| §3 IMU-integrity parity untested | deferred: needs a Witmotion swing (05's "later decision"); not platform work |
| §3 report figures and key screens in the parity baseline | not this page's: inside the domain refactor. At the close they are not: the baseline holds analysis and diagnostics outputs only, and the screens and the report were checked by before/after sheets[^c-parity] |
| §3 `swinglab_run --doc-blocks` exit code | Stage B |
| §3 `parity_baseline.py --self-test` | Stage B |
| §3 the per-release re-run of the audit tools | Stage B (an owner), §9 Q12 |
| §3 the pre-commit hook | §9 Q12 (the Solution Architect's decision) |
| §3 B-14 | Stage B |
| §4 the settings-fallback idiom | Stage E |
| §4 the device key | Stage C |
| §4 markup truth files | stale: now the `Shot`'s (appendix B) |
| Decisions: the `# unplanned` lines (26 at the close)[^c-allow] | placed line by line above; `ppcp_clip_filer.cpp` A5 (not a 05 row): Stage F |
| Decisions: the lint-scope calls | §9 Q5 |
| Decisions: the pre-commit hook | §9 Q12 |
| Decisions: G-05, D-20, E-16 | out of scope: design-system items 05 holds only so they are not lost |
| Not a 05 row: the lines tagged `platform refactor (#224)` at the close (J-105) | split four ways ("Mandatory carry-ins" (ii)): Stage B Part 1 (#224-B) 4 lines, Stage D 3, #224-H 1 (the metric manifest's A7 line), #224-U 26 |
| Not a 05 row: the stale `# step 1` and `# step 4` lines (06 §4.5) | `swing_reanalyzer.cpp` A1: Stage B Part 1. The two A6 lines: #224-U ("Mandatory carry-ins" (iv)) |

## Appendix B. Stale 05 rows (a note for whoever next updates 05; 05 is not edited here)

- **05's header and purpose** say "steps 1–8" and are dated 14 Nov at `3b0ead97`. The refactor ends at step 7; step 8
  is #222, and step 7's remainder is #224[^po23-plan]. 05 does not list the carry-ins of this page's "Mandatory
  carry-ins" section, the Vocabulary root first.
- §1 `ShotProcessor`: "`shot_processor.cpp` A1 ×16". At the close the allowlist has `shot_processor.cpp` A1 ×13 and `shot_processor.h` A1 ×4[^c-allow].
- The "L n" line numbers throughout are stale; for example, `app_settings.h`'s A6 entry and the window types' lines
  have moved since 05 was written. Cite by path and rule instead.
- §1 "Studio and acquisition files": "The trunk ceremony recorder still writes a part file then renames". It now
  writes through `atomicWrite`, copying the earlier ceremony aside first (`trunk_ceremony_recorder.cpp`; IM
  §8.1 FILE-SESS-11).
- §2 `archive_controller.h`: "Check after step 3". It was re-checked at the close and is unchanged, as is
  `archive_controller.cpp`'s A5 ×3 (`# platform refactor (settings)`), which 05 does not list; that line is Stage E's.
- §4 "Markup truth files": FILE-SHOT-12 is now owned by the `Shot` (IM §8.1)[^dec187], and its A5 lines are tagged
  `# platform refactor (#224)` (re-tagged at the close of steps 5–7) at `44ae9230`.
- §4 "The settings-fallback idiom: 48 uses". At the close, 24 declarations by `AppSettings +\w*[fF]allback`[^c-fb];
  05 should state the pattern it counts.
- "Decisions waiting": "The 23 platform-layer `# unplanned` allowlist lines (… 4 A1 and 19 A6 …)".
  At the close there are 26: 6 A1, 1 A5 (`ppcp_clip_filer.cpp`) and 19 A6[^c-allow].

## Notes

[^dec193]: DEC-193: approved by the Solution Architect, 21 Nov 2026; applied in IM v1.9.12.
[^dec142]: DEC-142.
[^dec120]: DEC-120.
[^m21nov]: Solution Architect, 21 Nov 2026.
[^m15nov]: The Solution Architect's decision of 15 Nov 2026.
[^dec153]: DEC-153.
[^dec80]: DEC-80.
[^dec187]: DEC-187.
[^po23-plan]: Product Owner, 23 Nov 2026: step 8 moved out of the refactor plan to #222 ("code hygiene, not an architectural refactor"), and step 7 reduced to the allowlist's `# step 7` A1 and A5 lines, the rest moved to #224 (`aa305dfb`; the bodies of #222 and #224).
[^sa23-vocab]: Solution Architect, 23 Nov 2026, a live ruling during step 7.3 (J-100 in `build/refactor/step-3/JUDGEMENT-CALLS.md`, which is in the repository as `docs/implementation/swe-audit/06a-judgement-calls.md`, superseding J-99's folder name; recorded on #224): "MANDATORY in the platform refactor (not optional)"; on #224: "This is not optional." IM's RING-VOCABULARY row carries it at `origin/main`.
[^review]: Reported by the outside review (`build/refactor/inputs/platform_refactor_feedback_20261123.md` §1) as "discussed and agreed with the Product Owner". No other source read for this revision records it. To confirm with the Product Owner.
[^j92]: The refactor coordinator's reading in 5.8b, 23 Nov 2026 (J-92), left for the Solution Architect.
[^j93]: Product Owner, 23 Nov 2026 (J-93): J-92 stays as built; the wrapping "is settled after the refactor ends: the final count is taken at the close, and the wrapping may fold into the platform refactor that follows. The final report must call this out."
[^j95]: Reviewed live by the Product Owner, the Solution Architect and the Design Lead, 23 Nov 2026 (J-95): D-6 goes to the platform refactor; 6.3a's D-1 goes to the platform refactor; J-83's D-2 and D-3 go to #224; J-88's lint A5 widening is approved.
[^j105]: The refactor coordinator's call at 7.1's commit (J-105), carried out by the close (`build/refactor/CLOSE-BRIEF.md`, deliverable 2): one new tag, `platform refactor (#224)`, for every line leaving the plan; `measure_sample.cpp`'s A1 and A5 lines and the metric manifest's A7 line among them.
[^j107]: Logged for the Solution Architect, 24 Nov 2026 (J-107); not yet ruled.
[^dec194]: DEC-194: Solution Architect, 21 Nov 2026 (J-43); IM v1.9.13. IM's DEC-194 row: "It is the first instance of the platform assessment's long-lived-thread helper …, private to `PoseRunner` for now", its ring "Rules by its directory, which the platform refactor may move to Platform when it generalises the helper". `src/Pose/inference_thread.h`: "it is deliberately not that helper".
[^rins5]: R-INS-5: Solution Architect, 23 Nov 2026 (IM `dd71f2d8`; J-96). The estimator for the low point is #229 and the audit of every derived metric is #230, both after the refactor.
[^c-sha]: `git rev-parse origin/main` gives `44ae9230` ("Refactor: steps 5 to 7 close …"); IM's Version row there reads 1.9.52, 24 Nov 2026. Every count re-measured at the close is read at that commit (`git show`, `git grep` or `git archive` of it), not from a working tree.
[^c-vocab]: At `44ae9230`: the 42 headers of `src/Metrics/` (with its subfolders), `src/Recording/`, `src/Identity/`, and Core's ten files the lint counts as Vocabulary, of which seven are and three (`headless_probe`, `same_folder`, `version`) are Platform and stay out of the move (Stage B, Part 1)[^g24] (`club_standards`, `club_vocabulary`, `figure_format`, `headless_probe`, `height_text`, `pp_physical_constants`, `pp_tuned_constants`, `same_folder`, `sidecar_policy`, `version`: the `Core/` files the lint's `fileRings()` puts in no other ring, so its `Core/` rule makes them Vocabulary). Each `#include` or `#import` in `git grep` is resolved relative to its file, else by a unique path suffix; a file counts once.
[^c-gui]: The audit gives no method for "about 5,000" (02 F-11/D-09/C-08; `swe-audit/findings/F-charts-report.md` F-11). Its own figures for the ten pure rule files it names (`session_warp`, `session_traces`, `session_relations`, `session_consistency`, `consistency_grades`, `session_posture`, `session_delivery`, `session_sequence`, `session_bridge`, `variability_clubs`; `.h` + `.cpp`) sum to 8,491, which `wc -l` reproduces at the audit's pin `37289e33`. At `44ae9230` all ten are in `src/Gui/charts`: 8,529 lines. D-09's `session_spread.h` has left Gui (`src/Coaching/session_spread.h`). No source lists any other domain code in Gui, so none is counted.
[^c-globals]: `src/Gui/main.cpp` at `44ae9230`: 18 `setContextProperty` calls (lines 1714–1964), the 18 names listed; no other production file makes one. `ppcpOffers` is inside `#ifdef HAVE_PPCP` (1738–1760) and `ppcpHost` inside `#ifdef HAVE_PPCP_TRANSPORT` (1762–1956), so a build without the phone link has 16. (`build/refactor/CLOSE-NOTES.md` puts both behind `HAVE_PPCP_TRANSPORT`; the code is cited.)
[^c-allow]: The allowlist at `44ae9230` (`git show origin/main:src/Gui/tests/architecture_allowlist.txt`): 138 lines, 428 findings. The close's lint run found every allowed count equal to the tree's (`build/refactor/CLOSE-NOTES.md`, C-2), so these counts are the lint's findings. Against `8cd42e44`, the counts this page cites are unchanged, except that `# unplanned` went from 25 to 26: `ppcp_clip_filer.cpp`'s A5 line, `# step 3` ×3 then, is `# unplanned` ×2 now.
[^c-lines]: This page's method (production `.cpp`, `.h`, `.mm` and `.qml`, every `tests/` left out) on a `git archive` of `44ae9230`. The method reproduces the 21 Nov figures at `8cd42e44`: `src/Core` 41 files and 8,894; Buffer 4,045, Video 8,946, IMU 10,302, LaunchMonitor 6,337, Gui/cameras 9,257, Gui/imu 7,596, Gui/calibration 3,978, Gui/shot 11,800 (62,261 together; the first four 29,630); `src/Ppcp` 47 files and 24,871; Gui/app 2,868, Gui/main.cpp 1,662, Gui/monitor 5,133, Gui/settings 11,155, Update 2,678, Secrets 229, Audio 1,950, LLM 767, STT 1,567, TTS 1,380 (29,389 together).
[^c-cmake]: `wc -l` of the root `CMakeLists.txt` at `44ae9230` (3,900 at `8cd42e44`).
[^c-ppcptest]: The `.cpp` and `.h` files under `src/Ppcp/tests/` at `44ae9230`. The 21 Nov figure (16,608 in 23 at `8cd42e44`) reproduces only without the folder's one `.qml`; with it, 24 files and 16,765 lines. 16,625 / (24,871 + 16,625) is 40%.
[^c-libs]: At `44ae9230` the only `add_library` calls outside the tests are `pinpoint_buffer` (`src/Buffer/CMakeLists.txt:10`) and `pinpoint_swingstore` (`cmake/swingstore/CMakeLists.txt:16`), as at `8cd42e44`.
[^c-exe]: A parse of the root `CMakeLists.txt` at `44ae9230`, comments stripped, matching `.cpp` and `.mm` names in each call's arguments: 103 in `qt_add_executable(PinPointStudio …)`, 20 `target_sources(PinPointStudio …)` calls, 271 names in all, none named twice. The 21 Nov 102 does not reproduce: the parse gives 101 at `8cd42e44`, and a line count of the call also counts one comment line that names a `.cpp` (102 then, 104 now). The parse does reproduce the 260.
[^c-incdirs]: The `target_include_directories(PinPointStudio PRIVATE …)` block at `CMakeLists.txt:852` at `44ae9230`, unchanged since `8cd42e44`. The other such calls add third-party and platform directories and `src/Ppcp`; the 21 Nov count left them out, and so does this one.
[^c-span]: At `44ae9230`, `dirRings()` and `fileRings()` in `src/Gui/tests/architecture_lint_test.cpp` place named files of each of the eight in a ring other than their directory's: `Core` (Platform, Rules and Adapters files; the directory is Vocabulary), `Analysis` (Rules and Adapters; Application), `Export` (Vocabulary; Adapters), `LaunchMonitor` (Vocabulary and Rules; Adapters), `Buffer` (Vocabulary; Adapters), `IMU` (Adapters; Rules), `Pose` (Adapters; Rules), `Gui` (`Gui/shot`'s Application and Rules files; Adapters). IM v1.9.52 §4.1's "Directories that span rings" lists the same eight, but its `Analysis` row still names "Shared types (vocabulary)", which 7.3 moved: stale, for the Solution Architect.
[^c-incs]: Every production `.cpp`, `.h` and `.mm` under `src` (`tests/` left out) on a `git archive` of `44ae9230`, by the rule stated after this figure. The same count gives 411 in 135 at `8cd42e44`.
[^c-calib]: The production `.qml` files under `src/Gui/calibration` at `44ae9230`, which are the whole directory; the same as at `8cd42e44`.
[^c-key]: A grep for `"|"` in production code at `44ae9230`: `camera_instance.cpp` 2, `camera_manager.cpp` 1, `imu_role_map.h` 1, `resource_monitor_controller.cpp` 2, `shot_processor.cpp` 1, the same sites as at `8cd42e44`. The grep also finds `camera_manager.h:216` (`model + "|" + id`) at both commits. The 21 Nov count left it out, and so does this one.
[^c-files]: `wc -l` at `44ae9230`; lint counts from the allowlist at that commit, and `setContextProperty` by grep of `main.cpp`. The 21 Nov figures at `8cd42e44`: `shot_processor` 2,153 + 503; `shot_controller` 640 + 346; `camera_instance.cpp` 2,186; `camera_manager.cpp` 1,052 and `imu_manager.cpp` 1,041; `main.cpp` 1,662 lines and 43 calls; `app_settings` 1,958 and 163; `STTBackendAssemblyAI` 296; `ppcp_host_service` 3,709 + 1,191.
[^c-fb]: `grep -rE 'AppSettings +\w*[fF]allback' src` at `44ae9230`, `tests/` left out: 27 hits. 3 are comments (`app_settings.cpp:58`, `imu_manager.cpp:625`, `stt_processor.h:49`), which leaves 24 declarations: `camera_manager.cpp` 11, `imu_manager.cpp` 9, `shot_processor.cpp` 3, `capture_context.cpp` 1. The counts are the same at `8cd42e44`.
[^c-thr]: The lines that contain each string in production `.cpp`, `.h` and `.mm` under `src` (`tests/` left out) at `44ae9230`. The same count gives 21 Nov's 47, 50, 26, 0, 23 and 14 at `8cd42e44`. `new QThread` also matches `new QThreadPool`: one line now (`report_stills.cpp`), two then.
[^c-owners]: `new QThread`, the `QThread` members and `moveToThread` in production code at `44ae9230`, the same as at `8cd42e44` apart from line numbers. With one camera: `CameraInstance` 4, `ImuInstance` and `HmInstance` 1 each (both use `ImuManager`'s one I/O thread, so 11 distinct `QThread` objects), `TranscriptionController` 2, and `TtsController`, `LlmController`, `DeviceEnumerator` and `SttProcessor` 1 each.
[^c-parity]: `tools/parity/parity_baseline.py` at `44ae9230` dumps each session's diagnostics (`regrade_ledger`), the athlete dump (`athlete_dump`) and the analysis digest (`swinglab_run`). Its `SCREENS:screens` entry is the core pack's `screens.json`, not the app's screens. The close compared a 34-frame tour, the report probe and the PDF's pages against a CONTROL build, which is now deleted (`build/refactor/CLOSE-NOTES.md`, deliverable 7).
[^l-live]: 06 "State at the end" and Part B §8.6: the 3-shot live check "not run"; "no live run recorded after the 21 Nov combined run". Part B §4.1 and §7.3, and `06b-item-ledger.csv`: of the 24 items from 23 Nov, 17 were self-checked and 7 reviewed.
[^l-gate]: 06 Part B §1.2 (92.0 h waiting for the D22 live sessions, 18.4 h for the combined run) and §4.2 (what those sessions found: every live shot fired "with no measures, no LM data and no club", and about 550 club-baseline differences behind it, while "the stored-session parity passed"); J-39 and J-42. The rule "no blocking gates on the Product Owner's time unless the only evidence", with the gate named in the brief and work pipelined behind it: the step-3 retrospective's recommendation, agreed by the Product Owner, 22 Nov 2026 (06 Part B §5).
[^l-win]: 06 Part B §8.6 (J-84's Windows spellings: "no Windows test build has run them") and §7.2 (54% of step 3's compiles were QML-cache units; after `ad9b1edc` a two-line header edit compiles 4 units, not about 415). `cmake/PinPointQmlModule.cmake` at `44ae9230`: the keep-unchanged step is for "POSIX hosts only … on Windows hosts it does nothing". 06 Part A §6, item 8, proposes the cabin PC as a build lane "once its toolchain is scripted". No source read says whether the cabin PC builds the test tree or runs `ctest` today.
[^l-aim]: 06 Part B §4.5 and §8.6: the step-5 aim "not fully met" (51 instances: 16 gone, 29 justified, 6 left), measured by the close's survey; J-110 and 5.10 for the loss it found. "Justified" is defined differently in the two surveys (`build/refactor/step-4/LOCAL-COPIES.md`, 22 Nov; `build/refactor/LOCAL-COPIES-CLOSE.md`, 24 Nov). The QML globals' target of 4 and result of 18: 06 Part B §8.5; J-92, J-93.
[^l-tier]: 06 Part B §5 (the cost and pace rules: Product Owner, 23 Nov 2026: "a review only for cut-overs, deletions and data writes"), §4.1 (25 blocking findings in 21 reviewed items in steps 4–7) and §7.3 (review about 1.42 M of 11.29 M recorded tokens). `06b-item-ledger.csv`: 5.8b, 6.4 and 5.10 are marked self-check.
[^l-loss]: 06 Part A §4 and Part B §8.6 ("the parity set has no left-hander and no skied shot"); J-84 and J-110 ("anything that can lose golfer input is blocking, however unusual the path").
[^l-princ]: J-96 (R-INS-5: "a missing principle, found by a ruling, not by a test"), J-99 and J-100 (the Vocabulary root, ruled live during 7.3); 06 Part B §6.5, and §6.2 ("No DEC row is dated 23 or 24 Nov").
[^l-hard]: 06 Part B §4.3 and §6.4: a probe that ran against the live library on 22 Nov and rewrote three derived files; tests that wrote into the real Trash and application data (#187, #203); device prompts and two app crashes raised by agents' launches; a debugger run (J-105); four live questions in an unattended run (J-79); `Session::shotChanged` built before approval (5.7a). The guarded launcher is `tools/probe/run_headless.sh` (`0b555ca7`).
[^l-qml]: J-94, corrected by J-97; J-94's rule (an item that changes QML runs the `tst_*.qml` files covering it, and the close runs the whole `qml_ui`) was kept at the live review of 23 Nov 2026 (J-95); 06 Part B §4.3 ("`qml_ui` unrun for about 30 items across steps 4 and 5"; 5.8b "rewrote 980 QML references and was pushed without its QML tests").
[^l-calls]: 06 Part B §6.2 and §6.3 (the calls logged, those taken live and those still waiting at the end); J-20 (about 8 hours idle on a blocking question); J-79; J-92 and J-96 (a rule applied to a case its text did not name); 06 Part B §6.4 (5.7a's information-model change built before approval).
[^l-scope]: 06 Part B §3 (step 8 to #222 and most of step 7 to #224: Product Owner, 23 Nov 2026), §4.5 ("#224 does not mention the 29 step-5/6 lines now tagged with it"; four residues owned "by re-tag only") and §10 (the items routed to the platform refactor, and those "not placed anywhere yet").
[^l-items]: 06 Part B §5 (the 21 Nov changes: numbered deliverables, a duties inventory before deleting code, tests re-pointed, a build budget per item, one review and one delta check; agreed by the Product Owner, 22 Nov 2026) and §6.4 (a deliverable added after an item started caused a blocker). The step-3 retrospective: "Items grew from 12 to 23 … Every split added a review cycle and its builds."
[^l-lanes]: 06 Part B §5 (14 and 15 Nov: a worktree per parallel item after two build clashes; 23 Nov: "two lanes at most", "one heavy command at a time" through `build/refactor/one_at_a_time.sh`: Product Owner, 23 Nov 2026) and §7.2 (`main.cpp` changed in 14 of 22 code commits in step 3). `06b-item-ledger.csv`, row 7.3: "types-move.sh for lane B".
[^l-record]: 06 Part B §8.1: per-finding close records exist only for steps 3 and 4; of the 33 Critical and High findings, 16 are closed with a record and 9 are "committed, no record". §7.1 and §7.3: working hours recorded for step 3 only, tokens from 23 Nov only.
[^l-tools]: `git check-ignore -v build/refactor/one_at_a_time.sh` at `d2c3d41e`: matched by `.gitignore`. The project's `CLAUDE.md` rule on IM ("never read it whole … An agent whose brief quotes these excerpts reads nothing else from it") depends on the excerpts that `im_excerpt.py` cuts.
[^j108]: J-108 (the refactor coordinator, 24 Nov 2026), logged for the Solution Architect; 06 Part B §3, §8.6 and §10; `build/refactor/step-6/NOTES-6.4.md` §B, as 06 cites it. Not yet ruled.
[^close-res]: 06 Part B §4.5 and §10, citing `build/refactor/LOCAL-COPIES-CLOSE.md`: "neither #224's text nor `05-deferred-worklist.md` names them".
[^po24]: The morning review of 24 Nov 2026: the Product Owner, the Solution Architect, the Design Lead and the Coaching Lead, each ruling on the items for their role (J-101 to J-112 in `build/refactor/step-3/JUDGEMENT-CALLS.md`, and this page's second outside review). Every ruling took the recommendation put to it. Issues filed for the rulings that are not platform work: #236 to #240; DL-4 is recorded on #233.
[^g24]: A second outside review of this page (24 Nov), checked against `src/Gui/tests/architecture_allowlist.txt` (unchanged from `44ae9230` to `94b3b9a1`), `architecture_lint_test.cpp`'s `fileRings()`, the three `Core/` headers, 06 §4.5 (the stale tags, C-4), 06 §6 item 6 and Part A §3 (the process-document count), #235's text and `gh issue list` by the coordinator before any edit. Its reading of re-analysis' A1 line is right: `swing_reanalyzer.cpp` includes `recorded_products.h`, which the lint (`fileRings()`) and IM's RING-ADAPTERS place in the Adapters ring although it sits in `src/Analysis`. The review placed #235 in Stage D whole; its half (2) is a golfer-input loss and goes to Stage A instead. `version.h` was added to the review's two Platform headers by the same reading.
[^j110]: J-110 (the refactor coordinator, 24 Nov 2026), citing the close's note C-6; logged for the Solution Architect. Not yet ruled.
[^j101]: J-101 (the refactor coordinator, 23 Nov 2026), logged for the Solution Architect as P-6.3b-1; 06 Part B §10, last row: "Not placed anywhere yet". Not yet ruled.
[^j106]: J-106 (the refactor coordinator, 24 Nov 2026): the board's legacy-stub exception is built as an interim, pending the Solution Architect's ruling; 06 Part B §8.6 and §10.

[^lever-25nov]: The Product Owner and the Solution Architect, 25 Nov 2026: the lever pass on #224-U, Stage C and Stage D (`docs/implementation/platform_d_capture_seam.md`, "The lever pass of 25 Nov"). D-V3: Buffer's stream types stay in `src/Buffer`, J-109's Buffer part withdrawn by the Solution Architect (IM v1.12.4); D-V1: `CameraInstance`'s two collaborators and the managers' shared mechanics dropped; D-V2: `pp_rules_vision` moved to #292; C-V1: the calibration procedures' move to C++ deferred to #291, with #266 and #270 (`docs/design/rig_design.md` §4.8).
