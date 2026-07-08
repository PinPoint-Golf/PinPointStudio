# `ball_state_machine.py` explained — the reference bible

**What this document is.** A complete, self-contained explanation of the ball-detection v2 exemplar
(`tools/balllab/ball_state_machine.py`, driven and gate-checked by `tools/balllab/acceptance.py`):
what it does, why it does it that way, the physics it leans on, the image processing it performs,
the state machine at its core, and the acceptance method used to trust it. It is written for a
reader who is **not** assumed to know golf, computer vision, or signal processing; terms of art are
defined where they appear.

**Why it exists.** The Python file is an *exemplar* — a research reference implementation. It will be
ported to C++ (`src/Pose/ball_temporal.h`) for the app. Python is easy to change while the ideas
settle; C++ is fast and ships. When the port happens, the C++ must reproduce this exemplar's output
(that is the acceptance test — see §12). This document is the durable record of *the approach* so the
reasoning survives the port, long after anyone remembers the specific Python lines. It follows the
same convention as the shaft port's bible,
[`club_track_v3_exemplar_explained.md`](club_track_v3_exemplar_explained.md).

**Companion documents.** The design rationale and the measured evidence are in
[`ball_detection_v2.md`](ball_detection_v2.md); the staged plan, the delete-vs-rework footprint, and
the promotion path are in
[`../implementation/ball_detection_v2_impl_plan.md`](../implementation/ball_detection_v2_impl_plan.md);
the ground-truth format is [`../reference/truth_json_schema.md`](../reference/truth_json_schema.md).

---

## 1. The problem, in one sentence

Given a video of a golfer at address filmed from the front (the "face-on" camera), find **where the
stationary golf ball is** (to a few pixels), report **when it is present**, and detect **the instant
it is struck** — *without* any user calibration, and *without* ever confidently reporting a ball
that is not there.

### 1.1 What makes this hard (and what makes it easy)

A golf ball on a hitting mat looks, to a naive detector, like "a small bright circle." That
description fails in practice for three measured reasons (full evidence in `ball_detection_v2.md`
§2):

- **The mat is often the brightest thing in the studio.** Under bright lighting a white ball on a
  pale/washed-out mat has almost **no luminance contrast** — the ball is obvious to the *eye*
  (colour) but nearly invisible in grayscale. In the worst case the mat clips to pure white (255)
  and the ball is white-on-white.
- **The scene never stops moving.** The player's shadow sweeps the mat, hands and club enter and
  leave, feet shuffle. A single-frame "find the bright circle" lights up on all of it.
- **Static distractors mimic the ball.** A painted target line, the light-pool boundary, chrome
  club-head reflections (ball-scale!), white shoes, mat speckles.

What makes it tractable is one fact the naive description ignores:

> **A golf ball is the only ball-scale feature that *appears* in the hitting area, *persists
> perfectly motionless for seconds*, and *vanishes within two frames*.** It is defined by its
> **behaviour over time**, not its appearance in any single frame.

Everything in the exemplar follows from exploiting *staticness* and *disappearance* rather than
brightness.

### 1.2 The camera and the corpus

Face-on `Face-On.mp4`, 1280×1024 (one session 720×1024), ~149 fps within the 5 s swing window. The
validation corpus is 44 real swings across four sessions spanning three lighting regimes: healthy
(dark mat, ball pops), bright/weak-contrast (pale mat, ball barely differs in luma), and fully
saturated (clipped mat). Ground truth: a single hand-marked ball centre per swing in
`truth.json` (`"ball":[px,py]`), and `capture.impactUs` (IMU/acoustic-derived, independent of vision)
for the launch instant.

---

## 2. The guiding philosophy — four pillars

Each pillar earns its keep against one of the measured failure modes above.

1. **Scale-matched band-pass (spatial).** Convert the ball's *size* into a filter. A Difference-of-
   Gaussians (DoG) at the ball's radius responds strongly to ball-scale blobs and ignores anything
   much larger (shadows, light pools) or much smaller (speckle). This is the only per-pixel image
   operation. It is insensitive to *absolute* brightness — it measures local *contrast* at ball
   scale, not luma.
2. **Novelty against a static-scene baseline.** Keep a slowly-adapted per-pixel baseline **of the
   response map** (not the image). Static distractors (painted line, pool edge, speckle) live in the
   baseline and cancel; a newly-placed ball is *novel* against it. No calibration protocol — the
   baseline is learned from the stream.
