# Position-first shaft measurement — design (v3.5)

Status: PROPOSED (2026-07-11) · Author: session with Mark following the club-length
fusion P6 validation · Companion: [`docs/implementation/shaft_position_first_impl_plan.md`](../implementation/shaft_position_first_impl_plan.md)

## 1. Problem statement

Three findings motivate this design, all confirmed on the 2026-07-10 corpus
(`corpus-0710-fusion`, 11 swings) and by direct review of the replay overlay:

1. **The drawn shaft line frequently does not lie on the club even when θ is
   nearly correct.** Mechanically inevitable: the per-frame line is
   `grip anchor + θ·dir + length`, and the grip anchor is the raw midpoint of
   the two pose-estimator hand keypoints (`shaft_tracker.cpp` ~:142), linearly
   interpolated to the camera timestamp. Nothing downstream ever re-registers
   the line against image evidence, so a 10–20 px hand-keypoint error
   translates the whole line off the shaft. Worse, the evidence engines score
   rays *through* that anchor — with an offset anchor, the best-scoring θ is a
   compromise ray that clips the shaft rather than lying along it. This is the
   same root cause as the ψ-rail finding: pose-φ noise, not physics.

2. **We have no metric for the failure the eye sees.** `ShaftSample2D.conf`
   derives from the smoothed θ-posterior variance — confidence in the *angle*,
   not in "does this line lie on the club." A frame can be conf 0.8 and
   visibly 15 px off the shaft.

3. **The optimization objective is uniform per-frame coverage**
   (`coverage = spanMeas / spanFrames`, gate `shaft.coverageMin`), which
   spends effort equally across ~750 frames. But coaching review happens at
   the P-positions: coaches and players scrub to P1–P8, compare against
   references, and draw on those frames. A track that is 72% covered but weak
   exactly at P4/P6/P7 fails its actual users. The club-length fusion (P6,
   d325268) improved *summary* robustness and added confidence reporting, but
   did not change how any single shaft measurement is made — this design does.

### The coaching P-system (contract of this design)

| P | Event | Today's source |
|---|---|---|
| P1 | Address | `Phase::Address` (segmentation) |
| P2 | Shaft parallel, backswing | **missing** |
| P3 | Lead arm parallel, backswing | `Phase::MidBackswing` (hand-proxy) |
| P4 | Top | `Phase::Top` (segmentation) |
| P5 | Lead arm parallel, downswing | **missing** |
| P6 | Shaft parallel, downswing | `Phase::Delivery` (hand-proxy — enum comment already says "until shaft-measured") |
| P7 | Impact | `Phase::Impact` (detector-anchored) |
| P8 | Shaft parallel, follow-through | **missing** |

(P9 `FollowThrough` and P10 `Finish` already exist; they ride along for free.)

**Accepted caveat (Mark, 2026-07-11):** "shaft parallel" is read in the image
plane of the face-on camera, not true 3-D parallel. This matches how coaches
work with face-on video today, so it is standard practice, not a compromise —
but it is documented here and in the schema so nobody later mistakes P2/P6/P8
for 3-D geometry.

## 2. Design overview — three layers

The v3 DP tracker is **kept as the temporal backbone**. It locates events,
carries the wrap through occlusion, and provides the physics prior. The layers
below refine, anchor, and synthesize *around* it — nothing replaces it.

```
            existing v3 DP track (θ(t), grip(t), tiers, Stage-2 heads)
                    │
   A. line re-registration («snap»): per frame, refine (⊥offset, Δθ)
      against the ridge field; record lineConf     ── fixes “not on the club”
                    │
   B. P-anchors: locate P1–P8 times from smoothed signals, then run a
      MILESTONE FIT (±k frames, shift-and-stack, joint grip/θ/L) at each
      → analysis.club.positions[]                  ── guaranteed quality where it matters
                    │
   C. synthesis: constrain the kinematic model through the P states;
      frames between anchors become an explicit
      Synthesized tier for visualisation           ── smooth scrub, honest flags
```

### Layer A — line re-registration + `lineConf`

After the DP settles θ per frame (post PASS 2 placement, pre-serialization),
refine two degrees of freedom per sample — perpendicular offset `d ∈ ±snapMaxPx`
(~15 px) and small `Δθ ∈ ±snapMaxDeg` (~3°) — by maximizing the shaft-ridge
line integral along the candidate line. The ridge/Sobel field already exists
for E2 evidence; the search is a small local grid (or two 1-D golden-section
passes), so cost is negligible next to the pose pass.

Outputs per sample (additive):
- the snapped `gripPx`/`headPx` (the anchor moves onto the club; the pose grip
  remains recoverable from the pose2d stream — we do not overwrite pose data),
