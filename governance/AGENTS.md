# Working contract

How work is done here, for people and agents alike. This page is the whole of the mandatory reading; everything else in `governance/` is read when doing that kind of work (the index below). One person owns the project and makes the decisions: "the owner" below.

## The non-negotiables

1. **Nothing is called done, fixed or working** without running the relevant tests or observing the behaviour, and the report says which. A requirement is done when it is in `main`, shipped in a beta and used for real, with no open blocking bug.
2. **Agree the design briefly with the owner before** changing an on-disk format, which object owns what (`artefacts/information_model.md`, read through `python3 tools/method/im_excerpt.py ID…`, never whole), a lifecycle (`artefacts/lifecycle_model.md`), or a design-system component or token (`artefacts/design_system/README.md`). Update those documents when such a thing actually changes; no version bumps or change-log rows for small edits.
3. **No rule in QML** (AD-52): thresholds, maths and pass/fail live in C++. **An object's files are read and written only by their owner.** **Never add a lint allowlist line to make a test pass**: fix the code, or ask.
4. **Safety:** launch the app only through `tools/probe/run_headless.sh` (AG-15). Tests never touch the real library, settings, keychain, Trash or devices, and make no sound.
5. **Build economy:** every build, ctest run, lint run and probe goes through `tools/method/one_at_a_time.sh`, within the machine's job limit; test builds pass `--target` for only the suites and lints being run; the app tree is built once, at the end of a change; ccache is on (BT-2). Tell the owner before heavy work on a machine they are using.
6. **Git:** `main` only, no branches. Ask before committing or pushing unless the task pre-approved it. Commit titles are `Area: plain-English summary`, with no programme ids and no AI attribution. Write commits, issues, comments and docs in the author's voice — never put the owner's personal name in them (say "the owner" if needed). A commit carries the tests and docs its change touches.
7. **Honest numbers:** show every reading with its warning, never hide one; a missing reading shows "-", never 0. The launch monitor is primary where present. Never assume a square set-up.
8. **Privacy:** nothing private enters the repository: no golfer's name or athlete id, no machine name, login, share path or home path. How one developer uses their own machines goes in their local, untracked agent file.
9. **A personal, non-commercial product.** Licence questions are weighed accordingly; vendor software is never decompiled, bypassed or reached into.
10. **Put decisions in plain words with a recommendation, and push back** when a request or an assumption looks wrong, before doing the work.
11. **Rules live in `governance/`,** never only in an agent's private notes.

## How an issue is worked

1. Read the issue and the relevant code. Propose the plan in a few plain bullets, highlighting any design, format or UI choice for the owner. Wait for the go-ahead.
2. Implement in one pass, with targeted test builds and the lints (`architecture_lint_test`, `design_system_lint_test`, and any other the change touches).
3. If start-up, QML or app wiring changed: one app build and `headless_probe_test`.
4. Report briefly in chat: what changed, the tests run, anything for the owner to eyeball. Commit on the go-ahead, then a short closing comment on the issue.

- **A bug fix** starts with a test that fails on the bug: see it red, fix, see it green. A refactor or a feature gets solid invariant tests, run green; no mutation patches (BT-40).
- **No independent reviewer by default.** Use one only for a risky stored-data migration, a destructive file operation or tricky capture concurrency, and tell it to flag real bugs and data-loss risks only (AG-21).
- **Issues only for real things:** a real bug, a real data-loss risk, or concrete follow-up worth doing; never a contrived or theoretical edge case (AG-22).
- **No long notes files or multi-comment essays** for routine work: a short commit message and a closing comment are enough. Anything the owner must run goes in a script under `build/run-me/` (AG-27); a long command gets a watchdog (AG-26).

## Read on demand: what to read for each kind of work

| Doing | Read |
|---|---|
| Any code change | Look up the rule you need by id; don't load the pages whole for a small fix: CODE-1…4 and AD-52 in [`controls/build.md`](controls/build.md); BT-3…BT-7 in [`controls/test.md`](controls/test.md); GIT-6…GIT-10 in [`controls/commit.md`](controls/commit.md) |
| Data, C++ structure, on-disk formats | [`controls/decide.md`](controls/decide.md) (what needs agreeing first); the information model by excerpt; [`artefacts/lifecycle_model.md`](artefacts/lifecycle_model.md) for a persisted or long-lived object (AD-51) |
| QML and screens | [`artefacts/design_system/README.md`](artefacts/design_system/README.md); [`controls/product/ui-behaviour.md`](controls/product/ui-behaviour.md); UI-17, UI-18, UI-20 in [`controls/review.md`](controls/review.md) and [`controls/decide.md`](controls/decide.md) |
| Analysis and metrics | its developer guide first (AG-37); [`controls/product/analysis-and-data.md`](controls/product/analysis-and-data.md); [`artefacts/sign_conventions.md`](artefacts/sign_conventions.md); [`artefacts/imu_frame_contract.md`](artefacts/imu_frame_contract.md) |
| Diagnostics and coaching content | [`controls/product/diagnostics-and-coaching.md`](controls/product/diagnostics-and-coaching.md); [`artefacts/normative_reference.md`](artefacts/normative_reference.md) |
| Capture and sessions | [`controls/product/capture-and-session.md`](controls/product/capture-and-session.md); the lifecycle model |
| A bug fix, a refactor, a cut-over | [`controls/kinds-of-work.md`](controls/kinds-of-work.md); [`procedures/cut-over.md`](procedures/cut-over.md) |
| Corpus, sweeps, re-analysis, studies | [`controls/kinds-of-work.md`](controls/kinds-of-work.md), "Data and analysis work"; [`procedures/`](procedures/README.md) (sweep, corpus and capture protocols) |
| A plan for something larger | [`controls/plan.md`](controls/plan.md) |
| A review, a crash report | [`controls/review.md`](controls/review.md) |
| Requirements and release readiness | [`artefacts/requirements.md`](artefacts/requirements.md); [`artefacts/test_strategy.md`](artefacts/test_strategy.md) |
| A release | [`controls/release.md`](controls/release.md); [`procedures/`](procedures/README.md) (release and signing) |
| A visit to the capture rig | BT-16 and BT-43 in [`controls/test.md`](controls/test.md); the capture protocols |
| Lessons that bite | [`guidance/lessons.md`](guidance/lessons.md); [`guidance/advice.md`](guidance/advice.md) |

Control ids (AG-15, BT-2, UI-1 …) are kept so older text, commits and notes still resolve. Older text names four roles (Product Owner, Solution Architect, Design Lead, Coaching Lead) and footnotes their rulings; all four are the owner. New text names no roles and adds no provenance footnotes.
