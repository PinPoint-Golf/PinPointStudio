# The down-the-line shaft tracker — prototype results, 2026-09-20

What was built, what it was graded against, and what the grade means. The design
is `docs/design/dtl_shaft_tracker_design.md`; its "As built" section lists every
departure this note measures. Stage 0's probe is `stage0_probe_summary.md` in
this directory, and the truth instrument's own record is
`band_truth/band_truth_summary.md`.

Read the scope first, because it bounds everything below: **one golfer, one rig,
twelve taped swings, and truth that exists only in the address region.** The
accuracy numbers are address-only. The mid-backswing, downswing and impact bands
have no automatic truth at all; they were adjudicated by eye on montages, and
that is a weaker claim, stated as one.

---

## 1. What was built

`DtlShaftTracker` runs beside `ShaftTracker` on the down-the-line stream of a
swing that also has a face-on camera. It inherits time (the P-ladder, the impact
instant, chirality) and a **visibility schedule** from the face-on analysis of
the same swing, solves θ_D independently inside each sighted band, and publishes
nothing in an end-on gap. It never feeds anything back: the face-on
`result.json` is untouched by a `--dtl` run, and that is gated rather than
asserted (§7). No app surface, no metric, no UI — the tracker is reachable from
`swinglab_run --dtl` only, and its product is a sibling file `club_dtl.json`,
not a block inside `analysis`.

| file | what it holds |
|---|---|
| `src/Analysis/dtl_shaft_types.h` | `DtlShaftTrack2D`, the tier/length/ρ-source/waiver enumerations, the face-on witness |
| `src/Analysis/dtl_shaft_config.h` | every resolved scalar, echoed into `club_dtl.json` with a config hash |
| `src/Analysis/dtl_shaft_tracker.cpp` | the `SwingWindow` layer: anchors (both forearms and the eight body joints from day one), timebase, decode-once cache |
| `src/Analysis/dtl_shaft_decide.cpp` | witness, quarantine, clean plate, DTL ball, sighted bands, three evidence channels, D1–D6 and the ball gate, one Viterbi per band |
| `src/Analysis/dtl_shaft_post.cpp` | the snap (line re-registration), the evidenced-run measurement, the tier ladder |
| `src/Analysis/shaft_track_shared.h`, `shaft_frame_io.h` | the seams pulled out of the face-on assembly so both views share one scorer and one frame cache |
| `tools/shaftlab/dtl_probe.py` | Stage 0 |
| `tools/shaftlab/dtl_band_truth.py` | the truth instrument (§3) |
| `tools/swinglab/montage_dtl.py` | the adjudication montages |
| `tools/swinglab/src/swinglab_run.cpp` | `--dtl`, `--dtl-pose`, and the club-record injection flags `--bands` / `--club-length-mm` / `--hosel-mm` |

A DTL markup capability also landed in the app — side-by-side Face-On | DTL
panes on one playhead, marks persisted to `truth_dtl.json` and never to
`truth.json` (`src/Gui/markup/markup_truth.h:101`). **No DTL marks have been
made yet.** It exists to supply the truth §3 says does not exist.

---

## 2. The instrument, and why E1 could not be it

Design §6 named the unconditioned DTL **band lock (E1)** as the instrument that
grades the whole coupling. It could not be, for two reasons discovered in that
order.

**First, E1 was not running at all.** Stage 0 found zero band locks on the dev
six in both views: `frameBandMatch()` returns nothing when `bandsMm.size() < 2`,
and these swings carry no `capture.club` block, so `job.bandCentersMm` was empty
(`stage0_probe_summary.md`, preamble). The `bandPx` number the design's §2 read
as "E1 locked" is the length ladder's rung computed from the **segment** lock's
scale — a different measurement entirely. That is the first of the two tooling
errors this work contributed (§13).

**Second, with the geometry injected, E1 still barely locks.** `swinglab_run`
gained `--bands 308,362,560,758,808,854 --club-length-mm 940 --hosel-mm 882`,
and the runs record the injection (`runmeta.json` → `clubOverride`). E1 then
locks on **0–4 DTL frames per swing** (c4: 2, 1, 0, 0, 0, 4; held-out: 1, 1, 0,
0, 0, 0). The cause is optical and not fixable by threshold: the DTL camera has
**no ring light**, so the tape images as ordinary white paint between black tape
rather than the saturated retro-reflective blobs the face-on engine was built on
(`dtl_shaft_decide.cpp`, the E1 comment).

**What replaced it** is `tools/shaftlab/dtl_band_truth.py`: a zero-mean signed
band-**template** match along a ray from the DTL pose grip, correlating on the
white/black *alternation* rather than on brightness, because bare steel reads
+20…+160 grey levels against its lateral background and is as bright as the
bands. It takes **no face-on input of any kind** — no track, no phases, no
timing, no ladder.

| | dev six | held-out six |
|---|---|---|
| accepted frames | **506** | **620** |
| per swing | 50, 107, 67, 113, 97, 72 | 65, 105, 87, 69, 34, 65 |
| accept rate of posed frames | 12–25% | — |
| self-consistency (local-quadratic residual) | **p50 0.155°, p90 0.375°, std 0.274°** (n = 224) | — |
| tiles drawn and counted by eye | **204**, 0 wrong | — |

The 204 follows from the instrument's own adjudication rule — every accept where
a swing has 60 or fewer, every third otherwise: 50 + 36 + 23 + 38 + 33 + 24 =
204 (`band_truth_summary.md`, "Adjudication").

**The instrument's hard limit, and it is the headline caveat for this whole
note.** It locks in the **address region only** — roughly 1.7 s before P1, as the
golfer settles over the ball, to about 50–190 ms after it — and abstains through
the entire swing. Through the swing the 6.5 ms exposure smears the 25 mm bands
along the shaft, the three-group template has nothing to correlate with, and
every candidate that still scores is a body line; loosening the gates produced
false locks on the torso and the trouser seam, which were adjudicated and
rejected. So:

> There is **no automatic truth** in the mid-backswing, downswing or impact
> bands. Everything claimed there rests on montage adjudication by eye.

---

## 3. Baseline — the number the tracker had to beat

The unmodified face-on tracker pointed at the DTL stream (`--face-on DTL`),
paired to band truth within 8 ms:

| | dev six |
|---|---|
| truth frames | 506 |
| baseline MEASURED samples on a truth frame | 25 |
| of those, more than 15° from truth | **25 (100%)** |
| \|error\| p50, per swing | 152.5 – 154.8° |
| \|error\| p90, per swing | 152.8 – 155.3° |

Read the denominator honestly: the baseline commits a measured sample on only 25
of 506 truth frames, and two of the six swings commit on none. Where it does
commit it is 152–155° out — roughly 180° minus 27°, i.e. **the club published
pointing back up the lead arm, head where the butt is**. That is the design's §2
failure with a face-on-independent number against it. Source:
`stage0_probe_summary.md`, section (3) of the band-truth addendum.

The montage over all frames (not only truth frames) reads the same:
`00_baseline_dev6_summary.md` gives p50 152.8–158.3°, p90 153.3–159.8°, with
every truth-paired sample over 15°.

---

## 4. Stage 0 — the numbers that mattered

Only four of Stage 0's six tables survived contact with real truth; the rest
were computed against a stand-in the probe itself calls a negative control.

**The visibility schedule reproduces.** ρ̂_D = √(max(0, 1 − (ρ_F cos θ_F)²)) at
the ladder instants, on all six dev swings: ≥ 0.97 at P1, P3, P5; 0.84–0.98 at
P7; 0.01–0.25 at P2 and P6; 0.06–0.56 at P4. Long / stub / long / gone / long /
thin / long, seven for seven before the occlusion, on six swings rather than the
design's one. The *discrimination* is robust; the *value inside a stub* is not —
near P2/P4/P6 ρ̂_D is a difference of two near-1 numbers, and a 2% error in the
face-on length denominator moves ρ̂_D at P4 from 0.05 to 0.24.

**The corridor is worth having, in one regime only.** Against band truth, split
by face-on ρ_F:

| ρ_F regime | n | corridor-centre residual p50 | p90 |
|---|---|---|---|
| ρ_F ≤ 0.93 (face-on length honest) | 53 | **7.5°** | **13.9°** |
| ρ_F > 0.93 (face-on railed, equation (c) degenerate) | 320 | 34.6° | 38.8° |

That confirms the design's one-swing estimate (5.6° / 10.8° by eye) on six
swings against a face-on-independent reference, and confirms `rhoFMax` as the
gate that separates the two regimes.

