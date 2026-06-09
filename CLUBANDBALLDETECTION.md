# Camera-Based Golf Ball & Club Detection + Robust Swing-Phase Identification

**A state-of-the-art review and adoption plan for PinPoint Studio**
Compiled 2026-06-09. IR/active-illumination is explicitly **out of scope** (it conflicts with co-located launch monitors). Everything below is **RGB-camera-only**.

> **How to read this report.** Each technical claim is tagged with a confidence
> (`high` / `medium`) and the adversarial-verification vote it survived
> (e.g. `3-0` = all three independent verifiers confirmed; `2-1` = one dissent on a
> nuance). Claims that were *refuted* in verification are listed in
> [§9](#9-refuted-claims--do-not-cite) so we don't accidentally repeat them. Source URLs
> are in [§10](#10-sources).
>
> **Time-sensitivity caveat (applies throughout).** The strongest golf-*ball*-specific
> benchmark (arXiv 2012.09393) is from 2020 and names YOLOv3 / YOLOv4 / YOLOv3-tiny.
> By 2026 these are superseded by YOLOv8/v11/v12 and RT-DETR. **Read the named models as
> architectural *categories*** — "accurate one-stage", "two-stage high-precision",
> "tiny fast" — not as literal version recommendations. The *principles* (patch crop,
> Kalman loop, motion channel, P2 high-res features, multi-frame heatmaps) carry forward
> unchanged.

---

## 1. TL;DR — what to adopt

| Problem | SOTA technique | PinPoint action |
|---|---|---|
| **Ball detection** | CNN object detection on **small cropped patches** (not the full frame), wrapped in a **Kalman tracking-by-detection loop** (predict → crop → CNN-refine → update). `high, 3-0` | Replace the current **Hough-circle** `BallDetector` with a small ONNX YOLO model + Kalman patch tracker, reusing the existing ONNX runtime + GPU (Vulkan/CUDA) infra. |
| **Ball model choice** | Accurate one-stage (≈YOLOv4) ≈ two-stage (Faster R-CNN) at ~95–96% mAP@0.5; **tiny one-stage** (≈YOLOv3-tiny) trades a little accuracy for ~358 fps. `high, 3-0` | Use a modern **nano/tiny YOLO** (the 2026 equivalent) for real-time live; optionally a heavier model for offline post-shot refinement. |
| **Small-fast-blurred ball** | **Multi-frame heatmap** networks (TrackNet, 3-frame input) + **inter-frame motion as an extra input channel** + **P2 high-res shallow features** materially raise recall. `high, 3-0` (`medium, 2-1` on specific numbers) | Feed the detector temporal context (motion channel and/or 3-frame stack); prefer an architecture with a P2/high-res head. |
| **Club / clubhead detection** | **No verified golf-specific source exists** — this is an open gap. `caveat` | Transfer the ball techniques to the clubhead (small-fast-object) **and** detect the **shaft as a line/keypoint** via the existing pose pipeline. Treat as R&D, not a solved problem. |
| **Swing-phase ID (vision)** | **GolfDB / SwingNet** paradigm: MobileNetV2 → **bidirectional LSTM** detects 8 canonical events as temporal event-localization. `high, 3-0` | A reference baseline if/when we want vision-only phases. |
| **Robustness to waggles / fake moves** | A dedicated **down-weighted "No-event" class** (weight 0.1 vs 1.0, ~35:1 imbalance) **+ temporal LSTM context** absorbs pre-shot routine motion. `high, 3-0` | Mirror this in our segmenter; **fuse with PinPoint's existing IMU swing signal** — our single biggest advantage over vision-only papers. |

**The headline for PinPoint:** we already own two assets the literature treats as hard problems —
a **GPU ONNX inference pipeline** (MoveNet/MediaPipe/ViTPose) and a **calibrated IMU swing
signal** (host-fused quaternions, `PhaseSegmenter`). The SOTA recommendation is therefore not
"copy a paper" but "**host a small YOLO in our existing ONNX runtime, and fuse vision phase
events with the IMU we already trust**."

---

## 2. Part 1 — Camera-based golf BALL detection (no IR)

### 2.1 Where PinPoint is today

`src/Pose/ball_detector.h/.cpp` uses the **OpenCV Hough circle transform**
(`HOUGH_GRADIENT_ALT`) + a white-saturation HSV mask, searching only inside a user ROI.
This is a **classical** detector. The literature is unanimous that classical
circle/colour detection is brittle for the golf ball case (small, fast, motion-blurred,
variable lighting, white-on-white backgrounds). It is fine as a cheap "ball is teed up and
stationary" presence check (which is roughly how we use it), but it will **not** survive
impact/launch where the ball blurs and accelerates.

### 2.2 SOTA technique #1 — patch-based detection (don't feed the full frame)

> **Claim `high, 3-0`:** The core SOTA technique for reliable real-time golf-ball detection is
> **patch-based detection** — run the CNN on small **~416×416 image patches** rather than the full
> >1080p frame. Feeding high-resolution frames directly "led to significant computational load and
> terrible results"; detection on small patches "increase[s] the performance of small ball
> detection." (arXiv 2012.09393; corroborated by SAHI/sliced-inference, Ultralytics, MDPI Sensors
> 21(9):3214.)

A golf ball is a handful of pixels in a 1080p+ frame. Downscaling the whole frame to a
detector's input size destroys it; running the detector at native resolution is too slow and
still weak on tiny objects. The fix is to **crop a small window and detect within it** — the
window is at native resolution, so the ball is large *relative to the patch*.

### 2.3 SOTA technique #2 — Kalman tracking-by-detection loop

> **Claim `high, 3-0`:** Wrap detection in a **tracking-by-detection** loop: a discrete **Kalman
> filter predicts** the next ball location each frame, a small patch is **cropped around the
> prediction**, the CNN detector **refines** the location, and the result **updates** the filter — a
> closed predict → crop → refine → update cycle that handles fast motion and stays real-time.
> (arXiv 2012.09393, §III; reference implementation at `github.com/rucv/golf_ball`.)

This is what makes §2.2 usable on a *moving* ball: the Kalman prediction tells you *where to
crop next*. The loop:

```
            ┌──────────────────────────────────────────────┐
            │  Kalman TIME-UPDATE: predict next ball (x,y)  │
            └───────────────────────┬──────────────────────┘
                                     ▼
        crop ~416×416 patch around the predicted location (native res)
                                     ▼
                 CNN detector refines ball centre in patch
                                     ▼
            ┌──────────────────────────────────────────────┐
            │  Kalman MEASUREMENT-UPDATE: correct with det. │
            └───────────────────────┬──────────────────────┘
                                     └────────► next frame
```

When the detector misses a frame (blur/occlusion), the Kalman prediction carries the track,
and the next good detection re-corrects it.

### 2.4 Model-family selection — accuracy vs speed

> **Claim `high, 3-0/2-1`:** On golf-ball data, **accurate one-stage** (YOLOv4: 99.3% mAP@0.25 /
> 95.6% mAP@0.5) and **two-stage** (Faster R-CNN: 98.3% / 95.9%) are **essentially tied at the top**,
> far ahead of SSD (78.3% @0.5), RefineDet (81.5%) and the tiny model (84.2%). (arXiv 2012.09393,
> Table III.)

> **Claim `high, 3-0`:** **Tiny one-stage** (YOLOv3-tiny) is the **speed pick**: **2.79 ms / ~358 fps**
> vs YOLOv4 35.84 ms / 27.9 fps and Faster R-CNN 36.00 ms / 27.8 fps, at modest accuracy cost
> (92.3% mAP@0.25). The paper calls it the "best-buy detector." (arXiv 2012.09393, Table III.)

> **Claim `high, 2-1`:** Selection heuristic — **one-stage (YOLO/SSD) when real-time matters;
> two-stage (Faster R-CNN) when maximum precision matters**. Modern YOLO has narrowed the gap, so
> hybrid pipelines are increasingly recommended. (Ball-detection review, PeerJ CS 2025 / PMC12453710.)

**Practical mapping for PinPoint:** because the patch is small (≈416×416), even a "heavy" model
runs fast *per patch*. The accuracy/speed split therefore matters most for **live** (use a
nano/tiny YOLO at high fps) vs **offline post-shot refinement** in `ShotProcessor`/
`ShotAnalyzer` (we can afford an accurate model on the frozen `SwingWindow`).

> **Feasibility `high, 3-0`:** Single-stage CNN detection of small fast balls in **plain RGB video,
> no IR**, is proven; YOLOv3 was successfully adapted using domain-specific priors. (Hiemann/Kautz et
> al., *Sensors* 21(9):3214, 2021.) — directly supports PinPoint's IR-free constraint.

### 2.5 Small-fast-blurred-ball enhancements (the recall wins)

The golf ball at/after impact is the worst case: tiny, ~50 m/s, heavy motion blur, sometimes
occluded by the clubhead. Three transferable techniques raise recall here:

1. **Multi-frame heatmap networks (TrackNet).**
   > **Claim `high, 3-0` (`2-1` on exact numbers):** TrackNet generates a **Gaussian detection
   > heatmap** from 640×360 input; feeding **three consecutive frames** lets it learn ball
   > trajectory, sharply improving detection of **blurry / afterimage / momentarily occluded** balls.
   > Single-frame F1 92.5% → three-frame F1 98.2% (tennis). (arXiv 1907.03698.)
   The heatmap regression (vs box) and temporal stacking are the transferable ideas.

2. **Inter-frame motion as an extra input channel.**
   > **Claim `high, 3-0`:** Adding a **motion channel** (e.g. HSV value-channel difference vs prior
   > frames) improves fast-ball recall and raises processing speed by >10%. *Caveat:* gain is
   > **training-dependent** — without proper refinement training, the motion channel raised precision
   > but **dropped recall** (91.4%→77.8%). (Sensors 21(9):3214 / PMC8124271; beach-volleyball data.)

3. **Modern targeted YOLO with P2 high-res features.**
   > **Claim `medium, 3-0` (architecture) / `2-1` (numbers):** A **dual-flow shallow fusion pyramid
   > combining P2 (high-resolution shallow) features with bidirectional (BiFPN-style) fusion**, plus
   > occlusion-aware attention and an NWD+IoU loss, beats stock YOLOv8/v10/v11 on small high-speed
   > balls — "by up to 12.5%" (YOLO-Ball, *Proc. IMechE Part P*, 2026). *Confidence is medium:* results
   > are on a **self-constructed tennis** dataset, absolute mAP is modest (70.9%), and golf transfer is
   > a reasonable but unproven inference. P2 heads independently show +20–36% small-object mAP.

**Takeaway:** if a stock nano-YOLO under-detects the ball at impact, the proven levers (in
order of evidence strength) are: **(a) tighter patch crop**, **(b) multi-frame/heatmap input**,
**(c) a motion channel**, **(d) a P2 high-resolution detection head**.

### 2.6 Recommended ball-detection approach for PinPoint

```
[ teed / stationary ]   keep the cheap Hough/HSV presence check (already wired,
                        gates "ball present" overlays + shot arming)
        │
        ▼  on shot trigger (IMU impact / SHOT button)
[ launch / flight ]     ONNX nano-YOLO  +  Kalman tracking-by-detection
                        - seed Kalman from the known tee position
                        - predict → crop ~416² native patch → CNN refine → update
                        - run on the down-the-line + face-on streams we already have
        │
        ▼  offline (ShotProcessor / ShotAnalyzer on the frozen SwingWindow)
[ refinement ]          heavier accurate model + heatmap/motion for missed frames,
                        recover full launch trajectory for metrics
```

Reuse points in the existing codebase:
- **ONNX runtime + GPU EP selection** already exists for pose (`pose_estimator_movenet.cpp`,
  `pose_estimator_vitpose.cpp`, `KokoroTTSEngine.cpp`). A YOLO `.onnx` slots into the same
  pattern (CoreML → CUDA → DirectML → CPU fallback).
- **`BallDetector`'s ROI + thread + FrameThrottle wiring** (`detect()` / `detectionSkipped()` /
  `ballDetected()`) is the right shape — swap the *internals* (Hough → CNN+Kalman) while keeping
  the `BallDetection {found,x,y,radius,detectMs}` contract and the `detectionSkipped()`
  busy-release semantics (a `found=false` `ballDetected` would fire a spurious ball-lost replay).
