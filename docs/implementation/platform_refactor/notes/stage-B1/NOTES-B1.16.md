# B1.16 · `src/Export` is renamed `src/Stores` (26 Nov 2026)

Lane 1 (`build/wt-37c`), unattended overnight. Before the reset wt-37c was clean, detached at origin/main `4f3a843e`
(B1.15b), no stash. I fetched (origin/main still `4f3a843e`), ran `reset --hard origin/main` and `clean -fd`, and
regenerated the excerpts (`make_im_excerpts.sh build/wt-37c`). **One lane:** wt-stack is clean at `ebd8c952`. wt-5b
(`027f9cbf`) still has its 4 modified files and 1 new one (`governance/controls/{build,test}.md`,
`src/Gui/tests/{CMakeLists.txt,session_setup_lint_test.cpp,qml_rule_allowlist.txt}`). Its `CMakeLists.txt` hunks are at
`qml_reactivity_test` and `session_setup_lint_test` (main's :521 and :533); this move's last line in that file is :498,
and it touches neither `session_setup_lint_test.cpp` nor the QML allowlist. No overlap. I left wt-5b alone.

## 0. The ruling
`docs/implementation/platform_b1_placements.md` R6, **Ruled:** (a), `src/Stores` (the Solution Architect, 24 Nov 2026).
The brief's "HARD: stop and propose" does not apply: the name is ruled. **IM version:** main is at 1.12.12 (B1.15b), not
1.12.11 as my task said, so this change is **1.12.13**.

## 1. Check first (on 4f3a843e)
- `count_includers.py Export/`: **59 files + 12 under `tests/` = 71 moved** (the brief's "60" counted the `tests` folder
  as one). **185 includer files, 544 includes** (383 relative, 129 bare, 32 dir-qualified). The brief's 191/545 were on
  `bdd04541`.
- Include directories: `src/Export` is in no global list. It is in 22 target-level ones (the tool re-pathed them, §2):
  5 in the root `CMakeLists.txt` (offline tools), `cmake/swingstore` (PUBLIC), `src/Analysis/tests` 3,
  `src/Gui/CMakeLists.txt` 2, `src/Gui/tests` 7, `report_model_tests.cmake` 1, `src/Pose/tests` 1. **None was removed**
  (the brief doesn't ask, and generated moc or QML registration code may include a moved header by bare name).
- The lint's keyed places (before): `dirRings()` :189 `Export/` Adapters; `a3Allowed` :292 `Export/swing_paths`; A4's
  owners :309-325 (`Export/session_files`, `Export/athlete_files`); `countedStoreDoor()` :343-344; `a5Exempt()` :350
  (`rel.startsWith("Export/")`); `isRepositoryHeader()` :360-371 (13 `Export/` stems); the fixtures :718-719, :737-739,
  :755, :774, :788, :797, :803, :810, :813-817. **A1's rule is no longer keyed on `Export`**: since step 2 (S1-g) it is
  "an Adapters-ring file outside `Gui`, or a `Vocabulary/Recording` file" (:544), so it takes `Stores/` from `dirRings()`.
- Allowlist: 4 entry lines keyed `Export/…` (:58 `session_list_reads.cpp` A5, :59 `session_index_summary.cpp` A5, :132
  `swing_zip_exporter.h` A6, and :54's text naming `Export/phase_grid_store.h`) plus the comment :23.
- Strings that are not paths, **unchanged**: the thread name `"Export.Zip"` and profile tags `"Export.Staging"`
  (`swing_zip_exporter.cpp:246,296,314,327`), every `Export…` class (`SwingExporter`, `ExportJob`, `ExportToneCodec`, …),
  every QML word ("Export", "Export session", …), no settings key, no namespace. Paths in code: `display_tone_test.cpp:309-314,336`
  (the test opens those files; hand edit, §2).
- Tools: 18 files under `tools/` include from `Export/` (the tool rewrites them). `tools/studio/studio-build.cmd` and
  **every packaging script (`packaging/`, all four platforms) name nothing under `src/Export`**: no packaging script changed.
- No other repo (`libppswing`, `libppcp`, `libwrist`, `libgspro`, `PinPointCapture`) names `src/Export` or
  `pp_define_swingstore`.

## 2. The map, the run and the hand edits
- Map `tools/refactor/maps/B1.16-export.map`: `src/Export/ -> src/Stores/`, and `keep:` IM 2076, 2082, 2092 (change-log
  rows 1.9.52, 1.10.4, 1.12.1; dated records).
- Dry runs on 4f3a843e (`B1.16-dryrun.txt`, then `B1.16-dryrun-final.txt` with the keeps). TOTALS: move 71 files; 410
  include lines in 141 files; 645 path mentions (build 400, lint 53, allowlist 5, context 1, tools 2, docs 112 less 3
  kept, comments 75); 22 include dirs re-pathed; 13 can't place; 0 errors. Real run `--over-work` (`B1.16-run-real.txt`):
  the same report; 71 moved, 234 files rewritten.
- Hand edits after the run:
  - `src/Stores/tests/display_tone_test.cpp:309,310,312,314,336`: `"Export/…"` → `"Stores/…"` (`--allow-code-path`).
  - Lint: the `dirRings()` comment :189 ("the stores and file doors (R6)"); the header comment :37 and the fixture's
    message :739 name `Stores`. :85 left: it is history ("four directories straddled rings (03 §2) — … Export").
  - `src/Gui/diagnostics/session_diagnostics_model.cpp:1966`, a comment split over two lines (`(Export/` / `session_diagnostics_files.h`).
  - `README.md:86` (the src/ table's row: `Stores/`, "The stores: the swing, session and athlete files, and the video and
    zip exports"); `docs/developer/testing_developer_guide.md:387` (`${SRC}/Stores/tests/swing_doc_test.cpp`);
    `docs/design/rig_design.md:565` (the Rig's planned store, `src/Stores/rig_store.*`, as :102 now says);
    `docs/developer/swinglab_developer_guide.md:217` ("the Export ring" → "the stores").
  - Can't place, left as history: `shaft_fusion_design.md:90` (names `swing_doc.cpp`, which no longer exists),
    `shot_analyzer_design.md:252` (absolute `/home/markl/…` paths), IM :2098 (change-log row 1.12.7),
    `ball_detector_design.md:447` and `rig_design.md:801` (a dated census).
  - The IM (§5).
- `src/Export/`: 0 files.

## 3. Verify and configure
`move_files.py … --verify HEAD --allow-prose README.md --allow-prose governance/artefacts/information_model.md
--allow-prose docs/developer/testing_developer_guide.md --allow-prose docs/developer/swinglab_developer_guide.md
--allow-prose docs/design/rig_design.md --allow-code-path src/Stores/tests/display_tone_test.cpp:309,310,312,314,336
--configure <scratch> --seed build/wt-37c/build --cmake-arg -DPINPOINT_BUILD_TOOLS=ON --cmake-arg -DPINPOINT_BUILD_LM_REPAIR=ON`
through the lock (`B1.16-verify.txt`): renames 71, 287 files, listed 15, configure app and tests **PASSED**. The first
pass failed on 4 other lines, all mine: new IM prose that spelled `src/Export` ("the map still rewrites"). I reworded them to
"the `Export` folder", and the re-verify gave **other lines 0, VERIFY PASSED** (configure unchanged by a prose edit; the
final verify and configure, §7). The lint's comment :37, its fixture message :739 and the
`session_diagnostics_model.cpp:1966` comment pass as the map's image; the `dirRings()` comment is listed as a table-line hand edit.
- `count_includers.py Stores/`: **185 files, 544 includes** (= `Export/` before; 408 relative, 104 bare (siblings in the
  folder), 32 dir-qualified); `Export/` after: 0. `artefact_paths_check.py`: 0 stale (3,583 files). `context_check.py
  --list c`: exit 0. `src/Export` left in `src`, `tools`, `cmake`, `tests`, `packaging`, `.github`: only B1.5's map and the
  tool README's example of it (history).

## 3b. The Mac half
- **Builds (2, the budget)**, each under `b116/dog.sh` (`ps -o etime=`, 2 h), through the lock, `-j 3` (`b116/build2.sh`):
  tests tree exit 0, 0 compile errors (`b116-build-tests.log`); app tree configured with `PINPOINT_BUILD_TOOLS=ON`,
  `PINPOINT_BUILD_LM_REPAIR=ON` ("lm_repair enabled"), built exit 0, 0 errors (`b116-build-app.log`): `swinglab_run` and
  `lm_repair` built. App tree set back to tools off after the review (`b116-configure-tools-off.log`; CMakeCache TOOLS and LM_REPAIR OFF).
- **ctest** (`b116-ctest.log`, -LE qml_ui): the brief's regex plus every suite under the moved `tests/` folder
  (`display_tone`, `swing_paths`, `recording_repository`, `session_repository`, `analysis_block_roundtrip`) and
  `lint|artefact_paths|design_system_tokens|context_tree|move_files|im_excerpt|im_changelog|work_ons_controller|report_meta|housekeeping`:
  **32/32 passed**, among them the four lints (architecture, design_system, store_isolation, session_setup),
  `artefact_paths_check`, `swing_doc_test`, `swing_store_test`, `session_archiver_test`, `session_list_test`,
  `athlete_repository_test`, `rule_data_test`, `display_tone_test`.
- **Plants** (`b116/plants.sh`, `b116-plants.log`, `b116/plants-lint.out`; one lint run, then removed and the lint
  re-run: exit 0):
  - P1 `Stores/zz_b116_p1.cpp` including a Gui header: A1 "an adapter outside Gui reaches the UI" (`dirRings()`'s
    `Stores/` entry is Adapters, and A1's rule takes it);
  - P2 `Gui/zz_b116_p2.cpp` including `Stores/session_store.h`, `swing_doc.h`, `phase_grid_store.h`: 3 × A5 "a
    repository outside its owner" (`isRepositoryHeader()`'s re-keyed stems);
  - P3 one more `QFile` line in `Stores/session_list_reads.cpp`: "2 findings, 1 allowed" (`countedStoreDoor()`'s stem;
    a stale stem would have exempted the file);
  - P4 `Export/zz_b116_p4.cpp` with a `QFile`: A5 "file access outside the repositories" and "every C++ file's directory
    has a ring" FAILED (the old key is dead: `a5Exempt()` no longer exempts `Export/`, and it has no ring).
  - `a5Exempt()`'s new prefix, `a3Allowed`'s `Stores/swing_paths` and A4's owners are proven by the clean lint itself:
    every store's file access, folder-name parse and file name would otherwise be a finding.
- **Parity:** `parity_baseline.py b116-stores` exit 0 (`b116-parity.log`); `parity_diff.py b1-base-2 b116-stores`:
  **0 differences** (`b116-parity-diff.log`).
- **Real stores:** none in reach (paths only; no store's path on disk changes); store_isolation_lint passed; parity ran on share copies.
- **Packaging scripts touched: none** (`git diff --name-only -- packaging tools/studio`: empty). For the next installer
  build: nothing to re-check in packaging; none of the root `CMakeLists.txt`'s changed lines sits in a Windows-only or
  Linux-only block (they are in the shared source lists, the FFmpeg block both the Mac and the studio PC take, and the offline
  tools' targets); the studio PC run builds the Windows side. No Linux build was run tonight.

## 4. Cabin run
`b116/cabin_wrap.sh` (B1.15b's wrapper, 2 h ceiling; log `b116-studio PC.log`), launched at 05:42 once the hand edits were
in and before any plant (the patch is taken at launch). Run `studio PC-wt-37c-20261126-054205` (summary
`build/wt-37c/build/studio PC-runs/studio PC-wt-37c-20261126-054205.txt`): exit 12. Cabin HEAD 4f3a843e + patch 7aa9f6e1… (359
files, the renames as delete + add). Build exit 0 (jom -j16, 17 min 43 s); stores unchanged; checkout restored clean; wall
time 35 min.
- **Pass 1: 345/347**, 1 not run. `session_list_test` L7a (a session "could not" be moved to the Windows trash): **#250**,
  open, intermittent on Windows: the same line failed in B1.6's studio PC run and in 18 earlier runs on main without a move; it passes on the Mac (§3b).
  `paced_connect_queue_test` T4 (77 ms): **#263**, open, a timing check (B1.6's run had the same 77 ms).
- **Pass 2: 41/43**, the standing two: `tst_charts_panel` test_15 (#231) and `tst_session_report` page count (#253).
- Every suite over the moved files passed there, the lints and `display_tone_test` (which opens the moved files by path) among them.
- **Accept: no new failure** against the last studio PC runs on main: B1.15b's (the same pass-2 pair) and B1.6's (the same four).

## 5. IM 1.12.13 (C9 and C4, R6)
The version line; §1.4's A1 and A5 rows (`Stores`); §2.2's "The name" (the store is in `src/Stores`, was "moves out of
`src/Export` in step 7"); §4.1's RING-ADAPTERS row, both cells; §5.4's housekeeping (purge carried out in `Stores`);
§11's rows: the rename (done, past tense), A1's widening (its fixture and "none in `Stores`"), P11 (A5 reads every file
but `Stores`), housekeeping, the `Shot` entity ("the stores'"); the tool's path rewrites elsewhere. The spanning table
has had no `Export` row since B1.5 (1.12.1), so nothing to delete there. New footnote `[^sa24-r6]` and change-log row
1.12.13. Prose naming the old folder uses the bare name ("the `Export` folder"), so `--verify` sees no stale anchored path.
DEC-146's register row keeps `Export` (a decision as recorded).
**Deck:** `docs/developer/information_model_walkthrough.pptx` names `src/Export` twice ("Eight directories span rings
(Export, …)" and "renaming src/Export" in the remaining work): it needs the Product Owner's refresh.

## 6. Review (AG-21)
Fresh reviewer agent (not a fork), given the diff and these notes: `REVIEW-B1.16.md`, **SIGNED OFF**, no blocking
finding. It confirmed the work tree equals `B1.16-diff.patch`, re-ran `--verify` (71 renames, 0 other lines), checked the
allowlist (176 lines before and after, re-keyed), the includers (185/544 at `Stores/`) and re-ran 9 suites (9/9). Fixed
in the item (wording and evidence only): the commit message's two over-claims (the exporters are several files; three
lint tables rest on the clean lint, not a plant); the passing verify log saved (`B1.16-verify-final.txt`); these notes'
dangling §8 and the tools-off claim (the app tree is now set back). The rest are edges (§8).

## 7. Final verify and configure
Main still `4f3a843e` at commit time (fetched), so no rebase; IM 1.12.13 still free. `--verify --configure` on the final
tree (`B1.16-verify-final.txt`): renames 71, 287 files, listed 15, **other lines 0, VERIFY PASSED, CONFIGURE PASSED**.

## 8. EDGE (filed as one issue)
1. `docs/reference/swing_json_schema.md` names 14 `src/Export/` paths and `docs/user/killer-features.md:304` one: the move
   tool doesn't scan `docs/reference/` or `docs/user/` (B1.5's old paths are there too).
2. `governance/artefacts/lifecycle_model.md` got 17 path-only rewrites from the tool's governance scan, with no change-log
   row of its own; its citations are pinned to older commits. For the Solution Architect.
3. Comments that call the folder "Export" in words (not paths): `swing_reanalyzer.h:45` ("the Export ring"),
   `swing_folder_reanalysis.h:23`, `housekeeping.h:27,73`, `main.cpp:468` and the others the review lists.
4. Stale before this item, left as history: `shaft_fusion_design.md:90` (`swing_doc.cpp` no longer exists),
   `shot_analyzer_design.md:252` (absolute paths), `ball_detector_design.md:447`, `rig_design.md:801`.
5. The walkthrough deck names `src/Export` twice: the Product Owner's refresh.

## 9. Committed and pushed
**`f38e2339`** on main (fast-forward from 4f3a843e, GIT-2; main had not moved, so no rebase and no re-run). The staged diff
was byte-identical to the reviewed `B1.16-diff.patch`: 288 files (71 R, 1 A, 216 M), staged with `git add -A .` (the
`':!build'` pathspec is refused because `build/` is ignored). Message `b116/commit-msg.txt`, re-read against
GIT-8/9/10/13 (area `Platform:`, which the log uses; no trailer). The edges in §8 are filed as **#307** (`edge case`,
`documentation`, `priority: low`). wt-37c is clean at f38e2339; the app tree is back to tools off. The parity run tree
`scratch/parity/b116-stores` is kept, as the earlier items' were.
- **For the coordinator:** (1) the IM is now 1.12.13 (the task said main was at 1.12.11; it was at 1.12.12). (2) No
  packaging script was touched: nothing to re-check for the next installer. (3) The deck needs the Product Owner's refresh.
  (4) wt-5b still carries its uncommitted work on 027f9cbf, now two moves behind main.