**The depth sign is not settled by any data here.** Both centres are offered to
the solve, the cost is the min over the two, and the column that records which
one the evidence took splits roughly 60/40 inside the long mid bands. Stage 0's
own sign table is a table of which wrong answer was nearer and must not be used;
the band-truth sign table at address is degenerate for the same reason the
corridor is there (36.3° against 35.2° is not a preference). **`w0` for the mid
bands is still not sized.** The prototype ships a placeholder `corridorW0Deg` =
25° and logs every escape.

**The cross-view anchor fit is loose.** The grip-row affine y_D ≈ a·y_F + b
gives a consistent scale (a = 1.19–1.24 on every swing) but a residual p50 of
8–20 px and p95 of **27–97 px** on a 1024-row frame. Design §5.2's "quarantine
above the fit's p95" is therefore a percentile of a bad fit; §6 below records
what was done instead.

**No constant inter-camera clock offset is demonstrated.** The grip-row
cross-correlation reads +2,726 to +6,866 µs across six swings — a spread
comparable to the value — so nothing is applied. The ~3.2 ms the design calls
"DTL leads face-on" is *frame phase*, is exact arithmetic on the recorded
timestamps, and is removed by interpolating the witness, which the tracker does.
`clockOffsetUs` is 0 in every `club_dtl.json`.

---

## 5. The iteration record

Five configurations on the dev six. Each row is one defect, the rule it bought,
and what the numbers did. "Address error" is pooled over all dev-six truth
frames paired within 4 ms; "published" is RAY + SEG + BAND frames over all six
swings.

| config | hash | published | truth-paired | p50 | p90 | > 15° | escapes / swing |
|---|---|---|---|---|---|---|---|
| c1 — first build | — | 641 | 61 | 64.25° | 80.75° | **50** | 10–66 |
| c1b — schedule denominator fixed | — | 585 | 61 | 61.00° | 80.75° | **42** | 12–31 |
| c2 — ball gate, rhoSolveMin 0.50 | `c58dd6f9ce…` | 498 | 92 | 2.88° | 75.47° | **19** | 9–19 |
| c3 — gate hold extended, length off the snapped line | `a3edfd9d8e…` | **1023** | 322 | 3.50° | 6.50° | **0** | 9–19 |
| c4 — snap extent, reverse-ray waiver | `6d49771b0a9cf28c` | **1150** | 352 | **0.38°** | **3.50°** | **0** | 9–20 |

### c1 → c1b: the schedule denominator must come from in-plane frames

ρ_F is the face-on length divided by that swing's full length. Taking the
denominator as a p95 over *all* measured face-on frames gave 328–351 px, where a
p95 over **in-plane** frames only (|cos θ_F| ≥ 0.94) gives 290–321 px, on four of
six swings. Perspective magnifies the club at address — the head is ~0.5 m nearer
the face-on lens than the hands — so the address frames inflate the denominator,
ρ_F comes out small, and ρ̂_D at P2 and P6 reads 0.51 instead of 0.00. **The
end-on gaps never opened.** With the in-plane denominator they do: END-ON frames
go from 706 to 783 over the six swings and corridor escapes halve, 251 → 135.

A second correction went in with it. Face-on does **not** measure a club length
at the address hold (it coasts) or at impact (it reconstructs) — about 96 frames
a swing — and those are two of the four best-seen DTL moments. Refusing them for
want of a face-on length threw the address hold and the impact zone away with
"ρ̂ unknown" over frames where the club is sharp and in plain view. Where ρ_F is
absent but θ_F is finite, **ρ_F := 1 is used as a conservative bound, for the
schedule only**: ρ_F = 1 maximises |u_x| = |cos θ_F| and therefore *minimises*
ρ̂_D, so a near-horizontal face-on shaft still reads END-ON and only the
near-vertical ones are admitted. D4 and D5 still require a measured face-on
tier. `rhoSrc` records which of the two every frame used (c4 dev six, over all
2,629 frames: 1,474 bound, 1,153 measured, 2 none).

`rhoSolveMin` moved 0.35 → 0.50 in the same pass: at 0.35 swings 0007 and 0008
solved a 610–636 ms band straight through the top of the backswing. The cost is
the P3.9→P4.3 frames, which now read END-ON.

### c1b → c2: the address leg lock, and the ball gate that fixed it

The first build published **113–132°** through the address hold — down-left,
along the trail leg and the trouser edge to the feet — where band truth is
**58–62°** (`dtl_shaft_decide.cpp`, the still-club comment). The leg is a longer,
higher-contrast ray out of the hands than the shaft, and after the shared
percentile normalisation a limb and the shaft **tie at EV ≈ 1**. There is no
evidence margin to win on; the discrimination has to come from a constraint.

Two candidate constraints were built and graded separately, because a combined
number would have hidden which one worked.

- **The ball gate (kept).** At still-club frames — the address hold, and impact
  ± 20 ms — a candidate more than 20° from DTL's **own** grip→ball line is
  refused. Only the *timing* is inherited; the angle is a direct DTL
  measurement. Where there is no ball those frames are **not solved at all** and
  the reason says so ("no ball witness at address"), which is face-on's own rule
  ("probe address toward the ball, not along a clamp") in this view. This alone
  fixes the address lock: confidently-wrong frames 42 → 19.
- **The generalised limb veto (kept under review, unearned).** D2 was extended
  past the forearms to hips, knees and ankles, since the measured failure is a
  ray down the trail leg that an elbows-only veto says nothing about. On its own
  it is a **regression**, and in the run it fired at the solved θ on **2 frames
  of 2,629** (c2, `limbVetoJoint = leftAnkle`) — and on **zero** frames in c4 and
  in the held-out run. It also blocks two P5 tiles on 0007 and 0008 by refusing
  a good snapped line. It has not earned its place; it is kept because the
  counterfeit it was written against is real and the ball gate does not cover
  the swing.

Shoulders and head are deliberately outside the veto: at P3 the true shaft
passes near them.

### c2 → c3: the hold ends when the head leaves the ball, not 30 ms after P1

Nineteen confidently-wrong frames survived c2, all on 0005 and 0007, all in the
first frames after the ball gate released. P1 + 30 ms is face-on's instant for
"the takeaway has begun", which is a claim about the *hands*; the gate wants "the
head has left the ball". On a slow one-piece takeaway the club is still within
5° of the ball line for ~190 ms after P1, and the frame after the window closed
the solve jumped to the trouser/shin edge at 103–132°. The hold now runs forward
from P1 and releases when the face-on witness says θ_F has moved more than 10°,
or at a 300 ms cap. **Measured on the dev six: it releases at 83–224 ms; the cap
is never reached.** Confidently wrong 19 → **0**.

The same configuration fixed the length measurement, which is worth its own
paragraph because it is three symptoms of one cause.

**Off-axis pose grip, three symptoms.** The DTL pose grip is the midpoint of two
wrist keypoints and sits 17–22 px off the shaft axis.

(a) `ridgeSweep`'s `rEnd` is the argmax of a normalised cumulative score searched
only from `minLenPx` onward, so a ray that leaves the thin shaft early reports
**exactly `rLo` + `minLenPx` = 98 px** whatever the club is doing — a floor
wearing a length's clothes. Measured: 51–101 refused frames per swing carried
`rEnd` = 98 px to the digit. Measuring the evidenced run along the **snapped
line** instead took published frames **498 → 1023** and ladder tiles **12/24 →
17/24**.

(b) The snap's objective is a **mean** over r ∈ [rLo, drawnLen), so `drawnLen`
decides which part of the club is scored. With `drawnLen` = the short `rEnd` the
snap scored the near half only, where a brighter ridge than the club lives, and
sat about +3° off. Setting `drawnLen := max(rEnd, ρ̂_D · L̂_D)` — the visibility
law's own predicted projected length, which is D3's ceiling and costs nothing new
to know — moved swing 0004's thirteen address frames from 55.0–58.0° to
54.0–54.5° against a truth of 50.75°, and over all six swings took address p50
from 3.5° to **0.38°** and p90 from 6.5° to **3.50°**, with not one frame more
than 2° worse. `max()`, not replace: scoring a line over less than its own drawn
length credits a ray for the part of it nobody looked at.

(c) About **+3.7°** of the residual address error on 0004 is **convention**: band
truth is the full shaft axis, while the tracker's line starts at the pose grip,
17–22 px off it. That is not an error the tracker can remove and it is not
claimed as one.

### c3 → c4: the reverse-ray test has no free space in this view

