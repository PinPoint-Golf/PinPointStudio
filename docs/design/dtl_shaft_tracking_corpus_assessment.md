# Down-the-line shaft tracking — corpus assessment and development set

**Status: assessment, 2026-09-20. GO for development on 21 corpus swings; no
implementation started.** Mark asked whether the corpus holds down-the-line video
good enough to begin a DTL shaft tracker, and which specific swings to develop
against, with validation captures to be generated later. The answer is yes, on a
named set (§3). This document records the audit behind that answer so the decision
can be re-checked rather than re-taken, and so the caveats travel with the data.

Successor context: the face-on tracker is `markerless_club_tracker_design.md`
(P0–P5 built and graded; band lock is the reference figure). Nothing here changes
that design — DTL is a second view for the same tracker, and the first work item
is plumbing, not tracking (§5).

Every number below was measured on 2026-09-20 from the corpus files, not taken
from `CORPUS.md` or the manifest. §6 says how, so the measurements can be redone.

---

## 0. Summary

Three findings drive the recommendation:

1. **The imagery is adequate and, in one session, taped.** 24 clips are usable at
   149.3 fps, 1024 px tall, well exposed — club-zone luminance mean 78–129/255
   against the paired Face-On's 54. Twelve of them show a **black/white banded
   shaft**, which means DTL truth can be bootstrapped from band detection instead
   of hand-labelled, and the band lock is available as the reference figure on the
   same frames. This is what turns "we have some video" into "we can start".

2. **The camera is on the down-the-line axis but not in the down-the-line place.**
   It sits on the ball–target line extended behind the *ball*, at head height —
   not the ball–hands line extended behind the *golfer*, at hand height. The
   projection is the right family; the vantage is not the convention. Detection
   and tracking quality grade honestly on this data. Plane and path *angles* do
   not, and will not agree between the two usable sessions.

3. **There is no DTL ground truth of any kind, and no code path to a second
   camera.** Every `truth.json` label in the corpus is Face-On pixel space, and
   `analysis_stage.h` declares `DownTheLine` reserved and never populated. The
   tape bands are the bootstrap; the plumbing is the first task.

---

## 1. What exists today, and what it assumes

The analysis consumes exactly one camera. `src/Analysis/analysis_stage.h:52-54`:

> Where a camera sits relative to the golfer. FaceOn is the only placement the
> current analysis consumes (pose/shaft/head/foot all run off it); DownTheLine
> is reserved for the stereo/DTL fusion proposals and is not populated yet.

That is pinned as an invariant by `src/Analysis/tests/analysis_stage_test.cpp:219`
("fromJob: DownTheLine is never populated (reserved, not yet consumed)").

The diagnostics layer already knows which measures *want* the view —
`src/Diagnostics/measure_facets.cpp:466-484` routes target-line and ball-line
references, thoracic/lumbar segments, spine-to-thigh angles, and perpendicular
distance to the stance line to `ViewNeeded::DownTheLine`. Those measures are the
eventual prize. They are **not** what this data can deliver (§4).

The pose caches (`pose2/`, `pose3/`) are a flat `frames` list with no stream
identity, so there is today no place to put a second view's pose.

---

## 2. What the corpus actually holds

### 2.1 Inventory

34 of the 115 corpus swings carry a second video. `corpus.json` agrees
independently: 34 swings with `videos: 2`, 56 with 1, 25 with none.

| Session | n | File | `setup.perspectiveName` | DTL width | Club (measured, not labelled) |
|---|---|---|---|---|---|
| 2026-06-11 Wrist_01 | 9 | `Down-the-Line.mp4` | *absent (pre-versioned app)* | 576 | gap wedge, **bare shaft** |
| 2026-07-03 Wrist_01 | 10 | `Down-the-Line.mp4` | `DownTheLine` | 576 | 7 clubs, GW→5W |
| 2026-07-04 Wrist_01 | 15 | `DTL.mp4` | `DownTheLine` | 512 | iron, **taped shaft** |

Nothing from 2026-07-05 onward has a second camera — not the 09-09 markerless
benchmark set, not the two 08-18 wG3-bound sessions. All 34 sit inside the pinned
61-swing `pose2` set.

