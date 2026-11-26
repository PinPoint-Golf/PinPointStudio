# Review · B1.12 · LaunchMonitor's inferred reads move to `src/LaunchMonitor/inferred/` (AG-21, 26 Nov 2026)

Independent reviewer, read-only, unattended. Reviewed: `build/wt-37c` at HEAD `176e6d44` plus the uncommitted change;
the live `git diff HEAD -M` is byte-identical to `B1.12-diff.patch` (checked at review time, after 04:18), plus the untracked
`tools/refactor/maps/B1.12-lm-reads.map`. The plant files are gone (`git status`: only the map is untracked).

## Verdict: SIGNED OFF (the diff, the lints and the Mac suites), with 5 EDGE findings, no BLOCKING

**Scope of the sign-off.** It covers the diff, `--verify` and configure, the plants, the Mac builds and the Mac suites.
**Parity against `b1-base-2` (must be 0) and the studio PC run were not evidenced when I reviewed**: NOTES §5–8 are empty,
there is no `b112-parity*.log`, and `b112-studio PC.log` stops at "393 tests registered; jom -j16". The coordinator should
not commit until both are in NOTES: parity 0, and no new studio PC failure against the last studio PC run on main.

## The checks

1. **Accept.** `fileRings()` has no `LaunchMonitor/` key left (8 -> 0; `grep -n LaunchMonitor architecture_lint_test.cpp`
   gives only `:85`, `:93-94`, `:190-191`, `:411` and `:860`). `dirRings()` `:190` `LaunchMonitor/` Adapters, and `:191`
   `LaunchMonitor/inferred/` Rules. `ringOf()` (`:267-276`) takes the longest prefix, so the folder entry wins. The
   allowlist is 176 lines at HEAD and in the tree, unchanged. Its only `LaunchMonitor` lines (`:94-95`, `gcquad_monitor.cpp`
   and `premier_monitor.cpp`, A5) are connectors, so the claim "none re-keyed because none names the eight" is **verified**.
2. **Complete and pure.** All 9 files moved (8 stems; only `standalone_gate` has a `.cpp`), 7 at 100% similarity and 3
   changed only in their outward includes. I checked it independently: the `-U0` residue with the stem names filtered
   out is exactly the 4 outward include pairs, the IM rows, the lint's table and prose lines and the context table
   lines. No code line changed but the five `producer` strings named in the brief. Leftover old paths outside
   `docs/implementation` and `docs/research`: only `tools/lm_flight_fit/README.md:51` (the deliberate `keep:`, a `git
   show` at `16f12352`). Build and script files (`*.txt`, `*.cmake`, `*.cmd`, `*.sh`, `*.py`, `*.yml`, `*.json`) hold no
   variable-prefixed old path; the hits that remain are bare-name comments, and the `docs/implementation/swe-audit/data`
   JSON, which is a dated snapshot. The only bare includes of a stem are the two inside the folder
   (`lm_inferred_reads.h:43`, `standalone_gate.cpp:18`). `count-after.txt` gives 9/5/2/3/1/7/3/3 at the new paths and
   0 at the old ones.
