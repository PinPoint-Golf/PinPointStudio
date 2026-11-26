# B1.19 phase 1 · Diagnostics re-cut by layer: the map, the order check, the dry run (26 Nov, wt-37c at da6c2360)

**State: STOPPED in phase 1. The order check finds 20 upward includes** on the by-kind map (LAUNCH-B-close.md's stop
rule). Nothing moved, no build (0 of 2 used), no commit, no allowlist line. **No file is unplaceable**; 6 are weak fits
(marked *weak*: no R8 word fits squarely) and 6 stems fit two kinds (marked *two*, with their hit counts below). Each
was placed by its own header comment before the check ran, except `measurement_precedence.h` (see its row).

**Written** (uncommitted, in `build/wt-37c`): `tools/refactor/dir_order_check.py` (`--order`, `--map` for a proposed
layout, `--cycles`; skips any `tests/` path; an unlisted folder, an unresolved include or a map line that matches
nothing is exit 2) and `tools/refactor/maps/B1.19-diagnostics.map`. **Sanity run on today's tree:** `--cycles` finds
1 cycle over all five folders, 16 folder edges, so the resolver sees it. Evidence beside this page:
`B1.19-order-check.txt`, `B1.19-dryrun.txt`, `B1.19-includers-before.txt`.

## The map, one line per stem (65 files; `derived` 55 unchanged: 36 + 19 in `series/`)
**model, 18 files**
- `characteristic` (from engine): characteristic, the condition graph's types.
- `enum_table_p.h` (engine): vocabulary, the enum spellings characteristic and norm share.
- `pack_io` (engine): pack reading (RuleFile, ValidationReport, LoadResult).
- `anatomy_vocabulary`, `measure_facets`, `measure_vocabulary.h` (measures): vocabulary.
- `measurement_precedence.h` (measures): *two*. Its header says "the one rule for which source wins"; placed in model
  as the measure vocabulary's source ladder (`Measure::preferKeys`) **because `measure_vocabulary.h:22` includes it**:
  in engine it adds 1 hit (21). The one file the include graph placed; the coordinator's call.