All 34 are 1024 px tall, ~746 frames, and **149.3 fps actual**. Note that 06-11
and 07-03's Face-On both *declare* `fps_num/fps_den = 30000/1000`; the per-frame
`t_us` spacing is ~6.7 ms. Use the timestamps, never the declared rate.

### 2.2 Camera placement — the axis is right, the position is not

The alignment stick settles the axis: in the DTL frame it recedes near-vertically
up the mat toward a vanishing point; in the paired Face-On the same stick runs
horizontally across the bottom. The optical axis points down the target line.

But the golfer's chest and face are toward the camera and the sim screen fills the
background, so the camera is not behind him. Reading the two together: it sits on
the **ball–target line extended behind the ball**, displaced laterally from the
classic **ball–hands line** by roughly a stance distance, and at head height or
above (the top of the head is visible and the mat is strongly foreshortened)
rather than the hand height the convention calls for.

At ~2–3 m studio range that displacement is large. Consequence in §4.

⚠ This placement is **inferred from 2D frames and has never been measured.** The
mat edges, the two alignment sticks and the known ball position give enough
coplanar structure to fit a homography and recover actual height and lateral
offset per session — roughly an hour of work, worth doing only if someone wants
to correct the geometry rather than re-capture it.

### 2.3 Image quality

Measured at frames 300 and 430, lower 38 % of the frame (the club/ball/mat zone):

| Clip | frame mean | club-zone mean | club-zone p95 | edge energy |
|---|---|---|---|---|
| 06-11 DTL | 74–76 | 120–129 | 254 | 6.4–6.7 |
| 06-11 Face-On | 60–61 | 120–122 | 254 | 3.0–3.1 |
| 07-04 DTL | 67–70 | 78–84 | 240–249 | 6.0–6.3 |
| 07-04 Face-On | 31–34 | 54 | 239–247 | 2.5 |
| 07-03 DTL | 71–72 | 81–83 | 248–250 | 4.1–4.4 |

The DTL stream is the better-exposed of the two in both sessions. This is not a
repeat of the 15 September light-starved clips (club 10–30/255). The shaft is
visible through address, downswing and impact; mid-downswing frames carry real
motion smear, but it is structured smear with resolvable leading and trailing
edges — the same problem the face-on tracker already handles at the same frame
rate, and with less in-plane speed here because much of the club's motion runs
along the optical axis.

### 2.4 Labels

**No DTL ground truth exists.** Every `truth.json` is Face-On pixel space. The
proof is arithmetic: `07-04/swing_0001` carries `ball: [690.4, 999.0]` in a frame
that is 512 px wide.

What shaft labels do exist, all Face-On:

| Session | swings with shaft labels | points each |
|---|---|---|
| 06-11 | `swing_0008`, `swing_0009` | 51, 18 |
| 07-03 | all 10 | 10 |
| 07-04 | none | — |

---

## 3. The development set

### 3.1 Primary — `2026-07-04_Mark-Liversedge_Wrist_01/swing_0004` … `swing_0015` (12)

- **Taped shaft, black/white bands**, clearly resolvable at address and through
  impact. Bootstrap DTL truth from band detection; report every markerless result
  beside the band-lock figure on the same frames, as the face-on design requires.
- `capture.impactUs` present on all 15; impact lands at **DTL frame 517–520**.
- Framing clean and stable across 0004–0015 — golfer fully in frame with margin,
  club never exits the top edge.
- ⚠ Truth labels the club `DRIVER`. It is an **iron with a taped shaft.** The July
  club labels are not trustworthy; classify by evidence.

### 3.2 Markerless counterpart — `2026-06-11_Mark-Liversedge_Wrist_01/swing_0001` … `swing_0009` (9)

- **Bare, untaped shaft** (zoom-verified: plain dark shaft, white grip, no bands).
  Gap wedge. This is the markerless transfer test.
- Framing clean on all nine — measured, no edge contact.
- ⚠ **No `capture.impactUs`** (pre-versioned app), and the swing sits at a
  different place in each window: at frame 519, `swing_0001` is at impact while
  `swing_0009` is still at the top of the backswing. Impact must be found.
- Do not tune on these. They are the transfer test, not development data.

### 3.3 Excluded

