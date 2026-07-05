# Shaft detection v3 — implementation & test plan

**Document type:** implementation + test plan (staged, corpus-gated). **Owner:** Mark Liversedge.
**Design:** [`club_tracking_v3_design.md`](../design/club_tracking_v3_design.md). **Research:**
[`club_detection_from_video.md`](../research/club_detection_from_video.md). **Capture protocols (already
written):** [`instrumented_club_protocol.md`](../validation/instrumented_club_protocol.md),
[`shaft_validation_protocol.md`](../validation/shaft_validation_protocol.md).

---

## 1. Why, and the gate ladder

v2.0 stripe fusion reached "zero adjudicated errors" only by **abstaining** — each counterfeit met a
generic guard, each guard bled real coverage (finish/address fell to 0%). v3 replaces
**honesty-by-abstention** with **honesty-by-discrimination**: the four fundamentals (C1 held-in-hands,
C2 no body-overlap mid-swing, C3 one reversal at the top, C4 club+lead-arm double pendulum) become
constraints checked *before* the (validated) evidence engines, inside a global Viterbi estimator.

**Gate ladder:** `synth (machinery) → single-swing full-res adjudication → corpus → freeze`. Governance
(research §3.1): exemplar-first; the median lies (per-tier, per-phase, mean/p90/%-bad); fixtures rot
(re-freeze in the code's own commit); byte-for-byte determinism; corpus gate before any generalisation
claim.

**Scope (confirmed):** full detail on classical core **v3.0→v3.2** (+ **v3.11** IMU witness); **v3.3** NN and
**v4** C++ port are outlined milestones only.

---

## 2. Corpus strategy — start from what we have; capture only on demonstrated need

**Principle (Mark, 2026-07-05):** the existing 10-swing taped corpus is enough to *get started*. New
capture is **not** planned up front — a new corpus is introduced **only when a specific gate cannot be met
with data already in hand** (i.e. the existing corpus has been genuinely defeated on that gate). This
avoids capture work for corpora the algorithm has not yet earned.

### 2.1 What already exists (the starting workbench — no new capture)

| Asset | Contents | Gates it can already serve |
|---|---|---|
| **`tape_20260705`** | **10 taped 7-iron swings**, raw ON, exposure 6.57 ms embedded; v2 fusion truth = **1033 entries, zero adjudicated errors**; vision-only (no IMU) | **v3.0 primary workbench**; v3.1 impact zone; v3.2 address θ — all gated apples-to-apples vs the v2.0 numbers that came from *this same* corpus |
| **`tape_20260704`** | 15 taped 7-iron swings; the F11 finish-wipe fixtures | secondary; F11-class regression |
| **c1 corpus** | 10 uncropped swings, **8 club types**, 100 clubhead labels | **already multi-club** for stage-2 clubhead/length — defer new multi-club capture until this is proven insufficient |
| **s0008 / s0009** | 51 / 18 hand shaft labels (0009 pathological) | passive-tier hand-label cross-check |
| **Frozen adjudicated failures** | every catalogued counterfeit (trouser crease, screen/mat edge, leg shadow, shirt texture, arm line, mat speckle, bag rack, finish body-line+shoes, impact streak-flip) — already frozen frames in the lab records | **the v3.0 counterfeit-regression suite is harvested from these — no capture** |
| **Synthetic** | `make_synth.py` + `stripe_fusion.py --selftest` | machinery gate |

**Immediate Phase 0 = no capture.** Prep `tape_20260705` (already raw), extend the **synthetic gate**
(Set S below), and **harvest the frozen counterfeit frames** into named test fixtures. Then build v3.0
against `tape_20260705`.

**Set S — synthetic (generated, no capture, no markup):** extend `make_synth.py` + `--selftest` with
per-phase θ profiles, body polygons, and the counterfeit population; require θ mean ≤ 2°, zero flips,
correct tier + phase, **every counterfeit VETOed by the right constraint** (render the frames and look).

### 2.2 New capture is trigger-gated (introduce a corpus only when its gate bites)

Each row is captured **only if/when** its trigger fires. Specs reference the existing protocol docs rather
than re-deriving them. Ordered by how early the trigger is likely to bite.

| Candidate corpus | **Trigger — the blocker that justifies it** | Minimal spec (when triggered) | Markup | Not before |
|---|---|---|---|---|
| **New counterfeit class** | a v3 test surfaces a distractor **not** in the frozen set | 1 swing staging it (taped, or label the contested frames) | contested frames | as encountered |
| **Multi-club instrumented** | v3.2 / stage-2 **length-model form selection** needs held-out *clubs* **and** the c1 multi-club clubhead labels prove insufficient for the shaft-length fit | +Driver +wedge × ~6 taped swings, raw ON (`instrumented_club_protocol.md`) | auto (stripe) | stage-2 length work |
| **IMU-bound instrumented subset** | starting **v3.11** (the existing tape corpus is vision-only) | ~8 taped swings, wrist IMU bound + calibrated, 200 Hz | auto | v3.11 |
| **Untaped accuracy** | making an **absolute θ-RMSE product-path claim** on unmarked clubs beyond s0008/0009 | 10–12 untaped, raw, blinded sparse markup + 5 re-labelled (`shaft_validation_protocol.md §6`) | **required** | a validation milestone |
| **Coverage soak (~35)** | stating a **coverage-proportion** statistic (≥0.6 on ≥90%, ±10%) | ~35 clean face-on MP4 | none | a coverage claim |
| **Left-handed** | **v3.0 freeze**, and **only once v3.0 is proven on RH** (per Mark: no LH golfer until something is proven) | ~3 taped LH swings, 1 golfer | auto | v3.0 freeze / RH proven |

### 2.3 Capture settings + per-swing gate (apply *whenever* a trigger does fire)

Universal: Wrist `sessionType=1`; face-on `perspective=2`, pose ON (DTL not needed for 2D θ); exposure with
hitting-area highlights **below clipping** (`exposureUs` auto-embedded since `cb5c646`); ring light ON for
taped; whole-arc trail-side uncropped framing; no natural light; `CORPUS.md` at the root (else `lab.py
ingest` won't bless). **Tier-0 per swing:** `sessionType=1` + `perspective=2` + pose present; `impactUs` or
P7; taped → `meta.club ∈ clubs.json`; IMU subset → `bindings[]` populated + `calibrated:true` (bindings=0 is
fatal, `swing_reanalyzer.cpp:549`); raw set → `.raw` present + `frameBytes` consistent.

---

## 3. The two machines and the compute split

| | **Studio PC** (`GOLFSIMPC`) | **MBP** (dev laptop) |
|---|---|---|
| Spec | Windows 11, **RTX 5080**, 32 GB, fast disk | 16 GB, slow CPU, low disk |
| **Owns** | all **capture**; in-app **ViTPose** analysis (CUDA); `prep_swing` raw→FFV1 decode; **all batch/corpus/sweep** (4-way parallel safe); **v3.1 shift-and-stack**; **v3.3 NN training** (torch); the **canonical determinism / fixture-freeze host** | exemplar **code dev**; **single-swing** triage; **DP** dev/debug (1-D, cheap); montage/adjudication **review** |
| **Never** | — | whole-corpus batch; **parallel heavy jobs** (16 GB OOMs > ~4 workers); host raw sidecars; **freeze fixtures** (CUDA pose ≠ CPU pose → never diff cross-host) |

Shared medium: NAS `//192.168.1.92/SwingData` (804 GB free) = `/mnt/swingdata` ≡ `C:\...\Data`.

**Frame-volume budget (1280×1024 BayerRG8, 746 frames/swing):** raw `Face-On.raw` = **933 MB/swing** →
raw + FFV1 live **NAS/studio only** (the MBP cannot host a corpus). Whole clip decoded ≈ **2.9 GB BGR /
~1 GB gray** — the MBP OOM risk is `shaft_annotate.py`/`stripe_fusion.py` whole-clip caching **under
parallelism**, so the MBP runs **serial, one swing**, and v3 exemplars prefer **windowed/streamed** access.
**v3.1 shift-and-stack** holds only a window (~160 MB) → cost is **CPU**, not RAM → studio PC. **ViTPose**
(the one heavy GPU pass) already runs in-app on the studio PC; `prep_swing.py` runs no pose model (it reads
the precomputed `analysis.pose2d` from `swing.json`) — perception is on the right machine by construction.

---

## 4. Implementation — v3.0 → v3.2 (+ v3.11), all against `tape_20260705`

**New exemplar** `tools/shaftlab/club_track_v3.py`, reusing the validated engines in place: **E1** band
matcher (`stripe_annotate.py` + dense `frame_band_match` in `stripe_fusion.py`), **E2** polarity ridge
(`ridge_sweep()`/`_sample()` in `stripe_fusion.py`).

### v3.0 — constraint system + DP  *(dev MBP; corpus gate studio PC)*
1. **Phase model from the hands alone** (`anchors.csv`): still→takeaway→backswing→TOP→downswing→impact→thru
   →finish + **chirality**, with a **phase confidence** (low spans fall back to conservative v2 rules).
2. **C1 butt-termination (full)** — evidence within `r0 ∈ (0, ~260 mm]` behind the grip; support behind the
   butt is vetoed (v2 had only the weak "within 80 px" form).
3. **C2 body-overlap veto** — polygon from `skeleton.csv` (8 joints + margin, temporally smoothed); veto
   majority-inside in takeaway→thru; admit-not-sufficient at address/impact/finish.
4. **C4 reachable cone** — ψ = θ − α collapses the θ search from 360° to tens of degrees; ψ smoothness prior.
5. **C3 phase-signed rotation** — a 180° flip becomes structurally impossible; bridging = monotone sweep.
6. **Global Viterbi DP over a θ grid across the clip** — transitions from C3/C4, emissions from E1/E2 inside
   C1∧C2∧C4; tiers `band`/`ray`/`pred`; no RNG (deterministic).

**Gates (on `tape_20260705`):** Set S synth → single-swing full-res montage (MBP) → **corpus gate (studio
PC):** coverage ≥ v2.0 in down/thru, > 0 at finish + address-adjacent, **zero readmitted junk** (harvested
counterfeit suite green), per-phase coverage AND accuracy vs the v2 truth, byte-identical rerun. **LH is
NOT part of this gate** — it is captured and checked only at freeze, once RH is proven (§2.2).

### v3.1 — shift-and-stack + exposure-arc  *(studio PC)*
Coarse ¼-res corridor proposal (register on grip anchor, rotate by −∫ω dt from the C3/C4 ω hypotheses,
stack; **C2 masks body pixels from coherence**, **C1 tested on the composite**); full-res E1/E2/DP inside
the corridor. **Exposure-arc** sector edges = sub-frame θ; arc-length ÷ band width = single-frame |ω|.
**Gate (`tape_20260705`, impact ±10 frames):** θ/ω emitted; ω(t) smooth with a plausible 7-iron peak;
composites adjudicated; no regression elsewhere.

### v3.2 — address / hold θ  *(dev MBP; gate studio PC)*
Inside the C4 cone with C1: **mat-crossing prior** + **lateral line fits** (fit-then-require-hand-proximity)
+ hold-period stack. θ-only truth for stills; (s,r0) only if bands lock (usually can't at this exposure).
**Gate:** agreement with stage-1 address meas + hand-label spot checks; zero flips.

### v3.11 — IMU conditioning  *(deferred; needs the §2.2 IMU-bound capture)*
One-directional (epistemic firewall): IMU conditions the *search*, never fits the truth. Corroborates C3's
top; collapses the DP + shift-and-stack hypothesis sets; quarantines vision locks whose implied spin
contradicts the IMU. **Gate:** quarantine precision/recall; no truth entry moved toward the IMU.

---

## 5. v3.3 / v4 — outline only (gated)

**v3.3 heel/toe NN.** *Entry:* v3.0–v3.2 frozen; instrumented truth dense enough to weak-label heel/toe;
stage-2 classical head characterised as the bar to beat. Small heatmap U-Net inside the v3.1 ROI; data
flywheel (stripe truth → weak labels → stratified adjudication → gold; blur augmentation from measured ω(t));
physics-consistency loss + conformal calibration. **Training studio-PC-only** (RTX 5080; torch = new dep);
inference deterministic via existing ORT (v1.26.0) subclassing `PoseEstimatorBase`. *Gate:* beats classical
stage-2 head on held-out instrumented swings.

**v4 C++ port.** *Entry:* classical exemplars frozen + a per-platform byte-oracle. `IShaftDetector` ABC +
factory; byte-match target is the attic `shaft_annotate_port` (vision-only) — **not** the shipping
`ShaftTracker`. *Gate:* byte-agreement modelled on `swing_window_parity_test.cpp` +
`tuned_constants_parity_test` + the `truth.json` C++↔Python precedent. Cross-platform bit-equality is
untested → establish the oracle on one platform first.

---

## 6. Testing & determinism

Byte-identical rerun **per host**; canonical host = studio PC; **never diff MBP↔studio**. Fixtures re-frozen
atomically in the code's own commit. Metrics always per-tier/per-phase (mean/p90/%-bad); honesty clauses
(≥ ⅔ wrong frames low-conf; ≤ 5 % high-conf frames wrong). The harvested counterfeit suite grows with every
new adjudication. Harnesses: `tools/shaftlab/` (exemplar) + `/swinglab` skill / `tools/swinglab/lab.py`
(production `ShaftTracker`).

---

## 7. Verification

```bash
$PY tools/shaftlab/prep_swing.py /mnt/swingdata/.../tape_20260705/swing_0001 /tmp/s1   # raw→FFV1
$PY tools/shaftlab/stripe_fusion.py --selftest                                          # extended Set S
$PY tools/shaftlab/club_track_v3.py /tmp/s1/faceon_swing.avi --anchors /tmp/s1/anchors.csv \
      --clubs /mnt/swingdata/shaftlab/clubs.json --fps-override <fps> --out-dir /tmp/s1/v3
$PY tools/shaftlab/montage.py /tmp/s1/v3/overlay.mp4                                     # full-res eyeball
$PY tools/shaftlab/score_truth.py /tmp/s1/v3/*_track.csv /tmp/s1/clipmeta.json          # per-tier/phase θ
#   determinism: rerun on the studio PC, diff CSVs byte-for-byte
```
Success = each phase's gate green on `tape_20260705`, adjudicated on full-res montages, byte-identical rerun.

---

## 8. Critical files

- **Reuse:** `tools/shaftlab/stripe_fusion.py` (E1 dense + E2), `stripe_annotate.py` (E1), `prep_swing.py`,
  `make_synth.py`, `score_truth.py`, `montage.py`; `docs/design/club_tracking_v3_design.md`.
- **New:** `tools/shaftlab/club_track_v3.py` (v3.0), v3.1 shift-and-stack module, extended Set S scenes,
  harvested counterfeit fixtures.
- **Corpus/schema:** `src/Export/swing_exporter.cpp`, `swing_doc.cpp`, `swing_reanalyzer.cpp:536–553`
  (validity gates), `src/Pose/pose_estimator_vitpose.cpp` + `src/Analysis/pose_runner.cpp` (offline pose).
- **Future port:** `.claude/attic/auto-markup-2026-07-02/shaft_annotate_port.{h,cpp}`,
  `src/Analysis/tests/swing_window_parity_test.cpp`, `src/Pose/pose_estimator_base.*`.
