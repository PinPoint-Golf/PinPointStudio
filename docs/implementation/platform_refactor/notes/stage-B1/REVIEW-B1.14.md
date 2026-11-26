# REVIEW B1.14: the corridor scan's forwarder leaves Diagnostics (independent review, `build/wt-37c` over `50ba450b`)

**Verdict: SIGN OFF**, on condition that the commit carries fixes 1 and 2. Both are docs only, and nothing blocks. The rest are notes.

## 1. Built what R11 ruled
- R11 (`platform_b1_placements.md:187`): (a) the forwarder and its exception comment go beside the model browser in `src/Gui/diagnosticmodel/`, `model_browser.cpp`'s existing A5 line takes the finding under a corrected count and reason, and no line is added. That is what was built: `corridor_grid_read.h` (new, declaration only, the exception comment moved whole), with the definition and the door include in `model_browser.cpp:29-32,52-58`.
- **Allowlist:** two lines deleted (`measure_sample.cpp` A1 and A5), none added. One count goes up, `model_browser.cpp | A5 | 2 → 3`, and R11 explicitly allows it. The reason now names the door include, FILE-SHOT-07, PR-21 (b), PR-23 and R11. The tag `# not domain (§8.3)` is unchanged.
- **I counted the A5 findings myself** (lint `architecture_lint_test.cpp:686-708`):
  - the include `Export/phase_grid_store.h` at `:32` (a repository header since P-3, `:404`);
  - `QFile f(path);` at `:8254` and at `:8283`.
  - `:5441`'s `QFileInfo(...).fileName()` is blanked as path text, and `swing_paths.h` / `rule_data.h` are not repository headers.
  - So the count is **3, exact**. The lint fails a count over its line (`:1104`, `over`); a count under its line is only a printed note (`:1116`, "lower the count"). The 3 is exact by my count, whichever way the lint enforces it.
- No A1 from the new header: it includes only `Vocabulary/Analysis/phase_grid.h`, which points inward.

## 2. Behaviour unchanged
- **The body.** Old (`measure_sample.cpp`, inside `pinpoint::analysis`): `return PhaseGridStore::read(swingDir, sidecarPolicyOf(writeSidecar), cfg);`. New (`model_browser.cpp:52-58`): the same line, fully qualified, `pinpoint::PhaseGridStore::read` and `pinpoint::sidecarPolicyOf`. These are the same functions:
  - `sidecarPolicyOf` exists only in `pinpoint` (`Core/sidecar_policy.h:46`);
  - `PhaseGridStore` is only `pinpoint::PhaseGridStore`;
  - `read`'s 4th argument, `parsed`, defaults to null in both.
- **The defaults** are the same: `writeSidecar = true`, `cfg = {}`.
- **The corridor scan** (`:8529`) passes `true` and `withMsWindows({}, msWindowsOf({copy}))`, as before.
- **The gate count's fallback** (`gate_count_reads.h:46`, inside `pinpoint::diagmodel`): an unqualified `readPhaseGrid(swingDir, true)` with the default cfg, as before. `analysis::readPhaseGrid` no longer exists, so nothing is ambiguous.
- **ODR:** `diagmodel::readPhaseGrid` is defined in `model_browser.cpp` and in `session_gates_test.cpp`. No target links both: session_gates_test compiles from `SESSION_GATES_TEST_SOURCES`, which has no `model_browser.cpp`. `measure_sample_test`'s copy is `static`.
- **Callers:** `grep -rnw readPhaseGrid src tools` finds no other code caller, only comments.
- **Sidecar policy:** `ParseAndWrite` for both callers, as before. Parity against b1-base: 0 differences (NOTES).

## 3. The two test edits
- `measure_sample_test.cpp:54-60`: a file-local `static` helper, the same one line onto the door with the same defaults. The 15 call sites and every assertion are unchanged: the diff only adds lines.
- `session_gates_test.cpp:55,73-80`: it defines the declared `diagmodel::readPhaseGrid` onto the door, with the same body. No assertion is touched.
- **Note (not blocking):** I found no suite that runs the production definition in `model_browser.cpp`.
  - Before, session_gates_test ran the real forwarder in `measure_sample.cpp`.
  - None of the three targets that link `model_browser.cpp` (`model_browser_test`, `inspector_refresh_test`, `diagnostics_catalogue_integrity_test`) mentions `scanCorridor`, the gate count or `addSwingByFolder`.
  - It is one line, the same as the tests' copies, and R11's shape requires the definition to live in `model_browser.cpp`. Accept it, and log it as EDGE.

## 4. The IM
- **FILE-SHOT-07** gains: "that read is the model browser's own, beside its callers (`Gui/diagnosticmodel/corridor_grid_read.h`, defined in `model_browser.cpp`), not the sampler's[^sa24-r11]".
- **The footnote** `[^sa24-r11]` cites R11, option (a), and states the ruling as made.
- **Roles, not names:** no person's name appears.
- **One changelog row,** 1.10.7, sits after 1.10.6.
- **The Version header** reads `1.10.7, 24 Nov 2026`.
- **B1.13's work is intact:** the 1.10.6 row and `[^sa24-r10]` are there (4 mentions, the same as on origin/main).
- `[^sa24-p3b-corridor]`'s spelling `readPhaseGrid(dir, …)` is still true.
- §11 names no forwarder by name, but two of §11.1's status rows are now stale: fix 2 below.
- **Cosmetic, optional:** `[^sa24-r11]` opens "Solution Architect, …", where its neighbour `[^sa24-r10]` opens "The Solution Architect, …".