Face-on's D1 attachment test assumes free space behind the butt. Down the line
there is none — the lead arm is near-collinear with the shaft at address and at
impact, and the forearms sit on the opposite side of the grip at P3 and P5 — so
**"the reverse ray is as strong" is the normal condition of a correct frame
here.** Measured: **126** in-span frames refused on it across the dev six, four
of them ladder tiles the montage review had already adjudicated right.

The test is now waived where the reverse direction lies within 25° of grip→elbow
or grip→shoulder of either arm, or at a ball-gated frame where DTL's own ball has
already decided the direction. Refusals **126 → 12**. The cost is stated plainly:

| | dev six (c4) | held-out |
|---|---|---|
| published frames with the waiver applied (`arm`) | 973 | 919 |
| published frames with the waiver applied (`ballGate`) | 6 | 8 |
| published with no waiver | 171 | 202 |
| **waiver covers** | **85% of published frames** | **82%** |

**D1 is effectively off in this view.** A lateral-proximity form of the test —
"the reverse ray must not merely be parallel to the arm, it must run along it" —
is the obvious replacement and is not built.

---

## 6. Final dev six (c4), per band

Bands are named from the inherited face-on ladder, so the boundaries differ by a
tenth of a P between swings; they are grouped here and the raw names given. All
from `build/dtl/c4/<id>/club_dtl.json`, truth from
`docs/research/data/dtl/band_truth/<id>.json`, paired within 4 ms.

| swing | band | sighted | published | cov | truth frames | paired | p50 | p90 | > 15° | escapes |
|---|---|---|---|---|---|---|---|---|---|---|
| 0004 | address (addr→P1.8) | 145 | 145 | 1.00 | 34 | 34 | 0.87° | 3.75° | 0 | 0 |
| 0004 | mid-backswing (P2.3→P3.6) | 39 | 31 | 0.79 | 0 | — | — | — | 0 | 8 |
| 0004 | downswing (P4.6→P5.7) | 17 | 15 | 0.88 | 0 | — | — | — | 0 | 4 |
| 0004 | impact (P6.5→P7.6) | 13 | 13 | 1.00 | 0 | — | — | — | 0 | 0 |
| 0004 | after impact (P8.2→P8.6) | 6 | 3 | 0.50 | 0 | — | — | — | 0 | 6 |
| 0005 | address (addr→P1.8) | 135 | 135 | 1.00 | 76 | 76 | 2.00° | 3.75° | 0 | 0 |
| 0005 | mid-backswing (P2.3→P3.6) | 40 | 30 | 0.75 | 0 | — | — | — | 0 | 18 |
| 0005 | downswing (P4.6→P5.7) | 17 | 15 | 0.88 | 0 | — | — | — | 0 | 2 |
| 0005 | impact (P6.5→P7.6) | 13 | 13 | 1.00 | 0 | — | — | — | 0 | 0 |
| 0006 | address (addr→P1.7) | 121 | 121 | 1.00 | 44 | 44 | 0.38° | 0.75° | 0 | 0 |
| 0006 | mid-backswing (P2.3→P3.6) | 39 | 32 | 0.82 | 0 | — | — | — | 0 | 9 |
| 0006 | downswing (P4.7→P5.6) | 15 | 14 | 0.93 | 0 | — | — | — | 0 | 0 |
| 0006 | impact (P6.4→P7.7) | 15 | 15 | 1.00 | 0 | — | — | — | 0 | 0 |
| 0007 | address (addr→P1.8) | 129 | 129 | 1.00 | 80 | 80 | 0.25° | 0.75° | 0 | 0 |
| 0007 | mid-backswing (P2.3→P3.8) | 43 | 33 | 0.77 | 0 | — | — | — | 0 | 16 |
| 0007 | downswing (P4.2→P4.4, P4.6→P5.6) | 28 | 14 | 0.50 | 0 | — | — | — | 0 | 0 |
| 0007 | impact (P6.6→P7.6) | 12 | 12 | 1.00 | 0 | — | — | — | 0 | 0 |
| 0007 | after impact (P9.8→P9.9) | 7 | 7 | 1.00 | 0 | — | — | — | 0 | 0 |
| 0008 | address (addr→P1.8) | 132 | 132 | 1.00 | 64 | 64 | 0.25° | 0.50° | 0 | 0 |
| 0008 | mid-backswing (P2.3→P3.6) | 41 | 32 | 0.78 | 0 | — | — | — | 0 | 15 |
| 0008 | downswing (P4.6→P5.7) | 17 | 14 | 0.82 | 0 | — | — | — | 0 | 2 |
| 0008 | impact (P6.5→P7.6) | 12 | 12 | 1.00 | 0 | — | — | — | 0 | 0 |
| 0009 | address (addr→P1.8) | 129 | 129 | 1.00 | 54 | 54 | 0.25° | 0.50° | 0 | 0 |
| 0009 | mid-backswing (P2.3→P3.6) | 39 | 29 | 0.74 | 0 | — | — | — | 0 | 17 |
| 0009 | downswing (P4.6→P5.6) | 17 | 12 | 0.71 | 0 | — | — | — | 0 | 2 |
| 0009 | impact (P6.4→P7.6) | 13 | 13 | 1.00 | 0 | — | — | — | 0 | 0 |

Every truth-paired figure in that table lives in the address band. **Every other
band's `p50`/`p90`/`> 15°` column is empty because there is no truth there, not
because it scored well.**

Whole-run columns, c4:

| | 0004 | 0005 | 0006 | 0007 | 0008 | 0009 |
|---|---|---|---|---|---|---|
| RAY / BAND / SEG published | 204 / 2 / 0 | 192 / 1 / 0 | 182 / 0 / 0 | 195 / 0 / 0 | 190 / 0 / 0 | 179 / 4 / 0 |
| END-ON | 58 | 55 | 58 | 57 | 59 | 61 |
| OCCLUDED (anchor quarantined) | 69 | 89 | 85 | 180 | 84 | 78 |
| UNSEEN (looked, found nothing) | 100 | 87 | 83 | 96 | 90 | 90 |
| sighted fraction of the span | 0.683 | 0.657 | 0.642 | 0.494 | 0.650 | 0.660 |
| **publishedInEndOn** | **0** | **0** | **0** | **0** | **0** | **0** |
| L̂_D source | ball | ball | ball | ball | ball | ball |

**Ladder tiles: 20 of 24** (P1, P3, P5, P7 across six swings), with θ at the
rungs:

| swing | P1 | P3 | P5 | P7 |
|---|---|---|---|---|
| 0004 | 54° | 245° | 241° | 58° |
| 0005 | 55° | **225°** | 241° | 60° |
| 0006 | 54° | 243° | 240° | 58° |
| 0007 | 54° | 245° | UNSEEN | 56° |
| 0008 | 54° | UNSEEN | UNSEEN | 58° |
| 0009 | 54° | UNSEEN | 242° | 57° |

0005's P3 at 225° is 18–20° off the other five and off its own held-out
counterparts. It was not adjudicated separately and is an open item, not a
result.

---

## 7. Held-out six — run once, on the frozen config

07-04 swings 0010–0015, DTL `configHash` **6d49771b0a9cf28c**, the same hash as
c4. One run, no tuning afterwards.

