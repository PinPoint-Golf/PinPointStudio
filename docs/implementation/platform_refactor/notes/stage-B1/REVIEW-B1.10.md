# B1.10 review (AG-21, independent) · 26 Nov 2026

Worktree `build/wt-37c` at HEAD `1e3269a8`, uncommitted. `git diff HEAD` is `B1.10-diff.patch` (ignoring the
index lines). I read the brief, COMMON, the IM excerpt, the diff, NOTES and the `b110/` evidence. I ran the
suites through the lock. I edited nothing and built nothing.

## The brief, bullet by bullet: every one met

- **What 1:** met. `src/Core/pp_model_path.h:82`'s body is `HEAD:src/Export/model_file_fingerprint.h:115-119`
  token for token, in the same namespace. `golden/model_file_fingerprint_1e3269a8.h` diffs equal to HEAD's file.
- **What 2:** met. `pose_runner.cpp:973` calls it, with the include at `:45`. The slot, the install function, the
  `@?` branch and the warning are gone, and `pose_model_selection.h` keeps only `useVitPoseLarge`. Each of the
  three install sites loses its line and two includes. The header is staged as a delete.
- **What 3:** met. `pose_model_selection_test.cpp:74-102` checks six cases in a `QTemporaryDir`, each against a
  literal string the old reader produced, so the text is really pinned. NOTES §2 says why there is no longer a
  "no reader" state.
- **The stamp doesn't move:** met. `stamp-before.txt` equals `stamp-after.txt` (I diffed them): both stamps are
  `vitpose-b-wholebody.onnx@360242862` on two scratch swings.
  - The before run (01:47) used the old `swinglab_run`, which the app build then relinked at 01:50.
  - The after run (01:53) used the new one.
  - Parity diff: 0 differences.
- **Accept:** met. The brief's grep finds 0. `readModelFileFingerprint` and `model_file_fingerprint` appear 0
  times in src, tools and docs/developer. The lowercase name has one definition and one caller. No changed line
  names a `StageVersion`.
- **Docs:** met.
  - Header comment at `pp_model_path.h:48-55`, plus `pose_runner.h:145`.
  - IM 1.12.7 is a correctly recorded §11 status edit: version line `:6`, the row at `:1316`, change log `:2094`,
    and the footnote at `:2113`, defined once.
  - The hook survives only in history rows. Plan `:960` already reads "unless it becomes a direct call".
  - No `docs/developer` page described the hook.

## Checks

- **Every input gives the same text**, because the code is the same. That includes the golden cases the test
  omits (`>2^31` bytes, a `..` path, a directory).
- **Read at every stamp, never cached:** `analysis_pipeline.cpp:673` and `:2442` call it per stamp, with a fresh
  `QFileInfo` each time.
- **Includes:**
  - `pp_model_path.h` adds only `<QFileInfo>`, and the context table has it as Platform.
  - `main.cpp` (`:36`, `:41`), `swinglab_run.cpp` (`:64`, `:68`), `raw_reencode.cpp` (`:43`) and
    `pose_runner.cpp` (`:25`, `:36`) each include `<QFileInfo>` and `<atomic>` themselves where they use them.
  - Nothing in their `Q_OS_*` blocks used the removed headers.
- **Who reads the stamp:** only `reuse_gate.cpp:53` `recordedPoseModel()`, which carries it onto a reused pose
  as provenance. The JSON code writes and reads it, and the test fixtures use synthetic values. NOTES §1 is right
  that it is no reuse key, so the brief's "re-runs pose" risk was a wrong premise.
- **`@?` is gone everywhere:** none in src, tools, docs/developer or governance. `swing_window_parity_test`
  asserts no stamp. The tests tree has no `HAVE_VITPOSE`, so it stamps `"none"`.
- **Portable:** the accented name is built from code points, so MSVC's source encoding doesn't matter. Each
  `QFile` closes before the temporary directory goes.

## Findings

1. **EDGE (behaviour, intended):** a process that never installed the reader used to stamp `"<file>@?"` and warn
   once. It now stamps the real `"<file>@<bytes>"`. Nothing reads or asserts `@?`. This is the ruling's purpose,
   not a regression.
2. **EDGE:** the `big.onnx` fixture extends a file to 360,242,862 bytes with `QFile::resize`.
   - On the studio PC it passed in 0.09 s (`tests_ctest.log`), so it isn't slow.
   - On NTFS it may still reserve about 344 MB in `%TEMP%` while the test runs (not observed), and leave the
     file behind if the test is killed.
   - It adds little over the identical code. Shrink it in-item or file it.
3. **EDGE (process):** NOTES §5 reports one 0.1 s `ctest -V` that ran outside the lock (BT-15). No harm done.
4. **Minor, fix in item:** NOTES does not yet carry the studio PC summary, which COMMON item 2 requires (below).

My Mac run, through the lock at `-j 3`: 15 of 15 passed. That is `pose_model_selection_test` (16 checks
passing), `reuse_gate_test`, the other `pose_*` tests, the four lints, `artefact_paths_check`, and the two `im_*`
self-tests.

**Cabin** (`studio PC-wt-37c-20261126-015407`, exit 12): build exit 0; pass 1 346/347; pass 2 41/43.
- The three failures are `session_list_test` L7a (trash on Windows), `qml_ui.tst_charts_panel` test_15 and
  `qml_ui.tst_session_report` page count.
- None of the three touches the pose stamp. The lane's previous run, for B1.7 on `008b8eb4` without this patch
  (`studio PC-wt-37c-20261126-010549`), failed the same three. The two QML tests have failed in every one of this
  lane's 19 runs since 24 Nov, and L7a in 9 of them. So they predate this change.
- `pose_model_selection_test` passed under MSVC.

SIGNED OFF
