# Swing storage: compact JSON, raw-vs-mp4, and the `.ppsw` binary format

*Session record, 22 Sept 2026. It covers steps 1–4 of the storage roadmap below. The format and library live in the sister repository **libppswing** (`github.com/PinPoint-Golf/libppswing`). Raw data behind this doc:*
- *`swing_storage/ppsw_corpus.csv`: the corpus conversion;*
- *`swing_storage/raw_vs_mp4*.csv`: the re-analysis comparison.*

## Why

The goal is that a user leaves capture running for 30–60 minutes without disk space becoming a concern. Measured on the corpus (170 swings, `/mnt/swingdata/corpus/swings` plus the live library `/mnt/swingdata/Mark-Liversedge`), a swing at default settings costs:

| File | Per swing | Notes |
|---|---|---|
| `swing.json` | **29.8 MB face-on, 65.8 MB two-camera** (median) | Written `Indented`: 58% whitespace. 85% of it is `analysis.pose2d`/`poseDtl`: 133 keypoints per frame, each number as 17 digits. |
| `Face-On.mp4` / `DTL.mp4` | 6 MB / 9 MB | x264 CRF 23, the full 4 s window |
| `<alias>.raw` | 780–980 MB | Only with `storage/saveRawFrames` (off by default). Uncompressed BayerRG8. |

So **the document, not the video, was about 80% of a default swing**. At about 60 swings an hour, that is roughly 4.8 GB an hour.

Whole-document JSON also costs time. Every reader parses the entire file. Every review, launch-monitor or origin update rewrites all of it. The 650k-node pose tree is what `QmlPayload` converts per shot.

## The roadmap

| Step | What | Status |
|---|---|---|
| 1 | Write swing.json compact (stopgap) | **Done** (this session) |
| 2 | Measure re-analysis from the mp4 against the `.raw` | **Done**: the mp4 is not a faithful stand-in (see below) |
| 3 | Specify a binary per-swing format | **Done**: `libppswing/docs/specification/ppsw-format.md` 1.0 draft, independently reviewed |
| 4 | Library + CLI + Python reader + converter | **Done**: libppswing, corpus-verified |
| 5 | Switch the app over | Next. Decisions listed below |
| 6 | Archiving + housekeeping + the Archiving settings tab | After 5 (proposal below) |

## Step 1: compact swing.json

All five swing.json writers in `src/Export/swing_doc.cpp` now write `QJsonDocument::Compact`:
- `writeSwingJson`
- `updateStreamOrigin`
- `updateReview`
- `updateLaunchMonitor`
- `writeDeviceOnlySwing`

Switching only the first would not have been enough: the next review or launch-monitor rewrite would have re-indented the file.

No reader cares about the change. Every C++ and Python reader parses JSON, none reads it line by line, and there are no golden byte files.

`swing_doc_test` now asserts that a swing.json has no indentation after `updateReview`.

- **Effect:** 2.4× smaller across the corpus (5.18 GB → 2.15 GB).
- **Tests:** `swing_doc_test`, `shot_ledger_test`, `ppcp_clip_filer_test`, `swing_window_parity` pass.

## Step 2: re-analysis from the mp4 vs the `.raw`

**Question.** At default settings a swing keeps only the mp4 (x264 CRF 23, yuv420p, EA-demosaiced, 1280×1024). Live analysis ran on the raw Bayer frames in RAM. Every later re-analysis of a default swing therefore runs on the mp4 instead: a version-gated re-analysis, the in-app Re-analyse button, or a corpus sweep. How far does that move the result?

