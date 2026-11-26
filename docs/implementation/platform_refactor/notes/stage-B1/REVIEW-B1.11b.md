# Review: B1.11, the last step (`Core/` becomes Platform), AG-21

Reviewer: independent agent, 26 Nov 2026. Read-only on `build/wt-37c` (detached at origin/main `df996920`, 3 files modified).
Ruling: R4 part 2, `docs/implementation/platform_b1_placements.md:62-76`.

## What I checked myself

- **No real file changes ring.** `src/Core` holds 21 stems (plus `CMakeLists.txt` and `tests/`; the lint skips `tests/`).
  Against `git show HEAD:src/Gui/tests/architecture_lint_test.cpp`, every one of the 21 had a `fileRings()` entry: 20 Platform
  and `device_enumerator` Adapters. After the change, the 20 fall to `Core/` → Platform (`architecture_lint_test.cpp:169`) and
  `device_enumerator` keeps its entry, Adapters (`:206`). Ring for every file: unchanged. `fileRings()` entries under `Core/`:
  21 → 1.
- **The exception still holds.** `device_enumerator` resolves to Adapters (`:206`), and A8 still names it by stem
  (`isAcquisitionOrAppState`, `:411-412`, with `App/studio_profile` and `pp_settings`).
- **The fixtures test the flip.** `:765-769`: `Core/x.cpp` including `Vocabulary/Metrics/metric_catalogue.h` must give one A1
  "Platform → Vocabulary". On the old table `Core/x.cpp` is Vocabulary, so Vocabulary → Vocabulary gives no A1 and the check
  fails. `ringOf("Core/zz_new_file.h") == Platform` (`:865`) fails on the old table too (Vocabulary). The `device_enumerator`
  halves (`:767`, `:865`) pass on both tables. They guard the exception: delete its entry and both fail (Platform → Adapters,
  ring ≠ Adapters). Both headers are in the fixture tree (`:724`).
- **`context_check.py`'s deletions are all redundant.** Imported old (HEAD) and new `tag()` and compared them over all 1055
  C++ files under `src` (tests excluded), ruled and true tags: **0 differences**. `App/studio_profile` → HUB by `'App/': HUB`
  (`:46`). The full outputs on the same tree are byte-identical (`cmp`).
- **Lint passes.** I ran `tools/method/one_at_a_time.sh ctest ... -R '^architecture_lint_test$'`: Passed. The test binary
  (03:33:47) is newer than the edited source (03:32:56). `b111b-plants.log` shows the planted Core files failing as they
  should (A1 Platform → Vocabulary / Rules / Adapters, ctest exit 8) and `device_enumerator.cpp` + an Adapters header passing.
  `git status --untracked-files=all` lists only the 3 files, so the plants are gone, and `device_enumerator.cpp` matches HEAD
  (restored by `cp`; its mtime moved, so the next incremental build recompiles it).
- **Parity:** `b111b-parity-diff.log`, 0 differences.
- **IM.** Version line 1.12.10 (`information_model.md:6`) follows main's 1.12.9 (origin/main is still `df996920`).
  RING-PLATFORM (`:435`) gains "`Core`, but for `device_enumerator`". RING-VOCABULARY (`:436`) loses "the rest of `Core`",
  with its punctuation intact. The spanning table's `Core` row (`:451`) and the tail of `[^sa24-r1-4]` (`:1986`) match each
  other and the lint comments (`:204-205`). RING-RULES and RING-ADAPTERS were done in 1.12.8 and still read correctly. Nothing
  the brief's Docs paragraph names is left stale. The deck needs no change (slide 11's "spans rings" still holds for `Core`).
  The change-log row (`:2098`) has the four columns, sits after 1.12.9, cites the ruling through `[^sa24-r1-4]`, and gets
  "twenty" right. It is a C9 change (IM §1.2), ruled by the Solution Architect.

## Findings

1. **edge**: `build/platform/B1/b111b/commit-msg.txt` ("The full suite ran on the studio PC"). `b111b-studio PC.log` ends at
   "ctest pass 1 … 3:36:36" (file last written 03:36:42; it was 03:40 when I checked), and NOTES §9 records no studio PC result.
   So the message claims a result nobody has read yet. Before committing, read the studio PC summary, then give its exit code and
   any red suites in the message (the earlier B1.11 run exited 12).
2. **edge**: `src/Gui/tests/architecture_lint_test.cpp:91`, "so 03 has them inside Vocabulary". 03 §2 gives logging and
   profiling their own ring 0, Platform, "inside" Vocabulary in the sense of nested within it
   (`03-target-architecture.md:54, 234, 245`). The new wording reads as though 03 puts them in the Vocabulary ring. The
   clause before it, "Platform (ring 0) is a decision of this test, not of 03", was already stale and is kept. Suggested
   wording: "Platform is ring 0, inside Vocabulary (03 §2)".
3. **edge**: `governance/artefacts/information_model.md:2098`. The row's list of what changed names RING-PLATFORM,
   RING-VOCABULARY and the spanning table, but not the edit to the tail of footnote `[^sa24-r1-4]` (`:1986`). 1.12.7's row
   does name its footnote.
4. **edge**: `governance/artefacts/information_model.md:436`. RING-VOCABULARY still calls `Vocabulary/Common`'s files
   "Core's other vocabulary files", in both the Context and the Directories cells. Now that the row says Core holds no
   vocabulary, the phrase reads like a leftover; "the vocabulary files that came from `Core`" would be clearer. The wording
   is older than this change, and the brief doesn't name it.

No blocking findings. Every rule the brief named is followed: the R4 ruling exactly as written, GIT-6 (the test and the doc in
the same change as the table), and no real store reachable.

**Verdict: SIGNED OFF** (no blocking findings). Condition on the commit: finding 1, read the studio PC result before the message
claims it.
