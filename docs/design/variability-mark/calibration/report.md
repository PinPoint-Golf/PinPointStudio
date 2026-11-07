# Variability marks: calibration report (Session 1)

**7 Nov 2026.** Independently reviewed: the review's corrections are made, and every figure was recomputed from the CSV. This is the grader (`src/Metrics/variability/`) run over every logged session, with the spec §8.10 constants as they stand. Proposals are listed at the end, and **none has been applied.** Every constant still carries `TODO(calibrate)`.

## What was run

- **Data:** a read-only extract of all five athlete libraries, made on the cabin PC on 7 Nov (`tools/diagnostics/consistency_extract.py`, after the 7 Nov library sweep). It has 23 sessions and 284 swings. The kinematic-sequence order the app stored was extracted alongside (`tools/diagnostics/variability_ks_extract.py`).
- **Tool:** `variability_calibrate <fixtures> <ks> <out>` (`src/Gui/tests/variability_calibrate.cpp`). It grades within each club and never pools clubs.
  - The corridor scale is (σLo+σHi)/2, the in-range band.
  - σnoise is each reading's propagated σ.
  - Guarded swings are excluded where the guard applies.
  - The run takes 23 s on the Mac and writes nothing outside its output folder.
- **Files:** `variability_calibration.csv` has one row per graded or ungraded item. `variability_trend.csv` has one row per (athlete, metric, phase, club, basis).
  - **`phaseListed` column.** It is 1 when the descriptor's `phases` lists the read phase. The descriptors list where a peak *matters*, not where a value is read, so a 0 is not proof the reading is meaningless.
  - **What "listed" covers.** The figures below use the 789 listed scalar rows plus 38 with no descriptor (swingPlaneIotaBack and swingPlaneIotaDown, all ungraded, no scale). That drops readings like clubhead speed at address. The 635 unlisted rows are mostly address posture readings; they are in the CSV.
- **Launch-monitor readings** are graded as scalar rows (`source` = lm, §6 below). They are not in the tables above them.
- **Warm-up.** No swing was excluded as warm-up, because the extract carries no warm-up flag.
- **Guarded swings** are excluded from scalar readings that rest on the P-ladder, from traces, and from the ordinal window.

**Only 13 of the 23 sessions get a consistency grade.** N_MIN = 8 swings: 9 sessions have fewer, and 2026-08-04 has launch-monitor data only. The graded rows have a median n of 10.

## Results as the constants stand

| Basis | Graded rows | 5 | 4 | 3 | 2 | 1 | Grade 0 | Ungraded |
|---|---|---|---|---|---|---|---|---|
| Scalar | 296 | 177 (60%) | 61 | 21 | 12 | 25 | 204 | 327 no scale |
| Trace | 282 | 10 | 29 | 22 | 33 | 188 (67%) | 555 | 0 |
| Ordinal | 4 | 3 | 1 | 0 | 0 | 0 | 11 | 9 no resolved order |
| Measurement (Needle) | 215 | 14 | 1 | 33 | 29 | 138 (64%) | 0 | 341 no σ, 271 no scale |

Grade words: consistency 5 Grooved … 1 Scattered; Needle 5 Locked … 1 Noisy.

The scalar grades lean to Grooved, the traces to Scattered, and the Needle to Noisy. Each lean has one main cause, set out below.

### 1. The propagated σ is not a noise SD (§8.6)

- **The evidence.** On 58% of corridor readings that carry a σ, the session's whole observed spread is smaller than that σ (the median observed SD/σ is 0.86). A real noise SD can't routinely exceed everything we observe. The propagated σ is an uncertainty bound that includes fixed offsets.
- **The effect.** Subtracting it as noise (√(SD² − σ²)) zeroes the spread on 87 of 296 graded scalar rows, which become automatic Grooved. Without the subtraction, the 229 corridor rows grade 74 / 72 / 32 / 16 / 35, a sensible spread: 32% Grooved, 15% Scattered.
- **Trend knock-on.** A zeroed ρ has a standard error of 0, so any change in it reads as Tightening or Loosening. leadArmPeakSpeed P7 "Tightening" from ρ 0 is an example.
- **The Needle.** Read as the Needle's uncertainty, the same σ makes 64% of readings Noisy (median ρm 1.28 at MEAS_TOL_FRACTION 0.25). It would put "Checking" in place of 95 of 296 golfer grades (§8.6).

