# skeleton3d — the club's depth branch where the down-the-line camera is blind

*2026-09-28. Status: **APPROVED, being built.** It was briefly deferred when the 3-D panel was cut
to end at P8 (`swing_3d_annotations_design.md` §11), then reinstated the same day: "get a plan to fix
the real issue". Mark's answers are in §8. The P8 cut-off in the panel stays until he decides.
It follows `swing_3d_viz_design.md` (the fit; §12 is what was built) and
`swing_3d_annotations_design.md` §10 (where Mark found it).*

## 0. Summary

In the 3-D swing panel, seen from down the line, the club veers far off its plane through the
follow-through, and sometimes at the top (Mark, 28 Sept: "this looks like a bug rather than a
measurement error"). It is a bug in the fit. **Wherever the DTL camera does not see the club, the
fit is free to choose between two mirror-image clubs that look the same to the face-on camera, and
after impact it picks the wrong one almost every time.** The fix gives the fit the one piece of
knowledge that tells them apart — a club stays near its swing plane — and searches the other branch
explicitly, because the two are separated by a barrier that a local solver cannot cross.

## 1. Deliverables and definition of done

| Deliverable | What |
|---|---|
| **Document** | This file. After the build it gains a "Built and graded" section with the numbers of §6 |
| **Implementation** | `src/Analysis/skeleton3d/skeleton3d_fit.{h,cpp}` (the blind mask, the plane term, the branch pass, the flags); `skeleton3d_json.h` (tuning keys, the flag, the diagnostics); `analysis_versions.h` (`kSkeleton3DStageVersion` 1 → 2); `src/Analysis/tests/skeleton3d_test.cpp` (a blind follow-through case); `tools/swinglab/skeleton3d_run.sh` + `skeleton3d_grade.py` (the out-of-plane rows and the DTL-dropout config) |

**Done when:**
- The synthetic tests of §6.1 pass.
- The corpus table of §6.2 is filled for the 24 two-camera swings and judged by Mark. An honestly
  written-up miss counts as done; a skipped table does not.
- The 15 library swings of 4 July have been re-analysed through the reuse path (no `--full-window`).

## 2. What is wrong — the evidence (15 library swings, 4 July)

**The shaft is not a free unknown in the fit.** It is the lead hand's orientation times one fixed,
hand-local grip axis (`shaftDir = rot[leadHand] · gripAxis`). The shaft's direction is therefore
the lead wrist, forearm and elbow's joint state, and a wrong club is a wrong lead arm.

**What pins that direction, frame by frame:**
- The face-on shaft image angle (σ 3°) and, where it was measured, the face-on clubhead.
- The DTL shaft image angle (σ 4°) and clubhead, **only on frames the DTL tracker published** (tier
  RAY/SEG/BAND).
- "Both hands on one club", **only from address − 50 ms to impact + 60 ms**.
- Weak neutral priors on the wrist (30°) and pronation (45°), and smoothness.

**The ambiguity.** The face-on camera fixes the shaft's image angle and, through the clubhead, how
foreshortened the shaft looks. It cannot tell a shaft tilted *toward* it from one tilted *away* by
the same amount. Reflecting the fitted shaft through the face-on line of sight gives its mirror: the
same face-on picture, the opposite depth. Only the DTL camera separates the two.

**Which branch the fit takes**, against the fused downswing plane (club3d `planes.down`):

| Frames (Address → Finish) | DTL tracker | Fitted shaft, \|out of plane\| median | Its mirror | Mirror nearer the plane |
|---|---|---|---|---|
| Backswing | sees | 6.5° | 42.0° | 78 / 325 |
| Backswing | **blind** | 13.9° | 34.0° | 13 / 450 |
| Downswing | sees | 3.6° | 51.9° | 53 / 285 |
| Downswing | **blind** | 6.1° | 28.4° | 13 / 332 |
| Through | sees | 9.2° | 12.6° | 72 / 141 |
| **Through** | **blind** | **60.8°** (p90 70.9°) | **15.9°** | **420 / 437** |

Swing 5 at P8: the fitted shaft is +65° out of plane, pointing *toward* the face-on camera
(component −0.86 along the line of sight); its mirror is −27°, pointing away. In a follow-through
the club wraps round behind the golfer, away from that camera. The fitted shaft's face-on image
stays within 5–16° of the tracker throughout; the error is entirely in depth.

**Why the fit lands there.** After impact + 60 ms nothing couples the club to the trail hand, and
DTL is blind from the release to the finish on most swings. The branch is then decided by the
neutral wrist and pronation priors and by continuity from impact, and neither knows where a golf
club goes. The two branches are separated by a barrier: between them the shaft passes through
"square to the line of sight", where its face-on image is at full length and the measured face-on
clubhead is far off. Levenberg–Marquardt stays in whichever basin it started in.

**Not the cause:**
- The 3-D panel's 20 ms display stabiliser moves the shaft at most 7.5°.
- The top of the backswing is a separate, weaker case. There the fitted shaft *is* the nearer
  branch (13 of 450 blind backswing frames favour the mirror), but it drifts up to ~30° off plane
  on some swings, because depth is unobserved and nothing holds it. §3.1 covers that as well.

## 3. The fix

### 3.1 A swing-plane term where the DTL is blind

A new residual, **r_plane**, applies only on DTL-blind frames. It compares the fitted shaft
direction's out-of-plane angle against a reference plane:

`r = asin(shaftDir · n) / σ_plane`, with σ_plane = **15°** and the fit's Cauchy tail (so a genuinely
off-plane position, laid off or across at the top, costs a bounded amount, not a quadratic one).

