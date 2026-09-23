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
| 5 | Switch the app over, and convert every swing document | **Done** 23 Sept: 170/170 swing.ppsw, both decoders EQUAL (Phase 2 results) |
| 6 | Archiving + housekeeping + the Archiving settings tab | **Done** 23 Sept (Phase 2 results, stage 6) |

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

## Phase 2 plan (approved 23 Sept 2026)

*Steps 5 and 6 plus the two follow-ups from step 2. Results are filled in per stage as they land.*

### Context

Steps 1–4 of the storage roadmap are pushed:
- swing.json is written compact;
- the raw-vs-mp4 verdict is recorded;
- the `.ppsw` spec and libppswing are published, and 170 of 170 corpus swings round-trip exactly.

This phase switches PinPoint Studio to `.ppsw`. **It ends with every swing document in the new format**: 115 in `/mnt/swingdata/corpus/swings` and 55 in `/mnt/swingdata/Mark-Liversedge`, with no `swing.json` left. After that it measures higher-quality video, investigates the club-arc metrics, and builds archiving.

**Run rules (Mark, 23 Sept):**
- The plan runs unattended to the end.
- Everything is committed and pushed **at the end**, with no mid-plan approval stops.
- This explicit instruction overrides the results-before-commit rule for this run. The decision rules below are pre-agreed instead.

**Decisions taken:**
- **Lossless**, not display-f32. The document is no longer the dominant cost, three offline tools read `pose2d.smoothed`, and lossless keeps verification an exact `semanticEqual`.
- **A `/summary` block in the root chunk**, rather than a higher split threshold. It holds exactly what `SwingSummary` holds today (`src/Export/swing_doc.h:324`). It retires `swing_summary.json` and its size/mtime freshness guard, and it makes an archive stub simply "the root chunk".
- **Scope of "all data" = swing documents only.**
  - Converted: `swing.json` → `swing.ppsw`.
  - Retired: `swing_summary.json`.
  - Left as regenerable caches: `swing_phasegrid.json` and `diagnostics.json`.
  - Out of scope: pose caches and `runs/*/result.json`.
- **Old JSON:** one tar.gz backup before conversion, then delete each file after its swing verifies EQUAL.
- **Zip export** stays compact JSON, for interchange (via `ppsw::toJson`).
- **Typed QML buffers** are out of scope; the QmlPayload path stays.
- **The pre-session free-space check** is out of scope: it touches the session wizard, which needs per-change approval.

### Deliverables

- **Document.** `docs/implementation/swing_storage_impl.md`:
  - Stage 0 adds this plan as "Phase 2 plan".
  - The results are filled in at the end: conversion report, CRF table, club-arc sensitivity, archiving.
  - Every figure has a command next to it that reproduces it.
- **Implementation.** Listed per stage below.
- **Done when:**
  - `find` shows 170 `swing.ppsw` and 0 `swing.json` across both roots;
  - both decoders have verified every one;
  - the affected tests pass on the Mac and GOLFSIMPC;
  - the CRF and club-arc results are written up. An honestly failed gate counts as done; a skipped one does not.

### Stage 0: doc

Add the "Phase 2 plan" section to `swing_storage_impl.md`: the decisions, stages and gates from this file. Update the roadmap table so step 5 is in progress and step 6 is planned.

### Stage 1: the app reads both formats and writes `.ppsw` (Mac, build 1)

1. **Embed** in `CMakeLists.txt`.
   - Copy the libppcp block (lines ~445–500): `PP_LIBPPSWING_LOCAL` (sibling `../libppswing`) + `PP_LIBPPSWING_FETCH`, which defaults ON because the repo is public.
   - Clear the stale `FETCHCONTENT_SOURCE_DIR_*` cache the same way libppcp does.
   - Turn the library's tests and tools OFF.
   - Record provenance (version + short sha) for the About box.
2. **New `src/Export/swing_store.{h,cpp}`**, the single door to the document:
   - `load(dir) → QJsonObject` prefers `swing.ppsw` and falls back to `swing.json`;
   - `save(dir, root)` builds `/summary` from the existing `summaryFromRoot()`, writes `.ppsw` atomically (`ppsw::writeFile`), then removes any sibling `swing.json` and `swing_summary.json`;
   - `hasDocument(dir)`, `documentInfo(dir)` (path, size, mtime), `documentPath(dir)`;
   - `convertDir(dir, report)` does parse with `ppsw::parseJson` → add `/summary` → write → reopen with a fresh `Reader` → check `semanticEqual(loadAll minus /summary, original)` → delete the JSON only if EQUAL.
3. **Qt adapter** `src/Export/ppsw_qt.{h,cpp}`: `QJsonValue ↔ ppsw::Value`. Integers stay integers via `toVariant()` type ids. NdArray and Table are flattened through `ppsw::toPlain`.
4. **Route every I/O site through the store:**
   - `swing_doc.cpp` (all eight `/swing.json` sites; `sourcePath`/`summaryPath`, and `readSwingSummary` reads `/summary` from the root chunk only)
   - `swing_reanalyzer.cpp:264`
   - `reanalysis_controller.cpp:116`
   - `disk_replay_source.cpp:152`
   - `swing_data_source.cpp:313`
   - `markup_truth.cpp:128/363/371`
   - `measure_sample.cpp:517`: the phase-grid guard is retargeted to `documentInfo`
   - `session_diagnostics_model.cpp:409`
   - `ppcp_clip_filer.cpp:113/213`: existence checks
   - `swing_zip_exporter.cpp:66/100`: emits JSON
   - the C++ tools `swinglab_run.cpp`, `regrade_ledger.cpp` and `lm_repair.cpp`.

   A grep for `swing.json` outside comments and the store must come back empty.
5. **Storage panel** (`src/Gui/settings/StoragePanel.qml` + controller): an in-app "Convert library" action with progress and a result line. No dialogs, no menu.
6. **CLI** `tools/storage/pps_convert_library.cpp`, using the same `convertDir`: `--dry-run`, `--roots …`, a CSV report and resume. This is what runs on the data.
7. **For stage 4, built in the same pass:** `tools/swinglab/src/raw_reencode.cpp` (`raw_reencode <swingdir> --crf N --out <mp4>`). It re-encodes `Face-On.raw` through the app's own path (`FfmpegVideoEncoder`, the EA demosaic, yuv420p) using the `SwingExportJob` fields from `shot_processor.cpp:1453`.
8. **Tests:** new `swing_store_test`, covering:
   - reading both formats;
   - migrate-on-rewrite (updateReview on a JSON-only swing leaves only `.ppsw`);
   - the `/summary` round trip;
   - convert verify failure (a corrupted write) leaves the JSON in place;
   - the adapter round trip on both libppswing fixtures.
   `swing_doc_test`'s compact assertion becomes a format assertion.
