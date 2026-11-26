# Review: B1.19 step 6, the Diagnostics folders by layer (26 Nov 2026)

Independent reviewer (AG-21), on `build/wt-37c` (HEAD `f271b823`, uncommitted: 226 paths, 54 renames, 170 modified, 2 new).

## Verdict: CHANGES NEEDED

**One BLOCKING finding (E1).** The brief's test for the re-keyed `isRulesIoDir()` is "exactly the files the old keys covered, no more and no fewer". It is not met by one file: 102 → 103.
- The extra file changes no lint result today, but which message it takes is the Solution Architect's to rule, not a reviewer's or an implementer's.
- the standing block (#402) says: keep today's behaviour and log it.
- Either fix is cheap (below).

Everything else checks out: the move, the order check, the allowlist, the include paths, traceability and the docs. One EDGE finding (E2) is a wording fix to the IM, which AG-22 allows in the fix round.

## What I checked, and how

- **The move is the move.**
  - I applied the committed map (`tools/refactor/maps/B1.19-diagnostics.map`, 40 lines) to HEAD's file list myself and compared the result with `git diff -M`'s renames. All 54 match, no map line matches nothing, and no file is left in `packs/`, `norms/` or `measures/` (those folders are gone). 24 renames are R100. The other 30 differ only in include lines.
  - The map against `parked-B1.19/B1.19-diagnostics-optionB.map`: the file lines are identical. Only comments differ: the header (now the ruled order, the ruling and the seven re-placements) and the engine section's "stay" comment, which in the parked map wrongly listed `condition_reading.h` and `excludes_rule.h`, two of the re-placed files.
  - My own line classifier over `git diff -M HEAD -U0`, prose files left out: every changed code or build line is either an include line or a path re-spelled to the map's image, apart from 28 lines. Those 28 are exactly the hand edits the notes list:
    - the include-path deletion, `CMakeLists.txt:936`;
    - the three comments, `delivery_grid_numbers.h:30`, `rule_data_reads.cpp:20` and `rule_data_store.h:38`;
    - the nine-line ctest entry, `src/Gui/tests/CMakeLists.txt:741-749`;
    - `isRulesIoDir()`, `architecture_lint_test.cpp:532-541`;
    - one tool rewrite I checked by hand: `src/Core/tests/CMakeLists.txt:76`, `${CORE}/../Diagnostics/engine/pack_io.cpp` → `${PP_SRC}/Diagnostics/model/pack_io.cpp`, the same file.

    I found no namespace, type, function or behaviour change. I read `B1.19-verify.txt` (VERIFY PASSED, other lines 0, CONFIGURE PASSED for both trees) and `B1.19-step6-verify-final-lines.txt` (the same 18 hand-edit lines, by design). They agree with the above.
  - Duplicate header names: the only clash is `variability_measures.h` (in `Diagnostics/derived` and in `Gui/variability`). Neither moved, and every include of it uses a path.
- **The order check.**
  - On the moved tree, `diagnostics_dir_order_check -V` gave 120 files (model 20, content 26, derived 56, engine 18), **0 upward includes and 0 folder cycles**, and all six self-test cases ok.
  - The red is a real plant of the fault the check guards, in both forms. The fixture plants `model/a.h` including `../engine/d.h`, which must be exit 1. On the real tree, `B1.19-step6-plants.log` shows `model/zz_b119_p1.h:2 [model -> engine] "../engine/fire_findings.h"`: 1 upward include and 1 cycle, test Failed. With the plant removed, it passed (BT-41).
- **`isRulesIoDir()`, re-keyed** (`architecture_lint_test.cpp:529-542`).
  - I computed both sets over the real files. The old keys (`derived/`, `engine/`, `norms/`, `measures/`) covered 102 files at HEAD. Mapped to their new paths, the new keys cover all 102, lose none, and **gain 1**: `derived/swing_themes_pack.h` (E1).
  - The function chooses only A5's message (`:785-791`), not whether A5 fires.
  - The lint binary (22:17:13) is newer than its source (22:16:57), so the plants ran against the re-keyed code. `B1.19-step6-plants2.log` shows the narrowed keys at work:
    - `content/norm_pack.cpp` and a new `model/` file get the rules message;
    - a new `content/` file gets the P11 message.
  - The drill pack's A9 entry (`:378`), its fixture (`:1139`) and `context_check.py:84` are re-keyed to `Diagnostics/content/drill_pack`, which exists.
- **The two other calls.**
  - `gate_catalogue_rules.h:137`: a producer path re-spelled to the map's image (`measures/` → `engine/live_measure_source.cpp`). It follows `--allow-code-path` and B1.6's precedent (its NOTES:45, the same file), and `gate_catalogue_test` passes. Accepted.
  - IM §5.3 OBJ-PHASE-GRID, `Diagnostics/measures` → `Diagnostics/model`: `buildPhaseGrid` is in `model/measure_sample.cpp`, so the new path is correct. It follows the ruling's "the IM's paths follow it" (the step brief, "The ruling"). Accepted.
- **The allowlist:** 171 lines before and after, 126 entries before and after. There is one re-key (`Diagnostics/model/measure_sample.cpp`, A7), no line was added, and every `Diagnostics/` key names an existing file. `design_system_allowlist.txt` is untouched.
- **Include paths:** no include directory names a `src/Diagnostics` sub-folder in any `CMakeLists.txt` or `*.cmake` (4 → 0). `${SRC}/Diagnostics` itself remains in test cmake files (the notes' EDGE 2; outside the four).
- **Traceability:**
  - the new test is labelled `pp_requirement(diagnostics_dir_order_check CTL-BT-40)`;
  - `rtm.md` changes by the one new row and the counts (441 → 442 registered, 16 → 17 labelled);
  - `macos.txt` gains `diagnostics_dir_order_check` and the commit line;
  - `rtm_baseline.txt` is unchanged;
  - `rtm_check` passes.
- **GIT-6:** the IM, the placements page, the developer guide (§14 and the Location line), both tools' READMEs and the tool's doc rewrites are all in the change.

## My runs (all on the implementer's build, through `tools/method/one_at_a_time.sh`, no rebuild)
- `ctest -LE qml_ui -R 'architecture_lint_test|design_system_lint_test|diagnostics_dir_order_check|move_files_selftest|rtm_check|rtm_selftest|im_excerpt_selftest|artefact_paths_check|gate_catalogue_test|store_isolation_lint_test|diagnos|norm|pack|measure|characteristic|reduction|fire_findings|corridor|context' -j 6`: **37/37 passed**.
- The 40 suites in `src/Diagnostics/tests` (`-R '^(<40 names>)$'`): **40/40 passed**.
- `ctest -R '^diagnostics_dir_order_check$' -V`: passed, with the counts above.
- `git status --short` was identical before and after (226 lines): nothing changed the worktree.
- Staleness: `content/norm_pack.cpp`'s mtime is later than the binaries, because of the restore after plant 2. It is R100 against HEAD, so its content is unchanged and the binaries are current in substance. A ninja build would recompile it, but nothing would change.

## Findings
- **E1. BLOCKING: `isRulesIoDir()` covers one file more than the old keys** (`src/Gui/tests/architecture_lint_test.cpp:540`).
  - What: `Diagnostics/derived/`, taken whole, now includes `derived/swing_themes_pack.h`. That file came from `packs/`, which the old keys did not cover, so today it gets A5's P11 message.
  - Why BLOCKING: the brief makes the test binary and says a scope change needs the Solution Architect.
    - The ruling (`DECISION-R8-files.md`) places the file in `derived`, but rules nothing about which A5 message it takes.
    - The implementer's disclosure (call 3) is not an approval, and the standing block (#402) says to keep today's behaviour and log it.
  - Evidence that keeps the fix cheap:
    - 0 files lost;
    - the file has no I/O token, so no lint output changes today;
    - the function picks only A5's message, never whether A5 fires (`:785-791`).
  - Fix, either one:
    - **(a)** a one-line carve-out, ahead of the `derived/` return: `if (rel.startsWith(QLatin1String("Diagnostics/derived/swing_themes_pack."))) return false;`, with a comment, and then the lint target rebuilt by the coordinator's clerk;
    - **(b)** the Solution Architect rules, before the commit, that the file takes the rules message. Put this beside call 3's proposal (key `content/` whole), so that one ruling settles the message's scope.
- **E2. EDGE (wording, allowed in the fix round): CLS-PACKS-NORMS half-misplaces the packs' types** (`governance/artefacts/information_model.md`, the CLS-PACKS-NORMS row, about line 711).
  - The added sentence says "The packs' and norms' types are in `src/Diagnostics/model`". That is true of `CharacteristicPack` (`model/characteristic.h:435`), the norm types and the pack file's shape (`pack_io`). It is not true of the other packs' types, which are in `content`: `DrillSet`, `ScreenSet`, `ReferenceSet`, `GateWordSet` and `NormPack`, in `content/*_pack.h`.
  - §2 "The packs" likewise describes `content` as only "loaders, validators, registries and provider interfaces".
  - Fix:
    - the row: "The characteristic pack's and the norms' types, and the pack file's shape, are in `src/Diagnostics/model`; the other packs' types, and every loader, validator and registry, in `src/Diagnostics/content`";
    - §2: "`content` (the packs and norm packs, their loaders, validators, registries and provider interfaces)".

## Taken from the notes, unchecked
- Both Mac builds' results (`B1.19-step6-build-tests.log`, `-build-app.log`: exit 0, tools and lm_repair on) and the configure back to tools off.
- Parity: 0 differences against `b1-base-2` (`b119-parity-diff.log`).
- `count_includers.py`: before and after identical stem by stem.
- `context_check.py`'s report: the same as HEAD's.
- The scan showing that no bare include relied on the deleted include paths. The app build is the evidence, and I did not rebuild.
- The IM's changelog row numbering. It is written by hand and may renumber on rebase.

Notes discrepancy, not a finding: the notes give the allowlist as "164 lines". It has 171 lines, 126 entries; the count is unchanged either way.

Left for the coordinator: the studio PC run, which is this check's first Windows run (the notes' EDGE 4).
