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
  distinction — a regression silently discards frames).
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
  Kabsch still-window length.
- **Accept/Refine.** corr ≥ 0.9 on ≥ 90 % bound; converged before Takeaway; all calibrated. **Corpus-2
  refinement:** a *constant* per-axis wrist bias vs HackMotion localises *here* — an `A·M` alignment error
  or a filter sign — versus random LoA (noise). HackMotion is the tool that separates "noisy filter" from
  "mis-aligned frame".

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

*Modules:* `SwingScorer` (`kWristBands`), `WristAssessmentEngine` (Tier-1 bands + Tier-2 rules F1–F8 +
strengths + archetype), score v2.

- **Verify.** Golden-tested against synthetic fixtures (`makeCast`/`makeFlip`/`makeOpenFaceTop`/…): each
  scripted fault produces its finding; score v2 contributions sum to the score; banding monotone.
- **Validate.**
  - **Known-groups (Corpus 1).** Scripted-fault capture: the engine must flag the matching fault and
    **not** flag it on the clean control. Report recall + FP rate **per rule** — *with the §3.1 honesty*:
    **~10–15 scripted swings/fault** gives a point estimate + wide CI ("the rule fires and is specific"),
    **not** a recall ≥ 0.95 guarantee (that needs ~60/fault, deferred to a pooled fault library).
  - **Band calibration (Corpus 2).** `kWristBands` μ/σ and the Tier-1 corridors are *provisional*. Anchor
    on (1) the **observed corpus distribution** and (2) **HackMotion's published tour ranges** (top
    −30/+5, impact −15/−40, extension-positive) — external content-validity for where corridors sit.
  - **Score internal consistency.** Monotone w.r.t. metric quality; weighted **geometric** mean prevents
    one severe fault being averaged away.
  - **Inter-rater (Corpus 3+).** Coach annotations (§8): **Cohen's κ** (fault presence), **ICC** (score).
    Today the diagnosis has **no external arbiter** — the biggest validity gap. *Sample size:* κ needs
    enough swings *with the fault present*; ~30–50 annotated swings for a usable κ CI, more golfers for
    generalisation.
- **Tune.** `kWristBands` (μ/σ/one-sided/weight: FE 0.45 / RUD 0.15 / PS 0.20 / elbow 0.20); `RuleTuning`
  (`confidenceFloor 0.45`, `scoreScale 18`, Fault 1.0 / Watch 0.5, `corroborationBoost 0.30`,
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
- **Co-mounting perturbation, single golfer, modest N** — LoA are estimates with CIs; **within-subject
  only** until Corpus 3 adds golfers (§3.5).
- **Correlation ≠ agreement; regression-to-criterion trap** — always Bland–Altman; never tune a sign or
  offset to chase the criterion without the static-pose anchor.

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
| **Filtering** | `imu_vision_corr ≥ 0.9` (≥ 90 % bound, ±14 %); converged < 500 ms | wrist bias localises `A·M` | calibration cross-checked |
| **Pose / shaft** | coverage ≥ 0.6 (≥ 90 % clean); θ-RMSE < 3° / head < 25 px (10–15 labelled) | (regression) | + metric-scale camera metrics |
| **Segmentation** | monotone 100 %; Top ≤ 30 ms; tempo ∈ [1.2,6.0] | (regression) | (regression) |
| **Wrist metrics** | repeatability < 5° FE / < 8° RUD (≥ 25–30 repeats); < 2 % Indeterminate | **FE/PS RMSE ≤ 4–5°, ICC ≥ 0.90; RUD ≤ 8°; traj r ≥ 0.95 (≥ 50 paired)** | + between-golfer LoA |
| **Diagnosis** | scripted-fault recall point-estimate high; ≈ 0 FP on clean | bands vs HackMotion ranges | κ/ICC vs coach; outcome predictivity |

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

*This document is the reference; the corpus plan is the runbook; the `/swinglab` skill is the operator
contract. When the three disagree, fix the disagreement — do not pick one silently.*
