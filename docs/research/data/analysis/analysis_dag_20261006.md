# The post-shot analysis as a dependency graph — what was built on 6 Oct 2026 and what it measured

**Status:** results, 6 Oct 2026, for `docs/design/analysis_dag_design.md`. Every number is the studio
(RTX 5080 / Core Ultra 9 285K), re-analysis from MP4, median over the 21 two-camera corpus swings,
one exe per step carrying the whole tree. Run roots under `/mnt/swingdata/scratch/dag-step*/`
(delete once accepted). Nothing committed at the time of writing.

## 0. Summary

| step | what | wall median | gate |
|---|---|---|---|
| before the pose work (5 Oct) | 39 stages in sequence | 21.9 s | — |
| pose work (134eac67) | batched inference, DTL schedule B … | 18.6 s | pose record |
| A + B | per-stage timings persisted, the graph declared per stage, a DAG executor (`analysis.parallel`), the visualiser | 15.1 s seq → 14.7 s par | result.json byte-identical 21/21 |
| D | Skeleton3D `evaluate()` on a thread team | fit 4.1 → 2.0 s | bit-identical (synthetic, exact ==; corpus 0 diffs) |
| C | ball bounded at max(launch, impact) + 150 ms, ROI-only decode; segmenter session cached | Ball 2.5 → 1.6 s | 0 truth worse, no metric lost, ladder/positions identical 21/21 |
| G | shaft trackers: snap, head pass, forward–backward, DTL medians and post loop parallel | face-on 3.9 → 2.9 s, DTL 2.7 → 1.55 s; wall 15.1 → 12.6 s | byte-identical 20/21 (the 21st is the CUDA DTL-pose flip, shown by an OFF repeat) |
| F | DTL pose off the chain (early window) | DtlPose overlaps Shaft 21/21, wall unchanged | **FAILS**: 1–2 truth frames worse, back planes move up to 11°, 3–6 swings under σ — the DTL window grows and the person crop moves; OFF |
| E | one decode per frame per camera (`FrameStore`), shared by every stage; AddressMarks authored after DtlShaft | **11.5 → 8.4 s** (8.1 s with the 4 GB cap now default) | byte-identical 21/21 against an OFF repeat |

Where the 8.1 s goes now (studio, MP4): FrameDecode face-on 1.9 s ∥ DTL 1.9 s (the MP4 fetch, serial
per camera by the payload contract — the floor of a re-analysis) → Pose 1.1 → Ball 0.65 → Shaft 1.4 →
ImpactAnchor 0.14 → seg → DtlPose 0.55 → DtlShaft 0.34 → fusion → Skeleton3D 2.0 → sequence. A live
shot has no MP4 fetch: its FrameDecode is one demosaic per frame (≈ 0.3 ms each, parallel) — **not
measured; the first cabin shot's `analysis.timings.stages` is the number.**

## 1. The graph

39 stages declare reads/writes over 28 named resources (analysis_stage.h `res::`); edges are
read-after-write, write-after-read, write-after-write, the series appends, and the serial group
{SegResolve, EventRefine, PositionsLadder, TimelineFusion}. `analysis_dag.h` runs ready stages on a
pool of min(8, physical cores), longest-remaining-chain first; `detail->series` is spliced in authored
order from per-stage buffers; two stages holding the same camera never run together (the payload
contract); `job.progress` is atomic. `swinglab_run --dag <file>` exports graph + timeline;
`tools/analysis/analysis_dag.py` draws them (graphviz if present, else its own layout) — see
`build/run-me/dag/*.svg|html`. The critical path after B is the whole wall: the leaves (Impact, the
seven body stages, Kinematics, ShaftPlane, Bindings, AddressMarks) were never the time.

## 2. The chain, link by link

