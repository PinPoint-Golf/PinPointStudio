# The markerless tracker's fragility: two failure families and the fixes (30 Sept – 1 Oct 2026)

**Status:** diagnosis complete from the 29 Sept run trees alone; fixes built and gated on the studio
(§6, final round v11, 30 Sept). Uncommitted: Mark judges the numbers first. Follows `docs/research/markerless_club_track_investigation_20260929.md` §6–§7 (item 4).

## 1. The question

On the 21 ball-visible Medium swings of 9 and 15 Sept, ViTPose-B tracked 19 and ViTPose-L 17,
each breaking swings the other tracked, always the same way: the address shaft sideways
(P1 ≈ 185–210°) and the backswing lost. On 16 Sept, three High swings broke on B and tracked
on L. A pose difference of median 0.6 % of the frame moved a whole backswing onto another
structure. Why is the tracker that sensitive, and what makes it robust?

## 2. Evidence: two different families, read from the existing `result.json` trees

No new run was needed for the diagnosis. Everything below is in
`/mnt/swingdata/scratch/{ab-0916-02-s2, b-vs-l, pose-rerun-20260929, pose-rerun-L-20260929}`:
per-frame `club.samples` (θ, conf, flags, grip), `club.positions`, `phases`, `ball.samples`,
and the `pose2d` hand centroids.

### 2.1 Family A — 16 Sept Wrist_02 s2 on the B pose: the grip origin is off the club

| | B pose (broken) | L pose (clean) |
|---|---|---|
| lead-hand centroid at address (px) | (750, 556) = on the lead **wrist** | (696, 635) = on the grip |
| trail-hand centroid | (677, 656) | (677, 661) |
| grip origin (mean of hands) | (713, 606) | (687, 648): B is 42 px up the lead arm |
| A1 club length grip→ball | 374 px | 330 px |
| address θ, 0.9–1.3 s | 132° coasted; RAY-measured 132° at 1.18–1.23 s | 94° ball-anchored |
| P1 | 1.257 s, 132° | 1.432 s, 100° |
| 1.43–1.69 s | measured 177→252° | measured 100→144° |
| P3 / P4 | 341° @1.93 / 5° @2.00 | 261° @1.92 / 345° @2.20 |

The chain: (1) the B hand centroid puts the lead hand on the wrist, so the ray origin sits 42 px
up the arm; (2) inside the address collar (`i >= spanLo`) the RAY tier is admissible, and from that
origin the strongest line is origin→real hands, 132°, published as Measured; (3) `applyBallAnchor`
cannot correct it: its departure test compares the DP's own θ with θ_ball
(`ball_anchor.cpp:286-299`), so a wrong address θ reads as "already departed", and measured
samples are only corroborated; (4) P1 is sampled from that DP; (5) the takeaway then walks up the
**trail forearm** (206–252° = grip→trail elbow), which nothing vetoed (`frameEmission` vetoed the
lead forearm only, and `kBodyJoints` carries no elbows); (6) the trail arm and the shaft converge
at the top, so P4 lands early and coverage stays 0.69 = VALID.

On **both** models the trail hand also jumps ~85 px every ~80 ms through the address hold (pose
frames at 969, 1050, 1130, 1211, 1291, 1371, 1452 ms). The lerp turns each jump into an 8-frame
speed burst — the "fidget" the phase model's bridging / m3gate / no-return machinery exists to
fight — and on B it put the top 194 ms early.

### 2.2 Family B — 15 Sept Wrist_02 s1 on the L pose (and s2 on B): the phase model manufactured the address

The grips are identical on B and L to within 6 px through the whole takeaway and the ball
detector found one cluster at (666, 942) in both runs. Yet:

