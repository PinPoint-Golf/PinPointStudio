# The Shaft Tracker — Developer Guide

*How PinPoint Studio finds a golf club in video, what it believes about it, and where that belief is weak.*

State of the code described: `main` as of 1 October 2026 (commits up to `f76d3b65`).

---

## Before you start

This guide explains the part of PinPoint Studio that turns camera frames into statements about the golf club's shaft:

- where the grip is;
- which way the shaft points;
- how long it looks in the image;
- where the clubhead is;
- when the swing passes through its coaching positions;
- what the club did in three dimensions.

It also explains the two **synthetic** tracks that are built on top of the measured one. It describes every metric the shaft feeds. And it lists, at length, the things that are wrong, weak or unproven.

It is written for two kinds of reader. Developers need to know what the code does, in what order, and why; they should be able to change it without breaking a rule they did not know existed. Domain experts (and the person who commissioned the work) want to understand what has been built over the last few months and how far to trust it. Neither group is assumed to know the algorithms or the mathematics. Every technique is explained from the ground up the first time it appears, usually with a small worked example. Section 3 collects the mathematical background in one place. If you already know dynamic programming, isotonic regression, Kalman smoothing and projective geometry, you can skip it and come back when you need it.

The guide is long because the tracker is not one algorithm. It is about forty mechanisms, each added to fix a specific, named failure on a specific swing. Most of them only make sense once you know the failure they were built for. Where a design decision was forced by an observation, the observation is quoted, usually from a code comment or a design document. That way you can find the evidence and judge it yourself.

**A note on numbers.** Every accuracy figure in this document comes from somewhere: a code comment, a design document, a research report or a commit message. Almost all of it was measured on **one right-handed golfer in one studio**, with a small number of clubs. That is not a footnote. It is the most important fact about every number here, and §10 comes back to it.

**Conventions used throughout.**

- **θ (theta)** is the shaft's angle *in the image*. It is measured from the grip toward the clubhead with the usual `atan2(dy, dx)` convention, but with the image's y-axis pointing **down** (image rows count downward). So:
  - θ = 0° points to the right of the picture;
  - θ = 90° points straight **down** — at address the shaft hangs from the hands toward the ball at roughly 90–100°;
  - θ = 180° points left;
  - θ = 270° points straight up.

  The internal code mixes degrees (the decision logic) and radians (the stored samples).
- **φ (phi)** is the lead forearm's image angle, from the elbow toward the grip, in the same convention.
- **ψ (psi)** = θ − φ is the angle between the shaft and the forearm: the image version of the wrist hinge.
- Times are in microseconds (`t_us`). At the face-on camera's ~150 frames per second, one frame is about 6.6–6.7 milliseconds.
- "Face-on" means the camera facing the golfer's chest. "DTL" (down the line) means the camera behind the golfer, looking along the target line.

---

## Contents

