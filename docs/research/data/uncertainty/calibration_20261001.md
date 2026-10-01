# Shaft uncertainty: calibration and gate verdicts, 1 October 2026

Implements `docs/design/shaft_uncertainty_propagation_design.md`, stages U0–U7. The work was delivered in one session, and every switch was decided by a gate fixed in advance.

The sweeps ran on GOLFSIMPC (Release `swinglab_run`, patch branch, no push) over 90 swings:

- the 68 pinned-pose swings in `corpus/pose3`;
- 22 truth-carrying swings with pinned poses in `scratch/uncert/pose_extra`;
- for 34 of them, the down-the-line view as well.

Truth comes from three sources:

- 996 face-on hand marks on 58 swings (`truth.json`);
- 32 marked P7s;
- the DTL band-template truth in `corpus/dtl_heldout_truth`.

Tools: `tools/shaftlab/uncertainty/` (`sweep.py`, `calibrate_sigma.py`, `grade_coverage.py`, `shaftcal.py`). The generated tables are in this folder:

- `sigma_table.csv`, `bias_table.csv`, `rho.csv`, `dtl_sigma.csv`;
- `coverage_U1.md`;
- `coverage_metrics.csv` and `gates_20261001.md`.

## Verdicts

| Gate (rule fixed in the plan) | Result | Evidence |
|---|---|---|
| `uncertainty.enabled=0` is byte-identical to 27e1e96d | **PASS** | 90 / 90 swings. Version stamps and wall-clock timing keys (`timings/*`, `*Ms`) are excluded; the timing keys differ between two runs of the same binary. |
| σ on, with the value-changing switches off, is value-identical | **PASS** | 90 / 90, after `uncertainty.sequenceSigma` was split out (below). |
| Per-sample σθ coverage, held out | **PASS** | 76 % within ±1σ and 93 % within ±2σ over all groups (pooled A/B rotations); 79 % / 96 % as published. |
| Synth posterior coverage | **PASS** after a scale | As first built, the posterior was 2.5× too wide (95 % / 100 %). With `kSynthSigmaScale` = 0.40 it is 78 % / 94 % held out, and 77 % / 92 % as run in the confirmation sweep. |
| Soft anchors (U3) ON | **FAIL → dark** | Synth \|median\| error is worse in P1–P4 (0.76° → 0.79°) and P4–P7 (1.85° → 1.95°), and better in P7–P8 (4.99° → 4.81°). P7 θ is unchanged. 247 readings moved on 90 swings. |
| One impact instant (U4) ON | **FAIL → dark** | 10 of 32 truth swings read their lean worse by more than 0.05°, while 9 read it better. The median \|error\| improves, 1.99° → 1.84°, but the rule is "no swing worse". |
| Forward–backward (U5) replaces the table | **FAIL → dark** | The best temperature (T = 2) gives held-out ±2σ of 76–80 %, against the table's 96 %. Spearman(σ, \|r\|) is 0.59 against the table's 0.67. It remains a trace diagnostic. |
| Lean σ against the 32 P7 marks | **PASS → calibrated** | 63 % / 90 % on 30 readings. Bias +0.73° (reported separately); median \|error\| 1.64°; median σ 2.34°. The two gross readings are listed below. |
| Attack angle, clubhead speed against the GC Quad | not gated (n = 6) | They ship `propagated`. Speed covers poorly on the one LM session in the set (below). |
| Cost ≤ +10 % | **PASS** | Shaft stage +3.4 % (median over 90 swings, confirmation sweep); whole analysis +0.8 %. |

## The per-sample table (U1)

Cells are tier × phase group. A cell with fewer than 30 frames or 5 swings is pooled:

1. first to the same tier within the same half of the swing (pre-impact: address to downswing; post: impact to finish);
2. then to the whole tier;
3. then to the design prior.

**σ by quantile, not MAD.** The first pass used σ = 1.4826·MAD, which covered ±1σ but only 75–80 % of the downswing within ±2σ. The residuals' tails are heavier than a Gaussian's. σ is therefore max(q68 |r|, q93 |r| / 2), taken about zero rather than the median, so the uncorrected bias sits inside σ.

**Group inflation.** The gate rule then widened three columns until their pooled held-out coverage reached 60 % at ±1σ and 90 % at ±2σ:

| Group | Factor |
|---|---:|
| Top | 1.55 |
| Downswing | 1.40 |
| Impact | 1.05 |

Some groups run conservative at ±1σ: early backswing 90 %, through 81 %, finish 78 %. This comes from the heavy tails and from a 0.5° floor, which is the precision of a hand mark.

**Gross errors** are |r| > max(15°, 4σ), iterated twice. pGross is (k + ½)/(n + 1).

