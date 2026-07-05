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
- **Left-handed athletes** → chirality from trajectory, mirrored anatomy
  bounds; add a LH capture to the corpus before v3.0 freezes.
- **NN scope creep** → v3.3 is a keypoint head inside a physics ROI, not
  an end-to-end tracker; anything more waits for corpus evidence.
- **Compute** → ROI-first ordering keeps full-res work bounded; DP grid
  is 1-D per frame (θ), cheap.
