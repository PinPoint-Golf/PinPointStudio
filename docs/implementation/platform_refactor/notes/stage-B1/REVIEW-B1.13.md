# B1.13 · independent review: `recorded_products.h` → `src/Vocabulary/Analysis/`

**Verdict: SIGN OFF.** No blocking finding. Two optional wording fixes and one merge instruction for the coordinator are below.

Reviewed: `build/wt-5b` at `12992896`, uncommitted (`git diff -M HEAD`, 11 files plus the untracked map), against R10 and R13 (`docs/implementation/platform_b1_placements.md`), `BRIEF-B1.13.md` and `NOTES-B1.13.md`.

## 1. Built what R10 ruled, nothing more: yes
- `git diff -M`: `src/Analysis/recorded_products.h → src/Vocabulary/Analysis/recorded_products.h`, 99% similar. The only changed lines are its two includes, now same-folder (`swing_analysis.h`, `uncertainty_json.h`). The body is unchanged.
- Includers re-pathed: `swing_reanalyzer.cpp:20`, `swing_doc_analysis.cpp:53` (and its table comment at :29), `analysis_block_roundtrip_test.cpp:43`, `swing_doc_test.cpp:16`. CMake: `CMakeLists.txt:2892` is the only source-list entry anywhere (I grepped every `CMakeLists.txt` and `*.cmake`). I searched `src`, `tools`, `tests`, `cmake` and `docs` for `Analysis/recorded_products` not preceded by `Vocabulary/`. The only hits are the map itself, R10's "Today" text, and the frozen swe-audit snapshots (`docs/implementation/swe-audit/data/*.json`, pinned). None is live. `src/Analysis/recorded_products.h` no longer exists.
- The map is one line with R10 cited in its header. The tool's three prose re-paths are correct (CMake, the comment in `swing_doc_analysis.cpp`, and `analysis_pipeline_developer_guide.md:490`).

## 2. The lint places it correctly: yes
- `fileRings()`: the `Analysis/recorded_products` entry is gone (was :244), so `dirRings()`'s `Vocabulary/` (:163) places the file in Vocabulary. No other `recorded_products` entry remains in the lint, `context_check.py` or the allowlist.
- `context_check.py`: the `HUB` entry was deleted (:65), so `DIRS` `'Vocabulary/Analysis/': DIA` (:39) places it in Diagnostics, as R13's table rules. `--list c` has no `swing_reanalyzer` or `recorded_products` row.
- Allowlist: `numstat` is 0 added, 1 deleted, and the deleted line is the old line 27 (`swing_reanalyzer.cpp | A1 | 1`), the only entry for that file. Counts: entries **138 → 137**, `(#224)`-tagged entry lines **36 → 35** (comment lines excluded), `# step 1` appears only in the header comment at :8. The `fileRings()` comment rewording at :230 is accurate under the ruling (NOTES C3).

## 3. The Vocabulary rule holds: yes
`recorded_products.h` includes `swing_analysis.h` and `uncertainty_json.h` (both Vocabulary), plus `<QJsonArray>`, `<QJsonObject>`, `<QJsonValue>`, `<algorithm>` and `<optional>`. All are allowed. There is no `QVariant` (A6) and no file access. `architecture_lint_test` passes, so A1, A6 and A7 hold in ring 1.

## 4. The IM edits: correct, two optional nits
- RING-VOCABULARY (:417) names `recorded_products` under `Vocabulary/Analysis/`, and its context column already tags `Vocabulary/Analysis` Diagnostics (R13). RING-ADAPTERS (:421) drops it from both columns. The `Analysis` spanning row (:432) loses "the recorded products' serialiser (adapter)". All three say what R10 says.
- Provenance is footnote-only (`[^sa24-r10]`, :1937), credited to the Solution Architect by role, with no name. There is one changelog row, **1.10.6** (:2028), after 1.10.5, and the header is 1.10.6. Consistent.
- *Optional nit (changelog row):* "no code changed" is not literally true, since five files' include lines changed. Suggest "no behaviour changed (include paths only)".
- *Observation, no fix:* the developer guide (:487–490) and R-PERS-4 say "one serialiser, beside its producer", and now give `Vocabulary/Analysis/recorded_products.h` as an example. The file now sits beside its value types and `uncertainty_json.h`, not its producers. R10 rules this, so it is consistent with the ruling. Flagged only in case the Solution Architect wants R-PERS-4's wording to say "beside its producer or its value types".

### Merging with lane 1 (B1.14, `build/wt-37c`): it does clash
B1.14 also bumps to **1.10.6** (its row "FILE-SHOT-07's recorded exception moves its reader"). Its IM hunks fall on the same three places:
1. **Changelog row** (both insert after line 2026/2027): a real conflict. Run `python3 tools/method/im_changelog_merge.py governance/artefacts/information_model.md` mid-rebase. It keeps the first side and renumbers the second to **1.10.7**. (NOTES C2 names the tool without its path; it is in `tools/method/`.)
2. **Version header (line 6):** both sides make the *identical* edit 1.10.5 → 1.10.6, so git merges it silently with **no conflict**. After step 1, set it by hand to **1.10.7** or it is stale. This is the trap.
3. **Footnotes:** B1.14 inserts `[^sa24-r11]` at the same spot (after `[^sa24-p3b-corridor]`, :1937). Expect a conflict the merge tool leaves unresolved. Keep both lines (`r10` then `r11`).

Allowlist and lint hunks do not overlap: this item touches allowlist line 27 and lint lines 230/244; B1.14 touches allowlist lines 30, 55 and 57 and lint line 405. After both items, re-count the allowlist. Expect 135 entries (138 − 1 − 2 by B1.14's numstat). Confirm the A1 totals the lint prints.

## 5. Parity and the studio PC: both meet the bar
- Parity: `the share are **byte-identical** to `b1-base/out/` (`cmp`), which is stronger than the tool's 0.
- Cabin `studio PC-wt-5b-20261124-213207` (patch 13 files, HEAD 12992896, build exit 0): pass 1 has 12 failures of 338, pass 2 has 2 of 43, and the stores are unchanged. The failing-test set is identical to this lane's 19:31 run. Against 20:35 it adds only `session_list_test` L7a (Windows trash move, intermittent, also seen in lane 1's 20:14 run). A header move cannot reach the Recycle Bin path. This is the standing set.
- Mac, run by me through the lock: `ctest -R 'recorded|reanaly|swing_doc|architecture_lint|design_system|move_files' -j 3` passed 6/6 (swing_doc, reanalysis_controller, design_system_lint, design_system_tokens_check, architecture_lint, move_files_selftest). `analysis_block_roundtrip_test` is outside that regex; NOTES reports it passed.
- The header's mtime (21:31:54) is later than the Mac build (21:27). That is the byte-identical restore after the plant test. The diff shows only the two include lines, and the studio PC built from the final tree.

## Disclosure
Outside the one permitted command, I also ran `python3 tools/refactor/context_check.py --src build/wt-5b/src [--list c]` (read-only, a few seconds) and greps/`cmp`. There were no builds, edits, commits or studio-PC jobs.
