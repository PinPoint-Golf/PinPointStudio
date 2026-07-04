# Best Practices for Indoor Golf Motion Capture

A practical reference for building and validating an indoor golf swing capture system, grounded in the biomechanics and motion-capture literature. The emphasis is on the decisions that actually determine data quality: modality choice, sampling rate, camera geometry, synchronisation, signal processing, and honest error reporting.

The golf swing is an unusually demanding target. It is a whole-body, asymmetric, high-velocity rotational movement in which the quantity of interest (clubhead speed at impact) lives at the fast, distal end of an open kinematic chain, while the biomechanics that produce it (pelvis and thorax rotation, weight transfer) live at the slow, proximal end. A single sampling rate or filter setting rarely serves both ends well, and most of the recommendations below flow from that tension.

---

## 1. Choosing a capture modality

No single modality captures the whole swing well. Marker-based optical systems remain the reference standard, but each alternative trades accuracy for accessibility in a different way. The systematic review by Bernardina et al. (methodological recommendations for golf kinematics) found the field overwhelmingly relies on laboratory optoelectronic systems running at 100–300 fps, with the driver as the most common club and clubhead speed at impact as the dominant performance indicator.

| Modality | Reference accuracy | Strengths | Key limitations |
|---|---|---|---|
| **Marker-based optical** (Vicon, OptiTrack, Motion Analysis) | Sub-mm marker residual (~1.0–1.2 mm typical) | Gold standard; high sampling rates; full 3D | Cost, calibration burden, soft-tissue artefact, marker occlusion, indoor-only |
| **Markerless multi-view** (OpenCap, OpenPose-based) | Sagittal joint angles r > 0.94; MAE ~1.9–5.0°; ~80% of joint positions within 30 mm | Low cost, no markers, athlete-friendly | Weak in frontal/transverse planes (r ≈ 0.5–0.8); pose-estimator failures; lower temporal resolution |
| **IMU / inertial** (WitMotion, Xsens, Perception Neuron) | Upper-torso & pelvic rotation ICC 0.91–1.00 vs optical | Portable, no volume limit, cheap, mobile-deployable | Orientation drift, soft-tissue mounting error, magnetometer disturbance indoors, no absolute position |
| **Force / pressure plates** (Bertec, Kistler, Swing Catalyst) | Force-plate reference standard for GRF | Direct kinetics; weight transfer & kinetic sequence | Measures only ground interface; needs separate kinematic system |

**Practical implication.** A capable indoor rig is usually *hybrid*: cameras for club and body kinematics, IMUs for high-rate rotational segments and mobility, and a force/pressure surface for kinetics. Design each subsystem behind a common abstraction so modalities can be swapped or fused without rewriting the pipeline.

---

## 2. Sampling rate and temporal resolution

This is the decision most often gotten wrong, and it follows directly from the **Nyquist–Shannon theorem**: to reconstruct a signal without aliasing, sample at more than twice its highest meaningful frequency. The golf swing contains three regimes with very different frequency content.

**Body kinematics (pelvis, thorax, arms).** The literature clusters at **240 Hz** for optical systems (e.g. eight-camera Motion Analysis setups in the Swing Performance Index work; Polhemus electromagnetic tracking at 240 Hz for kinematic-sequence studies). The review range of 100–300 fps reflects this. 240 Hz gives comfortable headroom for the ~10–15 Hz true content of body-segment motion and clean numerical differentiation to angular velocity.

**Clubhead and shaft.** The club is roughly an order of magnitude faster than the body. At 100 mph, the clubhead travels ~150 ft/s (~46 m/s). At 30 fps it moves ~5 ft between frames — useless around impact. At 240 fps that drops to ~7.5 inches per frame (Cheetham/TPI). For shaft-angle and release dynamics, treat 240 fps as a floor, not a target.

**Impact.** Ball–club contact lasts under half a millisecond. Genuine impact-instant measurement (smash factor, precise face-contact timing) requires effective rates in the **hundreds to >1000 fps**, which is why photometric golf simulators run their impact cameras above 1000 fps. If you only need the *timing* of impact rather than contact mechanics, an audio trigger (impact sound) plus a 240 fps camera is a pragmatic, well-established compromise.

