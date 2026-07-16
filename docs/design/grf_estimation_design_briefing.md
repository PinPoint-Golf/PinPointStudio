# Estimating Ground Reaction Forces Without Force Plates
### A science-grounded briefing for the PinPoint Studio product team

**Date:** July 2026
**Scope:** Feasibility, current state of the art, and product implications of estimating ground reaction forces (GRF), centre of pressure (CoP) and their derived coaching metrics in the golf swing without a force or pressure plate — i.e. from pose, physics, and/or the IMU streams PinPoint already ingests.

---

## 1. Executive summary

**The opportunity is real and the science now supports it — but with important caveats about which quantities are trustworthy and which are not.**

- GRF is the coaching quantity behind everything "ground reaction" vendors (Swing Catalyst, BodiTrak/TrackMan) sell. It is *invisible* — you cannot see it in video — and today it is measured only with force/pressure plates costing thousands. That is exactly the kind of "measure the cause, not just the effect" gap PinPoint can attack.
- Three independent research lines now estimate GRF without a plate: **(A) IMU + deep learning**, **(B) markerless video → pose → physics/ML**, and **(C) hybrid pose+IMU fusion**. All three are directly compatible with PinPoint's existing architecture (multi-camera vision pipeline + WT9011DCL BLE IMU + `ImuVisionFuser`).
- **What is achievable now, honestly:** the *vertical* GRF component and the *shape/timing* of the CoP trace and force-peak sequence are recoverable to coaching-useful accuracy. The horizontal (shear) components and absolute CoP magnitude are materially harder and lower-confidence.
- **Recommended framing:** position this as **"GRF-informed coaching cues" — kinetic *sequence* and *pattern* detection — not "virtual force plate" absolute-Newton claims.** The literature strongly supports the former and is honest about the limits of the latter. This protects credibility, which is PinPoint's differentiator.

---

## 2. Why GRF matters in golf (the coaching demand)

Ground reaction force is the reaction the ground exerts on the golfer's feet; by Newton's third law it mirrors the net force the golfer applies to the ground. It has three orthogonal components plus a free vertical torque:

| Component | Coaching name | Role in the swing |
|---|---|---|
| Vertical (Z) | "Vertical force" / push | Weight transfer, lead-leg bracing, "jump" for speed |
| Antero-posterior (Y) | part of "horizontal/shear" | Braking & propulsion timing |
| Medio-lateral (X) | "lateral force" | Pressure shift trail→lead |
| Vertical free moment | "torque" | Rotational drive against the ground |

The commercial coaching model that has taken hold — popularised by Swing Catalyst — is the **Kinetic Sequence**: in the downswing the force peaks occur in a consistent order across golfers. Swing Catalyst's account is that **horizontal (back/front) force peaks first, torque second, and vertical force third**, and that this ordering is remarkably consistent regardless of player. GolfWRX's coaching material similarly frames the useful outputs as *which forces exist, in what order, and whether their peaks fall in the correct swing windows* — i.e. **sequence and timing, not just raw magnitude**.

**Product-relevant consequence:** the highest-value coaching outputs are *relative and temporal* (ordering, timing windows, symmetry, CoP trace shape). These are precisely the outputs that force-plate-free estimation is *best* at, because normalisation and shape are more robust than absolute Newtons. This is a fortunate alignment.

**Important honesty point for marketing:** pressure mats (e.g. BodiTrak) fundamentally measure *vertical* pressure distribution; true 3D shear/torque needs *force* plates. Vendors openly distinguish "force" from "pressure." Any PinPoint claim should be at least as careful as the incumbents about what is measured vs. inferred.

---

## 3. The three technical approaches

### Approach A — IMU + deep learning (closest to PinPoint's current stack)

Body-worn IMUs measure the linear accelerations and angular velocities of segments; because F = ma, distal-segment kinematics carry GRF information, and a learned model maps IMU-derived features → GRF.

**Golf-specific, state of the art (Li et al., *Biomimetics* 2026):** this is the most directly relevant paper in existence for PinPoint. 48 professional golfers, 7 Xsens IMUs (feet, shanks, thighs, pelvis) at 60 Hz, Kistler plates as ground truth, subject-level 10-fold cross-validation. Key findings:

- Best model **TCN-BiGRU** reached **R² = 0.94 ± 0.02, NRMSE = 0.064, MRE = 0.044** for 3D GRF.
- **Vertical (Z) is predicted best** (R² ≈ 0.95, NRMSE ≈ 0.06); **medio-lateral moderate; antero-posterior worst** (R² ≈ 0.92 but far lower signal-to-noise). This direction hierarchy recurs in *every* study below.
- **A lead-leg-only 4-IMU set matched the full bilateral 7-IMU set.** This is a major practical result: you don't need to instrument both legs.
- Architecture matters: bidirectional temporal models (BiGRU/BiLSTM) beat unidirectional and beat CNN/MLP, because the backswing→downswing coupling is non-periodic and needs future context.

**Golf-specific, motion-capture-driven (Mori & Kwon, ISBS 2025):** Bi-LSTM trained on ~1,000 swings from ~340 golfers, validated on 29 unseen golfers. **ICC up to 0.985, nRMSE 3.81–7.88%, best for lead-foot vertical GRF.** Confirms the golf swing is learnable at scale and that the lead-foot vertical channel is the most reliable.

**General-movement context (for calibrating expectations):** across running/gait/jumping, IMU+DL vertical-GRF errors cluster at **~6–9% NRMSE** in the best configurations (Lee 2020 ~6.7%; Alcantara 2022 ~6.4%; Yılmazgün 2025 ~6.2%). Golf sits in the same band. Horizontal components are consistently ~2–3× worse in relative terms.

**Known failure modes to design around:**
- **60 Hz IMU is marginal at impact.** The Li et al. authors explicitly flag that 60 Hz may miss high-frequency transients near impact. PinPoint's roadmap already targets higher-rate IMUs (ICM-42688-P). Impact-window forces will be the least reliable region regardless.
- **Soft-tissue artefact under impulsive load** degrades signal in the downswing — a documented, sensor-design-dependent error.
- **Purely data-driven models don't respect physics** (predicted forces need not satisfy F=ma with the observed motion) and extrapolate poorly outside the training distribution (all golf models to date are trained on *low-handicap* golfers — generalisation to amateurs is unproven).

### Approach B — Markerless video → pose → physics or ML

This is the "pose inference + biomechanics" route the brief asks about, and the one that needs no wearable at all.

**Two sub-families:**

1. **Physics/musculoskeletal simulation (OpenCap lineage).** Video → 3D pose → OpenSim musculoskeletal model → GRF via dynamics/contact modelling.
   - The landmark validation (Uhlrich, Falisse, Delp et al., *PLOS Comp Biol* 2023): two+ smartphones, **GRF error ~6.7% body weight**, joint-moment error ~1.3% BW·height, kinematics ~4°. Comparable to lab markerless systems.
   - **OpenCap Monocular (2026)** now does this from a *single* static smartphone video, reporting GRF accuracy "comparable to or better than" the earlier two-camera system — but **validated only for walking, squatting, sit-to-stand.** Not golf. Not high-speed rotation.

2. **Learned pose→GRF (no explicit sim).**
   - **GRF-MV (Birmingham, 2024):** monocular video → 3D mesh (HybrIK-XL) → physics-refined contact → GRF, evaluated on the GroundLink dataset. Single ordinary camera; aimed at sports/clinic/home.
   - **UnderPressure (2022):** deep foot-contact + vGRF distribution from motion, widely used as a component.
   - **GaitDynamics foundation model (2025):** trained on the large AddBiomechanics corpus, predicts vertical GRF at **3.9% BW error** from marker kinematics — vs. **12.8% BW** for physics-sim on *video-derived* kinematics. The gap quantifies how much error the *video pose* stage injects, and points to the emerging hybrid (ML forces + physics consistency) as the winning recipe.
   - **Frontier signal (June 2026):** "From Pixels to Newtons" shows self-supervised video world models (V-JEPA 2) disambiguating activity context for force inference from a single uncalibrated camera — early, but the trajectory is toward less setup and more generalisation.

**Directly analogous precedent — other sports:** single-leg-landing vGRF from 2D video + pose AI (Ishida et al. 2024) matched 3D mocap (peak-vGRF absolute error ~0.20 BW; Pearson R ≈ 0.84). And **BadmintonGRF (2026)** is now a public multimodal benchmark for *markerless GRF in a fast racket sport* — the closest methodological cousin to a golf effort, and a template for how PinPoint could build and publish its own dataset.

