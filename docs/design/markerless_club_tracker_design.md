# Markerless club tracking — design

**Status: design 2026-09-08; P0–P3b built and graded the same day (§6). P2 missed its targets and redefined Phase 3; P3a meets the direction, position and scale gates; P3b wires the lock into the tracker's consumers, dark behind `shaft.seg.enabled`. Every result is reported against the marked club's band lock on the same frames (§5.1). P4 adds the two club-record fields, the UI, the persistence and the still-frame probe. P5 graded 2026-09-09 on an unmarked 6-iron (§5.1): the segment lock covers 40% of span frames against the taped club's 54% segment / 26% band, at 1.0° from the tracker's direction; club length matches the ball to 3% when a ball exists and is unsolved without one. Open: the scale tail, the held finish, length without a ball, P6 (flip).** Successor to club tracking v3 (`club_tracking_v3_design.md`,
as-built `shaft_track_assembly.*` / `shaft_tracker_math.*`). Premise, from Mark: the
retroreflective bands on the shaft change the club's swing weight, and a club whose swing
weight has changed produces a different swing. The tracker must therefore reach today's
instrumented-grade detection on a **bare club**, using the reflective properties of the
steel shaft where that helps. Two riders: (1) when a club *is* marked, the marks must still
be used; (2) the club record gains a **shaft-length setting** so the detector can turn a
bare shaft into measured geometry the way it turns band ratios into geometry today.

This document is written to be read start to finish by someone who has not worked in the
shaft tracker. §1–§3 explain what exists and what the evidence says; §4 is the design;
§5–§7 are validation, implementation and risk. Numbers are from the analysis of
2026-09-08 (corpus frames, code, and the research record) and are cited to file and
line where they come from code.

---

## 0. Summary

Three findings drive the design:

1. **The tape is not what makes the shaft bright.** On the same 7-iron, same rig, same
   6.57 ms exposure, the bare steel shaft saturates the sensor (255) at address and
   through the lit downswing exactly as the bands do. In the dark arc at the top of the
   backswing the bare steel between the bands still reads 35–90 grey levels over a
   background of 6–8 — a continuous line that the existing ridge sweep already credits
   (it counts any sample above 8 as support and clips at 90). §2 has the measurements.
2. **What the tape uniquely provides in the code is metric geometry and a pin, not
   contrast.** The band matcher (E1) returns a per-frame pixel scale `s` and butt offset
   `r0`; a band lock is a negative-emission well the global Viterbi path is forced through,
   the BAND tier, an 8-vs-2 weight in the wrist-rail fit, and — via the `bandNear` clause —
   the only route by which a Finish or static frame can ever be published. §1 has the
   dependency map with line references.
3. **A bare shaft has its own along-shaft landmarks.** The grip end (black rubber → bright
   steel) and the ferrule/hosel (bright steel → black plastic → wide chrome head) sit at
   known millimetre positions from the butt. Along-shaft positions survive tangential
   motion blur for exactly the reason band spacings do. Two landmarks at known distances
   give the same two unknowns the bands give — `s` and `r0` — with no 180° ambiguity,
   because the grip end is the one near the hands.

Phase 0 of the plan — a corpus-wide measurement with the tracker's own ridge reduction —
was run on 2026-09-08 and is reported in §2.4. Its headline: between the bands on the
taped 7-iron, in the same frames and the same light, bare steel carries an E2 evidence of
60–85 grey levels in every phase but delivery, against the engine's clip of 90. For the
ridge engine the bands are already almost fully redundant; only the band *matcher* (E1)
consumes the extra brightness. It also found that the corpus club labels are wrong: the
July "DRIVER" sessions are the taped 7-iron, so the taped population is 64 swings and the
genuinely unmarked full-resolution population is the ten 3 July swings.

The design adds a fourth evidence engine, **E4 — the steel-segment lock**, which fits the
shaft's own landmarks (plus any band blobs that happen to be present) into the same
`(θ, s, r0)` result the band matcher produces; unifies BAND and SEGMENT locks into one
*lock* concept that the emission, DP, reconcile, tiering, length ladder, fusion and head
placement all consume; adds `shaftLengthMm` (and the already-plumbed `hoselFromButtMm`)
to the club record; and ports the address-phase profile machinery that never left the
Python exemplar. Everything ships dark behind `shaft.seg.enabled` and is graded on the
existing taped corpus first, because the segment landmarks exist on taped clubs too.

Explicitly out of scope: shortening the exposure (Mark's 2026-07-05 ruling, recorded in
`stripe_fusion_design.md:21-26`, stands), IR illumination (launch-monitor conflict), a
learned shaft detector, and graphite shafts as a gated target (§8).

---

## 1. What the tape does today — the dependency map

The tape was introduced as a *measuring instrument* to generate truth for the passive
detector (`club_detection_from_video.md:150-155`), and became load-bearing in the product
path along the way. Six 25 mm glass-bead retroreflective bands, grouped 2-1-3 from grip to
head, at 308, 362, 560, 758, 808, 854 mm from the butt on the lab 7-iron; hosel at 882 mm;
club length 940 mm (`/mnt/swingdata/shaftlab/clubs.json`; protocol in
`docs/validation/instrumented_club_protocol.md`). The tracker learns of them through the
athlete's club record → `ShotAnalysisJob::bandCentersMm` (`shot_analyzer.h:48-53`,
`shot_processor.cpp:1050-1053`). An empty list is the untaped switch: `frameBandMatch`
returns `ok=false` when fewer than two bands are recorded (`shaft_tracker_math.cpp:290`).

### 1.1 Roles, in order of how hard they are to replace

| Role | Where in code | What happens untaped today |
|---|---|---|
| **Per-frame metric scale** `s` (px/mm) and **butt offset** `r0` (mm) | `BandMatch.s/.r0` (`shaft_tracker_math.h:86-92`); head placed directly `butt = grip − s·r0·û`, `head = butt + s·clubLenMm·û` (`shaft_track_assembly.cpp:1963-1971`); ladder rung 2 `sTypical·(clubLenMm − r0Med)` (`:1230-1231`); E-band fusion estimator (`club_length_fusion.h:30`) | No per-frame scale at all. Ladder falls to rung 1 (ball, address only, needs a ball lock), else rung 3 (stature surrogate, documented as reading ~33% short, `club_length_fusion.h:32-34`), else `0.45·frameH`. |
| **The DP pin** | band bin raised to `ev=1` then `em[bandBin] = −wBand` (8.0) applied *last* so it dominates every gate (`shaft_track_assembly.cpp:900-904, 966`) | The Viterbi has no hard anchor; θ rests on normalised E2 plus the soft C1/C2/C4 priors. The research record says the through-swing came out ~90° wrong before the well existed (`club_detection_from_video.md:778-784`). |
| **BAND tier** | `tier=BAND, conf = min(0.9, 0.75 + 0.05·(n−4))` when the DP θ is within `bandTol` 6° of the band θ (`:1690-1692`) | Tier ceiling drops to RAY (0.55) or WEDGE. |
| **Publishability of Finish and static frames** | `verifiable = (phase==Finish) ? bandNear : (!stat[i] \|\| bandNear)` with `bandNear` = a band lock within ±5 frames (`:1703-1705`) | **No Finish frame and no still address-hold frame can ever reach RAY.** This is a gating artefact, not physics. |
| **Wrist-rail weight** | `wIsoBand` 8 vs `wIsoRay` 2 vs `wIsoPred` 0.3 in `reconcilePsi` (`:1084-1088`) | Rail fitted from ¼-weight evidence; every Impact-phase frame is reconstructed from the arm (`!bandOk` universally true). |
| **Truth supply** | 1,033 fast-phase truth samples, every Phase 5–9 corpus gate; 64 of the library's 83 tracked swings are the taped club (§2.4) | No dense downswing/impact truth; blur defeats human annotators too. |
| 180° flip resolution | 2-vs-3 asymmetry | **Already replaced** by the one-reversal law (`club_tracking_v3_design.md` C3) and the grip→ball direction. |
| Scale floor | band `s` | **Already superseded** by the ball anchor `L_px`, "now the top of the length hierarchy" (`shaft_tracker_impl.md:265-278`). |

Two fields are plumbed into the job and read by nothing in the analysis stack:
`ShotAnalysisJob::shaftType` ("steel" | "graphite") and `hoselFromButtMm`
(`shot_analyzer.h:54-55`). Both are consumed by this design.

### 1.2 What the tape does *not* do

The research record's own A/B on the passive detector (the `tape_20260704` pilot,
`club_detection_from_video.md:617-627`): tape *helps address and backswing*, *leaves the
downswing unchanged* ("blur destroys the bands there as thoroughly as everything else"),
and *hurts the finish* (two θ clusters inside one grip-still run). And the instrumented
detector's own per-phase table has it **absent at address** (bands bloom or vanish on the
blown mat) and **absent at the finish** (`club_detection_from_video.md:262-269`). So the
tape's product-path value is concentrated in exactly the roles of §1.1, not in coverage
per se.

