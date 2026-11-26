# B1.13 · `recorded_products.h` leaves Analysis for `Vocabulary/Analysis` (notes)

Lane 2, `build/wt-5b` at `12992896` (origin/main, ff: already up to date). Route (a), ruled by R10 (the Solution Architect, 24 Nov 2026). 1 Mac build budgeted, 1 used (app + tests trees); 1 studio PC run.

## Check first (on 12992896, before any edit)
1. Allowlist line 27 = `Analysis/swing_reanalyzer.cpp | A1 | 1 | … # platform refactor (#224)` (already re-tagged from `# step 1` by B1.T); `swing_reanalyzer.cpp:20` `#include "recorded_products.h"`.
2. File: 613 lines, no `.cpp`, file access grep 0, includes only `Vocabulary/Analysis/swing_analysis.h`, `uncertainty_json.h`, Qt JSON. No `QVariant` (A6 safe). Longest function 223 lines (A7 safe in the Vocabulary ring).
3. Includers (`count_includers.py --root build/wt-5b Analysis/recorded_products`): 4 files, 4 includes: `swing_reanalyzer.cpp`, `Export/swing_doc_analysis.cpp`, `Export/tests/{analysis_block_roundtrip,swing_doc}_test.cpp`.
4. Re-analysis uses `shaftTrackFromAnalysisJson` and `segmentationFromAnalysisJson` (`swing_reanalyzer.cpp:206,212`) on `ls.analysisIn`, the `analysis` object the Recording loader returned (`:65`, `lr.analysisIn`): JSON already loaded by the store, so no I/O moves.

## What changed
- Map `tools/refactor/maps/B1.13-recorded-products.map` (one line). Tool: dry run 0 errors / 0 can't place; run with `--over-work` (the only dirt was the untracked map): 1 file moved, 6 include lines in 5 files, 3 path mentions (`CMakeLists.txt:2892`, `swing_doc_analysis.cpp:29` comment, `docs/developer/analysis_pipeline_developer_guide.md:490`).
- Hand edits (the brief's): `architecture_lint_test.cpp` `fileRings()` entry `Analysis/recorded_products` deleted (was :244; `dirRings()` `Vocabulary/` now places it) and the table comment at :230 corrected; `tools/refactor/context_check.py:65` `'Analysis/recorded_products': HUB` deleted (`DIRS` `Vocabulary/Analysis/`: DIA places it, as R13 rules); `architecture_allowlist.txt` line 27 **deleted**.
- IM (`governance/artefacts/information_model.md`, by grep): RING-VOCABULARY's directory column names `recorded_products` (:417); RING-ADAPTERS drops it from both columns (:421); the spanning table's `Analysis` row loses its adapter part for it (:432); new footnote `[^sa24-r10]`; changelog row **1.10.6**; Version header 1.10.5 → 1.10.6. Walkthrough deck: its slide text never names `recorded_products` or a serialiser's ring (unzip + grep of `ppt/slides/*.xml`), so it is unchanged by this edit.

## Tests and accept
- `--verify 12992896` on the pure move: **PASSED** (1 rename, 7 files). After the hand edits, with `--allow-prose information_model.md`: the lint/context table edits listed for the reviewer; FAILED only on the allowlist, 138 → 137 entries, line 27 deleted: the brief's required deletion, which the tool by design never allows in a move. Re-run on the final tree (after the plants were reverted): the same two lines only.
- Accept: A1 line for `swing_reanalyzer.cpp` 1 → 0 by deletion; allowlist entries **138 → 137**; `# platform refactor (#224)` entry lines **36 → 35** (header comments excluded; P2's 36 → 32 path); no stated total elsewhere changes (grep of the allowlist header, the lint and `docs/developer/`: none; the lint now prints A1 "31 allowed in 12 files"); `context_check.py --list c`: the `swing_reanalyzer.cpp:20` row gone (c 7 → 6, crossings 973 → 971); no `# step 1` tag on an A1 line (grep: only the header comment at :8). Includers after: 4 files, 4 includes, all relative, 0 unresolved.
- Table checks (planted then reverted, byte-identical from a backup): an outward include in the moved header made `architecture_lint_test` fail with `Vocabulary → Application` (A1), so the file reads Vocabulary; a hub include made `context_check.py` report `[Diagnostics -> Domain+Application]`, so it reads Diagnostics.
- Mac (through the lock): app and tests trees built, 0 errors. `ctest -R 'reanaly|recorded_products|swing_doc|reuse_gate|analysis_block|…'`: 9/9 passed (analysis_block_roundtrip, reuse_gate, context_tree, reanalysis_controller, swing_doc, im_excerpt_selftest, design_system_lint, architecture_lint, move_files_selftest). No test is named `recorded_products`.
- Parity: `parity_baseline.py b113-recorded-products` vs `b1-base`: **0 differences** (diagnostics.json, athlete.json; 8 sessions); run kept at `the share (copies deleted).
- Cabin (exit 12; `build/wt-5b/build/studio PC-runs/studio PC-wt-5b-20261124-213207.txt`, logs `the share 21 min, patch 13 files incl. the delete and the add): pass 1 326/338, pass 2 41/43, stores unchanged, checkout restored. **No new failure:** the full FAIL lines (check text, temp paths masked) are identical to this lane's 19:31 run's (`diff` empty): 14 suites (13 standing + `session_list_test` L7a "could not be moved to the trash", intermittent: also in the 19:31 and lane 1's 20:14 runs, absent at 20:35).

## Real stores
None reachable: no behaviour changed. Parity copied the library read-only and deleted its copies; no app launch; no QSettings or keychain touched.

## Calls
- C1 MOVES.md's "one lane" rule: the coordinator ran B1.14 in lane 1 at the same time. The move touches 11 files, none in B1.14's area except the allowlist (one line deleted, line 27) and the lint (one table line, one comment). Coordinator: rebase order matters only there.
- C2 IM Version header bumped to 1.10.6; lane 1 may bump too: `im_changelog_merge.py` resolves the row clash, the header line by hand.
- C3 The lint comment at :230 reworded (it said rebuilding products is "the store's"), as the ruling makes it untrue.

## For the coordinator: #224's closing sentence
"`Analysis/swing_reanalyzer.cpp`'s A1 line (Stage B, B1.13) is cleared: `recorded_products.h` moved to `src/Vocabulary/Analysis/` by R10 (the Solution Architect, 24 Nov 2026), so re-analysis includes only inward and the allowlist line is deleted (138 → 137)."

## Paths
`CMakeLists.txt`, `docs/developer/analysis_pipeline_developer_guide.md`, `governance/artefacts/information_model.md`, `src/Analysis/swing_reanalyzer.cpp`, `src/Analysis/recorded_products.h` → `src/Vocabulary/Analysis/recorded_products.h`, `src/Export/swing_doc_analysis.cpp`, `src/Export/tests/analysis_block_roundtrip_test.cpp`, `src/Export/tests/swing_doc_test.cpp`, `src/Gui/tests/architecture_allowlist.txt`, `src/Gui/tests/architecture_lint_test.cpp`, `tools/refactor/context_check.py`, new `tools/refactor/maps/B1.13-recorded-products.map`.

## EDGE
None found. (Bare `recorded_products.h` mentions in comments, e.g. `analysis_pipeline.cpp:839`, `shot_analysis_job.h:197`, carry no path and stay.)
