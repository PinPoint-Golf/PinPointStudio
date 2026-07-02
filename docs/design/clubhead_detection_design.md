# PinPoint — Clubhead Detection: Technical Design (Stage 2)

**Status:** Design for implementation (2026-07-03). Exemplar-first, same
methodology as the shaft work — see
[shaft_detection_exemplar_findings.md](shaft_detection_exemplar_findings.md)
for the methodology and the F1–F17 lessons this design inherits.
**Scope:** locate the clubhead position `P_h(t)` (pixels) per frame by tracing
along the stage-1 shaft ray, with an honest measured/predicted output model.
**Non-goals (v1):** clubface orientation, 3D head position, ball tracking,
strike location on the face.

---

## 1. Problem statement and the two-stage split

Stage 1 (the shaft exemplar) answers *"in which direction does the club point
from the hands?"* — a 1-DOF angular problem. Stage 2 answers *"how far along
that direction does the club end?"* — a 1-DOF **radial** problem:

```
P_h(t) = P_g(t) + r_h(t) · d(θ(t))        r_h = projected grip→head distance (px)
```

The head is therefore found by a **search along the ray**, exactly as the shaft
was found by a search over angle. All the machinery that made stage 1 work
transfers: per-sample evidence channels robustly accumulated, a 1-D tracker
with gating, per-segment RTS smoothing, and a measured/predicted output tier.

`r_h(t)` is the *projected* shaft length: constant when the shaft lies in the
image plane, smoothly shrinking under foreshortening (top of backswing),
recovering by impact. Smoothness in time is the head's strongest prior, the
way "attached to the hands" was the shaft's.

## 2. Decoupling contract — the load-bearing requirement

Shaft detection will keep improving; clubhead detection must not notice.

### 2.1 The interface

Stage 2 consumes **only**:

1. the source video (clip) and `clipmeta.json`;
2. the stage-1 **track CSV**, reading exactly these columns
   (shaft track contract **v1**):

   | column | meaning |
   |---|---|
   | `frame` | clip frame index |
   | `grip_x`, `grip_y` | grip anchor (px) |
   | `theta_out` | the shaft angle a consumer should use (deg, unwrapped) |
   | `kind` | `meas` \| `pred` |
   | `conf` | stage-1 confidence 0..1 |

   All other columns are stage-1 internals and MUST NOT be read.

Stage 2 never imports stage-1 code, never reads its internal state, and
computes its own scene references (background median etc.) from the video
independently. Anything else it needs from stage 1 must arrive as a new,
versioned column in the contract — a deliberate, reviewed act.

### 2.2 Error-budget robustness (semantic decoupling)

Interface stability is not enough: stage-1 *behaviour* changes too (a better
shaft algorithm shifts θ by a degree, re-tiers frames, moves the grip). Stage 2
must be **specified to tolerate** stage-1's documented error envelope rather
than fitted to its current quirks:

- θ tolerance: search a wedge of **±5°** about `theta_out` (stage-1 measured
  median error is ~3°; hand-label noise similar).
- grip tolerance: lateral refinement window absorbs **±10 px** anchor error.
- tier awareness: on `kind=pred` frames the ray itself is a kinematic guess —
  stage 2 may still measure (the head streak is often visible when the shaft
  is not) but must widen the wedge and demand stronger evidence; its own
  output tier is decided by its own evidence, never inherited from stage 1.

### 2.3 Test decoupling

Stage-2 regression tests run against **frozen stage-1 track CSVs** (the lab
keeps per-swing `v6/faceon_swing_track.csv` fixtures). Stage-1 improvements
cannot break stage-2 tests; a separate integration run exercises the live
pipeline. When stage 1 is intentionally re-frozen, the fixtures are re-frozen
with it — one reviewed event, not silent drift.

### 2.4 No feedback loops (v1)

Stage 2 produces a running projected-length estimate `L(t)` that stage 1 could
use (design §8.2 scale estimation — currently unbuilt). **v1 deliberately does
not feed it back.** If adopted later, it flows through the same contract
discipline (a versioned input stage 1 reads at startup), never a runtime loop.

