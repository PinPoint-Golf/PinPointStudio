# B1.7 · the Recording contract's capture-time types to Vocabulary/Recording; two shared headers to Vocabulary/Common (26 Nov 2026)

Lane `build/wt-37c`, unattended overnight. Status before reset: clean, HEAD = origin/main `008b8eb4` (B1.6), nothing off
main, no stash. Reset/clean to origin/main `008b8eb4`. IM excerpt regenerated (`make_im_excerpts.sh build/wt-37c`).

## 1. Check first (on 008b8eb4)

- `recording.h:45` includes `../Analysis/swing_analysis.h`; it takes from it only `ImuSegmentBinding` (`:159`),
  `BallTrack2D` (`:126`), `BallBaselineRef` (`:128`); `BallSample2D` comes with `BallTrack2D`. Nothing else in
  `Vocabulary/Recording/*.h` names them.
- What the four structs need (`swing_analysis.h:116-157, 514-549, 625-648`): `SourceId`/`kInvalidSourceId`
  (`Buffer/types.h`), `SegmentRole` (`segment_role.h`), `TrunkCalibProvenance` (`trunk_calib_provenance.h`), Qt value
  types (`QQuaternion`, `QString`, `QPointF`, `QByteArray`, `QRectF`), `<cstdint>`, `<vector>`. No free function of
  `swing_analysis.h` belongs to them (the one inline is `BallBaselineRef::isValid`, a member). Nothing of Diagnostics'.
  `segmentRoleForSlot` (a legacy reader's rule) stays in `swing_analysis.h`. No type outside the plan must move.
- `count_includers.py`: `segment_role` 2, `trunk_calib_provenance` 5, `swing_analysis` 98 (= the brief).
- `segment_role.h` includes only `<QString>`; `trunk_calib_provenance.h` only `<QJsonObject>`, `<QString>`.
- `context_check.py --list c`: 5 rows (B1.1 is in), two of them the brief's (`trunk_stand_tall.h:45`, `recording.h:45`).

## 2. The map, the run and the hand split

- Map `tools/refactor/maps/B1.7-shared.map`: the two headers `-> src/Vocabulary/Common/`. Dry run (`B1.7-dryrun.txt`):
  move 2; 7 include lines in 6 files; 4 path mentions (CMakeLists 731, 2806; `tools/trunk/trunk_hw.py:80`;
  `docs/design/rig_design.md:807`); 0 include dirs; 0 can't place; 0 errors; context `Vocabulary/Common/` DIA -> S by
  its folder (no table edit). Real run `--over-work` (`B1.7-run-real.txt`): the same; 2 moved, 9 rewritten.
- **Call: the header's name is `capture_time_types.h`, not the brief's example `capture_facts.h`:** `recording.h` already
  has a `struct CaptureFacts` (session type, shot source, impact instant), which the new header does not hold.
- Hand split (`b17/split.py`, exact line cut): the three blocks (with their comments) cut byte for byte into
  `src/Vocabulary/Recording/capture_time_types.h` (namespace `pinpoint::analysis`, order kept: binding, sample, track,
  baseline), which includes Qt, `../../Buffer/types.h`, `../Common/segment_role.h`, `../Common/trunk_calib_provenance.h`.
  `swing_analysis.h` gains `#include "../Recording/capture_time_types.h"` after the two Common includes (and keeps
  them: `segmentRoleForSlot`, `BindingRecord`). `recording.h:45` -> `#include "capture_time_types.h"`. Root
  `CMakeLists.txt:2940` lists the new header beside `recording.h` and `analysis_context.h`.
- Proof the text moved unedited (Python byte compare against `git show HEAD:`): each of the three blocks occurs exactly
  once, byte-identical, in the new header; `swing_analysis.h` against the base minus the blocks differs only in its
  three include lines.
- Docs: IM 1.12.6 (header; RING-VOCABULARY Context: Shared `Vocabulary/Common` gains the segment roles and the trunk
  calibration provenance, Acquisition's `Vocabulary/Recording` its capture-time types; Directories: `Vocabulary/Analysis`
  drops "the segment roles", `Vocabulary/Recording` gains `capture_time_types`, `Vocabulary/Common` the two headers
  `[^sa24-q27]`; change-log row 1.12.6). `shot_analyzer_developer_guide.md:808` and `swinglab_developer_guide.md:678`
  named `swing_analysis.h` as `ImuSegmentBinding`'s home: re-pointed. The deck: no mention of any of these (unzipped
  slide text searched): no deck change. README: no mention.
