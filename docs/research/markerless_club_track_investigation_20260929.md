# Markerless club track: the 29 Sept 2026 "regression" investigation

**Status:** diagnosis complete. 16 Sept: cause verified and fixed. 15 Sept: cause identified
(the impact-camera rig hides the ball) and confirmed by Mark. Two things remain for next time:
the session warning and unusable face-on marking (§5a), and tracker robustness (§7).

## 1. What was seen

After the 29 Sept library re-analysis, 16 Sept Wrist_02 swing 2 (an **untaped 6-iron**; its club
record wrongly says a banded 7-iron, see §6) showed the club track completely off the club. At
mid-backswing the shaft line was drawn horizontal, to the right, at hand height, while the video
shows the shaft pointing up. The day's tracker commits (a0391fa3..6a8b0fd7: the blur's leading
edge, the evidence-fitted synth, the address-ball anchor) looked like the obvious suspect.

## 2. Short answer

- **Not a tracker code regression.** On the same input pose, code from 17 Sept (3b8dc071),
  from the morning of 29 Sept before the tracker commits (63a6f15b), and from HEAD (6a8b0fd7)
  produce the **identical** broken track. That spans the 3-D shaft fusion (b49a9dea, 21 Sept),
  the DTL tracker (20 Sept) and everything else in the window. Switching today's features off
  by parameter (`shaft.wedge.leadEdge=false`, `synth.fitEvidence=false`) changes nothing
  before the top.
- **16 Sept, cause verified: the wrong pose model.** Swings recorded at High quality must run
  ViTPose-L. On the studio PC the tools could not find the L model and silently ran ViTPose-B,
  and that B pose is what the library stored. On L the same swings track cleanly.
- **15 Sept: the impact-camera rig hides the ball.** Recorded at Medium, so B is the correct
  model and the bug above does not apply. The broken swings are, almost exactly, the swings
  whose address ball was not found. The impact-camera rig sat on the ball position in the
  face-on view. Mark confirmed this is the cause (29 Sept).
- **Why it surfaced on 29 Sept:** the shaft stage version moved 2 → 3 → 4, which forces every
  library swing to be **re-tracked from its stored pose**. Before that, the library still held
  club tracks from earlier runs. This is an inference: the pre-change backups were cleared, so
  what those tracks looked like cannot be checked.

## 3. Does markerless tracking work?

Yes, where its input is sound. "Clean" below is a **sanity check**, not an accuracy grade against
hand marks: the shaft points down at address (P1 ≈ 90–110°), up at P3 (≈ 245–300°), with high
coverage.

| Session | Club (actual) | Quality | Clean / swings | Notes |
|---|---|---|---|---|
| 9 Sept Wrist_01 | 6-iron, unmarked | Medium | **7 / 7** | re-tracked with 29 Sept code (shaft v4); cov 0.97–1.00 |
| 15 Sept Wrist_01 | 7-iron, no bands | Medium | 10 / 13 | see §5 |
| 15 Sept Wrist_02 | 7-iron, no bands | Medium | 3 / 13 | see §5 |
| 16 Sept Wrist_01 | gap wedge, no bands | High | 2 / 3 before the fix, **3 / 3 on L** | §4 |
| 16 Sept Wrist_02 | **6-iron, untaped** (record: banded 7-iron) | High | 1 / 3 before, **3 / 3 on L** | §4, §6 |
| 16 Sept Wrist_03 | record: banded 7-iron (unconfirmed) | High | 1 / 1 | |

The older corpus (June–Aug, taped or pre-markerless) is clean apart from 4 Aug Wrist_05, which
has no video. 7 July's untaped 7-iron predates markerless and is not evidence either way.

## 4. 16 Sept: the pose-model bug (verified)

