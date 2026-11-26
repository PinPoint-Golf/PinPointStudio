# NOTES · B1.20a · namespace_collision_test

**Status: done, uncommitted, the test GREEN.** The check on main found one duplicate. I stopped as the brief says, then the coordinator ruled to fix it here (PR-39).

## The finding, and its fix (PR-39)

    pinpoint::analysis::LmShotValues: Gui/launchmonitor/lm_session_model.h:51, LaunchMonitor/lm_session_reductions.h:76

Tree: `origin/main` at 46a87e2e (the worktree's HEAD, fast-forwarded first).

- Both say `using LmShotValues = QHash<QString, double>;`. The model header repeats it on purpose so as not to include `lm_session_reductions.h` (its comment, lines 39–46).
- **Observed risk: low today.** Identical alias redeclaration is legal C++, so this is not an ODR violation now. The model header's copy is used by no declaration in that header. The files that use the name (`lm_session_model.cpp`, `club_distances.h`, `lm_session_reductions_test.cpp`) include the rules header, so if the two drift apart the compiler rejects it there. It would only become the silent DagEdge shape if a future file used the name through the model header alone.
- The brief passes only "a forward declaration and its definition", so the test reported it.
- **Fixed (PR-39):** the alias stays only in `LaunchMonitor/lm_session_reductions.h`. Its copy in `Gui/launchmonitor/lm_session_model.h` is deleted, with a comment saying where it lives (`lm_session_model.h:47-48`).
- **Call: the model header does NOT include the rules header.** PR-39 said "includes the rules header instead", but that isn't needed and would undo the header's documented reason for staying light (lines 39–41: keep the field catalogue out of the 7 files that include it). That header names no `LmShotValues`. Every file that uses the name already includes `lm_session_reductions.h` (`lm_session_model.cpp:30`, `club_distances.h:48`, `lm_session_reductions_test.cpp:10`), and the build proves it. If the coordinator wants the include anyway, it is one line, inward (Adapters → Rules), and the lint allows it.

## What changed (uncommitted, build/wt-5b; every path changed)

- `src/Gui/tests/lint_source_text.h` (new): the lint's `Source`, `isIdent`, `strip`, `makeSource` and `lineAt`, moved unchanged into `namespace pp_lint` as `inline` functions. It is header-only, so no new library.
- `src/Gui/tests/architecture_lint_test.cpp`: the include at :133 and the `using pp_lint::…` lines at :444-449. Nothing else in the file changed.
- `src/Gui/tests/namespace_collision_test.cpp` (new): the header comment (rules, calls, known limits); `declarations()` :169 (a brace-stack scanner over tokens); `collisions()` :337; the fixtures `selfTest()` :379; the tree scan `main()` :442.
- `src/Gui/tests/CMakeLists.txt:500-508`: `pp_add_test(namespace_collision_test …)`.
- `src/Gui/launchmonitor/lm_session_model.h:47-52`: the duplicate alias deleted (PR-39).
- `docs/developer/testing_developer_guide.md`: a paragraph on `namespace_collision_test`, after the architecture lint's section.
- `build/platform/B1/NOTES-B1.20a.md` (this file).

## Tests (through one_at_a_time.sh; 3 Mac builds of 3: PR-39 and PR-42 each allowed one)

- Build: `namespace_collision_test`, `architecture_lint_test` and `design_system_lint_test`, in one `cmake --build`. Log: `build/wt-5b/build/b120a-tests.log`. No warnings.
- `ctest -R` on the three (`build/wt-5b/build/b120a-ctest.log`):
  - `design_system_lint_test`: Passed
  - `architecture_lint_test`: Passed (the shared header changed nothing it does)
  - `namespace_collision_test`: Failed, on the one finding above
- Build 2, after the fix: the whole tests tree (`b120a-tests2.log`, exit 0; no warnings in the changed files). Then `ctest -R` on `namespace_collision_test`, `architecture_lint_test`, `design_system_lint_test`, `lm_session_model_test` and `report_lm_test`: **5/5 Passed** (`b120a-ctest2.log`).
- All 12 fixtures PASS, including:
  - a planted duplicate fails
  - a forward declaration plus its definition passes
  - one name in two namespaces passes
- The tree floors also PASS: 1048 files; 2568 declarations, 2205 of them definitions before the fix (2567 and 2204 after); the DagEdge anchor; 0 files whose braces didn't balance.
- **Cross-check against an independent grep (column-0 definitions only).** It covered three sets:
  - Every class, struct or enum name defined at column 0 in two or more non-test files: 75 names.
  - The 54 `final` classes, which the grep misread as the name "final". With their real names resolved, there are no repeats among them.
  - Every repeated `using X =`, indented or not.

  I opened each site's enclosing namespace. Every name the test doesn't report is in a different namespace, in an anonymous namespace, or a class member. `LmShotValues` is the only same-namespace repeat the grep found.
- Build 3, after PR-42 (`b120a-tests3.log`, exit 0, no warnings): `namespace_collision_test` (all fixtures PASS; 2567 declarations, 2204 definitions; 0 collisions; no notes), `architecture_lint_test` and `design_system_lint_test`: **3/3 Passed** (`b120a-ctest3.log`). No studio PC re-run: PR-42 tightens a test rule and changes no product code.
- **Cabin (full suite, both passes; MSVC compiles the new test and lint_source_text.h):** run `studio PC-wt-5b-20261124-225745`, over 46a87e2e, with a 6-file patch.
  - **The build passed:** jom, exit 0, so MSVC compiles the new test and `lint_source_text.h`.
  - **On the studio PC, `namespace_collision_test` and `architecture_lint_test` both passed.**
  - The run exits 12. Pass 1 has 328 passed and 11 failed of 339; pass 2 has 41 passed and 2 failed of 43.
  - **All 13 failures predate this change.** They are the 13 standing failures, exactly the set of wt-37c's run at 12992896 (21:54). wt-37c's run at fcd5ac53 (22:36) and wt-5b's at 50ba450b (22:14) each had one more (`diag_stage_panel_test`, `paced_connect_queue_test`). The 13:
    - tendencies, dtl_posture, shot_commands, migrate_step2, parity_json
    - session_diagnostics, athlete_diagnostics, reduction_rule_golden, folder_lock
    - charts_delivery_presenter, athlete_repository
    - qml_ui tst_charts_panel and tst_session_report
  - None of the 13 touches the files this item changes. The test count went from 338 to 339, the extra one being the new test.
  - Stores: unchanged. Summary: `build/wt-5b/build/studio PC-runs/studio PC-wt-5b-20261124-225745.txt`.

## Calls I made (logged, unattended)

1. **`detail` (tightened by PR-42, review R1).** C++ gives a `detail` type external linkage, so it is not per file. A `detail` repeat with a site in a header now FAILS; one whose every site is a `.cpp`/`.cc`/`.mm` is printed as a `note:`. Two new fixtures cover both cases (header repeat fails; source-only repeat is a note; header plus source fails). There are none on main today: the test is green. Only parts named exactly `detail` count, so `synth_detail`, `club_standards_detail` and the like are read as ordinary namespaces.
2. **The global namespace counts** (printed `::Name`). There are no global collisions today.
3. **An identical `using X =` in two files fails**, per the brief. That is what made the finding above a failure.
4. **A file whose braces don't balance fails**, once its preprocessor lines are blanked (an `#if` whose branches open different scopes). There are none today.
5. **The blanking is shared rather than copied**, as the brief asked: a header, no library.
6. Process slip: an early `grep -rn architecture_lint docs/developer/*.md` printed six lines of `information_model.md`. I didn't open the file. Later greps were scoped to `testing_developer_guide.md`.

## Real stores

None. The test only reads `src/` under `PP_SRC_ROOT`, read-only, and writes nothing.

## EDGE (for an issue; not fixed)

- `typedef` and `union` aren't read (the brief names class, struct, enum and using).
- `#if 0` blocks are read. A macOS and a Windows variant that are never linked together would still be reported.
- A type declared by a macro (`Q_DECLARE_FLAGS`) isn't read.