| swing | band | sighted | published | cov | truth | paired | p50 | p90 | > 15° |
|---|---|---|---|---|---|---|---|---|---|
| 0010 | address | 133 | 133 | 1.00 | 65 | 65 | 0.25° | 0.50° | 0 |
| 0010 | mid-backswing | 43 | 36 | **0.84** | 0 | — | — | — | 0 |
| 0010 | downswing | 16 | 14 | 0.88 | 0 | — | — | — | 0 |
| 0010 | impact | 15 | 15 | 1.00 | 0 | — | — | — | 0 |
| 0010 | after impact | 20 | 9 | 0.45 | 0 | — | — | — | 0 |
| 0011 | address | 133 | 133 | 1.00 | 105 | 105 | 0.25° | 0.50° | 0 |
| 0011 | mid-backswing | 42 | 30 | **0.71** | 0 | — | — | — | 0 |
| 0011 | downswing | 18 | 13 | 0.72 | 0 | — | — | — | 0 |
| 0011 | impact | 13 | 13 | 1.00 | 0 | — | — | — | 0 |
| 0012 | address | 118 | 118 | 1.00 | 87 | 87 | 0.25° | 0.60° | 0 |
| 0012 | mid-backswing | 41 | 29 | **0.71** | 0 | — | — | — | 0 |
| 0012 | downswing | 16 | 13 | 0.81 | 0 | — | — | — | 0 |
| 0012 | impact | 13 | 13 | 1.00 | 0 | — | — | — | 0 |
| 0013 | address | 122 | 122 | 1.00 | 69 | 69 | 0.25° | 0.75° | 0 |
| 0013 | mid-backswing | 41 | 30 | **0.73** | 0 | — | — | — | 0 |
| 0013 | downswing | 15 | 14 | 0.93 | 0 | — | — | — | 0 |
| 0013 | impact | 13 | 13 | 1.00 | 0 | — | — | — | 0 |
| 0014 | address | 122 | 122 | 1.00 | 34 | 34 | 0.25° | 0.50° | 0 |
| 0014 | mid-backswing | 40 | 35 | **0.88** | 0 | — | — | — | 0 |
| 0014 | downswing | 26 | 21 | 0.81 | 0 | — | — | — | 0 |
| 0014 | impact | 13 | 13 | 1.00 | 0 | — | — | — | 0 |
| 0015 | address | 128 | 128 | 1.00 | 65 | 65 | 0.25° | 0.75° | 0 |
| 0015 | mid-backswing | 40 | 34 | **0.85** | 0 | — | — | — | 0 |
| 0015 | downswing | 19 | 14 | 0.74 | 0 | — | — | — | 0 |
| 0015 | impact | 14 | 14 | 1.00 | 0 | — | — | — | 0 |

Pooled over the held-out six: 620 truth frames, **425 paired**, p50 **0.25°**,
p90 **0.50°**, **zero** over 15°. 1,129 published frames, of which 1,127 RAY and
2 BAND; no SEG. `publishedInEndOn` = 0 on all six.

**Ladder tiles: 21 of 24**, and the values are close to identical across swings —
P1 **54°** on every one, P3 **243–244°**, P5 **240–242°**, P7 **58–60°**:

| swing | P1 | P3 | P5 | P7 |
|---|---|---|---|---|
| 0010 | 54° | 244° | UNSEEN | 58° |
| 0011 | 54° | 244° | 241° | 58° |
| 0012 | 54° | 243° | UNSEEN | 59° |
| 0013 | 54° | 244° | 242° | 60° |
| 0014 | 54° | 243° | 240° | 59° |
| 0015 | 54° | 244° | UNSEEN | 58° |

---

## 8. Transfer — 06-11, a bare gap wedge on a different rig

Nine swings, bare (untaped) club, 576×1024, a dark room, a different session.
Run once, last, on the same frozen config. There is no band truth here and none
is possible: no tape. Everything in this section is either a count or a montage
adjudication by eye.

**The DTL ball is not found on any of the nine** — reason recorded on every
swing as "no bright compact blob in the address prior". The ball is white on a
blown-white mat. The consequence is structural, and it is *not* that the
address and impact bands vanish:

- The **still-club frames** — the address hold through P1, and impact ± 20 ms —
  are refused outright, because a still club with no ball to point at is not
  solved. So **P1 publishes nothing on 9 of 9, and P7 publishes nothing on 9 of
  9.** The first band on every swing opens at **P1.2–P1.4**, once the hold
  releases.
- The bands after the hold do publish: 19–24 frames per swing at **60–67°** in
  the P1.2→P1.8 band, and 8–11 frames at 51–68° in the P7.2→P7.9 band. Those
  frames run **368–440 px**, at or a little above the schedule's own L̂_D
  estimate of 325–380 px. **They were not adjudicated.** If the address leg lock
  has a residue on this rig, that is where it will be.

| | value |
|---|---|
| swings | 9 |
| ball found | **0 / 9** |
| P1 / P7 ladder tiles published | **0 / 9** each |
| P3 published θ | **242–247°** on 9 of 9 |
| P5 published θ | **239–243°** on 8 of 9 (0003 has no P5 rung) |
| ladder tiles (P1/P3/P5/P7) | **17 of 35 rungs present** — all of them P3 and P5 |
| `publishedInEndOn` | **0** on all nine |
| sighted fraction of the span | 0.257 – 0.353 |
| L̂_D source | `faceOnRowScale` on all nine (no ball to measure from) |

By eye, the P3 and P5 rails sit on the shaft. Two findings against that:

**One confirmed confident forearm lock.** Swing 0002 publishes at **P2** with
θ = 225° and ρ̂_D = **0.52** — just over `rhoSolveMin` — flagged
`corridorEscape` and published anyway, on a 238 px run. This is the failure class
the whole design exists to prevent, published on a different rig.

**Two more of the same class, unconfirmed.** Swings 0001 and 0007 publish at P4
(θ 200° and 292°) on a **98–100 px** run at ρ̂_D 0.62–0.69. 98 px is
`ridgeSweep`'s floor to the digit. They are very likely the same thing.

**What that says about the constants.** `rhoSolveMin` = 0.50 was fitted on a rig
where ρ̂_D's *stub values* were already known to be untrustworthy (§4), and
Stage 0 said so. It does not transfer at a fixed value: on 06-11 the schedule
admits frames at 0.52–0.69 that this rig's geometry does not support. The
schedule's *shape* transfers; its thresholds are a property of the rig.

---

## 8A. Where the ladder publishes, per P position

Coverage per band is the right denominator for the tracker; **publication at the
named instants is the right denominator for a consumer**, because a coach reads
θ at P1, P3, P5 and P7 and nowhere else. The tile is the nearest DTL frame to
each face-on ladder time, within one frame interval.

| P | dev six (c4) | held-out six | 06-11 transfer |
|---|---|---|---|
| P1 address | **6/6** — 54–55° | **6/6** — 54° ×6 | **0/9** — every one UNSEEN, "no ball witness at address" |
| P2 shaft parallel | 0/6 — END-ON ×6 | 0/6 — END-ON ×6 | **1/9** — END-ON ×8 and **one published forearm lock**, θ 225° |
| P3 lead arm parallel | 4/6 — 243, 245, 245, and 0005's 225° | **6/6** — 243–244° | **9/9** — 242–247° |
| P4 top | 0/6 — END-ON ×6 | 0/6 — END-ON ×6 | **2/9** — END-ON ×6, UNSEEN ×1, and **two 98–100 px lines**, θ 200° and 292° |
| P5 | 4/6 — 240–242° | 3/6 — 240–242° | **8/8** — 239–243° (0003 has no P5 rung) |
| P6 shaft parallel | 0/6 — END-ON ×6 | 0/6 — END-ON ×6 | 0/8 — END-ON ×8 |
| P7 impact | **6/6** — 56–60° | **6/6** — 58–60° | **0/9** — UNSEEN ×7 ("no ball witness at address"), END-ON ×2 |
| P8 | 0/6 — OCCLUDED ×4, END-ON ×2 | 0/6 — OCCLUDED ×4, END-ON ×2 | 0/9 — OCCLUDED ×6 |
| P10 finish | 0/6 — OCCLUDED ×6 | 0/6 — OCCLUDED ×6 | 0/9 — OCCLUDED ×9 |

P2, P4, P6, P8 and P10 are absences by construction — three by geometry, two by
occlusion — and they are the design working, not failing.

**On the second rig the two best-seen DTL moments are never published, and that
is the most serious open defect in this work.** Address and impact are the
positions this whole view was built for: at impact the clubhead's velocity is
along the DTL optical axis, so the shaft is a sharp line there while face-on sees
a fan. On 06-11 the ball is white on a blown-white mat, the detector abstains on
all nine swings, and the still-club rule then leaves address and impact unsolved
rather than guessing — which is honest, and useless. A DTL tracker that publishes
P3 and P5 but not P1 and P7 has kept the positions a coach can already read
face-on and lost the one it cannot. Either the ball detector has to work on that
mat, or the still-club frames need a second DTL-native witness that is not the
ball.

The same table also isolates the transfer leak: the three published frames on
06-11 that should not exist — P2 on one swing and P4 on two — are all in bands
that are END-ON on every other swing in the set.

---

## 9. Gates — every one in design §6, with a verdict

