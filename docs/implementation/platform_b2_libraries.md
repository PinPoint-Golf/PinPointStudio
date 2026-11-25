# Platform refactor, Stage B Part 2: the libraries by ring. Fifteen questions, classified

**Status: ruled, 24 Nov 2026** (each row's **Ruled** line). One question was the Solution Architect's (L10's rules 3
and 4), three the Product Owner's (P1–P3), and eleven the coordinator's calls (L1–L9, L11, L12), logged for review.
The briefs that wait read each row's **Ruled** line. Built by Stage B Part 2's items.

**Why this page exists.** Stage B, Part 2 of the platform plan cuts one static library per ring and has the tests
link them[^plan]. The plan marks that structure "proposed", and the pilot library left six questions "binding on the
other libraries"[^pilot]. None is ruled. Part 2's plan and briefs are in `build/platform/B2/`.

**How each question is classified.** By the information model's definition of a change, §1.2 (C1–C11), read through
`tools/method/im_excerpt.py`[^imc]:

| Class | Meaning | Count | Questions |
|---|---|---|---|
| **HARD: the Solution Architect** | An information-model change. Nothing that depends on it is built before the ruling[^hard] | **1** | L10 (its rules 3 and 4 only) |
| **Coordinator's call** | A build-structure or scheduling choice with no C1–C11 change. The coordinator decides and logs it as a judgement call | **11** | L1–L9, L11, L12 |
| **Product Owner** | Scope and order. A recommendation is given; the coordinator applies it and logs it | **3** | P1–P3 |

**Why most are not information-model changes.** A build target is not an object of the model. C9 is "a ring, or the
ring of a directory or file": no question here moves a file, changes a directory's ring, or edits the lint's ring
tables. The pilot's brief and its independent review took the same reading[^pilot]. Where one option of a question
would be a change, the row says so and recommends the option that is not.

**How to read the counts.** Taken on main at `12992896` with two read-only scripts[^tools]. A "listing" is one product
source named in one test executable's source list. Lane 2's tree has work in progress and reads slightly higher
(242 executables, 5,733 listings); the briefs re-take every figure at run time.

**What can start with no HARD ruling.** Every brief except two rules of one: B2.0–B2.3, B2.5–B2.23, and B2.4's
rules 1, 2 and 5. Only B2.4's rules 3 and 4 wait for L10. The library briefs still wait for their entry conditions
(#258, the pilot's commit after cabin visit 1, B2.1, B2.2 and the Part 1 moves each one names) and for the
coordinator's calls below to be logged.

## The questions on build structure

### L1 · The libraries' names
- **Today.** Two internal libraries, `pinpoint_buffer` and `pinpoint_swingstore`; the pilot is `pp_platform`
  (`grep -rn add_library CMakeLists.txt src/*/CMakeLists.txt cmake`).
- **Options.** (a) `pp_<ring>`: `pp_platform`, `pp_vocabulary`, `pp_rules`, `pp_rules_vision`, `pp_domain`,
  `pp_stores`; the two older libraries keep their names. (b) Rename all to one prefix.
- **Recommendation: (a).** (b) touches every test list that links the swing store (every Qt test) for a name.
- **Class: Coordinator's call.** C1–C11: none. Waits: B2.3, B2.5–B2.10.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026, as recommended (build/platform/JUDGEMENT-CALLS.md PR-34; for the Product Owner's review)).

### L2 · What a library exports to its users
- **Today.** The pilot gives `src/Core` to itself and to whoever links it, which is `PUBLIC` in effect. 425 quoted
  includes resolve only through a global include directory, in 144 files; 59 of them are in the libraries' own files
  (Rules 49, the stores 7, Vocabulary 3) and 366 in the executable's (`ring_census.py globals`, the plan's method).
- **Options.** (a) A library's own files need no include directory (their includes become relative), and it exports
  its directories to users, as the pilot does. (b) It exports nothing; every user spells a path.