| | B (clean) | L (broken) |
|---|---|---|
| grip speed 1.53–1.75 s | 2.5–6 px/frame | 2.5–6 px/frame (`swSpd` = 8) |
| Takeaway (phase 1) | 1.51 s | **1.95 s = impact − 0.55 s**, the A3 near edge (`bsMinBeforeImpactUs`) |
| Top | 2.18 s | 2.33 s (downswing start) |
| evidence span start | ~1.41 s | ~1.85 s |
| P1 | 1.44 s, 91° | 1.49 s, **196°** |
| P2 / P3 | 176° / 246° | absent |
| A1 club length | 277 px | **407 px** (grip→ball is 280 px at P1) |
| samples 1.5–1.8 s | measured 111→162° | ball-anchored 100→71° while the hands travel 12 % of the frame |
| `club.addressBall` | found | none |

The early takeaway never exceeds `swSpd`, so the backswing run is not formed; the two-longest
ranking takes the downswing; the walk-back stalls in the top dwell; A3 pins the onset at
impact − 550 ms, which the code itself names "the manufactured-Address signature"
(`shaft_track_assembly.cpp`, the pin-gated reseed comment), and the reseed did not rescue it.
From there: no frame before 1.85 s has evidence, so the DP carries the mid-backswing θ (196°)
back over the whole address hold and P1 inherits it (P1's *time* was right: grip stillness found
1.49 s); `applyBallAnchor` then paints grip→ball over the real takeaway because `bs0` is late;
A1 measures the club over a "hold" that is mid-backswing (407 px) and seeds the length ladder with
it; `ImpactAnchorStage` searches for the ball near P1's head, so a sideways P1 means "no address
ball".

**Correction to the 29 Sept doc §5:** the "ball not found ⇔ broken" correlation is partly
circular. The ball *was* seen on these swings; the track broke through the phase model, and the
missing `addressBall` is a consequence of the sideways P1, not its cause. The rig may still hide
the ball on other 15 Sept swings, and the product gate on a missing ball at capture (a separate,
mechanical item) stands.

Not a format change: every corpus session is 1280×1024 at 149 fps, June to September. The ball
detector's `clubActivity` reads 0.0–0.3 throughout the takeaway on these swings, so it is not a
usable takeaway witness as it stands.

### 2.3 The structural reading

The tracker is one chain: hands → grip → speed runs (absolute px/frame thresholds) → phases →
evidence span → DP → tiers → ball anchor → P-positions → club length. Each stage trusts the one
before; the address hold is deliberately given no evidence; the two things that know where the
club is at address (the ball, and the steel-segment probe along grip→ball) run **after** the DP
and are accepted only if the DP already agrees; one forearm is vetoed, not both; and nothing checks
the result against an independent witness, so both families published with conf 0.55 and coverage
0.69–0.92, both VALID. Small pose differences flip thresholds because the design gives them
nothing to be checked against.

## 3. Experiments (Mac, one swing each, pose injected, `/mnt/swingdata/scratch/robust-20261001/`)

| Run | Change | Result |
|---|---|---|
| `E0_15L_ctrl` | 15 Sept W02 s1, L pose, as shipped | reproduces the studio: Takeaway 1.95 s, P1 196°, length 407 px |
| `E1b_15L_nospan` | `shaft.spanBound=false` (evidence over the whole window) | P1 100°, P2 180° @1.80, P3 240° @1.98; P4 still at the wrong top (2.33 s, 262°); length still 407 |
| `E3a_15L_swspd6` | `shaft.swSpd` 8 → 6 | **fully clean**: P1 100 / P2 176 / P3 244 / P4 284 @2.17, length 278, ball found, cov 0.95 |
| `E3b_15L_bsmin300` | `shaft.bsMinBeforeImpactUs` 550 → 300 ms | worse: Takeaway 2.18 s, P1 271°, chirality flipped — the rail is a correct symptom detector, not the cause |
| `E2_16B_leadfix` | 16 Sept W02 s2, B pose with the lead hand moved onto the trail hand | address still wrong, now 238° = the **trail forearm**; the origin is a contributor, the collar RAY trap and the unvetoed trail arm are the mechanism |
| `E4` (scan, no runs) | the 21-swing B/L set | of the 6 broken runs, 5 sit on the A3 pin and all 6 lack P2/P3; none of the 36 clean runs trip either flag; length ratio > 1.2 on 4 of the 6, 0 of 36 |

