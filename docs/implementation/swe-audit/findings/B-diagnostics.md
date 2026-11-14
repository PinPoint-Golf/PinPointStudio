# Reviewer B — `src/Diagnostics` (packs · engine · norms · measures) and its seams

Pinned at `37289e33`. Read-only review; nothing was built or run. "Verified" below means verified by
reading the code at that commit; every runtime claim I have not reproduced is labelled **suspected**.

## 1. Summary

The core of the area is sound. The engine (`detect`, `evaluate`, `explain`) is pure, takes its
content as arguments, works the same at shot and session level, and gets the athlete's baselines
through an injected interface. Content really is data, and the leaf providers, `LoadResult<T>` and
`atomicWrite` are well built. What is weak is **who holds the content at runtime, and how long for**:

1. **Nobody owns the loaded library.** I found ≥ 10 separate characteristic-pack assemblies in the
   app, each with its own lifetime (per object, per call, process-static). The norm set is one
   process-wide cache whose only reset, `ModelBrowser::refresh()`, has no caller. So after a save in
   the Diagnostic Model, different screens grade with different content until the app is relaunched
   (suspected; the code path is clear) (B-01, B-02).
2. **The process-wide registry caches are not thread-safe, yet worker threads touch them first**,
   including from inside the pack loader. Callers work around this with "take it on the GUI thread
   first" rules and by freezing content for the life of the process (B-03, B-12).
3. **Whether a norm is provisional or decided lives in prose.** "Decided" is a substring match on
   the free-text `citation`, and the generated release dashboard already gets one row wrong
   (B-05). Flight norms are a second norm registry inside Coaching (B-06).
4. **A stated limitation that is no longer true turns off a design rule.** Shots with no declared
   club are never marked "club assumed". The in-app rule text says they are (B-07).
5. **The directories are not layers, and presentation lives in the domain.** `engine/` holds both
   the bottom vocabulary and the top-level DAG pixel layout. One adapter includes Coaching. There is
   a Wrist ↔ Diagnostics cycle the include graph missed. The developer guide still says the engine
   is "not wired" (B-04, B-08 – B-10).

## 2. How the area is put together

- **Content** (`src/Resources/diagnostics/*.json`, embedded as Qt resources): `core.json` (704 KB:
  218 measures, 181 signals, 192 conditions, 673 edges), `norms.json` (280 rows), `contexts.json`,
  `screens.json`, `drills.json`, `references.json`, `gate_words.json`, plus `flight_norms.json`,
  which nothing in `src/Diagnostics` reads.
- **Value types**: `measures/measure_vocabulary.h` (Measure, Signal, Shape),
  `engine/characteristic.h` (Condition, Edge, CharacteristicPack), `norms/norm.h` (Norm, Grade,
  GradePolicy, wording), `norms/context_tree.h`.
- **Loading**: `packs/characteristic_pack.cpp` (`loadPack`/`validatePack`/`savePack`),
  `norms/norm_pack.cpp`; the providers `{resource,file,memory,merged}_{pack,norm}_provider.cpp`
  over a shared `detail::LeafProvider<Traits>` (`engine/provider_leaf_p.h`). The four "flat"
  registries (screens, drills, references, gate words) each hold their own raw-pointer singleton
  (`sharedXSet()` / `resetSharedXSet()`). Norms use the process-wide `sharedNormProvider()`.
- **Execution**: `measures/live_measure_source` reads a swing's phase grid (from the
  `swing_phasegrid.json` sidecar, or by parsing `swing.json`). Then `norms/norm_measure_source.h`
  grades it, `engine/characteristic_engine` `detect()` gives findings, and
  `engine/relation_resolver` `explain()` gives ranked causes. `measures/session_measure_source.h`
  feeds session-scope measures through the same `detect()`.
- **Consumers**: `Gui/diagnostics/SessionDiagnosticsModel` (per-shot `detect` on a worker
  thread, session `explain`), `WorkOnsController`, `LmSessionModel`, `ModelBrowser`, the charts
  presenters, `SessionReportModel`, Coaching (`diagnostic_ledger.h`, `*_pack.h` adapters), and Wrist
  (`NormBandProvider`).
- **Ownership**: every consumer builds its own pack provider with
  `makeCharacteristicPackProvider()`. All of them share one norm assembly through a `shared_ptr`,
  which each keeps for as long as it likes. Edits are written by `ModelBrowser` only.
- **Build**: no library target. Each binary lists the Diagnostics `.cpp` files by hand (B-14).

## 3. Findings

### B-01 · A saved corridor edit never reaches the rest of the app: the norm cache's only reset has no caller
- Category: 1 Ownership and lifetime · 4 One source per derived fact · 8 State      - Severity: High
- Evidence:
  - `src/Diagnostics/norms/norm_provider.h:258` — `// Call resetSharedNormProvider() after writing a user norm set, or edits will not be visible until the next launch.`
  - `src/Gui/diagnosticmodel/model_browser.cpp:1023` — `resetSharedNormProvider();`. This is the only call in `src` outside tests. It sits in `ModelBrowser::refresh()` (`:1017`), a `Q_INVOKABLE` (`model_browser.h:656`) that no QML or C++ calls. A grep of `src/Gui` for `browser.refresh` finds nothing.
  - `src/Gui/diagnosticmodel/model_browser.cpp:5162` — `if (!saveUserNormPack(m_workingNorms, &whyNot)) {`. After a successful save it resets the screens, drills and gate words (`:5211-5213`) but not the norms.
  - `src/Gui/charts/consistency_env.h:52-53` — `it keeps the norm set it found for the life of the process, so a corridor edited in the Diagnostic Model reaches this at the next launch.`
  - Long-lived holders take the `shared_ptr` once: `session_diagnostics_model.cpp:703` `, m_norms(sharedNormProvider())`, `lm_session_model.cpp:689`, `consistency_env.cpp:55`, `session_posture.cpp:858` (function-static), `Wrist/reference_bands.cpp:46`.
