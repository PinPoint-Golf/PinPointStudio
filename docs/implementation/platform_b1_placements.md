# Platform refactor, Stage B Part 1: thirteen placements and names to rule, and two questions on scope

**Status: a proposal. Nothing here is built until it is ruled.**[^hard] R1 to R13 are for the Solution Architect;
P1 and P2 are for the Product Owner. Each row has an empty **Ruled** line: the coordinator writes the ruling, the
role and the date there, and the briefs that wait read it.

**Why this page exists.** Stage B, Part 1 of the platform plan[^plan] moves files between rings and contexts so
that Part 2 can cut one library per ring. Some of its moves are ruled (the Vocabulary root and its areas, today's
`src/Coaching` to `src/Diagnostics/derived/`, the studio profile to `src/App/`, the direct fingerprint call, lint
A9 itself)[^ruled]. Thirteen others are marked "proposed, for the Solution Architect" in the plan, or are named
with no destination. Each changes the ring or context of a directory or file (IM §1.2, C9) or a lint rule's scope
(C4, C9)[^imc]. Eleven items wait on them.

**How to read it.** Counts were taken on origin/main at `f53626c4` (24 Nov 2026), each with its command. "Includers"
are files with a quoted include that resolves to the header as the architecture lint resolves it. Every
recommendation is **(proposed, for the Solution Architect)** unless it carries a footnote. The nine includes that
lint A9 would reject today are called "the nine" below; they are listed under R13.

**Include check, as the method asks before any ring or context ruling**[^inc]: of 972 includes that cross a context
today, 895 fit a ruled relationship, 68 pass only by a ruled deferral or carve-out, and 9 fit neither. With R3, R10
and R11 ruled as recommended, and the ruled items built, the 9 go to 0. No recommendation here adds a carve-out.

## The questions for the Solution Architect

### R1 · `Core`'s three unplaced headers are Platform
- **Today.** `src/Core/headless_probe.h`, `same_folder.h` and `version.h` are in no `fileRings()` entry, so the
  lint's `Core/` rule counts them as Vocabulary. None includes a project header (`grep -n '#include "' src/Core/{headless_probe,same_folder,version}.h`: 0 lines). Includers: 10, 3 and 5.
- **Options.** (a) Platform entries for the three. (b) Move them to `src/Vocabulary/Common` with Core's vocabulary files.
- **Recommendation: (a).** They hold the process environment, path spelling per operating system and the release
  version: nothing domain-shaped, which is the test for Platform (IM §4). The plan proposes the same[^plan-core].
- **C9.** Waits: B1.11.
- **Ruled:**

### R2 · The Kalman smoother's Rules directory
- **Today.** `src/Core/kf3_smoother.h` is Rules by the lint and by RING-RULES, which lists it "to place in Stage B".
  Two includers: `src/Club/club_rate_filter.h:39` and `src/Pose/pose_smoother.cpp:21`, both Rules, both Diagnostics.
- **Options.** (a) `src/Kinematics/`. (b) `src/Pose/` or `src/Club/`, beside one user. (c) A new directory for generic maths.
- **Recommendation: (a), context Diagnostics.** It is a signal filter with no golf in it, and `Kinematics` already
  holds helpers of that kind (`gap_bridge`, `angular_rate`). Neither user includes `Kinematics` today, so this adds
  one include edge from `Club` and one from `Pose`, both Rules to Rules. (b) puts a shared file in one user's
  directory; (c) creates a directory for one file. The context-map side
  track tagged it Shared[^cm]; Shared is a Vocabulary tag (Q27), and both users are Diagnostics, so the tag buys nothing.
- **C9.** Waits: B1.11.
- **Ruled:**

### R3 · `pp_settings` is Platform
- **Today.** `src/Core/pp_settings.h` (the `QSettings` handle; it includes only `<QSettings>`) is Adapters. 14
  production includers (`Gui` 6, `Ppcp` 3, `Export` 2, `Core` 1, `Secrets` 1, `STT` 1) and 10 in tests and tools;
  none in a Vocabulary, Rules, Domain-model or `Analysis` directory (`grep -rlE '[/"]pp_settings\.h"' src`).
  Three of the nine are `Ppcp`'s includes (`ppcp_host_inventory.cpp:30`, `ppcp_host_service.cpp:47`, `ppcp_pairing_store.cpp:73`), with 17 `ppSettings()` calls behind them.
