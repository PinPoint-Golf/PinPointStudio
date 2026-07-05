# Physics-Constrained Detection of a Golf Club from Fixed-Environment Video: Honest Measurement Tiers, Instrumented Truth Generation, and an Exposure-Arc Reading of Motion Blur

*PinPoint shaftlab programme — research report, 2026-07-05, covering the
programme from inception. Empirical basis: hand-labelled swings 0008/0009,
the c1 multi-club corpus (100 head labels), the tape_20260704 pilot and
tape_20260705 instrumented corpora; tooling `tools/shaftlab/`; records in
`docs/design/shaft_detection_*`, `clubhead_detection_design.md`,
`stripe_fusion_design.md`, `club_tracking_v3_design.md`, and lab
`tape_20260705/RESULTS.md`.*

## Abstract

We report the complete arc of a programme to measure a golf club's shaft
direction θ, projected scale s (foreshortening ρ), and clubhead position
from a single fixed face-on camera at its hardware ceiling (150 fps,
~6.6 ms exposure, fixed studio lighting with a deliberately blown hitting
area). The programme comprises: (i) a **passive two-stage detector**
(shaft, then clubhead along the shaft ray) developed through twenty-one
adjudicated fixes from a confidently-wrong v1 to a corpus-validated v7
(hand-label accuracy: measured-tier median 2.5°, 0% >30° on both output
tiers); (ii) an **instrumented-truth generator** exploiting a
retroreflective 2-1-3 band pattern of known geometry, developed through a
saturated-blob ratio matcher (v1: 1.1° median, zero flips, club-up phases
only) into a multi-evidence fusion detector (v2: zero adjudicated errors
over a 10-swing corpus, 1,033 truth samples concentrated in the fast
phases where truth previously did not exist); and (iii) a **physics-first
redesign** (v3) motivated by the finding that every counterfeit detection
in both detectors' histories violates one of four elementary facts about
the golf swing — the club starts at the hands, does not overlap the body
mid-swing, reverses rotation exactly once at the top, and forms a dual
pendulum with the lead arm. We show that the passive detector's fix
history amounts to a piecemeal, reactive rediscovery of these same facts,
and argue for encoding them a priori as constraints rather than
post-failure guards. We further contribute an *exposure-arc tomography*
reading of motion blur — at the measured 98% exposure duty cycle,
consecutive streaks tile the swing arc contiguously, making blur a
continuous angular record with sub-frame θ samples at streak edges and
single-frame |ω| from streak length — plus one-directional wrist-IMU
conditioning, conformal calibration of confidence tiers, and a
rotation-compensated shift-and-stack region-of-interest method.
Cross-scoring the passive pipeline against dense instrumented truth
corrected a contaminated estimate (stage-1's measured tier is 0.6% bad in
the fast phases, not 7%), exposed one genuine measured-tier failure and a
validation-label selection bias that had flattered the clubhead stage's
confidence calibration, and quantified that stage's honesty-clause failure
(7/10 swings) as the next tuning target.

## 1. Problem statement

A coaching-studio capture system must recover club kinematics from video
that is, by commercial and physical necessity, hostile to generic computer
vision:

- **The environment is at its ceiling.** 150 fps is the camera's maximum;
  exposure (6.574 ms, embedded per-stream in capture metadata) is
  effectively full-frame against the light budget; the studio downlight
  necessarily blows out the hitting area; a ring light sits at the lens.
  No hardware lever remains — all improvement must be algorithmic.
- **The shaft's appearance is regime-dependent.** Polished steel returns
  light specularly; retro bands return the ring light within a narrow
  cone. Depending on pose and background the shaft reads as a bright line
  over dark cloth, a *dark* line over the blown mat (bands white-on-white
  and truly invisible), discrete blazing dashes when up in the light, or a
  bloom-merged saturated stretch near the grip. A human fuses these
  effortlessly; single-threshold detectors cannot.
- **Peak angular velocity (~15–20°/frame near impact, 2,200–3,000°/s)**
  smears the shaft into arc sectors within one exposure.
- **The scene is counterfeit-rich**: shadows, mat edges, neon strips, a
  golf-bag shaft rack, trouser creases, leg lines, sleeve highlights,
  floral-shirt texture (quasi-periodic peaks that satisfy affine ratio
  matches), and mat speckle form collinear, temporally self-consistent
  structures that pass residual, flip, and photometric tests.

