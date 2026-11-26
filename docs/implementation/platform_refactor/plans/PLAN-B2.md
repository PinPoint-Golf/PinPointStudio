# Stage B, Part 2: the libraries by ring. The plan (24 Nov 2026)

From `docs/implementation/platform_refactor_assessment.md`, "Stage B", Part 2 only. Premises checked on main `12992896`: `docs/implementation/platform_refactor/plans/NOTES-B2-plan.md`. Every brief reads the standing block (#402). Each agent first regenerates its IM excerpt from its own tree (`bash build/platform/B2/make_im_excerpts.sh <worktree>`). Lane 1 is `build/wt-37c`, lane 2 `build/wt-5b`. The briefs are written against the tree as Part 1 leaves it (`src/Stores`, `src/Diagnostics/derived/`, `src/Vocabulary/<area>/`), and each "check first" confirms the moves it needs have landed.

**Four things the plan's text gets wrong** (NOTES §2).
1. **"Test targets that hand-list sources: to 0" can't be met** by the five libraries the plan names. 232 of 347 test executables hand-list 5,698 sources; about 120 would still list about 770 (`Gui`, `Ppcp`, the connectors, `App`, the pipeline). B2.0 puts a figure to the Product Owner: **hand-listed sources that belong to a ring library: 0**.
2. **"The case for libraries is enforcement, not rebuild time"** (§10 row 2.1c) was argued about the app's rebuilds. For the test tree it understates the gain: that tree compiles each Diagnostics source about 140 times. Expect the studio PC's clean build to fall from 5,614 compiles to about 1,400.
3. **A ring library can't hold everything in its ring.** The test tree has no ONNX Runtime, whisper or FFmpeg, so the pose estimators, the runners and the video encoder stay in the executables; Rules splits by third-party need (OpenCV), which is not the ruled "context seam".
4. **Nothing in Part 2's structure is ruled.** The plan marks ring libraries "proposed", and the pilot's six questions are open. B2.0 puts them to the Solution Architect, as B1.0 did, before any library is cut.

**Entry.** The library items (B2.3 on) start when: the moves each brief names have landed; **#258** is on main; **B1.4 is committed** (after visit 1), because every item here changes the Windows build (§5 rule 1) and stacks on its build files; B1.20a's duplicate-name test is green; **B2.0 is ruled; B2.1 and B2.2 are on main** (the briefs call their tools). No B2 item that edits `src` or a build file is open while a Part 1 scripted move runs. B2.0–B2.2 and B2.18–B2.20 need none of that and are brought forward.

| # | Item (one agent run each) | After | Lane | Tier | Accept | h |
|---|---|---|---|---|---|---|
| B2.0 | The library proposals, L1–L12 and P1–P3 (docs). **Written:** `docs/implementation/platform_b2_libraries.md`; 1 HARD (L10's rules 3 and 4), 11 coordinator's calls, 3 for the Product Owner | now | docs | coordinator reads | 15 questions, each with counts and a recommendation | 1.5 |
| B2.1 | The studio PC links the Release app without installing it (`studio-build.cmd`) | now | either | independent | one proven run on main; the installed exe's hash unchanged | 1.5 |
| B2.2 | The two census tools kept in `tools/refactor/`, with self-tests; the start figures | now | either | independent | 2 self-tests in ctest; the baseline table | 1.5 |
| B2.3 | The library frame (`cmake/rings/`), with `pp_platform` in it | entry; B1.11 | 1 | independent | 1 frame, both trees; `pp_platform` unchanged in content | 2.5 |
| B2.4 | The library-membership check (a ctest lint over the manifest) | entry; L10 | 2 | independent | red on 4 planted faults, green on main | 2 |
| B2.5 | `pp_vocabulary` | B2.3; B1.5, B1.7, B1.13 | 1 | independent | global-only includes in its files 3 → 0 | 2 |
| B2.6 | `pp_rules`, the Diagnostics part (B-14) | B2.5; B1.6, B1.14, B1.15b, B1.19 | 1 | independent | B-14 closed; link census rows 0 | 3 |
| B2.7 | `pp_rules`, the measurement directories without OpenCV | B2.6; B1.11, B1.12 | 1 | independent | global-only includes in Rules files without OpenCV → 0 | 2.5 |
| B2.8 | `pp_rules_vision` (OpenCV: `Club`, `Ball`, `Pose`'s single-ring files). **Recommended: defer to Stage D** (P3) | B2.7 | 1 | independent | Rules sources left in executables: named, ORT and Stage D only | 3 |
| B2.9 | `pp_domain` (the first library with `Q_OBJECT`) | B2.7 | 1 | independent | 6 moc'd headers in one library; no test mocs them | 2 |
| B2.10 | `pp_stores` | B2.9; B1.16, B1.17 | 1 | independent | store-isolation lint sees its 2 settings users | 3 |
| B2.11 | The header-only workarounds (`atomic_write.h`, `gate_catalogue.h`) | B2.5 | 1 | independent | 2 → 0, or each kept one explained | 1.5 |
| B2.12 | Tests link `pp_platform` (every suite list) | B2.3, B2.4 | 2 | independent | Platform listings 472 → 0 | 2 |
| B2.13 | Tests link `pp_vocabulary` | B2.5, B2.12 | 2 | independent | Vocabulary listings 404 → 0 | 1.5 |
| B2.14a | Tests link `pp_rules`, the Diagnostics part (the shared lists) | B2.6, B2.13 | 2 | independent | Diagnostics listings 2,640 → 0 | 2.5 |
| B2.14b | Tests link `pp_rules`, the measurement directories | B2.7, B2.14a | 2 | independent | `pp_rules` listings → 0 | 1.5 |
| B2.15a | Tests link `pp_domain` | B2.9, B2.14b | 2 | independent | Domain model listings 254 → 0 | 1.5 |
| B2.15b | Tests link `pp_stores` (and `pp_rules_vision` if built) | B2.10, B2.15a | 2 | independent | stores listings 728 → 0 | 2 |
| B2.16 | `qml_ui_test` and `session_setup_ui_test` link the libraries | B2.15b | 2 | independent | `qml_ui_test`'s 126 listed sources → its `Gui` ones only | 2.5 |
| B2.17 | #90: the two stale UI suites run and are re-pointed | entry (a filler; never beside B2.16) | 2 | independent | every red fixed in the test or filed | 2.5 |
| B2.18 | `swinglab_run --doc-blocks` exits non-zero on a failed write | now | either | independent | exit code ≠ 0, with a test | 1 |
| B2.19 | `parity_baseline.py --self-test` beside other arguments | now | either | independent | the self-test in ctest | 0.5 |
| B2.20 | The audit tools' per-release re-run gets its owner (docs) | P2 | docs | coordinator reads | 3 runbooks name the step | 0.5 |
| B2.21 | The app's global include directories trimmed to what its own files need | B2.10 | 1 | independent | 50 → the number set at check-first | 2 |
| B2.22 | Part 2's close: counts and timings re-taken by an agent that built none of it | all | alone | n/a | the Accept row, each figure with its command | 2.5 |
| B2.23 | #203: a probe's discards go to a scratch trash (the Closes row pairs it with #187, done in B0.4) | entry (a filler) | 2 | independent | the real Trash's count unchanged by a probe | 1.5 |

**Lanes.** Lane 1 owns the root `CMakeLists.txt`, `cmake/` and `tests/cmake/`: each library is defined there and wired into the app and the offline tools, so the cuts are serial. Lane 2 owns `tests/CMakeLists.txt`, every `src/*/tests/CMakeLists.txt` and `src/Gui/CMakeLists.txt`. Lane 2 relinks a library only after lane 1 has landed it, so it trails by one item; its waits are filled by B2.17, B2.23, B2.18 and B2.19, which need no library. Hours are from entry, on the estimates.

| Hours | Lane 1 runs (then its next two) | Lane 2 runs (then what is ready) |
|---|---|---|
| before entry | B2.1 (B2.18, B2.19 ready) | B2.2 (B2.18, B2.19 ready) |
| 0–2.5 | B2.3 (B2.5, B2.6) | B2.4 (B2.17, B2.23 ready) |
| 2.5–4.5 | B2.5 (B2.6, B2.7) | B2.12 (B2.17, B2.23 ready) |
| 4.5–7.5 | B2.6 (B2.7, B2.9) | B2.13, then B2.17 or B2.23 (the other ready) |
| 7.5–10 | B2.7 (B2.9, B2.10) | B2.14a (B2.23 or B2.17, B2.18 ready) |
| 10–12 | B2.9 (B2.10, B2.11) | B2.14b (fillers as above) |
| 12–15 | B2.10 (B2.11, B2.21) | B2.15a, then **a wait of about 1.5 h for B2.10** unless a filler is left: bring forward a test-only issue (#256's kind) |
| 15–18.5 | B2.11, B2.21 (then B1.20, #224-U) | B2.15b, B2.16 (B2.22 after both lanes) |

If B2.8 runs (P3), it goes after B2.7 and every later lane 1 row moves 3 h. Lane 2's ready list falls to one item twice (after B2.13 and after B2.15a) once the four fillers are used: that is the place to bring a test-only issue forward, and the plan says so rather than hide it.

**Review tier.** Independent for every code item: each alters what the app or a test links (the plan's Tier row), and no item is a scripted move, which is §5 rule 5's only ground for a self-check. The coordinator reads B2.0 and B2.20.

**Accept, for the stage** (start figures on `12992896`; B2.2 re-takes them after Part 1). Ring libraries: 1 → 5 (6 with B2.8). Hand-listed test sources that belong to a ring library: about 4,900 → 0; executables that still hand-list anything: 232 → about 120, each named with its reason. Global-only quoted includes (the plan's method): 425 → the executable's own, about 366; in library files 59 → 0. `_pinpoint_offline_sources`: 94 → those in no library. Spanning directories: 8 → `Buffer`, `IMU`, `Pose`, `Gui`, `Core` (one file), `LaunchMonitor` (by sub-folder), all Stage D's, **and `Analysis`, which has no owner** (L8). Build times on the studio PC against `e10eddf7` (clean 1,082 s, `pp_debug.h` 228 s, `swing_analysis.h` 762 s, link 11 s): none worse; the link row is the one at risk.

**Effort.** About **50 h** (37–63 h) for 26 items, against ORDER's 10–16 h, which counted no test relinking and no proposals. At the measured 2.4 h an item it is 62 h. Two lanes: about 19 h from entry, plus 5 h before it and the close, about 27 h of running: **2 days** at 14–16 h a day, plus the wait for B2.0's ruling and visit 1. **Recommendations to the Product Owner:** defer B2.8 (it serves 55 of 5,698 listings, 1%, and carries the OpenCV, moc and `-O2` traps); B2.6 with B2.14a is 46% of the listings for 5.5 h.

**Cabin** (🟡, no hands, studio idle; no visit). Every code item: the full suite through `tools/method/cabin_run.sh`, which is the Windows **Debug** link (the studio PC has no Debug app tree). **Release:** each library item links `swinglab_run` with `studio-build.cmd tool nopull patch` (no install, exists today) and the app with B2.1's mode. **Timings:** `tools/method/build_times.sh` builds main only, so the coordinator runs it after B2.3, B2.14b and in B2.22, about 35 min each.

**Risks.**
- **MSVC drops unreferenced objects.** The five rings hold no file-scope registrar, no `Q_GLOBAL_STATIC`, no start-up function and no `QML_ELEMENT` (census). Rule: no library holds a QML module or a `.qrc` in Part 2; resources stay on the executables (`resource_norm_provider.cpp` reads `:/`).
- **`Q_OBJECT` and moc.** 6 headers in `DomainModel`, 5 in `Pose`, 1 in the stores. The library runs moc itself in both trees; a test that also lists the source is a duplicate, which B2.4 fails.
- **Duplicate symbols.** `pinpoint_swingstore` already holds two store files and `pinpoint_buffer` holds `swing_window.cpp`; four test stubs replace product symbols (none in the five libraries today); two types of one name in `pinpoint::analysis` link silently once objects are pooled (B1.20a first).
- **Link order.** One inner-to-outer symbol exists today (B1.14 removes it). Each library names its inner libraries `PUBLIC`; GNU ld (the Linux release) is order-sensitive and is not on the studio PC.
- **One build per configuration.** `/bigobj` for Diagnostics (C1128); `-O2` test targets against `/RTC1` (D8016); `skeleton3d_fit.cpp` is compiled with `NO_QT` by one test; sanitizer presets need the libraries instrumented.
- **Membership.** A source in two libraries, or in a library and an executable; the store-isolation lint not seeing a library (#258).

**Ownership (IM §10).** No domain data: no object, file or id changes its owner, level, writer or persistence, and no view reaches anything new. No file changes ring or directory (C9: none), except where a brief says "stop and propose". Ids: RING-PLATFORM, RING-VOCABULARY, RING-RULES, RING-DOMAIN, RING-ADAPTERS (the stores), B-14.