| # | gate (design §6) | verdict |
|---|---|---|
| 1 | zero confidently-wrong published frames | **MET.** 0 of 425 truth-paired published frames over 15° on the held-out six. Address region only. |
| 2a | θ vs truth, **address** band: p50 ≤ 1.5°, p90 ≤ 5° | **MET.** p50 0.25° on all six, p90 0.50–0.75°. |
| 2b | θ vs truth, **mid-backswing** band | **NOT GRADABLE.** 0 truth frames. The instrument abstains there (§2). |
| 2c | θ vs truth, **downswing** band | **NOT GRADABLE.** 0 truth frames. |
| 2d | θ vs truth, **impact** band | **NOT GRADABLE.** 0 truth frames. This is the band the whole design is for. |
| 3a | sighted-band coverage ≥ 0.80, **address** | **MET.** 1.00 on all six. |
| 3b | sighted-band coverage ≥ 0.80, **mid-backswing** | **MISSED on three of six.** 0010 0.84, 0011 **0.71**, 0012 **0.71**, 0013 **0.73**, 0014 0.88, 0015 0.85. |
| 3c | sighted-band coverage ≥ 0.80, **impact** | **MET.** 1.00 on all six. |
| 3d | sighted-band coverage ≥ 0.60, **downswing** | **MET.** 0.72–0.93. |
| 4 | zero published frames in END-ON spans | **MET.** `publishedInEndOn` = 0 on all six held-out, all six dev, and all nine transfer swings. Counted, not asserted. |
| 5 | face-on `result.json` byte-identical with the DTL tracker on and off | **MET.** 6-swing pinned-pose control spanning five sessions (`build/dtl/run_control.sh`), `parity_diff` with timings excluded, after every C++ package, with and without `--dtl`. **61-swing parity: a binary built from untouched HEAD (`7458d2e9`) in a detached worktree against the current working-tree binary, all 61 pinned `corpus/pose2` swings, identical swing-path strings, `tools/swinglab/parity_diff.py` (excludes `analysis.timings` only) — 61 compared, 61 pass, 0 diff, 0 unpaired.** |
| 6 | deterministic re-run | **NOT RUN.** No A/B re-run of an identical configuration was made. Owed. |
| 7 | corridor residual (centre vs truth) p50 / p99, per band | **PARTIAL.** Sized only by the ρ_F split of §4; the per-band residual exists for the address band alone. |
| 8 | corridor escapes, counted **and adjudicated** | **PARTIAL.** Counted (9–20 per dev swing, 10–20 held-out, 2–18 transfer). Only the transfer escapes were looked at individually. |
| 9 | ablation: the same run with the corridor **off** | **NOT RUN.** Owed. |
| 10 | ablation: the same run with the schedule **off** too | **NOT RUN.** Owed. |
| 11 | transfer (06-11), a finding not a gate | **DELIVERED.** §8. Partial: the sighted mid bands publish and look right; address and impact never publish; one confirmed forearm lock and two probable ones. |
| 12 | second referee — Mark's hand marks on the dev six | **NOT DONE.** The DTL markup panel now exists; no marks made. |

---

## 10. What is inert, and what is unearned

Stated separately from the gates, because a rule that never fires and a rule
that fires for the wrong reason are different problems.

- **The `lineConf`-for-EV rule.** Where the snap was accepted, the support under
  the re-registered line may stand in for the ray's own EV. On the **dev six it
  fires on 0 frames — inert.** It fires on **4** held-out frames and **2**
  transfer frames. It was tuned as the p10 of the support over frames that
  already published on EV, so it admits nothing the published set does not
  already call ordinary — but it was carried through the whole dev iteration
  without ever doing anything, which is a reason to watch it, not to trust it.
- **The generalised limb veto.** Fires at the solved θ on **0** dev-six (c4)
  frames, **0** held-out frames, and **1** transfer frame (0002-era trace,
  `trailElbow`, on a frame that did not publish). It was a measurable regression
  on its own, and it blocks two adjudicated-good P5 tiles. Unearned; kept.
- **D1, the reverse-ray test.** 85% of published dev-six frames and 82% of
  held-out ones carry the arm waiver. The test is effectively off in this view.
- **The SEG tier.** Deliberately not built. `dtlTierName` can print it; nothing
  emits it. Every published DTL angle so far is **RAY** (1,142 of 1,150 dev, 1,127
  of 1,129 held-out), with a handful of **BAND** and no SEG.
- **E1 / BAND.** 0–4 frames per swing. The design's strongest tier is, in this
  view, a rounding error.
- **The corridor's `w0`.** 25° is a placeholder, not a fit. Stage 0 could size
  it only for the address band, where the corridor is degenerate and does not
  matter.
- **The depth sign.** Not settled. Both centres offered; the taken-sign column
  splits about 60/40 in the long mid bands.
- **The clean plate's motion channel at address.** Suppressed by construction
  where the plate window overlaps the hold; the phase-aware plate exists to avoid
  it and is not separately graded.

---

## 11. Owed

1. **The two ablation rows the design's §6 demands** — the corridor off
   (schedule and D3 kept), and the schedule off as well — on the *final*
   configuration. Early runs cannot substitute: the rules changed underneath
   them. Design §0 predicts most of the gain is in the schedule; that is a
   prediction with no measurement behind it.
2. **DTL hand marks through the swing**, via the panel that now exists, at
   least at P3, P5 and P7 on the dev six. Until they exist, gates 2b–2d cannot
   be graded at all and the mid-band claims rest on montages.
3. **A deterministic re-run** of one configuration (gate 6).
4. **A DTL ball on the second rig, or a second DTL-native still-club witness.**
   Until one exists, address and impact publish 0/9 there (§8A) — the two
   moments this view exists for. This is the most serious open defect.
5. **Adjudicate the 06-11 address-shoulder and impact-band publishes** (§8) and
   the three published frames the per-P table isolates: P2 on one swing, P4 on
   two.
6. **A lateral-proximity form of D1**, so the reverse-ray test means something
   in a view with no free space behind the butt.
7. **0005's P3 at 225°** — 18–20° off every other P3 measured here.
8. **`rhoSolveMin` per rig**, or a DTL-native measurement to replace the
   threshold (§8).
9. **The published length at P3.** On the re-rendered held-out montage the
   0013 P3 rails are on the shaft but stop short of it: θ 244°, `lenPx` 232,
   with the real shaft continuing toward the top-left corner. Placement is right
   and length is not; D3 is one-sided by design, so a short run is never
   refused, and nothing yet grades it.
10. **`montage_dtl.py` still reads `trace.jsonl` in track mode**, which in a
    `--dtl` run directory is the *face-on* trace. Its only effect today is the
    white truth-ray fallback drawn on tiles the truth file does not cover, and
    on a face-on trace that ray is in the wrong view. It should read
    `trace_dtl.jsonl`, or not fall back at all.

---

## 12. Provenance

All runs on 2026-09-20, macOS 27.0, `Marks-Mac-mini.local`, binary
`build/tools-parity-ninja/swinglab_run`, corpus at `/mnt/swingdata/corpus/swings`
(read-only). Capture host for all swings was `GOLFSIMPC`, app 0.1.10007, git
`bf4348d`, pose backend CUDA.

**Commands.** Per swing, with the club record injected because these swings carry
no `capture.club`:

```
swinglab_run <swing> --out <run> --dtl \
    --bands 308,362,560,758,808,854 --club-length-mm 940 --hosel-mm 882
```

(`runmeta.json` → `clubOverride` records the injection; `--dtl-pose` was used to
pin the DTL pose where a run was repeated.)

Truth:

```
tools/shaftlab/dtl_band_truth.py      # the instrument; no face-on input
tools/shaftlab/dtl_probe.py           # Stage 0
tools/swinglab/montage_dtl.py         # adjudication montages
```

**Run folders** (all under `build/dtl/`, none of them in the repo):

| folder | what |
|---|---|
| `fo/`, `baseline/` | face-on runs, and the `--face-on DTL` baseline of §3 |
| `c1/`, `c1b/`, `c2/`, `c3/`, `c4/` | the five dev-six configurations of §5 |
| `heldout/`, `heldout_truth/all/` | the held-out six and their band truth |
| `transfer/` | the nine 06-11 swings |
| `heldout_dtlonly/`, `transfer_dtlonly/` | the same DTL products with no face-on `result.json` beside them — see §13 |
| `control/`, `run_control.sh` | the 6-swing five-session pinned-pose face-on parity control |
| `grade/grade2.py` | throwaway grader; its truth directory is hard-wired to the dev six |

**Config hashes.** c2 `c58dd6f9ce…`, c3 `a3edfd9d8e…`, c4 / held-out / transfer
**`6d49771b0a9cf28c`**. c1 and c1b predate the hash.

**Montages** on `~/Desktop/DTL-shaft-tracker/`: `00_baseline_dev6`,
`01_stage0_probe_dev6`, `01b_band_truth_<swing>`,
`02_address_impact_adjudication`, `02_stage2_v0_dev6`, `03_stage3_iter1_dev6`,
`03_stage3_iter2_dev6`, `04_heldout6`, `05_transfer_0611`.