Two detector families serve different masters. The **passive detector**
(no markers) is the product path: it must run on any club and fail
honestly. The **instrumented detector** (taped club, known geometry) is
the measurement instrument: its output becomes ground truth for
validating and tuning the passive path, so a wrong entry silently corrupts
every downstream decision. Both live under the same governing rules,
learned early and expensively (§3.1): measured/predicted/absent tiers with
low-confidence detections discarded rather than emitted; confidence that
correlates with error (≥⅔ of bad frames low-confidence, ≤5% of
high-confidence frames bad); mean/p90/%-bad reporting split by tier, never
medians or totals alone; full-resolution visual adjudication before any
numeric conclusion; byte-identical determinism as regression and porting
oracle; and corpus gates for every promotion.

## 2. Research goals

**G1 (passive accuracy with honesty).** A shaft tracker whose measured
tier is trustworthy (~2–3° median, zero confident-wrong) across all swing
phases and studio conditions, and a clubhead stage that inherits that
trust through a frozen inter-stage contract.

**G2 (dense truth).** Per-frame instrumented truth — θ everywhere
achievable; s and head position where the band pattern resolves —
prioritising downswing/impact/thru, where the passive detector is blind
and hand-labelling is impractical (blur defeats human annotators too).

**G3 (methodological).** Deterministic, classical, portable reference
implementations; every acceptance rule traceable to an adjudicated
failure; synthetic machinery gates before real data; corpus gates before
generalisation; fixtures re-frozen only in reviewed events.

**G4 (strategic).** A bridge from instrumented lab clubs to unmarked
production clubs, admitting learned components only where classical
methods demonstrably saturate.

## 3. Methods

### 3.1 Programme governance: the exemplar-first rule

The programme's founding failure: a v1-style shaft tracker was ported to
C++ and wired into the application's markup panel before frame-level
validation existed; it produced confidently wrong markups and was
reverted. Since then the rule is absolute: algorithms are proven in a
Python exemplar with eyes on frames before any port. A second founding
lesson: **median error lies.** v1-era validation reported "median 7.4°"
while 24% of frames were >30° wrong at confidence 0.93–0.96. All
subsequent reporting splits by output tier and gates confidence honesty
explicitly. A third: **fixtures rot.** The v6 "freeze" fixtures proved
non-reproducible against re-prepared clips (encode noise × gate
marginality); fresh runs of the same code were *better* than the
documented freeze. Fixtures are now re-frozen atomically with code
promotions, and any re-prep invalidates them.

### 3.2 Passive stage 1: shaft tracking and the F1–F21 fix programme

Architecture: per-frame oriented evidence (ridge + antiparallel edge-pair
width prior) along candidate rays from the pose grip anchor; a tracking
fan with Kalman filtering and RTS smoothing; full-circle (re)initialisation
with escape; measured/predicted output tiers (F9): conf ≥0.5 in runs ≥4
frames is MEASURED; everything else is discarded and replaced by the
smoother/bridge/decay prediction, honestly labelled.

The fix history, compressed (full tables in the findings doc):

- **F1–F9 (→v6 prototype).** Run-start gate — evidence must begin near the
  hands ("the club is attached to the hands"); edge-pair width prior;
  forearm plausibility sector (tracking fan only); wrong-lock escape
  rescans; ω sanity clamp (real clubs peak ~2,500°/s; v1 once reported
  62,000); 180°-flip test crediting a distal clubhead blob; re-init
  confirmation (conf capped 0.35 until 3 accepted measurements);
  either-path acceptance (global support OR dense run) for foreshortened
  finishes.
- **F10–F14 (still-hold programme).** Stacked re-acquisition over still
  windows (average 9 frames, ~√K noise gain — the first use of temporal
  stacking in the programme); windowed stillness with still-period
  consistency (F11: a static club has one angle; outliers demoted);
  scene-permanence veto against permanent structure (F12), whose first
  implementation used a frame-0 snapshot that *contained the address club*
  and thereby vetoed impact-zone re-acquisition — the club returns to its
  address angle at impact — silently killing the downswing until a user
  eyeball caught it (fixed with a club-free whole-clip median); clubhead
  blob AND-test (bright AND changed); quasi-static gating of the hardened
  gates (every distractor lock occurred at stillness; global application
  strangled mid-downswing recovery).
