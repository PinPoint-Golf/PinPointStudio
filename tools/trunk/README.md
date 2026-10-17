# trunk — the trunk-IMU hardware measurement session

`trunk_hw.py` analyses Mark's ~30-minute hardware session: design
`docs/design/trunk_imu_sequence_grf_design.md` §16 step 2, *"Hardware measurements before the
estimator … Each decides whether a planned piece is needed at all."* There is one subcommand per
measurement. Each writes a CSV and a `<cmd>_summary.txt` (also printed) ending in `VERDICT` lines.

```
PY="$HOME/Projects/.venv-hackmotion/bin/python -I"
$PY tools/trunk/trunk_hw.py selftest
$PY tools/trunk/trunk_hw.py rate  [--out DIR] <recorder files | record dir | swing dirs ...>
$PY tools/trunk/trunk_hw.py still [--out DIR] <recorder files | swing dirs ...>
$PY tools/trunk/trunk_hw.py hop   [--out DIR] <swing dirs | session dir ...>
$PY tools/trunk/trunk_hw.py tilt  [--out DIR] <refusion.json ...>
$PY tools/trunk/trunk_hw.py hips  [--out DIR] <bound result dir> <unbound result dir>
```

Without `--out`, output goes to `/mnt/swingdata/scratch/trunk-hw/<cmd>-<time>/`. The tool only
reads its inputs. It is stdlib only: the HackMotion venv has no numpy or scipy, and nothing here
needs them. Swing documents are read only through `tools/pp_swingdoc.py`.

## Inputs

- **Recorder files.** The app writes one per device when `PINPOINT_IMU_RECORD_DIR` is set:
  `# pinpoint.imu_record/1`, then a `# serial=… name=… role=… configuredRateHz=… startedUtc=…`
  line, then rows of `t_us,ax..gz,qw..qz`. `t_us` is the host arrival time; accel is in g, gyro
  in °/s, both in the sensor frame. Pass the files, or the directory that holds them.
- **Swing documents** (`swing.ppsw` or `swing.json`). Their IMU lanes are the `streams[kind=imu]`
  entries. The role comes from `device.role`/`roleName`; failing that, from the
  `analysis.bindings` row with the same serial. HackMotion lanes carry role 0 in `device` and
  the real role in `bindings`. The face-on pose is `analysis.pose2d.frames`.
- **`refusion.json`** from `swinglab_run --refuse` (`pinpoint.refusion/1`), with the per-lane
  `sources[].tilt` block when present.
- **swinglab result trees** (`<run>/<swing>/result.json`) or written-back swing dirs. Where a dir
  holds both `result.json` and `swing.json`, the newer file is read. The old rename onto an
  existing file failed silently (memory note `swinglab-run-output-files`).

**Time base (checked in the source).** IMU `samples.t_us` and pose `frames[].t_us` share one
clock. Both are EventBuffer host-monotonic stamps, and both are made window-relative by
subtracting the same `t0 = window.startTimestampUs()`:
- `swing_exporter.cpp:141` sets t0; `:305` applies it to video frames and `:605` to IMU samples.
- `swing_doc.cpp:1182` (`poseTrackToJson`) uses `rel()` with the same `windowT0`.

The two stamps mark different events, though. A pose frame's stamp is its video frame's
**exposure end**; an IMU stamp is the sample's **arrival** at the host. The hop offset is
therefore the physical lag between those two kinds of stamp.

## Subcommands and the decision each one feeds

| Command | Measures | Decides (design) |
|---|---|---|
| `rate` | Per device: samples, duration, mean rate against configured, the inter-arrival histogram (0–20 ms in 0.5 ms bins, `rate_hist.csv`), p50/p95/p99/max gap, gaps > 2 periods, loss events and lost samples, bursts (arrivals < 1 ms apart, `--burst-ms`, and also < T/2). | §4.2: whether 200 Hz holds over BLE with 3 Witmotions, and with 2 + wG3, on the Mac and on GOLFSIMPC. The verdict "200 Hz sustained" needs a mean ≥ 194 Hz and < 1 % of samples short. Otherwise the KS timing stays at 100 Hz. |
| `still` | The longest still span (\|gyro\| < 3 °/s, \|f\| within 5 % of 1 g, up to 2 glitch samples tolerated). Per axis: bias (mean), SD raw and detrended, drift (means over 30 s blocks in `still_blocks.csv`, and a slope in °/s per min). The \|f\| mean and its deviation from 1 g. | §4.2 / §7.2: a worst-axis bias < 0.5 °/s means the bias estimator is **not** needed for a 1.5 s swing; the drift over 1.5 s is printed. The \|f\| result is checked against the ±2 % health gate in §6.3. |
| `hop` | Per trunk sensor: the landing, its offset t_imu − t_video, the residual, and how well the two sensors agree. Over several hops: the mean offset, its SD (repeatability) and range. | §6.3 step 4 / §7.1 step 2: the session's IMU ↔ camera clock offset. It passes if the residual < 5 ms and the sensors agree within 5 ms. A repeatability SD < 5 ms means one offset per session is enough; above that, the per-swing check (§7.1 step 3) must carry the offset. |
| `tilt` | Per lane: `addressDeg`, `topDeg`, `impactDeg`, `finishDeg` and `finishVsAddressGravityDeg`. A lane without the block reads "absent". | §7.2: if max \|impact tilt error\| < 1° on the trunk lanes, the live filter keeps tilt and trunk re-fusion is optional. Otherwise re-fusion is needed. |
| `hips` | The hip (pelvis) KS node from an analysis with the trunk sensors bound against one with them ignored: the IMU peak and the camera peak (ms against impact, + = after impact), the difference and both σ. Swings are matched by relative path. | Part K: whether the library's "hips peak after impact" (camera, about +20 ms median) survives when the IMU measures the hips. |

