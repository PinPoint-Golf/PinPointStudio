# Score-Estimand Alignment — Implementation Plan

**Created:** 2026-06-28 · **Status:** draft for review (no code changed yet) · **Owner:** scoring/analysis

**Purpose.** Sequence the work that aligns three surfaces to the clarified score estimands so they
stop drifting: (A) the application code that *computes* the score, (B) the SwingLab harness that
*measures* it, (C) the validation/optimisation doc that *governs* its tuning. This plan operationalises
validation-doc **Appendix A.5 #12–18** (+ A.1#1, A.2#4) and the estimand decisions in
`shot_analyzer_design.md` **§B.0 / §B.0a / §B.7**.

**Cold-start context:** `.claude/alignment-kickoff.md` (decisions index) and the Phase-0 survey that
produced this plan. Canonical sources: `docs/design/shot_analyzer_design.md` §B.0/§B.0a/§B.7 +
Appendix C; `docs/validation/pipeline_validation_and_tuning.md` Appendix B (A1–D5), A.5, §2, §5.6/§6,
§7/§7.1, §10.

---

## 0. The estimand, restated (the target every WP serves)

- **Per session type, 0–100, never aggregated.** Wrist / Swing / GRF each own a score *and* a method.
  The AI coach is a feedback layer on top, **not a fourth score**.
- **Criterion-referenced**, never a population percentile and never an outcome prediction.
- **Swing / GRF = adherence** (closeness to a defined-good action) → keep `SwingScorer` (banded
  weighted geometric mean).
- **Wrist = per-archetype resemblance diagnostic** — *no good/bad pattern*. Compute resemblance to
  **each** of bowed/neutral/cupped, surface max + label, flag **blended** when top-two ≤ ~10 pts apart.
- **Wrist faults move to the coach layer.** They are no longer a score input.
- **Uncertainty:** band σ = *coaching tolerance only*; measurement error is a **separate** interval;
  **low confidence widens the interval, never moves the central score** (the `severity × confidence`
  central term is removed).
- **Freeze:** `score.*` / `rules.*` / `bands.*` stay **frozen, not swept**, until labels/outcomes exist.

---

## 1. Phase-0 findings that shape the sequence

1. **There is no resemblance producer to "swap in."** Live GUI persists *only* `SwingScorer`
   (`wrist_analyzer.cpp:359,382`). `WristAssessmentEngine` runs live only in a non-persisted
   diagnostics view (`wrist_diagnostics_model.cpp`) and offline-opt-in in the analyzer
   (`runAssessment` defaults false, never set in `buildAnalysisJob`). `detectArchetype` yields a
   *label*, not the three independent `R_p`. **#12 is a build, not a swap.**
2. **FaultRanker (L8) does not exist** (Appendix C). "Faults → coach" needs the findings *wired into the
   persisted path at all* first (today live `findings` = empty), then routed to coach not score.
3. **Three band representations, not two.** `SwingScorer.kWristBands` (impact μ/σ) vs `reference_bands`
   corridors (Δ-from-address lo/hi) vs the resemblance spec's `μ_p/σ_p` per archetype — a **third**
   table, externally anchored now, re-seated at Corpus 2.
4. **SwingLab is blind to the product score.** Its "score" is % of scorecard checks passing; `score.py`
   never reads `analysis.score`. The reworked fields are invisible to the harness until Phase 2.
   `score.*`/`rules.*` are also swept today with **no freeze**.
5. **Data-gating is crisp.** Only #12's `μ_p/σ_p` numerics + the band re-seat are Corpus-2-gated.
   Everything else — the structural recast, the interval, the error budget, exposing-but-freezing the
   literals, the soft penalty, the doc edits, the tests, the pitch-proxy fix, the synth spike — is
   **buildable now** with provisional external-anchored μ/σ.
6. **Two doc edits genuinely pending:** §7.1 still lists `score.*` in coordinate-descent knobs; §10
   still has `imu_vision_corr ≥ 0.9` in the Validate column with no cross-talk caveat.

---

## 2. Load-bearing design: the score data shape (WP-1)

Everything downstream carries this. Decide it first.

