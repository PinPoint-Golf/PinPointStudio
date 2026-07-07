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

**Scope (confirmed):** full detail on classical core **v3.0→v3.2** (+ **v3.0-r1** C4 ψ-monotonicity rail,
+ **v3.11** IMU witness); **v3.3** NN and **v4** C++ port are outlined milestones only.

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
(`ridge_sweep()`/`_sample()` in `stripe_fusion.py`). **A full, self-contained pedagogical explanation of
the exemplar — the durable "bible" for the approach and the C++ port, written for a reader new to golf,
computer vision, dynamic programming, and the statistics — is in
[`../design/club_track_v3_exemplar_explained.md`](../design/club_track_v3_exemplar_explained.md).**

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

### v3.0 as-built — single-swing gate PASSED on s01 (2026-07-06, dev box)

`tools/shaftlab/club_track_v3.py` built, reusing E1 (`frame_band_match`) + E2 (`ridge_sweep`) from
`stripe_fusion` unchanged. Phase model (C3) + chirality are hands-only from `anchors.csv`; impact from
`swing.json capture.impactUs` (hands-only grip-return fallback when absent). Global banded Viterbi over a
1° θ grid (deterministic, no RNG). What the s01 build taught, beyond the design:

- **Band locks must be a negative-emission WELL, not merely evidence.** A ratio-verified, butt-terminated
  (`0 < r0 ≤ 260 mm`, C1) band lock is set to `emis = −W_BAND` so the global path is *forced* through the
  band chain. Without this the DP takes the smoother wrong branch across the evidence-free impact gap and
  thru/finish land ~90° off (the club's true θ **decreases monotonically, wrapping mod 360, from the top
  all the way through impact→finish** — one reversal, at the top; v2's impact flip is now structurally
  impossible). This single mechanism moved thru from 94.9°→0.8° and finish from 72.7°→0.3° median error.
- **C4's cone is WIDE, not "tens of degrees".** Raw φ (lead-arm, pose) jumps to ~87°/frame at the top and
  in the blur zone; robustly unit-vector-smoothed φ still leaves per-phase ψ=θ−φ spanning 65–107° and
  *unbounded* (351°) in the finish. So C4 = a wide chirality-centred cone (disabled at addr/finish/top) +
  an into-forearm veto; **C3 phase-signed rotation + the DP θ-smoothness carry flip-prevention and the
  search-space collapse.**
- **Honesty rules (design C2 "never sufficient alone" made concrete).** A `ray` needs real E2 support at
  θ* that beats its own reverse (dir-safety). At **finish** a ray must be **band-corroborated** (static
  holds are the counterfeit danger v2 abstained from); in free-space phases a ray must be **motion-
  verifiable** (not in a sustained static grip run). Address stays `pred` (v3.2 territory). `pred` is
  never written to truth.

**s01 result vs the v2.0 fusion truth (same corpus):** per-phase θ error — backswing 0.7°, top 0.1°,
downswing 0.4°, thru 0.8°, finish 0.3° (median; **zero** frames >15°), impact 7.1° (1 borderline frame,
f509 @15.7°). Coverage **down 61%→91%, thru 55%→89%** with the verified finish rotation kept and the
unverifiable deep hold honestly abstained. By tier: band 0.3° / ray 1.2° median, **zero flips, zero
readmitted junk** (full-res montage adjudicated). Determinism: **byte-identical rerun on-host**. Per
[[single-swing-never-judges-model-accuracy]] the thresholds are NOT tuned to s01 — they are phase-level
policies to be re-adjudicated at corpus scale.

### v3.0 CORPUS GATE — PASSED on all 10 swings (2026-07-06, GOLFSIMPC)

Ran `tools/shaftlab/run_v3_corpus.py` on the studio PC (RTX 5080, 32 GB — the whole-corpus batch host per
§3; the 16 GB dev box cannot: only swing_0001 is synced to the NAS, so 9/10 swings' impact + v2 truth live
only on C:\). All 10 swings vs their v2.0 fusion truth:

- **Coverage:** down **57%→96%**, thru **54%→83%** (aggregate); addr_back 5%→7%, finish 0%→2% (honest —
  the deep hold abstains). Consistent gains on every swing.
- **Accuracy:** down **0.5°**, thru **0.4°** median (aggregate); p90 3.4° / 3.8°. **By tier (the honesty
  split): band n=434 median 0.3° / max 6.0° / bad>15° = 0 (0%); ray n=434 median 1.7° / bad>15° = 14 (3%,
  within the ≤5% clause); pred n=165 median 9.4° / bad>15° = 44 (27%).** Everything emitted to truth
  (band+ray) is clean; the softer addr_back error (median 7.4°) is entirely the honest `pred` bridges in
  the address zone (v3.2 territory), never written to truth.
- **FLIPS = 0 across all 10 swings** — the headline v3 win; C3 makes the v2 impact-flip structurally
  impossible.
- **Counterfeit-regression suite** (`tools/shaftlab/counterfeit_check.py`, harvested from the adjudicated
  catalogue): **0 fail.** s01 impact streak-flip prevented (max err 7.6°); s02/s04/s05 address
  shirt-texture/waggle-junk not re-emitted; s06 leg-shadow abstained. **s07 f97–110** emits ratio-verified
  BAND locks at θ≈95° in the address — adjudicated (montage) as the **real club at a still address**, i.e.
  v3.0 incidentally recovers real address θ here (a v3.2 preview), not a counterfeit. NOTE: v3's band tier
  lacks v2's motion-corroboration, so a *static* band lock isn't motion-gated — real here, but the corpus
  adjudication should watch for a wrong static band lock and add corroboration if one appears.
- **Determinism:** byte-identical rerun with identical args on the canonical host.

Still owed (dev box): fixture-freeze on the studio PC. Then v3.2 address θ. (Set S synth machinery
gate DONE — `make_synth_v3.py --selftest`; v3.1 shift-and-stack + exposure-arc DONE, see below.)

### v3.1 — shift-and-stack + exposure-arc  *(studio PC)*
Coarse ¼-res corridor proposal (register on grip anchor, rotate by −∫ω dt from the C3/C4 ω hypotheses,
stack; **C2 masks body pixels from coherence**, **C1 tested on the composite**); full-res E1/E2/DP inside
the corridor. **Exposure-arc** sector edges = sub-frame θ; arc-length ÷ band width = single-frame |ω|.
**Gate (`tape_20260705`, impact ±10 frames):** θ/ω emitted; ω(t) smooth with a plausible 7-iron peak;
composites adjudicated; no regression elsewhere.

### v3.1 as-built — impact-zone ω(t), synth + single-swing gates PASSED (2026-07-06, dev box)

**New:** `tools/shaftlab/shift_stack_v3.py` (exposure-arc + shift-and-stack), `make_synth_v31.py` (Set S
v3.1 machinery gate), `run_v31_corpus.py` (corpus gate). **Additive by construction:** reads the frozen
v3.0 `*_v3.csv`, touches only impact ±K, writes only `*_v31_impact.csv` + an ω-plot + a composite montage
— the v3.0 track/truth are never rewritten, so **"no regression elsewhere" holds structurally**.

**Reframe (evidence-driven, confirmed on the data).** v3.1's shift-and-stack was designed against
*v2.0's* impact-zone gap ("v2.0 emits nothing"). **v3.0 already closed that gap** — the impact zone is
covered by rays (corpus down 57→96 %, thru 54→83 %). And on the retro-reflective taped corpus at
near-full-frame exposure (τ 6.57 ms ≈ T 6.70 ms, `frac≈0.98`), per-frame SNR is already high: √N stacking
is **outweighed by pose-grip-anchor jitter** (grip-registration + rotation-about-grip maps the rigid club
onto itself, so the residual is anchor noise, not a pivot error), and **E1 cannot re-lock discrete bands
on the composite** (too smeared) → **0 tier upgrades** (s01 confirmed; also single-frame E1 locks nothing
in the deep impact zone). The astronomy-style integration payoff is for the **low-SNR passive/untaped
regime** (v3.3+) where per-frame signal is weak; the module is retained for that path. The genuinely-new
v3.1 product on taped data is an **independent physical ω(t)**; the corridor "¼-res → full-res E1/E2/DP
inside" is not needed on tape (v3.0 covers it) and is deferred to the passive path.

