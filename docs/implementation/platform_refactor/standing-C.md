# Stage C: the standing block for every Rig and calibration brief

Read after the standing block (#402). It binds every `BRIEF-C-*.md`. The design is `docs/design/rig_design.md`: read only the sections your brief names, plus §2 (the ten principles, RP-1 to RP-10).

**1. The authority.** The design, its information-model changes (§8: C1–C5, C7–C9, C11) and its 19 answers (§9) are approved[^pr35]. Build exactly what your brief's design section says. The calibration procedures in C++ (§4.8's step machines, drivers, `app.calibration`, the routines as bindings, the real ceremonies as fixtures) are deferred to #291 and are no item's work[^c-v1]. Anything the design does not say that would add, move or rename a domain object, a persisted file, a settings key, an id, an event, a ring or a rule is a NEW information-model change: keep today's behaviour, log it, carry on with the rest.

**2. The tree is moving under you. Check paths first, never trust a path in a brief.** Stage B is still running.
- `ls src/Vocabulary`: `Recording/`, `Identity/`, `Common/` exist only after B1.5 (and B1.7 for `segment_role.h`, `trunk_calib_provenance.h`). Before B1.5 they are `src/Recording`, `src/Identity`.
- `ls -d src/Stores src/Export 2>/dev/null`: B1.16 renames `src/Export` to `src/Stores` (ruled, R6). Use whichever exists; the brief writes "the stores folder".
- `ls cmake/rings 2>/dev/null`: once Stage B Part 2's frame exists, a new source file joins its ring library's manifest (`pp_vocabulary`, `pp_rules`, `pp_domain`, `pp_stores`) and the membership check must pass. Before that, it joins the same hand lists its neighbours are in (root `CMakeLists.txt`, the test's own list). Never add an include directory.
- `grep -c '"A9"\|A9 ' src/Gui/tests/architecture_lint_test.cpp`: lint A9 (B1.21) names the hub's contract headers. If it is in, a new contract header (`rig_repository.h`, `rig_calibration_sink.h`) joins its list in your item; if not, say so in NOTES so B1.21 takes it.
- `python3 tools/refactor/context_check.py --src <your worktree>/src`: a new directory needs its context tag (`Vocabulary/Rig/` and `IMU/calibration/`: Acquisition).
- Namespaces (a coordinator's call, NOTES-C-plan C-6): the rig value types in `pinpoint::rig`; the ids in `pinpoint`, beside the other ids; the step machines in `pinpoint::calibration`. `grep -rn 'namespace pinpoint::rig\|namespace pinpoint::calibration' src` must be empty before the first of each, and the duplicate-name test (B1.20a), if on main, stays green.

**3. Never straddle a scripted move** (`docs/implementation/platform_refactor/moves.md`: B1.5, B1.6, B1.9, B1.12, B1.15b, B1.16, B1.19). A code item starts from main after a move commits, and is committed or parked as a patch before the next one starts. A docs-only item is exempt. Never run beside an item that edits the lint or the allowlist if yours does (B1.11, B1.17, B1.21). `src/Gui/main.cpp` and `src/App/` are also Stage E part 1's: the coordinator never opens an item of each on them at once.

**4. One list of deliverables.** The numbered "What" in your brief is the whole list. Before any code that removes or re-routes something, write its duties (every caller, writer, side effect and test, and where each goes) at the top of your NOTES. A test is re-pointed at the new owner, never deleted: list each. A defect your own testing finds is a new item for the coordinator, never folded in. A fix round fixes blockers only.

**5. Nothing is lost, and nothing analysed changes.**
- Settings keys are copied, never deleted or rewritten, in Stage C (§4.4). Stage E removes them.
- No test or probe writes the real `rig/rig.json`: the store takes its folder from the composition root, with the test override C-4 names. Name the guard in NOTES.
- No analysis input changes (RP-9): no stage version is bumped, no shot goes stale. Parity against the coordinator's current baseline label: 0.
- A swing is a recorded input: no old swing is rewritten or backfilled. When you add a field, write down in the schema reference what its ABSENCE means.

**6. QML.** Before touching any, read `governance/artefacts/design_system/README.md` and the page in `governance/artefacts/design_system/components/` for each `Pp*` component in the files you edit. Stage C plans no change a golfer can see (§4.9). A cut-over item ends with a before/after sheet of each page it touched (through `tools/probe/run_headless.sh`, on scratch copies, under the render memory guard) and a grep count of 0 for each removed property under `src/**/*.qml`, tests included. **Any visible change beyond the design is a design-system change: stop, and propose it to the Design Lead (the need, the screens, a before/after picture) before building it.** No new `tst_*.qml` file: extend the existing ones. **No rule is built in QML** (AD-52): the calibration QML in `src/Gui/calibration` (3,978 lines) is the one accepted exception until #291; it may shrink and must not grow, so no item adds a line of logic there[^c-v1].

**7. The sensors.**
- The rule: a routine starts and retries only when the sensors are on and still, from the device gyro (peak over about 250 ms), never on page entry and never from the quaternion-difference rate. A step where the golfer faces away is taught before it and started and ended by the sensors. The trunk routine meets it today.
- **The arm routines do not, and that is accepted until #291**: each starts on a timer when its page opens, and after a failure the golfer presses Recalibrate. #291's track builds #266's start on a still arm with the procedures[^arm-hf]. No Stage C item changes either routine: keep today's behaviour exactly (PR-48), and a visit or test that sees the timed start or Recalibrate records it as expected, not as a fault.
- The maths in `src/IMU` (`imu_calibration.h`, `trunk_stand_tall.h`, the mount M, the device config) is called, never re-derived or "corrected". When a reading looks wrong, test our code with a recorded input. Never ask the Product Owner to characterise a device.
- One arm instrument per wearing: a HackMotion OR Witmotions, never both. "One claim per role" and "first match" are the rule, not a defect to fix. A wrist HackMotion with trunk Witmotions is allowed.
- Headless tests are not evidence that a routine is usable: only the studio PC visit is. Say so in NOTES.

**8. The information model.** Edit only the rows your brief names, opened by grep at those rows (read those rows of `governance/artefacts/information_model.md` directly, by grep; never the whole file). State the fact plainly in the row; the approval goes in a footnote[^pr35]. One changelog row, with `tools/method/im_changelog_merge.py`. Say in NOTES whether the walkthrough deck shows what changed.

**9. No parked chain.** Under C-V1 the items that replaced the routines a golfer runs (C-11b.2, C-12b, C-13b, C-15) are out of scope, with C-11a to C-13a, C-11b.1 and C-17 (#291)[^c-v1]. C-14, re-scoped to the ceremony file's write with #265's fix, replaces no routine: it commits after its review like any item. No `C-routines-parked.patch` exists and none is made; nothing waits for the studio PC visit to commit.

**Every item delivers** `build/platform/C/NOTES-C-<item>.md` as the standing block (#402) says. Reply in 6 lines or fewer.

[^pr35]: The Solution Architect, 24 Nov 2026, "approve as recommended": the Rig design, §8's information-model changes and §9's answers (`docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, PR-35; `docs/design/rig_design.md`, status line).
[^c-v1]: The Product Owner and the Solution Architect, 25 Nov 2026, lever C-V1, as recommended, confirmed by the Product Owner the same night (`docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`; `docs/implementation/platform_refactor/rulings/LEVERS-U-C-D.md` §2.2; #291); AD-52 (`governance/controls/build.md`) records the exception.
[^arm-hf]: The Product Owner and the Solution Architect, 25 Nov 2026 (21:56): the arm routines' hands-free breach is accepted until #291's track, which takes #266 with it; named on #291 (`docs/implementation/platform_refactor/rulings/JUDGEMENT-CALLS.md`, "Three rulings"; `docs/design/rig_design.md` §4.8).
