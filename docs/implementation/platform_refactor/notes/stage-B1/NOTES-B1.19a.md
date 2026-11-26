# B1.19a · context_tree.h's include narrowed (26 Nov, wt-stack at da6c2360, unattended)

**State: done, uncommitted.** One file changed, no behaviour, type or name change, no folder move, no test change, no
allowlist line. Builds 2 of 2 used.

## 1. What changed
`src/Diagnostics/norms/context_tree.h:21-30` (the only path changed). Line 21 was
`#include "../packs/characteristic_pack.h" // ValidationReport`. Now:
- `:21` `#include "../engine/characteristic.h" // Condition, ContextBinding`
- `:22` `#include "../engine/pack_io.h"        // ValidationReport`
- `:24` `<QByteArray>` and `:29` `<utility>`: the header names `QByteArray` (`loadContextTree`) and `std::move` (`:69`)
  itself; both reached it through the wider include before.

**Call (logged):** the brief and line 21's comment said one header. The header also uses `Condition` and
`ContextBinding` (`resolveContextBinding`, `ownContextBinding`, `:122-129`), which live in `engine/characteristic.h`,
so it gets both. Both map to `model` in either parked map, so the order check is still clean. Not forward-declared:
that would change what includers see as complete types. `characteristic.h` names `context_tree.h` only in a comment
(`:219`): no cycle. A `norms/ → engine/` include already exists (`norm_measure_source.h`).

**Includers checked.** An include-closure script over src/tests/tools found `characteristic_pack.h` the only header
any file stops reaching, and only 4 files stop reaching it: `context_tree.h`, `context_tree.cpp`,
`tests/context_tree_test.cpp`, `derived/tendencies_pack.h`. None names anything declared only in
`characteristic_pack.h` (grep of its declarations, including `kPack*`, `MetricDomainFn`, `std::function`), so no
includer needed its own include. **After the builds**, `ninja -t deps` on both trees: the only objects that depend on
`context_tree.h` and not on `characteristic_pack.h` are `context_tree.cpp.o` (in each target that compiles it) and
`context_tree_test.cpp.o`.

## 2. The order check (`python3 build/platform/B1/parked-B1.19/dir_order_check.py --root build/wt-stack --base src/Diagnostics --order model,content,derived,engine --map build/platform/B1/parked-B1.19/B1.19-diagnostics-optionB.map`)
- **Before:** 120 files, 250 includes inside; model 20, content 26, derived 56, engine 18. **1 upward include:**
  `src/Diagnostics/norms/context_tree.h:21 [model -> content] "../packs/characteristic_pack.h"`. Exit 1.
- **After:** 120 files, 251 includes inside; **0 upward includes.** Exit 0.
- Extra record, option A (`B1.19-diagnostics.map`, `--order model,content,engine,derived`), after the fix: 19
  (NOTES-B1.19 recorded 20 before). That parked map is the by-kind map without the decision page's five re-placements,
  so 19 is not option A's count: the remaining hits are the 15 engine → derived, `norm_provider.h:22`,
  `pack_provider.h:21`, `swing_themes_pack.h:61-62`.

## 3. Builds and tests (all through `tools/method/one_at_a_time.sh`, `-j 3 -- -k 0`)
- `build/tests`: 1665 steps (a stale tree), exit 0, 0 errors.
- `build/app`: 919 steps, exit 0, 0 errors, signed.
- `ctest --test-dir build/wt-stack/build/tests -R 'diagnos|norm|pack|measure|characteristic|context|architecture_lint|design_system_lint' -j 3`:
  **29/29 passed** (66 s). These include `context_tree_test`, `characteristic_pack_test`, `pack_io_test`,
  `norm_pack_test`, `norm_test`, `core_pack_test`, `live_measure_source_test`, `diagnostics_*`,
  `session_diagnostics_*`, `architecture_lint_test`, `design_system_lint_test` and six `qml_ui` cases matched by name.
- No studio PC run (per the brief). No test changed, so no BT-39 label and no RTM change.

## 4. Paths, stores, EDGE
- Changed: `src/Diagnostics/norms/context_tree.h` only (`git status`: one path; HEAD still da6c2360). Docs: grep of
  `docs/` and `governance/` for `context_tree` with an include: one hit, `docs/implementation/swe-audit/findings/B-diagnostics.md:207`,
  the audit finding this item fixes. It is a dated audit record (pinned commit), so left as is.
- Real stores: none reachable (a compile-time include change; tests only).
- **EDGE:** the same finding names `norm_pack.h:21` (`../packs/characteristic_pack.h // ValidationReport,
  ValidationIssue`). Not in scope and not an order break (both content); a like narrowing for an issue.
- **EDGE:** the check's scripts live in the scratchpad, not committed. `tools/` sources that are not built in either
  tree were covered by the closure script only; none reaches `context_tree.h` by a path that loses the pack header.
