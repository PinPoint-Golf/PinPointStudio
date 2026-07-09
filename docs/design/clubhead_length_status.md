# Clubhead / Club-Length Detection — Status Report

**Purpose:** hand-off brief for a design pass on the club overlay. The replay
overlay draws the club as a straight `grip → head` line and that line currently
renders **too long**. This report explains exactly where the line length comes
from, why it is wrong today, and what is designed-but-unbuilt, so the design
work can start from ground truth rather than the code archaeology.

**Author:** Claude (code trace, 2026-07-09). **Audience:** design (Fable).

---

## 0. TL;DR

- The overlay line's far end (the "head") is **never measured — it is always
  projected**: `head = grip + L · dir(θ)`. The shaft tracker only measures the
  *angle* θ; the *length* L is an assumption. (`ShaftHeadProjected`,
  `shaft_track_assembly.cpp:889‑892`.)
- So a too-long line is a **length (L) problem**, not an angle problem.
- The dominant cause: **clubs default to untaped**, and with no retro-reflective
  bands there is no pixel scale, so L falls back to a hardcoded
  **`0.55 × frame height`** (`shaft_track_assembly.cpp:847`). On a 1080p face-on
  frame that is ~594 px of club drawn regardless of the real club.
- Secondary causes: the assumed club length defaults to a **driver (1.12 m)**;
  projected frames are drawn **from the grip, not the butt** (overshoot); and
  the one real length signal we *do* compute (`measuredClubLenPx`, grip-to-ball
  at address) is **serialized but never consumed**.
- The proper fix — a **Stage-2 clubhead detector** that *measures* the head
  along the ray — is fully designed but **not built anywhere in the app**
  (`clubhead_detection_design.md:246`).

---

## 1. How the overlay line is produced (end-to-end)

1. **Draw.** `PpCameraFrame.qml` replay overlay draws a straight line from
   `grip` to `head` at the current playhead, plus a fading head trail and a dot
   at `head` (`src/Gui/cameras/PpCameraFrame.qml:602‑614`). It draws whatever
   `grip`/`head` it is given — it does no length logic of its own.
2. **Source.** `grip`/`head` come from `d.club.samples` (normalized 0..1), built
   in `shot_processor.cpp:172‑173` / `185‑186` by dividing `ShaftSample2D.gripPx`
   / `headPx` by the frame dims.
3. **Where head is set.** `headPx` is assigned in the shaft-track assembly
   (`src/Analysis/shaft_track_assembly.cpp`), per frame, by one of two paths
   (see §2). This runs inside `ShaftTracker::track()`, invoked from
   `wrist_analyzer.cpp:382`.

`ShaftSample2D` (`swing_analysis.h:279`) carries `gripPx`, `headPx`, `thetaRad`,
`visibleLenPx`, `conf`, and a `flags` byte. The relevant flags:

| flag | meaning |
|---|---|
| `ShaftMeasured` (0x01) | a real vision measurement of θ this frame |
| `ShaftCoasted` (0x04) | predict-only (no channel this frame) |
| `ShaftHeadProjected` (0x10) | **headPx projected from `grip + L·dir(θ)`, not measured** |
| `ShaftBallAnchored` (0x40) | θ soft-anchored from grip→ball (v3.4 ball anchor) |

The θ path is mature; **the head/length path is the weak link.**

---

## 2. Root cause: the head is projected, never measured

Per-frame in `shaft_track_assembly.cpp:851‑893` there are two length regimes:

### 2a. BAND tier (taped club only) — `shaft_track_assembly.cpp:858‑865`
When a retro-reflective band is matched this frame, the code recovers a pixel
scale `s` (px/mm) and butt offset `r0` (mm) from the band geometry and places:

```
butt = grip − s·r0·dir(θ)
head = butt + s·clubLenMm·dir(θ)        // clubLenMm = physical club length, butt→head
```

This geometry is **correct**: it projects the full physical length from the
butt, so the drawn `grip→head` segment equals the true visible club below the
hands. Scale is data-driven (`s` from the bands).

### 2b. Projected tier (everything else) — `shaft_track_assembly.cpp:889‑892`
When there is no band match this frame (or the club is untaped, so *never*):

```
projLenPx = sTypical > 0 ? sTypical · clubLenMm : 0.55 · frameH   // line 847
head      = grip + projLenPx · dir(θ)                             // line 891
```

- `sTypical` = median band scale across the swing (`line 845`). **If the club is
  untaped there are zero band frames → `sTypical = 0` → the `0.55·frameH`
  fallback is used for every frame.**
- Note the projection is from **`grip`**, not `butt` — so even when `sTypical`
  is valid, projected frames overshoot band frames by the grip-to-butt length.

---

## 3. Why the line is too long today (ranked)