- **Options.** (a) Platform, as a settings handle (the plan's first option[^plan-ctx]). (b) Inject the settings
  into `Ppcp` (the plan's second option, "the two settings").
- **Recommendation: (a).** (b) is 17 call sites over about nine keys, the pairing store's five among them: that is
  Stage F's work, and a pairing can be lost if it is rushed.
- **What (a) gives up.** Once the file is Platform, lint A1 no longer stops a Rules or Domain-model file including
  it. Lint A8 still names it for `src/Analysis`. Today's count of inner-ring includers is 0, so nothing is hidden
  on the day. **Proposed guard:** A8's name check for `pp_settings` is widened from `src/Analysis` to the
  Vocabulary, Rules and Domain-model rings (a lint-scope change, C4 and C9, with no allowlist line since the count is 0).
- **C9 (and C4 for the guard).** Waits: B1.11, and through it B1.21.
- **Ruled:**

### R4 · `app_info` leaves `Core`; the `Core/` rule becomes Platform; the directory keeps its name
- **Today.** `src/Core/app_info.{h,cpp}` is a `Q_OBJECT` façade for the About box, Adapters, with one includer
  (`src/Gui/main.cpp`). The plan's Core split does not name it. `src/Core/device_enumerator.*` (Adapters, 12
  includers in `Audio`, `Video`, `Ppcp` and `Gui`) waits for Stage D by the plan[^plan-core]. `src/Core` has 370
  includer files and 544 includes.
- **Proposed, in three parts.**
  1. `app_info` moves to `src/Gui/app/` (presentation; Domain + Application).
  2. The lint's `Core/` rule changes from Vocabulary to Platform. Its 16 Platform entries and R1's three become
     redundant and go. `Core/device_enumerator` stays as the one named exception, Adapters, until Stage D.
  3. The directory stays `src/Core`. The plan allows "`src/Core/` or a new `src/Platform/`"; a rename is a second
     tree-wide rewrite (370 files) for a name, and the library can be called `pp_platform` whatever its folder is.
- **Consequence to accept.** "`Core` holds only the Platform ring" is not reached in Part 1: one file remains,
  owned by Stage D.
- **C9.** Waits: B1.11.
- **Ruled:**

### R5 · LaunchMonitor's inferred reads: their directory and their context
- **Today.** Eight stems are Rules by `fileRings()`, inside an Adapters directory: `lm_inferred_reads`,
  `lm_flight_path`, `lm_enclosing_ellipse`, `lm_club_evidence`, `lm_gates`, `lm_session_reductions`, `shot_pairing`,
  `standalone_gate`. They include only the reading types (Vocabulary), each other, `Vocabulary/Analysis/gate_record.h`
  and `Coaching/handedness_gloss.h`. No connector is included by one, and no connector includes one. Includers of
  the eight: 30 files (`Gui` 8, tests 9, `Coaching` 3, `Diagnostics` 2, `tools` 2, and others).
- **Options.** (a) `src/LaunchMonitor/inferred/`, placed Rules by a directory entry (the lint takes the longest
  prefix, as it will for `Diagnostics/derived/`). (b) A new top-level `src/LmRules/`. (c) `src/Ball/`.
- **Recommendation: (a), context Diagnostics; the connectors Acquisition.** The files stay beside what they
  interpret, and Part 2's Rules library takes the sub-folder. (c) mixes the launch monitor's reads with the ball
  tracker's, which rank against each other (R-INS-3).
- **C9.** Waits: B1.12.
- **Ruled:**

### R6 · `src/Export`'s new name
- **Today.** The plan carries "`src/Export` renamed" with no name[^plan-224]. The audit's example, `src/Recording`,
  is now the Recording contract's types. After the Vocabulary move the directory is one ring: 58 files, the stores
  and file doors, with the zip and video exporters. 191 includer files, 545 includes, about 320 path mentions in build files.
- **Options.** (a) `src/Stores`. (b) `src/Persistence`. (c) Keep `Export`.
- **Recommendation: (a).** "The stores" is the IM's own word for these files (§1.4's A5 row, §4). (c) is a fair
  choice too: the rename buys clarity only, and no library depends on it. If the answer is (c), item B1.16 is dropped.