- **The reference planes come from the fit itself**, not from club3d. `club3d` expresses its normal
  in a face-on camera frame it assumes level, while the fit pitches and rolls that camera. Taking the
  normal from club3d would couple the term to the camera unknowns, or misplace the plane by the pitch.
  Instead:
  - the **back plane** is the least-squares plane of the fitted shaft directions on DTL-seen frames
    from address to top;
  - the **down plane** is the same over top to impact.
  - Both are recomputed between solver rounds and held fixed within one. They are used when they
    rest on ≥ 8 frames and fit within 5° rms: club3d's own `planeOopMaxDeg` rule, since a plane
    scattered wider than that is not a plane.
- **Which plane a frame uses:** the back plane up to the top, the down plane from the top to the
  finish. The follow-through has no plane of its own that we measure. The delivery plane is the one
  the club stays near through P8, and the Cauchy tail lets the finish leave it.
- **"Blind"** means the frame has no DTL shaft angle *and* no DTL clubhead observation. On a swing
  with no DTL every frame is blind, and the plane is the catalogue one (§3.1a).
- **The span** is the whole blind span, address → finish (Mark, §8 q1). The finish frames are
  held too, so that junk past P8 cannot drag the P8 pose through the smoothness coupling.

### 3.1a The catalogue plane (Mark, §8 q2)

The catalogue plane is used on face-on-only swings, and on any DTL swing whose self-plane is refused
(thin or scattered).

- **Inclination** comes from the club's **length** (the club record, `FitInput::clubLengthM`), not
  its name: the corpus club labels are unreliable. `clubPlaneInclDeg(lengthM)` in
  `club_plane_catalogue.h` interpolates between the two measured points (7-iron 0.94 m → 60.3°,
  wedge 0.89 m → 62.8°) and typical values for longer clubs (≈ 56° at 1.02 m, ≈ 50° at 1.12 m).
  Outside the measured pair the value is flagged `catalogueUncalibrated`.
- **Orientation** is the golfer's own stance:
  - the plane contains the stance axis `e_t` (trail heel → lead heel, from the foot anchors);
  - it rises toward the golfer, on the side `e_g` = the horizontal from the address clubhead toward
    the mid-heels, ⟂ `e_t` (the clubhead is always in front of the golfer);
  - so `n = e_t × (cos α·e_g + sin α·ẑ)`.

  It is recomputed between solver rounds, because the anchors are unknowns.
- **Honesty:** the heading is the stance, not the target line, since the cameras are uncalibrated.
  A face-on-only swing stays **face-on-only in the panel** whatever this does. The term only keeps
  the fitted club, and so the fitted lead arm, out of the wrong mirror.

### 3.2 A branch pass, like the label-swap pass

The plane term alone cannot cross the barrier of §2, so the other branch is tried explicitly, the way
`swapPass` already tries a mirrored keypoint labelling:

1. After stage 2 has converged (with the plane term on), find each maximal run of DTL-blind frames.
2. **Seed the mirror branch.** For each frame in the run, the target direction is the mirror,
   `u' = u − 2(u·r̂)r̂` (r̂ = the face-on line of sight through the shaft's midpoint), and the club is
   pulled onto it. A temporary tight direction term (σ 2°) on those frames runs a few solver
   iterations, moving the lead wrist, forearm and elbow through the anatomy with every joint limit
   still active.
3. **Relax.** The temporary term is dropped and the solve continues with the true objective,
   including r_plane.
4. **Keep the cheaper branch, per run.** The branch whose true objective, summed over the run's
   frames plus a 3-frame margin each side for the smoothness coupling, is lower wins. The winning
   frames' states are assembled, then one final polish. A kept flip is flagged `shaftBranch` on
   its frames.

Only runs whose mirror is nearer the plane by more than 10° are seeded. That keeps the pass cheap:
from §2 that is the blind follow-through on nearly every swing, and a handful of backswing frames.

### 3.3 What it changes, and what it doesn't
- It changes the lead arm, wrist and hand on DTL-blind frames, and therefore the club.
  Everything else should move by at most the smoothness coupling at the run edges, and §6.2 checks
  that.
- `skeleton3d` feeds no metric (panel design §3.7), so no metric changes. The kinematic sequence's
  club node reads club3d, not this.
- A HackMotion or IMU on the lead forearm, where worn, already fixes the depth branch. The plane
  term then costs almost nothing, and the grade reports it.

### 3.4 Tuning keys (`skeleton3d.*`)
`usePlane` (true), `planeSigmaDeg` (15), `planeMinFrames` (8), `planeMaxRmsDeg` (5), `branchPass`
(true), `branchSeedDeg` (10), `branchSeedSigmaDeg` (2). The grade's dropout run uses the test hook
`debugDropDtlShaftAfterUs` (§6.2).

### 3.5 Persistence
- `pinpoint.skeleton3d/1` is unchanged in shape. It gains the frame flag `FlagShaftBranch` (0x08)
  and `diagnostics.plane`: the two planes' normals, frame counts and rms, `nPlaneFrames`,
  `nBranchRuns`, `nBranchKept`, `planeFree`.
- `kSkeleton3DStageVersion` goes 1 → 2, so a re-analysis refreshes it. The library write-back runs
  through the reuse path.

## 4. Why not the alternatives
- **Fix it only in the display** (flip the drawn shaft). The fitted arms would still be posed for the
  wrong club, and `analysis.skeleton3d` would stay wrong for anything that reads it later.
- **Take club3d's plane as the reference.** Different frame conventions (§3.1). club3d is also the
  grade's independent reference, and using it as an input would grade the fit against its own prior.
- **A stronger smoothness prior through the release.** It shaves the real ~2000 °/s arm and club
  rates the kinematic sequence reads (panel design §3.3), and it would not choose a branch anyway.
- **The plane term without the branch pass.** The barrier of §2 keeps the solver in the wrong basin;
  §6.1 (c) is there to show this.
- **Keep the trail-hand coupling after impact + 60 ms.** The trail hand genuinely leaves the club in
  the finish (the reason the window exists), and a coupled trail hand has the same face-on ambiguity.

## 5. Risks
- **A genuinely off-plane position** (laid off or across at the top, a "chicken wing" follow-through)
  is pulled toward the plane. σ 15° with a Cauchy tail bounds the pull, and §6.2 reports how far
  DTL-seen off-plane frames move when their DTL is hidden.
- **A bad reference plane.** It rests on the fit's own seen frames, and it is refused when scattered
  (> 5° rms) or thin (< 8 frames).
- **Solve time.** The branch pass adds roughly one stage-2 solve at worst (today 2.0 s median on the
  Mac). It is measured, not assumed.
- **GOLFSIMPC** runs the solver unoptimised in Debug (panel design §12.5): the pass will be slower
  there.

## 6. Verification

### 6.1 Synthetic (`skeleton3d_test`)
The existing generator swing, with the DTL shaft and clubhead **removed from impact + 60 ms to the
finish**:
- **(a) Recovery from the truth.** The fit keeps the true branch. Shaft direction p90 over the
  blind frames ≤ 8°.
- **(b) Recovery from the mirror.** Seeded on the mirror branch (the test hook `debugInitTheta`),
  the branch pass returns it to the true branch, and the run is flagged `shaftBranch`.
- **(c) Ablations:**
  - plane term off and branch pass off: the mirror start stays mirrored;
  - plane term on, branch pass off: still mirrored (the barrier);
  - both on: recovered.
- **(d) A truly off-plane follow-through.** The generator's through-swing is tilted 25° off the
  down plane. The fit's error on those frames is reported: this measures the prior's pull.
- **(e) Parity.** With the DTL seeing everything, the result is unchanged (no blind frames, no term).

### 6.2 Corpus (24 two-camera swings; judged per swing, by count)

| Check | Against | Pass |
|---|---|---|
| \|Out of plane\| on DTL-blind through frames | club3d `planes.down` | median ≤ 20° (from 60.8°) |
| Mirror-nearer frames, blind through | — | ≤ 10 % (from 420 / 437) |
| **DTL dropout** (config `dtlDropThrough`: the DTL shaft and head hidden from impact + 60 ms on the frames where DTL *did* see the club) | the full fit's shaft on those frames, **and** the DTL tracker's angle | median ≤ 10°; before the fix, reported |
| DTL tracker angle, DTL-seen frames | `clubDtl` | not worse (the 3.539 s, −30.6° kind of disagreement should shrink) |
| Face-on shaft angle and clubhead, all frames | `club`, `club.synth` | not worse than 1° / 2 px median |
| Every other joint, DTL-blind frames | v1 fit | per-joint shift reported; the lead arm's is the change |
| Solve time | v1 | reported |
| 3-D annotations reprojection (`swing3d_trace_reproject.py`) | the video | clubhead rows reported beside §9.2 / §10.1 of the annotations design |

The **DTL-dropout** row is the honest test. It is the only place we have ground truth for what the
fit does when DTL is blind: frames where DTL did see the club, hidden from the fit, and graded
against what DTL saw.

### 6.3 UI
The same P4 and P8 grabs from down the line (swing 5 and Mark's two screenshots' swing) before and
after, for Mark.

## 7. Build order

| Phase | Deliverable | Done when |
|---|---|---|
| 1 | The blind mask, the self-planes, r_plane + its exact Jacobian (checked by the debug Jacobian pass); synthetic (a), (d), (e) | Those tests pass; the Jacobian check < 1e-6 |
| 2 | The branch pass and the flags; synthetic (b), (c) | (b) recovers; (c) shows the barrier |
| 3 | Tuning keys, diagnostics, version bump; `skeleton3d_run.sh` config `dtlDropThrough`; grade rows | The §6.2 table filled on the 24; Mark judges it |
| 4 | Library re-analysis of the 15 swings of 4 July (reuse path); the grabs | The panel shows the club on plane through P8 |

## 8. Questions for Mark — answered 28 Sept
1. **σ_plane 15° with a Cauchy tail.** It is generous enough for a laid-off or across top. Do you want
   the follow-through held to the *delivery* plane (as designed), or left freer after P8, e.g. the
   term only up to P8 and the finish unconstrained? **Answer: the whole blind span, address → finish.
   The grade is judged to P8, with the finish reported separately.**
2. **Face-on-only swings** stay unchanged in v2 (no seen frames, no plane). A catalogue plane for the
   club, oriented by the stance, could stand in later. Agreed to leave it? **Answer: use a catalogue
   plane (§3.1a).**
3. **The panel's P8 cut-off** stays after the fix. Mark decides later.

## 9. Built and graded (28 September 2026)

### 9.1 What was built

| Piece | Where |
|---|---|
| The blind mask, the two self-planes and the catalogue plane (recomputed between solves), r_plane with its exact Jacobian, the seed term | `skeleton3d_fit.cpp` (`frameResiduals`, `computePlanes` in `fitSkeleton`) |
| The branch pass: whole blind stretches seeded on the mirror with the shared unknowns frozen, relaxed, and kept per stretch by cost | `skeleton3d_fit.cpp` (`seedAndRelax`, `runsWhere`, `frameLocalTerms`) |
| The catalogue | `src/Analysis/skeleton3d/club_plane_catalogue.h` (`clubPlaneInclDeg`) |
| Config keys, `diagnostics.plane`, the flag `FlagShaftBranch` (0x08) | `skeleton3d_fit.h`, `skeleton3d_json.h` |
| Version | `kSkeleton3DStageVersion` 1 → 2 |
| Tests | `skeleton3d_test.cpp`, the "depth branch" section (p-a)–(p-f) plus the catalogue table |
| The corpus runs and grade | `skeleton3d_run.sh` configs `v1`, `dtlDropThrough(V1)`, `faceOnlyV1`; `skeleton3d_grade.py --branch` |

**Where the build departed from §3:**
1. **The branch pass seeds WHOLE blind stretches, not frame runs selected by "nearer the plane".**
   - *First build:* frames were seeded where the mirror was nearer the plane by > 10°. On the
     synthetic swing, forced onto the mirror, the release came back (errors ≤ 4°), but the finish
     stayed mirrored at 67–80° error. There the true club is itself 26–31° off the plane, so neither
     branch is clearly nearer frame by frame, and a partly flipped stretch was dragged back by the
     smoothness at its edge.
   - *At 2°:* the median error fell from 44.5° to 17°, but the finish tail still stayed mirrored.
   - *Now:* any blind stretch with ≥ 3 frames whose mirror is nearer (by > `branchSeedDeg`, now 2°)
     is seeded **as a whole**, and the cost decides the whole stretch. Continuity with its
     clearly-wrong frames carries the ambiguous ones. The blind shaft error is 0.8° median and
     1.3° p90.
2. **The planes are computed whenever the branch pass or the test hook needs them.** The *term* is
   `usePlane` alone, so the ablations can switch the two apart.
3. **The synthetic swing's self-planes are refused** (address → top and top → impact are not planar
   within 5° on the generator), so the synthetic runs on the catalogue plane (59.8°, uncalibrated at
   0.95 m).