- **Multi-camera fan-out** (`CameraInstance` publish/subscribe) already delivers preprocessed
  BGR frames to consumers.

---

## 3. Part 2 — Camera-based CLUB / clubhead detection

> **⚠ Honest gap.** Across 22 verified claims, **no source directly covers camera-based golf
> club/clubhead detection or tracking.** The verified evidence is for **ball** detection and
> **swing-event** sequencing. Club detection must be treated as a **transfer/inference** from the
> small-fast-blurred-object techniques above, **not** as something with golf-specific verified
> evidence. This is the **largest gap** relative to the research question and a genuine R&D area
> for PinPoint to own.

### 3.1 Why the club is harder than the ball

The clubhead at impact is *faster than the ball* (it drives the ball) and is **non-rigid in
appearance** (rotates, glints, blurs into a streak). The shaft is a thin line that overlaps the
body. There is no canonical public clubhead-detection benchmark equivalent to GolfDB/2012.09393.

### 3.2 Two transferable strategies

1. **Clubhead as a small-fast object** — apply §2 verbatim: patch-based CNN + Kalman
   tracking-by-detection, with multi-frame/heatmap + motion-channel enhancements (the clubhead
   streak is *exactly* the blur case TrackNet's 3-frame input was built for). Seed the Kalman
   track from the address position (clubhead starts behind the ball, near a known location).

2. **Shaft as a line / keypoint via the existing pose pipeline** — PinPoint already runs
   keypoint estimators (MoveNet/MediaPipe/ViTPose). The club shaft can be modelled as **two
   keypoints (grip + clubhead)** or a **line segment**, learned as an extra "limb" in a custom
   keypoint head, or detected with a line/segment detector constrained to extend from the lead
   hand (which pose already localizes). This gives shaft angle / clubhead path without solving
   full clubhead object detection, and is robust to the clubhead's appearance changes because it
   is anchored to the (well-tracked) hands.

   *Combine both:* use the hand keypoints to **seed and bound** the clubhead patch search (§3.2.1),
   collapsing the search space dramatically.

### 3.3 Recommended club approach for PinPoint

Phase it as R&D, lowest-risk first:
1. **Shaft/clubhead as keypoints anchored to the lead hand** (reuses pose infra; robust).
2. **Clubhead patch detector seeded by the hand keypoint** (adds clubhead-path precision).
3. **Multi-frame/heatmap + motion channel** only if (1)+(2) miss the impact-window streak.

Validate against PinPoint's **IMU club-mount signal** where available — the wrist/club IMU gives
an independent ground truth for clubhead motion that the literature's vision-only methods lack.

---

## 4. Part 3 — Swing-phase identification robust to waggles & fake movements

### 4.1 Where PinPoint is today

Phases are currently derived from the **IMU** stream: `PhaseSegmenter::segment(streams,
impactUs)` consumes fused per-segment quaternions and emits `PhaseEvent`s (Address / Top /
Impact etc.) used by `MetricExtractor` and `SwingScorer`. This is a strong base — IMU dynamics
are far less fooled by *visual* clutter than a camera. The question is how to make **event
boundaries** robust to **waggles, practice swings, and idle fidget** that look like the start of
a swing.

### 4.2 SOTA vision paradigm — GolfDB / SwingNet (the reference standard)

> **Claim `high, 3-0`:** Frame swing-phase ID as **temporal event localization**: detect the **8
> canonical, single-frame events** that delimit the phases — **Address, Toe-up, Mid-backswing, Top,
> Mid-downswing, Impact, Mid-follow-through, Finish**. (GolfDB, McNally et al., CVPR-W 2019,
> arXiv 1903.06528.) This taxonomy is still the stable standard in 2026.

> **Claim `high, 3-0`:** **SwingNet** is the canonical detector: **MobileNetV2 backbone → single-layer
> bidirectional LSTM (256 hidden units)** over the frame sequence, mobile-deployable. It detects all 8
> events at **76.1%** average and 6-of-8 at **91.8%** (PCE = Percentage of Correct Events within
> tolerance). The LSTM provides the temporal context that makes per-frame event spotting robust.
> (arXiv 1903.06528.)

> **Resource `high, 3-0`:** **GolfDB** = **1400** labelled real-world swing videos (event frames,
> bounding box, player, sex, club type, view type) with public code — a ready fine-tuning base.
> *(Note: "club type" is metadata, not clubhead-position annotation.)* (github.com/wmcnally/golfdb.)

### 4.3 The waggle-rejection mechanism (this is the direct answer)

> **Claim `high, 3-0`:** Robustness to **waggles / practice swings / idle motion** comes from a
> dedicated **down-weighted "No-event" class** plus **temporal LSTM context**. SwingNet adds a **9th
> "No-event" class** to absorb the abundant non-event frames, given **loss weight 0.1** (vs 1.0 for
> events) to counter the **~35:1** class imbalance; the recurrent context disambiguates pre-shot
> routine motion. The paper explicitly names the problem: Address "may be falsely detected during the
> pre-shot routine, which often includes full or partial practice swings, and frequent clubhead
> waggling." (arXiv 1903.06528.)

So the two robustness primitives are:
1. **An explicit "nothing is happening" class**, heavily down-weighted so it doesn't swamp
   training but exists to *catch* waggle/idle frames instead of mislabelling them as events.
2. **Temporal context** (bidirectional LSTM, or any sequence model) so a candidate "Address" is
   only accepted if the *surrounding* frames form a plausible swing — a waggle has no follow-on
   backswing, so it's rejected.

### 4.4 PinPoint's structural advantage: IMU ⊕ vision fusion

The papers above are **vision-only** and cap at 76.1% all-8-event PCE. PinPoint has a **calibrated
IMU swing signal** the papers don't. The most robust achievable design is **sensor fusion**:

- **IMU as the primary gate.** A real swing has an unmistakable IMU dynamic signature (angular
  velocity ramp into transition, impact spike). Waggles and practice motion are **low-energy /
  non-monotonic** and can be rejected *before* committing to a phase boundary — gate phase
  detection on the IMU swing-energy / monotonic-backswing test, which a camera cannot see as
  cleanly. This is effectively the "No-event class" job, done with physics instead of a learned
  prior, and it's already half-built in `PhaseSegmenter`.
- **Vision for events the IMU localizes poorly.** Address (a *static* pose) and Top (a velocity
  zero-crossing that's noisy on integrated gyro) benefit from the visual SwingNet-style detector;
  Impact and transition are sharpest on the IMU.
- **Cross-confirmation.** Accept a phase boundary only when IMU and vision **agree within a time
  tolerance**; otherwise trust the IMU and flag low confidence. This beats either modality alone
  and is exactly the open question the research flagged (§8).

**Recommended phase pipeline for PinPoint:**
```
IMU swing-energy gate ──► is this a real swing or a waggle?  (reject low-energy / non-monotonic)
        │ (real swing only)
        ▼
IMU PhaseSegmenter ─────► coarse 8-event boundaries (Impact/transition sharp)
        │
        ▼  (optional, when face-on camera active)
Vision SwingNet-style ──► refine Address & Top frames; add "No-event" rejection as a second vote
        │
        ▼
Fused PhaseEvent[] ─────► MetricExtractor / SwingScorer  (with per-event confidence)
```

This keeps our IMU-first design (CLAUDE.md: quaternion-only, IMU-fused world frame) and layers
vision in only where it strictly helps.

---

## 5. Recommended end-to-end architecture for PinPoint

```
                 ┌───────────────────────────────────────────────────────────┐
                 │                     CameraInstance(s)                       │
                 │  face-on (perspective==2)  +  down-the-line  (existing)     │
                 └───────────────┬───────────────────────────┬───────────────┘
                                 │ preprocessed BGR frames     │
            ┌────────────────────▼─────────┐     ┌────────────▼───────────────┐
            │  BALL  (replaces Hough)       │     │  POSE / CLUB                │
            │  • Hough presence (teed)      │     │  • MoveNet/MediaPipe/ViT    │
            │  • ONNX nano-YOLO + Kalman    │     │    (existing ONNX)          │
            │    tracking-by-detection      │     │  • shaft keypoints anchored │
            │  • patch crop ~416²           │     │    to lead hand (new head)  │
            │  • motion ch. / heatmap (opt) │     │  • clubhead patch (seeded)  │
            └───────────────┬───────────────┘     └────────────┬───────────────┘
                            │ ball/clubhead tracks               │ keypoints
                            └──────────────┬─────────────────────┘
                                           ▼
                          ┌────────────────────────────────────┐
                          │  PHASE FUSION                        │
                          │  IMU swing-energy gate (waggle rej.) │
                          │  + IMU PhaseSegmenter (existing)     │
                          │  + vision event refine (No-event)    │
                          └────────────────┬─────────────────────┘
                                           ▼
                       ShotProcessor / SwingWindow → ShotAnalyzer
                       MetricExtractor / SwingScorer (existing)
```

**Reuse vs build:**
- **Reuse:** ONNX runtime + GPU EP fallback chain, `BallDetector` threading/ROI/throttle
  contract, multi-camera publish/subscribe, `PhaseSegmenter` + IMU fusion, `ShotProcessor`/
  `SwingWindow`/`ShotAnalyzer` pipeline.
- **Build:** YOLO `.onnx` ball model + Kalman patch tracker; shaft-keypoint head; phase-fusion
  layer with explicit waggle gate + per-event confidence.

---

## 6. Adoption roadmap (lowest-risk first)

1. **Ball v2 (highest value, lowest risk).** Train/obtain a nano-YOLO golf-ball `.onnx`; wrap in
   the Kalman patch tracker; host in the existing ONNX runtime; keep Hough as the stationary
   presence check. Validate launch-frame recall against the IMU impact timestamp.
2. **Phase fusion.** Add the **IMU swing-energy waggle gate** in front of `PhaseSegmenter`; add
   per-`PhaseEvent` confidence. This alone fixes most "waggle triggered a fake phase" issues with
   no new models.
3. **Vision phase refine (optional).** Fine-tune a SwingNet-style MobileNetV2+BiLSTM on
   GolfDB + our own captures for Address/Top refinement and a "No-event" second vote, **only on
   the face-on camera**.
4. **Club R&D.** Shaft keypoints anchored to the lead hand → clubhead patch detector seeded by
   the hand → multi-frame/heatmap only if needed. Validate against the club-mount IMU.
5. **Ball/club offline refinement.** Heavier accurate model on the frozen `SwingWindow` for full
   trajectory recovery in `ShotAnalyzer`.

---

## 7. Caveats (read before acting)

- **Club detection is unproven here.** No verified golf-specific source. Everything in §3 is a
  reasoned transfer, not evidence. Budget it as research.
- **Models are 2020-era categories.** YOLOv3/v4/tiny are superseded by 2026 — use modern
  nano/small YOLO or RT-DETR equivalents; keep the *principles*.
- **Cross-sport extrapolation risk.** TrackNet and YOLO-Ball numbers come from **tennis**; the
  motion-channel result from **beach volleyball**. Golf balls are smaller, faster off impact, and
  blur more — **reported precision/recall will not transfer 1:1**. Re-benchmark on golf data.
- **SwingNet 76.1% is a 2019 baseline.** Newer temporal/transformer models likely exceed it, but
  none survived verification in this pass — treat 76.1%/91.8% PCE as a floor, not a target.
- **Motion-channel recall gain is training-dependent** — done wrong it *drops* recall (§2.5.2).
- **The Hough detector is not wrong, just limited** — keep it for the teed/stationary case.

---

## 8. Open questions (flagged by the research, worth answering next)

1. **Camera-based clubhead/face-angle/lie-loft from RGB** — the biggest unfilled gap; likely
   needs club-shaft keypoint estimation + clubhead small-object transfer.
2. **Do modern detectors (YOLOv8/v11/v12, RT-DETR) beat the 2020 golf-ball benchmark on a
   golf-specific set, and at what fps on our Vulkan/CUDA targets?**
3. **How well does the 8-event taxonomy hold at high frame rates + multi-camera, and how much
   does IMU fusion raise it beyond vision-only 76.1%/91.8% PCE?**
4. **Minimum frame rate / resolution for reliable impact-frame ball & clubhead capture**, and how
   the patch+Kalman approach degrades as impact blur grows.

---

## 9. Refuted claims — do NOT cite

These were rejected in adversarial verification (need 2/3 to kill; shown with the vote):

- ✗ `1-2` — "TensorRT lets the YOLOv3 micro+motion model hit up to 40 FPS." *(unverifiable as stated.)*
- ✗ `1-2` — "For golf specifically, Faster R-CNN is most accurate **while** YOLOv3-tiny is competitively
  accurate and the preferred real-time choice." *(conflates two findings; tiny is the speed pick at a
  real accuracy cost, not "competitively accurate".)*
- ✗ `0-3` — "GMP substantially outperforms YOLOv8 (F1 0.912/0.963 vs 0.781/0.701)." *(Nature 2024;
  unanimously refuted — do not cite a GMP-over-YOLOv8 advantage.)*

---

## 10. Sources

**Primary — golf-ball detection**
- arXiv 2012.09393 — *Golf ball detection (patch-based CNN + Kalman tracking-by-detection)*, the
  authoritative golf-ball-specific benchmark (Table III). https://arxiv.org/abs/2012.09393v2 ·
  https://arxiv.org/pdf/2012.09393 · reference impl: github.com/rucv/golf_ball
- *Sensors* 21(9):3214, MDPI 2021 (PMC8124271) — *Enhancement of Speed/Accuracy Trade-Off for
  Sports Ball Detection* (motion channel, RGB no-IR feasibility). https://pubmed.ncbi.nlm.nih.gov/34066380/
  · https://pmc.ncbi.nlm.nih.gov/articles/PMC8124271/
- PeerJ Computer Science 2025 (PMC12453710) — ball-detection review (model-family heuristic).
  https://pmc.ncbi.nlm.nih.gov/articles/PMC12453710/

**Primary — small-fast-object detection / tracking**
- TrackNet, arXiv 1907.03698 — heatmap, multi-frame (3-frame) ball tracking. https://arxiv.org/pdf/1907.03698
- YOLO-Ball, *Proc. IMechE Part P* 2026, DOI 10.1177/17543371261423768 — P2 + bidirectional fusion,
  occlusion attention (*medium* confidence, tennis dataset). https://journals.sagepub.com/doi/10.1177/17543371261423768

**Primary — swing-phase / event detection**
- GolfDB + SwingNet, McNally et al., CVPR-W 2019, arXiv 1903.06528 — 8-event taxonomy,
  MobileNetV2+BiLSTM, No-event class, waggle rejection. https://arxiv.org/pdf/1903.06528 ·
  code: https://github.com/wmcnally/golfdb ·
  https://openaccess.thecvf.com/content_CVPRW_2019/papers/CVSports/McNally_GolfDB_A_Video_Database_for_Golf_Swing_Sequencing_CVPRW_2019_paper.pdf

**Supporting / context**
- Ultralytics — tracking golf balls with YOLO (practitioner blog).
  https://www.ultralytics.com/blog/tracking-golf-balls-using-ultralytics-yolo-models
- PiTrac — DIY camera launch monitor (practitioner). https://hackaday.io/project/195042-pitrac-the-diy-golf-launch-monitor
- *Refuted:* Nature 2024 s41598-024-80056-3 (GMP vs YOLOv8) — see §9.

---

### Research provenance
Produced by a deep-research pass: 5 search angles → 22 sources fetched → 110 claims extracted →
25 verified under 3-vote adversarial verification → **22 confirmed, 3 refuted** → synthesized.
Confidence/vote tags above reflect that process. The largest residual uncertainty is
**camera-based club detection**, which has **no direct verified source** and is the clearest
opportunity for PinPoint to lead rather than follow.