- `lineConf ∈ [0,1]` = normalized ridge support under the drawn line — the
  first metric that measures exactly what the eye objects to. `-1` = pass off.

Snap NEVER changes tier decisions, DP costs, θ smoothing, ψ, or segmentation —
it is a display/measurement registration applied after the decision layer, so
its blast radius is confined to the emitted samples. θ may shift by ≤ snapMaxDeg
on snapped frames; the corpus gate (impl plan) bounds the effect on θ-metrics.

### Layer B — P-anchors and milestone fits

**B-time location** (cheap, robust): P-times are 1-D events on already-smoothed
signals, far more stable than any per-frame measurement:
- P1/P4/P7 from existing segmentation events (unchanged).
- P2/P6/P8: image-plane shaft-parallel crossings of the smoothed θ(t) from the
  v3 track (θ crossing horizontal, in the correct swing phase window each).
- P3/P5: lead-arm-parallel crossings of the pose φ(t) (upgrading P3's existing
  hand-proxy; P5 new).
New enum values (additive): `ShaftParallelBack = 12 (P2)`,
`ArmParallelDown = 13 (P5)`, `ShaftParallelThrough = 14 (P8)`; P3/P6 events
gain `provenance = Club` once shaft-measured (the enum comment's promise).

**B-fit** (the heavy accuracy work, spent only ~8 places per swing): at each
P-time, a milestone fit over a window of ±k frames (k≈3–5):
1. **Shift-and-stack registration**: register the ±k frames along the locally
   predicted motion (from the smoothed ω(t)) and average — √N noise reduction,
   the same mechanism planned for the untaped-club investigation. At P1 the
   shaft is still, so the stack is a plain average; at P7 the stack *through*
   the impact blur gap is exactly the untaped plan's bridge.
2. **Joint refinement** of (grip, θ, L) on the stacked image against ridge +
   band evidence + arm-plausibility sector, plus ball evidence at P1/P7.
   Initialized from the (snapped) v3 track at the P-time.
3. Output per P: `{p, t_us, gripPx, thetaRad, lenPx, headPx, conf, sigmaThetaDeg,
   sigmaLenPx, stackN, source}` → new `analysis.club.positions[]` block.

**Free wins that fall out of B:**
- The escalated band-scale bias (`runs/ESCALATION.md`, 2026-07-10) largely
  dissolves: the band px/mm scale gets measured at P1 where the shaft is
  in-plane and still, instead of a median over foreshortened mid-swing locks.
- Club-length fusion (P6 work) re-points at per-P lengths: E-ball and E-band
  at P1, E-head at P4–P8 stacks — tighter, in-plane estimators. The fusion
  math, prior, persistence, and serialization all stay exactly as shipped;
  only candidate *sourcing* improves. `fusion.sigFracBand` should re-tighten
  after the corpus re-fit (it currently encodes the foreshortening bias).

### Layer C — synthesis between anchors

Between consecutive P states, fit the existing kinematic model (the R7
predicted-series machinery / DP physics bridge already model θ̈) constrained to
interpolate through the anchor states — a boundary-value fit in (θ, grip),
C¹-continuous at anchors. Frames between anchors are emitted as an explicit
**Synthesized** tier:
- new flag `ShaftSynthesized` (see §4 — flags widen to uint16_t),
- replay overlay extends its existing measured-vs-projected honesty: bright at
  anchored/measured frames, dim synthesized in between,
- consumers that must not use synthesized data (metrics, scoring, SwingLab
  truth comparisons) filter on the flag — same pattern as `KinematicPredicted`.

Synthesis REPLACES nothing: `samples[]` keeps the real per-frame track
(post-snap); the synthesized series is emitted alongside (reusing the
`predicted[]` channel semantics or a third array — impl plan decides, but
serialization is additive either way).

## 3. QA contract change

Headline per-swing quality becomes **per-position coverage**:

> "8/8 positions measured · min conf 0.61 · P7 σθ 1.4° · σL 5 px"

instead of "72% of frames measured". `ShaftTrack2D.coverage` and its gate stay
(they still guard the DP backbone); the new `positions[]` block carries the
per-P statement, the data viewer's Analysis group gains a positions row, and
SwingLab gains per-P scorecard checks (`track.p_coverage`, `track.p7_theta_err`
vs markup). Marketing/coach-facing claims should be made ONLY from the per-P
numbers, per the score-estimand discipline.

## 4. Data model & schema (all additive)

- `ShaftSample2D`: `float lineConf = -1.f` (Layer A). `flags` widens
  `uint8_t → uint16_t` — the byte is full (0x01–0x80 all assigned) and Layer C
  needs `ShaftSynthesized = 0x100`. JSON already serializes flags as int;
  readers use `toInt()`; QML sees int. Pure-C++ recompile, no schema break.
- `ShaftTrack2D`: `std::vector<ShaftPosition> positions` (Layer B) — new
  struct as in §2B. Absent/empty for pre-v3.5 swings; readers treat empty as
  "no position data" (same contract as `lengths`).
- `Phase` enum: three additive values (12/13/14); phase serialization is by
  int, and the UI phase-label map gains three labels.
- swing.json `analysis.club`: `samples[].lineConf` (absent = pass off),
  `positions[]`, and Layer-C synthesized samples carry their flag — all
  additive; both parity writers (swing_doc.cpp + shot_processor
  toAnalysisDetail) stay in lock-step as with `lengths`.
- Config: new dotted-key namespaces `shaft.snap.*` (Layer A), `positions.*`
  (Layer B), `synth.*` (Layer C) via the existing `fromOverrides` pattern, each
  with a master `enabled` gate defaulting OFF until its corpus gate passes
  (then flipped ON in a dedicated commit, exactly like `shaft.headPass`).

## 5. Blast radius (what this deliberately does NOT touch)

Preserving the progress to date is a design constraint. The following are
untouched by all three layers:

| Surface | Status |
|---|---|
| v3 DP tracker (evidence engines, tiers, physics bridge, ψ rail) | kept as-is; snap runs after placement; B/C consume its outputs |
| Stage-2 head pass | kept; reused inside B-fits at P4–P8 |
| Club-length fusion + persistent prior (P1–P6 work, d325268) | kept byte-for-byte; B3 re-points its candidate sourcing only |
| Segmentation v3, ball pipeline, IMU stack, capture/EventBuffer | untouched |
| Wrist scoring/metrics | untouched until per-P data is validated (they keep reading `samples[]`) |
| Replay overlay | Layer A changes only *where* the line draws (onto the club); Layer C adds a dim tier — additive states |
| swing.json schema | additive only; every reader defaults on absence |
| SwingLab harness | additive scorecard checks; existing checks unchanged |

**Rollback story:** each layer sits behind its `enabled` flag; OFF reproduces
the pre-layer output byte-for-byte (soak-gated per layer, same discipline as
`fusion.enabled=0` in P6). A regression discovered post-flip is handled by
flipping the default back — one-line change.

**The one deliberate behavior change:** Layer A moves the drawn line onto the
club. Downstream θ consumers see ≤ `snapMaxDeg` shifts on snapped frames; the
A2 corpus gate requires θ-vs-markup error to improve (or stay) globally with 0
per-swing regressions before the flag flips.

## 6. Validation strategy

Ground truth: markup-lab hand labels (`truth.json`; P7 markup already used as
the impact reference in IMU-less runs). Per layer:
- **A**: new estimand = median ⊥ distance from drawn line to labelled shaft
  segment on labelled frames; gate = distance improves on every labelled swing,
  θ-RMS vs markup does not regress, snap-off soak byte-identical.
- **B**: per-P estimands = |Δθ|, |ΔL|, grip ⊥ error at each labelled P vs the
  P-time's markup frame; conf must correlate with error (same discipline as
  fusion conf). P-time location error vs labelled P-times ≤ 1 frame median.
