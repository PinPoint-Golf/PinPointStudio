# Metric Catalogue — developer guide (adding a metric end-to-end)

The **Metric Catalogue** owns metric *identity + metadata* (label, unit, meaning, normative
corridor, requirements) and abstracts *which producer computes each metric*. It is additive metadata
over the existing `MetricSeries` keys — no `swing.json` schema change. This guide is the recipe for
adding a new metric so it appears (correctly) in the directory and resolves per shot.

Companion process doc: [`analysis_pipeline_developer_guide.md`](analysis_pipeline_developer_guide.md)
(stage orchestration + §6.4 "three CMake touchpoints"). Read it first if you are also adding a new
*producer stage*.

## 1. The layer at a glance

```
src/Analysis/                         (pure, Qt-only value types — no Qt-GUI)
  metric_type.h                   MetricType { Summary, PointInTime, TimeSeries, Sequence }
  metric_descriptor.h             MetricDescriptor, MetricRequirement, MetricNormative, NormativeCorridor
  metric_provider.h               IMetricProvider seam, ShotContext, MetricAvailability
  metric_catalogue.{h,cpp}        MetricCatalogue value object + makeMetricCatalogue() factory
  metric_resolver.{h,cpp}         provider fusion + describeRequirement() reason renderer
  metric_catalogue_manifest.cpp   installMetricManifest() — the ONE list of every descriptor
  metric_providers.{h,cpp}        WristMetricProvider / KinematicSeriesProvider / FootMetricProvider
src/Gui/review/
  metric_catalog.{h,cpp}          MetricCatalog : QObject (QML_ELEMENT) — QVariant façade
  MetricLibrary/Detail/Row.qml    the directory + detail screens
```

Design invariants (do not break):
- **Identity is decoupled from production.** A descriptor never names a producer. A provider declares
  which descriptor keys it can satisfy and, per shot, at what quality.
- **No startup singleton / self-registration.** The catalogue is assembled on demand by
  `makeMetricCatalogue()` (mirrors `makeReferenceBandProvider(Kind)`), which installs the manifest
  and a fixed provider set. This matches the Analysis module's ban on stage/provider registration
  (`analysis_stage.h` anti-goals).
- **Live producers, plus documented aspirational.** v1 declares the 12 `MetricSeries` keys real
  producers emit (metric_extractor ×4, kinematic_series ×3, foot_metrics ×5) and 3 `Summary` scores
  sourced from the `ScoreBreakdown` (not `MetricSeries`): `wristScore` / `wristResemblance` (live for
  the Wrist session) and `swingScore` (aspirational — the swing adherence scorer is not wired, so it
  is declared but always resolves `Unavailable`). Prefer live producers; only declare an aspirational
  metric deliberately, and make its provider return `Unavailable` with a clear reason.

Two easy traps:
- The descriptor member is `requirement`, **not** `requires` — `requires` is a C++20 keyword and
  cannot be an identifier.
- The producer `Phase` enum (Address…Finish, `swing_analysis.h`) is **distinct** from
  `PpSwingPosition` (P1…P8, `wrist_assessment_types.h`). DOF normative lookup maps between them via
  the one canonical table `wristCheckpoints()` (`wrist_analysis_adapter.h`) — reuse it, never
  duplicate the mapping.

## 2. Recipe — add a new metric

### Step A — make sure something produces it
A metric must have a live producer emitting a `MetricSeries` with your `key`. Producers are
`AnalysisStage`s pushed into `wristProfile()` / `cameraKinematicsProfile()` in
`src/Analysis/wrist_analyzer.cpp`; each writes into `ctx.series` (scored) or `ctx.detail->series`
(unscored). If you are adding a brand-new producer, follow the analysis-pipeline guide §6 first, then
come back here. Confirm the key, unit, `MetricType` shape, and which phases matter.

Shapes: `Summary` (one value), `PointInTime` (empty curve + one `PhaseSample`, e.g. foot address
scalars), `TimeSeries` (a curve), `Sequence` (ordered peak events — no v1 producer yet).

### Step B — declare the descriptor in the manifest
Add one `cat.addDescriptor({...})` block to `installMetricManifest()` in
`src/Analysis/metric_catalogue_manifest.cpp`. Fill every field; source `description`/`howToRead` from
`docs/` (the wrist/foot/speed prose lives in `docs/design/shot_analyzer_design.md`,
`docs/reference/wristmetrics.md`, `docs/reference/swing_json_schema.md`) — the manifest is where that
scattered prose becomes structured. `usedBy` is static, hand-authored (e.g.
`{"chart:review","score:wrist"}`). Keep `requirement.minTier` at `Angles2D` unless the metric
genuinely needs a higher *camera reconstruction* tier — IMU metrics do not.

```cpp
cat.addDescriptor({
    .key = QStringLiteral("myMetric"),
    .type = MetricType::TimeSeries,
    .label = QStringLiteral("My metric — long name"),
    .shortLabel = QStringLiteral("Short"),
    .unit = QStringLiteral("°"),
    .group = QStringLiteral("Wrist & forearm"),      // reuse an existing group where possible
    .description = QStringLiteral("What it means (from docs/)."),
    .howToRead   = QStringLiteral("Sign, when to read, what good looks like; what it needs."),
    .flexPositive = true,
    .phases = { Phase::Top, Phase::Impact },
    .scored = false,
    .normative = { /* see Step D */ .heuristic = true },
    .requirement = { .imuRoles = { SegmentRole::LeadForearm, SegmentRole::LeadHand } },
    .usedBy = { QStringLiteral("chart:review") },
});
```