- **Decode (E).** The face-on clip was fetched+decoded by the pose (twice), Ball, Shaft, ImpactAnchor
  and AddressMarks; the DTL by the DTL pose and DTL shaft. The store fetches each camera's whole
  window once (serial, 32-frame chunks, parallel decode), keeps grey for every frame (face-on
  400–934 MiB, DTL 335–419 MiB on the corpus) and BGR for every frame when the cap (4096 MiB per
  camera) and the RAM floor (24 GiB) allow — the pose picks its frames after its own coarse pass, so
  a BGR subset would send it back to the window. The shaft trackers take the store's grey Mats by
  reference. On the Mac (16 GB) BGR is never kept; grey is. Below the cap, consumers use today's path.
- **Ball (C).** 723 frames → ~530 when a launch is seen (bound at max(launch, impact) + 150 ms —
  launch fires on flicker ~1 s before impact, so a launch-only bound would have cut the address
  hold); ROI-only decode with a 2-row Bayer margin, byte-equal. Every post-launch reader was
  enumerated: none reads past the cut; the replay overlay holds the last sample.
- **Shaft trackers (G).** Profiled first: face-on = fetch 1.46 s (now the store's), head pass 1.0 s,
  snap 0.4, segProbe 0.32, evidence 0.28, forward–backward 0.25; DTL = fetch 1.2 s, post loop 1.06,
  three serial medians 0.27. Parallelised the snap search, the head pass measurement (the running
  average stays serial in frame order), the two recurrences side by side, the DTL medians by row and
  the DTL post loop per frame. No `decideTrack` ever ran twice on the corpus, so attempt caching
  was not needed. Untaken temporal cut: still-frame probing at stride 4 (~0.2 s, output change).
- **Skeleton3D (D).** `frameResiduals` audited pure; frames handed out two at a time to a team of
  min(8, physical cores) (fixed ranges swung 25 % on the M4's mixed cores); each frame writes its own
  blocks and its own share of the shared block; the reduction runs in frame order — bit-identical.
  45 % of the fit is still serial (solve, globalTerms, projectToCoefficients).
- **AddressMarks.** The u2netp session build was 40–50 ms, not the 0.85 s assumed; the cost is the
  inference (0.55 s on 2 threads) and the address-frame fetch (0.28 s, an MP4 back-seek). It is a
  leaf and now runs after DtlShaft, beside Skeleton3D.
- **DtlPose (F, not taken).** Its window and schedule read the resolved ladder. Fed from the coarse
  pose span instead (capped at impact − 1.5 s / + 1 s), the window is still ~1 s wider than the
  resolved one (frames +55 %), the DTL person crop moves on 19/21 swings and the fit follows. The
  overlap is real (DtlPose starts with Ball on 21/21) but worth ~0.6 s at most; the clean way is a
  window from impact rather than onset — a design call, parked.

## 3. Memory and keys

`analysis.parallel` (ON), `analysis.parallelThreads` (0 = min(8, cores)), `decode.frameStore` (ON),
`decode.frameStoreMaxMiB` (4096), `decode.frameStoreBgrMinRamGiB` (24), `ball.boundAfterLaunch` (150),
`ball.roiDecode` (ON), `segmenter.sessionCache` (ON), `skeleton3d.evalThreads` (0 = auto),
`shaft.parallel.{snap,head,fb}`, `shaft.dtl.parallel.{medians,post}` (ON), `pose.dtlEarly` (OFF).
Every key's OFF is the pre-change path. `kBallStageVersion` is bumped with the ball bound.
Studio memory with everything on: ~3.4 GB of frames per analysis (face-on 2.1 GB BGR+grey, DTL
1.3 GB) on a 31 GB machine; the Mac keeps grey only (~0.9 GB).

## 4. Not verified

- The live cabin shot (raw Bayer ring): timing, memory, the progress callbacks from pool threads.
- The cameraKinematics profile and IMU-bound sessions under the executor.
- The two knife-edge corpus swings (06-11 s1, 07-04 s5) still move under the pose work's batching
  (pose record §3); nothing here moved them further.
- Repeats on the studio show the CUDA DTL pose flipping one frame in the 9th decimal on 06-11 s7
  / s4 / s2 between runs — the only non-parity seen, and it is upstream of every stage here.