- **F19–F21 (anti-distractor, c1-driven).** A code audit found both
  anti-distractor gates wired behind `quasi_static` — fast-motion re-inits
  had *no veto at all* — and a `swing_seen` guard that disabled permanence
  through the entire backswing. Fixes: any-speed strict permanence veto
  (adjudicated kill: a golf-bag shaft rack); an in-motion permanence
  reference (median over highest-motion frames — park-free by
  construction, protecting both the address club and the finish hold);
  sector-conditioned hold gates resolving the four-quadrant
  body-adjacency × blob-credit confusion (the shouldered finish club is
  legitimately body-adjacent and blob-free; body-line junk with bright
  shoes is neither).
- **Tried and rejected** (each by corpus A/B, kept as negative results):
  kinematic confidence shaping for fast-born segments (good and junk
  segments overlap on *every* kinematic statistic — the class is
  content-distinguishable only); forearm sector at fast flips (breaks
  takeaway, where the club genuinely lies near the forearm); shoulder-arm
  collinearity veto (never fired — the locks were scenery, not arms); blob
  rescue on the permanence veto (the moving golfer makes junk "changed");
  hold-density gates (collapsed real dark-stratum holds).

In hindsight — and this is a central observation of this report — F1
(hands attachment), F3/F21 (forearm/pendulum), F5 (ω bound), F12/F19/F20
(scene vs club), and F16-class body gates are *piecemeal, reactive
encodings of the same four physical fundamentals* that §3.8 promotes to a
priori constraints. Each was installed only after its counterfeit was
adjudicated; none was derived from the swing model in advance.

### 3.3 Passive stage 2: clubhead along the ray

Decoupled by a frozen contract (frame, grip, θ, kind, conf — the only
stage-1 output stage 2 may read; development against frozen stage-1
CSVs). Per-frame head measurement: gap-tolerant on-axis terminus,
multi-width edge pairs, permanence veto, and candidate scoring under a
projected-length prior. The length model (M0–M4) supplies that prior; the
production path is a per-swing *censored self-fit* (foreshortening
censors the observable length; the fit must respect that). An arm-length
plausibility floor rejects heads inside the golfer's reach envelope;
segmented 1-D Kalman + per-segment RTS with measured/predicted/off tiers
and a flip check complete the stage. The contract's value was demonstrated
when v7's stage-1 improvements changed stage-2 outputs gracefully with
zero stage-2 code changes.

### 3.4 Corpora, labels, and validation strata

Hand-labelled: swing 0008 (51 shaft labels), 0009 (18, deliberately
pathological: overhead wrap, hanging club), later 100 clubhead labels
across the c1 corpus (10 uncropped swings, new studio, 8 club types —
the first off/crop-tier and multi-club stratum). Corpus discipline:
single-swing residuals never select a model form; decisions defer until
corpus-gated. Capture guidance itself became a finding (framing, exposure
strata). Hand labels have a structural limit that §4.6 quantifies: humans
can only label frames where they can see the club, which biases validation
toward exactly the frames detectors find easy.

### 3.5 Instrumentation and geometric invariants

The instrumented 7-iron carries six 25 mm retro bands at
308/362/560/758/808/854 mm from the butt (2-1-3 grouping; hosel 882 mm;
length 940 mm). Two invariants carry the analysis: *ratio preservation*
(along-shaft distances project linearly, so the pattern matches at any
orientation via t = s·(r − r₀)) and *asymmetry* (2-1-3 breaks 180°
symmetry — though tip-trio subsets, 46/50 mm gaps, are flip-ambiguous at
realistic scale and require ~1 px positional discrimination; intensity
templates cannot tell the flip at all). The 2026-07-04 pilot triaged the
tape's effect on the *passive* detector: it helps address/backswing
(grazing light, extra contrast), leaves downswing blur loss unchanged, and
*hurts* the finish through F11's still-period consistency — two confident
θ clusters inside one grip-still run mutually demote to conf 0.35
("still grip ≠ static club"), an adjudicated F11 design flaw awaiting a
corpus-gated fix.

### 3.6 Raw capture and decode

