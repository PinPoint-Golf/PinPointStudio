# Club tracking v3 — physics-first design and research plan

**Status: design, 2026-07-05.** Successor to stripe fusion v2.0
(`stripe_fusion.py`, as-built notes in `stripe_fusion_design.md`; full
adjudication record in lab `tape_20260705/RESULTS.md`). Reviewed premise,
from Mark: v2.0 fought every counterfeit with a generic guard and bought
honesty by abstention — coverage shrank with each patch. v3 makes the golf
swing's physical structure the primary discriminator, so counterfeits are
rejected by *understanding* while real measurements are kept.

## 1. The four fundamentals, promoted to first-class constraints

**C1 — the club is held in the hands.** It is impossible for the club to
be anywhere that does not start at the hands. The full-strength form is a
*butt-termination test*: club evidence terminates within `r0 ∈ (0,
~260 mm]` behind the grip anchor. A line whose support continues behind
the butt (trouser crease, screen edge, shaft shadow) is a scene line —
vetoed regardless of how well it scores. (v2.0 used only the weak form:
"ray passes within 80 px of the anchor". Every adjudicated counterfeit
passed that gate.)

**C2 — the club overlaps the body only at address/impact and finish.**
From takeaway through top, downswing, and follow-through the club is in
free space. `skeleton.csv` (8 body joints/frame, already produced by
`prep_swing.py`) defines a per-frame body polygon (+ margin, temporally
smoothed through blur-degraded pose frames). Phase-dependent rule:

| phase | body-region evidence |
|---|---|
| takeaway → top → downswing → thru | ignored; candidate with majority support inside the body polygon is VETOED |
| address, impact zone, finish | admitted, but never sufficient alone — requires free-space support or a band lock |

Every counterfeit adjudicated in v2.0 (trouser creases, the s06 leg shadow
that stage-1's measured tier tracked, sleeve/shirt texture, foot-region mat
speckle) lives inside the body polygon during phases where the club cannot.

