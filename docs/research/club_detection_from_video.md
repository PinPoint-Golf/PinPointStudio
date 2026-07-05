# Physics-Constrained Detection of a Golf Club from Fixed-Environment Video: Honest Measurement Tiers, Instrumented Truth Generation, and an Exposure-Arc Reading of Motion Blur

*PinPoint shaftlab programme — research report, 2026-07-05, covering the
programme from inception. Empirical basis: hand-labelled swings 0008/0009,
the c1 multi-club corpus (100 head labels), the tape_20260704 pilot and
tape_20260705 instrumented corpora; tooling `tools/shaftlab/`; records in
`docs/design/shaft_detection_*`, `clubhead_detection_design.md`,
`stripe_fusion_design.md`, `club_tracking_v3_design.md`, and lab
`tape_20260705/RESULTS.md`.*

## Abstract

We report the full arc of a programme to measure a golf club from a single
fixed, face-on studio camera running at its hardware ceiling (150 fps,
~6.6 ms exposure, a deliberately blown-out hitting area). From that one
view we recover, per frame, the shaft's image-plane direction **θ**, its
projected scale **s** (which shrinks under foreshortening, **ρ**), and the
clubhead position — the raw inputs to the downstream coaching metrics
detailed in §1.

The programme has three parts. (i) A **passive, markerless two-stage
detector** — shaft first, then clubhead along the shaft ray — carried from a
confidently-wrong v1 to a corpus-validated v7 through twenty-one
individually-adjudicated fixes (measured-tier accuracy: median 2.5°, 0% of
frames worse than 30°). (ii) An **instrumented-truth generator** that reads
a known retroreflective band pattern on a taped club, evolving from a
blob-ratio matcher (1.1° median, zero flips, but only in the slow club-up
phases) into a multi-evidence fusion detector (zero adjudicated errors over
a ten-swing corpus; 1,033 truth samples in the fast phases where none
existed before). (iii) A **physics-first redesign (v3)** built on a single
observation: every false detection in either detector's history violates one
of four elementary facts about a golf swing — the club is held in the hands,
does not overlap the body mid-swing, reverses rotation exactly once at the
top, and forms a double pendulum with the lead arm. We argue the passive
detector's whole fix history was a slow, reactive rediscovery of these
facts, and that they belong a priori as constraints rather than as
post-failure guards.

We further contribute an **exposure-arc** reading of motion blur — at the
measured 98% shutter duty cycle, consecutive frames' streaks tile the swing
arc almost gaplessly, turning blur into a continuous angular record
(sub-frame θ at streak edges, single-frame speed from streak length) — plus
one-directional wrist-IMU conditioning, conformal calibration of the
confidence tiers, and a rotation-compensated shift-and-stack. Grading the
passive detector against the new dense truth paid off at once: it corrected
a contaminated figure (stage-1's measured tier is 0.6% bad in the fast
phases, not 7%), exposed a genuine failure of that tier and a
label-selection bias that had flattered the clubhead stage, and turned that
stage's honesty failure (7 of 10 swings) into a precise, per-frame tuning
target.

## 1. Introduction

### What we measure, and why a degree matters

The quantity at the centre of this report is **θ (theta)**: the direction
the club's shaft points *as the face-on camera sees it*, measured as an
angle in the image plane and taken from the **grip** — the point where the
hands hold the club, which a pose estimator locates for us in every frame.
The absolute zero of θ is arbitrary; what matters is θ(t), the shaft angle
traced through the swing, which *is* the swing as far as this one camera is
concerned. Two companion angles travel with it: **α**, the direction of the
lead arm (also from the pose), and their difference **ψ = θ − α**, the
**wrist angle** — how the club is hinged relative to the forearm. Alongside
the angles we recover **s**, the shaft's *projected scale* (how many pixels
correspond to one millimetre of shaft), which shrinks whenever the club
tilts toward or away from the lens — the effect called **foreshortening
(ρ)** — and the **clubhead position** in the image.

None of these is the deliverable a golfer actually sees; they are the
*inputs* to almost everything a coach reads. In PinPoint's shot-analyzer
pipeline
([`docs/design/shot_analyzer_design.md`](../design/shot_analyzer_design.md)),
the shaft direction θ feeds **swing plane, shaft lean, club path, attack
angle, clubhead speed, and a face-angle proxy**; the lead-arm angle α feeds
**lead-arm flexion and the kinematic sequence**; and the wrist angle
ψ = θ − α *is itself* the headline metric of a Wrist session — the
flexion/extension and radial/ulnar deviation a coach diagnoses. Each of
these is then scored against a reference band that is itself only a few
degrees wide, which is exactly why accuracy of a degree or two is not a
nicety but the requirement: an error in θ does not stay contained, it
propagates straight into a number the golfer will act on. A shaft angle
that is casually 10° wrong is not a slightly worse measurement — it is a
*different diagnosis*. Meeting that standard, and being honest about the
frames where it cannot be met, is what the rest of this report is about.

### Why the measurement is hard

A coaching-studio capture system has to recover club motion from video
that — by commercial and physical necessity — is close to a worst case for
ordinary computer vision. It is worth being specific about *why* the scene
is so hostile, because each difficulty below drove a design decision later
in the report.

- **The camera is already at its limit.** 150 frames per second is the
  fastest this camera runs. The exposure — 6.574 ms, recorded per-stream
  in the capture metadata — is essentially as long as the frame allows,
  because there simply is not enough light to shorten it. The studio's
  ceiling downlight has to blow out the hitting area to light the golfer
  properly, and there is a ring light around the lens. None of these are
  bugs to be fixed; they are the fixed reality. There is no hardware knob
  left to turn, so every improvement from here has to be algorithmic.

- **The shaft looks completely different depending on where it is.** Its
  appearance is *regime-dependent*, and this is the single fact that
  defeats naive detectors. Polished steel is a mirror: it reflects light
  *specularly*, throwing a bright highlight only when the angle happens to
  line up, and looking dark otherwise. The reflective bands, by contrast,
  are *retroreflective* — they bounce light straight back toward wherever
  it came from, so under the camera's own ring light they blaze, but only
  within a narrow cone. Put those two facts together with a changing pose
  and a changing background and the same shaft reads, from moment to
  moment, as: a bright line against dark trousers; a *dark* line against
  the blown-out mat (where the white bands sit on white and vanish
  entirely); a row of separate blazing dashes when it is up in the light;
  or a smeared, bloom-merged bright smudge near the grip. A person fuses
  all of these into "that's the club" without effort. A detector built
  around a single brightness threshold cannot — whatever threshold it
  picks is wrong for most of the swing.

- **Near impact the club moves faster than the shutter can freeze.** At
  its peak the shaft sweeps roughly 15–20° *per frame* — about
  2,200–3,000°/s — so within a single exposure it does not appear as a
  line at all but as a smeared arc-shaped sector.

- **The scene is full of things that look like a club but aren't.** We
  call these *counterfeits*. Shadows, the edges of the mat, neon strips, a
  golf-bag shaft rack, creases in the trousers, the lines of the legs,
  highlights running down a sleeve, the quasi-periodic texture of a floral
  shirt (whose repeating bright peaks can even satisfy the band-ratio
  match described later), speckle on the mat — all of these form straight,
  club-shaped structures that persist frame to frame and that sail through
  the obvious sanity checks (a residual test, a flip test, a photometric
  test). The scene does not just add noise; it actively manufactures
  plausible fakes.

