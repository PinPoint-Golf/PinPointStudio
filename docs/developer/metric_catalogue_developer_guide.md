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
- **The full design catalogue — live + planned.** The manifest declares every metric in
  `shot_analyzer_design.md §A`, each either **live** (a producer emits it) or a **planned**
  placeholder (`.planned = true`, no producer yet). Live today: metric_extractor ×4,
  kinematic_series ×3, shaft-lean, foot_metrics ×5, ball_position ×1, head_track ×3,
  tempo_metrics ×2, plus the `wristScore` / `wristResemblance` `Summary` scores (sourced from a
  `ScoreBreakdown`, not a `MetricSeries`).
  Planned: the whole-body rotation / spine / pelvis / club-delivery / kinematic-sequence /
  address-and-impact alignment (shoulder / elbow / hip / feet) metrics and `swingScore`.
- **Placeholders resolve "planned", not "missing sensors".** The `PlannedMetricProvider` claims every
  planned key and always returns `Unavailable` with reason `"planned — not yet produced in this
  build"`, regardless of the shot's capability — a planned metric's `.requirement` is documentation
  of *what it will need*, surfaced as "will need …" on the detail page and a **Planned** badge in the
  directory. **Promoting a placeholder to live:** add the producer, drop `.planned`, move the key out
  of `PlannedMetricProvider::provides()` into a real provider that returns `Measured` when capable,
  and update the metric_catalogue_test counts.

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

The new-dashboard rewrite (query-driven zones); the kinematic **Sequence** producer; wiring a live
swing adherence scorer so `swingScore` becomes Measured; a non-DOF `SpeedBandProvider` /
player-baseline normative provider; retiring `ChartMetrics::shortLabel` once the catalogue is the
single source of short names.

*(2026-07-21: `tempoBackswing` / `tempoRatio` / `ballPosition` left this list — all three now have
producers. `tempoRatio` is also the manifest's first user of `inlineCorridors`, the non-DOF normative
path; worth cribbing from if you need a band for a speed or a setup scalar.)*

## Appendix A — per-metric work plan (capture · detection · calibration · V&V)

The work needed to bring each metric to production, across the pipeline stages. For **live** metrics
the cells describe what is already in place (the producer + its test); for **planned** metrics they
describe the outstanding work. This is a roadmap, not a contract — effort estimates live in the
per-feature plans, and every "validate" step is corpus-scale (a single labelled swing is development
data only). Promote a planned metric only when all four columns are satisfied.

**Legend.** Capture: `F/H/U` = lead forearm/hand/upper-arm IMU · `Plv/Thx/Thg` = pelvis/thorax/thigh
IMU · `FaceCam` = face-on whole-body camera (pose) · `DTL` = down-the-line camera (depth axis) ·
`Club` = shaft/club track · `Ball` = ball track · `Phases` = segmentation phase events.
Calibration: `anat+mount` = IMU anatomical zero + mount check ([[calibration-state-signals]]) ·
`camCal` = camera intrinsic/extrinsic (`cameraFixedInPlace`) · `ground` = ground-plane · `px→mm` =
ball-scale (`setup.ballDetection`) · `stereo` = DTL/stereo extrinsics · `clubDev` = club-device mount.
V&V: unit = header-only standalone test (`src/Analysis/tests`); validation source in parentheses.

### Score

| Metric | Status | Capture | Detection | Calibration | Verification & validation |
|---|---|---|---|---|---|
| `wristScore` | live | F+H (+U) | WristAssessmentEngine rollup ✓ | anat+mount | `composite_score_v2_test` · (corpus: score stability) |
| `wristResemblance` | live | F+H | WristResemblanceScorer ✓ | anat+mount | `wrist_resemblance_test` · (corpus: per-archetype) |
| `swingScore` | planned | Plv+Thx + FaceCam | wire a live adherence scorer (SwingScorer is dark) | anat+mount, camCal | `swing_scorer_test` (exists) · (corpus: adherence vs coach) |

### Wrist & forearm

| Metric | Status | Capture | Detection | Calibration | Verification & validation |
|---|---|---|---|---|---|
| `leadWristFlexExt` | live | F+H | MetricExtractor Cardan-1 ✓ | anat+mount | `wrist_angles_test` · per-rig sign ("check your sensors") · (corpus) |
| `leadWristRadUln` | live | F+H | MetricExtractor Cardan-2 ✓ | anat+mount | `wrist_angles_test` · (corpus; weakest IMU axis ~5°) |
| `forearmPronation` | live | F+H+U | MetricExtractor twist ✓ | anat+mount | `wrist_angles_test` · (corpus) |
| `leadArmFlexion` | live | F+H+U | MetricExtractor elbow angle ✓ | anat+mount | `wrist_angles_test` · (corpus) |

### Body rotation

| Metric | Status | Capture | Detection | Calibration | Verification & validation |
|---|---|---|---|---|---|
| `pelvisRotation` | planned | Plv (slot map lacks a pelvis mount today) | axial `turn(e_ml)` extractor | anat+mount | new unit · (mocap ground truth) |
| `thoraxRotation` | planned | Thx | axial-turn extractor | anat+mount | new unit · (mocap) |
| `xFactor` | planned | Plv+Thx | thorax−pelvis separation | anat+mount (both) | new unit · (mocap) |
| `xFactorStretch` | planned | Plv+Thx | early-downswing peak − top | anat+mount (both) | new unit · (corpus: speed correlation) |
| `hipInternalRotation` | planned | Plv+Thg | thigh-vs-pelvis twist | anat+mount (pelvis+thigh) | new unit · (mocap) |

### Spine & pelvis

