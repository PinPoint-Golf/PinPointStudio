# SwingLab — an offline analysis lab for real-swing development (proposal)

> **Status:** L1–L5 IMPLEMENTED (validated end-to-end on a synthetic corpus —
> real-data missions await the clean recording session) · **Date:** 2026-06-11
>
> As the analysis goes deeper (high-speed swing data, shaft tracking, segmentation),
> assessing correctness by a human watching replays does not scale — and it locks the
> assistant out of the loop entirely. SwingLab makes recorded swings a development
> substrate: re-run the REAL analysis pipeline on them offline, score the output
> against ground truth and physics invariants, render the evidence as plots an LLM
> can read, and drive tune/soak loops with the cheapest model that can do the job.
>
> Seeded first targets: `imuVisionCorr == 0` on real shots (the ŝ_hand IMU channel
> never engaged on 2026-06-11 studio data) and overall shaft-track quality
> ("seems a way off").

## Design principles

1. **Replay the production code, never a reimplementation.** A Python re-write of
   the tracker would drift from the app within a week. The lab's core is a C++
   runner that rebuilds a `SwingWindow` from a recorded swing dir and executes the
   exact `ImuVisionFuser → PhaseSegmenter → PoseRunner → ShaftTracker → metrics`
   pipeline the app runs. Everything downstream (scoring, plots, tuning) is Python.
2. **Make failure legible.** LLMs (and humans) iterate well against small, named,
   numeric verdicts — not against "watch this video". Every run produces a
   per-swing scorecard (JSON) + a contact sheet (PNGs) + an aggregate report (MD).
3. **Parameters change without rebuilds.** All tuning knobs (`ShaftDetectConfig`,
   `AssemblyConfig`, `SegmentationConfig`) are injectable from a JSON file. A
   small model — or a dumb grid search — can iterate at binary speed.
4. **Don't use a model where a for-loop works.** Parameter sweeps are mechanical
   (scripted hill-climb/grid — zero tokens). The cheap model is the *operator*
   (run, read, triage, summarize); bigger models are *engineers* (code changes),
   reached by an explicit escalation contract.
5. **Every scorecard is attributable**: git SHA + params hash + corpus manifest
   hash, so "regression in swing_0007" always answers *what changed*.

## What a recorded swing already gives us

A PinPointStudio swing dir is self-describing (verified against the exporter):

| Artifact | Contents | Use |
|---|---|---|
| `swing.json` `streams[]` | per-camera pixelFormat/dims/stride, per-frame `t_us[]`, full-window IMU samples (raw sensor-frame vectors + fused quat, per-sample timestamps) | reconstruct the window |
| `<alias>.raw` (with `saveRawFrames` ON) | the undecoded sensor payloads, one frame per encoded frame, `stride`/`frameBytes` recorded | **bit-faithful** camera replay — same bytes the analyzer saw |
| `<alias>.mp4` | h264 of the full window | replay fallback when raw is off (re-encode pixels ≈ source; flagged in scorecards) |
| `analysis` block | phases + conf + provenance, segmentation bounds, pose2d, club track, metrics | the app's answer at record time — regression baseline |

