# Shaft-tracker validation & tuning — standalone, IMU-independent

**Document type:** V&V&T protocol (one stage, decoupled from the IMU corpora).
**Scope:** the **2D image-plane** club-shaft track — angle θ, clubhead pixel, visible length — produced
by `ShaftTracker` (`src/Analysis/shaft_*`). **No 3D plane / club-path** here (that needs calibrated
cameras + new outputs; out of scope).
**Reference (the "why"):** [`pipeline_validation_and_tuning.md`](pipeline_validation_and_tuning.md) §5.4
(pose/shaft validation) and §3.4 (sample size). **Parameter catalog:**
[`tunable_parameters_reference.md`](tunable_parameters_reference.md) §2.2. **Harness:** the `/swinglab`
skill + `tools/swinglab` (`lab.py`, `swinglab_run`).

> **Why standalone.** In production the IMUs are on the **spine** (Pelvis/Thorax) — there is **no
> club-mounted or lead-hand IMU**. The shaft tracker must therefore be accurate **from vision alone**, so
> this programme is run on **IMU-less** swings, independent of the IMU/wrist criterion programme
> (Corpus 1–3). The tracker is already vision-first: the only IMU input is the optional `LeadHand`
> channel (`shaft_tracker.cpp`), which simply stays **inert** when no lead-hand IMU is bound — so an
> IMU-less swing exercises *exactly* the spine-IMU production path. The invariant is locked by the
> "Vision-only baseline (IMU-independence)" case in `src/Analysis/tests/shaft_track_test.cpp` and by the
> camera-only synthetic fixture (below).

---

## 1. What is measured, and the gates

| Tier | Check (`score.py`) | Gate | Needs labels? |
|---|---|---|---|
| **1 — track sanity** (soak, all frames) | `club.valid`, `club.coverage` | coverage ≥ 0.6 | no |
| | `track.monotonic_t`, `track.theta_step`, `track.head_step`, `track.len_step` | continuity (no teleports) | no |
| | `track.downswing_sweep`, `track.peak_rate_near_impact` | full-swing arc/timing near impact | no (needs impact) |
| **3 — absolute accuracy** (labelled) | `truth.theta_rms_deg` | **< 3°** | yes |
| | `truth.head_median_px` | **< 25 px** | yes |
| | `truth.parallel_p2/p6/p8_deg` | ≤ 20° (label sanity) | yes |

All IMU/wrist checks (`xmodal.imu_vision_corr`, `diag.*`, `filter.*`, `score.*`) **auto-skip** on an
IMU-less swing — `xmodal` passes when no bindings; the others return no checks when their data is absent.
So the IMU-less scorecard is purely the shaft `club.*`/`track.*`/`truth.*` set.

**MP4-vs-raw caveat (important).** Coverage, continuity, timing and *relative* θ are valid on MP4.
Sub-pixel **θ-RMSE** conclusions are **advisory on MP4** (compression smears thin ridges) — for tight
θ-RMSE capture a **raw subset** (`saveRawFrames` ON) of ~10–12 swings.

**Stage-2 clubhead / length model rides this same corpus.** The exemplar
([clubhead_detection_design.md](../design/clubhead_detection_design.md), stage-2 of the markup exemplar)
consumes the same labels' `head`/`len` fields, scored by `tools/shaftlab/score_truth.py --head` (head px
error, per-phase length error, conf honesty). Its gates at exemplar freeze: head meas-tier median
**< 25 px** (same bar as `truth.head_median_px`), and **length-model form selection** — which this corpus
is the prerequisite for: it needs held-out swings **and held-out clubs** (backbone doc §3.4/§5.5; a single
labelled swing may itself be off-plane and can never adjudicate the model form).

---

## 2. Capture spec (the not-yet-recorded corpus)

- **Session type = Wrist (`sessionType = 1`).** This is the only analyzer that runs the shaft tracker
  (Swing/GRF/Coach are stubs → no club track). If a swing was captured under another type, force it at
  run time with `lab.py … --session-type 1`.
- **Face-on camera tagged `perspective = 2`.** DTL (`perspective = 1`) is optional and unused for 2D
  scoring. The face-on stream is what the tracker and labeller use.