| Metric | Status | Capture | Detection | Calibration | Verification & validation |
|---|---|---|---|---|---|
| `spineForwardBend` | planned | Plv+Thx (or 3D cam) | thorax-rel-pelvis flex | anat+mount / camCal | new unit · (mocap) |
| `spineSideBend` | planned | FaceCam (or IMU) | lateral flexion | camCal | new unit · (mocap) |
| `secondaryAxisTilt` | planned | FaceCam | frontal spine vector vs vertical | camCal, ground | new unit · (mocap) |
| `pelvisSway` | planned | FaceCam + ground | lateral pelvis translation | camCal, ground | new unit · (mocap) |
| `pelvisThrust` | planned | **DTL** (optical axis) | toward-ball translation | stereo | new unit · (mocap; needs depth) |
| `pelvisLift` | planned | FaceCam + ground | vertical translation | camCal, ground | new unit · (mocap) |

### Club & speed

| Metric | Status | Capture | Detection | Calibration | Verification & validation |
|---|---|---|---|---|---|
| `clubheadSpeed` | live | Club | `buildKinematicSeries` head-path speed ✓ | px→mm / ground | `kinematic_series_test` · (launch monitor) |
| `handSpeed` | live | Club (grip) | grip-path speed ✓ | px→mm | `kinematic_series_test` · (launch monitor) |
| `lagAngle` | live | Club + FaceCam pose | forearm-vs-shaft angle ✓ | px→mm, pose | `kinematic_series_test` · (strobe/montage review) |
| `impactShaftLean` | live | Club | shaft-lean stage ✓ | px→mm | `shaft_*` tests · (corpus) |

### Club delivery

| Metric | Status | Capture | Detection | Calibration | Verification & validation |
|---|---|---|---|---|---|
| `swingPlane` | planned | Club (DTL best) | SVD best-fit plane of head path | camCal | new unit · (DTL cross-check) |
| `clubPath` | planned | **DTL** + Club | horizontal velocity angle | stereo | new unit · (launch monitor) |
| `attackAngle` | planned | Club (DTL) | vertical velocity angle | camCal / stereo | new unit · (launch monitor) |
| `faceAngle` | planned | **clubDev** (or Club proxy) | clubface-normal angle | clubDev | new unit · (launch monitor) |
| `lowPointAhead` | planned | FaceCam shaft-head + Ball | arc low-point vs ball (needs measured clubhead) | px→mm | `low_point_*` · (corpus) |

### Tempo & sequence

| Metric | Status | Capture | Detection | Calibration | Verification & validation |
|---|---|---|---|---|---|
| `tempoBackswing` | **live** | Phases | Address→Top duration (`tempo_metrics.cpp`; refuses an unconfident ladder) | none | `tempo_metrics_test` · corpus distribution owed |
| `tempoRatio` | **live** | Phases | backswing ÷ downswing time, + propagated 1σ | none | `tempo_metrics_test` · **`truth.event_top_s` still unmeasured — Top error is doubly leveraged here**; corridor provisional pending the Address→Takeaway gap distribution |
| `kinematicSequence` | planned | Plv+Thx+F + Club | per-segment peak-ω order/timing stage (Sequence shape) | anat+mount | new unit · (mocap sequence) |

### Feet & stance

| Metric | Status | Capture | Detection | Calibration | Verification & validation |
|---|---|---|---|---|---|
| `stanceWidth` | live | FaceCam (+Ball) | `buildFootSeries` heel-to-heel ✓ | **ball-diameter px→mm ruler**; falls back to ×frame when no ball | `foot_metrics_test` · mm distribution owed (no corridor yet) |
| `ballPosition` | **live** | FaceCam + Ball | ball projected on the heel line ÷ stance width (`ball_position.cpp`) | none — a same-plane ratio, scale-free | `ball_position_test` · per-club distribution owed |
| `leadFootFlare` | live | FaceCam | foot heel→bigtoe angle ✓ | none | `foot_metrics_test` · (corpus) |
| `trailFootFlare` | live | FaceCam | foot heel→bigtoe angle ✓ | none | `foot_metrics_test` · (corpus) |
| `toeLineAngle` | live | FaceCam | bigtoe→bigtoe line angle ✓ | none | `foot_metrics_test` · (corpus) |
| `leadHeelLift` | live | FaceCam | heel-vs-toe elevation curve ✓ | none (still ×frame — deliberately not converted with stanceWidth) | `foot_metrics_test` · (corpus) |

### Alignment

| Metric | Status | Capture | Detection | Calibration | Verification & validation |
|---|---|---|---|---|---|
| `shoulderAlignment` | planned | FaceCam (DTL for target-line) | shoulder-line angle @Address+Impact | camCal (target-line ref needs DTL) | new unit · (corpus) |
| `elbowAlignment` | planned | FaceCam | elbow-line angle @Address+Impact | camCal | new unit · (corpus) |
| `hipAlignment` | planned | FaceCam (DTL for target-line) | hip-line angle @Address+Impact | camCal | new unit · (corpus) |
| `feetAlignment` | planned | FaceCam | ankle-line angle @Address+Impact | camCal | new unit · (corpus) |

### Head

| Metric | Status | Capture | Detection | Calibration | Verification & validation |
|---|---|---|---|---|---|
| `headSway` | live | FaceCam | `buildHeadSeries` lateral disp ✓ | none (×frame isotropic) | `head_track_test` · (corpus) |
| `headLift` | live | FaceCam | `buildHeadSeries` vertical disp ✓ | none | `head_track_test` · (corpus) |
| `headTilt` | live | FaceCam | `buildHeadSeries` eye-line angle ✓ | none | `head_track_test` · (corpus) |
