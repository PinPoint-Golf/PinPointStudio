# Corpus 3 — Data Collection Protocol (Calibrated cameras + IMU + HackMotion verification, multi-golfer)

**Document type:** Experimental methods / data-collection protocol (collection team).
**Corpus:** 3 of 3 — *full calibration + multi-golfer; **external validity**, the camera-metric branch,
and HackMotion verification.*
**Version:** 1.0 (2026-06-26) · **Status:** later (after Corpus 2 freeze) · **Owner:** Mark Liversedge.
**Reference (the "why"):** `docs/validation/pipeline_validation_and_tuning.md` (§3.5 between-subject N;
§5.4 camera-metric branch; §8 coach/outcome markup). **Camera calibration method:**
`docs/design/shot_analyzer_design.md` §0 (ChArUco intrinsics + single-camera ground-plane extrinsic).
**Reporting standard:** GRRAS + Bland–Altman + **STROBE for the multi-subject observational design**; if a
launch monitor is used, report the predictive analysis per TRIPOD-style conventions.

> **What the collection team must produce:** a multi-golfer dataset in which cameras are **metrically
> calibrated** (ChArUco intrinsics + ground-plane extrinsic), IMUs are fully characterised, HackMotion
> runs as an ongoing **verification** check, and — across **several golfers** — the pipeline can finally be
> tested for **generalisation beyond one person**, the camera-derived metrics (sway / lift / tilt) can be
> validated, and (if a launch monitor is present) faults can be tested against ball-flight outcomes.

---

## 0. Changes from Corpus 2 (read first)

Corpus 3 **is Corpus 2's protocol, plus camera calibration, full IMU characterisation, multiple golfers,
and HackMotion repositioned as verification.** Everything in `corpus1_` and `corpus2_collection_protocol.md`
still applies as a regression net (Wrist session, IMU + HackMotion co-mount, Tier-0 gate, sparse/dense
markup, reconciliation). The **deltas:**

| Area | Corpus 2 | Corpus 3 (new) |
|---|---|---|
| Subjects | one golfer (within-subject) | **8–12 golfers (between-subject → external validity)** |
| Cameras | optional, uncalibrated | **ChArUco-calibrated** (intrinsics + ground-plane extrinsic) |
| Camera metrics | not validated | **sway / lift / secondary-tilt (Mono3D+IMU) validated** |
| IMU calibration | per-session gate | **fully characterised** (calibration repeatability across golfers/mounts) |
| HackMotion | the criterion study | **ongoing verification** (confirms Corpus-2 agreement holds across golfers) |
| Diagnosis arbiter | none (synthetic faults) | **coach annotation** (`coach.json`) → κ / ICC |
| Outcomes | none | **launch monitor** (`outcome.json`) → predictive validity *(if available)* |
| Validity earned | within-subject criterion | **external validity + camera branch + (predictive)** |

---

## 1. Study identification & scope

| Field | Value |
|---|---|
| Corpus ID | `corpus-3` |
| Instruments | calibrated face-on (+ DTL) cameras **+** PinPoint IMUs **+** HackMotion **+** (optional) launch monitor |
| Primary unit | one swing, **nested within golfer** (clustered data) |
| Validity earned | **external (between-subject)**, camera-metric, HackMotion verification, diagnosis κ/ICC, (predictive) |
| Validity NOT earned | anything the instruments still can't measure (e.g. club-face without a club device) |

---

## 2. Objectives, hypotheses & pre-registered acceptance

| # | Objective | Pre-registered acceptance |
|---|---|---|
| X-O1 | Wrist agreement **generalises across golfers** | Corpus-2 LoA hold per-golfer; between-golfer LoA reported with design-effect-adjusted CI |
| X-O2 | Camera calibration is metric | ChArUco reproj RMS **< 0.5 px**; wand-length QA **within 0.5 %** |
| X-O3 | Camera-derived translations are valid | sway/lift/secondary-tilt plausible + repeatable; cross-checked vs DTL where present |
| X-O4 | IMU calibration is repeatable across mounts/golfers | mount-gate pass rate; calibration SEM across re-dons |
| X-O5 | HackMotion verification holds | spot-check agreement within Corpus-2 LoA on a per-golfer subset |
| X-O6 | Diagnosis agrees with a coach | Cohen's κ (fault presence) and ICC (0–100 score) vs blinded coach |
| X-O7 *(if LM)* | Faults predict outcomes | flagged fault ↔ measured ball-flight association (e.g. open face → push/slice) |

---

## 3. Study design

- **Design:** prospective, **multi-subject** method-comparison + reliability + (optional) predictive
  study. Data are **clustered** (swings within golfers).