---

## 13. Three tooling errors, for the catalogue

All three belong in the research report's catalogue of errors and are recorded
here with their numbers. The first two are in the paper's Phase 14; the third
arrived after it was drafted.

**A review montage drew the rails at the wrong scale.** `montage_dtl.py` took
the DTL frame dimensions from `<dtl-run>/result.json`, and a `--dtl` run writes
the **face-on** result into that directory — 1280×1024, against a DTL stream of
512×1024. Normalised coordinates out of `club_dtl.json` were therefore scaled by
the face-on width, and the drawn rails sat about 120 px off the shaft while the
graded number for the same frames was 0.3°. The picture said one thing and the
number said another, and that disagreement is the only reason it was caught. The
tool now takes frame dimensions and the DTL pose from `club_dtl.json` and
`pose_dtl.json` only, and a selftest plants a face-on-sized `result.json` in the
DTL run directory and asserts the rails do not move. The same field also carried
the L̂_D ladder: on 0013 the face-on `result.json` offers `ballPx` −1 and
`posePx` 252 **in face-on pixels**, which the montage was taking as a DTL length.

Verified after the fix by re-rendering `04_heldout6` and `05_transfer_0611`, and
by rendering 0013 from `build/dtl/heldout_dtlonly/` — a run directory with no
`result.json` in it at all. **Every rail-bearing tile is byte-identical between
the two**; the 1,103 differing pixels of 5.1 M are header text and the white
truth-ray on the two END-ON tiles, which the DTL-only directory has no
`trace.jsonl` to draw.

**The same montage drew a prior the tracker had switched off.** The dashed
face-on corridor centres were rendered on every track tile, including the ones
where the corridor is **gated off** by `rhoFMax` — which is precisely P1 and P7,
where ρ_F is railed and equation (c) returns a near-vertical prediction. The
reviewer read those near-vertical dashes as the tracker's own output missing the
shaft. They were not the tracker's output; they were a prior the tracker had
already declined to use. The corridor is now drawn only where the frame's own
`corridor.on` is true (it is `null` on 353 of 407 frames on 0013 and an object
with `on` false on more), which on a pixel count comes to 0 corridor pixels at
P7 on all 21 swings and at P1 on 20 of 21 — the exception being a frame whose
data genuinely has the gate on. The lesson is the one the montage was built for:
**a diagnostic must draw what the algorithm did, not what it considered.**
Refusal reasons on END-ON, OCCLUDED and UNSEEN tiles are now rendered at caption
size under the tier stamp, wrapped to two lines, because on an absence tile the
reason *is* the result — and the ρ̂ in "end-on ρ̂=0.48" had been ASCII-replaced
into "end-on ??=0.48" for as long as the tool had existed.

**A feasibility note read a segment-derived number as evidence an engine had
run.** Design §2 recorded "the evidence engines transfer … E1 locked (`bandPx`
185)". `lengths.bandPx` is the length ladder's rung derived from the **segment**
lock's scale; E1 had produced zero locks, because the swing carries no club
record. A non-zero field named after a thing is not that thing having happened.

---

## 14. After the checkpoint — the ball from its shadow, and two closing rules (2026-09-20, evening)

> Appended after §§1–13 were written. Nothing above is edited. The section is
> numbered 14 because 12 and 13 were already taken; its title is the one the
> work was done under, "After the checkpoint".

Two packages landed after the checkpoint above, both against §11's item 4 — the
one §8A calls the most serious open defect. **C5** gives the DTL ball detector a
second cue, the ball's contact shadow. **C6** adds two closing rules: one named
club-away window with a fallback ladder, and a refusal of any published length
that is the ridge sweep's own floor.

**Read this first, because it bounds the whole section.** The held-out result of
§7 belongs to config hash `6d49771b0a9cf28c` and to that hash only. C5 and C6
were developed **with 06-11 in the loop**, by the owner's explicit decision, so
**06-11 is no longer a clean transfer set.** Every 06-11 number below is a
development number. The 07-04 twelve are the set that stayed out of the loop for
these two packages, and the only claim made from them is a null one.

| package | config hash | what changed |
|---|---|---|
| c4 / held-out / transfer (§§6–8) | `6d49771b0a9cf28c` | the frozen checkpoint |
| **C5** | `194731185cd3e119` | the shadow ball cue, and `shadowMatMin` / `shadowDrop` / `shadowLaunchRise` / `shadowToCentreR` |
| **C6** | `0fc7c3613ef16e0f` | `clubAwayWindowOf` with its fallbacks and its recorded name; `len.floorSlackPx` and the floor refusal |

The hash changed at each package, which is what makes "before" and "after" below
two different configurations rather than two runs of one.

---

### 14.1 The ball is white on white; its shadow is not

On 06-11 the mat under the ball is blown out and the ball has no edge on it. The
bright-blob cue is not failing on a visible ball — it is looking at a ball that
is not there to find. What **is** there, as Mark pointed out, is the small dark
crescent the ball casts at its own lower rim.

Measured while the cue was built, and recorded in
`src/Analysis/dtl_shaft_config.h:203-234` where each threshold's default is the
measurement:

| quantity | measured on the 06-11 nine | the threshold, and the gap it sits in |
|---|---|---|
| local 31×31 mat median at the crescent | **253** | `shadowMatMin` **200** — on 07-04 the mat under the ball is a dim 90–100, excluded by design |
| crescent grey level | **77–110** on a 253 mat, i.e. **143–176** below its own local median | `shadowDrop` **60**; the 99th percentile of the drop over the whole prior is 26–27 |
| crescent area / elongation | **51–79 px**, **2.4–3.2 : 1** | shape gates 12–400 px scaled by frame width, elongation ≤ 4 : 1 |
| **launch rise** at the true crescent | **+113 to +124** grey levels | `shadowLaunchRise` **40** |
| launch rise at the four static marks that pass the shape gates on the same swings | **−30 to +1** | — |

The discriminator is the last two rows and nothing else. A scuff, a tee hole or a
mat seam is still dark after the ball has gone; a ball's shadow is not. The
thresholds sit in the middle of those gaps rather than on their edges.

**When to look is not the address hold.** At address the clubhead and its own
shadow sit on the ball — the address median carries one dark mass spanning
x 440–600 where the crescent is (`src/Analysis/dtl_shaft_decide.cpp:381-384`).
The cue medians the **club-away** frames instead, the stretch where the club is
above the waist and what is left on the mat is the scene.

**Geometry, and it is small.** The crescent is the contact shadow at the ball's
lower rim, so the ball centre is one radius straight up from its centroid
(`shadowToCentreR` = 1). On the nine, the radius from the scene scale is
**6.20–7.25 px**, so the correction is under 8 px on a grip→ball distance of
310–353 px — 0.6° in θ_ball against a ±20° gate.

**Ordering, and why it is not a preference.** The bright cue is unchanged and
answers first; the shadow is consulted only where brightness found nothing. That
ordering is load-bearing, and the evidence for it is a negative result: on 07-04,
where the mat under the ball is dim, the shadow cue **alone** picks a dark patch
by the golfer's foot some 200 px from the ball. It is not a standalone detector
on a partly-lit mat, and it is not offered as one.

The shadow is nevertheless **measured on every swing**, including the twelve
where brightness wins, so a trace on a scene that carries both can put them side
by side. `summary.ball.nCandidates` is 1–2 on the 06-11 nine and 1–2 on the
07-04 twelve.

---

### 14.2 C6's first closing rule: one named club-away window, and the swing whose ladder is short

Two consumers want the same frames — the phase-aware clean plate's low region
(§5.4 of the design) and the shadow cue — so there is now **one** definition,
`clubAwayWindowOf` (`src/Analysis/dtl_shaft_decide.cpp:334-364`), and it comes
back with its own name attached in `summary.clubAwayWindow`.

The rule is P2 + 40 % of (P2→P4) → P5, with three fallbacks, each the nearest
thing the ladder still knows: no P5 ⇒ half way from the top to impact; no P7
either ⇒ P4 + 120 ms; no P2/P4 at all ⇒ 35–80 % of P1→impact.

This is not hypothetical. **06-11 swing_0003 has no P5 rung**, so the primary
window is empty and both consumers lost their source on the same swing for the
same reason. Under C5 the cue reported "only 0 club-away frames (need 5)" and the
ball was not found; under C6 the window falls back to **`P4P7`** and the ball is
found. `summary.clubAwayWindow` reads `P2P5` on the other eight 06-11 swings and
on all twelve 07-04 swings, and `P4P7` on 0003 alone.

