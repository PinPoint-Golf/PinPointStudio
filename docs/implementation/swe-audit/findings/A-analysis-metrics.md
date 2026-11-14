# SWE audit — reviewer A: `src/Analysis`, `src/Metrics` and their seams

Pinned at `37289e33`. Read-only review. Scope: `src/Analysis` and `src/Metrics` (excluding `tests/`), their seams with the
producers they call (Pose, Club, Ball, Body, IMU, Kinematics, Wrist, Skeleton3d, Timeline) and with `src/Export`, plus the
GUI's entry points into them (`shot_processor.cpp`, `reanalysis_controller.cpp`, `shot_list_model.cpp`, `disk_replay_source.cpp`)
and the SwingLab write-back (`tools/swinglab/src/swinglab_run.cpp`).

## 1. Summary

The core of the area is sound. One analyzer runs every swing. Stages are gated on what the capture holds. A typed context
carries the work and the declared data flow is checked. The worker never reads settings. Re-analysis loads a swing from disk
through one loader, which the app and SwingLab share. Where the area is weak is everything *around* that core: how an
analysis is made, refreshed, carried and read.

1. **A shot's analysis is made by two job builders that already disagree, and it is read back through four hand-kept
   projections and at least five JSON parsers, with no typed reader** (A-01, A-02). The comments say "mirrored in …, must
   agree" in a dozen places. Live and reloaded copies of the same shot already differ (`standardClubLengthM`, `overall`,
   missing blocks, the chip strings).
2. **Analysis time is path-dependent.** It is absolute on a live shot and window-relative on a re-analysis. So every writer
   and reader guesses the domain from the size of the number (A-03).
3. **Which route or instrument wins a metric is stated four ways.** It is then enforced by key-specific erase passes bolted
   onto the end of `runProfile`, outside the stage graph (A-04).
4. **The metric catalogue is data written as one 5,460-line C++ function.** Three Python tools scrape it with regexes. One of
   its fields (`usedBy`, 118 lists) has no reader (A-06, A-17).
5. **Stage versions only gate reuse within a re-analysis that someone starts.** Nothing records or shows that a library swing
   was analysed by older code. The reuse predicate is written twice and has no test (A-08). Write-back has two hand copies,
   and both differ from the live persist: `savePose` is ignored and the integrity rule differs (A-09).

There are no Critical findings. I found no wrong *data* shown at HEAD that I could prove. Several High findings are "copies
that already disagree".

## 2. How the area is put together

**Making an analysis.** `makeShotAnalyzer()` (`shot_analyzer.cpp:24`) returns one `PipelineAnalyzer`.
`PipelineAnalyzer::analyze` (`analysis_pipeline.cpp:4562`) builds an `AnalysisContext`, which holds:
- the capabilities;
- a `const ShotAnalysisJob&`;
- a `SwingWindow*`;
- a fresh `shared_ptr<SwingAnalysis>`.

It then calls `runProfile(fullProfile(), ctx)`. `fullProfile()` (`:4481`) is an authored list of about 50 stage objects. All
of them are file-local structs in `analysis_pipeline.cpp`. Each stage declares the resources it reads, writes and appends
(`StageDecl`). `runProfile` runs them either as a loop or as a dependency graph (`analysis_dag.h`). After the stages it runs
six more fix-up steps that are not stages: trunk and lead-elbow sensor precedence, route substitution, scoping the shoulder
records, the shoulder tilt against the bend, the club slot, and the P8 refusal. `projectResult` then flattens the output into
`ShotAnalysisResult`:
- a carousel `metrics` string map;
- `tracePoints`;
- `score`;
- `detail`, the typed `SwingAnalysis`.

**Where jobs come from.**
- **Live:** `ShotProcessor::buildAnalysisJob` (`shot_processor.cpp:1170`, 312 lines) builds the job on the UI thread from the
  replay tracks, the athlete and session models, `AppSettings` and `ImuManager`. It runs on a `QtConcurrent` worker. The join
  (`maybeJoin`, `:2190`) then does three things: it writes `swing.json` through `SwingDocWriter::writeSwingJson` on a second
  worker; it projects the typed detail to a `QVariantMap` with `toAnalysisDetail` (`:338`, 342 lines); and it hands that map
  to `ShotListModel::addShot`. **The typed `SwingAnalysis` is not kept after the join.**
- **Re-analysis (in-app and SwingLab):** `reanalyzeSwingDir` (`swing_reanalyzer.cpp:886`) calls
  `SwingDiskLoader::load` (`:309`, 575 lines). The loader parses `swing.json` into a disk-backed `SwingWindow` and a second
  `ShotAnalysisJob`. `reanalyzeSwingDir` then applies version-gated reuse of the pose, ball, shaft and ladder, and of the DTL
  pose, and calls the same analyzer. Two callers write the result back, each with its own copy of the same steps:
  `ReanalysisController::onWorkerFinished` (`reanalysis_controller.cpp:130`) and `swinglab_run --write-back`
  (`swinglab_run.cpp:734`). The SwingLab lab mode calls `SwingDiskLoader::load` plus `analyze()` directly, without the reuse
  gate.
- **Launch-monitor rows** sit inside `analysis.metrics`. Three Export functions write them: `updateLaunchMonitor`,
  `refreshLaunchMonitorRows` (`swing_doc.cpp:2037`) and the carry-forward inside `writeSwingJson` (`:1644–1700`).
  `tools/launchmonitor/lm_repair.cpp` calls the first two.

**Who owns results at runtime.** The document is the only durable owner. On screen, a shot's analysis exists only as
`QVariantMap` copies:
- the live replay map (`m_replayAnalysisDetail`);
- the shot-list row (`QmlPayload analysisDetail`, from `toAnalysisDetail` live, or from `readSwingJson` after `refreshShot` /
  `attachSwingDir`);
- `DiskReplaySource::m_analysisDetail`, built by its own JSON walk;
- Charts' `SessionSeriesModel` and Diagnostics' `measure_sample.cpp`, which each parse `analysis.metrics` themselves.

**Metrics.** `MetricCatalogue` holds 134 `MetricDescriptor`s. `installMetricManifest` installs them, and 21 claim-list
providers are added to them. `resolve()` answers availability from each descriptor's route ladder. The gate catalogue
(`gate_catalogue*.h`, header-only, about 2,200 lines) holds the reader-facing words for gate records. `series_reduce.h`,
`metric_channel.h` and `variability/` hold the reducers and graders that the producers, Diagnostics and the GUI share.

## 3. Findings