Raw Bayer sidecars (BayerRG8, 746 frames/swing) decode with the same
edge-aware demosaic as the application into FFV1-lossless clips — no
encoder generation between sensor and measurement; per-stream exposure
flows from capture metadata into analysis automatically. A controlled A/B
showed the compressed path *helps* slow phases (encoder denoising) and
ties in fast phases: detector robustness, not decode fidelity, was
binding.

### 3.7 Instrumented truth, generation one and two

**v1 — saturated-blob ratio matching:** threshold 235, connected
components, RANSAC collinearity gated on the grip anchor, order-preserving
affine ratio match, positional flip test, dark-within-group-gap check.
1.1° median, zero flips — but only where bands bloom discrete (club-up
phases), and §5.1 records its address-waggle contamination.

**v2 — multi-evidence fusion** (`stripe_fusion.py`): adds a
polarity-aware ridge (lateral **max** vs background over dark cloth,
lateral **min** over blown background — a mean washes out a 2–3 px line
under sub-pixel misalignment — with piecewise sign coherence); temporal
evidence (pixel-median stacks over pose-still runs; scene-median
subtraction in motion); dense profile machinery (percentile local-steel
estimation because the tip trio self-masks a median; bounded-window
*prominence* peaks because any scalar baseline hallucinates peaks on
sloped specular steel; peak-assignment (s, r₀) grids where hypotheses pay
for prominent peaks they strand). Honesty mechanisms, each earned by an
adjudicated failure: forearm veto; impact-gap interpolation guards (θ
sweeps >180° through impact in ~37 frames); and the load-bearing
*motion-verified corroboration* — matched blobs must travel with the
hands (≥ max(1.5 px, 0.25·hand displacement)); no lone locks; no
static-period locks (in a hold, motion cannot separate club from
counterfeit and the counterfeits pass every appearance gate — therefore
unverifiable, therefore never emitted). Machinery is gated on a
randomized synthetic generator (blown region with inverted contrast,
bloom-saturating steel, speckle, anchor noise; harsh + easy regimes: easy
*requires* full locks, harsh requires honest abstention from scale).

### 3.8 Physics-first constraint system (v3 design)

All surviving and historical counterfeits violate swing physics the
methods never encoded a priori. v3 promotes four fundamentals to
constraints evaluated *before* the evidence engines: **C1**
butt-termination — club evidence terminates ≤ ~260 mm behind the grip; a
line continuing behind the butt is scene structure (the full-strength form
of F1); **C2** phase-scheduled body-overlap veto — from takeaway to
follow-through the club is in free space; candidates majority-supported
inside the skeleton-derived body polygon are vetoed; at
address/impact/finish body evidence is admitted but never sufficient
alone (systematising F16/F21); **C3** a phase model segmented from the
hand trajectory alone with known rotation sign per phase — direction
flips become structurally impossible, outage bridging becomes a monotone
bounded-rate sweep (subsuming F4/F6 and the v2 interp guards); **C4** the
dual-pendulum reachable cone — θ confined to anatomically feasible wrist
angles about the measured lead-arm direction (the full form of F3/F21's
sector). A global dynamic-programming estimator over θ replaces local
corroboration; the validated evidence engines are unchanged as emission
terms.

### 3.9 Rotation-compensated shift-and-stack (v3.1)

For a frame window and rotation hypotheses ω(t) drawn from the C3/C4
prior: register on the grip anchor, rotate each frame by −∫ω dt about it,
stack. Under the correct hypothesis club pixels integrate coherently
(~√N) while body and background smear; the coherence maximum yields the
club region of interest, a refined ω, and a partially deblurred composite
in which the band pattern can re-emerge. The body polygon is excluded
from the coherence measure (C2); C1 is tested on the composite;
coarse-to-fine ordering bounds compute. F10's still-stack was the
zero-rotation special case; this generalises it to the phases that matter.

### 3.10 Exposure-arc tomography (novel)