A rung the face-on ladder did not name is a statement about the ladder, not a
statement that the club was never above the waist.

---

### 14.3 What C5 and C6 did to the 06-11 nine

Published-frame counts and the sighted fraction, per swing, all three
configurations. Sources: `build/dtl/transfer/<id>/club_dtl.json`,
`build/dtl/c5/<id>/club_dtl.json`, `build/dtl/c6/<id>/club_dtl.json`.

| swing | published c4 | published C5 | published C6 | sighted frac c4 | sighted frac C6 | ball C5 | ball C6 | window C6 |
|---|---|---|---|---|---|---|---|---|
| 0001 | 103 | 218 | 198 | 0.323 | 0.627 | shadow | shadow | P2P5 |
| 0002 | 79 | 188 | 181 | 0.350 | 0.650 | shadow | shadow | P2P5 |
| 0003 | 73 | **73** | **188** | 0.257 | 0.573 | **none** | shadow | **P4P7** |
| 0004 | 74 | 185 | 179 | 0.299 | 0.674 | shadow | shadow | P2P5 |
| 0005 | 78 | 182 | 176 | 0.308 | 0.675 | shadow | shadow | P2P5 |
| 0006 | 74 | 177 | 171 | 0.353 | 0.702 | shadow | shadow | P2P5 |
| 0007 | 76 | 183 | 173 | 0.305 | 0.668 | shadow | shadow | P2P5 |
| 0008 | 84 | 201 | 197 | 0.333 | 0.719 | shadow | shadow | P2P5 |
| 0009 | 75 | 177 | 170 | 0.260 | 0.568 | shadow | shadow | P2P5 |
| **total** | **716** | **1,584** | **1,633** | | | **8 / 9** | **9 / 9** | |

`publishedInEndOn` is **0** on all nine in all three configurations, counted not
asserted. `lFullSource` goes from `faceOnRowScale` on all nine (c4) to **`ball`**
on eight (C5) and **nine** (C6).

**The cross-check on the ball's placement.** The grip→crescent distance at the P1
tile, against the c4 run's independent L̂_D from the cross-view row scale:

| swing | grip→shadow px (C6) | L̂_D px, c4 `faceOnRowScale` | L̂_D px, C6 `ball` |
|---|---|---|---|
| 0001 | 322 | 364.0 | 322.9 |
| 0002 | 315 | 367.6 | 311.7 |
| 0003 | 319 | 380.2 | 326.3 |
| 0004 | 338 | 350.4 | 390.5 |
| 0005 | 315 | 334.9 | 373.2 |
| 0006 | **308** | 332.7 | 321.1 |
| 0007 | 316 | 331.6 | 322.8 |
| 0008 | 323 | 342.0 | 334.5 |
| 0009 | 314 | 325.0 | 320.5 |

**308–338 px** against an independent **325–380 px**. The grip→shadow distance is
**84–97 %** of the c4 row-scale length on every swing, never above it and never
absurdly below — which is the right shape, because the two are not the same
measurement: the grip→shadow distance runs from a wrist midpoint to the crescent
and L̂_D is a full club length. That is the same convention §5A already records
for the address angle. No swing puts the shadow somewhere the club could not
reach.

**The two L̂_D estimates themselves do not agree as well as that.** Ball-derived
against c4 row-scale, per swing: −11 %, −15 %, −14 %, **+11 %**, **+11 %**,
−3.5 %, −2.7 %, −2.2 %, −1.4 %. Four of nine agree within 4 % and five do not,
and the two positive outliers (0004, 0005) sit on the opposite side from the
three large negative ones (0001, 0002, 0003). Nothing here says which estimate is
right; both feed only the schedule's denominator. **Unadjudicated, and new —
under c4 there was one estimate and no way to notice.**

---

### 14.4 Where the ladder publishes on 06-11, per P position, before → after

The tile is the nearest DTL frame to each face-on ladder time, within one frame
interval (6.69–6.71 ms on these swings). Ladder from
`build/dtl/transfer/<id>/result.json` → `analysis.club.positions`; tiles from
each configuration's `club_dtl.json`.

**Before — c4 / transfer, config `6d49771b0a9cf28c`** (this is §8A's third
column, restated so the comparison is on one page):

| P | published | what |
|---|---|---|
| P1 | **0 / 9** | UNSEEN ×9, "no ball witness at address" |
| P2 | 1 / 9 | END-ON ×8; one forearm lock on 0002, θ 225° |
| P3 | 9 / 9 | 242–247° |
| P4 | 2 / 9 | END-ON ×6, UNSEEN ×1; **0001 at θ 210°** and **0007 at θ 292°**, both on a 98–100 px run |
| P5 | 8 / 8 | 239–243° (0003 has no P5 rung) |
| P6 | 0 / 8 | END-ON ×8 |
| P7 | **0 / 9** | UNSEEN ×7, END-ON ×2 |
| P8 / P10 | 0 / 9 | OCCLUDED / END-ON |

**After C6, config `0fc7c3613ef16e0f`**, per swing:

| swing | P1 | P2 | P3 | P4 | P5 | P6 | P7 |
|---|---|---|---|---|---|---|---|
| 0001 | **59.0°** | END-ON | 244.5° | **UNSEEN, floor** | 241.5° | END-ON | **67.0°** |
| 0002 | **58.5°** | **225.0°** | 244.0° | UNSEEN, reverse ray | 238.5° | END-ON | **64.5°** |
| 0003 | **59.5°** | END-ON | 247.5° | END-ON | — no rung | — | END-ON |
| 0004 | **60.0°** | END-ON | 246.0° | END-ON | 243.5° | END-ON | **68.5°** |
| 0005 | **59.5°** | END-ON | 244.0° | END-ON | 243.0° | END-ON | **66.0°** |
| 0006 | **59.5°** | END-ON | 243.0° | END-ON | 241.5° | END-ON | **67.0°** |
| 0007 | **59.5°** | END-ON | 243.0° | **UNSEEN, floor** | 242.0° | END-ON | **66.5°** |
| 0008 | **59.5°** | END-ON | 242.0° | END-ON | 240.0° | END-ON | **65.0°** |
| 0009 | **60.0°** | END-ON | 242.5° | END-ON | 241.5° | END-ON | END-ON |

Rolled up, and this is the whole point of the two packages:

| P | c4 | C5 | C6 |
|---|---|---|---|
| P1 address | **0 / 9** | 8 / 9, 58.5–60.0° | **9 / 9, 58.5–60.0°** |
| P2 | 1 / 9 (θ 225°) | 1 / 9 (θ 225°) | 1 / 9 (θ 225°) |
| P3 | 9 / 9, 242–247° | 9 / 9, 242.0–247.0° | 9 / 9, 242.0–247.5° |
| P4 | **2 / 9** (210°, 292°) | **2 / 9** (210°, 292°) | **0 / 9** |
| P5 | 8 / 8, 239–243° | 8 / 8, 238.5–243.5° | 8 / 8, 238.5–243.5° |
| P6 | 0 / 8 | 0 / 8 | 0 / 8 |
| P7 impact | **0 / 9** | 7 / 9, 64.5–68.5° | **7 / 9, 64.5–68.5°** |
| P8, P10 | 0 / 9 | 0 / 9 | 0 / 9 |

P1 goes 0 → 9 and P7 goes 0 → 7. The two P7 absences are 0003 and 0009, both
END-ON at ρ̂ 0.38 and 0.28 — refused by the schedule, not by the ball. The
P3 and P5 angles move by at most 0.5°, which is the grid, so the new ball did
not disturb the bands that were already publishing.

**The one published frame that should not be there is still there.** 0002's P2 at
θ 225° on a genuine 238 px run at ρ̂_D 0.52 — §8's confirmed forearm lock —
publishes under C6 exactly as it did under c4. Neither closing rule touches it:
its length is a real measurement and its ρ̂_D clears `rhoSolveMin`. The fix for
that one is `rhoSolveMin` per rig (§11 item 8), and it was not attempted.

---

### 14.5 07-04, C5: nothing moved, which is the result

The twelve 07-04 swings were re-run under C5 into `build/dtl/c5_0704/`. Every
ladder tile — tier, angle and refusal reason at all nine P positions on all
twelve swings — is **identical** to c4 / held-out, and the 425 truth-paired
frames are unchanged. `summary.ball.source` is `bright` on all twelve, as before.
The only differences in the whole file set are `summary.configHash` and, under
C6, the newly recorded `summary.clubAwayWindow`.

