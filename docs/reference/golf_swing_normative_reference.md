# Golf Swing Biomechanics — Normative Reference and Design Constraints

**Function:** this document is the authoritative source of population norms and benchmark values
drawn from the peer-reviewed and industry literature. It constrains design, development,
verification and validation of PinPoint metric derivation, and it is the source of the normative
context surfaced to end users.

**Standing:** normative statements are identified `NR-nn` and are binding. Data tables are factual
records of published findings and carry their cohort and source. Supersedes
`cheetham-golf-biomechanics.md`.

---

## 0. Surfacing constraints

Golf biomechanics normative data is not equivalent to clinical reference ranges. It derives from
small cohorts (n = 5–40), overwhelmingly male and right-handed, measured with inconsistent segment
definitions. The 2022 systematic review of 92 golf kinematics papers concluded that a lack of
methodological consensus prevents generalisation of results and produces contradictory findings
across studies. This document is context. It is not ground truth.

| ID | Statement |
|---|---|
| **NR-01** | Every surfaced normative value carries its cohort and n. "19 PGA tour professionals (Cheetham et al., 2008)" is compliant. "Tour average" is not. |
| **NR-02** | Every surfaced normative value carries the segment definition it was measured under. Where PinPoint's definition differs from the source's, PinPoint either normalises to the source definition or withholds the comparison. |
| **NR-03** | PinPoint reports direction of difference in preference to absolute gap. "Your pelvis peaks after your thorax; professionals peak pelvis first" is compliant. "You are 47°/s below tour average" asserts a precision the literature does not support and is not compliant. |
| **NR-04** | PinPoint surfaces variability alongside magnitude. Consistency, not peak magnitude, is the most robust discriminator in this literature (§2.3). |
| **NR-05** | Normative values are not targets. PinPoint does not present a normative value as a goal state, and does not gate a good/bad verdict on proximity to one. |
| **NR-06** | PinPoint makes no injury-risk claim. Injury-adjacent metrics (crunch factor, lateral bend, wrist loading) may be reported as descriptive values. They are not interpreted as risk. PinPoint is not a medical device. |
| **NR-07** | Peer-reviewed sources and commercial datasets are labelled distinctly. Trackman figures are cited as "Trackman tour averages", never as research. |

---

## 1. Definition hazards

These are the mechanisms by which a correct measurement produces a wrong comparison. Each is a
binding constraint on implementation.

