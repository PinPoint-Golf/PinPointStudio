# The post-shot analysis as a dependency graph: ≤ 8 s on the studio

**Status:** design, 6 Oct 2026; BUILT the same day — results in docs/research/data/analysis/analysis_dag_20261006.md (studio 18.6 → 8.1 s on MP4 re-analysis; the live shot is unmeasured). Step F stays off. Mark: "reduce the overall time for post shot analysis to 8 s in the
studio … introduce parallelism where possible and temporal optimisation to reduce the problem space
… shaft track and metrics must not be degraded … under 6 s would be fantastic … an analysis DAG we
could visualise." The pose pass is already done (pose_inference_performance_plan.md).

## 0. Where the 19 s is (studio, 21 corpus swings, re-analysis from MP4, after the pose work)

`runStages()` runs 39 stages strictly in sequence on one worker (analysis_stage.h:237-258). The
expensive ones form ONE chain, and the chain is the total:

```
Pose(FO) 1.7 → PoseSmooth → Ball 2.9 → Shaft(FO) 4.7 → SegResolve/Refine → DtlPose 1.3 →
AddressMarks 0.85 → DtlShaft 2.5 → ShaftFusion → Skeleton3D 4.1 → KinematicSequence …  ≈ 18.6 s
```

Everything else — Impact, the seven body-metric stages, Kinematics, ShaftPlane, Bindings,
Assessment — is under a second in total and hangs off that chain. So parallelism of the leaves alone
buys ~1 s; reaching 8 s means shortening the chain's links and overlapping the ones that only
pretend to depend on each other.

| link | why it is on the chain | what takes it off or shrinks it |
|---|---|---|
| Ball 2.9 s | Shaft's `canRun` wants the ball; it replays EVERY face-on frame of the pose span (723) through a DoG + causal tracker, no early exit after launch, full-frame decode for an ROI | stop at launch + N, decode only the ROI rows, parallel DoG — and let Shaft start on the pose while Ball finishes if its witness use can wait for `applyBallAnchor` |
| Shaft(FO) 4.7 s | the ladder (seg) comes from it when there is no IMU | per-frame evidence is already `cv::parallel_for_`; `decideTrack` runs 1–3 ATTEMPTS, the DP/snap/post are serial, the scene median and the frame cache are built per attempt-set — profile first (§3) |
| DtlPose 1.3 s | reads the RESOLVED seg for its window and schedule (wrist_analyzer.cpp:1802-1860) | with an IMU ladder it can start right after Pose(FO); without, the two-pass face-on pose already holds a span estimate — start DTL pose on that and overlap it with Shaft(FO); the schedule then comes from the pose-span estimate (P1 ≈ onset, P8 ≈ finish0) — an output change, gated |
| AddressMarks 0.85 s | a leaf nothing reads | a new u2netp ORT session is built on EVERY shot (person_segmenter.cpp:41-101): cache it like the pose session; and run it off the chain |
| DtlShaft 2.5 s | needs the face-on witness and the DTL pose | three serial `medianImage(parallel=false)`, the serial post loop; internal parallelism |
| Skeleton3D 4.1 s | needs both shaft tracks | LM with ~100–130 iterations; `evaluate()` is a serial loop over frames whose per-frame residuals/Jacobians are independent (skeleton3d_fit.cpp:1080-1120); the banded Cholesky is cheap — parallelise `evaluate()` over frames with per-thread partials of the shared block |
| frame decode | Ball and Shaft each decode the whole face-on span; Impact, ImpactAnchor, AddressMarks decode again; DtlShaft decodes the DTL | one grey decode per camera per analysis, shared (525 MB at 688×1024 for 745 frames — the shaft tracker already holds exactly that), behind a memory cap; on a live shot this is the demosaic, repeated the same way |

## 1. Targets