**Recording protocol (the user's side — cheap by design):** turn ON
`saveRawFrames` + IMU streams, then just hit balls. Target corpus v1: ~20–30
swings across 2–3 clubs (driver/mid-iron/wedge), full + half swings, a couple
with deliberate waggle/regrip, one or two intentional torture cases (alignment
stick in frame, busy background). Copy swing dirs to a corpus root
(`~/SwingData/corpus-v1/`, outside the repo); `lab.py ingest` builds a manifest
(`corpus.json`: swing list, hashes, tags like `club=7i half=yes raw=yes`).

## Components

### 1. `swinglab_run` — the C++ offline runner (the cornerstone)

A CLI built inside the existing app build (`-DPINPOINT_BUILD_TOOLS=ON` — it needs
the same OpenCV/ORT/FFmpeg machinery the app already configures):

```
swinglab_run <swing_dir> --out <run_dir> [--params params.json] [--trace]
```

- Rebuilds an `EventBuffer` + `SwingWindow`: registers camera/IMU sources from
  the stream metadata, writes payloads (raw sidecar bytes, else decoded MP4
  frames as BGR24) and IMU samples at their **original timestamps**, pauses,
  captures the window.
- Resolves a `ShotAnalysisJob` from the persisted manifest (impact from the
  recorded Impact phase / marker entry; handedness, bindings from stream info)
  and runs `makeShotAnalyzer` — the production pipeline, unmodified.
- `--params`: overrides for the three config structs, threaded through a new
  dev-only `AnalysisTuning` value member on `ShotAnalysisJob` (default = today's
  constants; production callers never set it).
- `--trace`: per-frame internals as JSONL — shaft candidates (θ/σ/score/L per
  candidate), Viterbi selection, ŝ_hand fit (directions tried, residual, sign,
  δ), KF priors/posteriors, segmentation detector signals (envelope, plane
  rate, stillness mask). Gated by a job field; zero cost in production.
- Emits `result.json` (the SwingAnalysis serialized exactly like swing.json's
  analysis block) + `trace.jsonl` + run metadata (SHA, params hash, wall time).

### 2. `lab.py` — the Python evaluation layer (numpy/matplotlib/opencv)

One entry point, subcommands; all outputs file-based so any model can consume:

- `lab.py run [--corpus all|tag] [--params X]` — batch swinglab_run, collect.
- `lab.py score` — per-swing **scorecard.json**:
  - **Tier 1 — physics invariants (no labels needed; the soak-test backbone).**
    The "expected tracking shape" encoded as named checks with margins, e.g.:
    θ(t) monotone through the downswing and total sweep in [220°, 340°]; θ̇
    profile unimodal with peak within 60 ms of impact; no sample-to-sample θ
    jump > 25° outside `Coasted` spans; L(t) smooth, foreshortening dip only
    near Top; head-point speed continuous (no >150 px teleports); grip point
    inside the torso band from pose2d; coverage ≥ gate; tempo ratio in [1.5, 5].
    Each check → pass/fail + a magnitude, rolled into a 0–100 track score.
  - **Tier 2 — cross-modal consistency.** vision↔IMU θ̇ correlation (the >0.9
    acceptance), impact-time agreement (vision head-at-lowest vs marker vs
    acoustic when present), segmentation events vs IMU energy envelope.
  - **Tier 3 — hand labels where they exist.** θ/endpoint RMS vs labelled
    frames (slow vs blur frames reported separately), P-position timing error.
- `lab.py plots` — the contact sheet per swing: track overlay rendered on 6 key
  frames (P1/P2/P4/impact±1), θ(t) + θ̇(t) vs IMU-predicted, L(t), candidate
  cloud vs chosen path, segmentation ladder over the envelope. PNGs at stable
  paths — **this is how the assistant "watches" a swing**.
- `lab.py label <swing>` — minimal OpenCV click-UI: step decoded frames (t_us
  from swing.json), click grip→head (~12–15 frames/swing, ~2 min), arrow-mark
  P1/P2/P4/P7/P8/P10 on a scrubber. Writes `truth.json` into the swing dir.
- `lab.py sweep --target <metric> --space sweep.json` — mechanical grid/random
  search over params, hill-climbing the aggregate score. No LLM involved.
- `lab.py report` / `lab.py diff <runA> <runB>` — aggregate MD table; per-swing
  regression diff ("swing_0007 θRMS 1.2°→4.5° ⬇ REGRESSION") — the gate that
  makes soak loops safe.

### 3. The model loop — operator/engineer split with explicit escalation

Encoded as a Claude skill (`.claude/skills/swinglab/` — local-only, NOT
committed; Claude-specific artefacts stay out of the repo) so any model
follows the same recipe:

```
TIER 0 (no model)   lab.py sweep — parameter search is a loop, not a prompt.
                    random | coordinate-descent over space.json, with the per-swing
                    regression gate (--baseline) and Tune/Validation/Held-out partition
                    (--partition / --freeze) enforced in-loop (see pipeline_validation_and_tuning §7.1).
TIER 1 (Haiku)      Operator loop: lab.py run → score → report; triage worst
                    swings from scorecards + contact sheets; classify each
                    failure against a rubric (parametric? data? algorithmic?);
                    file findings to runs/<id>/TRIAGE.md. May edit params.json
                    and re-run. NEVER edits C++.
TIER 2 (Sonnet)     Engineer loop: invoked when triage says "algorithmic" or a
                    sweep plateaus below target. Reads trace.jsonl for the
                    failing swings, changes code, must add/extend a standalone
                    test reproducing the failure, reruns the corpus + diff
                    before committing.
TIER 3 (Opus/Fable) Design changes (new detector stages, model upgrades) — via
                    a written ESCALATION.md containing the evidence trail.
```

Escalation triggers are mechanical, not vibes: score plateau over N sweep
rounds, any Tier-1 invariant failing on >30% of corpus, regression diff after a
code change, or triage classifying ≥2 swings as the same algorithmic failure.
Practically this runs as: the user (or a cron) opens a cheap-model session with
`/swinglab soak`; artifacts accumulate in `runs/`; a bigger-model session picks
up `ESCALATION.md` when one appears. The same artifacts serve interactive
sessions like this one — I read the contact sheets directly.

## Repo layout

```
tools/swinglab/
  src/swinglab_run.cpp          # C++ runner (built via -DPINPOINT_BUILD_TOOLS=ON)
  lab.py + swinglab/*.py        # ingest, score, plots, label, sweep, report, diff
  configs/                      # params.json presets + sweep spaces
.claude/skills/swinglab/SKILL.md       # local-only (gitignored), per host
docs/implementation/swinglab_impl.md   # this doc
~/SwingData/corpus-v1/...              # data lives OUTSIDE the repo
runs/ (gitignored)                     # scorecards, plots, traces per run
```

## As built (usage)

```bash
# one-time: configure the tool target, build it
cmake -S . -B build/Desktop_Qt_6_11_0-Debug -DPINPOINT_BUILD_TOOLS=ON
cmake --build build/Desktop_Qt_6_11_0-Debug --target swinglab_run --parallel 4

cd tools/swinglab
P=~/.swinglab-venv/bin/python          # numpy / opencv / matplotlib venv

$P lab.py synth /tmp/corpus/synth_0001            # ground-truthed synthetic swing
$P lab.py one   /tmp/corpus/synth_0001 /tmp/runs/x  # run+score+contact sheet
$P lab.py ingest /path/to/corpus                  # corpus.json manifest
$P lab.py run    /path/to/corpus /tmp/runs --id baseline [--params p.json]
$P lab.py diff   /tmp/runs/baseline /tmp/runs/candidate   # exit 1 = regressions
$P lab.py sweep  /path/to/corpus /tmp/runs space.json --trials 20 \
                 [--method random|coordinate] [--baseline /tmp/runs/baseline] \
                 [--partition partitions.json] [--freeze]   # gate + Tune/Val/Held-out in-loop
$P lab.py label  /path/to/swing                   # truth.json click-UI (needs display)
```

Validation evidence (synthetic corpus, no real data touched):
- full pipeline E2E score **100/100** — 16 named checks including
  `imuVisionCorr 0.986` (the ŝ_hand fit engaged with the closed-form-predicted
  `sign=−1`), θ RMS vs truth **0.2°**, head median error **2.6 px**, Top
  within **7 ms** of ground truth;
- clutter variant (alignment stick) passes; deliberately broken params
  (`ridgeKernelPx 3`, absurd threshold) drop the corpus to 33/100 and
  `lab.py diff` flags 3/3 regressions with a non-zero exit;
- the production additions are inert in the app: `tuningOverrides` /
  `poseTrackPath` empty, trace sinks null, `analysis.bindings` additive.

Production hooks added for the lab (all additive, app behaviour unchanged):
`ShotAnalysisJob::tuningOverrides` (dotted keys applied onto
`SegmentationConfig` / `ShaftDetectConfig` / `AssemblyConfig` at analysis
time), `ShotAnalysisJob::poseTrackPath` (+ `PoseRunner::loadFromJson`),
optional trace out-params on `ShaftTracker::track` / assembly, and
`analysis.bindings` (serial-keyed A/M calibration snapshot) persisted in
swing.json so recorded swings are re-fusable offline.

### Capture provenance in swing.json (2026-06)

The app now records capture metadata the lab consumes (all additive — legacy
swings without the fields keep the old behaviour; see
`docs/developer/swing_export_developer_guide.md` §6 for the full schema):

- **Top-level `capture`** — sessionType, shotSource, detection sensitivity,
  back-dating latencies, host provenance (app version/git sha, hostname,
  platform, pose backend). `swinglab_run` takes `sessionType` from here when
  `--session-type` isn't passed, and echoes the whole block into runmeta.json.
- **Video `setup`** — recorded perspective. Face-on selection now uses
  `setup.perspective == 2` instead of the `--face-on` alias substring guess
  (an explicit `--face-on` still wins; legacy swings fall back to substring).
  Since B5 the block also carries `ballDetection` (`calibrated`, `margin`,
  `driftAtCapture`, `calibratedAt`) — the calibrated ball detector's state at
  capture time.
- **IMU `device`** — `outputRateHz` replaces the hardcoded 200 Hz registration
  (camera fps likewise comes from the stream's `capture.fps_num/den` instead
  of the hardcoded 150).
- **`analysis.bindings[]` calibration status** — `calibrated` (composite mount
  gate), gate angles, `calibratedAt`/`calibAgeSec`. The runner warns on
  uncalibrated bindings; runmeta.json carries `calibrated: true|false|null`
  (null = legacy, field absent). `lab.py ingest` surfaces
  `sessionType / shotSource / calibrated / calibAgeSec / perspectives /
  appVersion / ballCalibrated / ballMargin` per swing in corpus.json so a
  corpus can be filtered before tuning (ball fields null for pre-B5 swings).

The operator/engineer model contract is encoded in
`.claude/skills/swinglab/SKILL.md` (`/swinglab`).

## Stages

| Stage | Deliverable | Size |
|---|---|---|
| L0 | Recording protocol agreed; corpus v1 recorded (user) + `ingest` manifest | user time |
| L1 ✓ | `swinglab_run`: window reconstruction + production pipeline + `result.json`; tuning injection; `--trace`; `--pose` | M (the unlock) |
| L2 ✓ | `score` (Tier-1 invariants + Tier-2 cross-modal) + `plots` contact sheets + `report` | M |
| L3 ✓ | `label` tool + truth.json + Tier-3 metrics (+ `synth` ground-truth generator) | S |
| L4 ✓ | `sweep` + `diff` regression gate | S |
| L5 ✓ | `/swinglab` skill + escalation contract (first real soak awaits corpus v1) | S |

L1+L2 alone already transform the workflow: the imuVisionCorr=0 investigation
becomes "run swing_0007 with --trace, read the ŝ_hand fit record, see why it
refused" instead of speculating from app logs.

## Multi-host operation (Windows studio PC + Linux dev box)

The Windows host (i9 Ultra / 32 GB / RTX 5080, idle outside simulator
sessions) is the preferred engine for batch runs and sweeps: the pose pass —
the dominant cost — runs on CUDA there, and the corpus is on local NVMe
instead of SMB. The design needs no structural change for this because the
interface is files in, files out:

- **The SwingData share is the artifact exchange medium.** Corpus and runs
  both live on it (Windows `C:\Users\developer\Data\PinPointStudio\…` ≡ Linux `/mnt/swingdata/…`), so
  scorecards, contact sheets, TRIAGE.md and ESCALATION.md produced on one
  host are immediately readable on the other — operator sessions on the
  Windows box and design sessions on the dev box share state with zero sync
  machinery. Remote execution needs nothing new either: with OpenSSH enabled
  on Windows, `ssh studio "… lab.py run …"` works as-is.
- **Context-less hosts bootstrap from the repo alone**: `lab.py doctor`
  verifies binary + deps and prints the conventions; `requirements.txt` +
  `-DPINPOINT_BUILD_TOOLS=ON` are the whole setup. The lab tooling and this
  doc are in-repo and travel with git. Exception: the `/swinglab` skill
  (`.claude/skills/swinglab/SKILL.md`) is local-only — `.claude/` is
  gitignored (Claude artefacts stay out of the repo) — so a new operator
  host needs it copied over once (e.g. via the SwingData share).
- **Data trust travels with the data, not with a machine**: a corpus root
  must contain a `CORPUS.md` stating recording date and calibration
  provenance; `ingest` marks the manifest `blessed` only then. (This encodes
  the 2026-06-11 decision that pre-corpus-v1 studio recordings are
  unreliable.)
- **Cross-host runs are attributable, not comparable**: runmeta.json records
  host + platform; CPU and CUDA pose outputs differ subtly, so the diff gate
  compares same-host runs only.
- Windows bring-up checklist (first session there): pull, configure with
  `-DPINPOINT_BUILD_TOOLS=ON`, build `swinglab_run`, `py -m venv` +
  `pip install -r tools/swinglab/requirements.txt`, `lab.py doctor`, then
  `lab.py synth` + `lab.py one` as the acceptance test (expect 100/100).

## Risks & honest limits

- **MP4-only swings are approximate** (re-encoded pixels). Mitigation: protocol
  says raw ON; scorecards carry a `source: raw|mp4` flag; tuning conclusions
  drawn only from raw swings.
- **Invariants can overfit to "typical" swings** — a deliberately weird swing
  (punch shot) may flag falsely. Tags in corpus.json scope which invariants
  apply; labels arbitrate disputes.
- **Tier-1 model discipline**: small models follow recipes well but can loop
  unproductively; the skill caps iterations and *requires* the regression diff
  before any params change is kept.
- **ViTPose nondeterminism across GPUs/EPs** is possible (CUDA vs CPU). The
  scorecard records the EP; cross-machine diffs compare against same-EP
  baselines only.
- The runner needs ~the app's build deps — that's why it lives inside the app
  build rather than the standalone test suites.

## First missions (after L2)

1. **Why is `imuVisionCorr` 0 on real shots?** Trace the ŝ_hand fit on studio
   swings: eligible-frame count, span, residual vs gate. Likely suspects: the
   LeadHand binding not present in the job, eligibility too strict for real
   waggle, or the fit residual gate.
2. **Shaft-track quality baseline**: invariant scores across corpus v1, worst-5
   contact sheets → triage into detector vs assembly vs anchor failures; then
   the first sweep (`ridgeKernelPx`, NMS separation, σ floors) with labels on
   5–10 swings as the arbiter.