### Step C — declare the provider capability
A metric is `Unavailable` until a provider claims its key. Either extend an existing provider or add a
new one in `src/Analysis/metric_providers.{h,cpp}`:
- Add the key to that provider's `provides()`.
- Handle it in `availability(key, ctx)`. Encode **session-profile gating here** (not in the
  requirement struct): e.g. wrist/foot metrics return `wristSessionOnly()` unless
  `ctx.sessionType` is `1` or `-1` (`-1` = directory browse → session-agnostic). Build the
  sensor/camera/club verdict from a `MetricRequirement` via the shared `fromRequirement()` helper so
  reasons read identically to the resolver's `describeRequirement()`.
- If you add a **new** provider class, register a process-lifetime instance of it in
  `makeMetricCatalogue()` (`metric_catalogue.cpp`) with `cat.addProvider(&yourProvider)`.

Resolution rule (in `metric_resolver.cpp`): over all providers that list the key, the best state wins
(`Measured` > `Bridged` > `Unavailable`), ties broken by `priority()`. If no provider claims the key,
it is `Unavailable` with the descriptor requirement rendered as the reason.

### Step D — normative values
- **DOF metric** (wrist / forearm / elbow): set `.normative.dof = PpJointDof::…`. `corridor()` then
  delegates to `makeReferenceBandProvider()` (Phase mapped to `PpSwingPosition` via
  `wristCheckpoints()`). If your DOF is new, add it to `PpJointDof` (append before `Count`) and to the
  `ConfigReferenceBandProvider` table (`reference_bands.cpp`), and extend `dofForMetricKey()`.
- **Non-DOF metric** (speeds, setup scalars): leave `.dof` unset and either supply
  `.inlineCorridors` (a `NormativeCorridor` per phase, same green-core/amber-margin semantics) where
  `docs/` gives a defensible range, or leave them empty — the detail page then shows "no normative
  reference yet" rather than a fake band. There is deliberately no non-DOF band *provider* in v1.

### Step E — CMake (only if you added files)
Manifest/provider edits need no CMake change. New `.cpp` files reach three targets (analysis guide
§6.4): (1) the app — `target_sources(PinPointStudio PRIVATE …)` in the root `CMakeLists.txt`;
(2) the offline stack — `_pinpoint_offline_sources` (or `swinglab_run`/parity link-diverges);
(3) the unit test — `pp_add_test(...)` in `src/Analysis/tests/CMakeLists.txt`. The catalogue's own
sources are already wired.

### Step F — extend the unit test
Add cases to `src/Analysis/tests/metric_catalogue_test.cpp`:
- **completeness**: your key resolves via `descriptor()`; bump the expected count and the per-type
  counts.
- **resolve()**: a `ShotContext` where it is `Measured`, and one where it is `Unavailable` with the
  right reason (session gate / missing sensor).
- **corridor()** (if it has normative): DOF path matches `reference_bands`, or inline path returns the
  corridor; a non-checkpoint phase and unknown key return `nullopt`.

Build + run (targeted, `--parallel 4` per the box's memory cap):
```bash
cmake --build build/analyzer-tests --target metric_catalogue_test --parallel 4
ctest --test-dir build/analyzer-tests -R metric_catalogue --output-on-failure
```

### Step G — verify in the app
Build the app once, open **Settings → Metrics**, confirm the metric appears in its group with the
right unit/short-label and source glyph, and that its detail page renders the meaning, how-to-read,
normative bar (for DOF/inline metrics), requirement, and usedBy. Run the full 7-suite gate before any
release.

## 3. QML façade shapes (for UI work)

`MetricCatalog` (`src/Gui/review/metric_catalog.h`, `QML_ELEMENT`) marshals registry types to
QVariant. Row shape from `query(filters, shotCtx={})`:
`{ key, label, shortLabel, unit, type, group, scored, sources:[…], availability:{state,reason,tier} }`.
Detail from `descriptor(key, shotCtx={})` adds `description, howToRead, flexPositive, phases:[int…],
normative:{contextNote,heuristic,corridors:[…]}, requires:{…}, usedBy:[…]`. **Phases are Phase ints**
— render with `TimelineLabels`. `shotCtx` (all optional): `{ tier, sessionType, imuRoles:[roleName…],
hasFaceOn, hasClubTrack, hasBallTrack, archetype, club, shape }`; empty `{}` = the context-free
directory view.

## 4. Deferred (not in v1)

The new-dashboard rewrite (query-driven zones); the kinematic **Sequence** producer; `tempo` and
`ballPosition` (no producer); wiring a live swing adherence scorer so `swingScore` becomes Measured;
a non-DOF `SpeedBandProvider` / player-baseline normative provider; retiring
`ChartMetrics::shortLabel` once the catalogue is the single source of short names.
