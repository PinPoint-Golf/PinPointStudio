# Variability marks: calibration pass R28 (Session 5)

**7 Nov 2026. Proposals only.** No constant, descriptor or norm in `src/` was changed. Each section gives the evidence, one recommendation, and what changes if it is adopted.

## What was run

- **Data:** the 7 Nov read-only extract of all five libraries (`/mnt/swingdata/scratch/variability/fixtures`, `ks`): 23 sessions, 284 swings. It answered every question; no new cabin-PC extract was needed.
- **Grader:** the tree with R3 (observed spread), R22 (robust z) and R26 (no CV fallback). The run from before R26 is kept as `baseline/` for question 6.
- **Tool:** `variability_calibrate` (test tool). It now also writes `typicality_scalar.csv`, `typicality_trace.csv` (leave-one-out, R8/R22) and `trace_descriptors.csv`, and samples traces as the Charts presenter does (one-sample end tolerance; P-anchor or landmark routes only). Graded trace windows went from 282 to 307.
- **Run trees:** `/mnt/swingdata/scratch/variability/s5-calibration/{baseline,current}`; analysis scripts in `…/analysis/` (`q1c.py`, `q2.py`, `corridors.py`).
- **Window:** one session × club × trace, n ≥ 8.

## 1. Trace corridors (proposal 5)

Charts' session rows grade every preset member that draws a curve on 3 or more shots: 36 TimeSeries keys. Under R16 none is graded today.

**Rule used.** A trace takes as its corridor the key's own measure, matching its unit, read inside P4–P7: the one nearest impact; at the same phase an `at` reading before a `delta`, a `delta` before an `extremum` (an extremum counts at its window's end). Half-width (σLo+σHi)/2 for the club's context (R12). The median of all in-window measures gives the same grades to within one or two windows.

| Trace | Corridor measure (phase, half-width) | Windows | Proposed 5/4/3/2/1 | Today (fallbacks) |
|---|---|---|---|---|
| headLift | m_headLiftImpact (Δ P1→P7, 3 cm) | 12 | 6/6/0/0/0 | 0/0/1/0/11 |
| headRise | m_headRiseBackDtl (Δ P1→P4, 3 cm) | 7 | 0/6/1/0/0 | 0/0/0/0/7 |
| headSway | m_headSwayBack (Δ P1→P4, 3 cm) | 12 | 0/9/3/0/0 | 0/0/0/0/12 |
| lagAngle | m_lagAngleDown (min P5–P6, 26°) | 10 | 9/0/1/0/0 | 0/1/0/2/7 |
| leadHeelLift | m_leadHeelLiftTop (P4, 2 cm) | 11 | 1/7/3/0/0 | 0/0/0/0/11 |
| leadKneeFlexion | m_leadKneeFlex_p7 (Δ P1→P7, 6°) | 11 | 1/9/1/0/0 | 0/0/0/4/7 |
| pelvisRotation | m_pelvisRotP7 (P7, 10°) | 11 | 4/7/0/0/0 | 0/1/1/2/7 |
| pelvisRotationSigned | m_pelvisRotP7 (borrowed) | 11 | 4/7/0/0/0 | 0/1/1/2/7 |
| pelvisThrust | m_pelvisThrustImpact (Δ P1→P7, 2.5 cm) | 7 | 0/5/2/0/0 | 0/0/0/1/6 |
| spineForwardBend | m_spineBendChangeImpact (Δ P1→P7, 3.75°) | 8 | 3/4/1/0/0 | 1/1/1/3/2 |
| thoraxRotation | m_thoraxRotP4 (P4, 12°) | 11 | 1/0/6/3/1 | 0/0/0/1/10 |
| thoraxRotationSigned | m_thoraxRotP4 (borrowed) | 11 | 1/0/6/3/1 | 0/0/0/1/10 |
| trailKneeFlexion | m_trailKneeFlex_p7 (Δ P1→P7, 8°) | 11 | 6/5/0/0/0 | 0/0/7/2/2 |
| trailWristFlexExt | m_trailWristFlexExt_p5 (Δ P1→P5, 12°) | 12 | 3/1/1/1/6 | 0/0/0/1/11 |
| clubheadSpeed | m_clubheadSpeedImpact (P7, iron 3.75 / wedge 3.4 / driver 4.65 mph) | 4 | 0/1/1/2/0 | 1/3/0/0/0 |
| **Total** | | **149** | **39/67/26/9/8** (26/45/17/6/5 %) | 2/7/11/19/110 (74 % Scattered) |

