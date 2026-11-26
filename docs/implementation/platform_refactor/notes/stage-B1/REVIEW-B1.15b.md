# B1.15b · Independent review (AG-21)

Reviewer: an independent agent, 26 Nov 2026. Tree: `build/wt-37c`, HEAD = origin/main `a0379ef8`, uncommitted.
I edited nothing in the worktree. I wrote only this file and scratch files under `/tmp`.

**Verdict: SIGNED OFF.** No blocking finding. The commit waits for two things: the studio PC result (pending, not a
finding) and the rewording of one sentence in the commit message (E1).

## What I checked, and how
- **The patches match the tree.** `git diff -M HEAD` is byte-identical to `B1.15b-diff.patch` (79 files, 19 renames).
  `git diff a0379ef8 20dd4f41` is byte-identical to `B1.15b-part1.patch`.
- **The ruling was followed, not the brief.** R7 keeps `session_posture` and `session_delivery` in `Gui`. The map holds
  the eight stems, `session_series_types.h` and `session_curves` (19 files), and names the two held files in a comment.
- **Part 1 changes no arithmetic.**
  - E4: I re-cut `session_sequence.h:40,111-136` and `.cpp:22-24,168-288` from `a0379ef8`. All 27 + 124 lines are in
    the new `session_sequence_words.{h,cpp}`, in order and unedited, which agrees with `--verify`.
  - `:314` now calls `pinpoint::figures::isAfterImpact`. That is the same function: `charts_words.h:74` is a `using` of it.
  - E2: `consistencyGradeScales` moved unchanged to `variability_env`; no caller changed.
  - E3: `consistencyGuardWords` moved unchanged into `charts_words.h`, inside `namespace pinpoint::charts::words`.
    Its three call sites only gain `words::`.
  - E1: the include and its two comments.
- **The movers include nothing from Gui.** The 19 files include only Vocabulary, `Kinematics/gap_bridge`,
  `Wrist/wrist_profile`, `../consistency_home.h` and each other. None declares a Q_OBJECT, a Q_GADGET or a QML type.
- **No stale paths in code.** There is no stale include or build path in `src`, `tools` or `tests`.
  - All 130 quoted includes of the moved headers and the new words header resolve. The only ones that resolve through
    `${GUI}` are test includes of `charts/…` headers that stay in Gui.
  - Every target that compiles `session_sequence.cpp` also compiles `session_sequence_words.cpp`: 13 lines each, one
    for one. Every C++ user of the words functions includes the new header.
- **The new source list.**
  - `git grep PP_QML_SOURCES` finds exactly three module consumers: the app, `qml_ui_test` and `session_setup_ui_test`.
    Each adds `PP_CHARTS_RULES_SOURCES`, root-relative in the app and prefixed with `PP_ROOT` in `src/Gui`, in the scope
    where `PP_QML_SOURCES` gets the same prefix.
  - The drop-count check still holds: `ting_player.{h,cpp}` are still in `PP_QML_SOURCES` (`:596-597`).
  - The global include list is unchanged.
- **The tests were re-pointed, not changed.** In the 17 test sources, the only edits are include lines and two comment
  paths. No check was removed.
- **The lint.**
  - The allowlists, the lint tables and `context_check.py` are untouched.
  - `b115b-plants.log` shows both planted violations caught as A1 Rules → Adapters. The new file's ring comes from the
    `Diagnostics/` entry.
  - `context_check --list c` after the move: 0 `series` lines and 0 new violations. The crossings rose from 993 to 1023,
    which is expected (Gui composing Diagnostics).
- **The information model.**
  - 1.12.12 is in the version line.
  - RING-RULES gains the folder in its context cell and its directories cell.
  - The note under the table is corrected.
  - `[^sa24-r7]` is defined once.
  - The change-log row's last cell has the same form as 1.12.11's.
  - Every new sentence traces to R7. The provenance is in footnotes; the text states facts.
- **The gate catalogue.** All six producer paths in `gate_catalogue_rules.h` exist.
- **The figures check out.**
  - Lines moved: 4,801. The words files: 215 lines.
  - Includer counts match before and after; the +1 for `consistency_grades` and for `session_sequence` are the E2 and
    E4 includes.
  - `--verify` lists 136 other lines: 68 from part 1 and 68 from the list relocation. I counted each group in the file.
  - Parity is 0 for both parts.
  - ctest: 29/29 after part 1, 34/34 after the move. QML: only `tst_charts_panel` test_15 (#231) fails.
- **My own re-run, through the lock:** `architecture_lint_test`, `gate_catalogue_test`, `charts_session_maths_test`,
  `session_consistency_test` and `trust_readings_test`: 5/5 passed.
  - The only changed file newer than the binaries is `series/session_warp.h`, which `plants.sh` restored. It is a 100%
    rename.
- **The studio PC.** Windows built the tests tree (exit 0). Pass 1 was still running when I read the log.

## Findings (all edge)
- **E1 · The commit message's test sentence is not supported.** It says "Every suite passed on the Mac and the studio PC,
  except the QML tests already failing there".
  - The Mac ran 34 named suites and 4 QML files, not every suite.
  - `tst_charts_panel` test_15 (#231) also fails on the Mac.
  - The studio PC run has not finished.
  - Reword it to what ran, once the studio PC result lands.
- **E2 · The `session_setup_ui_test` line was never run.** `PP_SESSION_SETUP_UI_TEST` is OFF in every tree, so neither
  its `target_sources` line nor the drop-count check was configured. I read both and they are correct. Say in NOTES that
  this part is "read, not exercised".
- **E3 · One figure in NOTES is from the dry run.** NOTES §3 says "101 path mentions". That is the dry run's total. The
  real run reports 100: 88 build, 6 docs and 6 comments.
- **E4 · One claim in NOTES is too broad.** NOTES §3 says "Old paths left outside docs/implementation: 0". There are 3 in
  `docs/research/data/gates/census_20261016.md`. That file is a dated census, and leaving it is right, but the claim
  should say so.
- **E5 · NOTES doesn't list the test files.** The brief asks for each re-pointed test file to be listed. There are 17 (see the diff stat).
- **E6 · Cosmetic.** The cut leaves a double blank line in `series/session_sequence.h` (about :109) and in `.cpp` (about :166).
- **E7 · Plain words (GIT-9).** The message body uses some code-internal phrases: "the adaptor that wraps the screens'
  scale provider", "gate lists", "the QML module's source list". GIT-8 and GIT-13 are met: the title has an area, the
  body is separated by a blank line, and there are no trailers.
