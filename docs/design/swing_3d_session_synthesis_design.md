# 3-D swing panel — the session's usual swing

*2026-11-09. Status: **shipped; results in §8; Mark's six decisions are in §2.** Follows
`swing_3d_viz_design.md` (the panel, `skeleton3d`) and `swing_3d_annotations_design.md` (the
annotations and the fused plane).*

## 0. Summary

The 3-D swing panel gets a **Session / This shot** toggle, defaulting to Session. Session view draws
one figure synthesised from the session's swings with the selected shot's club, weighted toward the
golfer's usual pattern. It is worked out when the panel asks for it, only for a completed session.

It is **display only**. Nothing changes in the analysis, the swing files or any metric, no swing is
re-analysed and no stage version moves. Every `skeleton3d` use other than this panel is untouched.

The synthesis is built into the same `SwingRigDriver::Prepared` the panel already draws, so the
figure, the club, the traces and the P-marks work with no special cases.

## 1. Deliverables

| | What | Where |
|---|---|---|
| Document | This note, with §3 and §8 regenerated from the tool below | `docs/design/swing_3d_session_synthesis_design.md` |
| Evidence tool | The re-runnable prototype; the recipe's numeric reference | `tools/swinglab/session_synth/` |
| Implementation | The synthesis, the driver's session mode, the panel toggle | Files in §6 |

**Done means** every gate in §7 has been run and its result written into §8. A gate that fails and is
written up honestly counts as complete. A skipped gate does not.

## 2. Decisions (Mark, 9 Nov 2026)

1. **Which shots:** only those with the same club as the selected shot.
2. **Orbit:** allowed when at least 5 of the shots used have a fused downswing plane; otherwise
   face-on only, as the panel does for a single shot.
3. **The club after impact:** averaged from the shots with a confirmed plane only.
4. **Tempo:** locked to the selected shot's replay, so the figure reaches the top when the video does.
5. **Availability:** completed sessions only, in Replay and Analyse, from at least 3 usable shots, with
   a "from N shots" chip. Below 3, the panel shows the shot and says why.
6. **The two "stand where the camera stood" views:** hidden in session view.

## 3. What the prototype measured

One session: 8 Oct 2026, 7 iron, 32 swings, 31 usable (`swing_0032` has no P-positions), 9 with a
fused plane. Regenerate with:

```
tools/swinglab/session_synth/run.sh 2026-10-08_Mark-Liversedge_Wrist_01
```

The approved recipe (§4) against the session's single swings:

| Check | Single swings | Synthesis |
|---|---|---|
| Head wobble, 2nd difference (mm per grid step²) | 0.60 median | 0.20 |
| Lead hand wobble, 2nd difference | 1.77 median | 1.44 |
| Lead hand to grip | 0.6 mm median | 0 by construction |
| Trail wrist to shaft line | 75 mm median, 82 p90 | 71 mm median, 88 p90 |
| Clubhead to ball at impact | 2.9 cm median | 1.9 cm |
| Peak clubhead speed, P4 to impact | 35.1 m/s (SD 3.0) | 32.7 m/s |
| Shoulder turn at top | 104.8° (SD 14.5) | 102.1° |
| Pelvis turn at top | 45.6° (SD 3.3) | 45.6° |
| Pelvis turn at impact | −3.9° (SD 4.3) | −4.7° |

- With ball position removed, shots differ by 1.2–2.0 cm at the pelvis, 2.7–3.7 cm at the head and
  2.6–5.1 cm at the lead hand. The synthesis is 2.1 cm from the most typical real swing.
- The weights run 0.45–0.84 (effective N 30.3 of 31), and no shot stands out. Weighted and plain means
  differ by 0.09° median and 2.35° at most, so here the weighting is protection against a bad fit
  rather than something that changes the picture.
- The synthesis is slightly soft at speed, about 7 % on peak clubhead speed. No number may be read off
  it, and the panel shows none beyond the plane angle.
- The club's shaft step per grid frame peaks at 2.61° before impact and 3.12° after, and is 1.00° at
  impact, so the switch to plane-only swings shows no jump.

