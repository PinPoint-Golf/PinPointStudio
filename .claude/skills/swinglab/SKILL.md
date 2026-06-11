---
name: swinglab
description: Run, score, tune, and triage the swing-analysis pipeline against recorded or synthetic swings using the SwingLab harness (lab.py + swinglab_run). Use for soak tests, parameter sweeps, regression checks, and diagnosing shaft-track/segmentation failures on real data.
---

# SwingLab operator recipe

You are operating the SwingLab harness (docs/implementation/SWINGLAB_IMPL.md).
Follow this recipe exactly. **Tier rules at the bottom are binding.**

## Setup facts

- Python: `~/.swinglab-venv/bin/python` (numpy/cv2/matplotlib installed).
- CLI: `cd tools/swinglab && ~/.swinglab-venv/bin/python lab.py <cmd>`.
- Runner binary: `build/Desktop_Qt_6_11_0-Debug/swinglab_run` (rebuild with
  `cmake --build build/Desktop_Qt_6_11_0-Debug --target swinglab_run --parallel 4`
  after C++ changes; configure once with `-DPINPOINT_BUILD_TOOLS=ON`).
- Corpus = a directory of swing dirs (each has swing.json). Real corpus lives
  under `/mnt/swingdata/...` (only use folders the user has blessed); a
  synthetic corpus can always be created fresh with `lab.py synth`.
- Runs land where you point them (use `/tmp/swinglab-runs/...` or a path the
  user gives). NEVER write into the corpus dirs except `truth.json`.

## The loop

1. **Baseline**: `lab.py ingest <corpus>` then
   `lab.py run <corpus> <runs> --id baseline`.
   Read `<runs>/baseline/REPORT.md`. Mean score and per-swing failures.
2. **Triage the worst swings**: for each, read `scorecard.json` (named checks
   with values vs thresholds), look at `contact_sheet.png` (Read tool — you
   can see images), and if needed `trace.jsonl` (per-frame candidates +
   association; last line is the ŝ_hand fit record). Classify each failure:
   - **parametric** — a config threshold plausibly wrong (detector kernel,
     gates, sigma floors). → fixable by params.
   - **data** — bad recording, missing bindings, wrong calibration. → report,
     don't tune around it.
   - **algorithmic** — the method itself fails (wrong association, fit
     refusing for structural reasons). → escalate.
3. **Parametric fixes**: write a params JSON (`{"shaft.ridgeKernelPx": 11}`,
   keys = `seg.* / shaft.* / assembly.*` matching the config struct fields),
   `lab.py run … --id candidate --params p.json`, then ALWAYS
   `lab.py diff <runs>/baseline <runs>/candidate`. Keep the change ONLY if
   mean improves and `regressions: 0`. For broader searches use
   `lab.py sweep <corpus> <runs> space.json --trials N` (mechanical — prefer
   it over hand-iterating more than ~3 times).
4. **Record findings**: write `<runs>/TRIAGE.md` — per swing: failure names,
   classification, evidence (one line each), action taken/proposed.

## Escalation contract (mechanical triggers — not vibes)

Escalate by writing `<runs>/ESCALATION.md` containing: the trigger, the
affected swings, scorecard excerpts, contact-sheet paths, and your hypothesis.
Triggers:
- a sweep plateaus below the target mean for 2 consecutive sweeps
- any single invariant fails on >30% of the corpus
- ≥2 swings classified **algorithmic** with the same failure signature
- any change requires editing C++

## Tier rules (binding)

- **Haiku/operator**: run the loop above. You may edit params JSONs and
  truth.json labels. You may NOT edit C++, QML, or lab.py internals. Cap:
  10 candidate runs per session, then summarize and stop.
- **Sonnet/engineer**: engage from an ESCALATION.md. You may change C++ but
  MUST (a) reproduce the failure in a standalone test or synth variant first,
  (b) rebuild swinglab_run + the app, (c) rerun the corpus and attach the
  diff (0 regressions required) before committing.
- **Opus-class/design**: algorithm/architecture changes, with the design docs
  updated (SHOT_ANALYZER_DESIGN.md addenda pattern).
- Never push without explicit user approval. Diagnostics go to the in-app log
  (ppInfo) in app code; print/log freely in lab tooling.

## Quick reference

```
lab.py synth  <dir> [--clutter]          ground-truthed synthetic swing
lab.py one    <swing> <run_dir>          run+score+plot a single swing
lab.py run    <corpus> <runs> --id X     batch run + REPORT.md
lab.py diff   <runA> <runB>              regression gate (exit 1 = regressions)
lab.py sweep  <corpus> <runs> space.json mechanical param search
lab.py label  <swing>                    hand-label truth (needs display)
swinglab_run <swing> --out <dir> --trace [--params f] [--pose pose.json]
```