- **Subjects:** **8–12 golfers** spanning handedness, handicap, and body type (record all). This is the
  *minimum* for a usable between-subject variance estimate; the **effective N for any population claim is
  the number of golfers, not swings** (reference §3.5).
- **Clustering / design effect:** repeated swings within a golfer are correlated; the variance inflation is
  `≈ 1 + (m−1)·ρ` (m swings/golfer, ρ intra-golfer correlation). Account for it in the analysis (mixed
  models / cluster-robust CIs) — **do not treat `golfers × swings` as independent observations.**
- **Per-golfer load:** ~12–15 full swings spanning conditions → **~100–180 swings total**. Keep each
  golfer's session short enough to maintain calibration discipline.
- **Blinding:** labeller blinded to analyzer output (as before); **coach blinded** to analyzer output —
  the coach annotates from video only (§9.3).

---

## 4. Participants: inclusion, consent, recruitment

- **Inclusion:** golfers who can complete a full swing; aim for a spread of handicap (e.g. low ≤ 5, mid
  ~10–18, high ≥ 20) and **both handedness** to exercise the mirror path.
- **Exclusion:** injury precluding full ROM; inability to consent.
- **Consent & ethics:** informed consent for video + IMU + HackMotion + (if used) launch-monitor capture,
  and for a **coach to review their swings**; stored under athlete UUID; right to withdraw. Multi-subject
  capture raises the ethics bar — ensure approvals are in place before recruiting.
- **Sample-size rationale:** **golfers, not swings** (reference §3.5). 8–12 golfers is the practical floor;
  per-golfer agreement still follows the Corpus-2 LoA logic (≥ 40 paired *instances* per axis pooled), and
  camera-metric agreement needs ≥ ~40 paired camera-vs-reference instances.

---

## 5. Equipment & instrumentation

As Corpus 2 (§5), **plus:**

| Item | Spec / setting | Note |
|---|---|---|
| ChArUco board | e.g. 7×10 squares; **measure the printed square with calipers** (never trust print scale) | OpenCV ≥ 4.7 unified API |
| Ground-plane board | one large ChArUco laid flat at the hitting spot | single-frame extrinsic (IPPE) |
| Calibration wand | known length | QA gate (within 0.5 %) |
| Camera mounting | rigid, fixed; set `cameraFixedInPlace` | re-calibrate if moved |
| Launch monitor *(optional)* | per device | `outcome.json` source |
| Coach station | video review + annotation | blinded to analyzer output |

---

## 6. Setup & calibration protocol

### 6.1 Camera calibration (per camera, per fixed setup) — the new pre-flight
1. **Intrinsics (ChArUco):** capture **15–30 board views** spanning all four image corners and a range of
   tilts/distances; run `calibrateCamera`; **gate reproj RMS < 0.5 px** (re-shoot otherwise). Persist `K`,
   `dist` to `AppSettings::cameraIntrinsics` (keyed `description|serial`).
2. **Ground-plane extrinsic:** lay one large ChArUco flat at the hitting spot, grab **one** frame, run
   `solvePnP(SOLVEPNP_IPPE)`. The board frame is the world frame; `t` is metric (object points in metres).
   Persist to `cameraExtrinsics`.
3. **QA:** measure a **wand of known length within ~0.5 %**; record it.
4. Set **`cameraFixedInPlace`**; if a camera is bumped/moved, **recalibrate** that camera.
5. *(Two cameras)* solve each extrinsic from the **same shared board** (board = world); do **not** use
   `stereoCalibrate` rectification (degenerate at ~90° convergence).

Camera calibration is a **per-setup census gate**, not per-swing: every golfer's swings inherit the
setup's calibration, so calibration QC is logged once per setup (Table B) and re-checked if the rig moves.

### 6.2 IMU + HackMotion calibration
Inherit Corpus 1 §6 (PinPoint 2-pose + cadence) and Corpus 2 §6 (HackMotion zero, perturbation check, sync
action, static poses). **Per golfer:** full calibration at the start of each golfer's session; the
**re-mount sub-set** (Regime B) is captured across golfers to characterise calibration repeatability
between *people* and *mounts* (X-O4), not just within one person.

### 6.3 Calibration cadence (multi-golfer)
Per Corpus 1 §6.2 within each golfer's session, **plus** a full re-calibration at every golfer change
(every new donning is a new mount). Log calibration events per golfer in Table B.

---

## 7. Data collection protocol

### 7.1 Per-golfer swing set
Each golfer performs ~12–15 **full** paired swings spanning: **club** (≥ 2), **tempo**, and **wrist ROM**.
Keep the per-golfer set efficient — calibration drift over a long session is a bigger risk than a few
extra swings.

