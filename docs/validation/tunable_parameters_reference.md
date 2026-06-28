# PinPoint Studio — Tunable Parameters: Validation, Optimisation & Developer Guide

**Status:** Reference (current). Companion to
[`pipeline_validation_and_tuning.md`](pipeline_validation_and_tuning.md) — that document is the
*methodological backbone* (validity ladder, the three-corpus progression, sample-size/power per
statistic); **this** document is the *parameter-centric* view: every knob that can be tuned, what it
moves, how we decide its value statistically, and exactly how the injection + sweep machinery works in
code.
**Audience:** Part I (Reference) for analysts/reviewers; Part II (Developer Guide) for anyone wiring a
new parameter or running a sweep.
**Harness:** SwingLab (`tools/swinglab/`, `swinglab_run` + `lab.py`).

> **One-line model.** A parameter is *injected* at run time via a dotted key
> (`<area>.<field>` → number) into `ShotAnalysisJob::tuningOverrides`; each stage applies the keys it
> owns onto its config struct, so a sweep iterates at binary speed with no rebuild. Its frozen default
> lives in `src/Core/pp_tuned_constants.h`. It is *optimised* by `lab.py sweep` against a scorecard
> objective, under a per-swing regression gate and a Tune/Validation/Held-out partition. When validation
> locks a value, you edit the one literal in the header.

---

# Part I — Reference

## 1. The injection contract (what "tunable" means)

"Tunable" has a precise operational meaning here: **the parameter is injectable via the dotted-key
`tuningOverrides` mechanism, so SwingLab can sweep and diff-gate it without a rebuild.** A parameter that
is only a compiled-in constant is *not* tunable in this sense until it is wired to a dotted key.

Three statuses appear in the §2.4 tuning ledger of the backbone doc, and they are the vocabulary here:

- **dotted-key** — injectable today; a `space.json` can sweep it.
- **code** — a real knob, but rebuild-only (e.g. live-thread seed tolerances, runtime filter choice).
- **n/a** — not a tuning surface (a capture gate / census).

A fourth, historical status — **escalation** (a knob that existed but had no injection path) — has been
retired for the wrist/scoring/diagnosis/filter layers: they are now dotted-key (this work).

## 2. The parameter catalog

Every namespace, its owning config, where its default is frozen, and **what observable it moves**. The
last column is the crucial one for optimisation: a sweep can only optimise a parameter whose change is
visible in a scorecard check (§4).

| Namespace | Owner config (file) | Default source | Tuned at | Moves which observable |
|---|---|---|---|---|
| `seg.*` | `SegmentationConfig` (`phase_segmenter.h`) | struct defaults | C1 | `seg.monotone`, `seg.tempo_ratio`, `truth.event_*_s` |
| `shaft.*` | `ShaftDetectConfig` (`shaft_tracker_math.h`) | struct defaults | C1 | `club.coverage`, `track.*`, `truth.theta_rms_deg`, `truth.head_median_px` |
| `assembly.*` | `AssemblyConfig` (`shaft_track_assembly.h`) | struct defaults | C1 | `club.coverage`, ŝ_hand residual, `xmodal.imu_vision_corr` |
| `score.*` | `kWristBands` + deadbands (`swing_scorer.cpp`) | `pp_tuned_constants.h` `scoring::` | **C2** | `analysis.score` (`r.score`) — **no Tier-1 check; objective is HackMotion** (§4) |
| `sampler.*` | `PpWristSamplingConfig` (`wrist_angle_sampler.h`) | `sampler::` | C1→C2 | `analysis.assessment.findings` → `diag.*` (gimbal proxy currently inert — §4) |
| `rules.*` | `RuleTuning` (`assessment_rule.h`) | `rules::` | C1→C3 | `analysis.assessment.findings`/`scoreV2` → `diag.*` |
| `bands.*` | `BandTuning` margins (`reference_bands.h`) | (table; margins runtime) | C2 | `analysis.assessment.findings` → `diag.*` |
| `filter.*` | `RefuseConfig` (`orientation_refuser.h`) | `filter::` | C1→C2→C3 | wrist angles → `xmodal.imu_vision_corr`, `diag.*`, `filter.impact_continuity` |
| `seed.*` (status **code**) | `kInit*` (`imu_base.h`) | `seed::` | C1 | live filter convergence (offline-unreachable) |