What shaped the recipe:

| Finding | Evidence | Consequence |
|---|---|---|
| Ball position varies between shots | Pelvis offset SD 6.8 cm along the stance; two shots 15 and 21 cm off | Weights ignore it |
| Averaged joints pull the hand off the grip | Median 5 mm, p90 18 mm, max 38 mm | Club attached to the averaged hand |
| P8 is unreliable | 75–412 ms after impact | Align P1–P7, then a fixed 90 ms |
| P1, P4, P7 alone align worse | Lead hand 4.2–7.7 cm against 2.6–5.1 cm | Use all of P1–P7 |
| The club after impact scatters | Shaft 41° median; plane swings 17°, others 37° | Decision 3 |
| Plane and no-plane swings agree before impact | Group medians within 0.9–5.1° | All swings count up to impact |

**Limits.** One session measured. Errors shared by every swing, such as the camera model, do not
average out. A session with no down-the-line camera has no skeletons at all, and the panel keeps
today's refusal message.

## 4. The recipe

**Eligible shots.** From `SwingDocReader::findSwingDirs(sessionDir)`, a shot counts if it has a valid
`skeleton3d` with `dtl`, all of P1–P7 present and in time order, the same `review.club` as the selected
shot (exact match; no label matches no label), and the session's most common `skeleton3d` stage
version among those, so fits from different solvers are never mixed. Fewer than 3: no synthesis.

**Scene frame.** Each shot goes into the driver's existing scene frame: rotated by its own stance yaw,
origin at its own `displayOrigin`.

**Alignment.** Each shot is resampled piecewise-linearly between its own P1–P7, 40 samples per segment,
then 40 more over a fixed 90 ms after P7. The canonical segment lengths are the eligible shots' median
durations.

**Weights.** One weight per shot, from the shape of its swing only:
1. Remove the shot's mean pelvis position over the grid.
2. `d` = RMS distance over all joints and grid frames to the per-frame median of all shots.
3. `w = 1 / (1 + (d / (1.5 · median d))²)`.

Every eligible shot is in; an unusual one counts for less.

