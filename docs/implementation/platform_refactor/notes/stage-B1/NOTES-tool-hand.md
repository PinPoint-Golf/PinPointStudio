# tool-hand · the move tool learns `hand:` (a file split by hand, declared in the map) (26 Nov 2026)

Lane 2 (`build/wt-stack`), unattended. Tools only, no `src/` edits. wt-stack was clean, at 721da30f (on origin/main, nothing
off main, no stash). I fetched, ran `reset --hard origin/main` (1e3269a8, B1.7) and `clean -fd`. I didn't touch wt-37c.

## 1. The gap (NOTES-B1.7.md §3)

B1.7's whole-tree `--verify` failed on exactly 4 lines, all from the hand split: the added header, `CMakeLists.txt:2940`,
`swing_analysis.h`'s 13th include, and `recording.h:45`'s new target. It also needed `--allow-prose` on `swing_analysis.h`
and `recording.h`, which waived every line of those two headers, the 105 deleted struct lines included. Without those
waivers, the same tree fails on 115 lines (`tool-hand-B1.7-verify-undeclared.txt`).

## 2. What `hand:` does (tools/refactor/move_files.py; README "The map")

`hand: <new file> <- <source file>:<lines>[,<lines>…]`. Both files are C++ under `src/`. The line numbers are the
source's in the tree before the move, as `keep:` uses. Each range is one block.
- **The move or the dry run** makes no split. It refuses only a source that is not in the tree (it matches nothing), a
  new file that the map moves or moves to, a line two `hand:` lines both take, and a malformed line. It does not check
  ranges or whether the new file exists, because an `--over-work` replay may run after the split. The dry-run report
  lists each declared split.