### 2.1 `seg.*` — phase segmentation (≈25 keys)
Envelope cut-off, top/takeaway/transition windows, vote-agreement, finish stillness gates
(`fcEnvelopeHz`, `top*BeforeImpactUs`, `takeawayFracOfPeak`, `voteAgreeUs`, `finish*Us`, …). **Load-bearing:**
every wrist metric is sampled *at a phase*, so a Top-timing error propagates into every angle.

### 2.2 `shaft.*` / `assembly.*` — club track (≈30 + ≈12 keys)
Ridge/Hough detection (`ridgeKernelPx`, `noiseSigmaK`, `thresholdFloor`, `nmsSeparationDeg`,
`clutterMaskDeg`, `minScoreFrac`, `runMaxGapPx`, `interHandSigmaDeg`, …) and assembly/fusion
(`coverageMin`, `jerkPsd`, `transSigma*`, `visionSigmaFloorRad`, `calibAcceptRad`, …), plus the skeleton-aware flag-flips (`useArmScale`, `useKinematicPrior`, `useEnvelope`, `useBlurMode`,
`emitPredicted`, `useBackgroundSub`, `twoPassCalibration`, `autoChirality`, default OFF/byte-identical).

### 2.3 `score.*` — wrist scoring bands
Per metric × phase: `score.<metricKey>.mu / .sigma / .weight / .oneSidedDir`, plus the deadband shape
`score.zIn / .zOut / .p`. Metric keys: `leadWristFlexExt`, `leadWristRadUln`, `forearmPronation`,
`leadArmFlexion`. Frozen defaults (`pp_tuned_constants.h::scoring`):

| metric | μ | σ | oneSided | weight |
|---|---|---|---|---|
| leadWristFlexExt | 15 | 12 | +1 (penalise below) | 0.45 |
| leadWristRadUln | 0 | 12 | 0 | 0.15 |
| forearmPronation | 0 | 25 | 0 | 0.20 |
| leadArmFlexion | 5 | 12 | −1 (penalise above) | 0.20 |

Deadband: `zIn=1.0, zOut=3.0, p=2.0`. These set the **0–100 number the golfer sees**; re-seated at
Corpus 2 on the observed + HackMotion tour-range distribution.

### 2.4 `sampler.*` — windowed-median wrist sampler
`sampler.windowHalfUs` (15000), `sampler.gimbalThresholdDeg` (75.0), `sampler.minValidSamples` (1).
Governs the Gap / Indeterminate / Ok decision at each P-position. **Caveat:** `gimbalThresholdDeg` is
inert in the current offline path because the analysis adapter sets `pitchProxyDeg = 0` — see §4.

### 2.5 `rules.*` — Tier-2 fault/strength engine
`rules.confidenceFloor` (0.45), `rules.scoreScale` (18.0), `rules.severityWeightFault` (1.0),
`rules.severityWeightWatch` (0.5), `rules.corroborationBoost` (0.30),
`rules.strengthsRequireAdjacentFault` (true). Decides which faults fire and how hard they score.

### 2.6 `bands.*` — reference-corridor margins
`bands.{radUln,flexExt,forearm,trailWrist,elbow}Margin` — the amber margin (degrees either side of the
green corridor) per DOF; a negative value (default) means "use the compiled-in table margin". The full
8-position lo/hi corridor arrays stay in `reference_bands.cpp` (a Corpus-2 re-seat, not a sweep target).

### 2.7 `filter.*` — orientation re-fusion + phase-adaptive schedule
`filter.refuse` (bool — the master switch that feeds re-fused orientation into the fusion),
`filter.adaptive` (bool — phase-adaptive schedule vs fixed-gain), `filter.beta` / `filter.betaStatic`,
`filter.betaDynamic`, `filter.accelErrGateG` (0.30), `filter.gyroGateDps` (200), `filter.accelSatG` (16),
`filter.impactBlankPreMs` (5), `filter.impactBlankPostMs` (15). The deepest tuning surface in the IMU
path — see [`pipeline_validation_and_tuning.md` §5.3.1](pipeline_validation_and_tuning.md). **Open
tail:** ESKF `R` is not exposed (vendored lib, no warm-start — outside the re-fusion loop).

### 2.8 `seed.*` — stillness-gated seeding (status: code)
`kInitAccelTolG` (0.15), `kInitGyroMaxRadps` (0.5), `kInitMaxSeedAttempts` (200). Live I/O-thread only;
sourced from `pp_tuned_constants.h::seed` but not offline-reachable, so classified **code** not
dotted-key.

