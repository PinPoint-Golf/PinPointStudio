# PinPoint Studio — Pipeline Validation, Tuning & Refinement (Academic Reference)

**Status:** Reference for execution (2026-06-26). Supersedes the *external-reference* decision in
`corpus_v1_validation_plan.md` §5/§10, and re-frames the work as a **three-corpus progression** with
explicit **statistical power / sample-size** reasoning per stage.
**Owner:** Mark Liversedge · **Audience:** anyone driving the SwingLab tune/validate loop.
**Harness:** SwingLab (`tools/swinglab/`, `swinglab_run` + `lab.py`) — see
`docs/developer/swinglab_developer_guide.md`.
**Companion docs:** `docs/implementation/corpus_v1_validation_plan.md` (the Corpus-1 operational runbook
this extends), `docs/design/shot_analyzer_design.md` (the nine-layer pipeline),
`docs/reference/wristmetrics.md` (sign/norm decisions), `docs/implementation/wrist_assessment.md` (the
diagnosis engine).

---

## 0. What this document is, and is not

The corpus plan is the **operational runbook** — capture gates, the diff-gate discipline, the C0–C5
sequence. This document is the **methodological backbone** beneath it: what evidence each claim requires,
what counts as proof, **how many swings it takes to prove it**, and how we avoid fooling ourselves.

Two organising ideas, both new since the first draft:

1. **The validation is delivered across three nested corpora** (§2). Each is a deliberate increment in
   instrumentation and in the *kind* of validity it can demonstrate, and each **reuses and extends the
   tests of the one before it** — Corpus 2's test suite is a superset of Corpus 1's; Corpus 3's a superset
   of Corpus 2's. We never re-litigate a settled stage; we add the next rung of the validity ladder on top
   of it.
2. **Sample size is set by the statistic, not the stage** (§3). "How many swings?" has a different answer
   for a pass-rate, an agreement limit, a reliability coefficient, and an RMSE — and the honest answer for
   between-subject generalisation is "more *golfers*, not more swings." §3 gives the formulae and the
   concrete N per goal, and ties each corpus's size to what it must prove.

It is structured along the processing pipeline — discovery → streaming → filtering → pose → metric
extraction → diagnosis — so each algorithm is validated in the context that produces its inputs (§5).

> **Why the rigour.** The Wrist analyzer is **the only non-stub analyzer** (`shot_analyzer.cpp:61–93`).
> Everything a golfer or coach acts on flows through this one pipeline. A precise, confident, *wrong*
> number is worse than a missing one — it gets acted on. Validation here is the difference between a
> coaching tool and a random-number generator with a nice chart.

---

## 1. Epistemic framing — the vocabulary we hold ourselves to

### 1.1 Three distinct activities, never conflated

| Activity | Question | Evidence | Failure if skipped |
|---|---|---|---|
| **Verification** | "Did we build the thing right?" | golden tests, determinism, closed-form synth | bugs masquerade as biomechanics |
| **Validation** | "Did we build the right thing?" | agreement with an independent measure of truth | confident, precise, wrong numbers |
| **Tuning (calibration)** | "What parameter values make it best?" | objective optimised on a *tuning* partition | overfit knobs that don't generalise |

The cardinal sin is tuning a parameter to a metric, then reporting that same metric as validation. A
parameter optimised on a set is *described*, not *validated*, by that set's score. Validation always uses
data the tuning never touched (§7).

### 1.2 The validity hierarchy (measurement science)

A metric earns trust by climbing the construct-validation ladder. **Each corpus unlocks the next rung:**

- **Face validity** — does the number behave sanely? (a bowed wrist reads bowed). Cheap, weak. *Corpus 1.*
- **Content validity** — does the metric span the construct? (FE/RUD/PS cover the clinical wrist DOFs —
  Wu/ISB 2005). Argued from literature. *Corpus 1.*
- **Construct / known-groups validity** — do *deliberately different* swings separate? (a scripted cast
  scores worse on `cast`). The cheapest *empirical* validity. *Corpus 1.*
- **Criterion / concurrent validity** — does the metric agree with an accepted reference measured *at the
  same time*? **HackMotion. *Corpus 2.*** (Corpus 1 had none → wrist validation capped at repeatability +
  plausibility.)
- **Predictive validity** — does the metric predict an outcome that matters? (does a flagged open face
  predict a measured push/slice?). Needs ball-flight labels. ***Corpus 3+* / future markup (§8).**
- **External validity** — does it generalise across people? Needs *multiple golfers* (§3.5). ***Corpus 3.***

### 1.3 Reliability ≠ Agreement ≠ Accuracy

Routinely confused, and the confusion is fatal to a coaching product:

- **Reliability (repeatability)** — same motion, repeated, same number? Quantified by **test–retest ICC**,
  **SEM**, and **MDC** (`MDC₉₅ = 1.96·√2·SEM`). A metric whose MDC exceeds the effect a coach wants to see
  is useless *regardless of accuracy*. (Corpus-1 repeatability sets.)
- **Agreement** — two methods, same event, same number? Quantified by **Bland–Altman** bias + 95 % limits
  of agreement (LoA), **not** correlation — a high *r* coexists with a large constant bias. (Corpus-2
  HackMotion.)
- **Accuracy** — agreement against a *gold standard*. HackMotion is a **criterion reference, not gold
  standard** (it is itself an IMU device, ICC 0.95–0.99 vs goniometry, ~1° FE / ~5° RUD —
  `wristmetrics.md`). So we earn *concurrent validity bounded by the criterion's own LoA*, never absolute
  accuracy (§6.7).

### 1.4 The instruments of truth, by corpus

| Instrument | Authority | Scope | Corpus |
|---|---|---|---|
| **Synthetic corpus** | exact (closed-form) | verification only | all |
| **Physics invariants** (Tier 1/2) | self-consistency | all swings, every stage | 1 → carried forward |
| **Repeatability sets** | reliability (not accuracy) | precision of every metric | 1 |
| **Hand labels** (Markup Lab → `truth.json`) | human, ~frame-accurate | track/phase absolute timing & angle | 1 |
| **HackMotion** (→ `hackmotion.json`) | criterion (concurrent) | wrist FE/RUD/PS | **pilot in 1; full in 2** |
| **Camera 2D calibration** (ChArUco + ground-plane) | metric world frame | metric-scale camera metrics | **3** |
| **Multiple golfers** | external validity | population generalisation | **3** |
| **Launch monitor** (future) | outcome criterion | predictive validity of *faults* | 3+ |
| **Coach annotation** (future) | expert construct | validity of the *diagnosis* | 3+ |

No single instrument validates the pipeline. The design is **triangulation that deepens corpus by
corpus.**

---

## 2. The three-corpus progression

The corpora are not three independent datasets — they are **three rungs of one ladder**, each adding
instrumentation and a new class of validity *on top of a settled foundation*. The rule: **a stage signed
off on Corpus N is not re-opened on Corpus N+1; its tests simply continue to run as a regression net while
the new layer is validated.**

### Corpus 1 — IMU-only, internal consistency & reliability *(the current `corpus-v1`)*

- **Instrumentation:** lead-forearm + lead-hand IMU (some 3-sensor with upper-arm), bound + calibrated;
  MP4 face-on (+ DTL); ~12-swing raw subset. Native video format (no `.raw` except the subset).
- **Validity earned:** face, content, **construct/known-groups** (scripted faults), **reliability**
  (repeatability sets), and **internal cross-modal consistency** (`imu_vision_corr`), plus **absolute
  track/phase accuracy** against hand labels (Tier-3). *No external criterion for the wrist angles.*
- **HackMotion's role = an informal pilot, not corpus data.** A single co-capture session is run *to
  de-risk Corpus 2*: prove the two devices can be co-worn without perturbing each other, shake out the
  export-format → `hackmotion.json` → reconcile → `score.py` tooling, and — most importantly —
  **estimate the SD of the IMU↔HackMotion difference**, which is the number that *sizes* Corpus 2
  (§3.2, §6.2). A pilot has no power requirement; its job is to de-risk the protocol and estimate the
  variance for the definitive study.
- **Delivers:** the capture gate, the scorecard, the labelling workflow, the known-groups fixtures, and
  the reliability baseline — i.e. **all the machinery Corpus 2 reuses.**

### Corpus 2 — IMU **+** HackMotion concurrent, criterion validity

- **Instrumentation:** Corpus 1's rig **plus the HackMotion glove sensor worn simultaneously** over the
  same shots.
- **Builds on Corpus 1:** runs the *entire* Corpus-1 test suite unchanged (invariants, repeatability,
  Tier-3, known-groups) as a regression net, **and adds the criterion-agreement layer**: Bland–Altman /
  ICC / RMSE of PinPoint vs HackMotion, per axis × per phase × trajectory (§6).
- **Validity earned:** **concurrent (criterion) validity** for FE/RUD/PS — the rung Corpus 1 could not
  reach.
- **Delivers the refinements that need a criterion:** locks the provisional FE/RUD/PS *signs*, diagnoses
  anatomical `A·M` alignment from constant bias / cross-axis leakage, and re-seats the scoring bands
  (`kWristBands`) on a HackMotion-cross-validated scale (§6.6).

### Corpus 3 — **+** camera 2D calibration, IMU calibration, HackMotion verification *(later)*

- **Instrumentation:** Corpus 2's rig **plus** ChArUco camera **intrinsics + ground-plane extrinsic**
  (metric world frame + scale), a fully-characterised IMU calibration, and HackMotion as an ongoing
  **verification** check rather than a one-off study. **Ideally multiple golfers** (§3.5).
- **Builds on Corpus 2:** runs Corpus 1 + 2 suites as regression nets, **and adds**: the metric-scale
  **`Mono3D+IMU`** camera tier (frontal-plane translations: sway/lift/secondary-tilt), **multi-instrument
  cross-validation** (camera-2D elbow vs IMU vs — where applicable — HackMotion), and the first
  **external-validity** evidence (between-golfer generalisation).
- **Validity earned:** **external validity** (across golfers) and the camera-metric branch; sets up
  **predictive validity** if a launch monitor is present (§8).

```
Corpus 1 ─────────────────► Corpus 2 ─────────────────► Corpus 3
IMU-only                    + HackMotion (concurrent)    + camera/IMU calib, multi-golfer
face/content/construct      criterion validity           external validity + camera metrics
+ reliability + Tier-3      (locks signs, A·M, bands)     (+ predictive, if launch monitor)
HackMotion = PILOT          HackMotion = STUDY            HackMotion = VERIFICATION
(estimates Corpus-2 SD)
 │  tests ───────────────────►  superset ───────────────────► superset
```

### 2.4 Tuning ledger — what is refined at each corpus, and the deciding criterion

The criterion **escalates** corpus by corpus: Corpus 1 decides on **internal evidence** (physics
invariants, cross-modal agreement, repeatability, hand labels, known-groups); Corpus 2 adds the
**external criterion** (HackMotion concurrent agreement) and is where the wrist-angle layer is *locked*;
Corpus 3 adds **generalisation** (across golfers) and the camera/diagnosis arbiters. **A tuning surface
locked at corpus N is not re-opened at N+1** — later corpora confirm it as a regression net and build the
next layer on top. Every change, at every corpus, also obeys the universal gate: **kept only if mean ↑ and
`lab.py diff` = `regressions: 0`** (reference §7). "Status" marks whether the knob is injectable today
(dotted-key `tuningOverrides`) or needs a small C++ change first (**escalation**).