### A-01 · Live and re-analysis build the `ShotAnalysisJob` in two hand-written builders, and they already disagree
- Category: 5 Duplication (parallel pipelines), 4 One source per derived fact      - Severity: High
- Evidence:
  - `src/Analysis/swing_reanalyzer.cpp:839` — `if (st.valid) job.standardClubLengthM = st.lengthMm / 1000.0;`. This is the
    only writer in `src/` and `tools/` (grep). `ShotProcessor::buildAnalysisJob` (`shot_processor.cpp:1170–1482`) never sets
    it.
  - Consumers: `analysis_pipeline.cpp:2959` — `} else if (ctx.job.standardClubLengthM > 0.5) { in.clubLengthM = ctx.job.standardClubLengthM; in.clubLengthSigmaM = unc::kClubLenSigmaStandardM;`
    (DtlPosture). Also `:3369–3371` (Skeleton3D).
  - `src/Analysis/shot_analyzer.h:68–71` documents the field with no caveat that it is reload-only: "Only the 3-D skeleton
    reads it". This is wrong on two counts: DtlPosture reads it too, and the live path never fills it.
  - Handedness is parsed twice. `shot_processor.cpp:1264` — `job.handedness = hand.compare(QLatin1String("Left"), …) == 0 ? 2 …`.
    `swing_reanalyzer.cpp:784` is the same expression.
  - Camera roles are decided twice. Live uses the Gui enum: `shot_processor.cpp:1181` — `track.ctrl->perspective() == CameraInstance::FaceOn`.
    Re-analysis uses magic ints: `swing_reanalyzer.cpp:429` — `…[QStringLiteral("perspective")].toInt() == 2)`, and `:495` (`== 4`), `:515` (`== 1`).
  - Fields only one builder sets: `markerSourceId` is set live only (`shot_processor.cpp:1210`) and never read. `ballBaseline`
    and `path` are set on re-analysis only.
- What goes wrong: take a camera swing with a DTL camera, where the session club has no `lengthMm` on the athlete's club
  record. Live, DtlPosture's ball distance is computed with the driver default and its wide σ, and Skeleton3D holds 1.12 m.
  Re-analyse the same swing: both use the standard length of the declared club. The live document and its re-analysed self
  differ in `ballReachExcess`, the posture σ and the skeleton fit. The live value is the worse one, and nothing marks either
  as path-dependent. **Suspected** as to size (not reproduced). The difference in inputs is certain from the code. Every new
  job field has to be remembered in both builders.
- Fix: one resolver, `ShotAnalysisJob jobFromCapture(const CaptureRecord&, const LiveExtras*)`. `CaptureRecord` is the typed
  `capture`/`athlete`/`streams.setup` content the exporter already writes (`SwingExporter::captureBlock`). The live path
  builds the record first and then derives the job from it. Live-only extras (the progress callback, the ball accumulator
  snapshot, IMU bindings from live instances) are passed separately. Then the two paths cannot resolve a field differently.
- Effort: M        - Related: A-13, A-18, A-07 (A-07 for the perspective enum)

### A-02 · One analysis, one writer, four hand-kept projections, no typed reader — and the copies already differ
- Category: 5 Duplication, 7 Contracts, 4 One source per derived fact      - Severity: High
- Evidence:
  - Writer: `src/Export/swing_doc.cpp:270` `serializeAnalysis` (628 lines).
  - The live projection: `src/Gui/shot/shot_processor.cpp:338` `toAnalysisDetail` (342 lines). Its comment at `:362` reads
    "Mirrored in disk_replay_source.cpp: a live shot and its reloaded self must agree."
  - The row reload: `src/Export/swing_doc.cpp:2259–2340` (`readSwingJson` builds `analysisDetail` with `toVariantMap`).
  - The replay reload: `src/Gui/shot/disk_replay_source.cpp:266–466`, its own walk, with "ShotProcessor's twin" at `:288`,
    `:298`, `:305`, `:315`.
  - Typed or partial JSON re-parsers of `analysis.metrics`: `src/Diagnostics/measures/measure_sample.cpp:215`,
    `src/Gui/charts/session_series_model.cpp:291`, `swing_doc.cpp:1102` (`impactMetricsFromAnalysis`).
  - QVariant re-parsers: `charts_traces_presenter.cpp:451`, `charts_relations_presenter.cpp:199`, `chart_metrics.cpp:782`,
    `lm_session_model.cpp:101`.
  - `src/Analysis/uncertainty_json.h:21–23` patches over the symptom: "the document writer …, the reuse reader …, the live
    detail bridge … and the reload … cannot drift". Per-field helpers shared across four hand-written walks.
  - No `MetricSeries`-from-JSON function exists anywhere (grep for `fromJson` / `seriesFrom`: none).
  - Differences observed between the copies:
    - (a) `disk_replay_source.cpp:344` — `{ QStringLiteral("overall"), an[QStringLiteral("score")].toInt() }`. Since
      `pinpoint.analysis/3`, `score` is an object (`serializeScore`, `swing_doc.cpp:232`), so this is always 0. Live gives the
      real score.
    - (b) `readSwingJson` carries no `kinematicSequence`, `dtl` or `addressMarks`, while `toAnalysisDetail` does. Consumers
      patch the gap: `charts_traces_presenter.cpp:3484` — `if (sh->kinematicSequence.isEmpty()) sh->kinematicSequence = s.kinematicSequence;`.
    - (c) Live phases carry `segment`; the replay reload's phases do not (`disk_replay_source.cpp:337–340`).
    - (d) `session_series_model.cpp` reads `routeId` but not `routeDirect` or `withheld`. `measure_sample.cpp` reads all three.
    - (e) Times are absolute live and relative on reload (A-03).
    - After a re-analysis, `ShotListModel::refreshShot` replaces the live-shaped map with the `readSwingJson`-shaped one
      (`shot_list_model.cpp:270`), so the same row changes shape.
- What goes wrong: each new analysis field must be added to between three and seven places by hand. The ones that were missed
  are already visible:
  - (a) is a dead key today: I found no reader of `overall`.
  - (b) is patched in one presenter and not necessarily in others.
  - Every surface has its own idea of which fields exist.
  I have not proved a wrong number on screen at HEAD. The mechanism is the same one the `+0°` chip bug came from (A-12).
- Fix:
  1. A typed reader `SwingAnalysis swingAnalysisFromJson(const QJsonObject&, qint64 t0)` in Export, beside
     `serializeAnalysis`, round-trip tested.
  2. One projection `QVariantMap analysisDetailFor(const SwingAnalysis&)`, used by the live join, `readSwingJson` and
     `DiskReplaySource`.
  3. Diagnostics and Charts read `SwingAnalysis` (or a typed `MetricSeries` view), not JSON.
  Better still, keep the typed `shared_ptr<const SwingAnalysis>` as the shot's owned object and derive the QML map from it on
  demand.
- Effort: L        - Related: A-03, A-12; cpp_duplicates.md rows 31, 36, 67, 86 (`swing_doc.cpp` ↔ `shot_processor.cpp` clones)

