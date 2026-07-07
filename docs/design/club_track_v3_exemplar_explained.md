# `club_track_v3.py` explained — the reference bible

**What this document is.** A complete, self-contained explanation of the v3.0 club-tracking
exemplar (`tools/shaftlab/club_track_v3.py`) **and its two companions — the v3.1 impact-zone velocity
module (`tools/shaftlab/shift_stack_v3.py`, §13) and the v3.2 address / hold-θ module
(`tools/shaftlab/address_theta_v3.py`, §14)**: what they do, why they do it that way, the physics
they lean on, the image processing they perform, the algorithm at their core, and the statistical
method used to tune and trust them. It is written for a reader who is **not** assumed to know golf,
computer vision, dynamic programming, or statistics. Where a term of art appears it is defined.

**Why it exists.** The Python files are *exemplars* — research reference implementations. They will
be ported to C++ for the app. Python is easy to change while the ideas settle; C++ is fast and
ships. When the port happens, the C++ must reproduce these exemplars' output (that is the acceptance
test — see §15). This document is the durable record of *the approach* so that the reasoning
survives the port, long after anyone remembers the specific Python lines.

**Companion documents.** The design rationale is in
[`club_tracking_v3_design.md`](club_tracking_v3_design.md); the staged plan and the measured
results are in [`../implementation/shaft_detection_v3_impl.md`](../implementation/shaft_detection_v3_impl.md);
the evidence engines it reuses are described in
[`stripe_fusion_design.md`](stripe_fusion_design.md).

---

## 1. The problem, in one sentence

Given a video of a golfer swinging a club, filmed from the front (the "face-on" camera), measure
**where the shaft is pointing in every single frame** — an angle we call **θ (theta)** — as
accurately as possible, and *never* report a confident answer that is wrong.