### 2.1 In-memory (`src/Analysis/swing_analysis.h`)

`ScoreBreakdown` today: `int overall; QHash<QString,int> perRegion; QHash<QString,int> perPhase;
std::vector<ScoredMetric> metrics;` — this is the **adherence** shape; keep it as-is for Swing/GRF.

Add a discriminator and the resemblance/uncertainty carriers (populated only for Wrist):

```cpp
enum class ScoreKind { Adherence, Resemblance };

struct ScoreInterval {            // §B.7 — measurement error, NOT tolerance
    int halfWidth = -1;          // -1 = not computed; e.g. 9 means "±9"
    int lo = -1, hi = -1;        // clamped [0,100]; convenience for UI
};

struct ScoreBreakdown {
    ScoreKind                 kind = ScoreKind::Adherence;
    int                       overall = 0;     // adherence: geo-mean | resemblance: max R_p
    // --- resemblance (Wrist) ---
    QHash<QString,int>        resemblance;     // {"bowed","neutral","cupped"} -> R_p in 0..100
    QString                   patternLabel;    // argmax label, e.g. "bowed"
    bool                      blended = false; // top-two within blendedDeltaPts
    // --- uncertainty (all kinds, §B.7) ---
    ScoreInterval             interval;
    // --- adherence (Swing/GRF), unchanged ---
    QHash<QString,int>        perRegion;
    QHash<QString,int>        perPhase;
    std::vector<ScoredMetric> metrics;
};
```

**Faults stay in `SwingAnalysis::findings`** (the existing `std::vector<PpWristFinding>`); the change is
*semantic*: findings are **coach input**, never a score input. No new fault struct needed — see WP-3.

> **Decision D-1 (recommended): extend the one struct, discriminated by `kind`.** Alternative: a
> separate `WristScore` struct. Extending keeps one persisted carrier and one QML role set; Swing/GRF
> simply leave the resemblance fields empty. *Recommend extend.*

### 2.2 Persisted (`src/Export/swing_doc.cpp`, currently writes `score = a.score.overall` at `:46`)

Bump to `pinpoint.analysis/3` and write `score` as an object (the design doc already specifies
`"score": { /* ScoreBreakdown */ }`):

```json
"score": {
  "kind": "resemblance",
  "overall": 86,
  "pattern": "bowed",
  "blended": false,
  "resemblance": { "bowed": 86, "neutral": 40, "cupped": 8 },
  "interval": { "halfWidth": 9, "lo": 77, "hi": 95 }
}
```

> **Decision D-2 (recommended): schema bump to /3, object form, with a tolerant reader.**
> `readSwingJson` must accept both the legacy bare-int `score` (schema /2) and the object (/3). Pre-
> 2026-06-11 swings are already unreanalysable; this only needs back-compat for /2 docs already on disk.
> Alternative (additive sibling `scoreBreakdown`, keep int `score`) avoids a version bump but leaves two
> score fields forever. *Recommend the bump.*

### 2.3 QML surface

`shot_processor.cpp::toAnalysisDetail` (`:83-188`) currently exposes `{tier, overall, series, phases}`.
Add `pattern`, `blended`, `resemblance` map, and `interval` so `ScreenWrist.qml` / `PpShotPanel` /
`PpMetricGraph` can show "bowed · 86 (±9)" + the three-bar resemblance and the blended badge.

**WP-1 acceptance:** struct + schema + reader round-trip a resemblance score with interval; a /2 doc
still loads; QML detail map carries the new keys. *Data-gated: No. Satisfies: §B.0a, §B.7, A1 note.*

---

## 3. Track A — application code (the estimand rework)

Dependency order: **WP-1 → WP-2 → {WP-3, WP-4} → WP-5 → WP-7**; WP-6 is independent (slot early).

### WP-2 — Wrist resemblance scorer (A.5 #12, core)

**Build** a new producer `src/Analysis/wrist_resemblance.{h,cpp}`:

- For each pattern `p ∈ {bowed, neutral, cupped}`: `R_p = round(100·exp(−½·d_p²))`,
  `d_p² = Σ_phase ((x − μ_p)/σ_p)²` over scored phases **{Top, Impact}**.
  **v1 scores FE only** (RUD/pronation are secondary, lower-weight, add later).
