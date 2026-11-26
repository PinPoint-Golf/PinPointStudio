# NOTES B1-plan: the premises of Stage B, Part 1, checked against main (24 Nov 2026)

**The tree.** origin/main is `bdd04541` (the end of Stage A). The repo root's checkout is one commit behind it (`c06f399f`), so every count below was taken on `build/wt-5b`, which is clean at `bdd04541`. P-3b's uncommitted work in `build/wt-37c` is read where it changes a premise. No code was changed and nothing was built.

**Tools written for this plan** (in `build/platform/B1/`, read-only; `count_includers.py` reads the checkout it sits in unless given `--root`, so every brief passes `--root`):
- `count_includers.py [--root <checkout>] <set>…` resolves each quoted include as the lint does and counts includers of a set of headers. Sets: `vocab` (the move as ruled today), `vocab-ruling4`, `vocab-plan` (the plan's method), `coaching`, or any `Dir/` or `Dir/stem`.
- `make_im_excerpts.sh` regenerates the `IM-B1.*.md` files.
- `build/context-map/check_includes.py --list c` for A9's blockers. It reads only the tree it sits in, so it was run as a copy with its `SRC` line set to `build/wt-5b/src`. Its tables key on today's paths and go stale at the first move: B1.2 makes a kept copy (`tools/refactor/context_check.py --src`) that each move re-keys.
- The `IM-B1.*.md` excerpts were cut from v1.10.4. P-3b and every move edit the rows they quote, so each brief regenerates them from its own tree first.

**The information model** was read through `tools/method/im_excerpt.py` only, except three line ranges read directly while finding that the tool takes a heading's text (`'1.4 The lint rules'`) and not `§1.4`: lines 131–150, 152–234 and 353–448.

## 1. Counts

| Premise in the plan | Command | Today | Verdict |
|---|---|---|---|
| The Vocabulary move: 268 files include a moved header, 375 includes (212 production, 26 inside, 54 tests, 2 tools) | `count_includers.py --root build/wt-5b vocab-plan` | 266 files, 374 includes (212, 26, 52, 2) | Reproduced by the plan's method, which counts Core's ten files. |
| The same, for the move as ruled since (Q15, J-109: the three Platform headers out; LaunchMonitor's two reading types and `frame_decode` in) | `… vocab` | **279 files, 402 includes**; 53 files move; 214 production (28 inside), 61 tests, 4 tools. 78 of the includes are bare names that resolve only through a global include directory | **The plan's 268 is stale.** Briefs use the map's own count at run time. |
| Today's `src/Coaching` → `Diagnostics/derived` (no count in the plan) | `… coaching` | 35 files (36 with P-3b's `session_gates.h`); 111 includers, 238 includes, 56 bare | New figure. |
| 34 allowlist lines tagged `platform refactor (#224)` | `grep -c '(#224)$' architecture_allowlist.txt` | 34 of 138 lines | Holds on main. P-3b re-tags one (below). |
| Stage B's 4 lines: `measure_sample.cpp` A1 ×1 and A5 ×4; `session_list_reads` A5 ×1; `session_index_summary` A5 ×3 | `sed -n '30p;55p;61p;62p'` | `measure_sample.cpp` A5 is ×1 since P-3; the others as written | Count of findings stale (9 → 6); lines right. |
| `swing_reanalyzer.cpp` A1 ×1, `# step 1` | line 27 | as written | Holds. |
| The ten chart rule files: 8,529 lines in `src/Gui/charts` | `wc -l` on the ten `.h` + `.cpp` | 8,529 | Holds. |
| Core's seven vocabulary files and three Platform headers | `ls src/Core` against `fileRings()` | as written | Holds. |
| A9's blockers, 9 includes fit neither rule | `check_includes.py --list c` | 9, the same nine | Holds; two of the plan's fixes for them do not (§2). |
| `pinpoint::analysis` spread | `grep -rl 'namespace pinpoint::analysis' src`, by directory | 511 files in 18 directories, tests included (345 outside test folders, 246 of them headers); 1,808 qualified uses | Not comparable with the audit's figures, which counted headers per directory by another method; today's are the ones B1.20 is sized on. |
| `Export`: includers | `… Export/` | 60 files; 191 includers, 545 includes; 320 path mentions in build files | New figure for the rename. |
| Quoted includes through the global directories: 425; directories that span rings: 8; test targets that hand-list sources | not re-taken | n/a | Part 2's figures; the close (B1.22) re-takes the first two after the moves. |

**Premises that hold** (each run on `build/wt-5b`):

| Premise | Command | Result |
|---|---|---|
| `video_overlay_pose` and `_base` are compiled in but created by nothing | `grep -rnE 'VideoOverlayPose\|VideoOverlayBase\|video_overlay_(pose\|base)\.h' src tools tests cmake CMakeLists.txt`, less the four files and comment lines | only `CMakeLists.txt:827` and `:2709–2710` |
| `hm_session_worker.cpp:28` includes `hm_frame.h` and uses nothing from it | `grep -cE 'hm_frame::\|toAnatomical\|wristRelFromHm\|frameMap\|mountM\|pronationRateDps\|kCandidate' src/IMU/hm_session_worker.cpp` | 0 |
| `Ppcp`'s three includes of `Core/pp_settings.h` at `:30`, `:47`, `:73` | `grep -rn pp_settings.h src/Ppcp` | as written; 17 `ppSettings()` calls behind them |
| `Recording/recording.h` includes `swing_analysis.h` for three types | `grep -n swing_analysis.h src/Recording/recording.h` | line 45, as written; `BallSample2D` (`swing_analysis.h:519`) is held by `BallTrack2D` |
| `SegmentRole` is used by `Gui/imu` too; `trunk_calib_provenance.h` by Acquisition | `grep -rn 'trunk_calib_provenance.h"' src`; `grep -rl SegmentRole src` | 5 includers (`IMU/trunk_stand_tall.h`, `Gui/imu/imu_instance.h`, two in `Export`, `swing_analysis.h`); `SegmentRole` in 10 directories |
| The fingerprint hook has three install sites | `grep -rn 'installModelFileFingerprint(&' src tools` | `Gui/main.cpp:337`, `swinglab_run.cpp:591`, `raw_reencode.cpp:66`, and one test |
| `swing_reanalyzer.cpp` includes `recorded_products.h`, an Adapters file in `src/Analysis` | `grep -n recorded_products src/Analysis/swing_reanalyzer.cpp`; `fileRings()` | line 20; `Analysis/recorded_products` is Adapters |
| `kf3_smoother.h` is Rules by the lint, with few users | `grep -rn 'kf3_smoother.h"' src` | 2: `Club/club_rate_filter.h:39`, `Pose/pose_smoother.cpp:21` |
| The studio profile's includers | `grep -rn 'studio_profile.h"' src tools` | 5, one new since the plan (`Core/tests/isolated_stores_test.cpp:27`, B0.4) |
| `layoutDag`'s A7 line, tagged for #224 and #222 | `grep -n dag_layout.cpp architecture_allowlist.txt` | line 172, A7 ×1 |
| The drill pack's three Diagnostics includers | `grep -rn 'drill_pack.h"' src/Diagnostics` | `diagnostics_health.{h,cpp}`, `file_pack_provider.cpp`, `swing_themes_pack.h`, as the IM says |
| `frame_decode` has no file access (it leaves A5's exempt directory in B1.5) | `grep -cE 'QFile\|QDir\|QSaveFile\|QFileInfo' src/Export/frame_decode.*` | 0 |

## 2. Premises I think are wrong, or that no longer hold

1. **"`measure_sample.cpp`'s store read, which P-3 removes."** It is still there: `measure_sample.cpp:28` includes `Export/phase_grid_store.h`, and the Solution Architect's ruling in PR-21 (b) keeps the folder-keyed forwarder for the corridor scan as a recorded exception. P-3b gives it a second caller (the gate count's fallback) and re-tags the A1 line `# kept`. So (a) A9 can't land "with no allowlist line" while the forwarder sits in Diagnostics, and (b) the accept figure "34 → 30, Stage B's 4 at 0" can't be met: two of the four lines stay. **Proposed** (B1.0, R11): the forwarder moves beside its two callers in `Gui/diagnosticmodel`, which is already where the exception lives. I have not changed the accept figure; it is the Product Owner's.
2. **"`Ppcp`'s three includes … or by injecting the two settings."** `Ppcp` has 17 `ppSettings()` sites over about nine keys, including the pairing store's five. Injection is Stage F's size, not a blocker fix. Placing `pp_settings` in Platform (R3) is the only fix that fits Part 1.
3. **"`Core` holds only the Platform ring."** Not reachable in Part 1: `device_enumerator` waits for Stage D by the plan's own text, and `app_info` (an Adapters file, a QML façade for the About box) is named nowhere in the stage. The lint's `Core/` rule can become Platform with those two as named exceptions (R4).
4. **"The Coaching adapter out of Diagnostics (B-08)."** The audit's fix was to move `swing_themes_pack.h` into `src/Coaching`. PR-4 moves the other way: today's `src/Coaching` goes into `Diagnostics/derived`. After B1.6 the adapter and what it adapts are in one context and one ring, so B-08's non-wrist half closes with no further move. No item is planned for it; B1.6 records it.
5. **"`src/Export` renamed."** The audit's example name, `src/Recording`, is now taken by the Recording contract's types. No name is ruled (R6).
6. **"The panel's 'Work-ons' heading becomes 'Priorities'."** `grep -rn 'Work-ons' src --include='*.qml'` finds one heading, a table column in `Gui/home/HmSwingAreas.qml:178`, and no heading in the Session diagnostics panel. Other golfer-facing wordings say "work-on" in about 40 places (the report, Home, the intent page). The ruling names "the panel that shows the session's priorities"; B1.8 must find that one string or stop.
7. **The ten chart rule files are not a pure move.** They include `charts_words.h`, `session_series_types.h`, `session_curves.h`, `variability_env.h` (other `Gui/charts` files), `../typeset/box_stats.h` and `DomainModel/rule_data.h`. Moved into a Rules directory as they are, each of those is an A1 finding. Hence B1.15a (an inventory, no code) before B1.15b.
8. **`Analysis/recorded_products`** is a 613-line header with no `.cpp` and no file access; it includes only Vocabulary. The lint and the IM call it Adapters ("the store's serialiser"). The A1 line is a matter of where the file is placed, not of a store read (R10).
9. **"Two tools move into `tools/` … first in the stage"**: done (B0.1). **"#187 first"**: done (B0.4). **"A Windows test lane"**: done (B0.2, B0.3). **"Build times … timed on the Mac and on the studio PC"** and **"the first library (Platform) is cut and linked under Microsoft's compiler, before the scripted pass"**: neither is recorded anywhere. B1.3 and B1.4 are planned for them; without B1.3 the close can't answer "no worse".
10. **What "cleared" means for the three store-door lines** (J-88, J-110) is not written down. The doors are inside `src/Export`; the lint counts them by name (`countedStoreDoor()`). B1.17 can't be built until that is said (R9).
11. **"One namespace per layer"** has no ruled names, and "layer" now clashes with the IM's own use of the word (GL-43). It is also the largest change in the stage and the one Part 2's libraries need least (R12).
12. **§5 rule 5's self-check** asks for "parity (0 differences)". The IM's own footnote on step 7.3 says the parity dumps "exercise the diagnostics and athlete levels, not the analysis stages". So for a move, the proof that carries weight is the tool's `--verify` (only renames, include lines and path strings changed); parity is kept as a second check.
13. **"All three [stale tags] re-tagged and named on #224 in the stage's first commit"** ("Mandatory carry-ins" (iv)). Not done, and it moves the accept figure's start: the `#224` lines go from 34 to 37 when the three are re-tagged (36 once P-3b re-tags one `# kept`). B1.T does it; "34 → 30" is then counted from 37, with `swing_reanalyzer.cpp`'s line cleared by B1.13 and the two A6 lines going to #224-U.
14. **§7's reason for the entry check** ("if Stage B's 268-file rewrite landed first, a fault from steps 4–7 would have to be found across it") still applies to B1.5, the first tree-wide rewrite. ORDER says nothing waits for visit 1. Call C-3 below.

## 3. Calls made (unattended; for the roles to confirm)

- **C-1. One proposal first (B1.0).** Thirteen placements and names are "proposed, for the Solution Architect" in the plan, or not stated at all. Each is C9 (the ring or context of a directory or file) or a lint-scope change. They go to the Solution Architect in one page, with include-check counts, before any brief that depends on them builds. Those briefs say "stop and propose" until the ruling is in.
- **C-2. The Vocabulary move doesn't wait for B1.0.** It moves only what Q15 and J-109 name. The three Platform headers stay in `Core` and stay Vocabulary by the `Core/` rule until B1.11.
- **C-3. B1.5's commit waits for visit 1's 3-shot live check** if the visit happens before B1.5 is reviewed (it is planned for 25 Nov). **B1.4's commit waits for it in any case:** it changes the Windows build, which §5 rule 1 names. If the visit slips past that, the coordinator decides; the plan's ruling (Q20) allows the commit.
- **C-4. Self-check only for B1.5, B1.6, B1.9, B1.12, B1.16 and B1.19** (a map and the tool, with the lint's keys). Every item that edits code by hand, deletes, renames a type or changes the lint is independent.
- **C-5. B1.4 (the Platform library pilot) is in Part 1** because the plan lists it "before Part 1". It is Part 2's first library in all but name. If the coordinator moves it to Part 2, nothing in Part 1 depends on it.
- **C-6. Shared files are one lane at a time:** the root `CMakeLists.txt`, `cmake/PinPointQmlModule.cmake`, `src/Analysis/tests/CMakeLists.txt`, `architecture_lint_test.cpp`, the allowlist and `main.cpp`. Most headers are listed in the first two, so few pairs of code items are truly disjoint; the plan's table names the pairs that are.
- **C-7. The IM's version.** Main is at 1.10.4 and P-3b takes 1.10.5. Each brief that edits the IM takes "the next free version" at commit, through `tools/method/im_changelog_merge.py`.

## 4. For the coordinator
- P-3b adds `src/Coaching/session_gates.h` and has the allowlist, `main.cpp`, the IM, `cmake/PinPointQmlModule.cmake` and `src/Analysis/tests/CMakeLists.txt` open. Only B1.0, B1.1 and B1.2 are planned beside it.
- B1.1 leaves one stale comment (`src/Analysis/tests/CMakeLists.txt:1297`) if P-3b has not landed; B1.5's item picks it up.
- ORDER's two rows for Part 1 (12–16 h and the rest of 8–12 h) should be replaced by the plan's re-estimate.