### 7.2 Conditions to span across the cohort
Stratify *across golfers*: handedness, handicap band, body type, club, tempo. The **between-golfer spread
is the point** — a corpus of 10 similar golfers does not test generalisation.

### 7.3 Camera-metric validation captures
For the camera-derived translations (sway / lift / secondary-tilt), ensure the golfer is **fully in frame**
with the **ground-plane calibration valid**. Where a DTL camera is present, its in-plane view of the
toward/away axis cross-checks the face-on depth estimate (reference §5.4). Tag these swings.

### 7.4 HackMotion verification (not the primary study here)
HackMotion is co-mounted on a **per-golfer subset** (not necessarily every swing) to **verify** that the
Corpus-2 agreement holds across people (X-O5). Reconcile as Corpus 2 §7.4. If agreement degrades for a
new body type, that is a finding (possible mount/`A·M` sensitivity).

### 7.5 Scripted faults & coach annotation
Capture scripted faults per golfer where feasible (pooled into the cross-corpus fault library). **Coach
annotation** (§9.3) is the new diagnosis arbiter.

### 7.6 Outcomes *(if a launch monitor is available)*
Record ball-flight (carry, face-to-path, launch, etc.) per shot into `outcome.json`. This enables the
first **predictive-validity** test (does a flagged open face predict the measured face/path?).

---

## 8. Sampling plan & stratification (TARGET — fill in §13)

| Stratum | Unit | Target | Markup |
|---|---|---|---|
| Golfers (cohort) | golfer | **8–12** (spread of handedness/handicap/body type) | — |
| Per-golfer swings | full paired | ~12–15 each → **~100–180 total** | subset sparse |
| Camera-metric swings | full, fully-in-frame, calib valid | ≥ 40 (pooled, for camera-metric agreement) | some dense |
| HackMotion-verified | paired subset per golfer | ≥ 40 paired instances pooled (per axis) | — |
| Sparse markup | swings | 10–15 **spanning golfers** | P1–P10 + club |
| Dense markup | swings | 2–3 (≥ 2 different golfers) | every ~10th frame |
| Inter-rater label | swings | 5 | 2nd labeller |
| Coach annotation | swings | 30–50 (across golfers) | `coach.json` |
| Outcomes *(if LM)* | shots | as many as captured | `outcome.json` |
| Camera calibration | per setup | every fixed setup | reproj RMS + wand QA |

---

## 9. Markup & annotation plan

### 9.1 Hand labels (`truth.json`)
Inherit Corpus 1/2: sparse P-positions (10–15 swings **spanning golfers**, not all from one), dense (2–3,
≥ 2 golfers), inter-rater re-label (5), **labeller blinded**.

### 9.2 HackMotion (`hackmotion.json`)
Export + reconcile on the verification subset (Corpus 2 §7.4). Pairing/sync QC as before.

### 9.3 Coach annotation (`coach.json`) — the diagnosis arbiter (new)
A qualified coach reviews each annotated swing's **video only (blinded to the analyzer output)** and
records: named faults present (from the F1–F8 vocabulary + free text), a 0–100 quality score, and
confidence. This is the diagnosis layer's **only external arbiter** — today it is validated against
synthetic faults only (reference §5.6, §8.1). For inter-rater coaching reliability, have **≥ 2 coaches**
annotate an overlapping subset.

### 9.4 Outcomes (`outcome.json`) — predictive validity (if LM)
Launch-monitor ball-flight per shot, matched to the swing dir. Enables the fault-vs-outcome contingency
analysis (X-O7).

---

## 10. Data management, naming & provenance