## 3. The frozen-defaults header — the single freeze edit-point

`src/Core/pp_tuned_constants.h` (`namespace pinpoint::tuned`) is the **single source of truth** for every
parameter that is tuned during validation and then frozen. `src/Core/` is the lowest common layer (both
`src/IMU/` live code and `src/Analysis/` offline code already include from it), and the header is
**pure numeric literals** (quaternions as `{w,x,y,z}` float arrays) so even the deliberately Qt-free
`orientation_filter.h` can include it.

The override path does **not** consult the header — overrides start from these as their *baseline*. When
validation locks a value: change the one literal here; every consumer (live + offline) picks it up; the
`tuned_constants_parity_test` proves the indirection stayed byte-identical. (Wrist axis **sign**
conventions stay in `wrist_angles.h` — they are code-structure choices, not single literals.)

## 4. Observability & objectives — *which check does this parameter move?*

The SwingLab scorecard objective is the **pass-rate of the score.py checks**. A sweep can only optimise a
parameter whose change is visible in a check. This is the make-or-break table:

| Namespace | Visible in scorecard? | Objective check(s) | Pre-condition |
|---|---|---|---|
| `seg.*`, `shaft.*`, `assembly.*` | **yes** | `seg.*`, `track.*`, `club.*`, `truth.*` | labelled / clean swings |
| `score.*` | **no** (moves `analysis.score`, not a check) | **HackMotion agreement** (Corpus 2) | paired IMU+HackMotion |
| `rules.*`, `bands.*` | **yes** (via offline assessment) | `diag.recall`, `diag.clean_no_fault` | **known-group labels** (`truth.json` `knownGroup`) |
| `sampler.*` | partial (inert `gimbalThresholdDeg`) | `diag.*` | labels; pitch-proxy populated |
| `filter.*` | **yes** (after C3) | `xmodal.imu_vision_corr` (vision), `diag.*` (labels), `filter.impact_continuity` (provisional) | vision shaft track / labels / real impact |

Three consequences worth internalising:

1. **`score.*` is injectable but not scorecard-optimisable.** No Tier-1/2/3 check reads the wrist *score*
   — scoring quality has no ground-truth on the lab corpus. Its objective is the HackMotion criterion
   (Corpus 2), so it is *described* by a sweep but *validated* against HackMotion.
2. **`rules.*`/`bands.*` need labels.** `diagnosis_metrics` returns no checks unless a swing declares a
   `knownGroup` (scripted fault or `clean`); without labels these knobs are injected but unobserved.
3. **`filter.*` became observable at C3.** Re-fusion feeds the wrist angles, so the filter moves
   `imu_vision_corr` (camera swings) and `diag.*` (labelled swings). `filter.impact_continuity` is an
   IMU-only diagnostic but **provisional** until a real impact-shock swing calibrates it (the synthetic
   corpus models no impact saturation).

## 5. Statistical methods — *how we decide a value is right*

Three activities, never conflated (the cardinal sin is tuning to a metric then reporting it as
validation): **Verification** (built it right — golden/parity tests), **Tuning** (best value — objective
on a *Tune* partition), **Validation** (built the right thing — agreement on *held-out* / against an
independent criterion). The sample size is set by the **statistic**, not the parameter
(full derivations in [`pipeline_validation_and_tuning.md` §3](pipeline_validation_and_tuning.md); the
operative results):

| Statistical goal | Driver / formula | Usable N | Tight N |
|---|---|---|---|
| Proportion (pass-rate, fault recall, FP) | `n = z²·p(1−p)/E²`; rule of three (`k/k` ⇒ rate ≥ `1−3/k`) | ~35 (±10 %) | ~140 (±5 %); ~60/fault for recall ≥ 0.95 |
| Agreement LoA (Bland–Altman) | `SE(LoA) ≈ 1.71·s/√n` | ~25 (±0.68 s) | ~50 (±0.48 s) |
| Reliability (SEM/MDC/ICC) | `CV(SEM) ≈ 1/√(2(n−1))` | ~25–30 (±14 %) | ~50 (±10 %) |
| RMSE/SD vs labels (track, phase timing) | χ² CI on SD | 10–15 swings × ~10 pts | — |
| Regression detection (A/B) | paired-t power | 50 pairs: d ≥ 0.5 @0.9 | 200 pairs: d ≈ 0.2 |