Measured duty cycle: 98.1% (6.574/6.699 ms). Consecutive frames' streaks
therefore tile the swing arc *contiguously* — the angular sector swept in
frame t ends, to within 1.9% of a frame, where frame t+1's begins. Motion
blur is not noise but a nearly gap-free continuous angular record of
θ(t). Three measurements follow: (i) the leading/trailing *edges* of a
band's streak sector are θ samples at known sub-frame times (shutter
open/close), doubling temporal resolution exactly where θ changes
fastest; (ii) streak arc-length ÷ band width gives per-frame |ω| from a
*single* frame, no differencing; (iii) cross-frame sector continuity
validates or rejects whole windows at once. This reframes the impact zone
— the one remaining coverage hole — as potentially the *densest*
measurement region, and composes with §3.9: the stack finds the corridor;
sector-edge extraction reads θ(t) inside it.

### 3.11 Cross-modal IMU conditioning (novel in this pipeline)

Each swing already records a wrist IMU, time-aligned with captured
latency metadata. It provides: a club-independent transition (top)
instant corroborating C3; an ω(t) prior that collapses the DP transition
model and the shift-and-stack hypothesis set from tens to a few; and the
only fully vision-independent witness available — a lock sequence whose
implied ω contradicts the IMU is quarantined for adjudication. Fusion is
one-directional (IMU conditions the vision *search*; vision truth is
never fitted to the IMU), preserving the truth generator's independence.

### 3.12 Learned components (v3.3), subordinated to physics

A small heatmap keypoint network for clubhead **heel/toe/hosel**,
evaluated only inside the physics ROI, trained on a data flywheel: stripe
truth supplies weak labels (shaft line, scale, hosel extrapolation);
stratified human adjudication promotes a subset to gold; blur
augmentation is synthesized from *measured* ω(t) so hard examples match
this camera's physics. Two additions of our own: a *physics-consistency
training loss* (heel on the truth ray at hosel radius; heel–toe axis
within loft/lie bounds of shaft-perpendicular), and **conformal
calibration** — split-conformal prediction sets give finite-sample
coverage guarantees mapping directly onto the honesty clauses, making
"≤5% of high-confidence frames bad" a property enforced by construction
on exchangeable data rather than audited after the fact. Weights frozen
and versioned; deterministic inference; acceptance requires beating the
classical stage-2 head on held-out instrumented swings. Shaft θ and phase
segmentation stay deliberately unlearned (classical + physics already
deliver ~1° with zero flips; a network there adds risk, not accuracy).

## 4. Results

### 4.1 Passive stage 1 (hand-label evaluations)

- **v1**: confidently wrong — locked a shadow/mat edge at address (43° vs
  ~98° true) and, with no escape mechanism, stayed wrong all swing while
  reporting LINE_OK; all-frames mean 18.8° with no honesty signal.
- **v6 prototype (F1–F9)**: 0008 measured tier n=39/51, median 2.9°, p90
  7.1°, 5% >30°; 0009 zero confident-wrong frames (broken follow-through
  correctly declared as prediction).
- **v5n still-hold programme**: finish-region measured coverage 4→49%
  (0009) and 19→37% (0002) with landmarks verified at full resolution.
- **v7 (fresh-baseline comparison)**: 0008 measured median 2.5°, **0%
  >30° on both tiers**; measured coverage 57→63%, finish 32→52%; c1
  label bad>30 16→14; the s9v2 "hang region" corrected from a confident
  92.5° junk lock to measured 65–69° on the visible shaft; one open
  counterfeit class survives (quasi-static finish body-lines with
  bright-shoe blob credit).
- **Stage-2 decoupling test**: v7 stage-1 fixtures changed, stage-2 code
  untouched — head measured medians 18.8/19.5 px on 0008/0009 with
  honesty clauses passing: the contract held.

### 4.2 Instrumented truth (tape_20260705 corpus)

- **Synthetic gate (fusion core)**: 26/26 correct band locks, θ max
  1.17°, scale error ≤1.6%, zero flips, honest abstention where bands are
  not discrete.
- **v1 blob-ratio**: θ median 1.1°, p90 3.4°, zero flips in the anchor
  tier; 46–85 anchors/swing, club-up phases only; 713 truth entries —
  later found contaminated at address-waggle frames (§5.1).
- **v2 fusion**: per-swing band-tier medians 1.1–3.3°, ray-tier
  0.4–0.8°; **zero adjudicated errors**; byte-identical determinism;
  coverage 43–75% of downswing and 46–59% of thru-swing frames; 1,033
  truth entries. Fusion measures correctly through frames where stage-1's
  own track coasts ~160° wrong (flip-book adjudicated).