- What goes wrong (suspected, not run): edit a corridor in Settings → Diagnostic Model and Save.
  The Diagnostic Model draws the new corridor from its own working assembly. The wrist grid,
  metric detail rails, Charts (posture, consistency, wrist), the session diagnostics panel and the
  session report all keep grading against the old one until relaunch. `SessionDiagnosticsModel`'s
  content stamp (`session_diagnostics_model.cpp:929-944`) hashes its own stale norms, so it never
  learns that its ledger is out of date.
- Fix: give the library one owner (B-02) that holds an immutable snapshot
  (`shared_ptr<const DiagnosticsLibrary>`: pack + norms + contexts + flat sets), swaps it
  atomically on save, and emits one "library changed" signal. Consumers re-take the snapshot at a
  safe point. Workers keep the snapshot they started with, which is what `resetSharedNormProvider`'s
  comment already argues for. Until then, at minimum call the reset from `save()`.
- Effort: M        - Related: B-02, B-03

### B-02 · ≥ 10 independent pack assemblies with unrelated lifetimes; after an edit they disagree
- Category: 1 Ownership and lifetime · 2 Identity      - Severity: High
- Evidence (each line parses and validates the 704 KB `core.json` and the user packs again):
  - per object: `session_diagnostics_model.cpp:702` `, m_packProv(makeCharacteristicPackProvider())`. There are three or more instances: `PpSessionDiagnosticsPanel.qml:90`, `work_ons_controller.cpp:351` `m_model = new SessionDiagnosticsModel(this);` and `session_report_model.cpp:301`. Also `lm_session_model.cpp:688` and `model_browser.cpp:811` (core only, plus its own merge).
  - lazy, app lifetime: `work_ons_controller.h:649` `mutable std::unique_ptr<…ICharacteristicPackProvider> m_packProv;`, made on first use in 7 places (`work_ons_controller.cpp:693, 718, 1262, 1537, 1871, 1954, 2315`). Also `main.cpp:444` (the report-list goal resolver), `charts_wrist_presenter.cpp:159` and `session_series_model.cpp:599`.
  - process-static, never reloaded: `session_posture.cpp:857` `static const std::unique_ptr<pa::ICharacteristicPackProvider> provider = …` and `consistency_env.cpp:126` `static const ConsistencyPacks packs;`
  - per call: `session_report_model.cpp:499` and `:1163`, two loads for every report.
  - `model_browser.cpp:5228` `emit libraryChanged();`. Its only connection is to `ModelBrowser` itself (`:826`).
- What goes wrong (suspected, not run): change a condition's `goal` or a signal and Save. A report
  made afterwards (`:1163`, fresh pack) prints the new goal, while the Sessions list (`main.cpp:444`,
  frozen pack) shows the old one beside it. The session panel's `SessionDiagnosticsModel` keeps the
  old pack, while the report's own `SessionDiagnosticsModel`, if created after the save, uses the new
  one. Two graders then read the same session with different packs. Each load also repeats a 700 KB
  parse plus the 739-line validation.
- Fix: one library owner at app level (or per athlete, if packs ever become per athlete), injected
  into every consumer, as in B-01. `makeCharacteristicPackProvider()` stays as the factory that
  owner calls. Remove the `mutable` lazy members and the function statics.
- Effort: M        - Related: B-01, the GUI reviewer's "view-owned model" findings

### B-03 · The registry caches are unsynchronised lazy singletons, first touched from worker threads
- Category: 8 State and concurrency · 13 Comments as a symptom      - Severity: High
- Evidence:
  - `src/Diagnostics/packs/drill_pack.cpp:192` `DrillSet *g_drills = nullptr;`, then `:223` `if (!g_drills) g_drills = new DrillSet(assembleDrillSet());` and `:229` `delete g_drills;`. The same pattern appears at `screen_pack.cpp:209-252`, `reference_pack.cpp:470-513` and `gate_words_pack.cpp:241-279`. Each returns a `const &` into an object that `reset*()` deletes.
  - `src/Diagnostics/norms/merged_norm_provider.cpp:243-252` — `static std::shared_ptr<const INormProvider> slot; … if (!slot) slot = …`, an unsynchronised read and write of a `shared_ptr`.
  - The pack loader itself reads one: `characteristic_pack.cpp:54` `const Reference *ref = sharedReferenceSet().byCitation(citation);`. This runs on every `loadPack`.
  - Worker-thread first use: `session_series_model.cpp:595-599` (`// assembled on first use and on this thread … the GUI thread never waits on it` followed by `makeCharacteristicPackProvider()`). Also `charts_posture_presenter.cpp:1806` `m_pool.start([…]{ auto r = computePosture(*w, in);` → `:1593` `postureSharedEnv()`, whose static initialiser calls `makeCharacteristicPackProvider()` and `sharedNormProvider()` (`session_posture.cpp:857-858`).
  - The workaround, written down: `consistency_env.h:56-58` — `sharedNormProvider()'s own first build is unsafe against a first build on another thread … A first call from any other thread is a programming error: it asserts in a debug build`. Also `session_wrist.h:58` `Safe on any thread once warmWristBands() has run on one.`
- What goes wrong (suspected race, not reproduced): open Charts → Posture before anything on the GUI
  thread has touched `sharedReferenceSet()` or `sharedNormProvider()`, while the GUI thread builds a
  pack provider (for example when a `SessionDiagnosticsModel` is created). Both threads test
  `g_refs == nullptr` and both assign it, which is a data race (UB, a leak at best). Separately, the
  fact that these caches are unsafe is the reason B-01's consumers freeze content for the life of
  the process.
- Fix: the immutable-snapshot owner of B-01. Short of that, use `std::call_once` or a function-local
  static for first build, `std::atomic<std::shared_ptr<…>>` (or a mutex) for the swap, and return
  `shared_ptr<const T>` rather than `const T&` into storage that `reset` deletes.