**Per parameter class — the statistic and the N:**

| Class | Validation statistic | Target | N (driver) |
|---|---|---|---|
| `seg.*` | Top-timing RMSE vs hand labels | `truth.event_top_s ≤ 0.03 s` | 10–15 labelled |
| `shaft.*` | θ-RMSE / head-px vs labels (proportion of clean passing coverage) | `theta_rms < 3°`, `head < 25 px`, coverage ≥ 0.6 on ≥ 90 % | 10–15 labelled (+ raw subset for sub-pixel θ) |
| `assembly.*` | coverage proportion; ŝ_hand residual | residual < `calibAcceptRad ≈ 7°` | 50 |
| `score.*` | Bland–Altman bias + 95 % LoA vs HackMotion; ICC(A,1) | FE/PS RMSE ≤ 4–5°, ICC ≥ 0.90; RUD ≤ 8° | ~50 paired |
| `sampler.*` | between-swing repeatability RMSE; Indeterminate rate | < 5° FE / < 8° RUD; < 2 % Indeterminate | ≥ 25–30 repeats |
| `rules.*` | known-groups recall + FP (point estimate + Wilson CI) | rule fires & is specific (*not* a numeric recall guarantee) | ~10–15 / fault |
| `bands.*` | distribution overlap vs observed + HackMotion tour ranges | corridors monotone, consistent | 50 (+ paired) |
| `filter.*` | per-phase `imu_vision_corr`; FE/RUD/PS just-after-impact vs HackMotion LoA; impact-continuity | corr ↑, within-LoA, continuous across impact | 50 + synth verification |

**Reliability ≠ Agreement ≠ Accuracy.** A parameter whose repeatability MDC (`1.96·√2·SEM`) exceeds the
effect a coach wants to see is useless *regardless of accuracy*; agreement (Bland–Altman, not
correlation) is bounded by the criterion's own LoA (HackMotion ≈ 1° FE / ≈ 5° RUD — never claim accuracy
finer than the reference's noise floor).

## 6. Optimisation methods — *how we search*

Escalate to the surface; do not over-engineer (full guidance in
[`pipeline_validation_and_tuning.md` §7.1](pipeline_validation_and_tuning.md)):

1. **Coordinate descent (default).** One knob at a time, accept on the diff gate, move on. Interpretable,
   diff-friendly, no dependencies — right for the largely-separable `seg.*`/`shaft.*`/`assembly.*`/`score.*`.
2. **Bayesian (TPE/GP) — on plateau.** Sample-efficient for ~14 `seg.*` + ~8 `shaft.*` against ~50
   swings when interactions are suspected (the "sweep plateau ×2" escalation trigger).
3. **CMA-ES — for the filter schedule.** Continuous, non-separable, gradient-free `filter.*` knobs.

**Partition the search, not just the report** (the overfitting guard): sweep on **Tune**, select on
**Validation**, touch **Held-out** once at freeze (`--freeze`). **Regression-gated objective:** maximise
the mean SUBJECT TO per-swing `regressions == 0` (the 5-pt diff), enforced *inside* the loop — a mean
gain that hides a 2-swing regression is rejected. **Convergence:** stop on no-Validation-improvement over
N trials, acquisition floor, or a trial/wall-clock budget — never a fixed count reported as best.

## 7. Acceptance criteria — by parameter class

A change is **kept only if mean ↑ AND `regressions: 0` per swing** (the universal gate), AND it meets the
class-specific bar:

- **seg/shaft/assembly:** monotone 100 %, Top ≤ 30 ms, coverage ≥ 0.6 (≥ 90 % clean), θ-RMSE < 3°.
- **score (C2):** bands consistent with observed + HackMotion; score monotone; locked against the
  criterion, not the lab scorecard.
- **sampler:** repeatability < 5° FE / < 8° RUD; < 2 % Indeterminate.
- **rules (known-groups):** scripted-fault recall point-estimate high + ≈ 0 FP on clean (with honest
  Wilson CI; a numeric recall ≥ 0.95 needs ~60/fault — deferred to a pooled fault library).
- **bands (C2):** corridors anchored on distribution + HackMotion ranges.
- **filter:** per-phase `imu_vision_corr` ↑; orientation continuous across impact; FE/RUD/PS
  just-after-impact within the Corpus-2 LoA; net drift bounded at the still finish.