---

## 2. What bare steel already gives — the evidence

### 2.1 Frames

Same club (the lab 7-iron), same rig (Chameleon3, 1280×1024, 150.7 fps, 6.57 ms
auto-exposure — 98% duty cycle), before taping (session 2026-07-03, `swing_0006`) and
after (2026-08-18 Wrist_02, `swing_0002`). Grey-level readings are 8-bit luma.

| Regime | Bare steel (07-03) | Taped (08-18) |
|---|---|---|
| Address, shaft against dark trousers | peak 255, clipped, ~6 px wide line | 255, clipped |
| Address, tip section over the blown mat | dark line on 254 (the E2 dark-polarity regime) | same; bands white-on-white, invisible |
| Downswing, −80 ms | saturated blurred fan, hands to tip | saturated fan **with band streaks inside it** |
| Delivery, −25 ms | wide saturated fan, faint internal lines | same, with streaks |
| Top of backswing (dark arc) | continuous thin line, 240–250 peak on 10–20 bg | **bands 227; the bare steel between them 35–90 on 6–8 bg** |
| Finish (+250 ms) | bright streak near the hands, fading upward out of the light | band streaks visible further up |

**The confound, stated plainly:** the 07-03 session had daylight through the windows and
the 08-18 session was blacked out with the simulator screen on. Cross-session brightness
comparisons flatter the bare club. The honest dark-arc number is the *within-frame* one:
in the taped 08-18 top-of-backswing frame the bare steel in the gaps between bands reads
35–90 over a 6–8 background. That is dim, but it is a continuous line, and the ridge
sweep's evidence term `e = max(5 lateral) − bg − 12`, clipped to [−30, +90] with support
counted at `e > 8` (`shaft_tracker_math.cpp:117-141`), credits it. The research record's
"ray tier 0.4–0.8° in the backswing" was measured on exactly this signal.

### 2.2 Why a cylinder is not a mirror

The research record models polished steel as a mirror that "throws a bright highlight only
when the angle happens to line up" (`club_detection_from_video.md:114-128`). That is true
of a *flat* specular surface. A straight **cylinder** under a point source throws a
specular line along its entire length for every orientation except the one where the
shaft axis lies along the light–camera bisector: at every station along the shaft there is
one generatrix whose normal bisects the light and view directions, and it is the same
generatrix all the way along. With the ring light at the lens that generatrix is the one
facing the camera, so the highlight is present in every frame in which the shaft is not
pointing at the camera — the same condition under which the bands are visible.

What differs is *gain*. Retro tape returns the ring light's beam to its source within a
1–2° cone; a specular cylinder spreads the reflected beam into a fan across viewing
angles, so the camera collects a small fraction of it. The measured ratio in the dark arc
(bands ~227 vs bare gaps 35–90) is that gain difference. It scales linearly with source
power, which is the one hardware lever this design leans on (§4.7). In the lit arc the
ambient studio downlight already drives bare steel into clipping, so the gain difference
is invisible there.