## 3. What the head looks like (observations from the shaft work)

Hard-won facts from the corpus, all of which killed naive designs in stage 1:

1. **Appearance is unreliable.** The head is bright chrome against dark wall in
   one pose (0009 hanging), an *unlit dark silhouette* in another (0009
   shouldered finish, B=0.12), and dark-against-bright-mat at address. No raw
   luma gate survives this (F13/F16 lessons).
2. **Change-vs-scene is reliable for static poses**: the head *arrived* where
   the pre-swing median scene had none — works regardless of polarity
   (|I − median| responds to dark-on-bright and bright-on-dark alike).
3. **The shaft's own evidence run ends at the hosel.** Stage 1 already
   computes the last sustained run sample; its radial position is a noisy
   lower bound on r_h (specular dropout ends runs early — F8 lesson).
4. **Width expansion**: the shaft is a ~5 px thin line; the head is 30–60 px
   wide. The lateral profile widening at the end of the thin section is a
   shape cue independent of brightness polarity.
5. **At speed the head is an arc streak**, the fastest-moving object in the
   frame (motion evidence is enormous), smeared along the swing arc — the
   radial position is still well-defined (the streak's radial extent is
   narrow), even when its tangential extent is huge.
6. **The ball is a decoy at address** — compact, bright, near the ray end. But
   it is present in the pre-swing median scene → the change channel suppresses
   it for free. (After impact it is gone entirely.)