- With a corridor but no library window of 8 or more: hipLineTilt, leadKneeDrift, pelvisLift, pelvisSway, spineSideBend, thoraxLateralDrift, trunkRise (held), hm.leadWristFlexExt, hm.leadWristRadUln.
- No corridor exists (12 traces): the four angular speeds, handSpeed, handPlaneCurve, swingPlaneCurve, headTilt, shoulderPlaneAngle, leadUpperArmToChest (its measure reads P1–P3), plumbBobDistance (its P7 measures carry no norm), hm.forearmRotation (its only norm is the ±180° "monitored" row).
- For comparison, swing scalars against corridors read 32/31/14/7/15 %.
- Examples, Mark 7 Oct 7-iron, n 48: lagAngle 3.4° against 26° → 0.13 Grooved (today Scattered on ρₜ 1.25); pelvisRotation 4.5°/10° → 0.45 Repeatable; thoraxRotation 7.9°/12° → 0.66 Settling; trailWristFlexExt 30.9°/12° → 2.57 Scattered (camera hands are unreliable: likely measurement).
- Caveat: lagAngle's 26° corridor is wide (9 of 10 Grooved); that is the norm as written.

**Recommendation.** An optional descriptor field naming each trace's corridor measure, filled as in the table (24 drawn traces); the provider resolves it for the club's context. The 12 with none stay ungraded (R16). The grades assume question 2's timing rule; with the 20 ms timing half on every trace they fall to 5/21/7/12/78.

**If adopted:** a descriptor field (model change); `traceHasOwnScale` passes for those 24; Overlay badges appear on Charts session rows (per club) and TypicalityTrace on Against. Display only: no stage-version bump.

## 2. Timing scale (proposal 4)

| Class | Windows | SD of time-to-peak q25 / median / q75 | What the "peak" is |
|---|---|---|---|
| Rate traces (°/s, mph) | 32 | 5.0 / 9.7 / 20.1 ms | A real interior peak; clubheadSpeed peaks at impact (65 %), SD 1 ms |
| Angles/positions peaking at the top | 50 | 9.8 / 20.3 / 33.3 ms | Downswing duration, not a peak |
| Angles/positions peaking at impact | 13 | 4.7 / 6.8 / 44.0 ms | The window's end |
| Angles/positions peaking inside | 212 | 15.3 / 27.3 / 53.4 ms | Largest |value| of a curve that often crosses zero; not an event |

Per segment (median SD): pelvis 46 ms (6 windows), lead arm 9.5 ms (4), club angular 10.6 ms (8), hand speed 6.1 ms (10), thorax none (face-on curves never whole). Cheetham: amateurs 38/29/23 ms, professionals 19/14/8 ms.

Rate-trace timing grades: 20 ms → 4/9/4/3/8 (28 windows; pelvis 1/0/0/0/5); 30 ms → 9/8/4/0/7; professional SDs → 1/0/1/0/8; amateur SDs → 2/3/0/0/5.

**Recommendation.** Grade the timing half only on rate traces with an interior peak (the four sequence speeds and handSpeed); every other trace, clubheadSpeed included, on amplitude alone. Keep TIMING_SCALE_MS = 20 ms. Don't take the professional SDs as `variabilityScale`: a cohort yardstick (the R6 conflict), and as half-widths they put the professionals themselves at the 2/1 edge. The pelvis's ~46 ms is Scattered on every scale tried; it may be peak-detection noise on a broad camera curve (HackMotion or trunk-IMU sessions can tell).

