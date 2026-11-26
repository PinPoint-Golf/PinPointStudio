# B1.15a · The ten chart rule files: inventory (no code, no build)

Read on `build/wt-37c` at `ad172f2c` (origin/main; the tree is before B1.5 and B1.6, so the `Metrics/`, `Core/` and `Coaching/` paths below shift when those run). Excerpt the information model's sections this brief names (the excerpt files were local) regenerated. Nothing in the worktree edited; 0 builds. Written: this file and the draft map `tools/refactor/maps/B1.15b-charts.map` (repo root; first line says it waits for R7). Every recommendation is **(proposed, for the Solution Architect)**.

**Check first.** `wc -l`: 8,529, as the brief says. `grep -lE 'Q_OBJECT|Q_GADGET|QQuick|QColor|QtGui|QML_ELEMENT'`: none, in the ten or in the two added stems. Also no mutable static in the eight movers. File access: one `QFileInfo` (session_delivery.cpp:355). `QMutex`: only in session_posture.cpp:863. A7, by an approximate brace scan: the largest functions are 239 lines (session_posture.cpp:1554) and 207 (session_warp.cpp:460), both under 300.

## 1. Per file (all in `namespace pinpoint::charts`; tests in `src/Gui/tests`, ctest names)
| File (lines) | Computes | Included by (outside the ten) | Tests |
|---|---|---|---|
| session_warp (921) | P-alignment: canonical P-times, a warp per shot | traces/relations/wrist presenters; 5 tests + `variability_calibrate` | charts_session_maths, charts_traces/relations/consistency_presenter, variability_env |
| session_traces (672) | group curve fit, bands, refusals on the canonical axis | relations/traces presenters | charts_session_maths, session_consistency |
| session_relations (734) | readings at P-positions, box, r, drift, facets | relations/traces/wrist presenters | charts_session_maths, charts_relations_presenter |
| session_consistency (773) | consistency sampling and stand-out rules | consistency/relations presenters, consistency_env.h, variability_env.h | session_consistency, charts_consistency/relations_presenter |
| consistency_grades (279) | variability grades per reading, cell and head | consistency presenter | charts_consistency_presenter |
| session_sequence (477) | kinematic sequence per shot and session; **also** its words | relations/traces presenters, Gui/trust/trust_readings.cpp | charts_session_maths, charts_traces_presenter, trust_readings |
| session_bridge (372) | GP bridge of the chest's gap (probable peak) | traces presenter | charts_session_maths |
| variability_clubs (264) | R2a/R2b within-club grader inputs | posture/traces/wrist presenters, Gui/diagnostics/session_diagnostics_model.cpp | variability_clubs |
| session_posture (2,531) | **a section view builder**: rules + sentences (54 lines by the brief's grep) into `Posture*View` | posture presenter, `tools/diagnostics/day_readings_dump.cpp` | session_posture, charts_posture_presenter |
| session_delivery (1,506) | **a section view builder**: rules + sentences (61 lines) into `Delivery*View` | delivery presenter, Gui/report/report_delivery.h, day_readings_dump.cpp | session_delivery, charts_delivery_presenter |

Totals: 13 files in Gui/charts (25 include lines), 12 test files (25 lines: 11 ctest suites, plus the `variability_calibrate` executable at charts_maths_tests.cmake:127, which no ctest runs), and one each in trust, report and diagnostics. The brief's counts miss `tools/diagnostics/day_readings_dump.cpp:36,38`, which includes posture and delivery.

**Verdict on two files.** `session_posture` and `session_delivery` are not pure rules. Their rule functions write sentences into their own results: `deliveryFit` sets `why` (session_delivery.cpp:481-512), as do `deliveryPlace` and `deliveryShotShape`; `postureMatch` sets `basisWords` (session_posture.cpp:1126-1129), as does `postureTrailStyle`. The result types carry text (session_posture.h:385-487, session_delivery.h:245-419). Separating the words means editing those bodies, which is #224-H's work, not a move. **Recommendation:** both stay in Gui, and the audit's list is wrong for them as whole files. They are leaves, so the other eight don't wait.

## 2. Every outward include, placed
Vocabulary or Rules, no action: `Metrics/*` (fitted_curve, variability_*, series_reduce, gate_catalogue), `Core/pp_tuned_constants`, `Coaching/consistency_home` (→ derived/ in B1.6), `Kinematics/gap_bridge`, `Wrist/wrist_profile`. Posture and delivery only: `Diagnostics/{norms,engine,measures,packs}`, `LaunchMonitor/lm_inferred_reads`, `Analysis/route_substitution`, `Coaching/*`.
| Include | What they take | Placement |
|---|---|---|
| `session_series_types.h` ×8 | ShotSeries, SessionSnapshot: the value types of OBJ-SESSION-SERIES | **moves with them.** It includes `Wrist/` (Rules), so it can't be Vocabulary. 27 includers, all in Gui. The list grows by this file. |
| `session_curves.h` ×5 | shotCurve, valueAt, quantile7, medianOf | **moves** (216 lines; includes only the types and box_stats; 11 includers, all in Gui). The list grows by this stem. |
| `../typeset/box_stats.h` ×2 (session_posture.h:143, session_delivery.h:118) | box statistics, quantile7 | None of the three options: it only forwards `Metrics/box_stats.h` (Vocabulary). Nothing to do while the two stay. Re-point it if Q1 moves them. The added stem's third include is E1. |
| `charts_words.h` ×4 | figure helpers, `Sentence`, word, list | Words: **stays in Gui**. The sequence's word functions are split out (E4). Posture and delivery stay whole. |
| `variability_env.h` (consistency_grades.cpp:22) | only `IVariabilityScaleProvider`, for the adaptor `consistencyGradeScales(provider)` | **Value in.** The adaptor goes to variability_env.{h,cpp} (E2). The rule keeps `ConsistencyGradeScales`, a value. |
| `DomainModel/rule_data.h` (session_posture.cpp:20) | `RuleData::current()` in `postureSharedEnv()`, a mutex-guarded static cache (:859-878) | Value in: `postureEnv(pack, norms)` already takes values, so `postureSharedEnv` is Gui's either way. Moot while posture stays. |

## 3. Words
- **In the eight movers.** `consistencyGuardWords()` (session_consistency.h:149) goes to Gui (E3). The sequence's `UnplacedWhy`, `unplacedWhy`, `peakTimesSentence`, `appendProbableWhen`, `appendPeaksWhen` and `segmentGates` stay behind as a new Gui file (E4).
- **The brief's grep under-counts:** it gives session_consistency 0, because the sentence starts lower-case. Every literal of the other movers is an id or a key (checked once, every distinct literal). The `"with"`/`"without"` group keys become words in the presenter (charts_traces_presenter.cpp:2248). The ball-flag ids `"short"` and `"offline"` (session_consistency.cpp:350-351) are printed as they are (charts_consistency_presenter.cpp:571). They are ids in the rule, and the presenter's lmWords is #224-H's.
- **Posture and delivery** keep all their wording (115 lines by the brief's grep, more in lower case) for #224-H.

## 4. Destination
R7 is not ruled. I'd take its proposal, `src/Diagnostics/derived/series/`:
- Under R8 `derived` is the top layer, so every include above stays legal.
- `dirRings`' `Diagnostics/` and `context_check.py`'s `Diagnostics/` give Rules and the Diagnostics context with no table edit.
- What moves: **19 files** (the eight stems plus session_series_types.h and session_curves), all into the Rules ring. The new `src/Gui/charts/session_sequence_words.{h,cpp}` is Adapters.
- **Lines leaving `src/Gui`: about 4,807.** That is 4,492 (the eight) + 473 (the two added) − 158 (the words and adaptor of E2-E4). Posture and delivery's 4,037 stay (4,492 + 4,037 = 8,529).

## 5. The map and B1.15b's edit list (part 1: one reviewable change, needed whatever the destination; no split of B1.15b)
The draft map holds the ten stems above, with posture and delivery commented out.
- **E1** session_curves.h:58 and comments :101-102: include `Metrics/box_stats.h` (as the tree then spells it), not the Gui forwarder.
- **E2** consistency_grades.h:60 (forward declaration), :69-70 (declaration), :46 (comment) and consistency_grades.cpp:22 and :52-57 move to variability_env.h/.cpp, same name and namespace. variability_env.h then also includes `consistency_grades.h`, for `ConsistencyGradeScales`. The callers (charts_consistency_presenter.cpp:747, :816; charts_consistency_presenter_test.cpp:160) already include variability_env.h.
- **E3** session_consistency.h:149 moves to charts_words.h as `words::consistencyGuardWords()`. Callers: charts_consistency_presenter.cpp:490-491, charts_relations_presenter.cpp:2906.
- **E4** session_sequence.h:40 and :111-136 go to the new session_sequence_words.h; session_sequence.cpp:168-288, with its includes :22-24, goes to session_sequence_words.cpp. At :314, `words::isAfterImpact` becomes `pinpoint::figures::isAfterImpact`, with `Core/figure_format.h` included.
  - Includes added: charts_traces_presenter.cpp, charts_relations_presenter.cpp, trust/trust_readings.cpp, tests/charts_traces_presenter_test.cpp. No transitive use was found: charts_session_maths_test, variability_calibrate and session_bridge use no words.
  - Build lists: the new pair goes beside cmake/PinPointQmlModule.cmake:657-658. The .cpp goes beside every `session_sequence.cpp` line: charts_maths_tests.cmake:21,36,129; charts_traces_tests.cmake:21; charts_relations_tests.cmake:26,84; replay_score_tests.cmake:21; charts_wrist_tests.cmake:20; tests/CMakeLists.txt:111,324.
- **For part 2:** the QML module list (cmake/PinPointQmlModule.cmake:622, 631-660) and the test lists naming the movers are rewritten by the tool. Docs: docs/design/{charts_panel_traces_relations,consistency_design,club_delivery_chart_design}.md. The IM row naming `Gui/charts/session_series_types.h` (information_model.md:1290) needs `--allow-prose`. Tests stay in src/Gui/tests and are re-pointed.

**Ownership (§10).**
- What changes: the chart maths and OBJ-SESSION-SERIES's value types.
- Kind: derived, in memory. Owner: none; they are pure functions over the session's `SessionSeries` (session level).
- Created per call, no identity. Inputs: the snapshot plus envs passed in; never stamped; EVT as OBJ-SESSION-SERIES.
- Persisted nowhere. The ring changes from Adapters to Rules. Views reach it through the Charts presenters, unchanged.
- **IM change: yes, C9** (R7). Nothing owned, written or persisted changes.

## 6. Questions for the Solution Architect
- **Q1.** Posture and delivery stay in Gui until #224-H separates their words (recommended)? Or move now, beside Coaching's existing `frequency_words.h` and `shot_factor_words.h`, words and all? Today `derived/` already holds words, despite IM §4.
- **Q2.** R7's ruling should name the grown list: session_series_types.h (OBJ-SESSION-SERIES's types change ring) and session_curves.
- **Q3.** The movers keep `namespace pinpoint::charts`, a view's name, in Rules. Should a rename be logged for later (never in a move)?
- **EDGE** (for issues; both files stay in Gui):
  - session_delivery.cpp:355 names the session by swingDir's parent folder (C5; A3 doesn't catch the `QFileInfo` form).
  - The loft table at session_delivery.cpp:146-158 is a second copy of the athlete controller's defaults.
