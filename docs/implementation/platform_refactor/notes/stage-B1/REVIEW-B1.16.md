# Review · B1.16 · `src/Export` is renamed `src/Stores` (AG-21, 26 Nov 2026)

Independent reviewer. I read the brief, MOVES.md, the standing block (#402), the notes, the diff and the logs, and re-ran the checks
below. I edited nothing in the repo and rebuilt nothing.

## What I checked, and what it showed

1. **The patch is the work tree.** `git -C build/wt-37c add -A . && git diff --cached -M` is byte-identical to
   `B1.16-diff.patch` (6,532 lines). It holds 71 renames (52 at R100; 19 R095–R099, the .h and test files whose
   includes changed), 216 modified files and 1 added file (the map). After my `reset -q` the index is empty.
   **Note for the coordinator:** before my reset the implementer's index held the `git mv` renames. They are now
   unstaged (`D src/Export/…`, `?? src/Stores/`), so the commit needs GIT-7's `git add -A -- . ':!build'`.
2. **`--verify`, re-run by me without configure:** renames checked 71; files with changed lines 287; 15 lines listed
   for the reviewer (the map, the 5 allow-prose files, lint :189, `display_tone_test.cpp` :309/310/312/314/336, and the
   3 `keep:` change-log rows left exactly as written). **Other lines: 0, VERIFY PASSED**, then NOT CONFIGURED (exit 3),
   as expected.
   - My own check: I mapped every removed line `Export/`→`Stores/` and compared it with the added lines. Every line
     left over is an include line, the IM's prose, the map's comments, the lint's :189 comment, or one tool rewrite in
     `src/Pose/tests/CMakeLists.txt:37`: `${POSE}/../Export` became `${PP_SRC}/Stores`, the same folder named another
     way. `PP_SRC` is already used on :36, and the build passes.
   - No code line changed except path strings and comments.
3. **Nothing that is not a path changed.**
   - `"Export.Zip"` (`swing_zip_exporter.cpp:246`) and `"Export.Staging"` (:296, :314, :327) are unchanged.
   - No QML or JSON file is in the diff.
   - No namespace, class, settings key or on-disk path changed. The lint fixture's variable `exportToGui` keeps its
     name, which is correct.
   - The packaging scripts and `tools/studio` are untouched.
4. **The lint.**
   - Every keyed place is re-keyed: `dirRings()` :189, `a3Allowed` `Stores/swing_paths`, A4's 16 owners, both
     `countedStoreDoor()` stems, `a5Exempt()`'s `Stores/` prefix, `isRepositoryHeader()`'s 13 stems, and the fixtures.
   - `Export` survives only at :85, as history.
   - The allowlist has 176 lines before and after, and is identical once `Export/`→`Stores/` is applied: 4 entries and
     comment :23, re-keyed, none added.
   - `b116/plants-lint.out` shows each plant bites:
     - P1: A1 "an adapter outside Gui reaches the UI";
     - P2: 3 × A5 "a repository outside its owner";
     - P3: "Stores/session_list_reads.cpp: 2 findings, 1 allowed";
     - P4: the old `Export/` prefix is no longer exempt and has no ring.
   - The lint run 3 failures with the plants in, and passed after they were removed.
   - The new `a5Exempt` prefix, `a3Allowed` and the A4 owners are proven by the clean lint and its fixtures (a stale
     allow-key would make findings), not by a plant. That proof is sound.
5. **Includers.**
   - `count_includers.py Stores/`: 59 files outside `tests/`, 185 includer files, 544 includes. That is the notes'
     "before" for `Export/` (I did not re-count on HEAD).
   - `Export/` now: 0 includes; `src/Export` does not exist; `src/Stores` holds 71 files, matching HEAD's
     `src/Export`.
   - Both builds and the studio PC build pass, so no include is unresolved.
6. **IM 1.12.13.** I read every changed hunk (19).
   - The version line, §1.4's A1 and A5 rows, §2.2 (:277), §4.1's RING-ADAPTERS (:440), §5.4 (:669), the FILE rows
     (:962, :975) and the §11 rows (rename done, A1, P11, housekeeping, the `Shot` entity's "the stores'") are
     accurate, and each traces to R6 (`platform_b1_placements.md` R6, "Ruled: (a), `src/Stores`", Solution
     Architect, 24 Nov).
   - The footnote `[^sa24-r6]` and change-log row 1.12.13 are placed after 1.12.12.
   - Rows 1.9.52, 1.10.4 and 1.12.1 are kept by `keep:`, and 1.12.7 by "can't place". DEC-146 keeps `Export`, as
     recorded.
   - RING-ADAPTERS' second cell now says "(the Recording store, the session and athlete stores and the file doors)",
     not "(the Recording store)". That describes what the folder already holds (R6's "Today"), not a new placement.
     I accept it.
7. **The allow-prose files, read by eye.**
   - README :86: "The stores: the swing, session and athlete files, and the video and zip exports". True.
   - `rig_design.md` :102 and :565–567: the planned store and doors are re-pathed. They are designs, so the new path
     is right.
   - `swinglab_developer_guide.md` :217, :265, :680; `testing_developer_guide.md` :385, :387, :589, :595, :675, :757:
     path changes, plus "the Export ring" → "the stores".
   - All of these are true, and none adds meaning (AG-14).
8. **Tests.**
   - I re-ran 9 suites on the existing tree through the lock: `architecture_lint_test`, `design_system_lint_test`,
     `store_isolation_lint_test`, `display_tone_test`, `swing_doc_test`, `session_repository_test`,
     `swing_paths_test`, `artefact_paths_check` and `im_changelog_merge_selftest`. **9/9 passed.**
   - The build is not stale. The only source newer than the tests tree's `.ninja_log` is `session_list_reads.cpp`, put
     back after the plant with its content unchanged. No file was edited after the studio PC launch (05:42).
   - `b116-ctest.log`: 32/32. The Mac builds exit 0. Parity against `b1-base-2`: 0 differences.
   - **Cabin** `studio PC-wt-37c-20261126-054205`: build exit 0.
     - Pass 1: 345/347. Pass 2: 41/43. The totals are the same as every earlier run, so no suite was dropped.
     - The four failures are standing, and all four issues are OPEN:
       - #250, `session_list_test` L7a: 22 earlier runs show it;
       - #263, `paced_connect_queue_test` T4 at 77 ms: 9 earlier runs show it;
       - #231, `tst_charts_panel` test_15;
       - #253, `tst_session_report` page count.
     - The claim holds.
9. **Commit message** `b116/commit-msg.txt`.
   - GIT-8: "Platform:" is an area in the log (3 of the last 60).
   - GIT-10: no trailer. GIT-6: the tests and docs are named.
   - GIT-9: plain words, with two wording slips (finding 7).

## Findings

1. **EDGE.** `docs/reference/swing_json_schema.md` still names 14 `src/Export/…` paths, and
   `docs/user/killer-features.md:304` names one more.
   - `docs/reference/` is outside the move tool's `SCAN_DIRS` and the brief's doc list.
   - The gap predates this item: the same file already carries B1.5's stale `src/Recording/recording.h`,
     `src/Metrics/…` and `src/LaunchMonitor/launch_monitor_reading.cpp`.
   - The fix is path-only, so the coordinator may fold the 14 lines into this commit (GIT-6) or file one issue: widen
     the tool to `docs/reference/` and sweep every move's stale paths.
2. **EDGE.** `governance/artefacts/lifecycle_model.md` has 17 path-only rewrites from the tool. The brief and the
   notes don't mention it, and its change log (§8) gets no row, though every earlier commit that edited it added one.
   - Its file:line citations are pinned "at `15d41578`" and "at `8f33e190`", and now name a folder those commits
     lacked. The line numbers still hold, because the .cpp files are R100.
   - For the Solution Architect: a "paths only" 1.7 row, or accept as is.
3. **EDGE (evidence).** The saved `B1.16-verify.txt` is the first, **failed** verify ("other lines: 4", VERIFY
   FAILED) with the configure attached. No saved log holds the passing verify.
   - The notes say a re-verify passed. My re-run confirms 0 other lines, and only IM prose changed after that
     configure, which the configure does not read.
   - Before the push, the coordinator should save a passing `--verify --configure` log on the rebased tree (MOVES:
     "A `--verify` without `--configure` is not a pass").
4. **EDGE (notes).**
   - NOTES cites a "§8" twice (:70, :81) that does not exist.
   - It says the app tree was "set back to tools off after", but `build/wt-37c/build/app/CMakeCache.txt` still has
     `PINPOINT_BUILD_TOOLS=ON` and `PINPOINT_BUILD_LM_REPAIR=ON`, and there is no `b116-configure-tools-off.log`.
   - There is no EDGE list (COMMON item 4).
5. **EDGE, minor (wording).** Bare-word comments still call the folder "Export":
   - `src/Analysis/swing_reanalyzer.h:45`, which now reads "src/Stores/… (the Export ring)";
   - `src/Stores/swing_folder_reanalysis.h:23` ("in the Export ring");
   - `src/App/housekeeping.h:27`, `:73` and `src/Gui/main.cpp:468` ("carried out in Export"; the IM's §5.4 twin was
     changed);
   - `src/DomainModel/recording_repository.h:275`;
   - `src/Analysis/tests/CMakeLists.txt:542`, `:585`, `:597`, `:617` ("(Export)");
   - `architecture_lint_test.cpp`'s "src/Stores's files", which reads better as "src/Stores' files".
6. **EDGE.** The walkthrough deck names `src/Export` twice (NOTES §5). It needs the Product Owner's refresh, as the
   notes say.
7. **EDGE, minor (wording), commit message.**
   - "Only two of its files export anything (the zip and the video)" is not exact: the video and zip exporters are
     several files (`swing_zip_exporter.*`, `video_encoder.*`, `ffmpeg_video_encoder.*`, `export_tone.*`). Say "only
     its zip and video exporters export anything".
   - "each re-keyed lint table was proven by a planted violation" overclaims: `a5Exempt`'s new prefix, `a3Allowed`
     and A4's owners rest on the clean lint. Say "the re-keyed tables were proven by planted violations and by the
     clean lint".
   - "the move tool's whole-tree check (71 renames, 0 other lines)" is true only of the re-verify, whose log was not
     saved (finding 3).

No blocking finding. The rename's deliverable is met:
- `src/Export` goes from 71 files to 0;
- the allowlist's 4 lines and 1 comment are re-keyed, with the same count;
- every keyed place in the lint is re-keyed and shown to bite;
- no include is unresolved;
- nothing but paths changed;
- no new failure on the Mac or the studio PC.

SIGNED OFF