| Swings | Reason |
|---|---|
| `07-04 swing_0001`–`swing_0003` | The rig was repositioned after swing 3 — the golfer is clipped at the left edge (trail arm and part of torso cut). The club is still fully in frame, so keep them as a stress case. Anything calibrated per session on 07-04 must be done per sub-group. |
| All 10 of `07-03` | The golfer is out of frame for the entire clip — only the cap and a sliver of shoulder. Expensive to lose: this is the club-variety session (GW, 9i, 7i, driver, 4-hybrid, 3-hybrid, 5-wood) and the only one with shaft labels on every swing. |

### 3.4 Suggested split

Develop on `07-04 s0004–s0009` (6). Hold `07-04 s0010–s0015` (6) untouched as
regression. Then 06-11's 9 as the markerless transfer test, run once, late.

---

## 4. What this data can and cannot grade

**Can:** detection rate, track continuity, pixel precision, band lock vs bare
shaft on the same rig, behaviour through impact blur, robustness to the
body-overlap phases. All of it against the taped session's band lock.

**Cannot:**

- **Plane and path angles.** The camera is off the hand line and above hand
  height, so numbers read here will not equal classic DTL numbers, and will not
  agree between 06-11 and 07-04 — different rigs, different placements.
- **Anything metric in 3D.** Intrinsics are uncalibrated (Angles2D tier) and there
  are no extrinsics between the two cameras, so there is no stereo lift. A second
  view buys a second 2D projection and nothing more.
- **Generalisation.** One golfer, one studio, two club presentations, and a rig
  that moved once mid-session.

Report DTL results as tracking quality. Do not promote a DTL *metric* off this
data.

---

## 5. Code preconditions

In order, before any tracking code:

1. **Stream selection.** Populate `CameraPlacement::DownTheLine` in
   `CaptureCapabilities` from the swing's streams, and retire the
   never-populated invariant in `analysis_stage_test.cpp:219`. Keying off
   `setup.perspectiveName` works for 07-03/07-04 but **not** 06-11, which predates
   the field — that session needs the stream alias as a fallback.
2. **Stream-keyed pose/track cache.** `pose2`/`pose3` carry no stream identity.
   The cache format needs a stream key before a second view's output can be
   stored next to the first.
3. **Version gate.** Bump `kShaftStageVersion` (`src/Analysis/analysis_versions.h:46`,
   currently 2) when the shaft producer changes, or the corpus will not re-analyse.

---

## 6. Provenance

Every figure above is reproducible from the corpus tree at
`/mnt/swingdata/corpus/swings`:

- **Inventory, fps, impact frame, labels** — read from each `swing.json`
  (`streams[].alias`, `streams[].setup.perspectiveName`, `streams[].frames.t_us`,
  `capture.impactUs`) and each `truth.json` (`meta.club`, `shaft`, `ball`).
  Impact frame = nearest `t_us` to `capture.impactUs` in the DTL timebase.
- **Luminance and edge energy** — frames decoded to 8-bit gray via ffmpeg
  rawvideo; statistics over rows `0.62·h … h`; edge energy = mean gradient
  magnitude.
- **Edge clipping** — bright-pixel occupancy in the outer 4 columns over rows
  100–700, sampled at frames 300/430/470/500/520. ⚠ This test is only valid where
  the background is dark: on 07-04 the lit sim screen touches the frame edge and
  dominates the metric, so the 07-04 framing verdicts in §3 were made visually and
  the metric was used only to detect the s0003/s0004 change.
- **Camera placement and tape state** — visual, from extracted frames and crops.
  Inferred, not measured (§2.2).

---

## 7. Open questions

1. **Is the geometry worth correcting, or only re-capturing?** The homography fit
   in §2.2 answers it. Re-capture is almost certainly cheaper.
2. **What should the validation capture look like?** At minimum: camera on the
   ball–hands line at hand height, both views on one clock, a marked club and the
   same club bare, and more than one golfer. That capture is what converts this
   work from tracking quality into a shippable DTL metric.
3. **Does the face-on tracker transfer at all, or does DTL need its own model?**
   Unknown until §5's plumbing exists and the tracker is pointed at a DTL clip.
   That experiment is the first real result, and it is cheap once the stream
   selection lands.