Two different detectors serve two different masters, and it is important
to keep them distinct. The **passive detector** uses no markers. It is the
product path — it has to run on whatever club the golfer walks in with,
and when it cannot see the club it has to say so honestly rather than
guess. The **instrumented detector** works on a specially taped club whose
band geometry we know exactly. It is not a product; it is a *measuring
instrument*. Its whole job is to generate ground truth for grading and
tuning the passive path — which means a single wrong entry from it
silently poisons every downstream decision we make about the passive
detector. Because the instrument's errors are so much more costly than a
mere missed frame, both detectors are held to the same governing rules,
which we learned early and paid for dearly (see §3.1):

- Output is split into **tiers** — *measured*, *predicted*, and *absent* —
  and any detection we are not confident in is thrown away rather than
  emitted. (We would rather have a gap we admit to than a number we
  secretly doubt.)
- **Confidence has to track error.** Concretely: at least two-thirds of
  the genuinely-wrong frames must carry low confidence, and no more than
  5% of the high-confidence frames may be wrong. A detector that is
  confidently wrong is worse than useless, because it defeats the human
  review it is supposed to enable.
- Results are always reported as **mean, 90th percentile, and %-bad, split
  by tier** — never as a lone median or a single lumped total, because
  those hide exactly the failures we care about (see §3.1 on "median error
  lies").
- Every numeric conclusion is **checked by eye at full resolution** before
  it is trusted.
- The code is **byte-for-byte deterministic**, which lets us use an exact
  re-run both as a regression test and as the contract for a future port
  to C++.
- Every promotion from one version to the next has to clear a **corpus
  gate** — a body of many swings — not just one good example.

## 2. Research goals

Four goals organise the work.

**G1 — passive accuracy, but with honesty.** We want a shaft tracker whose
*measured* tier can genuinely be trusted — around 2–3° median error, and
never confidently wrong — across every phase of the swing and every
lighting condition in the studio. And we want a clubhead stage that
inherits that trust through a frozen contract, so improving one stage
cannot quietly break the other.

**G2 — dense truth.** We want per-frame ground truth from the instrumented
club: θ everywhere we can get it, and s and head position wherever the
band pattern is legible. The priority is precisely the downswing, impact,
and follow-through — the fast phases where the passive detector is blind
*and* where hand-labelling is impossible, because the blur defeats human
annotators just as thoroughly as it defeats the algorithm.

**G3 — a defensible method.** Every reference implementation should be
deterministic, classical, and portable. Every acceptance rule should be
traceable to a specific failure someone actually looked at. Synthetic test
data should gate the machinery before real data is ever touched, and a
corpus should gate any claim of generalisation. Fixtures — the frozen
reference outputs we compare against — should only be re-frozen as part of
a deliberate, reviewed event.

**G4 — a bridge to the real product.** Ultimately we want to carry what we
learn from the instrumented lab club over to unmarked, everyday clubs — and
we want to admit learned (machine-learning) components only where the
classical methods have demonstrably run out of road.

## 3. Methods

### 3.1 How the programme is governed: prove it on the exemplar first

The programme has a founding failure that shaped everything after it. An
early, v1-style shaft tracker was ported to C++ and wired into the
application's markup panel *before* anyone had built a way to check it
frame by frame. It produced confidently wrong markups in the live app and
had to be ripped back out. The rule since then is absolute: an algorithm
is proven in a Python *exemplar*, with a human looking at real frames,
before a single line of it is ported into the product.

A second founding lesson, and one we repeat throughout: **the median
lies.** Our v1-era validation proudly reported a "median error of 7.4°" —
while 24% of the frames were more than 30° wrong, and wrong at a
confidence of 0.93–0.96. A median is dominated by the easy frames and says
nothing about the tail, and the tail is where a coaching tool does its
damage. So from then on every report splits results by output tier and
tests the confidence-honesty clauses explicitly.

A third lesson: **fixtures rot.** The "frozen" reference outputs from
version 6 turned out not to be reproducible when we re-prepared the video
clips — a combination of video-encoding noise and detections that sat right
on the edge of a gate. Awkwardly, fresh runs of the very same code were
*better* than the numbers we had written down as the freeze. The fix is
procedural: fixtures are now re-frozen atomically, in the same reviewed
commit as the code they belong to, and re-preparing a clip automatically
invalidates its fixtures.

### 3.2 Passive stage 1: tracking the shaft, and the F1–F21 fix programme

The shaft tracker works like this. In each frame it gathers *oriented
evidence* along candidate rays fanning out from the grip — where "grip"
comes from a pose estimator that finds the golfer's hands. The evidence is
a combination of a ridge response (a thin bright-or-dark line) and an
antiparallel edge pair the right width apart (the two sides of a shaft).
On top of the single-frame evidence sits a *tracking fan* — a Kalman
filter that predicts where the shaft should be next and only looks nearby,
plus RTS smoothing that cleans the whole trajectory up afterward by
sweeping backward through it. When the track is lost, the detector rescans
the full circle to re-initialise, with an escape mechanism so it cannot
lock permanently onto the wrong thing. And its output is tiered (this is
fix F9): a detection is labelled **MEASURED** only when its confidence is
at least 0.5 and it persists in a run of at least four frames; everything
weaker is discarded and *replaced* by an honestly-labelled prediction from
the smoother, a bridge across the gap, or a decaying extrapolation.

A word on the notation before the history. Individual fixes are labelled
**F1, F2, … F21**. Each `Fn` is *one* named, separately-adjudicated change
to the detector — each motivated by a specific frame someone could point at
and verified on that same frame — so the numbering is simply a running
ledger of every distinct thing we had to teach the tracker, in the order we
learned it. The complete before-and-after table for all twenty-one lives in
[`docs/design/shaft_detection_exemplar_findings.md`](../design/shaft_detection_exemplar_findings.md)
(the clubhead stage keeps its own parallel `Hn`-series in
[`clubhead_detection_design.md`](../design/clubhead_detection_design.md)).
The full ledger is long, so rather than march through it entry by entry the
account below compresses into four episodes — and the *shape* of each
episode matters more than any individual fix.

- **F1–F9 (building the v6 prototype).** This is where the tracker learned
  its basic manners. A *run-start gate* insists that the evidence begin
  near the hands — because "the club is attached to the hands," the single
  most useful fact in the whole system (F1). An *edge-pair width prior*
  demands two parallel edges the right distance apart, killing single
  edges like a mat border (F2). A *forearm plausibility sector* restricts
  the tracking fan to angles near the lead arm — but only while tracking,
  never during a full-circle re-init, because at the finish the club wraps
  around and the forearm assumption breaks (F3). *Wrong-lock escape*
  rescans and forces a re-init when a distant peak decisively beats the
  current track twice (F4). An *ω sanity clamp* rejects impossible
  spin rates — real clubs top out around 2,500°/s, and v1 once cheerfully
  reported 62,000°/s (F5). A *180°-flip test* checks a ray and its exact
  opposite and credits a bright clubhead blob at the far end to decide
  which way round the club actually points (F6). A *re-init confirmation*
  caps confidence at 0.35 until three good measurements have followed, so
  a shaky new lock cannot immediately claim certainty (F7). And an
  *either-path acceptance* rule admits a detection on either global
  support *or* a dense local run, so a foreshortened club at the finish
  (whose evidence is spread thin over a long ray) is not thrown out (F8).

- **F10–F14 (the still-hold programme).** The finish of a swing often ends
  with the club held nearly still, and a single frame of a static, dimly
  lit club is marginal evidence. The answer was *temporal stacking*:
  average several still frames together to beat the noise down by roughly
  √K, then scan the cleaner composite (F10 — the programme's first use of
  stacking, an idea that returns in a much bigger form in §3.9). This
  brought its own hazards. A static club has exactly one angle, so
  measured outliers within a still window can be demoted (F11) — but that
  same premise later turned out to be false in a subtle way we return to.
  A *scene-permanence veto* rejects any candidate that already existed in
  a pre-swing snapshot of the scene, on the logic that permanent structure
  (like neon strips) is not the club (F12). The first version of that veto
  used a snapshot of frame 0 — which *contained the club at address* — and
  therefore vetoed the club every time it returned to its address angle
  *at impact*, silently killing the entire downswing until a human noticed
  by eye. (The fix was to build the snapshot from a club-free median across
  the whole clip.) A clubhead-blob AND-test requires the far end to be
  *both* bright *and* changed-from-the-scene (F13). And a *quasi-static
  gate* confines all of these hardened checks to near-still moments,
  because when they were applied everywhere they strangled the tracker's
  ability to recover mid-downswing (F14).

- **F19–F21 (killing distractors, driven by the c1 corpus).** A code
  audit turned up something embarrassing: both anti-distractor vetoes were
  wired to fire only during quasi-static moments, which meant a
  fast-motion re-init had *no protection at all* — and a `swing_seen`
  guard had been keeping the permanence veto switched off through the
  entire backswing. The fixes made the strict permanence veto apply at any
  speed (its first adjudicated kill was a golf-bag shaft rack, F19), built
  the permanence reference from the highest-motion frames so it is
  guaranteed free of parked clubs at address *and* finish (F20), and
  resolved a genuinely four-way confusion at the finish (F21): a club held
  over the shoulder is legitimately close to the body *and* has no bright
  blob, whereas junk running along the body line often *does* have a bright
  blob (from the golfer's shoes), so the gate has to be conditioned on
  which situation it is in.

- **Things we tried and rejected** (each ruled out by an A/B test on the
  corpus, and kept as documented negative results — knowing what does *not*
  work is part of the record). Shaping confidence by kinematics for
  fast-born segments failed because good segments and junk segments overlap
  on *every* kinematic statistic we could measure; they are only
  distinguishable by content, not by motion. Applying the forearm sector
  at fast flips broke the takeaway, where the real club genuinely does lie
  near the forearm. A shoulder-to-arm collinearity veto never fired,
  because the locks it was meant to catch were scenery, not arms. Blob
  rescue on the permanence veto let junk back in, because the moving
  golfer makes everything look "changed." And a hold-density gate collapsed
  real dark holds along with the junk.

Here is the observation this report is built around, visible only when you
stand back from the whole list. F1 (attachment to the hands), F3 and F21
(the forearm and the pendulum), F5 (the spin-rate bound), F12/F19/F20
(scene versus club), and the F16-class body gates are all *piecemeal,
reactive encodings of the same four physical fundamentals* that §3.8 will
promote to first-class constraints. Every one of them was installed only
*after* its counterfeit had been caught and adjudicated. Not one was
derived, in advance, from the fact that we already know how a golf swing
works.

### 3.3 Passive stage 2: finding the clubhead along the ray

Once stage 1 has told us which direction the shaft points, stage 2 answers
a simpler-sounding question: how far out along that direction does the
club end? Where stage 1 searched over *angle*, stage 2 searches over
*distance* along the ray.

The two stages are deliberately decoupled by a *frozen contract*. Stage 2
is allowed to read exactly five things from stage 1 — the frame index, the
grip position, the shaft angle θ, the tier (measured or predicted), and
the confidence — and nothing else. It is developed against frozen CSV files
of stage-1 output, never against live stage-1 code. The point is that
stage 1 will keep improving, and stage 2 must not have to care.

For each frame, stage 2 measures the head by looking for where the thin
shaft line ends: a gap-tolerant search for the terminus on the axis,
edge-pairs at several candidate widths, the same permanence veto stage 1
uses, and a scoring step that prefers heads at a plausible projected
length. That plausible length comes from a *length model* (developed
through forms M0–M4); in production it is a per-swing **censored
self-fit** — "censored" because foreshortening hides part of the true
length whenever the club tilts toward the camera, so the fit has to be
built to respect the fact that it is only ever seeing a lower bound. An
*arm-length plausibility floor* throws out any "head" that falls inside
the golfer's own reach (it cannot be a clubhead if it is closer than the
hands can hold it). A segmented 1-D Kalman filter with per-segment RTS
smoothing and its own measured/predicted/off tiers, plus a flip check,
finishes the stage. The value of that frozen contract showed up concretely
when v7's stage-1 improvements flowed through to changed — and gracefully
improved — stage-2 output with *zero* stage-2 code changes.

### 3.4 The corpora, the labels, and the layers of validation

Two swings were hand-labelled in detail: swing 0008 (51 shaft labels) and
swing 0009 (18 labels, chosen deliberately because it is pathological — an
overhead wrap and a club left hanging). Later, 100 clubhead labels were
placed across the c1 corpus: 10 uncropped swings shot in a new studio with
8 different club types — our first data with proper off-frame and
crop-tier examples and our first spread across many clubs.

Corpus discipline is strict for a reason we keep having to relearn: the
residuals from a *single* swing must never be allowed to choose a model
form, because that one swing might itself be atypical (off-plane, oddly
lit, whatever). Real decisions wait for a corpus gate. Even the guidance on
how to *capture* the video became a finding in its own right — how to frame
the shot, and how to manage the exposure strata.

And hand labels have a structural limitation that §4.3 will quantify and
that matters enormously: a human can only label a frame in which they can
actually see the club. That means our hand labels are systematically
biased toward exactly the easy frames — the ones detectors already handle —
and away from the blurred fast frames that are the whole problem. Grading a
detector only on frames a human could label flatters it.

### 3.5 The instrumented club and its two geometric invariants

The instrumented 7-iron carries six 25 mm retroreflective bands at
308, 362, 560, 758, 808, and 854 mm from the butt of the club — a spacing
that groups them 2-1-3. The hosel sits at 882 mm and the club is 940 mm
long. Two geometric facts about this pattern do the heavy lifting.

The first is *ratio preservation*. When a straight object is photographed,
the projection is (locally) affine, and one thing affine projections
preserve is the *ratio* of distances measured along a line. So no matter
how the club is oriented in space, the bands' positions along the shaft
map to image positions by a single linear rule, `t = s·(r − r₀)` —
which is exactly what lets us match the pattern at any pose by solving for
the scale `s` and offset `r₀`.

The second is *asymmetry*. The 2-1-3 grouping is not symmetric, so it
breaks the 180° ambiguity that would otherwise leave us unable to tell the
butt end from the head end. That said, the asymmetry is not free: the
three closely-spaced tip bands (gaps of 46 and 50 mm) are nearly
flip-ambiguous at realistic image scales, needing about one pixel of
positional discrimination to resolve — and intensity alone (how bright each
band is) cannot tell the flip at all.

We ran a pilot on 2026-07-04 to check what the tape does to the *passive*
detector — because in principle taping the club could help or hurt it. The
answer: it helps address and backswing (the grazing light and the extra
contrast give the tracker more to grab), it leaves the downswing unchanged
(blur destroys the bands there just as it destroys everything else), and
it actually *hurts* the finish. That last effect is instructive: F11's
"a still club has one angle" logic sees the two distinct θ clusters that a
banded club can produce inside a single grip-still run and demotes both,
because it assumes a still grip means a static club. The taped pilot
falsified that assumption ("a still grip is not the same as a static
club") and exposed an F11 design flaw that awaits a corpus-gated fix.

### 3.6 Raw capture and decode

The camera writes raw Bayer sidecar files (the sensor's unprocessed
colour-filter data — BayerRG8, 746 frames per swing). We decode these with
exactly the same edge-aware demosaic algorithm the application uses, into
FFV1-lossless clips. The point is to have *no* lossy encoder standing
between the sensor and the measurement, and to carry the per-stream
exposure value straight from the capture metadata into the analysis
automatically. Interestingly, a controlled A/B comparison showed the
compressed path actually *helps* the slow phases (the encoder's denoising
cleans them up) and ties in the fast phases — which told us that the
binding constraint is the detector's robustness, not the fidelity of the
decode. Chasing decode quality would have been effort spent on the wrong
thing.

### 3.7 The instrumented-truth generator, generations one and two

**Version 1 — matching the saturated blobs.** The first truth generator
thresholds the image at 235 to find saturated blobs, groups them into
connected components, uses RANSAC to find the collinear set that passes
through the grip anchor, matches the 2-1-3 pattern with an
order-preserving affine ratio fit, tests the flip by position, and
verifies that the gaps within the group are genuinely dark. It achieves a
1.1° median error with zero flips — but *only* where the bands bloom into
discrete dots, which is to say the club-up phases. And §5.1 records how it
was quietly contaminated at the address-waggle frames.

**Version 2 — fusing several kinds of evidence** (`stripe_fusion.py`).
This is where the design gets interesting, because it is built to see the
shaft the way a human does — through whichever physics happens to be
available in a given frame. It combines three evidence terms.

- A *polarity-aware ridge*. The shaft is brighter than dark cloth but
  *darker* than the blown-out mat, so a fixed "look for bright" rule is
  blind half the time. The ridge term takes the lateral maximum over dark
  backgrounds and the lateral minimum over blown backgrounds (a simple
  mean would wash a 2–3 px line out entirely once you account for
  sub-pixel misalignment), and it insists the sign of the contrast be
  *coherent* within each background segment, only allowed to flip where
  the background itself changes. This is the term that finally sees the
  dark-shaft-on-bright-mat regime.

- *Temporal evidence*. During still runs it stacks the pixel medians over
  the run (√N noise suppression again); during motion it subtracts the
  scene median so that static clutter disappears and only the moving shaft
  and its blazing bands survive.

- *Dense profile machinery* for reading the band pattern robustly. It
  estimates the local steel brightness from a percentile rather than a
  median, because the three tip bands sit so close together that they mask
  a median. It finds band peaks by their *prominence* within a bounded
  window rather than against any absolute baseline, because a sloped
  specular highlight on the steel will hallucinate peaks against any fixed
  threshold. And it scores band-assignment hypotheses on an (s, r₀) grid
  in which a hypothesis is penalised for every prominent peak it fails to
  explain.

Layered on top are a set of *honesty mechanisms*, each one earned by a
specific adjudicated failure: a forearm veto; interpolation guards across
the impact gap (θ genuinely sweeps more than 180° through impact in about
37 frames, so naive interpolation across that gap is nonsense); and the
load-bearing one, *motion-verified corroboration* — a matched blob is only
believed if it actually travels with the hands (moving at least
max(1.5 px, 0.25× the hand displacement)). No lone locks are allowed, and
— importantly — *no locks at all are allowed during static periods*. The
reasoning there is deep: in a hold, there is no motion to separate the
real club from a counterfeit, and the counterfeits pass every
appearance-based test, so a static lock is fundamentally *unverifiable* and
is therefore never emitted. The whole machine is gated on a randomised
synthetic generator (a blown region with inverted contrast, bloom-
saturating steel, speckle, anchor noise, in both a harsh and an easy
regime — where the easy regime *requires* full locks and the harsh regime
*requires* honest abstention from reporting scale).

### 3.8 The physics-first constraint system (the v3 design)

Standing back from both detectors, one pattern is impossible to miss:
every surviving counterfeit, and every historical one, violates some
physical fact about a golf swing that the methods never encoded up front.
Version 3 promotes four such facts to *constraints* that are checked
*before* the evidence engines ever run, so that a candidate which is
physically impossible is rejected on principle rather than out-scored on
evidence.

- **C1 — butt termination.** The club is held in the hands, so the club's
  evidence must *stop* within about 260 mm behind the grip. A line whose
  support continues on *behind* the butt (a trouser crease, a screen edge,
  a shaft shadow) is scene structure, and is vetoed no matter how well it
  scores. This is the full-strength version of F1. (The old v2.0 only had
  the weak form — "the ray passes within 80 px of the anchor" — and every
  counterfeit it ever caught had passed that weak gate.)

- **C2 — phase-scheduled body-overlap veto.** From takeaway through
  follow-through, the club is out in free space, *not* overlapping the
  golfer's body. Using the eight body joints the pose estimator already
  produces each frame, we build a body polygon (with a margin, smoothed in
  time to survive blur-degraded pose frames), and during those phases any
  candidate that is mostly *inside* that polygon is vetoed. At address,
  impact, and finish — the phases where the club genuinely does overlap the
  body — body evidence is admitted, but it is never sufficient on its own.
  This systematises the F16 and F21 body gates.

- **C3 — the swing reverses direction exactly once.** We segment the swing
  into its phases (still → takeaway → backswing → top → downswing → impact →
  through → finish) from the hand trajectory *alone*, with no club
  detection required, and we detect the swing's chirality once per swing
  from that trajectory (no handedness is hard-coded). Because we then know
  the sign of θ's rotation within each phase, a 180° flip becomes
  *structurally impossible* rather than something we filter out
  statistically, and bridging across an outage becomes a monotone,
  bounded-rate sweep instead of a naive interpolation. This subsumes F4,
  F6, and the v2 interpolation guards.

- **C4 — the club and the lead arm form a double pendulum.** With the lead
  arm's direction known from the pose and the shaft direction θ being what
  we are solving for, the wrist angle between them is anatomically bounded
  and evolves smoothly. This gives us a per-frame *reachable cone* — the
  search for θ collapses from a full 360° to a few tens of degrees — and a
  smoothness prior on the wrist angle. This is the full form of the
  forearm sector from F3 and F21.

Underneath, a single global estimator — dynamic programming (a Viterbi
search) over a grid of θ values across the *whole* clip, with transition
costs coming from C3/C4 and emission costs from the (unchanged, already
validated) evidence engines — replaces the old local, frame-by-frame
corroboration. The evidence engines survive intact; they simply become
emission terms inside a globally-consistent, physically-constrained
solution.

### 3.9 Rotation-compensated shift-and-stack (v3.1)

This is a trick borrowed from astronomy, where faint moving objects are
recovered by shifting a stack of exposures to follow the object's known
motion so that its light adds up coherently while everything else blurs
away. Here the club rotates about the (moving) grip pivot, so for a window
of frames and a set of rotation hypotheses ω(t) drawn from the
C3/C4 prior, we: register every frame on the grip anchor, rotate each
frame backward by the integral of ω about that pivot, and stack. Under the
*correct* hypothesis the club's pixels line up and integrate coherently —
gaining roughly √N in signal-to-noise — while the body and background,
which do not share that motion, smear into arcs. The hypothesis that
maximises coherence hands us three things at once: the club's region of
interest, a refined estimate of ω, and a partially deblurred composite in
which the band pattern can actually re-emerge at speeds where any single
6.6 ms frame is hopeless. The body polygon is masked out of the coherence
measure (that is C2 again), C1 is tested on the composite, and a
coarse-to-fine ordering keeps the compute bounded. F10's still-stack was
simply the zero-rotation special case of this; this generalises stacking to
the fast phases that actually matter.

### 3.10 Exposure-arc tomography (novel)

This is a new idea and worth dwelling on. The measured *duty cycle* — the
fraction of each frame during which the shutter is actually open — is 98.1%
(6.574 ms open out of a 6.699 ms frame period). That number has a lovely
consequence: the arc that the club sweeps out while frame *t*'s shutter is
open ends, to within 1.9% of a frame, exactly where frame *t+1*'s begins.
Consecutive frames' motion streaks therefore *tile the swing arc
contiguously*. Motion blur, in other words, is not noise to be fought — it
is a nearly gap-free, continuous recording of θ(t).

Three concrete measurements fall out of reading the blur this way:

1. The leading and trailing *edges* of a band's streak are θ samples at
   known sub-frame moments — the instant the shutter opened and the instant
   it closed — which effectively *doubles* the temporal resolution exactly
   where θ is changing fastest.
2. A streak's arc-length divided by the band's width gives the club's speed
   |ω| for that frame, from that *single* frame, with no differencing
   between frames at all.
3. Checking that each frame's streak sector continues smoothly into the
   next validates or rejects a whole window of frames at once.

The striking implication is that the impact zone — today the one remaining
hole in our coverage — could turn out to be the *densest* measurement
region of all. And it composes neatly with §3.9: the shift-and-stack finds
the corridor the club is in, and sector-edge extraction reads θ(t) inside
it.

### 3.11 Cross-modal conditioning from the wrist IMU (novel in this pipeline)

Every swing we capture already records a wrist-worn IMU (inertial
measurement unit), time-aligned to the video with a known latency. We have
not been using it, and it offers three things the vision cannot get on its
own. It gives a club-independent instant for the *top* of the swing, which
independently corroborates C3. It gives an ω(t) prior that collapses both
the DP transition model and the shift-and-stack hypothesis set from tens of
candidates down to a handful. And it is the only witness we have that is
*completely* independent of the vision — so a vision lock whose implied
spin rate contradicts the IMU can be quarantined for a human to look at.
Crucially the fusion is *one-directional*: the IMU conditions the vision
*search*, but the vision truth is never fitted to the IMU. That asymmetry
is deliberate — it preserves the truth generator's independence, which is
the whole reason it is trustworthy.

### 3.12 Learned components (v3.3), kept strictly subordinate to physics

Finally, a place for machine learning — but a narrow one. We propose a small
heatmap keypoint network that localises the clubhead's heel, toe, and hosel,
evaluated *only* inside the physics-defined region of interest. It is
trained on a *data flywheel*: the stripe truth supplies weak labels (the
shaft line, the scale, the extrapolated hosel), a stratified human
adjudication promotes a subset of those to gold-standard labels, and blur
augmentation is synthesised from the *measured* ω(t) so that the hard
training examples match this specific camera's physics rather than some
generic blur.

Two additions are our own. The first is a *physics-consistency training
loss*: the heel must lie on the truth ray at the hosel radius, and the
heel-to-toe axis must fall within the club's loft and lie bounds of being
perpendicular to the shaft. The second is *conformal calibration* —
split-conformal prediction gives finite-sample coverage guarantees that map
directly onto our honesty clauses, so that "no more than 5% of
high-confidence frames may be wrong" becomes a property *enforced by
construction* on exchangeable data, rather than something we audit after
the fact. The weights are frozen and versioned, inference is deterministic,
and the network only ships if it beats the classical stage-2 head on
held-out instrumented swings. Notably, we deliberately keep shaft θ and
phase segmentation *unlearned* — the classical methods plus physics already
deliver about 1° with zero flips, and a network there would add risk
without adding accuracy.

## 4. Results

### 4.0 Summary — accuracy and coverage at a glance

Before the detail, it helps to see the whole landscape at once, because the
single most important result is not any one number but the *shape* of the
numbers: no single detector covers the whole swing, and the two detectors
are blind and sighted in almost exactly opposite places. Read the two
tables below together and that complementarity — and the two holes that
remain — jumps out.

**What counts as good.** Three things, in order of importance. First, and
non-negotiable: **no confidently-wrong frames** — a high-confidence value
that is actually wrong becomes a false diagnosis the golfer acts on (§1),
which is strictly worse than no measurement at all. Second: a **median
error at or below ~3°** — the reference bands the coaching metrics are
scored against are only a few degrees wide, so anything much coarser blurs
one diagnosis into the next. Third, and genuinely last: **coverage** — the
fraction of a phase that yields a *measured* value rather than an honest
prediction. Coverage is what we most want to grow, but it is the one of the
three we will trade: an admitted gap costs a single frame's data, whereas a
confident error costs the reader's trust in *every* frame. Read both tables
with that priority in mind — a phase graded "Blind" is doing the *right*
thing whenever the alternative would have been a guess.

The first table tracks how the shaft-angle accuracy improved across the
generations of each detector. "Confidently wrong" is the metric that
matters most for a coaching tool, because a confident error is the one that
gets acted on; a coverage gap merely leaves a frame unlabelled.

| Detector (shaft angle θ) | Best measured accuracy | Confidently wrong? | Strong phases | Blind / weak phases |
|---|---|---|---|---|
| Passive **v1** | mean 18.8°, no honesty signal | pervasive — one bad init poisons the whole swing | none reliably | wherever it mis-initialises |
| Passive **v6** (F1–F9) | median 2.9°, p90 7.1°, 5% >30° (0008) | none | address → backswing | downswing / impact → predicted |
| Passive **v7** (F1–F21) | median 2.5°, **0% >30° in both tiers** (0008) | none | address → backswing, partial finish | downswing / impact → predicted |
| Instrumented **v1** (blob-ratio) | median 1.1°, p90 3.4° | zero flips | club-up phases only | address (bands invisible), blur |
| Instrumented **v2** (fusion) | band-tier 1.1–3.3°, ray-tier 0.4–0.8° | **zero adjudicated errors** | fast phases (down / thru) | address, finish, impact ±10 fr |

The second table is the one that shows the complementarity directly. It
reads the swing phase by phase and asks, for each, which detector can
actually *measure* it and how well — with an honest note on what the net
result is. The pattern is the thesis of the whole report in one grid: the
passive detector owns the slow phases, the instrumented truth owns the fast
phases, and between them they tile the entire swing except for two genuine
holes — the scale at address (which the optics simply do not contain) and
θ right at impact (the target of the v3.1 work in §3.9–3.10).

| Swing phase | Passive shaft θ (v7) | Instrumented θ truth (v2 fusion) | Best available truth | Grade (θ) |
|---|---|---|---|---|
| **Address** | measured, reliable (stage-1's strongest phase) | absent — bands bloom/vanish on the blown mat | passive; **scale optically absent** | **Trustworthy** (θ) · scale **Blind** |
| **Takeaway / backswing** | measured, ~2–3° median | measured — band 1.1–3.3°, ray 0.4–0.8° | both, in agreement | **Trustworthy** |
| **Downswing** | predicted (blur-blind); pred tier 13–22% >30° | **measured, 43–75% coverage** | instrumented fills the blind spot | **Usable (with review)** |
| **Impact (±10 fr)** | predicted only | ≈ none — v2 emits nothing here | neither yet | **Blind (honest gap)** |
| **Through** | predicted mostly | measured, 46–59% coverage | instrumented | **Usable (with review)** |
| **Finish** | measured ~52% (up from 32%); one surviving junk class | absent — abstained to zero | passive only | **Usable (with review)** |

**How to read the grades.** They rank, in our own terms, what a *reader*
should conclude a phase's angle measurement is fit for — and they turn on
coverage and confident-error rate, not on median alone:

- **Trustworthy** — dense measured coverage, median ≤3°, zero
  confidently-wrong frames. Safe to feed straight into the coaching metrics
  of §1.
- **Usable (with review)** — genuinely measured, but with a caveat: partial
  coverage, or a known surviving junk class a human reviewer must catch.
  Good to inform, not to auto-score unwatched.
- **Directional** — honest but thin: mostly the predicted tier. The angle's
  broad trajectory is right (fine for the on-screen replay and for context)
  but too sparse or soft to derive a fine metric from.
- **Blind (honest gap)** — no reliable measurement exists; the system
  abstains and says so. A *hole, not an error* — expect no number, and none
  is fabricated.
- **Confidently wrong** — a high-confidence number that is actually wrong.
  This is the single unacceptable state, the v1 disease the whole programme
  was built to eliminate; no current phase earns it.

The table grades the *best available* truth, combining both detectors. The
**passive product path on its own is one tier lower through the fast
phases** — **Directional** in the downswing and **Blind** at impact — which
is precisely the gap the instrumented truth fills today, and the gap the v3
design (§5.3) aims to close in the product itself.

Two footnotes complete the picture:

- **Cross-scoring the two against each other (§4.3) corrected a number we
  had wrong.** Stage-1's *measured* tier is only **0.6% bad** (4 frames of
  643) across the fast phases — not the 7% we had believed, an artefact of
  contaminated v1 truth — while the 13–22% error lives entirely in its
  *predicted* tier, frame-exactly in the blur coast. In other words the
  passive detector's honesty tiering works: where it says "measured," it is
  almost always right; where it is guessing, it says so.
- **Stage 2 (clubhead position)** measures a median of 18.8 px (0008) /
  19.5 px (0009) and passes its honesty clause on the hand labels — but
  *fails* that same clause on 7 of 10 swings once graded against dense
  fast-phase truth (high-confidence-bad of 9–34% against the ≤5% clause).
  That gap is not a regression; it is the label-selection bias of §4.3
  becoming visible for the first time.

### 4.1 Passive stage 1, graded against hand labels

- **v1** was confidently wrong. On swing 0009 it locked onto a
  shadow/mat-edge at address (reading 43° against a true ~98°) and, with
  no escape mechanism, stayed wrong for the entire swing while reporting
  LINE_OK the whole way. Its all-frames mean error was 18.8°, with no
  honesty signal to warn anyone.
- **The v6 prototype (F1–F9)** on swing 0008 produced a measured tier of
  39 frames out of 51, with a 2.9° median, a 7.1° 90th percentile, and 5%
  of frames beyond 30°; on the pathological swing 0009 it had zero
  confidently-wrong frames, correctly declaring the broken follow-through
  as prediction rather than measurement.
- **The v5n still-hold programme** lifted finish-region measured coverage
  from 4% to 49% on swing 0009, and from 19% to 37% on 0002, with the
  landmarks verified by eye at full resolution.
- **v7** (compared against a fresh baseline) reached a measured median of
  2.5° on 0008 with **0% of frames beyond 30° in *both* tiers**; measured
  coverage rose from 57% to 63% and the finish region from 32% to 52%; on
  the c1 corpus the count of frames worse than 30° dropped from 16 to 14;
  and the s9v2 "hang region" was corrected from a confident 92.5° junk
  lock to a measured 65–69° on the genuinely-visible shaft. One counterfeit
  class survives (quasi-static finish body-lines that get blob credit from
  bright shoes).
- **The stage-2 decoupling test** confirmed the contract holds: with v7's
  stage-1 fixtures changed but stage-2 code untouched, stage-2's head
  measured medians were 18.8 and 19.5 px on 0008 and 0009 with the honesty
  clauses still passing.

### 4.2 Instrumented truth (the tape_20260705 corpus)

- **The synthetic gate on the fusion core** passed cleanly: 26 of 26 band
  locks correct, a maximum θ error of 1.17°, scale error within 1.6%, zero
  flips, and honest abstention wherever the bands were not discrete.
- **v1 blob-ratio** achieved a 1.1° median and 3.4° 90th-percentile θ with
  zero flips in its anchor tier, 46–85 anchors per swing, but only in the
  club-up phases — 713 truth entries in all, later found to be contaminated
  at the address-waggle frames (§5.1).
- **v2 fusion** achieved per-swing band-tier medians of 1.1–3.3° and
  ray-tier medians of 0.4–0.8°, with **zero adjudicated errors**,
  byte-identical determinism, coverage of 43–75% of downswing frames and
  46–59% of through-swing frames, and 1,033 truth entries. It measures
  correctly through frames where stage-1's own track is coasting about 160°
  wrong (adjudicated by flip-book review).
- **The cost of buying honesty through abstention** shows up plainly here:
  each v2 guard removed a class of junk *and* removed real coverage — the
  finish and address emissions fell all the way to zero, and the stacked
  still tier had to be cut entirely after whole address runs
  self-corroborated a shirt-texture counterfeit at a self-consistent but
  wrong scale.

### 4.3 Grading the passive pipeline against the instrumented truth

This is where the dense truth earned its keep, in four distinct ways.

- **A correction.** The v1-truth contamination had inflated a figure we
  believed — "stage-1's measured tier is 7% bad in the fast phases."
  Against the clean v2 truth, the real number is **4 bad frames out of
  643 — 0.6%** — and 9 of the 10 swings have zero.
- **A discovery.** Those 4 bad frames are a *genuine* failure of stage-1's
  measured tier: during one takeaway it confidently tracks a leg shadow
  while the banded club is plainly visible. This is the first
  machine-documented failure of that tier — its hand-label record had been
  clean, precisely because a human labelling those frames could see the
  real club and never labelled the shadow.
- **A localisation.** Stage-1's *predicted* tier is 13–22% bad (worse than
  30°) on 6 of the 10 swings, and the errors land frame-exactly in the
  blur-zone coast — telling us precisely where prediction is being asked to
  do too much.
- **A label-distribution finding, and it is the important one.** The
  clubhead stage *passes* its honesty clauses on the 51 + 18 hand labels,
  yet *fails* them on 7 of 10 swings against the dense fast-phase truth
  (high-confidence-bad running 9–34% against the ≤5% clause), with per-swing
  length-error means as large as −215 px implicating the censored self-fit.
  The reason is the structural bias flagged back in §3.4: hand labels can
  only be placed where a human can see the club, which is exactly where the
  detector already succeeds, so the prior validation was systematically
  easy. The dense instrumented truth removes that bias — and the moment it
  does, a stage that looked calibrated turns out not to be.

## 5. Discussion

### 5.1 A catalogue of errors, across the whole programme

It is worth cataloguing the failures deliberately, because the *pattern* of
them is one of the report's main results. They fall into four groups.

**The passive-detector era.** The v1 confidently-wrong lock class, where
one bad initialisation with no escape poisons an entire swing. The
premature C++ port, reverted, which is the origin of the exemplar-first
rule. "Median error lies" — 24% of frames beyond 30° hiding behind a 7.4°
median at confidence 0.93+. The F12 frame-0 snapshot that contained the
address club and therefore silently vetoed the whole downswing (because the
club returns to its address angle at impact). Gates wired behind the
quasi-static condition, leaving fast-motion re-inits entirely unguarded,
and a `swing_seen` flag that disabled the permanence veto for whole
backswings — both found by code audit, not by any metric. Non-reproducible
frozen fixtures. F11's "a still grip means a static club" premise,
falsified by the taped pilot (the finish clusters mutually demote each
other). And the five rejected fixes of §3.2, kept as documented negative
results.

**The instrumented era — method errors** (each now a named regression
case). The tip-trio flip ambiguity, which positional RMS can discriminate
but intensity cannot. A top-N-by-area blob cap that discarded real 1–25 px²
band specks because they ranked 58th–132nd by size. Admitting 60
proximity-sorted blobs, which made junk affine fits combinatorially
dominant (with only four bands, the median error blew out to 112°). A
median local-steel estimate that the tip group masks. A scalar-baseline
peak extractor that hallucinated about 26 peaks per ray on sloped specular
steel. A swing-median scale gate that was *physically* wrong — the scale
genuinely halves under foreshortening (that *is* ρ), so a gate assuming a
constant scale destroyed real finish locks while nearby junk survived.
Streaked-band centroids drifting off the shaft line for about 2 frames
around impact, where mutually-consistent wrong locks passed corroboration
and chained flipped rays together. Motion thresholds — first rate-scaled,
then absolute — each of which leaked on slow waggle until the
noise-floored proportional form fixed it. And the formal conclusion that
static-period locks are simply unverifiable.

**Tooling errors.** The synthetic generator read background values *from
the image it was drawing into* — a feedback loop that manufactured phantom
echo peaks after every band, and two whole tuning cycles were spent fitting
detectors to that artifact. The lesson: synthetic gates need their own
adjudication too. Render the synthetic data and look at it.

**Epistemic errors** — the mistakes of belief. "There is no ring light
present" was confidently inferred from the absence of saturated blobs at
address, and it was simply wrong, corrected only by full-resolution pixel
inspection after someone challenged it. v1 truth was trusted for scoring
before cross-detector disagreement exposed its contamination — which taught
us that truth generators must be validated *adversarially against each
other*, not merely against the consumer that eats their output. All the
corpus accuracy figures use stage-1's measured tier as a cross-check
referee, and the leg-shadow case proves that referee is itself imperfect,
so "zero errors" formally means zero *adjudicated* errors under a
visually-verified but fallible referee. And the label-selection bias of
§4.3 stands as a standing warning: a validation regime can pass its own
clauses simply because its labels avoid the hard frames.

### 5.2 Honesty by abstention versus honesty by discrimination

This is the central methodological finding, and it only becomes visible
when you look at the entire history at once.

Both detector families walked the same road. Start with a generic evidence
engine. It picks up a counterfeit. You adjudicate the counterfeit and add a
guard against it. The guard costs you some real coverage. Then another
counterfeit appears, and you repeat. The guards — permanence vetoes,
quorums, motion corroboration, stillness gating — all work the same way:
they refuse to measure under the conditions the junk exploits. But *real
signal shares those same conditions*, so coverage decays monotonically as
robustness rises. We watched this happen directly: v2's finish and address
emissions went to zero, and earlier v5's hardened gates strangled the
downswing recovery until F14 confined them to the regime where they were
needed. Buying honesty this way — by *abstaining* — inevitably abstains
yourself into sparsity.

Physical constraints do something categorically different: they
discriminate *within* those conditions. Every counterfeit we catalogued —
shadow, mat edge, neon, bag rack, trouser crease, leg line, arm, shirt
texture, speckle constellation — fails at least one of C1–C4, while no true
club configuration fails any of them. The passive fix history is itself the
strongest evidence for this claim: its most durable fixes (F1, F3, F5,
F16/F21, F12/F20) are *exactly* the four fundamentals, discovered one
adjudication at a time. Version 3.0's corpus gate tests the resulting
falsifiable prediction directly: constraints-first should recover the
abstained coverage while readmitting zero junk.

### 5.3 From diagnosis to treatment: what the v3 design actually changes

Section 5.2 is a diagnosis. This section is the proposed treatment, and the
treatment follows so directly from the diagnosis that it is worth spelling
out the logic. If the disease is that generic guards buy honesty by
*refusing to measure* under the conditions that junk exploits — and thereby
bleed away the real signal that shares those conditions — then the cure is
not a better guard. It is a *discriminator* that separates the real club
from the counterfeit *within* those conditions, so that no refusal is
needed. The four fundamentals (§3.8) are that discriminator. The whole v3
design is a bet on two claims about them: that they are **necessary** (every
counterfeit violates at least one) and **near-sufficient** (no true club
configuration violates any). Everything below is the argument for why that
bet is a reasonable one — and, equally important, the specific ways it could
lose.

**Why the constraints work where guards did not — counterfeit by
counterfeit.** The power of a physical constraint, as opposed to a guard,
is that a single fact excludes an entire *family* of counterfeits at once,
and does so without touching the real signal. Walk the catalogue of §5.1
through the four constraints and this becomes concrete. The trouser crease,
the screen edge, the shaft shadow — each is a line whose support continues
*behind* the butt of the club, so C1 (butt termination) vetoes all of them
on the single ground that a club cannot have evidence extending past the
hands that hold it. The leg shadow that stage-1's measured tier actually
tracked — the one genuine measured-tier failure that dense truth exposed in
§4.3 — sits *inside the golfer's body* during the takeaway, a phase when the
real club is out in free space, so C2 (the phase-scheduled body-overlap
veto) rejects it. The impact-zone flips, where mutually-consistent wrong
locks chained flipped rays together, become *structurally impossible* under
C3, because the sign of the club's rotation in each phase is known in
advance from the hand trajectory — you cannot chain a 180° reversal that the
phase model forbids. And the arm locks, together with the whole four-way
forearm confusion that F3 and F21 fought by hand, are subsumed by C4's
reachable cone, which confines θ to the anatomically feasible wrist angles
about the measured lead-arm direction. The static scenery that is *not* on
the body — the neon strips, the golf-bag shaft rack — is the one family the
four constraints do not each catch directly; it is handled instead by the
permanence machinery that v3 retains from v2 and that C2's free-space
schedule reinforces (during the swing the club is out where that scenery is
not). The point is not that C1–C4 are magic; it is that they are a *small*
set of facts, each excluding a large family, and — unlike a guard — excluding
it while leaving the real measurement in that same phase perfectly
measurable.

**Why the reasoning has to become global.** There is a second, quieter
shift in v3 that matters as much as the constraints themselves: it stops
reasoning frame by frame. Version 2 made every decision locally — this
frame's evidence, this frame's corroboration, this frame's gate. Version 3
solves the whole clip at once, with a dynamic-programming (Viterbi) search
over θ across every frame, in which the physics enters as *transition
feasibility* (the phase-signed rotation of C3, a bounded angular speed, a
wrist angle kept inside anatomy by C4) while the validated evidence engines
survive untouched as *emission costs*. The reason this is not a mere
implementation detail is epistemic. A single heavily-blurred frame is
genuinely ambiguous in isolation; no local rule can resolve it *honestly*,
which is exactly why the guards abstained. But that same frame, embedded in
a trajectory that is required to be physically continuous, frequently has
only one reading consistent with the frames on either side of it. Global
estimation is what converts "I cannot tell in this frame" into "only this
value is consistent with the swing" — and that converted certainty is
precisely the coverage the local guards had been forced to throw away.

**The falsifiable payoff.** These two moves make a concrete, testable
prediction, and it is the one the v3.0 corpus gate is built to check. The
finish and address emissions that v2 abstained all the way to zero (§4.2)
were abstained because their counterfeits — finish body-lines wearing a
bright-shoe blob, self-corroborating address shirt texture at a
self-consistent wrong scale — simply could not be told from the real club by
appearance or motion. Under v3 those same counterfeits are excluded
*structurally* (the finish body-lines by C2 and permanence; the behind-butt
address lines by C1), which removes the reason to abstain, which should let
the real measurements in those phases come back. The design states this as a
prediction rather than an assumption: v3.0 must **recover the abstained
finish and address coverage at zero readmitted junk**, with the
counterfeit-regression suite — every catalogued failure in §5.1 turned into a
named test case — passing green. If constraints-first fails to recover that
coverage, or recovers it only by letting junk back in, the central
hypothesis is simply wrong, and the gate is designed so that we would see it
rather than talk ourselves past it.

**Blur as signal, not just signal not thrown away.** Section 5.2 is about
not *discarding* real signal; §3.9–3.10 go a step further and mine a signal
we had been treating purely as damage. Shift-and-stack recovers the club
from the very motion blur that defeats any single frame, and the
exposure-arc reading turns the near-total (98%) shutter duty cycle into a
continuous angular record of θ(t). The consequence is worth restating in a
discussion because it inverts the problem the whole report has been
circling: the impact zone — today the single hole in *both* detectors'
coverage (Table B, §4.0) — could become the *densest* measurement region we
have, precisely because it is the most blurred. If that holds up on the
corpus, the phase the system currently fears most turns into its richest
source of truth, and the last hole closes from an unexpected direction.

**The IMU as the one witness that owes nothing to the pixels.** Everything
above is still vision reasoning about vision, and §5.1's catalogue of
*epistemic* errors is a standing warning about how far self-consistent
vision can fool itself: v1 truth was self-consistently *wrong*, and the leg
shadow passed every visual test we had. The wrist IMU (§3.11) is the only
witness in the entire system that is independent of the pixels. Its value is
not mainly accuracy — it is *independence*: a vision lock whose implied spin
rate contradicts the IMU can be quarantined, which is the only mechanism in
the design that breaks the closed loop in which vision validates vision. The
strictly one-directional coupling — the IMU conditions the *search*, but the
truth is never fitted to the IMU — is a deliberate epistemic firewall. It
buys the search a strong prior without ever letting the instrument's truth
become circular, which is the exact failure mode §5.1 warns about.

**Learning, kept on a short leash, and honesty made structural.** The
learned heel/toe head (§3.12) is the strategic payload of the whole
programme — it is the bridge from taped lab clubs to the unmarked clubs a
customer actually owns (G4) — yet it is deliberately the *smallest* learned
component that could do the job: a keypoint head, inside the physics region
of interest, aimed at the one part of the club (the head) that has no ratio
template to exploit. It is trained on truth the classical pipeline itself
generated (the data flywheel), and — most important for a coaching tool — it
is wrapped in conformal calibration, which converts the honesty clause from
something we *audit after the fact* into a finite-sample coverage guarantee
enforced *by construction*. That is the design's direct answer to the entire
epistemic-error category of §5.1: rather than hope the confidence is honest
and check later, make honesty a mathematical property of the method.

**Honest status, and how the whole thing could fail.** None of §5.3 is a
result. All of v3 is design; its expected effects are to be verified, not
assumed, and the failure modes are specific enough to name. The pose anchor
is both an input and a bias, and it degrades worst exactly where we need it
most (peak blur) — which is why the design fits lines laterally and tests C1
on the stacked composite rather than forcing every ray through the anchor.
The phase segmentation, which C3 and C4 both lean on, could mis-fire on
unusual swings (rehearsal waggles, pump-fakes), so the phase model must emit
its own confidence and fall back to conservative v2-style rules where that
confidence is low — because a mis-segmented swing would apply the wrong
rotation sign and manufacture *new*, confident errors, the worst possible
outcome. Left-handedness is handled by detecting chirality from the
trajectory rather than hard-coding it, but that must be proven on a
left-handed capture before v3.0 is frozen. The learned head invites scope
creep, held off by the rule that it stays a keypoint head inside a physics
ROI and never becomes an end-to-end tracker. And underneath all of it sits
the load-bearing assumption that the four fundamentals really are
exhaustive: if some real counterfeit fails *none* of C1–C4, or some
legitimate club pose fails *one* of them, the central premise cracks. That
is precisely why the standing counterfeit-regression suite — and the
discipline of adding to it every single time a new failure is adjudicated —
is not bookkeeping but the safeguard the entire design rests on.

### 5.4 Limitations and threats to validity

Honesty about the results demands honesty about their limits, and there are
several.

The data is narrow: one athlete, one studio geometry, right-handed only.
The instrumented corpus is a single club on a single day (the c1 corpus has
breadth across clubs, but only clubhead labels). The pose anchor is both an
input *and* a bias — forcing the ray to pass through the anchor
demonstrably disadvantaged the true ray at address, and the lateral fitting
meant to fix that is designed but not yet validated. The truth heads are
on-axis extrapolations at 940 mm, which differ *by definition* from the
visual centroid that stage 2 targets, and that gap bounds how hard stage 2
can honestly be pushed against them. Address-phase scale truth is
*optically absent* at this exposure — the bands either bloom or vanish — and
no algorithm can recover information the data does not contain. The
hand-label and instrumented-truth regimes disagree about stage-2
calibration (§4.3), and until a labelled hard-frame subset exists, part of
that gap could in principle be truth-definitional rather than a real
miscalibration. And determinism is verified per-machine; cross-platform
bit-equality — the oracle we intend to use for the C++ port — is still
untested.

### 5.5 Future research

In gate order — because each stage has to clear its gate before the next
begins:

- **v3.0**: the constraint system plus the DP estimator. Its gate is
  coverage recovery at zero junk, with the counterfeit-regression suite —
  every catalogued failure turned into a named test — passing green.
- **v3.1**: shift-and-stack, with exposure-arc sector-edge extraction as
  its measurement layer. Its gate is a smooth ω(t) with a physically
  plausible peak through impact ±10 frames, adjudicated on the stacked
  composites.
- **v3.2**: address θ, via a mat-crossing prior and lateral fits.
- **The F11 redesign** in the passive tracker — cluster the still-run
  measurements, or split runs at confident θ jumps — corpus-gated against
  the v7h fixtures.
- **IMU conditioning** (§3.11): cheap, uses data we already capture but
  currently discard, and is the only vision-independent witness we have.
- **Stage-2 re-calibration** against the dense truth, and then **conformal
  honesty** across all stages — turning the honesty clauses from audits into
  guarantees.
- **Metric grounding**: the detected ball plus the address hosel give a
  per-session mm-per-pixel scale, which converts s into an absolute ρ and
  lets us pool measurements across sessions.
- **Broader corpora**: multi-club, left-handed, and hard-frame-labelled.
- **The heel/toe network and its data flywheel** (§3.12) — strategically
  the whole point of the instrumented programme, because it is the bridge
  to unmarked clubs.
- And the **C++ port** behind the existing detector interface, with the
  exemplar as its byte-oracle, running the marker and passive modes
  together.

## 6. Conclusion

Across two detector families, three generations of method, and roughly two
dozen adjudicated counterfeits, the evidence supports a single conclusion.
In a fixed, hostile capture environment, the discriminative power that
generic computer vision lacks is available *for free* in the physics of the
golf swing — and both of our detectors had been rediscovering that physics
the hard way, one post-mortem at a time.

Generic evidence engines are necessary, and ours are genuinely validated
(the passive measured tier is 2.5° with 0% bad on hand labels; the
instrumented band tier is about 1° with zero flips). But on their own,
without physical constraints baked in a priori, they face an unhappy
choice: leave the constraints out and they hallucinate counterfeits; guard
against the counterfeits and they abstain themselves into sparsity. The
instrumented-truth corpus we built here — 1,033 verified fast-phase samples
in the region where truth never existed before — has already repaid its
cost several times over. It corrected a wrong conclusion about the passive
detector, documented the first-ever failure of that detector's
most-trusted tier, exposed a label-selection bias that had quietly
flattered *all* of our prior validation, and converted the clubhead
stage's miscalibration from a vague impression into a precise, per-frame
measurement.

The v3 programme — constraints first, blur read as signal rather than
fought as noise, the IMU brought in as an independent witness, and learning
admitted only where perception genuinely runs out — is specified with
falsifiable gates. Its central hypothesis is risky in exactly the right
way, and for the first time the instruments needed to test it actually
exist.