3. **Static-scene accumulation (temporal integration) — the acquisition engine.** Because the ball
   is *motionless for seconds*, integrate the novelty over a short window (a fast EMA). The ball,
   pinned to the same pixels every frame, reinforces; per-frame texture and moving-shadow noise
   average out (≈√N signal-to-noise gain). **This is what makes a low-contrast ball detectable** —
   a white ball on a bright mat has near-zero *per-frame* contrast yet is unmistakable once
   integrated. (Measured: bringing accumulation into the causal tracker took the weak-contrast
   session from **0/10 to 10/10** acquired.)
4. **Stability lock + per-frame monitoring (real-time).** A candidate must hold position (±2 px)
   with sustained *accumulated* novelty before it *locks*. Once locked, presence and the launch edge
   are read from the **instantaneous** response at the frozen spot — no accumulation lag. **The whole
   game is the split: accumulate to *find* the static ball (slow, robust); read per-frame to *watch*
   it (instant).** So the low-contrast robustness costs nothing at monitoring time, and the live
   present/absent + 2-frame launch stay real-time.

Two standing rules that fall out of these:

- **Everything is expressed in noise units of the scene itself** (robust σ of the response map),
  never absolute luma. The same thresholds hold from a dim corner to a nearly-clipped mat.
- **Colour is deliberately not a cue anywhere.** Range balls are yellow/orange, and the saturated
  regime proves luma-chroma cues can vanish entirely. The eye's "white ball on green" is a *colour*
  read the detector does not use; staticness is what carries the low-contrast case instead.

---

## 3. Inputs — the data contracts

The tracker (`BallTracker`) is deliberately thin: it consumes a **precomputed response map** and
some scalars, and owns only state + thresholds. The caller (`acceptance.py` offline, `BallDetector`
live) owns the image pipeline. This is intentional so the C++ detector can own the ROI/padding while
the tracker stays a pure, portable state machine.

Per frame the caller provides:

- **`R`** — the scale-matched DoG response over the search region, `float32`. **Computed on a
  PADDED crop then sliced to the region interior** (see §4.1 — this avoids a GaussianBlur crop-edge
  artifact that otherwise mislocks onto the shaft where it enters the region top).
- The frame's **capture timestamp** (µs, same clock as the EventBuffer). Carried so the launch
  estimate is the *frame's* time, not the handler's (§6).

Set once at construction:

- **`r_hat`** — ball radius estimate in pixels (`≈ 9.5 · frameWidth/1280`).
- **`fps`** — detector frame rate (sets the EMA rate and the lock/collapse frame counts).
- **`baseline`** (B) — the response baseline (§4.2). Offline it is seeded from the ball-absent tail;
  live it is a slow EMA of R accumulated from the between-shots empty mat.
- **`noise_scale`** — a fallback σ for degenerate (flat) frames.

Set by the caller before/around the run:

- The **search region** (hitting-area ROI, §5) — the caller crops to it; the tracker sees only that
  region.
- `address_end_idx` (offline gate bookkeeping only) — the frame before which a launch would be
  "during address" (i.e. a false launch).

Outputs (attributes read after the run, or signals live): `locked = {idx, x, y, L0}` at acquisition;
`launched = {idx, x, y}` at the collapse; `false_launches` count.

---

## 4. The response math (§4.1 of the design)

### 4.1 The band-pass, and why it is padded

```
R(x,y) = GaussianBlur(gray, σ₁) − GaussianBlur(gray, σ₁·3.2)     σ₁ = r_hat / 1.6
```

A DoG is a band-pass tuned to a blob of radius ≈ σ₁·1.6 = r_hat. Subtracting a wider blur removes
large-scale illumination structure (shadows, light pools, gain drift) — so a sweeping shadow, being
large-scale, produces **no** ball-scale response. This is the single most important reason the
design band-passes *before* reasoning temporally: differencing raw luma first lets shadow deltas
dominate (measured); band-passing first keeps them out of the pipeline entirely.

**Padding (a hard-won detail).** A GaussianBlur of a *tightly* cropped region reflects at the crop
border; where a bright feature (the club shaft) is cut at the region's top edge, the DoG lights up a
spurious ridge along that edge and the tracker locks it. **Fix:** the caller crops the region **plus
a margin** (`≈ 6·r_hat` each side), computes the DoG on that padded crop, and slices out the region
interior. The tracker then never sees a crop-edge artifact. *This must be reproduced in C++.*

### 4.2 Baseline, novelty, and the accumulator

