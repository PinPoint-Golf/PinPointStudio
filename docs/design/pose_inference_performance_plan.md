# Pose inference, two cameras: 2–3× faster without losing the skeleton, the shaft or the metrics

**Status:** plan, 6 Oct 2026. Mark: "the pose inference when we have dtl and faceon cameras takes
twice as long … get 2–3× better performance on pose inference without losing accuracy for the 3-D
skeleton, shaft tracker and metrics." Nothing off the table.

## 0. Where the time goes today (read from the code and the 5 Oct documents)

The offline pose pass is ViTPose-B WholeBody (133 keypoints, 256×192 input, fp32, 360 MB) through
ONNX Runtime 1.26, CUDA EP on the studio (RTX 5080), CoreML on the Mac, one frame per `Run()`, the
input tensor built on the CPU and the 133 heatmaps decoded (DARK) on the CPU. Measured on the 5 Oct
session: face-on `poseMs` 1478–1767 ms for 181–215 frames = **8.0 ms per frame end to end**. The DTL
pass is not persisted, but it poses 427–477 frames at stride 1 over [start − 1 s, end + 0.3 s], so
the two-camera pose cost is roughly 1.7 s + 3.5 s ≈ 5 s of a 13 s analysis.

Five things are wrong with that, and they multiply:

| # | Fact (file:line) | Cost |
|---|---|---|
| F1 | The ORT session (model load + EP init) is rebuilt on every `PoseRunner::run()` — twice per shot (pose_runner.cpp:136-157) | a 360 MB model load and a CUDA warm-up, twice |
| F2 | One frame per `Run()`, fp32, no IO binding, host↔device copy both ways per frame (vitpose.cpp:476-492) | the GPU idles between frames; a 5080 should do ViTPose-B at well under 2 ms a frame batched |
| F3 | Demosaic (BayerRG8, edge-aware), resize and normalise on ONE producer thread, queue depth 3 (pose_runner.cpp:197-385, frame_decode.h:30-65) | likely the real bottleneck on CUDA: ~5–8 ms of CPU per 688×1024 frame feeds an inference that could take 1 ms |
| F4 | DTL poses every frame over a window 1.3 s wider than the swing (wrist_analyzer.cpp:1776-1863); face-on poses stride 4 outside [impact − 500, + 250 ms] | 2.2× the face-on frame count for the same swing |
| F5 | Face-on and DTL run strictly in sequence; the MP4 reader rewinds to frame 0 on every back-seek (swing_reanalyzer.cpp:174-181) | no overlap of CPU decode with GPU inference across cameras; re-analysis decodes the clip up to three times |

Nothing is measured below `poseMs`. The first deliverable is therefore a split.

## 1. Targets and the accuracy gate

**Target:** two-camera pose ≤ ⅓ of today on the studio (≈ 5 s → ≤ 1.7 s) and ≤ ½ on the Mac,
measured by the new timing split on the same swings, pose model unchanged unless step 4 earns it.

**The gate — every step, before it is switched on:**

| what | against | tolerance | tool |
|---|---|---|---|
| 2-D keypoints | the same swing posed by today's path (fp32, stride as today) | body joints: median error ≤ 1 px, p95 ≤ 3 px at 688×1024; hands excluded (they are a cross-check only) | new `tools/pose/pose_diff.py` |
| face-on shaft | 996 hand-labelled θ on 58 corpus swings | p50 / p90 unchanged to 0.5° (the grid); frames worse ≤ 1 % | `tools/shaftlab/score_truth.py` |
| DTL track | 425 held-out band pairs | 0 frames worse by > 0.5° | `tools/shaftlab/dtl_continuous_grade.py --truth` |
| skeleton3d | the baseline fit | reprojection median unchanged to 0.2 px, grip offset, plane inclinations ±0.5°, no swing valid → invalid | `tools/skeleton3d_grade.py` |
| metrics | the baseline run | count unchanged on every swing; each metric's value within its own σ on ≥ 95 % of swings | `tools/metrics/compare_runs.py` |

Corpus for the gate: the 21 two-camera corpus swings (pinned poses are NOT used — the point is to
re-pose), plus the 5 Oct session (28 tracked swings), on the studio, Release, each step as a
`--params` switch with a bit-identical OFF.