---

# Part II — Developer Guide

## 8. The injection mechanism

The whole contract is five overloads in `src/Analysis/analysis_tuning.h`:

```cpp
namespace pinpoint::analysis::tuning {
// apply(map, "area.field", field) — write the override onto `field` if the key is present; no-op else.
inline void apply(const QVariantMap &ov, const char *key, float   &field);
inline void apply(const QVariantMap &ov, const char *key, double  &field);
inline void apply(const QVariantMap &ov, const char *key, int     &field);
inline void apply(const QVariantMap &ov, const char *key, int64_t &field);
inline void apply(const QVariantMap &ov, const char *key, bool    &field);
}
```

The map is `ShotAnalysisJob::tuningOverrides` (`shot_analyzer.h`), a `QVariantMap` of `"<area>.<field>"`
→ number. **Empty in production** (the app never sets it); the offline runner fills it from a params
JSON. Unknown keys are logged and ignored — a typo silently no-ops, visible in `runner.log`.

```
params.json ──flattenParams──▶ job.tuningOverrides ──┬─ segConfigFor(ov)            → seg.*
 (swinglab_run)                                       ├─ SwingScorer::score(.,.,ov)  → score.*
                                                      ├─ wristAssessmentConfigFor(ov)→ sampler./rules./bands.*
                                                      ├─ refuseConfigFromTuning(ov)  → filter.*
                                                      └─ ShaftTracker::track(.,job)  → shaft./assembly.*
```

`flattenParams` accepts both nested (`{"shaft":{"ridgeKernelPx":11}}`) and flat
(`{"shaft.ridgeKernelPx":11}`) forms.

## 9. How each stage consumes its keys

**The canonical pattern** (copy this for a new stage) — `segConfigFor` in `wrist_analyzer.cpp`:

```cpp
SegmentationConfig segConfigFor(const QVariantMap &ov) {
    namespace tn = pinpoint::analysis::tuning;
    SegmentationConfig c;                      // struct defaults (sourced from pp_tuned_constants.h)
    tn::apply(ov, "seg.fcEnvelopeHz", c.fcEnvelopeHz);
    tn::apply(ov, "seg.voteAgreeUs",  c.voteAgreeUs);
    // … one apply() per field …
    return c;                                  // empty ov ⇒ byte-identical defaults
}
```

**`score.*`** — `swing_scorer.cpp` copies the frozen band table into a mutable vector and overlays the
overrides; `SwingScorer::score(series, sessionType, overrides)` is called from `wrist_analyzer.cpp`:

```cpp
std::vector<ScoreBand> scoreBandsFor(int sessionType, const QVariantMap &ov) {
    std::vector<ScoreBand> bands(std::begin(kWristBands), std::end(kWristBands));
    for (ScoreBand &b : bands) {
        const QByteArray pfx = QByteArray("score.") + b.key + '.';
        tn::apply(ov, (pfx + "mu").constData(),          b.mu);
        tn::apply(ov, (pfx + "sigma").constData(),       b.sigma);
        tn::apply(ov, (pfx + "weight").constData(),      b.weight);
        tn::apply(ov, (pfx + "oneSidedDir").constData(), b.oneSidedDir);
    }
    return bands;                              // + deadbandFor(ov) for score.zIn/.zOut/.p
}
```

**`sampler.*`/`rules.*`/`bands.*`** — one builder, `src/Analysis/wrist_assessment_tuning.h`:

```cpp
inline WristAssessmentConfig wristAssessmentConfigFor(const QVariantMap &ov) {
    WristAssessmentConfig cfg;                 // sampling + rules + band.tuning, all frozen defaults
    tn::apply(ov, "sampler.gimbalThresholdDeg", cfg.sampling.gimbalThresholdDeg);
    tn::apply(ov, "rules.confidenceFloor",      cfg.rules.confidenceFloor);
    tn::apply(ov, "bands.flexExtMargin",        cfg.band.tuning.flexExtMargin);   // … etc
    return cfg;
}
```

**`filter.*`** — `src/Analysis/orientation_refuse_tuning.h` builds a `RefuseConfig` (shared by the
analyzer and the `--refuse-orientation` tool, so the key list lives once):