- **No IMU** (the point of the exercise).
- **Impact:** none is recorded without an IMU — it is supplied later from the **markup P7 (impact)**
  label (§3), so no special capture step is needed. (If the SHOT button was pressed, `capture.impactUs`
  is also usable.)
- **MP4 for the bulk**; a **raw subset (~10–12)** for sub-pixel θ.
- **Vary conditions** — club, lighting, background clutter, tempo (don't label only easy swings).
- **≥ 3 clubs as a stratum (GW / mid-iron / driver minimum)** — required by the stage-2 length-model
  form selection: plane/foreshortening geometry varies with club length, so clubs must be holdable-out
  as whole groups.
- **Frame wide to the player's trail side** (face-on: more room to the player's right) — most post-impact
  detection loss on the existing corpus is the club leaving the frame, not detector failure
  (shaft findings §6.1). Uncropped captures are also what makes crop/off-frame validation meaningful.
- **Background contrast is an environmental REQUIREMENT, not a nice-to-have** (user directive,
  2026-07-04, after marking up the dark-studio corpus): in the studio's "normal" (dark) lighting the
  club is lost against the black wall — dark-on-dark defeats every photometric channel simultaneously
  (edge-pair, change-vs-median, motion), and no detector change can conjure signal that isn't there.
  Provide a backdrop behind the swing arc with luminance contrast to a steel/graphite shaft (mid-grey or
  lighter; matte, non-reflective so it doesn't join the specular clutter). Record the backdrop
  presence/type in the swing's markup `meta` (alongside `lighting`) so corpus analysis can stratify by
  it. The detector's dark-wall performance is still measured (it is the current reality) but capture
  FOR validation should include the backdrop stratum; detection targets are gated on contrast-adequate
  captures.

---

## 3. Markup (ground truth) — in-app, blinded

Use the in-app **Markup panel** (`PpMarkupPanel`; session toolbar **View → Markup**) — it already labels
the shaft line and P-positions; `lab.py label <swing>` is the headless fallback. Both write `truth.json`
into the swing dir (the **only** file written into a swing dir post-capture; `swing.json` is immutable).

- **Place the club line** (click **grip → clubhead**) per labelled frame. The clubhead click IS the
  stage-2 ground truth (`head` px + `len` = |head−grip| land in truth.json automatically) — place it with
  care even when only θ is of interest; ~10–15 head points/swing spanning P1–P10 feed the length model.
- **Tag P-positions** including **P7 (impact)** — this is what supplies the run's impact instant.
- **Sparse mode (primary):** label at the P-positions (~10/swing). **Dense mode:** 2–3 swings labelled
  every ~10th frame as a between-point θ(t) reference.
- **Blinded:** label from raw video only — do **not** view the analyzer's recovered track first.

`truth.json` shape (byte-compatible with `score.py`):
`{"shaft":[{t_us,grip[px],head[px],theta,len}], "events":{p7_s,…,t0_us}, "meta":{…}}`.

---

## 4. Running SwingLab (IMU-less)

```bash
P=~/.swinglab-venv/bin/python                      # the SwingLab venv
# 0) deterministic self-check of the whole IMU-less path (no capture needed):
$P tools/swinglab/lab.py synth --no-imu /tmp/sv-synth
$P tools/swinglab/lab.py one   /tmp/sv-synth /tmp/sv-run     # → club.valid, truth.theta_rms PASS, IMU checks absent

# 1) real corpus:
$P tools/swinglab/lab.py ingest /data/shaft-corpus            # camera-only swings are first-class
$P tools/swinglab/lab.py run    /data/shaft-corpus /runs --id baseline   # gate reference
$P tools/swinglab/lab.py one    /data/shaft-corpus/swing_0001 /runs/one   # single-swing triage + contact sheet
```

- **Impact is automatic.** `run`/`one` pass `--impact-us` from the markup **P7** label whenever
  `swing.json` itself carries no impact (an IMU-less capture) — implemented in `core.run_one` via
  `Swing.impact_from_truth()`. So a swing runs as soon as it is marked up; before markup, only the
  label-free Tier-1 soak is meaningful (pass a provisional `--impact-us` to the binary if needed).
- **Session type:** add `--session-type 1` if any recording was captured under a non-Wrist session.

---

## 5. Tuning (after labels exist)

`shaft.*`/`assembly.*` are freely sweepable (not frozen). Configs are in `tools/swinglab/configs/`.

```bash
# coordinate-descent sweep, regression-gated, partitioned:
$P tools/swinglab/lab.py sweep /data/shaft-corpus /runs tools/swinglab/configs/shaft_space.json \
      --method coordinate --baseline /runs/baseline \
      --partition tools/swinglab/configs/partitions.template.json --trials 40
$P tools/swinglab/lab.py diff  /runs/baseline /runs/<candidate>     # exit 1 ⇒ a swing regressed
```

**K-flag A/B (the K5 decision).** The skeleton-aware flags ship OFF (`autoChirality` is the only one ON).
Decide each by diffing its ON variant against the all-OFF baseline:

```bash
for f in tools/swinglab/configs/shaft_kflags/*.json; do
  $P tools/swinglab/lab.py run  /data/shaft-corpus /runs --id "$(basename "$f" .json)" --params "$f"
  $P tools/swinglab/lab.py diff /runs/baseline /runs/"$(basename "$f" .json)"
done
```

Flip a flag default (in `src/Analysis/shaft_tracker.cpp`) only when its K5 gate is met
(`docs/implementation/shaft_detection_skeleton_impl.md` "Rollout / default-flip summary":
0 regressions / 0 legit samples lost / 0 false rejections).

**Acceptance & freeze.** Keep a sweep value only if **mean ↑ AND `regressions: 0` per swing** AND the
class bar: coverage ≥ 0.6 on ≥ 90 % of clean face-on swings; **θ-RMSE < 3° / head < 25 px** on the
labelled set. Partition the search (sweep **Tune**, select **Validation**, touch **Held-out** once with
`--freeze`). Freeze a locked value by editing the single literal in `src/Core/pp_tuned_constants.h`; the
`tuned_constants_parity_test` proves byte-identity. **Always rebuild `swinglab_run` after any analysis C++
change; never diff across hosts** (CPU vs CUDA pose differs).

---

## 6. Sample sizes (from the backbone doc §3.4 / §5.4)

- **θ-RMSE / head-px** (χ² on variance vs labels): **10–15 labelled** swings (~8–10 P-points each);
  ~12 of them **raw** for sub-pixel θ.
- **Coverage proportion** (≥ 0.6 on ≥ 90 %): **~35** swings for ±10 %.
- **Label reliability:** re-label ~5 swings with a 2nd labeller (ICC on placed θ) — bounds how tight any
  θ-RMSE claim can legitimately be.

---

## 7. Status / what's wired

- Tracker is vision-only (degrades cleanly with no `LeadHand` IMU) — **verified** + guarded by test.
- `swinglab_run` + `score.py` run IMU-less out of the box; impact + session-type passthrough added to
  `lab.py`/`core.py`; `lab.py synth --no-imu` provides the deterministic camera-only fixture.
- Markup already labels shaft line + P7 impact.
- **Detector accuracy work** is tracked separately in
  [`../implementation/shaft_detection_phase2_plan.md`](../implementation/shaft_detection_phase2_plan.md):
  **Phase 1** (lead-arm plausibility sector, `shaft.useArmPlausibility`) is implemented + validated on one
  swing (fixes the address arm-as-club pick; detection 57→63 %); **Phase 2** (blur-line / line-integral
  wedge detector for the fast downswing) is designed and **corpus-gated**.
- **Pending:** the real corpus capture + markup, then the sweeps, K-flag flips, the Phase-1 default flip,
  and the Phase-2 build above.

> **Production note (out of scope here):** the tracker is invoked only from `WristAnalyzer` today
> (camera-gated). Running it under whichever analyzer owns the spine-IMU session is a separate
> *production* wiring task — the vision-only accuracy validated here transfers directly (LeadHand inert ≡
> no-IMU).
