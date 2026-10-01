# Shaft Uncertainty Propagation — Design

**Status:** implemented 1 October 2026 (U0–U7). `uncertainty.enabled` is ON. Soft anchors (U3), one impact instant (U4) and the forward–backward posterior (U5) are built and dark: each failed its gate. Calibration and verdicts are in `docs/research/data/uncertainty/calibration_20261001.md`. Written 1 October 2026 for a clean implementation session.
**Owner:** Mark. **Implements:** the shortcoming recorded in `docs/developer/shaft_tracker_developer_guide.md` §10.4 ("uncertainty is not propagated").
**Deliverables:**
1. This design note.
2. The implementation in stages U0–U7 (§8), each behind its own switch, gated on the corpus, and committed only at the end after Mark has judged the results.

---

## 1. The problem

The shaft tracker is careful about *what kind* of statement each frame is:

- a band lock;
- a segment lock;
- a ridge measurement;
- a wedge edge;
- a reconstruction;
- a prediction;
- a synthesised tick.

It says almost nothing about *how wrong* that statement may be. The metrics built on it inherit even less. As of `f76d3b65`:

| Where | What exists | What is wrong with it |
|---|---|---|
| `ShaftSample2D::conf` | 0.30 / 0.40 / 0.45 / 0.55 / 0.62–0.70 / 0.75–0.90 by tier | Labels, not probabilities. A RAY frame is 0.55 whether its evidence was 0.46 or 0.99. There is no unit. |
| `ShaftSample2D::headSigmaPx` | Kalman posterior σ of the head radius | Real, but no metric reads it. |
| `ShaftPosition::sigmaThetaDeg/sigmaLenPx` | Plateau half-width from the milestone fit | Only on positions the fit re-measured (B2). −1 on every track-sampled position. |
| `ShaftWedgeObs::sigmaDeg` | 4.5° per edge | The median hand-mark residual, used as a constant. |
| `ClubLengthEstimate::fusedSigmaPx` | Inverse-variance fusion σ | Real. Used only to draw milestone-fit heads. |
| `MetricSeries::sigma` | One scalar per series. Persisted, bridged to QML, drawn as the "± σ" chip, and used by `ChartMetrics::formatBare` to choose the display step. | Lean: a constant 9.5°. Low point: a constant 2.0 in. Swing plane: the plane RMS, which is scatter rather than the standard error of the inclination. Every other shaft metric: absent. |
| `PhaseSample` | Value at an instant | No σ. The card's headline is an instant reading, but the only σ available is per series. |
| Fusion `Sample3D`, DTL `DtlSample` | `cond`, `conf` | No σ on the direction. |

The consequences:

- A reading off a band lock and a reading off a coasted frame look identical on a card.
- A clubhead speed computed with the default 1.12 m driver length looks as certain as one with the club recorded.
- The scorer and the diagnostics cannot weigh a reading by its quality.

The project already has the right contract for fixing this. The `MetricSeries::sigma` comment says: *"1σ MEASUREMENT uncertainty … ABSENT means 'not characterised', NOT 'zero error' … Set it only when a real error budget was propagated; confidence must WIDEN this, never nudge `value`."* `score_uncertainty.h` follows the same pattern for the wrist score, with the budget σ_x = √(σ_sensor² + σ_crosstalk² + (dθ/dt·σ_timing)²). This design extends that contract to the club.

## 2. Goals and non-goals

**Goals:**

1. **Per-sample σθ.** Every face-on and DTL shaft sample carries a 1σ angular uncertainty in degrees, calibrated against hand marks, plus a separate **gross-error probability**.
2. **Per-instant σ.** Every shaft metric in guide §8 publishes a 1σ uncertainty for each phase reading, together with the series-level σ. The σ is propagated from the samples, the lengths, the ball, the pose and the timing, not asserted.
3. **Honest σ, demonstrably.** On held-out hand marks, about 68% of normalised errors fall within ±1σ and about 95% within ±2σ, per metric, wherever truth exists.
4. **Bias reported separately from noise**, never folded into σ.
5. **The dark idiom.** With the master switch off, every swing on the pinned corpus is byte-identical to `f76d3b65`. With σ on, every metric **value** is unchanged; only σ fields are added. The one deliberate exception, soft anchors in U3, has its own switch and its own gate.

**Non-goals for this session:**

- Changing any metric's value, except through the separately gated U3 soft anchors.
- Uncertainty for non-shaft metrics. The body metrics have their own plan in `metric_presentation_honesty.md` §5.3. This design reuses its display path, not its producers.
- Using σ in fault firing or scoring thresholds. §9 covers that as a follow-up with an open question.
- Per-frame σ shading on charts. U7 is optional, depending on time.

## 3. Principles

These rules govern every stage below. Most are restatements of decisions the project has already made elsewhere.