**Rig (reproducible).**
- **Swings:** all 38 corpus swings that have both `Face-On.raw` and `Face-On.mp4` (sessions 07-05 W2, 07-09 W1, 07-10 W1/W2, 08-18 W1/W2, all face-on only).
- **Inputs:** two symlink trees under `build/rawmp4/`. `in_raw` holds the raw; `in_mp4` is the same swing *without* the `.raw`, so `SwingDiskLoader` falls back to `Mp4FrameReader`. No code change was needed.
- **Tool:** `swinglab_run --out`. It re-runs pose, ball and shaft; recorded products are never reused. `runmeta.json` `frames` confirmed the source for every run.
- **Arms:**
  - R1, R2 (raw, twice) and M1, M2 (mp4, twice): the noise floor;
  - PR, PM: raw and mp4 with `--pose` pinned to R1's pose, isolating everything downstream of pose.
- **Comparison:** `tools/swinglab/raw_vs_mp4.py` writes `swing_storage/raw_vs_mp4.csv` (long form), `raw_vs_mp4_summary.csv` and `raw_vs_mp4_metrics.csv`. Coverage was 38 of 38 swings in all six arms.

**The noise floor is zero.** R1 and R2 are identical on every item, and so are M1 and M2. The pipeline was deterministic on this Mac in this run, so **every R1–M1 difference below is caused by the mp4**.

**Counts first.**

| | Result |
|---|---|
| Phase count | identical in 38 of 38 swings |
| Ball launch found / not found | identical in 38 of 38 |
| Ball found per frame | 100% agreement |
| Club frames tracked by only one source | 0 |
| Metric keys | differ in **3 of 38** swings, 5 keys in all |

The metric-key differences, by swing:
- 07-05 W2 s6: mp4 loses `lowPointAhead`;
- 07-05 W2 s7: mp4 gains `leadHeelLift`, `plumbBobDistance` and `stanceWidthMm`;
- 07-05 W2 s9: mp4 gains `lowPointAhead`.

**Signals (R1 vs M1, over swings).**

| Item | Median | p90 | Pose pinned (PR vs PM) median / p90 |
|---|---|---|---|
| Body keypoints, mean px | 2.2 | 3.6 | 0 / 0 (pinned) |
| Body keypoints, p95 px | 5.3 | 9.1 | 0 |
| Hand keypoints, mean px | 3.3 | 5.9 | 0 |
| Club head, median px | 4.4 | 9.9 | 1.0 / 5.8 |
| Club head, p95 px | 29.6 | 83.2 | 18.2 / 40.2 |
| Club θ, median ° | 0.16 | 2.0 | 0 / 1.0 |
| Club θ, p95 ° | 3.0 | 12.5 | 1.0 / 5.3 |
| Impact P7, ms | 4.3 | 22.2 | 0 / 0 |
| Top P4 / other P, ms | 0–1.3 | up to 97 (P1) | ≤ 6 |
| Ball position, px | 0.02 | 0.3 | 0.02 / 0.3 |
| Ball launch time, ms | 0 | 2.7 | 0 / 29 |

**Metrics.** These are per-phase metric values; "scale" is the median |value|. The full table is `raw_vs_mp4_metrics.csv`. The largest shifts:

| Metric | Scale | R1–M1 median / p90 | Pose pinned median / p90 |
|---|---|---|---|
| attackAngle (°) | 23.9 | **13.6 / 56.6** | **11.3 / 47.6** |
| clubheadPeakLead (ms) | 50 | 2.9 / **159** | 0 / 52.5 |
| pelvisAngularSpeed (°/s) | 209 | **62 / 398** | 0 / 0 |
| thoraxAngularSpeed (°/s) | 356 | 56 / 421 | 0 / 7.4 |
| headTilt (°) | 9.4 | 4.5 / 54 | 0 / 0 |
| transitionPlaneDelta (°) | 6.0 | 4.6 / 12.2 | 3.4 / 14.7 |
| clubheadSpeed (per-phase) | 3.7 | 0.8 / 6.9 | 0.04 / 1.35 |
| shaftAngleVsHorizontal (°) | 66 | 2.0 / 33.9 | 1.0 / 22.0 |
| toeLineAngle (°) | 177 | 1.9 / 350 | 0 / 0 (the p90 is a ±180° wrap, not a real 350° move) |
| stance / tempo / arm-connection family | | ≤ 0.1 relative | 0 |
| hm.* (HackMotion) | | 0 | 0 (IMU only, as expected) |