- **Recommendation: (a).** No user's include changes and no target gains a directory it did not already have. (b) is
  366 rewrites, 273 of them in `Gui`, which is #224-U's ground. B2.21 then trims what the app no longer needs.
- **Class: Coordinator's call.** An include's spelling is not an object of the model. Waits: B2.3, B2.5–B2.10, B2.21.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026, as recommended (build/platform/JUDGEMENT-CALLS.md PR-34; for the Product Owner's review)).

### L3 · Where libraries are defined, and the standalone suite configure
- **Today.** The pilot is defined in `src/Core/CMakeLists.txt`, and `src/Core/tests` adds it under `if(NOT TARGET)`.
  A ring's sources come from many directories (Rules: 12), and several suites will link each library.
- **Options.** (a) `cmake/rings/<name>/CMakeLists.txt`, added once from the root build file and once from
  `tests/cmake/PinPointTests.cmake`, as `cmake/swingstore` is. (b) One build file per source directory.
- **Recommendation: (a).** One definition for both trees. It also lets two lanes work: one owns the definitions and
  the root file, the other the suite lists. A suite's standalone configure (`cmake -S src/X/tests`) keeps working
  because it includes the same file; if one can't, it is retired and `BUILDING.md` says so.
- **Class: Coordinator's call.** C1–C11: none. Waits: B2.3.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026, as recommended (build/platform/JUDGEMENT-CALLS.md PR-34; for the Product Owner's review)).

### L4 · Test stubs
- **Today.** Four stub files replace product symbols at link time: `EventBuffer::nowMicros` (6 targets), one
  `AthleteController` method, `reanalyzeShot` and eight `Video` symbols. None is in a planned library.
  `src/Core/tests/pp_log_stub.cpp` has no user (`grep -c pp_log_stub src/*/tests/CMakeLists.txt`: a comment only).
- **Options.** (a) A stub may replace only a symbol that is in no library a test links; `pp_log_stub.cpp` is deleted.
  (b) Allow a stub beside a library and rely on link order.
- **Recommendation: (a).** (b) is LNK2005 under Microsoft's compiler and a silent pick of one copy on the Mac.
- **Class: Coordinator's call.** C1–C11: none (a test file). The deletion gets an independent review. Waits: B2.4, B2.12.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026, as recommended (build/platform/JUDGEMENT-CALLS.md PR-34; for the Product Owner's review)).

### L5 · One build of a library per configuration
- **Today.** Tests pass their own definitions and options. The ones a product file reads: `PINPOINT_PROFILE`,
  `PINPOINT_PROFILE_BASELINE` and `PINPOINT_DEBUG_LEVEL` (headers only, so one build serves them: the pilot's review);
  `NO_QT`, read by `Skeleton3d/skeleton3d_fit.cpp`; `-O2` on five timing tests that compile `shaft_tracker_math`
  (`grep -n 'COMPILE_OPTIONS -O2' src/Analysis/tests/CMakeLists.txt`). The app tree runs moc on everything; the test
  tree per test.
- **Options.** (a) One build per library. A test that needs a source compiled differently keeps its own compile, is
  named, and does not link the library. `AUTOMOC` is set on each library itself, the same in both trees. (b) A
  variant of a library per value.
- **Recommendation: (a).** Two named exceptions today, against a second copy of a 100,000-line library.
- **Class: Coordinator's call.** C1–C11: none. Waits: B2.3, B2.7–B2.9, B2.14b.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026, as recommended (build/platform/JUDGEMENT-CALLS.md PR-34; for the Product Owner's review)).

### L6 · The store-isolation lint and libraries
- **Today.** The lint reads each test executable's own sources only, so a source moved into a library leaves its
  view. #258 fixes that and is briefed.
- **No option to choose.** #258 lands before the first library that reaches the settings (`pp_stores`: two files).
- **Class: Coordinator's call** (an order, already in ORDER). Waits: B2.3 (entry), B2.10.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026, as recommended (build/platform/JUDGEMENT-CALLS.md PR-34; for the Product Owner's review)).

