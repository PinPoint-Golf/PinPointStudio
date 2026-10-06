# Pose inference, two cameras — measurements and gates for the performance plan

**Status:** running record, 6 Oct 2026, for `docs/design/pose_inference_performance_plan.md`.
Run roots on the share: `/mnt/swingdata/scratch/pose-time/` (the split), `/mnt/swingdata/scratch/pose-step4/`
(the schedule experiment). Delete once this record is accepted.

## 1. The split (step 0), swing_0013 of 2026-10-05, MP4 re-analysis, ViTPose-B fp32

| host / provider | camera | frames | session build | decode | preprocess | `Run()` | per frame | heatmap decode | total |
|---|---|---|---|---|---|---|---|---|---|
| studio RTX 5080, CUDA EP | face-on | 209 | 333–411 ms | 974–1105 | 59–69 | 1234–1377 | **5.9–6.6 ms** | 363–368 | 2.3–2.6 s |
| studio RTX 5080, CUDA EP | DTL | 441 | 272–275 (0 cached) | 1124–1204 | 124–128 | 2304–2372 | **5.2–5.4 ms** | 1147–1149 | 3.7–4.1 s |
| Mac M4, CoreML EP (NeuralNetwork format) | face-on | 209 | 263–302 | 194–224 | 87–95 | 9012–9264 | **43–44 ms** | 285–294 | 9.7–9.9 s |
| Mac M4, CoreML EP (NeuralNetwork format) | DTL | 441 | 125–128 (0 cached) | 221–314 | 97–105 | 18858–28096 | **43–64 ms** | 623–634 | 19.7–29.0 s |

Studio: two-camera pose 6.0–6.7 s of an 18.7–19.5 s analysis. `Run()` is 55–60 % of it, MP4 decode
25–30 % (the two-pass face-on path decodes the clip twice), the single-threaded DARK decode of all
133 heatmaps 14–28 %. Steps 1–2 (session cache, producer pool) change 6.0 → 6.0 s here: the cache
only saves the DTL's session build inside one swinglab process (275 → 0 ms), and the pool has
nothing to parallelise on the MP4 path because the H.264 decode is the serial part.
Mac: `Run()` is 93–97 % — the graph is not running on the ANE/GPU (step 3 is where the Mac goes).
Run-to-run the Mac varies 2× with other load; the studio varies < 5 %.

## 2. The frame budget (step 4), without any code change

The pinned corpus poses (`pose3`, `pose2_dtl`) thinned by phase zone with `tools/pose/thin_pose.py`
and injected through `--pose` / `--dtl-pose` on the studio; 21 two-camera corpus swings; graded by
`tools/pose/schedule_grade.py` (metrics, phases, skeleton3d) and `dtl_continuous_grade.py --truth`.

