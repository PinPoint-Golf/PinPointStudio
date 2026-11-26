# B1.10 · the pose model file's fingerprint is a direct call; the installed hook goes (26 Nov 2026)

Lane `build/wt-37c`, unattended overnight. Status before reset: clean, detached HEAD = origin/main `1e3269a8` (B1.7),
nothing off main, no stash. Reset/clean to origin/main `1e3269a8`. IM excerpt regenerated
(`make_im_excerpts.sh build/wt-37c`; RING-PLATFORM names `pp_model_path`, EXT-14 the model files).

## 1. Check first (on 1e3269a8)

- Install sites, `grep -rn installModelFileFingerprint src tools`: `Gui/main.cpp:338`, `tools/swinglab/src/swinglab_run.cpp:596`,
  `tools/swinglab/src/raw_reencode.cpp:66` (3; the brief's line numbers moved by 1 and 5), and the test
  (`Pose/tests/pose_model_selection_test.cpp:82,86`). One caller: `Pose/pose_runner.cpp:972` (`PoseRunner::modelIdentity`,
  itself called at `analysis_pipeline.cpp:673` and `:2442`, the face-on and DTL stamps). No other site.
- The text: `readModelFileFingerprint` (`src/Export/model_file_fingerprint.h:36`) = `QFileInfo(path).fileName() + '@' +
  QString::number(fi.exists() ? fi.size() : 0)`.
- **Premise, for the coordinator:** the stamp is **not** a reuse key. `reuse_gate.h:32,93`, the pipeline guide (DEC-64,
  `analysis_pipeline_developer_guide.md:733`) and its one reader (`swing_reanalyzer.cpp:175`, `recordedPoseModel()` carried
  onto a reused pose as provenance) say so: a changed stamp would not make swings re-run pose. It is still the text every
  swing carries in `analysis.versions.pose.model` / `poseDtl.model`, so it is proved byte-identical below.

## 2. Duties of the hook (AG-10), and where each goes

| Duty | Today | After |
|---|---|---|
| The stamp's text, `"<file>@<bytes>"`, 0 bytes when missing | `readModelFileFingerprint`, through the slot | `pinpoint::modelFileFingerprint` (`Core/pp_model_path.h`), the same two statements moved token for token |
| Read at every stamp, never cached (a model downloaded mid-session reads fresh) | the slot asked per call | a plain call per stamp; no cache |
| An uninstalled reader stamps `"<file>@?"`; `pose_runner.cpp:977` warns once | any process that forgot the install | **no such state**: every process reads the file. No test or tool asserts `@?` (grep: only the hook's own comments, the warning and the test's line 81) |
| Who relied on `@?` | the test's "no reader installed" / "uninstalled again" cases | re-pointed: there is nothing to install, so the cases go; the test checks present and missing files instead |
| Thread safety | the slot is a `std::atomic` | a free function of its argument: nothing shared |
| Platform includes nothing outside Platform | — | `pp_model_path.h` gains `<QFileInfo>` only (Qt) |
| Processes that never installed it now stamp the real fingerprint, not `@?` | 7 test mains link analysis without the install (`shot_source_map_test`, `swing_window_parity_test`, `ball_anchor_test`, `skeleton3d_pool_test`, `architecture_lint_test`, `swing_doc_test`, `recording_store_test`) | none asserts a pose model stamp; the tests tree has no ViTPose (`modelIdentity` = `"none"` there) |

## 3. Before: today's code on real recordings and on the test's files

- Real recordings, read only: two corpus swings (`2026-06-11_…_Wrist_01/swing_0001`, `2026-07-03_…_Wrist_01/swing_0002`)
  copied to `the share `b110/stamp_run.sh before` runs B1.7's `swinglab_run`
  (`build/wt-37c/build/app`, built 01:07 on 1e3269a8, = this HEAD) with `--out` on the scratch tree and `--pose` the pinned
  `corpus/pose2` track, no `--write-back`. With an injected pose the stamp is `modelIdentity()`'s own answer
  (`analysis_pipeline.cpp:671`: only a reused pose carries its recorded stamp). `b110/stamp-before.txt`: both swings
  `pose.model=vitpose-b-wholebody.onnx@360242862`, `poseDtl.model=` the same; the file in `build/app/_deps/vitpose` is
  360,242,862 bytes, so the stamp is the real size, not `@0`.
- The stored strings for the test: `b110/golden/golden.cpp` compiles 1e3269a8's `model_file_fingerprint.h` verbatim
  (`golden/model_file_fingerprint_1e3269a8.h`) and prints its answer on each case (`golden/golden-1e3269a8.txt`):
  `vitpose-test.onnx@7`, `absent.onnx@0`, `empty.onnx@0`, `modèle fp16.onnx@12`, `big.onnx@360242862`, `@0` for no path.
  Today's test before the change: `b110/ctest-before-pose_model_selection.log`, 5/5 checks.

## 4. What changed

- `src/Core/pp_model_path.h`: `pinpoint::modelFileFingerprint(const QString &path)` after `modelFilePath`; `<QFileInfo>`;
  the header comment says what the text is and why the read is allowed here. Its body is 1e3269a8's
  `readModelFileFingerprint` body byte for byte (`cmp` of the two `{…}` blocks: identical).
- `src/Pose/pose_runner.cpp`: `modelIdentity` returns `pinpoint::modelFileFingerprint(path)`; the empty check, the
  once-only warning and the `"@?"` return are gone; includes `../Core/pp_model_path.h` (`<QFileInfo>` and `<atomic>` stay:
  used at `:402`, `:79`, `:272`). `pose_runner.h:145` says where the text comes from.
- `src/Pose/pose_model_selection.h`: the slot, `installModelFileFingerprint`, `pinpoint::pose::modelFileFingerprint`, its
  comment and `<atomic>` removed (no includer relied on it: `main.cpp` and `swinglab_run.cpp` include `<atomic>` themselves).
- `src/Gui/main.cpp`, `tools/swinglab/src/swinglab_run.cpp`, `tools/swinglab/src/raw_reencode.cpp`: the install line, its
  comment and the two includes. None used anything else of `pose_model_selection.h`.
- `src/Export/model_file_fingerprint.h`: deleted.
- `src/Pose/tests/pose_model_selection_test.cpp`: re-pointed, not deleted: six checks of `pinpoint::modelFileFingerprint`
  against §3's stored strings in a `QTemporaryDir` (present, missing, present and empty, an accented name with a space, the
  bundled model's size as a sparse file, no path). The "no reader installed" and "uninstalled again" cases go: there is no
  such state now. `src/Pose/tests/CMakeLists.txt:54` comment.
- Docs: `governance/artefacts/information_model.md` 1.12.7 (a status edit, §1.2's §11 exclusion): §11's "Rules without
  file I/O" row names the direct call; header version line; change-log row; footnote `[^sa24-j107]`. `docs/developer/`:
  no page names the hook (grep). The deck: no mention (slide text searched). The plan's J-107 rows are the plan: left.
- Real stores: none reachable (the model file is read with `QFileInfo` only, as before).

## 5. Mac (on 1e3269a8 + the change)

Every heavy job through `tools/method/one_at_a_time.sh`, `-j 3`, under `b110/dog.sh` (`ps -o etime=`).
- Builds (2, the budget; `b110/mac_builds.sh`): tests tree exit 0, 0 errors (`b110-build-tests.log`); app tree configured
  with `PINPOINT_BUILD_TOOLS=ON`, `PINPOINT_BUILD_LM_REPAIR=ON` (`b110-configure-tools.log`, "lm_repair enabled") and built,
  exit 0, 0 errors (`b110-build-app.log`): `PinPointStudio`, `swinglab_run`, `raw_reencode`, `swing_window_parity_test` relinked.
- **The stamp, after** (`b110/stamp_run.sh after`, the new `swinglab_run`, same scratch copies and pinned poses):
  `b110/stamp-after.txt` = `stamp-before.txt` byte for byte (`cmp`): `vitpose-b-wholebody.onnx@360242862` for `pose.model`
  and `poseDtl.model` on both swings.
- ctest (`b110-ctest.log`): **20/20 passed**: `pose_model_selection_test` (the six new checks pass on the stored strings),
  `reuse_gate_test`, the nine `pose_*` / `camera_pose_sticks` / `inference_thread` tests, `swing_doc_test`, the four lints
  (architecture, design_system, store_isolation, session_setup), `artefact_paths_check`, `design_system_tokens_check`,
  `im_excerpt_selftest`, `im_changelog_merge_selftest`, `context_tree_test`. No `pose_runner` test exists (the brief's regex
  names one). One slip: a single `ctest -R '^pose_model_selection_test$' -V` re-read (0.1 s) ran outside the lock.
- Accept greps: install sites 0; `grep -rn 'modelFileFingerprintSlot\|installModelFileFingerprint' src tools` 0;
  `model_file_fingerprint` in src/tools/CMake 0; no `k*StageVersion` in the diff (one unchanged context line).
- `artefact_paths_check.py`: 0 stale (3,577 files).
- **Parity:** `parity_baseline.py b110-fingerprint` exit 0 (`b110-parity.log`); `parity_diff.py b1-base-2 b110-fingerprint`:
  **0 differences** (`b110-parity-diff.log`). (The parity half reads diagnostics and athlete records; it runs no pose.)

## 6. Cabin run

`b110/cabin_wrap.sh` (B1.7's wrapper, log `b110-studio PC.log`). Run `studio PC-wt-37c-20261126-015407` (summary
`build/wt-37c/build/studio PC-runs/studio PC-wt-37c-20261126-015407.txt`, run dir kept on the share): exit 12. Cabin HEAD 1e3269a8 +
patch fb9d351a… (11 files); build exit 0 (12 min 56 s); 393 registered; stores unchanged; checkout restored clean.
- **Pass 1: 346/347**, 1 not run; `pose_model_selection_test` passed on Windows (0.09 s). The one failure is
  `session_list_test` L7a (#250), the same line as B1.6's and B1.7's runs.
- **Pass 2: 41/43**, the standing two: `tst_charts_panel` test_15 (#231), `tst_session_report` page count (#253).
- Accept: no new failure against the last studio PC runs on main.

## 7. Independent review (AG-21)

A fresh reviewer agent (not a fork), given the brief, COMMON, the excerpt, the diff and these notes: `REVIEW-B1.10.md`,
**SIGNED OFF**. Its own ctest run through the lock passed 15/15. No blocking finding.
- EDGE 1: a process that never installed the reader now stamps the real fingerprint rather than `@?`. This is intended:
  it is the point of the ruling.
- EDGE 2: the test's sparse `big.onnx` (360 MB by `QFile::resize`) may reserve that much in `%TEMP%` on NTFS while it
  runs. It passed on the studio PC in 0.09 s. Left as it is, for an issue if wanted.
- EDGE 3: the 0.1 s ctest re-read outside the lock (§5). Minor 4 (the studio PC summary in NOTES) is done in §6.

## 8. Commit

Main had moved to `ebd8c952` (two move-tool commits, `tools/refactor/` only; IM still 1.12.6 there, so 1.12.7 is free).
Committed in wt-37c (`git add -A -- . ':!build'`, 11 files: 1 D, 10 M), rebased cleanly; on the rebased tree the item's
suites, both lints and `move_files` re-ran: **21/21 passed** (`b110-ctest-rebased.log`; no C++ changed upstream, no
rebuild). Message `B1.10-commit-msg.txt`. **`14f37426`** pushed to main (fast-forward from `ebd8c952`) under GIT-2 after
the sign-off, the Mac half and the studio PC run. wt-37c clean; app tree set back to tools off (`b110-configure-tools-off.log`).
Scratch run tree `the share deleted after the numbers were logged here (stamps in `b110/`).
- For the coordinator: (1) the brief's premise is wrong: the stamp is provenance, not a reuse key (§1); (2) no
  `pose_runner` ctest exists; (3) EDGE 2 (the 360 MB sparse fixture on NTFS) if an issue is wanted.