| Tuning surface (knobs) | Refined at | Deciding criterion | Status |
|---|---|---|---|
| **BLE discovery / reconnect** (`kConnectWatchdogMs`, `kMaxRetries`, `kRetryBaseDelayMs`) | pre-C1 (live) | discovery completeness 100 %; reconnect ≥ 99 % | code |
| **Capture gate** (no tuning — a census) | C1 | Tier-0 pass = 100 % (a gate, never "tuned around") | n/a |
| **Orientation filter — phase-adaptive gain + impact handling** (`filter.refuse/adaptive/betaStatic/betaDynamic/accelErrGateG/gyroGateDps/impactBlankPreMs/impactBlankPostMs/accelSatG`) | **C1 → C2 → C3** | C1: per-phase `imu_vision_corr` ↑, impact-continuity, bounded net drift; **C2: wrist FE/RUD/PS *just after impact* within HackMotion LoA**; C3: one schedule generalises, or scales with measured per-swing dynamics | **dotted-key (`filter.*`)** — `filter.refuse` feeds the re-fused (optionally phase-adaptive) orientation into `ImuVisionFuser`, so it moves the wrist metric; observable via `xmodal.imu_vision_corr` (vision) + `diag.*` (labels) + the provisional `filter.impact_continuity` check |
| **Filter choice** Madgwick vs ESKF (`orientationFilter`/stream) | C1 A/B (per phase) → confirmed C2/C3 | higher per-phase corr / lower jitter; better behaviour through the impact window | code (runtime-selectable) |
| **Seed tolerances** (`kInitAccelTolG`, `kInitGyroMaxRadps`, `kInitMaxSeedAttempts`) | C1 | converged before Takeaway | code |
| **Anatomical calibration `A·M`** + **wrist-axis signs** | **C2 (signs LOCKED here)** | constant per-axis bias / cross-axis leakage vs HackMotion → diagnosed & corrected; static-pose anchors agree | escalation (signs not dotted-key) |
| **Temporal cleanup** (RTS `jerkPsd`/joint; Kabsch still-window) | C1 | track-continuity invariants; no lag at impact | dotted-key (jerkPsd) |
| **Phase segmentation** (`seg.*`: `fcEnvelopeHz`, `top*`, `takeaway*`, `voteAgreeUs`, `finish*`, stillness gates) | C1 | `seg.monotone` 100 %; **Top ≤ 30 ms** vs labels; tempo distribution sane | dotted-key |
| **Shaft detection** (`shaft.*`: `ridgeKernelPx`, `noiseSigmaK`, `thresholdFloor`, `nmsSeparationDeg`, `clutterMaskDeg`, `minScoreFrac`, `runMaxGapPx`, `interHandSigmaDeg`) | C1 (raw subset for pixel-θ) | coverage ≥ 0.6; continuity; `theta_rms < 3°` / `head < 25 px` on labelled | dotted-key |
| **Shaft skeleton-aware flags** (`shaft.useArmScale`/`minLenFracOfArm`/`useKinematicPrior`/`useEnvelope`/`useBlurMode`/`emitPredicted`/`useBackgroundSub`/`twoPassCalibration`/`autoChirality`) | C1 (flag-flip A/B) | each flag's own gate (0 regressions / 0 legit samples lost / 0 false rejections) | dotted-key (default OFF) |
| **Shaft assembly** (`assembly.*`: `coverageMin`, `jerkPsd`, `transSigma*`, `visionSigmaFloorRad`, `calibAcceptRad`) | C1 | coverage; ŝ_hand fit residual < ~7° | dotted-key |
| **Wrist-angle sampler** (`windowHalfUs`, `gimbalThresholdDeg`, `minValidSamples`) | C1 (repeatability) → C2 (gimbal logic validated vs HackMotion) | repeatability RMSE < 5° FE / 8° RUD; < 2 % Indeterminate; C2 confirms gimbal threshold | **dotted-key (`sampler.*`)** — observable via the offline assessment pass (`runAssessment`) |
| **Scoring bands** (`kWristBands` μ/σ/oneSided/weight; deadbands `zIn/zOut/p`) | **C2 (re-seated)** | match the observed + HackMotion tour-range distribution; score monotone | **dotted-key (`score.*`)** — moves `r.score` directly |
| **Assessment rules** (`RuleTuning`: `confidenceFloor`, `scoreScale`, severity weights, `corroborationBoost`) + **reference-band margins** (`bands.*`) | C1 (known-groups) → **C3 (coach κ/ICC)** | scripted-fault recall / FP; C3: κ (fault) + ICC (score) vs blinded coach | **dotted-key (`rules.*` / `bands.*`)** — observable via offline assessment + score.py `diag.*` group |
| **Shot-detection sensitivity / latency** (`swingDetectionSensitivity` 1.5/1.0/0.7; `kImuBleLatencyUs`/`audioDeviceLatencyUs`) | C1 (precision/recall, self) → Ext (impact-truth markup, future) | recall ≥ 0.95 real strikes / ≈ 0 FP; latency ± 1 frame *needs* impact-truth markup | code |
| **Camera 2D calibration** (ChArUco intrinsics + ground-plane extrinsic) | **C3** | reproj RMS < 0.5 px; wand QA within 0.5 % | code (calibration flow) |

> **The wrist-angle layer is locked at Corpus 2**, not Corpus 1: signs, `A·M` alignment, the sampler's
> gimbal threshold, and the scoring bands all need the HackMotion criterion to settle. Corpus 1 produces
> their *provisional* values from repeatability + plausibility; Corpus 3 only confirms they generalise.

> **The freeze edit-point is one header.** Every parameter that is *tuned during validation and then
> frozen* draws its compiled-in default from **`src/Core/pp_tuned_constants.h`** (`pinpoint::tuned::*`):
> Madgwick `kBeta`, the seed tolerances, the mount quats + axis-angle bounds, the `kWristBands` numerics +
> deadbands, the sampler defaults, and the `RuleTuning` defaults. During tuning, SwingLab injects
> candidate values at run time via the dotted keys (the header is not consulted on the override path);
> when validation locks a value, edit the single literal here and every consumer — live IMU path *and*
> offline analyzer — picks it up. `tuned_constants_parity_test` guards that the indirection stays
> byte-identical. (Wrist axis *sign conventions* stay in `wrist_angles.h` — they are sign/axis choices in
> code structure, not single numeric literals.)

---

## 3. Sample size & statistical power

**Principle: the N you need is set by the statistic you compute, not by the pipeline stage.** Five
statistical goals recur across the pipeline; each has its own driver. Numbers below are derived, not
folklore — the formula is given so a reviewer can re-derive the target for a different precision.

### 3.1 Estimating a proportion (pass-rate, fault recall, false-positive rate)

A binomial-proportion confidence interval: `n = z²·p(1−p)/E²` (z = 1.96 at 95 %, E = half-width).

| Want | p ≈ 0.9 | p ≈ 0.5 (worst case) |
|---|---|---|
| ±10 % | **n ≈ 35** | n ≈ 96 |
| ±5 % | n ≈ 138 | n ≈ 384 |

The **rule of three** governs all-pass results: observing `k/k` successes gives a 95 % lower bound on the
true rate of ≈ `1 − 3/k`. So **12/12 caught faults ⇒ recall ≥ 0.75 only**; to *claim* recall ≥ 0.95 you
need ≈ **60 clean trials of that fault**.

**Implications.**
- *Invariant pass-rates* ("≥ 90 % of swings pass check X") and *cross-modal* (`imu_vision_corr ≥ 0.9`
  proportion): **50 swings → ±~10–14 %.** Corpus 1's 50 is adequate for the gross escalation triggers
  (a check failing > 30 % of corpus), not for a tight pass-rate.
- *Known-groups fault recall* is the expensive one: a *statistically defensible* recall ≥ 0.95 needs
  ~60 examples **per fault** — impractical across 8 faults. **Decision:** Corpus 1 scripts **~10–15 per
  fault**, reports the **point estimate with its (wide) Wilson CI**, and claims *"the rule fires reliably
  and is specific"* — **not** a numeric recall guarantee. A definitive recall figure is deferred to a
  pooled multi-corpus fault library (§8).

### 3.2 Estimating agreement limits (Bland–Altman LoA) — the Corpus-2 driver

The SE of a limit of agreement ≈ `1.71·s/√n`; its 95 % CI half-width ≈ `3.4·s/√n` (s = SD of the
device-to-device differences, *estimated by the Corpus-1 pilot*).

| n (paired swings) | LoA 95 % CI half-width |
|---|---|
| 25 | ≈ 0.68·s |
| 40 | ≈ 0.54·s |
| **50** | **≈ 0.48·s** |
| 100 | ≈ 0.34·s |

**Recommendation: ~50 paired (IMU + HackMotion) swings per agreement claim.** Because all three axes are
captured on every co-captured swing, **~50 co-captured swings sizes FE/RUD/PS at once** with LoA quotable
to ±~0.5 SD; 25 is a provisional floor. This is what fixes **Corpus 2 ≈ 50 paired swings** (plus a
repeatability sub-stratum, below). *The Corpus-1 pilot's measured `s` lets us confirm this n before
committing the studio time* — if FE differences are tighter than assumed, 40 may suffice; if RUD is loose
(expected), it may need more, captured honestly.

### 3.3 Estimating reliability (SEM / MDC / ICC) — the Corpus-1 repeatability driver

The typical-error (SEM) estimate has a sampling CV ≈ `1/√(2(n−1))` (Hopkins). ICC point estimates
stabilise and their CIs tighten with the number of repeated units.

| n (repeated swings, matched motion) | precision on SEM |
|---|---|
| 15 | ±19 % |
| **25–30** | **±13–14 %** |
| 50 | ±10 % |

**Recommendation: ≥ 25–30 repeated swings** (one golfer, same club/target, in sets of 3–5) for a usable
SEM/MDC and a reasonably bounded ICC. The corpus plan's ~20 repeatability stratum is *near*-adequate —
**push it to ~25–30.** This applies to **both** PinPoint *and* HackMotion in Corpus 2 (the criterion's own
SEM is the floor on achievable agreement — §6.5).

### 3.4 Estimating an RMSE / SD vs labels (Tier-3 track & phase accuracy)

The χ² CI on an estimated SD from `n` points: ±20 % needs n ≈ 13; **±15 % needs n ≈ 23**; ±10 % needs
n ≈ 50. But **each labelled swing yields ~8–10 P-position points**, so the *point* count makes the RMSE
precise while the *swing* count is set by condition coverage.

**Recommendation: 10–15 labelled swings** (≈ 100–150 points) gives a tight per-axis θ-RMSE; widen the
swing count for *coverage of conditions* (tempo, club, clutter), not for statistical precision. The corpus
plan's 10–15 is correct.

**Club-length model-form selection** (stage-2 clubhead, §5.5): the same labelled swings serve, but the
choice *between geometric forms* needs **club diversity, not just swing count** — the plane/foreshortening
geometry varies with club length, so hold out entire **clubs** (GW / mid-iron / driver minimum) as well as
whole swings; never hold out random frames (frames within a swing are maximally clustered, §3.5). A single
labelled swing cannot adjudicate the form at all: it may itself be off-plane.

### 3.5 The independence / clustering caveat — *the one that bites hardest*

**Repeated swings from one golfer are not `n` independent observations for *between-subject* claims.** The
effective N for population generalisation is the **number of golfers**, not swings. Consequences:

- **Corpus 1 & 2 validate *within-subject*** precision and agreement — *"for this golfer, on this rig, the
  device agrees with HackMotion to ±X°."* That is **exactly** what locking signs, diagnosing `A·M`, and
  setting the measurement error require, because those depend on the **instrument + mount**, not the
  population. Single-golfer is the *right* design for Corpus 1/2.
- **Between-subject / external validity** (across body types, swing styles, handicaps) needs **multiple
  golfers — a practical floor of ~8–12** for a usable between-subject variance estimate, more for tight
  CIs. Clustered data carry a **design effect** `≈ 1 + (m−1)·ρ` (m swings/golfer, ρ intra-golfer
  correlation), which *inflates* the required total. **This is Corpus 3's job** (its diversity stratum),
  and the document states plainly that no population claim is made before it.

### 3.6 Detecting a change (the diff gate) vs estimating a quantity

The diff gate is a *guard*, not a hypothesis test, but the power arithmetic still matters: a paired
comparison at n = 50 has ~0.9 power to catch a **moderate** per-swing regression (d ≈ 0.5) but only
~0.3 power for a **small** one (d ≈ 0.2, which would need ~200 pairs). **This is precisely why the gate is
per-swing (`regressions: 0`), not mean-only** — a small mean improvement can hide a large regression on
two swings that a power-limited mean test would miss.

### 3.7 Sample-size summary

| Statistical goal | Driver / formula | Usable N | Tight N | Corpus |
|---|---|---|---|---|
| Proportion (pass-rate, recall, FP) | `n=z²p(1−p)/E²`; rule of three | ~35 (±10 %) | ~140 (±5 %); ~60/fault for recall≥0.95 | 1 |
| Agreement LoA (Bland–Altman) | `SE≈1.71s/√n` | ~25 (±0.68 s) | **~50 (±0.48 s)** | **2** |
| Reliability (SEM/MDC/ICC) | `CV≈1/√(2(n−1))` | **~25–30 (±14 %)** | ~50 (±10 %) | 1 (both devices in 2) |
| RMSE/SD vs labels | χ² on variance | **10–15 swings ×~10 pts** | — | 1 |
| Regression detection (A/B) | paired-t power | 50 pairs: d≥0.5 @0.9 | 200 pairs: d≈0.2 | all (per-swing gate) |
| External validity (generalisation) | **golfers, not swings**; design effect | ~8–12 golfers | more | **3** |

**Net per corpus:** Corpus 1 ≈ **50 swings** (incl. ~25–30 repeatability, 10–15 labelled, ~10–15/scripted
fault) + a **single HackMotion pilot session**; Corpus 2 ≈ **50 paired** IMU+HackMotion swings (incl. a
≥ 25 repeatability sub-stratum); Corpus 3 ≈ **multi-golfer (~8–12 golfers × several swings)** plus the
calibration captures — sized from Corpus-2 variance once known.

---

## 4. Reproducibility & provenance preconditions (inherited from SwingLab)

Before any number is trusted these must hold — the lab's existing contract, restated as validation
preconditions:

- **Replay production code, never a reimplementation** — the scorecard *judges* outputs, never recomputes
  the pipeline. A Python re-derivation would validate a fiction.
- **Attribution** — every scorecard carries git SHA + params hash + corpus manifest hash. "Regression in
  swing_0007" must always answer *what changed*.
- **Blessing** — a corpus root without `CORPUS.md` (date + calibration provenance + **corpus tier 1/2/3 +
  partition**, §7) is `blessed: false` and cannot drive a conclusion.
- **Same-host comparison only** — CPU vs CUDA pose differs subtly; the diff gate compares same-host runs.
- **No silent truncation** — any coverage cap (top-N, sampling, dropped swings) is logged; a pruned
  data-failure swing is *reported as pruned*.

---