### L7 · What a ring library holds: bounded by what the test tree can build
- **Today.** The test tree links no ONNX Runtime, whisper or FFmpeg[^ort]. In the Rules ring, 12 of 83 sources are
  never compiled there (the pose estimators and their base, the three runners, the person segmenter, four `Club`
  files), and 33 files include OpenCV, which only some tests link. `frame_decode` (Vocabulary) includes OpenCV.
  `Wrist` and Diagnostics' norm stack include each other.
- **Options.** (a) `pp_rules` (Qt and Eigen: Diagnostics, `Wrist`, `Kinematics`, `Timeline`, `Body`, `Skeleton3d`,
  the launch monitor's inferred reads, the single-ring Rules files of `IMU` and `Analysis`) and `pp_rules_vision`
  (OpenCV: `Club`, `Ball`, `Pose`'s single-ring files). The ONNX Runtime and FFmpeg files stay in the executables.
  `frame_decode` goes in `pp_vocabulary`, with OpenCV linked only by the users that call it if that can be done, else
  in a small library of its own. (b) One Rules library that needs OpenCV everywhere. (c) A separate Diagnostics
  library.
- **Recommendation: (a).** (b) makes about 200 tests link OpenCV to compare two numbers. (c) is a cycle with `Wrist`.
  This is a split by third-party need, not by context, so the ruling that libraries stay by ring is kept[^l6]: no file
  changes ring, and both libraries are the Rules ring.
- **Class: Coordinator's call.** C9: none. Waits: B2.5–B2.8.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026, as recommended (build/platform/JUDGEMENT-CALLS.md PR-34; for the Product Owner's review)).

### L8 · `Analysis`' files join libraries by name
- **Today.** After Part 1, `src/Analysis` still holds three rings: twelve Rules stems, the pipeline (Application)
  and `mp4_frame_reader` (Adapters). The stores need the last at link time (`Export/recording_loader.cpp`;
  `link_census.py`). The plan lets a library take "only their single-ring files" from a directory that spans.
- **Options.** (a) `pp_rules` lists `Analysis`' Rules files by name, and `pp_stores` lists `mp4_frame_reader` by name.
  Each file keeps its directory and its ring. (b) Move them into single-ring directories first.
- **Recommendation: (a).** (b) is a directory move, which is C9 and belongs to whoever owns the split (P3).
- **Class: Coordinator's call** for (a): no ring or directory changes. Option (b) would be HARD. Waits: B2.7, B2.10.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026, as recommended (build/platform/JUDGEMENT-CALLS.md PR-34; for the Product Owner's review)).

### L9 · `Core/pp_debug.cpp`
- **Today.** A Platform file that includes `whisper.h`, `ggml.h` and `QMediaPlayer` (`:22`, `:23`, `:39`), so the
  pilot left it in the app. No test compiles it.
- **Options.** (a) Leave it in the app's own list. (b) Split its third-party hooks into their users' directories, so
  the rest joins `pp_platform`.
- **Recommendation: (a) for Part 2.** One file compiled once; nothing waits on it. (b) is worth doing when Stage E
  touches start-up, and is proposed then.
