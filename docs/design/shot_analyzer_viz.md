# Shot-analyzer metric visualization plan

Three M1 items, all one pipeline — the analyzer's per-metric time series rendered on
screen — done in layers: **data → component → placement**. (Reload-related persistence
items — rating/note, MP4 replay, export-fail header — are a separate later set.)

## Decisions (locked 2026-06)
- **Overlay** all metrics on one graph (shared degree axis), **chips filter** which are shown.
- **All four** metrics in the default view (bow/cup · hinge · roll · elbow), filterable.
- **Scalar** Δ-from-address (`value − value@Address`); the exact quaternion-referenced Δ
  curve is deferred.

## Phase 1 — Dual reference (data foundation)
Today the shot series is **address-relative** (`wristRel = addr⁻¹·(f⁻¹·h)`, ~0 at address)
while the live overlay is **neutral-relative** (`f⁻¹·h`, absolute). Unify on **neutral/
absolute as primary**:
- `MetricExtractor` computes the neutral series directly (drops the address referencing) —
  identical reference to `LiveWristAngles`.
- The Address phase-sample is already recorded, so **Δ-from-address = value − value@Address**
  (scalar) is derived in the UI.
- **Fixes the scorer**: it grades `phaseValue(Impact)`, which becomes the **absolute impact
  posture** vs the absolute bands (mu = "15° bowed at impact") instead of the address-delta.
- The carousel flat value also becomes the absolute impact posture.
- `analysisDetail` shape unchanged. `wristRel`/`elbowRel` kept in `wrist_angles.h` for the
  deferred exact-Δ work.

## Phase 2 — Multi-metric `PpMetricGraph`
- Extend from one series to **N** (`seriesList`): colour-coded curves on the shared degree
  axis, keep phase ticks + playhead.
- **Filter chips** (one per metric) toggle each curve; all on by default.
- Playhead readout: **absolute value + "Δ from address"** at the cursor (and at Top/Impact).
- `PpShotPanel` passes the full metric set; the graph owns selection.

## Phase 3 — In-replay graph in `ScreenWrist` (scrub-with-video)
- A `PpMetricGraph` in `ScreenWrist`'s reserved area, visible while
  `shotProcessor.isReplaying`, bound to the **latest shot's** `analysisDetail` (shotModel
  row 0) + `replayPositionUs` / `replayStart`/`EndUs`. Reuses Phase 2; the playhead scrubs
  in lockstep with the ¼× video.

## Sequencing
Phase 1 (foundation + scorer fix) → Phase 2 (shared component) → Phase 3 (placement). Each
independently committable. Phase 1 shifts curves/scores to absolute — worth a quick hardware
sanity-check after.

## Status
- [x] Phase 1 — neutral-relative primary + scalar Δ
- [x] Phase 2 — multi-metric overlay + filter chips
- [x] Phase 3 — in-replay graph in ScreenWrist (live render needs a real shot)