- **C9, and C4 (A5's exempt directory is keyed on the name).** Waits: B1.16, B1.17.
- **Ruled:**

### R7 · The ten chart rule files' home
- **Today.** `session_warp`, `session_traces`, `session_relations`, `session_consistency`, `consistency_grades`,
  `session_posture`, `session_delivery`, `session_sequence`, `session_bridge` and `variability_clubs` (`.h` and
  `.cpp`, 8,529 lines) are in `src/Gui/charts`, Adapters[^plan-224]. They hold no Qt object or colour type. They
  include other `Gui/charts` files (`session_series_types.h` ×8, `session_curves.h` ×5, `charts_words.h` ×4,
  `variability_env.h`), `Gui/typeset/box_stats.h` and `DomainModel/rule_data.h`: so they can't move as they are.
- **Proposed.** A folder under `src/Diagnostics/derived/` (for example `derived/series/`), Rules, Diagnostics. An
  inventory with no code (B1.15a) first places every outward include: moved with the ten, passed in by the Gui
  caller, or left in Gui as words. **The ruling asked for now is the destination and the ring only**; the inventory
  comes back if it finds a file that is a presenter and not a rule.
- **C9.** Waits: B1.15b.
- **Ruled:**

### R8 · Diagnostics re-cut by layer, and the pixel layout to the UI ring
- **Today.** `src/Diagnostics` has `engine` (21 files), `packs` (18), `norms` (17) and `measures` (13). Bottom-layer
  code sits in `engine/` and top-layer code in `norms/` and `measures/`, which makes a directory cycle with no
  file-level cycle[^b09]. 181 includer files, 422 includes. `engine/dag_layout.*` (pixel layout, an 852-line
  function, allowlist A7 ×1) and `norms/corridor_plot.*` (plot layout) have one production user, the model browser.
- **Proposed.**
  1. Folders, lowest first: `model` (vocabulary, characteristic, norm, context, pack reading, provider interfaces),
     `content` (loaders, validators, registries, the packs), `engine` (detect, explain, measure sources, health),
     `derived` (today's `src/Coaching` files, ruled). A file may include its own folder and those before it. A
     small check of that order joins the test suite, and is run on the proposed file list before anything moves:
     an include that breaks the order is a code fix and a new item, never a reason to bend the list.
  2. `dag_layout` and `corridor_plot` move to `src/Gui/diagnosticmodel/layout/`: Rules to Adapters, still plain
     C++ with their tests. Their A7 line then leaves A7's scope (Vocabulary and Rules only); the function's split stays with #222.
- **The file-by-file list** comes with B1.19's dry run for a second look; what is ruled now is the folder names,
  their order and the ring change in part 2.
- **C9 for part 2. Part 1 changes no ring; it is a change because IM §2 ("The packs") and CLS-PACKS-NORMS name these paths.** Waits: B1.18, B1.19.
- **Ruled:**

### R9 · What clears a store door's A5 line
- **Today.** Lint A5 exempts `src/Export`, but counts two of its files by name as store doors
  (`countedStoreDoor()`): `session_list_reads.cpp` (33 lines, A5 ×1) and `session_index_summary.cpp` (61 lines,
  A5 ×3, of which its includes of other stores' doors)[^j88]. `rule_data_reads.cpp` (116 lines, 7 file-access
  sites) is ruled to be counted too, "with one allowlist line cleared in Stage B"[^j110]. No source says how a
  finding inside the stores' own directory reaches 0.
- **Options.** (a) A door is named in the IM as part of its store (`session_list_reads` and
  `session_index_summary` with the session store; `rule_data_reads` with `RuleDataStore`), and the lint then exempts
  it like the rest of the store; the counted-door list ends empty and is deleted. (b) The reads move into the
  store's own `.cpp`. (c) The reads go behind the repository interfaces.
- **Recommendation: (a).** The counting was added so that new doors would be seen and decided; naming each in the
  IM is that decision. (b) undoes the reason `rule_data_reads` is its own file (the light tools link it without
  the settings file). (c) is right for `session_index_summary`'s cross-store reads in time, and is #224-U's size.
- **C4 and C9 (a lint rule's scope).** Waits: B1.17.
- **Ruled:**

### R10 · `recorded_products.h`'s ring
- **Today.** `src/Analysis/recorded_products.h` is Adapters by `fileRings()` and RING-ADAPTERS ("the recorded
  products' serialiser"). It is a 613-line header with no `.cpp` and no file access; it includes only
  `Vocabulary/Analysis/swing_analysis.h`, `uncertainty_json.h` and Qt's JSON types, and turns JSON into value
  types. Includers: `Analysis/swing_reanalyzer.cpp:20` (the A1 line, tagged `# step 1`, and one of the nine),
  `Export/swing_doc_analysis.cpp:53` and two tests.
- **Options.** (a) `src/Vocabulary/Analysis/`, beside `uncertainty_json.h`, which is the same kind of file. (b)
  Leave it Adapters, and have the store hand re-analysis the parsed products, which changes what the Recording carries.
- **Recommendation: (a).** By content it is the value types' own JSON form, with no I/O. (b) widens the Recording
  contract (C1, C3) to clear one include.
- **C9.** Waits: B1.13.
- **Ruled:**

### R11 · The corridor scan's forwarder leaves Diagnostics
- **Today.** The model browser's corridor scan keeps a direct, folder-keyed read of each swing's phase grid, as a
  recorded exception[^pr21]. Its forwarder, `readPhaseGrid(dir, …)`, is in `src/Diagnostics/measures/measure_sample.{h,cpp}`
  and includes `Export/phase_grid_store.h` (`measure_sample.cpp:28`). Production callers: `model_browser.cpp:8461`
  and `:8523` only; `measure_sample_test.cpp` uses it in about 15 places. It costs two allowlist lines (A1 ×1,
  A5 ×1) and is one of the nine: with it in Diagnostics, **A9 cannot land with no allowlist line**, which IM §1.4 requires.
- **Options.** (a) The forwarder and the comment that records the exception move to a header beside their callers
  in `src/Gui/diagnosticmodel/`. (b) It stays, and A9 gets a named exception.
- **Recommendation: (a).** The exception is the model browser's, so its read belongs with it. The include of the
  phase grid's door then sits in a `Gui` file, where A5 counts it. **So (a) needs this said in the ruling:** either
  `model_browser.cpp`'s existing A5 line (×2) takes the finding under a corrected count and reason, or the new
  header is listed as the recorded exception's reader. No line is added in either case without the ruling.
- **C4 (who reads FILE-SHOT-07 outside its door), and the FILE-SHOT-07 row's text.** Waits: B1.14, and through it B1.21 and P2.
- **Ruled:**

### R12 · One namespace per context: the names
- **Today.** `namespace pinpoint::analysis` is declared in 345 files outside the test folders (511 with tests), in
  18 directories: `Diagnostics` 69, `Club` 49, `Coaching` 32, `Analysis` 31, `Wrist` 27, `Gui` 22, `Vocabulary` 19,
  `Metrics` 19, `Body` 19, `Timeline` 10, `Pose` 9, `Kinematics` 9, `Export` 9, and five more. 1,808 qualified uses.
  No `Q_DECLARE_METATYPE` or named `qRegisterMetaType` spells `pinpoint::analysis`
  (`grep -rnE 'Q_DECLARE_METATYPE\(pinpoint::analysis' src`: 0), so a rename should reach no registered name; that
  is to be re-proven per run. The plan says "one namespace per layer"[^plan-224]; "layer" is not the IM's word for
  these (GL-43).
- **Proposed names, if it is built.** `pinpoint::diagnostics` for `src/Diagnostics` (with `derived`); `pinpoint::metrics`
  for `Vocabulary/Metrics`; `pinpoint::analysis` kept for `src/Analysis`, `Vocabulary/Analysis` and the measurement
  directories (`Club`, `Body`, `Wrist`, `Ball`, `Pose`, `Kinematics`, `Timeline`), which share the analysis record's
  types; the stores and `Gui` use what they include. `pinpoint::coaching` is left for the Coaching context's first file.
  One run per namespace, `pinpoint::diagnostics` first: it is where the 7 Oct collision came from.
- **Not an IM change by C1 to C11** (no object, file, id, ring or rule), but the names are architecture, so they are asked here.
- Waits: B1.20. Whether it is in Part 1 at all is P1.
- **Ruled:**

### R13 · The contexts still "to place", and the hub's contract headers
- **Today.** IM §4.1 marks rows "to place in Stage B", and §1.4 says the contract set "is listed when A9 is
  built". The include check was run with the placements below; the counts under "Include check" above are with them.
- **Proposed placements.**

  | Directory or file | Context |
  |---|---|
  | `Body`, `Wrist`, `Skeleton3d`; LaunchMonitor's inferred reads (R5); `kf3_smoother` (R2) | Diagnostics |
  | LaunchMonitor's connectors; `Audio`'s input and the acoustic shot detector; the IMU's device back-ends; `Core/device_enumerator` | Acquisition |
  | `LLM`, `STT`, `TTS`, `Secrets`, `Update`; `Audio`'s output (`audio_output`, `audio_output_base`, `ting_player`); `app_info` (R4); `Pose/motion_capture_probe`; `Analysis/mp4_frame_reader` | Domain + Application |
  | `pp_settings` (R3); the three headers (R1) | none: Platform |
  | `recorded_products` (R10) | Diagnostics, with `Vocabulary/Analysis` |

- **Proposed contract set**, each a `src/DomainModel` header that declares only interfaces and value types (Q26's
  test[^q26]): `recording_source`, `shot_command_values`, `recording_repository` (the three in use from outside the
  hub today), and `recording_reader`, `session_input` (which those include). **Proposed rule for the rest:** a
  header joins the set when a context first needs it, by a ruling, with the test applied then. Nine more
  (`session_repository`, `athlete_repository`, `rule_data_repository`, `firing_reader`, `series_reader`,
  `report_record`, `session_roll_up`, `golfer_input_rules`, `summary_rules`) look as if they meet the test by name;
  none is included from a context today, so listing them now would approve crossings nobody has examined.
- **The nine includes A9 rejects today, and what clears each:** `Video/video_overlay_pose.h:24` (deleted, ruled);
  `IMU/hm_session_worker.cpp:28` (include deleted, ruled); `Recording/recording.h:45` and `IMU/trunk_stand_tall.h:45`
  (types moved, ruled by Q27 and its consequences); `Ppcp` ×3 (R3); `Analysis/swing_reanalyzer.cpp:20` (R10);
  `Diagnostics/measures/measure_sample.cpp:28` (R11).
- **C9.** Waits: B1.21 (lint A9).
- **Ruled:**

## The questions for the Product Owner

### P1 · Move the namespace change out of Part 1 (for the Product Owner)
- **Today.** "One namespace per layer" is in #224-B, which was ruled into Stage B[^po224]. It is 345 product files
  (511 with tests) and 1,808 qualified uses: about 6 hours of the stage's estimated 52, in runs that each stop the other lane.
- **What depends on it.** No library in Part 2: a library is cut by directory and ring, not by namespace. No
  accept figure of Stage B names it. What it buys is protection against a silent duplicate-name link, as on 7 Oct.
- **Recommendation: move it out of Part 1, to straight after Part 2, as its own short plan** (after #224-U or
  beside it). By then every directory has its final path, so the rename is made once. In the meantime, add the
  cheap half now: a test that fails on two different definitions of one type name in `pinpoint::analysis` (about
  an hour, in Part 1's close). That gives the protection without the rename.
- **If kept in Part 1:** it runs last, after every folder move, one run per namespace, and R12's names are needed.
- **Decided:**

### P2 · The allowlist's accept figure (for the Product Owner)
- **The plan's figure.** Lines tagged `platform refactor (#224)`: 34 → 30, with Stage B's 4 at 0[^plan-accept].
- **Why it can't be met as written.**
  1. Two of Stage B's four lines are `measure_sample.cpp`'s (A1 ×1, A5 ×1). The corridor scan's forwarder, kept
     by ruling[^pr21], is what they count. They reach 0 only if R11 is ruled as recommended.
  2. The plan also has three stale-tagged lines re-tagged `#224` "in the stage's first commit"[^plan-stale]. That
     is now done (B1.T, `d37bbfa6`): one more for Stage B (`swing_reanalyzer.cpp`, A1) and two for #224-U. P-3b's ruled re-tag of `measure_sample.cpp`'s A1 line to `# kept`[^pr23] took one off, so **the start on main is 36**, not 34.
- **Recommendation: count Stage B's own lines, and state the rest as what remains.**
  - Stage B clears **5 lines to 0**: `measure_sample.cpp` A1 and A5 (by R11), `session_list_reads` and
    `session_index_summary` A5 (by R9), `swing_reanalyzer.cpp` A1 (by R10). `rule_data_reads` is counted and ends at
    0 with no line (J-110).
  - Tagged lines: **36 → 32** (`measure_sample.cpp`'s A1 line is now tagged `# kept`, so R11 clears it without changing this count). The 32 are Stage D's 3, #224-H's 1 and #224-U's 28, each named on #224.
  - One more line goes that the plan's figure never counted: `dag_layout.cpp`'s A7 line, by R8.
- **If R11 is not ruled as recommended:** Stage B clears 3 lines, the two `measure_sample.cpp` lines are re-tagged
  `# kept` with the ruling as their reason, tagged lines still read 36 → 32 (three cleared, and the A5 line leaves the tag as `# kept`), and lint A9 needs a named exception
  for that include, which IM §1.4's "no allowlist line" does not allow today. That is why R11 is the better route.
- **Decided:**

## What waits on what

| Ruling | Items that wait |
|---|---|
| R1, R2, R3, R4 | B1.11 (Core's placements) |
| R5 | B1.12 |
| R6 | B1.16; B1.17 after it |
| R7 | B1.15b |
| R8 | B1.18, B1.19 |
| R9 | B1.17 |
| R10 | B1.13 |
| R11 | B1.14; P2's figure |
| R12, P1 | B1.20 |
| R13 | B1.21 (lint A9), which also needs B1.11, B1.13 and B1.14 |

Not waiting: the two deletions, the move tool, the baselines, the Vocabulary root move, today's `src/Coaching` to
`Diagnostics/derived`, the Recording contract's types, the priorities' renames, the studio profile, the fingerprint
call, and the chart files' inventory.

[^hard]: `CLAUDE.md`, "Information model and architecture": any information-model change needs the Solution Architect's approval before it is built.
[^plan]: `docs/implementation/platform_refactor_assessment.md`, "Stage B · Build libraries, the `Core` split and the Vocabulary root", Part 1.
[^ruled]: The same page, "Rulings of 24 Nov": Q15 (the area names), Q11 (the studio profile), Q26, Q27, J-107's J-2, J-109, J-110, and the four contexts (L1–L8).
[^imc]: `docs/developer/information_model.md` §1.2 (C1–C11) and §1.4's last line: "A change to a lint rule's scope, or to A4's list, is an information-model change (C4, C9)".
[^inc]: The context-map check of 24 Nov 2026 (the plan's rulings row "The context map: kept for later"), re-run on `f53626c4`. Its first run, at `4ae8ac9c`, counted 963 crossings; the rejected nine are the same.
[^plan-core]: The plan's Stage B, Part 1, "The Vocabulary root move" and "`Core` holds only the Platform ring", both marked proposed for the Solution Architect.
[^plan-ctx]: The plan's Stage B, Part 1, "The four contexts' moves": "cleared by `pp_settings`' placement (Platform, as a `QSettings` handle; proposed …) or by injecting the two settings".
[^plan-224]: The plan's "Mandatory carry-ins" (ii), the #224-B row; audit findings F-11, B-09, B-10, C-14, B-15 and A-20 (`docs/implementation/swe-audit/findings/`).
[^cm]: The context-map side track's placements, 24 Nov 2026; kept for later by the Product Owner.
[^b09]: `docs/implementation/swe-audit/findings/B-diagnostics.md`, B-09 and B-10.
[^j88]: J-88, approved as J-95 (`docs/implementation/swe-audit/06a-judgement-calls.md`); `src/Gui/tests/architecture_lint_test.cpp`, `countedStoreDoor()`.
[^j110]: The plan's "Rulings of 24 Nov": "Lint A5 counts `Export/rule_data_reads.cpp` as a store door, with one allowlist line cleared in Stage B (J-110)", Solution Architect.
[^pr21]: Solution Architect, 24 Nov 2026: the corridor scan keeps its direct read as a recorded exception (P-3's summaries proposal, part (b), option 2). It is being written into the IM's FILE-SHOT-07 row by item P-3b.
[^q26]: Solution Architect, 24 Nov 2026 (the plan's §9 Q26; IM §1.4's A9 row).
[^po224]: Product Owner, 24 Nov 2026: #224 in three groups, #224-B in Stage B (the plan's "Rulings of 24 Nov").
[^plan-accept]: The plan's Stage B table, the Accept row.
[^plan-stale]: The plan's "Mandatory carry-ins" (iv), the row "the close (06 §4.5, C-4)".
[^pr23]: The Solution Architect, 24 Nov 2026: no `resultsVersion` bump for P-3b, and allowlist line 30's re-tag to `# kept` confirmed (`build/platform/JUDGEMENT-CALLS.md` PR-23).