Studio re-analysis from MP4 (the measurable thing): 18.6 s → ≤ 8 s; live shot lower still (no
MP4 decode). Stretch 6 s. The gate for every change that reorders work: **result.json byte-identical**
to the sequential run (`parity_diff.py`, once it strips skeleton3d's `diagnostics.ms`/`branchMs`).
For every change that alters an output (DtlPose started early, Ball bounded): the same corpus gate
as the pose work — shaft truth 0 worse, DTL truth 0 worse, no metric lost, phase samples within σ,
fused planes, skeleton3d reprojection — on the 21 corpus swings and the 5 Oct session.

## 2. The graph

Stages stay as they are (stateless functors over `AnalysisContext`). Each declares what it reads and
writes on the context and the detail, from the map in §0's source (the reconnaissance of 6 Oct:
every stage's R/W is listed in the implementation's stage table). A small executor builds the DAG
from those declarations, runs ready stages on a bounded pool, and keeps three things exactly as the
sequential run had them:

- **`detail->series` order.** Fourteen stages append to it and the order is the output. Each stage
  appends to its own buffer; the executor splices the buffers in authored order.
- **One reader per camera.** `SwingWindow::payloadOf` is a pass-through to a single sequential
  reader per camera (the MP4 decoder rewinds on a back-seek; the raw reader has one buffer). Frame
  fetch is serialised per camera behind the executor; decode is what runs in parallel — and the
  shared grey cache (§0 last row) means each frame is fetched once.
- **`ctx.seg` is a barrier**: written by SegResolve, mutated by EventRefine / PositionsLadder /
  TimelineFusion, read by everything after. The executor treats the mutators as one serial group.

`job.progress` keeps an unlocked `lastPct` — it becomes atomic. `thread_local` pose timing is read
on the stage's own thread. OpenCV's global `parallel_for_` pool is shared by concurrent stages;
`cv::setNumThreads` is set once to the physical core count and the pool size bounds oversubscription.

The sequential path stays as the OFF position (`analysis.parallel` false) and is what parity is
measured against.

## 3. The order of work, and who does it

| step | what | gate | agent |
|---|---|---|---|
| A | **Instrument + DAG export + visualiser.** Persist every stage's ms, thread and start/end offset in `analysis.timings.stages`; export the declared graph + the run's timeline as JSON; `tools/analysis_dag.py` draws it (graph with critical path, and a Gantt of a run) to SVG/HTML; fix `parity_diff.py` to strip skeleton3d's ms fields | parity unchanged | 1 |
| B | **The executor** (§2) with every stage declared; leaves off the chain; series splice; per-camera fetch lock; progress atomic | byte-identical result.json, sequential vs parallel, 21 swings + 5 Oct | 1 (same agent as A: same files) |
| C | **Cheap chain cuts:** segmenter session cache; Ball bounded to launch + N frames with ROI-only decode and a parallel DoG — report every reader of post-launch `ball.frames` first | parity for the cache; corpus gate for the Ball bound | 2 |
| D | **Skeleton3D: parallel `evaluate()`** over frames (per-thread partials of `C`/`gs`), `frameResiduals` audited for purity; nothing else in the solver changes | byte-identical fit (same floating-point order within a frame; the shared reduction summed in a fixed order) — else the corpus gate | 3 |
| E | **Shared grey frame cache** per camera (decode once; Ball, Shaft, ImpactAnchor, AddressMarks read it; DTL likewise), with the 1200 MiB cap the shaft tracker already has | parity | after B |
| F | **DtlPose off the critical path** (IMU ladder, else the pose-span estimate) overlapping Shaft(FO) | corpus gate (output changes) | after B |
| G | **Shaft(FO) and DtlShaft internals** from A's profile: attempts, serial medians, post loops | parity where reordering; gate where not | after A |

Fable holds the design, reads every timeline from A's visualiser, decides the order, runs the studio
gates and reviews each diff. Opus agents implement one step each on disjoint files. Mark judges the
gates and the first cabin shot.

## 4. Record

`docs/research/data/analysis/analysis_dag_20261006.md`: the stage timeline before and after each
step on the same swings, the parity results, and the graph as drawn.
