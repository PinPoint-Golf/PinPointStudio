# Impact shaft lean — the blur wedge's edges (2026-09-29)

Why impact shaft lean read ~+13° too far forward, and the before/after of the fix. Every
number here comes from these scripts run on traced `swinglab_run` output (studio Release
build) for the **32 corpus swings with a hand-marked P7** plus 07-04 s8, scored against the
hand-marked shaft frames in each swing's `truth.json`.

## Run

```
# per swing: swinglab_run <corpus>/swings/<session>/<swing> --out <root>/<session>__<swing> --trace
#            [--pose <corpus>/pose3/<id>.json]  (07-04: --bands 308,362,560,758,808,854 --club-length-mm 940 --hosel-mm 882)
python before_after.py <root-before> <root-after>   # impact lean, per-frame θ, P-times, speeds, metric counts
python synth_eval.py   <root> [<root> …]            # the synthetic curve vs every marked frame + its peak |θ̈|
python peak_pair.py    <root>                       # the two-ridge (blur ends) hypothesis
python edge_study.py   <root>                       # plateau edge methods (all failed — kept as the negative result)
```
`<corpus>` = `/mnt/swingdata/corpus`; the scripts need the `wrow_*` / `wedge_lead` trace fields
added with this work.

## What was wrong

1. **The tracker read the blur's trailing ridge.** The proximal sweep is a ridge detector; a
   blurred shaft shows as two ridges — where the exposure started and where it ended — about
   ω·t_exp apart. The wedge centroid sat on the larger, trailing one. On 123 marked downswing
   frames: leading ridge −1.2° median (|4.6|), tracker +6.4° (|8.2|), trailing +10.4°; at
   16°+/frame the tracker was +15.9°, the leading ridge +1.6°.
2. **The plateau "width" was never ω·t_exp.** ≈30° at any rotation (slope ≈ 0), and the width
   based exposure estimate pinned at its 8 ms clamp on every swing. Half-max and threshold edge
   methods (`edge_study.py`) did not fix it — the profile is two peaks, not a plateau.
3. **The impact ψ reconstruction ("arm is the witness") overrode good θ.** Where it differed
   from the tracker by more than reconTol it read +112° median off the marks; that was the two
   ±100° swings (06-11 s9, 09-09 s1).
4. **The synthetic track ignored the evidence between P-anchors** — a Hermite through the
   anchors, which clubhead speed, hand speed, lag and low point read.

## Before → after (studio Release, 32 swings)

| | before | after |
|---|---|---|
| impact lean − marked (median / \|median\| / p90) | +12.5 / 12.7 / 28.8° | −0.4 / 6.7 / 13.3° |
| within ±10° of the mark | 11/32 | 23/32 |
| tracked θ P6–P8 vs marks (\|median\|) | 9.7° | 4.5° |
| tracked θ P1–P6 | — | unchanged |
| synth θ P7–P8 (median / \|median\|) | +12.9 / 12.9° | +2.1 / 5.9° |
| P6 / P8 time vs marked (\|median\|) | 3.2 / 10.5 ms | 2.0 / 4.3 ms |
| P1–P5, P7, P10 times | — | unchanged |
| clubhead speed shift | — | \|0.7\| mph (p90 2.6) |

The synth plausibility scale (`synth.evidenceAccelSigmaDps2`) was tuned over 1k–80k °/s²;
5 000 was best on the marks at a plausible peak |θ̈| (table in `shaft_synthesis.h`).

## The 3-D fusion (34 DTL corpus swings, old tracker vs new, same build via config)

The fused shaft and its plane fits are essentially unchanged: downswing plane rms 1.90 → 1.98°
(median), |off-plane| within ±40 ms of impact 1.11 → 1.09°, plane readings shifted 0.02° median,
sign disagreements 43 → 42, usable downswing planes 28 → 27 (07-03 s4 had exactly the minimum 8
usable frames; two became wedge readings — that session's DTL placement is unusable for plane
work anyway). Improvements on 07-04 s2/s3 (near-impact off-plane 4.6 → 2.2°, 4.2 → 2.6°) and
07-03 s10 (12 → 7 sign disagreements).

The first cut applied the leading edge before the top too, where the model's ω̂ is high while the
club reverses: on 06-11 s7 it took a structure ~70° off the shaft and the DP carried the whole
backswing onto it (P1 102° → 237°, 24 sign disagreements, both planes lost). Edges now apply from
the top onward only; 06-11 s7 is back to 0 disagreements with both planes.

Tried and NOT kept: counting wedge face-on frames as measured in the fusion. Near-impact
|off-plane| improves (1.09 → 0.96°) but the downswing plane gets noisier (rms 1.98 → 2.24°,
06-11 s9 1.31 → 2.68°) — wedge readings (±4.5°) are rougher than thin-line ones, and the planes
feed the Plane metric.

## Still open

- Impact TIMING: pipeline P7 median +4 ms after the mark (sd 6); 06-11 s8 is 19 ms early (+26°
  of lean on its own).
- A few impact frames on a thin-line (RAY) reading still ~13° forward (07-03 s4/s5, 08-18 W1 s2/s3,
  09-09 s2/s4) — not wedge frames, so untouched by this fix.
- 07-03 s6 differs by ~30° between the Mac and studio builds on the same pinned pose: video
  decode, not the tracker.
- The trailing edge is kept only when its separation matches |ω̂|·t_exp; it exists on ~20 % of
  wedge frames.