**The figure.**
- Joint DoFs (every DoF but the root's): weighted mean. Hinges stay hinges.
- Root position: weighted mean in the scene frame. Root rotation: hemisphere-aligned weighted
  quaternion sum, normalised. No Eigen in this header.
- Bone-length scales and club length: per-group median. Grip offset: median.
- Tier per joint and frame, and the shaft's tier: median across shots.
- Ball: median scene position.

**The club.**
- Grip point = the averaged figure's lead hand plus the median grip offset, from forward kinematics.
  Lead hand comes from `athlete.handedness`.
- Shaft direction = weighted mean of unit vectors, renormalised. Up to impact, all eligible shots.
  Over the 10 ms after impact, shots with no plane fade to zero weight; after that, only plane shots.
- Fewer than 3 plane shots: all eligible shots stay in after impact, with a caveat chip (§5.3).
- The existing 20 ms display stabiliser then runs as it does for a single shot.

**The plane.** With at least 5 plane shots: the normal is the weighted mean of each plane shot's
scene-frame normal, the angle is their median, and the quad goes through the synthesised clubhead at
impact. Otherwise there is no plane, and `faceOnOnly` follows from the existing rule.

**P-marks.** P1–P7 at the canonical times, P8 at the end of the fixed 90 ms. Each mark's source is the
majority of the shots used.

**Fitted cameras.** The synthesis has none, so `cameraAvailable()` is false and the existing preset
code drops both camera views (decision 6).

## 5. Where it goes

### 5.1 Reading the session

- `SwingStore::loadParts(swingDir, paths)`: the existing metrics-path logic in `swing_store.cpp`,
  generalised to a list of paths, so only `/analysis/skeleton3d`, `/analysis/club/positions` and
  `/analysis/club3d/planes` are read, plus the root's `review`, `athlete` and `clock`. A JSON-era
  document falls back to `load()`.
- It runs in one background task, with the same generation guard as `startLoad()`.
- The finished synthesis is held in memory per session and club, with a table of every shot's club and
  P-times. Each read's fingerprint is the shots' `SwingStore::info` size and mtime, checked when the
  session or the toggle changes; `SessionMode.swingTrashed` also drops it.

### 5.2 The driver

- `skeleton3d_rig.h`: `forwardKinematics` is split into its root part and `forwardChain()` (everything
  below the root). The solver's behaviour is identical; the synthesis calls `forwardChain()` with its
  own root.
- `prepare()` is split into a per-frame `appendFrame()` and a final `finishPrepared()` (the end trim and
  the club stabiliser). Single shots call them with their `SceneFrame`, the synthesis with the identity.
- **Tempo by retiming.** The canonical `Prepared` is copied with its times mapped through the selected
  shot's own P1–P7: `t`, `annot.t`, `addressUs`, `impactUs`, `endUs`, `annot.finishUs` and `positions`.
  Nothing downstream of `Prepared` changes. Changing the selected shot within the club is this copy, on
  the GUI thread, with no file read.
- A selected shot missing some P-times keeps the known ones and fills the rest from canonical
  durations; with none, canonical time starts at 0.
- New on the driver: `sessionScope` and `panelShown` (write), `sessionShots`, `sessionPlaneShots`,
  `sessionFallback` (the words when below 3), `clubAfterImpactUncertain`, `invalidateSession()`, and
  `loadSessionNow()` for tests. With `panelShown` false, a session load waits for the panel to return.

### 5.3 The panel

- `SwingViz3DView.qml`: `property string scope: "session"` (not persisted: every launch starts on
  Session), and `property bool sessionAvailable`. `drv.sessionScope` is their conjunction.
- The toggle is a `PpSegmentedControl` (`solid: false`, the same as the presets) under the preset row
  at its left, visible when `sessionAvailable`. **No design-system change.**
- Chips, all with the existing local `Chip` (PpChip):
  - `Session · from %1 shots`
  - `face-on only: %1 of %2 shots have a plane`, replacing the single-shot wording in session view
  - caveat `club after impact uncertain`, when `clubAfterImpactUncertain`
  - caveat with `sessionFallback`, e.g. `Session needs 3 shots in 3-D · showing this shot`
- Changing scope while on a camera preset applies Face-on.
- `SwingViz3DHost.qml` passes `sessionAvailable` and `panelShown` (it has a slot); `ScreenWrist.qml`
  binds `sessionAvailable` to `sessionReviewController.reviewActive`. So a session open from the library
  counts as completed, Capture can never show it, and nothing is worked out until the panel is on show.
- With no selected shot the panel shows today's empty state, since the club comes from that shot.

## 6. Build order

Each step's files. No step waits for a judgement mid-way; the gates are in §7.

1. **FK split.** Modified: `src/Skeleton3d/skeleton3d_rig.h`.
2. **Narrow read.** Modified: `src/Export/swing_store.h`, `src/Export/swing_store.cpp`.
3. **Synthesis.** New: `src/Skeleton3d/skeleton3d_synthesis.h` (namespace `pinpoint::skeleton3d`; types
   `SynthSwing`, `SynthResult`, `synthesiseSession()`, about 300 lines; grep all of `src` for each new
   name first), `src/Analysis/tests/skeleton3d_synthesis_test.cpp`. Modified:
   `src/Analysis/tests/CMakeLists.txt`.
4. **Driver.** Modified: `src/Gui/swing3d/swing_rig_driver.h`, `src/Gui/swing3d/swing_rig_driver.cpp`
   (about 100 lines), `src/Gui/tests/swing_rig_driver_test.cpp`.
5. **Panel.** Modified: `src/Gui/swing3d/SwingViz3DView.qml`, `src/Gui/swing3d/SwingViz3DHost.qml`,
   `src/Gui/session/ScreenWrist.qml` (about 30 lines). Any agent brief for this step carries the
   CLAUDE.md design-system rule and reads `docs/design/system/README.md` and `PpSegmentedControl.md`.
6. **Probe.** New: `tools/probes/swing3d_session.qml`. No `tst_*.qml` and no `qml_ui` test is added.
7. **Record.** Modified: this note's §8. The track reader gained `gripOffsetLocal` and `stageVersion`
   (`src/Skeleton3d/skeleton3d_json.h`).

## 7. Gates

Builds at most three, bounded `-j`. Tests through `ctest`, affected targets once. Probes run offscreen,
where View3D draws nothing: Mark judges how the figure looks in the app himself (his call, 9 Nov 2026,
in place of review sheets).

| # | Gate | Pass |
|---|---|---|
| G1 | FK split | `skeleton3d_test` green, and FK output bit-identical on a fixed pose in the new test |
| G2 | `skeleton3d_synthesis_test` | Identical swings return that swing; time-stretched copies give the same pose at the same phase; a copy with a shifted ball gets its twin's weight; a wild swing is down-weighted; lead-hand gap is zero; after impact only plane shots count; below 3 eligible is refused with a reason; other clubs, missing P-times and minority stage versions are excluded; below 3 plane shots sets the uncertainty flag; below 5 gives no plane |
| G3 | Against the prototype, 8 Oct | 31 shots, 9 plane shots; clubhead to ball at impact, before the display stabiliser, 1.9 ± 0.5 cm; `faceOnOnly` false |
| G4 | `swing_rig_driver_test` | Retiming puts the canonical P-poses at the selected shot's P-times; a focus change within the club reads no file; below 3 falls back and says why; session view reports no fitted cameras |
| G5 | Panel probe, offscreen | The toggle defaults to Session and works both ways; the screen's host offers it only with a session open from the library; a change made while the panel is hidden waits for the panel |
| G6 | First-open time, 7 Oct (50 shots), Mac over the share | At most 5 s, off the GUI thread |
| G7 | Design system | `design_system_lint_test` and `design_system_tokens_check` green, no allowlist line added |

Mark judges the result in the app before any commit. The full GUI suite runs only if he asks.

## 8. Results

9 Nov 2026, macOS Debug build. Probe commands are in the header of `tools/probes/swing3d_session.qml`.

| # | Result | Evidence |
|---|---|---|
| G1 | **Pass** | `skeleton3d_test` green; FK bit-identical in `skeleton3d_synthesis_test` |
| G2 | **Pass** | `skeleton3d_synthesis_test`, every case in §7 |
| G3 | **Pass** | 31 shots, 9 plane shots, weights 0.46 / 0.69 / 0.84 and effective N 30.3 (prototype 0.45 / 0.69 / 0.84, 30.3); clubhead to ball 1.87 cm before the stabiliser (prototype 1.9); `faceOnOnly` false |
| G4 | **Pass** | `swing_rig_driver_test`, and on the real session another shot's P4 moved with no file read, in under 1 ms |
| G5 | **Pass** | Probe on 8 Oct and 7 Oct |
| G6 | **Pass** | First open 0.65 s for 8 Oct (32 swings) and 0.95 s for 7 Oct (50 swings), against a 5 s limit |
| G7 | **Pass** | `design_system_lint_test`, `design_system_tokens_check`; no allowlist line |

Other affected tests green: `swing_store_test`, `swing_annotation_geometry_test`.

**Through the panel, the clubhead reads further from the ball than G3's number.** The existing 20 ms
display stabiliser runs after the synthesis, as it does for one shot. 8 Oct: 5.8 cm for the synthesis,
4.3–12.8 cm (median 6.7) for nine single shots through the same panel. 7 Oct: 6.5 cm for the synthesis.

**7 Oct, a second session:** 50 shots, 33 with a plane, a plane angle of 62.0°, no warnings. Only G3's
counts were compared to a prototype, and that on 8 Oct alone.

**Seen in the app (Mark, 9 Nov 2026):** better than the single shot, so it stays, but still not great.
Errors shared by every swing do not average out (§3, Limits), and the hope is that camera calibration
helps there.
