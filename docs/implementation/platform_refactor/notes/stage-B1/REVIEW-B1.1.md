# REVIEW B1.1 · independent review (build/wt-5b, uncommitted diff on e10eddf7)

**Verdict: BLOCKING.** The fix is to the wording of three comment lines; the code is sound. The deletion and the include removal are correct. Three of the new comments say something false, and saying the right thing in those comments is this item's job. Once the three lines below are fixed and the two design-system checks re-run, I sign off. No rebuild is needed.

## 1. Nothing used them: confirmed
- `git grep -nIi 'video_overlay\|VideoOverlay'` over the whole tracked tree (src, tools, tests, cmake, every CMake file, docs) finds nothing outside the four deleted files except:
  - `src/Analysis/tests/CMakeLists.txt:1299`, a comment;
  - the historical docs that NOTES §3 lists (`swing_characteristics_impl_plan.md`, `wholebody_pose_design.md:292`, `code_quality_review.md:151`);
  - the plan's own mentions (`platform_refactor_assessment.md:748–750`, `platform_b1_placements.md:214`).
- No slot, signal or string lookup survives: there are no hits for `overlayFrame`, `onAnnotatedFrame`, `updatePose`, `clearPose` or `drawSkeleton`.
- Nothing registers or names the classes: no `qmlRegister*` or `QML_ELEMENT` overlay type, no moc or wrap list, and no `"…Overlay…"` factory string. Both classes were `Q_OBJECT` under AUTOMOC only.
- The deleted `.cpp` defined nothing global outside its anonymous namespace and its class's members. No other file included either header, so nothing could have relied on what they included.
- **`hm_frame.h`:** `hm_session_worker.cpp` uses none of its symbols (`hm_frame::`, `Candidate`, `kCandidate*`, `isSelected`, `frameMap`, `mountM`, `toAnatomical`, `wristRelFromHm`, `pronationRateDps`, `tuned::`, `QVector3D`). Two of the things that header brought in still reach the file another way:
  - `QQuaternion` comes through `hm_session_worker.h`, then `hm_session_types.h:33`;
  - `<cmath>` and `<QtMath>` are included directly.
  - The compilers confirm it. Clang compiled `hm_session_worker.cpp.o` with 0 errors (`build/b11-app.log`, rc 0). MSVC on the studio PC built the patch on c8d387ec with exit 0.

## 2. The parity test keeps every check: confirmed
- With comments stripped, the old and new `anatomy_overlay_parity_test.cpp` are identical (`diff` rc 0). Every non-comment line in the diff is unchanged.
- The constants still mean what they meant, because the deleted C++ and the QML agreed:
  - `kMinScore` 0.25 is in C++ `:54` and QML `:409`, `:435` and `:598`;
  - the neck and pelvis midpoints and `neckScore = min` are in C++ `:172–176` and QML `:478–480`;
  - the ten body bones are C++ `kBones` and the first ten entries of QML `kBlueprintBones` (`:376–380`).
- `anatomy_overlay_parity_test` passed.

## 3. QML and Theme.qml changed in comments only: confirmed (but see BLOCKING)
- Every added and removed line in `PpCameraFrame.qml` (6/6), `Theme.qml` (1/1) and `src/Analysis/tests/CMakeLists.txt` (4/4) is a `//` or `#` line. No code line or token changed.
- `design_system_tokens_check` (`make_design_system.py --check` on wt-5b's src) passed, and so did `design_system_lint_test`.

## 4. No information-model change: confirmed
IM `RING-ADAPTERS` (line 421) does name `hm_session_worker` and `Video`. Membership is unchanged: no file moves between rings, and the change only removes one include that pointed out of its context. No IM id names the overlay files.

## Tests (run through the lock)
`ctest -R 'anatomy|hm_|imu_|architecture_lint|design_system' -j 3` passed 20 of 20. That is 20, not NOTES' 19, because `anatomy_vocabulary_test` also matches. It includes:
- `anatomy_overlay_parity_test`;
- `design_system_lint_test` and `design_system_tokens_check`;
- `architecture_lint_test`;
- every `hm_*` and `imu_*` test.

## BLOCKING: three false comments, with the exact fixes
The new text says the QML is "the only copy" and the pose tokens are "read only by paintBlueprint()". It is not:
- `_paintElem()` (`PpCameraFrame.qml:597` on, the replay fan pass) repeats the body bones (`:641–690`), its own `kMinScore` and its own midpoints.
- The `Theme.pose*` tokens are read by:
  - `_paintElem` (`:603–606`), the club ink (`:1434`) and the ROI handles (`:1992–1995`) in `PpCameraFrame.qml`;
  - `PpCameraTiles.qml:558`, `PpMarkupPanel.qml:974–1036` and `SwingViz3DView.qml:282–287`;
  - `Theme.colorScrim` (`Theme.qml:418`), which is built from `poseInk`.

A future change to a token based on these comments would silently change all of those places. The fixes are one line each, so no line count changes:

1. **`src/Gui/theme/Theme.qml:414`.** Replace
   `    // direction. Read only by PpCameraFrame's paintBlueprint(), the one skeleton painter.`
   with
   `    // direction. QML only (PpCameraFrame, PpCameraTiles, PpMarkupPanel, SwingViz3DView, colorScrim); no C++ mirror.`
2. **`src/Gui/cameras/PpCameraFrame.qml:374`.** Replace
   `    // in paintBlueprint(). The only copy: anatomy_overlay_parity_test pins it.`
   with
   `    // in paintBlueprint(). No C++ mirror; _paintElem() repeats the body bones; anatomy_overlay_parity_test pins those.`
3. **`src/Gui/cameras/PpCameraFrame.qml:423`.** Replace
   `    // C++ painter (retired unused); now the app's only skeleton renderer: face`
   with
   `    // C++ painter (retired unused); with _paintElem() (fan mode) the skeleton painters: face`

Then re-run `design_system_tokens_check` and `design_system_lint_test` through the lock. These are comment edits, so no build is needed.

The feet and hands comments (`:382–383`, `:392`) are true as written, because `_paintElem` draws neither, so they stay.

## Not blocking
- **NOTES are stale on `src/Analysis/tests/CMakeLists.txt`.** NOTES §3 and §4 say it was left alone ("None of lane 1's five files was touched"), but the worktree edits it. It is comments only, with an mtime of 17:20:57, after NOTES at 17:20:28. The studio PC's 9-file patch therefore did not include it.
  - Correct NOTES §1, §3 and §4 to say who edited it and when.
  - The coordinator should commit it after lane 1, or rebase it onto lane 1's version, because lane 1 holds that file.
  - The new text there is accurate.
- **Parity test header (optional).** "That QML is the one copy the app draws" and "the overlay's one bone table" overstate in the same way. Suggested wording: "the one file the app draws from" and "kBlueprintBones (and _paintElem's repeat of its body bones)".
- **Cabin.** Exit 12. The 11 + 2 failures match the patch-free baseline run (`studio PC-wt-5b-20261124-160500`) test for test, as NOTES says. I accept C2.