- **`--verify`** checks the split, then removes its lines from both sides before the existing diff and include checks,
  so everything else in those files is checked as before:
  - The new file must hold each block byte for byte, in the declared order. Each block must be whole (its braces
    balance in the source) and sit in the same scope (namespaces and nesting, from a brace walk that skips comments,
    strings and `#` lines). Every other line must be scaffold: blank, a comment, `#pragma once`, an `#include` or
    `#import`, or a namespace opened or closed (after the review: no `#define`, `#if` or splice line; see §6). Braces must balance at the end. Every include
    must resolve, never through an include directory, and a taken include must name the map's image of what it named
    in the source.
  - The source loses exactly the taken lines and may gain an include of the new file (not through an include directory).
  - In any file, an include that named the source in the base may now name the new file. Only the path may change, plus
    the spacing before its comment (B1.7's `recording.h:45` re-aligned its comment). A relative include must stay relative.
  - A build file may gain a line whose only words name the new file (a source list's entry).
  - Every accepted line is listed for the reviewer. Nothing passes silently.
  - It is refused when it matches nothing: a new file not in the tree, or already in the base; a source not in the base;
    a range past the source's end; blocks the new file does not hold.
- **Limits (calls):** a block must be copied unedited, so a taken include that the map would also rewrite can't be
  declared; the verify fails, which is the safe side. `--allow-prose` on the source skips its line check, and that line
  check is what proves the lines left the source. The README says not to do this.

## 3. Self-test (`hand_selftest`, a fourth fixture with no cmake, so it runs on Windows too): 20 cases

The brief's four:
- a declared split passes (and the dry run and move pass before the split exists);
- an undeclared one fails;
- a `hand:` line that matches nothing is refused (no such file; a block it does not hold; a range past the end; a source
  not in the tree, at the move);
- a split plus an unrelated bad include fails.

The others: a malformed line; a taken line edited in the new file; a code line added; a `#define` added; an include guard
accepted; another namespace; a taken line left in the source; an include of the split added outside its source; an
includer re-pointed at another file; a build line naming another new file; a pass again once all are undone.
- `move_files.py --self-test`: **PASSED (0 failures)**, 120 checks (`tool-hand-selftest.txt`).
- ctest through the lock, in wt-stack's tests tree (Python 3.9), `move_files_selftest` and `artefact_paths_check`:
  **2/2 passed** (`tool-hand-ctest.txt`).

## 4. B1.7 re-verified (read only)

wt-stack is at 1e3269a8, B1.7's commit, plus this tool change (`1e3269a8^` = 008b8eb4). The map is a copy with the
declaration, `B1.7-shared-hand.map`: `hand: src/Vocabulary/Recording/capture_time_types.h <-
src/Vocabulary/Analysis/swing_analysis.h:116-158,514-550,625-649`. The blocks come from `git diff -U0` and match B1.7's
§1 struct ranges, each with its comments and the blank line at one edge. `--verify 008b8eb4`, with `--allow-prose` on
the IM and the two developer guides only, so **not** on the two headers (`tool-hand-B1.7-verify.txt`):
- renames 2, 16 files;
- listed 9: the hand split, 105 lines byte for byte plus 44 scaffold; `CMakeLists.txt:2940`; `swing_analysis.h:44`'s
  include; `recording.h:45` re-pointed; the three prose files; the map; the tool;
- **other lines 0, VERIFY PASSED**. Exit 3 (NOT CONFIGURED). I didn't run the configure: the tree is byte-identical to
  B1.7's, whose configure passed (B1.7-verify.txt). Only the tool changed.

With the committed map (no `hand:`), it still fails: other lines 115, exit 1.

## 5. Commit

**cb015d0e** on main, a fast-forward from 1e3269a8, pushed under GIT-2 after the self-test passed. Two files:
`tools/refactor/move_files.py` (code, docstring, self-test) and `tools/refactor/README.md`. Message:
`tool-hand-commit-msg.txt`, re-read against GIT-8/9/10/13 (no trailer). MOVES.md's how-to is updated locally, not in git.

(The review then reopened this commit; its fixes are ebd8c952, §6. The B1.7 re-verify in §4 is regenerated with the final tool.)

**For the coordinator:**
1. B1.7's committed map has no `hand:` line. I didn't change it (the brief said read only). Adding the line would let
   B1.7's map re-verify on its own; that would be a one-line map change, if you want it.
2. From B1.12 on, a brief's hand split goes in its map as a `hand:` line, and `--allow-prose` is no longer put on the
   split's headers.

## 6. Independent review (AG-21): `REVIEW-tool-hand.md`, **SIGNED OFF** after two fix rounds

A fresh reviewer agent (not a fork) was given the diff and these notes. It ran loophole probes on throwaway fixtures.
- **Round 1: CHANGES NEEDED, two blocking findings.**
  - B1: the new file's scaffold could switch the taken code off or add a macro, and verify still passed. The probes:
    `#ifndef NDEBUG` around the blocks, a guard-shaped `#define`, the blocks inside `/* */`, a `// … \` splice
    before a code line, and a range taking only a blank line.
  - B2: the source's added include could end in `\` or `/*` and swallow the source lines after it.
- **The fix:** no `#define`, `#if`, `#ifdef`, `#ifndef`, `#else` or `#endif` in the scaffold (include guards dropped:
  `src/` uses `#pragma once`), and no splice line. Each taken block's lines must read the same way (code, comment,
  string) as in its source. A block may not sit under an `#if`, must take at least one code line, and its last line
  may not splice. After an include's path, only nothing or a `//` comment is allowed.
- **Round 2: CHANGES NEEDED, R1 (blocking):** a backslash followed by spaces still splices (clang and gcc do so with a
  warning). Fixed with one `SPLICE_RE` (`\\[ \t\f\v]*$`) at all three places. Round 3: **SIGNED OFF**.
- I added a self-test case for each probe: 28 hand cases in all. The self-test passes, and ctest (lock, Python 3.9)
  passes 2/2. Re-run over B1.7 with the final tool (the evidence regenerated, the reviewer's E7): 0 other lines with
  the split declared, VERIFY PASSED (exit 3, NOT CONFIGURED); 115 lines without it.
- **ebd8c952** on main, the fixes, pushed after the sign-off (`tool-hand-fix-commit-msg.txt`).
- The edge findings E1–E6 and E8–E11 are filed as **#299**, not fixed here (AG-22). The main ones: the source isn't
  required to include its split; angle includes in the new file go unchecked; a build line is accepted in any target;
  `--allow-prose` on a source is discouraged but not refused.