- `x` = neutral-relative lead-wrist FE sampled at Top and Impact from `MetricSeries` phaseSamples
  (impact-anchored). Reuse the sampling path already feeding `buildWristAngleSource`.
- Independent absolute resemblances (NOT normalised to sum 100). `overall = max R_p`,
  `patternLabel = argmax`, `blended = (top1 − top2) ≤ blendedDeltaPts (~10)`.
- `μ_p / σ_p` come from HackMotion published bowed/neutral/cupped top/impact ranges, sign-reconciled to
  flexion-positive (validation §6.3). Declared in `pp_tuned_constants.h`, **frozen** (see WP-5),
  flagged **provisional — re-seat at Corpus 2**.

**Wire** in `wrist_analyzer.cpp`: replace `SwingScorer::score(...)` (`:359`) **for sessionType == 1
only** with the resemblance scorer; `r.score = detail->score.overall` (`:382`) now = max R_p.
`SwingScorer` stays the producer for Swing(0)/GRF(2) adherence.

- **Files:** new `wrist_resemblance.{h,cpp}`; `wrist_analyzer.cpp:359,382`; `pp_tuned_constants.h`
  (new `scoring::resemblance::{bowed,neutral,cupped}` μ/σ); `CMakeLists.txt`; SwingLab `swinglab_run.cpp`
  (rebuild — links analyzer headers).
- **Test/synth:** `wrist_resemblance_test.cpp` — clean bowed → bowed high / others low; clean neutral;
  clean cupped; a blended (bowed/neutral) fixture flags `blended`. Needs WP-10 synth bowed/cupped/clean
  variants for end-to-end golden via SwingLab.
- **Data-gated:** structure **No**; `μ_p/σ_p` numerics **Yes (Corpus 2)** — provisional external anchor now.
- **Satisfies:** §B.0a (formula, surfacing), §B.0 (estimand), validation §6.3 (sign), A.5 #12 (B1).

### WP-3 — Wrist faults → coach layer; retire SwingScorer-for-Wrist (A.5 #12, rest)

- **Produce findings in the persisted path.** Today live `runAssessment = false` so `findings` is empty.
  Enable findings generation for Wrist (set `runAssessment = true` in
  `shot_processor.cpp::buildAnalysisJob`, or always-run the assessment block for sessionType==1) so
  `WristAssessmentEngine` findings reach `SwingAnalysis::findings`.
- **Route findings to coach, not score.** Persist `findings` as a **coach-input block** in swing.json
  (rename/relabel the existing `assessment.findings` write at `swing_doc.cpp:79-100` so it is
  unconditional for Wrist and clearly the coach feed, not a competing score). The resemblance `overall`
  is the only headline; findings never subtract from it.
- **Retire `SwingScorer`-for-Wrist** confirmed by WP-2 (no longer called for type 1).
- **Decision D-3:** WristAssessmentEngine `assess()` adds offline cost. *Recommend* always-run for
  Wrist (it is the only fault producer until FaultRanker/L8 exists) and revisit if it shows up in
  post-shot latency. Flagged, not a blocker.

- **Files:** `shot_processor.cpp::buildAnalysisJob` (`:427`); `wrist_analyzer.cpp:369-378`;
  `swing_doc.cpp:79-100` (persist coach findings unconditionally for Wrist); `swing_analysis.h`
  (doc-comment findings as coach input).
- **Test:** a scripted-cast synth fixture surfaces the finding in the coach block **and** the headline
  resemblance is unaffected by the finding (decoupling check).