**Rule of thumb.** Sample body segments ≥ 240 Hz; sample the club at the highest rate your hardware sustains; and if impact mechanics matter, treat it as a separate, faster measurement problem rather than expecting one camera to do everything.

---

## 3. Camera setup for indoor capture

### Camera geometry
Two orthogonal views are the established minimum for golf: **face-on** (down the target line's perpendicular, capturing sway, lateral weight shift, and spine tilt) and **down-the-line** (along the target line, capturing swing plane and shaft angle). This mirrors the dual-angle convention in both coaching and research and is the sensible basis for a markerless or hybrid rig. For 3D markerless reconstruction, more views reduce occlusion and depth ambiguity — OpenCap-class systems use two calibrated smartphones as a baseline, and multi-camera OpenPose reconstructions improve with additional synchronised views.

### Shutter, not just frame rate
Frame rate sets temporal sampling; **shutter (exposure) time** sets per-frame motion blur, and they are independent. A fast frame rate with a slow shutter still yields a smeared clubhead. Set shutter to **≤ 1/1000 s** for swing capture (Cheetham/TPI); faster still near impact. This is the single biggest lever on clubhead-tracking precision indoors.

### Global vs rolling shutter
Use **global-shutter** sensors. Rolling shutter scans line-by-line and skews fast-moving objects, bending a straight shaft into a curve and corrupting swing-plane and shaft-angle measurements (Edmund Optics; simulator-camera guidance). This is not a minor artefact for golf — it directly biases the geometry you are trying to measure.

### Lighting
Indoor capture is lighting-limited: fast shutters need light. Provide bright, flicker-free, diffuse illumination. Avoid mains-frequency flicker beating against the frame rate (use DC or high-frequency-ballast/LED sources). For marker-based systems, control stray IR and reflective surfaces. Consistent lighting also stabilises markerless pose estimators, which degrade badly with shadows and low contrast.

### Choosing your capture environment: a quality ladder

The environment is the **user's choice** — nobody should be told to repaint a
room or hang a backdrop to use vision capture. What follows is the honest
quality ordering of the common setups, so the choice is informed. Two axes
matter, and they are not equal:

1. **Illumination is the primary axis.** Missing photons are unrecoverable in
   software: a dark scene forces long exposures (motion blur), high gain
   (noise), and produces the twin contrast failures — a dark shaft vanishing
   against dark surrounds, and a light shaft merging into an over-exposed
   hitting-mat cone. No detector change can fix either.
2. **Background quietness is the secondary axis.** Static clutter (window
   frames, screens, club racks, mirrors) creates false-lock risk, but it is
   *manageable* in software: permanence tests can veto structure that exists
   in the scene reference, and honest confidence tiers degrade gracefully.
   Manageable is not free — busy backgrounds still cost coverage and
   robustness — but unlike darkness they are not a hard wall.

**The ladder, best to worst, for passive (untaped) club tracking:**

| Tier | Environment | Expected capture quality | Retro bands? |
|---|---|---|---|
| 1 | **Bright, even light + quiet mid-tone background** (the coaching-studio ideal, or a light-filled room with a plain wall behind the swing arc) | Full detector performance: short exposure, contrast along the whole swing arc, few distractors. | Unnecessary — optional precision upgrade only. |
| 2 | **Bright light + busy background** (light-filled room, windows/furniture in view) | Good: signal exists everywhere and short exposures tame blur; static clutter is largely handled by permanence vetoes, at some cost in coverage and occasional false locks near strong scene lines. | Optional; worthwhile if the clutter is severe (mirrors, club racks in the swing arc). |
| 3 | **Darkened room + quiet background** (dim room, plain dark walls, hitting area spot-lit) | Workable with caveats: the club is only reliably tracked where the light falls; top-of-backswing and finish regions against unlit surrounds degrade to honest predictions; exposure discipline (don't blow out the mat) is critical. | Recommended — recovers the dark portions of the arc outright. |
| 4 | **Dark room + busy background** (the typical home simulator: black walls, impact screen, windows, equipment in view) | The hard case: both failure axes at once. Passive detection degrades honestly (low-confidence predictions rather than confident errors) but measured coverage is sparse and no amount of tuning changes the physics. | **Strongly recommended** — bands + a camera-mounted ring light lift club tracking in this room to instrument grade, better than tier 1's passive tracking. |

Rules that apply at every tier: hitting-area highlights must stay below
clipping (an over-exposed mat swallows the shaft at any tier), and the
background must be *static* during a session (background-subtraction methods
assume it). The retroreflective-band option (see
"Instrumenting the club" below) is deliberately environment-independent —
that is its point: it makes tier 4 perform like tier 1, which is why it is
the right answer for a black sim room, and largely unnecessary in a bright
room with a quiet backdrop.

### Capture environment: surfaces and wall colour
If you are building or adapting a space and CAN choose the surfaces (tiers 1
and 3 above), this is what to choose. No golf-biomechanics paper prescribes
wall colour, but the guidance triangulates cleanly from motion-capture studio
practice and the object-detection literature, and golf has one specific
wrinkle that overrides the usual studio default.

- **Matte finish is non-negotiable.** Gloss and satin create specular highlights that scatter light and — worse for vision — mimic exactly what you're detecting: a bright spot on a gloss wall reads like a ball, a marker, or a clubhead to a blob detector. Mocap installation guidance (Vicon; MoCap Online) singles out reflective surfaces — shiny fixtures, chrome, mirrors, gloss paint — as primary noise sources for optical systems, and recommends matte or satin paint plus blackout curtains.
- **Go mid-to-dark grey or matte black, not white.** The usual studio default of white walls is actively wrong for golf because the ball is white: vision projects repeatedly report the white ball blending into light backgrounds and colour-mask segmentation failing outright. A dark, desaturated surround behind the hitting zone restores ball contrast, absorbs stray fill light (more lighting control), and is the mocap-studio default anyway.
- **Prefer mid-grey for the swing volume.** The shaft is the awkward second target — chrome/steel is bright and specular, graphite is dark — so a mid-grey backdrop gives usable contrast against both extremes, plus against skin and most clothing for silhouette/pose work. Pure black maximises ball contrast but can swallow a dark shaft or dark trousers.
- **Avoid saturated colours** (red, pink, purple, green) unless deliberately chroma-keying: saturated walls bounce coloured light onto the golfer (spill), corrupting colour-based segmentation and skin detection. The exception is a dedicated key mode — a proper saturated green/blue screen to matte out the golfer — which conflicts with ball contrast and general-purpose grey, so treat it as a separate configuration, not the default.
- **Keep the background uniform and unchanging.** Background-subtraction methods assume a static, uncluttered scene; anything that moves or shifts between sessions breaks them. Keep the wall plain and clear of distractor objects within the camera field of view.
- **IR caveat.** The above assumes visible-light capture. If you add active IR illumination or IR markers, a paint's near-IR reflectance does not track its visible appearance — a wall that looks dark to the eye can be bright in IR — so verify the surround through the actual sensor rather than trusting the visible colour.

### Instrumenting the club: retroreflective shaft bands (optional, high value)

Vision-only club tracking works — it is the default, and no user should be
required to modify their equipment — but a difficult environment (dark
surrounds, mixed lighting, cluttered backgrounds) takes its toll on any
photometric detector long before it troubles a marker. A few grams of
retroreflective tape on the shaft changes the problem class entirely, and is
the single highest-leverage *optional* upgrade a studio can make:

- **What it buys.** Retro bands lit by a camera-mounted LED return light
  straight into the lens and saturate as bright, compact blobs regardless of
  background — dark wall or blown-out hitting mat alike. A banded shaft is
  detectable in conditions where the plain shaft is genuinely invisible, and
  band pairs at *measured* spacings turn each frame into direct geometry:
  projected spacing ÷ known spacing gives the shaft's foreshortening
  outright, the band line gives its angle without relying on pose-estimated
  hands, an asymmetric band count (e.g. two near the grip, three near the
  hosel) resolves the 180° direction ambiguity, and the clubhead position
  follows as a known extrapolation even when the head itself cannot be seen.
- **Why it is trustworthy.** Along-shaft distance ratios are preserved under
  projection, so the pattern is recognisable at any club orientation — a
  specular flash is one blob; five collinear blobs at recorded ratios are
  unmistakably the marker set. Motion blur smears tangentially while the
  band separations run along the shaft, so the pattern survives the impact
  streak.
- **The physics constraint.** Retro tape returns light *to its source* within
  a ~1–2° cone: the illuminator must sit at the camera lens (a small LED ring
  is ample — on-axis watts beat off-axis hundreds). Ceiling or wall lights do
  not light retro tape for the camera; with only ambient light, matte-white
  tape is the angle-tolerant fallback, at the cost of dark-arc coverage and
  white-on-white contrast over a bright mat. Visible-band illumination only
  (IR conflicts with launch monitors and IR-blind RGB pipelines).
- **Materials and pattern** (full protocol: `docs/validation/instrumented_club_protocol.md`):
  thin glass-bead conformable retro tape, ~25 mm wide bands and gaps (sized to
  survive sensor bloom at typical capture scales), an asymmetric grip/mid/
  hosel layout, and — the step that converts visibility into *measurement* —
  a per-club record of band positions (`clubs.json`).
- **Role discipline.** Keep the taped club as (a) a ground-truth instrument —
  every instrumented swing yields dense auto-labels for validating and tuning
  the *unmarked* detector — and (b) an opt-in high-accuracy mode for users
  who want it. The untaped pipeline remains the product; score it on unmarked
  clubs (tape changes the shaft's appearance, so keep an unmarked control
  subset in any validation corpus).

### Calibration
Calibrate the capture volume every session and record the residual. Golf studies typically calibrate over a ~3.5 × 2.8 × 3 m volume and report **sub-millimetre to ~1 mm mean marker residual**; treat anything materially worse as a setup fault. For markerless multi-view, intrinsic and extrinsic calibration quality dominates 3D accuracy — a checkerboard/charuco routine per session, with reprojection error logged, is the equivalent discipline.

---

## 4. Marker sets and body models (marker-based)

Follow **International Society of Biomechanics (ISB) recommendations** for marker placement and joint coordinate systems. The systematic review's central methodological criticism is that most golf studies *don't*: only a minority cite ISB standards, and many place markers on clothing rather than skin, which the authors flag as a soft-tissue-artefact problem that undermines comparability.

Practical minimums drawn from the literature:

- **Pelvis:** markers on both anterior superior iliac spines (ASIS) to define pelvic orientation in the transverse plane.
- **Upper torso:** markers on both acromia to define thorax orientation; pelvis-minus-thorax gives the X-factor.
- **Club:** a marker on the distal shaft (commonly ~5 cm from the clubface centre), or proximal/middle/distal shaft markers, to track shaft and clubhead. A practice ball wrapped in reflective tape gives a contact reference.

Two hard rules:

1. **Place markers on skin, not clothing**, and minimise soft-tissue artefact by siting over bony landmarks.
2. **Do not smooth club/ball/impact markers** the way you smooth body markers (see §7) — at impact these undergo large, near-instantaneous displacement, and filtering introduces phase lag and amplitude loss exactly where you need fidelity.

---

## 5. Markerless pipeline best practices

Markerless capture is the most active area and the most seductive for an accessible product, but the accuracy profile is *anisotropic* and you must design around it.

- **Plane-dependent accuracy.** In the OpenCap return-to-sport validation, sagittal-plane joint angles agreed well (r > 0.94), the ankle less so (r ≈ 0.84–0.93), and **frontal (r ≈ 0.47–0.78) and transverse (r ≈ 0.51–0.60) planes were markedly weaker**, with mean absolute errors of ~1.9–5.0°. Transverse-plane rotation is precisely where golf lives (pelvis/thorax rotation, X-factor), so treat markerless axial-rotation figures with caution and validate them explicitly.
- **Position error floor.** Multi-camera OpenPose reconstructions put ~47% of joint-position estimates within 20 mm and ~80% within 30 mm, but ~10% exceeded 40 mm, usually from tracking failures (a limb swapped or misidentified between frames). Your pipeline needs outlier detection and gap handling, not just raw pose output.
- **Single-view is possible but limited.** Single-video markerless analysis can distinguish proficiency and individual swing characteristics (including X-factor patterns), but a single 2D view cannot resolve true 3D rotation without strong assumptions. Prefer ≥ 2 calibrated views for anything quantitative.
- **Temporal resolution.** Consumer markerless setups often run at 25–60 Hz, which is inadequate for club and impact and marginal even for body derivatives. Push camera frame rate up and be explicit that derivative quantities (velocities, accelerations) inherit the noise.

---

## 6. IMU / inertial best practices

IMUs are the most deployable modality and validate surprisingly well for the rotational quantities golf cares about. Against 3D optical reference across 36 golfers, IMU-derived upper-torso and pelvic rotation, pelvic rotational velocity, and the X-, S- and O-factors showed **ICCs from 0.91 (O-factor) to 1.00 (upper-torso rotation)** and comparable Pearson coefficients — strong agreement for the headline metrics.

Best practices:

- **Placement and calibration.** Site sensors over bony landmarks (thoracic spine/sternum for upper torso, sacrum/pelvis for pelvis) with firm strapping to reduce soft-tissue movement. Run a defined static/functional calibration pose per session to establish segment frames.
- **Sensor fusion.** Use an orientation filter (complementary/Madgwick or an error-state Kalman filter) rather than raw integration. Gyro integration alone drifts; accelerometer and (where usable) magnetometer references correct it.
- **Magnetometer caution indoors.** Indoor environments are magnetically dirty (steel structure, screens, motors). Magnetometer-dependent yaw can be corrupted; prefer gyro-dominant yaw with periodic correction, or a magnetometer-free fusion, and validate heading stability across a session.
- **Segment count.** A four-IMU set (pelvis, torso, lead arm, hand) is the established configuration for a proximal-to-distal kinematic-sequence report; more sensors buy finer segmentation at the cost of setup and drift management.
- **Sampling.** Run IMUs fast (commonly 100–200+ Hz) so that peak-angular-velocity timing — the substance of the kinematic sequence — is well resolved.

---

## 7. Ground reaction forces and kinetics

Kinematics tell you *what* moved; GRF tells you *how the ground was used to make it move*. This is a distinct and complementary measurement.

- **Reference hardware.** Six-degree-of-freedom force plates (Bertec, Kistler) are the standard, sampled very fast — golf studies commonly run force plates at **~2000–2400 Hz**, far above the kinematic rate, because force signals contain higher-frequency content and drive impulse/moment calculations. Dual plates (one per foot) resolve lead/trail asymmetry.
- **Pressure vs force plates.** Pressure mats give centre-of-pressure and relative loading cheaply; combined pressure+force systems (e.g. Swing Catalyst 3D plate) add true 3D force vectors. Be clear which you have: centre-of-pressure ≠ force vector.
- **Magnitudes to expect.** Vertical GRF routinely exceeds body weight during the downswing — reports of peaks around 150% body weight for driver swings are typical — and weight-transfer patterns differ systematically between low- and high-handicap golfers.
- **The kinetic sequence.** Efficient swings show an ordered build of ground forces: **horizontal (lateral) force, then rotational torque, then vertical force**, with the vertical peak occurring roughly between lead-arm-parallel and shaft-parallel in the downswing. Report both the magnitudes and their *timing windows*, since mistimed peaks (e.g. vertical force peaking at or after impact) are a common inefficiency.

---

## 8. Synchronisation across modalities

A hybrid rig is only as good as its time alignment, and golf's short impact window makes this unforgiving. Sub-frame skew between a camera and a force plate will scramble any claim about the kinetic sequence.

- Use a **hardware trigger or common clock** where possible (genlock for cameras, a shared TTL/sync pulse across cameras, IMUs, and force plates).
- Where hardware sync isn't available, use a **physical sync event** every trial — an impact transient is ideal because it appears in audio, force, and video simultaneously and gives a sharp cross-modal fiducial.
- Log every stream's timestamps and quantify residual skew; report it. Cross-modal latency is a first-class error source, not an afterthought.
- Resample thoughtfully. When fusing a 200 Hz IMU stream with 240 Hz video and a 2000 Hz force plate, interpolate to a common timeline with methods appropriate to each signal's bandwidth, and never upsample a slow stream and pretend it gained information.

---

## 9. Signal processing and filtering

Filtering choices materially change discrete outputs (peak velocities, sequence timing), so they must be principled and reported.

- **Filter type.** Use a **fourth-order, zero-lag (dual-pass) Butterworth** low-pass filter for body kinematics — the biomechanics standard. Zero-lag (forward–backward) filtering avoids the phase shift that would otherwise displace peak-timing, which is fatal for sequence analysis.
- **Cut-off frequency.** Golf kinematic studies commonly use **~12 Hz** for body-marker data. Don't just inherit a number: determine the cut-off from the data via **residual analysis** (Winter's method; Yu, Gabriel, Noble & An, 1999), noting that residual analysis can under-estimate the optimum at high sampling rates, so sanity-check against the acceleration signal.
- **Differentiation.** Velocities and accelerations amplify noise. Filter *before* differentiating, and prefer analytic differentiation of a fitted/filtered signal over raw finite differences.
- **The impact exception.** Do **not** low-pass the clubhead, ball, or impact markers with the body cut-off. Large near-instantaneous displacement at impact means smoothing introduces delay and attenuates magnitudes exactly where fidelity matters most — use these channels raw (or minimally processed) for event timing.
- **IMU-specific.** Apply the orientation filter as your primary smoothing; additional heavy low-passing of fused orientation can hide the very velocity peaks you're trying to time.

---

## 10. Kinematic variables and the kinematic sequence

The quantities worth computing, and the caveats attached:

- **Rotational separation.** X-factor (thorax−pelvis axial separation), plus S-factor (shoulder obliquity) and O-factor (pelvic obliquity). These are the headline golf metrics and validate well across modalities — but axial rotation is markerless capture's weakest plane, so validate it specifically if you go markerless.
- **The kinematic (proximal-to-distal) sequence.** Efficient swings reach peak angular velocity in order — **pelvis → thorax → lead arm → club** — each segment peaking slightly after and faster than the last, producing a summation-of-speed "whip." This pattern is robust across skill levels and shot types (full and partial).
- **A methodological warning on the sequence.** How you compute segment angular velocity changes the answer. One study found that depending on whether you use the resultant norm, the longitudinal component, or another convention, **5–7 different sequence orderings emerged across 13 participants** — and even good players sometimes deviate from strict proximal-to-distal. Fix and *report* your angular-velocity definition; don't treat "the kinematic sequence" as method-independent.
- **Clubhead speed at impact** remains the field's primary performance indicator and the natural top-line output — but see §2 on why measuring it precisely is a separate, faster problem.

---

## 11. Validation and error reporting

If you build a system, validate it against a better one and report the numbers the literature reports, so your figures are comparable.

- **Concurrent validation.** Record simultaneously with a reference (optical mocap for kinematics, force plate for kinetics) on the same swings.
- **Report agreement *and* error.** Agreement: intraclass correlation (ICC) with 95% CI, Pearson/CMC. Error: RMSE and MAE in native units (degrees, mm, N), per plane and per variable — not a single global number that hides the weak transverse plane.
- **Report per-plane and per-phase.** Accuracy varies by anatomical plane (sagittal good, transverse weak for markerless) and by swing phase (backswing top, transition, impact behave differently). A single headline statistic is misleading for golf.
- **State your processing.** Sampling rate, shutter, calibration residual, filter type and cut-off, sync method and residual skew, number of valid swings, and marker-dropout handling. The systematic review's core complaint is under-reporting of exactly these, which is why golf kinematics studies are hard to compare.
- **Sample realistically.** Include both sexes and a spread of skill levels; metrics that hold for tour pros can differ for recreational golfers, and small unrepresentative samples produce non-generalisable "norms."

---

## 12. Indoor-specific practical checklist

- [ ] Two orthogonal camera views minimum (face-on + down-the-line); more for 3D markerless.
- [ ] Global-shutter sensors; shutter ≤ 1/1000 s; frame rate ≥ 240 fps for body/club.
- [ ] Bright, flicker-free, diffuse lighting matched to the fast shutter.
- [ ] Capture environment chosen consciously against the quality ladder (§3): light first, quiet background second; retro shaft bands + camera ring light where the room can't provide either (dark sim rooms).
- [ ] If surfaces are choosable: matte throughout; mid-to-dark grey (or matte black) walls behind the hitting zone — never white — and no saturated colours unless chroma-keying.
- [ ] Background uniform, unchanging, and clutter-free within the camera FOV; IR reflectance verified through the sensor if using active IR.
- [ ] Volume calibrated every session; residual logged (aim sub-mm to ~1 mm optical; reprojection error for markerless).
- [ ] Markers on skin over bony landmarks, ISB-compliant placement (if marker-based).
- [ ] IMUs firmly mounted, per-session calibration, magnetometer-robust yaw indoors.
- [ ] Force/pressure surface sampled ≥ 2000 Hz if kinetics are in scope; lead/trail resolved.
- [ ] Hardware sync or a per-trial impact fiducial across all streams; skew measured.
- [ ] Zero-lag 4th-order Butterworth for body (~12 Hz, residual-analysis justified); club/impact left unsmoothed.
- [ ] Angular-velocity convention fixed and documented before computing the kinematic sequence.
- [ ] Validation numbers reported per plane and per phase, with ICC/CI and RMSE/MAE.

---

## References

1. Bernardina, G. R. D., et al. *Golf Swing Biomechanics: A Systematic Review and Methodological Recommendations for Kinematics.* (PMC9227529) — optoelectronic systems 100–300 fps; ISB standards under-cited; soft-tissue-artefact critique.
2. *The Swing Performance Index: Developing a Single-Score Index of Golf Swing Rotational Biomechanics Quantified with 3D Kinematics.* Frontiers in Sports and Active Living, 2022 (PMC9816382) — 8-camera Motion Analysis at 240 Hz; ASIS/acromion and distal-shaft marker set.
3. *Validation of Inertial Measurement Units for Analyzing Golf Swing Rotational Biomechanics.* (PMC10611231) — 36 golfers; ICC 0.91–1.00; X/S/O-factor; 12 Hz Butterworth; clubhead/ball markers left unsmoothed.
4. Turner, J., Chaaban, C., Padua, D. *Validation of OpenCap: A Low-Cost Markerless Motion Capture System for Lower-Extremity Kinematics During Return-to-Sport Tasks.* — sagittal r > 0.94; frontal r 0.47–0.78; transverse r 0.51–0.60; MAE 1.91–5.01°.
5. Nakano, N., et al. *Evaluation of 3D Markerless Motion Capture Accuracy Using OpenPose With Multiple Video Cameras.* (PMC7739760) — ~47% of joint positions < 20 mm, ~80% < 30 mm, ~10% > 40 mm.
6. *Extracting Proficiency Differences and Individual Characteristics in Golfers' Swing Using Single-Video Markerless Motion Analysis.* (PMC10684732) — single-view markerless; X-factor individual differences.
7. *Elite Golfers' Kinematic Sequence in Full-Swing and Partial-Swing Shots.* — Polhemus Liberty at 240 Hz; proximal-to-distal sequencing and speed-summation across skill/sex.
8. *Biomechanical Analysis of the Golf Swing: Methodological Effect of Angular Velocity Component on the Identification of the Kinematic Sequence.* — 5–7 different sequence orderings across 13 participants depending on angular-velocity convention.
9. Yu, B., Gabriel, D., Noble, L., An, K. N. *Estimate of the Optimum Cutoff Frequency for the Butterworth Low-Pass Digital Filter.* Journal of Applied Biomechanics, 15(3):318–329, 1999.
10. Cheetham, P. *Measuring the Timing of the Golf Swing from Video.* TPI — 120–240 fps guidance; clubhead ~150 ft/s at 100 mph; shutter ≥ 1/1000 s.
11. Edmund Optics, *Machine Vision for Golf Simulators* — minimum ~160 fps; global-shutter requirement.
12. Swing Catalyst, *Ground Reaction Force* and *Golf from the Ground Up*; and GRF force-plate studies — vertical force to ~150% body weight; horizontal → torque → vertical kinetic sequence and timing windows.
13. Noraxon, *How to Analyze Golf Swing with IMUs* — four-IMU (pelvis/trunk/arm/hand) proximal-to-distal kinematic-sequence workflow.
14. Vicon, *How Do I Get Started in Motion Capture for Entertainment* and MoCap Online, *Motion Capture Studio Setup Guide* — matte/satin walls, non-reflective surfaces, blackout curtains, avoidance of shiny fixtures for optical systems.
15. Golf ball detection/tracking sources (e.g. CNN + Kalman-filter golf-ball tracking, arXiv 2012.09393; open-source ball-tracking reports) — white ball blending into light backgrounds and the reliance of background-subtraction methods on a static, uncluttered scene.
16. Studio paint-colour guidance (video/photography studio practice) — matte over gloss; neutral white/black/grey; avoidance of saturated red/pink/purple to prevent colour spill.

*Accuracy figures are drawn from the cited studies and reflect their specific hardware, tasks, and populations; treat them as indicative benchmarks, not guarantees for a given rig. Validate your own system.*