### A-03 · Analysis time is in two domains depending on the path, so the domain is guessed from the size of the number
- Category: 2 Identity (time base), 7 Contracts      - Severity: High
- Evidence:
  - `src/Gui/shot/shot_processor.cpp:1218–1224` — "live SwingWindow entries and job.impactUs … are absolute buffer-clock … the
    offline loader passes window-relative ball + window-relative entries … Do NOT rebase here".
  - The writer guesses: `src/Export/swing_doc.cpp:281–284` — `return tt >= windowT0 ? tt - windowT0 : tt;`.
  - The readers guess the same way:
    - `src/Gui/shot/disk_replay_source.cpp:68–72` — `return t >= t0 ? t - t0 : t;`;
    - `src/Gui/charts/session_series_model.cpp:189` (`relUs`);
    - `src/Gui/report/report_stills_pure.h:121` — `inline qint64 relUs(qint64 t, qint64 t0) { return t >= t0 ? t - t0 : t; }`.
  - The loader contradicts the writer: `src/Analysis/swing_reanalyzer.cpp:742` — "NB: analysis.phases t_us are ABSOLUTE", then
    `:777` — `impact = qint64(pv.toObject()[QStringLiteral("t_us")].toDouble()) - t0Us;`. But `serializeAnalysis` writes phases
    through `rel()`, so they are already relative.
- What goes wrong: every consumer must know that a `SwingAnalysis` in memory may be in either domain, and that a document may
  be in either too (older files). The magnitude test stands in for an identity.
  - For any document written by the current writer, the reanalyzer's phase-based impact fallback (`:775–778`) produces a
    negative value and is skipped. It only fires when `capture.impactUs` is absent. **Suspected** latent defect; I did not
    reproduce a swing without `capture.impactUs`.
  - The heuristic misfires whenever a relative time is at least `t0`. That is unlikely with a `steady_clock` epoch at boot,
    but not impossible.
- Fix: normalise at the analyzer boundary.
  1. `SwingWindow` exposes window-relative time on both paths, or `PipelineAnalyzer::analyze` rebases its output once using
     `window.startTimestampUs()`.
  2. `SwingAnalysis` records the domain it is in (`timeBase`).
  3. Persist with an explicit `analysis.timeBase` and drop the heuristics. Keep one migration for old absolute documents.
- Effort: M        - Related: A-02

### A-04 · Route and instrument precedence is stated four ways and enforced by bespoke erase passes after the run
- Category: 4 One source per derived fact, 10 Configuration vs code, 5 Duplication      - Severity: High
- Evidence:
  - The catalogue's statement: `src/Metrics/metric_descriptor.h:556–559` — "Every way this metric can be acquired, ORDERED BEST
    FIRST … the first decides which route wins on a capable shot". It is used only for availability (`metric_providers.h:30–40`).
  - The analysis layer's own ladder: `src/Analysis/swing_analysis.h:336–343` — `const QString preferred = QStringLiteral("hm.") + key;`.
    Its comment (`:329–334`) names a third copy, `Measure::preferKeys` in the diagnostics pack.
  - Hard-coded post-run passes:
    - `src/Body/upper_body_metrics.cpp:1360–1395` — `m.key == QLatin1String("leadArmFlexion") && m.routeId == QLatin1String("faceOn")`
      … `g.gate == QLatin1String("body.leadElbowLines") || g.gate == QLatin1String("body3d.leadElbowUnmeasured")`;
    - `src/Body/trunk_sensor_series.h:580–586` — `{ "spineForwardBend", &trunk_sensor::bendRouteId }, …`.
  - `runProfile` grows one such step per feature: `analysis_pipeline.cpp:4660–4738`. There are six steps, none of them a stage,
    none in the DAG or the stage trace.
  - Two conventions for "which instrument". A wG3 writes a new key: `hm.leadWristFlexExt` (`metric_providers.cpp:57–59`). A
    trunk sensor writes the same key with a `routeId` (`trunk_sensor_series.h:580–586`).
  - The writer admits the gap: `src/Export/swing_doc.cpp:1660–1663` — "a stage that starts emitting one must replace the
    carried row rather than sit beside it as a duplicate the resolver would pick between arbitrarily".
- What goes wrong: when a second producer starts writing an existing key, someone must remember to add another hand-written
  erase pass, with literal key, route and gate ids, to the tail of `runProfile`. If they don't, the document carries two
  series with one key, and readers that take the first match (`findSeriesByLadder`, the `m.key == key` loops) pick by stage
  order. The ladder in the catalogue, which *says* which route wins, is never consulted at analysis time.
- Fix: one post-run step, `resolveRoutes(series, gates, catalogue)`. It orders the candidates for each key by the descriptor's
  route ladder (keyed on `routeId`), keeps exactly one series per key, and moves `GateRecord.covers` off the losers in a
  data-driven way. The per-metric exceptions (a face-in sensor, fewer positions) become route predicates on the descriptor.
  Settle on one instrument convention: routeId on the bare key. Retire the `hm.` prefix with a migration, or document why it
  must stay.
- Effort: L        - Related: A-05, A-07