- **Data-gated:** **No** (rule calibration itself stays frozen — WP-5 / #15).
- **Satisfies:** §B.0 (faults→coach), Appendix C (single score source), A.5 #12 (B1).

### WP-4 — Score uncertainty interval + per-cell error budget (A.5 #13 + #14)

- **Per-cell error budget** (`#14`): `σ_cell = σ_sensor ⊕ σ_crosstalk ⊕ (dθ/dt · σ_timing)` where
  `dθ/dt` is the local angle slope at the sampled phase and `σ_timing` is phase-timing jitter — i.e.
  segmentation and wrist-angle error validated **jointly**, not as independent stages. New helper
  `src/Analysis/score_uncertainty.{h,cpp}`.
- **Propagate to interval** (`#13`): turn the per-cell budget into `ScoreBreakdown::interval` on the
  resemblance score (e.g. "82 ± 9"). **Low confidence widens the interval; it never changes `overall`.**
- **Remove the perverse central term:** formally retire the `severity × confidence` term from any score
  path (`wrist_assessment_engine.cpp:113-118` score-v2). Score v2 is no longer a headline (WP-2); if its
  number is kept at all it is telemetry only and must not feed `overall`.
- **Error-budget constants:** `σ_sensor ≈ 5–8°`, `σ_crosstalk ≈ 10–15°` (FE↔RUD), `σ_timing` from the
  phase-timing budget — declared frozen in `pp_tuned_constants.h`.

- **Files:** new `score_uncertainty.{h,cpp}`; `wrist_resemblance.cpp` (attach interval);
  `wrist_assessment_engine.cpp:113-118` (strip central coupling); `pp_tuned_constants.h`;
  `swing_analysis.h`/`swing_doc.cpp` (interval field — shape from WP-1).
- **Test:** interval **widens** monotonically as cell confidence drops; `overall` is **invariant** to
  confidence (kills B2); interval present and `0 ≤ lo ≤ overall ≤ hi ≤ 100`.
- **Data-gated:** **No**.
- **Satisfies:** §B.7, A.5 #13 (A3,B2,C5), #14 (C3).

### WP-5 — Expose discrimination literals as frozen dotted keys (A.5 #15)

Lift the inline literals into `pp_tuned_constants.h` + the dotted-key override path, **kept frozen**
(parity test guards byte-identity):

| Literal | Current location | New dotted key |
|---|---|---|
| flip `−8.0 / −5.0` | `assessment_rules.cpp:104-105` | `rules.flipFaultDeg` / `rules.flipWatchDeg` |
| trailWrist `−8.0` | `assessment_rules.cpp:221` | `rules.trailFlattenDeg` |
| `detectArchetype ±10.0` | `wrist_assessment_engine.cpp:37-38` | `rules.archetypeTopDeltaDeg` |
| `archetypeFaceOffset ±10.0` | `reference_bands.cpp:101-102` | `bands.archetypeFaceOffsetDeg` |
| resemblance `μ_p / σ_p` | new (WP-2) | `score.resemblance.<p>.{muTop,muImpact,sigma}` |
| `blendedDeltaPts ~10` | new (WP-2) | `score.resemblance.blendedDeltaPts` |

- **Files:** `pp_tuned_constants.h`; `assessment_rules.cpp`; `wrist_assessment_engine.cpp`;
  `reference_bands.cpp`; `wrist_assessment_tuning.h` / `analysis_tuning.h`; `tuned_constants_parity_test`.
- **Test:** parity test still green; an override of each key changes behaviour in a unit test.
- **Data-gated:** *expose* **No**; *sweep* **frozen until labels** (enforced in SwingLab — WP-9).
- **Satisfies:** A.5 #15 (C1).

### WP-6 — Fix inert `pitchProxyDeg = 0` (A.1 #1) — independent, slot early

- Populate a real per-sample pitch-proxy in the L6 MetricExtractor; thread it through
  `wrist_analysis_adapter.cpp:128` (live builder) and `:76` (reload) so the sampler's gimbal/
  Indeterminate gate (`wrist_angle_sampler.h:108`, `median(pitch) ≥ 75°`) can actually fire.
- **Files:** MetricExtractor (L6 metric pass) + `MetricSeries` shape; `wrist_analysis_adapter.cpp:76,128`.
- **Test:** a synth gimbal-region fixture marks the affected cells `Indeterminate`; a normal swing does
  not (so a `sampler.gimbalThresholdDeg` sweep becomes observable in `diag.*`).
- **Data-gated:** **No**.
- **Satisfies:** A.5 A.1#1 (B4). Prereq for any `sampler.*` sweep being meaningful (validation B4).

### WP-7 — Internal-consistency tests of the scoring construction (A.5 #18)

`src/Analysis/tests/wrist_resemblance_test.cpp` (+ extend `swing_scorer_test`):

- **Monotonicity:** `R_p` strictly decreases as FE moves away from `μ_p` (per phase).
- **Boundedness:** every `R_p`, `overall` ∈ [0,100]; interval clamped.
- **Deadband-seam continuity:** no discontinuity at band seams (the adherence path,
  `swing_scorer.cpp:130-137`).
- **Decoupling:** `overall` invariant to confidence; interval widens with lower confidence (from WP-4).
- **Surfacing:** `overall == max R_p`; `patternLabel == argmax`; `blended` iff top-two ≤ delta.
- **Files:** new test + CTest target; CMake.
- **Data-gated:** **No** (independent of any corpus). **Satisfies:** A.5 #18 (D5).

---

## 4. Track B — SwingLab alignment (Phase 2, part 1)

### WP-8 — Read & check the reworked score (A.5 #12/#13/#18 measurement)

- Rebuild `swinglab_run.cpp` (free — links analyzer; `result.json` carries the new fields once WP-1
  lands via SwingDocWriter).
- `tools/swinglab/swinglab/score.py` — add checks that **read `analysis.score`** (today it never does):
  - `score.resemblance_bounded` — each `R_p` ∈ [0,100] (fail).
  - `score.headline_consistent` — `overall == max(resemblance)`, `pattern == argmax` (fail).
  - `score.blended_flag` — `blended` iff top-two ≤ `blendedDeltaPts` (warn).
  - `score.interval_present` — interval present, `0 ≤ lo ≤ overall ≤ hi ≤ 100` (fail).
  - `score.resemblance_recall` — on a known bowed/neutral/cupped synth (WP-10), `pattern` matches the
    stamped archetype (fail; Tier-2-Ext, gated on `truth.meta.archetype`).
- **Files:** `score.py` (`scorecard()` + new check group), `swinglab_run.cpp` (rebuild only).
- **Satisfies:** makes #12/#13/#18 *measurable* offline.

### WP-9 — Freeze enforcement + soft sweep penalty (A.5 #15/#16)

- **Freeze (`#15`):** `core.py::sweep()` (`:245-327`) must **refuse** to sweep keys under
  `score.*` / `rules.*` / `bands.*` unless an explicit `--allow-frozen` flag is set (reserved for the
  post-label supervised pass). Today these are swept identically to `shaft.*` with no guard.
- **Soft penalty (`#16`):** replace the hard in-search gate (`gated = baseline and regressions>0` →
  excluded, `core.py:274-285`) with a **soft regression penalty** in the search objective; keep the
  **hard `regressions==0` gate only at the accept/freeze boundary**. Acknowledge `seg.*` non-separability.
- **Files:** `tools/swinglab/swinglab/core.py`.
- **Satisfies:** A.5 #15 (C1, the freeze half), #16 (D2). Aligns the harness with §7.1's split.

### WP-10 — Synth: impact-saturation + scripted faults/archetypes (A.5 A.2#4, +#12 goldens)

- **Impact-saturation (`A.2#4`):** add a `±16 g` accel clip + ringing near `impactUs` to `synth.py`'s
  IMU stream (today `phi(t)` is C¹ through impact, accel = pure gravity — `score.py:251-253` admits the
  gates can't be exercised). This is the **only pre-Corpus-2 way** to verify the impact path (D4).
- **Archetype/fault stamps (for WP-2/WP-3/WP-8 goldens):** add clean/control + scripted bowed / neutral
  / cupped variants and a scripted-cast/flip, stamping `truth.meta.archetype` and `truth.meta.knownGroup`.
- **Files:** `tools/swinglab/swinglab/synth.py` (+ CLI flags in `lab.py`).
- **Satisfies:** A.5 A.2#4 (D4); supplies fixtures for `score.resemblance_recall` and `diag.recall`.

---

## 5. Track C — validation/optimisation doc (Phase 2, part 2)

### WP-11 — Doc edits (A.5 #16 method + #17)

- **§7 / §7.1:** state that `score.*` / `rules.*` / `bands.*` are **frozen, not swept, until labels**;
  remove `score.*` from the coordinate-descent knob list; band-fitting is **distributional goodness-of-
  fit** (external tour ranges + corpus distribution), **never a corpus-score sweep**; the search uses a
  **soft** regression penalty, hard gate only at accept/freeze.
- **§10 acceptance tables:** relabel `imu_vision_corr` as **verification/consistency, not validation**
  (D3); add the cross-talk caveat: *"reliability < X° does not bound accuracy; a 10–15° systematic
  FE/RUD leak passes every Corpus-1 wrist gate"* (C4).
- **Files:** `docs/validation/pipeline_validation_and_tuning.md` (§7/§7.1, §10).
- **Data-gated:** **No** (doc-only). **Satisfies:** A.5 #16 (method), #17 (D3, C4).

### WP-12 — Reconcile design Appendix C (do *after* Track A lands)

- Update Appendix C "as-built": resemblance is the real Wrist score; converge to one band source of
  truth; note interval + coach-findings now persisted. Keep "validated synthetic-only" until Corpus 1.
- **Files:** `docs/design/shot_analyzer_design.md` Appendix C. **Data-gated:** **No** (post-impl).

---

## 6. Sequence & gating summary

```
WP-1 shapes  ─┬─► WP-2 resemblance ─┬─► WP-4 interval/budget ─► WP-7 tests
              │                      ├─► WP-3 faults→coach
              │                      └─► WP-5 freeze literals ─► WP-9 sweep freeze/soft
              ├─► WP-8 swinglab reads score
              └─► (WP-6 pitchProxy — independent, slot early)
WP-10 synth (parallel; needed for WP-2/WP-8 goldens)
WP-11 doc edits (parallel; cheapest — can land first to lock method)
WP-12 Appendix C (after Track A)
```

| WP | A.5 | Track | Data-gated | Buildable now |
|----|-----|-------|------------|---------------|
| 1 shapes | 12,13 | A | No | ✅ |
| 2 resemblance | 12 | A | μ/σ only (Corpus 2) | ✅ (provisional μ/σ) |
| 3 faults→coach | 12 | A | No | ✅ |
| 4 interval+budget | 13,14 | A | No | ✅ |
| 5 freeze literals | 15 | A | sweep frozen | ✅ (expose) |
| 6 pitchProxy | A.1#1 | A | No | ✅ |
| 7 consistency tests | 18 | A | No | ✅ |
| 8 swinglab reads | 12,13,18 | B | No | ✅ |
| 9 freeze+soft sweep | 15,16 | B | No | ✅ |
| 10 synth spike/faults | A.2#4,12 | B | No | ✅ |
| 11 doc edits | 16,17 | C | No | ✅ |
| 12 Appendix C | — | C | No | post-impl |

**Frozen until labels/Corpus:** all `score.*` / `rules.*` / `bands.*` *values* (the keys are exposed by
WP-5 but not swept); resemblance `μ_p/σ_p` are provisional/external-anchored and re-seated at Corpus 2.
**Nothing in this plan needs new hardware.**

## 7. Resolved decisions (locked 2026-06-28)

- **D-1 → Extend one struct.** `ScoreBreakdown` gains a `ScoreKind kind` discriminator plus the
  resemblance / label / blended / interval fields; Swing/GRF leave them empty. One persisted carrier,
  one QML role set.
- **D-2 → Bump `pinpoint.analysis/3`, `score` as object.** Writer emits the object form; `readSwingJson`
  stays tolerant of the legacy `/2` bare-int `score`.
- **D-3 → Always-run WristAssessmentEngine for Wrist.** It is the sole fault producer until FaultRanker
  (L8) exists; findings persist to the coach block. Revisit only if post-shot latency regresses.

## 8. Out of scope (data-gated — not blockers)

- Final `μ_p/σ_p` and `kWristBands` re-seat (Corpus 2 / HackMotion concurrent).
- Any `score.*` / `rules.*` / `bands.*` **sweep** (needs labels; supervised fault-rule calibration).
- FaultRanker (L8) proper; multi-golfer norm-referencing (Corpus 3).
- Corpus 1 capture itself (legacy data flagged unreliable).
