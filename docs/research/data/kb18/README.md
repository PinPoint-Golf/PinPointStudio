# KB-18 — the face-on club rate collapses at impact

**Run 17 Oct 2026.** Read-only, over the library (`/mnt/swingdata/Mark-Liversedge`, 140 swings with a club node).

## Files
- `probe_base_library_20261017.csv` comes from `tools/shaftlab/kb18_impact_probe.py`. It has one row per swing, and the tool's docstring defines the columns.
- `audit_20261017.csv` records 12 swings rendered face-on from −40 to +40 ms. Each frame shows the tracked line, the lead forearm and the grip→ball line.

## Baseline
- **The rate collapses.** It holds ≥ 0.8 of its pre-impact peak through ±8 ms of impact on only 31 of 140 swings (22 %).
- **The shaft under-sweeps.** The measured face-on shaft sweeps a median 0.67 of what its own pre-window rate predicts over −20…+20 ms. Physics predicts ≥ 1, because the face-on projection *speeds up* the 2-D rate at impact.
- **Backward readings.** 25 swings have ≥ 2 readings pointing back up the downswing after impact.

## Mechanism
The doc's "projection wobble" is not the cause. The 2-D image angle already stalls before any de-projection, and the synth track only copies it.

The cause is the face-on measurement in the impact window. From about −20 to +25 ms the shaft is a faint motion blur in front of the trousers and legs. Only its tape bands show, and on the dark sessions with no tape, nothing shows. In that window the tracker does one of two things:
- **M1, leg/shin stall:** it reports a near-vertical line on the leg or trouser edges, at 85–110° and roughly forearm speed. It then jumps once the club clears the legs on the target side.
- **M2, wrong branch:** wedge and recon tiers report the shaft pointing back up the downswing (137–168°).

**Counts over the 11 judged swings:** M1 on 6, M2 on 3 (one swing has both), clean on 3.

No single tracker tier separates the collapsed swings from the clean ones. Wedge frames are 37 % against 38 %, and coast frames 24 % against 21 %. The fault is in what the window *reads*, not in which tier reads it.

## The fix tried, and the gate (17 Oct 2026)

**What was tried:** an impact-window plausibility pass, `demoteImplausibleImpactWindow` in `shaft_track_assembly.cpp`, beside the follow-through pass.
- It demotes measured readings in −25…+35 ms that fall behind half the club's own pre-impact rate, point backwards, or run too fast.
- It measures every reading cumulatively from the last trusted sample before the window.
- The synth then bridges the gap, and the bridge σ grows with distance from the nearest measured frame.

The pass also runs on the reuse path, so no shaft stage bump was made.

**The A/B.** GOLFSIMPC Release swept the library twice into mirrors, with one binary and the pose reused: base = pass off, fix = pass on. Files: `probe_studio_{base,fix}_20261017.csv` and `g4_collateral_20261017.csv`.

| Gate | Pass needs | Base | Fix | |
|---|---|---|---|---|
| G1 rate at ±8 ms ≥ 0.8 × pre-impact peak | ≥ 85 % of swings | 32 / 139 (23 %) | 25 / 139 (18 %) | **fail** |
| G2 max rate in ±50 ms ≤ 3500 °/s | ≥ 95 % | 132 / 139 | 130 / 139 | fail (both) |
| G4 lean, lie, P-times, clubhead speed unchanged (1σ, 2 ms) | ≥ 95 % | — | 138 / 147 (94 %): speed moved on 9 | fail (narrowly) |
| G3 audited frames re-rendered | — | — | not run: G1 decided it | — |

**Why it fails.** The pass demotes the right readings; the demoted samples match the audit. But then there are two kinds of swing.
- **The camera never saw the club through impact (77 of 139 swings).** There are fewer than 2 measured samples in −30…−8 ms or in +8…+40 ms. With the bad readings removed, nothing is left there, and the synth bridges into its soft P7 ball anchor and flattens: on 5 Oct swing 4 the rate reads 32 °/s at impact. Base's "good" ratios on such swings were often spikes made by the wrong readings themselves (that swing read 1.71). G1 cannot be met there by any honest means, because nothing was measured.
- **The camera saw the club on both sides of impact (62 swings).** Base and fix agree: a median 0.59 / 0.60, and ≥ 0.8 on 10 against 10. The readings that remain are on pace.

**The 0.8 floor was the wrong premise.** On these swings about 0.6 is what the clubhead speed allows. The camera reads 63 mph at impact on 4 July swing 1, which needs only about 1400 °/s of shaft rate, about 0.55–0.65 of the 2200–2600 °/s peak before impact. So:
- on the covered swings, the early club peak may be the golfer, not the camera;
- on the uncovered ones, it is an artefact.

**Decision, by the plan's fail branch.**
- The pass ships **off** (`shaft.impactWindow.enabled`, default false; off is byte-identical to before).
- The camera club stays **withheld** (`sequence.cameraClubJudged`, default false).
- The plan's second iteration, a rate-side bridge, was **not run**. It would replace measured readings with a model on the covered swings, and has nothing to fit on the uncovered ones.

**What would let the camera time the club (for Mark's call).** Judge the camera club per swing, by coverage, instead of withholding it everywhere: judged where there are ≥ 2 measured samples on each side of impact (the 62), and withheld with its reason where there are not. Judged mode (`sequence.cameraClubJudged`) is built with a ±40 ms coverage test; making that test two-sided is the change. Turning it on reverses the 15 Oct "refuse to signal" call, so it is not done here.