- `club_baseline_provider.h` (measures): provider interface.
- `norm` (norms): norm. `context_tree` (norms): context.
- `norm_provider.h` (norms), `pack_provider.h` (packs): provider interfaces; *two* (they also declare the loaders' factories).

**content, 24 files**
- `characteristic_pack`, `norm_pack`: loader + validator ("persistence and validation", their headers);
  `characteristic_pack` is *two* (model files include it as the pack's types).
- `drill_pack`, `screen_pack`, `reference_pack`, `gate_words_pack`: registries (their own word).
- `swing_themes_pack.h`: the packs (marshalling for `derived/swing_themes.h`); *two*: its five `*_pack.h` siblings are in derived.
- `layered_set.h`: the four registries' shared machinery.
- `area_regroup_table.h`: the generated table the pack loader reads.
- `provider_leaf_p.h` (engine): the leaf loaders' shared machinery.
- `file_`, `memory_`, `resource_`, `merged_pack_provider.cpp`; the same four `_norm_provider.cpp` (norms): loaders.

**engine, 23 files** (13 stay, 10 in)
- `characteristic_engine`: detect. `relation_resolver`: explain. `diagnostics_health`: health.
- `fire_findings`: detect.
- `session_reduction`: detect, the session's diagnosis; *two*: its header says it reduces kept findings and each
  shot's derived data.
- `excludes_rule.h`: explain (which of two excluding conditions is kept, for explain() and the outcome composer).
- `condition_reading.h`: *weak*, explain (what a condition reads, walked for a reader).
- `wrist_domain_score.h`: *weak*, explain (a score over the wrist conditions' verdicts).
- `norm_measure_source.h`, `live_measure_source`, `session_measure_source.h`, `measure_sample` (norms, measures): measure sources.
- `composite_reading.h` (norms): *weak*, a measure source's arithmetic.
- `metric_corridor.h` (norms): *weak*, the measure → norm → corridor join.
- `shot_direction_bounds.h` (norms): *weak*, the pack's direction edges, read as the engine reads them.
- `delivery_grid_numbers.h` (measures): *weak*, a measure source's helper over the phase grid.

The drill pack moves to `content/` like any other; its includers (`diagnostics_health.*`, `file_pack_provider.cpp`,
`swing_themes_pack.h`, and outside Diagnostics) are re-pathed only. `dag_layout`, `corridor_plot` are not mapped (B1.18).

## The order check: 20 upward includes (`--order model,content,engine,derived --map …`; `--cycles`: 1)
1. **engine → derived, 15:** `fire_findings.h:54-56`, `fire_findings.cpp:29-31`, `session_reduction.h:44-47`,
   `session_reduction.cpp:23-27` include the ledger, shot factors, strike, club distances, firing side, sequence checks,
   tendencies, session results and the work-on and shot-outcome packs: firing's and the reduction's inputs and
   outputs. No include edit fixes these; it is the order or these two files' folder.
2. **model → content, 3:** `context_tree.h:21` → `characteristic_pack.h` (by its comment for `ValidationReport` only,
   which is in `pack_io.h`: a one-line fix, not compiled); `pack_provider.h:21` → `characteristic_pack.h`;
   `norm_provider.h:22` → `norm_pack.h`.
3. **content → derived, 2:** `swing_themes_pack.h:61-62` → `derived/swing_themes.h`, `derived/condition_role_pack.h`.

**For the second look** (each a ruling, not mine): hits under other placements, same order unless named.
| Placement | Hits |
|---|---|
| As mapped | 20 |
| `measurement_precedence.h` in engine | 21 |
| `swing_themes_pack.h` in derived | 18 |
| `pack_provider.h`, `norm_provider.h` in content | 18 |
| `characteristic_pack` in model (its .cpp then includes `reference_pack.h`, `area_regroup_table.h`) | 20 |
| `session_reduction` alone in derived | 11 |
| `fire_findings` alone in derived (`session_reduction.h` includes it: +1) | 15 |
| both in derived | 5 |
| rows 3, 4 and 8 at once | 1 (`context_tree.h:21`) |
| As mapped, order `model → content → derived → engine` | 9 (4 derived → engine: `excludes_rule.h`, `condition_reading.h` ×2, `delivery_grid_numbers.h`; plus 2. and 3.) |

## The dry run (`move_files.py … --dry-run`: exit 0)
Move 52 files; rewrite 310 include lines in 150 files (rel 281, src 23, base 6); includers of the moved set 165 files,
351 includes (spelling: bare 60, dir-qualified 23, relative 268); path mentions 551 (build 507 in 386 lines; allowlist
1, `measure_sample.cpp` re-keyed; context_check 1, `drill_pack`; tools 4, `area_regroup.py:8,29`,
`swinglab/coverage.py:30,63`; docs 18; comments 20; lint 0); include dirs re-pathed 0; rings and contexts changed 0;
errors 0. **Can't place 1:** `src/Vocabulary/Metrics/gate_catalogue_rules.h:137` names
`src/Diagnostics/measures/live_measure_source.cpp` in a C++ string: a hand edit under `--allow-code-path`, for the brief
to name. IM lines it rewrites: 207, 208 (§2 "The packs", named), 964, 1304, 1306, 1328 (current rows, not dated: no
`keep:`). By eye in phase 2: IM 1328's bare `Diagnostics/norms`; the developer guide's folder table (3 mentions).
`count_includers.py` before, at today's paths: packs 76 files/133 includes, norms 72/106, measures 57/70,
`characteristic` 16/16, `enum_table_p` 3/3, `pack_io` 17/17, `provider_leaf_p` 6/6.

**Global include paths: 4** (`CMakeLists.txt:929`, `target_include_directories(PinPointStudio …)`). In hand-written
source (src, tests, tools), no quoted include that fails relative resolution resolves in one of the four; none of the
four folders' files has a `Q_OBJECT`, `Q_GADGET` or `QML_*` macro, so no generated file includes them by bare name.
Not built, so not proven. The brief's 79 counted bare spellings (60 in the dry run), not reliance on these dirs. The 6
`base` rewrites (`src/Gui/tests/lm_session_model_test.cpp:51-56`) rely on that test's own `${SRC}/Diagnostics`
(`src/Gui/tests/CMakeLists.txt:94`); the tool makes them relative. So all four look removable by hand in phase 2.

**Judgement calls:** the excerpt script regenerated every `IM-B1.*.md`. The map is the by-kind layout only.
**Real stores:** none reachable (two read-only scripts; no app, no test run). **EDGE:** `dir_order_check.py` resolves a
bare name only within `--base`; includes outside it are out of scope by design.