3. **No connector included.** The nine files include only `Vocabulary/LaunchMonitor/launch_monitor_reading.h`,
   `Vocabulary/Analysis/gate_record.h`, `Diagnostics/derived/handedness_gloss.h`, each other and Qt/std. No connector
   in `src/LaunchMonitor` includes one of them; only the tests `gcquad_pairing_test.cpp:14` and
   `standalone_gate_test.cpp:13` do. **Connectors by content:** I agree with leaving `gspro_reading.*` (it wraps
   libgspro's wire struct, `<gspro/gspro.h>`) and `premier_club.h`. `premier_club.h` is pure and header-only, and its
   only includers are `Gui/launchmonitor/launch_monitor_controller.cpp:36` and a test, but it decodes Premier's own
   `ClubName` code, a device format, as `gcquad_csv_parser` decodes LastShot.CSV. R5 names the eight stems only, so
   moving it would need a ruling. Not a finding.
4. **The lint and context edits are correct.** The header prose, the `dirRings` and `fileRings` edits and the
   ring-table check (`:860`, the new folder is Rules and a connector is Adapters) are all right. **A8**
   (`isAcquisitionOrAppState`, `:400-416`) returns false unless the ring is Adapters. The moved files were Rules
   before (by `fileRings`) and are Rules after (by `dirRings`), so the `LaunchMonitor/` prefix list at `:411` means the
   same for them. Nothing else in any lint keys on `LaunchMonitor/`: I grepped the lint tests, `tools/**/*.py`, `*.sh`
   and `tests/`. `context_check.py` `:48-49`: `LaunchMonitor/` is ACQ (ruled, out of "to place"), and
   `LaunchMonitor/inferred/` is DIA by the longest prefix (`tag()` `:119-122`). Removing the eight FILES entries keeps
   every tag the same.
5. **The IM is accurate against R5 and the code.** It is at version 1.12.11. RING-RULES: the context cell names
   `LaunchMonitor/inferred` among Diagnostics' and drops it from "to place", and the directories cell names the folder
   and its eight stems "by its directory". RING-ADAPTERS: `LaunchMonitor` is the connectors, under Acquisition, no
   longer "to place", and its directories cell reads "(the connectors only)". The spanning table's `LaunchMonitor` row
   is gone, and the §11 row `:1293` is re-pathed. It has one changelog row and the footnote `[^sa24-r5]`, which cites
   R5, option (a), C9 and the Solution Architect on 24 Nov. Provenance sits in footnotes, in the style of the earlier
   rows. `im_excerpt_selftest` and `im_changelog_merge_selftest` pass (`b112-ctest.log`).
6. **The plants** (`b112-plants.log`). A1 failed on `LaunchMonitor/inferred/lm_gates.h:161` (Rules -> Adapters,
   `gcquad_monitor.h`) and on the new `inferred/zz_b112_plant.h:1` (Rules -> Adapters, `premier_monitor.h`). A
   connector including a moved rule gave no A1 finding (outward to inward is allowed), and the context check tagged it
   Acquisition -> Diagnostics, so the new DIRS entry is read. The tree was restored, and the plant file is absent.

**My own run**, through the lock: `architecture_lint_test`, `gate_catalogue_test`, `standalone_gate_test` and
`gcquad_pairing_test`, 4/4 passed. The implementer's `b112-ctest.log` has 31/31, both lints among them. The brief's
regex `lm_|launch_monitor|shot_pairing|standalone_gate|flight` also selects `qml_ui.tst_lm_graphics`, which is not in
the 31. No QML is touched, and the studio PC's qml_ui pass covers it. The Mac app and tests trees built (exit 0).
Configure passed for both trees (`B1.12-verify.txt`).

## Findings

- **EDGE · `src/Gui/tests/architecture_lint_test.cpp:84-85`.** The ring-table prose still says `fileRings()` "exists
  because four directories straddle rings today (03 §2) — Analysis, Core, LaunchMonitor, Export". `LaunchMonitor` no
  longer does, and the new sentence at `:93-94` says so, so the two contradict. This is a comment only. The fix is to
  drop `LaunchMonitor` from that list or mark it historical. The new prose `:93-94` and the ring-table check `:860` are
  the 3 "other" lines `--verify` listed. I read them, and they are correct.
- **EDGE · NOTES §2 ("Nothing else reads the `producer` field").** This is inaccurate.
  `src/Gui/diagnosticmodel/model_browser.cpp:4290-4291` shows `e->producer` in the in-app model browser as the "where it
  fires" row. The new string is the correct path, so this is right and not a behaviour change of substance. But the
  browser now shows different text, so the commit message's "Nothing else changes" should name it, and NOTES should be
  corrected. Watch whether the parity dump carries the catalogue's producer strings: if it does, a diff there is this
  path, not a regression.
- **EDGE · wording, IM `[^sa24-r5]` and the changelog row 1.12.11, and `b112/commit-msg.txt`.** These say "the eight
  inferred-read files" and "The eight files", but 9 files moved: 8 stems, with `standalone_gate` having a `.cpp`. "The
  eight inferred-read stems (nine files)" would be exact.
- **EDGE · IM `[^sa24-r5]`.** "The files stay beside what they interpret, and Stage B, Part 2's Rules library takes the
  sub-folder" is R5's *recommendation* rationale, not the "Ruled:" line. The footnote attributes it to the Solution
  Architect, and the ruling adopted option (a), not the reasoning. It should be marked as the recommendation's reason,
  or dropped (trace every design sentence).
- **EDGE · deck, carried forward.** `information_model_walkthrough.pptx` still says "Eight directories span rings (…
  LaunchMonitor …)". The Product Owner needs to refresh it (the implementer flagged this, and it is not to be edited by
  an agent).

Not a finding, but noted: `docs/design/lm_panel_fsx.md:106,116` and `docs/design/session_diagnostics_design.md:665` were
already stale before this item (`src/Analysis/lm_*.h`). This move didn't change them and MOVES' re-pathing didn't
touch them, because they never named `src/LaunchMonitor/`.