- **C**: synthesized frames are *excluded* from all accuracy estimands by flag;
  the only C gates are visual (overlay honesty) and continuity (no anchor
  discontinuities > small ε).
- Standing rule applies: this corpus (one club/camera/athlete) validates
  machinery, not accuracy claims; multi-club corpus re-validation before any
  coach-facing claim.

## 7. Risks / open questions

1. **Flags widening** touches every flags consumer (C++ bitmasks fine; QML
   `flags & 0x10` fine) — mechanical, but listed because it is the only
   non-additive type change.
2. **Snap on clutter**: in cluttered backgrounds the ridge corridor may snap to
   a distractor edge. Mitigations: corridor bounded ±15 px, arm-plausibility
   sector veto reused, lineConf floor before accepting a snap.
3. **P-times on short/abbreviated swings**: P2/P3/P5 crossings may not exist
   (e.g. punch shots). `positions[]` simply omits them; per-P coverage reports
   n/8 honestly.
4. **P8 visibility**: follow-through often exits frame on tight crops;
   off-frame handling reuses the Stage-2 `HeadOffFrame` convention.
5. **UI surfacing of P badges** (timeline, carousel) is explicitly deferred to
   a UX pass with Mark — this design lands the data; the View-menu/ViewLayout
   owns any new toggles when that pass happens.