That is the claim the shadow cue needed and the only one it gets from a set that
stayed out of the loop: **on a scene the bright cue already solves, adding the
shadow cue changes nothing.**

---

### 14.6 C6's second closing rule: a length equal to the sweep's own floor is not a length

`ridgeSweep` searches its cumulative-score argmax only from `j0 = minLenPx /
rStep` onward, so its shortest possible terminus is `rLo + minLenPx` — **98 px**
on the shipped constants — and a ray that leaves the club early reports that
number to the digit. §5A already recorded this as "a floor wearing a length's
clothes". A frame published on it has had **no length measured**: the argmax is
parked on its own lower bound.

The rule (`src/Analysis/dtl_shaft_post.cpp:450-478`): a published length within
`len.floorSlackPx` = **14 px** of 98 px, **with `lenSrc` = `rend`**, is refused
with the reason `length is the sweep's floor (N px), not a measurement`. Two
exemptions, both stated: not against a `rend` figure that is genuinely a short
measured run when the snapped line offers a longer one, and never against a BAND
lock, which measures the line directly and owes the sweep nothing.

14 px is a tolerance on an equality — two ridge steps of rounding either side —
not a threshold on a length. It is not fitted: no value of it was searched.

**06-11.** 69 published frames under C5 are floor-class. Under C6, **67 frames
are refused with the floor reason** and **0 floor-class frames survive
publication**; the two not in the 67 are on swing_0003, whose whole track is
different under C6 because the ball is now found. Both false P4 tiles — 0001 at
θ 210°, 0007 at θ 292° — are in the refused set. Excluding 0003, 66 of 66
floor-class frames are gone.

**07-04.** **85 published frames of 2,279 (3.7 %) are refused**, and this is the
stated cost, on the set that is not in the loop:

| what | count |
|---|---|
| published, C5 | 2,279 |
| published, C6 | 2,194 |
| refused with the floor reason | **85** |
| floor-class frames surviving publication | **0** |
| ladder tiles changed at any P position | **0** |
| truth-paired frames changed | **0** — all 85 lie after the truth span |

Every one of the 85 is a per-swing loss of 5–11 frames, spread evenly:

| swing | 0004 | 0005 | 0006 | 0007 | 0008 | 0009 | 0010 | 0011 | 0012 | 0013 | 0014 | 0015 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| refused | 11 | 6 | 7 | 8 | 5 | 6 | 5 | 7 | 8 | 7 | 7 | 8 |

**What was lost with them, and it is not nothing.** Of the 85, **30 agree to
within 3° with the nearest surviving published frame** — these are right angles
thrown away because the length behind them was not earned. That is the trade,
and it is taken deliberately: the rule asks whether a run was measured at all,
which is a different question from the minimum-length rule's "is the run long
enough for the schedule", and the two false P4 tiles clear the minimum-length
rule precisely because ρ̂_D is small where they happen.

**Where the rest of the 85 are.** 51 of 85 lie in the late-backswing band
(`P2.2–P2.3 → P3.5–P3.8`), and 50 of the 85 carry θ ≥ 260°, running **262–308°**
on lengths of 98–112 px. This is the drifting tail at the end of the band, where
the ray has already left the club and the angle is walking. The other 34 split
**29 in the P4→P5 band** (θ 234–252°) and **5 in the impact band** (θ 56–62°).

The same shape on 06-11, excluding 0003: of 66 refused frames, **18 agree to
within 3°** with the nearest survivor, the angles span **58–312°**, and the
lengths span 98–112 px.

**The counted claim about the 85 that this section does not make.** Nothing here
grades the 85 against truth, because no truth covers them. "30 of 85 agree with a
neighbour" is a consistency count, not an accuracy one.

---

### 14.7 Still open after both packages

Stated as plainly as §11, and none of these was chased today.

1. **06-11 swing_0002's P2 forearm lock still publishes.** θ 225°, a real 238 px
   run, ρ̂_D 0.52. Neither closing rule can reach it. §11 item 8 —
   `rhoSolveMin` per rig, or a DTL-native measurement to replace the threshold —
   is now the only route to it.
2. **Address on 06-11 reads 4–6° above its own ball line on every swing.** Rig
   or a residue of the address leg pull. **There is no truth on this session to
   settle it,** and the 58.5–60.0° figures above should be read with that on
   them. The 07-04 held-out P1 tiles, which do have truth, read 54.0–55.0°.
3. **06-11 is no longer a clean transfer set.** Both packages were developed with
   it in the loop. The next transfer claim needs a session neither package has
   seen.
4. **The two L̂_D estimates disagree by −15 % to +11 %** across the nine
   (§14.3), and there is now no tie-breaker between them. Unadjudicated.
5. **The face-on parity control was not re-run after either package.**
   `build/dtl/control/` is dated 10:38, before both. What holds instead is
   structural and weaker: a `--dtl` run does not write `result.json` at all
   (§5A), and every line of both packages is inside
   `src/Analysis/dtl_shaft_{config,types}.h` and `dtl_shaft_{decide,post}.cpp`.
   Gate 5 of §9 was **not** re-measured at these two hashes, and this note does
   not claim it was.
   > **Re-measured afterwards, same evening, on the final binary** (all of C5,
   > C6 and the kinematic-sequence pair route built in): `run_control.sh … --dtl`
   > with `{"sequence.pairTrunk.enabled": false}` against `build/dtl/control` —
   > *6 compared, 6 pass, 0 diff, 0 unpaired*. With the pair route at its
   > default the three control swings that have no DTL stream pass and the three
   > that have one differ, and only under `analysis.kinematicSequence`, the two
   > trunk angular-speed series and `timings` (`kinematic_sequence_design.md`
   > §13). The agents' per-package control runs were reported as passing but
   > their output trees were deleted under the clean-up rule, which is why there
   > was nothing on disk to cite; a gate whose evidence is deleted on success
   > cannot be audited, and the run folder for a parity gate should be kept
   > until the note that cites it is written.
6. **`montage_dtl.py` still shows the fallback length.** On
   `~/Desktop/DTL-shaft-tracker/06_ball_shadow_0611_summary.md` the `L_D source`
   column reads "0.35*frame height" and `L_D` reads 358.4 on all nine, while the
   same runs' `club_dtl.json` carries `lFullSource: "ball"` and `lFullPx`
   311.7–390.5. The montage is not reading the tracker's own length. It is a
   fourth tooling error of the same family as §13's three — a diagnostic showing
   something other than what the algorithm used — and it is recorded here rather
   than fixed.
7. **Everything §11 already owed.** The two ablations, the DTL hand marks, the
   deterministic re-run, `montage_dtl.py`'s `trace.jsonl`, 0005's P3 at 225°,
   the published length at P3. None of them moved.

---

### 14.8 Provenance for this section

Runs on 2026-09-20 between 17:28 and 18:07, same host, binary and corpus as §12.
Same command line as §12, including the injected club record.

| folder | what | config hash |
|---|---|---|
| `build/dtl/c5/` | the 06-11 nine, shadow cue | `194731185cd3e119` |
| `build/dtl/c5_0704/` | the 07-04 twelve, shadow cue | `194731185cd3e119` |
| `build/dtl/c6/` | the 06-11 nine, both closing rules | `0fc7c3613ef16e0f` |
| `build/dtl/c6_0704/` | the 07-04 twelve, both closing rules | `0fc7c3613ef16e0f` |

Montage: `~/Desktop/DTL-shaft-tracker/06_ball_shadow_0611*`, whose per-swing
published/sighted column reproduces `build/dtl/c6`'s counts exactly.

Code, all uncommitted at the time of writing beyond `5203052a`:
`src/Analysis/dtl_shaft_config.h` (the four shadow scalars, `len.floorSlackPx`,
both added to `dtlConfigHash`), `dtl_shaft_types.h` (`clubAwayWindow` on the
track and on the solve state), `dtl_shaft_decide.cpp` (`clubAwayWindowOf`,
`shadowBallCue`, the two-cue `dtlFindBall`), `dtl_shaft_post.cpp` (the floor
refusal and its reason string).

**Two numbers in this section are not traceable to a file.** The mat's p50 of
231 and p90 of 253 under the ball, and the crescent's 19 × 7 px bounding box,
were measured during development and not retained; what the repository holds is
the local 31×31 median of 253 at the crescent and the 51–79 px area, both in
`dtl_shaft_config.h:203-211`. They are stated here as the working measurements
they were, and they should not be cited as results.