**Verdict: the mp4 is *not* a faithful stand-in for the raw frames. Re-analysing a default swing moves its numbers.**

1. **Pose-driven metrics.** Pose moves 2–5 px between raw and mp4, and the kinematic metrics built on it amplify that: pelvis and thorax speed, head tilt and lift, knee drift. Pinning pose removes these differences entirely, so they are pose effects, not tracker effects. Structural metrics (stance, tempo, arm connection, foot flare) barely move.
2. **Club-arc metrics are fragile on either source.** `attackAngle` moves by a median 11° *even with pose pinned*: a few-pixel change in the club-head track produces a double-digit change in the angle. `transitionPlaneDelta`, `swingPlaneIota*` and `clubheadPeakLead` behave the same way. This is a conditioning problem in those producers, consistent with the open low-point / 6.5 ms-exposure finding. The mp4 merely exposes it.
3. **Impact timing.** P7 moves by a median 4.3 ms (under one frame at 150 fps) and by 22 ms at p90.

**What this means for storage:**
- Raw frames stay a corpus and research switch; at 780 MB a swing they cannot be a default.
- The default mp4 is too lossy to be the *re-analysis* source without numbers visibly moving.

**Follow-up (proposed, not run).** Repeat this rig with the face-on mp4 re-encoded from the raw at **CRF 18 and CRF 12**. The Impact camera already uses CRF ≤ 12. Record both the size cost and the R–M deltas.
- My earlier probe put a CRF 8 grey encode at ~34 MB per 150 frames, so expect about 20–60 MB per swing at CRF 12–18, against 6 MB today.
- A decision needs that trade-off in numbers. After the `.ppsw` switch, video is already about 70% of a default swing, so this is where the next storage/quality decision sits.
- Separately, the attackAngle and transition-plane sensitivity deserves its own look regardless of video quality.

## Steps 3–4: the `.ppsw` format and libppswing

**Format** (spec §1–§5):
- **Layout:** a 40-byte header, then chunk payloads, then a table of contents. Header, TOC and every chunk are CRC-checked.
- **Chunks:** each chunk is one subtree of the document, encoded as CBOR and compressed with zstd (level 9). A subtree whose encoding exceeds 64 KiB gets its own chunk, and its parent holds a *ref*. So `/analysis/pose2d/synth` never has to be decompressed to show a session list.
- **Generic encoding:** the format knows nothing about the swing schema. `schema` keys ride inside, and a new `analysis.*` block needs no format change.
- **Typed ndarrays:** numeric arrays are stored with the narrowest exact dtype, byte-shuffled, column-major for rank ≥ 2. Integer time bases are delta-coded from an origin: a 4 s `t_us` column becomes int16.
- **Tables:** arrays of objects are stored column-wise, so a frame list's `kp[399]` column becomes one `[N,399]` ndarray.
- **Lossless by construction:** decode(encode(T)) equals T under the spec §1.1 rules: bit-exact doubles, exact integers, and `-0.0` preserved.
  - There is one visible difference. Qt writes an integral double as `37`, so a real-valued column that mixes `37` and `0.5` reads back as all doubles.
  - An opt-in precision hint (`display-f32`) narrows only the display-only pose tiers (`synth`, `smoothed`). No reuse path reads them.

**Review.** An independent review before any code produced 20 findings: 1 blocker, 11 major, 5 minor, 3 nits. **All were applied**; see `libppswing/docs/specification/reviews/2026-09-22-r1.md`.
- The blocker: delta-coding as first written could never narrow a dtype.
- The rest were mostly allocation-bomb bounds and rules where the C++ and Python decoders would have diverged.