### 2. The CV fallback grades almost everything Grooved (§8.2 step 4.3)

- **What happens.** When there's no corridor, a Ratio measure is graded on its CV: ρ = SD/mean, on the same 0.25 / 0.5 / 0.75 / 1.0 bands. A golfer's CV is a few per cent (quintiles 0.04 / 0.06 / 0.07 / 0.11 on observed SD). So 61 of 67 CV rows are Grooved on observed SD (62 as graded, after noise subtraction): stanceWidthMm, tempoBackswing, ballBodyDistance and the peak speeds.
- **Why that's wrong.** Those bands were built for a ratio to a corridor half-width. A CV of 0.25 would mean a golfer whose swing speed wanders by a quarter.

### 3. Trace timing dominates, and the amplitude fallback is tight (§8.3)

- **Timing.** The median SD of time-to-peak is 25 ms (quartiles 10 / 48 ms). Against TIMING_SCALE_MS = 20 that gives a median ρₜ of 1.26, and timing limits 202 of 282 traces. Some of it may be event-timing noise; that is not yet measured for peak times. The "less 10 ms" row in the table below is illustrative only.
- **Amplitude.** No metric has an "associated scalar" corridor in code, so every amplitude scale is the fallback, 0.10 × mean peak. On amplitude alone, traces grade 63 / 59 / 45 / 16 / 99.
- **Some "peaks" sit on the window's edge.** 272 of the 282 graded traces are angles or positions, not rates. Counting swings whose peak lies within one grid step (1%) of an edge (CSV `peakAtTopShare` / `peakAtImpactShare`):
  - 49 traces peak at the top on most swings. There ρₜ measures downswing duration, not peak timing: 41 of them are timing-limited and 26 grade 1.
  - 25 traces peak at impact on most swings, including the 9 with a peak-time SD of 0 ms.
  - 208 traces peak inside the window.
  - The 15 graded rate traces (speeds) never peak at the top.

| TIMING_SCALE_MS | 5 | 4 | 3 | 2 | 1 |
|---|---|---|---|---|---|
| 20 (spec) | 10 | 29 | 22 | 33 | 188 |
| 30 | 17 | 26 | 58 | 31 | 150 |
| 40 | 23 | 39 | 60 | 41 | 119 |
| 50 | 25 | 53 | 63 | 35 | 106 |
| 20, less 10 ms timing noise | 25 | 17 | 36 | 30 | 174 |

### 4. Ordinal (kinematic sequence, §8.4)

- **What the app stores.** Its kinematic-sequence order contains placed segments only. On camera-only sessions that is mostly leadArm>club (k = 2), sometimes with the pelvis (k = 6).
- **What the calibration did.** It graded the swings whose placed segments match the session's most common set. That rule is a calibration-only choice: the spec has no rule for partial orders.
- **Result.** With k = 2, chance is 50%, so it grades high easily: 7 Oct (k = 2) is Repeatable on 12 of its 50 swings (guarded swings excluded). 8 and 20 Oct (k = 2) and 4 Jul (k = 6, 13 of 13) are Grooved.
- **No trend.** Ordinal series get no trend or funnel, because §8.8 is defined on ρ.

### 5. Trend (§8.8)

- **Counts.** Over the listed swing-reading scalar and trace groups (launch-monitor groups excluded): 70 Holding, 7 Tightening, 26 Loosening, 198 Insufficient.
- **Why so many Insufficient.** Most groups lack 3 graded sessions. Mark's 7-iron has 5 sessions with 8 or more swings, 4 of which grade more than one reading.