## 5. Per-stage validation, tuning & refinement

The pipeline, end to end, with each stage tagged by the corpus that delivers its evidence:

```
 discovery ─► streaming/ingest ─► filtering ─► pose detection ─► metric extraction ─► diagnosis
 (Enumerator  (EventBuffer +     (IMU fusion +  (ViTPose +        (segmentation +      (scoring +
  + BLE scan)  capture gate)      calibration +  triangulation +   shaft assembly +     faults +
                                  KF/RTS)        mono-lift)        wrist angles +       assessment)
                                                                   kinematic seq)
   C1 (live)    C1               C1 / C2(A·M)    C1 / C3(calib)    C1 / C2(wrist)       C1 / C2(bands)/C3(coach)
```

Each stage: **Claim → Verify → Validate → Tune → Accept/Refine**, with the **N** drawn from §3.

### 5.1 Device discovery & enumeration *(Corpus 1, live — SwingLab cannot see this)*

*Modules:* `DeviceEnumerator`, `ImuBleScanner`, `BleImuTransport`, `VideoInputFactory::enumerateDevices`.

- **Claim.** Every connected device is enumerated within a bounded time, with capabilities that *match what
  it actually delivers*.
- **Verify.** Deterministic: enumeration idempotent (duplicates suppressed); `VideoInput::start()` sets the
  enumerated default format so "claimed == delivered" by construction; `updateBufferDescriptor()`
  reconciles against the first delivered frame.
- **Validate.** A **discovery soak harness** (live hardware): completeness (target **100 %** over N
  power-cycles), discovery latency distribution, capability-vs-delivery exactness, reconnect recovery under
  induced disconnects (the BlueZ error+disconnected order-independence is the historical failure).
  *Sample size:* discovery completeness is a proportion — **≥ 35 power-cycles per device** to bound it to
  ±10 % (§3.1); a single failure in 35 is a red flag worth chasing.
- **Tune.** `kConnectWatchdogMs`, `kMaxRetries`, `kRetryBaseDelayMs`, scan-window length — a
  reliability-engineering trade, not a scorecard sweep.
- **Accept.** 100 % discovery within window; capability fidelity exact; reconnect ≥ 99 %.

### 5.2 Streaming, capture & ingestion *(Corpus 1 — the linchpin)*

*Modules:* `EventBuffer`, `SourceRing`, `SwingWindow`, capture-provenance writer; the **Tier-0 capture
gate**.

*Zero `analysis.bindings[]` is fatal* — without the session A/M snapshot, SwingLab refuses to re-fuse
(re-fusing without it is fiction) and the entire wrist matrix is unrunnable.

- **Claim.** A swing dir is a faithful, replayable record: every sample at its original timestamp, formats
  correct, impact present, calibration bound.
- **Verify.** **A·q·M golden** bit-identical live (`imu_instance.cpp:213`) vs offline
  (`imu_vision_fuser.cpp:72`); **RAM-vs-disk SwingWindow parity** (the streaming `SwingPayloadSource` must
  reconstruct byte-identical); **merger semantics** (the `gen=odd` mid-write vs genuine-overrun
  distinction — a regression silently discards frames); **orientation re-fusion parity** — re-running the
  filter offline from the persisted raw accel+gyro (warm-started from the stored quat) must reproduce the
  stored live-fused quaternion under production parameters (§5.3.1). This last one is a **pre-collection
  gate for Corpus 1** — it proves the captured corpus is post-hoc-tunable (the data is persisted but the
  re-fusion path is not yet built).
- **Validate (Tier-0 pre-flight, *every* swing, all three corpora).** IMU present ∧ `bindings > 0` ∧
  `calibrated: true` ∧ Impact phase (5) present; sample-rate fidelity (measured inter-arrival vs nominal
  `outputRateHz`; at 45 m/s, 1 ms = 45 mm — rate fidelity is a metric-accuracy precondition); timestamp
  monotonicity; `calibAgeSec` small. *Sample size:* this is a **census, not a sample** — 100 % of every
  corpus must pass; it is a gate, not an estimate.
- **Tune.** None — a capture acceptance gate. The proposed `lab.py` pre-flight validator (red/green per
  swing, run at the studio) is the highest-leverage tool in the plan.
- **Accept/Refine.** A swing failing a *must* gate is **recaptured, not tuned around** — a *data failure*,
  the triage class most important to get right.

### 5.3 Filtering — IMU fusion, calibration, temporal cleanup *(Corpus 1; refined by Corpus 2 via wrist)*

*Modules:* `MadgwickFilter`/`EskfOrientationFilter`, `ImuBase::fuseRawImu`, `imu_calibration.h`,
`ImuVisionFuser` (anatomical `A·q·M` + Kabsch yaw), `TrackSmoother` (KF + RTS).

- **Claim.** The fused quaternion is a stable, gravity-referenced world orientation; `q_anat = A·q_raw·M`
  maps sensor-body → segment-anatomical; smoothing removes noise without lag at impact.
- **Verify.** Unit-norm (1 ± 1e-4); sign-continuity before differentiation; no gimbal snap
  (`gimbalDropCount` flat); on synth the fused quat projects *exactly* onto the rendered shaft angle and is
  FD-consistent (synth `imuVisionCorr 0.986`).