7. **Cropping is real**: post-impact the head exits the frame before the shaft
   does. "Off-frame" is a first-class output state, never a fabricated
   position (capture guidance: frame wider to the player's right).

## 4. Evidence and measurement

Per frame, sample a band along the ray: radii `r ∈ [0.35·L̂, min(1.25·L̂, r_edge)]`
(L̂ = current smoothed length estimate; `r_edge` = where the ray exits the
frame), lateral offsets `u ∈ ±W_lat` (~24 px) across the ray normal, over the
θ wedge of §2.2. Channels, each clamped to [0,1]:

- `E_chg(r)` — max over lateral offsets of |I − median_scene| / τ_chg
  (the arrived-object channel; scene median computed by stage 2 itself).
- `E_mot(r)` — max over lateral offsets of min(|I − B|, |I − I_prev|) / τ_m
  (anti-ghost motion, the stage-1 validated form).
- `E_end(r)` — shaft-termination shape score: thin-line (edge-pair) evidence
  integrated over `[r−Δ, r]` minus over `[r, r+Δ]` — high where a sustained
  thin line *ends*.
- `E_wide(r)` — width-expansion score: lateral second moment of the
  gradient/change profile at r, normalized; high where the silhouette is
  head-wide rather than shaft-thin.

Combination (OR-family, per stage-1 lessons — never AND brightness-dependent
channels):

```
E_head(r) = max(E_chg, E_mot) · (0.5 + 0.5·max(E_end, E_wide))
```

i.e. presence (change/motion) is required; shape (termination/width) sharpens
the peak. Peak → `r_meas` with parabolic refinement; then a small 2-D centroid
over the change/motion mask in a `±W_lat × ±Δr` window refines `P_h` off-axis
(the physical head extends laterally from the hosel; the hand-label convention
is the visual end of the club line — adjudicate against 0008 zooms in H1).

**Acceptance gates** (all tunable, set from data as in stage 1): peak
prominence over the band median; minimum `E_head`; `r_meas` inside the
plausible annulus `[0.5, 1.15]·L_max_observed`; off-frame declared when
`r_edge < 0.8·L̂` (the expected head position is outside the image — emit
`off`, never a measurement).

## 5. Temporal model

1-D Kalman filter on `[r_h, ṙ_h]` (constant velocity; Q sized for
foreshortening rates, not gentle motion), 3σ gating, speed-aware coast budget,
**per-segment RTS** (the stage-1 structural lesson applies verbatim — never
smooth across a re-acquisition). Segments additionally split wherever the
stage-1 track itself re-initialises (a θ jump moves the whole ray; radial
continuity across it is meaningless).

Cross-check (honesty, not fusion): during smooth measured phases,
`|Ṗ_h| ≈ r_h·|ω|` (tangential) — persistent gross disagreement with stage-1's
ω flags the segment for review rather than silently trusting either stage.

## 6. Output contract (stage 2 → consumers)

Per frame, appended as a separate CSV (never merged into stage-1's file):
`frame, r_h, head_x, head_y, conf_h, kind_h, channels…` where
`kind_h ∈ {meas, pred, off}`:

- `meas` — gated, confirmed measurement (conf_h ≥ 0.5 in runs ≥ 4, as stage 1).
- `pred` — smoother/bridge value along the stage-1 ray at smoothed `r_h`
  (low-confidence detections are **discarded**, per markup policy).
- `off` — the expected position lies outside the frame: no position is
  emitted at all (`head_x/y` empty). Predictions may resume when the ray
  re-enters.

truth.json mapping: `head = (head_x, head_y)` for `kind_h=meas` frames;
`len = r_h`. The markup writer combines stage-1 `theta` + stage-2 `head`;
score.py's existing gates apply (`truth.head_median_px < 25`,
`track.head_step < 0.25`).

## 7. Failure-mode table (design-time, from stage-1 experience)

| failure mode | phase | mitigation |
|---|---|---|
| ball decoy near ray end | address | change-channel (ball is in the pre-swing median) |
| bright mat at ray end | address/hang | E_chg not raw luma (F13 lesson); shape term |
| unlit head, dark-on-dark | finish | change + width channels carry; no luma gate anywhere |
| specular dropout ends shaft run early | all | run-end is a *prior*, not the measurement; E_head peak decides |
| head streak at impact | impact ±3 fr | radial extent stays narrow — measure radial peak, ignore tangential smear; wedge widened by stage-1 band_deg is NOT used (internal column) — use fixed ±5° + motion mask |
| head occluded by body | wraparound | coast + pred tier; body proximity known from skeleton.csv if needed (optional gate, same file stage 1 uses — an independent input, not stage-1 coupling) |
| head off-frame | post-impact | `off` state, explicit; capture fix is the real solution |
| stage-1 θ slightly wrong | all | ±5° wedge + lateral centroid |
| stage-1 pred-tier ray | post-impact/finish gaps | widen wedge to ±10°, require higher prominence; stage-2 tier from its own evidence |
| foreshortening vs occlusion confusion | top of swing | L̂ dips smoothly under foreshortening (KF tracks it); occlusion is abrupt → gate rejects, coast |

## 8. Validation

Same protocol as stage 1 (findings doc + impl doc §Verification):

- **Numeric oracle**: swing_0008's 51 hand labels carry `head` px —
  per-frame head error (median/mean/p90 px), split by `kind_h`, honesty
  clauses (bad frames must be low-conf; ≤5% of high-conf frames bad).
  score.py's `truth.head_median_px < 25` is the existing external gate.
- **Visual**: montage triage + full-res zoom adjudication on 0009 (hard
  example) — head marker drawn on the annotated video; landmark frames
  adjudicated once, then regression-checked.
- **Corpus**: 0002–0007 sweep; per-phase coverage (address/backswing/
  downswing/impact/finish), never totals alone.
- **Synthetic**: extend `make_synth.py` with a head blob (+ streak under the
  sub-exposure integrator) and head ground truth; acceptance: r_h RMSE < 4 px
  at ≤40% dropout, zero false heads on clean clips.
- **Decoupling test**: stage-2 numbers must be *identical* across a stage-1
  internals-only change (same contract output) — assert byte-identical
  stage-2 CSV against a frozen stage-1 fixture.

## 9. Relationship to the app C++

Same rule as stage 1: **exemplar first, port later.** The production
`ShaftTracker` projects `headPx` from grip + L·dir(θ) (flagged
`ShaftHeadProjected`) — it does not measure the head; nothing in the app
implements this design. The eventual port ships stage 1 and stage 2 as two
modules with the same CSV-shaped contract between them (design §11.1's
`ClubheadDetector` slot), gated by §8's protocol.