9. **Encode-time gate.** Time `save()` on a two-camera fixture. If it takes more than 50 ms, move the end-of-shot write in `shot_processor.cpp:~2224` onto the worker, keeping the in-memory root cache.

**Build 1:** `cmake --build build/Qt_6_11_1_for_macOS_Debug` (app + tests), plus `build/tools-parity-ninja` for the tools. Explicit `-j`.

### Stage 2: Python tools

- Add `tools/pp_swingdoc.py`. It puts the sibling `../libppswing/python` on `sys.path` and re-exports `load_swing(dir_or_file)`. Python 3.14 is present, so stdlib zstd works.
- Move the 24 scripts that open `swing.json` to it:
  - `tools/swinglab/swinglab/core.py`, `__init__.py` and `synth.py`, which also covers `lab.py ingest` and swing discovery (accept `swing.ppsw`);
  - the metrics, balllab, shaftlab and impactlab scripts.
- Scripts that *write* swing.json (if any) go through `ppsw convert`.
- Check the one `swing.json` mention in `corpus/swings/corpus.json` and CORPUS.md, and update the ingest so the manifest names the document it found.

### Stage 3: GOLFSIMPC, then convert all data

1. **Build on GOLFSIMPC:**
   - clone `libppswing` beside `PinPointStudio` (`C:\Users\developer\Projects\libppswing`, the same convention as libhackmotion);
   - `git fetch; merge --ff-only` after checking the ahead-count;
   - build the studio's app dir and `build\swinglab-vs18` (swinglab_run, pps_convert_library), colocated with the Release DLLs;
   - run the stage-1 test targets in Windows Debug.
2. **Precondition:** no PinPointStudio process on GOLFSIMPC (`tasklist`) or on the Mac, so nothing writes JSON mid-conversion. The studio's installed app must be the new build from here on; released alpha/beta builds cannot see a converted library.
3. **Backup:** `tar -czf build\swingdocs-pre-ppsw-20260923.tgz -T <list>` over every `swing.json` and `swing_summary.json` in both roots (about 0.6 GB), on the studio. Only the JSON names, never the directories. Check the tarball's member count is 340 before continuing.
4. **Convert:** `pps_convert_library --roots <corpus\swings> <Mark-Liversedge>` on the studio (local disk), launched detached with a `.done` sentinel. Rule: a swing that fails verify keeps its JSON, and the stage stops for diagnosis. It does not skip.
5. **Independent second decoder (Mac).** Use libppswing's `corpus_check.py` logic, i.e. `ppswing.load_swing(swing.ppsw)` minus `/summary`, compared by `semantic_diff` against `json.load` of the tarball member, for all 170. Both decoders must agree, as in the corpus proof.
6. **End-state check:** `find … -name swing.json | wc -l` = 0 and `-name swing.ppsw` = 170 in each root's expected split (115 / 55). `swing_summary.json` = 0. Also:
   - open the app on the Mac against the library and confirm by probing, never by screencapture (`--probe-qml` with an absolute path), that every session lists its shots with metric chips, and that one swing replays with overlays;
   - run one `swinglab_run --write-back` on a converted swing and confirm it writes `.ppsw` and the metric count matches the previous run.

### Stage 4: face-on mp4 at CRF 18 and 12

- **Rig.** The appendix recipe under `build/rawmp4/`, over the 38 swings that have `Face-On.raw`. Symlinks now point at `swing.ppsw`.
- **Arms:**
  - R1 and PR (the raw references);
  - an R2 control;
  - **M23r**: the tool's own CRF-23 re-encode. This is the gate: its deltas against R1 must match the recorded-mp4 M1 medians within 10%, or the tool doesn't reproduce the live encode and the CRF arms don't count;
  - M18, M12, PM18, PM12.

  That is about 2 h on the M4, run detached with the per-swing watchdog.
- **Output.** `tools/swinglab/raw_vs_mp4.py` extended with a `--arms` list, writing `swing_storage/raw_vs_crf*.csv`, plus the size per swing for each CRF.
- **Decision rule:** report and recommend only. The default `videoQuality` stays at CRF 23 in this run, because it is a disk-cost trade-off for Mark. The write-up names the CRF (if any) at which pose-driven deltas fall to a body-keypoint mean-px median of 1 px or less.
- The rig tree is deleted once the CSVs are in the repo.

### Stage 5: attackAngle and the club-arc family

- **Offline, no build.** `tools/metrics/club_arc_sensitivity.py`:
  - re-derives `attackAngle` exactly as `club_delivery.cpp:134` does, from the persisted club track;
  - injects head-px noise (σ 1–10 px) and P7 jitter (±1 frame) to measure °/px and °/ms;
  - does the same for `transitionPlaneDelta`, `swingPlaneIota*` and `clubheadPeakLead`, the last of which has never been investigated.
- **Criterion:** `lm.attackAngle` on the LM-paired swings (GC Quad). Stability criterion: the stage-4 PR-vs-PM deltas.
- **Candidate estimators:** read it off `shaft.synth` (the way `lowPointAhead` is), a least-squares velocity over a wider span, and the agreed circle-fit R·tan(AoA).
- **Landing rule (pre-agreed):** a candidate lands only if all of these hold:
  - it cuts the median |error vs lm.attackAngle| by ≥ 30%;
  - it halves the pinned raw-vs-mp4 median delta;
  - it loses the metric on no swing;
  - there are ≥ 10 LM-paired swings to judge on.

  Otherwise it is written up and not landed.
- **If a candidate lands:**
  - port it to `club_delivery.cpp`;
  - bump the shaft/club constant in `src/Analysis/analysis_versions.h`;
  - build 2 (shared with stage 6);
  - run the full two-half sweep on GOLFSIMPC: `swinglab_run --write-back --full-window --session-type 1` per swing (it now writes `.ppsw`), then `regrade_ledger`;
  - back up first, and judge the sweep by the per-swing metric-count diff.