1. **σ is 1σ measurement noise, in the metric's own unit.** It is not a coaching tolerance (that is `bandLo/bandHi` and the norm σ) and not a confidence.
2. **Absent means "not characterised".** A producer sets σ only when it has propagated a real budget. A metric with no truth to calibrate against may still publish a *propagated* σ, but it must be marked uncalibrated (§6.4).
3. **Confidence widens, never moves.** Nothing in this design changes a value to reflect uncertainty, except U3's soft anchors (separately gated).
4. **Bias is not noise.** Known systematic errors go into a documented bias table (§6.5), and are corrected if correctable. They are never inflated into σ. Examples: clubhead speed reads 0.959 of the launch monitor; the acoustic trigger is 13–22 ms early.
5. **Gross error is not noise.** The tracker's characteristic failure is a *whole stretch on the wrong structure*, often off by 100° or more (guide §3.4, §10.3). No Gaussian σ describes that. Each sample gets a separate gross-error probability `pGross`. Metrics expose the gross risk of the samples they read as a flag, never folded into σ. Refusal (guide §4.18) remains the response to a detected gross error.
6. **Errors are correlated in time.** Adjacent frames share pose, lighting and solver state. Any fit or average that treats readings as independent will be overconfident. Every place that pools readings applies a calibrated effective-sample-size correction (§5.2) rather than √N.
7. **Deterministic.** Monte Carlo draws use a fixed seed and a hand-written normal generator (Box–Muller over `std::mt19937_64` raw output). `std::normal_distribution` is not portable across standard libraries, and the byte-identity gates require identical numbers on Mac and studio.
8. **One impact instant.** σ for an impact reading includes the timing term (dθ/dt)·σ_t. That only makes sense if every impact reading uses the same instant. U4 settles that (guide §10.5).

## 4. The model, layer by layer

```
pose σ (PoseKpAux::sigma, px) ─┐
ball σ (cluster scatter, px) ──┤
club record σ (mm) ────────────┤
                               ▼
  per-sample   σθ, pGross  ◄── calibrated tier table (U1)  /  forward–backward posterior (U5)
  per-sample   σ_grip
  length       σ_L (fusion, exists)
  timing       σ_t per event (U4)
                               ▼
  synthetic track posterior: σθ(t), σθ̇(t), draws (U3)
                               ▼
  metrics: closed-form propagation (U2) or Monte Carlo over draws (U3)
                               ▼
  PhaseSample.sigma, MetricSeries.sigma, gross-risk flag  →  cards (U0/U7)
```

### 4.1 Per-sample σθ and pGross: the calibrated table (U1)

The first version is empirical. It is cheap, it is honest by construction, and it gives every later stage something to propagate.

**Cells.** One cell per (tier × phase group). The phase groups are:

- address (Addr, and frames before the span);
- early backswing (onset to P2);
- backswing (P2 to Top − 2);
- top (±2);
- downswing (Top to Impact − 12);
- impact zone (±12 frames);
- through (to fin0);
- finish.

With face-on tiers BAND/SEG/RAY/WEDGE/RECON/PRED that is 48 cells. Many will be thin, so cells are pooled hierarchically: a cell with fewer than 30 marked frames from fewer than 5 swings borrows its phase group's pooled value, then its tier's.

**Residual.** r = circWrap(θ_track − θ_mark) per marked frame, using the published `samples` θ (after snap and anchor) at the marked frame's time.

**Estimates per cell:**

- `σθ` = 1.4826 × the median absolute deviation (MAD) of the residuals that are not gross. A residual counts as **gross** if |r| > max(15°, 4·σ_pooled), iterated twice.
- `pGross` = the fraction of gross residuals, with a Bayesian floor of (k + 0.5)/(n + 1), so an empty cell never claims zero risk.
- `bias` = the median of the non-gross residuals. It is reported (§6.5), not added to σ.

**Speed dependence.** Within the RAY, WEDGE and RECON cells, the residual spread grows with rotation rate. The marked downswing frames show this: the leading edge holds about 4.6° but the old tracker went from 6.4° to 15.9° above 16°/frame. So σθ for those tiers is fitted as σθ = a + b·|θ̇| (deg, deg/frame) by robust regression. That is two parameters per tier, not per cell.

**Truth sets for U1:**

| Set | Where | Size | Covers |
|---|---|---|---|
| Corpus hand marks | `truth.json` beside each swing (`/mnt/swingdata/corpus/swings/<session>/<swing>/truth.json`, schema `docs/reference/truth_json_schema.md`) | 32 swings with a marked P7; 283 marked frames used by the synth tuning | all phases, mostly P1–P8 instants |
| Unmarked 6-iron hand marks | `docs/research/data/markerless/unmarked_0909`, `hand_truth_scoring_20260909.md` | 7 swings, 42 marks | P1–P6 seen, P7–P10 inferred (score separately — memory "unmarked 6i truth convention") |
| Band-lock frames | traced runs, BAND tier | about 1,000 frames on the pinned corpus | BAND and SEG cells only, and *only as a consistency check* — not independent truth |

**Split.** Calibrate on half the swings and evaluate coverage on the other half, splitting by **swing**, never by frame. Rotate the halves, then publish the table from all swings. The split is recorded in the calibration report.