- **Class: Coordinator's call** for (a). Option (b) creates files in other rings from a Platform file; when in doubt
  that is a change (C9), so it would be HARD. Waits: nothing.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026, as recommended (build/platform/JUDGEMENT-CALLS.md PR-34; for the Product Owner's review)).

### L10 · The library-membership check
- **Today.** Nothing checks what a library holds or links. Three faults become possible: a source in two libraries;
  a source listed by a test that also links its library; a ring library that links an outer one, which no include
  betrays.
- **Proposed.** A ctest lint over the test manifest (B2.4) with five rules: (1) no source in two libraries; (2) no
  executable lists a source of a library it links; (3) a ring library links only inner libraries; (4) a ring library
  holds only sources the architecture lint places in its ring, with named exceptions (L8's); (5) a count of sources
  still hand-listed, which becomes a failure at the stage's close.
- **Recommendation.** Build all five. Rules 3 and 4 read the lint's ring tables and extend rule A1 from includes to
  links, with no allowlist file: an exception is a named line with its ruling.
- **Class: HARD: the Solution Architect, for rules 3 and 4.** IM §1.4 ends: "A change to a lint rule's scope … is an
  information-model change (C4, C9)", and these two widen what A1 covers. **Rules 1, 2 and 5 are build hygiene with
  no ring in them (C1–C11: none): Coordinator's call, and B2.4 builds them without waiting.**
- **Waits:** B2.4's rules 3 and 4. Nothing else: the library items check inward linking by `link_census.py` meanwhile.
- **Ruled:** all five rules; rules 3 and 4 approved by the Solution Architect, 24 Nov 2026; rules 1, 2 and 5 are the coordinator's call, 24 Nov 2026, as recommended (build/platform/JUDGEMENT-CALLS.md PR-34; for the Product Owner's review).

### L11 · No library for the Application ring in Part 2
- **Today.** The plan's list is Platform, Vocabulary, Rules, Domain model and the stores. `src/App` is listed 39
  times by tests and the pipeline 33; `Gui/shot`'s two Application files need eleven groups of `Gui` symbols.
- **Options.** (a) As the plan says: none. (b) Add `pp_app` for `src/App`.
- **Recommendation: (a).** 72 listings, and the ring is still spread over three directories until Stage D.
- **Class: Coordinator's call** (it follows the plan's text). C1–C11: none. Waits: nothing.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026, as recommended (build/platform/JUDGEMENT-CALLS.md PR-34; for the Product Owner's review)).

### L12 · `pp_stores` and the existing swing-store library
- **Today.** `pinpoint_swingstore` holds `swing_store.cpp` and `ppsw_qt.cpp` and is compiled optimised in every
  configuration, because the store sits under GUI-thread work in the studio's Debug build (its build file's header).
- **Options.** (a) `pp_stores` links it and never lists its two files; `pp_stores` itself is compiled as its
  configuration says. (b) Fold the two files into `pp_stores`. (c) Compile all of `pp_stores` optimised.
- **Recommendation: (a).** (b) loses the optimisation or spreads it without a measurement; (c) is a performance
  change with no evidence asked for.
- **Class: Coordinator's call.** C4: none (no writer or repository changes). Waits: B2.10, B2.15b.
- **Ruled:** as recommended (the coordinator's call, 24 Nov 2026, as recommended (build/platform/JUDGEMENT-CALLS.md PR-34; for the Product Owner's review)).

## The questions on scope, for the Product Owner

### P1 · The accept figure for hand-listed test sources
- **Today.** The plan's Accept row: "test targets that hand-list sources: to 0"[^accept]. 232 of 347 test executables
  list 5,698 sources (249 distinct). The five libraries hold about 4,900 of those listings. About 120 executables
  would still list about 770: `Gui` 497, `Ppcp` 105, the launch-monitor connectors 72, `App` 39, the pipeline 33.
- **Options.** (a) "Hand-listed sources that belong to a ring library: 0", enforced by L10's rule 5; the rest counted
  and named by directory, each with the stage that owns it. (b) Keep "to 0" and add libraries for `Gui`'s presenters
  and `Ppcp` to Part 2.
- **Recommendation: (a).** (b) is Stage D's, E's and F's ground and would roughly double the stage.
- **Class: Product Owner** (an acceptance target; an agent may not re-read one[^rule9]). Waits: B2.12–B2.16, B2.22.
- **Ruled:** as recommended (the Product Owner, 24 Nov 2026).

### P2 · Who owns the audit tools' per-release re-run
- **Today.** The audit asks each release to diff the include graph, the seam, identities, sizes and duplicates
  against the last. There is no step and no owner (`docs/implementation/swe-audit/05-deferred-worklist.md:101`).
- **Recommendation.** The release runbooks, as the plan proposes[^plan]: one section in the Windows runbook, with a
  pointer from the other two.