That last clause is the hard part. Anything can measure the club when it is held still and well
lit. The engineering challenge is the fast, blurred, half-occluded frames, and the many things in
a real scene (trouser creases, the edge of a mat, a shadow on a leg, the golfer's own arm) that
*look like* a straight bright line pointing away from the hands — and are not the club.

### 1.1 A crash course in the golf swing (for non-golfers)

A golf swing, filmed from the front, runs through a fixed sequence of **phases**. You do not need
to play golf to use them; think of them purely as a shape the hands trace over about one second:

| phase | what the body/club is doing | the club angle θ |
|---|---|---|
| **address** | standing still, club resting on the ground pointing down at the ball | steady, pointing down |
| **takeaway → backswing** | hands swing up and around to one side, cocking the wrists | rotates steadily one way |
| **top** | the highest point; the swing momentarily *stops and reverses* | turning point |
| **downswing** | hands drive back down, fast; wrists "release" | rotates the *other* way, quickly |
| **impact** | the club strikes the ball; this is the fastest, most blurred instant | sweeping through fastest |
| **follow-through (thru)** | club continues up and around the far side | keeps rotating the same way |
| **finish** | the golfer holds a balanced final pose; club comes to rest | comes to a stop |

Three facts from this table do almost all the heavy lifting later:

1. **The club is held in the hands.** It cannot be anywhere that does not start at the hands.
2. **The swing changes direction exactly once, at the top.** Going back the club rotates one way;
   coming down and through, it rotates the other way, and it keeps that direction all the way to
   the finish. There is only one reversal.
3. **The club and the lead arm form a *double pendulum*.** Picture two rods hinged end to end:
   the arm swings from the shoulder, and the club swings from the wrist. The wrist "hinge" cocks
   during the backswing and unhinges (the "release") through the downswing. Because a whip made of
   two hinged segments transfers energy to its tip, the club is *fastest at the bottom* (impact) —
   which is exactly where it is hardest to see in a single video frame.

### 1.2 Instrumented vs passive

For research we use an **instrumented** club: the shaft is wrapped with strips of
retro-reflective tape at *known, measured positions*. Under the studio's ring light those strips
glow bright white (they "saturate" the sensor). Their spacing forms a fixed, asymmetric pattern
(for the 7-iron used here, band centres at 308, 362, 560, 758, 808, 854 mm from the butt — a
distinctive "2-1-3" grouping). Because the pattern is asymmetric, seeing it tells us not only
*where* the shaft is but *which way round* it is (butt vs tip). This is the strongest single piece
of evidence the system has, and much of the design is about trusting it when it is present and
degrading gracefully when it is not (fast/blurred frames, where the bands smear).

The long-term product goal is **passive** detection (ordinary, un-taped clubs). v3.0 is built and
proven on the instrumented case first; the physics constraints below are exactly what a passive
version will also need.

---

## 2. What we measure, and the coordinate system

- **θ (theta): the shaft direction.** The angle, in degrees, of the line from the **grip** (the
  hands) toward the **club head**. This is the primary output, one value per frame.
- **Image coordinates.** Like all image code, x increases to the **right** and y increases
  **downward** (the top row is y = 0). So an angle of 90° points *straight down* the screen, 0°
  points right, 180° points left. Angles wrap around at 360°. A helper `circ_wrap` maps any angle
  difference into the range −180°…+180° so that, e.g., "5°" and "365°" are recognised as the same.
- **The grip anchor.** Every frame comes with the pixel position of the hands (the "grip"),
  supplied externally (from body-pose estimation — see §4). We do **not** detect the hands; we take
  them as given and measure the club *relative to* them.
- **s (scale) and r0 (butt offset).** When the tape pattern is readable we additionally recover
  **s**, the image scale in pixels-per-millimetre (how big the club appears — it shrinks when the
  club tilts toward or away from the camera; this is *foreshortening*), and **r0**, how far behind
  the grip the butt of the club sits. From (θ, s, r0) and the known club length we can draw the
  whole club, including the head.

### 2.1 Output confidence tiers

Not all measurements are equally trustworthy, so every frame's result is labelled with a **tier**:

- **`band`** — the tape pattern was matched. Strongest: gives θ *and* s *and* the head position.
- **`ray`** — no pattern lock, but a genuine straight line of the right kind was found along θ.
  Gives θ only.
- **`pred`** — no direct image evidence; the value is *inferred* by the physics/continuity model
  (a "bridge" across a gap). We keep these for a continuous on-screen line, but we deliberately
  **never** write them into the ground-truth files. This is the core of the honesty stance: we
  distinguish "measured" from "guessed" and only publish the former.

---

## 3. The guiding philosophy

Three ideas separate v3 from its predecessor (v2, "stripe fusion") and are worth stating plainly,
because every design choice flows from them.

**(a) Physics as a first-class filter, not an afterthought.** The earlier system detected the club
frame-by-frame and then tried to *reject* the many false positives with ad-hoc guards. Each guard
that killed a counterfeit also killed some real measurements, so coverage bled away — the finish
and address ended up at 0%. v3 turns the swing's physics (the three facts in §1.1) into **hard
constraints applied before the evidence is even scored**. A trouser crease is not rejected because
it "looks slightly wrong"; it is rejected because *the club cannot physically be there in this
phase*.

**(b) Global, not per-frame, estimation.** Instead of choosing the best θ for each frame in
isolation, v3 chooses the best **whole trajectory** θ(t) for the entire clip at once, balancing
image evidence against physical smoothness. This is what lets a confident measurement on one side
of a blurred, evidence-free patch (say, a tape lock just after impact) *reach back* and correctly
fill the blurred patch. The tool for this is a **dynamic program** (§8).

**(c) Honesty by discrimination, not by abstention.** v2 bought its "zero errors" record by
staying silent whenever unsure — which is honest but low-coverage. v3 keeps the honesty (it still
abstains where it genuinely cannot verify) but *recovers* the coverage v2 threw away, by using the
constraints to tell the real club apart from the look-alikes rather than refusing to answer.

---

## 4. Inputs (the data contracts)

The exemplar consumes five things, all produced upstream. The C++ port will consume the same
information, however it is packaged.

1. **The video** (`<clip>`). Read once into memory as grayscale frames. (Colour is not needed; the
   bands and the shaft are distinguished by *brightness*, not hue.) Frames are stored as 8-bit gray
   to keep memory bounded on a 16 GB laptop, and converted to floating point only for the frame
   being processed.
2. **`anchors.csv`** — one row per frame: `frame, grip_x, grip_y, phi_deg, phi_ok`.
   - `grip_x, grip_y`: the hands' pixel position (the anchor of §2).
   - `phi_deg` ("φ", phi): the direction of the **lead forearm** (elbow → grip), in image degrees.
     This is the arm's angle — one of the two rods of the double pendulum. `phi_ok` flags whether
     pose was confident enough that frame.
   These come from a body-pose estimator run earlier; the exemplar does not compute them.
3. **`skeleton.csv`** — one row per frame: `frame` then 8 body joints as `(x, y, confidence)`:
   left/right shoulder, hip, knee, ankle. Used to build the body outline for constraint C2.
4. **`clubs.json`** — the club's measured geometry: the tape band centres (mm from the butt) and
   the total length. This is what makes the pattern match possible.
5. **`clipmeta.json`** — frame rate, image size, and a per-frame timestamp list (`t_us`,
   microseconds). Timestamps let us line the video up with other sensors and with the reference
   truth.

Optionally, an **impact frame** can be supplied (`--impact-frame` / `--impact-us`) from a separate
sensor; if absent, the tool estimates it from the hands alone (§7).

---

## 5. The evidence engines (image processing)

Two low-level detectors, imported unchanged from the validated `stripe_fusion` module, turn pixels
into "how club-like is direction θ from the grip?" They are called **E1** and **E2**. The exemplar
treats them as trusted black boxes; understanding *what* they answer matters more than their
internals.

### 5.1 E1 — the band-pattern matcher (`frame_band_match`)

This is the strong detector, used when the tape bands are visible as distinct bright spots.

- **Find bright blobs.** Threshold the frame: pixels brighter than a saturation level become
  candidate spots; connected clusters of them are "blobs". (The tape saturates the sensor, so a
  simple brightness threshold isolates it well.)
- **Find a line through the hands.** Look for a set of blobs that are (i) roughly collinear and
  (ii) pass near the grip anchor. This is a small **RANSAC**-style search: try pairs of blobs to
  define a candidate line, keep the line with the most blobs close to it.
- **Match the known pattern by *ratios*.** Here is the key trick. The band spacing in millimetres
  is fixed and known. Along a straight line, **ratios of distances are preserved by projection** —
  no matter how the club is tilted or how big it appears, the middle band still sits the same
  *fraction* of the way between its neighbours. So we fit the observed blob spacings to the known
  band spacings with a single scale `s` and offset `r0`. If ≥4 bands line up within tolerance, we
  have a **lock**. Because the 2-1-3 pattern is *asymmetric*, the fit also fixes the butt→tip
  direction (a "flip" of the club would not fit).
- **A guard against fakes.** Between two adjacent bands of a group, the bare steel shaft is *dark*.
  A run of bright specks over the blown-out mat can occasionally imitate the spacing, but it will
  not have that dark gap. The matcher requires the dark gap, which kills most such imposters.

E1 returns θ, s, r0, the number of matched bands `n`, and the mean position of the matched blobs.
It is essentially never wrong when it fires (measured band-tier error across the corpus: median
0.3°, zero frames worse than 15°). Its weakness is *coverage*: at speed the bands smear into
streaks and the pattern dissolves, so E1 goes quiet exactly where the club is hardest.

### 5.2 E2 — the polarity-aware ridge (`ridge_sweep`)

This is the weaker but broader detector: it answers "is there a straight bright/dark *line* along
direction θ, starting at the grip?" for **many candidate angles at once**.

- **Sample along rays.** For each candidate angle it walks outward from the grip in small steps,
  reading pixel values along the ray and along strips just to either side (the "background").
- **Polarity.** The steel shaft is *bright on dark cloth* but *dark on the blown-out white mat* —
  its contrast **flips sign** depending on what is behind it. A naive detector that only looks for
  bright lines is blind over the mat. E2 checks the local background level and looks for a bright
  line where the background is dark, and a dark line where the background is blown out. This is
  what "polarity-aware" means.
- **Accumulate a score.** Credit accrues along the ray where the on-line/background contrast has
  the expected sign, and the score is length-normalised so a short strong line and a long weak line
  compare fairly. The output per angle is a **score** (higher = more line-like), the length of the
  supported run, and a support fraction.

E2 is run twice per frame: once on the raw grayscale, and once on the **motion image**
`|frame − scene_median|` (the current frame minus a median "background plate" of the whole clip).
The motion image suppresses anything static — furniture, the mat, fixed shadows — leaving what
*moved*, which is a good cue for a swinging club. The two are combined by taking, per angle, the
stronger normalised response.

---

## 6. The four constraints (the physics filter)

These are the heart of v3. Each is one of the §1.1 facts turned into a rule that shapes or vetoes
candidate angles *before* the global estimator weighs the evidence. Each also names the specific
real-world counterfeit it was built to kill.

### C1 — the club is held in the hands (butt-termination)

**Physics:** the club starts at the grip; its butt end sits a short distance *behind* the hands
(never far). **Rule:** a band lock is only accepted if the recovered butt offset `r0` is within
`(0, 260 mm]` behind the grip (line 288). A candidate whose evidence *continues* well behind the
hands is a **scene line** (a trouser seam, the edge of the projector screen, a shaft shadow), not
the club — so for the ray tier we add a penalty when there is strong reverse-direction ridge
support that is **not** along the arm (lines 306–309; the arm is legitimately behind the hands, so
it is excluded from this test). Kills: screen/mat edges, trouser creases, shaft shadows that pass
*through* the hands and out the other side.

### C2 — the club overlaps the body only at address, impact, and finish (body-overlap veto)

**Physics:** from the takeaway through the follow-through the club is out in free space, clear of
the torso and legs; only at address, at the moment of impact, and in the finish does it overlap the
body. **Rule:** `body_masks` builds a filled outline of the golfer each frame from the 8 skeleton
joints (convex hull, temporally smoothed to survive pose noise, then dilated by a margin). During
the mid-swing phases, a candidate angle whose shaft ray lies **mostly inside** that outline is
penalised (lines 311–318). At address/impact/finish the veto is switched off ("admitted, but never
sufficient alone"). Kills: leg shadows, shirt-texture lines, foot-region mat speckle — all of which
live *inside* the body during phases when the club cannot.

### C3 — the swing reverses exactly once, at the top (phase-signed rotation)

This is the most powerful constraint and the backbone of the whole estimator; §7 and §8 describe
its two halves (the phase model and the direction-signed motion). In one line: because θ **rises**
through the backswing and **falls** monotonically (wrapping past 0°/360°) from the top all the way
through impact and finish, a 180° flip becomes *structurally impossible*, and the blurred impact
gap can only be bridged by a smooth continued sweep — not by a jump to a look-alike. Kills: the
impact "streak-flip", where the smeared bands' centre-of-brightness momentarily implies a club
pointing 130° wrong.

### C4 — club + lead arm are a double pendulum (reachable cone)

**Physics:** the club angle θ and the arm angle φ are linked through the wrist; the wrist angle
ψ = θ − φ is anatomically limited and changes smoothly. So θ is confined to a *cone* around the arm
direction. **Two practical caveats learned from real data:**

1. The arm direction φ from pose is **noisy** — at the top and in the blur zone it jumps by tens of
   degrees frame-to-frame. So we first robustly smooth φ (`smooth_phi`: median then Gaussian filter,
   done on the unit vector so the 360° wrap is handled), and we make the cone **wide**
   (`CONE_HALF = 150°`) rather than the tight "few degrees" the ideal physics suggests.
2. In the finish the club wraps around behind the body and ψ becomes effectively unbounded, so the
   cone is **switched off** at address/finish/top.

Even wide and part-time, C4 earns its place two ways: a **hard veto** on any angle pointing *into*
the forearm (θ near φ+180 — the shaft never points back down the arm; lines 295–298), which kills
the very tempting "lock onto the bright sleeve/arm" counterfeit; and a **soft cone** that removes
the reverse half of the circle in mid-swing (lines 300–304). The fine work is left to C3 and the
estimator's smoothness.

> **Design note that matters for the port:** the physics *wanted* C4 to be the main search-space
> reducer ("collapse 360° to tens of degrees"). Real pose noise made that impossible. The working
> system reduces the search another way — via C3's signed, bounded motion inside the dynamic
> program (§8) — and uses C4 only as a wide guardrail. Do not "tighten the cone" in C++ expecting
> the ideal; it will start clipping real swings.

> **Evolution — the ψ-monotonicity reconciliation (v3.0-r1, 2026-07-07; built).** The cone above is a
> *magnitude* bound. The double pendulum's stronger fact is that ψ = θ − φ is **monotone with one
> reversal at the top** — C3's law on the wrist. First built as a DP **transition rail** on Δψ, this was
> then **superseded** by a cleaner form on Mark's reframe: *monotone ψ is truth, so a violation measures
> error → FIT the truth (per-phase robust isotonic regression, PAVA + Huber-IRLS) after a pure-C3 DP,
> reading the error off the residual*, rather than penalising Δψ per frame (which fires on the pose-φ
> noise floor — 25–62% of backswing steps show sub-degree "reversals" that are noise). The fit gives a
> per-frame φ-error map (`psi_err`), reconstructs the impact-blur shaft from the arm (`θ = ψ_iso + φ`),
> and cannot corrupt a good measurement (well-measured frames self-anchor). **Two corrections the corpus
> forced:** (1) ψ is a *double* reversal — cock→release-to-≈0-at-impact→passive centripetal **re-hinge**
> in the follow-through, with **forearm rotation** (a third DOF, invisible face-on) dominating the middle
> — so the law's valid domain is **address→impact** and the reconciliation is bounded to the impact blur
> (`RECON_PHASES=("impact",)`); (2) roll, the third dimension, is deferred to the IMU/DTL/clubhead, not
> estimated from the axially-symmetric shaft. Re-gated synth → s01 → 10-swing studio corpus, all PASS
> (2026-07-07): narrowing to the impact blur recovered the follow-through (thru p90 5.5→**3.9** = v3.0
> baseline, thru coverage 649→**671**) while keeping the impact-blur win (down `bad>15`→0); release ψ-viol
> 104→**84** (the impact-only form flattens only the spurious impact-blur re-hinges and leaves the real
> follow-through re-hinge/roll in `psi_err`), flips 0, byte-identical determinism. Full detail:
> `club_tracking_v3_design.md` §8.1; research §3.8a + §4.7; as-built +
> studio-run playbook: `../implementation/shaft_detection_v3_impl.md` §v3.0-r1. Porting invariants for
> the isotonic form join 5/11 in §15 (fit-not-penalise; bands pin + anchor; blur-weight interpolation;
> impact-only domain; `recon` excluded from truth).

---

## 7. The phase model (C3, part one): reading the swing from the hands alone

Everything above needs to know *which phase* each frame is in, and C3 needs to know the rotation
direction. Crucially, we derive all of this **from the grip trajectory alone** (`segment_phases`),
with no club detection — so it cannot be fooled by the very counterfeits it is meant to gate.

- **Grip speed.** Frame-to-frame distance the hands move, smoothed (median then Gaussian, to reject
  single-frame jitter). During the address the hands only **waggle** (a few px/frame); during the
  real swing they travel much faster. A threshold (`SW_SPD = 8 px/frame`) separates the two.
- **Find the swing.** The two longest runs of above-threshold motion are the **backswing** and the
  **downswing/through**; the quiet frames before them are the address, after them the finish. The
  low-speed dip *between* the two runs is the **top** (the hands slow to a stop as the swing
  reverses).
- **Find impact.** If an external impact time was given, use it. Otherwise estimate it from the
  hands: the first frame after the top where the grip has dropped **back to address height** (the
  club has returned to the ball line). On the corpus this hands-only estimate landed within a
  handful of frames of the sensor truth on every swing — good enough, because the estimator does not
  need impact to be exact, only approximately located.
- **Assign phases.** Each frame is labelled address / backswing / top / downswing / impact (a ±12
  frame window around the impact frame) / thru / finish.
- **Chirality.** "Which way does this golfer's swing rotate?" We do not hard-code handedness. We
  look at how the smoothed arm angle φ turns across the backswing (`chir`, line 241): its sign tells
  us whether θ rises or falls in the backswing, and everything downstream is expressed relative to
  that sign. A left-hander is handled automatically.

From the phase and chirality we get two tables the estimator uses directly:

- `PHASE_SIGN` — the expected **sign of the rotation** per phase: `+1` in the backswing, `−1` from
  the downswing through the finish, `0` (either) at address/top where the club is nearly still.
- `WMAX` — the maximum plausible **rotation speed** per phase (deg/frame), generous so the true fast
  impact sweep (~16°/frame at 149 fps) is never clipped.

> **Why θ is monotone with a single reversal — worked through.** Say the backswing takes θ from 90°
> up to ~260°. Coming down, θ *decreases*: 260 → 180 → 90 (impact, club pointing down) → 0/360 →
> 300 → back around, continuing to *fall* the whole way to the finish. It looks like it "comes back
> up" on the far side, but in angle terms it has simply kept decreasing and wrapped past 0°. So the
> only place the *sign* of the rotation flips is the top. This is why the estimator can forbid sign
> changes everywhere except the top, and why a mid-swing "flip" is impossible by construction.

---

## 8. The global estimator (C3, part two): a Viterbi dynamic program

This is the engine that turns per-frame evidence into a single, physically coherent trajectory.

### 8.1 What problem it solves, and why brute force is impossible

We want to assign an angle to each of the ~745 frames. Represent the possible angles as a grid of
360 values (1° steps — `NS = 360`). A *trajectory* is one choice of angle per frame. There are
360⁷⁴⁵ possible trajectories — more than atoms in the universe — so we cannot score them all.

We score a trajectory by adding up two kinds of cost (lower is better):

- **Emission cost** — for each frame, "how *unlike* the image evidence is this angle?" (built in §9).
- **Transition cost** — for each step from one frame to the next, "how *physically implausible* is
  this change of angle?" (the smoothness and the C3 sign/speed limits).

### 8.2 The trick: the Markov property and dynamic programming

The transition cost of a step depends only on the angle at frame *f−1* and the angle at frame *f* —
not on the whole history. This "memoryless" property is what makes the problem tractable. The
**Viterbi** dynamic program exploits it:

1. Sweep forward through the frames. At each frame, for **every** grid angle, record the cost of the
   best trajectory that *ends at that angle* (`cost` array), together with which previous angle it
   came from (`back` array). Because of the Markov property, computing this needs only the previous
   frame's `cost` array — not any earlier history.
2. At the last frame, the smallest value in `cost` is the cost of the best overall trajectory.
3. **Backtrack:** follow the `back` pointers from that endpoint back to the start to read out the
   winning angle at every frame (`thstar`, lines 353–357).

This finds the *globally optimal* trajectory in time proportional to (frames × angles × reachable
neighbours) instead of exponential time. It is exact — there is no randomness and no iterative
guessing, which is why the whole tool is deterministic (§11).

### 8.3 "Banded" transitions encode C3 directly

We never allow arbitrary jumps. Between consecutive frames the angle can only move within a band set
by the phase (lines 334–352):

- The band **width** is `WMAX[phase]` (the speed limit).
- The band is **one-sided** to encode the sign: in the backswing only *increasing* steps are
  allowed; from the downswing through the finish only *decreasing* steps; at address/top steps in
  either direction. Wrapping past 0°/360° is handled with a circular shift (`np.roll`).

So the impossibility of a flip and the monotone impact bridge are not soft preferences — they are
*built into which transitions exist at all*. Within the allowed band, a mild quadratic penalty
(`K_SMOOTH × step²`) prefers gentle changes, which is what pulls a smooth line through an
evidence-free gap. (A soft "wrong-direction" penalty constant, `C_SIGN`, exists in the file but is
unused — the sign is enforced by the hard band instead; noted here so the port does not puzzle over
it.)

---

## 9. Assembling the emission cost (the crux)

For each frame we build one number per grid angle: the emission cost. Everything the constraints and
evidence "say" is folded in here, and getting the *relative strengths* right is what makes the DP
choose correctly. Reading `main` lines 272–328:

1. **Evidence → base cost.** Run E2 (raw and motion) over all 360 angles, normalise each frame's
   scores into 0…1 by mapping the 50th…97th percentile to 0…1 (`norm`), and take the stronger of the
   two. Call this `ev`. The base emission cost is `W_E2 × (1 − ev)`: strong evidence → low cost.
   (Normalising *per frame* by percentiles makes the numbers comparable across bright and dark
   frames, and is robust to a few extreme pixels — see §11 on why percentiles.)
2. **Constraint penalties (added on top).** C4 arm-veto (`W_ARM`), C4 wide cone (`W_CONE`), C1
   reverse-support (`W_C1`), C2 body-overlap (`W_C2`). These are *finite* additions, not infinities:
   they strongly discourage an angle without making the DP unable to pass through if it truly must.
3. **Band locks as negative wells — the single most important line.** When E1 locks a band pattern
   (and passes C1's `r0` test), the emission at that exact angle is set to `−W_BAND` (line 326): a
   cost *below zero*, a "well" the trajectory is drawn into. **Why this is essential:** without it, a
   correct band angle and a competing bright ridge would tie at cost 0, and the DP — preferring
   smoothness — would take the flatter, *wrong* path across the impact gap and miss the finish
   entirely (measured: thru/finish came out ~90° wrong until this well was added; with it, they
   snap to 0.3–0.8° median). The band locks are the fixed anchors; the DP threads the smooth,
   sign-correct, wrapping path between them.

The ordering matters: gentle guidance from evidence and constraints, overridden by hard anchor wells
where the strong detector fired.

---

## 10. Turning the trajectory into tiers (honesty at output)

The DP produces an angle for *every* frame, but we must not claim every frame is *measured*. After
backtracking, each frame is labelled (lines 360–395):

- **`band`** if a lock exists at that frame and the DP's chosen angle agrees with it (within
  `BAND_TOL = 6°`). Emit θ, s, r0, and the head position. Confidence scales with how many bands
  matched.
- **`ray`** otherwise, *only if* the evidence at the chosen angle is genuinely strong (`ev ≥
  RAY_EV_MIN`) **and clearly beats its own reverse direction** (a line and its 180° opposite must be
  told apart — "direction safety"), **and** it is *corroborated*:
  - In free-space phases, the frame must not be in a **sustained still period** (a static bright line
    cannot be verified as the moving club — the exact lesson v2 learned when static "locks" turned
    out to be counterfeits; `static` mask, lines 246–262).
  - In the **finish**, where the club overlaps and holds, a ray is only trusted if a real band lock
    sits within a few frames (`BAND_NEAR`). Otherwise it is demoted.
- **`pred`** for everything else — the DP's bridge value. Kept for the on-screen line; **excluded
  from the truth files** (lines 465–467).

This is the concrete implementation of "honesty by discrimination": address and unverifiable static
holds fall back to `pred` (silent in the truth), while genuine, corroborated measurements are
published.

---

## 11. The statistics: how we tune and trust it

None of the thresholds above were guessed or fit to make one video look good. The method is as
important as the code, and it is what a newcomer most needs to internalise.

### 11.1 The vocabulary

- **Error** for one frame = the angular difference between our θ and the reference θ, in degrees,
  wrapped to 0…180.
- **Median** = the middle value when errors are sorted. We prefer it to the **mean** (average)
  because a handful of bad frames drag a mean around, while the median reports the *typical* frame.
- **p90 (90th percentile)** = the value below which 90% of frames fall. It describes the *tail* — how
  bad the worse frames get — without being dominated by a single outlier.
- **"bad > 15°"** = the count/fraction of frames whose error exceeds a threshold we consider clearly
  wrong. This is the honest headline: not "how good is the average" but "how often are we visibly
  off".
- **Percentiles in the code, too.** The per-frame evidence normalisation (`norm`) maps the 50th→97th
  percentile of the ray scores to 0→1 rather than min→max, precisely so one blown-out pixel cannot
  define the scale. Same robustness principle as using the median for errors.

### 11.2 Why we always break the numbers down

A single overall average is a trap: it can look fine while hiding total failure in one region. So we
report **per phase** *and* **per tier**, never just a grand total. This is what exposed, and then
confirmed the fix for, the address softness: the corpus `band` tier is 0.3° with **0%** bad and the
`ray` tier 1.7° with 3% bad (both published to truth), while the larger address error lives entirely
in the `pred` tier (9.4° median) — which is *not* published. Without the per-tier split you would
either panic at the address number or, worse, ship the `pred` guesses as if measured.

### 11.3 The honesty clauses

Two standing rules encode "don't be confidently wrong":

- of the frames we get wrong, at least ⅔ should be low-confidence (we should *know* when we are
  unsure), and
- at most ~5% of high-confidence frames may be wrong.

The ray tier's 3%-bad sits inside that 5% budget; that is why it is allowed into truth.

### 11.4 How a threshold actually gets set: adjudication, not fitting

A number like `BAND_TOL = 6°` or the finish band-corroboration rule is chosen by **adjudication**:
run the tool, render the frames where it disagrees with the reference, *look at them*, and decide —
is our answer wrong, or is the *reference* wrong, or is this genuinely unverifiable? The threshold is
set to the physically-justified value that survives that inspection, and **not** tuned to squeeze a
single swing. (This matters enough that it is a standing project rule: one labelled swing is
development data only; accuracy claims and any parameter that affects them must hold across the whole
corpus, many swings.) When a rule was added — e.g. the negative-emission well, the static-hold
demotion, the finish corroboration — it was because a *specific adjudicated frame* demanded it, and
the comment in the code says which.

### 11.5 Determinism and the gate ladder

- **Determinism.** Same input → **byte-identical** output, every run. There is no randomness (the DP
  is exact; no random sampling anywhere). This is not a nicety: it means any change in output is a
  *real* change, so regressions are detectable by a simple file comparison. (One rule: only ever
  compare runs on the *same* machine — different OpenCV builds can differ in the last bit, which
  would masquerade as a regression.)
- **The gate ladder.** A change must pass, in order: **synthetic** (a generated swing with *known*
  ground truth and planted counterfeits — `make_synth_v3.py --selftest`; proves the machinery and the
  vetoes), then **single-swing** (one real swing, adjudicated on full-resolution montages), then the
  **corpus** (all 10 real swings: per-phase, per-tier coverage and accuracy, zero flips, byte-identical
  rerun) — plus a standing **counterfeit-regression suite** (`counterfeit_check.py`) that re-checks
  every historical false positive is still rejected. Only after all three does a result count.

### 11.6 What the numbers came out to (the record)

Across the 10-swing corpus, versus the previous system's truth: coverage in the fast phases rose from
57%→96% (downswing) and 54%→83% (through); measured accuracy is 0.3° (band) / 1.7° (ray) median;
**zero flips across all ten swings**; determinism byte-identical. The full table lives in the impl
doc §4.

---

## 12. Outputs

- **`<clip>_v3.csv`** — the rich per-frame record: frame, time, phase, tier, θ, s, r0, head position,
  body-overlap fraction, confidence. For adjudication and scoring.
- **`<clip>_v3_track.csv`** — the compact "shaft-track contract" (frame, grip, θ, kind ∈
  {meas, pred}, confidence) that downstream tools consume.
- **`truth.json`** (with `--truth-out`) — the published measurements, **band and ray only** (pred
  excluded), timestamped, for use as reference truth. Refuses to overwrite hand-labelled truth.
- **`<clip>_v3.mp4`** (with `--overlay`) — the video with the measured shaft drawn on, colour-coded
  by tier (red = band, amber = ray, grey = pred). This is what a human looks at to adjudicate.
- **`<clip>_v3_phases.csv`** (with `--phases-out`) — the per-frame phase, grip speed, and smoothed
  arm angle, for debugging the phase model.

---

## 13. The v3.1 companion — impact-zone velocity (`shift_stack_v3.py`)

Everything above is v3.0: it estimates *where* the club points, θ(t), across the whole swing. The one
phase it fills only with the softer `ray` tier is the **impact zone** — the ±10 frames around the ball
strike, where the club moves fastest and every frame is a blur. `shift_stack_v3.py` is a small companion
that attacks that zone with a different question. Instead of asking *where* the club is (v3.0 already
answers that), it asks **how fast it is turning** — the angular velocity ω(t) — and it answers from the
blur itself, independently of v3.0's dynamic program. It is **additive**: it reads v3.0's frozen output,
touches only the impact window, and never rewrites v3.0's truth, so it cannot regress anything.

Why bother, when v3.0 already tracks θ through impact? Three reasons. First, clubhead speed at impact is
a headline coaching number in its own right (the shaft's ω times its length is the head's speed). Second,
a velocity read from the *raw blur* — owing nothing to the tracker — is an independent physical check: a
tracked angle whose implied spin is impossible for a human swing can be caught. Third, the future learned
clubhead detector (v3.3) trains on blur synthesised from the *measured* ω(t), so we need that ω per frame.

### 13.1 The physics: the shutter is open almost the whole frame

The camera's exposure is 6.57 ms and its frame period is 6.70 ms — the shutter is open for **98%** of
every frame. That one number is the key. During those 6.57 ms the club keeps rotating, so each frame's
shaft is not a line but a **fan** — a streak smeared across the small angle the club swept while the
shutter was open. Because the exposure is essentially the whole frame period, the angle swept *within*
one frame is essentially the angle *between* one frame and the next. So the same velocity can be read two
ways, and they must agree:

- **Between frames:** how far θ moved from frame *t* to *t+1* — just `dθ/dt` of v3.0's track.
- **Within one frame:** how wide the blur fan is — the angular extent of a single frame's streak.

The second is the novel one, and it needs no differencing between frames at all.

### 13.2 Two velocity estimators (and one we dropped)

- **`omega_track`** — the signed central difference of v3.0's θ, `(θ[t+1] − θ[t−1]) / 2`, wrapped to
  ±180°. Reliable, because v3.0's θ is reliable. The **emitted** ω is this, lightly smoothed (a Gaussian
  over ~1 frame), since the raw difference is noisy frame to frame.
- **`omega_exparc`** — the single-frame blur width (§13.3). **Independent of the tracker.** Its job is to
  *corroborate* omega_track, not replace it.
- *(Dropped: cross-correlating consecutive frames' angular profiles to find the rotation between them. It
  sounds independent, but where the club sweeps across the legs and mat it locks onto that static
  background instead of the club and reads ≈0 — an honest dead end, recorded so no one re-derives it.)*

The point of two estimators is the **agreement**: two methods that share no machinery landing on the same
curve is strong evidence the curve is real, not an artefact of v3.0's own smoothing.

### 13.3 The exposure-arc measurement, step by step

We already know roughly where the club points each frame (θ0, from v3.0). To measure the blur fan's width
we read brightness along an **annulus** — a ring of radii from the grip, out where only the club lives
(95–335 px) — as a function of angle, in a tight window (±30°) around θ0. That gives a 1-D profile: a bump
wherever the shaft's bright streak crosses the ring, flat and dark elsewhere. The fan's width is the width
of that bump. Getting it robustly is the whole game:

1. **Anchor at θ0, not at the brightest angle.** The single brightest point in the window might be the leg
   or the mat, tens of degrees away; we already *know* the club is at θ0, so we grow the bump outward from
   there. This one choice removes most of the errors an earlier version made.
2. **Use an equivalent width, not a half-maximum.** The bump is peaked, not a clean box, so "width at half
   height" underestimates it. Instead take (area of the bump) ÷ (its height) — the width of the rectangle
   with the same area and height. Threshold-free and stable.
3. **Subtract the shaft's own thickness.** Even a *stationary* shaft has some angular width (it is a few
   pixels thick). Subtract a small constant (~3°) so the number reflects the *sweep*, not the shaft.

