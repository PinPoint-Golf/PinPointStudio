# Three open issues, 2026-09-10 (evening)

Left open by `early_backswing_collapse_20260910.md`: the 0703_0007 impact emission
(+254 ms), the far-edge onset rail, and the §5.3 no-regression pass on the taped corpus
that the P6 flip shipped without. All three worked on the M4 Mac mini; the corpus
population is the 61 pinned-pose swings in `corpus/pose2` (no ViTPose run — pose loaded,
so every configuration sees the same grip track), the baseline is `3f40b01` (the last
commit before the flip) built in a worktree under `build/`.

## 1. The 0703_0007 impact emission — located and fixed

With the trace now the production run's own (previous note), HEAD reproduced the
defect exactly: `pm.impact 520` (3,485,709 µs, 7 ms from the marked P7) but
`impactGeomApplied 1, impactGeomFrame 558` — the P7 impact-geometry refine moved the
emitted Impact 38 frames late. The August investigation had read
`impactGeomApplied 0` off a trace that was tracking a different pose.

Why the geometry was late: through the impact blur the DP holds the P6 direction
(θ ≈ 205° from frame 512 to 532, support 0.1–0.5) and only meets the grip→ball
direction at frame 558, deep in the follow-through. The geometry found that crossing
faithfully and, at 275 ms from the anchor, judged the **anchor** implausible.

Both documented anchor failures leave the true impact at or *before* the emission (a
13–22 ms early trigger; a nearest-frame mapping that lands late across a coverage
gap). A crossing later than the emission by more than `overrideUs` is therefore the
geometry lagging, never the anchor failing. The override is now one-sided: a later
crossing does not override and does not corroborate; `shaft.impactGeom.overrideLater`
restores the two-sided rule for A/B. Pinned in `impact_geom_test`.

0703_0007 (two-pass pose): Impact **+254 ms → −7 ms**. On the 14 truth swings of the
pinned-pose corpus the emission is unchanged (median −0 ms, |err| p90 7 ms, before and
after) — on that pose the DP does not lag and the rule never bites.

## 2. The far-edge onset rail — fixed

On every swing whose backswing lost the two-longest ranking (0001/0004/0005/0007 on
the 6-iron, 0703_0007, five taped swings in the corpus) the onset sat at the A3 far
edge, impact − 1.6 s, 0.5–0.6 s before the marked P1. The pin-gated reseed's fallback
candidate was the *sub-swLow boundary* behind the backswing motion — but on the
lerped grip a 2–4 px/f creep floor runs the whole address hold, so that boundary is
the deep pre-fidget stillness half a second early, and A3 clamps it to the far edge.

The candidate is now the *end* of the backswing motion (the last frame above `swLow`
before the top dwell). Handing `walkBack` that horizon lets its no-return scan find
the last address settle before the grip departs for good — the takeaway.

| onset vs marked P1 | 0001 | 0004 | 0005 | 0007 | 0703_0007 |
|---|---|---|---|---|---|
| before | −593 ms | −563 ms | −545 ms | (far edge) | −502 ms |
| after | +34 ms | −27 ms | +34 ms | −54 ms | +94 ms |

Corpus (20 truth swings, Takeaway event vs P1): |err| p90 **345 → 179 ms**, median
+43 → +60 ms; the five far-edge swings moved +543..+596 ms. Top and Impact unchanged.

A consequence: with the onset now within ±55 ms of P1, on the swings where it is
early the P1 frame is labelled Backswing and `snap.skipAddr` no longer covers it — the
snap took 0004's P1 from 4.3° to 18.7° the moment the rail was fixed. The snap now also
skips the first `skipTakeawayUs` (80 ms) of the Backswing: it earns nothing there (its
gain is P3–P5) and the leg is the counterfeit. 0004 P1 back to 8.7°; 0002 P1 0.2° →
7.3° (the snap had been helping there). 42 seen marks: p50 1.9°, p90 4.9° (was 1.7 /
4.5 before the onset moved; the difference is two P1 marks, which no metric reads).

## 3. §5.3 no-regression on the taped corpus — run, and it caught one

**Contract** (baseline default → HEAD with every markerless key off): published tracks
byte-identical on 56/61 swings. The five that differ are the far-edge onset swings
above (bs0 281–284 → 364–370), where relabelling the address frames changes what the
DP publishes there; nothing else moved. Coverage 0.910 → 0.913, valid 61/61.

**Regression** (baseline default → HEAD default), first pass: coverage 0.910 → 0.938,
Top and Impact unchanged, Takeaway improved — and **θ against the band lock on 1,015
band frames went 0.26/0.49° → 0.61/3.18° p50/p90**, the same ~2.5–3.7° p90 on every
taped swing. The snap was re-registering lines the band lock had already placed to
0.3°; the hand-mark grading on 08-18 never saw it because the marks' own noise hid
it. The band lock is a direct measurement; the snap is for frames without one. BAND
frames are now excluded from the snap.

Second pass, after the exclusion: see the table below.

| baseline `3f40b01` → HEAD default, 61 pinned-pose swings | baseline | HEAD |
|---|---|---|
| θ vs band lock, 1,015 band frames, p50 / p90 | 0.26° / 0.49° | **0.26° / 0.49°** |
| coverage (mean) | 0.910 | **0.938** |
| valid | 61/61 | 61/61 |
| tier mix | band 2.1, ray 23.8, pred 72.6, wedge 1.3 % | band 2.1, seg 13.9, ray 11.4, pred 71.3, wedge 1.2 % |
| hand-mark seen frames (639), p50 / p90 | 0.7° / 28.3° | 0.5° / 28.7° |
| Takeaway vs P1 (20), median / \|err\| p90 | +43 / 345 ms | +60 / **179 ms** |
| Top vs P4 (14), median / \|err\| p90 | +20 / 58 ms | +20 / 58 ms |
| Impact vs P7 (14), median / \|err\| p90 | −0 / 7 ms | −0 / 7 ms |

Per taped swing the band p90 is unchanged to the hundredth (0.41–0.50°) or better
where it was poor (07-05/0006 7.15 → 6.36°, /0007 3.72 → 2.96°, /0010 5.90 → 4.47°).
The published track still differs on every swing (per-swing |Δθ| p90 median 4°, max
10° — the snap's re-registration of RAY frames, which is the feature); what the
contract protects is the band-locked frames, and they are now untouched. The hand-mark
p90 is the 3 July daylight session's lead-arm tail, on record and unchanged.

## Cleanliness

The baseline worktree (`build/baseline-3f40b01`, ~4 GB with its build) and every
corpus run tree were deleted once these numbers were in the repo.