- **Class: Product Owner.** Waits: B2.20.
- **Ruled:** as recommended (the Product Owner, 24 Nov 2026).

### P3 · What is deferred, and to whom
- **Today.** `pp_rules_vision` serves 55 of the 5,698 listings (1%), costs about 3 hours, and carries the OpenCV, moc
  and `-O2` traps; `Pose` and `IMU` still span rings until Stage D. Separately, `Analysis`' split into single-ring
  directories has no owner: the plan gives the spanning directories to Stage D but names only `Buffer`, `IMU` and
  `Pose`.
- **Recommendation.** (1) Defer B2.8 to Stage D; the lint keeps guarding those files meanwhile. (2) Give `Analysis`'
  split to #224-H, which already separates rules from words there, as a C9 proposal to the Solution Architect when
  that plan is written. Neither blocks Part 2 (L8 covers the library).
- **Class: Product Owner.** Waits: B2.8; B2.22's spanning-directories row.
- **Ruled:** as recommended (the Product Owner, 24 Nov 2026).

## What waits on what

| Question | Class | Briefs that wait |
|---|---|---|
| L1, L2, L3, L5 | Coordinator | B2.3, then every library (B2.5–B2.10) |
| L4 | Coordinator | B2.4, B2.12 |
| L6 | Coordinator | B2.10 (through #258) |
| L7 | Coordinator | B2.5–B2.8 |
| L8 | Coordinator | B2.7, B2.10 |
| L9, L11 | Coordinator | nothing |
| L10 | **HARD: the Solution Architect** (rules 3 and 4) | B2.4's rules 3 and 4 only |
| L12 | Coordinator | B2.10, B2.15b |
| P1 | Product Owner | B2.12–B2.16, B2.22 |
| P2 | Product Owner | B2.20 |
| P3 | Product Owner | B2.8 |

Not waiting on any question: B2.1 (the cabin's Release link without an install), B2.2 (the census tools), B2.17
(#90), B2.18 and B2.19 (two tools), B2.23 (#203). #187 is done (B0.4).

**Recorded with no question.** The build-time gain is larger than the plan claims for the test tree: the cabin's
clean build is 5,614 compiles, most of them the same 249 sources again, and should fall to about 1,400. The link row
(11 s) is the one that can get worse.

[^plan]: `docs/implementation/platform_refactor_assessment.md`, "Stage B · Build libraries, the `Core` split and the Vocabulary root", Part 2: "Ring libraries (proposed, for the Solution Architect)" and "The per-release re-run of the audit tools … (proposed, for the Product Owner)".
[^pilot]: Item B1.4, the Platform library pilot: its notes' §4 (six questions), its brief ("a build target is not an IM object") and its independent review of 24 Nov 2026, in `build/platform/B1/`.
[^imc]: `governance/artefacts/information_model.md` §1.2 (C1–C11) and §1.4's last line, read as excerpts (`build/platform/B2/IM-B2.0.md`).
[^hard]: `CLAUDE.md`, "Information model and architecture": any information-model change needs the Solution Architect's approval before it is built.
[^tools]: `build/platform/B2/ring_census.py` and `link_census.py`, with the commands and every figure in `build/platform/B2/NOTES-B2-plan.md` §1. Item B2.2 keeps them in `tools/refactor/`.
[^ort]: `tests/CMakeLists.txt:4` ("WITHOUT building the app (no whisper/ggml, FFmpeg, espeak …)"); a search of every test build file for ONNX Runtime finds comments only.
[^l6]: The plan's "Rulings of 24 Nov", the four contexts, L6: the libraries stay by ring; a library splits by context only where a clean seam already exists (the Solution Architect and the Product Owner, 24 Nov 2026).
[^accept]: The plan's Stage B table, the Accept row (proposed, for the Product Owner).
[^rule9]: The plan's §5 rule 9: an agent may not re-read an acceptance target; it is logged for the role that owns it.
