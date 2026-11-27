# Governance

What this folder holds, and how a rule here changes. Start with [`AGENTS.md`](AGENTS.md): the working contract, the only page read every session, with an index of what to read for each kind of work.

| Folder | Holds | Read |
|---|---|---|
| [`AGENTS.md`](AGENTS.md) | The non-negotiables, how an issue is worked, and the read-on-demand index | Every session |
| [`controls/`](controls/) | Area references: the rules for one stage or kind of work (build, test, commit, review, release, plan, decide, kinds of work), and the product rules in [`controls/product/`](controls/product/) (screens, analysis and data, diagnostics and coaching, capture and session). Each rule keeps its id (AG-15, BT-2, UI-1 …) | When doing that kind of work |
| [`artefacts/`](artefacts/README.md) | The reference documents a change is checked against: requirements, use cases, the information model, the lifecycle model, the design system, sign conventions, the normative reference, the sensor frame contract, the test strategy | On demand; the large ones by excerpt |
| [`procedures/`](procedures/README.md) | Step-by-step procedures: releases, signing, sweeps, cut-overs, capture and corpus protocols | When doing that task |
| [`guidance/`](guidance/) | Advice and lessons learned: not rules | When they bite |

## The automated checks

The rules that matter most are checked by tests, not by memory: `architecture_lint_test`, `design_system_lint_test`, `design_system_tokens_check`, `session_setup_lint_test` (W12: no rule in QML), `qml_reactivity_test`, `namespace_collision_test`, `store_isolation_lint_test`, `headless_probe_test` and `run_headless_selftest`, and `artefact_paths_check`. Their table, with where each one's rules are written, is in [`controls/test.md`](controls/test.md), "The checks".

The lint allowlists are the register of approved exceptions, each line with its reason: `src/Gui/tests/architecture_allowlist.txt` (`architecture_lint_test`), `src/Gui/tests/design_system_allowlist.txt` (`design_system_lint_test`) and `src/Gui/tests/qml_rule_allowlist.txt` (`session_setup_lint_test`). A line is never added to make a test pass: the code is fixed, or the owner agrees the exception.

## Changing a rule

Propose the change to the owner in plain words: what, why, and where it goes. A rule binding every developer goes in the one place it belongs here, stated once; a fact about one developer's own machines goes in their local, untracked agent file. A retired rule is deleted (git keeps it). New text names no roles and adds no provenance footnotes; older text that names the Product Owner, Solution Architect, Design Lead or Coaching Lead means the owner.

On 27 Nov 2026 the governance was slimmed: the programme and orchestration rules for multi-agent runs, the feature freeze, the four-role attribution, sentence-level tracing and the traceability matrix's commit gate were retired. The traceability report is now on demand ([`artefacts/test_strategy.md`](artefacts/test_strategy.md) §5).