```
noise = 1.4826 · MAD(R)                         robust per-frame response noise
B     = response baseline                        (seeded offline; slow change-gated EMA live)
A     = fast EMA of R,  A += α·(R − A),  α = 1/(τ_acc·fps),  τ_acc ≈ 0.35 s     ACQUISITION
D     = A − B                                    accumulated novelty (raw)
N_acc = D / (1.4826 · MAD(D))                    acquisition novelty, in scene-noise units
```

- **MAD** is the median absolute deviation; `1.4826·MAD` is a robust (outlier-proof) estimate of the
  standard deviation. Using it means a few strong features (the ball, a distractor) do not inflate
  the noise floor the way a plain standard deviation would.
- **The accumulator `A`** is the acquisition engine (pillar 3). It is a leaky integrator of the
  response with a ~0.35 s memory. A static ball reinforces in `A`; transient noise averages out. `A`
  is initialised to the first frame's `R` and updated causally — identical offline and live.
- **`D = A − B`** is the *accumulated novelty*: high only where the accumulated response exceeds the
  static-scene baseline. The ball (present, novel) is high; static distractors (in `B`) cancel; the
  moving shadow (washed out in `A`) is low.
- **`N_acc`** normalises `D` into σ units. **Acquisition runs on `N_acc`.** Monitoring (§4.4) does
  **not** — it reads the instantaneous `R`.

> **Why the baseline differs offline vs live, and why that is fine for parity.** A recorded 5 s
> window opens with the ball already at rest, so there is no empty warm-up for a live-style baseline.
> Offline, `acceptance.py` therefore seeds `B` from the **ball-absent tail** (post-launch frames),
> which still contains every static distractor but not the ball. Live, `B` is the between-shots empty
> mat accumulated by a slow EMA. **Parity (§12) is on the response math and the state machine given
> the *same* B — not on where B's samples come from.**

---

## 5. The search-space prior — ROI first, corridor second

Search is confined to the **hitting-area ROI** — the tight region around the ball spot. This is the
*primary* distractor cut: anything outside it is never a candidate, so only features *inside* the
hitting area (most notably the clubhead resting against the ball at address) can compete, and those
are handled by the shape gate (§4.5) and the temporal logic. **Measured:** with a loose fallback
band, a bare-mat novelty artifact ~100 px from the ball stole several weak-contrast locks;
constraining to a hitting-area ROI recovered the correct spot on all of them.

- **Live:** the user's drawn ROI (`setRoi`, unchanged from v1). This is better than anything the
  offline harness has.
- **Offline:** the harness has no recorded ROI (a **provenance gap** — swing.json does not store it),
  so it uses a **per-session ball-cluster proxy**: the median labelled ball position across the
  session, ±0.06 in x and from just above the cluster to the frame bottom. This is the aggregate spot
  a user would frame, not any single swing's own label.
- **Refinement, when pose is available:** the stance *corridor* — the ball is always between the feet
  and below the ankle line — tightens the ROI further, for free (pose runs on the same frames). See
  `ball_detection_v2.md` §4.1a.

---

## 6. The state machine (the core)

```
            candidate appears            stable ≥ T_lock
  SEARCH ────────────────────► CANDIDATE ────────────────► LOCKED
    ▲   N_acc>k_appear, disc     │  moved / not held         │
    │   inside the ROI           │                           │ per-frame collapse (cliff)
    │                            ▼                           ▼
    └─────────────────── back to SEARCH                 VANISHED ──► ballLaunched
```

### 6.1 SEARCH / CANDIDATE — acquisition (runs on `N_acc`, `D`)

Per frame:

1. **Find peaks** (`_find_peaks`): scan up to 12 local maxima of `N_acc`; keep a peak only if it is
   above `k_appear` **and** its accumulated response `D` there is a ball-scale disc (`is_blob`, §4.5);
   non-max-suppress each found peak by a disc of radius `2·r_hat`; stop at `K=3` kept blobs. Scanning
   past rejected ridges (not just taking the argmax) is essential — a bright ridge would otherwise
   occupy the slot the ball needs.
2. **Track candidates** across frames (`_search`): match each current peak to an existing candidate
   within `±2 px` (`LOCK_STABILITY_PX`); a matched candidate increments its `hold` and appends the
   peak's `N_acc`; unmatched candidates are dropped (they moved/vanished); unmatched peaks start new
   candidates. Keep the top `K=3` by `hold`. *This is why the stationary ball wins:* it accrues
   `hold` every frame while a waggling club/hand keeps resetting.
