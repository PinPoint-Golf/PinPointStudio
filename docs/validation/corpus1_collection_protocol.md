# Corpus 1 — Data Collection Protocol (IMU-only)

**Document type:** Experimental methods / data-collection protocol (collection team).
**Corpus:** 1 of 3 — *IMU-only; internal consistency, reliability & known-groups validity.*
**Version:** 1.0 (2026-06-26) · **Status:** ready for execution · **Owner:** Mark Liversedge.
**Reference (the "why"):** `docs/validation/pipeline_validation_and_tuning.md` (§2 three-corpus
progression; §3 sample size; §5 per-stage validation). **Operational runbook:**
`docs/implementation/corpus_v1_validation_plan.md`. **Reporting standard:** GRRAS (Kottner 2011) for the
reliability/agreement elements; STROBE (von Elm 2007) for the observational design.

> **What the collection team must produce:** a *blessed* set of ~50 full, scored Wrist-session swings
> (plus a partial-swing robustness set and a scripted-fault library) in which **every scored swing carries
> bound + calibrated IMU, a real impact instant, and complete capture provenance** — the precondition
> without which nothing downstream can be validated. Fill in the tracking tables in §13 **as you capture**,
> not afterwards.

---

## 1. Study identification & scope

| Field | Value |
|---|---|
| Corpus ID | `corpus-1` |
| Session type | **Wrist Motion** (sessionType = 1) — the only real analyzer |
| Instruments | PinPoint IMUs (lead forearm + hand; optional upper arm). **No HackMotion in the dataset** (see §3.4). **No camera calibration.** |
| Primary unit | one **swing** (a recorded shot dir) |
| Validity earned | face, content, **construct/known-groups**, **reliability (test–retest)**, internal cross-modal consistency, absolute track/phase accuracy vs hand labels |
| Validity NOT earned here | criterion (HackMotion → Corpus 2); external/population (multi-golfer → Corpus 3) |

**Scope boundary.** Corpus 1 establishes *within-subject* evidence and **all the tooling, gates and
markup workflow that Corpus 2 and 3 reuse**. It does not make any cross-golfer (population) claim — that is
explicitly deferred to Corpus 3 (reference §3.5).

---

## 0a. Pre-collection engineering dependency — orientation re-fusion parity (BLOCKING the *tuning*, not the *capture*)

A critical Corpus-1 outcome is **tuning the orientation filter post-hoc** (phase-adaptive gain + impact
handling — reference §5.3.1). This requires re-running the filter offline from the recorded raw inertial
data. Status, verified against the code (2026-06-26):

- **Capture is already sufficient — collection is NOT blocked.** The raw accel (g) + raw gyro (°/s) are
  persisted per sample (`imu_sample_v2`), with per-sample `t_us` and the per-device `alignA`/`mountM`
  snapshot. A swing captured on the current build **is re-fusable post-hoc**.
- **Offline re-fusion is NOT implemented.** The analyzer consumes the *stored live-fused* quaternion
  (`ImuVisionFuser` applies `A·qRaw·M`); nothing re-runs Madgwick/ESKF offline. The re-fusion + filter
  parameter-injection harness must be **built** — but it operates on captured data, so it can follow
  collection.

**Therefore, before Corpus 1, complete this one cheap de-risk** (engineering, not capture):

| # | Pre-collection check | Why it must precede 50 swings |
|---|---|---|
| E1 | Build a **minimal offline re-fusion + parity tool**: instantiate the filter → feed stored raw (gyro °/s→rad/s, accel g, **nominal `dt = 1/outputRateHz`** — *not* per-sample Δt) → **warm-start from the stored quat at the window's first sample** → compare to the stored live quat | proves the captured corpus is genuinely tunable; a re-fusion gap found *after* capture wastes the corpus |
| E2 | Confirm the **collection build writes `imu_sample_v2`** with genuine raw vectors (not legacy v1 display-frame, not zeroed) at **200 Hz** | pre-corpus recordings predate trustworthy provenance; one pilot swing settles it |
| E3 | Confirm each capture **window contains a still address segment** (the 5 s trailing window normally spans backswing+address) | the warm-start/seed and gravity-lock checks need a low-dynamics anchor |