## 5. The EDGEs
**Fix 1 (in this commit; docs only, stale since P-3, and made more wrong by this move):**
- `docs/developer/swing_export_developer_guide.md:704-705`.
  - Now: "`readPhaseGrid` /\n  `readLivePhaseGrid` take the same policy."
  - Make it: "the phase grid's door\n  (`PhaseGridStore::read`, `src/Export/phase_grid_store.h`) takes the same policy."
- `docs/developer/diagnostics_developer_guide.md:182` (one long line), inside the parenthesis "(Refactor 3.7-0 split that one bool: …)".
  - Now: "`readPhaseGrid` / `readLivePhaseGrid` also take a `SidecarPolicy`"
  - Make it: "the phase grid's door, `PhaseGridStore::read` (`src/Export/phase_grid_store.h`), also takes a `SidecarPolicy`".
  - The sentence's "The bool form keeps its meaning" is still true: the bool form is now `diagmodel::readPhaseGrid`.

**Fix 2 (in this commit; IM §11.1 status edits, §1.2's §11 exclusion, so no new ruling is needed):**
- **The P11 row,** `information_model.md:1207`.
  - Now: "`measure_sample.cpp`'s A1 and A5 lines stay (the phase grid)[^built-7-1]".
  - Make it: "`measure_sample.cpp`'s A1 and A5 lines stayed (the phase grid)[^built-7-1]; B1.14 deleted both, when the folder read moved to the model browser, under `model_browser.cpp`'s A5 line[^sa24-r11]".
- **The "Rules without file I/O" row,** `information_model.md:1272`. It has been stale since P-3, and after this move it is wrong.
  - Column 3, now: "Not yet: the phase-grid sampler (`Diagnostics/measures/measure_sample.cpp`) still reads swing documents through `SwingStore` and writes its sidecar (FILE-SHOT-07) from `Diagnostics`, because the model browser's corridor and gate scans read it by folder with no `Shot` door (proposed, for the Solution Architect); words out of reductions (C-04)".
  - Make it: "The phase-grid sampler (`Diagnostics/measures/measure_sample.cpp`) does no file work and includes no repository: the read and the sidecar are `PhaseGridStore`'s (P-3)[^sa24-p3], and the model browser's folder read is its own (`Gui/diagnosticmodel/corridor_grid_read.h`, B1.14)[^sa24-r11]. Not yet: words out of reductions (C-04)".
  - Column 4, now: "the phase grid (`measure_sample.cpp`; P-3, for the Solution Architect): the platform refactor (#224); words out of reductions (C-04): #224[^po-224]".
  - Make it: "words out of reductions (C-04): #224[^po-224]".
- **The 1.10.7 row** adds: "§11.1 status edits: the P11 row and the rules-without-file-I/O row".

**Checked and clean:**
- `testing_developer_guide.md:718` says that `measure_sample_test` pins the policies of "`readPhaseGrid`". That is still true: the suite's own helper keeps the name.
- No other developer guide says that the sampler includes the door.

**Optional comment-only mentions, pre-existing, not required:**
- `src/Vocabulary/Analysis/phase_grid.h:49`: "(readPhaseGrid)" means the door's union rebuild, so `PhaseGridStore::read` is more exact.
- `tools/diagnostics/measure_dump_core.h:106`: "readPhaseGrid with SidecarPolicy::ParseNoWrite". It was already stale, because the forwarder never took a policy.
- `live_measure_source_test.cpp:46,50,140` and `src/Analysis/tests/CMakeLists.txt:1640` describe the door's behaviour under the old name. They are harmless.
- IM `:1230`'s `readLivePhaseGrid` is a historical step 3.3 status row. Leave it.

**Link lines: harmless.**
- `phase_grid_store.cpp` stays a source on 17 link lines in `src/Analysis/tests/CMakeLists.txt`.
- Where only the sampler needed it, it is now just dead code in that target: one definition, so no ODR clash, and its dependencies still resolve, since both trees built.
- It is still needed where the target is `measure_sample_test` (which calls the door directly), `session_gates_test` (its own definition) or a `model_browser.cpp` target.
- Pruning it is build-time hygiene only. Keep it as EDGE.

## 6. The studio PC ran before the merge with B1.13
- **It doesn't matter.** `631a5282..50ba450b` (B1.13, 13 files) shares no code file with this change.
- The only shared files are:
  - the IM (the conflict was resolved; both footnotes and rows are present);
  - `architecture_allowlist.txt` (different lines: B1.13 deleted `swing_reanalyzer.cpp`'s A1);
  - `architecture_lint_test.cpp` (B1.13 changed a ring table entry and a comment; B1.14 changed a comment).
- The lint is a text scan that behaves the same on every platform, and it passes on the merged tree on the Mac.
- No file this change touches includes `recorded_products`.
- No studio PC re-run is needed.
- One caveat: the studio PC tested today's code files only if, as NOTES says, nothing but the IM conflicted on the re-apply. The patch is deleted, so I can't confirm that. The Mac's build of the merged tree and the 6/6 below cover it.

## Re-run here
- `one_at_a_time.sh ctest --test-dir build/wt-37c/build/tests -R 'measure_sample|session_gates|model_browser|architecture_lint|design_system' -j 3`: **6/6 passed** (measure_sample_test, session_gates_test, model_browser_test, architecture_lint_test, design_system_lint_test, design_system_tokens_check).
- The binaries (22:15) are newer than every changed source (22:14), so they are the merged tree's.