3. **Lock** the longest-held candidate that has `hold ≥ T_lock·fps` (`T_lock = 0.5 s`) **and** median
   `N_acc ≥ k_lock`. On lock: refine the centre by a 2-D quadratic fit over the 3×3 `N_acc`
   neighbourhood (`subpixel_peak`); record `L0 = at-spot max of R` (the per-frame level, for the
   collapse test); enter LOCKED.

### 6.2 LOCKED — monitoring (runs on per-frame `R`, real-time)

Once locked, the tracker reads only the **instantaneous** response `R` at the frozen integer spot
(`_at_spot` = max of a 5×5 neighbourhood). No accumulation. This keeps presence continuous and the
launch edge sharp. `L0` is the pre-launch at-spot level.

### 6.3 The launch edge — a cliff, not a fade (§4.5)

```
launch when:  at-spot R < 0.25·L0  for ≥ 2 consecutive frames,
              the frame BEFORE the run being ≥ 0.80·L0            (a cliff)
```

Measured collapse shapes are 84→39→−5 and 26→21→3 within 2 frames at 149 fps. The "preceding frame
≥ 0.80·L0" clause is what separates a genuine launch (a cliff) from an occlusion dip (a club passing,
a hand reaching in — partial and recovering). The tracker keeps a short history of recent at-spot
values and fires when the triplet `(above, below, below)` appears; the launch frame reported is the
**first** below-floor frame.

> **Ball-departure latency (a V0 measurement to carry into the port).** The visible collapse lands a
> consistent **+3–4 frames (~20–27 ms) after** the acoustic/IMU `impactUs` — the ball is in
> contact/compression before it visibly departs. This is real physics, not error. Treat it as a
> calibrated per-source constant (`kBallLaunchLatencyUs`, sibling of `kImuBleLatencyUs`) and
> back-date `estImpactUs` by it. The launch feeds the shot arbiter as a conf-0.6 candidate that
> **cannot self-commit** (single-modality floor 0.8), so it is safe even before this is dialled in.

---

## 7. The shape gate — `is_blob` (§4.4)

A golf ball is an **isotropic ball-scale disc**; the painted line and the address shaft are elongated
ridges; a noise/normalization spike is a near-point. The gate distinguishes them from the *shape* of
the accumulated novelty, **amplitude-invariantly** (this matters — an earlier peak/ring-ratio test was
amplitude-sensitive and wrongly rejected the *weak* 07-03 ball even though its novelty was strong):

```
window a (1.8·r_hat)-radius box of the positive novelty  P = max(D, 0)
covariance of P (value-weighted 2-D second moments) → eigenvalues λ1 ≥ λ2
elongation = sqrt(λ1/λ2)          disc ≈ 1, ridge ≫ 1
size       = sqrt(λ2) / (r_hat/2) ≈ 1 for a ball, ≈ 0 for a point spike
accept if  elongation ≤ 3.0  AND  0.35 ≤ size ≤ 2.2
```

Run on `D` (background-subtracted), where the ball is a clean disc even when it is buried in a bright
mat in the *raw* image. This single test rejects the painted line, the shaft, and the normalization
spike, while passing a weak-but-round ball.

---

## 8. Offline (windowed) vs live (incremental) — the same code

The tracker is causal and frame-at-a-time, so **the identical state machine runs live and over a
recorded window** — only the frame source and the baseline seeding differ:

| | Offline (`acceptance.py`) | Live (`BallDetector`) |
|---|---|---|
| Frames | decode `Face-On.mp4`, all frames | `framePreprocessed` stream |
| Baseline `B` | median DoG over ball-absent tail | slow change-gated EMA of R (between shots) |
| ROI | per-session ball-cluster proxy | user-drawn `setRoi` (+ pose corridor) |
| Accumulator `A` | causal fast EMA (identical) | causal fast EMA (identical) |
| Purpose | measure the §9.1 gates, be the parity oracle | presence, launch, exposure signal |

---

## 9. Parameters — every one, with provenance

All are module constants in `ball_state_machine.py` unless noted. None is an absolute luma.