- Effort: M        - Related: B-01, B-12

### B-05 · Norm standing ("provisional" / "Mark decided") is encoded in prose and parsed by substring; the release dashboard already misreads it
- Category: 10 Configuration vs code · 4 One source per derived fact · 7 Contracts      - Severity: High
- Evidence:
  - `src/Diagnostics/norms/norm.h:62-73` — `enum class NormSource { Heuristic, Seated, Literature, Imported, Measured };`. There is no "decided" or "provisional" state, and `normIsWeak()` (`norm.cpp:250-260`) cannot tell an owner-accepted heuristic from a placeholder.
  - `norm.h:339` — `QString citation; // DOI/PMID, or the note explaining a provisional figure`. One field holds both the DOI join key and design notes; the median length is 617 characters and the longest is 2,583.
  - `tools/metrics/gen_release_status.py:83` — `decided = any('Mark' in (r.get('citation') or '') and 'decision' in (r.get('citation') or '') for r in rows)`
  - `src/Resources/diagnostics/norms.json:1814` — `m_clubSlotTop`: `"citation": "NEW 2026-11-13 (Mark approved the pair 13 Nov 2026, provisional, …"`. It says "approved", not "decision", so `docs/validation/release_status.md:256` lists `clubSlotTop` with norm basis `heuristic`, not `decided`. `m_leadElbowFoldAfterImpact` ("PROVISIONAL; Mark decides the final value") is likewise invisible to the rule.
- What goes wrong: the hard rule "no norm becomes proven without Mark + evidence" has no field to
  live in. The app cannot show a norm's standing, and the release gate depends on wording. One row
  is already wrong in the generated release status (verified above). Rewording any citation changes
  the dashboard without warning.
- Fix: add explicit, validated fields to `Norm`, for example
  `standing: provisional | decided | seated` plus `decidedBy` / `decidedOn` and a short
  `basisNote`, and keep `citation` for resolvable identifiers only. Have `normIsWeak()` and
  `gen_release_status.py` read the field. Migrate the five plus two prose cases once. Adding a
  field to `norms.json` is a content-schema change, so `kNormPackSchemaVersion` should be bumped
  (`norm_pack.h:46`).
- Effort: S–M        - Related: B-06

### B-07 · "Club assumed" demotion never runs live, on the strength of a limitation that is no longer true, and the in-app rule text says it does
- Category: 4 One source per derived fact · 11 Errors and missing values · 13 Comments as a symptom      - Severity: High
- Evidence:
  - `src/Diagnostics/measures/live_measure_source.h:148-150` — `The club therefore carries the house-wide "DRIVER" stub for a shot whose review block declares none … so this seam cannot tell an undeclared club from a declared driver`
  - But `src/Diagnostics/measures/measure_sample.cpp:1000` — `grid.club = swingDocDeclaredClub(root);`. That is the resolution *without* the stub (`Core/club_vocabulary.h:117`), so `club()` is empty for an undeclared club.
  - `src/Diagnostics/norms/context_tree.cpp:278` — `if (c.isEmpty()) return kDefaultContextId();`. As a result `LiveMeasureSource::contextId()` (`live_measure_source.cpp:341`) is never empty, and `norm_measure_source.h:179` `r.contextInferred = m_contextId.isEmpty();` is never true. The 0.7 demotion at `characteristic_engine.cpp:303` therefore never applies on the live path (`session_diagnostics_model.cpp:1009-1010` passes `QString()` as the override).
  - `src/Gui/diagnostics/session_diagnostics_model.cpp:1034-1038` repeats the false premise and hard-codes `r.contextInferred = false;`.
  - `src/Metrics/gate_catalogue_rules.h:138-142` — `rule.diag.inferredContext`: `"When a shot has no club set, its readings are judged against the default club's range and trusted less, so a finding is kept but marked"`.
  - The design: `norm_provider.h:108-109` `the caller is responsible for marking the finding inferred; see the engine's confidence demotion.`
- What goes wrong (deterministic from the code; not run): every finding on a shot with no club set
  is graded against the default corridors at full confidence and is never marked "club assumed". So
  its ranking weight and wording overstate certainty, and the Gates page describes a safeguard that
  does not run.
- Fix: in `detectForSwing`, set inferred when `values.club().isEmpty()` (pass `QString()` as the
  context to `NormMeasureSource` and let it fall back, or give `NormMeasureSource` an explicit
  `contextInferred` flag). Delete the two stale comments and the hard-coded `false`.
- Effort: S        - Related: B-16

### B-04 · The developer guide describes an earlier system: "execution layer not wired", stale counts, a flat file map
- Category: design–code drift (brief §Inputs) · 14 Dead and leftover      - Severity: Medium
- Evidence:
  - `docs/developer/diagnostics_developer_guide.md:6` — `The EXECUTION layer — running a diagnosis against a real shot — is written, tested, and **not wired**.`; `:70-71` `detect() -> findings ◄ NOT WIRED (§8)`; `:618` `**detect()** … grep confirms no caller outside the module`; `:621` `IMeasureValueSource … implemented only by a test fake`.
  - The code: `session_diagnostics_model.cpp:2690` `const DetectionResult det = detect(pack, src);`, `:2911` `m_explanation = explain(m_packProv->pack(), det, m_screens, m_explainOpt);`, `live_measure_source.h:104` `class LiveMeasureSource final : public IMeasureValueSource {`.
  - `:653` `110 measures, 119 signals, 146 conditions, 306 edges`, and §8.2 says `SignalTest::Threshold` / `Ratio` are used by "no shipped signal". The real figures are 218 / 181 / 192 / 673, with 20 `threshold` and 4 `ratio` signals.
  - `:1053-1078` file map: a flat `src/Diagnostics/` with no `engine/packs/norms/measures`, and `src/Analysis/reference_bands.{h,cpp}` (the file is in `src/Wrist/`). `live_measure_source`, `session_measure_source`, `corridor_plot`, `reference_pack`, `swing_themes_pack` and the memory providers are missing. The guide was last touched in `469f4719` (13 Nov), so it is maintained but not re-read.
  - `docs/design/session_diagnostics_design.md:611-622` places the ledger in "`session_ledger.h/.cpp`, Diagnostics" and the reductions in Analysis. Both are in `src/Coaching/diagnostic_ledger.h`. (The policy numbers *are* injected through options with constant defaults, `diagnostic_ledger.h:243`, so §4 of the design holds.) `docs/design/analysis_dag_design.md` says nothing about Diagnostics.
