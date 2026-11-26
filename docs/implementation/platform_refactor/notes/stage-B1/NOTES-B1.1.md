# NOTES B1.1 · A9's two deletions (lane 2, build/wt-5b, base f53626c4)

**Check first: both premises held.**
1. Outside the four overlay files, the only hits for `VideoOverlayPose|VideoOverlayBase|video_overlay_` were the comments the brief lists and `CMakeLists.txt:827, 2709–2710`. Nothing creates, holds, registers or subclasses either class. A wider `VideoOverlay` grep, including `docs/developer`, found nothing more.
2. `hm_session_worker.cpp` uses no `hm_frame.h` symbol. Its `QQuaternion` comes from `hm_session_types.h`, `qRadiansToDegrees` from `<QtMath>` and `std::atan2` from `<cmath>`, all included directly. It uses no `tuned::` or `QVector3D`.

**1. What changed**
- Deleted `src/Video/video_overlay_pose.{h,cpp}` (59 + 298 lines) and `src/Video/video_overlay_base.{h,cpp}` (50 + 23 lines).
- Root `CMakeLists.txt`: removed the `video_overlay_base` line from the core list (was :827, now between `video_preprocessor_base` and `raw_video_frame`) and the two `video_overlay_pose` lines from the OpenCV `target_sources` (was :2709–2710, now between `video_preprocessor_opencv.cpp` :2707 and `frame_throttle.h` :2708).
- `src/IMU/hm_session_worker.cpp:28`: deleted `#include "hm_frame.h"`. The comment above it (:25–27) describes `hm_sample_convert.h`, which now follows it directly, so it is unchanged.
- Comments only, each line-count neutral: `src/Gui/cameras/PpCameraFrame.qml:374, 382–383, 392, 422–423` now call `kBlueprintBones` and `paintBlueprint()` the one copy, with no C++ mirror. `src/Gui/theme/Theme.qml:414` now says the `pose*` tokens are read only by `paintBlueprint()`. `git diff` on both files shows only `//` lines.
- `src/Diagnostics/tests/anatomy_overlay_parity_test.cpp`: re-pointed the header (:3–14) and the comments at :34, :67 and :105. No check was removed or changed, so the test keeps its duty. Each constant now mirrors `PpCameraFrame.qml`, transcribed by hand because the test does not read the QML:
  - `kOverlayMinScore` 0.25 mirrors `var kMinScore = 0.25` in `paintBlueprint()` (:435) and `_blueprintScale()`.
  - The neck and pelvis midpoints and `neckScore = min` mirror `paintBlueprint()` :476–479 (`nX/nY`, `pX/pY`, `Math.min(gs(5), gs(6))`).
  - The 10 body-bone pairs mirror the first ten entries of `kBlueprintBones` (:375–380).
  - The documented divergences (no fingers, head is the nose only) mirror the hand entries (:393–397) and the face-dropping in `paintBlueprint()`.
- Lines of product code removed: **431** (430 overlay + 1 include), plus 3 CMake lines.

**2. Tests**
- Accept, context-map check (c) on wt-5b: rejected includes **9 → 7** (ruled tags). The two lines gone are `hm_session_worker.cpp:28` and `video_overlay_pose.h:24`.
- `make_design_system.py --check`, through the lock, before and after the Theme.qml edit: "tokens.json is current", rc 0, no regeneration.
- Mac builds, ctest and studio PC: see Results below.
- QML tests: `grep -l PpCameraFrame src/Gui/tests/qml/*.qml` finds none. `src/Gui/tests/setup/tst_setup_smoke.qml` loads session setup, which hosts PpCameraFrame, but names the component only in comments. The app build's QML compile and the studio PC's qml_ui pass are the check.

**3. Docs.** `docs/developer/` never names the overlay class, so there was nothing to fix there. Stale references left for the coordinator (not mine to edit, or historical plans):
- `src/Analysis/tests/CMakeLists.txt:1297` (lane 1's file) still says "what video_overlay_pose.cpp draws today". It should say PpCameraFrame.qml's `paintBlueprint()`.
- `docs/design/wholebody_pose_design.md:292` and `docs/design/swing_characteristics_impl_plan.md:23–24, 68, 276` are historical design and plan text that cite `video_overlay_pose.cpp` line numbers.
- `docs/implementation/code_quality_review.md:151` is an audit finding that cites `video_overlay_pose.cpp:43`. It is moot now.

**4. Paths changed:** `CMakeLists.txt`, `src/IMU/hm_session_worker.cpp`, `src/Video/video_overlay_{pose,base}.{h,cpp}` (deleted), `src/Gui/cameras/PpCameraFrame.qml`, `src/Gui/theme/Theme.qml`, `src/Diagnostics/tests/anatomy_overlay_parity_test.cpp`. None of lane 1's five files was touched.
- Real stores: none reachable. These are deletions of unused code and comment edits.
- IM: no change, since no IM id names these files. Design system: none, since the edits are comments and the token check is clean.
- EDGE: none.

**Calls**
- C1: I kept the QML comment edits line-count neutral, so nothing line-keyed moves. Neither allowlist keys these files by line.

## Results
- Mac builds (1 of the 2-build budget, through the lock, `-j 3`): `build/app` rc 0 and `build/tests` rc 0, with 0 errors (`build/wt-5b/build/b11-{app,tests}.log`). The app build compiled the QML.
- Mac ctest, through the lock: 19/19 passed. That covers `anatomy_overlay_parity_test`, every `hm_*` and `imu_*` test (15), `architecture_lint_test`, `design_system_lint_test` and `design_system_tokens_check`.
- Cabin, `studio PC-wt-5b-20261124-165911`: exit 12. The patch (9 files, sha 5578d43d) was applied on main's tip, c8d387ec. The build exited 0. Pass 1: 326 passed, 11 failed of 337. Pass 2: 41 passed, 2 failed of 43. **The failures are the studio PC's baseline, not this change.** They match test for test the earlier patch-free run of origin/main, `studio PC-wt-5b-20261124-160500` at bdd04541 (11 + 2). Lane 1's run `studio PC-wt-37c-20261124-163535` shows the same set.
  - Pass 1: tendencies, dtl_posture (0xc0000409), shot_commands, migrate_step2, parity_json, session_diagnostics, athlete_diagnostics, reduction_rule_golden, folder_lock, charts_delivery_presenter, athlete_repository.
  - Pass 2: qml_ui.tst_charts_panel, qml_ui.tst_session_report.
  - The logs are in `the share The summary is `build/wt-5b/build/studio PC-runs/studio PC-wt-5b-20261124-165911.txt`.
- C2: I count the studio PC as passing for B1.1 because nothing new fails. The 13 baseline failures exist on main without this patch, so they are for the coordinator and not this item.