- **Cost of honesty-by-abstention**: each v2 guard removed a junk class
  *and* real coverage — finish and address emissions reached zero; the
  stacked still tier was cut after whole address runs self-corroborated a
  shirt-texture counterfeit at a self-consistent wrong scale.

### 4.3 Cross-scoring the passive pipeline against instrumented truth

- **Correction**: v1-truth contamination had inflated "stage-1 measured
  tier 7% bad in fast phases"; against clean truth v2 the figure is
  **4/643 (0.6%)**, 9/10 swings at zero.
- **Discovery**: the 4 bad frames are a *genuine* stage-1 measured-tier
  failure — during one takeaway it confidently tracks a leg shadow while
  the banded club is plainly visible; the first machine-documented
  failure of that tier (its hand-label record was clean).
- **Localisation**: stage-1's predicted tier is 13–22% bad (>30°) on
  6/10 swings, frame-exactly in the blur-zone coast.
- **Label-distribution finding**: the clubhead stage *passed* its honesty
  clauses on 51+18 hand labels yet *fails* them on 7/10 swings against
  dense fast-phase truth (high-confidence-bad 9–34% vs the ≤5% clause),
  with per-swing length-error means to −215 px implicating the censored
  self-fit. Hand labels are placeable only where humans see the club —
  which is where detectors succeed too. Prior validation was
  systematically biased easy; dense instrumented truth removes that bias.

## 5. Discussion

### 5.1 Error catalogue (programme-wide)

**Passive-detector era**: the v1 confident-wrong lock class (no escape;
one bad init poisons a whole swing); the premature C++ port (reverted;
origin of the exemplar-first rule); "median error lies" (24% >30° behind
a 7.4° median at conf 0.93+); the F12 frame-0 snapshot that contained the
address club and silently vetoed the downswing (the club returns to its
address angle at impact); gates wired behind `quasi_static` leaving
fast-motion re-inits entirely unguarded, and `swing_seen` disabling
permanence for whole backswings (found by code audit, not by metrics);
non-reproducible frozen fixtures; F11's "still grip ⇒ static club"
premise, falsified by the taped pilot (finish clusters mutually demote);
the five rejected fixes (§3.2) as documented negative results.

**Instrumented era, method errors** (each now a named regression case):
tip-trio flip ambiguity (positional rms discriminates; intensity cannot);
a top-N-by-area blob cap discarding real 1–25 px² band specks ranked
58th–132nd; admitting 60 proximity-sorted blobs making junk affine fits
combinatorially dominant (n=4 median error 112°); median local-steel
self-masking of the tip group; scalar-baseline peak extraction
hallucinating ~26 peaks/ray on sloped specular steel; a swing-median
scale gate that was *physically wrong* (s halves under foreshortening —
that is ρ — and the gate destroyed real finish locks while nearby junk
survived); streaked-band centroids leaving the shaft line ~2 frames
around impact (mutually-consistent wrong locks passed corroboration and
chained flipped rays); rate-scaled then absolute motion thresholds each
leaking on slow waggle until the noise-floored proportional form; the
formal conclusion that static-period locks are unverifiable.

**Tooling errors**: the synthetic generator read background values from
the image being drawn into, a feedback loop that manufactured echo peaks
after every band — two tuning cycles were spent fitting detectors to an
artifact. Synthetic gates need their own adjudication: render and eyeball
the synth.

**Epistemic errors**: "no ring light present" was confidently inferred
from absent saturated blobs at address and was wrong — corrected only by
full-resolution pixel inspection after a challenge; v1 truth was trusted
for downstream scoring before cross-detector disagreement exposed its
contamination (truth generators need adversarial validation against each
other, not just against their consumer); all corpus accuracy figures use
stage-1's measured tier as cross-check referee, and the leg-shadow case
proves that referee imperfect — "zero errors" formally means zero
*adjudicated* errors under a visually-verified but fallible referee; and
the label-selection bias of §4.3 stands as a warning that a validation
regime can pass its clauses simply because its labels avoid the hard
frames.

### 5.2 Honesty by abstention versus by discrimination