**ω(t), two ways.** Emitted ω = lightly-smoothed `|dθ/dt|` of the frozen v3.0 DP track (reliable; what the
swing actually did). Corroborated by an **independent intra-frame exposure-arc**: the shaft sweeps an arc
during τ; because τ ≈ T, one frame's streak angular extent about the grip ≈ the inter-frame Δθ. Measured
robustly as the **θ0-anchored equivalent width** of the annulus ridge (thickness-corrected, tight ±30°
window so far distractors — the leg/mat — cannot bleed in), it recovers ω **without using the DP**.
Sub-frame θ = the sector edges (`theta_lead`/`theta_trail`). (A third estimator — inter-frame ridge
cross-correlation — was tried and **dropped**: it collapses onto the static leg/mat exactly where the club
overlaps the body.) Shift-and-stack composites (nanmean of the de-rotated sub-window) are kept for
**adjudication**: the club integrates into a coherent streak on θ(t) while body/mat smear into arcs.

**Set S synth machinery gate (`make_synth_v31.py --selftest`) — PASSED.** Known θ(t) on the proven
flip-free `make_synth_v3` geometry, exposure τ≈T: **emitted-ω peak within 9 % of truth, exposure-arc peak
within 1 %**, per-frame arc error 2.6 deg/f (≈ its single-frame noise floor — why the *profile*, not
per-frame values, is reported), **0 flips**. Proves the exposure-arc recovers a known ω.

**Single-swing gate — s01 (impact ±10), PASSED (full-res montage adjudicated).** ω peak **13.6 deg/f
(74 mph clubhead)** from the DP track and **13.0 deg/f (71 mph) from the independent exposure-arc —
agreeing to 0.5 deg/f** (median |track−exparc| 2.6 deg/f); ω(t) smooth (roughness 0.99), a clean bell
peaking at impact and decaying through the follow-through (physically right for a 7-iron). Composite
montage: the de-rotated stack integrates the club into a coherent streak aligned with θ(t) while the
legs/mat smear into arcs — the club is where θ(t) says. **0 band upgrades** (honest). Determinism
byte-identical on-host.

**Corpus gate — all 10 swings PASSED (dev box, `run_v31_corpus.py`).** Track ω peak per swing
**13.0–16.9 deg/f → clubhead 71–92 mph** (every swing a plausible 7-iron); the **independent exposure-arc
peak confirms it to median 1.5 / max 3.6 deg/f** (`PEAK_AGREE ≤ 4` PASS). ω(t) smooth everywhere
(roughness 0.46–1.71, `≤ 3` PASS). **0 band upgrades except s07 (=1)** — the real still-address band v3.0
already flagged on s07, recovered here on the composite. Determinism **byte-identical on-host**.
- **Metric note (honesty):** the gate scores the *smoothed ω-peak* agreement — the deliverable. The raw
  *per-frame* exposure-arc agreement is the single-frame noise floor (2.6–5.5 deg/f, worse where the club
  overlaps the body) and is reported as a diagnostic, **not gated** (the profile is smoothed before use,
  same reason the synth gate scores the peak not per-frame values).
- **Provenance:** the exact-`impactUs` + v2-truth 10-swing gate is a **studio-PC job** (s02–s10 session
  dirs are C:\-only); on the dev box only the clips/anchors/skeleton/clipmeta are on the NAS, so this run
  used the **recorded** impact for s01 and **hands-only** impact for s02–s10 (the window only needs to
  contain the fast segment — ω(t) is insensitive to a few-frame centre shift). Determinism is **per host**
  (canonical = studio PC — never diff MBP↔studio; studio cv2 ≠ dev cv2).