**Where the numbers live.** A new `tuned::uncertainty::` namespace in `pp_tuned_constants.h`. The table is about 6 tiers × (σ_base, a, b, pGross) plus the phase-group overrides that survive pooling. The generating report goes in `docs/research/data/uncertainty/`.

**Apply.** In `decideTrack` PASS 2 (placement, `shaft_track_assembly.cpp`), each sample gets `sigmaThetaDeg` and `pGross` from the table using its tier, phase and θ̇. Later stages that move θ adjust σ as follows:

- **Snap accepted:** σ unchanged. The snap is a registration, and its effect is inside the calibration because the residuals are measured on published θ.
- **Follow-through demotion:** the sample takes the PRED row.
- **Ball-anchor address paint:** a new **BALL** row, calibrated on the address marks of rewritten samples. Its σ is propagated from the ball and grip σ (§4.3) if that is larger.

**DTL.** The same machinery runs over `DtlSample` tiers (BAND, RAY; HELD gets its own row). The only DTL truth is the band template around address (guide §5.9). So the DTL table is calibrated there and **marked "address-calibrated"**. Outside the address band, DTL σ is the address value inflated by 1/ρ̂_D, because angular error scales inversely with projected length. That rule is a stated assumption, flagged until DTL hand marks exist.

### 4.2 Per-sample grip σ and length σ

- **Grip σ.** `σ_grip` (px) = ½·√(σ_leadHand² + σ_trailHand²) from the smoother's per-keypoint posterior (`PoseKpAux::sigma`) at the hand centroids. Where only raw pose exists, use the calibrated pose-jitter floor from `metric_presentation_honesty.md` §5.3. It is not stored per sample. Metrics recompute it from pose at the instants they read.
- **Length σ.** `lengths.fusedSigmaPx` already exists. When the length came from a lower rung of the ladder (guide §4.11.1), σ_L is set per rung: ball 7%, band 30%, segment 35%, pose surrogate 33% (its known short bias goes into the bias table), frame-height fallback 50%. These are the fusion's own fractions.
- **Club-length (metres) σ.** σ_Lm = 5 mm when the club record has a measured length. When `clubLengthM` is the 1.12 m default, σ_Lm = 0.12 m, which spans wedge to driver. That makes the speeds' σ honestly wide, and is preferable to refusing. Whether to refuse instead is open question Q1.

### 4.3 Ball σ

The address ball is a cluster median over at least 5 detections (`medianGripBallLenPx`). Its σ is the cluster's robust scatter divided by √n_eff (§5.2), floored at 1 px. The departure-found ball (`ImpactAnchorStage`) gets σ = 2 px from its corpus placement accuracy (median 1.9 px, 29/32 within 10 px). Both are stored on the track beside `addressBallPx` (new field `addressBallSigmaPx`).

### 4.4 Timing σ (U4)

Every instant a metric reads at gets a σ_t (µs):

| Instant | σ_t |
|---|---|
| Crossing positions (P2, P3, P5, P6, P8) | σ_t = σθ(t*) / \|dθ/dt(t*)\| (σφ for P3/P5), floored at frame period / √12, the quantisation of a sub-frame crossing. |
| Impact (geometric crossing adopted) | As a crossing, with θ_ball's σ (§4.3) added in quadrature to σθ. |
| Impact (acoustic trigger) | The trigger's measured scatter after its bias is removed. Corpus: bias −17.1 ms mean, residual scatter from the 13 truth swings. |
| P1 (address hold end) | The width of the still window: p1StillWindow × frame period / √12. |
| P4 (top) | The speed-minimum's half-width at 1.5 × the minimum speed. |

**The single impact instant.** U4 makes the ladder Impact event (geometry-corrected where applicable) the one instant for every impact reading:

- `buildShaftLeanSeries` stops taking `job.impactUs`;
- the speed mask, the peak lead and the kinematic sequence read the ladder Impact instead of the P7 knot;
- P7 itself is set to the same instant (it already is, when the geometry decides).

**This changes values** on swings where the instants differed. It is therefore a separate switch (`uncertainty.oneImpact`) with its own gate, and those value changes go to Mark as a list of moved swings.

### 4.5 The synthetic track's posterior (U3)

The evidence fit (`fitSynthToEvidence`, guide §7.1.5) is already a weighted least-squares problem M x = r over the free ticks of each stretch, with M symmetric positive definite and factored by Cholesky. Its posterior covariance is Σ = M⁻¹, under the model's own assumptions. Three changes turn it into an uncertainty:

1. **Soft anchors** (`uncertainty.synthSoftAnchors`, separate gate). Today the P-anchors are fixed nodes, so the posterior σ collapses to exactly zero at every P-position. That is the opposite of the truth: P7 is the least certain point of the swing. Instead, anchors enter as readings with σ:
   - the milestone fit's `sigmaThetaDeg` where it exists;
   - otherwise the U1 σθ of the sample nearest the anchor time;
   - plus the timing term |θ̇|·σ_t from U4.

   This **moves the curve** slightly: an anchor that disagrees with strong neighbouring evidence is no longer obeyed exactly. That is the one value change in the design, and it is gated against the 283 marks exactly as σ_a was tuned (guide §7.1.5 table). Acceptance requires P1–P4, P4–P7 and P7–P8 |median| error no worse than today, and the P-position θ readings on the 32 P7 swings no worse.