1. [The problem, and the shape of the solution](#1-the-problem-and-the-shape-of-the-solution)
2. [The constraints: what the camera actually sees](#2-the-constraints-what-the-camera-actually-sees)
3. [The mathematics, explained from first principles](#3-the-mathematics-explained-from-first-principles)
4. [The face-on tracker](#4-the-face-on-tracker)
5. [The down-the-line tracker](#5-the-down-the-line-tracker)
6. [From two views to three dimensions](#6-from-two-views-to-three-dimensions)
7. [The synthetic tracks](#7-the-synthetic-tracks)
8. [The metrics the shaft feeds](#8-the-metrics-the-shaft-feeds)
9. [Plumbing: where it runs, what it stores, how it is drawn, how it is tested](#9-plumbing-where-it-runs-what-it-stores-how-it-is-drawn-how-it-is-tested)
10. [Shortcomings](#10-shortcomings)
11. [Glossary](#11-glossary)
12. [A short history](#12-a-short-history)

---

## 1. The problem, and the shape of the solution

### 1.1 What is being asked

A golf swing lasts about a second and a half from takeaway to finish. Filmed at 150 frames a second it becomes a few hundred frames, with a few hundred more of address and finish around it. For every one of those frames we want to know:

- **Where the hands are on the club.** This is the grip point, the pivot the shaft rotates about.
- **Which way the shaft points**, as an angle in the image.
- **How long the shaft looks**, because a shaft pointing toward the camera looks short. Together with the direction and the grip this places the clubhead.
- **How sure we are** of all three.

From that per-frame record we then want to derive everything a coach reads off a swing video:

- the classic checkpoints:
  - address;
  - shaft parallel to the ground on the way back;
  - lead arm parallel;
  - the top;
  - lead arm parallel coming down;
  - shaft parallel coming down ("delivery");
  - impact;
  - shaft parallel on the way through;
  - the finish;
- the shaft's lean at impact;
- how upright the shaft stands at address versus impact;
- the swing plane;
- the clubhead's speed and the angle it attacks the ball at;
- where the bottom of its arc is.

Then we want to draw all of it over the video, smoothly enough to replay at a quarter speed.

### 1.2 Why this is hard

If the club were a crisp black line on a white wall, this would be a weekend project. It is not, for three reasons. Each one shaped the design.

**First, the club is often not visible as a line at all.** The face-on camera runs at its maximum frame rate with its shutter open for 98% of each frame (§2.1). Near impact the club rotates about 15–20 degrees *per frame*. What the camera records then is not a shaft but a translucent fan, smeared across 20–30 degrees of arc, brightest near the hands and fading toward the head. A bare steel shaft through impact is, to all practical purposes, invisible.

**Second, the scene is full of things that look exactly like a shaft.** A golfer's forearm is a long, straight, bright structure that starts at the hands. So is the trailing leg seen from behind, which the project came to call "the trouser line". So are:

- the edge of the hitting mat;
- the alignment stick;
- the crease in a pair of trousers;
- the shadow of the club itself;
- the lit edge of a simulator screen.

Many of these are *more* visible than the real shaft. A detector that just looks for the strongest line through the hands will be confidently wrong a large fraction of the time.

**Third, the cheap ways to get it right were tried and failed.** The project's first C++ tracker (June 2026) used a classical line detector and a Kalman tracker. It "was wired into Auto Markup once, produced confidently-wrong markups, and was reverted", because its confidence "did not correlate with error". On one swing it locked onto a shadow at the mat edge, "reading 43° against a true ~98°", and "stayed wrong for the entire swing". Its median error looked fine, at 7.4°. But 24% of frames were more than 30° wrong, at a confidence of 0.93–0.96 (`docs/research/club_detection_from_video.md`, Phases 1 and 4). That experience produced three rules that run through everything built since:

1. *"Prove it on the exemplar first."* Algorithms were developed in a Python laboratory (`tools/shaftlab/`) against hand-marked frames, adjudicated by eye, and only then ported to C++.
2. *"The median lies."* A tracker that is right most of the time and badly, confidently wrong the rest of the time is worse than useless. Error distributions are always read at the tail (p90, the fraction of frames more than 15° or 30° wrong), not just at the median.
3. **Honesty over coverage.** A frame is only called measured when the pixels earned it. Everything else is labelled for what it is: coasted, predicted, held, end-on, occluded, synthesised. A track whose independent witnesses contradict it is *refused*. It is drawn as nothing, and every metric depending on it shows "–". Saying "I don't know" is always permitted; saying something wrong with confidence is the failure the whole design exists to prevent.

### 1.3 The shape of the solution

Rather than find the shaft in each frame independently and then try to join the dots, the face-on tracker treats the whole swing as **one optimisation problem**. In every frame it scores all 360 possible shaft directions (one per degree) for how much they look like a shaft. It adds penalties for directions physics forbids:

- pointing back up the forearm;
- passing through the golfer's body;
- being a line that continues through the hands rather than ending at them.

Then it finds the single sequence of directions, one per frame, that best balances "agrees with what this frame shows" against "moves the way a club can move between frames". The algorithm that finds that sequence is the **Viterbi algorithm**, explained in §3.4. Because the answer is chosen *globally*, a single frame dominated by a bright forearm cannot pull the track off the shaft. Its neighbours, where the shaft is visible, vote it down.

Around that core sit:

- an independent set of **evidence engines** that read the shaft in different ways: as a bright ridge, as a sequence of reflective bands on a taped club, as a run of bare steel between the grip and the hosel, or as the edge of a motion-blur fan;
- a **phase model** that segments the swing from the hands alone, so the solver knows which way the club should be turning;
- a **reconciliation** step that uses the forearm as a second witness through the blur of impact;
- a **clubhead** stage and a **club-length** estimate;
- a **coaching-position** locator;
- the **synthetic track**, a smooth curve through the coaching positions, fitted to every measurement between them;
- a set of **self-checks** that compare the result against the ball, and refuse it if they disagree.

A second camera, when there is one, gets its own tracker (§5), built from the same engines but with different decisions, because the club looks very different from behind. The two views are then combined geometrically into a three-dimensional shaft (§6).

### 1.4 The three trackers and how they relate

```
               face-on camera                              down-the-line camera
                     │                                              │
   pose (ViTPose WholeBody 133-pt) ─┐                pose (DTL) ────┤
   ball track ──────────────────────┤                               │
   club record (length, bands…) ────┤                               │
                                    ▼                               ▼
                       ┌──────────────────────┐   FaceOnWitness  ┌───────────────────┐
                       │ FACE-ON TRACKER       │ ───────────────► │ DTL TRACKER        │
                       │ ShaftTracker /        │  (one-way: time, │ DtlShaftTracker /  │
                       │ decideTrack           │   visibility,    │ dtlSolve /         │
                       │ → ShaftTrack2D        │   ball-gate      │ dtlPostSolve       │
                       │   samples, positions, │   timing)        │ → DtlShaftTrack2D  │
                       │   synth, lengths,     │                  │   samples, bands   │
                       │   wedgeObs, plane     │                  └─────────┬─────────┘
                       └──────────┬───────────┘                            │
                                  └──────────────┬───────────────────────────┘
                                                 ▼
                                   ┌──────────────────────────┐
                                   │ FUSION  fuseTracks       │
                                   │ → Track3D: 3-D direction │
                                   │   per DTL frame, back /  │
                                   │   down planes, η(t),     │
                                   │   address plane          │
                                   └──────────┬───────────────┘
                                              ▼
                       3-D synthetic DTL line (synth3d), Skeleton3D, metrics
```

**The face-on tracker** is the primary instrument. It runs on every swing with a face-on camera. Its code is in `src/Analysis/shaft_tracker.*`, `shaft_tracker_math.*`, `shaft_track_assembly.*` and a dozen helper headers.

**The DTL tracker** runs only when a second camera exists, which in the current corpus is 34 of 115 swings. It takes two things from face-on through a read-only object called the `FaceOnWitness`:

- *timing*: when the coaching positions happened;
- a *visibility schedule*: when the club should be visible from behind, and how long it should look.

Nothing ever flows the other way. The rule is stated in the header: "A witness that has been fitted to the thing it testifies about is not one." If face-on could adjust itself based on what DTL said, which in turn depended on what face-on said, neither would be an independent check on the other.

**The fusion** reads both trackers' published angles and feeds neither. It produces the shaft's three-dimensional direction on frames where both cameras measured it, the backswing and downswing planes, and a list of frames where the two views disagree.

### 1.5 Where it runs

The analysis runs as a pipeline of stages (`wristProfile()` in `src/Analysis/wrist_analyzer.cpp`). The shaft-related stages, in order:

1. **Pose**, **PoseSmooth**, **Ball**. These are prerequisites. The tracker uses pose for the hands, arms and body, and the ball as a witness.
2. **ShaftStage**: runs `ShaftTracker::track`, or *reuses* a track stored in the swing document if nothing relevant has changed (§9.2).
3. **ImpactAnchor**: finds the ball by its disappearance at impact and records it on the track.
4. **ShaftLean**: the shaft-lean series.
5. Event refinement, the positions ladder and timeline fusion. The coaching positions found by the tracker join the swing's event timeline here.
6. **ClubDelivery** and **Kinematics**: shaft angle at the top, attack angle, low point, clubhead and hand speed, lag.
7. **ShaftPlane**: the face-on, single-camera swing-plane estimate.
8. **DtlPose**, then **DtlShaft**, then **ShaftFusion**, then **DtlPosture**, then **DtlShaftLie**.
9. **KinematicSequence**, then **Skeleton3D**, then **DtlSynth3D**.

On the development Mac the face-on shaft stage takes about three seconds per swing after parallelisation (it was 10.8 s before). The DTL stage takes about 2.2 s.

---

## 2. The constraints: what the camera actually sees

The algorithms in this guide were not designed in the abstract. Each was shaped by a specific, physical fact about the cameras, the golfer, the club, the ball or the data. This section sets those facts out, because almost every "why does it do *that*?" question later has its answer here.

### 2.1 The face-on camera: a 98% shutter at 150 frames a second

The face-on camera is a FLIR Chameleon3 recording 1280×1024 pixels in a raw Bayer colour pattern at **150.713 frames per second**. Its exposure is about **6.57 milliseconds** out of a frame period of 6.70 ms. The shutter is open **98%** of the time. At the club the image scale is about 3.5 mm per pixel.

That duty cycle is the single most consequential fact in this guide. When a camera's shutter is open for a short fraction of each frame, a moving object is frozen. When the shutter is open almost the whole time, each frame records *everything that happened during that interval*, smeared together.

Through most of the backswing the club moves slowly enough that this does not matter. At the top it is nearly stationary. But in the last few hundredths of a second before impact the club rotates at around **2,200–3,000 degrees per second, 15–20 degrees per frame**. During one exposure the shaft sweeps through 15–20 degrees of arc about the hands, so the frame shows a fan.

The blur is not even. A point on the shaft moves at a speed proportional to its distance from the hands, so:

- the shaft near the hands is almost sharp;
- the middle is smeared;
- the head is a long faint streak.

On a taped or painted club the fan is visible as a translucent wedge. On a bare steel shaft, whose brightness comes from a thin specular highlight, the highlight is spread over so many pixels that it disappears into the background.

The project's research report puts it plainly: "Impact is *unmeasurable* on a bare club at this exposure." Several mechanisms exist because of this:

- the blur-wedge reader (§4.5.5);
- the arm-based reconstruction through impact (§4.9);
- the synthetic track (§7);
- the honest labelling of frames as reconstructed rather than measured.

Why not shorten the exposure? Because there is not enough light. Mark's ruling of 5 July 2026: 150 fps is the camera's maximum, the ~6.57 ms exposure "cannot shorten (light budget)", the blown-out highlights in the hitting area "ARE the studio downlight doing its job", and a ring light is mounted at the camera. The markerless design document lists exposure changes, infrared illumination (it conflicts with the launch monitor), polarisers and backdrops under "deliberately not done". The tracker has to live with the blur.

### 2.2 Timestamps you can trust, and some you cannot

Frames arrive with timestamps from the host computer. These carry about ±0.4 ms of jitter, because they are stamped when the frame is handled rather than when it was exposed. The video container also declares a frame rate, and **the declared rate can lie**. One corpus session declares 30 frames per second for a 150 fps stream. So every tracker computes its own timebase as the *median* interval between consecutive frame timestamps, and never reads the container's fps. Using the median rather than the mean means one dropped frame, which leaves a double-length interval, does not distort it.

Dropped frames matter elsewhere too. On 18 August a host stall left a 594 ms hole 33 ms after impact, followed by about fifty frames stamped about 1 ms apart: the stalled host catching up. The phase model contains a rule (§4.3) for recognising such a "capture hole" and not mistaking the burst for motion.

### 2.3 The down-the-line camera

The second camera exists on only **34 of the 115 corpus swings** (sessions of 11 June, 3 July and 4 July). Nothing recorded from 5 July onward has one. Its characteristics:

- **Resolution and rate.** 512–576 pixels wide and 1024 tall, at about 149.3 frames per second.
- **No ring light.** The background is the lit simulator screen.
- **Placement.** It stands roughly on the ball–target line extended behind the ball, "at head height or above". That is not where a coach would put it (on the line of the hands, at hand height). Its position and angle **were never measured**. Analysis of an alignment stick in the frame later suggested it was yawed 4–9° and rolled 2.4–6.5° away from the ideal, and that it **moved** between swings 3 and 4 of the 4 July session.
- **Timing.** Its frames lead the face-on frames by a median of 3.2 ms. No correction is applied for this; the DTL tracker interpolates face-on information to its own frame times instead.

### 2.4 The golfer, as seen by a pose model

The tracker never asks a neural network where the club is. It asks a pose model where the **person** is. It then uses the hands, forearms and body as anchors and constraints for its own, classical, search for the club.

The pose model is **ViTPose WholeBody**, which places 133 keypoints:

- 17 body joints;
- 6 foot points;
- 68 face points;
- 21 points on each hand.

"Medium" analysis uses the smaller ViTPose-B model and "High" uses the larger ViTPose-L. Until 29 September a configuration bug on the Windows studio machine silently ran B when L was asked for. That matters, because the two can differ enough to send the tracker down a different path (§10).

From the pose, the tracker takes:

- **the grip anchor**: the average of the two hands' keypoint centroids;
- **the lead forearm**, from elbow to grip, giving φ;
- **the trail forearm**, since 1 October;
- **an eight-joint body outline**: shoulders, hips, knees, ankles.

Three properties of the pose shape the design:

1. **Hand confidence cannot be trusted.** The model's confidence numbers for the hand keypoints do not reliably go down when it is wrong. A centroid can climb up onto the wrist, or flicker by 85 px for a frame, with no warning.
2. **The grip anchor is not on the shaft.** The hands wrap around the shaft, and the centroid of all their keypoints sits a median **39 pixels** to the side of the shaft's axis in the face-on view (17–22 px down the line). Every ray the tracker casts "from the grip" therefore starts slightly off the club. Several mechanisms exist to compensate: the snap (§4.14), the lateral band in the clubhead search (§4.11.3), and the lateral-origin search down the line (§5.8).
3. **The pose is not deterministic.** On the studio machine two runs of the pose model on the same swing can differ slightly. The project keeps a "pinned pose" corpus, where the pose is computed once and stored, so tracker changes can be compared byte for byte.

### 2.5 The club, and why a shaft is sometimes bright and sometimes dark

Each analysis carries a **club record**:

- total length;
- the distance from the butt to the top of the hosel (where the shaft enters the head);
- the exposed shaft length;
- how far down the grip the hands end;
- for taped clubs, the positions of reflective bands.

When no club has been recorded, the length **defaults to 1.12 m, a driver**. That default has consequences for every speed measurement (§10).

The lab's reference club is a 7-iron, 940 mm long with its hosel at 882 mm. On 4 July 2026 it was fitted with six 25 mm glass-bead retro-reflective bands at 308, 362, 560, 758, 808 and 854 mm from the butt. Read as gaps, that is a group of two, a single, and a group of three: "2-1-3". Retro-reflective material sends light straight back toward its source, so under the ring light at the camera the bands blaze white, saturating the sensor. That makes them easy to find, and their known spacing turns them into a ruler (§4.5.3). Later the project used an **unmarked** 6-iron (955 mm, hosel at 892 mm) to develop a tracker that works without tape.

The polarity of a bare shaft depends on what is behind it:

- **Against a dark background** (a blacked-out room, a dark mat) a steel shaft catching the ring light is a *bright* line.
- **Against a blown-out, saturated white mat** it is a *dark* line.

A detector that only looks for bright lines, or only dark ones, will miss half the swing. The ridge detector (§4.5.1) decides per sample which kind to look for, by checking how bright the background is.

Down the line, without a ring light, there is a third case: the **polarity trap**. The tape's black bands and white paint alternate along the shaft against a mid-grey lit screen. A detector that adds up "brighter than the background" along the shaft sums positive and negative contributions that cancel out, while a broad bright forearm next to it scores high. The DTL tracker uses a different, polarity-free contrast measure for exactly this reason (§5.6).

### 2.6 The ball

The ball is the best independent witness to where the club is at two moments:

- **At address** the clubhead rests directly behind it. So the line from the grip to the ball is the shaft's direction, give or take the few degrees by which the head sits behind the ball.
- **At impact** the shaft passes through it.

The ball also gives a **length**: at address, the distance from grip to ball in the image is very nearly the club's projected length.

The ball is found by a separate ball detector, with its own track (`BallTrack2D`). Every rule that relies on the ball is written so that "no ball" means "do nothing". But some sessions defeat that:

- On 15 September a new impact-camera rig hid the ball from the face-on camera altogether.
- Some sessions have **two balls** on the mat, and the detector cannot know which one the golfer is addressing.

The tracker therefore checks, using the image evidence itself, whether the ball it has been given is the one the club is pointing at (§4.6.2).

### 2.7 The data the tracker was built and validated on

The corpus is **115 swings by one right-handed golfer (Mark), in one studio**, with essentially:

- one taped 7-iron (several sessions);
- one session with an untaped 7-iron in daylight;
- one session with an unmarked 6-iron;
- a gap wedge filmed down the line.

Sessions recorded as "DRIVER" in July turned out, on inspection, to be the taped 7-iron. A 61-swing subset with stored ("pinned") pose is used for regression testing: a change must leave untouched swings byte-identical and improve or hold the others.

Ground truth is thin:

- **Impact timing** has 13–14 hand-marked swings.
- **Shaft direction** has hand-marked frames on 7 unmarked-6-iron swings, and on about 32 corpus swings marked during the September investigations.
- **"Band truth"** covers taped frames. This is the band-matching engine's own output on frames where it locks. Its self-consistency is 0.3°, but against hand marks "the marked club was never 0.3 degrees".
- **Down the line** there is band truth near address only, and **no hand marks at all**.

The project's own design document is candid about what that means: "this corpus (one club/camera/athlete) validates machinery, not accuracy claims." Keep that sentence in mind through every number in this guide.

---

## 3. The mathematics, explained from first principles

This section introduces each mathematical tool the tracker uses. Each subsection says what problem the tool solves, how it works, gives a small worked example, and says where in the code it appears. Nothing later depends on remembering the details. You can read a later section and come back here when a term is unfamiliar.

### 3.1 Angles go round in circles

Angles are periodic. 359° and 1° are two degrees apart, not 358. Three operations handle this.

**Wrapping a difference.** To compare two angles, subtract them and fold the result into the range −180° to +180°. The code's `circWrap(a)` does exactly this. So `circWrap(1 − 359) = circWrap(−358) = +2`, and the two directions are 2° apart. You will see the idiom `|circWrap(θ1 − θ2)| < tolerance` hundreds of times in the tracker. Any comparison of two angles that is *not* wrapped is a bug waiting for the day the shaft passes through 0°.

**Wrapping a value.** Stored shaft angles are folded into 0°–360° (`fmod`), so the same direction always has the same number.

**Unwrapping a sequence.** A swing rotates the shaft through more than a full turn, from hanging down at address, round over the top, down through impact, and round again into the finish. If you store that as wrapped angles, the sequence jumps by about 360° every time it crosses the 0°/360° boundary:

```
wrapped:    350  355    2    9   15
unwrapped:  350  355  362  369  375
```

To differentiate it (to get a rotation rate), interpolate it, or fit a smooth curve through it, you need the continuous version. Unwrapping walks the sequence and adds or subtracts 360° whenever two consecutive values differ by more than 180°, on the assumption that the real motion between two samples is less than half a turn. This is `shaftshared::unwrap`, a copy of NumPy's `np.unwrap`.

There is a trap here that the code guards against explicitly. Interpolating between two *wrapped* angles can go the long way round the circle: halfway between 350° and 10° computed naively is 180°, when the true answer is 0°. The DTL tracker keeps an unwrapped copy of the face-on angle for exactly this reason (`dtl_face_on_witness.cpp`). The synthetic track goes further and unwraps toward the *expected* direction of rotation (§7.1.3), because over a long bracket the shortest way round may not be the way the club went.

### 3.2 Robust statistics: medians, percentiles, and why the maximum is fragile

Real measurements contain outliers: a misdetected frame, a pose glitch, a single saturated pixel. Statistics that average everything get dragged by them. The tracker uses three outlier-resistant tools throughout.

**The median** is the middle value of a sorted list. For [3, 4, 5, 6, 100] the mean is 23.6 but the median is 5. The one wild value does not move it.

**A percentile** generalises this. The kth percentile, pₖ, is the value below which k% of the data falls. p50 is the median; p90 is "the value that 90% of the data is below". The tracker often uses p90, p95 or p97 where you might expect the maximum. The maximum is set by the single most extreme value, which is often the single wrong one. One blurred, over-long ridge would otherwise set the "full club length" for a whole swing, which is why the DTL witness uses a p90 of measured lengths.

**A Hampel filter** checks each sample of a series against the median of its neighbours and replaces it if it deviates by more than a threshold. The forearm angle from pose occasionally spikes by up to 87° in a single frame. `smoothPhi` replaces any frame more than 20° from the median of its 9-frame neighbourhood before smoothing.

### 3.3 Smoothing a noisy series

Pose keypoints and per-frame measurements jitter. Before the tracker differentiates or thresholds anything, it smooths it, almost always with the same two-step recipe: **a median filter of width 5, then a Gaussian filter with σ = 2 frames**.

- **The median filter** replaces each sample with the median of the five samples centred on it. It removes isolated spikes completely while preserving genuine steps. The sequence 4, 4, 30, 4, 4 becomes 4, 4, 4, 4, 4, but 4, 4, 30, 30, 30 keeps its step.
- **The Gaussian filter** then replaces each sample with a weighted average of its neighbours. The weights follow a bell curve, exp(−k²/2σ²) for a neighbour k frames away, cut off at 4σ. This removes the remaining small jitter. Larger σ means smoother and also more *lag* and more blurring of genuine sharp changes.

At the ends of a series there are no neighbours on one side. The tracker uses SciPy's "reflect" convention: the series is mirrored (… c b a | a b c …), so the ends are not pulled toward zero.

Every one of these filters is a deliberate re-implementation of the SciPy function of the same name. The C++ was ported from a Python laboratory, and the port was validated by showing it produced the same numbers.

### 3.4 Finding the best path: costs, dynamic programming and the Viterbi algorithm

This is the most important idea in the tracker, so it gets the most space.

**The problem.** In each of several hundred frames there are 360 possible shaft directions, one per degree. In each frame the image evidence is ambiguous: the shaft, the forearm and a trouser crease may all look like lines out of the hands. But the shaft cannot teleport. Between two frames 6.7 ms apart it moves a limited amount, and in a direction set by the phase of the swing: it rotates one way going back and the other way coming down. We want the *one sequence* of directions that best agrees with the evidence in every frame *and* moves plausibly from frame to frame.

**Writing it as a cost.** It is convenient to measure badness rather than goodness. Give each candidate path a total cost:

```
Cost(path) = Σ over frames f of   E_f(θ_f)                 ← "emission" cost
           + Σ over frames f of   T(θ_{f−1} → θ_f)          ← "transition" cost
```

- **E_f(θ)** is small where frame f's pixels look like a shaft at angle θ, and large where they do not, or where a physical rule forbids θ in that frame (§4.6).
- **T** is the cost of moving from one direction to the next. It is a small penalty that grows with the size of the move (here 0.03 × Δθ², so a 10° move costs 3). It is *infinite* for moves that are not allowed at all, such as rotating the wrong way during the downswing, or more than 24° in one frame.

**Why not just try every path?** There are 360 choices in each of, say, 700 frames, so 360⁷⁰⁰ paths. That is a number with about 1,800 digits.

**The insight.** Suppose you already know, for every direction j, the cheapest way to arrive at direction j in frame f−1. Call that cost C_{f−1}(j). Then the cheapest way to arrive at direction k in frame f must extend one of those:

```
C_f(k) = E_f(k) + min over the allowed previous j of [ C_{f−1}(j) + T(j → k) ]
```

So you compute C for frame 0 (just the emission costs), then frame 1 from frame 0, then frame 2 from frame 1, and so on. At each step you also note *which* j gave the minimum: a **back-pointer**. At the last frame you pick the cheapest k, and then follow the back-pointers backwards to read off the whole optimal path. This is **dynamic programming**, and this specific form, for a sequence of hidden states with emission and transition costs, is the **Viterbi algorithm**.

**A small worked example.** Take three frames and only three candidate directions, A, B and C. Moving costs 1 to an adjacent state, 4 to jump two states, and nothing to stay. The emission costs:

| frame | A | B | C |
|---|---|---|---|
| 0 | 1 | 5 | 5 |
| 1 | 5 | 5 | 0 |  ← frame 1's evidence says C strongly (perhaps a forearm)
| 2 | 1 | 5 | 5 |

- Frame 0: C = (1, 5, 5).
- Frame 1:
  - to A: min(1+0, 5+1, 5+4) + 5 = 6 (from A);
  - to B: min(1+1, 5+0, 5+1) + 5 = 7 (from A);
  - to C: min(1+4, 5+1, 5+0) + 0 = 5 (from A).
- Frame 2:
  - to A: min(6+0, 7+1, 5+4) + 1 = 7 (from A);
  - to B: min(6+1, 7+0, 5+1) + 5 = 11;
  - to C: min(6+4, 7+1, 5+0) + 5 = 10.

The cheapest end is A at 7, and its back-pointers give A → A → A. The single-frame evidence for C in frame 1 was outvoted by frames 0 and 2, because getting to C and back would have cost more than ignoring it. That is exactly the behaviour the tracker relies on to ignore a single frame where the forearm looks more like a shaft than the shaft does.

The same property is also the tracker's characteristic failure mode, and it is worth stating now. Viterbi does not make many small mistakes; when it is wrong, it tends to be wrong for a **whole stretch**. If the evidence for the wrong structure is consistent over many frames, and the constraints do not forbid it, the optimal path follows the wrong structure for that whole stretch, confidently. Much of §4.6 and §4.18 exists to make sure the constraints *do* forbid the wrong structures that real swings contain.

**"Banded" Viterbi.** The tracker restricts the allowed moves per frame to |Δθ| ≤ w_max, and optionally to one direction only: increasing in the backswing, decreasing in the downswing. This enforces physics and also speeds things up. With 360 states and at most 49 allowed moves per frame, the work is 700 × 360 × 49 ≈ 12 million simple operations. The angle grid is circular, so state 359 neighbours state 0. The implementation is `shaftshared::viterbiBanded` in `shaft_track_assembly.cpp`.

**Negative costs.** An emission cost can be negative, which is a *reward*. When the band matcher (§4.5.3) finds the taped club with certainty, it writes −8 into the single state at the band's angle. Every other state costs at least 0, so the global path is pulled through that state. A band lock acts as a pin through which the rest of the solution is threaded.

### 3.5 Making noisy values monotone: isotonic regression and PAVA

Some quantities should only ever go one way. During the backswing the angle between the shaft and the forearm, ψ, should only increase as the wrists cock. Measurements of it wobble up and down. **Isotonic regression** asks: what is the non-decreasing sequence closest to the measured one?

Formally, given values y₁…yₙ and weights w₁…wₙ, it finds the non-decreasing x₁ ≤ x₂ ≤ … ≤ xₙ that minimises Σ wᵢ(xᵢ − yᵢ)².

The **Pool-Adjacent-Violators Algorithm (PAVA)** solves this exactly and quickly:

1. Walk left to right. Whenever a value is larger than the one after it (a "violation"), merge the two into a block whose value is their weighted average.
2. Step back and check whether the merged block now violates its left neighbour; if so, merge again.
3. Repeat until nothing violates.

A worked example, all weights 1:

```
y = [1, 3, 2, 4, 3, 5]
3 > 2 → merge to 2.5, 2.5 :  [1, 2.5, 2.5, 4, 3, 5]
4 > 3 → merge to 3.5, 3.5 :  [1, 2.5, 2.5, 3.5, 3.5, 5]   (non-decreasing: done)
```

Each block is replaced by its mean. The result is the best monotone fit. That is `pava()` in the code. For a non-increasing fit, the code flips the sign, fits and flips back.

**Making it robust.** Least squares has a weakness: one far-off value drags a whole block toward it. The fix is **iteratively reweighted least squares (IRLS) with a Huber weight**:

1. Fit.
2. Compute each point's residual r.
3. Give any point with |r| greater than a threshold c (the "knee", here 8°) the reduced weight w·c/|r|.
4. Fit again; repeat three times.

Points far from the fit then count linearly rather than quadratically, so an outlier's influence is capped. That is `robustIsotonic()`. §4.9 explains how the tracker uses it.

### 3.6 Tracking a value over time: the Kalman filter and the RTS smoother

Suppose you are measuring something that changes smoothly over time, such as how far along the shaft the clubhead is, with noisy and sometimes missing measurements. A **Kalman filter** maintains a best estimate of the hidden quantity (here the radius r and its rate of change ṙ) together with an uncertainty. At each frame it does two things:

- **Predict.** Move the estimate forward using a motion model; here "constant velocity, plus random accelerations of a typical size". Grow the uncertainty to reflect that the future is unknown.
- **Update.** If a measurement arrived, blend it with the prediction. The weights are set by the relative uncertainties: a precise measurement counts for more, a vague one for less.

A measurement that disagrees with the prediction by more than a few standard deviations (here 3σ, the **innovation gate**) is treated as an outlier and ignored. When measurements stop arriving, the filter **coasts** on its prediction, its uncertainty growing until it gives up.

A forward-only filter always lags: at each frame it only knows the past. The **Rauch–Tung–Striebel (RTS) smoother** fixes that with a second pass backwards in time, revising each estimate using what was measured later. The result is a smooth best estimate at every frame that uses all the data. The clubhead's position along the shaft (§4.11.3) is tracked this way (`HeadKf1D`).

### 3.7 Drawing a smooth curve through points: Hermite cubics and Fritsch–Carlson

The synthetic track (§7) has to draw a smooth curve through a handful of known positions: the coaching checkpoints. At each one we know the angle and the rate of rotation. A **cubic Hermite** segment does exactly that. Between two points a and b, with values p_a and p_b, slopes m_a and m_b, and duration h, use the fractional time τ (0 at a, 1 at b):

```
p(τ) = (2τ³−3τ²+1)·p_a + (τ³−2τ²+τ)·h·m_a + (−2τ³+3τ²)·p_b + (τ³−τ²)·h·m_b
```

At τ = 0 this gives exactly p_a with slope m_a, and at τ = 1 exactly p_b with slope m_b. Consecutive segments therefore join with both value and slope continuous. The curve has no corners; mathematicians call this C¹.

Cubics have a known vice: they can **overshoot**. Between two points where the value rises, a cubic with steep end slopes can bulge above the upper point before coming back down. For an angle that is meant to be rotating steadily one way, that would show the club rotating past its target and back. **Fritsch–Carlson limiting** prevents it:

- any end slope whose sign is opposite to the overall change is set to zero;
- if the two slopes, measured relative to the average slope, are too large (outside a circle of radius 3), both are scaled down until they are not.

The curve is then guaranteed monotone between its endpoints. That is `limitMonotone` in `shaft_synthesis.h`.

### 3.8 Fitting by least squares, and solving it with Cholesky

Several parts of the tracker fit a curve or a set of numbers by making a sum of squared errors as small as possible. When the errors are *linear* in the unknowns, as they usually are here, there is a clean recipe.

Write the errors as a matrix equation: the residuals are Ax − b, where x holds the unknowns. The sum of squares ‖Ax − b‖² is smallest where its gradient is zero, which gives the **normal equations**

```
(AᵀA) x = Aᵀb
```

The matrix AᵀA is symmetric and, when the problem is well posed, *positive definite*. Such a matrix can be factored as LLᵀ, with L lower-triangular: this is the **Cholesky decomposition**. Solving then takes two cheap passes, forward through L and backward through Lᵀ.

**Regularisation** means adding extra squared terms that express a preference, such as "the curve should not bend sharply". For a curve sampled at points, its bending at point j is approximately the second difference (θ_{j−1} − 2θ_j + θ_{j+1}), divided by the square of the spacing. Adding Σ(bending/σ)² to the cost just adds rows to A. The parameter σ decides how strongly smoothness is preferred over fitting the data.

The synthetic track's evidence fit (§7.1.5) and the fusion's out-of-plane curve (§6.3) are both regularised least squares solved by Cholesky.

### 3.9 Why an ellipse reveals a plane: conics and the SVD

**The geometry.** A circle drawn on a flat plate, viewed straight on, looks circular. Tilt the plate away from you and the circle looks like an ellipse. Tilt it further and the ellipse gets thinner, until edge-on it is a line. The ratio of the ellipse's short axis to its long axis is the cosine of the tilt. So if you can see a circle's image, you can recover how tilted its plane is, from one camera.

During the swing the shaft sweeps around the hands roughly on a plane, the swing plane. Track the *vector* from grip to head over a part of the swing, plot its tip, and the points trace part of an ellipse. Fit the ellipse, read its axis ratio, and you have the plane's inclination to the image.

**The fit.** Every conic section (ellipse, parabola, hyperbola) satisfies an equation

```
a·x² + b·x·y + c·y² + d·x + e·y + f = 0
```

Each data point (x, y) gives one equation that is linear in the six coefficients. With many points the system is overdetermined, and we want the coefficient vector v = (a, b, c, d, e, f), of length 1 (otherwise v = 0 is a trivial solution), that makes all the equations as nearly true as possible.

That vector is the **right singular vector belonging to the smallest singular value** of the "design matrix" D, whose rows are [x², xy, y², x, y, 1]. The **singular value decomposition (SVD)** factors any matrix into rotations and stretches. The smallest stretch is the direction in which D comes closest to sending a vector to zero. That is `fitConic` in `shaft_plane.h`, which computes the SVD with a one-sided Jacobi method.

The conic is an ellipse precisely when b² − 4ac < 0. Its axes come from the eigenvalues of the little matrix [[a, b/2], [b/2, c]]. §6.5 covers the traps: scale the data isotropically, fit the shaft vector rather than the head path, and reject needle-thin "ellipses".

### 3.10 Vectors, planes and cross products

In three dimensions, the **cross product** u × v of two vectors is a third vector perpendicular to both. Its length is |u|·|v|·sin(angle between them). Two consequences are used heavily:

- The **normal** (perpendicular) of a plane that contains two directions is their cross product.
- The **line where two planes meet** points along the cross product of their normals.

**The two-camera idea.** A camera that sees the shaft as a line at angle θ in its image cannot tell how the shaft is tilted toward or away from it. All it knows is that the shaft lies somewhere in the **plane** containing the camera's viewing direction and the image line: the "view plane". A second camera, looking from a different direction, gives a second view plane. The shaft must lie in both, so it lies along their intersection, whose direction is the cross product of the two plane normals. That is the whole of the fusion in §6, in one sentence.

When the two view planes are nearly the same plane, the intersection is badly defined: a tiny error in either angle swings it wildly. The length of the cross product of the two (unit) normals, which is the sine of the angle between the planes, measures how well defined it is. The code calls this the **conditioning**, `cond`.

**Fitting a plane through many directions.** Given many unit vectors u that should lie in one plane through the origin, the best plane's normal is the direction in which the vectors have the *least* spread. That is the eigenvector with the smallest eigenvalue of the 3×3 "scatter" matrix Σ u uᵀ. The code finds it with Jacobi rotations (`smallestEigenvector` in `shaft_fusion.h`).

### 3.11 The camera model used for 3-D work

The 3-D code treats each camera as **orthographic**: a camera is just three perpendicular unit vectors — a viewing direction d, an image-right direction and an image-down direction. A 3-D direction u appears in the image as the 2-D vector (u·right, u·down). Its image angle is atan2(u·down, u·right), and its apparent length, as a fraction of the true length, is the length of that 2-D vector.

This ignores perspective, the fact that nearer things look bigger. The approximation is stated, not hidden: at address and impact the clubhead is about half a metre nearer the face-on lens than the hands, and perspective makes the club read 10–17% longer in the image than it would orthographically.

### 3.12 Score normalisation, and the trap inside it

The ridge detector (§4.5.1) gives each of the 360 candidate directions a raw score in arbitrary units. Those units depend on lighting, exposure, the background and the club. To compare directions *within* a frame, and to use the same cost weights in every frame, the tracker rescales each frame's scores:

```
normalised(s) = clip( (s − p50) / (p97 − p50), 0, 1 )
```

A typical direction maps to 0. A direction near the frame's best maps to about 1. Every frame ends up on the same 0–1 scale.

The trap is that normalisation removes absolute strength. **A frame containing no line at all, pure noise, still produces a "best direction" scoring 1.0**, just as a frame with a crisp shaft does. To the solver the two look equally confident. Nothing in the normalised score can tell them apart. The project calls this out as the most important honesty problem in the evidence layer. The fix (§4.5.2) is an absolute floor on the *raw* score: below it, the frame's evidence is discarded rather than normalised.

---

## 4. The face-on tracker

This is the heart of the system, so it gets the longest treatment. It is described in the order the code runs. Each stage covers the problem it solves, the idea behind it, what the code does, and why the numbers are what they are.

### 4.0 Orientation: files, configuration, and the "dark idiom"

The tracker is spread over about fifteen files. The division of labour is deliberate. The half that touches video is kept separate from the half that decides, so the deciding half can be tested on plain arrays without a camera.

| File | Role |
|---|---|
| `shaft_tracker.h/.cpp` | The outer layer. Takes the swing's video window and pose, derives per-frame anchors (grip, forearm, body), builds a frame decoder, runs the "hands ladder" (§4.1), calls the decision core, then applies the post-hoc ball anchor. |
| `shaft_tracker_math.h/.cpp` | The **evidence engines**: the ridge sweep (E2), the band matcher (E1) and the steel-segment lock (E4). Pure image arithmetic. |
| `shaft_track_assembly.h/.cpp` | The **decision core**. The configuration struct `ShaftV3Config`, the phase model, smoothing, body geometry, emission costs, the Viterbi solve, the ψ reconciliation, tiering, club length, the clubhead pass, sample placement, the snap, coaching positions, the synthetic track and the self-checks. The function `decideTrack()` alone is about 1,750 lines. |
| `shaft_track_shared.h` | The parts of the core the DTL tracker reuses: filters, unwrap, normalisation, the snap search, the banded Viterbi, the frame-cache size cap. |
| `shaft_wedge.h` | Reading the motion-blur fan (§4.5.5). |
| `shaft_kinematics.h` | The "R6" kinematic predictor: where the club *should* be, given the forearm and a model of the wrist hinge. |
| `shaft_positions.h` | Finding the coaching positions P1–P8 and the end of the address hold. |
| `shaft_position_fit.h` | Re-measuring positions that were lost, by stacking frames. |
| `shaft_synthesis.h` | The synthetic track between positions. |
| `shaft_hand_clean.h` | Repairing broken hand tracks. |
| `ball_anchor.h/.cpp` | The grip→ball line, the address-hold club length and the post-hoc ball anchor. |
| `impact_geom.h` | Finding impact from the club meeting the ball. |
| `clubhead_track.h/.cpp` | The measured clubhead: a terminus search plus a Kalman smoother. |
| `club_length_fusion.h` | Combining several club-length estimates, plus a long-term memory of the club's length. |
| `hand_axis.h` | Hand centroids and the direction the hand points. |
| `shaft_frame_io.h` | Pose interpolation, raw-Bayer decoding and the parallel frame cache. |

**Configuration.** Every tunable number lives in `ShaftV3Config` (`shaft_track_assembly.h`), with a few onset constants in `src/Core/pp_tuned_constants.h` under `tuned::shaft::`. Each field has a default, its validated value, and can be overridden at run time by a dotted key in a "tuning overrides" map: `shaft.wE2`, `shaft.seg.enabled`, `positions.hysteresisDeg`, `synth.fitEvidence` and so on. That lets the laboratory tool SwingLab sweep a parameter across the corpus without recompiling.

**The dark idiom.** Almost every feature has an on/off switch, and its OFF position is guaranteed to be **bit-for-bit identical** to the code before the feature was added. A new feature lands switched off ("dark"). It is then run across the regression corpus, both to prove that switching it off really changes nothing and to measure what switching it on does. If the gate passes, it is "frozen ON". The commit and gate evidence for each switch are recorded in `docs/developer/feature_switches_developer_guide.md`. This discipline is why the code is full of comments like "byte-identical when dark". Those comments are promises that a regression test checks.

### 4.1 Getting started: anchors, cleaning the hands, and the hands ladder

**What `ShaftTracker::track` does before any club-finding begins.**

**Which frames.** It takes the camera frames whose timestamps fall inside the time span covered by the pose. Pose is computed only over the swing plus some margin, so frames outside it have no hands. With fewer than two frames the track is invalid.

**The clock.** Frames per second = 1 / (median interval between consecutive frames), for the reasons in §2.2.

**Cleaning the hands.** On 16 September (swing 2 of the Wrist_02 session, analysed with ViTPose-B), two pose faults broke the tracker:

- the lead-hand centroid had crept up onto the wrist;
- the trail-hand centroid jumped 85 px every 80 ms.

The grip anchor is the average of the two hands, so both faults moved it. `shaft_hand_clean.h` has two repair rules, applied to a *copy* of the pose (the stored pose is never altered):

- **Pair consistency.** Two hands on one grip sit close together, within about 0.65 forearm-lengths in practice. When both hands are *still* and their centroids are further apart than one lead-forearm length (or 64 px if no forearm is confidently visible) for at least four consecutive frames, one of them has wandered. The rule keeps the hand nearer to where the lead forearm says the grip should be: one forearm-length beyond the wrists, along the elbow→wrist direction. With no forearm, it keeps the *lower* hand. At address the hands are at the bottom of the arms, and a centroid that has climbed is the wrong one. The "still" and "four frames" conditions exist because applying the rule mid-swing, or to single frames, moved the finish of a clean taped swing by 55 frames and put 50 px steps into the grip track.
- **Glitch rejection.** A single-frame jump of more than 40 px that comes straight back, from a hand that was resting before and after, is replaced by the average of its neighbours. It is applied only before impact. The finish hold has its own one-frame flaps, and "fixing" those moved the finish start by 50 frames on the taped swings.

**The hands ladder.** Cleaning helped the broken swing and hurt clean ones. The onset heuristics in §4.3 had been tuned on swings whose poses flicker in exactly the way cleaning removes. So, since 1 October:

1. The tracker first runs on the **raw** hands.
2. Only if that track comes back invalid or refused (§4.18), *and* cleaning actually changed something, does it run again on the cleaned hands.
3. It keeps the second result only if it is valid and not refused.

Swings that were fine stay exactly as they were.

**Per-frame anchors.** For each frame, the code linearly interpolates the pose to the frame's timestamp and derives:

- the **grip**: the average of the two hand centroids, in pixels;
- **φ**: the lead forearm's angle, from the lead elbow to the grip. It is only used when the elbow's confidence exceeds 0.30 and the forearm is more than 8 px long; otherwise it is left blank to be filled by interpolation;
- the **trail forearm angle**, the same way from the trail elbow;
- the **eight body joints**: shoulders, hips, knees, ankles;
- optionally, the **hand-axis direction** (§4.6, switched off).

**Club geometry.** From the club record:

- the hosel position (default: club length − 58 mm, which is what the lab 7-iron measures);
- the grip end (hosel − shaft length, default 265 mm);
- the band centres;
- the hands' end.

The camera's recorded exposure is passed to the blur-wedge reader.

**The frame cache.** Every frame in the span is decoded to grey once, in parallel, and kept in memory, up to a cap of 1,200 MiB. Beyond that it decodes on demand. The later stages read each frame many times, and decoding raw Bayer data is not cheap.

**One attempt** is then:

1. run the decision core (`decideTrack`, §4.2–§4.18);
2. apply the post-hoc ball anchor (§4.19);
3. if the ball anchor rewrote the sample the address position (P1) was read from, re-read P1 from the rewritten sample.

**A refusal of last resort.** If the pair rule fired on more than half of all pose frames, the pose has no usable grip. Every witness derived from it is fiction, and the track is refused (reason 4).

### 4.2 Smoothing the forearm, and which way the swing turns

The raw forearm angle φ has gaps, where the elbow was not confidently seen, and spikes. Gaps are filled by straight-line interpolation (`interpFillNan`). `smoothPhi` then:

1. converts each angle into its x and y components (cos φ, sin φ);
2. Hampel-replaces any frame more than 20° from its 9-frame median;
3. median-filters (width 9) and Gaussian-smooths (σ = 3);
4. converts back to an angle.

Working on the components rather than the angle avoids the wrap-around problem of §3.1: averaging 359° and 1° componentwise gives 0°, not 180°. The trail forearm gets the same treatment.

**Chirality** (`chir`) records which way the arm rotates *in the image* from takeaway to the top: +1 or −1, the sign of the change in unwrapped φ. It encodes handedness as the camera sees it, without trusting a setting. It centres the "reachable cone" constraint (§4.6) and the kinematic predictor (§4.5.5). It is not used by the solver's rotation-direction rule (§4.7); see §10 for why that matters.

### 4.3 The hands-only phase model: knowing which way the club should turn

**Why the tracker needs its own phases.** The solver's most powerful constraint is that the club rotates *one way* going back and *the other way* coming down. To apply it, the tracker must know, frame by frame, which phase of the swing it is in. It could take phases from the IMU sensor, when one is worn, but the tracker is deliberately **vision-only**. The IMU segmentation is passed in and ignored, so that the camera result is never contaminated by a different instrument's errors. The phases are derived from the **hands alone**: their speed, and the forearm's rotation.

This sounds simple, and it is the part of the tracker with the most patches, because grip speed from a pose model is a messy signal. Each step below fixed a named swing.

**Step 1 — find motion runs.** Grip speed is the distance the grip moves per frame, median-5 plus Gaussian-2 smoothed. A *run* is a stretch where the smoothed speed exceeds `swSpd` = 8 px/frame for at least 7 frames. On a normal swing there are two big runs: the backswing, and the downswing plus follow-through, separated by a slow patch at the top.

**Step 2 — clean up the runs.** In order:

- **Capture-hole clip.** After impact, a gap between frame timestamps longer than three frame periods is a dropped stretch of video, not motion. The frames after it carry the stalled computer's catch-up timestamps (§2.2), which look like very fast motion in frame-index terms. Runs are cut off at the hole.
- **Bridging.** Runs separated by fewer than 10 quiet frames are merged. A slow backswing on the interpolated pose often fragments into short bursts, and unmerged they lose the "two longest runs" contest (Step 3) to a follow-through fragment. The top then lands in the downswing.
- **Late clamp.** Runs that *start* more than 1 s after impact are dropped. That is post-finish fidgeting, not swing.
- **Early clamp.** Runs that *end* more than 1.6 s before impact are dropped. That is address waggling. Added after a 10-frame waggle on the 6-iron session out-ranked a fragmented backswing and left the swing with no backswing at all.
- **The "m3gate".** A run built by bridging three or more fragments must have travelled somewhere: its net start-to-end displacement must be at least 0.2 × its total path length. On one swing the pose "flapped" during a presentation move, producing seven short fast bursts that bridged into a long run going nowhere (net/path 0.013, against ≥ 0.34 for every genuine merged run). That run had won the contest.

**Step 3 — find the top.** Keep the two longest surviving runs, in time order.

- If there are two (backswing and downswing), the **top** is the slowest frame in the gap between them.
- If there is only one (a swing with no pause at the top), the top is where the grip is highest in the image.
- The start of the first run is provisionally the takeaway, `bs0`. The end of the last is where the finish begins, `fin0`.

**Step 4 — repair a collapsed top.** On 15 of the 61 regression swings the two longest runs were the *downswing and the follow-through*, not the backswing and downswing, so the "top" landed at impact. When the true impact time is known (from the acoustic trigger), a real top cannot be within 120 ms of it. So if it is, the top is re-derived:

- the highest grip point between 600 ms and 120 ms before impact;
- refined to the slowest frame within ±100 ms of that point.

If that leaves the takeaway *after* the new top, a candidate for the real takeaway is remembered for Step 6. This repair raised the number of swings with a located delivery position (P6) from 46 to 59 of 61.

**Step 5 — impact.** The impact frame is the frame nearest the supplied impact time (from the acoustic or marker trigger). Without one, it is the first frame after the top where the grip has come back down to within 20 px of its address height.

**Step 6 — find the true takeaway.** `bs0` is where the grip first exceeded 8 px/frame. That is late: the club starts rotating about the wrists, and the forearm starts turning, before the hands move fast. Three rules walk it back:

- **A1, speed.** Walk back while the smoothed grip speed stays above 1.5 px/frame.
- **A2, forearm rotation.** Walk back while the smoothed forearm rotation rate stays above 0.25°/frame. The forearm turns before the hands translate. Take the earlier of A1 and A2.
- **The no-return veto.** On real footage the interpolated grip *never* truly rests. It keeps a floor of 2–4 px/frame through every fidget, so A1 and A2 can walk right back through the golfer's waggles to the deep stillness before them, 0.5–1.5 s too early. The veto looks for the **last moment the hands ever come back to**: the last frame r such that, at least 15 frames later but before `bs0`, the smoothed grip returns to within 7 px of where it was at r. Waggles and settles are always revisited, because the golfer returns to address. The takeaway is departure for good. So the last revisited point is the final settle before the takeaway, and the onset may not be earlier than that. It is published as `onsetFloor`, so the later address-position search (§4.16) cannot walk past it either. Frozen ON on 17 July: median Address error on the 17 truth swings fell from 0.564 s to 0.060 s.
- **A3, the clamp.** With a known impact time, the takeaway is forced into the window [impact − 1.6 s, impact − 0.55 s]. A swing outside it is implausible. If the walk-back from a collapsed `bs0` would land at the near edge (impact − 0.55 s), the remembered candidate from Step 4 is used instead. `onsetRule` records which rule produced the onset: 0 walk-back, 1 reseed, 2 near-edge clamp, 3 far-edge clamp.

**Step 7 — a backstop.** If the top is still at or before the onset, it is re-derived after the onset, the same way as in Step 4.

**Step 8 — check itself.** Some failures leave a fingerprint. If the takeaway came from the clamp's near edge, the "Address" lands exactly 0.549 s before impact, to the microsecond, on swing after swing. That is the "manufactured address". The model is marked **suspect** if:

- that happened; or
- the backswing (top − onset) is shorter than 400 ms; or
- no motion run existed at all.

`segmentPhasesChecked` then re-runs the whole model with the speed threshold lowered by a quarter, up to twice (8 → 6 → 4.5 px/frame), and takes the first result that is not suspect. A slow takeaway at 2.5–6 px/frame never formed a run at the default threshold. On the 21-swing comparison set this fingerprint marked 5 of 6 broken runs and 0 of 36 clean ones.

**Step 9 — label every frame.**

| frames | label |
|---|---|
| before the onset | Addr |
| onset to top − 2 | Backswing |
| top ± 2 | Top |
| impact ± 12 | Impact |
| between top and impact, outside those | Downswing |
| after impact up to `fin0` | Thru |
| after `fin0` | Finish |

The same model, run on a coarse first-pass grip track, is also used by the pose runner to decide which part of the video deserves its expensive second pass (`estimateSwingSpanUs`).

### 4.4 Preparations: the evidence span, the scene background, the body outline

**The evidence span.** The expensive evidence computation runs only on frames from 100 ms before the takeaway to 100 ms after the finish begins. That gave a 2.3× speed-up with no measurable change in accuracy. Frames outside the span get a flat cost row, so the solver **coasts** through them: it simply continues whatever angle it had at the edge of the span. This is significant for the address hold, which is mostly *before* the span. The tracker used to carry a mid-backswing angle backwards over the whole address. §4.6.2 and §4.8 explain the fixes.

**The scene median.** For every pixel, take the median value over every 8th frame of the clip. Anything that moves through the frame — the club, the arms — occupies a given pixel for only a minority of frames, so it vanishes from the median. What remains is the static scene. Subtracting it from a frame, |frame − scene median|, highlights only the things that are moving. That is the second evidence channel (§4.5.1).

**The body outline, for the "C2" constraint.** The club cannot pass through the golfer's body. The eight smoothed joints are wrapped in their **convex hull**, the smallest convex polygon containing them, like a rubber band stretched around them. The hull is stored as a set of edges with outward-facing normals. A point is "inside the body" if it is within 34 px outside every edge. Using the geometric hull rather than rasterising and dilating a mask was twice as fast and gave identical accuracy; the raster version survives as a test oracle.

**Still runs.** Stretches of at least 25 frames where the grip moves less than 0.8 px/frame. These are used to demand corroboration before a still frame is trusted (§4.10).

### 4.5 The evidence engines: four ways of seeing a shaft

The tracker has four independent ways of reading the image for the shaft. They differ in what they need and in what they can say:

- **E2, the ridge sweep.** Asks only "is there a line here?" It works on any club, every frame. It is the weakest evidence and the most available.
- **E1, the band match.** Asks "do I see the known pattern of reflective bands?" It only works on a taped club, but when it locks it gives direction, scale and position along the club, with no ambiguity.
- **E4, the steel-segment lock.** Asks "do I see a run of bare steel that starts and ends where a club's grip end and hosel would?" It gives scale and position on an *unmarked* club, using the club's own structure as a ruler.
- **The blur wedge.** Asks "do I see a fan, and where are its edges?" It works exactly where the others fail: through the fast part of the downswing.

#### 4.5.1 E2, the polarity-aware radial ridge sweep

**The idea.** From the grip, cast a ray outwards in each of the 360 directions. If a shaft lies along a ray, then all along that ray the pixels are brighter (or darker) than the pixels just beside it. Measure that contrast along each ray, add it up, and the ray along the shaft scores highest.

**What the code does** (`ridgeSweep` in `shaft_tracker_math.cpp`). For each direction θ, step outwards from the grip in 2 px steps from a radius of 8 px to 470 px. At each step:

1. **Measure the background beside the line.** Take four pixels perpendicular to the ray, at 9 and 12 px on each side, and use their median. The median of four is robust to one of them landing on something bright.
2. **Measure the line itself.** Take five pixels across the ray at offsets −2 to +2. In the bright regime keep the brightest; in the dark regime the darkest. Taking the extreme of five means a line one or two pixels off the exact ray still counts. That matters because the grip anchor is not on the shaft (§2.4).
3. **Decide the polarity.** If the background is brighter than 200 (on a scale of 0–255), the scene behind is blown white and the shaft must show *dark*: evidence = background − darkest − 12. Otherwise the shaft must show *bright*: evidence = brightest − background − 12. The "−12" corrects a bias: the brightest of five noisy pixels on a perfectly flat background is still above the background by a few grey levels, and without the correction flat regions would accumulate spurious score. The evidence is clipped to the range −30 to +90, so a single saturated pixel cannot dominate.
4. **Accumulate.** Add the evidence along the ray. After each step j, compute the running total divided by √(j + 8). The ray's **score** is the largest value of that ratio at any radius beyond 98 px (8 px start + a 90 px minimum shaft). The radius where that maximum occurs is the ray's **terminus**, where the line seems to end.

Why divide by a square root? Summing evidence rewards long lines, which is right, since a shaft is long. But summing alone would let a long, faint, *noisy* ray beat a shorter, crisp one. Dividing by √length is the standard compromise: a consistent signal grows like the length, noise grows like its square root, so the ratio favours consistent signal. The 90 px minimum means very short lines — a knuckle, a cuff — cannot win.

5. **Support.** The fraction of steps up to the terminus where the evidence exceeded 8. A shaft is *continuous*. A ray that scores well because of a few very bright spots will have low support. Support is used later as an absolute check (§4.10).

Two sweeps are run per frame:

- the **raw channel** on the grey frame, with the polarity rule above;
- the **difference channel** on |frame − scene median|, which only highlights moving things. It is always bright-on-dark, and uses the mean of three pixels across the ray.

Sampling is "nearest pixel", not interpolated. That is a deliberate match to the Python original, so the C++ port could be verified number for number.

#### 4.5.2 Normalising, and the absolute floor

Each channel's 360 scores are normalised to 0–1 by percentiles, as described in §3.12. For each direction, the frame's evidence is the **larger** of the two channels' normalised scores, and its support likewise.

The trap of §3.12 — every frame has a winner at 1.0, even a frame of noise — was closed on 10 August by the **absolute floor**, `evAbsFloor` = 100. Before normalising, the code looks at each channel's raw p97 score. If it is below 100, that channel saw no line at all in this frame. It is "drowned": its scores and support are set to zero, so it can neither steer the solver nor vouch for the frame. The other channel can still speak. The floor was chosen to be inert on ordinary frames, whose raw p97 sits above 210. It catches only frames that are blank: deep blur, darkness.

#### 4.5.3 E1, the retro-reflective band match (taped clubs only)

**The idea.** If the club has bands of reflective tape at known distances from the butt, then any frame where you can see four or more of them, in a straight line, spaced in the right *ratios*, tells you:

- the shaft's direction;
- the image scale, in pixels per millimetre along the shaft;
- exactly where along the club the grip is.

Because you know which end the pattern starts from, it also tells you which way the club points: no 180° ambiguity. It is a ruler painted on the shaft.

**What the code does** (`frameBandMatch`).

1. **Find bright blobs.** Threshold the grey frame at 235 (near saturation), find the connected bright regions of 3–2,500 pixels within reach of the grip, and keep the 20 largest. Under the ring light the bands saturate into such blobs.
2. **Hypothesise lines.** For each pair of blobs, consider the line through them. Collect every blob within 4 px of the line. Discard lines that pass more than 80 px from the grip; the shaft goes through the hands.
3. **Project.** Measure each collinear blob's position along the line, from the grip. Try both directions, because the code does not yet know which way the club points.
4. **Match the pattern.** For every pair of projected blobs and every pair of known band positions, ask: if these two blobs are those two bands, what scale and offset does that imply?
   - The scale s (pixels per mm) must be between 0.05 and 0.55.
   - The offset r0 (distance from the butt to the grip anchor) must be between −50 and 260 mm.
   - Given s and r0, predict where every other band should appear, and pair each prediction with the nearest unused blob within a tolerance.
   - Score the hypothesis by how many bands it explains, then by how tightly (root-mean-square error).
   - Refine s and r0 by least squares over the matched pairs, insisting the bands appear in order along the shaft.
5. **Accept** only if:
   - at least 4 bands match, within 1.5 px RMS (for exactly 4) or 3 px (for 5 or more);
   - the **dark gap** check passes. Between two bands of the same group (less than 60 mm apart) the bare steel must dip below a brightness of 222. That rules out a single continuous bright streak, such as a specular highlight, being chopped into fake "bands". With only four matches the dark gap is compulsory.

A band lock is the strongest evidence the tracker has. Against its own band truth the median error is **0.3°**. On the 61-swing regression corpus the final track agrees with the band locks to 0.26° (p50) and 0.49° (p90) on 1,015 band frames. Against human marks it is not that good (§10).

Its limitation is coverage. Bands only saturate into distinct blobs when the club is up and catching the ring light. At address the reflection only marginally exceeds saturation and fragments. Band locks cover only about **26%** of the swing's frames.

#### 4.5.4 E4, the steel-segment lock: the bare shaft as its own ruler

**The idea.** In September the project asked whether the tape was needed at all. An unmarked shaft still has structure along it, at known distances from the butt:

- the rubber **grip**, which ends at a known point;
- then exposed **steel**;
- then the **ferrule**, a short dark plastic ring;
- then the chrome **hosel** of the head.

Two of those landmarks at known millimetres give exactly what two bands give: a scale and an offset. Along-shaft distances survive motion blur, because blur smears *across* the shaft, not along it. The landmark near the hands identifies the grip end, so there is no 180° ambiguity either.

**What the code does** (`segmentLock`). Along one ray from the grip, sample every pixel using exactly the same per-sample reduction as E2 (the two engines share code so they cannot drift apart). Then:

1. **Classify each sample** as bright (evidence ≥ 30), dark (≤ 8) or in between.
2. **Find the steel run.** Stretches of at least 5 bright samples are "anchors". The run is the longest chain of anchors whose gaps are short (≤ 80 px) *and* whose background in the gap matches the background beside the anchors (within 60 grey levels). Why the second condition? Bare steel's highlight drops out in patches of 50–70 px, and those holes must be bridged. A hole where the steel's highlight dropped out has the scene's background beside it. A clubhead's interior does not. Without the background test, a run could be dragged right across the clubhead by its far rim. The run must be at least 60 px long, and at least 60% of its samples must show some evidence.
3. **Read the far end** — what happens in the 25 px after the run:
   - the background shifts toward the run's own brightness: a wide bright (or dark) object follows, the **head**;
   - a few dark samples followed by a bright run of at least 5 px: the **ferrule** then the hosel;
   - just dark: the **end of the steel**;
   - otherwise there is no landmark and the probe fails.

   A run that ends within 25 px of the image edge has no measurable end, since the club may simply leave the picture. For unmarked clubs, a short dark dip near the end is read as a ferrule the run had bridged over. The measured end then refers to "hosel − 12 mm" (ferrule resolved) or "hosel + 40 mm" (the run reached the end of the hosel).
4. **Read the near end** — what comes just before the run, within 45% of the search radius:
   - a bright, wide, evidence-free stretch is the bloom of the **hands**. The run starts at the hands' edge, a known `handsEndMm` from the butt (default 180 mm, measured once with a tape per club);
   - a dark stretch followed by a step up in brightness is a visible **grip end**.

   Analysis of the corpus showed the visible grip end usually is not there: a light-coloured grip reads as bright as steel and belongs to the run. The hands' edge, measured at 165 mm from the butt (p10 121, p90 208) on the taped club, turned out to be the landmark that works.
5. **Fit.**
   - With both landmarks (**FULL** mode): scale = pixel distance between them ÷ millimetre distance between them, and the offset follows. On a banded club, any bands found along the run join the fit as extra landmarks.
   - With only the far end (**TERMINUS** mode), the scale is taken from the swing's other FULL locks.
6. **Gate.** Scale between 0.05 and 0.55 px/mm. Offset within range. The implied projected club length between 40% and 120% of the length measured from the ball. A FULL lock's scale within 25% of the swing's typical scale.
7. **Refine.** The probe is repeated at ±1° around the direction in 0.5° steps (±5° at address). A 1° grid is 4.4 px sideways at 250 px out, wider than the ±2 px on-line window, so a hairline of steel can fall between rays.

E4 is *not* run over all 360 directions. It is probed **after** the solver, along the solver's chosen direction, and along the band's direction where E1 locked. The design notes that raw ridge candidates are won by "a crease or the lead arm … on a third of frames", and a segment lock along a crease is a confident wrong answer. §4.8 describes how the probes are run and used.

On the regression corpus E4 against the band locks reads 1.0° median, with a scale error of 6.2% and a near-end position error of about 1 px. On the unmarked 6-iron it locks on 40% of frames (against 54% for the taped club, whose tape helps). Turned on together with the snap (§4.14) on 10 September, the "markerless stack" made the *bare* club track better than the *taped* one against human marks: direction p50 1.6°, p90 4.5°.

#### 4.5.5 The blur wedge: reading the fan instead of fighting it

**The problem.** Through the fast part of the downswing the shaft is a fan, not a line (§2.1). E2 looks for a thin straight ridge at least 90 px long and finds little. E1 and E4 need structure along the shaft that the blur has smeared away. Before the wedge existed, these frames were filled by the solver's smoothness and by arm-based reconstruction, which is to say they were guessed.

**The idea.** Instead of fighting the blur, measure it. Two ingredients are needed: a *prediction* of where the fan should be, so the search can be confined to a narrow sector, and a way to read the fan's edges.

**The prediction (R6, `shaft_kinematics.h`).** A golf club and the lead arm form a double pendulum: arm from the shoulder, club from the wrists. The club's direction is roughly the forearm's direction plus the angle by which the wrists are cocked. That angle follows a fairly stereotyped course through the swing:

- small at address;
- about 90–100° at the top;
- held through most of the downswing;
- released abruptly just before impact;
- reversed through the finish.

The code holds this as a 9-point table, indexed by **swing progress**: 0 at the takeaway, 0.5 at the top, 0.9 at impact, 1 at the finish. Each entry gives a mean angle and a spread. So:

```
predicted club direction = φ (measured forearm) + chirality × β̂(progress)
predicted rotation rate   = how fast that prediction changes, frame to frame
search envelope           = prediction ± 3 × spread
```

Crucially, this prediction uses the measured forearm and the table, **never the solver's own answer**. If the wedge searched around the solver's current guess, a wrong guess would find its own reflection.

**When it triggers.** On frames inside the evidence span where the predicted rotation rate exceeds 720°/s. Below that, the thin-line machinery is trusted.

**The proximal sweep.** A second ridge sweep, restricted to the envelope's directions and to the *inner* part of the shaft: out to 35% of the search radius, with a minimum line of 40 px rather than 90. Image speed grows with distance from the grip, so the inner shaft blurs least.

**Reading the fan, two ways:**

- **Before the top: the plateau centroid** (`measureWedge`). Find the contiguous run of directions whose response exceeds half the absolute floor and spans at least 2°, with the most total response. Take its response-weighted average direction. This is taken as the shaft at mid-exposure.
- **From the top onward: the edges** (`measureWedgeEdges`, since 29 September). Looking closely at the response across a fan revealed something the centroid had been hiding. A ridge detector does not respond across the *body* of a fan. It responds at the fan's two **ends**: where the shaft was when the exposure started, and where it was when it ended. On swing 4 of 18 August at impact the two peaks were at 106° and 88°. The centroid sat near the larger peak, which was usually the *trailing* one. That was the source of a long-standing +13° bias in impact shaft lean. So the code:
  1. smooths the response;
  2. finds the two highest peaks, at least 35% of the maximum and at least 4° apart;
  3. refines each to a fraction of a degree by fitting a parabola through its top three points;
  4. declares the **leading** peak — the one further along the direction of rotation — to be the shaft at the *end* of the exposure, which is the frame's timestamp.

  The trailing peak is only accepted as the exposure's start if the gap between the two matches the predicted rotation during one exposure (rate × exposure time) within tolerance. At slow rotation the "second peak" is usually an arm.

  Against 123 hand-marked downswing frames:
  - leading edge: −1.2° median, typical size 4.6°;
  - the tracker as it had been: +6.4° (8.2°);
  - the trailing edge: +10.4°.

  At the fastest frames (16°+ per frame) the old tracker was +15.9° and the leading edge +1.6°.

  The leading edge is used only from the top onward. Around the top the predicted rate is high (700–980°/s) while the club is actually *reversing*, so "further along the rotation" is meaningless there. Trying it before the top carried a whole swing's backswing onto a structure 70° off the shaft.

**How the wedge enters the solution.** For each triggered frame with a measurement:

- A Gaussian **well**, a reward, is added around the measured direction. Its depth is 6 cost units. Its width is the edge's own precision (4.5°), or, for a centroid, the larger of half the fan width and half the predicted sweep. It is clamped so it can never be deeper than the band well: a band lock always outranks a wedge.
- On every triggered frame, directions outside the predicted envelope pay a small penalty (4 units, the **kinematic cone**). This "defunds" a wrong path that had sat on strong but off-shaft structure: the evidence for it was real, just not the shaft.

**The exposure time** is taken from the camera's recorded value where available, else 99% of the frame period. An earlier scheme estimated exposure from the fan widths. It pinned at its 8 ms limit on every swing, because the fan width is not rotation × exposure: it is about 30° at any speed, a property of the detector, not the blur.

**A better table that is not used.** A refit of the wrist-hinge table to hand-marked truth, indexed by *seconds before impact* instead of swing progress (release happens a roughly fixed time before impact, not at a fixed fraction of the swing), cut the table's p10–p90 error from 74° to 21°. It stays **switched off**. Its fitted curve holds the wrist angle flat and releases it in 140 ms, so its predicted rate crosses the 720°/s trigger on 30% fewer frames, costing 38% of the wedge's measurements. Better centre, worse trigger. Separating the two is recorded as the prerequisite for turning it on (`docs/research/wrist_cock_model.md`).

### 4.6 Turning evidence and physics into costs

With the evidence in hand, each frame inside the evidence span gets a row of 360 costs, one per direction, which the solver will minimise. This is where physics enters. The design names four "fundamentals", physical facts about a golf club in a swing:

- **C1, butt termination.** The club *starts* at the hands. A line that continues straight through the hands and out the other side is a scene line (a mat edge, a door frame), not a club.
- **C2, free space.** The club never passes through the golfer's body.
- **C3, one reversal.** The club rotates one way going back and the other way coming down. This is enforced by the solver's allowed moves (§4.7), not by costs.
- **C4, arm coupling.** The club cannot point back up the lead forearm (the wrists cannot bend that far), and it stays within a reachable cone around the forearm.

The cost row is built in a fixed order. **The order is part of the contract**: later terms are designed to override earlier ones, and the band reward must come last so nothing can outweigh it. Cost units are arbitrary, scaled so that the evidence term spans 0 to 10.

| # | Term | What it does | Cost |
|---|---|---|---|
| 1 | **Evidence** | 10 × (1 − normalised evidence). A direction the frame strongly supports costs ≈ 0; one it does not support costs ≈ 10. With a band lock, the band's direction is first raised to full evidence. | 0 to 10 |
| 2 | **C4, lead forearm** | Directions within 12° of "back up the lead forearm" (φ + 180°). | +16 |
| 3 | **C4, trail forearm** (since 1 Oct) | The same for the trail forearm, only up to and including the top. | +16 |
| 4 | **C4, wide cone** | Except at address, the top and the finish: directions more than 150° from φ + chirality × 110°. This rules out a 60° sector behind the swing. | +4 |
| 5 | **C1, reverse ray** | If the *raw* ridge evidence pointing the *opposite* way (θ + 180°) is above 0.45, the line passes through the hands. Excused when θ continues straight on from the forearm, because then the reverse ray runs up the arm. | +10 |
| 6 | **C2, body** | Mid-swing only. Sample the ray from 45 to 470 px every 14 px. If more than half the samples fall inside the inflated body outline, the shaft would be passing through the golfer. | +13 |
| 7 | Hand-axis prior | **Switched off.** Directions more than 35° from the direction the hand points. | +6 × confidence |
| 8 | **Band reward** | The band-lock direction is set to −8, overriding everything above. | −8 |

Two further edits are made afterwards, outside the per-frame loop:

- **the blur-wedge well and kinematic cone** (§4.5.5);
- **the address ball well**, after a check that the ball is the right one (§4.6.2).

The band reward is re-asserted after each, so a band lock still wins.

#### 4.6.1 Why the constraints look the way they do

**Why +16 for pointing up the forearm?** It has to be large enough that a frame where the forearm is a vivid, long line and the shaft is faint still prefers the shaft. The forearm's evidence can be the frame's maximum (normalised 1, cost 0), and a faint shaft's might be 0.4 (cost 6). So the penalty must exceed the 10-unit evidence span, comfortably. The same reasoning sets C2 at +13 and C1 at +10.

**Why is the reachable cone so wide (150°) and so weak (+4)?** In the original design C4 was a tight cone around where the club *should* be relative to the arm. It did not survive contact with real pose data. The forearm angle "spikes to 87°/frame" on pose glitches, so a tight cone sometimes centred itself in the wrong place and penalised the true shaft. It was widened to 150° and switched off where the arm's direction is least informative: at address, the top and the finish. The exemplar notes carry the instruction "Do not tighten the cone."

**Why add the trail forearm, and only up to the top?** On 16 September swing 2 the takeaway "walked up the trail forearm", directions 206–252° from the grip toward the trail elbow, with nothing in the way: only the *lead* forearm was vetoed. After the top the trail elbow is often hidden behind the body and its estimated position is noise. On 15 September swing 8, applying the veto after the top cost 66 of 119 measured frames in the through-swing.

**The hand-axis prior.** ViTPose WholeBody gives 21 keypoints per hand, so the direction from the wrist root to the middle knuckle approximates the shaft direction at the grip. A cost term penalising directions far from it was built. Its corpus gate **was never run**, so it is off.

#### 4.6.2 The address: trusting the ball, and checking it is the right ball

**The problem.** The address is the moment a coach most wants to see, and the one the tracker found hardest. The club is still, so the difference channel sees nothing. The ring light may not catch a still shaft. And most of the address hold lies *before* the evidence span, where the solver simply coasts. On 15 September swing 1, the solver carried a mid-backswing direction (196°) backwards over the whole address hold and published it as the address position.

**The witness.** At address the clubhead sits behind the ball, so the line from the grip to the ball *is* the shaft's direction, to within a few degrees.

**Measuring the ball at address** (`medianGripBallLenPx`, `ball_anchor.cpp`). First the tracker decides whether it has an address ball at all:

1. **Choose the window.** The last stretch of stillness around the takeaway: the address hold proper. Not the teeing-up period before it, when the hands are near the ball; that gave lengths 107–178 px short.
2. **Cluster the ball detections** in that window. Take the component-wise median position, keep only detections within 6 px of it, and require at least five. Using the median of all detections, rather than the first one found, means a detector warming up on the wrong thing for the first few frames cannot veto the rest.
3. **Apply a golf-sense gate.** Face-on, the ball is always below the line of the ankles and between the feet (allowing 10% of the frame width either side). A detector that locked onto the clubhead of a driver, 130–175 px above the real ball, failed this test.
4. **Accept.** The median grip-to-ball distance becomes the club's projected length at address (called **A1** or **L_px**), and the cluster centre becomes the **address ball**.

**The decoy check** (since 1 October). The gates above catch gross mistakes but not a *second, real ball*. On the 3 July untaped session every swing's ball line pointed 115–124° while the club was really at about 97°: the golfer was addressing a different ball. On 15 September swing 2 the accepted ball was 200 px from the one being addressed. So, before trusting the ball, the tracker asks the image. On the still frames of the address that do have ridge evidence, it looks for the best "real" line, meaning one that:

- scores at least 0.45 normalised;
- has support of at least 0.4;
- is not along either forearm.

Then:

- If that line is more than 15° from the ball direction, and the ball direction has less than half its evidence, the frame votes **"decoy"**.
- If the best line is at the ball direction, or the ball direction itself has a real line, the frame votes **"ball"**.
- A frame with no real line anywhere votes for nothing.

Three or more decoy votes that outnumber the ball votes two to one, and the ball is **dropped**: no well, no A1 length, and the track is flagged `ballSuspect`.

There is a second, quieter condition. The ball is only **trusted** if at least one frame actually confirmed it. On the 3 July session, in daylight with bare steel, *no* frame showed any line at all, so neither side got a vote. Trusting the ball anyway moved the address from 5° off the hand marks to 24° off, because that session's ball was the wrong one.

**The ball well.** If the ball is trusted, every address-like frame gets an extra cost of 12 × min(1, |θ − θ_ball| / 30°). Address-like means before the evidence span, or labelled Addr *and* still. θ_ball is the direction from that frame's grip to the address ball. The cost is zero at the ball direction, rises linearly, and saturates at 12 (more than the whole evidence span) from 30° away. Frames before the span have no evidence at all, so the well alone decides them. That is the intent.

### 4.7 The solve: banded Viterbi

The solver of §3.4 now runs over every frame, using each frame's phase label (§4.3) to decide which moves are allowed:

| Phase | Largest move per frame | Direction allowed |
|---|---|---|
| Addr | 3° | either |
| Backswing | 9° | increasing only |
| Top | 5° | either |
| Downswing | 16° | decreasing only |
| Impact | 24° | decreasing only |
| Thru | 16° | decreasing only |
| Finish | 11° | decreasing only |

Each move also costs 0.03 × (degrees moved)², so the solver prefers gentle changes.

**Why these limits?** They are generous upper bounds on how fast a club rotates in each phase at about 150 fps. A real club's fastest downswing rotation is about 15–20° per frame, and the impact zone allows 24° to leave room. Tighter limits clip real motion; looser ones let the path jump to a distractor. The direction rule is the "one reversal" constraint, C3, and it is the most powerful single rule in the tracker. During the downswing the club *cannot* be rotating backwards, which rules out an enormous family of wrong paths.

**Where the direction rule comes from.** "Increasing" and "decreasing" here mean increasing or decreasing θ. For a right-handed golfer filmed face-on with the target to the image's right — the whole corpus — the shaft's image angle does increase through the backswing: from about 95° (hanging down) through 180° (pointing left) toward 270° (up). The rule is written in terms of θ directly, *not* in terms of the chirality the tracker measures. §10 discusses what that means for a left-hander.

The output is one direction per frame, `dp.thetaDeg`. That includes the frames outside the evidence span, where the solver either coasts or follows the ball well.

### 4.8 Probing for bare steel along the chosen direction

With the solver's directions known, the steel-segment engine (§4.5.4) is now run *along* them. It runs when it is switched on (it has been since 10 September) and the club record is complete enough to know where the grip end and hosel are.

**Which direction to probe.**

- Normally, the solver's direction for that frame.
- On address-like frames with a trusted ball: the grip→ball direction, with a wider refinement (±5°) and a tighter length gate (±15% of the ball-measured length). At address the head sits behind the ball, about 3° off the grip→ball line at this framing. The club is also flat in the image plane there, so its apparent length should match the ball measurement. A trouser crease locks "long and bright but 40% short".
- Where the band matcher locked, the band's direction as well.

**Which frames.**

- Every evidence-span frame.
- *Also* still frames outside the span — the address hold before it and the held finish after it — probed along the nearest in-span direction. This is what lets an unmarked club's address and finish appear at all.
- Address-like frames with **no** trusted ball are **not** probed. Probing along the solver's coasted direction there locks onto trouser creases.

**Two passes.** The first pass uses no prior knowledge. If at least five frames achieved a FULL lock (both landmarks), the median of their scales becomes a scale prior. A second pass re-probes the frames that did not lock, now allowing TERMINUS locks: frames whose near end is hidden in the hands' bloom but whose far end is visible.

**What the locks are used for.**

- A frame with a segment lock within 6° of the solver's direction earns the **SEG** tier (§4.10).
- For the ψ reconciliation (§4.9) and the "is a lock nearby?" test (§4.10), a band lock *or* a segment lock counts as "a lock".
- A **length** for the club-length fusion (§4.11): the median of scale × (club length − offset) over FULL locks. It uses address-phase locks if there are at least five, because the club is in the image plane there. Otherwise it uses the least-foreshortened quarter of the swing's locks, because a mid-swing median "is 40–50% short".
- When no band ever locked, the segment locks' typical scale and offset supply the "band-corrected" rung of the length ladder.

### 4.9 Reconciliation: using the forearm as a witness through impact

**The problem.** Through the impact zone the evidence is at its weakest (§2.1). The solver's answer there owes more to smoothness than to pixels. Is there another witness?

**The idea.** ψ = θ − φ is the angle between the shaft and the forearm, the wrist hinge as seen in the image. Physically, through the backswing the wrists cock, so ψ should only increase. Through the downswing, impact and follow-through they release, so ψ should only decrease. If the shaft's direction is uncertain but the forearm's is known, a smooth, monotone ψ curve fitted to the trustworthy frames says where the shaft must be on the untrustworthy ones: θ = ψ + φ.

**What the code does** (`reconcilePsi`):

1. Split the swing into two blocks: the backswing, and downswing + impact + follow-through. Leave out a window around the top, from 3 frames before to 12 frames after, where the wrists are changing direction and release lags.
2. In each block, unwrap ψ and fit a **robust isotonic regression** (§3.5): increasing for the backswing, decreasing for the other block. Weight each frame by how much its θ is trusted:

   | frame type | weight |
   |---|---|
   | band lock | 8 |
   | segment lock | 6 (FULL) or 3 (TERMINUS) |
   | ordinary ridge measurement | 2 |
   | prediction | 0.3 |

   The Huber knee is 8° with three reweighting rounds, so a few wild frames do not bend the fit.
3. Record every frame's distance from the fitted curve.
4. **Only on Impact-phase frames without a band lock** is θ actually *changed*, to ψ_fit + φ: the forearm's account of where the shaft is.

**Why only impact?** Because applying it more widely made things worse. The comparison (`club_tracking_v3_design.md` §8.1) on the ten-swing taped corpus:

| measure | reconciliation off | applied at impact + through | **applied at impact only (shipped)** |
|---|---|---|---|
| through-swing p90 error | 3.9° | 5.5° | 3.9° |
| through-swing frames measured (of 820) | 678 | 649 | 671 |
| downswing frames worse than 15° | 1 | 0 | 0 |

After impact the forearm *rolls*. The lead arm rotates about its own length as the hands turn over, "a third rotational degree of freedom a face-on view cannot see". ψ stops being monotone in the image. The rule is only physically valid from address to impact.

**A later refinement** (29 September). When the blur wedge has measured the fan's leading edge on an impact frame, *and* the forearm reconstruction would move θ by more than 6° from the solver's answer, θ is set to the **leading edge** rather than to the forearm's reconstruction. On 33 corpus swings the forearm reconstruction on those frames read +112° median from the hand marks. The leading edge read +1.0°. That is what had put two swings at +100° of impact shaft lean. Where the two agree, the frame is left alone. Substituting the edge everywhere replaced good readings with clutter on one swing.

### 4.10 How much is each frame worth? Tiers, confidence, coverage, validity

Every frame now gets a **tier**: a label saying what earned its angle. The tiers, in the order they are tested:

1. **BAND.** The band matcher locked, and the solver's direction is within 6° of the lock. Confidence 0.75 + 0.05 × (bands matched − 4), up to 0.9.
2. **SEG.** A steel-segment lock within 6°. Confidence 0.70 (FULL) or 0.62 (TERMINUS).
3. **RAY.** An ordinary ridge measurement, earned only if all of these hold:
   - the normalised evidence at the chosen direction is at least 0.45;
   - it is at least 1.15 times the evidence in the reverse direction (the line ends at the hands);
   - the absolute support is at least 0.4 (the line is continuous, not a few bright spots);
   - the frame is **verifiable**: the grip is moving, or there is a band or segment lock within 5 frames. In the finish a nearby lock is required.

   Confidence 0.55. The "verifiable" rule encodes a lesson from the Python laboratory: a static hold is the classic counterfeit trap. When nothing moves, a trouser crease and a shaft are equally still and equally straight, and only corroboration can tell them apart. Address-phase frames before the evidence span cannot earn RAY at all.
4. **WEDGE.** Not otherwise earned, but the blur wedge triggered and the solver's direction sits within (the wedge's precision + 4°) of the measured edge or centroid. Confidence 0.45.
5. **RECON.** The reconciliation moved θ by more than 6°. Confidence 0.40. If what it moved θ to is the measured leading edge, the frame is labelled WEDGE instead: it is a measurement.
6. **PRED.** Everything else: a direction carried by the solver's smoothness, not seen. Confidence 0.30.

**Coverage** is the fraction of frames from the takeaway to the finish that earned a measured tier: BAND, SEG, RAY or WEDGE. The track is **valid** if coverage is at least 0.60.

Be aware that coverage is calculated at this point, *before* two later edits that can demote frames (§4.13, §4.19). It is not updated afterwards.

The confidence numbers are fixed per tier. They are labels, not calibrated probabilities. A RAY frame is "0.55" whether its evidence was 0.46 or 0.99.

### 4.11 How long is the club, and where is its head?

Everything so far has been about the shaft's **direction**. To draw the shaft, and to compute anything about the clubhead, the tracker also needs its **length in the image**. That length varies through the swing: a shaft pointing toward the camera looks short (it is "foreshortened").

A design rule keeps this half separate: **nothing in this section feeds back into the direction**. The direction path (phase model, costs, solver, reconciliation) was validated on the corpus. Length and clubhead estimation are newer and less certain, and they are kept strictly downstream so they can be changed without touching the direction.

#### 4.11.1 The length ladder: a sensible length when nothing better exists

For any frame whose head is not directly measured, the head is drawn at grip + L × direction, for some projected length L. L comes from the first available rung of a ladder (`projectedClubLenPx`):

0. **The fused length** (§4.11.2), when the fusion is reasonably confident (confidence ≥ 0.35).
1. **The ball-measured length** at address (A1, §4.6.2).
2. **The band- or segment-corrected length**: the median scale (pixels per mm) times the club's length beyond the grip.
3. **A body-size estimate**: the distance from the mid-shoulders to the mid-ankles, in pixels, divided by 83% of an *assumed* 1.70 m stature, gives pixels per metre; multiply by the club's length minus 13 cm (the grip is held about 13 cm down from the butt).
4. **A last resort**: 45% of the frame height.

Whatever rung is used, the length is floored at 1.05 times the still shoulder-to-grip distance ("a club is always longer than the lead arm"). It is capped at 1.1 times the measurement it came from, or at 62% of the frame height, so a bad scale cannot draw the shaft off the edge of the picture.

#### 4.11.2 Combining several length estimates

**The problem.** No single length estimate is reliable on every swing:

- the ball measurement needs a ball;
- the band scale needs tape;
- the measured clubhead (below) needs a visible head;
- the body-size estimate assumes a height.

**The idea** (`club_length_fusion.h`). Treat each as an independent noisy measurement with its own uncertainty and combine them, rejecting any that disagree badly with the others. Also keep a slowly updated memory of this club's length for this athlete on this camera.

| Estimator | Source | Uncertainty (σ as a fraction of the value) |
|---|---|---|
| **E-ball** | grip-to-ball at address | 7% |
| **E-band** | band scale | 30% |
| **E-segment** | steel-segment scale | 35% |
| **E-head** | the 95th percentile of measured clubhead distances after the top (needs at least 6 confident frames, and is rejected if the head search was simply hitting its own upper limit) | 30% |
| **E-prior** | the stored long-term estimate (once it has at least two contributions) | its own variance |

Every σ is floored at 4 px.

1. **Sanity-clamp** each estimate between a floor (the arm, or 0.9 × the body-size estimate) and a ceiling (62% of the frame height, or 2.2 × the body-size estimate). The body-size estimate reads about a third short, so it is used only as a bound, never as a voter.
2. **Leave-one-out outlier rejection.** With three or more estimates, drop any that differs by more than 50% from the median of the others.
3. **Combine** the survivors by inverse-variance weighting: each weighted by 1/σ², so precise estimates count more.
4. **Confidence** = (how well they agree) × (how many there are):
   - agreement = exp(−(spread / 0.45)²), where spread is (max − min) / fused value;
   - support = min(1, 0.35 + 0.25 per instantaneous estimator + 0.10 per prior contribution, up to 4).

**Two passes.** The fusion runs once before the clubhead is measured (ball, band, segment, prior) to steer the clubhead search, and once after (adding E-head) for the recorded value.

**The long-term memory.** A version computed *without* the prior is also kept. Only that version, and only when its confidence is at least 0.5, updates the stored estimate. Otherwise the memory would vote for itself and could never correct. The memory lives in the application settings, keyed by athlete, club and camera. It is used only for a fixed camera, and it resets itself if the camera's frame size changes or several consecutive swings disagree with it. It is updated only during live capture, never during re-analysis.

**Calibration caveat.** These uncertainty fractions were fitted on 11 swings of one club, camera and golfer. On those swings the band and head estimates read 25–30% short of the ball estimate (they are taken mid-swing, where the club is foreshortened), and that bias was absorbed into their σ rather than removed. The code comment says the real fix, restricting the band scale to address-hold frames, "is escalated, not tuned around".

#### 4.11.3 The measured clubhead (Stage 2)

**The idea.** Along the shaft's chosen direction, walk outward from the grip and find where the club *ends*. Then smooth those per-frame findings over time.

**Per-frame: the terminus walk** (`measureHeadRadius`, H1). Walk the ray from a minimum radius (20 px, or 6% of the frame height, to skip the hands) to a maximum. At each step, decide whether the club is "there":

- **Thin line or moving.** The pixel lies on an edge-pair ridge, tested at three widths (5 px for a thin shaft, 12 px for a bloomed shaft, 24 px for the clubhead's blade), *or* it differs from the previous frame.
- **Changed from the scene.** It differs from the static scene median, *or* it is moving.
- **Not a permanent line.** It does not sit on a line that is also present in the scene median, such as the mat edge.

The ray is not a single line. Because the grip anchor is off the shaft (§2.4), the walk is repeated over a band of ±30 px across the ray, so it can find the club when the ray from the anchor misses it. Candidate end-points are the ends of locally sustained stretches of "club". Gaps are allowed: "a 75-px specular blowout gap must not terminate the run". The winner balances the quality of the evidence at its tail against a prior expectation of where the head should be.

**The search window** (`headBounds`).

- The upper limit is the lesser of the image edge and 1.15 × the club length: a projection cannot be longer than the club.
- A lower limit ("floor") rejects short counterfeits — a band edge, a break in the highlight — at a third of the club's length:
  - 50% of the length everywhere;
  - ramped up to 80% at the takeaway and again approaching impact, where the club is nearly full length in the image;
  - in still frames and around impact, also at least the lead-arm length.

**The prior on every frame.** The expected length is the in-plane length scaled by how far the lead arm currently reaches toward the camera compared with at address: a per-frame foreshortening proxy.

**The flip check.** The opposite ray is also measured. If it is decisively better supported, the frame is suspected of pointing the wrong way. It is then refused the "measured" label, but its direction is never *corrected* here: the head pass never changes θ.

**Over time: a Kalman smoother** (`runHeadTemporal`, H2). A tiny Kalman filter (§3.6) tracks the head's distance r along the ray and its rate of change:

- **segmented** wherever the shaft's direction jumps more than 20° in a frame;
- a 3σ gate rejects outlier measurements;
- it may coast for up to 12 frames when the head moves slowly, or 4 when it moves fast (over 800 px/s);
- each segment is RTS-smoothed;
- a head is declared **measured** only after a confirmed run of 4 consistent frames.

The outcome per frame is one of:

- **meas** — a measured head;
- **pred** — smoothed but unconfirmed;
- **off** — the head is expected to be outside the picture, because the ray leaves the frame before 0.8 × the expected length.

In the backswing, between takeaway and top, the head's confidence is capped at 0.45. The corpus showed "systematic short-lock on motion-blur streaks" there: the walk stops at the end of a streak rather than at the head.

### 4.12 Building the samples

Now each frame becomes a stored **sample** (`ShaftSample2D`): time, grip, direction, head, visible length, confidence, and a set of **flags** recording how each part was obtained. The head is placed according to the best available source:

| Situation | Head placed at | Flags |
|---|---|---|
| **BAND** frame | From the band fit: butt = grip − scale × offset along the shaft; head = butt + scale × club length. A direct measurement. | Measured |
| **SEG** frame, only if `seg.placeHead` (**switched off**: placing the head from the segment's end measured 73 px from the hand marks, against 34–48 px for the head pass) | — | — |
| Clubhead **meas** | grip + measured r along θ | Measured |
| Clubhead **off** | where the ray leaves the frame | the tier's flag + HeadProjected + HeadOffFrame |
| Clubhead **pred** | grip + smoothed r | the tier's flag + HeadProjected |
| Otherwise | grip + ladder length (§4.11.1) | the tier's flag + HeadProjected |

The "tier's flag" is Measured for RAY or SEG, Wedge for WEDGE, and Coasted for anything else. A WEDGE frame is deliberately *not* flagged Measured: it is evidenced by integration over a fan, not seen as a line, and it carries no length.

For WEDGE frames where both edges were read, three **wedge observations** are also stored:

- the leading edge, at the frame's time;
- the trailing edge, one exposure earlier;
- their midpoint, at mid-exposure;

each with a 4.5° precision. The synthetic track (§7.1.5) uses them as evidence.

Finally each sample's rotation rate θ̇ is computed from its neighbours by a simple central difference, with no smoothing.

### 4.13 After impact: refusing what a club cannot do

**The problem.** After impact the tracker can latch onto the lead arm. On 15 September's pitch shots the "head" was on the forearm at confidence 0.84. Or, with the club already at rest, the solver's coast can keep rotating it. Either way the samples claim the shaft flipped 190° in 80 ms, or lies along the arm. Neither is a club.

**The rule** (`demoteImplausibleFollowThrough`, since 17 September).

1. Measure the swing's own peak rotation rate going into impact: the larger of the 90th percentile of frame-to-frame rates over the last 150 ms, and the largest smoothed rate in that window.
2. Set a cap of max(1.0 × that peak, 1,200°/s). After impact the club only slows down. The floor stops a blurred impact, with few clean measurements and therefore a low apparent peak, from making a genuine follow-through look impossible.
3. Walk forward from the last good sample before impact. Any *measured* sample whose rate exceeds the cap is **demoted**: it loses Measured, Wedge and ImuBridged, gains Coasted, HeadProjected and Implausible, and has its confidence capped at 0.3. The rate is checked both against the last good sample and against its immediate predecessor; a wrong track sweeping *through* a plausible angle passes the first test at that instant but not the second.

A second test — "the shaft within some angle of the forearm is the arm" — exists but is **off**. In a face-on view a real, folded finish legitimately lines up with the forearm (21–26° apart on a full swing), and the test demoted 25 good samples.

Because demoted samples become predictions, everything downstream sees them as such: the snap skips them, the coaching positions built on them are marked as proxies, the synthetic track will not bridge into them, and the overlay draws them dim.

### 4.14 The snap: putting the drawn line on the club

**The problem.** The solver picks a *direction from the grip anchor*, and the anchor sits about 39 px to the side of the shaft (§2.4). So the drawn line can be parallel to the shaft but displaced, or slightly rotated about the wrong point. To the eye it looks like the line is floating beside the club.

**The idea.** Search for the line *near* the solver's line that actually lies along the shaft for its whole drawn length, and move to it (`snapSearch`).

**What the code does.**

1. Consider every line obtained by sliding the drawn line sideways by up to ±45 px and rotating it by up to ±10°. Search coarsely first (2 px × 1° steps), then finely (1 px × 0.5°) within ±6 px and ±2° of the best coarse candidate.
2. Score each candidate line by the average ridge evidence along its whole drawn length. The evidence uses a 5-pixel-wide corridor (±2 px) against a background 9–12 px to each side, with the same bright/dark rule as E2.
3. Several neighbouring candidates usually score almost equally, because a thick ridge looks the same over a few pixels of offset. Pick the candidate nearest the *centre* of that near-best plateau, not the first one found, so the line lands in the middle of the ridge.
4. **Accept** the move only if the evidence along the new line covers at least 25% of its length and it does not point back up the lead forearm.
5. On acceptance, the grip moves to the point on the new line nearest the old grip, θ rotates, and a projected head moves with it.

**Where the snap is not allowed**, each exclusion learned from a failure:

- **Band-locked frames.** A band lock is already a 0.3° measurement; the snap could only move it. On the corpus it took band-frame agreement from 0.26°/0.49° to 0.61°/3.18° (p50/p90).
- **Address, and the first 80 ms of the takeaway.** The snap re-registered onto the *leg*: one swing's address went from 4.3° off the marks to 18.7°.
- **Impact and follow-through.** "The shaft is a fan there", and the snap made through-swing agreement worse (9.6° to 12.4°).

On the unmarked 6-iron the snap took direction error from 5.5° to 1.8° (median). It was switched on, together with the steel-segment lock, on 10 September.

### 4.15 When was impact? Asking the club and the ball

**The problem.** The tracker is given an impact time from an acoustic or marker trigger. On the truth corpus that trigger:

- runs **13–22 ms early** on every swing, a fixed bias in the capture chain;
- on three swings, because of a gap in the video around impact, mapped to a frame **234–362 ms late**, deep in the follow-through. That also poisoned the search window for the delivery position (P6).

**The idea** (`impact_geom.h`). The address ball does not move until it is struck, and at impact the shaft passes through it. So find the moment the shaft's direction θ(t) sweeps through the grip→ball direction θ_ball(t). That crossing can be interpolated *between* frames, so it is immune to the gap problem.

**What the code does.**

1. Search within ±600 ms of the trigger time for the first crossing of θ through θ_ball that is confirmed by a hysteresis band: θ must go at least 8° beyond θ_ball on both sides. Note that "the shaft pointing away from the ball" (θ_ball + 180°) must never count. This is why it cannot reuse the "parallel to the ground" crossing finder of §4.16, which treats opposite directions as the same.
2. If the crossing is more than 100 ms *earlier* than the trigger's frame, override the trigger. Both documented failures leave the true impact at or before the trigger's frame. A crossing that is much *later* is the solver coasting through the impact blur and only meeting the ball line in the follow-through, so it is not allowed to override.

This corrected 8 badly placed impacts on the 61-swing corpus. One truth swing went from +234 ms to −10 ms. It changed nothing on the 11 swings where the trigger was sane.

A further option, retiming even a sane trigger to the crossing's sub-frame instant, **stays off**. It removed the average bias (18.3 → 15.9 ms) but scattered individual swings by −20 to +19 ms.

Only the later stages — the coaching positions around impact and the published Impact event — use the corrected time. Earlier stages, such as the wedge trigger and the tiers, already ran on the original.

### 4.16 The coaching positions (Layer B)

Golf coaching describes the swing by a sequence of checkpoints, the "P-system". The tracker locates them like this:

| P | What it is | How it is found |
|---|---|---|
| P1 | Address | the end of the still address hold (below) |
| P2 | Shaft parallel to the ground, going back | the shaft's angle crosses horizontal between P1 and P4 |
| P3 | Lead arm parallel, going back | the forearm's angle crosses horizontal between P2 and P4 |
| P4 | Top | the phase model's top |
| P5 | Lead arm parallel, coming down | the forearm crosses horizontal between P4 and P6 |
| P6 | Shaft parallel, coming down ("delivery") | the *last* shaft crossing between P4 and P7 |
| P7 | Impact | the impact frame, possibly corrected (§4.15) |
| P8 | Shaft parallel, going through | the first shaft crossing after P7 |
| P10 | Finish | where the finish begins, only if it is genuinely last in time |

P9 is not modelled.

**"Parallel to the ground", in a 2-D image.** A shaft is parallel to the ground when it is horizontal, whether pointing left or right. The code folds both horizontal directions together with an *elevation* angle:

```
elevation(a) = atan2(sin a, |cos a|)
```

It is 0 at horizontal and ±90° at vertical. A "parallel" moment is where the elevation crosses zero. This is parallel *in the image*. A shaft can look horizontal face-on while pointing down toward the ball in three dimensions. That is accepted coaching practice for face-on video, and the design document says so.

**Avoiding false alarms.** Noise near horizontal can make the elevation wiggle across zero several times. A crossing only counts once the signal has been more than 8° above *and* more than 8° below horizontal: **hysteresis**, as in a thermostat. The reported instant is the steepest zero-crossing between those two armed points, interpolated between frames.

**Why P6 uses the last crossing.** On swings whose top is close to horizontal (θ ≈ 20°), the shaft dips through horizontal a dozen frames after the top. The *first* crossing then lands just after P4, not at delivery. Delivery is by definition the last time the shaft is parallel before impact. Switching to the last crossing (10 August) moved four badly placed P6s to within 5 ms of truth.

**P1, the end of the address hold** (`addressHoldEndFrame`). The takeaway frame from the phase model is the start of *motion*, and on a slow takeaway it fired about 150 ms late, 29° into the backswing. The address is the end of the *still* period before it. Walking back from the takeaway, P1 is the last frame whose previous 10 frames show:

- **less than 2 px of net drift.** The takeaway's slow creep is directional and nets several pixels; pose jitter goes back and forth and nets out;
- **no single step of 2.5 px or more**, because a waggle that returns to where it started nets zero but is not stillness.

Preferences:

- frames where the ball was seen at the club;
- when the ball detector reports activity near the ball on at least half the hold, frames where the clubhead is also quiet — this catches the club bobbing while the hands stay frozen;
- never earlier than the no-return boundary of §4.3.

**Reading each position off the track.** At each position's time, the grip and direction are interpolated between the two neighbouring samples, and confidence and length are taken from the nearer one. Each position also records a **timing class**: *Measured* if both neighbouring samples were real measurements, otherwise *Proxy*. That lets the event timeline prefer a measured position from another source over a proxied one from this one.

**Re-measuring lost positions (B2, the milestone fit).** For positions whose nearest sample is *not* already a confident measurement (confidence below 0.5), the code tries to measure them directly from the pixels, by stacking several frames (`shaft_position_fit.h`):

1. **Shift and stack.** Take the 4 frames either side. Rotate each about the grip path by the amount the shaft is expected to have rotated in the intervening time (rate × time difference), then average them. A shaft that is consistent across frames reinforces; noise averages away, roughly by √(number of frames). The registration uses only the *rate* of rotation, not the absolute angle. So even if the track's θ is wrong by 180° at that moment (a known failure through impact), the true shaft still reinforces at its own orientation.
2. **Fit a line** to the stacked image. Search over direction, length, and small shifts of the grip (±6 px), scoring with the same corridor ridge integral as the snap.
   - For P1–P4, where the track is usually right, search narrowly (±7.5°) around the track's direction.
   - For P5–P8, where the track can be badly wrong, search a wide 170° sector centred on the grip→ball direction (or the forearm, if there is no ball).
   - At P1 and P7, reward lines whose head lands near the ball.
3. **Accept** if the support along the fitted line is at least 0.35 and it does not point up the forearm. The uncertainty in angle and length is read from the width of the near-best region of the score. The drawn length is then replaced by the fused club length. Otherwise keep the original position: the fit may only rescue, never degrade.

### 4.17 The synthetic track

Built here (Layer C), described in full in §7.1.

### 4.18 Self-checks: refusing a track the witnesses contradict

**Why refuse?** On 29 September two families of failure were found in which the tracker published a confident, wrong track. In both, an independent witness had been available that would have shown the problem, and nothing had looked at it. Since 1 October, `decideTrack` ends by comparing its answer with those witnesses:

- **Does the address point at the ball?** If the trusted ball's direction differs from the track's direction at P1 by more than 25°, there are two possible readings:
  - If P1 rests on real measurements (the median direction of measured samples within 6 frames of P1 agrees with P1 to within 10°), the image has spoken and the *ball* is suspect. The track stands, and the ball is flagged so other consumers do not lean on it. This was the 15 September swing 2 case: a second object 200 px from the real ball.
  - Otherwise the track is **refused (reason 1)**.
- **Was the phase model believable?** If it was still suspect after its retries (§4.3) *and* the shaft-parallel and arm-parallel positions of the backswing (P2 or P3) could not be found, the backswing it described did not exist: **refused (reason 2)**.
- **Does the length agree?** If the address-hold club length differs by more than 30% from the grip-to-ball distance at P1, the "address hold" window has drifted into the backswing. On 15 September swing 1 it read 407 px for a 280 px club: **refused (reason 3)**.
- **Was the pose usable?** The hands rule of §4.1: **refused (reason 4)**.

A refused track:

- is marked invalid;
- keeps its samples, for the laboratory;
- is stored with its reason;
- **draws nothing** in the application;
- shows "–" for every club metric.

That is Mark's 30 September ruling: no picture is better than a wrong one. The effect on the comparison sets:

- the 21 ball-visible swings analysed with both pose models went from 19 sane (B) and 17 sane (L) to 21 and 21;
- 15 September Wrist_02 went from 3 of 13 sane to 13 of 13;
- the 68 metrics of the pinned regression corpus did not change.

### 4.19 After the core: the ball anchor and the impact-anchor stage

Two more passes use the ball, after `decideTrack` has finished.

**`applyBallAnchor`** ("v3.4", `ball_anchor.cpp`) runs inside `ShaftTracker` straight after the core. Its design promise is that a missing or disagreeing ball can only leave the track unchanged, never degrade it. It does four things:

1. It computes, but does not act on, **tk0**: the first frame where the shaft departs the ball line by more than 25° (or by more than it had already departed at the takeaway). This is logged for analysis.
2. It flags the frames before tk0 where the ball was seen as **BallSeen**, a timing witness used by the event refinement.
3. **Address paint**, only with a trusted ball:
   - a *measured* sample within 15° of the ball direction is marked BallAnchored;
   - a measured sample that disagrees is left alone — the image wins;
   - an *unmeasured* sample is rewritten to point at the ball: head on the ball, length = grip-to-ball distance, confidence at least 0.5.
4. **Impact**: the last frame before the ball is launched, if not measured, is rewritten to point at the ball.

Then, as §4.1 noted, the address position P1 is re-read if its sample was rewritten.

**`ImpactAnchorStage`** is a separate pipeline stage that runs after the shaft stage, even on reused tracks. It finds the address ball a second, independent way, **by its disappearance** (`impact_anchor.h`):

1. Compute the per-pixel median of frames during the backswing (the ball is there, the club is up) minus the median of frames 150–410 ms after impact (the ball is gone).
2. A ball shows up as a bright disc in that difference. Search for one near the address clubhead and below the toes, scoring discs of radius 5–9 px.
3. If found, record it on the track (`ballAnchored`, `addressBallPx`) and rebuild the synthetic track.

It does **not** pin the ball into the track. Shaft lean (§8) reads the line from the hands to this ball directly, and the low point measures from it. Against 32 hand-marked contacts the hands-to-ball line reads −0.8° (standard deviation 2.1°), and the ball is placed within 10 px on 29 of 32 swings.

---

## 5. The down-the-line tracker

Files: `dtl_shaft_tracker.*` (the outer layer), `dtl_shaft_decide.*` (finding the ball, the visibility schedule, costs, the solve), `dtl_shaft_post.*` (snap, length, tiers, the HELD rule), `dtl_shaft_bands.h`, `dtl_shaft_config.h` (`shaft.dtl.*` settings), `dtl_shaft_types.h` and `dtl_shaft_track.h` (data types), `dtl_face_on_witness.*`, `dtl_shaft_json.*`. Design: `docs/design/dtl_shaft_tracker_design.md` and `dtl_continuous_track_design_update.md`.

### 5.1 Why not just run the face-on tracker on the other camera?

It was tried, and the result is the clearest illustration of why honesty checks matter. Run unchanged on DTL footage, the face-on tracker reported a coverage of 0.769 and declared the track **valid**. Graded against truth it was "152–155° from truth on 25 of 25 frames … 100% confidently wrong."

From behind the golfer, the swing looks fundamentally different:

- **The club points straight at the camera, three times.** Around P2 (shaft parallel going back), around the top, and around P6, the shaft lies along the camera's line of sight. Its image shrinks to almost nothing and its image angle swings through 180°. Mathematically the angle passes through a *pole*: it is undefined, and either side of that moment it is pointing in opposite directions. No smoothness rule can carry a track through a pole. The face-on tracker's whole strategy, one smooth path through the whole swing, cannot work here.
- **The arms lie along the shaft.** At address and impact, seen from behind, the lead arm and the shaft form an almost straight line. Face-on's C1 rule ("strong evidence the *other* way out of the hands means a scene line") assumes free space behind the butt of the club. From behind there is none: the reverse ray of a *correct* direction runs straight up the golfer's arm. C1 refuses correct frames.
- **The trailing leg.** At address, a long, high-contrast line runs from the hands down the trailing leg to the feet: "the trouser line". It is longer and brighter than the shaft, and the shared normalisation (§3.12) scores it just as highly.
- **The polarity trap.** There is no ring light, and behind the club is a lit, mid-grey simulator screen. A taped shaft alternates black tape and white paint. A detector summing "brighter than beside it" along the shaft adds positive and negative contributions that cancel, while a broad bright forearm alongside scores well.
- **No rotation law.** From behind, the shaft's image angle does not consistently increase in the backswing and decrease in the downswing. The face-on direction rule does not apply.

So the DTL tracker reuses face-on's **engines** — the ridge sweep, the band matcher, the snap search, the banded Viterbi — but **none of its decisions**: no phase model, no body-overlap rule, no ψ reconciliation, no bridging across gaps.

### 5.2 What face-on tells DTL: the witness

The DTL tracker's sole input from face-on is a read-only **`FaceOnWitness`** (`buildFaceOnWitness`), interpolated to DTL frame times. It carries:

- **The face-on direction θ_F** (unwrapped, so interpolation cannot go the wrong way round) and its rate.
- **ρ_F**, the face-on shaft's apparent length as a fraction of its full length. It is defined only on face-on frames that were real measurements. On every other frame it is deliberately blank (NaN), because the stored length there is a frame-edge clamp or a projection, not a measurement. The full length used as the denominator is the 90th percentile of measured lengths on near-horizontal frames (|cos θ| ≥ 0.94). Near-vertical frames are excluded because perspective makes the club read 10–17% long when the head is nearer the lens, at address and impact.
- **The face-on grip's height in the image** (its row).
- **Face-on's tier and phase labels.**
- **The coaching positions and the impact time.**
- **Chirality.**

Across a face-on gap longer than three frames, the witness says "no opinion" rather than interpolating. The code is emphatic that a valid answer does not imply a known ρ_F: "NaN is not a gap in the data, it is the datum."

### 5.3 When can the camera see the club? The visibility schedule

This is the DTL tracker's central idea.

**The geometry.** The face-on camera sees the shaft at angle θ_F with apparent fraction ρ_F of its length. The component of the shaft along the face-on image's horizontal axis — which is roughly the target line, the direction the DTL camera looks along — is u_x = ρ_F cos θ_F. The part of the shaft the DTL camera *can* see is what is left over, perpendicular to its line of sight:

```
ρ̂_D = √(1 − u_x²)
```

Read it this way:

- When the shaft points along the target line (u_x near ±1), the DTL camera sees almost nothing: ρ̂_D near 0, **end-on**.
- When the shaft is across the target line (u_x near 0), the DTL camera sees it at full length: ρ̂_D near 1.

**Handling unknowns.** Where face-on measured an angle but no length (it coasts at address and reconstructs at impact), ρ_F is taken as 1. That is the conservative choice: it makes u_x as large as it can be and ρ̂_D as small, so a near-horizontal face-on shaft still reads end-on. Only near-vertical ones — address and impact, the best-seen moments from behind — are admitted. Refusing these frames for lack of a face-on length had thrown away about 96 frames per swing where the club is "sharp and in plain view".

**Sighted frames and bands.**

- A frame is **sighted** if ρ̂_D ≥ 0.50. The design suggested 0.35. On the development swings ρ̂_D at the top fell only to 0.40–0.59, so at 0.35 the end-on gap at the top never opened and "the solve ran one 610–636 ms band straight through the frames where the club is pointing at the lens".
- Consecutive runs of at least 6 sighted frames are **bands**. Shorter runs are treated as flicker at an end-on edge, with one exception. Since 1 October, a run of at least 3 frames that sits within 2 frames of a full band (the hole between them is a quarantined or undecodable frame, not the schedule), or whose median ρ̂_D is at least 0.70, becomes an **edge band** of its own.
- **Each band is solved independently. Nothing is ever emitted in an end-on gap.** The angle there is undefined, and joining across it is the "flatter wrong branch across an evidence-free gap" failure by construction.

### 5.4 Trusting the hands: anchors and the cross-view quarantine

The DTL tracker takes the grip, both elbows and both wrists, and the eight body joints from the DTL pose. Any keypoint below confidence 0.3 is treated as unknown rather than guessed.

**The cross-view check.** Both cameras are roughly level, so "up" is up in both images. The grip's height in the DTL image should therefore be a fixed linear function of its height in the face-on image: y_D ≈ a × y_F + b. The code fits a and b over the address-to-impact stretch, where both views clearly see the hands. A DTL frame is **quarantined**, left unsolved, if any of these hold:

- there is no grip;
- either wrist is not confidently seen;
- its grip height disagrees with the face-on prediction by more than 80 px;
- more than 80 ms after impact, it disagrees by more than 40 px.

This catches "the post-impact invented hands": after impact the hands turn their back to the DTL camera, and the pose model invents them somewhere plausible. Hand confidence does not catch it, and "will not say so".

### 5.5 Finding the ball, from behind

As face-on, the ball at address and impact is the best witness. From behind it has to be found in a different scene.

**The bright cue.**

1. Take the median image over the address hold.
2. Threshold it at the lesser of 230 and the 99.5th percentile. Using the percentile alone failed: on the lit-screen session p99.5 was 254, above the ball's own 220–236.
3. Keep compact, round blobs (radius 4–16 px, circular, not elongated: "the alignment stick dies here") in the region a ball can be: below the line of the ankles, and beyond the grip on the golfer's side.
4. Apply **permanence with launch**. The blob must stay bright through three-quarters of the address hold — a quarter is allowed to be covered by the clubhead or its shadow — *and* fall below half its brightness after impact, because the ball has gone. A bright thing still there after impact is the mat.
5. Exactly one survivor is required. Two is "ambiguous, no ball".

**The shadow cue** (for a blown-white mat, 11 June). A white ball on a saturated white mat has no visible edge at all. What *is* visible is its contact **shadow**, a small dark crescent at its base.

1. In a median image from a period when the club is up and away from the ball, look for small, compact dark patches on bright mat inside the ball region.
2. The discriminator: the same pixels must **brighten by at least 40 grey levels after the ball is struck**. A scuff or tee hole stays dark.
3. If more than one patch passes, the clearest must beat the next by 1.5×, or the cue abstains.
4. The ball's centre is placed one radius above the crescent.

**The DTL club length L̂_D.**

- With a ball: the median grip-to-ball distance at address, divided by the address ρ̂_D.
- Without one: the face-on full length scaled by the cross-view height factor a.

### 5.6 The DTL costs

For each sighted frame there are three evidence channels. Each is a ridge sweep with the absolute floor of §4.5.2. Per direction, the strongest channel wins.

1. **Motion**: |frame − a clean plate of the scene|. The plate is built phase-aware: above the grip line it comes from the address hold, below it from a period when the club is up. A whole-clip median would contain the address club, because the golfer stands at address for half the clip, and that would hide the club exactly where it matters. The plate is invalid more than 100 ms after impact, when the simulator screen starts animating the ball's flight.
2. **Contrast**: |frame − a 31×31 blurred copy of itself|. This is *polarity-free*, and it defeats the polarity trap: along the true shaft it reads 37–48 grey levels over screen and mat, against 1–8 on a control line.
3. **Raw**, exactly as face-on.

The costs, in order:

| Term | What it does | Cost |
|---|---|---|
| Evidence | 10 × (1 − evidence) | 0..10 |
| **D1, reverse ray** | As face-on's C1, but *waived* when the reverse direction lies within 25° of the line from the grip to either elbow or shoulder (more than 40 px away), and at ball-gated frames. From behind, the reverse of a correct direction runs up the arm. | +10 |
| **D2, limb veto** | For both elbows, hips, knees and ankles — every limb joint below the shoulders — in every phase: a direction within 12° of the line to the joint, whose ray passes within 25 px of the joint (joints within 60 px of the grip are ignored as jitter). Charged once per direction however many joints line up. This is the trouser-line fix. The shoulders and head are deliberately excluded, because at P3 the real shaft passes near them. | +16 |
| **D3, too long** | One-sided: a run longer than 1.25 × ρ̂_D × L̂_D is longer than the club can look here. A *short* run is always allowed: occlusion, dim steel. | +8 |
| **D4, which half** | Where face-on measured, and its shaft is clearly up or down (\|sin θ_F\| > 0.25): directions pointing the other way vertically. Up is up in both views. | +6 |
| **D5, the face-on corridor** | Where face-on measured and ρ_F ≤ 0.93, face-on's measurement constrains the DTL angle to one of two centres (the depth sign is unknown). Directions outside ±25° of both pay a quadratic penalty up to 5. The cap sits below half the evidence span, so clean evidence outside can still win and be logged as an "escape". **The 25° is an unmeasured placeholder.** | ≤ 5 |
| **D6, ball well** | A shallow Gaussian reward at grip→ball at address and within 2 frames of impact. | −4, σ 8° |
| **D6b, ball gate** | Where the club is *known* to be at the ball — the address hold (released once face-on says the club has moved more than 10° from its address angle, or after 300 ms) and ±20 ms of impact — directions more than 20° from grip→ball pay heavily. With no DTL ball these frames are **not solved at all**. | +30 |
| Band reward | as face-on, last | −8 |

Why both a well (D6) and a gate (D6b)? The well alone lost. At address the trouser line and the shaft both reach normalised evidence of about 1, and "a 4-deep well cannot separate a tie against a 132° error". The gate is a hard decision dressed as a cost, so the trace can show what it refused. Without it the tracker published the address at 113–132° (down the trailing leg) where the truth was 58–62°.

### 5.7 The DTL solve

One banded Viterbi per band, independently:

- with **no** direction restriction;
- with a per-frame move limit of 12° ÷ max(ρ̂_D, 0.5). As the club turns toward the camera its image angle legitimately changes faster, so the allowance grows as the visible fraction shrinks;
- with the same smoothness cost as face-on.

### 5.8 After the solve: snap, length, tiers, and holding small holes

**Snap.** As face-on (§4.14), but on the polarity-free contrast image, for the polarity-trap reason, with these differences:

- only where ρ̂_D ≥ 0.6;
- never on band locks;
- scored over the length the schedule predicts the club should be. Scoring over the solver's run length had scored only the near half of the club, which at address contains a brighter ridge than the club and landed 4° off.

A snap is accepted only if it beats the original line, does not land under a limb veto, and stays inside the ball gate.

**Measuring the run.** The visible run is measured along the snapped line, or along the best of seven sideways offsets (up to ±30 px) when there was no snap, because the pose grip is off the shaft. A run equal to the ridge sweep's own minimum (98 px, "to the digit") was never measured. The search simply could not go shorter. It is recognised as such and not believed.

**Tiers.**

| Tier | Meaning |
|---|---|
| **BAND** | a band lock within 6°; confidence 0.75–0.90 |
| **RAY** | evidence ≥ 0.45 (or an accepted snap with line support ≥ 0.36); support ≥ 0.40; beats its reverse ray (or the test is waived); no limb veto; run at least 35% of the length the schedule predicts; not the sweep's floor; not a corridor escape after P8. Confidence 0.55 × evidence |
| **UNSEEN** | solved, but did not earn RAY. Publishes **nothing**: "the DP's path is not a measurement" |
| **END_ON** | the schedule says nothing could be seen |
| **OCCLUDED_WRIST** | quarantined: the hands were hidden |
| **OCCLUDED_ROW** | quarantined: the hands are not where face-on says they are |
| **OCCLUDED** | quarantined for another reason |
| **HELD** | see below |

There is **no PRED tier** down the line: "a frame is never measured on face-on's word." The three kinds of absence — end-on, occluded, unseen — are kept distinct on purpose: "publishing them as one 'no sample' is how a 0.95 coverage hides a 40 % end-on swing." Since 1 October END_ON is checked before quarantine. 1,078 of 2,063 frames labelled "hands hidden" were in fact end-on by the schedule.

**HELD** (since 1 October). A hole of at most 6 frames *inside one band*, with a measured frame on both sides, keeps the band's own solved angle at its neighbours' confidence. It is drawn, dimmer, so the tile does not flicker. It is never used by fusion or any metric, and never bridges an end-on gap: the band boundaries *are* the gaps. Its gate target (85% drawn coverage) was **not met**, 0.80 and 0.75. It is on by Mark's decision, to be judged in the application.

**One drawn length.** The drawn length is the schedule's ρ̂_D × L̂_D, not the per-frame measured run. Switching between length sources caused 713 jumps of more than 40 px in the drawn head on the 21 corpus swings. The measured run still decides the tier.

**A pin that is counted, not assumed.** The number of samples published on end-on frames, `publishedInEndOn`, should be zero by construction, and is counted anyway: "a construction nobody measures is a belief."

### 5.9 How well does it work?

On six held-out swings, against band-template truth:

- 425 of 620 truth frames paired;
- **median error 0.25°, p90 0.50°, no frame worse than 15°**;
- coverage 1.00 at address and impact on all six, 0.71–0.88 through the mid-backswing, 0.72–0.93 through the downswing.

Nearly all published frames are RAY (1,127 RAY and 2 BAND on the held-out set). Without a ring light the bands rarely lock.

Read these numbers carefully. The truth exists **only around address**: "three of the four bands … have no automatic truth". **No DTL frame has ever been hand-marked.** The ablations the design asked for were not run.

---

## 6. From two views to three dimensions

### 6.1 The shaft's 3-D direction from two angles (`shaft_fusion.h`)

**The coordinate frame** is the cameras', not the golfer's:

- X points to the face-on image's right, which is the target for a right-hander;
- Z points up;
- Y is the face-on camera's viewing direction.

The face-on camera looks along +Y. The DTL camera looks along +X, then is yawed and pitched (and optionally rolled) by configured angles. Those angles are **assumed to be zero** unless a calibration is supplied, and no calibration exists yet.

**For each DTL frame with a measured angle:**

1. Find the face-on angle at that instant. If two face-on *measurements* bracket it within 12 ms, interpolate between them: the frame is **Measured**. Otherwise, if the face-on synthetic track covers it, use that: the frame is **Bridged**.
2. Each camera's angle defines its **view plane** (§3.10), whose normal is the camera's viewing direction crossed with the image direction of the shaft.
3. The shaft lies along both planes, so its direction is the normalised cross product of the two normals.
4. **Which end is which?** The cross product gives a line, not an arrow. The camera that sees more of the shaft decides which way the arrow points. If the other disagrees, the frame is flagged `SignDisagree`: "a lost vote is a finding, not a tie-break".
5. **Conditioning.** If the two view planes are within about 15° of each other (cross-product length below 0.26), the direction is badly determined and the frame is flagged `IllConditioned`.

Neither camera's *length* is used. The face-on length is the weakest thing that tracker measures. Instead the fused direction *predicts* both cameras' apparent lengths, which can then be checked: "the lengths are then redundant, which is what makes them a CHECK and not an input."

### 6.2 The swing planes

Over the backswing (address to top) and the downswing (top to 20 ms after impact), the code fits a plane through the origin to the fused directions. It uses only frames that are Measured and carry no warning flags, and needs at least 8 (§3.10). Each plane gets:

- an **inclination** to the ground, arccos of the vertical component of its normal;
- an **out-of-plane spread** (RMS and p90).

A plane is offered to later stages only if its RMS spread is at most 5°. A backswing spread above 12° is flagged `backIncoherent`: "the backswing is not a plane (the takeaway and the lift are two)". Only the downswing is required to be planar.

**The address plane.** When the DTL camera looks along the target line, its view plane at address *is* the address shaft plane. So the address plane's inclination comes from the DTL view alone: the median inclination of the DTL view planes over the published address frames, needing at least 5. It owes face-on nothing, which matters, since face-on coasts at address. The delivery plane's difference from it (`deliveryVsAddressDeg`; positive means steeper) is one of the published numbers.

**Robustness to the unknown camera angles.** On the 4 July session the downswing plane's *inclination* moved by at most 1° as the assumed yaw was varied by ±15° and pitch from 0 to 15°. Its *heading* moves one-for-one with yaw, and is therefore not published.

### 6.3 The out-of-plane curve, η(t)

Real swings are not perfectly planar. Since 1 October a smooth curve η(t) is fitted through the measured frames' signed distances from their phase plane (§3.8):

- knot points every 30 ms;
- a smoothness penalty;
- a pull toward zero on any knot with no measured frame within 150 ms, so that across a gap the curve relaxes back into the plane rather than extrapolating a slope;
- limited to ±25°.

It is used only by the 3-D synthetic line (§7.2). Its gate, predicting a held-out band's out-of-plane angle, **failed** on this golfer: η is not continuous across an end-on gap. It is on by Mark's decision, because its value is at the band edges and only the tile can show it.

### 6.4 A mirrored band, re-read

When the backswing plane fit is incoherent, a likely cause is a DTL band that was solved mirror-imaged about the vertical: the D5 corridor's two centres are mirror images, and the solve picked the wrong one. The fusion tries reflecting each backswing band (θ → 180° − θ) and keeps reflections greedily while each lowers the plane's spread by more than 0.5° without creating sign disagreements, and while the refitted plane still faces the same way as the downswing plane. The DTL track itself is not changed. On 4 July swings 2 and 3 this took the backswing spread from 20.9°/18.0° to 12.0°/4.4°.

A related idea — replacing bridged frames' direction with the DTL view plane's intersection with the swing plane — is **off** and closed as degenerate. At impact the DTL camera looks along the plane, the two planes coincide (conditioning 0.06–0.08), and there is no direction to give.

### 6.5 Inclination from one camera: the face-on conic plane (`shaft_plane.h`)

Before fusion existed, and still on swings with no DTL camera, the face-on view alone estimates plane inclination by the ellipse method of §3.9. The grip-to-head *vector* sweeps a circle on the swing plane and images as an ellipse. Fit the ellipse in the backswing and in the downswing:

```
ι = arccos(minor / major)          (larger ι = flatter)
transition delta = ι_back − ι_down (positive = the club steepened in transition)
```

Two choices are described in the code as worth "tens of degrees each, and both learned the hard way":

- **Fit the shaft vector, not the head's path.** The head's path is the grip's movement *plus* the club's rotation, not a closed curve about a fixed centre. Fitting it made repeat fits on half the data disagree by 9.2° (worst 47.6°), against 0.6–0.7°.
- **Scale both axes by the same amount.** Normalising x and y separately distorts an ellipse's shape *and* orientation, the two things being measured. It once reported inclinations wrong by up to 40°.

**Rejected fits:** too few points (under 12); not an ellipse; degenerate; and **needles** — an axis ratio below 0.26 (ι above 75°). A short arc admits an arbitrarily thin ellipse threaded through it. Three of 33 reference fits were needles, giving transition deltas of ±47–58° "that no golf swing performs". A low split-half disagreement does not rescue them: a needle scored a perfect 0.00° on that test. Repeatability is not validity.

**Two channels:**

- **measured**, from samples with a head-pass result;
- **synth**, from the synthetic track.

Measured wins whenever both of its windows fit. Each channel has its own honest quality measure:

- the measured channel's is the split-half repeatability;
- the synth channel's is only the number and confidence of the coaching positions it interpolates. A split-half on interpolated points "measures interpolation smoothness, not repeatability". It is made structurally impossible to compute.

**Status: experimental.** The absolute inclination is uncalibrated: the design bounds a body-depth bias of up to 64°. The face-on plane was the input to the "over the top" fault. On a golfer who comes over the top on every swing it read −20° to +9° and changed sign, so on 23 September "over the top" moved to a down-the-line hand-path measure. Today only the "shallowing" characteristic reads it.

---

## 7. The synthetic tracks

"Synthetic" is used for two quite different things. They share a word and a purpose: give a continuous shaft where the camera cannot. Their mechanics differ.

### 7.1 The face-on synthetic track (Layer C)

#### 7.1.1 Why it exists

Two needs drove it.

The first was **display**. Replaying a swing at quarter speed shows each camera frame for four times as long, and a club drawn only at camera frames jumps visibly from position to position. The "fan" display (the shaft drawn at many past positions, fading) needs dense positions to look like a sweep rather than a set of sticks. Through impact the per-frame track is itself mostly reconstructions, not measurements.

The second, which arrived later and is more consequential, was **measurement**. The metrics that describe the club's *path* need a smooth path through exactly the stretch where the camera sees least:

- clubhead speed;
- the angle of attack;
- the low point of the arc.

You cannot differentiate a gappy, jittery track and get a sensible speed. You cannot find the bottom of an arc that is not there. The measured clubhead goes dark from about 45 ms before impact to 40 ms after (§4.11.3).

The synthetic track is a smooth curve, sampled at a fixed 240 per second, that passes exactly through the coaching positions (§4.16) and, since 29 September, is fitted to every measurement between them. Each of its samples is flagged `ShaftSynthesized` and stored separately from the measured samples (`ShaftTrack2D.synth`), so nothing can confuse the two by accident.

#### 7.1.2 The anchors and their rates

The anchors are the located coaching positions — P1 to P8 and P10, whichever were found — in time order. At each anchor the curve needs two things:

- **the shaft angle**, which the anchor already has;
- **the rotation rate.** This is the measured track's rate at that instant: the rate of the reconciled direction, computed by central difference and smoothed (median-5 then Gaussian-2). The grip's velocity is computed likewise.

**Impact is a boundary, not a knot.** At contact the club loses 20–30% of its speed within two frames. The smoothing above spans about ±27 ms at 150 fps, so at P7 it averages the speed before contact with the speed after. A smooth curve forced through P7 with that averaged rate would have to bulge: rotating faster mid-bracket to cover the angle, then slowing into impact. Clubhead speed then peaked 17–19 ms *before* impact instead of at it. Since 6 September:

- **P7's incoming rate** is a straight-line fit to the angle over the 24 ms *before* P7 (widened until at least 3 frames are included, preferring measured frames). It is floored at the average rate from P6 to P7. That average is immune to smearing, because both anchors are defined by crossings, not by smoothing.
- **P7's outgoing rate** is a fit over the 24 ms after.
- **The anchor before P7** gets an outgoing rate fitted forward, capped at the bracket average, so the rate rises steadily into impact.

On six swings paired with a launch monitor, this moved the speed peak to 1–9 ms before impact and cut the scatter of the impact-speed reading against the monitor from 3.3 to 1.8 mph.

#### 7.1.3 Drawing the curve between two anchors

For each pair of consecutive anchors, and each 240 Hz tick strictly between them:

- **Angle.** A cubic Hermite (§3.7) from the first anchor's angle and outgoing rate to the second's angle and incoming rate, with Fritsch–Carlson limiting so it cannot overshoot. Before interpolating, the second anchor's angle is **unwrapped toward the expected rotation** (average rate × bracket duration). A 200° bracket, as from the top to impact, is then traversed the way the club actually went, not the shorter way round.
- **Rate.** The exact derivative of that cubic (`curveRate`), so the stored rate is the rate of the curve actually drawn. Clubhead speed is computed from it.
- **Grip.** Taken from the **pose hand track** at that instant, interpolated between the nearest frames with valid hands (within 40 ms), **never** interpolated between anchors. An earlier version interpolated the grip between the anchors' grips. Wherever an anchor was missing or misplaced, the drawn grip swung away from the hands, a median of 59 px and up to 384 px. The hands are tracked on every frame; there is never a reason to invent a grip.
- **Length.** Straight-line interpolation of the two anchors' drawn lengths.
- **Confidence.** The smaller of the two anchors' confidences, reduced toward the middle of the bracket to 60% at the midpoint, so the curve reads least trustworthy furthest from a real position.
- **Head.** Grip + length × direction.

#### 7.1.4 Not carrying on where the club did not

A pitch shot stops short. The tracker then coasts, the follow-through is predicted, the finish (P10) lands on a predicted sample with an angle the club never reached, and a curve from P8 to that P10 can sweep 165° in 80 ms — drawn as if it were the club. Two rules split the anchors into separately bridged runs:

- **Rule 1, measured endings.** A bracket starting at or after impact is drawn only if its *end* anchor rests on real measurements (its timing class is not Proxy).
- **Rule 3, plausible speed.** A bracket starting at or after P8 whose anchors imply more than 1,500°/s is not drawn. Past P8 the club is slowing. A faster implied rate means the tracker had captured the lead arm.

#### 7.1.5 Fitting the evidence between the anchors

Until 29 September the curve between two anchors was decided entirely by the anchors and their rates. A dozen measured frames in between affected it only through the smoothed rates at the ends. That was a curve the speed, lag and low-point metrics were reading as if it were a measurement. Since then (`fitSynthToEvidence`), the curve's angle at every tick is the solution of a balance between two wishes:

- **Fit the evidence.** Every measured shaft reading in the stretch counts, each weighted by its precision:
  - measured, bridged and wedge-centroid samples at ±3°;
  - each blurred frame's leading, middle and trailing edge readings (§4.12) at ±4.5°, in place of the frame's single sample.
- **Stay physically plausible.** Penalise angular *acceleration*, the second derivative, with a scale σ_a: how sharply a club can plausibly change its rotation rate.

```
minimise   Σ over readings  ((θ(t) − reading) / σ_reading)²
         + Σ over ticks     (angular acceleration / σ_a)² × tick spacing
```

The coaching positions stay **fixed**: they are the definitions of the positions. Impact gets **no smoothness term** across it, because contact is a genuine break. It is a regularised least-squares problem (§3.8), solved per stretch by Cholesky. A stretch with no evidence keeps the Hermite curve exactly.

The scale σ_a was chosen against 283 hand-marked frames that the fit never sees:

| σ_a (°/s²) | error P1–P4 | error P7–P8 (median magnitude) | peak acceleration of the curve near impact |
|---|---|---|---|
| no evidence fit (Hermite only) | 2.7° | 9.5° | 6,000 |
| 1,000 | 2.1° | 7.8° | 8,000 |
| **5,000 (chosen)** | 2.0° | 6.5° | 18,000 |
| 20,000 | 2.0° | 7.0° | 36,000 |
| 80,000 | 2.1° | 7.0° | 71,000 — chasing noise |

There is an option to add the line from the hands to the address ball at P7 as one more reading (and to soften P7 itself). It **stays off**. It helped after impact but hurt just before it, and moved clubhead speed at impact by about 9 mph either way on swings with no launch-monitor speed to say which was right.

#### 7.1.6 The envelope clamp

As a final backstop (**Rule 2**), every tick is compared with the measured samples within ±25 ms. If it lies more than 10° outside the range they span, it is pulled back to the edge of that range, its head redrawn, and its rate recomputed. Where the club stopped, the synthetic track stops with it.

#### 7.1.7 Rebuilding it

Because the synthetic track is a pure function of the measured samples and the anchors, it can be rebuilt without re-running the tracker (`resynthesizeLayerC`). This happens:

- when a stored track is reused on re-analysis (§9.2);
- when the impact-anchor stage records the ball (§4.19).

The anchors' timing classes are re-derived from the stored samples, so a document written before timing classes were stored still obeys Rule 1.

#### 7.1.8 Who reads it, and what that means

The header of `shaft_synthesis.h` carries a warning: **"METRICS DO READ IT."** The flag was originally meant to exclude the synthetic tier from every measurement. Over time, the metrics that describe the club's *path* came to read it on purpose:

- clubhead speed, hand speed, lag and the speed-peak timing prefer it, because a smooth curve differentiates better than a gappy one;
- the attack angle prefers it, because the measured clubhead read a median 36° from the launch monitor and the synthetic arc 3.6°;
- the low point is defined on it;
- the kinematic sequence's club rotation rate prefers it;
- the face-on plane uses it as a fallback channel;
- fusion uses it to bridge gaps.

Scoring, the swing-quality estimands, the fusion plane fits and the wrist channel still exclude it.

That is a defensible engineering choice. Since the evidence fit, the synthetic track is a *fit to the measurements* rather than a guess between them. But it has two consequences a reader must keep in mind:

- it is anchored *hard* to coaching positions whose timing may itself be a proxy;
- switching the synthetic track off is no longer a display preference. It changes the speeds and removes the low point.

§10 returns to this.

### 7.2 The 3-D synthetic line in the down-the-line view

**The problem.** The DTL tracker leaves gaps at P2, the top and P6, where the club points at the camera (§5.3). The tile flickers between bands. Could a smooth line be drawn through the gaps? Not by fitting a curve to the DTL angles: the DTL angle passes through a pole at those moments, so "there is no smooth function of time to fit in that image".

**The idea** (`dtl_shaft_synth3d.h`, stage DtlSynth3D). What *is* smooth is the shaft's 3-D direction, and the face-on synthetic track already provides a smooth face-on angle through every gap.

1. A face-on angle confines the shaft to the face-on view plane.
2. The swing's plane for that phase supplies a second plane:
   - the address plane up to address;
   - the backswing plane to the top;
   - the downswing plane through the downswing;
   - then the downswing plane held for up to 250 ms more, flagged as extrapolated.

   Where the out-of-plane curve η(t) exists, the direction is tilted η off the plane rather than lying in it.
3. Intersect the two planes to get a 3-D direction, refusing it where the planes nearly coincide.
4. **Project** that direction through the DTL camera to get a DTL image angle and apparent length.
5. Draw it from the DTL grip. Where the DTL hands were quarantined, use the 3-D skeleton's hands projected into the DTL view.

**Status: on, as a preview** (Mark, 1 October). The DTL camera's angles are not calibrated. The projection uses the assumed zero yaw, so the line "is off by exactly the unknown yaw (4–9°)". It is drawn dashed and dimmer (the "Synthetic club" view preset). It is never read by fusion, the kinematic sequence or any metric. Its purpose is to let the drawing be judged by eye against the measured DTL club until the calibration session exists.

---

## 8. The metrics the shaft feeds

This section describes each metric that is computed from the shaft track: what it measures, what it reads, how, and how far to trust it. Several details matter more than they look:

- **which tier of samples a metric reads** — measured only, anything, or the synthetic track;
- **which "impact" instant it uses** — there are three, see §10;
- **whether it carries an uncertainty**.

### 8.1 Shaft lean at impact (`impactShaftLean`, face-on)

**What a coach means.** At impact, are the hands ahead of the clubhead, so the shaft leans toward the target ("forward lean"), or behind it?

**How it is computed** (`buildShaftLeanSeries`, `wrist_analyzer.cpp`):

1. Take every face-on sample — **all tiers, without filtering**.
2. Take the shaft's deviation from straight down (θ − 90°).
3. Unwrap it into a continuous curve, and flip the sign for a left-handed golfer, so positive means hands ahead for either hand.

The sign convention is marked "provisional, pending a hardware sign-lock pass".

If the address ball has been found by the impact-anchor stage, the whole curve is shifted so its value at impact equals the lean of the **line from the hands to the ball** at that instant. The tracker's own angle through the impact blur is the weak point; the hands-to-ball line is a direct geometric measurement. The curve is also shifted by whole turns so the impact value lies between −180° and +180°: one swing had read +362°.

**Read at:** impact, using the raw acoustic trigger time (`job.impactUs`), with the change from address to impact.

**How good is it?**

- The original version overshot 39 hand-marked impacts by a median of +12°.
- Reading the blur's leading edge (§4.5.5) took the median to 0.0°.
- Anchoring to the ball took the scatter from a standard deviation of 10.2° to 5.3°.

The published uncertainty is a fixed 9.5° either way.

### 8.2 Shaft lie (`shaftLie`, down the line)

**What it measures.** How upright the shaft stands, seen from behind, at address and again at impact. The change between them tells a coach whether the club came back to the ball steeper (more upright) or flatter than it was set. A fitter calls this dynamic lie, give or take the club's built-in lie, which is constant per club and cancels in the difference.

**How.**

- Only **measured** DTL samples (RAY, SEG, BAND) are read. Held or unseen frames never become a reading.
- The value is the angle between the shaft line and the image horizontal, 0–90°. It is deliberately unsigned: at address and impact the shaft sits at 55–65°, far from either fold point, and an unsigned value needs no convention about which way the golfer faces.
- Each reading is the nearest measured frame within 12.5 ms of address or impact. Further away than that is an absence, not a reading.
- The change is impact minus address; positive means steeper.

**Limits.** The absolute angle depends on where the DTL camera stands. A camera above hand height, or off the target line, projects the shaft at a different angle, and the corpus camera is both. Within one swing the camera does not move, so the *change* is sound. Absolute values are not comparable across cameras. The address and impact bands rely on the DTL ball (§5.5), so a swing whose DTL ball was not found usually shows "–".

### 8.3 Shaft angle at the top (`shaftAngleVsHorizontal`, face-on)

**What it measures.** At the top of the backswing, is the shaft short of parallel to the ground, parallel, or past parallel (a "long" backswing)?

**How.** Only samples with a **measured clubhead** (not projected, head confidence at least 0.30). The angle of the grip→head line above or below horizontal is computed with the horizontal distance taken as positive, so it reads the same for either hand. Positive means past parallel.

**Limits.** At the top the club is foreshortened. A short-looking head can fall below the head search's floor (§4.11.3) and become a prediction rather than a measurement. Those samples are excluded, which can leave a gap exactly at the top.

### 8.4 Attack angle (`attackAngle`, face-on)

**What it measures.** Whether the clubhead is travelling downward (negative) or upward (positive) at impact.

**How.** The direction of the clubhead's path at impact, from a centred difference over ±2 samples:

- preferring the **synthetic arc** where it has at least 5 samples within ±20 ms of impact and no jump bigger than three times the typical step (one swing had a 310 px discontinuity);
- falling back to measured heads otherwise.

**How good is it?**

- Measured heads were almost never present at impact, and read a median 36° from the launch monitor.
- The synthetic arc read +0.02° bias with 3.26° scatter on one session of six 7-iron swings.

**No fault or characteristic reads it.** Over 123 corpus shots the camera reading "read −65° to +38° and flipped shallow↔steep on the same swings", so the launch monitor's value is used for judgement where available. The camera value is shown on the chart.

### 8.5 Low point (`lowPointAhead`, face-on, estimated)

**What it measures.** How far in front of (or behind) the ball the clubhead's arc bottoms out. A good iron strike bottoms out after the ball.

**How** (`club_delivery.cpp`). Read off the **synthetic track only**, with no fallback:

1. Take the synthetic clubhead positions within ±60 ms of impact.
2. Find the lowest point in the image; refine it with a parabola through it and its neighbours. It is refused if it lies at either end of the window, because then the arc never turned up.
3. Measure its horizontal distance from the address ball, in millimetres via the ball's known diameter as a ruler.
4. Sign it by the direction the head was travelling: positive is the target side.

**Why only the synthetic track?** Requiring five measured heads within ±60 ms of impact produced a value on 9 of 108 swings. On the three that could be checked, the "low point" was 16–38 ms *after* impact: "over a metre beyond the ball".

**How good is it?** It carries a published **uncertainty of ±2.0 inches**. That comes from 36 inches (the club's radius) × tan(3.26°) (the attack angle's scatter). It is measured on six swings of one session, "and the measurement is thin". The low point is the bottom of an interpolation between P6, P7 and P8, so "its vertex is pinned near the P7 anchor". The documentation advises reading it as a session tendency and distrusting any single swing.

### 8.6 Clubhead speed, the timing of its peak, and hand speed (face-on)

**Clubhead speed** (`clubheadSpeed`, `kinematic_series.cpp`). The clubhead's velocity is the grip's velocity plus the shaft's rotation carried out to the club's length:

```
v_head = v_grip + L × θ̇ × (unit vector perpendicular to the shaft)
```

Speed is its magnitude. Here L is the fused club length in pixels and θ̇ the shaft's rotation rate, taken from the synthetic track where it exists (its exact curve rate, §7.1.3), else from the measured samples. Pixels convert to metres by dividing the club's real length beyond the grip (length − 0.13 m) by its length in pixels. Composing the speed this way, rather than differentiating the head's drawn position, avoids differentiating a projected head that may not be where the head is.

The curve is masked after the P7 anchor.

**Validation.** On six launch-monitor pairs it reads **0.959 ± 0.022** of the monitor's speed. The monitor itself over-reads by about 2 mph, so this is about 0.98 of the true speed. The remaining shortfall is attributed to the rate floor at impact reading a lower bound.

**The default-length trap.** If no club was recorded, the length defaults to a 1.12 m driver. A 7-iron's speed then reads about 22% too high.

**Speed-peak timing** (`clubheadPeakLead`). How many milliseconds before impact the speed was last within 97% of its peak. It uses a running median of ±17 ms to ignore spikes, which had been the maximum on 16 of 38 swings. Corpus median 3.7 ms, 95th percentile 49 ms.

**Hand speed** (`handSpeed`). The grip's speed, smoothed. It no longer feeds the "deceleration" fault: on all 97 corpus swings the hands were slowing into impact (peaking about 71 ms before it), so it does not separate swings.

### 8.7 Lag (`lagAngle`, face-on)

**What it measures.** The angle between the lead forearm and the shaft: how much wrist hinge is retained coming down.

**How.** For each pose frame, |shaft angle − forearm angle|, smoothed. 0 means the shaft continues the forearm's line, fully released. The shaft angle is the **nearest** sample in the synthetic-or-measured track, with no tier filter and no limit on how far away in time it may be.

**Read at:** P5, and at impact, with the change between them.

### 8.8 The swing planes

- **The face-on transition delta** (`transitionPlaneDelta`). §6.5. Experimental and uncalibrated. Only the "shallowing" characteristic reads it.
- **The fused swing plane** (`swingPlane`). §6.2. The backswing and downswing plane inclinations relative to the address plane, read at P3 and P6, with uncertainty equal to the plane's spread. The inclination is robust to the unknown camera yaw; the heading is not, and is not published.

### 8.9 The kinematic sequence's club rotation (`clubAngularSpeed`)

The kinematic sequence asks whether the body segments peak in speed in the right order: pelvis, then torso, then arm, then club. For the club, when no club-mounted sensor exists, the face-on shaft angle is converted from an image angle to a rotation *in the swing plane* ("de-projection") through:

- the fused downswing plane, if there is one;
- otherwise the face-on conic plane.

It is then differentiated. The same plane is also used to de-project the *lead arm's* rotation, so the face-on plane estimate feeds a body metric as well as a club one.

### 8.10 Things derived from the shaft that are not metrics

- **The coaching positions.** P2, P3, P5, P6 and P8 are promoted into the swing's event timeline by the positions-ladder stage.
- **The impact event**, possibly corrected by geometry (§4.15).
- **The vision phase segmentation** (address, takeaway, top, impact, finish), used for swings recorded without an IMU.
- **The club length** and its long-term memory.
- **Skeleton3D's club direction**, which reads face-on measured samples within ±4 ms and DTL measured samples within ±6 ms.

### 8.11 How far to trust each number: the uncertainty budgets

Since 1 October 2026 the shaft metrics carry a per-reading uncertainty, set up by `docs/design/shaft_uncertainty_propagation_design.md`. The figures behind it are in `docs/research/data/uncertainty/calibration_20261001.md`. Everything sits behind `uncertainty.enabled` (feature switches guide §3.18).

**Two numbers per sample, never one.** Every face-on sample carries:

- `sigTheta`, its 1σ angle error in degrees;
- `pGross`, the probability that it is on the wrong structure altogether (a forearm, a shadow, the mat).

Both come from one table, indexed by the tier that produced the sample and by the phase group it falls in (address, early backswing, backswing, top, downswing, impact, through, finish). The table was calibrated against 996 hand-marked frames on 58 swings. σ is set by coverage, not by a Gaussian fit to the scatter: the residuals have heavier tails than a Gaussian, so the larger of the 68th-percentile |error| and half the 93rd percentile is used. Each phase-group column was then checked on held-out swings and widened where it missed. The two numbers are kept apart deliberately, because folding a 5 % chance of a 40° error into a σ would make every reading look uncertain, when in fact most are good and a few are wrong.

**The synthetic curve's σ is a posterior.** The evidence fit (§7.1.5) solves a regularised least-squares problem by Cholesky factorisation. The same factor gives the curve's covariance, and its diagonal is each tick's σ. Two corrections apply:

- the weight on each reading is scaled by κ, so that correlated neighbouring readings are not counted as independent;
- each anchor's own σ spreads into the stretch beside it through the linear bracket weights.

The posterior also yields Monte Carlo draws of whole curves, using a fixed-seed generator (`det_rng.h`), so a re-analysis reproduces the same σ. Metrics that are non-linear functions of the curve take their σ from the spread over those draws.

**Each metric's budget**, all 1σ:

| Metric | Budget |
|---|---|
| Shaft lean (`impactShaftLean`) | Tracked: σθ at the reading ⊕ (rate × σ_t of the impact instant). Ball-anchored: the hands→ball line's angle, (σ_grip² + σ_ball²)/L², ⊕ the hands' sweep across the line × σ_t. |
| Lie (`shaftLie`) | DTL σθ at address and at impact, each ⊕ its timing term; the Δ in quadrature. |
| Top angle (`shaftAngleVsHorizontal`) | The head and grip positional σ through the line-angle formula. |
| Attack angle, low point | Monte Carlo over the synth posterior through `trackClubDelivery`; the low point adds ball σ and the ball-diameter ruler (5 %). |
| Clubhead speed | Monte Carlo over the synth posterior ⊕ grip velocity σ ⊕ s·σ_L/L. σ_L is 5 mm with a recorded club and 0.12 m with a defaulted one. The card says when the club length was assumed. |
| Speed-peak timing | Monte Carlo over the posterior. |
| Hand speed | Grip velocity σ from the pose smoother over n_eff. |
| Lag | σθ ⊕ the forearm angle's σ (3.6°, stated, not calibrated). |
| Swing plane, transition plane delta | A block bootstrap of the plane fit (blocks of 5 frames, 200 resamples) ⊕ a 0.5° camera floor, with the address plane's σ in quadrature. The conic channel also reports how often a resample turned into a needle. |
| Club rotation (`clubAngularSpeed`) | The per-sample σθ now feeds the rate and placement σ of the kinematic sequence, in place of the old `0.5°/conf`. |

**Timing σ.** Each P-position carries `sigmaTUs`:

- a crossing: σθ divided by the rate, floored at the frame quantum;
- P3 and P5: the forearm angle's σ divided by its rate;
- P4: the width of the speed minimum;
- P7: the geometry estimate against the trigger.

A reading taken at an instant inherits the rate × σ_t term.

**What the card shows.** The ± chip is the displayed reading's own σ. Its tooltip names the provenance:

- *calibrated*: checked against independent truth;
- *propagated*: from calibrated inputs, unchecked for this metric;
- *assumed input*: the club length was defaulted.

A ⚠ appears beside the chip when the reading's gross risk exceeds 0.2. It is a caveat, not a refusal.

---

## 9. Plumbing: where it runs, what it stores, how it is drawn, how it is tested

### 9.1 What the tracker is given

Every analysis is described by a job (`ShotAnalysisJob`, `shot_analyzer.h`). The fields the shaft tracker reads:

- **The club record:**
  - `clubLengthM` — **defaults to 1.12 m, a driver, when no club is recorded**;
  - `hoselFromButtMm`;
  - `shaftLengthMm`;
  - `handsEndMm`;
  - `bandCentersMm`.
- **The club-length memory:** `priorClubLenPx`, `priorClubLenVarPx`, `priorClubLenN`.
- **`impactUs`:** the impact time from the acoustic or marker trigger.
- **`handedness`:** which elbow is the lead.
- **`fullWindow`:** evidence over the whole video rather than the swing span. Note: on a swing captured live this re-runs pose, ball and shaft from the compressed video, which is a known trap.
- **`tuningOverrides`:** the `shaft.*`, `positions.*`, `synth.*`, `fusion.*` and `shaft.dtl.*` settings.

During live capture, `ShotProcessor` fills these from the application settings. On re-analysis, `SwingReanalyzer` fills them from the club record stored in the swing document, never from current settings, so a re-analysis reproduces the conditions of the capture.

### 9.2 Reusing a stored track

Re-running the tracker on a stored swing is not always possible or wise. The stored video may be the compressed mp4 rather than the raw frames the live tracker saw. On a dark clip, re-running once replaced a good live track with a worse one. So each analysis stage carries a version number (`analysis_versions.h`: shaft 5, DTL shaft 2, fusion 5).

A re-analysis **reuses** the stored face-on track — its samples, positions and lengths — when all of these hold:

- the pose and ball were themselves reused;
- there are no tuning overrides;
- the stored shaft version matches;
- a club block exists.

On reuse, the shaft stage:

- recomputes the forearm angles from pose;
- re-applies the follow-through rule (§4.13);
- rebuilds the synthetic track (§7.1.7);
- builds the DTL witness from the stored flags.

The DTL tracker and fusion are never reused; they are cheap enough (about 0.3 s) to recompute. **Any change to what the tracker produces must bump its version.** Otherwise stored swings silently keep their old tracks.

### 9.3 What is stored

**`analysis.club`.** The face-on track, in the swing document (`swing_doc.cpp`). It is written for valid tracks *and* refused ones, so the laboratory can study failures. Positions are normalised to the frame size and times are relative to the swing window. It holds:

- the samples, with their flags;
- the (always empty) predicted series;
- the synthetic track;
- the length estimates;
- the coaching positions, with their timing classes;
- the face-on plane fit;
- the address ball;
- a diagnostics block;
- the refusal reason;
- the wedge observations.

A typical swing's club block is about 270 KB, against 13 MB of pose. Three in-memory fields are **not** stored, and two diagnostics are stored but not read back (§10): `addressPhaseFrame`, `onsetFloorFrame` and `addrBallTrusted` are not stored; `diag.onsetTUs`/`topTUs` are not read back.

**`analysis.clubDtl`** (schema `pinpoint.clubDtl/1`). The DTL track. It carries a fingerprint of the exact configuration that produced it, so a results table can say which settings it came from.

**`analysis.club3d`** (schema `pinpoint.club3d/1`). The fused 3-D track and planes.

A two-camera swing document is 27–40 MB larger than a single-camera one, mostly because of the DTL pose.

### 9.4 How it is drawn

The replay overlay (`PpCameraFrame.qml`) draws the club only when the track is valid, so **a refused track draws nothing**.

- **A measured shaft** is drawn in the "blueprint" style: a bright line with end ticks, a ring at the head (fainter as head confidence falls), a ring at the grip, and a trail of the last ten measured head positions.
- **A projected head** — the head drawn at an estimated length rather than measured — is drawn as a lone dim line.
- **Synthetic samples** are drawn in the measured style. They are not flagged as projected.
- **Fan mode** draws the synthetic track plus any measured samples outside its span.
- **The coaching positions** are marked: green for those re-measured by the milestone fit, the accent colour for those read off the track.

On the DTL tile:

- measured frames are drawn normally;
- HELD frames are dimmer;
- the 3-D synthetic line is dashed.

The "**Synthetic club**" view preset shows only the measured DTL club and the synthetic line, for comparing them.

The "coach lines" feature draws the DTL address shaft plane through the measured grip at address and the ball.

The 3-D swing view does not read the face-on samples directly. It uses Skeleton3D's club direction, the positions and the fused downswing plane.

### 9.5 Tools

**SwingLab's `swinglab_run`** (`tools/swinglab/src/swinglab_run.cpp`) is the laboratory command-line tool. With `--trace` it writes one line per frame containing every internal of the decision: the tier, the solver's angle and the final angle, the ψ residual, φ, θ_ball, the head measurements, the raw and difference p97 scores, the support at the solver's angle, the segment and band fields, and the wedge rows and edges. It ends with a summary line. With `--dtl` it does the same for the DTL tracker. Multi-swing corpus runs are done on the studio PC in Release builds, with output written to the shared drive.

**`tools/shaftlab/`** is the Python laboratory where the algorithms were first developed and adjudicated. It was **retired as a development tool on 8 September**, when the C++ became the reference. Its scripts remain for parity tests, grading and montages.

### 9.6 Tests

Unit tests in `src/Analysis/tests/` cover each piece in isolation:

- the evidence engines, the segment lock and the wedge;
- the onset model;
- the coaching positions and the milestone fit;
- the synthetic track and its rebuild;
- the kinematic model;
- the face-on plane, including a corpus-golden test;
- fusion;
- the hand-axis prior;
- the decision core;
- every DTL stage;
- the ball anchor, impact geometry and length fusion;
- the clubhead measurement and smoother;
- club delivery;
- the positions ladder.

The swing-document test round-trips the flags. Tests must be run through `ctest`; the bare binaries lack the environment they need and fail spuriously.

The real acceptance test is the **corpus gate**. Run the 61 pinned swings with a feature off and on. Check that "off" is byte-identical to before, and that "on" improves the graded numbers without regressing others. That is how every switch in §4 was turned on, and its record is in the feature-switches guide.

---

## 10. Shortcomings

This section is deliberately unsparing. Some items are deliberate design decisions, and that is said where it applies, but a deliberate limitation is still a limitation. The items are grouped by kind:

- §10.1: what the evidence can and cannot support;
- §10.2: what the camera physically cannot see;
- §10.3: weaknesses in the algorithms;
- §10.4: the gap between the tracker's honesty and what the metrics read;
- §10.5: engineering and code-health problems;
- §10.6: what would most change the picture.

### 10.1 The evidence base

**One golfer, one studio.** This is the first and largest limitation, and it qualifies everything else.

The tracker contains dozens of numbers set by measurement on the corpus:

- the onset model's 7 px revisit box and 15-frame gap;
- the 8 px/frame motion threshold;
- the DTL's 0.50 visibility threshold;
- the clubhead search's floors and ramps;
- the wrist-hinge table;
- the length fusion's uncertainty fractions;
- the synthetic track's smoothness scale;
- many more.

Each number is a fact about *this* golfer's tempo and swing shape, *this* camera's scale (about 3.5 mm per pixel at 1280×1024), and *this* studio's lighting.

The corpus contains:

- no second golfer;
- no left-handed swing;
- no driver (the "driver" sessions were the 7-iron);
- no graphite shaft;
- no second studio.

The project's own wording — "validates machinery, not accuracy claims" — is the honest summary. A second golfer with a different tempo, or the same golfer in a different room, may break thresholds no test has ever exercised.

**The truth is thin, and partly circular.**

- **The band-lock "truth"** used for much of the regression grading is produced by the tracker's own band engine. Its 0.3° figure is self-consistency, and against human marks "the marked club was never 0.3 degrees".
- **Human marks are biased toward easy frames.** People mark the frames where they can see the club, which are the frames the tracker also finds easy.
- **Through impact on a bare club there is no truth at all of the right kind**, so every claim about P6–P8 on the markerless setup is unverified.
- **Down the line**, truth exists only near address and no frame has been hand-marked. Two planned measurements — ablating each constraint, and checking re-runs are identical — were not run.
- **Timing** truth rests on 13–14 swings.

**Pixel-sized constants.** Many constants are absolute pixel distances:

- the 470 px ray length;
- the ±9 and ±12 px background offsets;
- the ±45 px snap search;
- the hand-cleaning tolerances of 40 and 64 px;
- the address stillness thresholds of 2 px and 2.5 px per frame;
- the 7 px onset box;
- the 34 px body margin;
- the 98 px minimum ridge.

All were set at one camera scale and resolution. A camera placed further away, a different resolution, or a crop changes what they mean, and only a handful of constants scale with the frame size. The 720 px-wide face-on session and the 512–576 px DTL frames already run on the same absolute numbers.

**Frame-counted constants.** Similarly, the per-frame rotation limits, the 12-frame impact window, the 25-frame stillness run, the 10-frame bridging gap and the 7-frame minimum run all assume about 150 fps. Some timing windows are expressed in microseconds and convert correctly; many are not. At 120 or 240 fps they mean something different physically.

**Reproducibility is per machine.** Byte-identical comparisons hold on one machine with a pinned pose. Identical results across operating systems are untested, and a one-bit difference in `atan2` between compilers has been seen. The pose model itself is not deterministic run to run on the studio machine.

### 10.2 What the camera cannot see

**Bare steel through impact.** At this exposure the club through impact is a faint fan (§2.1). Everything published there for an unmarked club is constructed:

- the solver's smoothness;
- the forearm reconstruction;
- the blur wedge's edge reading;
- the synthetic track.

The research report puts it directly: the apparent 9.4° agreement there "is two constructions coinciding". The wedge's leading edge is a genuine measurement of the *blur*, but its ±4.5° precision is a median over 32 swings' hand marks, not a per-frame uncertainty.

**The head goes dark through impact.** The measured clubhead is missing from roughly 45 ms before impact to 40 ms after. Every metric about the club's path at impact — speed, attack angle, low point — therefore reads the synthetic curve. That curve is fixed at P6, P7 and P8 and fitted to whatever measurements lie between, and "its vertex is pinned near the P7 anchor".

**One camera sees no depth.** The face-on view measures angles *in the image*:

- "Parallel" is parallel in the image, not in three dimensions.
- The clubhead is about half a metre nearer the lens than the hands at address and impact, which makes the club look 10–17% too long.
- Shaft lean, the face-on plane and the face-on speeds ignore the motion toward and away from the camera altogether.

The 3-D answer exists only on the 34 swings with a second camera.

**The forearm's roll** after impact cannot be seen face-on. The wrist-hinge reconciliation is only valid up to impact.

**The DTL view's blind spots are physical.** At P2, the top and P6 the club points at the camera, and no curve fitted in that image can cross those moments. Around the top, "occluded" means the hands are hidden, not the club. At impact the two cameras' view planes nearly coincide, so the fused direction is badly determined exactly where it would be most valuable.

### 10.3 Weaknesses in the algorithms

**The pose is a single point of failure.** Grip, forearm, body outline, phase model, ray origin, ball geometry and length floors all come from the pose's hand and body keypoints. The pose's hand confidence does not tell you when it is wrong. A pose difference of a median 0.6% of the frame (ViTPose-B against -L on the same swing) "put the entire backswing on the wrong structure". The defences — hand cleaning, the hands ladder, the trail-arm veto, refusal — are heuristics that each fixed a named case. The grip anchor's 39 px offset from the shaft is accepted by design, and much downstream machinery compensates for it rather than fixing it. The one mechanism that would use the hands' own direction, the hand-axis prior, was built and never evaluated.

**The phase model is a stack of patches over one fragile signal.** Grip speed from interpolated pose drives the whole model. On top of it sit:

- a two-longest-runs choice;
- bridging;
- two candidacy clamps;
- the m3gate;
- the top repair;
- the onset reseed;
- the speed, forearm and no-return walk-backs;
- the takeaway clamp;
- a backstop;
- a self-check;
- a retry ladder.

Each fixed a named swing. Together their interactions can only be found by running the corpus, not by reasoning, and unusual swings — pumps, extended waggles, pitch shots, very slow takeaways — remain at risk.

This matters more than it might, because **the solver's allowed directions depend on the phase labels**. A frame mislabelled "downswing" *cannot* rotate backwards, so a wrong label forbids the right answer outright. That is exactly how collapsed phase models produced "the DP walks the backswing the wrong way under downswing constraints (150–160° at P2)".

**The rotation-direction rule ignores which way the golfer swings.** The solver forces the shaft's image angle to increase during the backswing and decrease from the downswing on (`phaseSign()` in `shaft_track_assembly.cpp`). It does this whatever the measured chirality says. The reachable cone and the wrist-hinge predictor *do* use chirality; the direction rule does not.

For the corpus — a right-hander filmed face-on, target to the right of the image — the rule is correct. For a **left-handed** golfer filmed the same way, the image rotation reverses. The solver would then forbid the true path in every swing phase. A camera mounted mirror-image does the same. The design's own risk list said to add a left-handed capture "before v3.0 freezes"; none has been captured. Shaft lean flips its sign for a left-hander, which makes the gap easy to miss: the tracker beneath it does not. This is inferred from the code, not observed, because no left-handed swing exists to test it on.

**Normalisation hides weak wrong lines.** Every frame has a full-strength winner after normalisation (§3.12). The absolute floor and the support test are the only absolute checks, and the floor was set to be inert on the corpus. It catches blank frames, not frames where a weak but real off-shaft line wins.

**Ties are broken by priors tuned to this golfer.** Where the shaft and a limb or trouser line both reach full normalised evidence, the decision falls to:

- the forearm vetoes;
- the body outline;
- the ball;
- the wedge's kinematic cone;
- the solver's smoothness.

The kinematic cone rests on a hand-written wrist-hinge table whose error against truth is a median −12° with a p10–p90 spread of 78°. The better, fitted table is switched off because it starves the wedge trigger.

**The address depends on the ball.**

- Without a trusted ball, address frames are not probed for steel and get no ball well.
- The solver carries whatever angle it had at the start of the evidence span backwards over the hold.
- The impact-camera rig of 15 September hid the ball: 11 of the 13 broken swings that session had none.
- The decoy check needs at least one real line at the ball's direction to trust the ball, so on a daylight bare-steel session the right ball can also go untrusted.
- A session-level warning ("face-on unusable: the ball is hidden") was specified but not built.

**The snap has a known tail.** On the daylight session the snap re-registers onto "the lead arm at the top and the leg at address", costing 15 of 60 marks. The fix "needs the elbow keypoint inside the tracker". The snap is simply disabled at address, in the first 80 ms of the takeaway, and through impact: exactly where it had failed.

**The clubhead search stops early.**

- The terminus walk "still stops at the last lit steel more often than at the clubhead": the p90 head error is about 100 px.
- In the backswing it systematically locks onto blur streaks, which is why confidence there is capped.
- Placing the head from the steel segment's end measured worse and is off.
- The flip check can refuse but never correct.

**The length fusion is calibrated on 11 swings** of one club. Two of its estimators carry a known 25–30% foreshortening bias absorbed into their uncertainty rather than removed. The body-size rung assumes the golfer is 1.70 m tall. The default club length is a driver, so a missing club record silently mis-scales every speed.

**The DTL tracker's priors are placeholders.**

- The face-on corridor's 25° width was never measured.
- Which of its two centres is right was never established (it splits about 60/40).
- The reverse-ray test is waived on 82–85% of published frames, so it is effectively off, and its planned replacement is unbuilt.
- The limb veto "fired on zero frames" on its measurement set.
- The 0.50 visibility threshold "does not transfer across rigs".
- A confirmed forearm lock was published at P2 on 11 June.
- The ball's shadow cue "is a scene assumption in disguise".
- The two estimates of the DTL club length differ by −15% to +11%.
- The 11 June address angle sits 4–6° above its own ball line, unexplained.

**The 3-D work assumes an uncalibrated, perspective-free camera.**

- Yaw, pitch and roll are assumed zero, while the alignment stick suggests 4–9° of yaw and 2.4–6.5° of roll, and the camera moved during one session.
- The planes' headings are not published for this reason. Their inclinations are claimed robust, but on the evidence of one session.
- The backswing is "not a plane": its fit is refused on 18 of 24 swings.
- Three features — the DTL HELD tier, the out-of-plane curve and the 3-D synthetic line — are on by Mark's decision **despite failing their gates**, to be judged in the application. That is a legitimate call, but they are unvalidated.

**The face-on plane is experimental.** It is uncalibrated (a body-depth bias of up to 64° is bounded but not removed). Its repeatability measure cannot detect a meaningless fit. It changed sign on a golfer who comes over the top every swing.

**Impact timing is layered heuristics.** The trigger is 13–22 ms early. The geometric correction only overrides in one direction, and retiming is off because it scatters. Different metrics use different impact instants (§10.5).

**The follow-through is guarded, not modelled.** The rate caps and the measured-ending rule catch known failure shapes. "The coasting model's undamped rotation remains open." The forearm test is off because a real finish looks like an arm lock face-on.

### 10.4 The tracker is honest; the metrics re-decide

The tracker labels every sample carefully. The metrics then make their own choices about which labels to respect, and those choices are not consistent.

**The synthetic tier feeds metrics.** Until the comments were corrected on 1 October, the flag's documentation said the synthetic tier was "EXCLUDED from metrics". It is read by:

- clubhead speed, hand speed, lag and speed-peak timing;
- attack angle;
- low point;
- the kinematic sequence;
- the face-on plane's fallback channel;
- fusion's bridge.

Since the evidence fit (§7.1.5) the synthetic curve is a fit to the measurements, which justifies much of this. But it is pinned to coaching positions that may themselves be proxies, and its smoothness scale was chosen against 283 marks on one golfer. Switching it off is no longer a display choice.

**Several metrics read every sample regardless of tier.**

- **Shaft lean** reads all samples, including coasted and implausible ones. At impact the sample is often a reconstruction or a wedge.
- **Lag** takes the nearest sample with no tier filter and no limit on how far away it is in time.
- **The speeds and the kinematic sequence** read the synthetic-or-measured track without filtering.
- **The face-on plane's "measured" channel** admits any sample with a head-pass result, including projected, coasted and implausible ones. That is looser than the club-delivery metrics, which require a measured head with confidence at least 0.3.

Each choice has a reason in its own comment. But the metric layer does not inherit the tracker's honesty automatically; each metric re-decides it.

**Uncertainty is propagated now, with gaps (1 October 2026, §8.11).** Every sample carries σθ and a gross risk, and every shaft metric carries a per-reading σ. What remains:

- **Few metrics are calibrated.** Only lean has independent truth enough to be checked (30 P7 marks: 63 % within ±1σ, 90 % within ±2σ, bias +0.7°). Attack angle and speed have six launch-monitor pairs in the corpus. Lag, top angle, lie and the planes are propagated from calibrated inputs but are unchecked as metrics.
- **The gross risk misses some gross readings.** The two lean readings off by 15° and 18° both carried a gross risk of 0.
- **One golfer.** The table was calibrated on 996 marks from one golfer, so a second golfer's swings may need a recalibration (`calibrate_sigma.py`).
- **Three switches that would change values ship dark**: soft anchors and one impact instant failed their gates, and the sequence's calibrated club σ is awaiting a decision.

**The attack angle is shown but trusted by nothing.** It is computed and charted, but no fault or characteristic reads it, because the camera value "flipped shallow ↔ steep on the same swings".

### 10.5 Engineering and code health

**Three different impact instants.**

- Shaft lean uses the raw trigger time.
- Club delivery, the planes, the lie and fusion use the timeline's Impact event, which may be geometry-corrected.
- The speed mask, the speed-peak timing and the kinematic sequence use the P7 anchor.

Within one swing these can differ by the trigger's 13–22 ms bias or more.

**Dead flags and fields.** Since the July rewrite:

- nothing sets the IMU-bridged or kinematic-predicted flags;
- the predicted series is always empty;
- the vision/IMU correlation is always zero.

Consumers still test and clear these bits. One consequence is real: when a stored track is reused, the synthetic-track rebuild reads "was this frame a prediction?" from the dead flag, which is always false. The impact-boundary fit's preference for measured frames therefore never applies on a reused track, while it does live. **A reused track's synthetic curve is not the curve the live run drew.**

**Wedge frames are not "measured".** By design the wedge flag is not combined with the measured flag. Consumers that test only the measured flag — fusion, Skeleton3D, the DTL witness's length ratio — treat delivery-zone wedge frames as unmeasured. Some of that is intended, since a wedge carries no length, but it is not stated where it happens.

**The stored angle is not always in its documented range.** The ball anchor writes raw `atan2` values, which can be negative, into a field documented as 0–2π. Most consumers wrap or unwrap anyway.

**Coverage is computed before later edits.** Coverage and validity are frozen before the follow-through demotion and the ball anchor. A track with many demoted follow-through frames keeps its earlier coverage.

**Not everything round-trips.** Three in-memory fields are not stored: `addressPhaseFrame`, `onsetFloorFrame` and `addrBallTrusted`. Two diagnostics are stored but not read back: `diag.onsetTUs` and `diag.topTUs`. A re-written reused document carries −1 for onset and top. The reuse path cannot reproduce the decision to trust the address ball.

**User-visible text still describes old behaviour.** The code comments listed in earlier versions of this guide were corrected on 1 October. Three catalogue descriptions shown to users were not:

- the attack angle "needs a measured clubhead";
- clubhead and hand speed are "scaled by the ball-diameter ruler" — they use the club length;
- the swing plane is "one number per swing".

**The decision core is one very long function.** `decideTrack` runs to about 1,750 lines. It interleaves:

- evidence and cost edits;
- the solve and the segment passes;
- reconciliation;
- the length ladder and two length fusions;
- the clubhead pass;
- placement and demotion;
- the snap;
- impact geometry;
- positions and the milestone fit;
- the synthetic track;
- the self-checks;
- trace-filling.

Its correctness depends on a documented but fragile *order*:

- the band reward last, re-asserted after the wedge and the ball well;
- tiers before placement;
- demotion before the snap and the positions.

Many interactions are guarded by "byte-identical when off" corpus gates rather than by unit tests. Its trace structure has grown to about sixty fields.

**Cost.** Per evidence-span frame the tracker runs:

- two 360-ray sweeps of about 230 samples each;
- wedge sweeps;
- segment probes at several angles over two passes;
- a snap search of about 580 line integrals per sample;
- a two-way clubhead walk across a ±30 px band;
- frame-stacking fits.

It caches up to 1.2 GB of frames. Three seconds per swing is fine offline. It is not an algorithm for the live path, and very long spans fall back to slow serial decoding.

**The Python oracle is retired but still binding.** Parity tests still pin behaviours that only existed to match the Python, such as nearest-pixel sampling and the median-of-four. The Python's oracle modes survive as production switches (raster C2, no span bounding, no reconciliation).

### 10.6 What would most change the picture

These are not a plan; they are the consequences of the shortcomings above.

- **A second golfer and a left-handed capture**, before any accuracy claim leaves the building. The left-handed case is probably broken outright.
- **Independent truth through impact.** A high-speed reference camera, or hand marks on the delivery frames of an unmarked club, would grade the synthetic track, the wedge edges, lean, attack angle and low point — today the least verified and most consumed numbers.
- **A calibrated DTL camera.** The protocol session's alignment-stick clips would unlock plane headings, an honest 3-D synthetic line and better-conditioned fusion.
- **Putting the metrics on the tier contract.** One impact instant, explicit tier filters and propagated uncertainty, so honesty is inherited rather than re-decided per metric.
- **Removing the dead IMU and predicted-series plumbing**, so the next reader sees only the code that runs.

---

## 11. Glossary

| Term | Meaning |
|---|---|
| **θ** | The shaft's image angle, grip→head, atan2 with y down. 90° is straight down. |
| **φ** | The lead forearm's image angle, elbow→grip. |
| **ψ** | θ − φ: the shaft relative to the forearm, the image wrist hinge. |
| **Chirality** | Which way the arm rotates in the image from takeaway to top (+1/−1). |
| **bs0, onset, top, impact, fin0** | The phase model's landmarks: takeaway start, top, impact frame, start of the finish. |
| **Evidence span** | From 100 ms before the takeaway to 100 ms after the finish begins: the frames that get evidence. |
| **E1, E2, E4** | The band matcher, the ridge sweep, the steel-segment lock. |
| **C1–C4** | Butt termination, body free space, one reversal, arm coupling. |
| **Tier** | What earned a frame's angle. Face-on: BAND, SEG, RAY, WEDGE, RECON, PRED. DTL: BAND, RAY, HELD, END_ON, OCCLUDED_*, UNSEEN. |
| **Measured** | An angle earned from pixels rather than carried by the model. |
| **Coasted / projected** | An angle, or a head, carried by the model rather than seen. |
| **Synthetic track (Layer C)** | The 240 Hz curve through the coaching positions, fitted to the evidence. |
| **3-D synthetic line** | The face-on synthetic angle turned into a 3-D direction via the swing plane and projected into the DTL view. |
| **Wedge** | The motion-blur fan of a fast shaft. Its leading edge is the shaft at the frame's time. |
| **ρ_F, ρ̂_D** | The face-on apparent length fraction; the predicted DTL visible fraction. |
| **Band (DTL)** | A run of frames where the DTL camera can see the club, solved on its own. Not to be confused with the reflective bands on a taped club. |
| **Conditioning (cond)** | How well two planes' intersection is defined: the sine of the angle between them. |
| **ι** | The face-on plane inclination, arccos(minor/major) of the fitted ellipse. |
| **η** | The fused out-of-plane angle curve. |
| **A1, L_px** | The club's projected length measured from grip to ball at address. |
| **Refused** | A track contradicted by its witnesses. It is invalid, draws nothing, and its metrics show "–". |
| **Dark / frozen ON** | A switch that is off and byte-identical to the code before it existed / switched on after passing its corpus gate. |
| **Pinned pose** | Pose computed once and stored, so tracker changes can be compared exactly. |

---

## 12. A short history

The tracker has been rebuilt several times. Knowing the order explains why the code looks the way it does.

- **June 2026 — the first C++ tracker.**
  - A classical line detector with a Kalman tracker, face-on only.
  - Reverted after producing confidently wrong markups.
  - A "skeleton-aware" enhancement series (19 June onward) introduced the arm-based ideas that survive today: the double-pendulum wrist-hinge predictor, the arm-length floor and the blur-first wedge.
- **Early July — the Python laboratory.**
  - A passive detector developed through seven versions, accumulating 21 numbered fixes against hand-marked swings.
  - The clubhead-measurement stage.
  - The instrumented club: retro-reflective bands taped on 4 July.
  - A "stripe fusion" truth generator that turned band locks into automatic truth.
- **5–7 July — club tracking v3.** The physics-first redesign:
  - the four constraints;
  - the global Viterbi solve;
  - the ψ reconciliation;
  - the geometric body outline;
  - span bounding.

  Ported to C++ on 7 July with zero difference from the Python on the reference swing. That port is the tracker described here.
- **8–13 July — the ball, and length.**
  - The ball as the far-end anchor.
  - True-onset segmentation.
  - The measured clubhead in C++.
  - Club-length fusion.
  - Parallelisation.
- **11–18 July — position-first.**
  - The snap (built and kept off).
  - Coaching positions and the milestone fit.
  - The synthetic track (dense at 240 Hz from 16 July).
  - The fidget-proof onset rules, frozen on 17–18 July.
- **August — timing and blur.**
  - Coaching positions joined the event timeline.
  - On 10 August the blur wedge, the absolute floor, the last-crossing P6 rule, the top repair and impact geometry came on together, taking located delivery positions from 19 to 59 of 61 swings.
  - The wrist-hinge model was fitted (and its better version kept off).
  - The face-on plane.
  - On 6 September, impact became a boundary, fixing the clubhead-speed peak.
- **8–11 September — markerless.**
  - The discovery that "the tape was never what made the shaft bright".
  - The steel-segment lock.
  - The unmarked 6-iron.
  - On 10 September the markerless stack (segment lock + snap) came on, because "the bare club is now better than the taped one".
- **15–28 September — the second camera.**
  - The synthetic track follows the hands; reuse of stored tracks; the follow-through rules (17 September).
  - The DTL tracker (20–21 September), the ball's shadow, the DTL tile in the application.
  - Two-camera fusion (21 September).
  - Skeleton3D's club branch (26–28 September).
- **29 September – 1 October — robustness.**
  - The blur's leading edge.
  - The evidence-fitted synthetic track.
  - The ball-anchored impact lean.
  - The two failure families investigated and closed:
    - the address anchored to a confirmed ball;
    - the phase model's self-check;
    - the trail-arm veto;
    - the hands ladder;
    - refusal.
  - The DTL continuity rules.
  - The out-of-plane curve and the 3-D synthetic line.
  - Shaft lie.

Commit hashes for each step are in `docs/developer/feature_switches_developer_guide.md` and in the research report `docs/research/club_detection_from_video.md`, which tells the same story in fourteen phases.