### hop — the method

1. **IMU landing.** Find the largest \|f\| > 2 g within 250 ms after a flight dip, where \|f\|
   stays < 0.4 g for ≥ 80 ms. The peak is placed by parabolic interpolation, on the §7.1
   re-timed timeline. Re-timing gives each arrived sample the next index × the nominal period,
   from a robust per-segment line through the arrivals, lowered to their 1st-percentile envelope:
   a sample can only arrive later than it was taken.
2. **Video landing.** Take the hip centre (the mean of COCO-WholeBody 11 and 12, conf ≥ 0.3) and
   its image y, which grows downwards. Smooth it with a **Savitzky–Golay 9-point cubic**, done as
   a local least-squares fit on the real frame times, so dropped frames and frames removed for low
   confidence stay correct. Take the second derivative; upward acceleration = −y″. The apex is the
   highest smoothed hip. The landing is the largest upward-acceleration peak ≤ 400 ms after it.
3. **Offset** = the §7.1 Pearson cross-correlation of the IMU (\|f\| − 1 g) against the video's
   upward acceleration, from 150 ms before the apex to 500 ms after it. It is seeded by the
   difference of the two landing peaks, reported as `offsetPeakMs`, and also run on the raw
   arrival stamps (`offsetRawMs`). The SG kernel is symmetric, so smoothing one side only does not
   move the correlation peak.
4. **Residual** = half the frame period + fit. Fit is half the spread of the offset over the
   9-, 11- and 13-point windows.

## What the selftest proves, and its limits

`selftest` builds synthetic data and recovers known answers, exiting non-zero on any failure. It
passed on all 25 seeds tried.
- **`rate`:** 60 s at 200 Hz in BLE bursts (15.1 ms events), with 10 dropouts of 5 samples:
  199.17 Hz, 50 lost, 10 events, about 3 samples per burst.
- **`still`:** 300 s with bias (0.3, −0.2, 0.1) °/s, 0.02 °/s per min of drift and 0.05 °/s of
  noise: each recovered within 0.01 °/s.
- **`hop`:** 150 fps video with 0.3 px-equivalent hip noise; the IMU leads by 37 ms and the chest
  lands 3 ms after the hip.
- **`tilt`** and **`hips`:** a fake `refusion.json` and two fake KS result trees, including the
  stale-`result.json` trap.

Limits:
- **The re-timed IMU clock still contains the BLE latency floor.** That floor is the smallest
  queueing delay the stream ever shows. It depends on how the connection interval beats against
  the sample period, and can sit anywhere in [0, T). For example, a 15 ms interval against 5 ms
  samples has no beat, so the floor stays wherever it started. It is constant while the
  connection lasts, so the hop absorbs it, but **a reconnect changes it: hop again after any
  dropout and reconnect.** The selftest expects −37 ms plus the synthetic floor.
- **The hop is limited by pose noise.** At 0.3 px the offset scatters by about 1.2 ms per hop,
  with tails to about 3 ms. Peak picking alone is about 3× worse, which is why the
  cross-correlation is the primary method. The residual formula does not capture this scatter;
  the SD over hops does. Do several hops.
- **The face-on pose must be dense.** Every frame of the hop shot needs a pose. On old swings the
  pose ran on every ~4th frame (26.7 ms), and half of that frame period alone is 13 ms, far past
  the gate.
- **A real landing is asymmetric**: a sharp heel strike, then a broad deceleration. The IMU sees
  the strike and the smoothed video sees the deceleration, so the **absolute** offset can carry a
  bias of a few ms, constant for one golfer and one landing style. Compare `offsetPeakMs` with
  `offsetMs` and the 9/11/13-point spread. Repeatability is not affected.
- **`rate` loss attribution.** A gap counts as loss only where the deficit
  (t − t0)/T − index rises for good: its trailing 1 s minimum, with a 0.75-sample hysteresis.
  `short`, the expected count minus the received count, is the figure the verdict uses. It also
  absorbs a slow device clock. On a ~5 s swing window the per-gap loss can over-read, because a
  backlog that is still draining when the window ends is counted as lost. Use recorder files,
  which run for minutes, for the rate measurement. A swing document with no
  `device.outputRateHz`, as on the 2026-06 corpus swings, is checked against an assumed 200 Hz,
  and the summary says so. To use another rate, pass `--assume-hz 100`.