| Metric | Definition variants in the literature | Consequence |
|---|---|---|
| X-factor | (a) acromion line vs. ASIS line (McLean's original); (b) torso segment vs. pelvis segment | (a) ≈ 60°, (b) ≈ 30° for the same swing. This is the largest single source of cross-study variance. |
| X-factor timing | At top of backswing / at peak (early downswing) / at impact | Peak occurs ~1–18% into the downswing, not at the top. Meister et al. found X-factor **at impact** more strongly correlated with clubhead speed than peak X-factor. |
| Segment angular velocity | Component about the segment's superior–inferior axis / resultant magnitude / component normal to the swing plane | Cheetham used S-I axis for pelvis and thorax, and swing-plane-normal for arm and club, because arm and club rotate about an endpoint rather than a centre point. The choice changes both peak values and detected sequence order. |
| Crunch factor | Five studies, ten computational methods (2022 review) | Not comparable across sources. No normative value exists. |
| Cardan sequence | Order of rotation decomposition | Published best practice is (1) lateral bend, (2) flexion/extension, (3) axial rotation. |
| Filtering | Cheetham applied no digital filter — the electromagnetic signals were clean, and inappropriate smoothing distorts peak timing and amplitude near impact (Winter, 1979) | PinPoint's constant-jerk Kalman/RTS smoother attenuates peaks relative to Cheetham's unfiltered values. |

| ID | Statement |
|---|---|
| **NR-08** | PinPoint's X-factor segment definition is recorded in this document and is stated wherever an X-factor value is surfaced. |
| **NR-09** | PinPoint's RTS smoother peak attenuation is characterised and quantified before any smoothed peak value is compared to an unfiltered literature value. The attenuation figure is recorded in this document. |
| **NR-10** | Where PinPoint reports X-factor, it reports X-factor at impact. Top-of-backswing X-factor alone is the weakest of the three available variants (§3.3) and is not surfaced in isolation. |

---

## 2. Kinematic sequence

### 2.1 Source

**Cheetham, P.J., Rose, G.A., Hinrichs, R.N., Neal, R.J., Mottram, R.E., Hurrion, P.D., Vint, P.F.
(2008).** *Comparison of Kinematic Sequence Parameters between Amateur and Professional Golfers.*
In Crews, D. & Lutz, R. (Eds.), *Science and Golf V: Proceedings of the World Scientific Congress
of Golf*, pp. 30–36. Mesa, AZ: Energy in Motion.

Cohort: 19 amateurs (mean CHS 88 mph) and 19 PGA touring professionals (mean CHS 109 mph), one
representative swing each, drawn from the TPI swing database. 240 Hz, 12-sensor Polhemus Liberty
electromagnetic system, TPI-3D software (AMM Inc.). No digital filtering. 18 downswing variables;
t-tests at experiment-wise p < 0.2, test-wise p < 0.011.

### 2.2 Benchmark values

| Parameter | Segment | Units | Pros (mean ± SD) | Amateurs (mean ± SD) | Sig. |
|---|---|---|---|---|---|
| Avg. rotational acceleration | Pelvis | k°/s² | 2.1 ± 0.4 | 1.5 ± 0.4 | ✔ |
| | Thorax | k°/s² | 3.3 ± 0.5 | 2.3 ± 0.5 | ✔ |
| | Arm | k°/s² | 5.1 ± 0.8 | 3.3 ± 0.9 | ✔ |
| | Club | k°/s² | 8.8 ± 1.1 | 6.0 ± 1.1 | ✔ |
| Avg. rotational deceleration | Pelvis | k°/s² | 2.0 ± 0.7 | 1.6 ± 1.0 | ✘ (p = 0.026) |
| | Thorax | k°/s² | 2.6 ± 1.1 | 1.6 ± 1.1 | ✔ |
| | Arm | k°/s² | 7.0 ± 1.2 | 3.0 ± 1.5 | ✔ |
| **Peak rotational speed** | Pelvis | °/s | **477 ± 53** | 395 ± 68 | ✔ |
| | Thorax | °/s | **727 ± 61** | 583 ± 84 | ✔ |
| | Arm | °/s | **980 ± 68** | 763 ± 95 | ✔ |
| | Club | °/s | **2254 ± 68** | 1790 ± 111 | ✔ |
| Rotational speed gain | Pelvis→Thorax | °/s | 250 ± 42 | 188 ± 72 | ✔ |
| | Thorax→Arm | °/s | 253 ± 59 | 185 ± 76 | ✔ |
| | Arm→Club | °/s | 1274 ± 65 | 1027 ± 147 | ✔ |
| **Time of peak before impact** | Pelvis | ms | **87 ± 19** | 78 ± 38 | ✘ (p = 0.124) |
| | Thorax | ms | **68 ± 14** | 59 ± 29 | ✘ (p = 0.147) |
| | Arm | ms | **65 ± 8** | 64 ± 23 | ✘ (p = 0.821) |
| Linear speed at impact | Club head | mph | 109 ± 3 | 88 ± 5 | ✔ |

k°/s² = thousands of degrees per second squared. ✔ = significant at test-wise p < 0.011.

### 2.3 Established findings

**Sequence order.** Professionals peak pelvis → thorax → arm → club. Amateurs peak pelvis → **arm →
thorax**. The amateur arm fires before the thorax. This finding is categorical rather than
magnitude-based and is therefore the most robustly surfaceable statement in this document.

**Timing consistency is the discriminator; timing mean is not.** Mean peak times did not differ
significantly between groups. Amateur standard deviations were at least double the professionals'
in every segment: pelvis 38 vs. 19 ms, thorax 29 vs. 14 ms, arm 23 vs. 8 ms.

**Pelvis deceleration is present/absent, not large/small.** Group difference did not reach
significance (p = 0.026 against a threshold of 0.011). Every professional swing showed pelvis
slowing before impact; not every amateur swing did.

**Casting is directly observable.** Amateurs cast: the club races ahead of the body, peaks early,
and is decelerating through impact. Professionals keep arm and club tracking closely until the
wrist release point late in the downswing, and the club peaks at impact. This pattern is present in
PinPoint's shaft tracker output and requires no normative comparison.

| ID | Statement |
|---|---|
| **NR-11** | Sequence timing is computed over a set of swings and reported as dispersion. A single-swing timing delta against a published mean is not surfaced. |
| **NR-12** | Pelvis deceleration before impact is reported as a binary present/absent characteristic. Its magnitude is not compared to a normative value. |

### 2.4 Corroborating work

**Tinmark, F. et al. (2010).** *Elite golfers' kinematic sequence in full-swing and partial-swing
shots.* *Sports Biomechanics.* n = 47 skilled golfers, three clubs, submaximal to maximal
distances. Proximal-to-distal ordering of both movement onset and peak angular speed at
pelvis/upper-torso/hand holds across all clubs and shot lengths, including partial swings, across
gender and expertise level. The speed-summation effect (each distal segment exceeding its proximal
predecessor) is confirmed, though the magnitude of the increment varies with gender and expertise.
This licenses PinPoint applying the same sequence model to partial shots.

**Neal, R.J., Lumsden, R., Holland, M., Mason, B. (2007).** *Body segment sequencing and timing in
golf.* *Annual Review of Golf Coaching* 2007, pp. 25–36. Timing lags between peak angular speeds of
contiguous segments showed no significant difference between shots that highly skilled players
self-rated as well-timed versus mistimed. Subjective swing "timing" tracks strike centredness
rather than kinematic sequence lag.

---

## 3. Torso–pelvis separation (X-factor and X-factor stretch)

### 3.1 Reference values

| Measure | Value | Cohort / source |
|---|---|---|
| X-factor at top (shoulders vs. pelvis) | ≈ 60° | Cross-study synthesis, 2022 systematic review |
| X-factor at top (torso vs. pelvis) | ≈ 30° | Cross-study synthesis, 2022 systematic review |
| X-factor at top | 45°, male and female tour | TPI |
| X-factor at peak (early downswing) | ≈ 50° | TPI |
| X-factor at top / stretch | 42° / ≈ 5° | TPI, via secondary sources |
| X-factor at top, long vs. short hitters | 38° vs. 24° | McLean (1992), n = 5 vs. 5, driving-distance rank 19th vs. 161st |
| X-factor at top, expert vs. experienced amateurs | 47.7° vs. 46.2°, not different | Egret et al. (2004) |
| Hips / shoulders at top, PGA | 55° / 87° | McTeigue et al. (1994) |
| X-factor stretch, % increase over top value | Highly skilled **+19%**, less skilled **+13%** | Cheetham et al.; group × swing-position interaction F₁,₁₇ = 6.90, p = 0.02 |
| Peak X-factor CoV among professionals | 7.4% | Meister et al. (2011), n = 10 |

### 3.2 The stretch discriminates; the static X-factor does not

McTeigue et al. (1994) found no evidence that X-factor magnitude at the top of the backswing
discriminates skill level. Egret et al. (2004) found expert and experienced amateurs achieve
statistically indistinguishable X-factor at the top. What separates skill levels is the increase in
separation during early downswing, as the pelvis fires toward the target while the torso is still
completing its turn.

Peak X-factor occurs approximately 1–18% into the downswing. Meister et al. found peak X-factor
preceded peak free moment in every swing of every golfer in their cohort.

### 3.3 X-factor at impact

Meister et al. (2011) reported X-factor at impact correlated with clubhead speed at median r ≈
0.943 — equal to peak free moment per kilogram, and above peak X-factor (0.900) and peak
upper-torso rotation (0.900).

### 3.4 Contested mechanism and inverted dose-response

More X-factor is not better. A large shoulder turn paired with restricted pelvic rotation (for
example 100°+ shoulders against 26° pelvis) is a breakdown pattern rather than a power pattern
(TPI). Moderate stretch produces maximum power.

The elastic-energy-storage mechanism popularly attributed to X-factor is contested. Human tissue
has no meaningful spring-like energy store at these timescales. The defensible explanation is
increased active contractile power from eccentrically pre-loaded torso musculature.

| ID | Statement |
|---|---|
| **NR-13** | X-factor and X-factor stretch are surfaced as descriptive values. No good/bad verdict is gated on either. |
| **NR-14** | PinPoint does not describe X-factor as storing or releasing elastic energy. |

---

## 4. Rotational velocity and the follow-through

### 4.1 Meister, D.W., Ladd, A.L., Butler, E.E., Zhao, B., Rogers, A.P., Ray, C.J., Rose, J. (2011). *Rotational Biomechanics of the Elite Golf Swing: Benchmarks for Amateurs.* *Journal of Applied Biomechanics* 27(3), 242–251.

n = 10 professional and 5 amateur male golfers. 3D kinematics and kinetics.

- Highly consistent among professionals: peak free moment per kilogram (CoV **6.8%**), peak
  X-factor (**7.4%**), peak S-factor / shoulder obliquity (**8.4%**).
- The downswing is initiated by reversal of pelvic rotation, followed by reversal of upper-torso
  rotation. This is the transition sequence, distinct from the peak-velocity sequence.
- Correlation with clubhead speed at impact (median r): free moment/kg 0.943, X-factor at impact
  0.943, peak X-factor 0.900, peak upper-torso rotation 0.900.
- For amateurs, the number of factors falling outside 1–2 SD of the professional means increased
  with handicap.

The out-of-band factor count is the framing this literature most directly supports: a count of
factors outside the professional band, rather than a per-factor score.

### 4.2 Rose, J. et al. (2018). *Golf Swing Rotational Velocity: The Essential Follow-Through.* *Annals of Rehabilitation Medicine.*

n = 11 professionals and 5 amateurs.

- Professional consistency during downswing: upper-torso rotational velocity CoV **0.086**; pelvic
  rotational velocity CoV **0.079**.
- Peak upper-torso rotational velocity and peak X-prime occur **after impact, in follow-through**.
  Both were reduced in amateurs (p = 0.005 each) and separated professionals from 4 of 5 amateurs.
- Peak pelvic rotational velocity occurs during the downswing. Pelvic velocity at impact was
  reduced in amateurs (p = 0.019), separating professionals from 4 of 5 amateurs.

X-prime is the rate of change of relative angular position between pelvis and upper torso in the
transverse plane — the derivative of X-factor. It is distinct from "X-factor velocity" as defined
in some other papers.

| ID | Statement |
|---|---|
| **NR-15** | The PinPoint capture and analysis window extends past impact through follow-through. Two of the three most discriminating rotational metrics in this literature are not computable from a window terminating at impact. `FrameThrottle` and segmentation boundaries conform to this. |

### 4.3 Rose, J. et al. (2022). *The Swing Performance Index.* *Frontiers in Sports and Active Living*, doi:10.3389/fspor.2022.986281.

n = 11 professionals (31.0 ± 5.9 yr) and 5 amateurs (28.4 ± 6.9 yr). Nine candidate metrics
selected on the criteria of being consistent among professionals and derivable from IMUs.

The index uses three inputs: peak pelvic rotational velocity pre-impact; pelvic rotational velocity
at impact; peak upper-torso rotational velocity post-impact. Standardised so professionals = **100
± 10**; amateurs scored **82 ± 4**. AUC **0.97**, standardised mean difference 2.12.

The authors state the index was developed on a limited sample and requires confirmation.

| ID | Statement |
|---|---|
| **NR-16** | PinPoint does not reimplement SPI and present it as validated. Any composite score PinPoint derives is identified as PinPoint's own and carries its own validation evidence. |

---

## 5. Ground reaction force and centre of pressure

PinPoint has no force plate. This section exists because users arrive with Swing Catalyst or
BodiTrak figures.

| Measure | Skilled | Less skilled | Source |
|---|---|---|---|
| Lead foot vertical GRF, backswing | 35 ± 11 %BW | 25 ± 9 %BW | Okuda et al., via Watson et al. systematic review |
| Lead foot vertical GRF, downswing | 33 ± 19 %BW | 59 ± 28 %BW | as above |
| Trail foot vertical GRF, backswing | 92 ± 12 %BW | 76 ± 13 %BW | as above |
| Trail-side loading during backswing, low handicap | 66–81 %BW (driver); 74–92 %BW (8-iron) | — | Force-plate study, n = 7 |
| Peak VGRF (mean max), trail / lead | −67% / 101% BW | — | Bourgain et al. (2020), n = 9 pro + 19 amateur (hcp 15 ± 6) |
| Peak HGRF antero-posterior, trail / lead | 23% / −21% BW | — | as above |
| Peak HGRF medio-lateral, trail / lead | 18% / −17% BW | — | as above |

Peak vertical GRF occurs before impact in essentially all golfers — all but one professional in
Bourgain et al.

Professionals did not exhibit higher VGRF or HGRF maxima than amateurs (Bourgain et al.). The
difference lies in application, not magnitude. Combined vertical and horizontal forces explained
R² = 0.70 of clubhead speed variance.

Worsfold et al. found handicap did not influence vertical GRF changes with a driver. This
literature is contradictory.

Horizontal GRF lever arms are substantially larger than vertical: mean ≈ 0.46 m against ≈ 0.04 m
trail-side, with an overall maximum of 0.91 m for trail HGRF against 0.28 m for VGRF. Vertical-only
pressure mats therefore miss most of the torque contribution.

---

## 6. Crunch factor

Defined as the product of torso lateral inclination angle and axial rotation velocity of the torso
relative to the pelvis (originating with the American Orthopaedic Society for Sports Medicine).
Intent: capture combined bending and shear stress on the intervertebral discs.

The 2022 systematic review identified five studies using ten different computational methods. There
is no consensus definition and therefore no quotable normative value.

Cole & Grimshaw (2013), n = 12 with low back pain and 15 asymptomatic: maximum crunch factors and
their timings were not significantly different between groups. Where CF was elevated, this was
attributable to simultaneous increases in both constituent terms rather than either alone. The
measure is not sensitive enough to distinguish golfers with LBP from asymptomatic players.

Joyce (2016), n = 20 professional and amateur: higher crunch factor correlates with increased
clubhead and ball speed. The metric is simultaneously promoted as an injury indicator and
correlated with performance.

| ID | Statement |
|---|---|
| **NR-17** | Crunch factor is computed and logged. It is not displayed. Where a user asks, PinPoint explains the absence of a consensus definition and the negative findings above. |

Adjacent descriptive values, coaching-source rather than peer-reviewed, low confidence: pelvis side
bend at top ≈ 10–12° (trail hip high); thoracic left lateral flexion ≈ 40–50°.

---

## 7. Wrist kinematics and release

### 7.1 The two wrists perform different functions

The lead (non-dominant) wrist is ulnar deviated at address, moves into maximal radial deviation at
the top, and returns through neutral to ulnar deviation at impact. Its primary degree of freedom is
radial/ulnar deviation.

The trail (dominant) wrist is neutral at address, moves into maximal extension at the top, and
returns to neutral and ultimately flexion just before impact. Its primary degree of freedom is
flexion/extension.

The wrist axes are not parallel during the swing. Bilateral flexion occurs through the phase
leading to impact, which parallel axes would not permit.

| ID | Statement |
|---|---|
| **NR-18** | PinPoint does not model wrist hinge as a single scalar. Lead and trail wrists are modelled with distinct primary degrees of freedom, and the non-parallel axis condition is preserved. |

### 7.2 Flexion versus ulnar deviation

**Sweeney, M. et al. (2012).** *Wrist kinematics during the golf swing: is flexion or ulnar
deviation more important?* Forward-kinematic model with imposed-restriction conditions.

Trail-wrist flexion showed by far the greatest range of motion and peak angular velocity of the
four anatomically available degrees of freedom, for every player analysed. Restricting flexion
significantly reduced clubhead velocity at impact. Restricting ulnar deviation did not.

Skilled players do not hold a flat (neutral flexion/extension) lead wrist at the top, and do not
maintain a fixed angle through the downswing. The lead wrist is measurably extended ("cupped") at
the top.

The motion popularly described as "un-cocking" is trail-wrist flexion, not bilateral hinging.

Peak angular velocities in skilled golfers: trail-wrist flexion **1531 ± 345 °/s**; trail-wrist (and
lead-wrist) ulnar deviation **548 ± 155 °/s**.

### 7.3 Skill and grip effects

**Fedorcik, G. et al. (2012).** *Differences in wrist mechanics during the golf swing based on golf
handicap.* n = 28 male right-handed golfers, mean age 26; low handicap (≤5) against high (>10);
8-camera 3D capture; top of backswing to contact. Lead-wrist radial deviation at impact averaged
≈ 8° in the low-handicap group and significantly more in the high-handicap group. Lower handicap
corresponds to less lead-wrist radial deviation at impact. The study also analysed angle of descent
(shaft against horizontal) at peak and at lead-forearm-parallel.

**Grip type**, n = 12 professional males, driver, 6-DOF hand-vs-forearm, 10-camera capture: strong,
neutral and weak grips produced the same clubhead velocity at impact but different face orientation
at impact — −1.5 ± 4.7° (strong), −2.6 ± 4.5° (neutral), −6.4 ± 6.9° (weak). Grip alters
flexion/extension and internal/external rotation at both wrists at all swing events;
ulnar/radial deviation is substantially less affected.

**Sub-elite lead versus trail wrist**, SPM analysis across pitching wedge, 7-iron and driver:
statistically different movement patterns in flexion/extension, internal/external rotation and
radial/ulnar deviation across all three swing phases. The lead wrist moves more slowly than the
trail wrist in both flexion/extension and radial/ulnar deviation through the downswing and at
impact.

### 7.4 Cheetham's dissertation

**Cheetham, P.J. (2014).** *The Relationship of Club Handle Twist Velocity to Selected Biomechanical
Characteristics of the Golf Drive.* PhD Dissertation, Arizona State University. Handle twist
velocity — forearm rotation transmitted to the grip — against broader downswing biomechanics.
Direct background for PinPoint's shaft-angle and release work.

### 7.5 Unresolved

Hume et al. (2005) proposed maintaining the lead wrist in approximately 35° flexion through the
downswing. Sweeney's data — skilled players are extended at the top and do not hold a fixed angle —
is inconsistent with this.

| ID | Statement |
|---|---|
| **NR-19** | The lead-wrist downswing angle prescription is recorded as unresolved. PinPoint does not adopt either position as a target or a fault. |

---

## 8. Clubhead and ball metrics (launch-monitor norms)

### 8.1 Driver, tour

| | Club speed | Ball speed | Attack angle | Carry | Total |
|---|---|---|---|---|---|
| PGA Tour (2024, Trackman) | **113 mph** | **167 mph** | ≈ −1.3° (down) | ≈ 275 yd | ≈ 296 yd |
| PGA Tour (2023, Trackman) | ≈ 115 mph | ≈ 171 mph | — | 282 yd | 300.2 yd (2024 PGA Tour stat) |
| LPGA Tour (Trackman) | **94 mph** | **139 mph** | **+3.0° (up)** | ≈ 220 yd | ≈ 246 yd |

Male data: 40+ events, 200+ players (PGA Tour and DP World Tour). Female: 30+ events, 150+ players
(LPGA and LET). Competition and range data combined.

Further Trackman-published relationships:

- Every club in a PGA Tour player's bag reaches ≈ 30 yd apex; LPGA ≈ 25 yd.
- Club speed increases ≈ 2 mph per club through the bag.
- LPGA players strike every club except the driver with a descending blow; the driver is +3.0° up.
- Carry gains ≈ 3 yd per 1 mph of club speed.
- Smash factor ceiling ≈ 1.50, attainable with the driver only. Tour drivers exceed 1.48; most
  amateurs sit between 1.40 and 1.46.
- Ball launches ≈ 75–85% in the direction of the face on full shots. Direction is determined by
  face angle, club path and impact location combined.
- Iron carry gapping is wider on the PGA Tour than the LPGA Tour: higher club speeds produce more
  separable distances.

### 8.2 Amateur driver ball speed by handicap — Trackman

| Handicap | Male | Female |
|---|---|---|
| Scratch or better | 161 mph | 131 mph |
| 5 | 147 mph | 125 mph |
| 10 | 138 mph | 119 mph |
| 14.5 (average) | 133 mph | — |
| 15 | — | 111 mph |
| Bogey | 131 mph | — |

Average male amateur club speed ≈ 90–95 mph; average female amateur ≈ 75–85 mph. Average male
golfer total drive ≈ 225 yd; all-abilities female ≈ 178 yd (USGA/R&A, March 2022).

These are commercial datasets. Cohort definitions and filtering are not published in detail. NR-07
applies.

---

## 9. Measurement validity and validation targets

### 9.1 IMU against 3D motion capture

**Kim, S. et al. (2023).** *Validation of Inertial Measurement Units for Analyzing Golf Swing
Rotational Biomechanics.* *Sensors* 23(20), 8433. n = 36 (nine each: male professional, male
amateur, female professional, female amateur). IMUs mounted directly on T1 and L4 vertebrae,
compared against lab 3D motion capture.

| | ICC | Pearson |
|---|---|---|
| Upper torso rotation | 1.00 (1.00–1.00) | 1.00 (1.00–1.00) |
| O-factor (pelvic obliquity) — worst case | 0.91 (0.89–0.93) | 0.92 (0.92–0.93) |

Bland–Altman absolute mean differences: **0.61° to 1.67°** across upper torso rotation, pelvic
rotation, pelvic rotational velocity, S-factor, O-factor and X-factor. p < 0.001 throughout.

This is the current published ceiling for IMU-derived golf rotational kinematics. It was achieved
with sensors mounted directly on bony landmarks, not soft-tissue-strapped consumer IMUs.

**Plausibility gate.** The same paper notes an earlier four-sport IMU validation reporting
upper-torso rotation at top of backswing of ≈ 30°, against ≈ 100° reported for both professional
and recreational golfers elsewhere; the authors infer those participants were not experienced
golfers.

| ID | Statement |
|---|---|
| **NR-20** | Upper-torso rotation at top of backswing of ≈ 100° is PinPoint's plausibility gate. A pipeline returning ≈ 30° is faulted, not reported. |

### 9.2 Validation targets

| PinPoint output | Reference | Target |
|---|---|---|
| Upper torso / pelvis rotation angle | Kim et al. (2023) | Bland–Altman mean diff < 1.7°, ICC > 0.91 |
| X-factor | Kim et al. (2023) | as above |
| Pelvis peak rotational speed | Cheetham et al. (2008) | Professional cohort mean 477 ± 53 °/s — magnitude plausibility only |
| Sequence order detection | Cheetham et al. (2008) | Categorical: recovers pelvis→thorax→arm→club in professional-standard swings |
| Peak timing dispersion | Cheetham et al. (2008) | Recovers SD ratio ≈ 2× amateur against professional across a swing set |
| Face-on shaft angle | PinPoint internal | 0.49° RMSE (smoothed) |
| DTL shaft angle | PinPoint internal | 2.44° RTS RMSE (synthetic) |

NR-09 governs comparison of any smoothed PinPoint peak to the Cheetham values.

---

## 10. Literature limitations

Per the 2022 systematic review of 92 articles:

- Cohorts are overwhelmingly right-handed men. As of that review there are no articles focusing on
  women only, despite published male/female biomechanical differences.
- There is no consensus methodology across studies. Results are contradictory and performance
  parameters cannot be generalised.
- Sample sizes are small. The canonical rotational-biomechanics benchmark papers run n = 10–19 per
  group. The Cheetham 2008 professional cohort — the most-cited benchmark in the field — is 19
  swings, one per player.
- Almost all data is indoor optoelectronic laboratory capture, not on-course.

| ID | Statement |
|---|---|
| **NR-21** | The limitations above are disclosed to users at the point normative context is surfaced. Approved user-facing text: *"These reference ranges come from small lab studies of mostly male professional golfers. They're a useful compass, not a target — your swing isn't wrong because it sits outside them."* |

---

## 11. Bibliography

**Kinematic sequence**
- Cheetham, P.J., Rose, G.A., Hinrichs, R.N., Neal, R.J., Mottram, R.E., Hurrion, P.D., Vint, P.F.
  (2008). Comparison of kinematic sequence parameters between amateur and professional golfers.
  *Science and Golf V: Proc. World Scientific Congress of Golf*, Crews, D. & Lutz, R. (Eds.),
  pp. 30–36. Mesa, AZ: Energy in Motion.
- Cheetham, P.J. (2014). *The Relationship of Club Handle Twist Velocity to Selected Biomechanical
  Characteristics of the Golf Drive.* PhD Dissertation, Arizona State University.
- Cheetham, P.J. (2014). *Basic Biomechanics for Golf: Selected Topics.* White paper.
  https://philcheetham.com/media/Basic-Biomechanics-for-Golf-Selected-Topics-by-Phil-Cheetham-2014.pdf
- Tinmark, F., Hellström, J., Halvorsen, K., Thorstensson, A. (2010). Elite golfers' kinematic
  sequence in full-swing and partial-swing shots. *Sports Biomechanics.*
- Neal, R.J., Lumsden, R., Holland, M., Mason, B. (2007). Body segment sequencing and timing in
  golf. *Annual Review of Golf Coaching* 2007, pp. 25–36. Multi-Science, Brentwood, UK.
- Putnam, C.A. (1993). Sequential motions of body segments in striking and throwing skills.
  *Journal of Biomechanics* 26, 125–135.
- Milburn, P.D. (1982). Summation of segmental velocities in the golf swing. *Med Sci Sports Exerc*
  14(1), 60–64.
- Sprigings, E.J., Neal, R.J. (2000). An insight into the importance of wrist torque in driving the
  golf ball: a simulation study. *J Appl Biomech* 16, 356–366.
- Winter, D.A. (1979). *Biomechanics of Human Movement.* John Wiley and Sons. (Filtering distortion
  of peak timing/amplitude.)

**Rotational biomechanics and benchmarks**
- Meister, D.W., Ladd, A.L., Butler, E.E., Zhao, B., Rogers, A.P., Ray, C.J., Rose, J. (2011).
  Rotational biomechanics of the elite golf swing: benchmarks for amateurs. *J Appl Biomech* 27(3),
  242–251. PMID 21844613.
- Rose, J. et al. (2018). Golf swing rotational velocity: the essential follow-through. *Ann Rehabil
  Med.* PMC6246863.
- Rose, J. et al. (2022). The Swing Performance Index. *Front Sports Act Living.*
  doi:10.3389/fspor.2022.986281. PMC9816382.
- Zheng, N., Barrentine, S.W., Fleisig, G.S., Andrews, J.R. (2008). Kinematic analysis of swing in
  pro and amateur golfers. *Int J Sports Med.*

**X-factor**
- McLean, J. (1992). Widen the gap. *Golf Magazine*, December 1992.
- McTeigue, M., Lamb, S.R., Mottram, R., Pirozzolo, F. (1994). Spine and hip motion analysis during
  the golf swing. *Science and Golf II.*
- Egret, C.I., Dujardin, F.H., Weber, J., Chollet, D. (2004). 3-D kinematic analysis of the golf
  swings of expert and experienced golfers. *J Sports Med Phys Fitness.*
- Myers, J. et al. (2008). The role of upper torso and pelvis rotation in driving performance during
  the golf swing. *J Sports Sci.* n = 100 recreational golfers.
- Cole, M.H., Grimshaw, P.N. (2009). The X-factor and its relationship to golfing performance.
  *J Quant Anal Sports.*

**GRF and kinetics**
- Watson, A. et al. (2025). Ground reaction force and centre of pressure during the golf swing and
  associations with clubhead speed and skill level: a systematic review. *Sports Medicine.*
  doi:10.1007/s40279-025-02391-3.
- Bourgain, M. et al. (2020). Effect of horizontal ground reaction forces during the golf swing.
  *Proceedings* 49(1), 45. MDPI.
- Okuda, I. et al. Ground reaction forces in skilled and unskilled golfers (via Watson et al.).

**Wrist**
- Sweeney, M., Mills, P., Alderson, J., Elliott, B. (2012). Wrist kinematics during the golf swing:
  is flexion or ulnar deviation more important? *ISBS Proceedings.*
- Sweeney, M. et al. Wrist kinematics during the golf drive from a bilaterally derived model. *ISBS
  Proceedings.* https://ojs.ub.uni-konstanz.de/cpa/article/view/5195
- Fedorcik, G.G. et al. (2012). Differences in wrist mechanics during the golf swing based on golf
  handicap. *J Sci Med Sport.* doi:10.1016/j.jsams.2011.11.259.
- Examining the influence of grip type on wrist and club head kinematics during the golf swing
  (2018). *J Sports Sci.* PMID 30110244.
- Extensor carpi ulnaris activity and golf swing joint kinematics in sub-elite golfers (2023).
  *J Sports Sci.* doi:10.1080/02640414.2023.2285121.
- Hume, P.A., Keogh, J., Reid, D. (2005). The role of biomechanics in maximising distance and
  accuracy of golf shots. *Sports Medicine* 35(5), 429–449.

**Crunch factor and injury**
- Cole, M.H., Grimshaw, P.N. (2014). The crunch factor's role in golf-related low back pain. *Spine
  J.* PMID 24291405.
- Joyce, C. (2016). The crunch factor's relationship with trunk movements and swing/launch
  parameters. n = 20.

**Validity and instrumentation**
- Kim, S. et al. (2023). Validation of inertial measurement units for analyzing golf swing
  rotational biomechanics. *Sensors* 23(20), 8433. doi:10.3390/s23208433. PMC10611231.
- Golf swing biomechanics: a systematic review and methodological recommendations for kinematics
  (2022). *Sports* 10(6), 91. PMC9227529.

**Industry data**
- Trackman (2024). *Tour Averages.*
  https://www.trackman.com/blog/golf/introducing-updated-tour-averages
- Trackman. *What is Ball Speed?* https://www.trackman.com/blog/golf/ball-speed
- Titleist Performance Institute. *X-Factor Essentials*; *X-Factor: Why More Isn't Always Better.*
  mytpi.com

---

## 12. Open actions

| # | Action | Blocks |
|---|---|---|
| 1 | Obtain full text of Cheetham (2014) dissertation. Currently cited from abstract. | §7.4 |
| 2 | Obtain Meister et al. (2011) full text and transcribe the benchmark curves. Only correlations and CoVs are captured here; the curves are the useful artefact. | §4.1 |
| 3 | Characterise and record RTS smoother peak attenuation. | NR-09, §9.2 |
| 4 | Record PinPoint's X-factor segment definition in §1. | NR-08, NR-02 |
| 5 | Verify analysis window extends through follow-through. | NR-15 |
| 6 | Transcribe the full per-club Trackman 2024 tour averages table from https://www.trackman.media/tour-averages. Published as images; §8.1 currently relies on secondary sources. | §8.1 |

## 13. Changelog

| Date | Change |
|---|---|
| 2026-07-15 | Initial version. Absorbed `cheetham-golf-biomechanics.md`. Broadened to normative reference. Recast as binding design constraints (NR-01 … NR-21). |