```cpp
inline bool tuningWantsRefusion(const QVariantMap &ov)            // the C3 master switch
{ return ov.value(QStringLiteral("filter.refuse")).toBool(); }

inline pinpoint::RefuseConfig refuseConfigFromTuning(const QVariantMap &ov, int64_t impactUs,
                                                     float betaDefault = pinpoint::tuned::filter::kBeta) {
    pinpoint::RefuseConfig cfg; cfg.betaStatic = betaDefault;
    tn::apply(ov, "filter.adaptive",   cfg.adaptive);
    tn::apply(ov, "filter.betaDynamic",cfg.betaDynamic);   // … betaStatic/gates/sat/blank …
    if (cfg.adaptive) cfg.impactUs = impactUs;             // impact-blank window armed only when adaptive
    return cfg;
}
```

## 10. The offline assessment path (`rules.*`/`sampler.*`/`bands.*` observability)

The Tier-2 engine is GUI-only in production; SwingLab opts into it via `ShotAnalysisJob::runAssessment`
(set by `swinglab_run`, default OFF). In `WristAnalyzer::analyze`, after the metric series exist:

```cpp
if (job.runAssessment && hasImu && !series.empty()) {
    const InMemoryWristAngleSource src = buildWristAngleSource(detail->series, detail->phases);
    const auto provider = makeReferenceBandProvider(BandProviderKind::Archetype);
    const WristAssessmentConfig acfg = wristAssessmentConfigFor(job.tuningOverrides);
    const PpWristAssessmentResult ar = WristAssessmentEngine::assess(src, *provider, acfg);
    detail->findings = ar.findings; detail->assessmentScore = ar.score.total;
}
```

Findings serialise **additively** to `swing.json` `analysis.assessment{scoreV2, findings[]}` (only when
`runAssessment` ran ⇒ production `swing.json` unchanged). `score.py`'s `diagnosis_metrics` reads them
against the swing's `truth.json` `meta.knownGroup` (a scripted fault id, or `clean`/`control`):
`diag.recall` (the fault is surfaced, low-confidence counts) / `diag.clean_no_fault` (no *confident*
false fault).

## 11. The adaptive orientation filter (`filter.*`)

Two layers, both header-only and standalone-testable
(`src/IMU/orientation_refuser.h`, `src/Analysis/tests/orientation_refuse_test.cpp`):

**`adaptiveBeta(...)`** — the per-sample gain. Returns 0 (gyro-only) under saturation (`|a| ≥ accelSatG`)
or inside the impact window `[impactUs ± blank]`, else a `betaStatic → betaDynamic` ramp on the worse of
the two dynamics gates (accel residual `|‖a‖−1g|` / `accelErrGateG`, gyro magnitude / `gyroGateDps`).

**`refuseOrientationAdaptive(filt, samples, cfg)`** — warm-starts the Madgwick filter from the stored
quat at the window's first sample, then re-integrates the raw accel+gyro with `filt.setBeta(adaptiveBeta(...))`
per sample. With `betaStatic == betaDynamic` and no gate tripped it reduces *exactly* to fixed-beta
(byte-identical) — the E1 unit-test invariant.

**C3 — feeding the metric.** `ImuVisionFuser::fuse(window, bindings, gridHz, const RefuseConfig*)`:
when the pointer is non-null it re-derives each source's orientation (slerped onto the grid) and uses
*that* as `qRaw` in `q_anat = A·qRaw·M`; null ⇒ the stored quaternion (production, byte-identical).
`WristAnalyzer::analyze` passes `&refusion` exactly when `tuningWantsRefusion(job.tuningOverrides)`. This
is what makes `filter.*` move the wrist metric (and hence `imu_vision_corr` / `diag.*`).

**C5 — the IMU-only objective.** `WristAnalyzer` computes `impactContinuityDeg` (max orientation step
across the impact window *beyond the gyro-predicted step* — the accel-correction/shock residual) →
`SwingAnalysis::filterImpactStepDeg` → `swing.json analysis.filter.impactStepDeg` → `score.py`
`filter.impact_continuity` (**warn**, provisional threshold pending real impact data).

## 12. The SwingLab harness — running a sweep

`space.json` is a flat map of bounds; `[lo, hi]` (float) or `[lo, hi, "int"]`:

```json
{ "seg.fcEnvelopeHz": [4.0, 12.0], "shaft.ridgeKernelPx": [5, 15, "int"],
  "score.leadWristFlexExt.mu": [8.0, 22.0], "filter.betaDynamic": [0.0, 0.05] }
```