- What goes wrong: the guide says "read section 8 before assuming any of this runs", so a new
  contributor is told the live diagnosis does not exist and is sent to a file map that does not
  match the tree.
- Fix: rewrite §2, §8 and §14 against the tree. Generate the census, as `release_status.md` is
  generated, rather than keeping it in prose.
- Effort: S        - Related: —

### B-06 · Flight norms are a second norm registry, loaded by Coaching, outside the norm stack
- Category: 5 Duplication · 10 Configuration vs code · 3 Layering      - Severity: Medium
- Evidence:
  - `src/Coaching/strike_from_flight.h:441-442` — `qgetenv("PINPOINT_CORE_FLIGHT_NORMS"); … QStringLiteral(":/diagnostics/flight_norms.json")`; `:446-458` its own `QFile`/`QJsonDocument` loader; `:464-466` `static const FlightNorms k = loadFlightNorms(flightNormsPath());`.
  - `src/Resources/diagnostics/flight_norms.json:2-4` — `"schema": 2` (the other registries use `schemaVersion`) and `"status": "provisional: heuristic starting values…"`, which is free text again (B-05).
  - Nothing in `src/Diagnostics` reads it: no `LoadResult`, no `ValidationReport`, no user layer, no health check, and no `PINPOINT_CORE_*` entry in `provider_leaf_p.h`.
- What goes wrong: a load failure is silent (`shippedFlightNorms()` keeps an empty table), Low/Mid/High
  bands cannot be overridden or inspected in the Diagnostic Model like every other corridor, and
  their standing is not on the release dashboard. Domain file I/O sits in a header-only Coaching
  rule file.
- Fix: make flight bands a registry in `src/Diagnostics/norms` that goes through `pack_io`
  (`LoadResult`, the issue vocabulary, `schemaVersion`) and the library owner of B-01. Coaching then
  receives the table as an argument.
- Effort: M        - Related: B-05, B-13

### B-08 · Wrong-direction dependencies: a Coaching adapter filed in Diagnostics, and a Wrist ↔ Diagnostics cycle the include graph missed
- Category: 3 Layering · 2 Identity      - Severity: Medium
- Evidence:
  - `src/Diagnostics/packs/swing_themes_pack.h:57` — `#include "../../Coaching/swing_themes.h"`. Its siblings, the pack→Coaching adapters, live in Coaching: `src/Coaching/shot_outcomes_pack.h`, `swing_areas_pack.h`, `work_ons_pack.h`. It also reads a global in a default argument (`:133` `const DrillSet &drills = sharedDrillSet()`).
  - `src/Wrist/reference_bands.h:22` — `#include "../Diagnostics/norms/norm_provider.h"` (a producer including Diagnostics), and `src/Diagnostics/norms/norm.h:21` `#include "../../Wrist/wrist_assessment_types.h" // PpRag`, with `norm.h:634` `inline PpRag ragOf(Grade g)`. That makes a directory cycle. `data/include_graph.md` lists only `Diagnostics/norms → Wrist` (it does not scan Wrist as a source).
  - `src/Wrist/reference_bands.cpp:30` — `QStringLiteral("m_%1_p%2").arg(QLatin1String(dofName(dof))).arg(p + 1)` builds measure ids by string format, bypassing the one metric→measure join (`measureForMetricAtPhase`, `characteristic_pack.h`). `:56` `bandEdgesOf(*res.norm, Shape::Target, …)` hard-codes the shape that `norm_provider.h:50-53` says only a caller holding the `Measure` may decide. All 79 `m_<dof>_p<n>` measures are `target` today, so nothing is wrong yet.
- What goes wrong: Coaching cannot be built or tested without Diagnostics' `packs/` and the reverse,
  and a domain value type (`norm.h`) depends on a wrist display enum. If a wrist measure is renamed
  or becomes one-sided, the wrist grid silently loses or misdraws its bands.
