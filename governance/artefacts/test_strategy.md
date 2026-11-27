# PinPoint Studio test strategy

What tests are for here, what makes one trustworthy, where each level runs, and how release readiness is read. Short on purpose: the rules it points to are in `governance/controls/`.

## 1. What "done" means

A requirement is **done** when it is in `main`, has shipped in a beta build and been used for real, and has no open blocking bug ([`requirements.md`](requirements.md), "Status values"). A change is called done, fixed or working only after the relevant tests ran green or the behaviour was observed, and the report says which. A failure is filed as a bug only when it was seen or reported (AG-24).

## 2. What makes a test trustworthy

1. **It checks against an independent oracle**, never a record of today's answer (BT-7).
2. **It gives the same verdict twice.** It is deterministic, or its noise floor is measured and its tolerance stated (AD-20; L-21 for intermittent failures).
3. **A test that sets up a condition checks that the condition took** (a refused file, a missing folder, a fault). Two tests passed whatever the product did because their set-up silently failed to bite: forced save failures by file permissions, which Windows ignores (#244), and a folder scan whose listed folder had been renamed (`swing_doc_test`).
4. **It is silent** (BT-6), **runs through ctest** (BT-3), and never touches the real library, settings, keychain, Trash or devices.
5. **It names the requirement it checks, when it checks one** (BT-39): `pp_requirement(<test> REQ-…)` beside its registration, or `// REQ:` lines in a QML test. A habit, not a gate.

## 3. Seen to fail: bug fixes only

A **bug fix** starts with a test that fails on the bug, on the code before the fix: write it, see it red, fix, see it green (BT-40). The red is recorded in the fix's commit body (GIT-14). A red found by reading the old code is a prediction, not a red run. A fault that shows only on a device, at the capture rig or in a timing race is shown by the failure observed on an old build and the behaviour observed on the fixed one, plus a test of the logic, red first, where one can be written.

A **refactor or a feature** gets solid invariant tests, run green. No artificial mutation patches.

## 4. The levels, and where each runs

| Level | Where | Ruled by |
|---|---|---|
| The working loop: the tests a change affects, built with `--target`, once, at the end | A development machine, through the one-at-a-time lock | BT-2, BT-4, BT-15 |
| Screen tests for the screens a change touches | A development machine | BT-4, BT-5 |
| Probes and renders | A development machine (macOS), headless, guarded | AG-15, AG-16, AG-25, BT-14 |
| The full suite, when a change reaches far | A development machine | BT-4, GIT-6 |
| The primary-platform run | A test machine of the primary platform, Windows | BT-13, AD-23; [`docs/developer/testing_developer_guide.md`](../../docs/developer/testing_developer_guide.md) |
| Parity and validation | A development machine, and the primary platform's release build | AD-20, AD-23, AD-24 |
| A visit | A capture rig | BT-16, BT-43 |
| The release gate | Each platform, a fresh test tree | BT-17, BT-25, BT-29 |

## 5. Release readiness

Two on-demand views read the same sources (this folder's [`requirements.md`](requirements.md), the tests' requirement labels and the live GitHub issues, through `tools/method/requirements_table.py`):

- **The growth chart's readiness panel** (`python3 tools/history/loc_history.py`): area by release and MoSCoW, each cell done out of all with how many rows an open bug blocks, and the open bugs by area label.
- **The traceability report** (`python3 tools/method/make_rtm.py`, written into `build/rtm/`): readiness by release and MoSCoW, the v1 Must and Should rows still to do, and every row with its status, linked tests and open issues.

Neither is a commit gate and nothing they write is committed. Before a release they are read as BT-42 says. A bug filed against a requirement carries a `Requirement: REQ-…` line, so the views can see it.