| arm | schedule | DTL frames | DTL track vs truth | metrics | skeleton3d | verdict |
|---|---|---|---|---|---|---|
| dtlA | DTL stride 1 in [P3 − 50, P8 + 50 ms], 2 in [P1 − 100, P3 − 50), 4 elsewhere | 465 → 221 (−52 %) | 425 pairs, 0 worse; bands unchanged | 4 swings lose the 5 trunk-rotation metrics (the DTL pair route) | the same 4 swings lose the DTL altogether (`dtlUsed` false); on the other 17: reproj FO −0.8 px, DTL +0.1 px, grip unchanged, planes ≤ 1.4° | **viable once the fit's DTL bracket (12 ms / 6 ms, wrist_analyzer.cpp ~2478-2514) follows the schedule** — with stride 4 at address no DTL frame pairs with the fit's reference instants, so `obsMid(hips)` finds nothing and the fit drops the camera |
| dtlA2 | as dtlA but stride 2 in the dense zone except [P6 − 30, P7 + 60] | 465 → 129 | 2 frames worse; impact bands reshuffled | 8 swings under the σ gate | fused plane moves to 9.7° | **too far** |
| foB | face-on stride 1 only in [P4 − 50, P8 + 50], 2 elsewhere | 217 → 146 | face-on θ changes on 21/21 | phases shift up to 951 ms; within-σ 56–76 % | — | **the face-on schedule is load-bearing as it stands** (its dense zone is the phase model's) |
| foB2 | face-on stride 1 only in [P6 − 30, P7 + 60] | 217 → 109 | as foB | as foB | — | no |

So the budget is the DTL's: schedule A halves it; the face-on's keeps today's two tiers.

## 3. Step 3 (batch, fp16, IO binding, body+hand heatmaps, MLProgram) — one swing, then the gate

One swing (swing_0013), studio: `Run()` 5.9 → 2.6 ms/frame face-on and 5.2 → 1.4 DTL with fp16 +
batch 16 + IO binding; heatmap decode 367 → 57 ms and 1135 → 172 ms with body+hand channels on
4 threads; two-camera pose 5.98 → 2.97 s. The MP4 decode (1.1–1.2 s per camera) is the floor left.
Mac: CoreML took 4 of 658 nodes in the legacy format; MLProgram with a STATIC batch takes 702 of
739 (the 13 partitions are the GELU `Erf` nodes), `Run()` 43 → 10 ms/frame, but the compiled model
costs 20 s per process from the cache (97 s cold) — the app pays it once per launch, the tool every
swing. fp16 alone on CUDA is SLOWER (9.8 ms/frame: float32 islands and casts); it pays only batched.
IO binding on CUDA was wrong at first (bound once, refilled: 80–207 px errors); fixed, worth 3–6 %.

Keypoint gate on that swing, every arm within median 0.03 px / p95 0.1 px — but fp16 leaves single
joints 15 px (CUDA) to 73 px (CoreML) off on single frames.

**The corpus gate (21 swings, fresh pose, studio):**

| | two-camera pose (median) | analysis total | metrics | phase samples within σ | phases | skeleton3d | DTL vs truth | fused planes |
|---|---|---|---|---|---|---|---|---|
| base vs base again (the noise floor) | 6.31 s | 21.9 s | none lost | 100 % on every swing | 0 ms | reproj ±0.00 px | 0 worse | ≤ 0.16° |
| base vs all (fp16 + batch 16 + IO + bodyHands) | 3.34 s (1.9×) | 19.0 s | 1 lost on 2 swings | median 98 %, **3 swings under 95 % (74, 82, 93 %)** | **958 ms on 06-11 s1** | ±0.01 px | **1 worse** | **5.8° on 07-04 s5** |

The noise floor is zero (CUDA single-frame inference is deterministic run to run), so the movers are
the option set's. Decomposed:

| arm | pose | under-σ swings | 06-11 s1 phases | 07-04 s5 back plane | truth worse |
|---|---|---|---|---|---|
| batch 16 + IO + bodyHands + 4 threads, fp32 | 3.61 s (1.75×) | 2 | same shift | 5.35° | 1 |
| fp16 + batch 16 alone | 4.11 s | 3 | same shift | 5.79° | 1 |
| all | 3.34 s (1.9×) | 3 | same shift | 5.79° | 1 |

So it is not fp16: batching alone (cuBLAS batched GEMM rounding, ≤ 3.3 px on single joints) is
enough to tip two swings whose phase model is bistable. On 06-11 s1 the base reads the finish at
impact + 1253 ms and the batched run at + 295 ms (every other corpus swing finishes 230–300 ms
after impact); P4 moves 335 → 254 ms before impact. On 07-04 s5 the back plane goes 53.5 → 59.3°
where the other 14 July swings read 60–63°. Neither is a regression the data can show; both are
the phase model and the fused plane being sensitive to sub-pixel pose changes on a knife-edge
swing. The gate as written ("unchanged beyond noise") cannot be met by ANY numeric change on such
swings; the honest gate is "no worse against truth" plus a look at each mover, which is what the
table gives. **Recommendation for the studio: batch 16 + IO binding + body-and-hand heatmaps on
4 threads, fp32** — fp16 buys 7 % more for 15–51 px single-joint outliers. The Mac needs fp16
(MLProgram, static batch) to get off the CPU at all; its 73 px DTL outlier is still to be looked at.
On the 5 Oct session (31 swings), all: pose 5.87 → 2.90 s, analysis 18.6 → 15.0 s, one swing +1 metric.

The two single-joint outliers under fp16 are one frame each: the DTL left big toe (72.7 px, feet seen
from behind) and the face-on right shoulder (9.7 px) — a heatmap argmax flipping between two
near-equal peaks, which the offline smoother already absorbs. The hands move up to 277 px on single
frames under every numeric change and are not trusted anyway.

## 4. Step 4 — the DTL schedule, built and gated

`pose.dtlSchedule` (zones off the inherited ladder) and `pose.dtlLocalGap` (skeleton3d's bracket
and segment_rates' two gap rules read the local DTL spacing instead of 12 ms / 3 × median). Both
OFF by default; OFF is bit-identical. With the first cut (schedule A: dense from P3 − 50 ms to
P8 + 50 ms, stride 2 in the backswing, rest 4) no swing drops the DTL and no metric is lost any
more, DTL pose frames 8972 → 4429 (−51 %) and the DTL pass 78.6 → 44.0 s over the 21 swings on
the studio (fp32, one frame per `Run()`); but one truth frame is worse (07-04 s10, in the stride-4
finish) and the back plane moves up to 7.8° (07-04 s5), 4.0° and 3.8° — the stride-2 stretch
between P2 and P3 is where swingPlane at P3 is read. Two gentler cuts, graded with the new
brackets on (pinned poses thinned, 21 swings):

| schedule | DTL frames kept | truth worse | fused plane max | within σ, worst swing | metrics lost |
|---|---|---|---|---|---|
| B: 1 in [P2 − 50, P8 + 150 ms], 2 in [P1 − 100, P2 − 50), rest 4 | 54 % | **0** | **0.06°** | 97 % | none |
| C: 1 in [P2 − 50, P8 + 150 ms], rest 3 | 56 % | 2 | 0.05° | 96 % | none |

**Schedule B is the default behind the switch** (pose_schedule.h). Stride 2 from P1 − 100 ms to
P2 − 50 ms is the address-to-takeaway stretch the trunk pair route reads at ~26 ms spacing without
complaint; stride 3 across the finish (C) is what makes two truth frames worse.

## 5. Step 5 — decode

On the studio the MP4 re-analysis is decoded by Media Foundation on D3D11, not FFmpeg: OpenCV's
backend list falls through because the FFmpeg plugin DLL is not beside the exe (CMake bundles only
`opencv_world`; its comment that videoio is unused is stale). MSMF costs 1.0–1.3 ms a frame against
0.4 for FFmpeg software on the same machine; D3D11VA hardware decode through FFmpeg is 1.3 ms
(the copy back and the BGR conversion cost more than the decode saves); decoder threads beyond
OpenCV's default gain nothing; seeking to the keyframe instead of rewinding saves 20–35 % of the
frames decoded but little time. **No decoder change is on by default**: every decoder gives
different pixels (0.2–2 grey levels on average, up to 47 at edges), and the shaft tracker reacts —
on swing_0013 FFmpeg software moved face-on shaft coverage 0.974 → 0.807 and the DTL schedule
441 → 492 frames. Switching Windows to FFmpeg is a behaviour change that needs its own corpus gate;
`decode.backend`, `decode.threads`, `decode.hwAccel`, `decode.seek` exist for it. The live capture
path never decodes an MP4 (it demosaics ring frames), so none of this touches a cabin shot.

## 6. Where it lands

Studio, re-analysis from MP4, 21 corpus swings, two-camera pose median: 6.31 s → 3.61 s with
batch 16 + IO binding + body-and-hand heatmaps on 4 threads (fp32), and the DTL pass a further
−44 % under schedule B: ≈ 3.0 s (2.1×). The rest is the MP4 decode (2.1 s of it, MSMF), which a
cabin shot does not have: there the pose pass is session (cached, 0) + demosaic (now on 4
threads) + inference + heatmaps, and the same arithmetic gives ≈ 1.3–1.5 s against ≈ 5 s today —
**the live number has not been measured; it needs a shot in the cabin with the new log lines.**
Mac: `Run()` 43 → 10 ms/frame with MLProgram + fp16 + static batch 8 (3.9× on the pose pass), paid
for by a 20 s compiled-model load per process — once per app launch if the app warms the session
at start, every swing in the tool.

**The Mac's own gate (21 corpus swings, fresh pose, base = CoreML NeuralNetwork as today; all =
MLProgram + fp16 + static batch 8 + body-and-hand heatmaps on 4 threads + IO binding):** truth 0
worse, no metric lost, fused planes ≤ 2.7°, phase samples within σ median 98 % with 4 swings under
95 % (worst 84 %) — among them 06-11 s1 with the same bistable finish as on the studio. Pose pass
29.6 → 8.7 s per swing; per process 29.9 → 28.7 s because the compiled model costs 20.0 s to load
each time. In the app that load happens once (step 1's cache), so a cabin-less Mac analysis would
see ≈ 3.4× on pose.

## 7. Defaults proposed (all still OFF in the tree — Mark judges first)

| key | studio (CUDA) | Mac (CoreML) | note |
|---|---|---|---|
| pose.sessionCache / producerThreads / queueDepth | on (already) | on | bit-identical |
| pose.batchSize / ioBinding | 16 / on | 8 / on | batching alone moves the two knife-edge swings |
| pose.staticBatch | off | on | CoreML needs a fixed batch to take the graph |
| pose.modelPrecision | fp32 | fp16 | fp16 buys 7 % on CUDA for 15–51 px single-joint flips; on CoreML it is the difference between the CPU and the GPU |
| pose.coreml | — | mlprogram | 702 of 739 nodes on CoreML |
| pose.decodeChannels / decodeThreads | bodyHands / 4 | bodyHands / 4 | face channels never read |
| pose.dtlSchedule / dtlLocalGap | on / on | on / on | schedule B; bump kDtlPoseStageVersion when flipped |
| pose.tensorrt | off | — | untested: no TensorRT on the studio |
| decode.* | off | off | needs its own gate (pixels differ per decoder) |

Owed with the flip: warm the CoreML session at app launch (20 s, in the background) so the first
shot does not pay it; the platform-conditional defaults in pp_tuned_constants.h; and the first
cabin shot's `[PoseRunner]` lines, which are the only measurement of the live path.
