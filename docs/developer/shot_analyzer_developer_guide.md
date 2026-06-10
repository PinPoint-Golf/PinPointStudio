# Pinpoint Shot Analyzer — Developer Guide

**Audience**: Developers working on or integrating with the Pinpoint application  
**Location**: `src/Analysis/` (analyzers + math), `src/Gui/shot_processor.{h,cpp}` (orchestration), `src/Export/swing_doc.{h,cpp}` (persistence)  
**Language**: C++17 (analysis value types and math) / C++20 (app integration)  
**Status**: Pipeline production; the M1 **Wrist** analyzer is real (IMU-only chain); Swing / GRF / Coach analyzers are deterministic stubs awaiting their pipelines.

---

## Contents

1. [What the Shot Analyzer Is](#1-what-the-shot-analyzer-is)
2. [Where It Fits in Pinpoint](#2-where-it-fits-in-pinpoint)
3. [Core Concepts](#3-core-concepts)
4. [The ShotProcessor Pipeline, Stage by Stage](#4-the-shotprocessor-pipeline-stage-by-stage)
5. [Getting Started — Writing a New Analyzer](#5-getting-started--writing-a-new-analyzer)
6. [The M1 Wrist Chain](#6-the-m1-wrist-chain)
7. [The Scoring Model](#7-the-scoring-model)
8. [Output Shapes — From Worker to QML](#8-output-shapes--from-worker-to-qml)
9. [Persistence — the Unified swing.json](#9-persistence--the-unified-swingjson)
10. [Threading and Lifetime Rules](#10-threading-and-lifetime-rules)
11. [Internals — Design Decisions Explained](#11-internals--design-decisions-explained)
12. [Testing](#12-testing)
13. [Common Mistakes](#13-common-mistakes)
14. [File Map](#14-file-map)

---

## 1. What the Shot Analyzer Is

The shot analyzer turns a **frozen 5-second `SwingWindow`** — raw IMU packets,
camera frames, and the shot marker — into everything the user sees about a
shot: a 0–100 **score**, per-metric **values at impact** (the carousel chips),
a **trace** sparkline, and the rich **`SwingAnalysis` detail** (full metric
curves over a shared time grid, swing-phase events, score breakdown, ranked
faults) that drives the replay-synced metric graph.

It is a **per-session-type** abstraction: a Wrist session and a GRF session
analyse the same kind of window with entirely different pipelines. The
`ShotAnalyzer` interface plus a factory keyed on
`SessionController::Type` keep that polymorphism out of the orchestration code.

The analyzer is **not** shot detection (deciding that/when a shot happened —
see `docs/developer/shot_detector_developer_guide.md`) and **not** the media export
(encoding MP4s + thumbnail — see `docs/developer/swing_export_developer_guide.md`). It
runs *concurrently with* the export over the same frozen window, and the two
join before anything is published.

---

## 2. Where It Fits in Pinpoint

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  ShotController ──shotDetected(source, impactUs, sessionType)──┐             │
│                                                                ▼             │
│                                                        [ShotProcessor]       │
│                                                        POSTROLL (500 ms)     │
│                                                                │             │
│                                       pauseBuffer → captureSwingWindow(5 s)  │
│                                                                │             │
│                       ┌───────────────── PROCESSING ───────────┴──────────┐  │
│                       ▼ (QtConcurrent)                    (QtConcurrent) ▼  │
│              [ShotAnalyzer]                              [SwingExporter]     │
│              makeShotAnalyzer(type)                      MP4s + thumb.jpg    │
│              fuse → phases → metrics                     + raw manifest      │
│              → score → detail                                  │             │
│                       └───────────────► join ◄────────────────┘             │
│                                          │  writeSwingJson (unified doc)     │
│                                          │  ShotListModel::addShot (ALWAYS)  │
│                                          ▼                                   │
│                              REPLAYING (¼×, iff both OK)                     │
│                                          │                                   │
│                            finish: window destroyed →                        │
│                            applyCaptureIntent() → Idle (trigger re-arms)     │
└──────────────────────────────────────────────────────────────────────────────┘
```

Consumers of the result:

- **`ShotListModel`** — every shot lands on the carousel, no matter what failed
  (failures degrade to `hasVideo=false` / score 0).
- **`ShotProcessor.replayAnalysisDetail`** — the in-replay metric graph
  (`ScreenWrist`) binds to the detail of the shot currently replaying, synced
  to the replay playhead (`replayPositionUs`, same µs domain as the series).
- **`swing.json`** — the analysis is folded into the one per-shot document so
  the shot reloads after a restart (`SwingDocReader` →
  `ShotListModel::addPersistedShot`) and feeds the session-review drawer.

---

## 3. Core Concepts

### `ShotAnalysisJob` — the value-type job

Everything the worker needs, **resolved on the UI thread before launch** (the
same rule as `SwingExportJob`): session type, shot source, impact timestamp,
the window's camera/IMU/marker source IDs, athlete handedness, the swing
directory, and — critically — the **IMU→segment bindings**. The worker must
never touch `AppSettings`, controllers, or live `ImuInstance` objects.

### `ImuSegmentBinding` — calibration snapshot

`{SourceId, SegmentRole, alignA, mountM}`. The anatomical calibration
quaternions A and M live on the `ImuInstance` (session-lifetime, GUI thread),
so they are *copied into the job* at build time. `SegmentRole` comes from the
user's placement slots (`AppSettings::imuPlacement`): for Wrist sessions,
slot A = LeadForearm, B = LeadHand, C = LeadUpperArm
(`segmentRoleForSlot`, shot_processor.cpp). Unknown roles are skipped by the
fuser but their A/M still travel with the job.

### The TimeGrid and `FusedStreams`

Raw IMU sources tick at their own rates and phases. `ImuVisionFuser::fuse()`
builds one fixed-rate (200 Hz) **master TimeGrid** over the bound IMUs' common
in-window coverage and resamples every segment onto it as **anatomical
quaternions** `q_anat(t) = A·q_raw(t)·M` (slerp via
`SwingWindow::interpolateImu`). Everything downstream — phases, metric curves,
the replay graph — indexes this one grid. Timestamps stay **absolute µs in the
`EventBuffer::nowMicros()` domain**, so the replay playhead needs no
conversion.

### Phases

`PhaseSegmenter` emits the swing-phase timeline (`Address / Top / Impact /
Finish` in M1). **Impact is never detected here** — it is the hard
`ShotMarker` anchor passed in via `job.impactUs`, clamped to the grid. Phases
carry confidences; low-confidence ticks fade in the UI rather than disappear.

### `MetricSeries`

One named metric's full story: `{key, label, unit}`, the continuous curve
(`t_us[]` + `value[]` over the TimeGrid), sparse `PhaseSample`s (the value at
Address/Top/Impact, with a score band at scored phases), and an optional
ideal band. Values are degrees, **Address-referenced** (zero at setup).
`flexPositive` records stored-sign polarity — flip only at the UI label, never
in storage.

### `ShotAnalysisResult` vs `SwingAnalysis`

The result has two tiers. The flat tier (`score`, `metrics` map,
`tracePoints`) mirrors the `ShotListModel` roles exactly, so the join hands
them to `addShot()` unmodified. The rich tier (`detail`, a
`shared_ptr<SwingAnalysis>`) carries the series/phases/score-breakdown/faults
— null from stub analyzers, folded into `swing.json` and the replay graph when
present.

### Degradation, not failure

`ok=false` (with `error`) is a *normal* outcome — no usable IMU data, missing
hand sensor, etc. The shot still lands on the carousel with score 0. The
analyzer must never crash on a sparse window; it degrades.

---

## 4. The ShotProcessor Pipeline, Stage by Stage

`ShotProcessor` (QML context property `shotProcessor`) owns the post-shot
pipeline and the SwingWindow lifecycle. States: `Idle → PostRoll → Processing
→ Replaying → Idle`; `busy` (any non-Idle state) disarms `ShotController` for
the duration.

### POSTROLL — `onShotDetected`

Captures the trigger tuple (source, impact µs, session type, wallclock label)
and starts a single-shot timer (`postRollMsFor(source)`, 500 ms for every
source today). The buffer **keeps capturing** so the follow-through lands in
the ring before it freezes.

### Freeze — `captureWindowAndLaunch`

`pauseBuffer()` → `captureSwingWindow(5 s)`. If the user pressed Stop during
the post-roll the rings froze early — still a valid (truncated) shot; only
buffer teardown aborts. One `ReplayTrack` is built per live camera with frames
in the window. Then **both** workers launch:

```cpp
// Both read the SAME frozen window concurrently — const, zero-copy reads
// over stable memory (producers stopped while Paused).
startAnalysis();    // QtConcurrent: makeShotAnalyzer(type)->analyze(*win, job)
startSwingSave();   // QtConcurrent: SwingExporter::run(*win, exportJob)
```

### Job building — `startAnalysis`

All UI-thread reads happen here: camera sources ordered **face-on first** (so
analyzers can prefer it without re-sorting), IMU + marker sources discovered
from the window's own `formatOf()` descriptors, athlete handedness, and the
`ImuSegmentBinding` snapshots (§3). The job then moves into the lambda by
value.

### Join — `maybeJoin`

Runs when **both** outcomes are non-Pending (each watcher calls it; so does
the synchronous export-skip path):

1. **The one unified `swing.json`** is written here, on the GUI thread, after
   both workers returned — so the two concurrent workers never write the same
   file. Export OK → exporter manifest + inline `"analysis"` block. Export
   failed/skipped but analysis OK → a **synthesised minimal manifest**
   (`buildSynthManifest`) so an analysis-only shot still survives a restart.
2. **`ShotListModel::addShot()` always runs** — with whatever the pipeline
   produced (trace/score/metrics empty or 0 on analysis failure;
   `hasVideo=false` on export failure).
3. **Replay gating**: the ¼× replay starts only when analysis AND export both
   succeeded and camera tracks exist; otherwise straight to `finishShot()`.

| analysis | export | swing.json | carousel row | replay |
|---|---|---|---|---|
| OK | OK | manifest + analysis | full | yes |
| OK | failed/skipped | synth manifest + analysis | no video, real score | no |
| failed | OK | manifest only (raw) | video, score 0 | no |
| failed | failed | none (in-memory shot only) | degraded | no |

### Finish

`finishShot()` (or the teardown stop-barrier `finishNowBlocking()` — camera
deselect and both destructors call it *before* any deregister) destroys the
window, then restores the user capture intent; `bufferStateChanged` re-arms
the trigger.

---

## 5. Getting Started — Writing a New Analyzer

The three stub analyzers (Swing, GRF, Coach) are placeholders for exactly this
exercise. To make one real:

```cpp
// ── 1. src/Analysis/grf_analyzer.h ────────────────────────────────────────
#pragma once
#include "shot_analyzer.h"

class GrfAnalyzer : public ShotAnalyzer
{
public:
    ShotAnalysisResult analyze(const pinpoint::SwingWindow &window,
                               const ShotAnalysisJob &job) override;
};

// ── 2. src/Analysis/grf_analyzer.cpp ──────────────────────────────────────
ShotAnalysisResult GrfAnalyzer::analyze(const pinpoint::SwingWindow &window,
                                        const ShotAnalysisJob &job)
{
    ShotAnalysisResult r;

    // (a) Read raw data from the frozen window — zero-copy, const only.
    //     job.imuSources / job.cameraSources / job.markerSourceId name the
    //     sources; window.entriesFor(id) / window.payloadOf(e) /
    //     window.interpolateImu(...) access them. Prefer the shared fuser
    //     when you need anatomical orientation streams:
    const auto streams = pinpoint::analysis::ImuVisionFuser::fuse(window, job.imuBindings);
    if (streams.timeGrid.empty()) {
        r.ok = false;                       // degrade, never crash:
        r.error = QStringLiteral("no usable IMU data in window");
        return r;                           // carousel row with score 0
    }

    // (b) Phases — Impact is job.impactUs, the marker anchor. Reuse the
    //     segmenter if lead-hand motion is meaningful for your type.
    const auto phases = pinpoint::analysis::PhaseSegmenter::segment(streams, job.impactUs);

    // (c) Build MetricSeries on the TimeGrid (absolute µs!), degrees,
    //     Address-referenced. Then score: add a band table for your session
    //     type in swing_scorer.cpp (bandsFor) and call SwingScorer::score.

    // (d) Fill both result tiers (flat mirrors ShotListModel roles):
    auto detail = std::make_shared<pinpoint::analysis::SwingAnalysis>();
    // detail->series / phases / tier / score ...
    r.detail = detail;
    r.score  = detail->score.overall;
    // r.metrics: key → { "label": ..., "value": "display string at Impact" }
    // r.tracePoints: ~24 QPointF normalised 0..1 for the PpTrace sparkline
    r.ok = true;
    return r;
}

// ── 3. Register in the factory (shot_analyzer.cpp) ───────────────────────
case 2:  return std::make_unique<GrfAnalyzer>();     // replaces GrfStubAnalyzer

// ── 4. Root CMakeLists.txt: add grf_analyzer.{h,cpp} to target_sources.
//      If your session type binds IMUs, extend segmentRoleForSlot()
//      (shot_processor.cpp) so placement slots map to SegmentRoles.

// ── 5. Add a pipeline test over a synthetic window/streams to
//      src/Analysis/tests (the pipeline_test.cpp pattern), plus scorer
//      goldens for your band table.
```

The contract in one sentence: **read only `window` (const) and `job` (values),
return in bounded time, and degrade to `ok=false` instead of throwing** — the
join, persistence, carousel, and replay all behave correctly around any
outcome you return.

---

## 6. The M1 Wrist Chain

`WristAnalyzer` (session type 1) is the reference implementation — four pure
stages, each independently testable:

```
ImuVisionFuser::fuse(window, bindings)
   │   200 Hz TimeGrid over common coverage; q_anat = A·q_raw·M per segment;
   │   hold-last for momentary gaps; empty grid when nothing is fusable
   ▼
PhaseSegmenter::segment(streams, job.impactUs)
   │   Impact = the marker anchor (clamped to grid). Address = settle just
   │   before sustained lead-hand motion onset (smoothed quat-derived angular
   │   speed crossing max(15% of peak, 0.3 rad/s)). Top = lead-hand orientation
   │   FURTHEST from Address in (addr, impact] — far more robust than
   │   angular-velocity valley hunting. Finish = grid end.
   ▼
MetricExtractor::extract(streams, phases, job.handedness)
   │   Lead-arm joint angles from RELATIVE quaternions between adjacent
   │   segments (forearm→hand, etc.), Address-referenced, via the swing-twist
   │   decomposition in wrist_angles.h:
   │     leadWristFlexExt  (bow/cup)  — needs forearm + hand
   │     leadWristRadUln   (hinge)    — needs forearm + hand
   │     forearmPronation  (roll)     — needs + LeadUpperArm binding
   │     leadArmFlexion    (IMU elbow)— needs + LeadUpperArm binding
   ▼
SwingScorer::score(series, sessionType)
       per-metric sub-scores against reference bands → weighted geometric mean
```

Two hardware-locked facts to respect (full story in `wrist_angles.h` and
`docs/design/IMU_FRAME_CONTRACT.md` §4–5):

- **Joint DOFs are read on the relative-rotation axes, not the segment-axis
  names**: in the forearm→hand decomposition, flexion/extension is about **Z**
  and radial/ulnar about **X** — deliberately the opposite of the segment
  +X=flexion naming. An earlier "flexion about X" form was wrong; do not
  "restore" it.
- **Relative quaternions cancel heading drift**: the shared (drifting) 6-axis
  yaw appears in both segments' `q_anat` and drops out of `qFore⁻¹·qHand`, so
  no per-shot re-zero is needed. The residual ~10–15° FE↔RUD cross-talk is the
  unobservable *relative* heading between two sensors — a known limitation,
  not a bug to fix in the extractor.

`job.handedness` (1 right / 2 left / 0 unknown) selects lead-arm sign
mirroring; the right-lead (left-handed golfer) case is not yet hardware-
verified.

---

## 7. The Scoring Model

`SwingScorer` (design: `SHOT_ANALYZER_DESIGN.md` §B) is deliberately
transparent and **non-compensatory**:

1. Each metric in the session's band table is read **at its scoring phase**
   (Impact for all current wrist bands) from its `PhaseSample`s.
2. `z = (value − mu) / sigma`, with one-sided bands clamping the *good*
   direction to no-penalty (e.g. extra lead-wrist bow is never penalised;
   cupping is).
3. **Deadband + bounded falloff**: |z| ≤ 1 → 100 (green); ramps down to ~1 at
   |z| = 3 (yellow in between, red beyond). No cliff edges, no negative scores.
4. Sub-scores aggregate by a **weighted geometric mean** into per-region,
   per-phase, and overall scores. Geometric, so it is weakest-link: one severe
   fault cannot be averaged away by three good metrics — the coaching premise.
5. Faults are ranked by `pointsLost = weight × (100 − subScore)`; the full
   `ScoredMetric` audit trail ships in the `ScoreBreakdown`.

The wrist band table (`kWristBands`, swing_scorer.cpp) is **provisional**
pending the sign-lock session; flex/ext carries the highest weight (0.45)
because it drives clubhead speed most (Sweeney), radial/ulnar the lowest
(0.15) because it is the weakest IMU axis. Bands are a versioned table per
session type — adding a session type means adding a `bandsFor()` entry, not
touching the math.

---

## 8. Output Shapes — From Worker to QML

Three views of one result, produced at the join:

| Consumer | Shape | Producer |
|---|---|---|
| Carousel chips | `metrics`: key → `{label, value}` (display string at Impact, e.g. via `wristMetricLabel`) | analyzer (flat tier) |
| Carousel sparkline | `tracePoints`: ~24 normalised `QPointF` (Wrist: lead-wrist FE from Address→Impact, y-up = more flexion) | analyzer (flat tier) |
| Replay graph + review | `analysisDetail`: `{tier, overall, series[], phases[]}` `QVariantMap` | `toAnalysisDetail(*detail)` (shot_processor.cpp) |

`toAnalysisDetail` flattens the `SwingAnalysis` for QML: each series becomes
`{key, label, unit, t_us[], value[], phaseSamples[]}` with **absolute µs**
timestamps — the same domain as `shotProcessor.replayPositionUs`, so the
replay graph scrubs with zero conversion. The identical map shape is stored as
the `ShotListModel` `analysisDetail` role and reconstructed from `swing.json`
on reload — live and persisted shots are indistinguishable to the UI.

---

## 9. Persistence — the Unified swing.json

One document per shot — **raw capture manifest and derived analysis in one
`swing.json`**, no separate analysis file:

- **Writer** (`SwingDocWriter::writeSwingJson`): composes the exporter's
  returned manifest tree with the analyzer's additive `"analysis"` object,
  stamps schema `pinpoint.swing/2`, writes atomically. Called exactly once,
  on the GUI thread, at the join. `analysis == nullptr` → raw-only document.
- **Degraded path**: export failed/skipped but analysis succeeded →
  `buildSynthManifest()` synthesises the header (athlete/session/clock/window,
  empty streams) from the cached export job + live window, so the shot
  reloads as analysis-only (`hasVideo=false`).
- **Review write-through** (`SwingDocWriter::updateReview`): the user's star
  rating and note are merged into an additive `"review"` block via atomic
  rewrite (`QSaveFile`), called from the shot model's setters. A shot whose
  swing.json was never written fails this harmlessly.
- **Reader** (`SwingDocReader::readSwingJson` → `PersistedShot`): rebuilds the
  exact `addShot` shapes — flat metrics, trace, score, `analysisDetail` —
  from disk for app-restart reload and the session-review drawer.

`savedSwingDir` is set only when a swing.json was actually written, so a
carousel row only ever links to a real file; an unwritten shot stays
in-memory-only by design.

---

## 10. Threading and Lifetime Rules

The pipeline's safety reduces to four rules:

1. **Jobs are values, resolved on the UI thread.** Nothing reachable from the
   worker may touch `AppSettings`, controllers, `ImuInstance` (its A/M
   calibration is why `ImuSegmentBinding` exists), `DeviceEnumerator`, or any
   QObject. If a new analyzer needs a new piece of live state, add a field to
   `ShotAnalysisJob` and fill it in `startAnalysis()`.
2. **The window is frozen and shared read-only.** While the buffer is Paused,
   producers cannot write (`slot.valid == false`) and the merger is quiesced —
   so the analyzer and exporter reading the same rings concurrently is safe
   *because both are const*. Never mutate anything reachable from the window.
3. **The window outlives both workers, and only just.** The
   `std::optional<SwingWindow>` storage is stable; the window is destroyed
   only in `finishShot()` (after replay) or `finishNowBlocking()` (which
   **blocks** on both futures first). The processor is declared **after**
   `cameraManager` in main.cpp so it is destroyed first — workers join before
   sources deregister and ring memory frees. This is the same producer/
   stop-barrier discipline as the EventBuffer contract, extended to readers.
4. **One writer per file.** The exporter writes media only and *returns* its
   manifest; the analysis returns values; the single `swing.json` write
   happens at the join on the GUI thread. Workers must not write into
   `swingDir` themselves (the exporter's media files are its own namespace).

Also inherited from hard-won experience: the thumbnail is saved via
`QImage::save()`, **never `cv::imwrite`** — OpenCV's imgcodecs can resolve
libjpeg symbols against the wrong libjpeg generation in this multi-FFmpeg
process and SIGSEGV through libjpeg's error-handler longjmp (observed).

---

## 11. Internals — Design Decisions Explained

### Why analysis and export run concurrently rather than sequentially

Both are pure readers of the same frozen memory, and the export (FFmpeg
encode) dominates wall-clock. Running the analyzer inside that shadow makes
analysis effectively free; the join is two `QFutureWatcher`s and an
outcome-pair check. The price — and it is the load-bearing constraint of the
whole design — is rule 4 above: neither worker may produce side effects that
could collide.

### Why Impact comes from the marker, not the segmenter

The shot detector already spent three modalities pinpointing the impact
instant, and wrote it into the ring as `shot_marker_v1` *on the same
timeline as every sample*. Re-deriving impact from IMU curves inside the
analyzer would be strictly worse (±5 ms sampling at best) and could disagree
with the replay alignment. The segmenter's job is only the phases that have
no marker: Address and Top.

### Why Top is "orientation furthest from Address"

Angular-velocity valley hunting (the classic approach) is fragile on real
swings — short pauses, double-pumps and noise create false valleys. The
backswing apex *defined as* maximum orientation distance from Address is
parameter-free, monotone-robust, and self-confidence-rating (the distance
itself, clamped, is the confidence).

### Why a fixed 200 Hz TimeGrid instead of native sample times

Joint angles need *pairs* of segments at the *same* instant; native timelines
never align. One grid makes every downstream consumer (extractor, scorer,
replay graph, QML) index-parallel, and 200 Hz matches the sensors' top rate so
nothing is invented. Slerp via `interpolateImu` is exact for orientation;
hold-last covers momentary BLE gaps without poisoning the grid.

### Why the geometric mean

An arithmetic mean lets a 100/100/100/10 swing score 78 — "pretty good" with a
catastrophic fault. The weighted geometric mean scores it ~46: faults are
things to fix, not average away. This is a coaching product decision encoded
in math; see SHOT_ANALYZER_DESIGN.md §B for the derivation.

### Why `detail` is a `shared_ptr`

The same `SwingAnalysis` is referenced by the result (worker → watcher), the
join (swing.json write), the model row (`analysisDetail` role) and the replay
binding — all on the GUI thread after the join, but with different lifetimes.
A `shared_ptr` in a registered metatype crosses the future boundary cheaply
and removes any copy/ownership question.

---

## 12. Testing

The standalone suite (own `main()`, CHECK macros, not in the root build):

```bash
cmake -S src/Analysis/tests -B build/analyzer-tests -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.0/gcc_64
cmake --build build/analyzer-tests -j
ctest --test-dir build/analyzer-tests --output-on-failure
```

| Test | Covers |
|---|---|
| `wrist_angles_test` | swing-twist decomposition: axial isolation, magnitude, the 180° singularity, hardware-locked axis assignments |
| `imu_calibration_test` | the A·q·M anatomical solve + end-to-end keystone golden |
| `live_wrist_angles_test` | the live wrist-angle math contract (mirrors `live_wrist_angles.cpp`) |
| `pipeline_test` | **PhaseSegmenter + MetricExtractor over a synthetic swing** — the analyzer-chain test to mirror for new session types |
| `swing_scorer_test` | banded weighted-geometric-mean scorer goldens |
| `imu_sample_test` | the stored IMU sample frame (`makeImuSample`) the fuser reads back |
| `swing_doc_test` | unified swing.json writer/reader round-trip |
| `orientation_filter_test`, `imu_driver_frame_test` | upstream provisioning: fusion filters and the real driver parse path |
| `session_summary_test`, `viz_frame_test`, `arbiter_test` | neighbours sharing the suite (review drawer, viz math, shot-detection arbiter) |

What is deliberately *not* unit-tested here: `ShotProcessor` orchestration
(threading, joins, lifecycle). That is exercised headlessly —
`QT_QPA_PLATFORM=offscreen`, trigger a shot, assert the swing.json and
carousel row — and was the migration target of the window-lifetime guards
(`finishNowBlocking`, `swingWindowLive`). Treat changes there with
correspondingly more care.

---

## 13. Common Mistakes

### Reading live objects from the worker

The compiler will not stop you capturing `m_appSettings` or an `ImuInstance*`
in the analyze lambda — the data race at runtime will. Everything crosses the
boundary inside `ShotAnalysisJob` (values) or the const window. If the job
lacks something you need, extend the job.

### Writing files from an analyzer

The join owns persistence. An analyzer that writes into `swingDir` races the
exporter and breaks the one-writer-per-file invariant that makes the unified
swing.json safe. Return data; let `maybeJoin` write it.

### Throwing (or crashing) instead of degrading

A window with no IMU data, one sample, or no hand sensor is a *normal Tuesday*
— the user forgot to strap a sensor. `ok=false` + `error` is the contract;
the shot still lands on the carousel. `QtConcurrent::run` will happily
propagate an exception into `result()` and take the app down with it.

### Inventing a second impact estimate

`job.impactUs` is the marker anchor — the single source of truth that the
replay, the export window and the metric phases all share. An analyzer that
"refines" impact privately will visibly desynchronise the replay graph from
the video.

### Relative-µs timestamps in MetricSeries

`t_us` is absolute (`nowMicros()` domain). The replay playhead, the phase
events and the swing.json clock block all assume it. If you want
window-relative time for a UI, convert in QML against `replayStartUs`.

### Re-deriving anatomical frames in an analyzer

`q_anat = A·q_raw·M` has exactly one implementation
(`imu_calibration::toAnatomical`, used by the fuser) and the axis/sign
assignments are hardware-locked. Analyzers consume `FusedStreams`; they do not
touch raw quaternions unless they are doing something genuinely new — and then
the new math belongs in a tested header, not inline.

### Skipping the factory

Constructing a concrete analyzer directly bypasses `makeShotAnalyzer`'s
guarantee (never nullptr, correct fallback for unknown/-1 session types) and
the single registration point the processor relies on.

### Blocking the join on replay state (or vice versa)

The shot is on the carousel and saved *before* the replay runs — replay is a
pure presentation tail. `cancelReplay()` (ESC) is just the normal
end-of-replay path taken early; do not attach completion semantics to it.

---

## 14. File Map

```
src/Analysis/
├── shot_analyzer.h             ShotAnalyzer interface, ShotAnalysisJob/Result,
├── shot_analyzer.cpp             makeShotAnalyzer factory + the three stubs
├── swing_analysis.h            All value shapes: SegmentRole, ImuSegmentBinding,
│                                 Phase(+Event/Sample), MetricSeries, ScoredMetric,
│                                 Fault, ScoreBreakdown, SwingAnalysis
├── imu_vision_fuser.{h,cpp}    TimeGrid + q_anat resampling → FusedStreams
├── phase_segmenter.{h,cpp}     Address/Top/Impact/Finish heuristics (M1)
├── metric_extractor.{h,cpp}    Wrist MetricSeries from relative quaternions
├── wrist_angles.h              Swing-twist decomposition (header-only, hardware-locked)
├── swing_scorer.{h,cpp}        Band tables + weighted-geometric-mean scoring
├── wrist_analyzer.{h,cpp}      The real M1 Wrist analyzer (chains the above)
└── tests/                      Standalone CTest suite (build/analyzer-tests)

src/Gui/
├── shot_processor.{h,cpp}      Pipeline orchestration: post-roll, window, jobs,
│                                 join, unified write, replay, stop-barriers
└── shot_list_model.*           Carousel rows: addShot (live) / addPersistedShot (reload)

src/Export/
├── swing_exporter.{h,cpp}      The concurrent media worker (separate guide)
├── swing_doc.{h,cpp}           SwingDocWriter/Reader — the unified swing.json
└── swing_paths.{h,cpp}         Session/swing directory allocation
```

---

*Design rationale and the metric/scoring evidence base: 
`docs/design/SHOT_ANALYZER_DESIGN.md` (architecture + scoring model),
`docs/implementation/SHOT_ANALYZER_M1_WRIST.md` (the M1 wrist chain),
`docs/design/SHOT_ANALYZER_VIZ.md` (replay graph), `docs/reference/WRISTMETRICS.md` (bands),
`docs/design/IMU_FRAME_CONTRACT.md` (frames and joint-DOF axes). Upstream:
`docs/developer/shot_detector_developer_guide.md`; sideways:
`docs/developer/swing_export_developer_guide.md`; underneath:
`docs/developer/event_buffer_developer_guide.md`.*