E1 parity criterion: `refuse(stored raw, production params, warm-started) ≈ stored live quat` on ~3 pilot
swings. **Pass → collection is safe.** Fail → fix the capture schema before committing studio time. The
*full* tuning harness (filter-param `tuningOverrides` + `lab.py sweep` over the §5.3.1 schedule) can be
built after collection. Record E1–E3 outcomes in §15 (deviations) or a pre-flight note in `CORPUS.md`.

---

## 2. Objectives, hypotheses & pre-registered acceptance

Pre-register these *before* collection; do not move a threshold to make a result pass (reference §7).

| # | Objective | Pre-registered acceptance | Feeds |
|---|---|---|---|
| O1 | Capture is faithful & complete | 100 % of scored swings pass the Tier-0 gate (§11) | streaming/ingest (ref §5.2) |
| O2 | Fusion ↔ vision are internally consistent | `imu_vision_corr ≥ 0.9` on ≥ 90 % of bound swings | filtering (ref §5.3) |
| O3 | Wrist metrics are **repeatable** | between-swing RMSE **< 5° FE / < 8° RUD**; < 2 % Indeterminate | wrist metrics (ref §5.5) |
| O4 | Track & phase timing are accurate | `theta_rms < 3°`, `head_median < 25 px`, Top error ≤ 30 ms on labelled subset | pose/segmentation (ref §5.4–5.5) |
| O5 | The diagnosis separates known groups | scripted faults flagged; ≈ 0 false positives on clean controls | diagnosis (ref §5.6) |
| O6 | Graceful degradation on partial swings | no crash / no fabricated impact on incomplete swings | segmentation/shot-detection robustness |

**Hypotheses are directional and pre-specified** (e.g., a scripted *cast* swing scores lower on the `cast`
rule than its clean control). Record the hypothesis register in `CORPUS.md`.

---

## 3. Study design

- **Design:** prospective, observational, **within-subject** measurement-validation study.
- **Participant:** **one** golfer (single lead-handedness; record it). Single-golfer is the *correct*
  design for O1–O5 — they depend on the instrument + mount + algorithm, not the population (reference
  §3.5). Do **not** infer population generalisation from Corpus 1.
- **Blinding:** the **labeller is blinded to the analyzer output** for the swing being labelled (§9.4) —
  this prevents the "ground truth" being contaminated by the thing it is meant to judge.

### 3.4 HackMotion's role in Corpus 1 = informal pilot only

A **single** HackMotion co-capture session is run alongside Corpus 1 **but its data is not part of the
Corpus-1 dataset**. Its purpose is to *de-risk and size* Corpus 2 (reference §3.2, §6.2):

1. confirm the two devices can be co-worn without perturbing each other;
2. shake out the export → `hackmotion.json` → reconcile → `score.py` tooling;
3. **measure the SD of the IMU↔HackMotion difference per axis** — the number that determines how many
   paired swings Corpus 2 needs.

Capture ~10–20 paired swings for the pilot, run the reconciliation (Corpus-2 protocol §6.3), and **record
the measured SD per axis in this document's §14** so Corpus 2 can confirm its `n`. No power requirement —
a pilot estimates variance, it does not test a hypothesis.

---

## 4. Participant: inclusion, consent, sample-size rationale

- **Inclusion:** an able golfer who can repeat a consistent full swing and perform scripted faults on cue.
- **Consent & ethics:** informed consent for IMU + video capture; store under an **athlete UUID** (never a
  name); video retained per the project data-management plan. Right to withdraw.
