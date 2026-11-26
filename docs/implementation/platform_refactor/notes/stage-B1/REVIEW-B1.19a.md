# B1.19a review · context_tree.h's include narrowed (26 Nov, wt-stack at da6c2360, unattended)

**Verdict: SIGNED OFF.** No blocking finding. Two EDGE notes.

## What was checked

1. **Diff.** `git -C build/wt-stack status`: one path, `src/Diagnostics/norms/context_tree.h`. The diff touches only
   the include block (`:21-30`): `../packs/characteristic_pack.h` becomes `../engine/characteristic.h` and
   `../engine/pack_io.h`, plus `<QByteArray>` and `<utility>`. No declaration, type, name or behaviour changes.
2. **Names declared directly.**
   - `context_tree.h`: `Condition`, `ContextBinding` come from `engine/characteristic.h:229,236`. `ValidationReport`
     comes from `engine/pack_io.h:85`. `QByteArray` (`:180`), `std::move` (`:69`), `QJsonObject`, `QString`,
     `QStringList` and `std::vector` each have their own include. No cycle: `characteristic.h:219` names
     `context_tree.h` only in a comment.
   - `context_tree.cpp`: `ValidationIssue` and `IssueSeverity` (`:172`) come from `pack_io.h` through its own header.
     `QSet`, `QJsonArray`, `QJsonDocument` and `QJsonParseError` have their own includes. It uses nothing that was
     declared only in `characteristic_pack.h`: I grepped every declaration there (`CharacteristicPack` users,
     `kPack*`, `MetricDomainFn`, `PackLoadResult`, `loadPack`/`savePack`, the report and causal helpers,
     `CausalAdjacency`, `std::function`, `QHash`).
   - Includers: 22 files include `context_tree.h`. I ran `ninja -t deps` on both built trees. In `build/tests`,
     1800 objects depend on `context_tree.h`. In `build/app`, 99 do. In each tree, the only objects that no longer
     reach `characteristic_pack.h` are `norms/context_tree.cpp.o` (in every target that compiles it) and
     `tests/context_tree_test.cpp.o`. The test uses only Qt and std names it includes itself or gets through
     `context_tree.h`, and nothing from the pack. Every other includer still compiles for a reason I can name:
     - it includes `characteristic_pack.h` directly (`fire_findings.cpp:28`, `work_ons_controller.cpp:34`,
       `core_pack_test.cpp:14`, `reduction_rule_golden_test.cpp:62`, `rule_version_golden_test.cpp:55`,
       `lm_session_model_test.cpp:55`, `characteristic_engine.h:21`, `shot_direction_bounds.h:34`, `norm_pack.h:21`);
     - or it reaches the pack through `norm_provider.h` → `norm_pack.h:21` / `pack_provider.h`
       (`consistency_env.cpp`, `variability_env.cpp`, `session_posture.cpp`, `lm_session_model.cpp`,
       `live_measure_source.cpp`, `lm_corridor_test.cpp`, `live_measure_source_test.cpp`, `fire_findings_test.cpp`
       via `fire_findings.h`, `charts_delivery_presenter.cpp` via `shot_direction_bounds.h`).
     - `derived/tendencies_pack.h` loses the pack and names nothing from it.
     No file in `tools/` includes `context_tree.h`.
3. **Order check** (`--root` is the checkout the check reads). To get the red side without touching the worktree, I
   extracted `git archive HEAD src` into the scratchpad.
   - Option B map, `model,content,derived,engine`: HEAD **1** (`norms/context_tree.h:21 [model -> content]`, exit 1).
     Worktree **0** (exit 0). 250 → 251 includes inside.
   - Option A map, `model,content,engine,derived`: HEAD 20 → worktree **19**. The `context_tree.h` hit is gone. The
     19 left are the known ones: 15 engine → derived, `norm_provider.h:22`, `pack_provider.h:21` and
     `swing_themes_pack.h:61-62`.
4. **Tests.** I ran `tools/method/one_at_a_time.sh ctest --test-dir build/wt-stack/build/tests -R
   'context|norm|pack|characteristic|architecture_lint|design_system_lint' -j 3`: **12/12 passed**, among them
   `context_tree_test`, `characteristic_pack_test`, `pack_io_test`, `norm_pack_test`, `core_pack_test`,
   `architecture_lint_test` and `design_system_lint_test`. I did not rebuild. `ninja -n` plans no compile on either
   tree (it shows only the glob re-check and the app's every-build plist and sign steps). 94 of 95
   `context_tree.cpp.o` objects, and `context_tree_test.cpp.o` (18:49:33), are newer than the header (18:48:35). The
   one older object (`session_setup_ui_test.dir/.../context_tree.cpp.o`, 11:40) belongs to a target that is no
   longer in `build.ninja`: a leftover, not staleness.
5. **Evidence (BT-41).** The item serves no requirement. Its claim is that the order break is removed, and that
   claim is shown: I saw the red myself (HEAD, 1 break, for exactly this include, not another) and the green (0).
   Both trees are built after the change (object times above), and the item's suites and both lints are green.
   HARD rules: no allowlist line, no test changed, no store reachable, no commit.

## Findings

- **EDGE** · `src/Diagnostics/norms/norm_pack.h:21`: the same audit finding's other half. It includes the whole
  pack for `ValidationReport` and `ValidationIssue`. It is not an order break (both are content) and not in scope:
  narrow it in its own small item, as NOTES-B1.19a §4 says.
- **EDGE** · `src/Diagnostics/norms/context_tree.cpp:220,232`: it uses `QJsonValue` through `pack_io.h:24` and
  Qt's JSON headers, not through an include of its own. This is not a regression, because it reached
  `QJsonValue` the same way before. Include hygiene only.