**Mechanism.**
- `PoseEstimatorViTPose::largeModelDir()` used `QStandardPaths::AppLocalDataLocation`.
- Nothing in the repo sets an application name, so Qt names that directory after the **running
  executable**:
  - the app `PinPointStudio.exe` looks in `%LOCALAPPDATA%\PinPointStudio\models\vitpose\`, where
    the downloaded L model lives (on the studio since 21 July);
  - `swinglab_run.exe` looks in `%LOCALAPPDATA%\swinglab_run\models\vitpose\`, which is empty.
- `useVitPoseLarge()` then falls back to B **without a word**.
- On the Mac, `swinglab_run` reports its name as "PinPointStudio", so it found L. That is why
  the Mac and the studio disagreed on the same swing.

**Evidence** (16 Sept Wrist_02 s2):

| Pose | Where | Coverage | P1 | P3 | P7 |
|---|---|---|---|---|---|
| B (stored in library) | studio | 0.69 | 132° | 341° | 137° |
| B, re-run fresh | studio | 0.69 | 132° | 341° | 137° |
| L | Mac | 0.96 | 100° | 261° | 95° |
| L | studio | 0.96 | 100° | 261° | 95° |

- The studio is deterministic: its fresh B pose equals the stored one to 3e-8 (normalised units).
- B against L differs by a median 0.006 of the frame (p90 0.027). That small difference is
  enough to move the whole backswing onto another structure (§7).
- Also verified: Wrist_01 s1 has no club track at all on B and a full one on L (cov 0.88,
  P1 97°, P3 260°). Wrist_02 s1 goes from cov 0.78 / P1 122° / no P3 on B to cov 0.94 /
  P1 97° / P3 259° on L.

**Scope.** Only 7 library swings are recorded at High, all on 16 Sept, and all 7 were stored
with a B pose. The studio's library sweeps have been writing B poses over High swings; the
23 Sept "Windows drops 16 Sept Wrist_01 s1's club track" finding
(`windows-reanalysis-divergence`) is very likely this same bug.

**Fix (made 29 Sept):**
- `largeModelDir()` now uses `GenericDataLocation + "/PinPointStudio/models/vitpose/"`, which is
  the app's own directory on macOS, Windows and Linux, whichever executable is running.
- `PoseRunner::run` warns once per process when a High swing falls back to B.
- Verified on the studio with no workaround in place: the patched `swinglab_run` loads
  ViTPose++-L by itself.

**Library.** All 7 High swings were re-analysed in place on the studio on 29 Sept
(`--force-rerun --write-back`, the fixed Release exe), all on ViTPose-L, and none lost a metric:

| 16 Sept swing | Before (B): cov / P1 / P3 / metrics | After (L): cov / P1 / P3 / metrics |
|---|---|---|
| Wrist_01 s1 | no club track / – / – / 54 | 0.88 / 97° / 260° / 65 |
| Wrist_01 s2 | 0.86 / 95° / 260° / 65 | 0.87 / 100° / 256° / 65 |
| Wrist_01 s3 | 0.85 / 91° / 256° / 65 | 0.89 / 98° / 256° / 65 |
| Wrist_02 s1 | 0.78 / 122° / – / 57 | 0.94 / 97° / 259° / 57 |
| Wrist_02 s2 | 0.69 / 132° / 341° / 62 | 0.96 / 100° / 261° / 62 |
| Wrist_02 s3 | 0.94 / 99° / 270° / 62 | 0.98 / 101° / 264° / 62 |
| Wrist_03 s1 | 0.82 / 89° / 260° / 57 | 0.85 / 81° / 252° / 57 |

Backups of all 7 pre-fix documents: `/mnt/swingdata/scratch/backup-pre-L-20260929/`. Delete them
once Mark has reviewed the swings in the app.

## 5. 15 Sept: the impact-camera rig hides the ball

**What is known:**
- Both sessions were recorded at Medium, so ViTPose-B is correct and §4 does not apply.
- A fresh B pose on the studio cleans 5 of the 13 broken swings (Wrist_02 s1, s6, s11, s12,
  s13). Their stored pose is therefore not what today's pose code produces. **Why is not known.**
- 8 stay broken on a fresh pose: Wrist_01 s1, s5, s12 and Wrist_02 s2, s3, s4, s5, s10.
- **ViTPose-L does not fix them.** Wrist_02 s2 re-run with the quality forced to High (a scratch
  copy) is broken differently: B gives P1 186°, L gives P1 291°. So the model is not the lever
  here.

**The lead: the address ball.** Across the two 15 Sept sessions:

| | Broken | Clean |
|---|---|---|
| Address ball not found | 11 | 1 |
| Address ball found | 2 | 12 |

- On 9 Sept every swing found its ball and every swing is clean.
- The 15 Sept face-on frames show the **impact-camera rig on the ball position**: the ring at
  the bottom centre of the frame (15 Sept was the impact camera tuning session). The tracker
  probes the address shaft toward the accepted address ball, so with no ball it is not probed.
- On Wrist_02 s2 the track reads 100–103° with the head on the clubhead for 0.8–1.6 s. At the
  P1 instant (1.49 s) it reads 186°, horizontal to the left into the dark beside the trousers.
- The club itself is clearly visible and reasonably lit in these frames. "Too dark to track" is
  not what the pictures show, though the lighting may still play a part.

**Confirmed by Mark (29 Sept):** the sessions shot with the impact camera are broken because
the rig hides the ball from the face-on camera. A ball-injection run is not needed to establish
this. It remains a useful check that the tracker recovers once a ball exists.

**Also possibly relevant:** a 15 Sept note records swings 5 and 12 as non-swings. That may be
Wrist_01 s5 and s12 (both broken, both without a ball); which session the note meant is
unconfirmed.

## 5a. What the product must do about it (Mark, 29 Sept) — not built yet

- **The session carries a warning symbol.** A session whose face-on view has the ball hidden,
  by the impact-camera rig or anything else, says so at session level. Per the logging rule,
  that is the session ⚠, not a log line.
- **The face-on view is marked unreliable / unusable** for that session. Its club track and
  everything derived from it (P-positions from the shaft, club metrics, shaft lean, low point)
  must not be presented as measurements.
- **The trigger is evidence, not the rig's presence.** The address ball not found on the
  face-on view is the signal already in the data. On 15 Sept it is absent on 12 of 26 swings
  and marks 11 of the 13 failures. The design must decide:
  - whether one swing without a ball flags only that swing or the whole session;
  - what share of swings flags the session.
  Gate the analysis on the data it has, never on the session type.
- The 15 Sept sessions should show the warning once it exists. Their current tracks stay in
  the library, but must not be read as results.

## 6. Does markerless break on Medium? No evidence that it does

Mark asked whether markerless tracking breaks at the Medium setting; if it did, Medium should
not be offered. The evidence says the setting is not the variable:
- 9 Sept is Medium and clean on 7 of 7.
- 15 Sept is Medium, and its failures follow the hidden ball (the impact-camera rig), not
  the model.
- On a 15 Sept failure, L does not help (§5).

**Recommendation:** keep Medium; the evidence does not implicate it. Mark's rule stands: if
markerless tracking is ever shown to break at Medium, Medium is removed as an option.

**Stale club record (corrected 29 Sept).** 16 Sept Wrist_02's record said "7 IRON", 940 mm,
bands at 308/362/560/758/808/854 mm, but the club was an untaped 6-iron (Mark). So the tracker
ran band matching (E1) against a bare shaft, with a 7-iron's lengths and pixel length prior.

The three Wrist_02 documents now carry the 6-iron record PPS wrote for the same club on 9 Sept:
"6 IRON", 953 mm, no bands, no length prior. They were re-analysed on the studio (shaft re-run,
ViTPose-L), with the tracks unchanged and clean and no metric lost:

| Swing | Coverage | P1 | P3 | Metrics |
|---|---|---|---|---|
| s1 | 0.94 | 97° | 259° | 57 |
| s2 | 0.96 | 100° | 261° | 62 |
| s3 | 0.98 | 101° | 264° | 62 |

Backups: `/mnt/swingdata/scratch/backup-club-6i-20260929/`. Wrist_03 carries the same 7-iron
record and is unconfirmed.

## 7. The underlying weakness (next session's work, item 4)

- A pose difference of median 0.6% of the frame put 16 Sept Wrist_02 s2's entire backswing on
  the wrong structure, and moved P1 by 175 ms and P4 by 194 ms.
- The tracker's address and backswing depend heavily on:
  - the pose (the arm veto, the hands' position, the phase ladder);
  - the address ball.
- Neither is guaranteed. A markerless tracker that is critical to the product needs to fail
  soft here:
  - hold the in-span track that was right for 0.8–1.6 s instead of jumping at P1;
  - refuse a P1 far from the shaft's own address hold;
  - flag the swing instead of storing a confident wrong track.

Suggested order:
1. Build §5a (the session warning and the unusable face-on view).
2. Trace 16 Sept Wrist_02 s2 on the B pose (`--trace`) to see which evidence carried the
   backswing onto the wrong structure.
3. Design the fail-soft rules against that trace.
4. Gate on 9 Sept + 15 Sept + 16 Sept (all markerless) on the studio.

## 8. Fixes

| # | Fix | State |
|---|---|---|
| 1 | Large-model path independent of the executable; warn on a High → B fallback | done; verified on Mac and studio |
| 2 | Re-analyse the 7 High swings with L | done 29 Sept, 7 / 7 clean, no metric lost (§4) |
| 3 | 15 Sept diagnosis | done: the impact-camera rig hides the ball (§5), confirmed by Mark |
| 3a | Session ⚠ and face-on marked unusable when the ball is hidden | specified (§5a), not built |
| 4 | Tracker fails soft on a poor pose or missing ball | next session |
| — | Correct the 16 Sept Wrist_02 club record | done 29 Sept (6-iron, re-analysed); Wrist_03 unconfirmed |

## 9. How it was established (reproducible)

- **Single-swing A/B on the Mac.** `swinglab_run <swing> --pose <pose.json> --params <json>`
  runs in about 3 s when the pose is injected; the library's own pose was extracted from the
  document (`ppsw dump`). Output: `/mnt/swingdata/scratch/ab-0916-02-s2/`.
- **Bisect over code.** In-tree `git checkout <sha> -- src tools`, build `swinglab_run`, run on
  the stored pose, restore HEAD. For builds older than the .ppsw switch (4d8ba169), use a
  swing.json copy with `analysis.club` stripped, so the build cannot reuse the stored track.
- **Studio pose re-runs.** `--force-rerun`, Release exe, output in
  `/mnt/swingdata/scratch/pose-rerun-20260929/` (B) and `pose-rerun-L-20260929/` (L,
  through a temporary hard link, since removed).
- **Medium vs L test:** `/mnt/swingdata/scratch/medium-vs-L/`.
- **Library survey.** `ppsw dump` of every document; the P1/P3 angles and coverage of the
  stored `analysis.club`, the address ball, and `capture.motionCaptureQuality` against
  `analysis.versions.pose.model`.

These scratch trees are kept for the next session and should be deleted once it is done.