### v3.2 — address / hold θ  *(dev MBP; gate studio PC)*
Inside the C4 cone with C1: **mat-crossing prior** + **lateral line fits** (fit-then-require-hand-proximity)
+ hold-period stack. θ-only truth for stills; (s,r0) only if bands lock (usually can't at this exposure).
**Gate:** agreement with stage-1 address meas + hand-label spot checks; zero flips.

### v3.2 as-built — address-hold θ, synth + single-swing gates PASSED (2026-07-06, dev box)

**New:** `tools/shaftlab/address_theta_v3.py` (address/hold θ), `make_synth_v32.py` (Set S v3.2
machinery gate). **Additive by construction** (like v3.1): reads the frozen v3.0 `*_v3.csv`, measures
only the pre-swing still HOLD, writes only `*_v32_address.csv` + an adjudication montage (+ optional
θ-only truth merge) — the v3.0 track/truth are never rewritten, so **"no regression elsewhere" holds
structurally**.

**The gap it closes.** v3.0 disables the ray tier at `phase=="addr"` (a *static* club is v2's exact
counterfeit trap), so the whole resting address is emitted as an unpublished `pred` bridge — on the
corpus, addr_back is the softest zone (`pred` median 9.4°, never in truth). v3.2 *measures* that
resting θ. On s01 v3.0's "addr" phase is a single 424-frame span (f0–423) that actually contains a long
**still hold** (f0–~383, unmeasured even by v2) followed by the **takeaway** (f~384–423, which v2 fusion
*did* band-track from f397 — a v3.0 phase-model mislabel; left to a later phase-model fix, out of v3.2
scope). v3.2 targets the still hold only.

**Three tools, evidence-driven (adjudicated on s01).** (1) **Hold-period stack** — grip-register every
still-hold frame to the hold-end grip and average (v3.1's shift-and-stack with *zero* rotation): the
grip-anchored still club integrates sharp while the swaying legs/body/shadow counterfeits smear. On s01
this alone suppresses the trailing-leg line that *outscores* the club in a single frame (raw ridge: leg
θ≈75° 449 vs club θ≈98° 427), so the club wins even at a wide cone after 81 frames stacked. (2)
**Tight address cone** — the bible's C4 cone is wide mid-swing (ψ spans 65–107°), but AT REST the club
hangs nearly in line with the lead arm (ψ small), so an address-only `|θ−φ_s| < 28°` cone rejects the
leg/crease look-alikes a wide cone admits. This is the address specialisation of C4; it does **not**
touch the wide-cone invariant that governs the moving swing. (3) **Mat-crossing prior** — reformulated
after adjudication into its *honest* role: a **lenient** sanity gate that the ray is a real
ground-reaching line (crosses onto the blown mat with a coherent polarity-correct shaft), NOT a
club-vs-counterfeit discriminator (both the club and the leg cross the mat — the cone/stack/stability do
the discrimination). **θ-ONLY:** bands do not lock at the address exposure (the blown mat swamps the
tape) — confirmed, 0 stack/single-frame locks on s01 — so no s/r0/head unless a rare stack band lock
fires. **dir-safety dropped at address** (principled): the lead arm is always above the grip, so the
club's reverse always carries the arm's ridge (the bible's "arm is legitimately behind the hands,
excluded" clause) — direction is pinned by the down-pointing cone + down-sector instead.

**Publish gate (honesty).** A hold θ graduates `pred`→`hold` (a new meas tier) only if the stack passes:
strong E2 evidence at θ0, the mat-crossing sanity, θ0 in the down sector, per-frame θ **stable** across
the hold (std ≤ 8°, a persistent club vs transient noise), and θ0 inside the tight cone (a 180° flip is
structurally impossible — it points up, out of the down-cone). Per-frame frames publish θ0 only where
their own cone-ridge θ agrees within 8°; else they stay `pred`.

**Set S synth machinery gate (`make_synth_v32.py --selftest`) — PASSED.** Mirrors make_synth_v3's proven
flip-free swing + a long still hold at a known θ with two address counterfeits: **A** in-cone,
body-attached and oscillating (a single frame's continuous bright line outscores the dotted club, but
the grip-registered stack smears it — tests stack suppression) and **B** out-of-cone, grip-rigid
(survives the stack — tests the cone). Result: θ0 = 90.00° (**err 0.00**), both counterfeits rejected
(θ0 15°/40° from A/B), published, **0 flips**. Proves the stack defeats an in-cone swaying counterfeit
and the cone rejects a stack-surviving one.

**Single-swing gate — s01, PASSED (full-res stack montage adjudicated).** Hold detected f304–384 (81
frames); address **θ0 = 88.65°** — the montage shows it landing on the resting club (down to the ball),
with the trailing-leg counterfeit (75°) smeared away by the stack; **77** previously-`pred` address
frames recovered as published `hold`-tier θ; per-frame std 3.4°; **0 flips**. Determinism
**byte-identical** rerun on-host.

**Still owed:** the corpus gate (all 10) is a **studio-PC job** (only s01 is synced to the dev-box NAS;
s02–s10 impact + v2 truth are C:\-only, per §3). Runs `run_v32_corpus.py` (to write, mirroring
`run_v31_corpus.py`): per-swing address θ published + adjudicated on montages, agreement with any v3.0
address band meas (e.g. s07 f97–110), **0 flips**, byte-identical rerun on the canonical host. Then a
fixture freeze on the studio PC.

### v3.0-r1 — C4 ψ-monotonicity transition rail  *(dev MBP design; gate studio PC)*

**The insight (Mark, 2026-07-06).** The shipped C4 (design §1) is a *magnitude* cone on ψ=θ−φ, which
the as-built widened to 150° and part-timed under pose-φ noise — leaving C3 to do the real
search-collapse. The double pendulum's *stronger* fact was never encoded: **ψ is monotone with a
single reversal at the top**, exactly as C3 constrains θ. ψ cocks (address→top), reverses once at
transition, releases (transition→impact→finish); re-hinge in the downswing / un-hinge in the
backswing is anatomically impossible. Since θ=ψ+φ and the arm φ moves, "ψ one-sided per phase" is
*strictly stronger* than C3's "θ one-sided per phase". Full write-up: design §8; research §3.8a/§4.6.

**The change.** Recast C4 as a **DP transition term on Δψ = Δθ − Δφ**, parallel to C3's on Δθ:
phase-signed sign-lock (free **window** at transition for the release-lag), rate bound |Δψ|≤ψ̇_max
(WMAX-class, corpus-set), mild smoothness. **No state-space growth** — ψ_f=θ_f−φ_f reads only the DP
state θ_f and the per-frame-constant φ_f. Data-driven requirements (below): de-spike φ (median +
unit-vector Gaussian + outlier reject), key on Δψ *sign/trend* not magnitude, relax at the top (C3
strong there), give the reversal a *window* (release lag).

**GATE-0 — hand markup on s01, PASSED (2026-07-06, dev box).** Before touching the DP, the law was
measured on real frames (governance §1: exemplar-first, prove it before you build it). Mark
hand-marked the shaft of s01 → `/mnt/swingdata/Mark-Liversedge/2026-07-05_Mark-Liversedge_Wrist_02/
swing_0001/truth.json` (121 grip+head+θ labels + P1–P10 events; honest blur gaps from f504). Analysis
script stashed `…/tape_20260705/s01/psi_plot.py`; figure `…/s01/psi_markup.png` (committed
`docs/research/figures/club_track_psi_s01.png`, Fig 2 of the research paper). ψ = θ_markup − φ vs the
pose φ (`s01/anchors.csv`) and the independent v2 fusion truth (`s01/fusion/faceon_swing_fusion.csv`):
- **ψ monotone 55/58 backswing, 53/56 downswing**; all 6 exceptions on pose-φ glitch frames
  (f447/449/452, f491/556/559) — not real reversals.
- **θ hand-vs-v2 median 0.01° / p90 3.4°** (n=78 overlap) — independent adversarial cross-check of the
  v2 truth (research §5.1's requirement), passed.
- **The enemy is φ, not the physics:** θ (φ-independent) is pristinely monotone-one-reversal; all ψ
  scatter is pose φ (median jump 1.6°/f, **max 87°, 17 frames >20°**, clustered at top+impact).
- **Release lag:** ψ peaks ~8 frames after hand-top P4 (f482) → the transition window, and a coaching
  metric in its own right.

**Remaining gates (v3.0-internal → full protocol).** Changes the DP transition bands → the global path
re-solves → the frozen v3.0 track changes → **not a companion**. Extend Set S synth (plant a
ψ-non-monotone counterfeit — a line that satisfies C3 on θ but implies an impossible wrist — the rail
must veto it) → s01 single-swing (coverage/accuracy through impact ≥ current v3.0, zero flips) →
**corpus (studio PC) + fixture re-freeze**. Adjudicate, don't assume: (1) φ de-spike must not eat a
real fast arm rotation at the top; (2) transition-window width vs the release lag on faster corpus
swings ([[single-swing-never-judges-model-accuracy]]); (3) ψ̇_max from corpus, generous — never clip a
fast release.

#### v3.0-r1 AS-BUILT (2026-07-07): isotonic reconciliation supersedes the transition rail

Built the DP transition rail first (Δψ sign-lock + smoothness + rate bound; `club_track_v3.py`). It
passed synth + s01 but had to be **scoped release-only + band-protected** because a per-frame ψ-sign
penalty *fires on the pose-φ noise floor* (`…/scratchpad/dpsi_noise.py`: 25–62% of backswing steps show
a ~3° apparent un-cock that is pure noise — even on hand-marked θ). Mark's reframe closed it: **the
monotone law is TRUTH, so a violation *measures* error → FIT the truth (isotonic), don't penalise the
measurement.** Proven on real s01 first (`…/scratchpad/isotonic_demo.py`: fitting monotone ψ to the v3.0
release flattens the f521–525 re-hinge and the residual spikes exactly there). The rail was **replaced**.

**As-built (`reconcile_psi`, post a pure-C3 DP):** per-phase weighted robust isotonic — `_pava`
(Pool-Adjacent-Violators, exact/deterministic) + `robust_isotonic` (Huber-IRLS, `ISO_HUBER=8`,
`ISO_ITERS=3`). Weights `W_ISO={band:8,ray:2,pred:0.3}`, but **blur-zone (`RECON_PHASES`) non-band
frames forced to pred-weight** so only confirmed bands anchor the fit and it *interpolates* ψ across the
blur. Blur non-band θ reconstructed as `ψ_iso+φ` (arm-witness); measured θ kept elsewhere (self-anchors,
no corruption); the residual `ψ*−ψ_iso` written as a new **`psi_err`** column (φ-error/confidence map,
also `phi_clean=θ−ψ_iso`). New **`recon`** tier = blur frame moved off its evidence by >`RECON_TOL=6°`
(honest arm-witness; excluded from truth like pred; kind=pred in the track contract). `--no-psi-rail` =
pure v3.0. Bands pinned. `make_synth_v3.py`: blackout gap + fixed-θ decoy, FAIR criteria (gap θ-truth is
physically unrecoverable — dropped as a metric): known-θ on non-gap measured, **release ψ-viol 5→0**,
decoy rejected (gap-measured 6→4), residual localises (gap 3.5° vs well-measured 0.0°). Determinism
byte-identical. (Gap moved to **f[85,95]**, fully inside the impact phase, and the residual baseline
changed to the *well-measured* swing when the fix below narrowed `RECON_PHASES` — see re-gate.)

**First-cut gates (`RECON_PHASES=("impact","thru")`).** synth `--selftest` PASS. **s01 PASS, BEAT the
transition rail:** coverage 134=134 (no frame lost — the rail lost f520), accuracy improved (down bad>15
1→0, hand median 0.5→0.4), flips 0, impact re-hinge flattened + `psi_err` localises it. **Corpus A/B
(studio, all 10; `run_v32r1_corpus.py`):** release ψ-viol **104→35** (every swing improved), flips 0,
determinism byte-identical, down bad 1→0, medians held — **but thru p90 3.9→5.5 and thru coverage
678→649**. Diagnosed (`_diag_thru.py`): 24/29 worsened thru frames are `ray`, **all at f547–565
(follow-through, ~30–45f post-impact)** where the club is re-tracked but the folding arm φ degrades →
`ψ_iso+φ` pulls a good ray off truth.

**The physics behind it (Mark, 2026-07-07):** ψ is a **double reversal** — cock (top) → release to ≈0
(impact) → passive centripetal **re-hinge** (follow-through), with **forearm rotation** (a third DOF,
near-unobservable face-on: axially-symmetric shaft carries no roll signal) dominating the middle. The
single-tent monotone-release law is only valid **address→impact**. Roll as a real third dimension is
deferred to the **IMU / DTL / clubhead** channels (epistemic firewall), not the face-on shaft DP.

**FIX — applied + re-gated (2026-07-07): `RECON_PHASES = ("impact",)`.** Reconstruct only the impact
blur; through the follow-through trust the (returned-sharp) evidence and merely record the residual as a
roll-onset signal. The isotonic fit still *spans* impact+thru as one release block (so the monotone law
bridges the blur); only the θ **write-back** is impact-only. Re-gated in order — synth `--selftest` (gap
moved fully inside impact) → s01 A/B → studio corpus — all PASS:

| metric (studio corpus, 10 swings, vs v2) | OFF (v3.0) | ON impact+thru | **ON impact-only** |
|---|---|---|---|
| thru p90 θ-err (°) | 3.9 | 5.5 | **3.9** (regression fixed) |
| thru coverage (band+ray / 820) | 678 | 649 | **671** |
| down `bad>15` | 1 | 0 | **0** |
| release ψ-viol | 104 | 35 | **84** |
| flips / determinism | 0 / — | 0 / id. | **0 / byte-identical** |

ψ-viol 35→84 is correct, not a loss: the 35 came from flattening the *real* second-reversal/roll in the
follow-through (what corrupted thru accuracy); impact-only removes only the ~20 spurious impact-blur
re-hinges and lets the physical motion stand in `psi_err`. `recon` dropped from ≤13/swing → 0–4/swing (the
recovery mechanism). On s01, the rail changes θ on **2 frames** (the impact blur) vs pure v3.0 and adds
`psi_err` on 121 release frames; the v3.0 track is otherwise byte-identical. Scripts/logs this session:
`…/scratchpad/{score_s01_gate,followthrough_cmp,probe_gap*,off_delta}.py`, `corpus_{beforefix,afterfix}.log`;
before-fix per-swing outputs preserved at NAS `v32r1_beforefix/`.

**Studio-run playbook (learned this session).** Corpus batch runs on GOLFSIMPC via `ssh studio`
(PowerShell, venv `C:\PinPointStudio\shaftlab\.venv\Scripts\python.exe`, python 3.14/cv2 5.0/numpy 2.5).
Stage tools to the shared NAS `/mnt/swingdata/shaftlab/v3run/` (≡ `C:\PinPointStudio\shaftlab\v3run\`).
**Detach long jobs with `Invoke-CimMethod Win32_Process Create` running a `.ps1`** — `Start-Process
-NoNewWindow` is killed when the ssh session closes. PowerShell `*>` writes **UTF-16LE** → read logs with
`iconv -f UTF-16LE`. Use `python -u` for a live log. NEVER cross-host diff (studio cv2 5.0 ≠ dev 4.13).

### ACTION — v3.0 phase model: takeaway mislabelled as address  *(folds into the v3.0-r1 ψ-rail re-gate)*

**Surfaced by v3.2 (2026-07-06).** `segment_phases` triggers the swing on **grip** speed (`SW_SPD=8 px/f`),
but in the takeaway the club rotates about the wrist while the grip barely translates — grip speed is a
*lagging* proxy for club motion. So every frame before the first 8-px/f run (`bs0`) is dumped into `addr`,
which mislabels the whole early takeaway. On s01 that is f≈384–423 (v2 fusion **band-tracked** it,
θ 133→191°). **Two concrete harms** (measured, s01):
- **Coverage** — those ~20–40 frames are emitted `pred` (the ray tier is off at `addr`), unpublished,
  though v2 measured them → v3.0 *loses* takeaway coverage v2 had.
- **Accuracy** — the pred track **lags the true club by 10–18°** through the mid-takeaway (v3 116/122/128
  vs v2 133/140/143 at f398–402), the signature of `WMAX["addr"]=3.0` *throttling* the DP below the real
  takeaway rate (~3.5°/f). So relabelling alone is insufficient — it would publish a lagged θ; the fix
  must correct the DP **transition band**, not just the tier gate.

**Proposed fix (v3.0-internal).** Add an explicit **`takeaway` phase** over `[tk0, bs0)`, where `tk0` is
the hands-only creep onset — walk back from `bs0` over the grip-speed creep to the still threshold, the
**same `tk0` `address_theta_v3.detect_hold` already computes** (factor into one shared helper so the v3.0
takeaway-start and the v3.2 hold-end are the same boundary by construction). Give it `PHASE_SIGN=+1`
(chirality), `WMAX≈7` (between addr 3 and backswing 9 — de-throttle without opening the band so wide the
DP can jump to a counterfeit while the club is near-still), ray tier **enabled**, C4 wide cone enabled.
Prefer a *distinct* phase over folding into backswing (`bs0:=tk0`): backswing's `WMAX=9` is too permissive
for the near-still first takeaway frames.

**Why not a companion:** this changes the DP's transition bands → the global path re-solves → the frozen
v3.0 track changes. So it is **v3.0-internal** and requires the full protocol: re-gate synth → s01 →
**corpus + fixture re-freeze** (studio PC). ~15 lines of code, but a corpus re-run, not a patch.

**Adjudicate, don't assume:** (1) **C2** turns on for mid-swing — the club just leaving address can graze
the trail leg/foot; it's a finite penalty and the takeaway has strong band evidence, but check those
frames and fall back to "admit-not-sufficient" (like addr/impact) if it bites. (2) **Boundary/waggle** — a
jittery pre-shot waggle can brush the creep threshold; the hysteretic walk-back (stop at the *sustained*
rise) handles it, watch the noisier corpus swings. (3) **`WMAX≈7`** is a s01 estimate — the corpus (faster
/ fuller swings) sets the real value; tune there, not on s01 ([[single-swing-never-judges-model-accuracy]]).

> **Ordering:** the C4 exploration is now scoped and gate-0-proven — the **ψ-monotonicity rail**
> (v3.0-r1 above, Mark 2026-07-06). Revisit this takeaway fix *together with* that rail in one
> v3.0-internal corpus re-gate: the ψ-rail already de-throttles and carries the near-still early
> takeaway (grip barely translates, but ψ evolves and the arm moves), so the two share a single
> synth → s01 → corpus → freeze cycle rather than each paying for one. Re-check `tk0`/`WMAX["takeaway"]`
> under the rail before committing the phase.

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
- **New:** `tools/shaftlab/club_track_v3.py` (v3.0), `shift_stack_v3.py` (v3.1),
  `address_theta_v3.py` (v3.2 address/hold θ); Set S generators `make_synth_v3.py` / `make_synth_v31.py`
  / `make_synth_v32.py`; corpus runners `run_v3_corpus.py` / `run_v31_corpus.py` (v3.2 runner owed);
  harvested counterfeit fixtures.
- **Gate-0 ψ-rail (v3.0-r1) artefacts:** hand markup `…/2026-07-05_Mark-Liversedge_Wrist_02/swing_0001/
  truth.json` (independent θ witness); analysis `…/tape_20260705/s01/psi_plot.py`; figure
  `docs/research/figures/club_track_psi_s01.png`. Reuses `s01/anchors.csv` (pose φ) + `s01/fusion/
  faceon_swing_fusion.csv` (v2 truth).
- **Corpus/schema:** `src/Export/swing_exporter.cpp`, `swing_doc.cpp`, `swing_reanalyzer.cpp:536–553`
  (validity gates), `src/Pose/pose_estimator_vitpose.cpp` + `src/Analysis/pose_runner.cpp` (offline pose).
- **Future port:** `.claude/attic/auto-markup-2026-07-02/shaft_annotate_port.{h,cpp}`,
  `src/Analysis/tests/swing_window_parity_test.cpp`, `src/Pose/pose_estimator_base.*`.