The fan width divided by the 98% duty cycle is |ω| for that single frame. The **leading and trailing
edges** of the fan (`theta_lead`, `theta_trail`) are θ at the instants the shutter opened and closed —
sub-frame angle samples, a bonus the differencing methods cannot give.

Honesty note: this single-frame estimate is *noisy* — about 2.6°/frame of scatter, worse where the club
overlaps the bright body. That is exactly why we never trust one frame: we **smooth the profile** and read
its **peak**, which is what corroborates the tracker. The per-frame scatter is reported as a diagnostic
and deliberately *not* used as a pass/fail bar (§13.5).

### 13.4 Shift-and-stack: exact geometry, and an honest null result

The second half of the module borrows a trick from astronomy: to see a faint moving object, shift a stack
of exposures so the object lines up, and its light adds up while everything else blurs away. Here the club
rotates about the grip, so for a short window of frames we translate each frame so its grip sits on the
reference grip, rotate it by (that frame's θ − the reference θ) about that grip, and average the stack.

A subtle geometric fact makes this *exact* for a rigid club: after registering the grips together and
rotating by the θ difference, the club's butt lands at exactly `grip − s·r0·û(θ0)` **for every frame,
whatever that frame's θ was** — so a perfectly rigid club maps onto itself. The club integrates coherently
while the body and background, which do not share its rotation, smear into arcs.

So why is this a *companion* and not a new measurement tier? Because on the **taped** corpus it buys
nothing. The retro-reflective tape at near-full-frame exposure makes every single frame already bright;
the stack is then limited not by sensor noise (which averaging beats down) but by **jitter in the supplied
grip anchor** — and since the geometry above is exact, that jitter is the *only* residual, and it
*broadens* the average rather than sharpening it. Concretely: zero band re-locks on nine of ten swings.
The composite is still worth producing — as an **adjudication image** it shows the club fused into one
clean streak lying exactly along v3.0's θ while the legs and mat smear, direct visual proof the tracked
angle is on the club through the blur — but the *product* is the ω curve, not a new tier.

The value is held in reserve for a different regime: the **passive, un-taped** club (a customer's own
club, no bright bands), where each frame's shaft genuinely is faint and √N integration over a de-rotated
window may be the only way to see it. That the de-rotation geometry is provably exact is what tells us the
payoff there is limited only by how well we register the pivot — an engineering problem, not a physical
wall.

### 13.5 Outputs, and the gates it passed

- **`<clip>_v31_impact.csv`** — per impact-window frame: θ (from v3.0), omega_track, omega_exparc, the
  emitted smoothed ω, the fan width and its sub-frame edges, and any tier carried over.
- **`<clip>_v31_omega.png`** — the ω(t) plot: track, exposure-arc, and the emitted curve overlaid, so a
  human can see the two independent estimators agree.
- **`<clip>_v31_composites.png`** — the montage of de-rotated stacks, for adjudication.

The gate ladder (§11.5) applies here too. The **synthetic** gate (`make_synth_v31.py --selftest`) uses a
rendered swing whose ω is known exactly: the exposure-arc recovers the peak to within 1%, the emitted peak
to within 9%, zero flips. The **single-swing** gate on s01 gave a peak of 13.6°/frame (74 mph clubhead)
from the track and 13.0°/frame (71 mph) from the independent exposure-arc — agreeing to 0.5°/frame at the
peak. The **corpus** gate over ten swings gave clubhead peaks of 71–92 mph on every swing, the two
estimators agreeing to a median of 1.5°/frame (worst 3.6°). The pass/fail is on the **smoothed peak
agreement**, not the per-frame scatter — the same reason we read the profile's peak and not one frame.

---

## 14. The v3.2 companion — address / hold θ (`address_theta_v3.py`)

v3.0 measures θ everywhere the club *moves*, but it deliberately stays silent at the **address** — the
resting setup before the swing. Recall §10: the ray tier is switched off whenever the phase is `addr`.
The reason is honesty (§11.3): a club held *still* is exactly the case v2 was fooled by — a static bright
or dark line at the hands cannot be motion-verified, and the address scene is full of look-alikes (a
trouser crease, the golfer's trailing leg and its shadow on the blown mat, the mat edge). So v3.0 emits
the whole resting address as an unpublished `pred` bridge (on the corpus, the softest zone: `pred` median
9.4°, never written to truth). `address_theta_v3.py` *measures* that resting θ and, when it survives the
honesty gates, publishes it. Like v3.1 it is **additive**: it reads the frozen v3.0 track, works only on
the address hold, and never rewrites v3.0 — so it cannot regress anything.

Why is the address, the very case v3.0 abstains on, now tractable? Three facts, each a tool.

### 14.1 The hold-period stack — a still-club shift-and-stack

The address is a long *near-still* hold: the hands only waggle a few pixels while the golfer settles.
Register every hold frame on the grip anchor and average them. This is exactly v3.1's shift-and-stack
(§13.4) but with **zero rotation** — the club is not turning, so no de-rotation is needed. The club, rigidly
attached to the (registered) grip, integrates into a sharp line; everything that moves *relative to* the
grip — the swaying legs, body and shadows — smears away. On the real corpus this is a powerful counterfeit
suppressor: on s01 the trailing-leg line actually *outscores* the club in any single frame (raw ridge:
leg ≈449 vs club ≈427), yet after 81 hold frames are stacked the leg is smeared below the club and the
club wins even with a wide search. (This is the same √N-integration geometry as v3.1; here it pays off on
taped data precisely because the counterfeit *moves* and the club does not.)

### 14.2 The tight address cone — C4, specialised for rest

The bible's C4 cone (§6) is deliberately **wide** (150°) because the wrist angle ψ = θ − φ swings across
65–107° during the moving swing. But **at rest the club hangs nearly in line with the lead arm** — ψ is
small (s01: θ ≈ 89°, φ ≈ 114°, so ψ ≈ −25°). So an address-*only* tight cone, `|θ − φ| < 28°` about the
smoothed arm direction, rejects the leg/crease counterfeits that the wide cone must admit. This does **not**
contradict the wide-cone invariant (§14 porting rule 5) — that governs the *moving* swing; this is its
address specialisation. It is also what pins **direction**: the arm points down to the grip at address, so
the cone is a *down* cone, and a 180° flip (which points *up*, into the body) falls outside it — flips are
structurally impossible here, just as C3 makes them impossible mid-swing.

> **A gate we deliberately do NOT use at address: forward-vs-reverse "dir-safety."** v3.0's ray tier
> checks that the evidence along θ beats the evidence along θ+180 (§10). At address that test is *wrong*:
> the lead arm is always directly above the grip, so the club's reverse direction always carries the arm's
> ridge — the same "the arm is legitimately behind the hands, so it is excluded" reasoning as C1/C4 (§6).
> Direction is pinned by the down-cone + a down-sector check instead. (Adjudicated: a perfectly vertical
> arm makes reverse-evidence equal forward-evidence, which would wrongly veto a correct measurement.)

### 14.3 The mat-crossing prior — a lenient sanity gate, not a discriminator

The resting club crosses from the hands, over the dark trouser (where the tape bands glow bright), down
onto the **blown-out mat**, where it becomes a *dark* line on a bright background — the polarity flip of
§5.2. The natural temptation is to use this to tell the club from the counterfeits. **Adjudication says
no:** on s01 both the club *and* the trailing-leg line cross the mat with the right polarity (the leg
casts a dark shadow there), so mat-crossing cannot separate them — the **cone + stack + stability** do
that. Its honest role is narrower and still worth keeping: confirm the measured ray is a *real,
ground-reaching* line (it reaches the blown mat and carries a coherent, polarity-correct shaft), which
kills a short body-only blob that never reaches the floor. Set the threshold leniently and let the cone do
the discrimination.

### 14.4 What it publishes, and the honesty gate

Because the tape almost never forms a lockable band pattern at the address exposure (the blown mat swamps
it — measured: zero band locks on s01, on the stack and on single frames), v3.2 is **θ-only**: it emits θ
(and grip), never s/r0/head, unless a rare opportunistic band lock fires on the stack. A hold θ graduates
from `pred` to a new `hold` tier — published to truth — only if the stack passes *all* of: strong ridge
evidence at θ0; the mat-crossing sanity; θ0 in the down sector; θ0 inside the tight cone; and the per-frame
θ **stable** across the hold (standard deviation ≤ 8° — a persistent club, not transient noise). Per-frame,
a hold frame publishes θ0 only where its *own* cone-ridge θ agrees within a few degrees; otherwise it stays
`pred`. The truth merge is additive and precedence-correct: it never overrides a v3.0 band/ray entry (v3.0's
stronger measurement wins where it exists), only fills the address frames v3.0 left silent.

### 14.5 Scope, outputs, and the gates it passed

v3.2 targets the **pre-swing still hold** only. (On s01 the v3.0 phase model lumps a 424-frame span into
`addr`; that span is really a long still hold *plus* the takeaway, which v2 fusion band-tracked from f397 —
a v3.0 phase-model mislabel that a later phase-model fix, not v3.2, will address.) Outputs: `*_v32_address.csv`
(per hold frame: θ, the v3.0 tier it replaces, the published tier, the per-frame θ, and any s/r0/head) and
`*_v32_address.png` (the stack with the measured line drawn, for adjudication).

The gate ladder (§11.5) applies. The **synthetic** gate (`make_synth_v32.py --selftest`) renders a known
resting θ with two planted address counterfeits: one *in-cone*, body-attached and oscillating (a single
frame's continuous bright line outscores the dotted club, but the grip-registered stack smears it — this
tests the stack) and one *out-of-cone* and grip-rigid (it survives the stack and must be rejected by the
cone). It recovers the club θ exactly (error 0.00°), rejects both counterfeits, publishes, and flips zero
times. The **single-swing** gate on s01 detects the hold (f304–384), measures **θ0 = 88.65°** — the montage
shows the line landing on the resting club while the trailing-leg counterfeit is smeared away — recovers
**77** previously-`pred` address frames as published `hold`-tier θ, with per-frame scatter 3.4° and zero
flips; the rerun is byte-identical on-host. The **corpus** gate (all ten swings) is the remaining rung, run
on the canonical studio PC (only s01 is synced to the dev box).

---

## 15. Porting to C++ — what must be preserved

The C++ port's acceptance test is **output agreement with this exemplar** on a fixed input (a
"byte-oracle" on one reference platform, modelled on the existing parity tests). To make that
achievable, preserve these invariants; they are the parts where a well-meaning "improvement" will
silently change results:

1. **Determinism and evaluation order.** No randomness. Compute the emission array, then the DP,
   then the tiers, in this order. The band **negative well is applied last**, after all constraint
   penalties, so it dominates them — do not reorder.
2. **The banded, sign-restricted transitions ARE constraint C3.** They are not an optimisation to be
   "cleaned up" into a full transition matrix; the one-sided band is what makes flips impossible.
3. **Percentile-based normalisation** (per-frame 50th/97th; error medians/p90) — not min/max, not
   means. Robust statistics are load-bearing.
4. **The tiering/honesty rules** (band well, ray dir-safety + corroboration, static-hold and finish
   demotion, pred-excluded-from-truth) are the "zero readmitted junk" guarantee. Each corresponds to
   an adjudicated failure; removing one re-opens that failure.
5. **Wide, part-time C4.** Keep the cone wide and disabled at address/finish/top. Do not tighten it.
6. **The evidence engines E1/E2** must match `stripe_fusion`'s numerically (they are the other
   half of the byte-oracle) — port them faithfully rather than re-deriving.