### 6. Launch-monitor readings (§9.6)

- **Rows.** 343 per-club scalar rows over 15 sessions, with one reading per shot.
- **Scales.**
  - 11 keys have a corridor (carry, club path, clubhead speed, face to path, launch, smash, spin and others).
  - 4 Ratio keys use the CV fallback (back spin, ball speed, peak height, total).
  - 10 have no scale, including offline, face angle and side spin.
- **No measurement rows.** LM figures are the vendor's (§9.4).
- **Graded:** 132 rows: 40 / 24 / 18 / 15 / 35 (5 … 1).
- **Two spec points.**
  - §9.6 wants per-club ball-flight tolerances (`variabilityScale`), and none are set, which is why offline has no scale.
  - §9.5.1 grades outcome measures only within a shot intent, and these shots carry none. So the LM grades here are calibration-only.

### 7. Trace typicality sits on its own edge (§8.7)

The reviewer simulated swings drawn from the window's own distribution, at n = 20. A swing from that same distribution has an RMS z(t) whose median is 1.08, so it reads "Your usual" only 15–55% of the time, depending on curve smoothness. The scalar z reads it that way about 68% of the time. The trace thresholds can't share §7.4's |z| 1 and 2 as they stand.

## Proposals: none applied, each needs Mark's ruling

1. **§8.6 noise.**
   - Don't subtract the propagated σ as noise, and don't use it as the Needle's uncertainty for the measurement-limited coupling.
   - Subtract only a measured repeatability noise, such as a static-hold SD or the validation baseline in `docs/validation/data/series_noise_baseline_corpus.csv`, once one is wired.
   - Until then, consistency grades on the observed SD.
2. **CV fallback.** Leave Ratio measures without a corridor ungraded until each gets a `variabilityScale`, rather than grading CV on the corridor bands. The alternative is a CV band set of its own, for which these 67 rows are too few to set it.
3. **Scalar bands.** Keep 0.25 / 0.50 / 0.75 / 1.00. On observed SD against corridors they spread 32 / 31 / 14 / 7 / 15%. Revisit once more golfers have sessions of 8 or more swings.
4. **TIMING_SCALE_MS.** Not yet. First decide which traces have a meaningful peak inside the top→impact window. The candidates are the rate traces, those whose descriptor `phases` says the peak matters, or those whose peaks sit inside the window on most swings (208 of 282). For the 49 top-edge traces, ρₜ measures downswing duration (§3). The table above is the evidence for a scale once that is settled.
5. **Trace amplitude scale.** Give each trace an associated scalar corridor in the descriptor: a new optional field naming the scalar, which is a model change for Mark. TRACE_AMP_FRACTION 0.10 then becomes a last resort.
6. **MEAS_TOL_FRACTION.** Leave it at 0.25 until a validation RMSE exists; the propagated σ is the wrong input to judge it with.
7. **Trace typicality thresholds.** Calibrate a separate pair for RMS z(t), for example from shuffled sessions as the Consistency stand-outs were. Until then, lean on the timing and amplitude deviations for the words.
8. **Ordinal partial orders.** Choose a rule. Options:
   - (a) the modal placed set, as here;
   - (b) only swings with all four segments placed, which are rare on camera-only sessions;
   - (c) pairwise agreement on each placed pair.
9. **N_MIN = 8.** Keep it as the default. 9 of 23 sessions fall under it. Grades at n = 8–10 move between consecutive sessions no more than at n ≥ 15: mean |Δgrade| 0.26 against 0.42, but noise zeroing confounds that.

## Reproduce

```
build/tests/Gui/variability_calibrate /mnt/swingdata/scratch/variability/fixtures /mnt/swingdata/scratch/variability/ks /mnt/swingdata/scratch/variability/calibration
```

The extracts are re-made on the cabin PC from each athlete's library. The extractor commands are in each script's header.