Notable cells:

- **PRED, pre-impact:** σ 4.9° with a 20 % gross rate. 8 of the 41 address and early-backswing PRED frames were off by more than 15°.
- **PRED, finish:** σ 48°. The prediction there is not a measurement.

**Lag-1 ρ** of consecutive marked residuals is 0.84 overall and 0.36 for BAND, so `kRho` = 0.84.

**The slope term** (σ growing with |θ̇|) is estimated and reported but not applied. The phase groups already carry the speed, and the slope fitted on the pooled tier double-counted it.

**DTL.** RAY σ is 0.50° (bias +0.25°, 425 frames). BAND and HELD have no matched truth frames, so they keep their priors (1.0°, 4.0°). Every RAY truth frame has ρ̂ ≥ 0.9, so the 1/ρ̂ inflation beyond address is untested. Those samples are tagged propagated.

## The synth posterior (U3)

The κ weight inflation scales only the evidence share of the posterior. The anchors' σ, spread by the bracket weights, does not move with κ, and at P-positions it dominates. Fitting κ alone would have needed κ = 4.2, which is meaningless. `SynthPosterior::scale` is therefore one overall factor on σ and on every Monte Carlo draw's deviation, fitted on held-out halves: A 0.40, B 0.45, shipped 0.40.

## Metric budgets

| Metric | n with truth | ±1σ / ±2σ | Bias | Tag |
|---|---:|---|---:|---|
| `impactShaftLean` (P7 marks) | 30 (+2 gross) | 63 % / 90 % | +0.73° | **calibrated** |
| `attackAngle` (GC Quad) | 6 | 100 % / 100 % | +6.5° | propagated |
| `clubheadSpeed` (GC Quad, scale removed) | 5 (+1 gross) | 20 % / 80 % | ×0.907 | propagated |
| everything else | no independent truth | — | — | propagated |

**Lean gross readings: the gross risk missed both.** 06-11 swing 9 (+15.1°) and 07-03 swing 9 (+18.2°) both carried grossRisk 0, so the ⚠ would not have fired on either.

**Speed on 08-18 Wrist_02** (the only LM session among the 90 swings). The readings run 0.63–1.19 of the GC Quad, far wider than the library's 0.959 ± 0.022. σ does grow on the worst readings (10.5 and 13.6 mph at 0.66 and 0.63 of LM), but it does not cover them. This is open: either the speed estimate on this pinned-pose run or this session's tracking needs a look before the speed σ can be called calibrated.

**Two defects found by the sweep and fixed:**

- **Attack angle** published σ = 0 on 18 swings, every one an implausible reading (−33° to −86°) taken from the measured-head estimator. The synth draws never touch that estimator. It now publishes no σ there: absent means not characterised.
- **Speed-peak timing** read ±0 on 13 swings, because the peak lands on a sample. Its σ is now floored at period/√12.

## The ⚠ (U7)

A card shows ⚠ when its headline reading's grossRisk exceeds 0.2. The headline is the Impact sample when there is one.

The first version took the worst over all phases. That flagged 38 of 90 speed, hand-speed and lag cards for their *address* reading, where the track coasts as PRED with a 20 % calibrated gross rate. With the headline rule, no speed card is flagged.

The UI was probed with `--probe-qml` on the Mac Debug build, which showed:

- the provenance tooltips: calibrated, propagated, and "the club length was assumed";
- the ⚠ on an injected 35 % risk;
- no ⚠ from a risky address reading.

## For Mark: decisions and lists

1. **`uncertainty.sequenceSigma`** feeds the per-sample σθ into the kinematic sequence's club node, replacing 0.5°/conf. It widens the node's σ, which un-places the club node and changes the order or verdict on some swings. It is dark: is the wider, calibrated σ the honest one to ship? It moves 3 of 90 swings. 07-03 s4 and 07-08 s8 lose the club node (order leadArm → club becomes leadArm alone). 09-09 s1 goes from partial to unresolved.
2. **Soft anchors** move 247 readings on 90 swings and fail the region gate by small margins: +0.02° in P1–P4 and +0.10° in P4–P7, against −0.18° in P7–P8.
3. **One impact instant** improves the median lean error but worsens 10 of 32 swings: 07-03 s3, s5, s7, s8, s9, s11; 07-08 s1; 09-09 s3, s6, s7. The largest change is 09-09 s6 (−4.5° → −7.0°).
4. **Not in scope, found on the way.** `dtl_shaft_post_test` C4 fails on main, independent of this work. The HELD hole-fill re-publishes a frame that the late-escape rule refused: "held: 1-frame hole between measured frames (was: corridor escape after P8 — not published)". A frame judged not to be a club comes back as HELD.