The programme's central methodological finding, visible only across its
whole history. Both detector families converged on the same trajectory:
generic evidence engine → adjudicated counterfeit → guard → coverage
loss → repeat. Guards (permanence vetoes, quorums, motion corroboration,
stillness gating) remove junk by refusing to measure under conditions
junk exploits; real signal shares those conditions, so coverage decays
monotonically with robustness — measured directly in v2's finish and
address going to zero, and earlier in v5's hardened gates strangling
downswing recovery until F14 confined them. Physical constraints
discriminate *within* those conditions: every catalogued counterfeit —
shadow, mat edge, neon, bag rack, trouser crease, leg line, arm, shirt
texture, speckle constellation — fails at least one of C1–C4, while no
true club configuration fails any. The passive fix history is itself the
strongest evidence: its most durable fixes (F1, F3, F5, F16/F21, F12/F20)
are exactly the fundamentals, discovered one adjudication at a time. The
v3.0 corpus gate tests the resulting falsifiable claim: constraints-first
recovers the abstained coverage at zero readmitted junk.

### 5.3 Limitations and threats to validity

One athlete, one studio geometry, right-handed only; the instrumented
corpus is a single club on a single day (the c1 corpus has breadth of
clubs but only clubhead labels). The pose anchor is both input and bias
(force-through-anchor sampling demonstrably disadvantaged the true ray at
address; lateral fitting is designed, unvalidated). Truth heads are
on-axis extrapolations at 940 mm and differ definitionally from the
visual centroid stage-2 targets, bounding how hard stage-2 should be
pushed against them. Address-phase scale truth is *optically absent* at
this exposure (bands bloom or vanish); no algorithm recovers information
the data does not contain. Hand-label and instrumented-truth regimes
disagree about stage-2 calibration (§4.3) — until a labelled hard-frame
subset exists, part of that gap could in principle be truth-definitional
rather than detector miscalibration. Determinism is verified per-machine;
cross-platform bit-equality (the port oracle) is untested.

### 5.4 Future research

In gate order: **v3.0** constraint system + DP (gate: coverage recovery
at zero junk; the counterfeit-regression suite — every catalogued failure
as a named test — green); **v3.1** shift-and-stack with exposure-arc
sector-edge extraction as its measurement layer (gate: smooth ω(t) with
physically plausible peak through impact ±10 frames, adjudicated on
stacked composites); **v3.2** address θ via mat-crossing prior and
lateral fits; **F11 redesign** in the passive tracker (cluster still-run
measurements or split runs at confident θ jumps — corpus-gated against
the v7h fixtures); **IMU conditioning** (§3.11 — cheap, uses captured but
unused data, the only vision-independent witness); **stage-2
re-calibration** against dense truth, then **conformal honesty** across
all stages (clauses as guarantees, not audits); **metric grounding** (the
detected ball plus address hosel give per-session mm/px, converting s to
absolute ρ and enabling cross-session pooling); **multi-club,
left-handed, and hard-frame-labelled corpora**; the **heel/toe network
and data flywheel** (§3.12) as the bridge to unmarked clubs —
strategically the point of the entire instrumented programme; and the
**C++ port** behind the existing detector interface with the exemplar as
byte-oracle, marker and passive modes together.

## 6. Conclusion

Across two detector families, three method generations, and roughly two
dozen adjudicated counterfeits, the programme's evidence supports one
conclusion: in a fixed, hostile capture environment, the discriminative
power that generic computer vision lacks is available for free in the
physics of the golf swing — and both detectors had been rediscovering
that physics piecemeal, one post-mortem at a time. Generic evidence
engines are necessary, and ours are validated (passive measured tier
2.5° / 0% bad on hand labels; instrumented band tier ~1° with zero flips)
— but without a priori physical constraints they either hallucinate
counterfeits or, guarded, abstain themselves into sparsity. The
instrumented-truth corpus produced here (1,033 verified fast-phase
samples) has already repaid its cost: it corrected a wrong conclusion
about the passive detector, documented the first failure of that
detector's most-trusted tier, exposed a label-selection bias in all prior
validation, and converted the clubhead stage's miscalibration from an
impression into a per-frame measurement. The v3 programme — constraints
first, blur as signal, the IMU as witness, learning only where perception
genuinely runs out — is specified with falsifiable gates; its central
hypothesis is the right kind of risky, and the instruments to test it now
exist.