**C3 — the swing changes direction at the top.** Phase segmentation comes
from the hands alone (`anchors.csv`): grip speed + trajectory reversal
give still → takeaway → backswing → TOP → downswing → impact zone → thru
→ finish, with no club detection required. Within each phase the rotation
direction of θ(t) is known (chirality detected once per swing from the
hand trajectory — no handedness hardcoding), so a 180° flip becomes
*structurally impossible* rather than statistically filtered, and outage
bridging becomes a monotone bounded-rate sweep instead of naive
interpolation (v2.0's impact-zone flips cannot occur).

**C4 — club + lead arm form a dual pendulum.** With α(t) = lead-arm
direction (pose; φ is already in `anchors.csv`) and θ(t) = shaft
direction, the wrist angle ψ = θ − α is anatomically bounded and evolves
smoothly: hinging during the backswing, monotone release through the
downswing, aligned (ψ ≈ 180°) near impact. This yields a per-frame
*reachable cone* for θ — the candidate search space shrinks from 360° to
tens of degrees — plus a smoothness prior on ψ that is gentler than on θ
itself (ψ changes slowest exactly where θ changes fastest is *not* true
near release, but ψ's bounds still hold; the DP transition model encodes
both).

> **Revised, 2026-07-06 — read with §8.** The *cone* above is a **magnitude**
> bound on ψ, and the v3.0 as-built found it unrealisable as a search-space
> reducer: pose φ jumps up to ~87°/frame at the top and through the blur, so the
> cone had to be widened to 150° and switched off at address/finish/top, with C3
> (not C4) carrying the real collapse. The *stronger* structure of the double
> pendulum — that ψ is **monotone with a single reversal**, exactly C3's law but
> on the wrist — was never encoded. §8 develops it as a DP transition rail; it is
> the more important half of C4 and the current work-front.

## 2. v3.0 core: constrained global estimator

State per frame: θ (shaft direction), with (s, r0) attached when bands
lock. Evidence engines are **unchanged and already validated** — E1
band-ratio matcher (1.1–3.3° median, zero flips on corpus) and E2
polarity-aware ridge — but they are evaluated only inside the constraint
set C1∧C2∧C4, per phase C3.

Estimation is global, not per-frame: dynamic programming (Viterbi) over a
θ grid across the whole clip, transition costs from C3/C4 (phase-signed
rotation, |ω| ≤ ω_max(phase), |ω̇| bounded, ψ within anatomy), emission
costs from E1/E2. This replaces v2.0's local corroboration/interp guards,
which demote to sanity assertions. Output tiers as in v2.0 — `band`
(θ+s+head), `ray` (θ-only), plus DP-bridged `pred` (kept out of truth
files, same honesty stance). Determinism preserved (no RNG; DP is exact).

Expected effect vs v2.0, to be verified not assumed: finish and address
θ coverage return (their counterfeits are now excluded by C1/C2 instead
of by abstention), down/thru coverage at least maintained, junk stays
zero.

## 3. v3.1 variant: rotation-compensated shift-and-stack ROI

The astronomy trick, adapted: the club rotates about a (moving) hand
pivot. For a window of 2k+1 frames and a hypothesized rotation profile
ω(t) (drawn from the C3/C4-constrained DP prior — a few tens of
hypotheses, not a blind grid):

1. translate each frame so the grip anchor is registered (kills hand
   translation);
2. rotate frame t by −∫ω dt about the registered pivot;
3. stack (median or mean).

Under the correct ω the club pixels integrate **coherently** across the
window — SNR grows ~√N on top of per-frame streak energy — while the body
and background smear into arcs. The stack that maximizes coherent ridge
energy along a ray simultaneously yields: the club **region of interest**,
a refined ω estimate for the window, and a deblurred composite in which
the 2-1-3 band pattern can re-emerge at speeds where any single 6.57 ms
frame is hopeless. C2 applies before scoring: body-polygon pixels are
masked out of the coherence measure (per Mark: ROI-finding must respect
the overlap schedule), and C1's butt-termination is tested on the stacked
composite.

Pipeline placement: coarse pass on ¼-resolution frames proposes the club
corridor per window; E1/E2 (and v3.0's DP) then run full-res inside the
corridor only. This is the designed attack on the last uncovered fast
segment (impact ±10 frames, where v2.0 emits nothing) and doubles as
compute containment.

A cheap sibling for moderate speeds stays: per-frame |frame − scene
median| ROI (v2.0's motion suppression), now intersected with ¬body.

## 4. v3.2: address/hold θ truth

With C1 (butt termination), C2 (address-mode body rule), C4 (cone) and
the hold-period pixel stack (valid — the club is physically static), the
address line search is no longer "brightest thin line wins": candidates
are lines inside the cone, terminating at the hands, with the mat-crossing
property (at address the shaft is the *only* line that continues — dark —
across the blown mat toward the ball). Lateral line fitting (fit the line,
then require hand-proximity) absorbs the pose-anchor bias that force-
through-anchor sampling suffered. Output: θ-only truth for stills; (s, r0)
only if the band pattern locks (it usually cannot at this exposure —
known optics, not a tooling gap).

## 5. v3.3: learned components — where training data beats hand-tuning

Physics stays the frame; learning fills perception gaps that classical
templates handle poorly. Rules of engagement: frozen versioned weights,
deterministic inference (no augmentation at test time), corpus-gated
acceptance like every other stage, ONNX Runtime deployment (already in the
app for Kokoro — the path exists).

**Heel/toe keypoint head (Mark's ask).** A small heatmap CNN (lightweight
U-Net-class, single class per keypoint: heel, toe, optionally hosel) that
localizes clubhead extremities inside the v3.1 ROI. Why learned: the head
is the one component with no ratio template — shape varies per club, blur
smears it anisotropically, and stage-2's classical terminus logic is the
current honesty bottleneck (clause fails on 7/10 swings). Why feasible
without hand-labeling thousands of frames — the **data flywheel**:

1. instrumented captures + stripe truth pin the shaft line, scale s, and
   hosel extrapolation per frame → weak labels for heel (hosel-adjacent)
   and a search ray for toe;
2. human adjudication of a stratified subset (the lab's montage workflow)
   promotes weak labels to gold;
3. blur augmentation synthesized from measured ω(t) (we know the true
   per-frame sweep from truth) manufactures hard training examples that
   match this camera's actual physics;
4. acceptance: head px error and conf-honesty clauses vs instrumented
   truth on held-out swings; the NN must beat stage-2's classical head
   measurement on the SAME corpus before it ships.

Payoff beyond the lab: a head detector trained this way does not depend on
tape — it is the bridge from instrumented lab clubs to the **passive
product path** (unmarked clubs), which is where the programme wants to
land (protocol §5.3).

**Deliberately NOT learned (for now):** shaft θ (E1/E2 + physics already
achieve ~1° with zero flips; a NN adds risk, not accuracy) and the phase
model (trivially derived from pose).

## 6. Research plan and gates (each stage corpus-gated before the next)

| stage | content | gate |
|---|---|---|
| v3.0 | phase model, C1–C4 constraint set, DP estimator wrapping validated E1/E2 | synth gate extended with phases + body polygons + counterfeit population; corpus: zero adjudicated errors AND coverage ≥ v2.0 in down/thru, > 0 at finish + address-adjacent; determinism |
| v3.0-r1 | **C4 ψ-monotonicity transition rail** (Δψ = Δθ − Δφ sign-lock + rate bound; §8) — the double pendulum's monotone structure, the substantive form of C4 replacing the wide cone | gate-0 (s01 hand markup) PASSED; then re-gate synth → s01 → corpus + fixture re-freeze; zero flips; coverage/accuracy through impact ≥ current v3.0; determinism |
| v3.1 | shift-and-stack ROI + ω measurement in the blur zone | impact ±10 frames: θ/ω emitted with visual adjudication of stacked composites; ω(t) smooth, peak within physical range for a 7-iron; no regression elsewhere |
| v3.2 | address/hold θ truth | agreement with stage-1 address meas (reliable there) + hand-label spot checks; zero flips |
| v3.3 | heel/toe NN + data flywheel | beats classical stage-2 head on held-out instrumented swings; honesty clauses pass; frozen weights versioned |
| v4 | C++ port behind `IShaftDetector` (marker mode + passive informed by truth); NN via ORT | exemplar-oracle byte-agreement for classical parts; app-side corpus replay |

Cross-cutting metrics from v3.0 on: per-phase coverage AND accuracy vs
instrumented truth (never totals alone), ω(t) profile sanity, stage-2 head
error trend, and a standing counterfeit-regression suite built from every
adjudicated failure to date (trouser crease, leg shadow, shirt texture,
arm line, mat speckle, streak-centroid impact locks — each now a named
test case).

## 7. Risks

- **Pose anchor bias/outage** at peak blur → lateral line fits + C1 test
  rather than force-through-anchor; body polygon temporally smoothed.
- **Phase mis-segmentation** on unusual swings (pumps, rehearsal waggles)
  → phase model must emit confidence; low-confidence spans fall back to
  v2.0's conservative rules.
- **C4 ψ-rail is only as clean as φ's trend** (§8) → the ψ monotonicity is
  pristine but pose φ spikes to ~87° at the top/impact; de-spike φ, key the
  rail on the *sign/trend* of Δψ not instantaneous magnitude, and relax it at
  the top (C3 covers direction there). Do **not** pin ψ's reversal to the
  hand-top — give a transition window; the lag is player-dependent and is
  itself a coaching metric.
- **Left-handed athletes** → chirality from trajectory, mirrored anatomy
  bounds; add a LH capture to the corpus before v3.0 freezes.
- **NN scope creep** → v3.3 is a keypoint head inside a physics ROI, not
  an end-to-end tracker; anything more waits for corpus evidence.
- **Compute** → ROI-first ordering keeps full-res work bounded; DP grid
  is 1-D per frame (θ), cheap.

## 8. C4 revisited — the ψ-monotonicity transition rail (2026-07-06)

**The gap.** §1's C4 promised a per-frame reachable *cone* collapsing the θ
search from 360° to tens of degrees. The v3.0 as-built (bible §6; impl §4) found
that promise unrealisable — pose φ jumps up to ~87°/frame at the top and through
the blur, so the cone was widened to 150° and switched off at address/finish/top,
and **C3, not C4, carried the search-space collapse**. C4 survived only as a wide
guardrail (an into-forearm veto plus removal of the reverse half-circle). But that
cone is a **magnitude** bound on ψ = θ − φ, and it was the *only* part of the
double-pendulum physics we encoded. The stronger structure was left out.

**The stronger structure (Mark, 2026-07-06).** ψ obeys the same one-reversal law
C3 gives θ, but on the *wrist*: from address to the top ψ cocks **monotonically**
(the interior arm–shaft angle closing ≈180°→≈90°); at transition it reverses
**once**; from transition through impact to the finish it releases
**monotonically**. Re-hinging in the backswing, or un-hinging in the downswing, is
anatomically impossible. Because θ = ψ + φ and the arm φ has its own motion,
"ψ is one-sided per phase" is *strictly stronger* than "θ is one-sided per phase"
(C3): it constrains the shaft's rotation **relative to the measured arm**.
(Convention: signed ψ = θ − φ *increasing* through the backswing is the same
physics as the coach's interior wrist angle *closing*; interior = 180° − |ψ|.)

**The recast: C4 becomes a transition term, not a cone.** Model C4 as a DP
transition cost on Δψ = Δθ − Δφ, structurally parallel to C3's cost on Δθ:

- **Sign-lock (phase-signed):** cocking sign through the backswing, the opposite
  through downswing→finish (both relative to the detected chirality); a
  transition **window** around the top where the sign is free.
- **Rate bound:** |Δψ| ≤ ψ̇_max — a wrist-hinge speed limit (WMAX-class,
  corpus-set, generous enough never to clip a fast release).
- **Smoothness:** a mild quadratic penalty on Δψ (or its curvature).

This drops into the existing Viterbi **with no state-space growth**: ψ_f = θ_f −
φ_f depends only on the DP state θ_f and the *per-frame constant* φ_f, so the
transition (θ_{f−1}, θ_f) → cost still reads only the two adjacent θ states. It
rides on top of C3; it does not replace it.

**Why this succeeds where the cone failed.**

1. **Monotonicity, not magnitude.** The standing warning is "do not tighten the
   cone — it clips real swings" (bible invariant 5). A monotonicity constraint
   never says ψ is *near* any value; it says ψ cannot reverse within a phase. It
   clips only the physically-impossible paths — exactly what we want — and it is
   far more forgiving of φ noise, needing only the *trend* of φ (Δφ over smoothed
   φ), not its instantaneous value.
2. **The arm becomes a witness for the club through the blur.** In the impact
   zone — our thinnest coverage, where the DP free-runs on θ-smoothness across an
   evidence-free gap — the arm is *more* measurable than the club (larger,
   slower, less blurred). A bounded monotone-release rail pins θ = ψ + φ using the
   well-tracked arm precisely where the club is invisible: a qualitatively better
   bridge than "flattest θ path."

**What it demands of φ.** The physics is clean; the noise is entirely in pose φ
(§8 gate-0 below). So the rail must de-spike φ robustly (median + unit-vector
Gaussian as v3.0 already does, plus outlier rejection), lean on the *sign/trend*
of Δψ rather than instantaneous ψ, and **relax at the very top**, where φ is worst
— conveniently the phase where C3 is already strongest.

**The transition is not the hand-top.** ψ's reversal *lags* the hand-top for
players who hold their cock into transition ("lag"). So the ψ-reversal must not be
pinned to C3's top: give it a window and let evidence place it. The lag magnitude
is itself a coaching metric (kinematic sequence).

**Gate-0 — hand-verified on s01 (2026-07-06).** Before any DP change, the law was
checked directly on real data. Mark hand-marked the shaft of corpus swing s01
(`2026-07-05_…_Wrist_02/swing_0001`, 121 labels of grip+head+θ, honest gaps
through the impact blur; artefact `truth.json`), and ψ = θ_markup − φ_pose was
compared against the *independent* v2 fusion truth. Where the two θ sources
overlap they agree to a **median of 0.01°** (p90 3.4°, n = 78). ψ is monotone on
**55/58 backswing steps and 53/56 downswing steps** — and every one of the six
exceptions falls on a pose-φ glitch frame, not a real wrist reversal. The θ
trajectory (which owes nothing to φ) is pristinely monotone with a single reversal
at the top; all residual ψ scatter is pose φ, which jumps a median of 1.6°/frame
but **spikes to 87° on 17 frames** clustered at the top and impact. ψ's peak sits
a few frames *after* the hand-top — the release lag. Figure 1 shows both panels.

![s01 hand markup vs v2 truth: θ single-reversal arc, and ψ = θ − φ as a monotone tent with one reversal.](../research/figures/club_track_psi_s01.png)

***Figure 1.** s01, hand markup vs v2 fusion truth. **Top:** shaft angle θ
(unwrapped) — the two independent witnesses coincide (median 0.01°), a clean
single-reversal arc (C3). The straight segment across f510–545 is the bridge over
the impact-blur gap. **Bottom:** ψ = θ − φ with robustly-smoothed pose φ (green) —
a textbook tent: cocks monotonically to the top, releases monotonically to the
finish, one reversal. Grey dots are ψ with **raw** pose φ, exposing the φ-noise the
rail must survive; red × is ψ from the v2 truth. Vertical lines mark P1–P10; the
dashed line is the hand-top P4 (ψ peaks slightly later — the release lag).*

**Status and cost.** This changes the DP transition bands → the frozen v3.0 track
re-solves → it is **v3.0-internal**, requiring the full protocol (re-gate synth →
s01 → corpus + fixture re-freeze), not an additive companion. It also
**supersedes the blocker on the deferred takeaway-mislabel fix** (impl §4): a
ψ-rail carries the near-still early takeaway (grip barely translates, but ψ is
already evolving and the arm is moving), which is exactly why that fix was parked
behind "explore C4 first."