- **Sample-size rationale (from reference §3, restated for the team):**
  - **Reliability (O3):** SEM/MDC precision ≈ `1/√(2(n−1))` → **≥ 25–30 repeated swings** for ±13–14 %.
  - **Track/phase RMSE (O4):** χ² on variance → **10–15 labelled swings** (each ~8–10 P-points = 100–150
    points → tight RMSE). Swing count is set by *condition coverage*, not statistics.
  - **Known-groups recall (O5):** a *defensible* recall ≥ 0.95 needs ~60 trials/fault (rule of three) —
    **impractical**, so capture **~10–15 per priority fault** and report a point-estimate + Wilson CI, not
    a recall guarantee.

---

## 5. Equipment & instrumentation

| Item | Spec / setting | Note |
|---|---|---|
| Lead-forearm IMU (🔴) | required; behind the wrist | slot A |
| Lead-hand IMU (🟡) | required; back of hand | slot B |
| Lead-upper-arm IMU (🟢) | **optional** — enables forearm pronation + IMU-elbow | slot C (capture *some* 3-sensor swings) |
| IMU output rate | **200 Hz** (±5 ms impact resolution vs ±10 ms at 100 Hz) | record `device.outputRateHz` |
| Face-on camera | perspective 2 (optional for Wrist, but capture it) | for shaft track + elbow cross-check |
| Down-the-line camera | perspective 1 (optional) | |
| Raw-frame capture | `saveRawFrames = ON` for the **raw subset** (~12 swings) | pixel-level shaft tuning needs it |
| Audio | capture if available | future shot-detection timing |
| Mount | strap "like a watch": face away from thigh, USB forward, firm | per `docs/user/wristcalibration.md` |

**Sensor count decision (reference §10.2):** capture **mixed 2- and 3-sensor** swings — 3-sensor validates
pronation + IMU-elbow; 2-sensor exercises the slot-C-absent graceful-degradation path. Tag each swing.

---

## 6. Setup & calibration protocol

Calibration teaches PinPoint how each sensor sits on the limb; it is **per-session and not saved**
(`docs/user/wristcalibration.md`). Calibration discipline is the single biggest determinant of data
quality — treat it as a measurement, not a formality.

### 6.1 Per-session setup
1. Mount sensors per §5; set handedness on the athlete profile.
2. Start a **Wrist Motion** session; **Connect** the enabled sensors.
3. Run the **two-pose calibration** (arm-down, arm-out); hold each until the progress bar fills.
4. **Confirm Tracking** — move the arm; verify the 3D model follows. If a sensor is mis-mounted the step
   fails and names the sensor → re-seat and recalibrate.

### 6.2 Calibration cadence (mandatory)

| Trigger | Action |
|---|---|
| Start of every session (every donning) | full 2-pose calibration + Confirm Tracking |
| Every **15 swings** or **20 minutes** (whichever first) | re-run Confirm Tracking; recalibrate if tracking looks off |
| Any sensor re-seat / slip | recalibrate |
| Any break > 10 min, battery swap | recalibrate |
| Any Tier-0 calibration-gate failure (§11) | recalibrate and recapture the affected swings |

Log `calibAgeSec` per swing (the app records it). **Keep `calibAgeSec` small** — recalibrate at least
every 30 min so no scored swing has stale calibration. The hard gate is the composite `calibrated: true`
(anat valid ∧ mount ≤ 15° ∧ gravity ≤ 25°).

### 6.3 Two repeatability calibration regimes (design-critical)

To separate *measurement noise* from *mount/calibration variability*, the repeatability stratum has two
sub-sets (§8):
- **Repeatability A (measurement noise):** **one calibration held fixed** across the whole set — isolates
  sensor + algorithm noise.
- **Repeatability B (re-mount sensitivity):** **re-mount + recalibrate between each set** of 3–5 —
  isolates how much the metric moves with re-donning (the real-world reliability a calibrate-once corpus
  hides). This is the "mount-perturbation set" of reference §8.5.

---

## 7. Data collection protocol

