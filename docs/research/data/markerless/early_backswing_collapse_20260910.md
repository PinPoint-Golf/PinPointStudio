# The early-backswing catastrophes, 2026-09-10

Four seen marks on the unmarked 6-iron — swings 0004 and 0005 at P2 and P3 — scored
151.7°, 159.9°, 71.2° and 63.7° against Mark's marks, bit-identical with every
markerless key off. Recorded in `p6_flip_gate_20260910.md` as "pre-existing DP
failures". They are not DP failures.

## What the tracker did

On both swings the DP is right and the segment lock agrees with it: at 0004's P2 mark
the DP says 183° (truth 177°), the lock 183.5°; at P3 283° (truth 289°), lock 282.5°.
The **published** direction on those frames is 25° and 0°. Across the whole backswing
the DP sweeps 99° → 346° as the club rotates through horizontal-back to the top; the
published θ steps 98 → 82 → 25 → 19 → 0 → 353, never following it, then reconverges
at the top. Every held frame is Coasted at conf 0.30.

The cause is the **hands-only phase model**. Its two-longest-runs ranking picked an
address-hold fidget — `[50,59]` on 0004, `[27,38]` on 0005, eight to ten frames of grip
speed above `swSpd` some 2.1 s before impact — together with the downswing run.
"Top" is then the speed minimum between the two, frame 79 / 90, **inside the address
hold**, and the A3 rail pins the onset to its far edge, 135 / 134. The phase loop
labels nothing Backswing: frames from the onset to impact are all Downswing, and the
DP, constrained to a downswing's transition kernel, walks the backswing the wrong
way. The top-collapse repair never fires because its gate is *top too close to
impact*, and this top is 2 s away.

The fidget beats the backswing because the backswing run is missing from the ranking
outright (the grip is slow through a one-piece takeaway; on a lerped pose it never
holds `swSpd` for long enough) — so a 10-frame waggle is the longest thing left
before the downswing.

## Why no one had seen it

`swinglab_run --trace` re-ran the pose — a second, independent `PoseRunner::run`,
plain full-window — and traced *that* swing. Production poses two-pass, span-bounded.
The two grip tracks built two phase models: the trace's had a backswing run
(`[284,310]`) and a top at 334; production's did not. `trace.jsonl` therefore showed
a perfect DP beside a broken `result.json`, and the trace's `tier` column read "seg"
on frames the published sample flagged Coasted. This is the "trace pm ≠ persisted
ladder, producer unlocated" lead from the 0703_0007 investigation. The trace now
reuses the analyzer's pose and ball; with that, the trace has one row per published
sample (589 = 589, was 597 vs 589) and its DP column agrees with the published θ to
within the snap's own re-registration (≤ 12°, since the trace column is written before
Layer A runs — it was 154° off), and the trace no longer costs a second pose run.

Feeding one pinned pose (`corpus/pose3`) to both runs settled the attribution: 0005
came right (the backswing run vanished cleanly, the repair rescued the top) while
0004 still collapsed — its fidget out-ranks the backswing on that pose too. Pose
variance decides whether the backswing run survives; the ranking's willingness to
seat a run from the address hold is the defect.

## The fix

Two guards in the phase model, both dark-safe:

1. **Early-side run-candidacy clamp.** With a supplied impact, a run that *ends*
   before the A3 far edge (impact − `bsMaxBeforeImpactUs`, 1.6 s) lies wholly inside
   the address hold and can never be the takeaway. Dropped before the ranking; the
   mirror of the existing late-side clamp, same keep-if-empty fallback.
2. **Invariant backstop.** If the top still lands at or before the onset, re-derive it
   the way the repair does — grip apex then speed argmin — bounded *below* by the
   onset. It did not fire on any of the seven swings; it exists for the ranking
   outcome no clamp anticipates.

On 0004 and 0005 the clamp removes the fidget, the ranking falls to one run, and the
existing repair does the rest (top 333 / 327).

## Results, all seven swings, default two-pass pose (the failing configuration)

| 42 seen marks | θ p50 | θ p90 | head p50 | head p90 | measured-head marks |
|---|---|---|---|---|---|
| before | 1.9° | 19.6° | 24 px | 224 px | 15 |
| after | 1.7° | **4.5°** | 21 px | **102 px** | 18 |

| | P1 | P2 | P3 | P4 | P5 | P6 |
|---|---|---|---|---|---|---|
| 0004 before | 17.7° | 151.7° | 71.2° | 0.4° | 2.4° | 1.1° |
| 0004 after | 4.3° | **4.3°** | **2.2°** | 0.4° | 1.9° | 1.1° |
| 0005 before | 1.5° | 159.9° | 63.7° | 3.3° | 1.6° | 1.5° |
| 0005 after | 8.5° | **2.6°** | **1.7°** | 0.8° | 0.4° | 1.5° |

The other five swings' phase models are unchanged (verified frame-for-frame). The
p90 on all 42 marks now matches the figure the gate report could only reach by
excluding these four.

## Exposure

The persisted library (90 swings with club samples and a P-ladder) shows none of
this on 0004/0005 — the GOLFSIMPC sweep's pose kept their backswing runs. One swing,
0002, carries a collapsed ladder of a related kind (Address 0.34 s, Takeaway 0.88 s,
Top 1.11 s against a true P1 at 1.43 s). The exposure is the live path and in-app
re-analysis, both two-pass, where a lerped takeaway is likeliest to lose its run.

## Residual

On the single-run swings (0001, 0004, 0005, 0007) the onset sits at the A3 far edge —
frames 130–135, against a true P1 at 215–219 — the "far-edge rail" already on record.
Those address frames are labelled Backswing, so `snap.skipAddr` cannot protect them
and the snap re-registers onto the leg there (0005 P1 1.5° → 8.5°). Same defect
family as the 3 July lead-arm tail; the tracker-side guard still needs the elbow.
