# B1.21 review · Lint A9 (R-CTX-1) · wt-37c at da6c2360, uncommitted (26 Nov)

**Verdict: SIGNED OFF.** There are no BLOCKING findings, and five EDGE findings, listed below. The independent reviewer (AG-21) read the brief, the B1.21 section of LAUNCH-B-close.md (which overrides the brief), R13 (placements lines 194–225), the NOTES and the IM rows. The reviewer ran every check below and edited no tracked file. The scratch work is in `/private/tmp/claude-501/-Users-markliversedge-Projects-PinPointStudio/e6a2aab6-6ce5-4b74-9800-1e8c7e7443e0/scratchpad/`.

## The accept bullets
- **A9 findings on the tree: 0.** From `ctest -R '^architecture_lint_test$' -V`: "A9 … none outside the allow-list (0 allowed in 0 files)", and "A9 read 981 includes that cross a context".
- **Allowlist lines for A9: 0** (`grep '| A9 |'` finds none).
- **The list refuses an A9 line, seen red.** A scratch copy of the lint, built against a scratch copy of the list with one A9 line added, failed with "allow-list line 172 is path | rule | count | WHY …". The tracked list was not touched.
- **One passing and one failing fixture per clause, each failing for its own clause, seen red (BT-41).** Four mutants of the lint, built in scratch and run on the real tree:
  - m1, the order clause off: only "A9 catches an earlier context including a later one" fails.
  - m2, the hub clause off: only "A9 catches a context including a hub header that is not a contract" fails.
  - m3, the Shared/Platform clause off: only "A9 catches a Shared file including a context's file … and Platform the same" fails. With this clause off, the Shared case falls through to the order clause, which still reports one A9. The fixture's word check ("never depend outward") is what catches it.
  - m4, `isHubContract()` always true: only the hub fixture fails.
  - Every passing fixture's include resolves inside the fixture tree, so none of them passes vacuously.
- **Every directory under src has a context or is Platform.** The lint checks every C++ file (1,073 of them) and checks for stale entries. Both pass. `Resources` and `Shaders` hold no C++.

## The tables against R13
- **Every file's context matches the script's ruled tags: 0 differences over all 1,073 C++ files.** The lint's `dirContexts()`/`fileContexts()` were parsed from the source and compared file by file with `context_check.py`'s ruled tags (scratch `a9x.py`).
- **The tables against R13's rows, row by row:**
  - Diagnostics: `Body`, `Wrist`, `Skeleton3d`, `LaunchMonitor/inferred` and `kf3_smoother` (in `Kinematics`).
  - Acquisition: the connectors, `Audio` (input and detector), the 12 IMU device back-ends and `Core/device_enumerator`.
  - Domain + Application: `LLM`, `STT`, `TTS`, `Secrets`, `Update`, the three Audio output files, `Gui/app/app_info`, `Pose/motion_capture_probe` and `Analysis/mp4_frame_reader`.
  - Platform: `pp_settings` and R1's three headers (`Core/`).
  - `recorded_products` is in `Vocabulary/Analysis`, which is Diagnostics.
  - `Vocabulary/Rig` is Acquisition. The 14 IMU Acquisition files match RING-RULES as it stands on HEAD.
- **The contract set is exactly the five headers** (`recording_source`, `recording_reader`, `recording_repository`, `shot_command_values`, `session_input`).
- **A9 allows nothing that R13 or R-CTX-1 forbids.** In some places it is stricter: Platform or Shared including the hub fails, and so does Platform including any context.
- **The deferrals sit in the tables, not in the rule:** `Gui/` is the hub, and the drill pack is Diagnostics.

## The cross-check
- **Crossings: the NOTES command gives 981 on wt-37c**, the same as the lint. My own re-implementation also gives 981, with 0 ambiguous includes skipped. By pair:
  - hub → Diagnostics 384, Platform 174, Acquisition 150, Shared 67;
  - Diagnostics → Shared 67, Acquisition 53, Platform 41;
  - Acquisition → Platform 26, Shared 16;
  - Diagnostics → hub 3 (2 × `recording_source.h`, 1 × `shot_command_values.h`).
- **Rejections: 0.** That is the script's "today's R-CTX-1 on ruled tags" and also my re-implementation with the five-header set and the Shared clause. `--list c` gives 0.
- **No Shared or Platform file includes a context today.**

## The rest
- **Tests skipped.** `skipParts` drops every path with a `tests` part. There are 11 files in `src/Diagnostics/tests` that include `../../Gui/` (the brief says six; its count is stale), and the lint reads none of them.
- **The Mac run:** `ctest --test-dir build/wt-37c/build/tests -R 'architecture_lint|design_system_lint|rtm_|artefact_paths|im_' -j 3`, through the lock, passed 8/8. The binary was newer than its source, so nothing was rebuilt.
- **The RTM:** `make_rtm.py --check` in the worktree says `rtm_check: ok`, with flag 4 at 426 and 0 new gaps.
  - The baseline loses one line, `4 architecture_lint_test`.
  - `rtm.md` has the CTL-BT-40 row and the labelled-tests row.
  - `macos.txt` changed only its header commit, since no test was added (433 tests).