### 7.1 Full vs incomplete (partial) swings
- **Full swings** (Address → Top → Impact → Finish) are what we score; they dominate the corpus
  (≈ 83 % of captures). Only a full swing yields a scorable, labelable record.
- **Incomplete / partial swings** (half swings, takeaway-only, deliberately *abandoned* swings, waggle +
  regrip without a strike) are a **dedicated robustness stratum** (≈ 17 %, n ≈ 10). Their purpose is to
  confirm the segmenter and shot-detector **degrade gracefully** (O6): no crash, no fabricated impact, a
  sensible "no clean impact" outcome. **They are NOT scored and NOT in the reliability/accuracy
  denominators** — keep them tagged and separate.

### 7.2 Variants / conditions to span (diversity & stress)
Vary deliberately and tag each: **club** (driver / 7-iron / wedge), **tempo** (slow / normal / fast),
**swing length** (full / three-quarter), **background** (clean / cluttered: mat lines, alignment stick),
**wrist ROM** (normal / extreme), **lie** (mat / normal). The diversity stratum proves the metric is not
tuned to one narrow condition; the stress stratum probes the failure edges.

### 7.3 Scripted faults (known-groups, O5)
For each priority fault, capture a deliberate-fault block **and a matched clean control** by the same
golfer. **Priority order** (highest scoring weight first — `wristmetrics.md`):
1. **Open / closed face at top** (F1/F2 — lead-wrist FE; weight 0.45) — *capture first.*
2. **Cast / early release** (F4 — radial-ulnar).
3. **Flip / scoop** (F3 — FE derivative through impact).
4. *(if resources allow)* over-rotation (F6), holding-off (F7), insufficient-set (F5), chicken-wing (F8).

Capture **~10–15 of each** with a matched clean control set. These may overlap the diversity stratum and
are pooled into a cross-corpus fault library (reference §3.1).

---

## 8. Sampling plan & stratification (TARGET — fill "Captured/Passed" in §13)

| Stratum | Swing type | Calibration regime | Target n | Raw? | Sparse markup | Dense markup |
|---|---|---|---|---|---|---|
| Repeatability A (noise) | full | fixed within set (5 sets × 5) | **25** | subset | 5 | — |
| Repeatability B (re-mount) | full | recal each set (e.g. 4 sets × ~3) | **10** | — | 2 | — |
| Diversity (club × tempo) | full | per-session | **10** | subset | 5 | 2 |
| Stress (clutter / extreme ROM) | full | per-session | **5** | subset | 2 | 1 |
| **Core scored total** | full | — | **50** | **~12** | **~14** | **~3** |
| Partial / incomplete (robustness) | partial | per-session | **10** | — | — | — |
| Scripted faults (known-groups) | full + clean control | per-session | **≥ 10–15 / priority fault** | — | flag | — |

- **Raw subset ~12** (`saveRawFrames=ON`) is a *tag* on swings across repeatability/diversity/stress, for
  pixel-level shaft tuning.
- **Sparse markup 10–15** and **dense markup 2–3** are *tags* on subsets (§9), not extra swings.
- 3-sensor (slot C present) on a meaningful fraction (≥ ~⅓) of scored swings; rest 2-sensor.

---

## 9. Markup & annotation plan

Labelling tool: the **in-app Markup panel** (`PpMarkupPanel`; session toolbar View → "Markup"). Writes
`truth.json` into the swing dir (byte-compatible with `score.py`). `lab.py label` is the headless fallback.