**Known failure modes to design around:**
- **Monocular depth/scale ambiguity** and foot-ground contact "float/slide" are the core error sources; methods like HuMoR/contact constraints exist specifically to fix them. PinPoint's *multi-camera* rig is a real advantage here — it largely removes the single-view scale problem that limits smartphone approaches.
- **No golf validation exists** for any video→GRF system. High-speed axial rotation and a near-stationary base of support are out-of-distribution for gait-trained models.
- **Frame rate:** video-pose GRF for a swing needs the same ≥100–150 fps regime PinPoint already runs for shaft tracking; standard 30–60 fps smartphone pipelines will alias the downswing.

### Approach C — Hybrid pose + IMU fusion

Fuse the complementary error profiles: cameras give drift-free global geometry and CoP location; IMUs give high-bandwidth acceleration for the vertical impulse. This is conceptually where PinPoint is *uniquely* positioned, because it already has both modalities and an `ImuVisionFuser` scaffold. No published golf-specific pose+IMU GRF system exists yet — this is open territory, i.e. a potential PinPoint first.

---

## 4. What is trustworthy vs. not (the honesty table)

| Output | Confidence force-plate-free | Basis |
|---|---|---|
| **Vertical GRF waveform (lead foot)** | **High** | Consistent R²≈0.95 / NRMSE≈6% across golf + general studies |
| **Kinetic sequence ordering (H→torque→V peaks)** | **High** | Timing/ordering is normalisation-robust; the coaching output that matters most |
| **Weight-transfer / CoP trace shape & timing** | **Medium–High** | CoM/CoP shape recovers well; trace *pattern* classification (linear, fish-hook, etc.) is feasible |
| **Peak-timing within swing windows** | **Medium–High** | Depends on frame rate; needs ≥100 fps |
| **Medio-lateral (lateral) force** | **Medium** | Style-dependent, lower SNR |
| **Antero-posterior (shear) force** | **Low–Medium** | Smallest magnitude, worst SNR everywhere |
| **Absolute CoP magnitude & vertical free torque** | **Low** | Needs true force plate; pressure mats themselves struggle here |
| **Impact-instant forces** | **Low** | High-frequency transient; 60 Hz IMU / sub-100 fps video both alias |

**Design rule:** surface high-confidence outputs as *quantitative*, medium as *qualitative/relative with uncertainty bands*, low as *not offered* (or explicitly labelled experimental). Attaching per-output confidence is itself a differentiator — it's what a credibility-led product does that the "virtual force plate" hype-marketers don't.

---

## 5. Competitive & positioning context

- **Incumbents** (Swing Catalyst Dual Motion/Pressure Plates, BodiTrak, TrackMan-linked systems) own the "measured GRF" high ground but require a **hardware plate** and are priced accordingly. Their own literature carefully separates *force* (3D, plates) from *pressure* (vertical, mats) — a distinction PinPoint should mirror, not blur.
- **The wedge:** PinPoint can offer *GRF-informed coaching* — kinetic-sequence ordering, weight-transfer timing, CoP-trace pattern recognition, symmetry — from cameras (± the IMU already in the box), at zero marginal hardware cost, integrated with the shaft/ball/pose pipeline already validated to sub-degree accuracy.
- **Credibility is the moat.** The research is unanimous that horizontal/absolute quantities are hard. A product that *over-claims* here will be caught by exactly the coaches (force-plate owners) who can compare. A product that ships *honest, well-bounded* estimates with visible uncertainty will earn trust and can tighten claims as validation data accrues.

---

## 6. Fit with PinPoint's existing architecture