**If adopted:** a descriptor flag for traces whose peak time is graded. No change on screen for the speeds: they still have no amplitude scale, so they stay ungraded unless a cohort scale is accepted.

## 3. Trace typicality thresholds (proposal 7)

Leave-one-out, robust z (R22). Scalars: 6,836 readings, 63.3 % |z| ≤ 1, 88.4 % ≤ 2. Traces: 2,860 readings from 128 windows; RMS z(t) quartiles 0.72 / 1.04 / 1.46, q90 2.00, q95 2.61; at the shared edges only 47 % read "Your usual". The cut points giving traces the scalar shares are 1.25 and 1.92 (n 8–10: 1.39/2.17; n 11–20: 1.31/1.97; n ≥ 21: 1.23/1.86). The timing and amplitude z of the basis words already match scalars (66 %, 64 % within 1).

**Recommendation.** RMS z(t): "Your usual" ≤ 1.25, "A little off" ≤ 2.0 (only the first edge moves); a new constant `kTraceTypicalityUsualZ`.

## 4. Ordinal partial orders (proposal 8)

R23 simulated on the KS extract (no guard or probable flags in it): 8 session-clubs get a window; 15 have fewer than 2 kept segments. Lead arm + club (k = 2): 6 (Grooved 4, Repeatable 1, Settling 1). Pelvis + lead arm + club (k = 6): 1 (Mark 4 Jul, 14 of 14, Grooved). Same verdicts as Session 1's modal-set rule, more graded.

**Recommendation.** R23 settles the rule. Grade k = 2 (lead arm and club alone): it is the only order camera-only sessions give, and the basis names the segments. Keep probable peaks out until the chest bridge's timing is validated (that needs `variability_ks_extract.py` to keep each node's probable flag).

## 5. MEAS_TOL_FRACTION (proposal 6)

Nothing in the app reads it today. One validation exists: camera vs GC Quad clubhead speed, 82 pairs from 9 sessions: bias +0.2 mph, RMSE 6.3, within-session robust SD 3.7, against the golfer's own LM spread of 1.95 mph. At 0.25 or 0.5 it reads Noisy, correctly, so one metric can't choose. Attack angle camera vs LM disagrees by a robust SD of 16° (bias +3.6°): too large to use until understood (reported, not filed).

**What would settle it:** validation RMSEs on 3–4 more metrics with different corridors (club delivery from the LM; HackMotion lead wrist; trunk-IMU pelvis/thorax rotation; a static address hold), each beside its within-session golfer SD. **Recommendation:** keep 0.25, provisional.

## 6. What R26 removes

204 rows lose the CV rung. Swing readings: 142 rows (77 graded: 61 Grooved, 9 Repeatable, 4 Settling, 3 Variable; 65 "Not enough swings") across stanceWidthMm, tempoBackswing, thoraxPeakSpeed, handSpeed, leadArmPeakSpeed, clubPeakSpeed, ballBodyDistance, pelvisPeakSpeed. Launch-monitor readings: 62 rows (ball flight, already badge-less under R4). Trend: scalar Holding 85 → 73, Loosening 29 → 23. On screen those eight swing measures show "-" where they showed a badge, mostly Grooved.

## Reproduce

```
build/run-me/s5-build.sh tests variability_calibrate
build/tests/Gui/variability_calibrate /mnt/swingdata/scratch/variability/fixtures /mnt/swingdata/scratch/variability/ks /mnt/swingdata/scratch/variability/s5-calibration/current
python3 -I /mnt/swingdata/scratch/variability/s5-calibration/analysis/q1c.py   # question 1; q2.py for question 2
```