- Fix: move `swing_themes_pack.h` to `src/Coaching/`. Move `PpRag` and `ragOf` out of `norm.h` into
  the Wrist/Gui side, or move a neutral 3-band enum down into Diagnostics. Have `NormBandProvider`
  resolve through the pack (`measureForMetricAtPhase` plus the measure's `shape`) and move it beside
  the other norm consumers.
- Effort: S–M        - Related: B-09

### B-09 · `engine / packs / norms / measures` are not layers: each directory holds bottom and top of the stack
- Category: 3 Layering · 6 Cohesion      - Severity: Medium
- Evidence: the real order is vocabulary → model → content → engine → sources → health/layout, but:
  - bottom-layer code in `engine/`: `engine/characteristic.h` (Condition, Edge, the pack struct), `engine/pack_io.h` and `engine/provider_leaf_p.h`, which `packs/` and `norms/` include (`packs/characteristic_pack.h:21,25`, `norms/file_norm_provider.cpp:21-22`, `packs/resource_pack_provider.cpp:21`). A `_p` "private" header is shared across three directories.
  - top-layer code in `norms/` and `measures/`: `norms/norm_measure_source.h:21` `#include "../engine/characteristic_engine.h"`, `measures/live_measure_source.h:23` `#include "../norms/norm_measure_source.h"`, `norms/corridor_plot.cpp:21` `#include "../engine/diagnostics_health.h"`.
  - a stale include from the `pack_io` consolidation: `norms/context_tree.h:21` `#include "../packs/characteristic_pack.h" // ValidationReport`. `ValidationReport` now lives in `engine/pack_io.h`, so every user of the context tree pulls in the 566-line pack header (`norm_pack.h:21` likewise).
  - `norms/norm_provider.h:23` `#include "../packs/pack_provider.h" // PackOrigin`: the norm interface depends on the pack interface for one enum.
- What goes wrong: the eight-way directory cycle in `data/include_graph.md` is produced by this
  layout rather than by real cyclic dependence (there are 0 file-level cycles). Even so, nobody can
  read a layer rule off a path, and nothing stops a real cycle from forming.
- Fix: re-cut by layer, for example `model/` (vocabulary, characteristic, norm, context, pack_io,
  providers), `content/` (loaders, validators, registries), `engine/` (detect, explain, sources) and
  `views/` (health, dag_layout, corridor_plot). Move `PackOrigin` and `ValidationReport` into
  `model/`. Add a CI check on the directory order.
- Effort: M (mechanical moves; many include lines)        - Related: B-08, B-10, B-14

### B-10 · Presentation lives in the domain: pixel layout, UI headings and user sentences in `engine/` and `norms/`
- Category: 3 Layering (presentation wording in domain code) · 15 Names      - Severity: Medium
- Evidence:
  - `src/Diagnostics/engine/dag_layout.h:298-332` — `double nodeH = 40; double gapX = 110; … double charW = 7.0; // approximate advance per character`; `:287` `double strokeWidthFor(Strength s);`. Its only caller is `Gui/diagnosticmodel/model_browser.cpp:4564`.
  - `dag_layout.cpp:785-787` — `out.headings.push_back({ QStringLiteral("Caused by"), …` and `QStringLiteral("Leads to")`. These are untranslated UI strings.
  - `relation_resolver.cpp:110` — `s.reason = QStringLiteral("'%1' and '%2' cannot both describe one swing; …")` (`relation_resolver.h:107` `// one line, for the UI`), also untranslated, built inside `explain()`.
  - `norms/norm.h:683-738` (`gradeLabel`, `rangePhrase`, `actionPhrase`, `implausibleNote`, `cohortLabel`), `norms/corridor_plot.{h,cpp}` (plot layout) and `diagnostics_health.cpp` (25 `tr()` sentences).
  - `dag_layout.h` is also where the `pinpoint::analysis::DagEdge` that crashed every in-app re-analysis was declared (`src/Analysis/analysis_dag.h:91-97`).
- What goes wrong: a layout or wording change rebuilds and retests the engine. Strings built in the
  engine bypass translation. Layout types in the domain namespace are the ones that collide (B-15).
  Keeping the layout in C++ so it can be tested is right; *where* it lives is the issue.
- Fix: move `dag_layout` and `corridor_plot` to a presentation library (for example
  `src/Gui/diagnosticmodel/layout/`, still plain C++ and still unit-tested) in its own namespace.
  Have `explain()` return a reason *code* plus ids, and word it in the façade. Keep `norm.h`'s
  phrase helpers if they are wanted as the single wording source, but in a `norm_words.h` beside the
  façade, not in the value-type header.
- Effort: M        - Related: B-09, B-15

### B-11 · `characteristic_pack.cpp`: one 2.7k-line file with eight jobs; the loader validates and rewrites content as well
- Category: 6 Size and cohesion · 5 Duplication      - Severity: Medium
- Evidence:
  - `validatePack` `characteristic_pack.cpp:1122-1861` (739 lines, 46 distinct issue codes, about 208 comment lines). `loadPack` `:1865-2394` (529 lines), which emits about 30 codes of its own (`unknownState`, `proposedTier`, `bookCitationAtPeerTier`, …) and then calls `validatePack` at `:2389`.
  - The same code is emitted in both places for the same subject: `:2283-2287` `c.provenance.tier = ProvenanceTier::Proposed; warn(r, QStringLiteral("proposedTier"), c.id, …demoted to proposed.")`, then `:1522-1524` `if (c.provenance.tier == ProvenanceTier::Proposed) warn(r, QStringLiteral("proposedTier"), c.id, …)`. One leaf's report carries the finding twice (the merged-provider dedupe, `pack_io.h` `appendUnreported`, does not apply inside a leaf).
  - Other jobs in the same file: the session report's copy rules (`:257-470` `golferPhraseProblem`, `goalProblem`, `reportSentenceProblem`; `:568-877` `reportFieldIssues`), graph algorithms (`:879-1035`), the metric→measure join, shared-evidence stamping (`:2630-2754`), the regroup migration (`:2409-2440`) and the bibliography lookup (`:39-73`).
- What goes wrong: every change to the pack format, the report's copy rules or the causal graph
  edits one file, and the loader/validator split means a rule's code can be raised in either place.
  Load-time rewriting (tier demotion) depends on global state (B-12), and `savePack` then writes the
  rewritten tier into the user's file.
- Fix: split into `pack_json.cpp` (pure parse and serialise, no judgement), `pack_validate_*.cpp`
  (a table of rule functions over the parsed pack, grouped by entity, each owning its codes),
  `pack_graph.cpp`, and `report_content_rules.cpp` (owned by the report/Coaching side). Make
  demotion a validator *warning* plus a derived "effective tier" read at use, rather than a rewrite.
- Effort: M–L        - Related: B-12, B-13

### B-12 · Pure functions read process state: `loadPack` and `diagnosticsHealth` consult the global bibliography, against the area's own stated doctrine
- Category: 12 Testability · 8 State      - Severity: Medium
- Evidence:
  - `characteristic_pack.cpp:46-49` — `This is the one place the pack loader consults process-global state … Where no bibliography is on the path — most standalone test binaries — the shared set is empty, nothing resolves, and the gate below is a no-op.`
  - `diagnostics_health.cpp:321` — `for (const ValidationIssue &i : referenceHealth(pack, sharedReferenceSet()))`.
  - The doctrine it breaks, `diagnostics_health.h:229-231`: `a module takes what it needs as arguments, and a global read here would make the picture depend on process state no test could set.` (`dag_layout` does take `opt.references`, `model_browser.cpp:4560`.)
  - Its cost in the build: `src/Analysis/tests/CMakeLists.txt:54-57` `every target that compiles characteristic_pack.cpp now links reference_pack.cpp too`.
- What goes wrong: `loadPack(sameBytes)` returns different provenance tiers in the app and in a test
  binary, so a test cannot see a demotion the app performs, and a thread-unsafe singleton is pulled
  into every pack load (B-03).
- Fix: `loadPack(bytes, label, const ReferenceSet *refs = nullptr)` and
  `diagnosticsHealth(…, const ReferenceSet &refs)`, with the library owner passing the set.
- Effort: S        - Related: B-03, B-11

### B-13 · The four flat registries copy the load / assemble / cache machinery the leaf template already solved, and silently drop their own load errors
- Category: 5 Duplication · 11 Errors and missing values      - Severity: Medium
- Evidence:
  - `drill_pack.cpp:160-231`, `screen_pack.cpp:172-254`, `reference_pack.cpp:433-513` and `gate_words_pack.cpp:202-280` each hold their own `qgetenv("PINPOINT_CORE_…")`, core load, `assemble…()` (user row replaces core row by id), `g_x` / `g_core` raw pointers, `core…()`, `shared…()` and `resetShared…()`. Compare `provider_leaf_p.h:61-64` `overridePath(...)` and `LeafProvider<Traits>`, which pack and norms already share. `data/cpp_duplicates.md:83-85,121-133` flags the same pairs.
  - The load report is thrown away: `drill_pack.cpp:167-171` `if (f.open(QIODevice::ReadOnly)) { DrillLoadResult res = loadDrillSet(f.readAll(), corePath); out = std::move(res.pack); }`, and `screen_pack.cpp:179-182` likewise. A missing or `schemaTooNew` core file gives an empty set and no error, whereas the pack leaf reports `Could not open the core pack` (`provider_leaf_p.h:71-73`).
  - Sibling file names are spelled twice: `file_pack_provider.cpp:168-171` (the skip list: `"screens.json"`, `"drills.json"`, `"gate_words.json"`, …) and `drill_pack.cpp:42`, `screen_pack.cpp:42`, `gate_words_pack.cpp:34`, `reference_pack.cpp:117`. A new registry file not added to the skip list would be loaded as a Community characteristic pack.
- What goes wrong: a broken `drills.json` shows up only indirectly, as `unknownDrillRef` on every
  condition, and every fix to one registry has to be made four times.
- Fix: a `LayeredSet<T>` (core + user by id, a kept `ValidationReport`, snapshot access) next to
  `LeafProvider`, owned by the library owner of B-01. Give each registry a `kFileName` constant that
  the skip list reads.
- Effort: M        - Related: B-01, B-03, B-06

### B-14 · Diagnostics is not a library: every target hand-lists its sources, and the comments record the link hazards
- Category: 6 Cohesion · 12 Testability · 13 Comments as a symptom      - Severity: Medium
- Evidence: `CMakeLists.txt:3244-3266` (inside `_pinpoint_offline_sources`), `cmake/PinPointQmlModule.cmake:681`, and 17 more lists of `characteristic_pack.cpp` across `src/{Analysis,Gui,Update}/tests/*.cmake|CMakeLists.txt` (19 in all). `src/Analysis/tests/CMakeLists.txt:49-51` `pack_io.cpp … travels with the pack the way reference_pack.cpp does, on every link line that compiles one of them`. `pack_provider.h:90-93` `the pack providers are compiled into test targets one .cpp at a time … an out-of-line default would put the definition in a translation unit half its callers never see.`
- What goes wrong: a new `.cpp` or a new dependency means editing about 20 link lines. Inline-vs-out-of-line
  placement decisions are made for the build rather than the design. This is also how `dag_layout.h`
  reached only the app and not swinglab, which is why the ODR crash was app-only.
- Fix: `add_library(pp_diagnostics STATIC …)` with its Metrics/Analysis/Export dependencies as
  `target_link_libraries`. Tests link the library, and the env-var seams can stay.
- Effort: S–M        - Related: B-09, B-15

### B-15 · Diagnostics shares `pinpoint::analysis` with Analysis and Metrics, using generic type names; it has already caused a silent ODR crash
- Category: 15 Names and namespaces      - Severity: Medium
- Evidence: 59 `namespace pinpoint::analysis` blocks in the four directories. Generic top-level names live there: `struct Edge` (`engine/characteristic.h:407`), `struct Measure` / `struct Signal` (`measures/measure_vocabulary.h:229,387`), `Condition`, `Norm`, `Grade`, `DagNode` / `DagEdge` (`dag_layout.h:166,242`). `src/Analysis/analysis_dag.h:91-97` — `StageEdge, not DagEdge: src/Diagnostics/engine/dag_layout.h already declares a pinpoint::analysis::DagEdge … every in-app re-analysis of 7 Oct 2026 died at buildStageGraph`. The defence is a rule held in memory ("grep the whole tree first"), not a structural one.
- What goes wrong: the next `struct Edge` or `struct Measure` added anywhere in Analysis links
  silently and corrupts the stack, as happened on 7 Oct.
- Fix: `pinpoint::diagnostics` (with `::layout` for B-10's types), with `using` declarations at
  the seams during the move.
- Effort: M (mechanical, wide)        - Related: B-10, B-14

### B-16 · The in-app rule catalogue restates engine and ledger constants as literals
- Category: 10 Configuration vs code · 5 Duplication      - Severity: Low
- Evidence: `src/Metrics/gate_catalogue_rules.h:142` `tune("diagnostics.inferredContextConfidence", 0.7, "")` against `src/Diagnostics/engine/characteristic_engine.cpp:29` `constexpr float kInferredContextConfidence = 0.7f;` (file-local, so the catalogue cannot reference it). Also `:57` `tune("ledger.patternWilsonLb", 0.30, "")` and the prose `"reaches 30%"` against `Coaching/diagnostic_ledger.h:105` `kDiagPatternWilsonLb = 0.30`, with the same pattern at `:61, :66, :71, :76`. The same file already references the constant elsewhere (`:90-92` `tuned::diagUncertainty::kBorderLo`).
- What goes wrong: re-tuning a constant leaves the Gates page showing the old number. Nothing pins
  the two together.
- Fix: export the constants from their headers (or `pp_tuned_constants.h`) and reference them, as
  `:90-98` already does.
- Effort: S        - Related: B-07

### B-17 · Phase → swing-position mapping is written four times, and the copies disagree on two enumerators
- Category: 5 Duplication · 2 Identity      - Severity: Low
- Evidence: `src/Diagnostics/packs/swing_themes_pack.h:69-86` `pPositionOf` (Downswing and Release fall to `default: … return 0`, "not placed"). `src/Metrics/metric_descriptor.h:424,428` `case Phase::Downswing: return 7; // v1 spelling of P5, same instant` and `case Phase::Release: return 12; // v1 spelling of P8`. `src/Diagnostics/measures/measure_facets.cpp:73-90` `phaseLabel` (P1–P9 plus words). `src/Analysis/analysis_pipeline.cpp:4380-4382` `kPositions`.
- What goes wrong: a measure anchored at `downswing` or `release` would be P5/P8 to Metrics and
  "not placed" (0) to the swing themes. No shipped reducer uses those two tokens today (counted in
  `core.json`), so the disagreement is latent.
- Fix: one `swingPositionOf(Phase)` in Metrics beside `phaseLadderIndex`, used by all four.
- Effort: S        - Related: —

### B-18 · Long engine functions, each a pipeline of phases kept in one body
- Category: 6 Size and cohesion      - Severity: Low
- Evidence: `dag_layout.cpp:187` `layoutDag` (852 lines, 12 numbered phases, `// ── 0.` … `// ── 9. Edge routing`). `relation_resolver.cpp:57` `explain` (374 lines: exclusions / corroboration / noisy-OR posterior). `characteristic_engine.cpp:132` `evaluate` (353 lines: about 110 of reading and refusal, then `switch (sig.test)` over 4 tests). `norm_pack.cpp:236` `validateNormsAgainst` (172 lines).
- What goes wrong: each section is internally coherent and well commented, but one section cannot
  be tested without driving the whole function, and locals leak between phases.
- Fix: extract each numbered phase or `case` into a named function over a small working struct. No
  behaviour change.
- Effort: M        - Related: B-10, B-11

### B-19 · Four conventions for "no value" in one result family
- Category: 11 Errors and missing values · 7 Contracts      - Severity: Low
- Evidence: `characteristic_engine.h` `MeasureReading`: `double value = 0.0;` with `bool hasCorridor`, `double measSigma = quiet_NaN();`, `float grossRisk = -1.f;`, `Grade grade = Grade::NotMeasured;`; `Finding`: `float pFire = -1.f;` (`:416`); `IMeasureSource::read` returns `std::optional`. `enum_table_p.h:54-58` `nameOf` returns `rows[0].name` for an unknown value (a silent default). `LiveMeasureSource::missingReason` is valid only after `value()` has been called for that id (`live_measure_source.h:198` `mutable QHash<QString, MissingKind> m_reasons;`, `.cpp:92-95`).
- What goes wrong: each consumer must remember which sentinel applies to which field. Comparing
  `pFire < 0.5` on an unformed −1 reads as "unlikely" rather than "unknown".
- Fix: `std::optional<float>` for pFire / grossRisk and NaN-or-optional for the doubles, chosen once
  and documented in `pack_io.h`'s vocabulary. Return `{value, missingKind}` together from
  `value()`.
- Effort: S–M        - Related: —

### B-20 · Pack versioning: schema 1 forever, with migration keyed on field presence and a side revision that `core.json` lacks
- Category: 9 Persistence and versioning      - Severity: Low
- Evidence: `characteristic_pack.h:49` `kPackSchemaVersion = 1` (and `drill`, `screen`, `reference` and `gate words` are all 1) while the format has grown (`retiredEdges`, `reportHold`, `picture`, `groupsRevision`). `:285` `kPackGroupsRevision = 2` and `:281-282` `core.json carries no marker and needs none, because its tokens are already current.` So `packHasCurrentGroups` (`.cpp:2411-2414`) is false for the shipped core, and on every load each core condition goes through the regroup alias (`.cpp:2419-2428`). I counted 0 rows affected today. Compare the disciplined `kPhaseGridSchemaVersion = 10` with its per-version reasons (`measure_sample.h:107-148`).
- What goes wrong (latent): if the author files a shipped condition back under its pre-regroup
  token (say `aim_bias_closed` → `setup`), the loader silently bounces it to `golfer`. That is the
  exact failure the marker exists to prevent.
- Fix: write `groupsRevision` into `core.json` (or bump `schemaVersion` and key the migration on
  it), and keep the per-version log in the pack header as the sidecar does.
- Effort: S        - Related: B-11

### B-21 · The phase grid re-derives a shot's identity from raw JSON, justified by a comment the includes contradict
- Category: 2 Identity · 13 Comments as a symptom      - Severity: Low
- Evidence: `measure_sample.cpp:981-985` — `Identity fields, spelled exactly as swing_doc.cpp's summaryFromRoot() spells them — that function is the authority and this must not drift from it … Read here rather than borrowed from SwingDocReader because Diagnostics must not depend on Export`. But `:25` `#include "../../Export/swing_store.h"`. The copy (`:986-990` `swing.index`, `clock.wallclock`) matches `Export/swing_doc.cpp:1146-1153` today. `:993-997` still says `THE shared resolver the summary uses (review.club, else capture.club.name, else the stub)` directly above `swingDocDeclaredClub`, which applies no stub (and feeds B-07's false premise).
- What goes wrong: the two can drift, and the comment trail misleads (it already did, see B-07).
- Fix: a shared `swingIdentityOf(root)` in `Export/swing_store` (already linked), and correct the
  club comment.
- Effort: S        - Related: B-07

### B-22 · "Earned club usual" is re-derived at each consumer
- Category: 4 One source per derived fact      - Severity: Low
- Evidence: `Diagnostics/measures/club_baseline_provider.h:54` `kClubBaselineMinShots = 10` (one constant, which is good). The test `n >= kClubBaselineMinShots` is repeated at `live_measure_source.cpp:226`, `Coaching/shot_outcomes.h:437`, `Coaching/club_distances.h:634`, `session_diagnostics_model.cpp:2028` and `work_ons_controller.cpp:2276`.
- What goes wrong: a change to what counts as earned (for example adding a σ floor, as
  `live_measure_source.cpp:226` already does with `sd > 0.0`) has to be repeated five times, and
  the copies already differ on that `sd` test.
- Fix: `bool ClubBaselineEntry::earned() const` (or a field set once by the athlete-level owner).
- Effort: S        - Related: —

## 4. Done well (keep)

- **The engine is pure and level-agnostic.** `detect(pack, IMeasureSource, contexts, contextId)`
  and `explain(pack, detection, knownScreens, opt)` (`characteristic_engine.h`, `relation_resolver.h`)
  take everything as arguments and hold no state. The same `detect` grades a shot
  (`NormMeasureSource`) and a session (`SessionMeasureSource`). Athlete context comes in through
  `IClubBaselineProvider` (`club_baseline_provider.h`), so shot, session and athlete levels are not
  mixed inside the engine.
- **One place for the status rule**: `characteristic_engine.cpp:120-129` `notLive()`. The comment
  explains why it cannot live in the sources.
- **"Could not assess" is never "fine"**: `FindingState::Unavailable` ≠ `NotFired`,
  `Grade::NotMeasured`, `implausible` as a separate state, and `SignalRefusal` with reasons
  (`characteristic_engine.h`, `norm.h:74-78`).
- **`pack_io.h` consolidation**: one issue vocabulary, one `LoadResult<T>` with the `parsed` /
  `loaded` contract, `appendUnreported` deduping on (code, subject), and `atomicWrite`, which checks
  the write and close results (fixing the full-disk truncation its four predecessors shared).
- **`detail::LeafProvider<Traits>`** (`provider_leaf_p.h`) gives the pack and norm leaves one
  implementation; the asymmetry (`admitLayerIssue`) is written down as a trait.
- **Norms are content**, with provenance per row, context-major resolution (specificity beats layer
  precedence, `norm_provider.h:210-218`), and cohorts that never key on an athlete id.
- **Versioned derived data**: the ledger's content stamp (`session_diagnostics_model.cpp:929-944`)
  hashes pack + norms + contexts + row schema + grid schema. The sidecar guard
  (`measure_sample.cpp:687-705`) checks size, mtime *and* reduction parameters, and refuses to cache
  a grid parsed from a stale document (`:1003-1004`). `kPhaseGridSchemaVersion` gives a reason for
  each bump.
- **The excludes keep-rule is shared** (`engine/excludes_rule.h`) by `explain()`,
  `Coaching/shot_outcomes.h:293` and `Coaching/club_distances.h:192`, so the shot and session levels
  cannot disagree about which label stood.
- **No file-level include cycles** (`data/include_graph.md`), and large test coverage at the right
  level (pure C++ tests per module, linked without the GUI).

## 5. June review status

`docs/implementation/code_quality_review.md` (June) has no section on `src/Diagnostics`; most of the
module postdates it. One item touches this area:

- Theme K, `reference_bands.cpp:97` "analyzer thresholds that bypass analysis_tuning.h": **fixed.**
  The compiled corridor table was migrated to `src/Resources/diagnostics/norms.json`, and
  `src/Wrist/reference_bands.cpp` is now 94 lines with no band literals (it is `NormBandProvider`
  over `sharedNormProvider()`). That move created B-08's Wrist ↔ Diagnostics cycle and B-01's cache
  dependency.
- Theme D (duplicated numeric helpers): none found in this area. The nearest analogue is B-17
  (phase→position tables).

## 6. Questions I could not settle from the code

1. Is "edits take effect at the next launch" an accepted product decision? `consistency_env.h:52-53`
   states it as fact, while `ModelBrowser::save()` resets three caches "or [the edit] would be on disk
   and invisible until relaunch" (`model_browser.cpp:5207-5209`). The answer decides whether B-01 is
   a defect or an unwritten rule.
2. Was `ModelBrowser::refresh()` ever wired to panel show or `libraryChanged`? `git log` would say
   whether a caller was removed.
3. Should Mark's acceptance of a provisional norm be a first-class field (B-05), and is the release
   dashboard meant to be the authority on it?
4. Are flight bands meant to be user-overridable like every other corridor (B-06)?
5. Given B-07, was the "club assumed" demotion deliberately switched off live (for example because
   most legacy swings have no club), or does it only look that way because of the stale stub comment?
6. Is the shipped pack ever meant to differ per athlete (a coach's pack per golfer)? If so, the owner
   in B-01/B-02 belongs at the athlete level rather than the app level.
