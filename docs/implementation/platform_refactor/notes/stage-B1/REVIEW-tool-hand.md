# Review (AG-21): tool-hand, commit cb015d0e (`hand:` in tools/refactor/move_files.py)

Reviewer: independent agent, 26 Nov 2026. Read only on the code. Line numbers are `tools/refactor/move_files.py` at cb015d0e (build/wt-stack).

## What I ran

- `python3 -I tools/refactor/move_files.py --self-test`: **PASSED (0 failures)**. All 20 `hand:` cases and every older case are `ok`.
- B1.7, declared (`B1.7-shared-hand.map`, `--verify 008b8eb4`, `--allow-prose` on the IM and the two developer guides only): **other lines 0, VERIFY PASSED**, exit 3 (NOT CONFIGURED), the same 9 listed lines as the notes.
- B1.7, undeclared (the committed `tools/refactor/maps/B1.7-shared.map`, same flags): **other lines 115, VERIFY FAILED**, exit 1.
- Loophole probes: throwaway git fixtures in a `tempfile.mkdtemp` folder, removed afterwards. Each copies the self-test's `hand` fixture (big.h, blocks 7-11 and 14-18, the clean split passing), applies one change, and runs `--verify <base> --no-configure-ok`. The mutations are given inline below.

## Definition of done

1. Declared split accepted, anything else fails: **not met**. B1 and B2 below let verify pass real non-move changes.
2. A `hand:` line that matches nothing is refused: met (self-test, and by reading `hand_errors` at 234 and the verify block at 2080).
3. The four required self-test cases: met. The cases don't cover the loopholes below.
4. B1.7 re-verified: met. My re-run passes. See E7 for the stale evidence file.
5. Tools and tests only, tests and docs in the same commit: met. The commit message follows GIT-8 (`Tools:` is an existing area; the title is long but the log has longer ones), GIT-9 and GIT-10 (no trailer). But it repeats the claim B1 shows to be false: "an include guard" only.

## Blocking