- **The docs:**
  - The IM's §1.4 A9 row, the `A1…A9` id row, line 216, §4.1's opening, RING-RULES and RING-ADAPTERS, the footnote `[^sa24-r13]`, the version line and changelog row 1.12.27 (in order after 1.12.26) all trace to R13 or the four contexts' ruling.
  - "A new directory … fails" traces to DEC-148 (a new directory is C9).
  - `test.md:80` and the developer paragraph trace to the same rulings.
  - No stale "lands with Stage B" or "to place" wording is left.

## Findings
1. **EDGE: the label `pp_requirement(architecture_lint_test CTL-BT-40)` is only half true** (`src/Gui/tests/CMakeLists.txt:591`).
   - The true half: `selfTest()` proves each rule can fail, so it checks BT-40's concern for its own rules. That is the pattern `quit_pins_selftest` is labelled for.
   - The other half: the test's main job, enforcing IM §1.4's A1–A9 on the tree, is a control nobody has named, and the label doesn't cover it.
   - It is not false, and taking it off later is easy: swap it for a ruled id, and no baseline line has to come back.
   - **Fix:** file an issue for the Solution Architect to give the architecture rules a citable control id (or to allow IM rule ids in `pp_requirement`), then relabel.
2. **EDGE: nothing but a comment keeps an A9 allow-list line out** (`architecture_lint_test.cpp:1123-1124`).
   - The list refuses an A9 line only because `ruleRe` is `^A[1-8]$`, and the refusal message is the generic "malformed" one.
   - A later edit widening the pattern to `A[1-9]` would quietly allow A9 lines, and no fixture pins the refusal.
   - **Fix (issue):** a `selfTest()` case that feeds `readAllowList()` an A9 line and expects it refused, plus a message that names R-CTX-1.
3. **EDGE: IM §4.1's RING-ADAPTERS wording is narrower than the lint's Audio placement** (`information_model.md:440`, `architecture_lint_test.cpp:347`).
   - The row says "Acquisition, `Audio`'s input and the acoustic shot detector".
   - The lint puts all of `Audio/` in Acquisition except the three output files. So `audio_processor_base`, `audio_stream_saver`, `AudioConverter` and `onset_detector` are Acquisition by the directory default.
   - **Why this is EDGE and not BLOCKING (the four files traced, excluding tests):**
     - `onset_detector` is included only by `acoustic_shot_detector.h`, which is on the input side.
     - `audio_processor_base` is included by `audio_input_base.cpp` (input) and by `STT/stt_processor.h` (the hub).
     - `audio_stream_saver` is included only by `Gui/media/transcription_controller.cpp` (the hub).
     - `AudioConverter` is included only by `STT/stt_processor.cpp` (the hub).
     - None of the three output files includes any of them, and no context outside Audio includes them. So the directory default allows no include that R13 or R-CTX-1 forbids.
     - Two of the four belong to the input R13 placed. The other two are reached only from the hub, which may include anything.
     - R13's list of the nine includes A9 rejects (placements lines 216–219) has no Audio entry, and that check ran with `Audio/` as Acquisition.
   - **Fix (issue):** reword the row as "`Audio`, but for its output (…)", or name `audio_stream_saver` and `AudioConverter` by a ruling if they belong with STT.
4. **EDGE: the context table exists twice** (`tools/refactor/context_check.py:44-71, 104-111, 128`).
   - The lint's table and the script's `DIRS`/`FILES` agree on every file today (0 differences).
   - The script's own text is now stale: "THIS SCRIPT'S placements" (now R13's), the 14-header `HUB_CONTRACT` guess (R13 ruled 5), and `today()` passing Shared → any context as "unruled" (A9 rejects it).
   - Used as a cross-check of rejections, the script would under-count.
   - **Fix (issue, as NOTES proposes):** retire the script's tables or have it read the lint's, and align `today()` with A9.
5. **EDGE: the brief says "six Diagnostics tests include ../../Gui/"; today there are 11 files** (10 tests and `owner_rig.h`). All of them are skipped, so nothing changes. **Fix:** none needed for this item; correct the count in any later brief that relies on it.

## How the scratch checks ran
- The four mutants and the scratch-list binary were compiled through `tools/method/one_at_a_time.sh` (the scratch `build.sh` and `build_base.sh`).
- The five binaries were then run directly, outside the lock. Each reads source text only, takes about 3 s, and writes nothing. ctest wasn't used because these binaries are not ctest entries.

## Not checked here
- The studio PC's full suite (both passes): this falls to the coordinator after this review. Per NOTES, most of wt-37c's test binaries are older than da6c2360, so that run needs a full build first.
- The walkthrough deck: the Product Owner must refresh it, as NOTES says.