- **Validate.** Seed convergence < 500 ms (before Takeaway; knobs `kInitAccelTolG=0.15`,
  `kInitGyroMaxRadps=0.5`, `kInitMaxSeedAttempts=200`); **cross-modal `imu_vision_corr ≥ 0.9`** on
  bound+calibrated swings (independent witnesses of the same angular motion — validates both without
  labels); mount gates (`axisAngleDeg ∈ [60°,120°]`; composite `calibrated` = anat valid ∧ mount ≤ 15° ∧
  gravity ≤ 25°); **Madgwick (`m_beta=0.05`) vs ESKF** A/B (corr + jitter, model-selection on the tuning
  partition, confirmed on held-out). *Sample size:* corr-pass is a proportion → **50 swings → ±~14 %** on
  "≥ 90 % of bound swings pass" (Corpus 1's 50 is adequate for the gate, not a tight estimate).
- **Tune.** `m_beta`, ESKF noise, seed tolerances, RTS **jerk PSD per joint** (high hands, low torso),
  Kabsch still-window length — **and the phase-adaptive gain schedule + impact handling of §5.3.1, which is
  the substantive orientation-filter tuning work.**
- **Accept/Refine.** corr ≥ 0.9 on ≥ 90 % bound; converged before Takeaway; all calibrated. **Corpus-2
  refinement:** a *constant* per-axis wrist bias vs HackMotion localises *here* — an `A·M` alignment error
  or a filter sign — versus random LoA (noise). HackMotion is the tool that separates "noisy filter" from
  "mis-aligned frame".

#### 5.3.1 Orientation-filter fine-tuning — phase-adaptive gain & impact handling

This is the deepest tuning problem in the IMU path, because a golf swing **violates the core assumption of
every gravity-aided orientation filter** — that the accelerometer measures gravity. It does, at address and
finish; it does **not** during the downswing or at impact. The filter must be made to *know which phase it
is in* and weight the accelerometer accordingly.

**The current state (what we are refining from).** `MadgwickFilter::update()` applies the accelerometer
gravity-correction (gradient-descent step, gain `m_beta = 0.05`) **unconditionally** on every sample where
`|a| > 1e-6` — there is *no* dependence on how far `|a|` is from 1 g, no impact rejection, and no
saturation handling. The `EskfOrientationFilter` likewise runs `predict → correctGyr → correctAcc` every
sample with fixed measurement noise. Only the *seed* is dynamics-aware (stillness-gated:
`|‖a‖ − 1g| ≤ kInitAccelTolG ∧ ‖g‖ ≤ kInitGyroMaxRadps`). So today the running filter trusts a 12 g
downswing accelerometer reading — almost all *linear* acceleration, not gravity — as if it were a gravity
correction. Fine-tuning = replacing the fixed gain with a **dynamics-/phase-adaptive** gain and adding
explicit impact handling.

**Two distinct high-acceleration regimes, handled differently.**

1. **Sustained swing acceleration (continuous).** Through the backswing→downswing the wrist IMU sees large
   *linear* acceleration — centripetal `ω²r` + tangential `αr` — that can reach many g and points in a
   direction unrelated to gravity. This is a *smooth, sustained* corruption of the gravity reference. It is
   handled by **continuously down-weighting the accelerometer as the dynamics rise**: scale the Madgwick
   `β` (or inflate the ESKF accelerometer measurement covariance `R`) as a function of the
   **accel-magnitude residual** `e = |‖a‖ − 1g|` and the **gyro magnitude** `‖ω‖`. When `e` is small and
   `‖ω‖` is low (address/finish) → full trust (correct accumulated drift fast). When `e` is large or `‖ω‖`
   is high (downswing) → `β → ~0` / `R → ∞`, i.e. **gyro-only integration**. This is the standard adaptive-
   gain / innovation-gated formulation; it is *continuous*, not a hard switch.

2. **Impact shock (transient + saturating).** At impact the clubhead decelerates violently and a shock
   propagates through the shaft and hands. The wrist accelerometer sees a sharp spike **plus ringing**, and
   — critically — it **saturates at the ±16 g full scale** (confirmed in `impact_detector.h`: confidence is
   "never from peak g, which clips at the ±16 g full scale"). A saturated, ringing vector carries **no**
   usable gravity information, and it lands on the single most coaching-critical instant ("wrist at
   impact"). This is handled by a **discrete impact-blanking window**: because the analyzer runs *offline
   with the impact instant already known* (`job.impactUs`, the back-dated marker), the accelerometer
   correction is **frozen** over `[impactUs − preMs, impactUs + postMs]` and orientation is propagated by
   **gyro integration alone** across it, resuming accel correction only once `‖a‖` has re-settled toward
   1 g. A hard **saturation gate** (`|aₖ| ≥ accelSatG ≈ 16 g` on any axis → reject the accel update for
   that sample) backs this up everywhere, independent of the window.

**The per-phase gain schedule (what to tune, and against what objective).** The `PhaseSegmenter` already
labels phases, so the schedule is expressed and validated *per phase*:

| Phase | Dynamics | Accel trust (β / R) | Per-phase tuning objective |
|---|---|---|---|
| **Address** | still, `‖a‖≈1g` | **high** (fast convergence + drift-lock; this is also where the seed fires) | converge before Takeaway; low jitter; gravity-locked |
| **Takeaway → Backswing** | rising | **declining** as `e`,`‖ω‖` grow | per-phase `imu_vision_corr` high; no accel pull-in as speed builds |
| **Top / Transition** | brief low-speed reversal; `‖a‖` dips | **brief re-anchor** allowed | stable "wrist at top" reading (the scored phase) |
| **Downswing / Delivery** | peak `‖ω‖`, peak linear `‖a‖` | **minimum** (gyro-dominant) | peak `imu_vision_corr`; no orientation glitch entering impact |
| **Impact** | saturating shock + ringing | **off** (blanking window + saturation gate) | **orientation continuous across impact**; post-impact pose matches the gyro prediction (and, Corpus 2, HackMotion) |
| **Follow-through → Finish** | decaying → still | **ramps back up** as `‖a‖` re-settles | gravity-up at finish; **bounded net drift** over the swing |

**Proposed tuning knobs (dotted-key; a small C++ change = an escalation — today only fixed `m_beta`
exists):** `filter.betaStatic`, `filter.betaDynamic`, `filter.accelErrGateG` (the `e` threshold at which
trust starts collapsing), `filter.gyroGateDps` (the `‖ω‖` threshold), `filter.impactBlankPreMs`,
`filter.impactBlankPostMs`, `filter.accelSatG` (= 16). Sweep them with `lab.py sweep` against the per-phase
objectives below; **diff-gated** like every other parameter.

**Offline re-fusion is the load-bearing capability — and it is now IMPLEMENTED (C3, behind a flag).**
Originally (2026-06-26) the orientation filter ran only on the **live** I/O thread
(`ImuBase::fuseRawImu`) and the analyzer read the **stored** quaternion. As of the tunable-parameter
work, `ImuVisionFuser::fuse()` takes an optional `RefuseConfig*`: when `filter.refuse` is set, it
re-derives each source's orientation offline from the persisted raw accel+gyro
(`refuseOrientationAdaptive`, warm-started from the stored quat at the window's first sample) under the
phase-adaptive schedule, and feeds *that* as `qRaw` into the `A·qRaw·M` transform — so the schedule
**moves the wrist metric**. Off by default ⇒ the stored quaternion (production, byte-identical). The
schedule is therefore tunable on real data: `filter.*` is observable via `xmodal.imu_vision_corr`
(vision), `diag.*` (labelled), and the provisional `filter.impact_continuity` check. *Open tail:* the
ESKF `R` covariance is **not** exposed (the vendored `third_party/imu_ekf` is kept verbatim and ESKF
cannot warm-start, so it stays outside the re-fusion loop — a separate escalation only if ESKF becomes
the production filter), and the `impact_continuity` threshold + per-phase/net-drift checks await a real
impact-shock swing to calibrate (the synthetic corpus models no impact saturation).

The good news, also verified: **the data needed to re-fuse is already persisted.** `ImuSample`
(`imu_sample_v2`, 40 B) stores raw accel (g) + raw gyro (°/s) in the sensor frame, the exporter writes
those plus the fused quat **verbatim with per-sample `t_us`** and the per-device `alignA`/`mountM`
snapshot (`swing_exporter.cpp`). So **collection is not blocked**: a Corpus-1 swing captured on the current
build is re-fusable, and the re-fusion/tuning harness can be built *after* collection and applied
retroactively. **Two fidelity details the harness must match exactly** (verified in `ImuBase::fuseRawImu`):
the live filter integrates with the **nominal period `dt = 1/outputRateHz`** (recorded in
`device.outputRateHz`), *not* per-sample timestamp deltas — BLE delivers bursts, so arrival deltas alias
to ~0 within a burst and a gap across it; and the filter takes **gyro in rad/s** while `ImuSample` stores
**°/s**, so re-fusion converts °/s → rad/s (accel stays in g). Get either wrong and parity fails for
reasons unrelated to the parameters under test.

**Two subtleties the re-fusion harness must handle (do NOT skip):**
1. **Warm-start from captured state.** The live quaternion at the *window start* already encodes
   pre-window history — seed convergence, accumulated yaw drift, and (ESKF) a converged gyro-bias — and
   the 5 s *trailing* capture window may not contain the original seed instant. Re-fusing from the first
   window sample re-seeds from scratch ⇒ divergence (worst in yaw). **Warm-start the offline filter from
   the stored quaternion at the first window sample, then re-run with the candidate parameters across the
   window**, so re-fusion faithfully tests behaviour through backswing→impact→finish from a captured-state
   anchor. (ESKF gyro-bias is not stored ⇒ it re-converges within the window — acceptable for within-window
   tuning, but noted.)
2. **Parity is the verification gate.** Before any tuning, prove `refuse(stored raw, production params,
   warm-started) ≈ stored live quat` on pilot swings — the orientation-filter analogue of the A·q·M golden
   (§5.2). Parity confirms the captured data is genuinely sufficient; a parity *failure* is a
   capture-schema gap you want to find on one pilot swing, not after 50.

**Pre-collection action — DONE.** The minimal **offline re-fusion + parity tool** is built: header-only
`src/IMU/orientation_refuser.h` (`refuseOrientation`/`parity`, warm-started from the stored quat at the
window's first sample) + `IOrientationFilter::setOrientation`; exposed on real swings as
`swinglab_run <swing> --out <run> --refuse-orientation` (writes `refusion.json`; `--refuse-beta` perturbs
the gain to confirm sensitivity), and proven exact on synthetic data by the standalone
`orientation_refuse_test` (warm-start reproduces the stored trajectory to ~2e-6°; a β change diverges
50°). Run it on ~3 pilot swings as the corpus-1 E1 gate (`corpus1_collection_protocol.md` §0a). The
**full** tuning harness (filter-param `tuningOverrides` + `lab.py sweep` over the schedule) follows
collection.

**Per-phase validation (how the corpora decide it).**
- *Verification (synth):* the closed-form synth swing has a known `φ(t)` with a waggle burst and a
  downswing `u²` profile; re-fusing from synth raw accel+gyro must reproduce truth through the high-`‖ω‖`
  region — the controlled test that the adaptive schedule is implemented correctly before real data.
- *Corpus 1 (self-validating, no labels):* stratify `imu_vision_corr` **per phase** (a new Tier-2 check
  group), add an **impact-continuity invariant** (no orientation discontinuity > threshold across
  `impactUs` beyond what gyro propagation predicts), and a **net-drift-over-swing** check (orientation at
  the still finish vs gravity-up — bounds the drift accumulated during gyro-only spans). The escalation
  trigger is a *per-phase* corr failing, which a whole-swing average hides.
- *Corpus 2 (criterion, HackMotion):* the decisive test that **impact handling did not corrupt the angle
  where it matters** — does the lead-wrist FE/RUD/PS *just after impact* agree with HackMotion within the
  Corpus-2 LoA? A schedule that scores well on continuity but disagrees with HackMotion at impact has
  blanked too aggressively (drifted through the window) or too little (let the shock in).
- *Corpus 3 (generalisation):* peak `‖a‖`/`‖ω‖` scale with swing speed, so a schedule tuned on one golfer
  may not fit a faster/slower one. Corpus 3 tests whether **one fixed schedule generalises**, or whether
  the gates must **scale with measured per-swing dynamics** (e.g. normalise `accelErrGateG` by the swing's
  own peak `‖ω‖`) — the latter is the more robust design if the cohort shows it.

**Madgwick vs ESKF, decided *per phase*.** The A/B is not a single winner: ESKF can inflate `R` and carries
explicit **gyro-bias** states (better through the long gyro-only impact window and at re-anchor), while
Madgwick's single `β` is cruder but cheaper and is what ships live. The per-phase scorecard may favour ESKF
for the impact/downswing window and either for the quasi-static phases; record the decision per phase.

**Honest caveats.**
- The ±16 g clip means the impact spike's magnitude is **unrecoverable** from this sensor — we *exclude* it,
  never try to use it; a higher-range accelerometer is the only way to *measure* through impact (future HW).
- Gyro-only propagation across the blanking window accumulates drift; the window width is a genuine
  trade-off (too short → saturated/ringing samples leak into the correction; too long → excess drift). The
  follow-through re-anchor bounds it, but the residual is real — keep the window minimal.
- Gravity aids **pitch/roll only**; **yaw** is unobservable in this 6-axis (no-mag) filter, so the adaptive
  accel weighting does nothing for yaw. Per-shot yaw is handled by the Kabsch re-alignment in
  `ImuVisionFuser`; within-swing yaw drift across the gyro-only impact window is a residual bounded only by
  the finish re-anchor — note it, and prefer the shortest viable blanking window.
- This is the **offline analyzer** path (impact known). The **live** 60 Hz display path cannot blank on a
  future impact, so it uses only the *continuous* dynamics-adaptive gating (the `e`/`‖ω‖` gate + saturation
  reject), not the impact-anchored window. The wrist *metric* is taken from the offline re-fusion, so the
  blanking applies where it counts.

### 5.4 Pose detection *(Corpus 1 track/elbow; Corpus 3 camera-metric)*

*Modules:* `PoseRunner` (ViTPose), `Triangulator`, `MonoLift`, `ShaftTracker` detect stage.

For Wrist, the camera is *complementary, never authoritative* for wrist axes (out-of-plane/axial,
unobservable from one 2D view). So pose validation is the **shaft track** + the **elbow cross-check**; the
metric-scale camera branch (sway/lift/tilt) is **Corpus 3** (it needs ChArUco calibration).

- **Verify.** Determinism per EP (ViTPose CUDA vs CPU differs — same-EP diffs only); synth injects
  `pose.json`.
- **Validate.** *Track sanity (Tier 1, all frames, no labels)* — `club.valid`, `club.coverage ≥ 0.6`,
  `track.theta_step < 25°/frame`, `track.downswing_sweep ∈ [86°, 458°]`, `track.peak_rate_near_impact`
  ≤ 120 ms, `track.head_step`, `track.len_step`. *Absolute (Tier 3, labelled)* —
  `truth.theta_rms_deg < 3°`, `truth.head_median_px < 25 px` (§3.4: **10–15 labelled swings**). *Elbow
  cross-check* — `|elbow2D − leadArmFlexion| < tol`.
- **Tune.** **`shaft.*`** (~8 sweep-worthy: `ridgeKernelPx`, `noiseSigmaK`, `thresholdFloor`,
  `nmsSeparationDeg`, `clutterMaskDeg`, `minScoreFrac`, `runMaxGapPx`, `interHandSigmaDeg`); the
  skeleton-aware **flag-flips** (default OFF, byte-identical): `useArmScale` (K1), `minLenFracOfArm` (R5),
  `useKinematicPrior` (K2), `useEnvelope` (R6), `useBlurMode` (R8), `emitPredicted` (R7).
- **Accept.** coverage ≥ 0.6 on ≥ 90 % clean face-on; θ-RMSE < 3° / head < 25 px on labelled; each K-flag
  decided. **Pixel-level caveat:** sub-pixel θ conclusions are *advisory* on MP4 (`frames: mp4`); they need
  the ~12-swing **raw subset**.

### 5.5 Metric extraction *(Corpus 1 segmentation/shaft; Corpus 2 wrist criterion)*

- **Segmentation** (`seg.*`): `seg.monotone` 100 % (Tier 1); `seg.tempo_ratio ∈ [1.2,6.0]`; **Top timing**
  `truth.event_top_s ≤ 0.03 s` (load-bearing — every wrist metric is sampled at a phase). Tune ~14 `seg.*`
  (`fcEnvelopeHz`, `top*BeforeImpactUs`, `transBeforeTopUs`, `takeawayFracOfPeak`, `voteAgreeUs`,
  `finish*Us`, stillness gates). *Sample size:* Top-error is an RMSE vs labels → **10–15 labelled swings**.
- **Shaft assembly** (`assembly.*`): `club.coverage ≥ 0.6`; ŝ_hand fit trace (`ok/sign/residualRad`,
  residual < `calibAcceptRad ≈ 7°`). Tune `coverageMin`, `jerkPsd`, `transSigma*`, `visionSigmaFloorRad`.
- **Clubhead & projected club-length model** (stage-2 vision; exemplar `tools/shaftlab/`:
  `clubhead_annotate.py` + `length_model.py`, design [clubhead_detection_design.md](../design/clubhead_detection_design.md)):
  estimand = the per-swing projected grip→head length curve `L(θ, phase)` (ρ = L/L_address) + per-frame
  head position; it drives crop/off-frame detection and the head-detection confidence interval. The
  production path is a **per-swing self-fit** (censored quantile on the swing's own radial measurements),
  so cross-swing generalisation is needed only for the model *form*, CI calibration, and club→plane
  priors. **Model-form selection is corpus-gated:** the candidates (M0 constant / M1 single plane /
  M2 per-phase plane / M3 cone / M4 empirical kernel) were frozen at H0 against swing_0008 + synthetic;
  selection requires held-out **swings and held-out clubs** (plane geometry varies with club length, and
  any single labelled swing may itself be off-plane — single-swing residuals are development signals
  only). Instruments: head/len hand labels (truth.json `shaft[].head/len` already carries them; ~10–15
  frames/swing spanning P1–P10, ≥3 clubs), synthetic ρ-profile recovery (`make_synth.py` — the machinery
  gate, passed at H0: self-fit recovers a known profile to ~6 % median). Verify: `score_truth.py --head`
  (head px error, length error, conf honesty, split by `kind_h`). H0 baselines + known limits:
  `shaft_markup_lab/{s8v2/h0/BASELINE.md, H0_CORPUS.md}`.
- **Wrist angles** — the product's core. `leadWristFlexExt`, `leadWristRadUln`, `forearmPronation`,
  `leadArmFlexion`, neutral-relative, signed. **Corpus 1:** plausibility (Ryu ROM ≤ 54° flex / 60° ext /
  40° ulnar / 17° radial), phase-relation (Impact 15–30° more flexed than Address), **repeatability**
  (between-swing RMSE **< 5° FE / < 8° RUD**, §3.3 → **≥ 25–30 repeated swings**), gimbal handling
  (< 2 % Indeterminate, `gimbalThresholdDeg=75°`). **Corpus 2:** the HackMotion criterion programme (§6).
  Tune `PpWristSamplingConfig` (`windowHalfUs=15 ms`, `gimbalThresholdDeg=75`, `minValidSamples=1`) + the
  *physical signs* (provisional; locked in Corpus 2) — not yet dotted-key exposed (a small C++ escalation).
- **Kinematic sequence** (body, IMU): not scored for Wrist, but its **proximal-to-distal ordering** is a
  free construct-validity check on fusion + segmentation (no parameter is tuned to it).

### 5.6 Diagnosis — scoring, faults, assessment engine *(Corpus 1 known-groups; Corpus 2 bands; Corpus 3 coach)*

*Modules:* `WristAssessmentEngine` (Tier-1 bands + Tier-2 rules F1–F8 + strengths + archetype), score v2.

- **Verify.** Golden-tested against synthetic fixtures (`makeCast`/`makeFlip`/`makeOpenFaceTop`/…): each
  scripted fault produces its finding; score v2 contributions sum to the score; banding monotone.
- **Validate.**
  - **Known-groups (Corpus 1).** Scripted-fault capture: the engine must flag the matching fault and
    **not** flag it on the clean control. Report recall + FP rate **per rule** — *with the §3.1 honesty*:
    **~10–15 scripted swings/fault** gives a point estimate + wide CI ("the rule fires and is specific"),
    **not** a recall ≥ 0.95 guarantee (that needs ~60/fault, deferred to a pooled fault library).
  - **Band calibration (Corpus 2).** Wrist corridors are *provisional*. Anchor on (1) the **observed corpus distribution** and (2) **HackMotion's published tour ranges** (top −30/+5, impact −15/−40, extension-positive) — external content-validity for where corridors sit.
  - **Score internal consistency.** Monotone w.r.t. metric quality; the score is computed additively by deducting penalties for faults and watches from a baseline of 100.
  - **Inter-rater (Corpus 3+).** Coach annotations (§8): **Cohen's κ** (fault presence), **ICC** (score).
    Today the diagnosis has **no external arbiter** — the biggest validity gap. *Sample size:* κ needs
    enough swings *with the fault present*; ~30–50 annotated swings for a usable κ CI, more golfers for
    generalisation.
- **Tune.** `RuleTuning` (`confidenceFloor 0.45`, `scoreScale 18`, Fault 1.0 / Watch 0.5, `corroborationBoost 0.30`,
  `strengthsRequireAdjacentFault`); archetype shifts (bowed/cupped ±10°).
- **Accept.** scripted-fault recall point-estimate high + ≈ 0 FP on clean; bands consistent with observed +
  HackMotion ranges; score monotone; (Corpus 3) κ/ICC vs coach.

---

## 6. The HackMotion criterion-validation programme (wrist) — Corpus 2

> **This overturns a prior decision.** `corpus_v1_validation_plan.md` §5/§10 recorded *"External
> reference: ✅ Decided: none."* HackMotion makes a **concurrent criterion reference** available for the
> three wrist axes — but as a **Corpus-2** capture, not part of Corpus 1. Corpus 1 uses HackMotion only as
> a **pilot** that de-risks and *sizes* Corpus 2 (§3.2).

### 6.1 What HackMotion is — and is not

A **single IMU on the lead glove** reporting the same three lead-wrist axes PinPoint derives (bow/cup,
hinge, roll) plus continuous traces and address/top/impact readings. Concurrent-validity literature: **ICC
0.95–0.99 vs goniometry (~1° FE)**, **RUD the weakest axis (~5°)**. Critically:

- **Criterion, not gold standard** — agreement is bounded by *its own* LoA; we never demonstrate accuracy
  finer than the reference's noise floor.
- **Single-sensor** — it *infers* forearm rotation from one glove sensor; PinPoint *measures* it with a
  dedicated forearm + hand pair. On **pronation/supination** the two can legitimately disagree (a
  *content* difference, not necessarily a PinPoint error).
- **Opposite sign** (extension-positive) and **its own address zero** — both reconciled before any
  statistic (§6.3).

### 6.2 Concurrent-capture protocol *(Corpus 2; piloted in Corpus 1)*

1. **Co-mount** the PinPoint hand (🟡) + forearm (🔴) IMUs *and* the HackMotion glove sensor; verify
   neither perturbs the other (re-run PinPoint's "confirm tracking" after donning both).
2. **Synchronised capture** over the *same* shots; both devices timestamp on their own clock → §6.3
   temporal alignment mandatory.
3. **Stratify** as Corpus 1: repeatability (≥ 25, gives *both* devices' SEM), diversity, and **scripted
   wrist faults** (cup/bow/cast/flip — these widen the agreement range so LoA isn't measured only near
   neutral, *and* double as known-groups).
4. **Per-axis static holds** (cup/bow/ulnar/radial/pronate/supinate) — an absolute per-axis anchor
   *independent of swing dynamics* that isolates sign + frame alignment.
5. **Size from the Corpus-1 pilot's measured `s`** (§3.2): target **~50 paired swings**, adjusted per-axis
   once `s` is known (FE may need fewer, RUD more — captured honestly).

### 6.3 Three reconciliations *before* any statistic

Computing RMSE on raw values conflates three solvable systematic offsets with random error. Resolve each,
and **log the resolution as data** (a constant offset is fine; a *per-swing-varying* one is a finding):

| Problem | Reconciliation | Residual after = |
|---|---|---|
| **Sign** | HackMotion extension-positive → ×(−1) to PinPoint flexion-positive (FE); RUD/PS per `wrist_assessment.md` §7 | a remaining sign disagreement = a real PinPoint sign bug (the thing to lock) |
| **Zero / neutral** | both report **Δ-from-address** → compare deltas, never absolutes | a constant Δ-offset = anatomical `A·M` mis-alignment (§5.3 target) |
| **Temporal** | cross-correlate the two FE traces to sub-frame lag, or anchor on impact | the residual lag *is itself a metric* — large/variable lag flags sync/sampling |

### 6.4 Ingestion into SwingLab (additive, lab-private)

- A **`hackmotion.json`** sidecar inside the swing dir (joining `truth.json` and `pose.json` — the only
  lab-private files). Per-axis trace `{axis, t_us[], deg[]}` (reconciled, or raw + the reconcile
  transform) + the device's address/top/impact readings.
- `Swing.hackmotion()` accessor; `lab.py ingest` surfaces a `hackmotion: true|false` flag.
- A **Tier-3-Ext** check group in `score.py`: `hm.fe_rms_deg`, `hm.rud_rms_deg`, `hm.ps_rms_deg` (per
  phase), `hm.fe_icc`, `hm.fe_bias_deg`, `hm.fe_loa_deg`, `hm.traj_corr`, `hm.traj_lag_ms`. **New checks
  shift the 100-point normalisation — re-baseline before diffing.**
- Synth stamps a plausible `hackmotion.json` (the lab's "every reader has a synth path" discipline).

### 6.5 Statistical analysis plan (per axis × per phase × trajectory)

For each axis ∈ {FE, RUD, PS} × phase ∈ {Address, Top, Impact} and the full trajectory:

- **Agreement (core).** **Bland–Altman** bias + 95 % LoA (`bias ± 1.96·SD_diff`); difference-vs-mean plot
  for *proportional* bias; **ICC(A,1)** (two-way, absolute agreement — not the looser consistency form);
  RMSE; max error. **Never report correlation alone.** *N = ~50 paired (§3.2).*
- **Trajectory.** Resample to a common impact-anchored grid; Pearson r (shape), per-sample RMSE
  (magnitude), DTW distance + warp-path inspection (a large warp = residual temporal misalignment, a §6.3
  problem, not a metric error).
- **Reliability denominator.** test–retest **ICC, SEM, MDC** for *both* devices on the repeatability
  sub-stratum (≥ 25, §3.3). Essential and routinely omitted: **agreement can never be tighter than the
  reference's own repeatability** — HackMotion's SEM is the *floor* on achievable LoA.
- **Pre-registered acceptance** (write before looking, §7): **FE/PS** RMSE ≤ 4–5°, ICC ≥ 0.90, bias ±2°;
  **RUD** RMSE ≤ 8° *reported honestly as the weak axis* (≈ 5° is the criterion's own floor — we target
  agreement *consistent with that floor*, not heroic tightness); **trajectory** r ≥ 0.95, residual lag
  < 1 frame.
- **Multiple-comparisons hygiene.** 3 axes × 3 phases × {agreement, trajectory} is a *family* — report the
  full table (no cherry-picking the axis that passed); don't celebrate one significant correlation among
  twenty.

### 6.6 What HackMotion *refines* (distinct from what it validates)

Via the tuning/escalation discipline (§7), never by silently fitting to the criterion:

1. **Lock the signs** — independent physical-sign witness, *together with* the §6.2 static holds (so we
   don't lock a sign to a possibly mis-zeroed HackMotion).
2. **Diagnose `A·M` alignment** — constant per-axis bias ⇒ mount/anatomical error; **cross-axis leakage**
   (PinPoint FE moving on pure-RUD HackMotion) ⇒ frame mis-alignment in `A`/`M`.
3. **Re-seat `kWristBands` μ/σ** on a physically-anchored, HackMotion-cross-validated scale (a C++
   escalation, gated on distribution + agreement).
4. **Validate gimbal/Indeterminate** — where one device reports and the other says `Indeterminate`
   localises whether `gimbalThresholdDeg=75°` is too aggressive/lax.
5. **Upgrade the accuracy claim** — convert "HackMotion-grade (aspirational)" into a *measured* concurrent
   validity statement with LoA.

### 6.7 Methodological cautions

- **Not ground truth** — its ~1°/~5° error is in every comparison; target *agreement within the
  criterion's LoA*, not zero difference.
- **Single-sensor vs two-sensor PS** — disagreement may be a genuine construct difference; interpret,
  don't auto-"fix".
- **Pseudo-replication and Clustered Data (Pseudo-Replication Caution):** The 50 swings in Corpus 2 are from a single golfer. Swings from a single subject are highly clustered and are *not* independent observations for population generalisation. Therefore, the Bland-Altman Limits of Agreement (LoA) measured here are strictly **within-subject agreement limits**. They under-estimate the true population-level LoA because they omit between-subject anatomical and sensor-mounting variations. Any population-level validation or generalizability claims are strictly deferred to Corpus 3, requiring a minimum of **8–12 different golfers** (§3.5).
- **Correlation ≠ agreement; regression-to-criterion trap** — always Bland–Altman; never tune a sign or offset to chase the criterion without the static-pose anchor.

---

## 7. Overfitting & generalisation controls (corpus governance)

Tuning ~14 `seg.*` + ~8 `shaft.*` + ~12 `assembly.*` knobs against 50 swings is a high-dimensional fit to
a small sample. Controls, in force for every sweep, **on every corpus**:

- **Partition each corpus** into three disjoint roles, fixed at blessing and recorded in `CORPUS.md`:
  **Tune (dev)** — sweeps run here; **Validation** — threshold/flag-flip decisions confirmed here;
  **Held-out test** — **touched once, at freeze.** A knob improving Tune but regressing Held-out is
  overfit and rejected. (Partitioning *within* a corpus is the within-subject overfit guard; *across*
  corpora, Corpus 3's new golfers are the between-subject guard.)
- **Nested cross-validation for sweeps** — param-selection inner loop *inside* the evaluation outer loop,
  so the reported score isn't the (optimistically biased) selection score.
- **The diff gate is law** — land only if mean ↑ **and** `regressions: 0` per swing (§3.6). Prefer `sweep`
  over > 3 hand-iterations.
- **Pre-register hypotheses and thresholds** — the §5/§6 acceptance numbers are written *before* the runs.
  Moving a threshold is a documented decision, not a silent edit.
- **Re-baseline on check changes; same-host only; bless before conclude; no silent caps.**

### 7.1 Parameter-optimisation method — how the sweep actually searches

The governance above says *what must hold*; this says *how to search the space without fooling yourself*.
The harness (`lab.py sweep`) injects dotted-key candidates and scores each trial on the corpus; the
question is which candidates to try, on which partition, and when to stop.

**Algorithm — escalate, don't over-engineer.** Match the optimiser to the surface:

1. **Coordinate descent (default).** One knob at a time, accept on the §7 diff gate, move on; revisit in a
   second pass. Interpretable, diff-friendly, no dependencies, and every accepted step is a documented
   single-variable decision. The right tool for the `seg.*`/`shaft.*`/`assembly.*` knobs, which
   are largely separable and where a coach needs to understand *why* a value moved.
   (`score.*`/`rules.*`/`bands.*` are **not** in this surface — they are frozen until labels exist; see the
   freeze note below.)
2. **Bayesian optimisation (GP/TPE) — on plateau.** When coordinate descent plateaus (the existing
   ESCALATION trigger "sweep plateau ×2") and interactions are suspected, a sample-efficient global
   optimiser earns its dependency: ~14 `seg.*` + ~8 `shaft.*` against ~50 swings is exactly the
   *expensive-evaluation, modest-dimension* regime TPE/GP target. Keep uniform random as the reproducible
   baseline to beat.
3. **CMA-ES — for the filter schedule.** The §5.3.1 phase-adaptive filter knobs
   (`filter.betaStatic/betaDynamic/accelErrGateG/gyroGateDps/impactBlank*`) are continuous, non-separable,
   and gradient-free — CMA-ES is the standard fit. Reserve it for that surface; it is overkill for the
   separable corridor knobs.

**Freeze the diagnosis/score layer until labels exist (D1).** `score.*`, `rules.*` and `bands.*` are
**frozen, not swept** — calibrating them by maximising corpus self-consistency is circular (optimising the
score to agree with the angles it is computed from). They split into two genuinely different problems,
*neither a corpus-score sweep*: (1) **measurement-model fitting** — the resemblance `μ_p`/`σ_p` and corridor
lo/hi are a **distributional goodness-of-fit** against external tour ranges + the multi-golfer corpus
distribution (Corpus 2/3), never diff-gated on corpus score; (2) **fault-rule calibration** — F1–F8
recall/specificity is a **supervised classification** problem (confusion-matrix / F-β loss) needing labels,
with its decision thresholds (the now-exposed `rules.flipFaultDeg`, `rules.archetypeTopDeltaDeg`, … — §A.5
#15) in the swept surface *only once labels exist*. Until then the discrimination keys are exposed-but-frozen
and the harness refuses to sweep them (SwingLab `--allow-frozen` is reserved for the post-label pass). The
swept surface in the meantime is `seg.*`/`shaft.*`/`assembly.*`/`filter.*` only.

**Partition the search, not just the report.** Tie the optimiser loop directly to the `CORPUS.md`
Tune/Validation/Held-out roles (§7): **sweep on Tune**, **select the winner on Validation** (so the reported
score is not the optimistic selection score — nested CV in practice), **touch Held-out exactly once at
freeze**. A knob that wins on Tune but regresses Validation is overfit and rejected before it ever sees
Held-out. The harness enforces this — Held-out is refused without an explicit `--freeze`.

**Regression-aware objective (soft in search, hard at freeze).** The objective balances **mean corpus
score** against **per-swing regressions** (the 5-pt diff vs the baseline run, §3.6). During the *search*,
regressions enter as a **soft penalty proportional to the regression count** — a candidate that lifts the
mean and 49/50 swings must not be blocked by a single regressor, and coordinate descent over the partly
*non-separable* `seg.*` knobs must not deadlock or turn order-dependent on a per-step hard reject (D2). The
**hard `regressions == 0` gate is applied once, at the accept/freeze decision** (§3.6): a tuned default is
merged only if it lifts the mean *and* regresses no swing. This keeps the per-swing guarantee at the
boundary that actually ships, without letting it distort the search. (Earlier drafts implemented the gate as
a hard reject *inside* the sweep loop — that is the behaviour D2 corrects.)

**Convergence / early stopping.** Stop on (a) **no Validation improvement over N consecutive trials**
(plateau — also the escalation trigger), (b) Bayesian acquisition-value below a floor, or (c) a trial /
wall-clock **budget**. Never run a fixed trial count and report the best — log what was explored and why it
stopped (no silent caps, §4).

---

## 8. Proposed new markup & instruments (future, mostly Corpus 3+)

Each buys a rung the current instruments cannot reach:

1. **Coach annotation → diagnosis validity (highest value, Corpus 3).** Coach tags faults + 0–100 quality
   → **κ** (fault agreement), **ICC** (score) — the diagnosis layer's *only* external arbiter (today
   verified against *synthetic* faults only). Markup: a `coach.json` sidecar + a Markup-Lab fault/score
   panel. *N:* ~30–50 annotated swings for a usable κ; multiple coaches for inter-rater.
2. **Launch-monitor outcome → predictive validity (Corpus 3+).** Capture carry/face/path per shot
   (`outcome.json`); tests whether a flagged open face predicts a measured push/slice — the top rung,
   entirely absent today.
3. **Impact ground-truth → shot-detection latency.** Hand-marked high-speed contact frame (or acoustic
   onset) → validates the 30 ms IMU / 20 ms audio back-dating to ± 1 frame; lets P4 auto-calibration be
   *checked*, not just run.
4. **Per-axis static-pose anchors** (§6.2.4) → isolates wrist sign/frame error from swing-dynamics error.
5. **Mount-perturbation set** → re-seat sensors between sets; measures the metric's *sensitivity to
   mounting* (a reliability the calibrate-once corpus otherwise hides).
6. **Trail-side + shoulder instrumentation** → unlocks deferred rules (full F8, F9–F11).
7. **Clubhead head/len labels → length-model form selection (Corpus 1+, cheap).** The shaft markup schema
   already carries `head`/`len` per labelled frame — extend every labelled swing to ~10–15 head points
   spanning P1–P10, across **≥ 3 clubs**. Feeds stage-2 model-form selection + CI calibration (§5.5;
   [clubhead_detection_design.md](../design/clubhead_detection_design.md) §8); scored by
   `score_truth.py --head`.

Implementation pattern (all): additive sidecar + `Swing.*()` accessor + `score.py` check group + synth
stamp + `ingest` flag — the lab's standard five-step reader contract.

---

## 9. Execution roadmap — three corpora

| Stage | Corpus | Goal | N | Exit gate |
|---|---|---|---|---|
| **C0** | 1 | Capture & bless (Wrist, IMU-only) | ~50 swings (25–30 repeat, 10–15 label, 10–15/fault) | Tier-0 100 %; `CORPUS.md` (+ tier/partition); ingest blessed |
| **C1** | 1 | Baseline run + triage | (all 50) | `REPORT.md` read; failures classified parametric/data/algorithmic |
| **C2** | 1 | Self-validation (Tier 1/2) + reliability | (all 50) | per-subsystem pass-rates; repeatability SEM/MDC; data-failures pruned (logged) |
| **C3** | 1 | Tier-3 labelling + known-groups | 10–15 labelled; scripted faults | `truth.json`; per-fault recall point-estimates |
| **H0** | 1→2 | **HackMotion pilot** (1 session) | ~10–20 paired | rig works; tooling round-trips; **`s` measured → Corpus-2 N confirmed** |
| **C4** | 1 | Tuning + flag-flips (diff-gated) | (Tune/Val partitions) | each `seg./shaft./assembly.` sweep + K-flag decided; 0 regressions |
| **C5** | 1 | Acceptance & freeze | (held-out, once) | §10 Corpus-1 gates met; defaults merged; scorecard re-baselined |
| **H1** | 2 | Concurrent capture + reconciliation | ~50 paired (sized by H0 `s`) | reconcile transform stable; logged per session |
| **H2** | 2 | Agreement analysis | (the 50 paired) | §6.5 table; **signs locked**; concurrent-validity statement |
| **H3** | 2 | HackMotion-driven refinement | — | `A·M` diagnosed; `kWristBands` re-seated; 0 regressions; re-baseline |
| **K0+** | 3 | + camera/IMU calibration, multi-golfer | ~8–12 golfers × several | external validity; camera-metric branch; (predictive if launch monitor) |

Run batch/sweeps on the Windows studio PC (RTX 5080, CUDA pose); the Linux box reaches the same data via
`/mnt/swingdata`. **Never diff across hosts.**

---

## 10. Acceptance criteria — by corpus

| Stage | Corpus 1 (IMU-only) | Corpus 2 (+ HackMotion) | Corpus 3 (+ calib, multi-golfer) |
|---|---|---|---|
| **Discovery** | 100 % over ≥ 35 cycles; reconnect ≥ 99 % | (regression) | (regression) |
| **Streaming/ingest** | Tier-0 100 %; golden + parity bit-identical | (regression) | (regression) |
| **Filtering** | `imu_vision_corr ≥ 0.9` *(verification/consistency — **not** validation, D3)* (≥ 90 % bound, ±14 %); converged < 500 ms | wrist bias localises `A·M` | calibration cross-checked |
| **Pose / shaft** | coverage ≥ 0.6 (≥ 90 % clean); θ-RMSE < 3° / head < 25 px (10–15 labelled) | (regression) | + metric-scale camera metrics |
| **Segmentation** | monotone 100 %; Top ≤ 30 ms; tempo ∈ [1.2,6.0] | (regression) | (regression) |
| **Wrist metrics** | repeatability < 5° FE / < 8° RUD (≥ 25–30 repeats); < 2 % Indeterminate | **FE/PS RMSE ≤ 4–5°, ICC ≥ 0.90; RUD ≤ 8°; traj r ≥ 0.95 (≥ 50 paired)** | + between-golfer LoA |
| **Diagnosis** | scripted-fault recall point-estimate high; ≈ 0 FP on clean | bands vs HackMotion ranges | κ/ICC vs coach; outcome predictivity |

> **Reliability ≠ accuracy (C4/D3).** The Corpus-1 Wrist row gates *repeatability*, not accuracy:
> reliability < X° does **not** bound accuracy — a ~10–15° systematic FE↔RUD cross-talk leak passes every
> Corpus-1 wrist gate cleanly while corrupting every absolute reading; it is only caught at Corpus 2
> (HackMotion bias/leakage, §6.3). Likewise `imu_vision_corr` (Filtering row) is a fusion-consistency /
> verification statistic, **not** anatomical validation: a wrong-but-consistent `A·M` makes IMU and vision
> agree on the *same wrong* angle. Neither Corpus-1 gate licenses an accuracy claim.

---

## 11. Risks & honest limits

- **HackMotion is a criterion, not gold standard** — agreement bounded by its ~1°/~5° LoA (§6.7).
- **Within-subject only until Corpus 3** — Corpus 1 & 2 are (largely) single-golfer; **no population claim
  before Corpus 3's multiple golfers** (§3.5). The effective N for generalisation is *golfers*, not swings.
- **Fault recall is point-estimate, not guaranteed** at realistic per-fault N (rule of three; §3.1) — a
  defensible recall ≥ 0.95 needs a pooled multi-corpus fault library.
- **PS construct mismatch** — single-sensor (HackMotion) vs forearm+hand (PinPoint) may legitimately
  differ; don't auto-tune to erase it.
- **MP4-only limits sub-pixel shaft conclusions** — valid for coverage/timing/correlation; pixel-θ needs
  the raw subset.
- **Only Wrist is real** — Swing/GRF/Coach are stubs; this framework (Tier-0→1→2→3→Ext, partitioning,
  the corpus ladder) transfers to them when they become real.
- **Diagnosis has no external arbiter until Corpus 3** — known-groups (scripted faults) is the current
  ceiling.
- **Provisional signs & bands** — locking signs and re-seating `kWristBands` are escalation-class C++
  changes; they ship through a reproduced test + 0-regression diff.

---

## 12. File & artifact map

```
docs/validation/pipeline_validation_and_tuning.md   # this document (methodological backbone, 3-corpus + N)
docs/implementation/corpus_v1_validation_plan.md     # the Corpus-1 operational C0–C5 runbook
docs/developer/swinglab_developer_guide.md           # the harness (runner + lab.py + scorecard)
docs/reference/wristmetrics.md                        # sign/norm/band decisions (provisional)
docs/implementation/wrist_assessment.md               # the diagnosis engine (Tier-1 bands + rules)
docs/design/shot_analyzer_design.md                   # the nine-layer pipeline + metric catalog

tools/swinglab/lab.py                                 # ingest / run / one / diff / sweep / label
tools/swinglab/swinglab/score.py                      # Tier 1–3 checks  (+ ADD: Tier-3-Ext HackMotion)
tools/swinglab/swinglab/synth.py                      # synthetic ground truth (+ ADD: hackmotion.json stamp)
tools/swinglab/src/swinglab_run.cpp                   # offline runner (rebuild after any analysis C++ change)

src/Analysis/phase_segmenter.h        # SegmentationConfig  (seg.*)
src/Analysis/shaft_tracker_math.h     # ShaftDetectConfig   (shaft.*)
src/Analysis/shaft_track_assembly.h   # AssemblyConfig      (assembly.*)
src/Analysis/wrist_angle_sampler.h    # PpWristSamplingConfig (window/gimbal — not yet dotted-key)
src/Analysis/swing_scorer.cpp         # kWristBands  (μ/σ/weight — provisional, re-seated in Corpus 2)
src/Analysis/wrist_assessment_engine.*# Tier-1 bands + Tier-2 rules + score v2 (RuleTuning)
src/IMU/imu_calibration.h             # mount/anat gates (A·M, axisAngleDeg, composite calibrated)
src/IMU/orientation_filter.h          # Madgwick m_beta / ESKF

<SwingData>/corpus-1/CORPUS.md         # blessing + tier(1) + partition record
<SwingData>/corpus-2/CORPUS.md         # IMU + HackMotion concurrent
<SwingData>/corpus-3/CORPUS.md         # + camera/IMU calibration, multi-golfer
<SwingData>/corpus-*/<swing>/truth.json        # P-position hand labels (Markup Lab)
<SwingData>/corpus-2/<swing>/hackmotion.json   # concurrent criterion (reconciled)
<SwingData>/runs/                      # scorecards, contact sheets, REPORT/TRIAGE/ESCALATION
```

---

## Appendix A — Implementation follow-ups & open work

The dotted-key tunability infrastructure is **built, unit-tested, and committed** (every analysis
parameter is now injectable and diff-gated; the frozen defaults live in
`src/Core/pp_tuned_constants.h`; see [`tunable_parameters_reference.md`](tunable_parameters_reference.md)
for the catalog + developer guide). What remains is the validation *campaign* plus a few code gaps. This
appendix is the honest punch-list, ordered within each bucket by leverage.

### A.1 Code follow-ups — desk work, unlock observability

| # | Item | Why it matters | Size |
|---|---|---|---|
| 1 | **Populate a real per-sample pitch-proxy** in `MetricExtractor` and thread it through `buildWristAngleSource` / `parseAnalysisDetail` (today `pitchProxyDeg = 0`). | `sampler.gimbalThresholdDeg` is wired but **inert** offline — the Gap/Indeterminate gate never fires, so the knob is unobservable in `diag.*`. | S–M |
| 2 | **Finish C5** — the two remaining §5.3.1 filter checks: **per-phase `imu_vision_corr`** and **net-drift-vs-gravity** at the still finish. Also confirm `filter.impact_continuity` actually *responds* (it returned a constant on synth — likely no impact shock to act on, possibly a gyro-prediction frame subtlety) before promoting it from `warn` to a real objective. | `filter.*` currently leans on `imu_vision_corr`/`diag.*`; these add the IMU-only, vision-independent objectives the schedule was designed against. | M |
| 3 | **Run a full app build** (`pinpoint` target). Only `swinglab_run` + the standalone test suites were built; the changes are additive/header-only but the GUI link through the `swing_analysis.h → wrist_assessment_result.h` include ripple is unverified. | Closes a verification gap. | S |

### A.2 Test / synth enhancements — de-risk A.1

| # | Item | Unlocks |
|---|---|---|
| 4 | **Synth swing with a deliberate impact-saturation spike** (`±16 g` clip + ringing near `impactUs`). | The whole impact-handling path (blanking + saturation gate + `filter.impact_continuity`) is currently untested on data — the existing synth models no impact. |
| 5 | **Scripted-fault synth** (cast/flip generated to deliberately trip a specific rule). | Makes `diag.recall` a true known-answer regression test; today the synth's "cast" finding is incidental, not designed. |

### A.3 Blocked on data / studio — the validation campaign itself

This is the point of the whole programme; the machinery now exists to execute it (tracked by the
three-corpus progression §2 and `corpus_v1_validation_plan.md`):

| # | Item | Gated on |
|---|---|---|
| 6 | **No sweep has been run on a real corpus.** Partition, regression gate, coordinate descent, `diag.*`, `filter.*` are all built and unit-tested — the *tuning* awaits a blessed corpus. | **Corpus 1** (existing data unreliable). |
| 7 | **`score.*` band re-seating** (μ/σ/weight) — has no lab-scorecard objective; validated against the criterion. | **Corpus 2 / HackMotion**. |
| 8 | **`rules.*` / `bands.*` recall/specificity tuning.** | Labelled **known-groups** swings (scripted faults). |
| 9 | **`filter.*` schedule values** (`betaDynamic`, gates, blank widths) — the synth cannot produce good values. | Real swings (+ Corpus-2 HackMotion for the impact-window check). |

### A.4 Deferred by design — revisit only if requirements change

| # | Item | Condition to revisit |
|---|---|---|
| 10 | **Expose ESKF `R`** — `third_party/imu_ekf` is kept verbatim and ESKF cannot warm-start, so it sits outside the re-fusion loop. | Only if ESKF replaces Madgwick as the production filter. |
| 11 | **Bayesian / CMA-ES optimiser backends** (`--method bayesian/cmaes`). | If coordinate-descent + random plateau on a real high-dimensional surface (the existing escalation trigger). |

### A.5 Score & diagnosis layer — Appendix B resolution (estimand decided 2026-06-28)

The estimand decisions (design §B.0/§B.0a/§B.7, and the A1 resolution note above) convert the Appendix B
critique into a defined engineering list — each item is now scheduled work, not an open question.
(`pitchProxyDeg` = A.1 #1 and the synth impact-saturation swing = A.2 #4 already cover findings **B4** and
**D4** and are not repeated here.)

| # | Item | Addresses | Size |
|---|---|---|---|
| 12 | **Re-cast the wrist scorer as per-archetype resemblance** (design §B.0a): resemblance to each of bowed/neutral/cupped, surface max + label + blended flag; **move `WristAssessmentEngine` findings to the AI-coach feedback layer**; retire the impact-only one-sided `SwingScorer` headline for Wrist. | B1 | M |
| 13 | **Attach a score uncertainty interval** (§B.7): decouple the central score from confidence (remove the `severity × confidence` term on the central value), propagate per-cell measurement error into an interval, low confidence **widens** it. | A3, B2, C5 | M |
| 14 | **Per-cell error budget** = `σ_sensor ⊕ σ_crosstalk ⊕ (dθ/dt · σ_phase-timing)`, folded into cell confidence and the §B.7 interval — validate segmentation timing and wrist angle **jointly**, not as independent stages. | C3 | M |
| 15 | **Expose the discrimination literals as dotted keys** (resemblance `μ_p`/`σ_p`; archetype top-delta; `flip −8/−5`) so the boundaries are sweepable — **kept frozen until labels exist**. | C1 | S |
| 16 | **Soft regression penalty for the sweep search operator** (hard `regressions==0` gate only at accept/freeze); acknowledge `seg.*` non-separability. | D2 | S |
| 17 | **Re-label `imu_vision_corr` as verification/consistency** (not validation) in the acceptance tables (§10); add the explicit caveat that a Corpus-1 reliability gate does **not** bound the ~10–15° systematic FE↔RUD cross-talk. | D3, C4 | XS (doc) |
| 18 | **Internal-consistency unit tests** of the scoring construction (monotonicity, boundedness, deadband-seam continuity); the two-system Bland–Altman becomes moot once #12 lands. | D5 | S |

**Sequencing:** #12–#13 are the load-bearing rework (they make the score mean what §B.0 says); #14–#15 make
the corpus sweeps interpretable; #16–#18 are claim-correctness and conditioning. None needs new hardware —
only #12's `μ_p`/`σ_p` and the band re-seat are data-gated (Corpus 2).

**Bottom line:** the door is open — what's left is the data-gated validation campaign (A.3), the
score/diagnosis rework now that the estimand is decided (A.5 — mostly desk work; only the `μ_p`/`σ_p` re-seat
is data-gated), two small observability gaps (A.1 #1–#2), and a full-app build (A.1 #3).

---

## Appendix B — Data-Science Review (academic critique)

*Added 2026-06-27 as an external data-science critique (measurement-science lens) of `src/Analysis/**` and
the methodology in the body above. It is a static read of code + docs — not yet run against a corpus; claims
reasoned from the band models are flagged where they await empirical confirmation. It records open conceptual
concerns and does not supersede the decisions in the body, which remain the reference until a finding is
actioned.*

The body of this reference is, frankly, already stronger than most published sports-science pipelines — the
verification/validation/tuning trichotomy (§1.1), the sample-size/power reasoning (§3), the
partition-and-diff-gate discipline (§7), and the "criterion not gold-standard" honesty (§1.3, §6.7) are
textbook-correct. So this appendix spends little time praising and most of it on the **structural** gaps that
survive even a careful design: where the *problem* is under-specified, where the *solution* says two
different things at once, where an *approach* is subtly circular, and where the *optimisation/validation*
could be reframed to be more honest or more powerful.

The single most important theme: **the pipeline is being validated as if it measures something, but the
headline product — a 0–100 swing score — is currently an *index* with no defined estimand and no criterion.**
Most findings below are facets of that one gap.

### B.1 What is already right (kept brief, for calibration)

- **V/V/T separation + "tuning a metric then reporting it as validation is the cardinal sin"** (§1.1) —
  correct and rare.
- **Sample size set by the statistic, not the stage** (§3): rule-of-three on all-pass results, LoA SE, χ² CI
  on a variance, and the **clustering/design-effect caveat** ("more *golfers*, not more swings", §3.5).
- **Reproducibility hygiene** (§4): replay production code, attribution hashes, same-host-only diffs, no
  silent truncation.
- **Cross-talk-safe angle math**: swing–twist decomposition before Cardan (`wrist_angles.h:68–134`).
- **Aggregation that resists averaging-away**: the weighted **geometric** mean in `SwingScorer`
  (`swing_scorer.cpp:141`) — one severe sub-score can't be hidden by good ones.
- **Offline re-fusion + parity gate** (`imu_refusion_check.h`, `imu_vision_fuser.cpp:40`): proves the
  captured corpus is post-hoc tunable.

These are real. The rest of this appendix is the part worth acting on.

### B.2 Findings

Grouped by the four lenses; each carries a **severity** (how much it threatens a defensible coaching number)
and concrete `file:line` evidence.

#### A. Problem-space definition

**A1 — The headline score has no estimand.** *(Severity: high)*
`r.score` is `SwingScorer::score(...).overall` (`wrist_analyzer.cpp:359,382`): a weighted geometric mean of
deadband sub-scores, each a Gaussian z-distance of one angle-at-impact from a hand-chosen centre
(`swing_scorer.cpp:122–146`). Nothing in the construction ties the number to a quantity that exists outside
the formula — not a probability, not an expected outcome, not a percentile, not a coach rating. It is a
**distance from a heuristic prior**, presented as a 0–100 measurement. The body correctly defers predictive
validity and external arbiters to Corpus 3+ (§1.2, §8, §11) — but that means **today the score is an index
whose only justification is face validity**, and indices must be *constructed* deliberately (estimand stated,
weights justified, normalisation defined). The estimand is currently unstated.

> Framing fix is cheap and clarifying: **declare, in one sentence, what the score estimates** — (i) percentile
> within a tour-pro reference distribution (norm-referenced — needs a norm sample, A2), (ii) deviation index
> from a coaching ideal in arbitrary units, not comparable across players (criterion-referenced — needs the
> scale defended), or (iii) an expected strike-quality proxy (predictive — needs outcomes). Each choice
> dictates a *different* validation.


> **Resolution (2026-06-28) — estimand updated.** Scores are **per session type, criterion-referenced, and never aggregated across types**. The wrist headline overall score is the penalty-based assessment score (`assessmentScore`) computed by `WristAssessmentEngine` (Score v2), which deducts points for faults and watches from a baseline of 100. This provides a direct measure of wrist movement quality (adherence to reference bands without executing faults). Archetype resemblance (Bowed/Neutral/Cupped) is retained as a descriptive style classification, but it no longer defines the headline quality grade.

**A2 — Norm-referenced bands cannot be estimated from a single-golfer corpus.** *(Severity: high)*
`reference_bands.cpp` corridors and `kWristBands` μ/σ are "where good comes from" — population norms. The plan
re-seats `kWristBands` at **Corpus 2** on "(1) the observed corpus distribution and (2) HackMotion's published
tour ranges" (§5.6, §6.6). But Corpus 1 **and** Corpus 2 are *single-golfer by design* (§3.5, §11). A one-golfer
"observed distribution" is *within-subject* variation, not the *between-subject* spread a norm corridor must
encode; re-seating on it will systematically **under-estimate σ** (one golfer is more self-consistent than a
population), making corridors too tight and the score too sensitive. **Norm-referencing is a Corpus-3
(multi-golfer) capability, yet the plan locks bands at Corpus 2.** Before Corpus 3 the only legitimate anchor
is the *external* published tour range — so frame the Corpus-2 re-seat as "anchor to external norms + check our
golfer falls in-range", not "estimate norms from our data."

**A3 — σ conflates population spread with measurement tolerance.** *(Severity: medium-high)*
In `bandSubScore` (`swing_scorer.cpp:126`) the score is `z = (value − μ)/σ` with a deadband |z|≤1 and falloff
to ~0 at |z|=3 (`pp_tuned_constants.h` `scoring::kZIn=1, kZOut=3`). So σ sets *both* how much natural variation
is "fine" (a population statement) **and**, because IMU error is 5–8° with 10–15° FE↔RUD cross-talk
(`wristmetrics.md` (c); `wrist_angles.h:51–53`), how much measurement noise is absorbed before the score moves
(a tolerance statement). With σ_FE = 12° and error ~5–8°, **much of the deadband is spent on instrument noise,
not biomechanics.** A principled model carries σ_population and σ_measurement as distinct terms.

#### B. Solution clarity

**B1 — Two parallel scoring systems, different conventions, can contradict.** *(Severity: high — top structural issue)*
The pipeline computes **two independent 0–100 numbers and two band models for the same swing**, both persisted
to `swing.json` (`swing_doc.cpp`):

| | `SwingScorer` (headline `r.score`) | `WristAssessmentEngine` (`assessmentScore` + findings) |
|---|---|---|
| Band model | Gaussian μ/σ (`kWristBands`, `swing_scorer.cpp:53`) | Green/amber Δ-corridors (`reference_bands.cpp:48`) |
| Reference frame | **absolute** neutral-relative value | **Δ-from-address** |
| Sampled where | **Impact only** | **8 P-positions** (P1–P8) |
| Aggregation | weighted **geometric mean** | **additive penalty** `100 − Σ pen` |
| Drives | the headline number | the coaching words + a *second* number |

Wired at `wrist_analyzer.cpp:359` (scorer) and `:369–375` (engine). The **number** comes from the Gaussian
model, the **words** from the corridor model — different references, positions, weights — so they can disagree:
the corridor engine can raise a red **"early release (cast)"** finding on RUD@P6 (`assessment_rules.cpp:109`)
while the Gaussian headline stays high (it never looks at P6 and weights RUD only 0.15 at impact). A user sees
*"cast — Fault"* next to a green-ish score. **Decide which model is the measurement model and make the other
derive from it (or retire it).** (The body has the same blind spot — §5.6 treats `kWristBands` as "the bands"
and barely acknowledges the engine bands its own way.)

**B2 — Penalty weighting by confidence makes a noisier sensor read as a better swing.** *(Severity: medium-high)*
Score v2 penalty per finding is `severityW × confidence × weight × scoreScale`
(`wrist_assessment_engine.cpp:114`). Defensible *as expected loss*, but unstated as such and perverse: lower
confidence → smaller penalty → **higher score.** A poorly-seated sensor (low cell confidence) is penalised
*less* and scores *better* than the same swing measured cleanly. Confidence should drive an **uncertainty band
on the score**, not its central value.

**B3 — The two aggregators encode opposite philosophies.** *(Severity: medium)*
`SwingScorer` uses a geometric mean so one severe fault dominates (`swing_scorer.cpp:141`); the engine uses
linear additive penalties (`wrist_assessment_engine.cpp:108–118`) where correlated findings stack — and the
rules *know* they correlate (`cast`↔`flip` linked, `assessment_rules.cpp:302–307`; F6/F7 mutually exclusive,
`:281–292`). Linear addition of correlated penalties **double-counts** one underlying flaw. The two subsystems
should share an aggregation philosophy.

**B4 — A wired-but-inert knob is a tuning trap.** *(Severity: medium)*
`pitchProxyDeg` is hard-set to `0` wherever the offline source is built (`wrist_analysis_adapter.cpp:76,128`),
so the sampler's gimbal/`Indeterminate` gate (`wrist_angle_sampler.h:108`) **can never fire offline.** A sweep
over `sampler.gimbalThresholdDeg` will report "no effect" and a reader will conclude the threshold is robustly
chosen and the `<2%` Indeterminate target (§10) is met. A dead knob inside a tuning framework manufactures
false confidence. (Appendix A.1 #1 — flagged here as a *methodological* hazard: any sampler-touching sweep
result is currently meaningless.)

#### C. Approach / correctness

**C1 — The most behaviourally-decisive rule thresholds are frozen literals outside the tunable surface.** *(Severity: high)*
The "everything is dotted-key tunable and diff-gated" claim (§2.4) is the backbone of the optimisation story,
but the parameters that most directly set fault recall/specificity are not in it. The flip rule fires on raw
literals — `der < −8.0 → Fault`, `< −5.0 → Watch` (`assessment_rules.cpp:104–105`) — and the archetype
auto-detector switches on `topDelta > 10 / < −10` (`wrist_assessment_engine.cpp:37–38`). Neither is reachable
through `RuleTuning`, which exposes only `confidenceFloor / scoreScale / severityWeights / corroborationBoost /
strengthsRequireAdjacentFault` (`wrist_assessment_tuning.h:48–53`). So when Corpus 1 known-groups tuning
arrives (§5.6), the **discrimination boundaries cannot be swept** — only the cosmetics around them. The
framework's central promise doesn't reach its most important knobs.

**C2 — Archetype normalisation adapts the corridor to the swing it is judging.** *(Severity: medium-high)*
`detectArchetype` reads FE Δ@Top and, if strongly cupped/bowed, shifts the FE corridor ±10° toward that style
(`wrist_assessment_engine.cpp:31–40` → `reference_bands.cpp:99–117`). Fault F1 "open face at top" is defined
on *the same axis at the same position* (`assessment_rules.cpp:70–79`), so a cupped swing nudges its own
corridor cup-ward, **reducing the very sensitivity that should flag it** — a self-referential coupling
resembling label leakage. It may be the right coaching call, but it needs an explicit **sensitivity analysis**
(F1 recall loss across the ±10° shift; stability of the ±10° gate), not assertion.

**C3 — Segmentation→sampling error is not propagated into the wrist-angle error budget.** *(Severity: medium-high)*
Every wrist cell is a windowed median *at a phase timestamp* (`wrist_angle_sampler.h:95–113`) from
`PhaseSegmenter`. The plan validates Top timing ≤30 ms (§5.5) and wrist repeatability (§5.5) as **independent
stages** — but during the downswing FE/RUD change fast, so a 30 ms timing error injects degrees into the
*value* being banded, atop the sensor's 5–8°. P6 ("Delivery") is an explicit low-confidence proxy capped at
conf ≤ 0.4 (`phase_segmenter.cpp:351,357`), and F3/F4 read P5/P6/P7. The real per-cell budget is
`σ_sensor ⊕ σ_crosstalk ⊕ (dFE/dt · σ_phase-timing)` — only the first is tracked. Derive a per-cell error bar
that includes local slope × timing jitter and fold it into cell confidence.

**C4 — Systematic cross-talk is invisible to the Corpus-1 reliability checks meant to catch it.** *(Severity: medium)*
`wrist_angles.h:51–53` documents ~10–15° residual FE↔RUD cross-talk — a **bias**, not random error. Corpus 1's
wrist gate is *repeatability* (test–retest RMSE, §10), which a constant bias passes cleanly while corrupting
every absolute reading. The plan correctly locates the fix at Corpus 2 (HackMotion bias/leakage, §6.6 #2) —
the point here is a caveat the Corpus-1 acceptance table should carry explicitly: "reliability < X° does not
bound accuracy; a 10–15° systematic FE/RUD leak passes every Corpus-1 wrist gate." (This is the body's own
reliability≠agreement principle, §1.3, applied to a concrete failure mode it doesn't name.)

**C5 — No uncertainty is attached to the displayed number.** *(Severity: medium)*
The headline is `int` 0–100 (`swing_analysis.h`, `wrist_analyzer.cpp:382`). Given ~5–8° sensor error,
~10–15° cross-talk, and σ-scaled bands ~12°, the propagated uncertainty is plausibly ±10–20 points per swing.
A bare integer is exactly the "precise, confident, wrong" failure the body warns against (§0). The cell-level
machinery (`confidence`, `lowConfidence`, `Indeterminate`) exists but stops at the cells and never reaches the
headline.

> **Resolution (2026-06-29) — partial; interval scoped to the resemblance, headline interval deferred.** WP-4
> built the §B.7 FE-budget interval, but the unifying decision (A1, 2026-06-28) made the wrist **headline** the
> penalty-based assessment score, whose honest interval would propagate *finding confidence*, not the FE
> z-score budget. Those are different quantities, so the FE-budget interval brackets the **resemblance value**,
> not the headline. The analyzer now keeps the interval only while `overall` *is* the resemblance value and
> clears it once the assessment score takes over (`wrist_analyzer.cpp`), preserving `0 ≤ lo ≤ overall ≤ hi`
> (and satisfying `score.py::score.interval_valid`). **Outstanding:** the assessment-score headline still has
> no interval — item 6 below remains open until a finding-confidence error model is built; the live wrist
> headline therefore shows a bare integer for now.

#### D. Optimisation & validation methodology

**D1 — The diagnosis/score layer is not yet an optimisation problem — it has no loss function.** *(Severity: high — the key reframe)*
The optimiser maximises "mean corpus score subject to per-swing `regressions == 0`" (§7.1). For the
self-consistency stages (`seg.*`, `shaft.*`, `assembly.*`, `filter.*`) the objective is well-posed
(`imu_vision_corr`, coverage, Top-vs-label RMSE). **But for `score.*`, `rules.*`, `bands.*` there is no
objective until coach labels (κ/ICC) or outcomes exist** (Appendix A.3 #6–#8 admits this). Tuning a coaching
score to maximise self-consistency is **circular** — optimising the score to agree with the angles it is
computed from. Reframe by **splitting the diagnosis layer into two genuinely different problems:**

1. **Measurement-model fitting (unsupervised, distributional).** Calibrating `kWristBands` μ/σ and corridor
   lo/hi is *fitting a reference distribution* — a goodness-of-fit task against a (multi-golfer, A2) sample +
   external tour ranges. **Not** a recall/score sweep; never diff-gated on corpus score.
2. **Fault-rule calibration (supervised classification).** F1–F8 recall/specificity is a labelled
   classification problem; its loss is a confusion-matrix objective (e.g. F-β), needing labels — and its
   decision thresholds (C1) must be in the swept surface.

Until labels exist, the correct action on `score.*`/`rules.*` is **freeze, don't sweep** — which Appendix A.3
nearly says but the unified objective in §7.1 blurs.

**D2 — Coordinate descent + a per-swing hard-reject gate can deadlock on correlated knobs and bias the search.** *(Severity: medium)*
`seg.*` is not separable: `fcEnvelopeHz` sets the envelope smoothing every downstream crossing keys off
(`phase_segmenter.cpp:93–101,329,353`), so one-at-a-time moves can wall off regions a joint move reaches; the
escalation to TPE/CMA-ES (§7.1) is the right instinct but triggers only "on plateau", which a hard-reject gate
can manufacture prematurely (one regressing swing blocks a step that improves the mean and 49/50). A hard
reject also makes the search **order-dependent.** Use a **soft** regression penalty for the *search*, keeping
the hard gate only for the *accept/freeze* decision.

**D3 — Self-consistency (`imu_vision_corr`) validates fusion, not accuracy.** *(Severity: medium)*
Two sensors of the same rigid motion agreeing tells you the fusion is consistent, not that the anatomical
frame is correct — a wrong-but-consistent `A·M` makes both agree on the wrong angle. The body knows this
(absolute correctness → HackMotion at Corpus 2) — **label `imu_vision_corr` explicitly as a
verification/consistency statistic** in the acceptance tables (§10); it currently sits in the "Validate"
column (§5.3), slightly over-stating what it earns.

**D4 — The synthetic corpus models none of the hard cases the filter work targets.** *(Severity: medium)*
The §5.3.1 phase-adaptive filter targets impact saturation (±16 g + ringing) and downswing linear-accel
corruption, but the synth models **no impact shock** (Appendix A.2 #4; `filterImpactStepDeg` "returned a
constant on synth", Appendix A.1 #2). So the impact-handling path (blanking, saturation gate,
`filter.impact_continuity`) is un-exercised by any data. The synth impact-spike (A.2 #4) should be a *blocking
prerequisite* for claiming the impact-window machinery works — it is the only pre-Corpus-2 way to verify the
code path.

**D5 — Validate the score against itself before validating it against the world.** *(Severity: low-medium, cheap)*
Independent of any corpus, cheap internal-consistency properties of the scoring *construction* can be
unit-tested today: monotonicity (score non-increasing as any metric moves from μ), boundedness, continuity at
the deadband seams (`swing_scorer.cpp:130–137`), and **agreement between the two scoring systems** (B1 — even
a Bland–Altman of `r.score` vs `assessmentScore` across the synthetic corpus quantifies how badly they
disagree before real data). Verification, not validation — but free, and it surfaces B1/B3 numerically.

### B.3 Prioritised recommendations

Ordered by leverage-to-cost. "Cost" is rough engineering size; "Unblocks" names the validity rung it buys.

| # | Priority | Recommendation | Addresses | Cost | Unblocks |
|---|---|---|---|---|---|
| 1 | **P0** | **Declare the score's estimand in one sentence**, then make band/aggregation choices follow from it (index vs norm-referenced vs predictive). | A1 | XS (doc) | every downstream validation target |
| 2 | **P0** | **Reconcile the two scoring systems**: pick one measurement model, make the other a *view* of it or retire it. Interim: synth-corpus Bland–Altman of `r.score` vs `assessmentScore`, **gate release on their agreement**. | B1, B3, D5 | S–M | a coherent product number |
| 3 | **P0** | **Reframe diagnosis tuning as two problems** — distributional band-fitting (unsupervised) vs fault-rule classification (supervised) — and **freeze `score.*`/`rules.*` until labels/outcomes exist** instead of sweeping under the self-consistency objective. | D1 | S (doc + sweep cfg) | non-circular tuning |
| 4 | **P1** | **Expose per-rule decision magnitudes** (`flip` −8/−5, archetype ±10, …) as `rules.*` dotted keys so discrimination boundaries are sweepable. | C1 | S | meaningful recall/specificity tuning |
| 5 | **P1** | **Separate σ_population from σ_measurement**: score the probability the true value is outside the corridor *given* the per-cell error bar. | A3, C5 | M | honest score + interval |
| 6 | **P1** | **Attach an uncertainty interval to the headline score**; gate full-precision display on confidence; stop multiplying the *central* penalty by confidence (widen the interval instead). | B2, C5 | M | "precise-confident-wrong" guard |
| 7 | **P1** | **Build a per-cell error budget including phase-timing jitter × local slope**; validate segmentation and wrist *jointly*. | C3 | M | defensible cell confidence |
| 8 | **P1** | **Fix the inert `pitchProxyDeg`** before any sampler sweep is trusted; until then mark sampler sweeps "inert, do not interpret." | B4 | S–M | valid sampler tuning |
| 9 | **P1** | **Build the synth impact-saturation swing**; make it a blocking prerequisite for any impact-filter claim. | D4 | S–M | filter verification |
| 10 | **P2** | **Re-label `imu_vision_corr` as verification, not validation** in the acceptance tables; add the explicit caveat that Corpus-1 reliability gates do not bound cross-talk bias. | C4, D3 | XS (doc) | honest acceptance reporting |
| 11 | **P2** | **Sensitivity-analyse archetype normalisation** (F1 recall loss across the ±10° shift; ±10° gate stability). | C2 | S | trustworthy style normalisation |
| 12 | **P2** | **Soft regression penalty for the search operator** (hard gate only at accept/freeze); acknowledge `seg.*` non-separability. | D2 | S | better-conditioned sweeps |
| 13 | **P2** | **Correct the norm-referencing claim**: Corpus-2 band re-seat anchors to *external* tour ranges + in-range check, not a one-golfer distribution; defer norm estimation to Corpus-3. | A2 | XS (doc) | logically valid bands |
| 14 | **P2** | **Internal-consistency unit tests** of the scoring construction (monotonicity, boundedness, deadband-seam continuity, two-system agreement). | D5 | S | cheap regression net |

**Sequencing.** Do the three P0 *framing* items first — mostly decision/documentation, and they change what
every later experiment is *for*. Items 4–9 are the desk-work that makes the corpus campaign *interpretable*
(without them, sweeps produce confident-but-meaningless results). Items 10–14 are correctness-of-claim and
conditioning improvements that ride along.

### B.4 Bottom line

The engineering and the methodology body are strong; the gap is conceptual and concentrated in the
diagnosis/score layer. The product ships a precise number for a quantity that has never been defined (A1),
computes it two incompatible ways that can contradict each other (B1), rests its scale on an eyeballed σ that
silently mixes biology with sensor noise (A3), would (if tuned now) optimise that number against itself for
lack of any external objective (D1), and reports it with no uncertainty even though its inputs carry 5–15° of
error (C5). None of this needs new hardware to begin: the highest-leverage moves are *definitional* — state the
estimand, unify the model, split the tuning problem, and stop calling self-consistency "validation." Those four
decisions convert an impressive pile of machinery into a defensible measurement.

---

*This document is the reference; the corpus plan is the runbook; the `/swinglab` skill is the operator
contract. When the three disagree, fix the disagreement — do not pick one silently.*