**B1 (blocking). The new file's "scaffold" can switch the taken lines off, or define a macro, and verify passes.** `scaffold_lines` (357-372, the test at 366-368) accepts any lone `#ifndef X` as scaffold: it never checks that a `#define X` follows. It accepts any guard name. `#endif` (SCAFFOLD_RES, 295-297) is always scaffold, with no pairing. Lines inside a `/* … */` comment are blank after stripping, so they count as scaffold. `check_hand_file` (300-355) finds the taken blocks byte for byte, but never checks that they are live code in the new file. The comment state, the preprocessor-conditional depth and any line splice in front of them are all unchecked. Probes, every one **VERIFY PASSED**, rc 0:
- P2b: `#ifndef NDEBUG` before the first block, `#endif` after the last. The code builds in Debug and vanishes in Release.
- P2: the same with `#ifndef __cplusplus`. The code is gone everywhere.
- P3: `#ifndef PP_FAST\n#define PP_FAST\n#endif` after `#pragma once`. This defines a feature macro in every file that includes the source, which is exactly what the docstring's `#define` ban (360) says it prevents.
- Q3: `hand: src/Recording/types.h <- src/Analysis/big.h:6`, where line 6 is one blank line. The new file is `#ifndef PP_FAST_PATH / #define PP_FAST_PATH / #include "rec.h" / namespace pp::analysis { / (blank) / } / #endif`, and big.h includes it. A header that moves no code, but defines a macro and pulls in another header, passes.
- P1: a `/*` line before the blocks and a `*/` line after them. The taken code is commented out.
- P7: blocks 7-11 and 15-17, with the scaffold comment `// note \` right before the code line `struct Track {`. The line splice turns that line into a comment.

The build catches some of these. It doesn't catch a dropped specialisation, overload or `static_assert`, and it doesn't catch P2b in Debug or P3 at all. Suggested fix:
- Drop include-guard support. `src/` uses `#pragma once`; only 3 headers there have `_H` guards. If guards stay, accept only a first-directive `#ifndef G` / `#define G` pair whose `#endif` is the last directive and whose G appears nowhere in the base tree.
- Accept no other `#if…`/`#else`/`#endif`.
- Require each taken line's `spans_cpp` classes in the new file to equal its classes in the source, so that a block can't start inside a comment.
- Refuse a scaffold line that ends in `\`.
- Require a `hand:` range to take at least one code line.

Add a self-test case for each.

**B2 (blocking). The source's added include of the split is unchecked after its path, so it can swallow source lines.** `hand_include` with `old=None` (2095-2105) checks only that the quoted path resolves to a split of the source. `INC_RE` (85) captures anything after the closing quote (`(".*)$`, with `re.S`). The `nj` filter (2200-2209) then drops that line from the diff, and the include pass (2253-2258) drops it from the count. The lines that follow are unchanged in text, so the diff sees nothing. Probes, every one **VERIFY PASSED**:
- P4: in big.h, `#include "../Recording/types.h" \` placed right before `inline int f() { return 1; }`. The splice makes `f()` part of the include directive, where it is ignored.
- P4b: a big.h with a later `/* a later comment */`, and `#include "../Recording/types.h" /*` placed before `f()`. Everything up to the later `*/` is commented out.

The re-pointed-include path (2106-2109) is safe here: it demands the same trailing comment, give or take spacing. Suggested fix: after the closing quote, allow only nothing or a `//` comment that doesn't end in `\`. Apply the same rule to the new file's include and comment lines (B1). Add self-test cases.

## Edge

- **E1 (edge). Nothing requires the split to be reachable, or included at the global scope.** P5 passes: the source doesn't include the new file, no includer is re-pointed, and no build line is added, so the taken blocks are deleted from every translation unit. P8 passes: the source's include sits inside `namespace pp::analysis {`. The build usually catches these, but not a removed specialisation, overload or `static_assert`. The rules are at 2080-2093 and 2104-2105. Suggest requiring each source to include each of its splits outside any brace or conditional, as B1.7 does at swing_analysis.h:44.
- **E2 (edge). The new file can include another header through an include directory.** An angle include (`#include <Core/log.h>`) is scaffold (SCAFFOLD_RES 295-296), but `INC_RE` matches quoted includes only, so its resolution check (339-342) never runs. P6 passes. The README's "never through an include directory" is untrue for `<…>`.
- **E3 (edge). The new file may include any existing header, even one its source never included.** Q2 passes: `#include "rec.h"`. Such a header's macros now reach every file that includes the source, the same threat as the `#define` ban. The new file needs its own includes, and the summary line tells the reviewer to read the scaffold, so I've tagged this edge. Suggest listing each untaken include on its own, or limiting them to targets the source included in the base, plus system headers.
- **E4 (edge). A build line naming the split is accepted in any build file and any command.** `hand_build_line` (2122-2135). P9 passes: the header is added to an unrelated `add_executable(other …)`. That's harmless for a header. A `.cpp` split (CPP_EXT allows it) would add a translation unit to another target. Suggest limiting it to the command that lists the source, or to headers only.
- **E5 (edge). Any includer of the source may be re-pointed at the split, and it loses the source's other declarations (P10).** This is the declared rule, and it's listed. The build catches most of it. A related path: at 2270-2273, an unchanged spelling whose resolution flips from the source to a split that shares its basename is listed as re-pointed. That case is rare. The shadowing case where an unchanged include of another file flips to the new file (Q1, new file `src/Gui/Core/log.h`) correctly **fails**.
- **E6 (edge). A taken block that holds a quoted `#include` always fails the include count (2260).** The base's include stays in `olds`. The taken-include branch of `check_hand_file` (343-349) can therefore never lead to a pass. This is the safe side, but the limit in the notes is narrower than the real one ("a taken include that the map would also rewrite"). Found by reading, not probed. Fix the doc, or drop taken lines from `olds`.
- **E7 (edge). The B1.7 evidence is stale.** `tool-hand-B1.7-verify.txt` lists the tool with "407 lines differ". At cb015d0e it is 419, so the file came from a pre-final tool. My re-run gives the same verdict. Regenerate the file with the committed tool.
- **E8 (edge). `--allow-prose` on a split's source is only discouraged.** The README says don't. The tool still honours it (2189), and that waives the check that proves the taken lines left the source. Suggest refusing it.
- **E9 (edge). "May gain one include" is not enforced.** Every include of the split in its source is filtered (2202, 2253-2258). Duplicates are harmless, but the docs say one.
- **E10 (edge). "Whole" means only that the braces balance (316).** A block may split a declaration, for example a lone `static`, `template <>` or attribute line. The build normally catches that. Combined with E1, it may not.

## No regression in the old checks

With no `hand:` line, `splits_of` and `hand_new` are empty, so `hand_include` and `hand_build_line` return None, and `oi`/`nj` hold every line. The diff and the include pass behave exactly as before. The re-indexing to real line numbers (`olds_`/`news_`, the table checks at `i + 1`/`j + 1`) is correct. Every older self-test case still passes, and the undeclared B1.7 run still fails on the same 115 lines.

CHANGES NEEDED (first review; superseded by the re-review below)

## Re-review (the fix, uncommitted in build/wt-stack; same as tool-hand-fix.diff)

Line numbers are `tools/refactor/move_files.py` in the working tree.

**What I ran**
- The self-test: **PASSED (0 failures)**. There are 26 `hand:` cases, including the new ones for P1, P2b, P3, P7, Q3, P4 and P4b.
- B1.7 declared: other lines 0, VERIFY PASSED. B1.7 undeclared: other lines 115, VERIFY FAILED.
- E7: `tool-hand-B1.7-verify.txt` is regenerated. It now shows the current tool (474 lines differ).
- Every earlier probe, re-run on the same throwaway fixtures, plus new ones that splice with whitespace after the backslash.

**Closed:** each of these now **fails** verify, as it should.
- P1: blocks inside `/* */`.
- P2 and P2b: `#ifndef __cplusplus` and `#ifndef NDEBUG` around the blocks.
- P3: a macro dressed as a guard.
- Q3: a range that takes no code.
- P4: the source's include ending in `\`.
- P4b: the source's include opening `/*`.
- P7: `// note \` before a code line.
- P4e: an include with a closed `/* */` after it. This is the safe side.
- X2: a re-pointed include with a changed trailing comment.

The class check (`line_classes`, compared at 357) and `pp_depth` (301) are sound for what they cover. Dropping the include guards is fine: B1.7 still passes.

**R1 (blocking). The B1/B2 splice check misses a backslash followed by whitespace, and Clang and GCC still splice that line.** All three checks test `rstrip('\r\n').endswith('\\')`:
- the scaffold check, `scaffold_lines`, 404;
- the source's added include, `hand_include`, 2140, which tests `mn.group(3)` after `rstrip('\r\n')`;
- the block's last line, 350.

A trailing space or tab after the `\` gets past all three. Probes, every one **VERIFY PASSED**, rc 0:
- P7b: blocks 7-11 and 15-17, with the new-file scaffold line `// note \␠` right before the code line `struct Track {`.
- P7d: the same with `#include "../Core/log.h" // x \␠`.
- P4c: in big.h, the source's include of the split written as `#include "../Recording/types.h" // x \␠`, placed before `inline int f()`.
- P4d: the same with a tab after the `\`.

Clang, the Mac's compiler, splices these lines with only a warning. I checked with `clang++ -E -P -x c++ -` on stdin:
- `int a; // note \␠` followed by `int spliced_away;` gives only `int a;`.
- `#ifdef Q // \␠` followed by `int kept;` swallows `int kept;`.

GCC also splices them. MSVC may not, so the Mac and Linux builds would then differ from the studio PC's. This is the B1/B2 loophole again, through the gap in the fix.

Fix: test for `\` followed by optional whitespace at the end of the line, at all three places, for example `re.search(r'\\[ \t\f\v]*$', line.rstrip('\r\n'))`. Add self-test cases for `\␠` in the new file's scaffold and in the source's include.

**Still open, as agreed (edge, to the issue):** E1 (P5, P8), E2 (P6), E3 (Q2), E4 (P9), E5, E6, E8, E9 and E10 all still pass, unchanged. One new edge:
- **E11 (edge).** A `#define` or `#undef` inside a taken block moves with it. The source then includes the new file earlier than the block used to sit, so the macro now reaches the source's lines between the include and the block's old place. It is a real move, but the scope change is silent. Suggest listing any taken preprocessor line for the reviewer.
- **X1 (no finding).** A new-file include line that opens a `/*` closed on the next line passes. That's harmless now, since the taken blocks' classes are checked.

CHANGES NEEDED

## Re-review 2 (the R1 fix, uncommitted in build/wt-stack; same as tool-hand-fix.diff)

Line numbers are `tools/refactor/move_files.py` in the working tree.

**What I ran**
- The self-test: **PASSED (0 failures)**, with 28 `hand:` cases.
- B1.7 declared: other lines 0, VERIFY PASSED. B1.7 undeclared: other lines 115, VERIFY FAILED.

**R1: closed.** One `SPLICE_RE` (`\\[ \t\f\v]*$`, line 298) is now used at all three places:
- the block's last line, 351;
- a scaffold line, 405;
- the tail after the include's path, 2141.

Every probe below now **fails** verify, as it should. These are the old ones and new whitespace variants:
- In the source's include of the split, after its path:
  - P4: a bare `\`.
  - P4b: an opening `/*`.
  - P4c: `// x \` plus a space.
  - P4d: `// x \` plus a tab.
  - P4f: `// x \` plus a form feed and a space.
  - P4g: `\` plus a space straight after the quote.
- In the new file's scaffold, just before a block's code line:
  - P7: `// note \`.
  - P7b: `// note \` plus a space.
  - P7e: `// note \` plus a tab.
  - P7d: an include line ending `// x \` plus a space.
  - P7f: `/* c */ \` plus a space.
  - P7g: spaces, then `\` and a space.
  - P7h: `} \` plus a space.
- Also failing: P1 (blocks inside `/* */`), P2b (`#ifndef NDEBUG` around the blocks) and P3 (a macro dressed as a guard).

The lines inside a taken block are byte for byte the same as in its source, so they splice there exactly as they did before. A re-pointed include must keep its base line's trailing comment. Neither can open a new splice.

**Still open, as agreed (edge, to the issue):** E1–E6 and E8–E11. None of them blocks.

SIGNED OFF