### 9.2 Synthetic (`skeleton3d_test`, the DTL shaft and clubhead hidden from impact + 60 ms)

| Case | Blind shaft error, median / p90 | Notes |
|---|---|---|
| (p-a) the fit's own start | 1.6° / 2.3° | 58 plane frames; no run seeded |
| (p-b) forced onto the mirror, then the branch pass | **0.8° / 1.3°** | 1 stretch, kept, flagged |
| (p-c) forced mirror, no plane, no branch | 70.5° / 81.7° | stays mirrored |
| (p-c) forced mirror, plane only | 67.6° / 80.0° | stays mirrored: **the barrier** |
| (p-d) blind frames truly > 15° off the plane (42) | 1.6° / 2.2° | the prior's pull on genuinely off-plane frames |
| (p-e) DTL sees every frame | worst DoF difference **0.0** | no blind frame, no term, the same fit |
| (p-f) face-on only, catalogue plane | 5.3° → 4.8° p90 (median 1.9° → 2.4°) | no worse |

Every pre-existing case is unchanged: (b) body bone direction 2.84° p90, the analytic Jacobian 6.5e-8
(the plane and seed rows included), the ablations, (c) and (e). The branch pass costs ~0.9 s when it
seeds a stretch (synthetic, 234 frames).

### 9.3 The corpus (§6.2) — 24 two-camera swings, 28 September