Graphite: a gloss clear-coat gives the same geometric line at Fresnel reflectance (~4%
vs steel's ~60%); matte graphite gives nothing. The design therefore targets steel and
treats graphite as a lower-confidence polarity variant, not a gated case (§8).

### 2.3 What this means for the "passive = weak signal" premise

The research record holds shift-and-stack "in reserve for the regime it was actually built
for: the passive, un-taped club, where the per-frame signal genuinely is weak"
(`club_detection_from_video.md:1139-1169`). For a steel shaft in this rig the premise
does not hold: the per-frame signal is saturated in the lit arc and a real line in the
dark arc. Stacking remains the right tool for the *dark-arc dim line* if the ring light
cannot be raised, and Layer B already stacks ±4 frames about the grip
(`shaft_position_fit.h:44-56`). It is not the primary mechanism here.


### 2.4 Phase 0 — corpus measurement (2026-09-08)

**Tool.** `tools/shaftlab/steel_profile_probe.py`. For every 150 fps face-on swing with a
recorded club track (83 swings, 21,635 frames inside the swing span), every frame is
sampled from the grip anchor outward along the tracked shaft direction (±6°, best E2
score wins) at 1 px radial steps with E2's own lateral reduction — background = median of
4 samples at ±9/±12 px, on-ridge = max of 5 at 0/±1/±2 px when background ≤ 200 else min
of 5, evidence `e = ±(on − bg) − 12` clipped to [−30, 90]. The longest evidenced run
(`e > 8`, holes ≤ 4 px) is "the shaft". On taped clubs the run is split into band plateaus
(≥ 0.8 × the run's p95 on-ridge level, separated by dips to ≤ 0.6 ×) and the bare-steel
gaps between them. Per-frame rows: `docs/research/data/markerless/steel_profile_probe.csv`;
tables: `steel_profile_probe_summary.md` beside it.

**Population, and a correction to the record.** The corpus manifest's `conditions.club`
says DRIVER for the 07-04, 07-08, 07-09 and 07-10 sessions. Address frames show an iron
head on every one, top-of-backswing frames show a banded shaft, and the recorded tracks
carry 40–144 BAND-tier frames per swing on 07-05, 07-09, 07-10 and 08-18 — E1 cannot lock
without tape. 07-04 is the `tape_20260704` pilot the research record describes as 15
taped 7-iron swings. Pre-0.1.10011 `swing.json` carried the athlete's single club record
(bands, shaft type) whatever club was hit, and no club name. Conditions were therefore
assigned from evidence (≥ 20 BAND-tier frames, or the two documented/inspected sessions),
not labels:

| condition | sessions | swings | frames | note |
|---|---|---|---|---|
| taped 7-iron (steel) | 07-04, 07-05, 07-08, 07-09, 07-10, 08-18 | **64** | 16,619 | the research record's "25 taped swings" is an undercount |
| untaped steel, 1280 px | 07-03 (7i ×2, 9i, GW ×2) | 5 | 1,499 | daylight room |
| untaped, wood/hybrid/driver *label* | 07-03 | 5 | 1,185 | label unverified; 07-03 swing_0005 "DRIVER" shows an iron head |
| untaped steel, 720 px wide | 06-11 (gap wedge) | 9 | 2,332 | different framing; kept separate |

The genuinely unmarked full-resolution population is ten swings from one daylight
session. §5.2's new capture is not optional.

**B — bare steel between the bands, same frame, same light** (taped 7-iron; frames with ≥ 2
plateaus on the run):

| phase | frames | gap steel e p50 | e p10 | gap on-ridge p50 | band on-ridge p50 | steel / band | background dark / mid / blown |
|---|---|---|---|---|---|---|---|
| address | 3,601 | 63 | 22 | 104 | 252 | 0.41 | 65 / 34 / 1 |
| backswing | 2,275 | 69 | 33 | 95 | 250 | 0.38 | 97 / 3 / 0 |
| top | 763 | **85** | 27 | 109 | 227 | 0.48 | 100 / 0 / 0 |
| downswing | 1,174 | 60 | 22 | 97 | 234 | 0.41 | 74 / 26 / 0 |
| delivery | 184 | **34** | 7 | 120 | 248 | 0.48 | 1 / 99 / 0 |
| through | 1,120 | 55 | 16 | 96 | 174 | 0.56 | 71 / 29 / 0 |
| finish | 1,080 | 48 | 18 | 72 | 149 | 0.48 | 98 / 2 / 0 |

Reading it: the bare steel's on-ridge grey level is 0.4–0.5 of the band's everywhere. But
E2 does not score grey levels, it scores evidence clipped at 90, and bare steel sits at
60–85 in every phase except delivery — 70–95% of the ceiling. The band's extra brightness
is invisible to the ridge engine; it is consumed only by E1's saturation threshold
(`satT` 235) and blob detector. Delivery is the exception (e 34, p10 7): the hands-to-tip
zone in the last 40 ms before impact, 99% mid-grey background, where the shaft is a blur
fan and the wedge engine, not the ridge, is the instrument.

**C — by background regime, unmarked steel run vs taped-club gaps.** "Detectable" = run
≥ 90 px with support ≥ 0.40, the tracker's own RAY gates:

| condition | regime | frames | e p50 | e p10 | on p50 | detectable |
|---|---|---|---|---|---|---|
| taped 7-iron (whole run, bands included) | dark | 11,323 | 90 | 65 | 230 | 70% |
| taped 7-iron | mid | 4,562 | 90 | 35 | 233 | 50% |
| untaped steel 1280 (07-03) | dark | 745 | 90 | 40 | 247 | 60% |
| untaped steel 1280 | mid | 748 | 90 | 48 | 245 | 43% |
| untaped steel 720 (06-11) | dark | 1,021 | 90 | 66 | 205 | 80% |
| untaped steel 720 | mid | 1,242 | 73 | 46 | 194 | 39% |
| taped-club **gaps** (bare steel) | dark | 8,026 | 66 | 26 | 94 | — |
| taped-club gaps | mid | 2,143 | 49 | 12 | 117 | — |

By regime, the unmarked steel run passes the RAY gates within ~10 points of the taped run
(dark 60–80% vs 70%; mid 39–43% vs 50%) — a difference of degree, not of class. Per phase
the 07-03 unmarked 7-iron is weaker at the top (21% detectable vs 86%, on-ridge 112 vs
223) and in the finish (5% vs 38%); the 06-11 wedge, in a different framing, is not (top
64%). The top-of-backswing deficit on 07-03 is consistent with that session having had no
on-axis light on a dark ceiling — the one regime where bare steel depends on the ring
light (§2.2) — and cannot be separated from that confound with this data.

**D — what the recorded tracker already does on unmarked steel.** Tier per phase, 07-03
unmarked irons: backswing 100% RAY, top 100% RAY, downswing 84% RAY + 13% other measured,
delivery 87% RAY + 13% WEDGE, through 52% RAY + 40% WEDGE, finish 8% RAY / 90% PRED. The
taped population's finish is 22% BAND + 21% RAY / 56% PRED. So on the same rig the ray-only
path already carries an unmarked steel shaft through the backswing, top and downswing at
RAY tier; the structural losses are the finish (the `verifiable` clause, §1.1), the
per-frame scale, and the DP pin — the three things §4 supplies.

**Two findings that changed the design.**
1. *Specular ripple mimics bands.* On the unmarked 06-11 and 07-03 shafts in the dark arc,
   the on-ridge profile shows dash-like highlights that a relative plateau detector counts
   as 3+ "bands" on 30–70% of frames. E4 therefore keys on the **ends** of the steel run
   (§4.2), never on dash structure, and the band matcher's ratio test remains the only
   thing that may call a dash a band.
2. *Delivery is a wedge problem.* Bare-steel evidence collapses to 34 only in the last
   40 ms before impact, where it is a fan; TERMINUS mode along the wedge centroid (§4.5)
   is the right shape there, and no amount of ring light changes that.

**Caveats.** The probe samples along the *tracked* θ, so frames where the tracker is
wrong by more than 6° are measured on the wrong ray; on unmarked swings the track is RAY
or PRED tier, so the "detectable" percentages inherit the tracker's own selection. The
within-frame gap statistics (B) do not have this problem: bands and gaps sit on the same
ray. Frame counts per phase differ across conditions because the phase bins come from
each swing's recorded P-ladder.

---

## 3. The tracker as built — the parts this design touches

A short tour so the change in §4 can be followed. The tracker is a **batch global
optimiser** over the coverage span, not a recursive filter. Per frame it receives the
grey image, the grip anchor `g` (midpoint of the two hands, `shaft_tracker.cpp:168-170`),
the lead-forearm direction φ, and eight body joints. Rays are cast from `g` over 360
directions on a 1° grid, radius 8 → 470 px in 2 px steps (`RidgeConfig`,
`shaft_tracker_math.h:43-49`).

**E2 — polarity-aware ridge sweep** (`ridgeSweep`, `shaft_tracker_math.cpp:66-143`). Per θ,
per radius: background `bg` = median of four lateral samples at ±9 and ±12 px; on-ridge =
lateral max of five samples at 0, ±1, ±2 px when `bg ≤ bgHi` (200), lateral *min* when
`bg > bgHi` (the blown-mat regime — the shaft must be dark there); evidence
`e = ±(on − bg) − 12`, clipped to [−30, +90]; cumulative sum normalised by `1/√(j+8)`;
score = max over terminus radii beyond `minLenPx` 90 px; `rEnd` = argmax radius;
`support` = fraction of samples with `e > 8`. Runs twice per frame: on the raw frame and
on `|frame − sceneMedian|` (bright-only), each channel gated by an absolute pre-normalisation
floor `evAbsFloor` 100 (`shaft_track_assembly.h:131`), percentile-normalised, and combined
by max.

**E1 — band match** (`frameBandMatch`, `shaft_tracker_math.cpp:286-369`). Global threshold
at `satT` 235 on the full frame, 8-connected components of area 3–2500 px within
`rmax = 0.62·frameH` of the grip, ≤ 20 blobs; for each blob-pair direction passing within
`gripGate` 80 px of the grip, collinear inliers at `latTol` 4 px; solve `s, r0` from
blob-pair/band-pair hypotheses, `s ∈ [0.05, 0.55]` px/mm, `r0 ∈ [−50, 260]` mm; accept at
`n ≥ 4` with RMS ≤ 1.5 (n=4) or ≤ 3.0 (n≥5); a within-group gap (< 60 mm) must dip below
`gapDark` 222 — bare steel between bands must be sub-saturated (the anti-speckle test).
The assembly accepts a match only when `0 < r0 ≤ 260` (`shaft_track_assembly.cpp:1519`).

**E3 — blur wedge** (`shaft_wedge.h`). On frames whose predicted club rate exceeds 720°/s a
proximal sweep finds the motion-streak *fan* rather than a line; centroid = mid-exposure θ,
width ≈ ω·t_exp.

**Emission** (`frameEmission`, `shaft_track_assembly.cpp:885-967`), in order: band bin
raised to `ev = 1`; `em = wE2·(1 − ev)` (10); C4 arm veto +16 within 12° of φ+180; C4 wide
cone +4 outside a 150° half-cone (off address/finish/top); C1 +10 where the *reverse* ray
has normalised evidence > 0.45 off the forearm; C2 +13 where > 50% of ray samples fall
inside the inflated body hull mid-swing; the hand-axis prior (dark); **band well
`−wBand` (8) last**.

**Global Viterbi** over the θ grid with phase-signed, rate-limited transitions (3°/frame at
address … 24°/frame at impact) and quadratic smoothness; then **ψ-isotonic reconciliation**
(ψ = θ − φ monotone per block, Huber-IRLS PAVA) with per-frame weights band 8 / ray 2 /
pred 0.3; in the Impact phase non-band frames have θ *reconstructed* as ψ_iso + φ.

**Tiering** (`:1683-1725`): BAND > RAY (`EV ≥ 0.45`, beats the reverse ray by 1.15×,
`SUP ≥ 0.4`, and *verifiable*) > WEDGE > PRED; RECON overrides when the reconcile moved θ
more than 6°. `coverage = spanMeas/spanFrames` over BAND|RAY|WEDGE; `valid` at ≥ 0.60.

**Length ladder** (`projectedClubLenPx`, `:1222-1244`): rung 1 ball-measured grip→ball at
address; rung 2 band `sTypical·(clubLenMm − r0Med)`; rung 3 stature surrogate
(`px/m = poseExtent / (0.83 × 1.70 m)`, minus 0.13 m grip-down); rung 4 `0.45·frameH`;
floored at 1.05× shoulder→grip, capped at `1.1·L_ball` or `0.62·frameH`.
**Fusion** (`club_length_fusion.h`): E-ball, E-band, E-head (p95 of Stage-2 head radius),
E-prior (persistent EMA); E-pose is a sanity bound only.

**Head placement** (`:1960-1990`): BAND frames place the head from `(s, r0, clubLenMm)`
directly and are never overwritten by the Stage-2 measured head; other frames take the
Stage-2 terminus or the projected length.

---

## 4. Design

### 4.1 The shaft as a ruler — landmarks and the club record

A bare steel shaft presents, along its axis from the hands outward:

```
butt ──── grip (black rubber, matte) ──┤ grip end ├── bright steel ──┤ ferrule ├─ hosel ─ head
  0                                   ~265 mm                        ~870 mm   882 mm  940 mm
```

Three along-axis transitions are detectable in the ray profile:

| Landmark | Transition | mm from butt | Source of the mm value |
|---|---|---|---|
| **Grip end** `m_g` | dark rubber → bright steel (or dark-on-blown-mat) | `gripEndFromButtMm` | derived: `hoselFromButtMm − shaftLengthMm` |
| **Ferrule / hosel top** `m_f` | thin bright line → 3–5 px dark gap → *wide* chrome | `hoselFromButtMm` (± the ferrule, ~12–15 mm) | club record, existing field |
| Head far edge | wide bright blob ends | `lengthMm` | club record, existing field |

At the rig's typical scale (~3.5 mm/px at the club, `instrumented_club_protocol.md:50`) the
exposed steel of the 7-iron (882 − 265 ≈ 617 mm) is ~175 px, the ferrule ~4 px, and the
grip-end lies 20–30 px beyond the bottom of the hands. The ferrule alone is marginal; the
*end of the thin run* is not, because the hosel and head that follow are several times
wider than the shaft. The design therefore defines the distal landmark as "where the thin
bright run ends", referenced to `hoselFromButtMm` with a ±15 mm (≈ ±4 px) tolerance —
about 2–3% of the 175 px span, which is well inside what the length ladder tolerates.

**Club record changes** (`athlete_controller.h:120-131`, defaults at
`athlete_controller.cpp:380-395`):

| Field | Type | Meaning | Default |
|---|---|---|---|
| `shaftLengthMm` *(new)* | int | exposed shaft: bottom of grip to top of hosel, measured with a tape | `hoselFromButtMm − 265` when hosel is known; else 0 = unknown |
| `hoselFromButtMm` *(existing, now read)* | int | butt to top of hosel | per-family seed: irons/wedges `lengthMm − 58` (the lab 7-iron measures 940 − 882 = 58), hybrids `− 60`, woods/driver `− 50`, putter 0 (unsupported) |
| `bandCentersMm` *(existing)* | list | retro-band centres | empty = unmarked |
| `handsEndMm` *(new, P3a/P4)* | int | where the golfer's hands end on the grip, mm from the butt, measured once with a tape at address | 0 = unknown ⇒ 180 (corpus: 165 p50, 121–208) |
| `shaftType` *(existing, now read)* | steel/graphite | polarity prior for E4 | "steel" |

`gripEndFromButtMm` is not stored; it is `hoselFromButtMm − shaftLengthMm` at job-build
time. The UI (`AthleteClubsSection.qml`) gains two fields under CLUB LENGTH, **SHAFT
LENGTH (MM)** and **HANDS END (MM FROM BUTT)**, and the section's helper text says what
they are for. `handsEndMm` is physically a property of the golfer's grip, not the club,
but the club record is already per athlete and the athlete form's save path is a fixed
positional signature through QML — so it lives with the club (P4 decision, 2026-09-08).
Band fields stay as they are.

`ShotAnalysisJob` gains `shaftLengthMm` and starts filling `gripEndFromButtMm`
(`shot_processor.cpp:1046-1058`); `swing.json` `capture.club` persists both so re-analysis
is deterministic. Missing `shaftLengthMm` on an old swing takes the derived default and the
analysis stamps `club.shaftGeomSource = "default" | "measured"`.

### 4.2 E4 — the steel-segment lock

E4 is a per-frame evidence engine alongside E1/E2/E3, living in `shaft_tracker_math.*`
and ported from a validated Python exemplar first (§6). It answers the same question as E1
— *which θ, at what scale, with the butt where?* — from the shaft's own landmarks.

**Step 1 — candidate directions.** E4 does not sweep 360°. It takes the E2 local maxima
above `rayEvMin` (≤ `seg.maxCand` 6 per frame), plus the band θ when E1 locked, plus the
wedge centroid when the wedge triggered. Cost is therefore bounded by a handful of
profiles per frame.

**Step 2 — the along-ray profile.** For each candidate θ, sample `r = 0 … rmax` in 1 px
steps with *the same* lateral reduction E2 uses (median-of-4 background at ±9/±12,
max-of-5 or min-of-5 on-ridge by the `bgHi` split, −12 bias) — refactored out of
`ridgeSweep` into a shared `rayProfile()` so the two engines cannot drift. Two extra
per-sample quantities are recorded: `wide(r)` = mean of the lateral samples at ±6 and
±8 px (to tell a thin line from a broad blob), and `bg(r)`.

**Step 3 — run-length parse.** The profile is turned into an ordered list of runs:
`BRIGHT` (`e ≥ seg.eOn` 30 for ≥ `seg.minRunPx` 6), `DARK` (`e ≤ 8` for ≥ 3 px),
`WIDE` (`wide − bg ≥ seg.eOn`, i.e. the lateral samples are bright too: hands, glove,
head, sleeve), and `UNK` (over-saturated background where neither polarity is evaluable —
the `evaluable = (bg ≤ BG_HI) & (steel ≤ STEEL_HEADROOM)` mask from
`stripe_fusion.py:120-133`, which never reached C++ and is ported here).

**Step 4 — landmark extraction.**
- *Proximal onset* `r_g`: the first BRIGHT run that begins after the last WIDE run within
  `r < 0.45·rmax` (the hands/glove bloom) and after a DARK run of ≥ 3 px (the visible
  grip). If the hands bloom straight into the steel (common in the blur regime) the onset
  is *unresolved* and E4 falls back to the terminus-only mode below.
- *Distal terminus* `r_f`: the end of the longest BRIGHT run that starts at or after `r_g`
  (or, unresolved, at or after the last WIDE run), where the run is followed by DARK or by
  WIDE. A BRIGHT→WIDE transition with no dark gap is accepted (ferrule unresolved, hosel
  bright) at the wider ±15 mm tolerance; BRIGHT→DARK followed by WIDE within 25 px is the
  ferrule-resolved case at ±5 mm.
- *Bands, when present*: BRIGHT runs at saturation (`≥ satT`) separated by DARK-or-dim
  gaps ≤ `gapDark` inside the steel run are collected as additional landmarks with their
  centres. E4 does not need them, but uses them.

**Step 5 — the fit.** The landmark set is `{(r_k, m_k)}`: `(r_g, m_g)`, `(r_f, m_f)`, and
`(r_band_j, bandCentersMm_j)` for any bands, assigned in order along the ray. With ≥ 2
landmarks solve `r = s·(m − r0)` by least squares — the same model E1's `matchPattern`
re-fit uses, so `s` and `r0` mean exactly what they mean in `BandMatch`. Gates:

| Gate | Value | Why |
|---|---|---|
| `s` | 0.05–0.55 px/mm | E1's foreshortening bounds, unchanged |
| `r0` | (0, 260] mm | C1 butt-termination: the anchor sits inside the grip |
| Steel-run support | ≥ 0.80 of `[r_g, r_f]` with `e > 8` | a real shaft is continuous; a mat edge or crease is not |
| Onset/terminus edge contrast | ≥ 30 grey levels within 4 px | the landmarks are transitions, not gradients |
| Length consistency | `\|s·(lengthMm − r0) − L̂\| ≤ 0.20·L̂` when a fused/ball length `L̂` exists | ties the new scale to the validated ladder |
| Temporal scale | `s` within ±25% of the median `s` over accepted neighbours (±8 frames) when ≥ 3 exist | foreshortening changes slowly except through the top |
| Reverse ray | no BRIGHT run ≥ 60 px on θ+180 outside the forearm sector | C1 weak form, as in `frameEmission` |
| Fit RMS | ≤ 3 px with bands, n/a with 2 landmarks | E1's n≥5 gate |

**Three details the implementation settled (P1).**
- *The distal millimetre depends on what ended the run.* A resolved ferrule (dark gap,
  then the head within 25 px) puts the steel's end at `hoselMm − ferruleMm` (12 mm, σ 5);
  a run that reaches the wide head with no gap ends at `hoselMm + hoselLenMm` (40 mm,
  σ 15); a bright→dark end with no head in reach is referenced to the ferrule at σ 15.
  A ferrule of 3–4 px is shorter than the hole the run bridges, so a run that reached the
  head is scanned back for ≥ 2 consecutive dark samples and cut there.
- *"Wide at once" is tested before "dark then wide".* The interior of a wide blob is
  evidence-free (`on ≈ bg ⇒ e ≈ −12`) and reads as dark, so a head beginning at the
  run's end would otherwise be mislabelled as a ferrule gap.
- *The grip must be darker than the steel, not merely evidence-free.* The hands' bloom is
  also `e ≈ −12`. The onset therefore requires the on-ridge level to step up by `edgeMin`
  into the run (bright regime); over a blown background the grip is itself a dark line and
  the onset is unresolved by construction.
- Bands are assigned in two passes: loose (8 px or 8% of the run) from the two-end fit,
  refit, then E1's tight tolerance from the refit.

**Result.** `SegmentLock { ok, mode, thetaDeg, s, r0, rG, rF, nLandmarks, rms }` with
`mode ∈ {FULL, TERMINUS}`. TERMINUS mode (onset unresolved) borrows `s` from the temporal
median and reports only `θ` and `r_f`; it exists so the blur regime, where the hands bloom
into the streak but the far end of the fan is still radially sharp, keeps a pin. It
carries lower confidence and does not contribute a fresh scale.

**Polarity.** The parse runs on signed `e`, so a dark steel line over the blown mat at
address is a BRIGHT run in evidence terms (E2's `bg > bgHi` branch). This is where E4
beats E1 outright: at address the bands are white-on-white and invisible while the bare
steel is a clean dark line on 254 (§2.1), so an unmarked-club address lock is *easier* than
a marked one. `shaftType == "graphite"` inverts the expected polarity on non-blown
backgrounds and lowers `seg.conf` by 0.10; it is not a gated target.

### 4.3 One lock, two sources — using marks when present

Today `band[i]`/`bandOk[i]` are threaded through emission, tiering, reconcile, ladder,
fusion and placement as *the* lock. The design introduces:

```cpp
struct ShaftLock {            // shaft_track_assembly.h
    enum Source : uint8_t { None, Band, Segment };
    Source  src = None;
    float   thetaDeg, s, r0;  // identical semantics to BandMatch
    float   conf;             // 0.75–0.9 band, 0.70 segment-full, 0.62 segment-terminus
    int     n;                // landmark count
};
```

Per frame: `lock[i] = bandOk ? fromBand(band[i]) : segOk ? fromSeg(seg[i]) : none`.
**Band wins when both exist** — its 4–6 collinear saturated blobs are the stronger
witness and its numbers are corpus-validated to 0.3°. Segment fills every frame band
cannot: address and the blown-mat tip, the dark arc where bands are dashes but E1's gap
test fails, the finish, and the whole swing of an unmarked club. When both exist and
disagree by more than `bandTol` the frame is logged (`trace->lockConflict`) and band is
kept; a conflict rate above 2% on the taped corpus fails the gate (§5).

Every consumer switches from `bandOk[i]` to `lock[i].src != None`:

| Consumer | Today | Design |
|---|---|---|
| Emission raise + well | `ev[bi]=1; em[bi] = −wBand` | raise; well depth `−wBand` (8) for Band, `−wSeg` (default 6, `shaft.seg.well`) for Segment-full, `−wSegT` (4) for terminus — all applied *last*, above the hand-axis prior, clamped like the wedge well |
| Tier | BAND at conf 0.75+ | BAND unchanged; new **SEG** tier at `seg.conf` 0.70 (terminus 0.62), ranked BAND > SEG > RAY > WEDGE > PRED; counted in `spanMeas` |
| `verifiable` | `bandNear` | `lockNear` — any lock source within ±5 frames. This is the change that lets Finish and static frames publish on an unmarked club. |
| `reconcilePsi` weight | 8 / 2 / 0.3 | Band 8, Segment-full `wIsoSeg` 6, terminus 3, ray 2, pred 0.3; Impact-phase reconstruction skips locked frames of either source |
| Ladder rung 2 | `sTypical` from band | `sTypical` = median `s` over *FULL* locks of either source; `r0Med` likewise |
| Fusion | E-band | E-band **and** E-seg (`sigFracSeg` 0.35, slightly wider than band's 0.30); E-seg is excluded when fewer than 5 FULL locks exist |
| Head placement | BAND frames place from `(s, r0)` | Band frames unchanged; Segment-full frames place from `(s, r0)` too but **are** eligible for Stage-2 override when the measured head disagrees by > 15% of length (bands stay authoritative) |
| Static-frame admissibility | `bandNear` | `lockNear` |

The `wBand` / `wSeg` split is deliberate: the band well was tuned on a corpus where a
band lock is essentially never wrong; the segment lock has two landmarks not six and must
not be able to drag the DP through a confident counterfeit at the same strength on day
one. Raising `wSeg` to 8 is a gate decision, not a default.

### 4.4 Address phase — porting what never left Python

`stripe_annotate.py:23-32` records that address needs "a profile-correlation detector
(band/gap template along candidate lines), not looser blobs", and `stripe_fusion.py`
carries `still_search` (a full sweep with the evaluable mask over the still hold) and
`profile_band_fit` (dense E1). Neither was ported; the C++ address path relies on the
ball anchor and hold stacking. E4's run-length parse *is* the profile detector, and the
evaluable mask is ported with it (§4.2 step 3). The address-hold stack (grip-registered
average over the still run, `club_detection_from_video.md:1171-1216`) feeds E4 a
denoised frame at address so the grip-end DARK run and the tip's dark-on-mat run are
both clean.

### 4.5 Blur regime

Peak shaft rate is 15–20°/frame (2,200–3,000°/s) at impact; within one 6.57 ms exposure
the shaft is a sector, not a line (`club_detection_from_video.md:129-132`). E4 in that
regime runs along the **wedge centroid** direction. Blur is tangential, so the radial
extent of the fan is the radial extent of the shaft: `r_f` at the fan's far edge is still
a measurement, and TERMINUS mode gives the DP a pin at the mid-exposure angle. This is
the same argument the band protocol makes for why band spacings survive the impact streak
(`instrumented_club_protocol.md:56-58`); it applies to the ends of the steel as well as
the ends of the bands.

### 4.6 Persistence and provenance

- `swing.json` `capture.club` adds `shaftLengthMm`; `analysis.shaft` adds a per-frame
  `lockSrc` (0 none / 1 band / 2 segment) beside `tier`, and the summary gains
  `lockCounts {band, segFull, segTerminus}` and `shaftGeomSource`.
- The re-analysis path (`SwingDiskLoader`) reads `shaftLengthMm` from the recorded
  `swing.json`, never from `AppSettings`, keeping re-analysis deterministic (the same rule
  as `bandCentersMm` and the length prior).
- Tuning keys, all under `shaft.seg.*`: `enabled` (default 0 at merge, flipped in §6 P6),
  `eOn` 30, `minRunPx` 6, `maxCand` 6, `well` 6, `wellTerminus` 4, `conf` 0.70,
  `confTerminus` 0.62, `wIso` 6, `wIsoTerminus` 3, `supportMin` 0.80, `edgeMin` 30,
  `lenTol` 0.20, `sTol` 0.25, `hoselTolMm` 15, `ferruleTolMm` 5, `sigFrac` 0.35.
  `enabled = 0` must reproduce today's output byte-for-byte (the same no-regression
  discipline as `fusion.enabled`).

### 4.7 Lighting guidance (the one hardware lever)

The design does not depend on any capture change. It does benefit from one: the dark-arc
steel line scales linearly with on-axis source power, and the existing camera-mounted ring
light was sized for retro tape, where "a few watts on-axis outperform hundreds of watts of
off-axis flood" (`instrumented_club_protocol.md:18-26`). For bare steel the reverse
applies — the on-axis return is a small fraction of the beam — so the ring light should be
as strong as the exposure budget allows without pushing the *body* into clipping. The
best-practices doc's environment ladder (`indoor-golf-motion-capture-best-practices.md:71-87`)
gets a fifth row: *unmarked steel + strong ring light* sits between tiers 2 and 3.
Exposure itself is not revisited (§8).

---

### 4.8 What Phase 2 found on real frames (2026-09-08)

The engine of §4.2 was wired into the evidence loop behind `shaft.seg.enabled`, traced
beside the band lock, and run over the 38 taped swings whose recorded club record carries
the band centres (07-05, 07-09, 07-10, 08-18 — the ones where E1 ran and a per-frame
reference exists). The numbers are in §5.1; the *shape* of the result changed the design
and is recorded here.

1. **Candidate selection before the DP is the dominant failure, and the DP already
   solves it.** Probing the E2 local maxima, the band direction and the wedge centroid,
   then ranking by evidence × support with the arm veto and reverse-ray test, still picks
   a wrong direction on roughly a third of the frames — with support 0.97–1.00, because
   a trouser crease or the lead arm is a perfectly good line. On the same frames the
   global Viterbi agrees with the band lock to 0.2° p50 / 0.5° p90. The segment lock
   cannot be a *pre-DP pin* on the strength of a 1-D profile; it can be a **post-DP
   measurement** along the DP's own direction, feeding a second DP pass if a pin is
   wanted (the same two-pass shape the ψ-reconcile already uses).
2. **The grip-end landmark does not exist on this club.** The grip is light-coloured,
   and from the takeaway on the hands cover it to within 10–30 mm. The onset the engine
   finds is the hands' bloom edge, 20–40 px early, which is where the 30–40% scale error
   of FULL locks comes from. The proximal feature that *is* present on every frame is
   the **end of the hands along the ray** (a wide bright blob giving way to a thin
   line). Its millimetre position is unknown but constant within a swing — hands do not
   slide on the grip — and close to the grip end for a normal grip. §4.2's onset becomes
   a *hands-edge* landmark with `m_H = gripEndMm − handsOverhangMm` (default 25, σ 20 mm,
   3% of the steel span), calibrated per swing when a better witness exists (address
   ball length, or the band lock on an instrumented club).
3. **The terminus is real, but its millimetre is the hosel top, not the ferrule.** On
   real rays the run ends 4–14 px past the steel's end, at the hosel/head junction: the
   ferrule gap is not resolved under bloom at 3–4 px. The look-back finds it on synthetic
   images and almost never on the corpus. The terminus therefore references
   `hoselMm + hoselOverhangMm` (default 10, σ 15) whatever ended the run, unless a
   ferrule dip is positively resolved.
4. **Bare steel drops out for 50–70 px between lit stretches** on the band-locked ray,
   and a retro band's bloom raises the background under the ray by 60–100 grey levels.
   Runs are chains of ≥ 5 px bright anchors bridged across background-like holes of up to
   80 px, with the hole's background compared to *either* side.
5. **The wide class was wrong.** At 6.6 ms the moving shaft is a bloomed ribbon 10–20 px
   wide; "lateral ±5/±7 also lit" classified the shaft as a blob. E2's own reduction
   already bounds width: anything wider than the ±9/±12 background offsets reads as
   evidence-free. The class was dropped; the rim of a wide blob, which reads bright for
   1–3 px, is excluded by the anchor rule instead.

6. **Phase 3a built items 1 and 2 and re-graded (2026-09-08).** The probe now runs
   after the Viterbi along the DP's direction (and the band's when E1 locked); the
   onset is classified by what precedes the run — a dark, background-like stretch is a
   visible matte grip (grip end, 265 mm), the hands' bloom is the hands' edge
   (`handsEndMm`, default 180). Measured on 812 hands'-edge locks the edge sits at
   **165 mm from the butt at p50 (p10 121, p90 208)**, so the default carries a few
   percent of bias and the spread is the σ the design guessed; it is view-dependent
   (impact 146, finish 206) and is the natural athlete setting for P4. The 82 locks
   classed as a grip end but measured at 166–187 mm are a hands' edge that happened to
   be preceded by a dark stretch, and carry the 14% scale error of that misclassification.
   The terminus millimetre was settled by the corpus: a run ends at the **hosel end**
   (922 mm) — dark-end and head-after locks sit at −1 px from it at p50 — unless a
   ferrule dip is positively resolved, and on marked clubs the look-back is off because
   the tip group's 25 mm inter-band gaps are ferrule-sized dips. Two gates were wrong for
   what the probe now sees: the butt-offset floor rejected hands'-edge locks whose
   assumed millimetre implied an anchor a little behind the butt (`r0MinHands` −60 mm),
   and the length gate was two-sided although a foreshortened mid-swing projection is
   legitimately far shorter than the address length it is compared with (now an upper
   bound plus a loose 0.4 floor). A run that ends within 25 px of the image edge has no
   terminus — a ray a degree off the line leaves a 4 px steel before the edge.

---

## 5. Validation

### 5.1 The taped corpus grades the segment lock directly

The grip end and the hosel exist on the taped 7-iron too. So E4 can be run on the
`tape_20260705` corpus (10 swings, 1,033 fusion-truth entries, zero adjudicated errors)
and scored frame by frame against the band truth — without a new capture. Metrics:

| Metric | Target | Baseline (band tier) |
|---|---|---|
| θ error, FULL locks vs band truth, fast phases | p50 ≤ 1.5°, p90 ≤ 5°, 0% > 15° | band 0.3°, ray 1.7° (3% > 15°) |
| `s` error vs band `s`, same frame | p50 ≤ 5%, p90 ≤ 12% | — |
| `r0` error | p50 ≤ 20 mm | — |
| FULL-lock rate where band locked | ≥ 70% | — |
| Any-lock rate where band did **not** lock (address, tip-on-mat, finish) | ≥ 50% at address and finish | band: 0% |
| Lock conflict rate (band vs segment > 6°) | ≤ 2% | — |

**Phase 2 result (2026-09-08, 38 swings, 26,602 traced frames, 2,300 band-locked
frames; `docs/research/data/markerless/segment_grade_summary.md`, per-frame CSV beside
it).** Graded with the engine as built through §4.8 items 3–5, before the Phase 3 change
of shape (§4.8 items 1–2). The committed engine differs from the graded binary by one
later rule — a hole's background is compared to either side of the gap, not only the
chain — which the synthetic suite covers and the corpus has not re-run.

| metric | target | measured | verdict |
|---|---|---|---|
| θ error, locks on band frames | p50 ≤ 1.5°, p90 ≤ 5°, 0% > 15° | p50 **1.7°**, p90 **71.6°**, **18.2%** > 15° | p50 near, tails fail: wrong-candidate locks (§4.8 item 1) |
| `s` error, FULL locks | p50 ≤ 5% | p50 **27.7%**, p90 66% | fails: the onset lands on the hands' edge (§4.8 item 2) |
| `r0` error | p50 ≤ 20 mm | 81 mm | fails, same cause |
| FULL-lock rate where band locked | ≥ 70% | **27%** (any lock 51%) | fails |
| any-lock rate at address / finish where band did not lock | ≥ 50% | 3% / 0% | fails — most address and finish frames sit outside the evidence span and are never probed |
| lock conflict (> 6°) | ≤ 2% | **27.9%** | fails |

Landmark anatomy on the on-ray locks (within 6° of the band): the **terminus** is 20 px
off at p50 (10% of the steel span; 173 px at p90 where the run stops at the mid band or
runs past the hosel), the **onset** is 37 px off at p50 and 47 at p90 — it is the hands'
edge, consistently. In the backswing, the phase with the most band frames (1,137), the
picture is the clearest: θ p50 1.2° but 22% conflict, FULL scale error 24%, terminus
18 px, onset 39 px. The DP on the same frames: 0.2° p50, 0.5° p90 against the band.

Verdict: **as a pre-DP pin with a grip-end landmark the segment lock does not reach the
band lock, and the corpus says why**. Along the right ray the terminus is a usable
landmark; the onset is not; and the ray must come from the DP. Phase 3 is redefined
accordingly (§6).

**Phase 3a result (2026-09-08, same 38 swings; graded binary = committed engine).**

| metric | target | Phase 2 | **Phase 3a** |
|---|---|---|---|
| θ error, locks on band frames | p50 ≤ 1.5°, p90 ≤ 5° | 1.7° / 71.6° | **1.0° / 1.4°**, 0.4% > 15° |
| lock conflict (> 6°) | ≤ 2% | 27.9% | **3.6%** (address 58% on 47 frames, downswing 11%; every other phase ≤ 7%) |
| `s` error, FULL locks | p50 ≤ 8% (P3a gate) | 27.7% | **6.2%** (hands'-edge locks 5.6%, n 812); p90 43% |
| `r0` error | p50 ≤ 20 mm | 81 mm | 28 mm |
| terminus, on-ray, vs the hosel end | p50 ≤ 10 px | — | **−1 px**, 53–61% within ±15 px; p90 105 px (runs stopping at the mid band) |
| FULL-lock rate where band locked | ≥ 70% | 27% | **40%** (any lock 52%) |
| any lock at address / finish where band did not lock | ≥ 50% | 3% / 0% | 2% / 1% — those frames sit outside the evidence span and are not probed |
| lock rate on band-absent frames, backswing / top | — | 38% / 21% | 34% / 49%, θ vs the DP 0.5–1.0° |

Direction and terminus position are solved; scale is at the P3a gate with a tail; coverage
is the open item.

**Phase 3b result (2026-09-08, same 38 swings) — measured against the marked club's own
lock on the same frames**, which is how every result is reported from here on. The lock
union of §4.3 is wired (SEG tier, `lockNear`, rail weights, ladder rung 2 for unmarked
clubs, E-seg in the fusion, head from the terminus); the emission well is not, because
P3a showed the DP already agrees with the band to 0.2° where a band exists and the
well's value in band-free regions cannot be measured on this corpus.

| phase | span frames | **band lock** | **segment lock** | either | band θ vs DP p50/p90 | seg θ vs DP p50/p90 |
|---|---|---|---|---|---|---|
| address (collar) | 570 | 8% | 63% | 67% | 0.4° / 10.5° | 0.5° / 1.0° |
| backswing | 4,462 | 25% | 68% | 79% | 0.3° / 0.5° | 1.0° / 1.0° |
| top | 190 | 56% | 53% | 88% | 0.3° / 2.6° | 1.0° / 1.1° |
| downswing | 950 | 9% | 33% | 38% | 0.2° / 2.2° | 0.5° / 1.0° |
| impact | 1,060 | 28% | 58% | 73% | 0.3° / 5.3° | 1.0° / 1.0° |
| through | 1,070 | 19% | 31% | 40% | 0.3° / 0.5° | 1.0° / 1.0° |
| finish (collar) | 563 | 75% | 55% | 92% | 0.3° / 0.5° | 1.0° / 1.3° |
| **all** | **8,865** | **26%** | **57%** | **69%** | 0.3° / 0.5° | 1.0° / 1.0° |

Read across: on the marked club the band lock covers a quarter of the span frames; the
segment lock covers more than half, and where both exist the segment sits 1.0° from the
DP against the band's 0.3° (the 1.0° is the ±0.5° refinement step, not measurement
noise). The band remains better in the two places it was built for — the top and the
held finish — and the segment lock is the wider net everywhere else, address most of
all (8% → 63%).

| tracker output, 38 swings | recorded (band only) | with the segment lock |
|---|---|---|
| tier mix over span frames: band / seg / ray / wedge / pred | 25 / — / 65 / 4 / 6% (Phase 0 D) | 25 / 44 / 25 / 2 / 4% |
| `coverage` p50 (min) | 0.958 (0.881) | 0.973 (0.901) |
| lock-off output vs the recorded tracker | — | byte-identical bar four wall-clock timing fields |

**Still frames outside the evidence span (P4, same 38 swings) — corrected 2026-09-09.**
The first P4 pass reported a 30% address-hold lock rate. The unmarked 6-iron session
showed those locks were the trouser crease: at address the DP's direction is a 90°
clamp while the club sits 8–14° off it, and a probe along the clamp finds the leg edge
beside the shaft, long, bright and 40% short. Address-like frames (the pre-span hold and
the address phase) are now probed from each frame's own anchor toward the **accepted
address ball centre**, with a ±5° refinement because grip→ball is a far-end anchor
about 3° off the shaft, and the ball length gates them at ±15% because the club is
in-plane there. Frames with no address ball are not probed — there is no witness.

| phase | frames outside the span | taped 7-iron, segment lock | unmarked 6-iron, segment lock |
|---|---|---|---|
| address hold | 11,220 / 994 | 6% | 9% |
| held finish | 6,517 / 1,122 | 1% | 0% |

Where an address lock now exists its terminus sits on the hosel within a few pixels of
the ball-implied position and the fused length matches the ball to 1–3%. The low rate
is the regime the validation protocol already names: bare steel over a lit but
unclipped mat has no contrast, and the tape does no better there (band lock at address
8%). The held finish is out of reach for both without a light on it.

**P5 — the unmarked club (2026-09-09, 6-iron, 7 swings, same rig, ring light, no
tape; club record from Mark's tape measure: length 955, hosel 892 taken as the 880 mm
ferrule entry plus 12, shaft 612, hands end 220).** Same engine, same day, graded beside
the taped 7-iron's 38 swings:

| phase | taped 7-iron **band lock** | taped 7-iron **segment lock** | **unmarked 6-iron segment lock** |
|---|---|---|---|
| address (collar) | 8% | 11% | 9% |
| backswing | 25% | 69% | 50% |
| top | 56% | 55% | 51% |
| downswing | 9% | 31% | 16% |
| impact | 28% | 59% | 54% |
| through | 19% | 30% | 15% |
| finish (collar) | 75% | 55% | 17% |
| **all span frames** | **26%** | **54%** | **40%** |
| θ vs the DP, p50 / p90 | 0.3° / 0.5° | 1.0° / 1.0° | 1.0° / 1.0° |
| coverage, p50 | 0.958 (recorded) | 0.976 | 0.963 (recorded 0.960) |

Fused club length against the ball's address measurement on the 6-iron: 0.97–1.00 on
all five swings with a ball. On the two swings without a ball the segment voice is a
projected length with no in-plane witness: one fusion abstained, one came out 40%
short. Without a ball or bands, club length on an unmarked club is not yet solved.

So on a bare club the segment lock reaches the same frames the taped club's segment
lock reaches in the backswing, top and impact, half as many in the downswing and
through-swing, and a third as many in the finish collar — and everywhere it locks it
sits 1.0° from the tracker's direction. Against the band lock it is ahead on every
phase but the top and the held finish.

**Scale, against the reference's own precision.** The band lock's scale changes by 1.6%
between adjacent band frames at p50 and 6.8% at p90 (1,863 pairs) — that is the floor a
per-frame scale can be graded against. The segment's 6.2% p50 is four times that floor;
its 43% p90 is the short-terminus tail of §5.1 and is where the fusion (σ 35%, one voice
among ball, band, head and prior) is meant to arbitrate rather than the per-frame engine. Of the 2,300 band frames the probe left unlocked, 712 fell at the s/r0
gate and 202 found no run of 60 px; the rest are spread thinly. The address and finish
numbers are a span question (§7), not a detector one.

### 5.2 Unmarked-club corpus

Two populations:
1. **Existing:** the 2026-07-03 session (10 swings, the only unmarked full-resolution
   video; five iron/wedge labels, five wood/hybrid labels that §2.4 shows cannot be
   trusted) and the 06-11 gap-wedge session (9 swings, 720 px wide). Everything else in
   the library is the taped 7-iron (64 swings, §2.4). 07-03 is a daylight room; the taped
   sessions are blacked out, so they bound the design from both sides rather than compare
   directly.
2. **New capture, required for the gate:** the same 7-iron, **untaped**, in the blacked-out
   room with the ring light on, ≥ 10 swings, raw ON, `Wrist` session, face-on. Truth: the
   blinded sparse markup of `shaft_validation_protocol.md` (~8–10 P-points per swing, 5
   swings re-labelled for ICC), plus the P-ladder. This is the untaped corpus that
   `shaft_detection_v3_impl.md:66` has had pending since July; it is now the gate.

Gates on population 2, against today's untaped output on the same swings:

| Metric | Target |
|---|---|
| Downswing measured coverage (BAND\|SEG\|RAY\|WEDGE) | ≥ 90% (taped v3: 96%) |
| Through-impact coverage | ≥ 75% (taped v3: 83%) |
| Finish frames published | > 0 on every swing (today: structurally 0) |
| θ vs sparse markup | p50 ≤ 2°, p90 ≤ 6° |
| Club length vs ball `L_px` at address | within 5% on every swing with a ball lock |
| `valid` (coverage ≥ 0.60) | 10/10 |

### 5.3 No-regression

`shaft.seg.enabled = 0` byte-identical on the 61-swing `stagegate/pose2` population and
the 11 IMU-bound 08-18 swings. With it on, the taped corpus θ p90 and coverage must not
move by more than the run-to-run floor (two 83-swing passes on GOLFSIMPC have been
byte-identical, so the floor is zero; any movement is real).

### 5.4 Compute

E4 adds ≤ `maxCand` (6) profiles of ~`rmax` samples at 9 lateral reads each per frame —
~6 × 635 × 9 ≈ 34 k samples, against E2's ~0.75 M. Negligible. The run-length parse is
O(rmax). Address-hold stacking already exists. No new decode pass.

---

## 6. Implementation plan

v3 was exemplar-first (Python in `tools/shaftlab`, then a numerically-identical C++
port). Mark's decision 2026-09-08: the shaftlab exemplar is superseded by the C++ and is
no longer used, so this work goes straight to C++ — the engine is unit-tested on
synthetic profiles and graded on the taped corpus through the tracker's own trace, with
the band lock as the per-frame reference. The porting invariants of
`club_track_v3_exemplar_explained.md §15` (nearest-neighbour sampling, percentile
normalisation) still apply to anything that shares code with E2.

| Phase | Deliverable | Done when |
|---|---|---|
| **P0 — measure** ✅ 2026-09-08 | `tools/shaftlab/steel_profile_probe.py`; `docs/research/data/markerless/steel_profile_probe.csv` (21,635 frames, 83 swings) + `_summary.md`; results in §2.4 | done — 64 taped + 19 unmarked swings; the two-frame table of §2.1 holds corpus-wide, with delivery as the exception |
| **P1 — engine (C++)** ✅ 2026-09-08 | `rayProfile()` + `segmentLock()` in `shaft_tracker_math.*`; E2's per-sample reduction factored into a shared `sampleRay()`; `SegmentConfig` on `ShaftV3Config` with every `shaft.seg.*` key parsed; `shaft_segment_test` (28 checks: bit-for-bit ridge-score pin, FULL/TERMINUS, polarity flip, forearm/off-axis/off-frame counterfeits, length and scale gates, bands as extra landmarks, four foreshortening scales) | done — 7 suites green under ctest, E2 pinned identical |
| **P2 — grade** ✅ 2026-09-08 | segment probe inside the evidence loop behind `shaft.seg.enabled` (pass 1 prior-free, pass 2 with the swing's median FULL scale), traced beside the band lock; `tools/shaftlab/segment_grade.py`; 38 taped swings | done — **§5.1 targets NOT met**; the corpus located the two design errors (§4.8 items 1–2) |
| **P3a — change of shape** ✅ 2026-09-08 | probe **along the DP's θ** after the Viterbi (and the band's when present); onset classified by what precedes the run (visible grip → grip end; hands' bloom → hands' edge at `handsEndMm`); terminus = hosel end unless a ferrule is resolved; look-back unmarked-only; image-edge guard; hands'-edge r0 floor; one-sided length gate; re-graded on the same 38 swings (§5.1) | θ p50/p90 **1.0°/1.4°**, terminus **−1 px** p50, `s` p50 **6.2%** — gate met; FULL-lock rate 40%, coverage is the open item |
| **P3b — consumers** ✅ 2026-09-08 | lock union: SEG tier (BAND > SEG > RAY, conf 0.70/0.62), `lockNear` in the verifiable clause, rail weights 6/3 via a weight override, ladder rung 2 from segment medians on an unmarked club, E-seg (σ 0.35) in the fusion, head placed from the terminus unless a Stage-2 measured head exists; all behind `shaft.seg.enabled`; the emission well deferred (P3a: the DP already sits 0.2° from the band) | done — lock-off byte-identical to the recorded tracker (timings aside); segment lock on 57% of span frames vs the band's 26%; coverage 0.958 → 0.973 (§5.1) |
| **P4 — record + UI + persistence** ✅ 2026-09-08 | `shaftLengthMm` + `handsEndMm` in the club record (defaults 0 = unknown), two fields in `AthleteClubsSection.qml`, both jobs filled from the record, `swing.json` `capture.club` carries both, re-analysis replays them, the tracker reads them; schema doc rows; **still frames** outside the evidence span are now probed along the nearest in-span DP direction (`shaft.seg.probeStill`) | app, tests and tool build; round-trip record → job → swing.json → re-analysis by construction (same read sites as `hoselFromButtMm`) |
| **P5 — corpus + capture** ✅ 2026-09-09 (first pass) | Mark's unmarked 6-iron session (7 swings), graded beside the taped corpus with the same engine (§5.1); results in `docs/research/data/markerless/unmarked_0909/`; the address probe re-pointed at the ball and its counterfeit removed on the way | partial — lock rates and direction graded; no hand truth yet, so θ vs truth and the §5.2 coverage gates remain open |
| **P6 — flip** | `shaft.seg.enabled` default 1; docs updated (`shaft_tracker_impl.md`, protocol docs, best-practices ladder row) | gate report attached |

Two deliverables, named up front: **this document** and **the engine + wiring through P4**.
P5/P6 need a capture session and are the definition of done for "same level of detection
without tape".

---

## 7. Risks and open questions

- **The grip end hides in the hands' bloom.** At address the glove and hands saturate and
  the visible grip below them is ~20–30 px; in the blur regime it vanishes. TERMINUS mode
  exists for this; the risk is that FULL-lock rate in the downswing is low and the scale
  comes mostly from address and backswing. That is still strictly better than today
  (rung 3, ~33% short), and the ball anchor remains rung 1.
- **The hosel/ferrule as a ±15 mm landmark.** A 2–3% scale error is acceptable for length
  and placement; it is not a 0.3° θ instrument. θ comes from the run, not the ends, so this
  bounds `s`, not θ.
- **Counterfeits with two ends.** A forearm or a mat edge has ends too. The gates that
  reject them are support ≥ 0.80, the reverse-ray test, the WIDE classification (a forearm
  is wide), C2 mid-swing, and the length-consistency check. The taped corpus's adjudicated
  counterfeit list (trouser crease, s06 leg shadow, sleeve texture, foot-region speckle) is
  the regression set.
- **Well depth.** `wSeg` 6 is a guess between the wedge well and the band well. If the
  untaped DP still routes down the wrong branch across impact with 6, the answer is to
  measure conflict and false-lock rates at 8, not to assume.
- **Daylight confound in the existing untaped data.** Bounded, not resolved, until P5.
- **Address and finish frames outside the evidence span** (resolved P4). The span runs
  from 100 ms before the takeaway to 100 ms after the finish onset; the address hold
  and the held finish were never probed. P4 probes them along the nearest in-span DP
  direction — the club is still there, so the direction is the same — and they publish
  as SEG-tier samples. Coverage still counts span frames only. Result in §5.1.
- **Corpus labels.** `corpus.json` `conditions.club` and pre-0.1.10011 club records do not
  say which club was hit (§2.4). Every gate report that split by club or by taped/untaped
  using those labels needs re-reading; the probe's condition assignment (BAND-tier count
  plus two inspected sessions) is the reference until the manifest is corrected.
- **Graphite.** Untested; the design carries a polarity switch and a confidence penalty and
  makes no claim.
- **Open:** should `shaftLengthMm` be the setting the user enters, or should the UI ask for
  "grip length" (easier to measure on a club with the grip on)? Both derive the same
  `gripEndFromButtMm`. This document picks shaft length per Mark's request; the field name
  is the only thing that changes if the other is preferred.

---

## 8. Deliberately not done

- **Exposure.** Mark's ruling of 2026-07-05 (`stripe_fusion_design.md:21-26`): 150 fps is
  the camera's maximum, the 6.57 ms exposure is the light budget, do not revisit. Nothing
  here depends on it. For the record, the bare shaft is clipped at 255 in the lit arc, so
  the *shaft* has exposure headroom the body does not; that is a fact, not a proposal.
- **IR illumination.** Excluded three times in the record (launch-monitor conflict, RGB
  pose pipeline shares the camera).
- **A learned shaft detector.** `club_tracking_v3_design.md:173-175` deliberately keeps θ
  classical; the planned learned component is a heel/toe/hosel keypoint head, which this
  design does not need and does not preclude — E4's landmarks are exactly the weak labels
  it would train on.
- **Polarisers / dulling.** Never considered in the record; a polariser would *reduce* the
  specular line this design relies on.
- **Environment modification.** Backdrops were rejected as impractical (2026-07-06); the
  only environment ask here is ring-light power, which the instrumented protocol already
  requires.

---

## Appendix A — measured numbers (2026-09-08)

Rig: FLIR Chameleon3, 1280×1024 BayerRG8, 150.713 fps, exposure 6,573.6 µs
(`ExposureAuto` Continuous, chunk-reported), 746 frames per 5 s swing, ~3.5 mm/px at the
club. Frames: `2026-07-03_Mark-Liversedge_Wrist_01/swing_0006` (7 IRON, untaped, daylight
room) and `2026-08-18_Mark-Liversedge_Wrist_02/swing_0002` (7 IRON, taped, blacked out),
impact from `capture.impactUs`, frames chosen by nearest `t_us`.

Perpendicular intensity profiles (8-bit luma):

| Frame | Location | Peak | Background | Note |
|---|---|---|---|---|
| untaped, address | row 720, shaft over trousers | 255 | 31 | `[19, 28, 79, 222, 251, 255, 255, 251, 242, 160, 105]` — ~6 px at clip |
| taped, address | row 720 | 255 | 12 | `[9, 14, 42, 193, 245, 255, 254, 253, 253, 252, 254]` |
| untaped, top −300 ms | x=560/640/720 | 250 / 251 / 246 | 22 / 17 / 9 | continuous; widths 64/50/9 px (the first two include a specular hot-spot) |
| taped, top −300 ms | x=560 (band) | 227 | 6 | 17 px wide |
| taped, top −300 ms | x=640 (gap steel) | 91 | 8 | `[16, 22, 42, 54, 91, 79, 75, 56, 37]` |
| taped, top −300 ms | x=800 (gap steel) | 35 | 6 | 4 px above bg+20 |

Band geometry, lab 7-iron: centres 308, 362, 560, 758, 808, 854 mm from butt; width
25 mm; hosel 882 mm; length 940 mm; taped 2026-07-04. Corpus (corrected by §2.4): 64
taped 7-iron swings across six sessions from 07-04 to 08-18; ten unmarked full-resolution
swings (07-03, daylight) and nine unmarked 720-px gap-wedge swings (06-11). The untaped
accuracy corpus has never been captured.

## Appendix B — swing weight, and the truth instrument

Swing weight is the moment about a fulcrum 14 in (355.6 mm) from the butt. Mass at the
fulcrum contributes nothing; mass at the tip contributes fully. The lab club's two
grip-side bands (308 and 362 mm) straddle the fulcrum and change swing weight by
essentially zero; the three tip bands (758–854 mm) are the entire change. The protocol's
"no swing-feel effect" (`instrumented_club_protocol.md:16`) was asserted from thickness
and never measured.

Consequence for the truth supply: the product path in this design is markerless, but the
*instrument* that grades it can stay marked without altering the swing — keep the grip
pair, drop the tip group. With E4's two shaft landmarks the fit then has four points
(grip end, band, band, hosel), which is E1's minimum, at no swing-weight cost. That is a
protocol change for `instrumented_club_protocol.md`, proposed here, not assumed by the
gates in §5.