Implementation found one more. A 512-level depth limit overflowed MSVC's 1 MB Debug stack; it is now 128.

**libppswing:**
- **Library:** C++17, with vendored zstd 1.5.7 as the only dependency.
- **CLI `ppsw`:** `convert`, `dump`, `info`, `verify`.
- **Python reader:** pure Python and written independently from the spec. It uses stdlib `compression.zstd` on 3.14 and optional numpy. `load_swing()` takes a swing dir, a `swing.json` or a `swing.ppsw`.
- **Tests (6 ctest targets):**
  - JSON edge cases, including a comma-decimal locale;
  - round trips, including 3,000 randomised trees;
  - every spec §10.1 rejection, plus 2,000 random byte-flip corruptions;
  - two anonymised real-swing fixtures (face-on + IMU; two-camera DTL);
  - the Python reader, with and without numpy;
  - a purity gate.
- **Test runs:** green on macOS arm64 (RelWithDebInfo, and Debug with ASan+UBSan) and on Windows MSVC 2026 Debug with `/W4 /WX` (GOLFSIMPC).

### Corpus conversion (`swing_storage/ppsw_corpus.csv`)

All 170 swing.json files were converted: the 115-swing corpus plus the live library, which includes 15 two-camera swings. Every one was verified by **both** decoders against the original.

| | Total | vs today |
|---|---|---|
| swing.json as written until today | 5.176 GB | 1× |
| compact JSON (step 1) | 2.149 GB | 2.4× |
| **`.ppsw` lossless** | **0.492 GB** | **10.5×** |
| `.ppsw` display-f32 | 0.287 GB | 18.0× |
| (17-swing sample) compact JSON + zstd -19 | 0.074 GB vs `.ppsw` 0.050 GB | `.ppsw` 1.5× smaller than the best plain compression |

- **Round trip:** **170 of 170 EQUAL** in the C++ decoder (`ppsw verify`) and in the Python decoder (`semantic_diff` against `json.load`).
- **Per swing (median):**
  - face-on: 29.8 MB → **2.77 MB** (1.63 MB display-f32);
  - two-camera: 65.8 MB → **6.12 MB** (3.68 MB display-f32).
- **Speed (Python, median):**
  - `json.load` 163 ms vs `.ppsw` full load 46 ms;
  - root-chunk-only open **0.7 ms**.
  - C++, measured on 07-04 swing 1: JSON parse 51 ms vs `.ppsw` full load 12 ms.

**What an hour costs now.** Take 60 swings with default settings: face-on + DTL mp4 (15 MB) plus the document.

| Document format | Per hour | Share |
|---|---|---|
| swing.json indented | ~4.8 GB | 81% document |
| compact | ~2.5 GB | |
| **`.ppsw` lossless** | **~1.3 GB** | 30% document |
| `.ppsw` display-f32 | ~1.1 GB | |

After the switch, **video is about 70% of what is left**.

## Step 5: decisions to make before switching the app

1. **Lossless or `display-f32`.** display-f32 saves a further ~40% of the document, about 0.2 GB an hour. It changes nothing that re-analysis reads back. The offline tools `hip_accel_reference.py`, `series_noise.py` and `dtl_posture_offline.py` read `pose2d.smoothed` and would see float32-rounded values. *Recommendation:* start lossless. The document is no longer the dominant cost.
2. **Metrics in the root chunk.** At 64 KiB splitting, `/analysis/metrics` (~100 KB with curves) is its own chunk. The session list wants metric *values* but not curves. Options:
   - raise the split threshold for that subtree;
   - have the app write the headline values into a small `summary` object (it effectively has one today: `swing_summary.json`).

   Either way, the `swing_summary.json` sidecar can then be retired.