As Corpus 2 §10, corpus root `<SwingData>/corpus-3/`. **`CORPUS.md` additionally records:** the cohort
(per-golfer UUID, handedness, handicap, body metrics), the camera calibration provenance per setup
(board spec, measured square, reproj RMS, wand QA), the coach(es) and their blinding, and the
launch-monitor model (if used). New sidecars: `coach.json`, `outcome.json` (additive; `Swing.*()`
accessors + `ingest` flags + `score.py` groups + synth stamps, per the lab's reader contract).

---

## 11. Quality control & acceptance gates

Inherit Corpus 1 §11 (Tier-0) and Corpus 2 §11 (paired-capture), **plus:**

**MUST (additional):**
- [ ] Camera calibration valid for the setup (reproj RMS < 0.5 px; extrinsic solved; wand QA within 0.5 %).
- [ ] `cameraFixedInPlace` set; not moved since calibration (else recalibrate).
- [ ] Golfer demographics + handedness recorded; calibration done at golfer change.
- [ ] Coach annotation done **blinded** (if this swing is in the coach subset).

**SHOULD:**
- [ ] DTL camera present for camera-metric cross-check swings.
- [ ] Launch-monitor outcome captured.

---

## 12. Statistical analysis (this corpus's contribution)

- **Generalisation (X-O1):** per-golfer agreement vs the Corpus-2 LoA; **between-golfer** LoA / ICC with
  **mixed-effects models or cluster-robust CIs** (golfer = random effect) — never pooled-as-independent.
- **Camera metrics (X-O3):** plausibility + repeatability of sway/lift/tilt; face-on-vs-DTL agreement
  (Bland–Altman) where both present.
- **IMU calibration (X-O4):** mount-gate pass rate and calibration SEM across re-dons/golfers.
- **HackMotion verification (X-O5):** spot-check agreement within Corpus-2 LoA per golfer.
- **Diagnosis (X-O6):** **Cohen's κ** (fault presence) and **ICC** (score) vs blinded coach; inter-coach κ
  on the overlap subset.
- **Predictive (X-O7, if LM):** fault-vs-outcome association (e.g. logistic / contingency), reported with
  the clustering accounted for.

---

## 13. Completion tracking tables — *fill in as you progress*

### Table A — Cohort & per-golfer progress

| Golfer UUID | Hand | Handicap band | Body type/height | Sensors (2/3) | HackMotion subset? | Swings captured | Passed QC | Coach-annotated? | Notes |
|---|---|---|---|---|---|---|---|---|---|
| G01 | | | | | | | | | |
| G02 | | | | | | | | | |
| … (target 8–12) | | | | | | | | | |

### Table B — Camera calibration log (per setup)

| Setup ID | Date | Camera (desc\|serial) | Board (sq size mm) | Views | Reproj RMS (px) | Extrinsic solved? | Wand QA (%) | Fixed-in-place? | Notes |
|---|---|---|---|---|---|---|---|---|---|
| SET01 | | face-on | | | | | | | |
| SET01 | | DTL | | | | | | | |

### Table C — Capture progress tracker

| Stratum | Target | Captured | Passed QC | % complete | Notes |
|---|---|---|---|---|---|
| Golfers | 8–12 | | | | spread of hand/handicap |
| Total swings | ~100–180 | | | | |
| Camera-metric swings | ≥ 40 | | | | fully-in-frame, calib valid |
| HackMotion-verified (paired) | ≥ 40/axis | | | | |
| Sparse markup | 10–15 | | | | span golfers |
| Dense markup | 2–3 | | | | ≥ 2 golfers |
| Coach annotation | 30–50 | | | | blinded |
| Outcomes (LM) | — | | | | if available |

### Table D — Per-swing QC ledger

| Swing ID | Golfer | Setup (camera calib) | Full/Partial | HackMotion? | Camera-metric? | Tier-0 + paired PASS? | Markup | Coach? | Outcome? | Partition (T/V/H) | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|
| sw_0001 | | | | | | | | | | | |

### Table E — Generalisation & diagnosis roll-up (at freeze)

| Objective | Metric | Target | Observed (design-effect-adjusted) | Pass? |
|---|---|---|---|---|
| X-O1 generalisation | between-golfer LoA / ICC (FE/RUD/PS) | within Corpus-2 LoA | | |
| X-O2 camera calib | reproj RMS / wand QA | < 0.5 px / 0.5 % | | |
| X-O3 camera metrics | sway/lift/tilt repeatability + face-on↔DTL | plausible + agree | | |
| X-O4 IMU calib | mount-gate pass / calib SEM | high / small | | |
| X-O5 HM verification | per-golfer agreement | within Corpus-2 LoA | | |
| X-O6 diagnosis | κ (fault) / ICC (score) vs coach | substantial / high | | |
| X-O7 predictive (if LM) | fault ↔ outcome association | significant, sensible direction | | |

---

## 14. Deviations & risk log

As Corpus 1/2, plus: camera bumped/recalibrated mid-session, a golfer's session abandoned, a coach
un-blinded by accident (exclude those annotations), launch-monitor dropouts. Record golfer/swing IDs and
dispositions.

---

## 15. Roles & sign-off

As Corpus 2, plus: a **recruitment/ethics lead** (cohort, consent), a **camera-calibration technician**
(Table B), and the **coach(es)** (blinded annotation). Sign-off blesses `corpus-3/CORPUS.md` and declares
the external-validity study complete.

---

## References

As Corpus 1 & 2, plus: von Elm E et al. *STROBE statement* (multi-subject observational). · OpenCV ChArUco
calibration (unified `aruco` API, ≥ 4.7). · Collins GS et al. *TRIPOD statement* (if predictive modelling
from launch-monitor outcomes). · Landis JR, Koch GG. *The measurement of observer agreement for
categorical data* (κ interpretation), Biometrics 1977.
