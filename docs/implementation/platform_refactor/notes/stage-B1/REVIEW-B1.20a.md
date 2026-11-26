# REVIEW · B1.20a · namespace_collision_test (independent)

**Verdict: SIGN OFF.** No code change is required. One recommendation (R1) disagrees with the brief, so the coordinator or the Solution Architect rules on it.

F1–F3 are wording only and don't block. The coordinator's clerk applies them at commit time:
- F1 edits the testing guide, which is in the patch. It changes the patch hash from the studio PC-tested `264e5d3d…` but no code, so it needs no studio PC re-run.
- F2 and F3 edit NOTES, which is not in the patch.

Reviewed: `git -C build/wt-5b diff` plus the two new files, at HEAD 46a87e2e. The uncommitted tree hashes to `264e5d3dd303b949…`, the same patch the studio PC run tested.

Run (through `one_at_a_time.sh`): `ctest -R 'namespace_collision|architecture_lint|lm_session|report_lm'` gave 5/5 passed:
- namespace_collision_test (12 fixtures PASS; 1048 files; 2567 declarations, 2204 of them definitions; DagEdge anchor PASS; 0 unbalanced files; 0 collisions; no `detail` notes)
- architecture_lint_test
- lm_session_model_test
- lm_session_reductions_test
- report_lm_test

The binaries were fresh: the lm and report test binaries were built at 22:59:53, after the header edit at 22:57:23. The collision test reads the source when it runs.

## 1. The rule

- **It reads what the brief asks for.**
  - class, struct, enum (including `enum class` and `enum struct`) and `using X =`, at namespace scope;
  - nested namespaces, written either way, become one key (the planted fixture spells them both ways);
  - anonymous namespaces are skipped;
  - folders named tests, Testing, third_party and build are skipped;
  - `.cpp`, `.cc` and `.mm` files are read as well as headers.
- **It allows** forward declarations beside one definition: class, struct and an opaque enum (fixture 2).
- **It keys on the name, not the kind,** so a `struct X` in one file and a `using X =` in another are also reported.
- **Templates are read.** A primary class template (`template <…> struct W {`) and an alias template (`template <…> using X = …`) are both read (fixture 10). Only explicit specialisations are skipped, and the tree has none.
- **The 7 Oct shape is caught.** At `a327086a^`, `struct DagEdge {` sat at namespace scope in `Analysis/analysis_dag.h:91`, inside `namespace pinpoint::analysis {` (lines 79–457), next to `Diagnostics/dag_layout.h`. That is a header at namespace scope, exactly what the planted fixture models. A struct defined in a named, non-anonymous namespace in two `.cpp` files is caught the same way.

### Misses, and which are real risks

