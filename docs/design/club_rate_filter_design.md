# Club rate filter — one estimate of the shaft angle and its rate

**Status:** ON by default at σ_jerk 6000 since 24 October 2026 (Mark, after the §7 gate and the off/on
angular-speed plots); the library re-analysed with it.
**Owner:** Mark. **Started:** 24 October 2026.
**Code:** `src/Club/club_rate_filter.{h,cpp}`, `src/Core/kf3_smoother.h`, wired in
`src/Club/shaft_track_assembly.cpp` (`applyClubRateFilter`), `src/Club/shaft_synthesis.h` (`synthDraws`),
`src/Kinematics/segment_rates.cpp` (the KS club channel). Test: `club_rate_filter_test`.
**Gate tool:** `tools/swinglab/club_filter_gate.py`. Data: `docs/research/data/club_filter/`.

---

## 1. The problem

On the KS chart the club angular speed wobbled while clubhead speed looked smooth. The two read
**different rates of the same face-on shaft angle θ**:

| Series | Rate it read |
|---|---|
| `clubheadSpeed` (`kinematic_series.cpp`) | the synth's `thetaDot`: the Hermite anchor-curve rate, flat at the P6→P7 mean from P6 to impact (the 3 Oct jitter fix kept this rate and set `synth.fitRate` off) |
| `clubAngularSpeed` (`segment_rates.cpp`) | the derivative of the synth θ **fitted to the frames**, de-projected through the swing plane |

Measured on the stored library (24 Oct, 261 swings with a club series): the KS club ω had two or
more humps in the last 250 ms on 91 % of swings; it tracked the image dθ/dt at r = 0.98, so the plane
de-projection was not the cause. Clubhead speed looked right partly by construction — its flat P6→P7
rate carries no timing — and read a median 0.837 of the GC Quad at impact (77 pairs, SD 13.2 mph).

Copying the anchor-curve rate into the KS series would have made the two charts agree and put every
club peak at the P6 knot: a false "peaked early" by construction. So neither rate was kept.

## 2. The estimator

A robust Kalman filter with an RTS backward pass over θ(t), state **[θ, θ̇, θ̈]**, white-jerk process
noise, variable dt — the same `Kf3` the pose smoother runs, moved to `src/Core/kf3_smoother.h`.

**Observations** — every independent reading the tracker made:
- each measured frame (flags Measured / Wedge / ImuBridged, not Coasted, Implausible, Predicted or
  Synthesized) at its own σθ from the uncertainty pass (`sigmaThetaDeg`; `sigmaUnsetDeg` where none
  was assessed; never below `sigmaFloorDeg`);
- each blur-wedge edge (`wedgeObs`: trail, mid, lead) at its own sub-frame time and σ, in place of
  the frame's wedge sample, as the synth fit does;
- the ball line at P7 (hands → address ball), σ `ballSigmaDeg`, when the ball anchored the track.

The P-position anchors are **not** observations: they are located on the same frames, and reading them
again would count those frames twice.

**Segments.** Top (P4) → impact (P7), reading evidence from `leadInUs` before the top, θ̇ = 0 ± `initRateDps`
at the start; and impact → the end of the synth, θ̇ free (± `initRatePostDps`). Two segments because the
collision steps the rate; this matches the clubhead-speed mask past the P7 knot. The backswing keeps the
existing synth. No P4 or no P7 ⇒ the filter does not run.