2. **Always fit.** A stretch with no evidence currently keeps the Hermite curve, with no posterior. With soft anchors plus the smoothness prior, M is positive definite even with zero readings, so every stretch is fitted. The value is the Hermite to within numerical noise when there is no evidence. The gate checks this, and the unit test pins it.
3. **Correlation correction.** The readings are not independent (principle 6). Each reading's weight is divided by an inflation factor κ = n_eff / n, calibrated so the posterior σ at marked ticks achieves the coverage of §6. Default κ = 1/3 until calibrated.

**Outputs:**

- **Per-tick σθ** = √Σ_ii, scaled to degrees. Stored on the synth samples as `sigmaThetaDeg`.
- **Per-tick σθ̇** from the covariance of neighbouring ticks: σ²(θ_{i+1} − θ_{i−1}) / (2h)² = (Σ_{i+1,i+1} + Σ_{i−1,i−1} − 2Σ_{i+1,i−1}) / (2h)².
- **Draws.** N draws per stretch, x = x̂ + L⁻ᵀ z with z ~ N(0, I) from the deterministic generator. Each draw gives a whole alternative synthetic track: rebuild θ̇ and head positions as `fitSynthToEvidence` does, then apply the same envelope clamp (Rule 2) so draws obey the same rules as the published curve. N = 200 by default (`uncertainty.mcDraws`). Draws are not persisted; they are regenerated on demand from the stored samples (§7).

Σ is the inverse of a banded matrix. Its diagonal and first off-diagonals come from the Cholesky factor by the standard selected-inverse recursion in O(n·b²), so the full inverse is never formed.

### 4.6 The forward–backward posterior (U5)

U1's table says how wrong *a typical RAY frame in the downswing* is. It cannot say that *this* frame was a near-tie between the shaft and the forearm. The Viterbi lattice can.

Viterbi minimises over paths. **Forward–backward** sums over them. Read the costs as negative log-likelihoods at a temperature T:

```
P(path) ∝ exp(−Cost(path) / T)
α_f(k) = E_f(k) − T·log Σ_j exp(−(α_{f−1}(j) + T_trans(j→k)) / T)      forward
β_f(k) = −T·log Σ_j exp(−(T_trans(k→j) + E_{f+1}(j) + β_{f+1}(j)) / T)   backward
p_f(k) ∝ exp(−(α_f(k) + β_f(k)) / T)                                     marginal of frame f
```

Use the same banded, sign-restricted transitions as `viterbiBanded`, with log-sum-exp for numerical safety. The cost is about twice the Viterbi's. From the marginal p_f over the 360 states:

- **σθ** = the circular standard deviation of p_f over the states within ±30° of the published θ, after snap and reconcile, shifted to the published θ.
- **pAlt** = the probability mass more than 15° from the published θ. This is the lattice's own **gross-error probability** for that frame: it is high exactly when another structure was nearly as cheap. It replaces U1's per-cell pGross where it is larger. The table remains a floor, because the lattice cannot know about errors its costs do not model.

T is a single global temperature, calibrated so that the U5 σθ achieves the coverage targets on the U1 truth sets. At T → 0 the marginal collapses onto the Viterbi path, and the unit test checks that the mode equals the Viterbi θ there.

**Gate.** U5 replaces U1's table (for in-span frames) only if, on the held-out halves, its coverage is at least as good *and* the correlation between per-frame σ and |residual| is higher. That is, it must rank frames by risk better than the table does. Otherwise U1 stays and U5 ships dark as a diagnostic, recorded in the trace.

Frames outside the span (flat emission rows) and frames whose θ was replaced after the DP (RECON with a wedge edge, BALL paint) keep the table σ.

DTL: the same forward–backward runs per band (`viterbiBanded` with the DTL bands). The DTL calibration caveat of §4.1 applies.

### 4.7 Fusion and the planes (U6)

- **Per-frame 3-D direction σ.** Propagate σθ_F and σθ_D through `fuseOne` by finite differences. Perturb each angle by ±σ, recompute u, and take the angular change; combine the two in quadrature. That gives σ_u (deg), which grows as 1/cond near the ill-conditioned frames. Stored on `Sample3D` as `sigmaDeg`. Draws are cheap here, so Monte Carlo is an equally good implementation. Use whichever matches the unit test more simply.
- **Plane inclination σ.** The current `swingPlane` σ is the plane's out-of-plane RMS. That is the *scatter of frames about the plane*, not the *uncertainty of the plane*. Replace it with a weighted bootstrap: resample the plane's input frames with replacement in blocks of 5 consecutive frames (the correlation correction), refit, and take the standard deviation of the inclination over 200 resamples. Add the camera term: the stated ≤ 1° inclination sensitivity over ±15° of yaw becomes a 0.5° floor in quadrature, until a calibration exists.
- **Address plane σ.** The median of the DTL view-plane inclinations: use the scaled MAD over √n_eff.
- **Face-on conic plane (ι, transition delta).** The same block bootstrap over the window's points, refitting the conic each time. A resample that becomes a needle (ratio < 0.26) counts toward a `pNeedle` reported beside σ.