- **Club path:** we have no producer. `clubPath` is a planned DTL-route metric, and fusion work waits on calibration and recording. Deliverable: the conditioning estimate for that route from the fused 3-D shaft, as analysis only.

### Stage 6: archiving and housekeeping (build 2)

- **Archiving tab** (`ScreenSettings.qml` / `SettingsIndex.qml` placeholder → a real panel):
  - trash retention in days, which empties `.pinpoint-trash` (`swing_paths.cpp:306`) on startup;
  - free space shown as **minutes of capture left**, using measured per-swing sizes;
  - archive location;
  - archive-by-age and auto-archive below a disk floor. Both default OFF.
- **Fixes:**
  - the raw-frames "remaining sessions" estimate uses about 780 MB per swing, not clip×4;
  - `savePoseKeypoints` actually gates `analysis.pose2d`, and its label no longer says MoveNet;
  - remove the dead `autoSaveSession` toggle, or wire it (decide from its call sites).
- **Session archive / restore** (`src/Export/session_archiver.{h,cpp}`):
  - pack a session into an uncompressed-zip `.ppsa` at the archive location, reusing the zip writer from `swing_zip_exporter.cpp`. Raw frames and regenerable sidecars are dropped;
  - leave a stub `swing.ppsw` in the library: the root chunk + `/summary` + `/archive {location, archivedAt}` + thumbnail. History and trends keep working;
  - opening a stubbed swing restores it in-app;
  - verify before stubbing: the packed `.ppsa` re-reads EQUAL.
- **Tests:** new `session_archiver_test` (pack → stub → restore → EQUAL; trash retention; minutes-left estimate).

### Stage 7: close out

- Fill in the doc sections with the numbers.
- Update the memory notes: swing-storage-ppsw, reanalyze-library-workflow (`.ppsw` now), golfsimpc-studio-build (the libppswing sibling), plus the swing-json-backward-compat one, which closes.
- **Commits at the end, one per area,** with "Area: " titles in the repo's spelling and no attribution:
  - `Build:` embed
  - `Export:` store/adapter/conversion
  - `Tools:` Python + CLI
  - `Diagnostics:` CRF rig
  - `Metrics:` club-arc (if any)
  - `Settings:` archiving
  - `Docs:`

  Then push `main`. The same goes for libppswing if it changed.
- Delete the rig and result trees. Keep the backup tarball on the studio.

### Verification: affected tests, once, at the end, through ctest

| Target | Why |
|---|---|
| `swing_store_test` (new) | both formats, migrate-on-rewrite, verify failure, adapter |
| `swing_doc_test` | writers, `/summary`, readSwingSummary |
| `shot_ledger_test`, `ppcp_clip_filer_test` | rewrites and existence checks |
| `swing_window_parity` | reanalyser read path, raw re-encode path |
| `session_diagnostics_model_test`, `measure_sample_test` | mtime and phase-grid guard |
| `test_markup_truth`, `chart_metrics_test` | direct readers |
| `session_archiver_test` (new) | stage 6 |

- **Not run:** capture, IMU, PPCP transport, LLM/TTS/STT. They do no document I/O.
- **The same targets run on GOLFSIMPC** (Windows Debug).
- **Data checks:** the stage-3 end-state counts and the two-decoder verify, the probe-qml library check, and one write-back.

**Build budget:** build 1 (stage 1 + re-encode tool), GOLFSIMPC build, build 2 (stages 5–6). Tests run only after build 2, apart from the stage-1 targets, which must be green before stage 3 touches data.

## Phase 2 results (23 Sept 2026)

### Stage 1: the app reads both formats and writes `.ppsw`

**Built.**
- **libppswing is embedded** like libgspro: a sibling `../libppswing` wins over GitHub `main`, and provenance is shown in the About box.
- **Two new files are the only place a ppsw header is included:**
  - `src/Export/swing_store.{h,cpp}` is the single door to a swing's document: `load`, `save`, `info`, `hasDocument`, `loadSummaryBlock`, `convertDir`, `swingDirsUnder`.
  - `src/Export/ppsw_qt.{h,cpp}` is the Qt adapter.

  Together they form the `pinpoint_swingstore` library (`cmake/PinPointSwingStore.cmake`, `cmake/swingstore/`). The app, the tools and every Qt test suite (through `pp_add_test`) link it.
- **Every read and write site** moved to the store: all of `swing_doc.cpp`, the reanalyser, the replay source, the data viewer, markup truth, the phase-grid cache guard, the diagnostics ledger, the PPCP clip filer, the zip exporter, `swinglab_run`, `lm_repair` and `regrade_ledger`. A grep for `swing.json` outside comments and the store finds only the zip exporter's interchange member name.
- **The summary block.** Every writer rebuilds a top-level `summary` from the root it is writing. `readSwingSummary` reads it from the root chunk alone. A JSON-era swing keeps the `swing_summary.json` sidecar until its first rewrite, which migrates it: `swing.ppsw` is written, and `swing.json` and the sidecar are removed.
- **Research outputs stay JSON.**
  - `swinglab_run --out` still writes `result.json`, now through `QSaveFile`. The old rename-into-a-reused-dir bug, which silently kept the previous run's result, is gone.
  - The zip export ships `swing.json`, generated from the `.ppsw`.
- **The in-app action and the CLI share one conversion.** The Storage panel's "Convert library" row (`LibraryConverter`) and `tools/storage/pps_convert_library` (`--dry-run`, `--keep-json`, `--keep-going`, `--csv`) both call `SwingDocWriter::convertToPpsw`. That is `SwingStore::convertDir` with the writer's own summary. It parses with libppswing's parser, writes, re-reads with a fresh reader, checks semantic equality (everything but `/summary`), and only then deletes the JSON.