**Robustness** (the KB-18 / #22 leg-lock and wrong-branch frames). Pass 1 gates innovations at
`gateSigma`. Each of `robustIters` further passes inflates every reading's R by its Huber weight on the
previous pass's smoothed residual (threshold `robustK` σ), with no gate. A stall through impact is
gated as soon as its innovation leaves the predicted motion; a single wrong-branch frame likewise.

**Process noise.** σ_jerk = `jerkDps3`. By the pose smoother's law the effective window is
T = 1/ω_c with ω_c = (σ_jerk² / (σ_m²·dt))^(1/6): at σ_m ≈ 2° and 240 fps, 12 000 °/s³ gives ω_c ≈ 45
rad/s (≈ 22 ms). The tracker's 60–80 ms wobble is attenuated about 100×; a club's own rise over ~250 ms
passes. The gate sweeps it (§7).

## 3. Who reads it

Where the filter ran, the synth ticks carry its θ and θ̇ and the flag `ShaftRateFiltered` (0x800):

- **clubhead speed, hand speed, clubhead peak lead, lag** read the synth already — no code change;
- **the KS club angular speed** reads the tick's θ̇ directly, de-projected by the plane gain dψ/dθ at
  the tick's image angle, its σ the filter's σθ̇ times the same gain — not differentiated or windowed
  again. The club node's withhold and its σ widening are unchanged (Mark decides on verdicts from §7);
- **the uncertainty draws** (`synthDraws`) take θ and θ̇ on filtered ticks from a backward sample of the
  filter's posterior (forward-filter backward-sample), so the σ of clubhead speed, peak lead, attack
  angle and low point is the filter's own;
- **Rule 2** (the envelope clamp) skips filtered ticks: clamping onto the frames the filter set aside
  would restore the wobble.

The synth is rebuilt on every load (`resynthesizeLayerC`), so this is a synth-only change:
`kShaftStageVersion` is not bumped. Stored metrics change only when a swing is re-analysed.

## 4. The diagnostic (developer only)

How far the frames sat off the filtered track **is** the tracker's wobble — a measurement-error marker.
Persisted per swing as `analysis.club.filter` (only when the filter ran), shown nowhere in the app:

| Field | Meaning |
|---|---|
| `nEvidence` | readings used, both segments |
| `nGated` | thrown out by pass 1's gate |
| `nDownweighted` | R inflated on the final pass |
| `nisMean` | mean normalised innovation, final pass (≈ 1 when σθ is honest) |
| `residualRmsDeg` | RMS of frame − filtered θ, downswing |
| `rateWobble` | RMS of (raw dθ/dt − filtered θ̇) / max(\|θ̇\|, 200 °/s), downswing |
| `humpsRaw` / `humpsFiltered` | humps in the raw and filtered rate, last 250 ms (`countHumps`: maxima above 40 % of the top with 10 % prominence; the window's ends count) |

Whether it really marks error is measured, not assumed: §7 reports its rank correlation with the
launch-monitor error.

## 5. Tunables (`tuned::shaft::clubFilter`, keys `synth.clubFilter.*`)

`enabled` (true) · `jerkDps3` (6000) · `sigmaFloorDeg` (1) · `sigmaUnsetDeg` (3) · `gateSigma` (3) ·
`robustK` (2) · `robustIters` (2) · `ballSigmaDeg` (8) · `leadInUs` (40000) · `initRateDps` (600) ·
`initRatePostDps` (2500).

## 6. The gate

Measured by `club_filter_gate.py` on the cabin PC, same binary, `swinglab_run --out` with the filter
off and on. Tuned only on the **tuning set** (10-05 W01, Jess W01–W03); reported separately on the
**held-out set** (08-18 W02, Mark 10-20 W01, Nick W02–W03).

| Check | Pass |
|---|---|
| Impact clubhead speed vs GC Quad, SD in mph (held-out) | lower than off |
| Median \|1 − ratio\| (held-out and all pairs) | no worse |
| Swings below 0.7 of the launch monitor | fewer |
| Club ω with ≥ 2 humps in the last 250 ms | ≤ 25 % |
| Correlation of club ω with clubhead speed through the downswing | ≥ 0.99 |
| Attack angle, low point, lag, P-positions | diffs reported; P-positions identical |
| Wobble statistic vs \|1 − ratio\| | Spearman reported |

The overall pixels-to-metres scale bias (#51, camera calibration) is not this filter's to fix; it is
reported, not chased.

## 7. Results (24 October 2026, cabin PC, patched build over 1c5d6321)

**Method.** `swinglab_run --out` over the 77 GC Quad pairs (74 with a club track). The off arm posed
each swing; every other arm read that pose (`--pose`, `sweep_pose.cmd`), so arms differ only by the
filter's keys. Control: 10 swings re-run off with the pinned pose were identical to the off arm on every
metric. σ_jerk was chosen on the tuning set alone (6000 / 12000 / 25000 °/s³) and then run on the
held-out set. Data: `docs/research/data/club_filter/*_20261024.*`.

**Clubhead speed at impact against the GC Quad (ratio ours / LM):**

| | n | median ratio | median \|1 − ratio\| | bias mph | SD mph | < 0.7 |
|---|---:|---:|---:|---:|---:|---:|
| off — held-out | 30 | 0.818 | 0.183 | −12.6 | 14.8 | 5 |
| **σ_jerk 6000 — held-out** | 30 | **0.948** | **0.085** | **−2.0** | **7.9** | **0** |
| off — all | 74 | 0.832 | 0.175 | −11.7 | 10.9 | 6 |
| σ_jerk 6000 — all | 74 | 0.988 | 0.070 | −0.4 | 6.4 | 0 |
| σ_jerk 12000 — all | 74 | 0.897 | 0.114 | −8.0 | 7.3 | 0 |
| σ_jerk 25000 — tuning only | 44 | 0.761 | 0.239 | −20.1 | 6.8 | 16 |

The impact reading depends strongly on σ_jerk: the stiffer the filter, the more of the downswing's rate
it carries into impact past the frames that stall on the legs. The low reading this project blamed on the
pixel scale (#51) was mostly that stall: with the filter at 6000 the median bias is −0.4 mph.

**The club's rate through the downswing (74 pairs):**

| | off | σ_jerk 6000 |
|---|---:|---:|
| KS club ω with ≥ 2 humps in the last 250 ms | 66 % | 9 % |
| correlation of KS club ω with clubhead speed, last 250 ms (median) | 0.898 | 0.980 |
| KS club ω peak before impact (median) | 46 ms | 28 ms |

On the stored library (261 swings, same hump count) the figure was 67 %. With the filter on, the filtered
IMAGE rate and clubhead speed peak together, a median 3 ms before impact (12000 run). The KS series still
peaks earlier because it is de-projected through the swing plane: the plane gain dψ/dθ is a median 1.08
at 45 ms before impact and 0.94 at impact (shaft near the ellipse's major axis vs its minor), so the
in-plane rate is lifted mid-downswing and lowered at the ball. Clubhead speed is not de-projected. Which
shape is right is a geometry question (§8).

**No-regression (off → 6000, 74 swings).** P-position times identical on 77 of 77. Attack angle against
the GC Quad: median |error| 12.9° → 9.7°; 5 swings worse by more than 5° (four in 10-05). The large
movers were mostly repairs: e.g. Mark 10-20 s17 38 → 80 mph (LM 76), attack +89° → +1° (LM −4°);
Nick W03 s7 47 → 72 mph (LM 81), attack +87° → −2° (LM −2°); Nick W03 s8 116 → 70 mph (LM 81). Lag moves
by a median 7.6° (max 39°) and has no truth to grade it.

**The diagnostic.** Spearman of the per-swing statistics with |1 − ratio|: `rateWobble` +0.23 and
`nisMean` +0.23 against the OFF run's error, ≈ +0.1 against the filtered run's. The wobble marks the
error the unfiltered speed carries, weakly; once filtered, little of it is left to mark.

## 8. Open

- σ_jerk is 6000 (the tuning-set choice; passes held-out). Plotted off against on, the filtered image rate
  through the downswing is close to a steady ramp from P4 to impact at this setting: the curve's SHAPE
  between the top and the ball is largely the filter's, so the club peak's timing is still not measured
  (the KS club verdicts stay withheld). Impact speed is the gated reading.
- De-projection: the KS club series is in-plane, clubhead speed is image-plane. Either de-project
  clubhead speed through the same plane (then both read in-plane and both peak earlier), or accept the
  difference as the plane's geometry. The plane rests on uncalibrated cameras.
- Lag changes with the rate it reads and has no criterion.