### 9.1 Sparse (P-position) markup — the primary labelling mode
For each labelled swing, mark the **P-system P1–P10** and place the **club (grip → head) at each P**
(~10 points/swing, ~3 min). This concentrates the expensive absolute-truth labels at the biomechanically
meaningful positions; dense per-frame track sanity is already free via the Tier-1 invariants.
**Target: 10–15 swings**, spanning conditions (don't label only the easy ones).

### 9.2 Dense markup — the high-resolution insurance subset
**2–3 swings**, club placed every ~10th frame, as a continuous θ(t) reference that confirms sparse
P-sampling didn't miss between-point error. Use the lab's stride-stepping placement.

### 9.3 Inter-rater label reliability (label QC — publishable requirement)
The hand labels are themselves a *measurement* with error. **Re-label ~5 swings by a second, independent
labeller** and compute inter-rater agreement (ICC on placed θ; agreement on P-times). Report this — it
bounds how tight any Tier-3 accuracy claim can legitimately be (you cannot validate the pipeline tighter
than your own labels are reliable).

### 9.4 Blinding (mandatory)
The labeller must **not view the analyzer's output** (recovered track, phase events, metrics) for a swing
before/while labelling it. Label from raw video only. This avoids anchoring the "truth" to the estimate.

---

## 10. Data management, naming & provenance

- **One swing = one shot dir** (`Face-On.mp4` + optional `Down-the-Line.mp4` + `swing.json` + `thumb.jpg`;
  `.raw` sidecars for the raw subset). Never write into a swing dir except `truth.json` (markup) and the
  lab-owned `pose.json`.
- **Corpus root** outside the repo: `<SwingData>/corpus-1/`. Copy swing dirs here; `lab.py ingest` builds
  `corpus.json`.
- **`CORPUS.md` (required for blessing):** recording dates, golfer UUID + handedness, IMU config & rates,
  calibration provenance, raw-subset list, **the hypothesis register (O1–O6 + thresholds)**, **the
  Tune/Validation/Held-out partition** (reference §7), and a known-deviations list. `ingest` refuses to
  bless without it.
- **Partitioning:** assign each swing to Tune / Validation / Held-out **at blessing, before any analysis**,
  and record it. Held-out is touched once, at freeze.

---

## 11. Quality control & acceptance gates (run at the studio, per swing)

Use the proposed `lab.py` pre-flight validator (red/green table) **before** a swing counts. A swing
failing any **must** gate is **recaptured, not tuned around** — it is a *data failure* (reference §5.2).

**MUST (hard gates):**
- [ ] Session type = Wrist (1).
- [ ] Lead-forearm (A) + lead-hand (B) IMU connected, streaming, **inline samples in swing.json**.
- [ ] **`analysis.bindings[]` populated** and `calibrated: true` for each binding.
- [ ] `calibAgeSec` small (per §6.2).
- [ ] Analysis populated: `phases[]` includes **Impact (phase 5)**; metrics non-empty; score sane.
- [ ] IMU rate = 200 Hz; inter-arrival within tolerance; timestamps monotonic.
- [ ] Face-on perspective = 2 (if camera used); handedness set.

**SHOULD (raises the ceiling):**
- [ ] Upper-arm IMU (C) present → pronation + IMU-elbow.
- [ ] Raw frames on (raw subset).
- [ ] Audio captured.

---

## 12. Statistical analysis (this corpus's contribution)

Full plan in reference §3, §5, §6. Corpus 1 feeds: Tier-0 census (O1); invariant & cross-modal pass-rates
(O2); **test–retest ICC / SEM / MDC** from the repeatability strata (O3); **θ/head RMSE & Top-error** vs
labels (O4); **per-fault recall + FP** with Wilson CIs (O5); robustness pass/fail on partials (O6). Report
to GRRAS (reliability/agreement) and STROBE (design) conventions.

---

## 13. Completion tracking tables — *fill in as you progress*

### Table A — Capture progress tracker

| Stratum | Target n | Captured | Passed QC (Tier-0) | % complete | Notes |
|---|---|---|---|---|---|
| Repeatability A | 25 | | | | |
| Repeatability B (re-mount) | 10 | | | | |
| Diversity | 10 | | | | |
| Stress | 5 | | | | |
| **Core scored** | **50** | | | | |
| Partial / incomplete | 10 | | | | |
| Faults: open/closed face | 10–15 | | | | + matched clean control |
| Faults: cast | 10–15 | | | | + control |
| Faults: flip | 10–15 | | | | + control |
| Raw subset (tag) | ~12 | | | | |
| Sparse markup (tag) | 10–15 | | | | |
| Dense markup (tag) | 2–3 | | | | |
| Inter-rater re-label | 5 | | | | 2nd labeller |

### Table B — Session log

| Session | Date | Golfer UUID | Hand | Sensors (2/3) | Calibrations run | Confirm-Tracking checks | Swings captured | Notes / deviations |
|---|---|---|---|---|---|---|---|---|
| S01 | | | | | | | | |
| S02 | | | | | | | | |

### Table C — Per-swing QC & markup ledger

| Swing ID | Stratum | Full/Partial | Variant (club/tempo/bg/ROM) | Sensors | calibAgeSec | Tier-0 PASS? | Raw? | Markup (none/sparse/dense) | Labeller | Partition (T/V/H) | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|
| sw_0001 | | | | | | | | | | | |
| sw_0002 | | | | | | | | | | | |

### Table D — Acceptance roll-up (complete at freeze)

| Objective | Metric | Target | Observed | CI | Pass? |
|---|---|---|---|---|---|
| O1 capture | Tier-0 pass rate | 100 % | | | |
| O2 cross-modal | % swings corr ≥ 0.9 | ≥ 90 % | | | |
| O3 reliability | FE RMSE / RUD RMSE | < 5° / < 8° | | | |
| O3 indeterminate | % Indeterminate | < 2 % | | | |
| O4 track | theta_rms / head_median | < 3° / < 25 px | | | |
| O4 phase | Top error | ≤ 30 ms | | | |
| O5 known-groups | recall / FP per fault | high / ≈ 0 | | (Wilson) | |
| O6 robustness | partial-swing crashes / false impacts | 0 | | | |

---

## 14. Pilot results to carry into Corpus 2 (complete after the HackMotion pilot, §3.4)

| Axis | SD of IMU−HackMotion difference (`s`) | Implied Corpus-2 n for LoA ±0.5 SD (`(3.4/0.5)²·…` → ~50) | Notes |
|---|---|---|---|
| FE | | | |
| RUD | | | (expected loosest) |
| PS | | | (single- vs two-sensor caveat) |
| Temporal lag (mean ± SD) | | | sync-method check |

---

## 15. Deviations & risk log

Record every protocol deviation (e.g., a recalibration skipped, a sensor slip mid-set, a mislabelled
perspective) with swing IDs affected and the disposition (recaptured / excluded / kept-with-flag). An
honest deviation log is a publication requirement, not an admission of failure.

---

## 16. Roles & sign-off

| Role | Name | Responsibility |
|---|---|---|
| Capture lead | | runs sessions, owns calibration discipline & Tables A–C |
| Labeller (primary) | | sparse + dense markup (blinded) |
| Labeller (secondary) | | inter-rater re-label subset |
| Analyst | | ingest, baseline run, Table D roll-up |
| Sign-off | | blesses `CORPUS.md`, declares Corpus 1 frozen |

---

## References

Kottner J et al. *Guidelines for Reporting Reliability and Agreement Studies (GRRAS).* J Clin Epidemiol
2011. · von Elm E et al. *STROBE statement.* 2007. · Bland JM, Altman DG. *Statistical methods for
assessing agreement.* Lancet 1986; Stat Methods Med Res 1999. · Hopkins WG. *Measures of reliability in
sports medicine and science.* Sports Med 2000. · Koo TK, Li MY. *Guideline for selecting and reporting
ICC.* J Chiropr Med 2016. · de Vet HCW et al. *Measurement in Medicine.* 2011 (SEM/MDC). · Wu G et al.
*ISB recommendation… wrist & hand.* J Biomech 2005. · Ryu J et al. *Functional ranges of motion of the
wrist joint.* J Hand Surg 1991.