**Found and fixed on the way:**
1. **libppswing header vs C++20.** `value.h` defined `Object::size()` inline while `Value` was still incomplete. That compiles as C++17 (the library's standard), but libc++ in C++20 (the app's standard) instantiates it eagerly and fails. It is now defined out of line, and a new `header_cxx20` test target builds the headers as C++20.
2. **The encode-time gate.** Saving a 65 MB two-camera document took **1,480 ms** in a Debug build: libppswing, zstd and the adapter were all at -O0. The end-of-shot write was already on a worker thread, but the review, launch-monitor and origin rewrites run on the GUI thread, and the studio runs a Debug build. Three fixes:
   - libppswing now compiles itself optimised whenever it is embedded (`PPSW_OPTIMIZE_ALWAYS`; `/RTC` and `/Od` are stripped in its own directory scope on MSVC);
   - `pinpoint_swingstore` does the same, from its own directory;
   - the adapter no longer builds a `QVariant` per number.

   Measured on `2026-07-04 Wrist_01/swing_0001` (65.6 MB JSON, 6.2 MB `.ppsw`), `PP_STORE_TIMING_DIR=<swing> ctest -R swing_store_test -V`:

   | | before | after |
   |---|---|---|
   | save (adapter + encode + write) | 1,480 ms | **207 ms** |
   | of which, the adapter | ~410 ms | 26 ms |
   | `.ppsw` full load | | 175 ms (JSON warm-cache 107 ms; the JSON's first read off the share took 700 ms) |
   | summary block alone (root chunk) | | **< 1 ms** |
3. **Qt's number model.** Qt 6 already holds an integral double (37.0) and -0.0 as *integers* inside `QJsonValue`. The adapter preserves exactly what Qt holds, typed; `swing_store_test` records Qt's model as the ceiling.
4. **`QDirIterator::Subdirectories` recurses into hidden directories** even without `QDir::Hidden`. The swing-dir walk is now explicit, so `.pinpoint-trash` is never entered.

**Tests.** New: `swing_store_test` (the adapter typed round trip, both formats, migrate-on-rewrite, verified conversion failing closed, both libppswing fixtures, opt-in timing). Reworked: `swing_doc_test`, where the summary block is tested on `.ppsw` and the sidecar on a legacy JSON fixture, including migration by the first rewrite. Two other suites now read through the store (`swing_window_parity`, `ppcp_clip_filer_test`).

### Stage 2: Python tools

`tools/pp_swingdoc.py` finds libppswing's pure-Python reader in the sibling checkout and provides `load_swing`, `has_swing`, `document_path` and `find_swing_dirs`. A `<dir>/swing.json` path whose swing has been converted resolves to the `swing.ppsw`, so call sites that build that path keep working.
- **21 scripts moved to it:** swinglab (`lab.py ingest`, `Swing`, discovery), metrics, shaftlab, balllab and impactlab.
- **Smoke-tested** on a mixed tree (one `.ppsw` swing, one JSON). `series_noise.py` and `hip_accel_reference.py` produce rows byte-identical to a control run on the original JSON.
- **Left alone:** the three writers of synthetic test documents.

### Stage 3: every swing document converted

**Order of operations:**
1. GOLFSIMPC was built from the same tree: the Debug app and the Release tools, with a libppswing sibling clone at `C:\Users\developer\Projects\libppswing`.
2. Preconditions: no PinPoint Studio process on either machine, and the CRF rig had finished copying its documents out of the corpus.
3. **Backup:** `build\swingdocs-pre-ppsw-20260923.tgz` on GOLFSIMPC holds 340 members (every `swing.json` + `swing_summary.json` of both roots), 1.06 GB. **It is kept there.**
4. **Conversion** ran on the studio's local disk (the share's host):

```
pps_convert_library --csv ppsw_convert.csv C:\PinPointStudio\corpus\swings C:\PinPointStudio\Mark-Liversedge
170 converted, 0 already swing.ppsw, 0 failed in 69 s
  JSON 5175724037 bytes -> .ppsw 491776672 bytes (10.5x)
```

   Per swing, the median time was 405 ms (max 1.1 s), and the median `.ppsw` 3.3 MB (`swing_storage/ppsw_library_conversion.csv`).
5. **The second decoder**, on the Mac, from the backup tarball (streamed, nothing extracted):

```
python3 tools/storage/verify_conversion.py --backup swingdocs-pre-ppsw-20260923.tgz --root /mnt/swingdata \
    --csv docs/implementation/swing_storage/ppsw_library_verify.csv
170 EQUAL, 0 DIFFER, 0 MISSING of 170
```

**End state:**

| Root | `swing.ppsw` | `swing.json` | `swing_summary.json` |
|---|---|---|---|
| `corpus/swings` | **115** | 0 | 0 |
| `Mark-Liversedge` | **55** | 0 | 0 |

The share's own `.pinpoint-trash` still holds one deleted swing's `swing.json`. It is trash, and restoring it is the user's call; it is left as it was.

**Checked in the app** (`--probe-qml`, offscreen, the Mac Debug build):
- the library lists 7 sessions;
- the 16 Sept Wrist_01 session loads 3 shots with their pose tracks (215/210/214 frames) and 70/65/70 metrics;
- the replay opens with 2 streams, document load **12 ms** on the GUI thread.

A `swinglab_run --write-back` on a copy of a converted swing rewrote `swing.ppsw`, carrying a `summary`, with the metric count unchanged (70 → 70).

⚠ **The Mac's SMB client showed the deleted `swing.json` files for a few minutes** after the studio deleted them server-side. A walk right after the conversion counted 114, then 113, then 0. It is harmless by construction: a conversion in that window finds it cannot open the phantom file and fails before writing anything. Wait, or run the conversion on the share's host.

### Stage 4: the face-on mp4 at other qualities

**The question.** Re-analysis of a default swing runs on its mp4, and the 22 Sept study found CRF 23 moves the numbers. Two things needed answering:
- does a better encode buy a faithful re-analysis?
- how much would a smaller one (Mark, 23 Sept: "I like the idea of … a lower CRF") cost?

**The rig** (`build/rawcrf`, deleted after the CSVs were committed):
- **Swings:** the 38 corpus swings that have `Face-On.raw`.
- **Encoding:** `tools/swinglab/src/raw_reencode` re-encodes the raw through the production `SwingExporter` (new `--crf` list and `--codec`).
- **Arms:**
  - H.264 at CRF 28, 23, 18 and 12;
  - H.265 at CRF 23 and 28;
  - the recorded mp4 (M1);
  - two raw runs;
  - pinned-pose twins of every video arm.