| Miss | In today's tree | Risk |
|---|---|---|
| **`detail` namespaces print a note, never a failure** | `pinpoint::analysis::detail` is opened in 8 headers in 3 folders: `Diagnostics/packs/area_regroup_table.h:28`, `Diagnostics/engine/provider_leaf_p.h:58`, `Diagnostics/engine/enum_table_p.h:44`, `Diagnostics/packs/layered_set.h:46`, `Diagnostics/measures/club_baseline_provider.h:64`, `Wrist/wrist_angle_sampler.h:55`, `Wrist/wrist_assessment_fixtures.h:134` and `Metrics/series_reduce.h:196`. No collisions there today. | **Real.** This is the 7 Oct namespace, shared across rings. A second `detail::Foo` with an inline constructor would be the DagEdge bug again, and CI would show only a note. The brief's premise that "detail is per file" is wrong in C++; the implementer says so in the header (call 1). See R1. |
| Inline free functions and inline variables (the brief covers types only) | **None today.** A rough scan of every non-test header tracked each `inline` function's namespace. Only one name is defined in two or more files in one namespace: `pinpoint::analysis::toJson`, in `swing_themes.h:2674`, `work_ons.h:919` and `diagnostic_ledger.h:2268`. Each takes different parameters, so these are overloads, not ODR. The repeated helpers (`medianOf` in 7 headers, `num` in 5, `nullValue` in 4) each live in their own namespace. Limit: the scan reads only lines that start with `inline`. | **Real but out of scope.** The 7 Oct crash went through an inline constructor, and two same-signature inline functions with different bodies in one namespace are the same mechanism. It is a follow-up issue, not this item (R2). |
| `typedef` / `union` | 6 typedef lines in all, every one in an anonymous namespace or `extern "C"` inside one (`report_mailer_win.cpp:45`, `pp_gpu_metrics.cpp:35`). No unions. | Latent only. `typedef struct {…} Name;` in a named namespace is a real ODR shape. 0 sites today. EDGE, already in the NOTES. |
| Explicit specialisations (`template <> struct hash<Foo>`) | 0 sites | Latent. EDGE. |
| Out-of-line nested definitions (`struct A::Impl {` in two `.cpp` files) | 30 qualified definitions | Low. A pimpl defined twice for one class in two `.cpp` files is the only way it happens. EDGE. |
| Types inside a class | n/a | **Not a risk.** A nested type is unique if its outer type is, and the outer type is checked. |
| Test sources (brief: excluded) | 14 test files open `pinpoint::*` namespaces. Their helper types are in anonymous namespaces or have names unused in product code. The only same names are `Rig` (product: `pinpoint::skeleton3d`) and `Outcome` (product: club_slot and others), both in other namespaces. | Low. A test helper that clashes would corrupt a test, not the app. EDGE. |
| Other file extensions | `src/` outside tests holds only `.h` (677), `.cpp` (363) and `.mm` (8) C-family files. There are no `.c`, `.m`, `.cc`, `.cxx`, `.hh`, `.hxx`, `.ipp` or `.inc` files. | None today. |
| `tools/` is not scanned (brief: `src/**`) | Tools define namespace-scope types only in `pinpoint::migrate` (`tools/migrate/migrate_step2.h`) and `pinpoint::tools` (`tools/diagnostics/owner_harness.h`). Neither namespace appears in `src/`. | Low today. A future tool type in `pinpoint::analysis` would be missed. That matters because swinglab links product headers, and swinglab was half the 7 Oct story. EDGE: widen the scan to `tools/*/src` later. |
| `#if` branches; macOS and Windows variants | Both branches are read | False positives only, already recorded as a known limit. That is the safe direction. |

No namespace-opening macros exist in `src/` (grep `#define … namespace`: none), so the `QT_BEGIN_NAMESPACE` limit costs nothing.

## 2. The fixtures

- **The 12 fixtures cover every rule the scanner applies:**
  - a planted duplicate fails;
  - a forward declaration plus its definition passes;
  - one name in two namespaces passes;
  - the global namespace counts;
  - `extern "C"` is transparent;
  - an identical alias fails;
  - anonymous namespaces are skipped and `detail` is a note;
  - nested, function-local and lambda-local types are not read;
  - comments, strings and preprocessor lines are blanked;
  - the head-parsing table;
  - an unbalanced file is reported;
  - the fixtures themselves balance.
- **The tree-side floors catch a scan that silently stops matching:** at least 500 files, at least 1000 definitions, the `DagEdge` anchor in `Diagnostics/engine/dag_layout.h`, and a check that every file balances. The rules and the tree share one `declarations()` and one `collisions()`.
- **Gap (EDGE, not blocking): nothing proves `.cpp` files are still read.** The anchor is a header. Dropping `"cpp"` from `cppExt` would probably still clear 500 files. Suggested: a second anchor on a namespace-scope struct defined in a `.cpp`, or a floor on the `.cpp` count.
- **Gap (EDGE, not blocking): a file that fails to open is skipped without a word** (`:468`).

## 3. The move to `lint_source_text.h`

- **The code is unchanged.** I diffed `git show HEAD:…/architecture_lint_test.cpp` (the old `// ── Source text` block) against `lint_source_text.h` (from `struct Source` to the end of the namespace). The only differences are four added `inline` keywords (`isIdent`, `strip`, `makeSource`, `lineAt`) and the closing `} // namespace pp_lint`. Every other byte matches.
- **The lint's diff is just the move.** It has exactly two hunks: the include at `:133`, and the block replaced by five `using pp_lint::…` lines at `:444-449`. There are no other hunks, so the NOTES line about "Lane 1's ring tables (lines 158–290) are separate hunks" doesn't match this diff (F3).
- **Linkage changes, behaviour doesn't.** The functions go from internal linkage to inline functions in `pp_lint`. Each test is one translation unit, so behaviour is the same. Clang compiles it (on the Mac) and MSVC does too (on the studio PC, where jom exited 0).
- **The lint passes** on the Mac and on the studio PC.

## 4. The `LmShotValues` fix