| Param | Value | Meaning / provenance |
|---|---|---|
| `r_hat` | `9.5 · W/1280` px | ball radius; corpus ball r ≈ 9–10 px @1280 |
| DoG σ₁ | `r_hat/1.6` | peak DoG response at true ball radius |
| DoG σ₂/σ₁ | `3.2` | wide-blur ratio; removes large-scale structure |
| pad | `6·r_hat` | padded-crop margin (kills the blur crop-edge artifact) |
| `T_ACC_S` | `0.35 s` | accumulator EMA window (static ball reinforces) |
| `K_APPEAR` | `5.0` σ | min accumulated novelty for a SEARCH peak |
| `K_LOCK` | `5.0` σ | min median novelty over T_lock to lock |
| `T_LOCK_S` | `0.5 s` | candidate hold time to lock |
| `LOCK_STABILITY_PX` | `2.0` px | candidate match/stability radius |
| `K_PEAKS` | `3` | concurrent candidate peaks tracked |
| `NMS_RADIUS_MULT` | `2.0` (·r_hat) | non-max-suppression radius |
| `BLOB_MAX_ELONG` | `3.0` | shape gate: max eigenvalue-ratio (disc vs ridge) |
| `BLOB_SIZE_LO/HI` | `0.35 / 2.2` | shape gate: blob extent vs r_hat/2 (spike/oversize reject) |
| `COLLAPSE_FROM` | `0.80·L0` | pre-collapse level (the top of the cliff) |
| `COLLAPSE_FLOOR` | `0.25·L0` | collapsed level (the bottom of the cliff) |
| `COLLAPSE_FRAMES` | `2` | consecutive below-floor frames to fire |
| ROI margin (offline) | `±0.06` x, `ny−0.07 → 1.0` | per-session cluster ROI proxy |
| `kBallLaunchLatencyUs` | ~`20–27 ms` (V0) | ball-departure lag to back-date `estImpactUs` |
| launch `conf` | `0.6` (`0.4` if satFrac>0.25) | below the arbiter's 0.8 self-commit floor by design |

Tuning philosophy: these are fixed by design and moved only by corpus evidence via `acceptance.py`.

---

## 10. Acceptance gates (what `acceptance.py` measures, design §9.1)

Run over every corpus swing at native fps, against the independent truth:

- **G1 — acquisition:** lock before `impact − 1 s` on ≥95 % of swings whose ROI exposure is trustworthy.
- **G2 — position:** locked centre within a few px of the human ball centre (`truth.json`).
- **G3 — no false launches:** zero launch events during address, corpus-wide.
- **G4 — launch timing:** launch within a few frames of `capture.impactUs` (after the latency constant).
- **G5 — exposure honesty:** the saturated session asserts the exposure warning and does not
  self-commit launches.

Positions are compared in normalized coordinates and converted to source pixels, because the mp4 and
the `source.width/height` a label uses may differ; the face-on stream is selected by
`setup.perspective == 2` (these captures also carry a down-the-line stream — do **not** normalize
against the first video stream).

---

## 11. Known refinements (open at V0 freeze)

State honestly what is *not* yet solved, so the port does not enshrine it as correct:

- **Position on weak contrast (~9 px).** The grounded clubhead sits against the ball at address and
  is also static, so it enters the accumulated blob and pulls the centroid. Fix directions: refine
  the centre from the sharper *per-frame* response at lock, or explicitly split the ball from the
  adjacent clubhead. Healthy-exposure position is already ≤3 px.
- **Launch completeness + latency.** Some locked swings do not produce a clean cliff (the launch
  never fires); the fires that do occur are +3–4 frames late (the calibrated latency above). Both
  are why the launch stays arbiter-gated.
- **Exposure health metric.** `satFrac` (fraction ≥250) measured over the *tight* ROI over-flags —
  07-03/07-05 read 0.32–0.35 yet acquire cleanly. A **ball-vs-mat contrast/SNR** signal is the better
  predictor of when *acquisition* is at risk; add it alongside satFrac.
- **Provenance.** swing.json must record the hitting-area ROI (§5) so re-analysis uses the real one.

---

## 12. Porting to C++ — what must be preserved

> **DONE (2026-07-08, `ff1e53d`).** Ported to `src/Pose/ball_temporal.h`; parity is **byte-exact** on
> 3 golden swings (healthy / weak-contrast / saturated) via `ball_temporal_parity_test` +
> `tools/balllab/gen_parity_ref.py`. Two must-preserve details bit the port and are now enforced +
> commented in the header: **(a)** the padded crop must be `.clone()`d before the DoG (a bare `cv::Mat`
> submatrix reads parent pixels past the ROI border, defeating the padding and diverging from the
> exemplar's fresh `cvtColor` slice); **(b)** the EMA `A += af·(R−A)` and `N_acc = D/noise` run in
> **float32** manual loops, not OpenCV's double-scalar path, else EMA feedback drifts the accumulator
> and shifts the lock frame vs numpy. The parity test consumes python's **own** dumped `R`+`B` (not a
> C++ decode) because independent OpenCV versions decode the corpus H.264 to different pixels — so it is
> a byte-exact test of the state machine; the DoG/padding pipeline is verified separately by
> `ball_temporal_test`. This python exemplar remains the **regression oracle**: iterate here, re-parity,
> re-port — never hand-tune the C++.