### A-05 · `analysis_pipeline.cpp` is the orchestrator plus 50 adapter stages full of domain logic, unreachable from the default tests
- Category: 6 Size and cohesion, 12 Testability, 10 Configuration vs code      - Severity: Medium
- Evidence:
  - 4,750 lines. About 50 `struct …Stage : AnalysisStage` in one anonymous namespace (`:439` FrameDecode … `:4037`
    AddressMarks).
  - Long bodies: `ShaftFusionStage::run` is 336 lines (`:2565`), `Skeleton3DStage::run` 288 (`:3348`), `AddressMarksStage`
    about 430 lines with helpers.
  - 98 `fromOverrides` / `tuning::apply` calls in the file.
  - Inline time windows that bypass `pp_tuned_constants.h`: `:2588` `int64_t backFromUs = topUs - 900000;` (repeated at
    `:3878`), `:3378` `in.addressUs = addr ? addr->t_us : impactUs - 1300000;`, `:3379`, `:1004–1006`, `:2315–2323`. About 30
    in total.
  - `ShaftStage`'s reuse branch rebuilds the tracker's forearm line itself (`:845–865`): "the same construction the tracker
    uses", with literals `0.30f`, `8.0`, keypoints `7`/`8` and a π literal at `:864`.
  - `runProfile`'s non-stage tail: `:4660–4738` (A-04).
  - Testability: the stages are file-local, so no unit test can construct one. `analysis_pipeline.cpp` and
    `swing_reanalyzer.cpp` are linked only into the app and `_pinpoint_offline_sources` (`CMakeLists.txt:3222–3227`), under
    `option(PINPOINT_BUILD_TOOLS … OFF)` (`:3217`). The tests' CMake records the cost twice: `src/Analysis/tests/CMakeLists.txt:245–248`
    ("Moved out of analysis_pipeline.cpp's anonymous namespace … to be reachable at all") and `:528–529` ("it lived in an
    anonymous namespace where no test could reach it").
- What goes wrong: a change to one stage recompiles and risks a 4.7k-line TU. Each stage hides small copies of producer logic
  and its own time windows. The only pipeline-level gates (`swing_window_parity_test`, the corpus) are off in the default
  build, so a stage regression surfaces on the cabin PC, not in `ctest`.
- Fix:
  - Move each stage into a file beside its producer (`src/Club/stages/shaft_stage.cpp`, …), with the stage keeping only
    marshalling. Push the domain fallbacks (`impactUs − 1.3 s`, `topUs − 0.9 s`) into the producer or into
    `pp_tuned_constants.h`.
  - Make the post-run steps stages with `StageDecl`, so the trace and the DAG see them.
  - Build `swing_window_parity_test` in the default test set, with fixtures that need no FFmpeg, or split its non-FFmpeg tests
    out.
- Effort: L        - Related: A-04, A-08; June [M] `shaft_tracker::track` god-function (same pattern, moved)

### A-06 · The metric catalogue is data written as a 5,460-line C++ function, and three tools regex-scrape it
- Category: 10 Configuration vs code, 6 Size      - Severity: Medium
- Evidence:
  - `src/Metrics/metric_catalogue_manifest.cpp:106` — `void installMetricManifest(MetricCatalogue &cat)`, 5,460 lines, 139
    `addDescriptor` calls, 118 `usedBy` lists.
  - Each descriptor is about 40 lines of designated initialisers and coach-facing prose (e.g. `:136–163`).
  - `tools/metrics/gen_release_status.py:27` — `for block in re.split(r'cat\.addDescriptor\(\{', src)[1:]:`, with the `.key`,
    `.group` and `.label` regexes at `:28–33`.
  - `tools/metrics/gen_sign_table.py:23` — `re.finditer(r"    cat\.addDescriptor\(\{", text)`, which depends on four-space
    indentation.
  - `tools/theme/metric_palette.py:84–85` reads `.color` the same way.
  - For contrast: the fault model and norms already live as data (`src/Resources/diagnostics/core.json`, `norms.json`, which
    `gen_release_status.py:18` reads as JSON).
  - The gate catalogue repeats the pattern: `appendBodyGates` is 569 lines (`gate_catalogue_body.h:27`), `appendClubGates` 318,
    `appendRules` 263, and all of it is header-only, included by about 14 TUs.
- What goes wrong:
  - Every prose edit recompiles the largest function in the tree.
  - A refactor of the manifest's layout, such as a helper per group, a different indent, or a key from a variable, makes the
    release dashboard (`docs/validation/release_status.md`) and the sign table silently miss metrics, while `--check` still
    passes on the reduced set.
  - The prose is untranslatable (`QStringLiteral`, never through `tr`).
- Fix: move descriptors into `src/Resources/metrics/catalogue.json`, or one file per group, with a schema. Write a small C++
  loader, validated by `metric_catalogue_test`. The Python tools read the JSON. Do the same for the gate catalogue's entries;
  the template engine stays in C++.
- Effort: L        - Related: A-07, A-14, A-15, A-17

### A-07 · Metric keys are repeated string literals across parallel tables kept in step by hand
- Category: 5 Duplication (parallel tables, ids as literals), 15 Names      - Severity: Medium
- Evidence:
  - `src/Metrics/metric_providers.cpp:24–29` — "Every list below must stay in step with what its producer stage actually emits,
    in BOTH directions … `stanceWidthMm` shipped that way … reported Unavailable on all of them".
  - `leadArmFlexion` is claimed by two providers (`:51` and `:97`).
  - `src/Metrics/metric_catalogue.h:41–55` — `isResearchOnlySeries` / `isLabelFromCatalogue`, hard-coded key lists outside the
    descriptors.
  - `"leadArmFlexion"` appears 25 times in 8 files (manifest, providers, `upper_body_metrics.cpp`, `metric_extractor.cpp`,
    `wrist_assessment_types.h`, `swing_data_source.cpp`, `ScreenWrist.qml`, `core.json`). `"spineForwardBend"` appears 24 times.
  - There is no `metric_keys.h` (`find src -name '*metric_keys*'`: none).
- What goes wrong: a renamed or new key needs coordinated edits in the producer, the provider claim list, the manifest, the
  precedence passes (A-04), the Diagnostics JSON and QML. A miss is silent. The provider comment documents one such miss that
  shipped. Tests catch only the directions they were written for.
- Fix: generate a `metric_keys.h` of constants from the catalogue (or declare it once, in C++ beside a JSON catalogue). Derive
  the provider claims from the route ladder: each `MetricRoute` names its producer stage. Make "research-only" and
  "label-from-catalogue" descriptor fields.
- Effort: M        - Related: A-04, A-06; June [M] "MetricSeries keys stringly-typed → `metric_keys.h`" — **open**

### A-08 · Stage versions gate reuse only; library staleness is invisible, the reuse rule is written twice, and it is untested
- Category: 9 Persistence and versioning, 12 Testability      - Severity: Medium
- Evidence:
  - The only reader of `analysis.versions` is `src/Analysis/swing_reanalyzer.cpp:922` (grep of `"versions"` across `src`,
    `tools`). Of the 15 stamps, 11 are "Stamped; never reused" (`analysis_versions.h:83, 91, 109, …`). Some stages have
    no constant at all: "(BodyRotationStage … has no version constant: it always recomputes.)" (`:200–203`). So nothing can say
    "analysed with an older body-rotation rule".
  - `src/Analysis/analysis_versions.h:236` — `bool stamped() const { return pose > 0; }`, and `src/Export/swing_doc.cpp:314` —
    `if (a.versions.stamped()) {`. So a swing with no face-on pose writes **no** versions block, even when
    `shoulderTiltVsBend` or `clubSlotTop` was stamped in `runProfile` (`analysis_pipeline.cpp:4701`). **Suspected**: an IMU-only
    trunk-sensor swing loses those stamps.
  - The reuse rule appears twice side by side: as booleans (`swing_reanalyzer.cpp:931–990`, `poseMatch` / `ballMatch` /
    `shaftMatch`) and again as a reason ladder (`:1009–1043`). The override-prefix list that blocks pose reuse is
    hand-maintained (`:926–930`).
  - No test references `kPoseStageVersion`, `kShaftStageVersion`, `posePreloaded` or `path.reuse` (grep of `src/*/tests`).
- What goes wrong:
  - Bumping a stage constant changes nothing for swings already in the library until someone re-analyses them by hand. The
    diagnostics then pool readings from several code versions with no marker.
  - A change to one copy of the reuse predicate and not the other makes `analysis.path.reuse` explain a decision that was not
    taken.
  - The gate's correctness (never reuse a stale track) rests on the corpus alone.
- Fix:
  - Each `AnalysisStage` exposes `version()`. `runProfile` stamps `{stageName: version}` for every stage that ran, whether or
    not it is reusable, and the block is always written.
  - One `ReuseDecision decideReuse(recorded, wanted, overrides)` returns both the verdict and the reason, with a unit test.
  - The library gets a "stale stages" query that the sessions screen can show (the owner's "show with a warning" rule).
- Effort: M        - Related: A-05

### A-09 · Re-analysis write-back is two hand copies, and both differ from the live persist
- Category: 5 Duplication (parallel pipelines), 9 Persistence      - Severity: Medium
- Evidence:
  - `tools/swinglab/src/swinglab_run.cpp:728` — "Mirrors ReanalysisController::startNext()/onWorkerFinished() exactly". Same
    steps in the same order at `:734–803` and `reanalysis_controller.cpp:71–175`: device-only check →
    `refreshLaunchMonitorRows` → `reanalyzeSwingDir` → reload manifest → empty guard → `applyImuIntegrity` →
    `applyCaptureIntegrity` → `writeSwingJson`.
  - `savePose` is ignored. Both call `writeSwingJson(dir, manifest, detail, &err)` (`reanalysis_controller.cpp:163`,
    `swinglab_run.cpp:800`), and the default is `bool savePose = true` (`swing_doc.h:134`). The live join passes the user's
    setting: `shot_processor.cpp:2329–2332` (`… &out.error, shotClub, savePose);`).
  - The integrity rule differs:
    - Live skips the re-fusion check on the *current* setting: `shot_processor.cpp:2234–2236` —
      `m_appSettings->imuOrientationFilter().compare(QStringLiteral("ESKF"), …) == 0`.
    - Re-analysis runs it only if the *recording* says Madgwick: `swing_reanalyzer.cpp:1093–1095`.
    - Live exempts only the impact camera (`shot_processor.cpp:2293`). Re-analysis also exempts off-clock cameras
      (`swing_reanalyzer.cpp:1108–1109`).
  - Manifest gates are appended in two places: `swing_doc.cpp:1627` (`manifestGatesOf(rawManifest)`) and
    `shot_processor.cpp:2412–2417` ("the same rule writeSwingJson applies").
- What goes wrong:
  - With Storage → "Save pose keypoints" off, live documents omit `pose2d`/`poseDtl`. An in-app re-analyse writes them back
    (about half the document), against the user's setting.
  - A live shot recorded with no `orientationFilter` gets an `imuIntegrity` verdict. Its re-analysis removes it.
  - Each future write-back step must be added to both copies.
- Fix: one `bool persistReanalysis(const QString &dir, const ReanalyzeResult&, const PersistOptions&)` in Export, called by
  the controller and by SwingLab. `PersistOptions` carries `savePose`, which the controller fills from `AppSettings`. One
  `integrityPolicy(recording, settings)` used by both live and re-analysis.
- Effort: S        - Related: A-01, A-10, A-19

### A-10 · Launch-monitor rows live inside `analysis.metrics`, owned by another writer, so every analysis writer must carry them
- Category: 1 Ownership, 9 Persistence      - Severity: Medium
- Evidence:
  - `src/Export/swing_doc.cpp:1644–1652` — "re-analysis owns what it computed and must not evict what it cannot recompute … No
    stage produces an `lm.` row … replacing the block wholesale deleted them — silently, and on every re-analysis".
  - Three writers of the same rows:
    - `updateLaunchMonitor` (`:1978–1982`);
    - `refreshLaunchMonitorRows` (`:2037–2075`), which regenerates both `lm.` and derived rows from the raw block
      (`:2062`);
    - the carry-forward in `writeSwingJson` (`:1667–1694`), which keeps `lm.` rows *verbatim* and re-derives only the derived
      rows (`:1672`, `:1684`).
- What goes wrong: there are two owners of one array. If `lmMetricEntry`'s shape changes (a σ, a label, a phase stamp), a
  swing re-analysed after the change keeps old `lm.` rows, while a swing refreshed by `lm_repair` gets new ones, so the same
  reading differs between swings in one session. The silent-deletion bug the comment describes is a direct symptom of the
  shared ownership.
- Fix: persist LM readings as their own product (for example `launchMonitor.metrics`, derived from the raw block). The
  reader or resolver merges them into the series view. `serializeAnalysis` then owns `analysis.metrics` outright. At the very
  least, all three writers should call one `lmRowsFor(root)`.
- Effort: M        - Related: A-09

### A-11 · Producers depend on the orchestrator's types; Analysis ↔ every producer is a directory cycle
- Category: 3 Layering      - Severity: Medium
- Evidence:
  - `src/Club/shaft_tracker.h:62–68` — `static ShaftTrack2D track(… const Segmentation &segmentation, const ShotAnalysisJob &job, …)`.
    `shaft_tracker.cpp` reads `job.handsEndMm`, `job.priorClubLenPx`, `job.tuningOverrides` and `job.fullWindow` (`:83–252`).
  - `src/Club/dtl_shaft_tracker.cpp:48` and `src/Club/ball_anchor.cpp:26` — `#include "../Analysis/shot_analyzer.h" // ShotAnalysisJob`.
  - Producer → Analysis includes: Club 26 files, Body 11, Wrist 10, Pose 6, Timeline 5, Ball 4, Kinematics 4, IMU 3. Examples:
    `src/Club/dtl_shaft_config.h:46` (`analysis_tuning.h`), `src/Club/shaft_fusion.h:27` (`det_rng.h`),
    `src/Kinematics/kinematic_sequence.h:46` (`gate_record.h`).
  - `src/Analysis/swing_analysis.h:22, 41–47` includes `../Body/address_marks.h`, `../Wrist/…`, `../Kinematics/…`, `../Club/…`
    and `../Skeleton3d/…`.
  - `ShotAnalysisJob` mixes production inputs with SwingLab seams: `poseTrackPath`, `ballTrackPath`, `tuningOverrides`,
    `skeletonCalib`, and `dtlCameraCalib` ("the app has no producer yet", `shot_analyzer.h:154`).
  - The include tool resolved only `../` includes, so its table under-counts these edges.
- What goes wrong:
  - Every field added to the job, including test-only seams, is visible to and recompiles the producers.
  - A producer cannot be tested or reused without the orchestrator's job type.
  - "The shared vocabulary" (`MetricSeries`, `Segmentation`, `GateRecord`, the tuning map) sits in the directory that is
    supposed to be above the producers.
- Fix: move the vocabulary (`swing_analysis.h`'s value types, `gate_record.h`, `segment_role.h`, `det_rng.h`,
  `analysis_tuning.h`, `uncertainty_config.h`) to a leaf module, e.g. `src/AnalysisModel/`. Producers take their own input
  and config structs (`ShaftTrackerInputs`), and the stage builds those from the job. Move the SwingLab seams into a
  `LabOverrides` sub-struct.
- Effort: L        - Related: A-05, A-20

### A-12 · Analysis builds carousel presentation strings, and its copy disagrees with the reload copy
- Category: 3 Layering (presentation in domain), 5 Duplication, 11 Missing values      - Severity: Medium
- Evidence:
  - `src/Analysis/analysis_pipeline.cpp:302–312` — `out.insert(m.key, QVariantMap{ … { QStringLiteral("value"), wristMetricLabel(m.key, phaseValue(m, Phase::Impact)) } })`.
  - `:293–297` — `phaseValue` falls back to `return m.value.empty() ? 0.0 : m.value.back();` when the series has no Impact
    sample.
  - `src/Wrist/wrist_angles.h:254–267` — any key other than the four named gets `QString::number(a) + "°"`, where `a` is the
    absolute value.
  - The reload copy, `src/Export/swing_doc.cpp:1102–1137` (`impactMetricsFromAnalysis`), skips metrics with no Impact sample
    and formats by unit. Its comment at `:1117–1119` records the bug it fixed: "hardcoding "°" rendered a ×frame heel lift or
    an mph clubhead speed as "+0°"". The live copy still behaves that way.
  - `buildTrace` (`:316–350`) builds sparkline coordinates (`0.9 - 0.8 * …`) inside the analysis.
- What goes wrong: a shot's chip map differs between live and reload in both its key set and its strings. Live has every
  series, including ones with no Impact reading; reload has only Impact-sampled ones. Live writes "102°" for a clubhead speed;
  reload writes "102mph". The visible effect at HEAD is limited, because `PpShotCard.qml:74, 88` reads only the *keys*
  (`sawMotion`, `deviceOnly`). Even so, those two flags can differ between a shot's live row and its reloaded row. The
  missing-value rule ("-", never a fake value) is broken in the live copy.
- Fix: delete `ShotAnalysisResult::metrics` and `tracePoints`. Make one GUI-edge function derive the chips from the typed
  detail or the document, used by both the live join and `readSwingJson`.
- Effort: S        - Related: A-02

### A-13 · Domain identities are defined in the GUI and travel down as magic integers
- Category: 2 Identity, 3 Layering      - Severity: Medium
- Evidence:
  - Camera perspective:
    - `src/Gui/cameras/camera_instance.h:145` — `enum Perspective { None = 0, DownTheLine = 1, FaceOn = 2, Other = 3, Impact = 4 };`;
    - `src/Analysis/swing_reanalyzer.cpp:429` (`== 2`), `:495` (`== 4`), `:515` (`== 1`);
    - the same ints in `report_stills_pure.cpp:465, 474`, `markup_truth.cpp:99` and `disk_replay_source.cpp:216`;
    - a copy of the enum for tests in `src/Gui/tests/setup/setup_enum_standins.h:46`.
  - Shot source: `swing_reanalyzer.cpp:768` — `captureIn[QStringLiteral("shotSource")].toInt(-1) == 4` ("Gui enum, compared as
    the persisted int").
  - Session type: `shot_analyzer.h:40`, `int sessionType … (Swing 0, Wrist 1, GRF 2, Coach 3)`.
  - Handedness: 0/1/2 from strings in two places (A-01).
- What goes wrong: reordering or extending a Gui enum silently changes what the analysis layer reads from every stored swing.
  The analysis cannot name these values without a Gui header, so it compares integers that a test has to pin
  (`shot_source_map_test`).
- Fix: Core enums (`CameraPerspective`, `ShotSource`, `Handedness`) with explicit persisted names or values and to/from
  helpers. The Gui and the analysis both use them.
- Effort: S        - Related: A-01

### A-14 · Tuning overrides are an untyped string map with no validation, though the job's comment says otherwise
- Category: 11 Errors and missing values (silent failure), 7 Contracts      - Severity: Medium
- Evidence:
  - `src/Analysis/analysis_tuning.h:31–55`: each `apply` does `const auto it = ov.constFind(QLatin1String(key)); if (it != ov.cend()) field = …`.
    There is no registry and nothing reports unused keys.
  - `src/Analysis/shot_analyzer.h:198` — "Unknown keys are logged and ignored". No code logs them (grep for "unknown key" in
    `src/Analysis` and `swinglab_run.cpp`: none).
  - 98 resolution sites in `analysis_pipeline.cpp`.
  - Reuse depends on key *prefixes* (`swing_reanalyzer.cpp:926–930`).
- What goes wrong: a SwingLab params file with a misspelt key (`shaft.ridgeKernalPx`) runs the baseline. `analysis.path.overrides`
  still lists the key, so the run looks like the candidate. A non-numeric value becomes 0 through `toDouble()`.
- Fix: each config's `fromOverrides` registers the keys it reads. After the run, `runProfile` compares the map against that
  set: SwingLab treats an unknown key as fatal, and the app logs it. Record consumed keys rather than supplied ones in
  `path.overrides`.
- Effort: S        - Related: A-08

### A-15 · Coach-facing wording lives in Metrics, and part of it contradicts the code
- Category: 3 Layering (presentation in domain), 13 Comments as a symptom      - Severity: Medium
- Evidence:
  - Six descriptor strings still say a metric is produced only in one session type:
    `src/Metrics/metric_catalogue_manifest.cpp:155` — "is produced only in a Wrist Motion session.", plus `:246`, `:1886`,
    `:3598`, `:3632`, `:3703` ("Wrist Motion session"). The same file's `:85` says the opposite: "NO DESCRIPTOR AND NO PROVIDER
    MAY READ `sessionType`". So does `src/Analysis/shot_analyzer.cpp:26` ("One analyzer for every swing").
  - The model browser shows this prose (`src/Gui/diagnosticmodel/model_browser.cpp:1901`, `d->description, d->howToRead`).
  - Reader wording is built in Metrics:
    - `src/Metrics/gate_catalogue.h:395–440` (`routeWords`, `subjectLabel`: "Pelvis peak", "The whole swing");
    - `src/Metrics/variability/variability_result.cpp:79–157` and `variability_typicality.cpp:127–322`
      (`QCoreApplication::translate("VariabilityBasis", "σ %1 · %2 of corridor · n %3")`).
  - Descriptor prose is `QStringLiteral`, so it never reaches `lupdate`, while `variability/` does go through `translate`.
    That is two i18n policies in one module.
  - Upward include: `src/Metrics/variability/variability_typicality.cpp:24` — `#include "../../Gui/typeset/box_stats.h"`.
- What goes wrong: the metric directory tells golfers that a metric needs a session type the analysis no longer cares about.
  Wording changes recompile Metrics and everything that includes the gate headers. The domain module cannot be used without
  Qt's translation machinery.
- Fix: correct the six strings now. Then move the prose and words with the catalogue data (A-06), translated at the GUI edge.
  Move `box_stats.h` (pure statistics) to Core or Metrics.
- Effort: S (strings) / M (move)        - Related: A-06

### A-16 · `SwingDiskLoader::load` is 575 lines with about ten jobs, including a silent data migration on every load
- Category: 6 Size and cohesion, 9 Persistence (where migration lives)      - Severity: Medium
- Evidence: `src/Analysis/swing_reanalyzer.cpp:309–884` does all of the following in one function:
  - parses video, IMU and ball streams;
  - chooses raw or MP4 readers;
  - assigns camera roles, including a regex fallback for files with no setup block (`:519–523`);
  - validates the ball baseline;
  - drops out-of-window ball tracks;
  - derives impact from three sources;
  - applies a legacy acoustic de-bias (`:755–773`, "corrected here to what the new capture chain would have produced");
  - parses handedness, height and club geometry;
  - picks a junior, women's or men's standard club from date of birth and sex (`:822–840`, `junior = age < 18`);
  - rebuilds bindings from two sources, plus HackMotion reconstruction.
- What goes wrong:
  - The de-bias is a migration applied in memory at every load and never persisted. Any other reader of the legacy document
    sees the uncorrected anchor, and every change to the loader risks the others.
  - Most of the block readers duplicate what the GUI's readers parse (`disk_replay_source.cpp` reads `streams` and
    `setup.perspective` again, `:216`).
  - Only the parity test, which is off by default, reaches this function (A-05).
- Fix: typed block readers in Export, each unit-tested: `readCaptureRecord`, `readStreams`, `readBindings`. Put migrations in
  one versioned `migrateSwingDoc(root)` that runs once and stamps the document. `load` then becomes assembly.
- Effort: M        - Related: A-01, A-03, A-05

### A-17 · Leftover fields and dead API at the seams
- Category: 14 Dead and leftover code      - Severity: Low
- Evidence:
  - `ShotAnalysisJob::swingDir` is declared "set at the join" (`shot_analyzer.h:112`). It is never assigned; the
    `job.swingDir` at `shot_processor.cpp:1523` is the export job's.
  - `markerSourceId` is written at `shot_processor.cpp:1210` and never read.
  - `shotSource` is written at `:1174` and never read from the job.
  - `sessionType` is read only by the profiler (`analysis_profiling.cpp:39`).
  - `MetricDescriptor::usedBy`, documented as "Powers the detail page's "Where it's used"" (`metric_descriptor.h:570`), has no
    production reader. The model browser computes its own count (`model_browser.cpp:1740`). The manifest has 118 hand-kept
    lists for it, and one test spot-checks two keys (`metric_catalogue_test.cpp:938–943`).
  - `ShotListModel::analysisDetailForSwingDir` / `previousAnalysisDetail` (`shot_list_model.h:227, 236`) are `Q_INVOKABLE`
    with no QML or C++ caller outside tests. `WristDiagnosticsModel.previousAnalysisDetail` is never bound. Also,
    `analysisDetailForSwingDir` returns the empty map of a cheap-loaded row instead of reading the disk
    (`shot_list_model.cpp:646–648`), which contradicts its header. It is latent only because nothing calls it.
  - `shot_analyzer.h:101`: "Agent B wires both".
  - The profile and log tags say "Wrist" for every swing (`analysis_pipeline.cpp:4483`, `[WristAnalysis]`).
  - `analysis_stage.h:28–33` and `:375–377` still state "no parallelism" and "per-session stage lists", while the module now
    has both a DAG executor and a single profile.
- What goes wrong: readers trust fields and comments that describe nothing. 118 `usedBy` lists will drift from the real
  graph, which the model browser already computes.
- Fix: delete the dead fields and `usedBy`. Delete the two `Q_INVOKABLE`s, or fix and wire them. Rename the profile, or add an
  explicit `name` that the profiler matches on.
- Effort: S        - Related: A-06

### A-18 · The comments and developer docs on the re-analysis window contradict the code and prescribe a known trap
- Category: 13 Comments as a symptom; design-vs-code drift      - Severity: Medium
- Evidence:
  - `src/Gui/shot/reanalysis_controller.cpp:119–124` — "fullWindow stays OFF … ReanalyzeOptions::fullWindow remains as an
    escape hatch for SwingLab/debugging only", and the call is `reanalyzeSwingDir(dir, …ReanalyzeOptions{})`.
  - These contradict it:
    - `src/Analysis/swing_reanalyzer.h:112` — "The in-app ReanalysisController sets this true on every explicit re-analyse";
    - `src/Analysis/shot_analyzer.h:204` — "explicit re-analysis sets it true";
    - `docs/developer/shot_analyzer_developer_guide.md:524–527` — "The in-app path sets `fullWindow` on every explicit
      re-analyse".
  - `docs/developer/analysis_pipeline_developer_guide.md:684` — "`swinglab_run --write-back --full-window` is the library
    sweep".
- What goes wrong: following the developer guide, a library sweep is run full-window. That differs from the in-app
  re-analyse. A full-window run re-poses and re-tracks from the MP4: the shaft guide calls this "a known trap" at
  `shaft_tracker_developer_guide.md:1940`. Nothing in the code stops the documented procedure.
- Fix: correct the two header comments and both guides. Consider having SwingLab refuse `--write-back --full-window` without
  an explicit `--i-know`.
- Effort: S        - Related: A-09

### A-19 · The catalogue is "not a singleton" but is rebuilt six or more times and installs a hidden global
- Category: 1 Ownership/lifetime, 8 State      - Severity: Low
- Evidence:
  - `src/Metrics/metric_catalogue.h:31–33` — "It is a plain value object, NOT a singleton: assemble it on demand … honouring
    the Analysis module's ban on startup registration / self-registering statics".
  - Instances:
    - function-local statics at `metric_catalogue.cpp:90`, `charts_relations_presenter.cpp:64`, `variability_measures.h:54`;
    - per object at `chart_metrics.h:50`, `metric_catalog.cpp:40`, `model_browser.cpp:814`;
    - manifest-only at `consistency_env.cpp:64`.
  - `metric_catalogue.cpp:100` — `gates::subjectLabeller() = &gateSubjectLabel;` is a process-global set as a side effect of
    building a catalogue. `gate_catalogue.h:414` says the manifest installs it, which is wrong.
- What goes wrong: about 134 descriptors with long prose are copied once per instance. Metric subjects in gate wording fall
  back to raw keys in any process or test that never called `makeMetricCatalogue()`. **Suspected** for tools that only call
  `installMetricManifest`. The label also depends on construction order.
- Fix: one immutable `const MetricCatalogue &metricCatalogue()` (thread-safe static init), or one instance owned by the app
  and passed down. `subjectLabel` takes the catalogue explicitly.
- Effort: S        - Related: A-06

### A-20 · One namespace spans every layer
- Category: 15 Names and namespaces      - Severity: Low
- Evidence:
  - `namespace pinpoint::analysis` is declared across every layer: Analysis (about 30 files), Metrics (19), Club (about 50),
    Body (about 20), Wrist (about 25), Pose, Timeline, Kinematics, IMU, LaunchMonitor (5), Export (`swing_doc.h`),
    Diagnostics (62), Coaching (about 19), and Gui (about 19 across diagnostics, charts, report and launchmonitor).
  - The known collision: `DagEdge` in `analysis_dag.h` vs `src/Diagnostics/dag_layout.h`. It is now renamed `StageEdge`, but
    the hazard that caused it remains.
- What goes wrong: an inline struct or function in any of these layers can ODR-collide with another layer's, and only in the
  binary that links both. That is what happened with `DagEdge`.
- Fix: a namespace per layer (`pinpoint::producers::club`, `pinpoint::metrics`, `pinpoint::diagnostics`, `pinpoint::coaching`).
  Use a CI grep that fails on duplicate type names within one namespace.
- Effort: M (mechanical)        - Related: A-11

## 4. Done well

- **One analyzer, gated on data, not intent.** `shot_analyzer.cpp:24–30` and `CaptureCapabilities::fromJob(job, window)`
  (`analysis_stage.h:155–178`) measure IMU rates *around impact* rather than trusting a device's declaration. Availability
  (`metric_providers.h:38–47`) follows the same rule, and the earlier `wristSessionOk()` gate was removed.
- **A typed blackboard with declared data flow.** `AnalysisContext`, `StageDecl` and a fixed resource table with a typo check
  (`analysis_stage.h:166–226`, `isKnownResource`). The DAG executor is required to reproduce the loop's output byte for byte.
  Appends and gate records are spliced in authored order (`tl_seriesSink`, `tl_gatesSink`). This is careful concurrency
  design.
- **The worker never reads settings.** The job is a value resolved on the UI thread (`shot_analyzer.h:36–38`). Re-analysis
  reproduces the capture's model tier, club prior and height from the document, never from `AppSettings`
  (`swing_reanalyzer.cpp:730–735, 793–815`).
- **One loader for re-analysis**, shared by the app and SwingLab (`swing_reanalyzer.h:24–25`). Its IMU lanes go through the
  shared `RamPayloadSource` rather than a private copy (`swing_reanalyzer.cpp:263–269`).
- **Honest provenance.** `analysis.path.reuse` records *why* each track was reused or re-run (`swing_reanalyzer.cpp:1001–1043`).
  Integrity verdicts are re-reached, or removed, on every re-analysis and never inherited (`reanalysis_controller.cpp:153–161`).
  The empty-manifest guard refuses to overwrite a document it could not read back (`:141–153`).
- **Gate records carry evidence, not prose.** The words are applied later from one catalogue (`gate_catalogue.h:19–40`), and
  `gate_catalogue_test` ties fired ids to entries in both directions.
- **The stage versions themselves are disciplined.** Each bump has a dated reason in `analysis_versions.h`, and the reuse gate
  includes the model's identity (name@bytes).

## 5. June review status (items now or formerly in this area)

The June review's "src/Analysis" section covered the wrist and segmentation code. That code has since moved to `src/Wrist`,
`src/Timeline`, `src/Kinematics` and `src/Body`. I checked status only; I did not re-audit those directories.

| June item | Status | Evidence |
|---|---|---|
| [H] two "wrist analysis" subsystems confusingly named | **Partly fixed.** `wrist_analyzer` is now `metric_extractor`, and both clusters moved to `src/Wrist`. `wrist_analysis_adapter.*` and `wrist_assessment_*` still sit side by side. | `ls src/Wrist` |
| [H] π ×6, `wrapPi` ×4 with two algorithms; create `analysis_math.h` | **Worse / open.** No shared constant and no `analysis_math.h`. The π literal is in 33 files, plus 39 `M_PI` uses. In this area: `analysis_pipeline.cpp:106` `kPiD`, a literal at `:864`, `M_PI` at `:2536`; `variability_grader.cpp:32`. `wrapPi` is still defined 4 times. | `body_rotation.cpp:44`, `trunk_angles.h:40`, `kinematic_series.cpp:48`, `skeleton3d_fit.cpp:234` |
| [M] `nearestIndex` / `phaseValue` / `find` duplicated | **Open, wider.** `nearestIndex` has 8 definitions: `metric_channel.h:224` inline in `pinpoint::analysis`, plus copies in `head_track.cpp`, `foot_metrics.cpp`, `kinematic_series.cpp`, `phase_segmenter.cpp`, `metric_extractor.cpp`, and 2 in the Gui. `phaseValue` has 3: `analysis_pipeline.cpp:293`, `score_uncertainty.cpp:40`, `wrist_resemblance.cpp:46`. `find` is now unified as `findSeriesByLadder`. | as listed |
| [M] thresholds bypass the tuning mechanism | **Mostly fixed** through `pp_tuned_constants.h`. `analysis_pipeline.cpp` still holds about 30 inline time windows. | A-05 |
| [M] stringly-typed `MetricSeries` keys → `metric_keys.h` | **Open.** | A-07 |
| (Export, at this seam) "swing.json header built twice" | **Partly fixed.** The capture block is shared (`SwingExporter::captureBlock`), but `buildSynthManifest` still "Mirrors the exporter's header exactly" (`shot_processor.cpp:2075–2114`). | — |

## 6. Questions I could not settle from the code

1. Is the live path's missing `standardClubLengthM` intended (A-01)? The field's comment says only the skeleton reads it, but
   DtlPosture does too.
2. Is `swinglab_run --write-back --full-window` still the intended library sweep (developer guide), or is it the span-bounded
   write-back the in-app path uses (A-18)?
3. Should Storage → "Save pose keypoints" apply to re-analysis write-back (A-09)? The reuse gate gains from keeping the pose,
   but the setting says otherwise.
4. Which instrument convention is the target: an `hm.` key prefix or `routeId` on the bare key (A-04)? The code uses both.
5. Is the "compare to previous / reference" ghost (`previousAnalysisDetail`, `analysisDetailForSwingDir`) retired or awaiting
   wiring (A-17)?
6. Do any documents in the library lack `capture.impactUs`? Those are the only ones where the reanalyzer's contradictory
   phase-time fallback (A-03) would run.