For the **v3.1 companion** (`shift_stack_v3.py`), the same discipline, plus:

7. **The emitted ω is the smoothed track difference, not the exposure-arc.** The exposure-arc is the
   *independent corroborator*; do not swap it in as the primary. Preserve the sign/wrap of the central
   difference and the ~1-frame Gaussian smoothing.
8. **The exposure-arc is θ0-anchored, equivalent-width, thickness-corrected.** Grow the bump outward
   from the *known* θ0 (not the global maximum), measure width as area÷height (not half-maximum), and
   subtract the fixed thickness constant. The annulus radii, the tight ±30° window, and the 98% duty
   cycle are calibrated numbers — port them, do not re-tune.
9. **Shift-and-stack registration is grip-translate then rotate-about-grip, averaged (`nanmean`).**
   The composite is *not* body-masked (that would eat the club at impact); it is an adjudication image.
   Do not "improve" it into the primary measurement — on high-signal taped clubs it adds no tier.
10. **Gate on the smoothed peak agreement, never the per-frame scatter.** The per-frame exposure-arc
    error (~2.6°/frame) is its intrinsic noise floor; the deliverable is the profile's peak.

For the **v3.2 companion** (`address_theta_v3.py`), the same discipline, plus:

11. **The address cone is TIGHT (`|θ−φ| < 28°`) and is a SEPARATE, address-only mechanism** — it does
    not touch, and does not contradict, invariant 5 (v3.0's mid-swing cone stays wide and disabled at
    address). Do not widen it (it re-admits the leg/crease) and do not apply it to the moving swing (ψ
    is large there — it would clip real swings).
