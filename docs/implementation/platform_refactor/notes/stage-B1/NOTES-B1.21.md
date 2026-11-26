# B1.21 · Lint A9: R-CTX-1 in architecture_lint_test (26 Nov, wt-37c at da6c2360, before B1.19's move)

**State: built and green on the Mac. A9 finds 0 on the tree; there are 0 allowlist lines for it, and the list refuses
one.** Builds: 2 of 2. No studio PC run (that is the coordinator's), no commit. R13 is ruled as LAUNCH-B-close.md says
(five contract headers; `Vocabulary/Rig` is Acquisition), so A9 is whole and switched on.

**Check first:** `context_check.py --src build/wt-37c/src --list c` gives (c) 0, (a) 978, (b) 65, and 0 untagged files.

## What changed
- `src/Gui/tests/architecture_lint_test.cpp`:
  - `:83-95`: A9 in the header.
  - `:133-134`: A9's known limit: it reads direct includes only.
  - `:293-461`: the contexts:
    - `Context` and `contextOrder()`.
    - `dirContexts()` `:314`, with one `Diagnostics/` entry at `:339`, and `fileContexts()` `:363`, with the drill pack at `:374`. Both come from IM §4.1 plus R13's placements.
    - `contextOf()` `:407`.
    - `hubContracts()` `:425`: the five headers, each with why it meets Q26's test.
    - `contextBreach()` `:446`: the three clauses.
    - The crossing counter.
  - `:739-745`: A9 in the include scan.
  - `:1047-1095`: the fixtures, described below.
  - `:1216-1240`: two new checks in `main()`. Every C++ file has a context, and no entry of the context tables or of `hubContracts()` is stale (the empty `Coaching/` is the one exemption).
  - `:1251`: the rule loop now runs to A9.
  - `:1282`: prints the crossing count.
  - `:1123`: `readAllowList()` still accepts only A1–A8, so an A9 line is refused as malformed.
- `src/Gui/tests/CMakeLists.txt:578-591`: the comment names A9; `pp_requirement(architecture_lint_test CTL-BT-40)`.
- `src/Gui/tests/architecture_allowlist.txt:4-9`: the header names A9 and says it never has a line.
- `governance/artefacts/rtm_baseline.txt`: dropped `4 architecture_lint_test`, which is now labelled, so its gap is closed.
- `governance/artefacts/rtm.md`: regenerated.
- `governance/artefacts/rtm_platform_tests/macos.txt`: retaken (433 tests).

## Tests (Mac, through the lock, `-j 3`)
- **Lint suite:** `architecture_lint_test`, `design_system_lint_test`, `store_isolation_lint_test`, `session_setup_lint_test`, `qml_globals_lint_test`, `im_excerpt_selftest`, `im_changelog_merge_selftest`, `rtm_check`, `rtm_selftest`: 9/9 pass.
- **The stale binary:** `session_setup_lint_test` first failed on a binary built at 09:32, older than its source in da6c2360 (the perspective move). It passes once rebuilt, which was build 2.
- **Fixtures:** one pass and one fail per clause. Each failing case is checked to fail for its own clause: its message must hold "R-CTX-1" and the clause's words.
  - (1) The order: Acquisition → Diagnostics and Diagnostics → Coaching fail. A later context including an earlier one passes, and so does a commented-out include.
  - (2) The hub: `DomainModel/session.h` fails. A contract passes, and so does the hub including every context.
  - (3) The base: Shared → `Vocabulary/Analysis` and Platform → IMU fail. Shared → Shared and Platform pass, and so does a context including both.
  - A table check: file entries win over directory entries, and a new directory or Vocabulary area has no context.
- **Allowlist refusal, proven:** one planted line `Video/zz_plant.cpp | A9 | 1 | …  # kept` turned the test FAIL ("allow-list line 172 is path | rule | count | WHY …"). Removed; PASS again.
- **Tests skipped:** A9 runs in the same scan as A1–A8, which skips every path holding `tests`, `Testing`, `third_party` or `build` (`main()`'s `skipParts`), so the six Diagnostics tests that include `../../Gui/` are never read.
- **Three plants**, in scratch files: `Video/`, which is Acquisition, including a Diagnostics header; `Diagnostics/derived/` including `DomainModel/session.h`; `Vocabulary/Common/` including `Vocabulary/Analysis/phase_grid.h`. Each failed A9 with an R-CTX-1 message, and the test went FAIL. All three are removed, and `git status` is clean of them.
- **Cross-check:**
  - **Crossings:** the lint reads 981 includes that cross a context. On `context_check.py`'s ruled tags the same count is Platform 241 + Shared 150 + hub composes 534 + Diagnostics → Acquisition 53 + hub contract 3 = 981. The script itself prints only 1,043 "crossing a context": any row that crosses on its ruled or its "true" tags, which undo the two deferrals. To reproduce 981 from the script, from wt-37c: `python3 -c "import sys,collections;sys.argv=['x','--src','src'];sys.path.insert(0,'tools/refactor');import context_check as c;f=c.walk();b=collections.defaultdict(list);[b[r.rsplit('/',1)[-1]].append(r) for r in f];print(sum(1 for r in f for l in open(f[r],errors='replace') for m in [c.INC.match(l)] if m for t in [c.resolve(f,b,r,m.group(1))] if t not in (None,'?') and c.tag(r)!=c.tag(t)))"`.
  - **Rejections:** the lint has 0, and `context_check.py`'s today's rule on ruled tags has 0.
  - **Shared and Platform** never include a context today (my scan), so clause (3) adds no rejection that the script lacks.

## A9 table lines that hold a `src/Diagnostics/<sub>/` path (B1.19's move re-paths these)
- `architecture_lint_test.cpp:374`: `Diagnostics/packs/drill_pack` (`fileContexts()`).
- `architecture_lint_test.cpp:1091`: the fixture `contextOf("Diagnostics/packs/drill_pack.h")`.
- Every other Diagnostics key is `Diagnostics/` itself or under `derived/`, which B1.19 does not move.
- Proven: a dry run of a one-line scratch map (`src/Diagnostics/packs/drill_pack -> src/Diagnostics/content/`) lists both `:374` and `:1091` under "path mentions, lint", so `move_files.py` needs no change. Were `:374` ever left behind, the stale-entry check would turn the lint red, not pass it.

## Docs
- **IM** (by hand; the tool row is v1.12.27):
  - `:131` and the §1.4 A9 row `:148`: what A9 checks, with the five-header set.
  - `:216`: "has no allowlist line".
  - §4.1's opening `:429-430`: the lint reads the column, and every row is placed.
  - RING-RULES: `Body`, `Wrist` and `Skeleton3d` are Diagnostics.
  - RING-ADAPTERS: the "to place" list is placed as R13 ruled.
  - A new footnote, `[^sa24-r13]`.
  - The version line, and one changelog row.
  - GL-43 doesn't mention the lint, so it is unchanged.
- **The walkthrough deck:** the Product Owner must refresh it, because two slides say "A9 … lands only with Stage B's folder moves".
- **Elsewhere:**
  - `governance/controls/test.md:80`: A1–A9.
  - `docs/developer/testing_developer_guide.md`: A9 in "The rules", plus a new paragraph, "Placing a new directory" (a ring and a context, each from §4.1, ruled first).

## Judgement calls, stores, EDGE
- **Label:** no control or requirement names the architecture lint. I labelled it `CTL-BT-40` ("an oracle shown able to fail", which the fixtures-first design proves), as `quit_pins_selftest` is labelled. A better id needs the Solution Architect.
- **Platform:** Platform including a context is rejected under clause (3), as `context_check.py` rejects it. A1 also catches it by ring.
- **IM status rows:** grepped for `R-CTX-1`, "unchecked" and "not checked": no §11 row says the direction is unchecked.
- **Outside the brief's Docs list:** `governance/controls/test.md:80` (the checks table said "A9 lands with Stage B").
- **Tree state:** most of wt-37c's test binaries predate da6c2360 (`session_setup_lint_test` did), so the review's or the studio PC's full suite needs a full build.
- **Real stores:** none are reached. The lint reads source text only, and no app or device runs.
- **EDGE:** the context table now exists twice, in the lint and in `context_check.py` (`DIRS`/`FILES`, which also has a stale guessed contract set of 14). The crossing cross-check is the only guard against drift; issue candidate: retire the script's tables, or have it read the lint's.