| PinPoint asset | Leverage for GRF |
|---|---|
| Multi-camera calibrated rig (face-on + DTL, 150 fps, global shutter) | Removes monocular scale ambiguity; supports ≥100 fps swing capture the video-GRF route needs |
| ViTPose pose pipeline (2D→lifted 3D) | Direct front-end for both video-physics and learned-GRF routes |
| WT9011DCL IMU + BLE integration | Ready input for Approach A (note 60 Hz-class limits at impact) |
| `ImuVisionFuser`, clock-bias/P1 work | The synchronisation backbone a fusion (Approach C) system requires; heel-contact/event sync is a solved pattern (OpenCap event-sync validation 2025) |
| Kalman/RTS smoothing already in shaft/head pipelines | Same smoother scheduling applies to GRF waveforms |
| Statistical validation methodology (RMSE/MAE/Bland-Altman/LOOCV) already in use | Exactly the toolkit needed to validate a GRF module against a borrowed/rented plate |

**Notable gap:** every credible system is trained against **force-plate ground truth**. PinPoint has none. A validation dataset (even a modest one — rent a Swing Catalyst/BodiTrak, or partner with a lab) is the single highest-leverage prerequisite. The golf models above used 1,000+ and ~500 swings respectively; the video/active-learning precedent (300–500 frames) suggests a few hundred *plate-synchronised* swings could bootstrap a lead-foot-vertical model.

---

## 7. Recommended approach & phasing

**Phase 0 — Ground truth (blocker for everything).** Acquire ≥ a few hundred swings with synchronised plate GRF + PinPoint cameras + IMU, spanning handicaps (the pro-only training bias is the field's biggest generalisation risk). Reuse existing sync (clock-bias/P1) and validation (Bland-Altman/LOOCV) work.

**Phase 1 — Vertical GRF + kinetic sequence (highest confidence, highest coaching value).** Lead-foot vertical waveform + peak-ordering + weight-transfer timing. Start with **Approach A** (IMU→BiGRU/TCN, lead-leg-only per Li et al.) since it reuses the current IMU path and has the strongest golf evidence. Ship with explicit uncertainty bands.

**Phase 2 — CoP trace & pattern recognition from vision.** Add the camera-derived CoM/CoP shape and trace-pattern classification (linear / fish-hook / Z, etc. — established coaching vocabulary). Multi-camera rig is the advantage here.

**Phase 3 — Fusion (Approach C) for the differentiated product.** Combine vision geometry + IMU impulse via the `ImuVisionFuser`; target improved vertical accuracy and *cautious* medio-lateral estimates. This is publishable and defensible — no one has shipped golf pose+IMU GRF.

**Throughout:** treat horizontal shear, absolute CoP, vertical torque, and impact-instant forces as **research/experimental**, never headline claims, until validation says otherwise.

---

## 8. Key references (grounding)

- **Li, Wei, Xie, Wu, Kim (2026).** *Prediction of 3D GRF in the Golf Swing Using Wearable IMUs and Biomimetic DL.* Biomimetics 11(3):159. — golf, IMU→TCN-BiGRU, R²=0.94; lead-leg-only ≈ full set; direction hierarchy.
- **Mori & Kwon (2025).** *Estimation of GRF During Golf Swing Using RNNs.* ISBS Proc. 43:60. — golf, mocap→Bi-LSTM, ICC≤0.985, nRMSE 3.81–7.88%.
- **Uhlrich, Falisse, Delp et al. (2023).** *OpenCap.* PLOS Comp Biol 19(10). — video→musculoskeletal GRF ~6.7% BW; **+ OpenCap Monocular (2026)** single-camera, gait/squat/STS only.
- **Katsu, Dasgupta, Chang (2024).** *GRF-MV: GRF from Monocular Video.* BMVC ANIMA. — single-camera mesh+physics on GroundLink.
- **GaitDynamics / hybrid ML+sim (2025).** vGRF 3.9% BW (marker) vs 12.8% BW (video-sim) — quantifies the pose-stage error and motivates hybrid.
- **Ishida et al. (2024)** single-leg vGRF from 2D video+pose (R≈0.84); **BadmintonGRF (2026)** markerless-GRF benchmark for a fast racket sport — methodological template.
- **Swing Catalyst / GolfWRX / BodiTrak** coaching literature — the kinetic-sequence framing and the force-vs-pressure distinction that should govern PinPoint's claims.

---

*Prepared for internal PinPoint Studio product planning. Accuracy figures are quoted from the cited peer-reviewed and proceedings sources; all cross-study comparisons should be read with the usual caution that datasets, populations (predominantly low-handicap golfers), and normalisation conventions differ.*