12. **The hold-period stack is grip-translate-and-average with ZERO rotation** (the club is still), over
    the still hold only. It is a counterfeit *suppressor*, not just an adjudication image: a swaying
    counterfeit that outscores the club per-frame is defeated by it. Detect the hold from the hands
    alone and cap its length (older frames risk a prior occupant of the bay).
13. **No dir-safety, and mat-crossing is a lenient sanity gate.** At address the arm is always above the
    grip, so forward-vs-reverse evidence is *not* a valid test (invariant excluded by the same logic as
    C1/C4's arm exclusion); direction is pinned by the down-cone + down-sector. Mat-crossing only
    confirms a real ground-reaching, polarity-correct line — it does **not** separate club from
    counterfeit (the cone/stack/stability do). Do not tighten it into a discriminator.
14. **θ-only; publish on stack + per-frame stability.** No s/r0/head unless a rare band lock fires on the
    stack (the address exposure blows the tape out). Graduate `pred`→`hold` only when the stack passes
    all gates and the per-frame θ is stable across the hold; the truth merge is additive and never
    overrides a v3.0 band/ray entry.

What is *safe* to change: language, data structures, parallelism, I/O. What is *not*: the constraint
logic, the cost ordering, the statistical estimators, the honesty gates, and — for the companions —
which estimator is emitted versus corroborating (v3.1) and the tight-cone/stack/stability publish gates
(v3.2).

---

## 16. Glossary

- **Address / backswing / top / downswing / impact / follow-through / finish** — the phases of the
  swing (§1.1).
- **θ (theta)** — shaft direction angle (grip → head), image degrees.
- **ω (omega)** — angular velocity of the shaft, degrees per frame; × frame rate × shaft length gives
  the clubhead's linear speed. The v3.1 product (§13).
- **φ (phi)** — lead-forearm direction (elbow → grip), from body pose.
- **ψ (psi)** — wrist angle, θ − φ; anatomically bounded (C4).
- **Grip / anchor** — the hands' pixel position, supplied per frame.
- **s (scale)** — pixels-per-mm; shrinks with foreshortening. **r0** — butt offset behind the grip.
- **Band / retro-reflective tape** — bright markers at known positions; their asymmetric pattern
  fixes θ, s, and direction (E1).
- **Ridge** — a bright-or-dark straight line along a ray from the grip (E2).
- **Polarity** — the shaft's contrast sign flips (bright on cloth, dark on blown-out mat).
- **Foreshortening** — apparent shrinkage of the club as it tilts out of the image plane.
- **Chirality** — the rotational sense of the swing (handedness), inferred from the arm's turn.
- **Tier (band / ray / pred)** — measurement confidence class; only band/ray reach truth.
- **Counterfeit** — a scene feature that mimics the club (trouser crease, arm/sleeve, leg shadow,
  mat speckle, impact streak-flip).
- **Emission / transition cost** — per-frame image fit / per-step physical plausibility, summed by
  the DP.
- **Dynamic program (Viterbi)** — algorithm that finds the globally optimal trajectory in linear
  time by exploiting the memoryless (Markov) step structure.
- **Convex hull** — the smallest convex outline enclosing a set of points (here, the body joints for
  C2).
- **RANSAC** — a robust fit that finds the model (here, a line) supported by the most inliers.
- **Median / percentile / p90 / "bad>15°"** — robust summary statistics (§11.1).
- **Determinism / byte-identical** — same input yields exactly the same output, enabling regression
  detection by file comparison.
- **Gate ladder** — synthetic → single-swing → corpus acceptance sequence.
- **Adjudication** — deciding disagreements by human inspection of rendered frames, the basis for
  every threshold.
- **Duty cycle / exposure fraction** — the share of each frame during which the shutter is open (98%
  here); it makes the within-frame sweep ≈ the between-frame sweep (§13.1).
- **Motion-blur streak / fan** — a single frame's smeared shaft, whose angular width is the angle the
  club swept while the shutter was open (§13.3).
- **Exposure-arc** — reading |ω| and sub-frame θ from the width and edges of that streak (§13.3).
- **Equivalent width** — a bump's area divided by its height; the width of the same-area rectangle, a
  threshold-free width estimate (§13.3).
- **Shift-and-stack** — de-rotating a window of frames about the grip and averaging, so a rigid rotating
  club integrates coherently while the background smears (§13.4).
- **Hold-period stack** — shift-and-stack with *zero* rotation: grip-register and average the frames of a
  *still* hold, so the resting club integrates sharp while swaying counterfeits smear (§14.1).
- **Address hold** — the pre-swing still period where the club rests on the mat; v3.2's target (§14).
- **Address cone** — the tight, address-only C4 cone (`|θ−φ| < 28°`); at rest ψ is small, so this rejects
  the leg/crease look-alikes a wide cone admits (§14.2). Distinct from v3.0's wide mid-swing cone.
- **Mat-crossing prior** — the resting club crosses onto the blown mat as a dark line (a polarity flip);
  used as a *lenient* ground-reaching sanity gate, not a club/counterfeit discriminator (§14.3).
- **`hold` tier** — v3.2's published address-θ tier (θ-only), between v3.0's `ray` and `pred` in the
  honesty hierarchy; graduates a resting frame from unpublished `pred` to truth (§14.4).