Configs: `full`, `v1` (the same binary, plane and branch pass off), `dtlDropThrough(V1)`,
`dtlDropDown(V1)` and `faceOnly(V1)`, 144 runs, all clean. Graded by
`skeleton3d_grade.py --branch` → `docs/research/data/skeleton3d/grade_branch_20260928.{csv,md}`.
Medians over swings:

| Check | v1 | v2 (the fix) | Pass |
|---|---|---|---|
| \|Out of club3d's down plane\|, DTL-blind frames, address → P8 | 14.4° | **5.9°** | — |
| Blind frames whose mirror is nearer, address → P8 (pooled) | 172 / 1266 (13.6 %) | **75 / 1266 (5.9 %)** | ≤ 10 % — **met** |
| \|Out of plane\|, DTL-blind, P8 → finish | 60.7° | **11.8°** | ≤ 20° — **met** |
| **DTL dropout from impact − 250 ms** (28 hidden downswing frames per swing): shaft vs the full fit | 22.1° | **7.6°** | ≤ 10° — **met**; better on 19 / 24, worse by > 1° on 3 |
| …the same frames vs the DTL tracker's own angle | 31.0° | **13.1°** | — |
| Face-on shaft angle vs the tracker, address → P8 | 2.8° | 2.7° | not worse — **met** |
| DTL shaft angle vs the tracker, DTL-seen frames → P8 | 1.0° | 1.1° | not worse — **met** |
| Joint shift v2 vs v1 on blind frames: lead arm / the rest | — | 0.4 cm / 0.2 cm | reported |
| Face-on only vs the two-view fit, address → P8 | 47.7° (p90 75.3°) | **18.6°** (p90 51.2°) | reported |
| Solve | 1.8 s | 2.4 s | reported |
| Branch runs kept | — | 11 / 24 swings | — |