`--full-window` on the `--out` path does **not** reach the tracker (E1 as first run was a no-op);
`shaft.spanBound` through `--params` does.

## 4. What changed (all default-on, every key with a bit-identical OFF; `kShaftStageVersion` 4 → 5)

1. **The ball inside the DP — once the image has confirmed it** (`shaft.addr.ballWell`,
   `shaft.addr.decoyCheck`). A1 accepts any consistent lock below the ankle line and between the
   feet, and that is not always the ball the club rests behind: on 07-03 (untaped 7-iron, all six
   swings) grip→ball read 115–124° against marked addresses of ~97°, and on 15 Sept W01 s2 the
   cluster sat 200 px from the address ball. So the still collar frames' ridge evidence, computed
   anyway, votes first: a supported RAY-quality line more than 15° from θ_ball with less than half
   its evidence at θ_ball is a vote against; three such frames outnumbering the confirming ones two
   to one make the ball a **decoy** (no well, no A1 length, `diag.ballSuspect`); and the well is
   applied only when at least one frame **confirmed** the ball. Then every address-like frame on
   which the hands are still pays a soft penalty for states away from grip→ball, inside the
   emission, before the Viterbi; frames before the evidence span have a flat row, so the well alone
   decides them. The post-hoc paint in `applyBallAnchor` obeys the same trust: unconfirmed, it
   painted the 07-03 hold at 124° (the old code was spared only because its departure test read
   the DP's 94° as "already departed"). With no vote either way the DP's own answer stands, as
   before.
2. **Both forearms vetoed** (`shaft.addr.trailArmVeto`): the trail elbow is plumbed beside the lead
   one and `φ_trail + 180°` pays `wArm` like the C4 lead veto — through the top only. After the
   top the trail elbow is often behind the body and its angle is noise; applied there it cost
   15 Sept W01 s8 (B) 66 of its 119 measured through-swing frames and `lowPointAhead`.
3. **The phase model checks itself and retries** (`shaft.phase.*`, `segmentPhasesChecked`): a
   model whose onset came off the A3 near rail, or with a backswing under 400 ms, or with no
   motion run at all (a non-swing), is *suspect*; it is re-segmented at `swSpd × 0.75`, twice at
   most, and the first non-suspect model wins. `PhaseModel::onsetRule` records which rule produced
   the onset. "No run starts before the top" was tried as a third signature and dropped: after the
   top repair and the reseed have done their job the run list legitimately has no pre-top run
   (06-11 s1, taped, clean), so it flagged sane swings and the retry moved their finish 55 frames.
4. **A cleaned hand track — as a second attempt** (`shaft.hands.*`, `shaft_hand_clean.h`). The
   tracker runs on the RAW hands first; only a track that comes back invalid or refused is tried
   again on the cleaned hands, and the second track is taken only if it is valid. Every swing the
   raw pose tracks is therefore bit-identical to before (the corpus: 68 of 68 sit on their base
   models), while 16 Sept B is rescued by the retry. This was the last of three bounds found by the
   gate: applied to every swing, the cleaning also moved the onset and P1 of clean 09-09 swings
   whose poses flicker just as much (13 jumps of 67–95 px on s7; the onset heuristics were tuned on
   that flicker, and P1 against the marks went from 40 ms p50 to 74 ms). The cleaning itself,
   applied to a copy of the pose (the persisted pose is untouched): The pair rule: on a run of at least four consecutive
   frames where both hands are still and the pair is wider than one lead-forearm length (64 px
   when no forearm is confident), the hand at the forearm's grip point becomes the grip — two
   hands on a grip sit within ~0.65 forearms (16 Sept L: 78 px on 123, clean), a centroid on the
   wrist ~1.8 (16 Sept B: 123 px on 69, the whole hold). Mid-swing pairs and isolated flickers are
   left as they were: the tracker has always read the midpoint there, and switching the grip
   between the midpoint and one hand on single frames put a 50 px step into the grip track and
   broke a sane model (06-11 s2). The glitch rule: a one-frame excursion over 40 px that returns,
   from a hand resting before and after it, is replaced — but only **before impact**: the address
   flicker and the backswing flaps (16 Sept B needs both; without the backswing ones its model
   collapsed to a 1.67 s top), never the finish hold, whose flaps are the phase model's last motion
   run (smoothing them moved the finish start 50 frames on the taped 06-11 swings). Hands
   inconsistent on more than half the frames refuse the track (`handsUnusable`): 15 Sept W01 s5,
   a non-swing, had 223 of 234. Three rounds of the studio gate were needed to find these bounds;
   each is recorded in §6.5.
5. **The ball as a timing witness even when untrusted** (`ShaftBallSeen`, flag 0x400). The address
   walk-back and EventRefine corroborate P1 on the frames the ball was seen; the old paint set that
   on every accepted ball, and gating the paint on trust silently removed it — 09-09 s5's P1 went to
   0.38 s and four address-time body metrics went with it. The seen-flag is now set for every
   accepted, non-decoy ball; θ is painted only for a trusted one.
6. **P1 follows the anchored samples** (`ShaftTracker::track`): a track-sampled P1 whose nearest
   sample the ball anchor moved now reads that sample, so P1 and the samples agree.
7. **Refusal** (`shaft.addr.refuse`): a P1 that rests on no measurement yet sits > 25° from a
   trusted ball, or the A1 length off the P1 grip→ball distance by > 30 %, or a still-suspect
   phase model without P2/P3, or unusable hands ⇒ `valid=false` with `analysis.club.refused`
   naming the reason (`p1BallConflict`, `lengthConflict`, `phaseSuspect`, `handsUnusable`). A
   *measured* P1 that disagrees with the ball flags the ball instead (`diag.ballSuspect`). A refused track keeps its samples for the lab,
   draws nothing (`PpCameraFrame.qml` gates synth/predicted/positions on `club.valid`), shows "-"
   for every club metric, and carries the card ⚠ with the reason (`dataWarningDetail.clubRefused`).
   It does **not** withhold the body/wrist rows from the session assessment; only the two
   recording-integrity facts do that.
8. **Diagnostics persisted** as `analysis.club.diag` (`onsetRule`, `phaseRetries`,
   `phaseSuspect`, `p1BallDeltaDeg`, `lenBallRatio`, `onsetTUs`, `topTUs`, `handPairFixed`,
   `handGlitchFixed`) and in the trace (`t_us`, `grip`, `phi`, `phi_trail`, `theta_ball`,
   `theta_final` per frame; the summary carries the onset rule, suspect mask, retries, the accepted
   ball, the P1 frame and both checks). `tools/shaftlab/robust_scan.py` reads them over a run
   root; `tools/shaftlab/robust_compare.py` diffs two roots swing by swing.

## 5. The two exemplars after the change (Mac, same injected poses)

| Run | Before | After |
|---|---|---|
| 15 Sept W02 s1, L | Takeaway 1.95 s (A3 pin), P1 196°, no P2/P3, length 407 | retry 1 → Takeaway 1.50 s, P1 100°, P2 176°, P3 244°, P4 284° @2.17, length 278 (ratio 0.99), cov 0.95, 44 metrics |
| 15 Sept W02 s1, B | clean | clean; 8 pairs / 3 glitches cleaned, cov 0.92 → 0.95 |
| 16 Sept W02 s2, B | P1 132° @1.26, P3 341°, P4 5° @2.00, cov 0.69 | 76 pairs / 26 glitches cleaned → P1 93° @1.46, P2 178°, P3 265°, P4 334° @2.20, cov 0.88, 44 metrics |
| 16 Sept W02 s2, L | clean | clean; P4 345° @2.20 unchanged, cov 0.96 → 0.94 (13 pairs cleaned) |

| 15 Sept W01 s2 (studio pose) | clean, but A1 had accepted a decoy 200 px from the ball | decoy 5 votes to 0 → ball dropped, P1 99°, valid, `ballSuspect` |
| 15 Sept W01 s5 (non-swing) | P1 187° at t = 0, VALID, 27 metrics | refused `handsUnusable` (223 of 234 frames) |
| 06-11 s1 (taped, pinned pose) | clean | clean; 3 pairs / 10 glitches cleaned, no retry, P4 333° @3.21 (base 330° @3.11) |
| 07-03 s3 (untaped, pinned pose, hand marks) | P1 5.0° off the mark | ball unconfirmed (0 votes) → no well, no paint: P1 **1.3°** off; with the well forced on it read 24° off |

The B-pose 16 Sept swing, which every code version since 17 Sept had tracked identically wrong,
now agrees with the L run to within 7° at P1 and 11° at P4.

## 6. Gate (studio, Release exe `swinglab_run_v11.exe` built from this tree, `/mnt/swingdata/scratch/robust-20261001/`)

Fresh runs on the studio (pose recomputed there, ViTPose-B for Medium, -L for High, as the
documents say), judged per swing with `robust_scan.py` and `robust_compare.py` against the
29 Sept runs of the same swings (`scratch/b-vs-l/out_{B,L}`, `scratch/pose-rerun-L-20260929`)
and, for the pinned corpus, against the 29 Sept exe run on the same pinned poses
(`corpus_base`). Eleven binaries were gated on the Mac exemplars and four rounds on the studio
(§6.5); the tables are for the final one.

### 6.1 The 21 ball-visible Medium swings, both models (`bl11_out_{B,L}`)

| | 29 Sept | v11 |
|---|---|---|
| clean on B | 19 / 21 | **21 / 21** |
| clean on L | 17 / 21 | **21 / 21** |
| refused | – | 0 |
| A3 pin / missing P2·P3 | 5 / 6 of the broken | 0 / 0 |
| runs changed at all | – | **the 6 broken ones only** (36 bit-identical or within 1°) |

Each broken run gained P2/P3 and 4 metrics (40 → 44) and its P1 moved 91–108° back onto the
club; every clean run kept its positions to ≤ 1° and its metric count (no loss, no coverage
drop). The 9 Sept hand marks read exactly as on 29 Sept on both models (B 2.1° / 4.9°,
L 2.3° / 4.6° p50 / p90): the ladder never touched those swings.

### 6.2 The library sessions, fresh pose (`lib11_out`: 9 Sept 7, 15 Sept 26, 16 Sept 7)

| | result |
|---|---|
| 9 Sept 6-iron (7) | 7 / 7 clean |
| 15 Sept W01 (13) | 11 / 11 real swings clean; the two non-swings refused `handsUnusable` (s5: 223 of 234 frames inconsistent; s12) |
| 15 Sept W02 (13) | **13 / 13 clean** (29 Sept: 3 / 13) |
| 16 Sept High on L (7) | 7 / 7 clean, metric counts identical to the 29 Sept L re-analysis |

15 Sept W01 s2 keeps its track (P1 99°) with the decoy ball dropped (5 votes to 0).

### 6.3 The pinned corpus, 68 swings on `corpus/pose3`, 29 Sept exe vs v11 (`corpus_base`, `corpus_v11`)

| | base → v11 |
|---|---|
| metric count | identical on **68 / 68** |
| coverage | identical on 68 / 68 (to 0.02) |
| refused / phase retried / hands retried | 0 / 0 / 0 |
| band-lock yardstick, 16 taped runs (θ vs band p50 / p90) | not worse on 16 / 16 (three p90 tails 1.6–3.2° → 0.5°) |
| P1 instant vs the marks, 17 marked swings | identical: p50 40 ms, p90 181 ms, max 328 ms |
| θ bit-identical over the whole track | 20 / 68; the other 48 differ only on address-hold frames |

The address-hold differences are the trust rule: the old code painted grip→ball over every
coasted pre-departure frame whenever the detector had *a* ball, whether or not it was the one the
club rested behind (07-03: the marked P1 error 7.2° → 3.4° once that paint stops); v11 paints
only a confirmed ball. Two swings changed more than that, both explained: 06-11 s4's ball had
been **rejected by A1** yet was painted (200 frames) and EventRefine's at-ball test leaned on that
paint, so with no paint its P1 instant lands 442 ms later (no marks on 06-11 to arbitrate); 07-05
s8's ball is confirmed 12 votes to 2, so its P1 now reads the ball direction (26.6° from the DP's
clamp) where the old departure quirk had painted one frame.