## 2. The steps, in the order the gains compound

| step | change | expected | risk to accuracy |
|---|---|---|---|
| 0 | **Measure.** Per-run accumulators in `PoseRunner` / `ViTPose`: session build ms, decode+demosaic ms, preprocess ms, `Run()` ms, heatmap decode ms, frames, per camera; written to `analysis.timings` (incl. `poseDtlMs`, already measured and dropped) and to swinglab's runmeta; a `[PoseRunner]` log line with the split. GPU utilisation sampled on the studio during a run. | the plan's numbers | none |
| 1 | **One session per process per model.** Cache the ORT session (keyed by model file + provider + threads) across `run()` calls and across cameras; warm it once. | removes F1: 2 × (load + warm-up) per shot | none (same graph) |
| 2 | **Pipeline the CPU.** N producer threads (demosaic/resize/normalise) feeding one inference thread; queue depth ≥ 2 × batch. Decode both cameras concurrently with inference. | removes F3 as the bottleneck | none |
| 3 | **Batch + fp16 + IO binding.** Batch of 8–16 frames per `Run()` (the ONNX batch dim appears dynamic — confirm), fp16 model converted once at configure time (or at first use, cached beside the model), input tensor bound on device, heatmaps read back once per batch; DARK decode on the CPU in parallel across the batch. On the studio add the **TensorRT EP** with an engine cache (ships in the ORT package already, never appended) with CUDA EP fallback; on the Mac CoreML `MLProgram` + fp16 and measure what actually runs on the ANE/GPU. | 3–5× on inference proper | fp16 ⇒ keypoint gate; TRT ⇒ keypoint gate (same numerics class) |
| 4 | **Frame budget.** (a) DTL gets the face-on's two-tier schedule: stride 1 in [P3 − 50 ms, P8 + 50 ms] (the fused-plane, impact and band-edge windows), stride 2 in the backswing, stride 4–6 in the address hold and after P8 to finish0; the DTL tracker already interpolates anchors every camera frame; skeleton3d's 12 ms bracket becomes 14 ms so stride 2 at 155 fps still pairs. (b) Face-on: stride 2 instead of 1 in [impact − 500, −250 ms] if the kinematic-series bracket rule (≤ 3× median dt) is made tier-aware rather than median-based. (c) Nothing is posed after finish0 + 150 ms on either camera (already so). | DTL 477 → ~180 frames; face-on 215 → ~160 | the real one: gated per zone, DTL bands and skeleton3d DTL pairing are the known breakers |
| 5 | **Decode.** Keep the decoded frames of pass 1 for pass 2 (no rewind), or seek-free two-pass by posing the coarse frames from the same forward decode; hardware decode of the MP4 for re-analysis (NVDEC / VideoToolbox through OpenCV's `CAP_PROP_HW_ACCELERATION`) only if step 0 shows decode still matters after 2. | re-analysis only | none |
| 6 | **Model**, only if 1–5 fall short: a body-only ViTPose-B head (133 → 17 + feet; the hands are not trusted), ViTPose-S, or int8 — each through the full gate. | up to 2× more | real; last resort |

Steps 1–3 are pure engineering with no accuracy question beyond fp16; they are expected to deliver
the 2–3× on their own on the studio. Step 4 is where the accuracy work is, and it is what makes the
Mac (no TensorRT) reach the target.

## 3. Who does what

- **Fable** holds this plan, the gate and its tolerances, reads every timing split and every
  grade, decides what is switched on, reviews each diff, and runs the studio sweeps.
- **Opus agents, one per step, on disjoint files:** step 0 (instrumentation + `pose_diff.py`),
  step 1+2 (session cache, producer pool), step 3 (batching, fp16 conversion, TRT/CoreML EP
  options, IO binding), step 4 (DTL schedule + consumers' bracket rules), step 5. Each agent
  builds at most three times, runs its unit tests through ctest, and hands back the numbers.
- **Mark** judges the gate results before any default is switched on, and runs the cabin.

## 4. Record

Results go to `docs/research/data/pose/pose_performance_20261006.md` with the per-step splits on the
same swings, the gate tables, and the run roots under `/mnt/swingdata/scratch/`.