3. **Embedding.** Embed libppswing the way libwrist and libppcp are embedded (`PP_LIBPPSWING_LOCAL` sibling override + FetchContent from GitHub `main`). Then add a Qt adapter: `QJsonObject` ↔ `ppsw::Value`, and `NdArray` → typed buffers for QML in place of the `QVariantMap` pose tree.
4. **Readers.** `SwingDocReader`, `SwingDiskLoader`, `DiskReplaySource`, `SwingZipExporter` and `readSwingSummary` should read either format. Writers should write `.ppsw` only. The 26 `tools/` scripts move to `ppswing.load_swing()`.
5. **Migration.** A library-wide `ppsw convert` pass, keeping `swing.json` until a verify pass succeeds. That settles the open swing.json backwards-compatibility concern (20 Aug), since the reader accepts both formats indefinitely.
6. **Zip export:** keep it JSON (`ppsw dump`) for interchange, or ship the `.ppsw`.

## Step 6: archiving (proposal, unchanged from the recommendation)

- **What exists today:**
  - the Archiving settings tab is a placeholder;
  - `autoSaveSession` is saved but nothing reads it;
  - `.pinpoint-trash` is never emptied;
  - nothing checks free space before recording;
  - the "remaining sessions" estimate counts raw frames at clip×4, when the real figure is about 780 MB;
  - `savePoseKeypoints` does not gate `analysis.pose2d`, and its label still says MoveNet.
- **Proposal:**
  - the session is the unit;
  - archive by age or on demand to an archive location (drop raw frames and regenerable sidecars, optionally re-encode video at an archive quality, pack the session into one uncompressed-zip `.ppsa`);
  - leave a stub in the library (root chunk + thumbnail) so history and trends keep working, and restore on open;
  - empty the trash after N days;
  - check free space before a session, expressed as minutes of capture left;
  - optionally auto-archive the oldest sessions below a disk floor.

## Appendix: the raw-vs-mp4 recipe

Swing trees: for every corpus swing that has `Face-On.raw` and `Face-On.mp4`:
- `build/rawmp4/in_raw/<session>/<swing>/` symlinks `swing.json`, `Face-On.mp4`, `*.ballbase.f32` and `Face-On.raw`;
- `in_mp4/…` symlinks the same files minus the `.raw`.

Then run this (about 75 min on the M4 mini: ~40 s per unpinned swing, ~5 s pinned):

```zsh
#!/bin/zsh
# raw-vs-mp4 re-analysis arms. Per-swing 900 s alarm (watchdog); resumable (skips done runs).
cd "$(dirname "$0")"
B=../tools-parity-ninja/swinglab_run
swings=(${(f)"$(cd in_raw && ls -d */swing_* | sort)"})
run_arm() { # arm input [extra args builder]
  local arm=$1 in=$2 pin=$3
  for s in $swings; do
    local out=out_$arm/$s
    [[ -f $out/runmeta.json ]] && continue
    mkdir -p $out
    local extra=()
    [[ -n $pin ]] && extra=(--pose pose_R1/${s//\//__}.json)
    perl -e 'alarm shift; exec @ARGV' 900 $B $in/$s --out $out "${extra[@]}" > $out/run.log 2>&1
    echo "$(date +%T) $arm $s rc=$?" >> progress.log
  done
}
run_arm R1 in_raw
run_arm M1 in_mp4
run_arm R2 in_raw
run_arm M2 in_mp4
mkdir -p pose_R1
python3 - <<'PY'
import json,glob,os
for f in glob.glob('out_R1/*/swing_*/result.json'):
    s=os.path.relpath(os.path.dirname(f),'out_R1').replace('/','__')
    p=json.load(open(f))['analysis'].get('pose2d')
    if p: json.dump({'pose2d':p},open(f'pose_R1/{s}.json','w'))
PY
run_arm PR in_raw pin
run_arm PM in_mp4 pin
echo "$(date +%T) DONE" >> progress.log
```

Compare the arms with:
```
python3 tools/swinglab/raw_vs_mp4.py --runs build/rawmp4 --out docs/implementation/swing_storage
```