Tables: `docs/research/data/markerless/robust_20261001/` (`corpus_compare_base_v11.md`, the
truth-score reports). Run trees kept on the share for Mark's review: `corpus_base`, `corpus_v11`,
`bl11_out_{B,L}`, `lib11_out`, `in/`; the superseded rounds were deleted.

### 6.4 What the marked sessions say (`segment_truth_score.py`, seen marks P1–P6)

| session / model | 29 Sept θ p50 / p90 | v11 | P1 mark |
|---|---|---|---|
| 9 Sept fresh pose, B (42 marks) | 2.1° / 4.9° | 2.1° / 4.9° | 9.3° → 9.3° |
| 9 Sept fresh pose, L | 2.3° / 4.6° | 2.3° / 4.6° | 2.3° → 2.3° |
| 9 Sept pinned pose (corpus) | 2.0° / 4.5° | 2.0° / 4.5° | 4.3° → 4.3° |
| 07-03 pinned pose, untaped 7-iron (60 marks) | 3.6° / 15.0° | **3.4° / 13.2°** | 7.2° → **3.4°** |

The §5.2 markerless gate (p50 ≤ 2°, p90 ≤ 6°) holds as before. The 9 Sept address on B stays
9.3° off its mark: the ball there is never confirmed by the collar (bare steel on a lit mat gives
no supported line), so the well does not apply and the DP's own address stands, as it did.