`partitions.json` assigns swing names to roles (held-out is never run unless `--freeze`):

```json
{ "tune": ["s001","s002"], "validation": ["s010","s011"], "heldout": ["s020"] }
```

```bash
P=~/.swinglab-venv/bin/python
$P lab.py ingest  /data/corpus-1                      # corpus.json (surfaces knownGroup, calibration, …)
$P lab.py run     /data/corpus-1 /runs --id baseline  # the gate reference
$P lab.py sweep   /data/corpus-1 /runs space.json \
        --method coordinate --baseline /runs/baseline \
        --partition partitions.json --trials 40        # gate + partition enforced in-loop
$P lab.py diff    /runs/baseline /runs/candidate       # exit 1 ⇒ regressions
```

The sweep prints `tune`/`val` means + `reg` per trial, rejects gated trials, and writes
`sweep-result.json` (history + best, selected on Validation). The scorecard objective is the pass-rate of
`invariants + truth_metrics + diagnosis_metrics + filter_metrics` (`score.py`). **Always rebuild
`swinglab_run` after any analysis C++ change**, and **never diff across hosts** (CPU vs CUDA pose
differs).

## 13. Recipe — adding a new tunable parameter

1. **Frozen default** → add a named `inline constexpr` in `pp_tuned_constants.h` (the appropriate
   sub-namespace); have the config struct's member initialiser reference it.
2. **Inject** → add a `tn::apply(ov, "area.field", cfg.field)` line in the stage's builder (or write a
   builder following `segConfigFor`). Thread `job.tuningOverrides` to the call site if not already there.
3. **Observe** → confirm the change moves a `score.py` check. If it does not (like `score.*`), it is
   injectable but not scorecard-optimisable — document the real objective (e.g. HackMotion). If it needs
   a new check, add one to `score.py` following the additive five-step reader contract
   (sidecar/field → `Swing`/`RunResult` accessor → check group → synth stamp → `ingest` flag).
4. **Test** → extend `tuning_overrides_test.cpp` (the override moves the consumed value; empty map is a
   no-op) and `tuned_constants_parity_test.cpp` (the new default equals its historical literal).
5. **Document** → add the key to this catalog (§2) and flip its row in the §2.4 ledger of the backbone
   doc.

## 14. Tests (the safety net)

| Test (`src/Analysis/tests/`) | Guards |
|---|---|
| `tuned_constants_parity_test` | every frozen constant == its historical literal; default structs unchanged (the header refactor is byte-identical) |
| `tuning_overrides_test` | each namespace's override actually moves the consumed value; empty map is a no-op |
| `orientation_refuse_test` (E1–E4) | adaptive refuser reduces to fixed-beta when flat; impact-blank / continuous gate / saturation gates fire correctly |
| `swing_scorer_test`, `reference_bands_test`, `composite_score_v2_test`, … | default-path behaviour unchanged after the override plumbing |
| `swing_window_parity_test` | RAM-vs-disk SwingWindow parity holds through the `ImuVisionFuser` change |

All run via `cmake -S src/Analysis/tests -B build/analyzer-tests && ctest --test-dir build/analyzer-tests`.

---

## 15. Honest limits

- **`score.*` has no lab-scorecard objective** — it is validated against HackMotion (Corpus 2), not
  swept against Tier-1 checks.
- **`rules.*`/`bands.*` need known-group labels** to be observable; without them they are injected but
  unmeasured.
- **`sampler.gimbalThresholdDeg` is currently inert** offline (pitch-proxy not populated by the adapter).
- **`filter.impact_continuity` is provisional** — the synthetic corpus models no impact shock, so its
  threshold and the per-phase/net-drift checks await a real impact-shock swing.
- **ESKF `R` is not exposed** — the vendored `third_party/imu_ekf` is kept verbatim and ESKF cannot
  warm-start, so it sits outside the re-fusion tuning loop (a separate escalation if ESKF ever becomes
  the production filter).
- **Single-host, single-golfer within-subject** until Corpus 3 — no population claim before multiple
  golfers (the effective N for generalisation is *golfers*, not swings).

*This document is the parameter catalog + dev guide; `pipeline_validation_and_tuning.md` is the
methodological backbone; the `/swinglab` skill is the operator contract.*