- **Output:** `python3 tools/swinglab/raw_vs_mp4.py --runs build/rawcrf --name raw_vs_crf --pairs …` writes `swing_storage/raw_vs_crf_summary.csv`, `raw_vs_crf_metrics.csv` and the long form `raw_vs_crf.csv.gz`.
- **The gate: the tool's CRF-23 re-encode reproduces the live encode**, passed. It lands within 10% of the recorded mp4 on every headline median:

  | Median change vs raw | recorded mp4 | re-encode |
  |---|---|---|
  | body pose | 2.19 px | 2.13 px |
  | body p95 | 5.33 px | 4.90 px |
  | hands | 3.34 px | 3.54 px |
  | club head | 4.41 px | 4.79 px |

  - The R1–M1 figures reproduce the 22 Sept study exactly, so the storage switch changed no analysis.
  - Two encodes at the same CRF still differ by 1.5 px of body pose: the encoder's own floor.
  - R1–R2 is 0 everywhere.

**Sizes, per swing, per 1280×1024 face-on camera, full window** (one swing, `raw_reencode`):

| Encoding | Size | vs today |
|---|---|---|
| H.264 CRF 28 ("low") | 2.5 MB | 0.35× |
| H.265 CRF 28 | 2.8 MB | 0.39× |
| **H.264 CRF 23 (today, "medium")** | **7.1 MB** | 1× |
| H.265 CRF 23 | 8.0 MB | 1.13× |
| H.264 CRF 18 ("high") | 32.5 MB | 4.6× |
| H.264 CRF 12 | 156 MB | 22× |
| H.264 CRF 0 ("lossless", after the fix) | 461 MB | 65× |
| raw BayerRG8 | 780–980 MB | ~125× |

x265's CRF scale is not x264's. At the same CRF, H.265 was *bigger* here, and it took about twice the encode time on the Mac.

**Re-analysis vs raw, median over 38 swings** (`raw_vs_crf_summary.csv` / `_metrics.csv`):

| | H.264 28 | H.265 28 | **H.264 23** | H.265 23 | H.264 18 | H.264 12 |
|---|---|---|---|---|---|---|
| body keypoints, mean px | 2.70 | 2.60 | 2.19 | 2.09 | 1.80 | 1.43 |
| body keypoints, p95 px | 6.34 | 6.06 | 5.33 | 4.96 | 4.22 | 3.53 |
| hand keypoints px | 4.21 | 4.17 | 3.34 | 3.51 | 2.76 | 2.53 |
| club head px | 5.14 | 4.63 | 4.41 | 4.23 | 3.49 | 3.92 |
| pelvis speed °/s | 71 | 86 | 62 | 48 | 40 | 68 |
| thorax speed °/s | 56 | 58 | 56 | 42 | 44 | 33 |
| head tilt ° | 4.6 | 5.3 | 4.5 | 4.6 | 3.7 | 3.0 |
| clubheadPeakLead ms | **23.2** | 2.7 | 2.9 | 2.3 | 2.3 | 2.4 |
| **pose pinned:** club head px | 1.23 | 0.58 | 1.03 | 0.37 | 0.42 | 0.24 |
| **pose pinned:** transitionPlaneDelta ° | 3.6 | 1.8 | 3.4 | 2.6 | 2.4 | 3.0 |
| phase P7, metric keys, score | 0 | 0 | 0 | 0 | 0 | 0 |

**What it says:**
1. **No mp4 is a faithful stand-in for raw, at any size.**
   - Pose improves slowly with quality (2.2 → 1.4 px from CRF 23 to 12, at 22× the bytes).
   - The metrics built on it do not reliably improve: pelvis speed at CRF 12 is *worse* than at 18.
   - The sensitive metrics amplify *any* re-encode, including two encodes at the same CRF. The limit is the producers' conditioning, not the pixels, which is exactly what stage 5 found for attackAngle.
2. **So raw frames cannot be replaced by an mp4 where re-analysis must match exactly.** Where "close" is enough (research browsing, most metrics), CRF 18 does about what CRF 12 does at a fifth of the size.
3. **A smaller default costs a little.**
   - H.264 CRF 28 saves 65% of the video (≈ 0.55 GB an hour for face-on + DTL at 60 swings/h). It adds ~0.5 px of pose error and ~10°/s of pelvis speed, and it breaks `clubheadPeakLead` (median error 23 ms, against 3).
   - H.265 CRF 28, at almost the same size, keeps `clubheadPeakLead` but moves pelvis speed more (86°/s).
4. **H.265 CRF 23 is the best quality per byte measured.** At 8 MB it beats H.264 CRF 23 on pose, pelvis and thorax, and on the pinned club track (0.37 px against 1.03). But it is *not* smaller than today, and it costs about 2× the encode time on the capture machine.

**Decision (Mark, 23 Sept, after stage 5b): the default is now CRF 28** ("low", relabelled "Compact"; `AppSettings`). An install that saved a quality keeps it; neither the Mac nor the studio had one saved. The analysis before the decision: The recommendation, if disk is the goal:
- **H.264 CRF 28 is the simple saving.** Its one outlier above, `clubheadPeakLead`, turned out to be fragile at *every* quality, and was then fixed (stage 5b): raw-vs-CRF 28 went 23.2 → 1.2 ms.
- **Leave the default at CRF 23** if the remaining ~0.5 px pose and ~10°/s pelvis-speed cost matters more than 0.5 GB an hour.
- Either way, the measurements say video quality is the wrong lever for faithful re-analysis. The producers are the right one, one by one, as with attackAngle and clubheadPeakLead.

### Stage 4b: trimming the pre-roll (tested and rejected, 23 Sept)

**The question** (Mark): the saved clips carry seconds of the golfer standing still before address. Should they be cut, keeping a quarter-second?

**Measured** over the 144 swings with video, address and impact:

| | median | mean | p10–p90 |
|---|---|---|---|
| Clip length | 4.99 s | 4.59 s | 3.99–4.99 s |
| **Before address** | 2.49 s | **2.08 s** | 1.40–2.55 s |
| Address → impact | 0.98 s | 1.00 s | 0.95–1.07 s (p99 1.34, max 2.27) |
| Impact → end | 1.50 s | 1.51 s | |

The mean hides two eras: about **2.5 s** before address in the June–July sessions, and about **1.45 s** since 18 Aug, when the window was shortened.

**It is worth it in bytes.** The encoder writes a keyframe every 10 frames, and sensor noise keeps still frames from being cheap, so bytes follow seconds. Cutting to address − 0.25 s would remove ~30 % of today's face-on bytes (44 % of the older DTL clips), and the same fraction of a raw sidecar.