The port target is `src/Pose/ball_temporal.h` (header-only, OpenCV-only, no Qt; its test is a `NO_QT`
target). Structure: **pure functions** + a small `TemporalBallTracker` struct.

**Must be reproduced exactly (these are the algorithm):**

1. **The response pipeline** — DoG (σ₁=r/1.6, σ₂=3.2σ₁) on a **padded** crop, sliced to the region
   interior. The padding is not optional; skipping it reintroduces the crop-edge shaft mislock.
2. **The accumulator** — `A += α(R−A)`, `α = 1/(τ_acc·fps)`, `A` initialised to the first `R`.
3. **The novelty** — `D = A − B`; `N_acc = D / (1.4826·MAD(D))`. `MAD` is the median-absolute-deviation,
   robust, over the region.
4. **The state machine** — SEARCH/CANDIDATE/LOCKED/VANISHED exactly as §6, including: K=3 concurrent
   candidate tracking with ±2 px matching and top-K-by-hold pruning; scan-past-rejected-ridges peak
   finding; lock on `hold ≥ T_lock·fps` and median `N_acc ≥ k_lock`; sub-pixel quadratic centre.
5. **The shape gate** — the value-weighted 2-D second-moment `is_blob` on `D` (elongation ≤ 3, size in
   [0.35, 2.2]). **Not** the old ring test.
6. **The launch cliff** — `(≥0.80·L0, then 2× <0.25·L0)` on **per-frame** `R` at the frozen spot;
   report the first below-floor frame; back-date by `kBallLaunchLatencyUs`.
7. **The acquisition/monitoring split** — accumulate to find, read per-frame to watch. Do not let the
   accumulator smear the launch edge.
8. **The timestamp is the *frame's*** (from the capture pipeline), never `nowMicros()` at handler time.

**Legitimately differs (and why parity still holds):**

- **Baseline `B` source** — offline tail-seed vs live slow-EMA. Parity is measured with the *same* B.
- **ROI** — offline cluster proxy vs live user ROI. Parity fixtures pin the ROI.
- **Cross-language float** — FFV1/lossless decode makes both sides see identical pixels; residual
  differences are float-arithmetic only (shaft-port lesson).

**The parity contract (V1 acceptance).** Generate a reference on the **same host** (never cross-host)
by running the Python exemplar on 3 golden swings with a fixed ROI and baseline; run the C++ core on
the same frames + ROI + baseline; compare **locked centre** (≤ ~0.5 px), **launch frame** (exact),
and **`N_acc` at the locked spot** (tolerance). Env-var fixture, SKIP when absent, tolerance gate —
modelled exactly on `src/Analysis/tests/shaft_parity_test.cpp`. Regenerate the Python reference on
the dev box; do not compare across machines.

**Do not** hand-tune the C++ against a single live swing. When a live issue is algorithmic, reproduce
it in `ball_state_machine.py` (seconds per iteration), fix it there, re-parity, re-port. The Python
exemplar remains the regression oracle after the port ships.

---

## 13. Glossary

- **DoG (Difference-of-Gaussians)** — one blurred copy of the image minus a more-blurred copy; a
  band-pass filter that responds to blobs of a chosen size.
- **σ (sigma)** — a Gaussian blur radius; also, "in σ units" = measured in robust standard deviations
  of the response noise.
- **MAD** — median absolute deviation; `1.4826·MAD` ≈ a robust standard deviation.
- **EMA (exponential moving average)** — a leaky running average; `A += α(R−A)`; `1/α` ≈ its memory
  in frames.
- **Novelty** — accumulated response minus the static-scene baseline, in σ units; "how unlike the
  empty scene is this spot right now."
- **Lock** — the tracker has committed to a stable, novel, ball-shaped spot as the ball.
- **Cliff** — the sharp two-frame collapse of the at-spot response that marks the ball being struck.
- **ROI (region of interest)** — the hitting-area rectangle the search is confined to.
- **Stance corridor** — the between-the-feet, below-the-ankle region derived from pose; an optional
  tightening of the ROI.
- **satFrac** — fraction of ROI pixels at/near clipping (≥250); an over-exposure signal.
- **Face-on** — the front-view camera (`setup.perspective == 2`).