- Left: `rig_design.md` A5-4 ("What `src/Vocabulary/Recording` includes ... `swing_analysis.h`") is the dated include
  inventory at `a5089848` (B1.5 left the same table's dated lines); `[^sa24-q27]`'s "moves ... at Stage B" is the
  ruling's words.

## 3. Verify and configure

- **The tool's part, alone** (`B1.7-verify-tool-only.txt`): the split taken out in place (the tool-only tree staged,
  then the split restored on top; WIP saved first, `B1.7-wip-on-008b8eb4.patch`), `--verify HEAD` with `--allow-prose`
  on the IM and the two developer guides only: renames 2, 14 files, **other lines 0, VERIFY PASSED** (exit 3: no
  configure on that tree; the full tree's configure is below). The split alone is `B1.7-hand-edits.patch` (4 files).
- **The whole tree** (`B1.7-verify.txt`), with `--allow-prose` also on `swing_analysis.h` and `recording.h`, `--configure`
  (tools and lm_repair on): **VERIFY FAILED, by design**, on exactly 4 lines, all the brief's hand split: the added
  `capture_time_types.h`, `CMakeLists.txt:2940`'s added list line, `swing_analysis.h`'s 13th include,
  `recording.h:45`'s new target. **CONFIGURE PASSED** (app, tests), "lm_repair enabled"; `ctest -N` 421, the same names as
  the lane's tree. **Call / for the coordinator:** the tool has no waiver for a file a brief adds by hand, so every hand
  split (B1.12 onward) will read VERIFY FAILED on its whole tree; the pair above (tool part PASSED, whole tree's other
  lines = the brief's) is how this item shows it.
- Moved text, `git diff --color=always --color-moved=zebra` over `swing_analysis.h` + the new header: 210 lines coloured
  as moved (105 out, 105 in), 45 plain: the new header's licence, comment, includes and namespace lines, and the one
  include in `swing_analysis.h`. No plain line inside a struct.
- Python checks: `artefact_paths_check` 0 stale (3,578 files); `make_design_system.py --check` current;
  `count_includers`: `Vocabulary/Common/segment_role` 3 (the 2 + the new header), `…/trunk_calib_provenance` 6 (5 + the
  new header), `Vocabulary/Analysis/segment_role` 0. `context_check.py` full report against HEAD's (`git archive`
  copy): (c) 5 -> 3 (the `recording.h:45` and `trunk_stand_tall.h:45` rows gone), (b) 68 -> 66 (two deferrals now
  Shared), shared kernel 153 -> 162, Recording 46 -> 47. `--list c` exit 0. `grep 'Analysis/' src/Vocabulary/Recording/*.h`
  in includes: 0.
- The studio PC patch (trial `make-wip-patch.sh`, sha 3666dac7…, base 008b8eb4): 19 files; the new header and the map are
  "new file" hunks; the two moves are D + A.

## 4. Independent review (AG-21)

A fresh reviewer agent (not a fork), given the brief, COMMON, the excerpt and the diff, not these notes:
`REVIEW-B1.7.md`, **SIGNED OFF**, conditional on the Mac results, studio PC run and parity below (its finding 1). It proved
the four structs byte-identical, the new header self-contained, that none of the seven files that no longer reach
`swing_analysis.h` through `recording.h` uses a type from it, the context check 5 -> 3, no allowlist change, and the IM
edits within Q27 and the plan's Stage B text (PR-9); its ctest run (26/26, `architecture_lint_test` among them) passed.
- EDGE 2 (the map's comment named `capture_facts.h`): fixed (a comment; before the studio PC run).
- EDGE 3 (`rig_design.md` A5-4 and §6 rule 1, dated `a5089848`, still say `recording.h` includes `swing_analysis.h`,
  beside A5-8 rewritten by the tool): left, for §6's re-run at Stage B's close.

## 5. The Mac half (on 008b8eb4 + the move)

Every heavy job through `tools/method/one_at_a_time.sh`, `-j 3`, each under `b17/dog.sh` (`ps -o etime=`, parsed).
- Builds (2, the budget): tests tree exit 0, 0 errors (`b17-build-tests.log`); app tree configured with
  `PINPOINT_BUILD_TOOLS=ON`, `PINPOINT_BUILD_LM_REPAIR=ON` ("lm_repair enabled", `b17-configure-tools.log`) and built,
  exit 0, 0 errors (`b17-build-app.log`): `swinglab_run`, `lm_repair`, `swing_window_parity_test` and
  `backfill_analysis_context` linked. `swing_window_parity_test` (not a ctest; run as B1.5/B1.6 did): PASS, 0 check
  failures (`b17-swing-window-parity.log`). App tree set back to tools off (`b17-configure-tools-off.log`).
- Named ctest (`b17-ctest.log`): the brief's `recording|swing_doc|imu_|trunk|ball_|binding` plus
  `lint|artefact_paths|design_system_tokens|gate_catalogue|ppcp_annotation|move_files|im_excerpt|context_tree|
  namespace_collision|im_changelog_merge|identity|frame_decode|hm_`: **47/47 passed**, the four lints among them
  (architecture, design_system, store_isolation, session_setup), with `recording_contract_test`,
  `recording_repository_test`, `recording_store_test`, `swing_doc_test`, `trunk_stand_tall_test`, `trunk_e2e_test`,
  `imu_role_map_test`, `hm_binding_recon_test`, `ball_baseline_io_test`, `ball_anchor_test`.
- **Parity:** `parity_baseline.py b17-shared` exit 0, 8 sessions counted (`b17-parity.log`);
  `parity_diff.py b1-base-2 b17-shared`: **0 differences** (`b17-parity-diff.log`).
- Real stores: none reachable (include paths and a header split only); parity on share copies.

## 6. Cabin run

`b17/cabin_wrap.sh` (B1.6's wrapper; kills the Mac side only 2 h 10 min after the launch, so `cabin_run.sh`'s own 2 h
exit wins). Run `studio PC-wt-37c-20261126-010549` (summary `build/wt-37c/build/studio PC-runs/studio PC-wt-37c-20261126-010549.txt`,
run dir kept on the share): exit 12. Cabin HEAD 008b8eb4 + patch e54af0a6… (19 files, after the map-comment fix); build
exit 0 (13 min 50 s); 393 tests registered; stores unchanged; checkout restored clean.
- **Pass 1: 346/347**, 1 not run. `session_list_test` L7a ("corrected and trashed: the session … could not" be moved
  to the trash): **#250** (open), the same line as B1.6's run and earlier runs on main without either move.
- **Pass 2: 41/43**, the standing two: `tst_charts_panel` test_15 (#231), `tst_session_report` page count (#253).
- Accept: no new failure against the last studio PC runs on main.

## 7. Commit

Main still 008b8eb4 (IM 1.12.5 there, so 1.12.6 is free); no rebase needed. Message `B1.7-commit-msg.txt`.
**`1e3269a8`** on main (fast-forward from 008b8eb4; 17 files: 2 R, 2 A, 13 M; staged `git add -A .`), pushed under GIT-2
after the independent sign-off, the Mac half and the studio PC run. wt-37c clean; app tree back to tools off.
- For the coordinator: (1) `move_files.py --verify` has no waiver for a file a brief adds by hand, so hand splits (B1.12
  on) read VERIFY FAILED on the whole tree: show the tool-only pass plus the whole tree's other lines, as §3 does;
  (2) the header is `capture_time_types.h`, not the brief's example name (a `CaptureFacts` struct already exists);
  (3) EDGE: `rig_design.md`'s dated A5-4 / §6 rule 1 for the Stage B close re-run.