**What was tried: option A.** Keep from **impact − 1.6 s**: the p99 address plus the quarter-second, measured from impact because the export runs in parallel with the analysis and impact is the one instant it already has. It reused the exporter's existing keep band (the impact camera's), which trims the mp4 and the raw sidecar alike, for a saving of ~17 % on today's clips.

**The check** (`build/rawtrim`):
- `raw_reencode` gained `--keep-before-impact-ms`, `--save-raw` and `--write-doc`, to write a swing exactly as the app would with the trim.
- The 38 raw-bearing corpus swings were re-analysed four ways: full raw (R1), trimmed raw (T1), full CRF 28 (M28), trimmed CRF 28 (T28).
- R1 vs T1 isolates the trim from compression. Output: `swing_storage/preroll_trim_{summary,metrics}.csv`.

**It failed:**
- **Mechanically fine.** Impact moved 1.2 ms, and the phase count and score were unchanged.
- **Takeaway moved 150–213 ms late** on several swings (p90 166 ms), and address a median 11 ms (p90 49 ms). With only ~0.6 s left before address, the detectors lack their stretch of stillness.
- **44 of 47 metrics changed**, far more than compression alone:

  | median change | trimmed vs full (raw) | CRF 28 vs raw |
  |---|---|---|
  | attackAngle | **10.2°** | 1.2° |
  | lowPointAhead | **5.3 in** | 0.75 in |
  | ballPosition | **23** | 1.7 |
  | pelvisAngularSpeed | **144°/s** | 71°/s |

  The late takeaway re-anchors the swing arc, which moves everything built on it.
- **12 of 38 swings lost a metric, and about as many gained one.** The setup family (ball position, stance width, lead-heel lift, plumb-bob) flickers in and out: it is measured in the still moment at address.
- Option B (cut at the detected address − 0.25 s) would leave even less before address.

**Decision (Mark): reverted, not shipped.** Live analysis never depended on it (it reads the frames in RAM), but every later re-analysis would; the library was just swept twice. The saving (~17 %) is smaller than the harm, and CRF 28 had already cut the video ~65 %.

**What would make trimming safe:** make takeaway detection and the setup metrics independent of how much pre-roll exists. Then a trimmed clip, or any clip that starts late, stops mattering, and the trim could be reconsidered. A gentler cut (impact − 2.0 s, ~1 s before address, ~9 % saving) was not tested.

### Stage 4c: how Swing Catalyst stores swings (for comparison, 23 Sept)