## 5. Two shared mechanisms

### 5.1 Closed-form propagation (the delta method)

For a metric y = f(x₁, …, xₙ) of independent inputs with σ₁…σₙ, to first order:

```
σ_y² ≈ Σ (∂f/∂x_i)² σ_i²
```

It is used wherever f is smooth and the σ are small compared with the scale on which f bends. It is cheap, deterministic, and easy to unit-test against a finite difference. The per-metric formulas are in §6.

### 5.2 Effective sample size

A median or mean over n consecutive frames is not √n more certain when the frames' errors are correlated. With lag-1 autocorrelation ρ of the residuals:

```
n_eff = n · (1 − ρ) / (1 + ρ)
```

ρ is calibrated once per source from the truth sets' residual series (the U1 report), and stored in `tuned::uncertainty::`. It is used by the ball σ, the address-plane σ, the synth correlation factor κ, and any reducer that averages frames.

### 5.3 Monte Carlo over the synth draws

For metrics that are not smooth functions of a few inputs, U3's draws are pushed through the metric's own code:

- the arc vertex (low point);
- the attack angle with its continuity check;
- the peak-lead plateau rule;
- the kinematic-sequence node.

The metric function is called on each of the N draws, and σ is the robust standard deviation (1.4826 × MAD) of the N results. When more than 10% of draws make the metric refuse (vertex at the window edge, continuity failure), the metric's gross-risk flag is set and the fraction is reported.

Every metric function must already be a pure function of a `ShaftTrack2D` plus inputs, so the implementation needs a small adapter that takes a draw and returns the scalar. Each draw costs one metric evaluation, which is microseconds.

## 6. Per-metric budgets

The notation below uses these symbols:
- `σθ(t)` is the per-sample σ, interpolated at t: from U1, U5, or the U3 synth posterior when the metric reads synth.
- `θ̇` is the rotation rate.
- `σ_t` is the instant's timing σ (U4).

All angles are in degrees unless stated.