1. **Untaped club → hardcoded 55%-of-frame length.** Clubs default to *untaped*
   (`athlete_controller.cpp:324`, `bandCentersMm` empty by default; the job
   comment at `shot_analyzer.h:48‑54` confirms "Empty ⇒ untaped club: … no band
   tier"). Untaped ⇒ `sTypical = 0` ⇒ `L = 0.55·frameH` for **every** frame.
   This is a fixed, uncalibrated ~594 px on 1080p and is almost certainly the
   long line being observed. (Cross-ref: the "shaft untaped" investigation note
   — bands are currently load-bearing for scale.)
2. **Driver length assumed.** `clubLengthM` defaults to **1.12 m** — a driver —
   "until club selection is real" (`shot_analyzer.h:47`). Applied to a shorter
   club (iron/wedge, ~0.9–1.0 m) the taped-tier line runs ~10–20% long.
3. **Projected from grip, not butt.** §2b projects full club length from the
   grip, overshooting by the whole grip-to-butt segment on all
   `ShaftHeadProjected` frames — inconsistent with the band tier.
4. **Foreshortening is not modeled.** The projected length should *shrink* as the
   shaft tilts toward the camera (top of backswing) and recover by impact, but
   the planned length-collapse "was **NOT** built"
   (`shaft_tracker_impl.md:216‑220`). A constant assumed length is therefore
   drawn even when the true projected club is much shorter — over-drawing worst
   at the top of the swing.
5. **The one real length measurement is unused for length.** The v3.4 ball anchor
   *is* wired live (`shaft_tracker.cpp:170`) and computes `measuredClubLenPx` =
   median grip-to-ball distance at address (`ball_anchor.cpp:178‑186`) — a
   genuine in-scene club-length observation. But (a) it only **soft-anchors θ**
   (sets `thetaRad`/`gripPx`/`conf`, never `headPx` or length); (b) it is **only
   written to swing.json** (`swing_doc.cpp:252`), never read back into the
   projection despite the struct comment calling it "a scale floor for
   implausibly-short shafts" (`swing_analysis.h:308`); and (c) it depends on a
   ball track (`job.ballTrackPath`) that is mid-build / unreliable, so in
   practice it is frequently a no-op. The *intended* proper use is
   `club_tracking_v3_design.md §9`: pin the head to the stationary ball at
   address/impact to get a **measured `L_px = |B−G|` as a hard floor and
   ceiling** (confidence → 0 below ~0.8·L_px) — designed, not built.
6. **Band-scale error (taped case).** When bands *are* present, a mis-estimated
   `s`/`sTypical` scales `clubLenMm` directly, so scale noise maps 1:1 onto line
   length.

---

## 4. What is designed but NOT built — the real fix

`docs/design/clubhead_detection_design.md` ("Clubhead Detection: Technical
Design (Stage 2)") specifies the intended end state:

- **Measure the head, don't assume it.** Treat the head as a **1-DOF radial
  search along the shaft ray** (mirror of the Stage-1 angular search):
  `P_h = P_g + r_h·d(θ)`, with `r_h` found from evidence, not from a physical
  constant (design §1).
- **Evidence channels** robust to the head's wildly varying appearance
  (chrome/dark/streak): change-vs-scene `E_chg`, anti-ghost motion `E_mot`,
  thin-line-termination `E_end`, width-expansion `E_wide`, combined OR-family
  (§3–§4). Presence required, shape sharpens the peak.
- **Temporal model**: 1-D Kalman on `[r_h, ṙ_h]`, per-segment RTS, foreshorten-
  aware (§5). `r_h` shrinks smoothly at the top of the backswing and recovers by
  impact — smoothness in time is the head's strongest prior.
- **Honest output tiers**: `meas | pred | off`. **Off-frame is a first-class
  state** — post-impact the head leaves the frame before the shaft does, and the
  design forbids fabricating a position (§3.7, §6).
- **Decoupling contract**: Stage-2 consumes only a versioned Stage-1 track CSV
  (`grip_x/y, theta_out, kind, conf`) and computes its own scene references; it
  must tolerate Stage-1's documented ±5° θ / ±10 px grip error budget (§2).

The design also adds a **plausibility floor** the app lacks entirely: the club is
*always* longer than the arm, so `r_h ≥ 1.05 × projected shoulder→grip`
(`clubhead_exemplar_plan.md` H2) — a lower bound that catches impossibly-short
heads. There is no corresponding *upper* bound on length in the app today.

**Status of that design:** exemplar-first, **not ported to the app**. The design
doc's own §9 states: *"The production `ShaftTracker` projects `headPx` from
grip + L·dir(θ) (flagged `ShaftHeadProjected`) — it does not measure the head;
nothing in the app implements this design."* In the Python lab the exemplar is
further along — **H0–H2 done** (length model + radial terminus measurement +
temporal KF/tiers/arm-floor), **H3 in progress, H4 (corpus + freeze) remaining**
(`clubhead_exemplar_plan.md`). None of it is in C++. And measuring the head is
genuinely hard: the research report records the classical Stage-2 terminus
**fails its ≤5%-bad honesty clause on 7 of 10 swings** against dense fast-phase
truth (`docs/research/club_detection_from_video.md`), which is why the ball
anchor (§9) is proposed as the load-bearing far-end fix.

---

## 5. Built vs designed — summary

| Piece | State |
|---|---|
| Shaft **angle** θ tracking (Stage 1) | Built, corpus-validated, ported to C++ (`ShaftTracker`) |
| Head position | **Projected only** — `grip + L·dir(θ)`, never measured |
| Length L, taped club | Band-scale × assumed physical length (`clubLenMm`) |
| Length L, **untaped club (default)** | **Hardcoded `0.55·frameH`** fallback |
| Assumed club length | Default **1.12 m (driver)**; overridden only if athlete club record has `lengthMm` |
| Foreshortening (length shrink) | **Not modeled** — planned L-collapse never built (`shaft_tracker_impl.md:216`) |
| Ball-derived length (`measuredClubLenPx`) | Wired live but soft-anchors θ only; length **computed + serialized, never consumed**; ball track mid-build |
| Arm-length plausibility floor | In the Python exemplar (H2); **absent** in the app — no length bound at all |
| Stage-2 clubhead **measurement** | Designed in full; **unbuilt in C++** (Python exemplar H0–H2 done, H3 in progress) |
| Club-selection UI feeding real length/bands | Club record schema exists (`athlete_controller.h:83`); default untaped, driver length |

---

## 6. Open design questions for Fable

1. **Scale without tape.** The core blocker: untaped clubs have no pixel scale,
   so length is a pure guess. Options to weigh — (a) require/assume a club
   length from the athlete's club record and derive scale from a body-based
   reference (e.g. known shoulder width / pose scale) instead of bands;
   (b) adopt `measuredClubLenPx` (grip-to-ball at address) as the primary scale
   when a ball is present; (c) build the Stage-2 measured head. Which is the
   near-term vs. end-state answer?
2. **Interim clamp vs. real fix.** Should we ship a cheap correctness patch now
   (consume `measuredClubLenPx`; project from butt not grip; replace the
   `0.55·frameH` guess with a pose-scaled estimate) *before* the Stage-2
   detector lands? What's the acceptable interim overlay behaviour?
3. **Club selection UX.** Length (and bands) should come from a selected club.
   How does club selection reach `ShotAnalysisJob.clubLengthM` reliably, and
   what's the default when the athlete hasn't entered a club?
4. **Off-frame honesty in the overlay.** The Stage-2 design makes "head off
   frame" a real state. Today the overlay always draws a head. How should the
   overlay render `off` / low-confidence length — shorten to `visibleLenPx`,
   dashed ghost, or draw nothing past the frame edge?
5. **Taped vs. untaped as product regimes.** Is taped-club a supported
   high-accuracy mode and untaped the default passive mode (matching the
   "passive detection is the product path" direction)? The length strategy
   differs per regime.
6. **Where length lives.** `measuredClubLenPx` is per-swing; `clubLengthM` is
   per-club; band scale is per-frame. Which is the single source of truth the
   projection should read, and how do they reconcile (e.g. the documented but
   unwired "scale floor")?

---

## 7. Key code references

| What | Location |
|---|---|
| Overlay draws grip→head | `src/Gui/cameras/PpCameraFrame.qml:587‑615` |
| head/grip → normalized samples | `src/Gui/shot/shot_processor.cpp:170‑188` |
| Head projection (band + fallback tiers) | `src/Analysis/shaft_track_assembly.cpp:845‑893` |
| `0.55·frameH` fallback | `src/Analysis/shaft_track_assembly.cpp:847` |
| `clubLengthM = 1.12` default (driver) | `src/Analysis/shot_analyzer.h:47` |
| Untaped ⇒ no band tier | `src/Analysis/shot_analyzer.h:48‑54` |
| Clubs default untaped | `src/Gui/athlete/athlete_controller.cpp:324` |
| Ball anchor wired (soft-anchors θ only) | `src/Analysis/shaft_tracker.cpp:170` |
| `measuredClubLenPx` computed (grip→ball) | `src/Analysis/ball_anchor.cpp:178‑186` |
| `measuredClubLenPx` unused for length (write + serialize only) | `swing_analysis.h:308`, `swing_doc.cpp:252` |
| Foreshortening L-collapse "NOT built" | `docs/implementation/shaft_tracker_impl.md:216‑220` |
| `ShaftHeadProjected` flag | `src/Analysis/swing_analysis.h:274` |
| Stage-2 clubhead design (the real fix) | `docs/design/clubhead_detection_design.md` |
| Stage-2 exemplar plan (H0–H2 done) | `docs/implementation/clubhead_exemplar_plan.md` |
| "nothing in the app implements this design" | `docs/design/clubhead_detection_design.md:246` |
| Ball far-end anchor design (§9, L_px floor/ceiling) | `docs/design/club_tracking_v3_design.md` §9 |
| Ball stream (`ballTrajectory`) — not started | `docs/design/ball_detector_design.md` §5 |
| Stage-2 honesty clause fails 7/10 swings | `docs/research/club_detection_from_video.md` |
</content>