**What the numbers say:**
- **The dropout is the ground truth.** It hides the DTL club on frames the DTL *did* see and grades
  the fit there. On the 4 July swings (taped 7-iron) it falls from 25–50° to 4–12° on most. On
  the 11 June swings (bare wedge) it is mixed: s1 improves 40 → 18°, while s2 and s8 are 4–5°
  worse. That session's self-planes are the ones the fit leans on, and they rest on fewer two-view
  frames.
- **The first dropout config, from impact + 60 ms, has almost no data**: the DTL tracker publishes a
  median of 0–1.5 club frames per swing after impact. It is kept in the table but not judged. The
  hook was extended to hide from before impact (`debugDropDtlShaftAfterUs` negative).
- **After P8 the down plane is not the reference.** Even where the DTL *sees* the club in the finish,
  the fitted club is 62° off the down plane (v1 66°) while agreeing with the DTL tracker (11°). A
  real finish leaves the delivery plane, and the "mirror nearer" count after P8 (199 / 626) is no
  verdict either way. The panel's P8 cut-off keeps that span off screen.
- **The backswing self-plane is refused on 18 of 24 swings**: address → top is not flat within 5°. The
  catalogue plane stands in, and the 7-iron's catalogue value is the measured 60.3°.
- **Not fixed:** 06-11 s3 stays 23° off plane to P8 (its stretch was seeded and not kept).

### 9.4 The library
The 15 swings of 4 July were re-analysed by `swinglab_run <dir> --write-back --session-type 1`, never
`--full-window`, with the documents backed up first to `build/lib_backup_20260928/`. Every log read
`reuse: pose recorded ball recorded shaft recorded (synth refreshed) ladder recorded dtl pose
recorded`. Each document was diffed against its backup: **only `analysis.skeleton3d` (stage version
1 → 2) changed**, on all 15. The club's |out of plane| on DTL-blind frames to P8 fell on every
swing, **12.7° → 4.5°** median over the 15 (p90s roughly halved). The branch pass kept a flip on 9 (swings 1, 2, 5, 6, 8, 10, 12, 13, 15).

**The panel:** swing 5 from down the line, before and after. At P4 the club now points down the target
line at the top, where it pointed far behind the golfer (Mark's first screenshot). At P8 the
follow-through loop that swung out across the line is gone, and the path runs back up the plane.

## 10. Mark's test, and the second round (28 September 2026)

**Mark, after §9: "P8 still looks like the club veers wildly off plane"** (screenshot: the club
across the chest, square to the line). §9 reported *medians over blind frames*. At the **P8 instant**
the first build was right on 9 of the 15 library swings and still 42–63° off on 3, 4, 7, 9, 11 and
14: exactly the swings where the branch pass had tried the mirror and **not kept it**. The medians
hid that. The grade now reports the P8 instant and its worst over ± 20 ms (`P8oop`, `P8oopMax`).

**Why the mirror lost.** A debug breakdown of each stretch's cost (`SK3D_BRANCH_DEBUG=1`) showed
four causes. The first three were fixed in turn:
1. **The neutral forearm-rotation prior** (and the wrist and humeral-rotation priors) — lead
   pronation +72 and +159 against the mirror on swings 4 and 14. Those priors pull toward "no
   rotation", which is right where they were put (splitting rotations a straight arm hides, address →
   impact) and wrong through the release, where the forearms genuinely roll. They outvoted both the
   cameras and the plane — the same wrong assumption that picked the wrong branch in the first place.
   - *First tried:* loosening them (σ × 4) for the **whole fit** after impact + 60 ms. That broke
     three synthetic guards (roll p90 8.0 → 13.4°, lead-forearm roll 24 → 30.6°, the calibrated IMU
     5.7 → 8.7°): roll went unheld after impact.
   - *Built:* loosened **inside the branch pass only** (`branchReleasePriorFactor` 4) — while the
     mirror settles and when the two branches are compared. The fit keeps its priors, and the guards
     are back (roll 8.02°, lead forearm 23.9°).