| Metric (guide §) | Budget | Mechanism | Calibration truth |
|---|---|---|---|
| **impactShaftLean** (8.1), not ball-anchored | σ² = σθ(t_imp)² + (θ̇·σ_t)² | delta | 32 marked P7s (`impact_lean/`) |
| **impactShaftLean**, ball-anchored | Lean = angle of (ball − grip) − 90°. σ² = (σ_grip² + σ_ball²)/\|b − g\|² (rad²) + (\|ġ⊥\|/\|b − g\| · σ_t)², where ġ⊥ is the grip velocity perpendicular to the ball line | delta | same; today's sd 5.3° is the target |
| Lean, Address→Impact Δ | √(σ_addr² + σ_imp²) — the two readings are separated in time, so independent | delta | — |
| **shaftLie** (8.2), each reading | The fold has derivative ±1, so σ = σθ_D(t) ⊕ θ̇_D·σ_t; Δ in quadrature. *Address-calibrated only* (§4.1). | delta | DTL band template (address) |
| **shaftAngleVsHorizontal** (8.3) | y = atan2(dy, \|dx\|) of (head − grip). σ² = (σ_head² + σ_grip²)/L² (rad²) + (ẏ·σ_t)², with σ_head from `headSigmaPx` | delta | top-of-swing marks (P4) |
| **attackAngle** (8.4), synth path | Monte Carlo over draws (the continuity check runs per draw) | MC | launch-monitor AoA, 13 paired swings |
| **attackAngle**, measured-head fallback | Delta over the ±2-sample centred difference, using `headSigmaPx` | delta | same |
| **lowPointAhead** (8.5) | MC over draws, ⊕ ball σ (§4.3) × mm/px, ⊕ ruler σ (the ball-diameter ruler's px σ × mm/px) | MC | LM low point where recorded; else the 0.02°/3.26° AoA argument as today |
| **clubheadSpeed** (8.6) | See the worked derivation below | delta, plus MC as cross-check | 6 LM pairs (08-18), plus the 13 AoA pairs where LM speed exists |
| **clubheadPeakLead** | MC over draws (the plateau rule per draw) | MC | none: **uncalibrated**, flagged |
| **handSpeed** | σ_v = √2·σ_grip / (h·√n_avg) through the existing 5-tap and 3-tap smoothing, × scale; plus the scale term | delta | none: **uncalibrated** |
| **lagAngle** (8.7) | σ² = σθ(t)² + σφ(t)², with σφ from the elbow and wrist pose σ over the forearm length: σφ² = (σ_elbow² + σ_wrist²)/\|w − e\|². **Also** the time offset to the nearest shaft sample × θ̇ (the metric takes the nearest sample with no gap limit). | delta | P5/P7 marks with pose |
| **swingPlane** (8.8) | Block bootstrap ⊕ 0.5° camera floor (§4.7) | bootstrap | none: **uncalibrated** |
| **transitionPlaneDelta**, ι | Block bootstrap; `pNeedle` | bootstrap | none |
| **clubAngularSpeed** (8.9) | Replace `segment_rates.cpp`'s σθ = 0.0087 rad / max(conf, 0.2) with the per-sample σθ (U1/U5, or the synth posterior). The existing propagation in segment_rates then does the rest. | existing | — |

**Clubhead speed, derived.** The composed speed is

```
speed = k · |v_g + L_px·θ̇·n|,     k = (L_m − 0.13) / L_px     (m/px)
```

Rewrite it with the head velocity in metres. The rotational term is k·L_px·θ̇ = (L_m − 0.13)·θ̇, so **the pixel length cancels from the dominant term**. Its uncertainty comes from the club's real length and the rotation rate, not from the image length. To first order, with ĉ the unit vector of the composed velocity:

```
σ_speed² ≈ ((L_m − 0.13)·(n·ĉ))² σθ̇²          (rotation rate)
         + (θ̇·(n·ĉ) + (v̂_g·ĉ)·|v_g|/L_px)² σ_Lm²   (club record — 0.12 m when defaulted; k scales the grip term too)
         + (k·σ_vg)²                            (grip velocity)
         + ((|v_g|·(v̂_g·ĉ))/L_px)² k² σ_Lpx²    (the pixel length, which only scales the grip term)
```

The value is in m/s, then mph. With a recorded club the σθ̇ term dominates near impact. With the default driver length the club term dominates: about ±12% of speed. That is the honest statement of guide §10.3's "a 7-iron reads ~22% fast".

### 6.4 Calibrated versus propagated

Each metric's σ carries a provenance tag, persisted beside it:

- **`calibrated`**: the budget was propagated *and* its coverage was checked on held-out truth for this metric.
- **`propagated`**: the budget was propagated from calibrated inputs, but this metric has no truth of its own. Peak lead, hand speed and the planes are in this group.
- **absent**: not characterised.

The card shows ± in both of the first two cases. The tooltip says which (U7).

### 6.5 The bias table

This lives in a new section of the calibration report, and in code as `tuned::uncertainty::bias*` constants. Each entry is recorded as a number, and corrected only under its own switch:

| Bias | Value | Source | Correct? |
|---|---|---|---|
| Clubhead speed vs launch monitor | 0.959 ± 0.022 | 6 LM pairs | no; reported |
| Launch monitor over-read | about +2 mph | memory, GC Quad | no; reported |
| Acoustic impact trigger | −17.1 ms mean (13–22 ms early) | 13 truth swings | already handled by geometry where the ball resolves; reported otherwise |
| Per-tier θ bias | U1 `bias` column | U1 | no; reported per tier and phase |
| E-pose length | about −33% | fusion header | not fused; reported |
| DTL lie absolute | camera-position dependent | guide §8.2 | Δ only |

## 7. Data model, persistence and versions

All additions are **additive**, absent or −1 by default, and serialised only when set. With the master switch off, documents are byte-identical.

| Type | New field | JSON (only when ≥ 0 or set) |
|---|---|---|
| `ShaftSample2D` | `float sigmaThetaDeg = -1`, `float pGross = -1` | `sigTheta`, `pGross` in `club.samples[]` and `club.synth[]` |
| `ShaftTrack2D` | `float addressBallSigmaPx = -1`, `float synthKappa = -1` | `addressBallSigma`, `synthKappa` |
| `ShaftPosition` | `float sigmaTUs = -1` (σθ/σL exist; B1 positions now fill σθ from the samples) | `sigmaTUs` |
| `PhaseSample` | `std::optional<double> sigma`, `uint8_t sigmaKind` (0 absent / 1 propagated / 2 calibrated), `float grossRisk = -1` | `sigma`, `sigmaKind`, `grossRisk` in the series phase samples |
| `MetricSeries` | `sigma` exists. Add `uint8_t sigmaKind`. Set `sigma` to the σ of the series' **headline** phase sample when the card is instant-only (guide: "the card decides whether the curve is drawn"), else the median per-frame σ. | `sigmaKind` |
| `DtlSample` | `double sigmaThetaDeg = NaN`, `double pGross = NaN` | `sigTheta`, `pGross` in `clubDtl.frames[]` |
| `fusion::Sample3D` | `double sigmaDeg = NaN` | `sigma` in `club3d.frames[]` |
| `fusion::PlaneFit` | `double inclSigmaDeg = NaN`, `double pNeedle` (conic) | `inclSigma` |

**Versions.**

- `kShaftStageVersion` 5 → 6 when U1 lands. Samples change meaning (they gain σ), and a reused v5 track has no σ. The alternative of re-deriving tier from `conf` is fragile: conf values are not unique to tiers once the ball anchor and demotion have run.
- `kDtlShaftStageVersion` 2 → 3 and `kShaftFusionStageVersion` 5 → 6 when U6 lands.
- The synth posterior is recomputed on reuse by `resynthesizeLayerC`, like the synth itself, so it needs no bump beyond U1's.
- **Fix in passing:** `resynthesizeLayerC` derives `isPred` from the dead `ShaftKinematicPredicted` flag (guide §10.5). U1 persists enough (σ, pGross) that the reuse path can re-derive "was PRED" from the U1 row instead. Do that, and add a test that live and reused synth are identical on the rich_7iron fixture.

**Switches** (`uncertainty.*`, a new section in `docs/developer/feature_switches_developer_guide.md`):

| Key | Default at merge | Stage |
|---|---|---|
| `uncertainty.enabled` | false (master; off ⇒ byte-identical) | all |
| `uncertainty.shaftTable` | true under master | U1 |
| `uncertainty.oneImpact` | false (value change, separate gate) | U4 |
| `uncertainty.synthPosterior` | true under master | U3 |
| `uncertainty.synthSoftAnchors` | false (value change, separate gate) | U3 |
| `uncertainty.mcDraws` | 200 | U3 |
| `uncertainty.seed` | fixed constant | U3 |
| `uncertainty.fbPosterior` | false (diagnostic until its gate passes) | U5 |
| `uncertainty.fbTemperature` | calibrated | U5 |
| `uncertainty.planeBootstrap` | true under master | U6 |

## 8. Stages

Each stage is buildable and gateable on its own. Per standing practice (memory "Build and test economy", "Commit only at the end", "Results before commit"):

- at most 2–3 builds per stage;
- only the affected test targets, run through `ctest`, once at the end;
- multi-swing runs on GOLFSIMPC Release, with run trees on the share (`/mnt/swingdata/scratch/`), cleaned up once the CSVs are in the repo;
- **no commits until Mark has judged the graded results**.

| Stage | Content | Touches | Gate |
|---|---|---|---|
| **U0** | Data model and plumbing: the §7 fields, JSON read/write, `PhaseSample.sigma` through the QML bridge, `ChartMetrics` preferring the headline instant's σ, the deterministic RNG utility with its unit test. No producer sets anything yet. | `swing_analysis.h`, `dtl_shaft_track.h`, `shaft_fusion.h`, `swing_doc.cpp`, `recorded_products.cpp`, `dtl_shaft_json.*`, `shaft_fusion_json.h`, `chart_metrics.*`, `shot_processor.cpp` (detail bridge), new `src/Analysis/det_rng.h` | 61 pinned swings byte-identical; `swing_doc_test` round-trips the new fields |
| **U1** | Calibration harness and per-sample table. New `tools/shaftlab/uncertainty/calibrate_sigma.py` reads traced runs plus `truth.json` and writes the cell table, ρ, the bias table and the coverage report. Then apply the table in PASS 2, the BALL row and DTL. Version bump to 6. Reuse `isPred` fix. | `shaft_track_assembly.cpp`, `ball_anchor.cpp`, `dtl_shaft_post.cpp`, `pp_tuned_constants.h`, `analysis_versions.h` | Coverage of per-sample σθ on held-out halves within 60–76% at 1σ and 90–98% at 2σ, per phase group with ≥ 30 frames; σ off ⇒ byte-identical |
| **U2** | Closed-form metrics: lean (both forms), lie, top angle, lag, hand speed, clubAngularSpeed's σθ input, the Δ readings. | `wrist_analyzer.cpp` (lean), `dtl_shaft_lie.h`, `club_delivery.cpp`, `kinematic_series.cpp`, `segment_rates.cpp` | Per-metric coverage on its truth (§6 table); **every metric value byte-identical** with σ on vs off |
| **U3** | Synth posterior: selected inverse, κ, draws, MC adapters for attack angle, low point, peak lead and the speed cross-check. Then, separately, soft anchors. | `shaft_synthesis.h`, `shaft_track_assembly.cpp` (`synthesizeLayerC`), `club_delivery.cpp`, `kinematic_series.cpp` | Posterior σθ coverage on the 283 marks; speed σ vs LM residuals; values unchanged with soft anchors off. **Soft anchors:** §4.5 accuracy criteria, plus a list of moved swings for Mark |
| **U4** | Timing σ for every instant; the one-impact-instant change behind its switch; timing terms folded into U2/U3 budgets. | `shaft_positions.h`, `shaft_track_assembly.cpp`, `impact_geom.h`, `wrist_analyzer.cpp`, `kinematic_series.cpp`, `segment_rates.cpp` | σ_t coverage on marked P-times; **oneImpact**: moved-swing list, no truth-swing impact reading worse |
| **U5** | Forward–backward posterior over the face-on and DTL lattices: σθ and pAlt; temperature calibration; σ/residual rank correlation. | `shaft_track_assembly.cpp`, `shaft_track_shared.h` (new `forwardBackwardBanded` beside `viterbiBanded`), `dtl_shaft_decide.cpp` | Replaces U1 in-span only if coverage ≥ U1's *and* rank correlation is higher; else dark as a trace diagnostic |
| **U6** | Fusion direction σ, plane block bootstrap, address-plane σ, conic bootstrap and pNeedle. Version bumps. | `shaft_fusion.h`, `shaft_plane.h`, `wrist_analyzer.cpp` | Unit tests against finite differences; values unchanged |
| **U7** *(optional)* | Presentation: the ± chip shows calibrated/propagated provenance, a gross-risk ⚠ on cards whose readings have pGross > 0.2, and optional per-frame σ shading. | QML (`PpChartSummary.qml`, `PpChartPlot.qml`), `chart_metrics.*` | `--probe-qml` checks (memory: verify UI by probing, never screencapture) |

**Tests to add** (in `src/Analysis/tests/`):

- `uncertainty_delta_test`: every §6 closed form against a central finite difference of the metric function.
- `det_rng_test`: fixed seed gives a fixed sequence, identical across platforms; Box–Muller mean and variance.
- `synth_posterior_test`: on a stretch with no readings and soft anchors, the posterior mean equals the Hermite within 1e-9. On a synthetic stretch with known σ, the posterior σ matches the analytic value. The selected inverse matches the dense inverse on small cases.
- `forward_backward_test`: as T → 0 the marginal's mode equals `viterbiBanded`'s path. On a two-path toy lattice, pAlt matches the closed form.
- `plane_bootstrap_test`: deterministic σ for a fixed seed; σ shrinks as √(1/n_eff) on synthetic planar data.
- Extend `swing_doc_test` and the DTL and fusion JSON tests for the new fields.

## 9. What this does not decide, and the questions for Mark

1. **Q1: the default club length.** With no club recorded, should speed and low point show a wide σ (±12% from σ_Lm = 0.12 m), or refuse ("–")? The design assumes the wide σ, with the provenance tooltip saying why.
2. **Q2: soft anchors.** They make the synthetic track honest at P7 but move the curve. Accept a moved-swing list as the decision basis?
3. **Q3: gross risk.** At what pGross should a card show ⚠ (the design proposes 0.2)? Should anything be *refused* on pGross alone, or does refusal stay with the witness checks?
4. **Q4: one impact instant.** The ladder Impact for everything, accepting the value moves on lean and the speed mask?
5. **Q5: scoring.** A follow-up could let faults fire only when |value − norm| > k·σ, or weight resemblance by σ. Out of scope here. Should it be the next design?
6. **Q6: DTL σ beyond address.** It rests on an assumed 1/ρ̂_D inflation until DTL hand marks exist. Acceptable to ship as `propagated` (not `calibrated`) on that basis?

## 10. Risks

- **Overfitting the calibration to one golfer.** The swing-wise split guards against leakage *within* the corpus, not against a second golfer. Every σ table is labelled with the corpus it came from. The tables must be re-run when a second golfer or a left-handed capture exists (guide §10.6).
- **Coverage looks right on average and wrong in the tail.** Coverage is checked per phase group and per metric, and the gross-error fraction is reported separately, so a good 68% cannot hide a 10% gross tail.
- **σ that is too small is worse than none.** If a stage cannot reach the coverage target, its σ ships as `propagated` with an inflation factor fitted to reach 68% (and the factor reported), or not at all. It never ships as a narrow, uncalibrated number.
- **Cost.** Forward–backward roughly doubles the solve (milliseconds). The selected inverse and 200 draws per stretch are tens of milliseconds per swing. The plane bootstrap with 200 refits of a 3×3 eigenproblem is negligible. Measure each stage's added wall time in the gate report; the budget is +10% of the shaft stage.
- **Byte-identity gates and floating point.** σ fields are computed after the values and never fed back, except soft anchors and oneImpact. Any accidental change to a value shows up in the pinned-corpus diff and blocks the stage.

## 11. Definition of done

- U0–U4 and U6 implemented, gated and judged by Mark. U5 either replaces U1 by its gate or ships dark as a diagnostic. U7 is optional.
- Every shaft metric in guide §8 publishes per-instant σ with a provenance tag, or is listed as absent with a reason.
- The calibration report in `docs/research/data/uncertainty/` shows held-out coverage per metric and per phase group, the gross-error fractions, ρ, κ, T and the bias table.
- The pinned corpus is byte-identical with `uncertainty.enabled = false`. With it on, values are identical except under `synthSoftAnchors` and `oneImpact`, whose moved-swing lists Mark has accepted.
- `feature_switches_developer_guide.md` has the `uncertainty.*` section. `shaft_tracker_developer_guide.md` §8 and §10.4 are updated to describe the published σ, and §10.4's item is closed or narrowed to what remains.
- Version bumps applied, and the corpus library re-analysed under the new versions (studio, memory "version-gated re-analysis").