### 6.5 The gate rounds and what each one taught

| round | change under test | what the gate found |
|---|---|---|
| v5 | well + trail veto + retry + fixed-64 px hand cleaning everywhere | fixed both families; but "no run before top" flagged clean taped swings and the retry moved their finish 55 frames; 64 px was inside a normal pair at the 16 Sept framing; 15 Sept W01 s2 refused on a decoy ball |
| v8 | forearm-relative, still-only pair rule; signature without that bit; decoy check + trust gate | corpus: hands cleaned on 67 of 68, finish start earlier on most (finish-hold flaps smoothed), 09-09 s5 P1 at 0.38 s (the unconfirmed ball no longer painted the timing flags) |
| v9 | run-gated pair rule, rest-gated glitch rule limited to before impact, ball-seen timing flag | yardsticks all better; but P1 against the marks p50 40 → 74 ms and one swing 600 ms early: de-flickering the 09-09 hands moved the onset the heuristics were tuned on |
| v10 | trail veto through the top only | 15 Sept W01 s8's through-swing back (66 frames, `lowPointAhead`) |
| v11 | the hands ladder: raw first, cleaned only on failure | every raw-tracking swing bit-identical; 16 Sept B still rescued |

The lesson across the rounds is the one the diagnosis started from: the phase model's heuristics
are tuned to the pose's own noise, so a cleaner signal is not a safer one unless the heuristics
are retuned with it. The ladder keeps the cleaning where it is the only way to a track at all.

## 7. Reproducing

- Mac single-swing runs: `build/tools-parity-ninja/swinglab_run <swingDir> --pose <pose.json>
  [--params <json>] --trace --out <dir>`; poses in `robust-20261001/in/` were lifted from the
  studio runs' `result.json` `pose2d` blocks.
- Studio: `C:\Users\developer\wedge-patches\{build_robust2.cmd, run_bl.ps1, run_lib.ps1,
  run_corpus.ps1}`; the exe is `…Release\swinglab_run_v5.exe` beside `swinglab_run_base.exe`
  (the 29 Sept build with the ViTPose-L path fix). The build copies the 18 changed files into the
  tree, builds, and restores them; no git state on the studio is touched.
- Scans: `tools/shaftlab/robust_scan.py <root>...` and `tools/shaftlab/robust_compare.py
  <base> <new> --md <out>`.