**Every user still gets the one definition** (grep over `src/` and `tools/`):

| User | How it gets the alias |
|---|---|
| `Gui/launchmonitor/lm_session_model.cpp:117/360/421/605/1140` | includes `lm_session_reductions.h` at `:30` |
| `Coaching/club_distances.h:239/398` | includes it at `:48` |
| `Analysis/tests/lm_session_reductions_test.cpp` | includes it at `:10` |
| `LaunchMonitor/lm_session_reductions.h:76` | the definition itself |

**Dropping the Gui header's copy was safe.** These files include `lm_session_model.h` but not the rules header: `lm_session_model_test.cpp`, `report_lm_test.cpp`, `report_lm.h` and `session_review_controller.h`, plus whatever includes those two headers. None of them names `LmShotValues`. The model header itself names it only in a comment (`:81`). The only definition left is the alias, so every user gets the same type.

**Proof that it compiles:**
- On the Mac, the whole tests tree was rebuilt (`b120a-tests2.log`, 1010 steps, exit 0).
- The studio PC's MSVC build of this exact patch exited 0.

**Deviation from PR-39: the rules header is not included. I agree with it.** PR-39 says "the Gui header includes it". The implementer didn't add the include, and the reason is sound: the header uses nothing from it, and its documented reason for staying light (lines 39–41) still holds. The coordinator should note the deviation against PR-39 when committing.

## 5. The studio PC run

- **The run is `studio PC-wt-5b-20261124-225745`, over 46a87e2e.** Its patch sha256 `264e5d3dd303b949` matches the current uncommitted tree. jom exited 0.
- **Pass 1:** 11 failed of 339. **Pass 2:** 2 failed of 43.
- **These 13 failures are the standing set.** They match the run `studio PC-wt-37c-20261124-215411` (at 12992896) exactly. Each other run the NOTES cites also has these 13, plus one more:
  - the run at fcd5ac53 (22:36) adds `diag_stage_panel_test`;
  - the run at 50ba450b (22:14) adds `paced_connect_queue_test`.
- **None of the 13 is a new failure.** On the studio PC, `namespace_collision_test` and `architecture_lint_test` passed. The count went from 338 to 339 because of the new test. The stores were unchanged. The NOTES wording needs a fix (F2).

## Recommendations (coordinator / Solution Architect)

- **R1 (I think the brief is wrong here): make a `detail` collision a failure, not a note.** C++ gives a `detail` type external linkage. `pinpoint::analysis::detail` is shared by 8 headers in 3 folders (Diagnostics, Wrist, Metrics), so the exemption covers exactly the namespace where the 7 Oct collision happened. There are 0 `detail` collisions today, so the change is free:
  - in `main()`, count `c.detail` collisions in `n` and drop the separate note loop (`:488-497`);
  - update fixture 7 to expect `failing(anon) == 1`;
  - update the header text (`:37-39`) and the testing guide.

  If the brief stands as written, the item still passes as delivered.
- **R2: file an issue for inline free functions and variables** (the same ODR mechanism as 7 Oct, outside the brief).

## Wording fixes (non-blocking; the clerk applies them at commit time)

- **F1. The testing guide line is wrong.** `docs/developer/testing_developer_guide.md` (the new paragraph) says "Anonymous and `detail` namespaces are per file". That is false for `detail`, and the test's own header says so. Write: "Anonymous namespaces are per file and not read. A `detail` repeat is printed as a note: C++ does not treat `detail` as per file." If R1 is taken, write the R1 wording instead.
- **F2. Three NOTES claims need correcting** (`NOTES-B1.20a.md`):
  - "The same 13 in wt-37c's run at fcd5ac53 and in wt-5b's run at 50ba450b": those two runs each had 14 failures. Say "13 standing failures, exactly the set of the 12992896 run; the 22:36 and 22:14 runs had one extra each (diag_stage_panel, paced_connect_queue)".
  - The users of `LmShotValues` are `lm_session_model.cpp`, `club_distances.h` and `lm_session_reductions_test.cpp`. `session_report_model.cpp` doesn't name it.
  - The tree floors say 2568 declarations and 2205 definitions. Those are the counts before the fix; after it the tree reads 2567 and 2204.
- **F3. Drop a stale NOTES sentence.** "Lane 1's ring tables (lines 158–290) are separate hunks" doesn't apply: the lint's diff in wt-5b has two hunks, both this item's.