2. **The club length, frozen with the other shared unknowns while the mirror settled.** The face-on
   camera sees perspective. A club fitted short (0.85 m against the 0.94 m record) reaches the
   measured face-on head only when tilted *toward* that camera — the wrong branch — so the mirror
   lost on the face-on clubhead (+155 and +127 on swings 7 and 9). The club length is now free
   while the mirror settles (the grip axis stays frozen). Each stretch is judged on the **whole
   fit's** cost, since a length change touches every frame, and stretches are tried one at a time.
3. **σ_plane 15° → 10°.** With the Cauchy tail at 15°, a club 50° off plane cost barely more than
   one 30° off, and small evidence differences outvoted it. At 10° the library P8 went from 12 / 15
   to 14 / 15 on plane; the synthetic (p-a) stays at 3.4° p90.
4. **Found by the regrade — seeded across frames the DTL saw.** A stretch's gap-bridging (≤ 2
   frames) let the seed reach DTL-measured frames. A DTL-seen frame now ends a stretch, and only
   blind frames are seeded or flagged.
   - The one remaining post-P8 disagreement with the DTL tracker (4 July s4, 87°) is a **DTL tracker
     artefact**. Its three post-P8 "RAY" frames are `escape: true`, with θ = 2π exactly and the head
     pinned at the image edge (x = 0.999). The first build "agreed" with junk.
   - Through the release (3.530–3.544 s), where the DTL genuinely sees the club, the fix agrees with
     it to ≤ 2°, where v1 was 25–42° off.
   - **Owed:** the DTL tracker should not publish `escape` frames as RAY observations.

**The final grade** (`grade_branch_20260928.{csv,md}`, all eight configs rerun on the final binary;
medians over the 24 swings):

| Check | v1 | v2 | Pass |
|---|---|---|---|
| **\|Out of plane\| at P8** | **51.9°** | **6.0°** | — |
| …worst over P8 ± 20 ms | 64.8° | 8.0° | **23 / 24 within 20°** (v1: 2 / 24); the miss is 11 June s1 (49°) |
| DTL-blind frames, address → P8 | 14.4° | 4.6° | — |
| Mirror nearer, blind → P8 (pooled) | 13.6 % | 3.6 % | ≤ 10 % — **met** |
| **DTL dropout from impact − 250 ms** vs the full fit | 24.2° | **9.1°** | ≤ 10° — **met**; better on 18 / 24, worse > 1° on 5 |
| …vs the DTL tracker | 31.0° | 13.6° | — |
| Face-on / DTL shaft angle vs the trackers, → P8 | 2.8° / 1.0° | 2.7° / 1.1° | not worse — **met** |
| Joint shift, blind frames: lead arm / the rest | — | 0.4 / 0.2 cm | reported |
| Face-on only vs the two-view fit, → P8 | 50.0° (p90 79.3°) | **15.8°** (p90 31.0°) | reported |
| Solve | 1.9 s | 3.0 s | reported |
| Branch stretches kept | — | 20 / 21 | — |

**Synthetic, final:** every case passes, the pre-existing guards included. (p-a) 2.6° / 3.4°,
(p-b) forced mirror → 2.6° / 3.4°, (p-c) the barrier holds (71–72°), (p-e) parity 0.0, (p-f) face-on
only 5.3° → 4.8°.

**The library, final:** the 15 swings of 4 July were rewritten through the reuse path; only
`analysis.skeleton3d` changed. The worst off-plane angle over P8 ± 20 ms went from 56–75° to 3–14°
on 14 swings; swing 4 is 20.8° (11.6° at the P8 instant). The grabs of swings 4 and 5 from down the
line at P8 show the club on the plane.