**Measured** on GOLFSIMPC (read only), Swing Catalyst 10.1.5.37386, under `C:\ProgramData\Swing Catalyst`:
- a SQLite database, `SwingCatalystDB.s3db` (2.4 MB), with dated startup backups in `database\backup`;
- one folder per golfer and per session under `database\golfers`;
- **25.8 GB over ~1,360 shots in 20 sessions** (Nov 2025 – Jun 2026; three golfers, about 12 GB of it Mark's).

**Per shot:**

| File | Size | What it is |
|---|---|---|
| `… Down the line.mp4` | ~17 MB | H.264 Main, 1280×1024, yuv420p, 149.3 fps, 5.0 s (747 frames), ~27.6 Mbit/s |
| `… Face on right.mp4` | ~9.7 MB | the same format, ~15.6 Mbit/s |
| `….jpg` per clip | ~12 KB | thumbnail |
| `…-balltrajectory.pbuf` | ~20 KB | the launch monitor's ball flight, protobuf |
| `….mp4.index` (some clips) | ~3 KB | binary; apparently a seek index |

- Clips are muxed by libavformat (Lavf58.76), with a keyframe every 20 frames (ours: every 10), I and P frames only.
- Mark's earliest sessions (Nov 2025) are ~1 MB a shot: the same resolution and length, but recorded at 33 fps.

**The same capture as ours:** the same Chameleon3 cameras, 1280×1024 at ~150 fps, a full ~5 s window, and **no trim before address** either.

**Different in what is kept:**
- **No raw frames, no pose, and no per-frame analysis.** Beyond the ball-flight file and a database row, a shot is its two videos.
- Their face-on video (9.7 MB) sits just above our former CRF 23 default (7.1 MB), and well above the CRF 28 default (2.5 MB). Their DTL clip is larger than their face-on for the reason ours is: a busier view.

| Per two-camera shot | size |
|---|---|
| Swing Catalyst | ~27 MB, all video |
| PinPoint, CRF 23 (former default) | ~23 MB (~16.5 MB video + ~6 MB `.ppsw`) |
| PinPoint, CRF 28 (default since 23 Sept) | ~12 MB (~6 MB video + ~6 MB `.ppsw`) |

**Reading.** Swing Catalyst made the choice this study converges on: moderate-bitrate video of the full window, and no raw frames. It spends its whole budget on video. PinPoint spends about half of its new, smaller budget on the swing document (pose, club track and every metric), which is what lets a swing be reviewed and re-analysed without reprocessing it.

### Stage 5: attackAngle and the club-arc family

**The finding: `attackAngle` was reading the wrong heads.** It was a ±2-sample centred difference of the *measured* head positions, interpolated at impact.

On the 13 swings that carry both our `attackAngle` and the GC Quad's `lm.attackAngle` (6 from 18 Aug, 7 from 16 Sept):
- The median number of measured heads within ±2 frames of impact was **zero**. The head is a blur at impact, and the detector drops it.
- So the difference spanned samples about 220 px apart, well away from the strike.
- The result read a median **36.5° from the launch monitor** (signed −11.8°), against a launch-monitor median of −2.6°.
- Monte Carlo jitter of the heads moves it only 0.1–0.5°, so this is **bias, not noise**.

(`tools/metrics/club_arc_sensitivity.py`, `swing_storage/club_arc_attack.csv`; the production estimator is re-derived and reproduces the stored value on 13 of 13.)

**Candidates:**

| Estimator | n | median \|error\| vs GC Quad | signed | spread at 2 px noise | impact ±1 frame |
|---|---|---|---|---|---|
| production (measured heads) | 13 | **36.45°** | −11.8° | 0.16° | 1.64° |
| **the synthesized arc**, ±2 steps (4.2 ms) | 13 | **3.59°** | −3.25° | — | — |
| least squares, measured heads ±15 ms | 0 | no measured heads there | | | |
| circle fit, measured heads ±60 ms | 6 | 12.87° | +7.0° | 0.32° | 21.4° |

The arc window was also checked at k = 1, 2, 3, 4 and 6 steps, giving median errors of 3.6, 3.6, 4.1, 4.9 and 7.1°. **k = 2 was kept.**

**Stability** (the raw-vs-mp4 rig's arms, `swing_storage/club_arc_stability.csv`):
- Estimates move by 0 between two raw runs.
- With pose pinned (PR vs PM), only the frames' encoding differs:

| Pair | production median / p90 | **arc (hybrid)** median / p90 |
|---|---|---|
| PR–PM1 (recorded CRF 23) | 11.25° / 42.9° | **0.45° / 2.85°** |
| PR–PM23 (re-encode) | 15.60° / 52.1° | 0.75° / 3.23° |
| PR–PM18 | 12.83° / 36.6° | 0.40° / 3.00° |
| PR–PM12 | 9.73° / 30.3° | 0.30° / 2.00° |
| R1–M1 (pose free) | 13.60° / 55.0° | 1.19° / 5.32° |

**Conditioning.** Near impact the arc head moves a median 12.1 px/ms, so a ±2-step difference spans about 100 px. Noise of σ px therefore costs about 0.4° per px: 0.4° at 1 px, 0.8° at 2 px, 2.0° at 5 px.

**⚠ The median hid two failures, found by the first library sweep.** On 16 Sept Wrist_01 swing_0001 the arc read **+82°** against the GC Quad's −3°, and Wrist_03 swing_0001 read +12.8° against −0.9°. The arc is an interpolation between located anchors, and a mislocated anchor near impact puts a *jump* in it: ~310 px in one 4 ms step on swing_0001.

**The continuity guard.** The arc is now used only if no step within the ±3 steps the difference spans exceeds **3× the window's median step** (`clubDelivery.attackSynthMaxStepRatio`); otherwise the swing keeps the measured estimate.
- Against the GC Quad: median |error| **2.7°** over the 13. 10 read off the arc, and 3 keep the old reading, which for them is −41°, +46° and −3.6°: no better, and no worse, than before.
- Across the library's 143 attackAngle swings: 112 read off the arc; 25 fall back on a jump, and 5 on coverage.
- On the rig the guard never fires (its arcs are clean), so the stability figures above are unchanged.
- `club_delivery_test` section 9 pins the behaviour: a jumped arc yields exactly the measured estimate.
- **Open, for a follow-up:** the 30 fallback swings still carry the ill-conditioned measured estimate. The options are to read the continuous side of a broken arc, or to decide that no value beats a wrong one; the latter is a coverage-rule question for Mark.

**Landing rule (pre-agreed): all four met, so it landed**, and re-checked with the guard (median error 2.7°, stability unchanged).
1. The median error was cut by **90%** (36.5° → 3.6°), against the ≥ 30% required.
2. The pinned raw-vs-mp4 median fell **25×** (11.25° → 0.45°), against halving required.
3. No swing loses the metric. The arc is used only when it has ≥ 5 samples within ±20 ms of impact, and otherwise the measured estimate stays. The arc is missing on 6 of 143 swings, so without the fallback they would have lost it; on the 38 rig swings every one keeps it.
4. There are 13 launch-monitor-paired swings, against ≥ 10 required.

**What changed:**
- `club_delivery.cpp` (with `clubDelivery.attackSynthWinUs` / `attackSynthMinSamples` in `pp_tuned_constants.h`);
- `club_delivery_test`: the no-measured-head case now expects the arc's reading.

No stage-version bump is needed: `analysis_versions.h` versions *tracks*, and metrics are recomputed on every re-analysis.

**The library sweep.**
- **Backup first:** `build\\swingdocs-pre-aa-sweep-20260923.tgz` on GOLFSIMPC holds all 170 `swing.ppsw` and 16 `diagnostics.json`.
- **The run:** `swinglab_run --write-back --session-type 1` for every swing, with `--full-window` on the corpus (its pose was recorded full-window) and without it on the live library.
- **Swings touched:** 145 rewritten. The 25 of `2026-08-04 Wrist_05` (LM only, no video) exit 1 as always.

**The first sweep ran unguarded, and was reverted.** It found the arc jumps described above: +82° on 16 Sept Wrist_01 swing_0001. The library was restored from the backup (186 of 186 files byte-identical) and swept again with the guarded build.

**A Mac sweep was also stopped and reverted.** On the Mac, re-analysis re-ran pose, and stage 4 shows exactly why that moves the numbers; on the studio, pose is reused. It was stopped after 7 swings and restored.

**Final (guarded, GOLFSIMPC), judged per swing against the backup:**

| | |
|---|---|
| `attackAngle` changed | 131 swings; median \|value\| 30.3° → 11.2° |
| against the GC Quad (12 paired swings that kept the metric) | median \|error\| **32.3° → 2.9°**. The largest remaining error, 48.6°, is a guard fallback, i.e. the old estimate |
| gained metrics | 35 swings, mostly the DTL posture family (landed 21 Sept, after these swings were last analysed) |
| other per-phase values changed | 13,605. A re-analysis adopts every producer change since the last sweep |
| lost `lowPointAhead` only | 7 swings. A known re-analysis behaviour, kept |
| **lost more** | **2 swings, both reproducible across both sweeps; restored from the backup** |

⚠ **The two restored swings are open.** 16 Sept Wrist_01 swing_0001 loses its entire club track when re-analysed on Windows (16 club metrics, 70 → 54), and swing_0003 loses 5 metrics. The same swing_0001 re-analysed on the Mac keeps all 70. It is a platform difference in re-analysis, and is **open**. Both swings were restored from the backup and keep their pre-sweep values, including the old attackAngle.

**Half 2: the ledgers.** All 16 sessions' `diagnostics.json` were re-graded under the current pack (`regrade_ledger --tag aa-sweep`); each previous ledger is kept as `diagnostics.json.aa-sweep`.

### Stage 5b: clubheadPeakLead (added on request, 23 Sept)

**The metric.** The ms before impact at which the composed clubhead speed peaked: near 0 is ideal; tens of ms is the "deceleration" fault.

**The CRF study flagged it:** raw vs CRF 28 moved it a median 23 ms. But per swing, the change was bimodal at *every* quality. It jumped ~60 or ~160 ms on 16 of 38 swings at CRF 23, 15 at 18, 13 at 12 and 20 at 28. With pose pinned it barely moved, so the trigger was pose-dependent.

**The cause** (a rebuilt three-arm rig: raw, recorded mp4, CRF 28, the same 38 swings):
- The Top tick and the P7 anchor were identical between arms; the peak itself was not.
- A jump in the synthesized arc (a mislocated anchor, the defect behind attackAngle's +82°) differentiates into a 1–3-sample **speed spike**. On 2026-07-05 Wrist_02 swing_0002 it read 76 and 90 mph ~185 ms before impact, on a curve reading 12–21 mph there.
- The spike *was* the maximum, so the existing 97 % plateau rule could not help.

**The fix.** The peak is searched on a **±17 ms running median** of the speed (in time, so the same on any grid; ±4 samples at the 240 Hz arc). A real loss of speed lasts tens of ms and survives it.
- The window was chosen on the rig from ±1 to ±4 samples: stability improved monotonically, and ±4 kept a genuine 100 ms loss of speed at 100 ms in the test.
- `clubheadPeakLeadFromSpeed` is exposed for the new test, which fails without the median (145.8 ms) and passes with it (91.7 ms).

**Results** (`swing_storage/peak_lead_stability.csv`):

| | before | after |
|---|---|---|
| raw vs recorded mp4: median / p90 / swings > 20 ms | 2.9 / 158 ms / 16 | **0.5 / 8.3 ms / 4** |
| raw vs CRF 28: median / p90 / swings > 20 ms | 23.2 / 156 ms / 20 | **1.2 / 8.3 ms / 4** |
| the raw reading itself: median / p90 | 62 / 176 ms | **2.1 / 5.2 ms** |
| swings losing the metric | | 0 |

⚠ **The old values were mostly spikes, so the `deceleration` characteristic will fire far less.** It appeared in the 07-04 and 08-18 Wrist_02 ledgers. That is correct, but it is a visible change in what the app says.

**The library sweep** (GOLFSIMPC, the same flags as stage 5):
- **Backup first:** `build\\swingdocs-pre-pl-sweep-20260923.tgz` (186 files).
- **Changes:** `clubheadPeakLead` changed on 116 swings; median 82.9 → **3.6 ms**, p90 169 → 55 ms.
- **Restored from that backup**, as before: the two 16 Sept Wrist_01 swings (the open Windows club-track loss), and 3 July Wrist_01 swing_0010, which lost `ballBodyDistance`.
- ⚠ **Re-analysis on Windows is not run-to-run reproducible.** Nothing but the peak-lead search changed since the previous sweep, yet 1,619 other per-phase values moved, mostly the club-speed family (`clubheadSpeed` 235, `lagAngle` 232, `handSpeed` 172). This is **open**, with the club-track loss.
- **Ledgers:** re-graded (`regrade_ledger --tag pl-sweep`; previous ledgers kept as `diagnostics.json.pl-sweep`). The `deceleration` pattern now fires in **no** session; after the previous re-grade it fired in 2 (07-04 and 08-18 Wrist_02).

### Stage 6: archiving and housekeeping

**What was built:**
- **`src/Export/session_archiver.{h,cpp}`**, where the session is the unit.
  - **Archive:**
    1. copy every file to `<archive>/<athlete>/<session>/` through `.partial` + rename;
    2. verify each copy **byte for byte**, touching nothing in the library before all of them match;
    3. thin each swing to a stub `swing.ppsw` (summary, metric and phase rows, review, stream identities, and an `archive` block) plus `thumb.jpg`;
    4. write `archived.json`.

    Raw frames are archived unless the user turns that off. Caches are neither archived nor kept.
  - **Restore:** copy back with the same byte check, keep any review made while archived, then remove the marker. The archive copy is kept.
- ⚠ **A folder, not the proposed `.ppsa` zip.** Qt's `QZipWriter` has no Zip64, so nothing can pass 4 GB. A session that kept its raw frames runs to ~900 MB a swing, so exactly the archives worth most would have been corrupt. A mirror folder has no limit and restores without this application.
- **`ArchiveController` and the Archiving tab**, which was a placeholder:
  - hours of capture left, using the mean size of this library's last 30 swings, at one swing a minute. The probe read 195 MB a swing (face-on + DTL + Impact, some raw) and **70 h** free;
  - the archive location, typed (no native dialogs);
  - keep raw in the archive (default on);
  - archive-by-age and archive-below-a-floor;
  - trash retention, and "empty now";
  - a session list with Archive and Restore;
  - progress and cancel.
- **Defaults.** Every automatic behaviour defaults **off**. The startup pass runs a minute after launch and never touches today's sessions.
- **Opening an archived session in the review restores it** (`archivedSessionOpened` → `restoreSession` → `sessionRestored` → reload).

**Fixes:**
- **The Storage panel's estimate** used invented multipliers: raw was counted as clip×4 (it is ~900 MB), and "sessions" meant 50 swings. It now uses measured sizes per camera: low 2.5 MB, medium 7.1, high 32.5, lossless 461 (stage 4), raw 900 and document 6.1. Capacity is shown in **hours of capture**.
- ⚠ **"Lossless" video exported no video at all.** CRF 0 under x264's `high` profile fails `avcodec_open2` ("high profile doesn't support lossless"). The encoder now asks for `high444` at CRF 0 (still 4:2:0; verified to decode).
- **`savePoseKeypoints` now gates `analysis.pose2d`/`poseDtl`** and their version stamps. Re-analysis then re-runs pose instead of reusing a track that is not there. Its label no longer says MoveNet.
- **The dead `autoSaveSession` toggle was removed.** Its "writes to archive" meaning would have started archiving on upgrade, because it defaulted to true.

**Tests.** New: `session_archiver_test` covers:
- the refusals, and a cancel leaving the library byte-identical;
- archive byte-identical, raw included;
- stubs that list with the same chips, score, stars and club;
- a review made while archived surviving the restore;
- a byte-for-byte restore;
- raw dropped only on request.

Trash retention and the hours-left figure live in `ArchiveController` (GUI), checked by probe, not by a suite.

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
