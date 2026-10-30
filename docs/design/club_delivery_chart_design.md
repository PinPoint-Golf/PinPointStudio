# Club delivery chart: the swing plane, read the way a coach reads it, and what it does to the ball

Status: DESIGN, 29 Oct 2026. Nothing built. Mark's brief (29 Oct): "the club delivery chart is silent on the
swing plane curve. We also do not have any domain specific insight on this chart. Given the importance of
these data series and their impact on ball flight, we should do better."

This is about the **plane and the club's delivery**: the shaft, the hands, the path and what they do to the
ball. It borrows only the *form* of the Wrist and Posture sections (a fold after Summary, a presenter in C++,
words not colour, trust said once); none of their content.

Evidence for every number here reproduces from `docs/research/data/club_delivery/club_delivery_swings_2026-10-29.csv`
with `tools/diagnostics/club_delivery_extract.py` (Appendix A).

---

## 0. Summary

The Club delivery preset draws the swing plane curve and the hand plane, but nothing on the chart says what
they mean. A **Club delivery section** (This shot, Session, the Replay glance and a page in the session report)
reads them in four steps, each a short fold:

1. **The plane story** — one sentence per shot: where the plane was set, how it moved going back, how much
   steeper the hands came down than they went up, where it was delivered. Drawn on the plot as the coach draws
   it down the line: the **address line** (up the shaft at address) and the **shoulder plane** (from the ball
   through the trail shoulder), with the space between them, the slot.
2. **Plane and your ball flight** — the payoff. For each golfer, from their own shots with the launch monitor,
   how far the path and the attack angle move per degree of plane, and where this shot sits on that line. On
   Mark's 28 such shots, each degree steeper at P6 moves the path 0.9° left (r −0.80).
3. **At impact** — what the shaft did to the face and the loft: lie at impact turned into face direction
   (Δface ≈ Δlie × sin loft), shaft lean against dynamic loft, attack angle and low point.
4. **Shot shape** — the face-and-path grid (push / straight / pull × fade / straight / draw) with this shot
   marked and the session's shots behind it. Launch monitor first.

Later stages add the release (lag at P6, where the club passes the hands) and a picture strip of the golfer's
own down-the-line frames with the lines drawn. Nothing here grades: no new conditions, no norm changes.

---

## 1. What we have

### 1.1 The readings

| Reading | Key | What it is | Source |
|---|---|---|---|
| Swing plane curve | `swingPlaneCurve` | The plane through the target line and the shaft, address → impact + 40 ms, 5 ms grid. Shaft frames where the shaft is well seen; the hands fill where it points along the line (the top, the stretch after it, P2, P6). Bridged stretches are `valid = 0`. | `shaft_plane_curve.h`, fusion stage v8 |
| Hand plane | `handPlaneCurve` | The plane through the ball line and the hands, from the DTL ball and grip | same |
| Hand plane steepening | `handPlaneSteepening` | Downswing minus backswing hand plane over 40–70 % of the hands' rise | same |
| Swing plane angle | `swingPlaneAngle` | The two fitted 3-D planes, backswing (P1–P4) and downswing (P4–P7), absolute | `shaft_fusion.h` |
| Hand path loop | `handPathLoop` | The lead wrist's downswing path against its backswing path, % of rise | `dtl_posture.h` |
| Shaft lie | `shaftLie` | The DTL shaft angle at each P | DTL tracker |
| Impact shaft lean, lag | `impactShaftLean`, `lagAngle` | Face-on shaft lean; the lead-arm-to-shaft angle | face-on tracker |
| Our path, attack, low point | `clubPath`, `attackAngle`, `lowPointAhead` | Camera readings, the fallback to the monitor's | various |
| Launch monitor | `lm.clubPath`, `lm.faceAngle`, `lm.faceToPath`, `lm.attackAngle`, `lm.dynamicLoft`, `lm.lieAngle`, `lm.launchDirection`, `lm.spinAxis`, `lm.offline` | Per shot, at impact | the monitor |

### 1.2 What the chart does today

- The Club delivery preset draws the curves it has (the swing plane curve, the hand plane, lag, clubhead
  speed, …) and cards for the instant readings (path, face, attack, low point, dynamic loft, each with its
  monitor twin). In Replay it shows the glance cards only (`charts_traces_presenter.cpp`, `kReplayGlance`).
- There is no section after Summary. Wrist and Posture each have one; Club delivery has none, so nothing reads
  the curves, links them to the ball, or says what a number is good or bad for.

### 1.3 What this design must respect

- **The down-the-line camera is not calibrated.** The plane series all come from it and are used in full; what
  the missing calibration limits is only the comparison of absolute angles across cameras. Absolute plane angles carry a few degrees of camera
  placement: on 20 Oct, Jess and Nick shared a camera and both read 52° at address; Mark's 7 Oct camera read
  58°. So the section compares a golfer **with themselves**, or with others **on the same camera**, and every
  line it draws is the golfer's own (their address line, their shoulder plane), never a pro's absolute angle.
  Changes, and links to the launch monitor, are unaffected.
- **The launch monitor is primary** for path, face, attack, lie and loft (`feedback_lm_is_primary`). Ours
  show beside it, never instead.
- **A correlation is per golfer, with its n.** Pooled across golfers, plane at P6 against path reads r −0.87;
  per golfer it ranges from −0.80 (Mark) to −0.22 (Nick). The pooled figure is mostly golfers differing from
  each other. The section never pools.
- **The hands carry the top.** Where the curve is the hands' (the shaft end-on), it is said once, as a light
  band on the plot labelled "hands carry the plane here", not as a dashed guess.

---

## 2. What the library shows

### 2.1 The curve, per golfer (medians, P1…P7, degrees)

| Golfer, club | n | Swing plane curve P1 P2 P3 P4 P5 P6 P7 | Hand plane P1 P2 P3 P4 P5 P6 P7 | Steepening | Shoulder plane at address |
|---|---|---|---|---|---|
| Mark, 7-iron | 145 | 57.5 53.8 66.2 65.0 63.2 60.0 59.4 | 53.1 53.8 57.6 64.9 63.9 59.8 57.3 | +5.5 | 66.6 |
| Mark, gap wedge (5 Oct) | 31 | 58.1 54.8 61.8 65.4 64.9 61.7 60.2 | 53.8 54.9 58.2 65.4 65.8 61.8 57.7 | +8.1 | 67.6 |
| Nick, 7-iron | 28 | 52.0 50.5 63.8 63.7 63.3 60.1 59.8 | 47.6 50.5 56.5 63.7 60.8 59.8 58.1 | +4.6 | 67.2 |
| Jess, 8-iron | 23 | 52.5 54.6 54.7 60.5 59.5 57.9 57.9 | 49.5 54.6 56.7 60.5 59.5 57.7 55.8 | +2.9 | 66.2 |
| Chris, 7-iron | 8 | 49.0 52.4 77.3 64.3 67.3 61.9 58.3 | 46.0 52.3 59.9 64.4 61.3 58.7 58.2 | +7.4 | 67.2 |

Read across, every golfer has the published shape (§3): a dip or flat stretch at P2 (the shaft along the
line), a climb to the top, and a settle back towards the address value by impact, finishing 1–6° above it.
Jess is the flattest going back (54.7° at P3 against 63.8–66.2° for the men, on the same camera as Nick).
Chris goes back very steep (77° at P3, above his shoulder plane).

### 2.2 Plane and the launch monitor, per golfer

`fits` in Appendix A. Slope is the monitor's reading per degree of ours.

| Our reading | Monitor | Mark (n 28) | Jess (n 16) | Nick (n 17) |
|---|---|---|---|---|
| Swing plane at P6 | path | −0.92 °/°, r −0.80, scatter ±1.4° | −0.59, r −0.63, ±0.8 | −0.33, r −0.22, ±1.4 |
| Hand plane steepening | path | −1.02, r −0.79, ±1.4 | +0.02, r +0.02 | −0.24, r −0.21 |
| Swing plane at P6 | attack angle | −0.51, r −0.79, ±0.8 | −0.56, r −0.66, ±0.7 | +0.11, r +0.18 |
| Hand plane steepening | attack angle | −0.41, r −0.57 | −0.05 | −0.08 |
| Hand path loop (existing) | path | −0.39 per %, r −0.63 | +0.06 | −0.14 |

What it says:

- **Mark's ball flight follows his plane.** Each degree steeper at P6 moves his path 0.9° left and his attack
  0.5° steeper; the plane explains about two thirds of his path's swing-to-swing variation. Steepening tells
  the same story (his over-the-top move is the cause).
- **Jess's follows the delivery plane, not the steepening.** Her hands barely steepen (+2.9° median, little
  spread), so the steepening has nothing to explain; the plane at P6 still moves her path (r −0.63).
- **Nick's barely follows either** on these 17 shots. His path varies for other reasons (face, low point,
  where the ball sits); the section must say so rather than draw a line through noise.

Coverage: launch-monitor shots with a measured plane are 28 of Mark's (5 Oct), 17 of Nick's and 16 of Jess's
(20 Oct). The 5 Oct monitor record carries path and attack but not face or lie.

### 2.3 The slot

The **address line** is the swing plane curve's own P1 value; the **shoulder plane** is the plane through the
ball line and the trail shoulder at address (`shoulderLine_P1`, measured exactly as the hand plane is). Every
golfer's shoulder plane reads 66–68°, 9–17° above their address line.

How often the curve leaves the slot:

| | P3 above | P4 above | P5 above | P6 above | Any P below the address line |
|---|---|---|---|---|---|
| Mark (120) | 39 | 15 | 9 | 0 | 6 at P3, 3 at P6 |
| Nick (27) | 10 | 0 | 3 | 0 | 0 |
| Jess (16) | 0 | 0 | 0 | 0 | 1 at P3, P5, P6 |

So the slot is mostly a **picture**, not a detector: these golfers come down inside it. What differs between
them is where in the slot they travel and how far the hands move up it in transition, which is what §2.2's
readings measure. Above-the-slot at P3–P4 is a steep backswing (Chris, a third of Mark's swings); below the
address line is under the plane.

### 2.4 Two of Mark's swings, 5 Oct

| | P1 | P3 | P4 | P5 | P6 | P7 | Steepening | Monitor path | Attack |
|---|---|---|---|---|---|---|---|---|---|
| swing 7 | 59 | 72 | 68 | 67 | 65 | 61 | +11.7 | −4.3 | −5.4 |
| swing 26 | 57 | 60 | 64 | 62 | 58 | 58 | +2.6 | +3.5 | −0.9 |

The same golfer, the same session: the hands came down 9° steeper on swing 7, the delivery plane was 6°
steeper at P6, and the path was 8° further left with an attack 4.5° steeper. That pair is the section's whole
argument in one line.

---

## 3. What the literature gives

All four papers are in `references.json` (d14f0d8a).

- **The plane is not fixed.** Vaughan (1981) plotted the shaft plane moving through the downswing; Coleman &
  Rankin 2005 (`ref.coleman2005`) found the lead-arm plane steepens through it; Coleman & Anderson 2007
  (`ref.coleman2007`) found one plane fits the club's downswing for some golfers and not others.
- **Two phases.** Kwon et al. 2012 (`ref.kwon2012`): skilled downswings are semi-planar, a transition phase then
  a planar execution phase into impact. That is the curve's shape: it moves in transition and settles from P6.
- **The shape, measured.** Morrison, McGrath & Wallace 2018 (`ref.morrison2018`), 52 golfers with drivers: the
  shaft is 2.2° under the delivery plane at mid-backswing, on it in the late backswing, 5.5° above it at the
  top and 1.8° above early in the downswing, back on it at impact. The section's comparison line uses this
  shape, never its absolute angle.
- **The numbers near impact.** Delivery planes of 49.7° (driver, Morrison), 59.1° (5-iron) and 63.1° (wedge)
  (Shin et al. 2008, ISBS, no DOI); whole-downswing planes 3–5° steeper (Coleman & Anderson). Quoted in the
  trust fold as context, not drawn (§1.3).
- **Lie and the face.** A shaft more upright at impact than the club was built for tilts the face left by about
  the lie error × sin(loft): nothing on a driver, about a degree per 2° of lie on a 7-iron, more on a wedge. This
  is the norm row for `m_shaftLieDelta` already, written as geometry.
- **Face and path.** The ball starts mostly where the face points and curves away from the path by the face-to-
  path. The grid in §4.5 is that sentence drawn; it needs no reference beyond the monitor's own numbers.

---

## 4. What the golfer sees

### 4.1 Where it lives

A **Club delivery section** in Charts, on This shot and Session, built as the Wrist and Posture sections are
(a C++ presenter, a QML section in a Loader slot after Summary, a fold that remembers its state with a one-line
state when closed). It shows when the shot carries `swingPlaneCurve` or any `lm.*` delivery reading and the
Club delivery preset is chosen. Replay shows the head, the plane story and the shot-shape mark only. Print is a
page in the session report.

### 4.2 This shot (Analyse)

```
┌───────────────────────────────────────────────────────────────────────────────────────────┐
│ Club delivery                                                                    ▾ fold    │
│ At P6 your plane was 65°, in your steepest third, and the path went 4.3° left.             │  head
│                                                                                            │
│ Plane     set on 59° · 13° steeper than address at P3 · peaked at the top · delivered at 61°, 2° above  │  the plane story
│           where you set it. Hands 12° steeper coming down.                                 │  (§5.1)
│                                                                                            │
│ Ball flight   On your 28 shots with the monitor, each degree steeper at P6 moves your      │  §4.4
│               path 0.9° left. This one: 65° at P6, your steepest third → path −4.3°.       │
│               [ small scatter: P6 plane → path, your shots as dots, this one ringed ]      │
│                                                                                            │
│ At impact     Shaft 2° more upright than at address · lean 6° · attack −5.4°              │  §4.5
│               Lie: toe 1° up → face about 0.6° left on this club.                          │
│                                                                                            │
│ Shot shape    [3×3 grid: pull-fade … push-draw; this shot ●, the session ·]  Pull, slight fade │  §4.6
└───────────────────────────────────────────────────────────────────────────────────────────┘
```

**On the plot itself** (the Club delivery traces, above the section; drawn by the trace figure, no new chart):

- the **address line**: a hairline across the plot at this shot's P1 value of the swing plane curve, labelled
  "address";
- the **shoulder plane**: a hairline at the shoulder-plane value, labelled "shoulders";
- the **slot**: a faint wash between them;
- the hands' stretches: a light shade over the stretches where the swing plane curve is the hands' (§1.3), with
  no words on the plot (a label there hid the curve; Mark, 29 Oct). The trust fold says what the shade is.
- the swing plane curve and the hand plane in their own colours (orchid, crimson); the gap between them in the
  downswing is the club under (or over) the hands, said on hover, not shaded.

### 4.3 The head

One sentence, the shot's most consequential plane fact, chosen in this order:

1. With a monitor and a fit that holds (§6.2): the steepening or the P6 plane, whichever the golfer's own fit
   says moves their path more, with the path — "Your hands came down 12° steeper than they went up, and the
   path went 4.3° left."
2. With a monitor and no fit that holds: the path and the plane side by side, no "and so" — "Path 2.1° left;
   your plane was in its usual place, so look at the face and the strike."
3. No monitor: the plane story's largest departure from the golfer's own session median — "You came down 4°
   steeper than your usual."
4. No down-the-line camera: the section shows only §4.6 (the monitor grid) and says why the plane is absent,
   once, in the trust fold.

### 4.4 Plane and your ball flight

The section's reason to exist (§2.2).

- **What is fitted:** per golfer, over the shots the section can see that have both readings: monitor path
  against the swing plane at P6, and against the hand-plane steepening; monitor attack against the plane at P6.
  Least squares, slope, intercept, r, the residual scatter and n. In v1 that is **the golfer's day**: this
  session and the golfer's other sessions on the same date, read from their `swing_phasegrid.json` files (small,
  and they already carry the plane and the `lm.*` values). A day is one camera set-up, so the uncalibrated
  camera's offset cancels, and it is the only pool that lets Jess's and Nick's 20 Oct shots (three and two
  session folders, at most 10 monitor shots in any one) reach 12. Mark's 5 Oct set is one session. A fit across
  days mixes camera set-ups and needs the camera calibration first (§8.2).
- **What is shown:** the fit with the larger |r| for path, as a sentence ("each degree steeper at P6 moves your
  path 0.9° left") and a small scatter (this golfer's shots as dots, this shot ringed, the fitted line). The
  attack sentence follows when its own fit holds.
- **When it holds:** n ≥ 12, |r| ≥ 0.5, and the slope has the physical sign (steeper → path left, attack
  steeper). Otherwise: "On your 17 shots your path doesn't follow your plane (r −0.22) — the cause is elsewhere."
  That is a finding, not a failure, and it is said as one (Nick, §2.2).
- **This shot's place:** where its plane sits in the golfer's own range (steepest third / middle / flattest
  third), and the monitor's path beside it. Never a predicted path presented as a measurement: when the monitor
  is absent the sentence says "on your fit, a plane like this goes with a path about 3° left", with the scatter.
- **Too few monitor shots in the session:** "12 shots with the monitor are needed to see how your path follows
  your plane; this session has 7." The scatter still shows the 7.

### 4.5 At impact

Physics, not grading. Each line appears only when its inputs exist; the monitor's value first, ours beside it.

- **Lie → face.** `lm.lieAngle` if present, else the change in `shaftLie` from P1 to P7. Turned into face
  direction with the club's loft (`lm.dynamicLoft`, else the club's nominal loft from the club record):
  Δface ≈ Δlie × sin(loft). "Toe 2° up at impact → face about 1° left on this 7-iron." On a driver the sentence
  says the lie barely matters.
- **Lean → loft.** `impactShaftLean` beside `lm.dynamicLoft`: "Hands 6° ahead; the monitor saw 27° of loft."
- **Attack and low point.** `lm.attackAngle` (else ours) and `lowPointAhead`, one line.

### 4.6 Shot shape

A 3×3 grid: start line (push / straight / pull, from face angle) against curve (fade / straight / draw, from
face-to-path), with tolerances ±1° start, ±1° face-to-path. This shot is the dot with a ring and a word ("pull,
slight fade"); the session's shots are small dots. Monitor values only: our camera does not measure the face.
For a left-handed golfer the words mirror.

### 4.7 Session

```
┌───────────────────────────────────────────────────────────────────────────────────────────┐
│ Club delivery · 28 shots                                                                   │
│ Your path follows your plane: steeper at P6 by a degree, 0.9° further left (28 shots).     │
│ [ plane band: the session's swing plane curves as a middle-half band, this shot on top ]   │
│ Steady going back (P3 middle half ±1.5°), loosest at P5 (±2.6°) — that is where the path   │
│ variation starts.                                                                           │
│ [ P6 plane → path scatter ]   [ shot-shape grid, all shots ]                               │
└───────────────────────────────────────────────────────────────────────────────────────────┘
```

- the band uses `box_stats.h` per output-grid point (the one-box-plot rule applies to boxes; this is a band of
  the same quartiles, drawn as the session curves already are in `session_curves`);
- "where it varies most" names the P position with the widest middle half of the swing plane curve, and says
  whether the path's variation follows it (the fit above);
- the scatter and the grid show every shot; a click opens that shot.

### 4.8 Replay and print

- **Replay** (beside the video): the head and the plane story, and the shot-shape mark when the monitor has
  one. No scatter, no controls.
- **Print:** a Club delivery page in the session report (as `PpPosturePage` is): the session sentence, the band,
  the scatter, the grid, the trust note. Built from the same presenter payload.

### 4.9 Trust, said once

In the fold under the plot, once:

- the camera rule (§1.3): angles compared within one camera; the session's address and shoulder planes are the
  golfer's own;
- where the hands carry the curve, and why (the shaft points along the target line);
- each fit's n, r and that it is this golfer's own;
- the monitor's role (primary) and which readings it lacked on this session (5 Oct: no face, no lie);
- the literature line of §3, briefly.

Nothing in the body repeats it.

---

## 5. Content

### 5.1 The plane story, word by word

Built from the swing plane curve at P1, P3, P4, P6, P7 and the steepening, rounded to whole degrees
(`feedback_no_sigma_rounding`):

- **Set:** "set on {P1}°".
- **Going back:** "{|P3−P1|}° {steeper|flatter} than address at P3" (said against address, because the head may compare P3 with the usual) when |P3 − P1| ≥ 3, else "on that plane at P3".
- **The top:** "peaked at the top" when the maximum of P2…P5 is at P4 ± one position; else "peaked at P{n}".
- **Delivered:** "delivered at {P7}°, {|P7−P1|}° {above|below} where you set it" (≤ 1° → "where you set it").
- **Hands:** "Hands {steepening}° steeper coming down" (≥ 3°), "came down on the plane they went up" (|x| < 3),
  "{x}° flatter coming down" (≤ −3).
- **Against the published shape** (second line, Analyse only, when the shot reads it): "Most good players go back
  a little under the plane they deliver on, reach the top a few degrees above it and are back on it by impact;
  yours {matches|is steep from P3|stays under}." The comparison is P3 − P7 and P4 − P7 against Morrison's
  −2 and +5.5 with ±4° bands, said in words only.

### 5.2 Words for the slot

"inside the slot", "above the shoulder plane at P3 — a steep backswing", "under the address line at P6 — under
the plane". Said only when the curve is outside the slot at that P by more than its own σ.

---

## 6. Rules

### 6.1 Thresholds

| Rule | Value | Why |
|---|---|---|
| A fit is shown | n ≥ 12, \|r\| ≥ 0.5, physical sign | Mark's and Jess's fits pass, Nick's does not (§2.2) |
| "Steepest third" | the golfer's library tertiles of the fitted reading | own range, own camera |
| Going-back word | \|P3 − P1\| ≥ 3° | about twice the curve's σ at P3 |
| Hands sentence | \|steepening\| ≥ 3° | Jess's median +2.9 reads as "on the plane" |
| Shot-shape tolerances | ±1° start, ±1° face-to-path | the monitor's own resolution class |
| Session band shown | ≥ 5 shots with the curve | the band means nothing below |

All live in one header beside the presenter, each with its reason, the way `session_posture.h` carries its
decisions.

### 6.2 What never happens

- No absolute plane angle is compared with a pro's or a book's (§1.3); the literature numbers live in the trust
  fold.
- No pooled correlation (§2.2).
- No predicted path shown as a measurement.
- No new condition, no norm change, no grade. The plane conditions of e6fdf257 (backswing/downswing plane too
  upright/steep/flat) keep reading `swingPlaneAngle`; the section links to them in Findings when they fired,
  as other sections do.

---

## 7. What it does not do

- It does not calibrate the camera (that is the calibration thread; when it lands, the absolute numbers in §3
  can be drawn).
- It does not measure the face. Shot shape is the monitor's.
- It does not fit across golfers, or teach a model plane.
- It does not replace the Findings list: faults stay in diagnostics; this section explains the delivery.

---

## 8. Decisions (29 Oct 2026)

1. **Shoulder plane definition.** Ball through the trail shoulder (66–68° on everyone), as measured in §2.3.
   Named the shoulder plane, not the shoulder line, which reads as shoulder alignment to the target. The base of
   the neck (Hogan's pane, higher, a wider slot) is not used.
2. **The fit's scope.** v1 fits over the golfer's day, one camera set-up (§4.4): a session alone leaves Jess and
   Nick under 12 monitor shots. Fits across days, or per club, wait for the camera calibration.
3. **Replay.** The section's head sits under the glance cards; it does not replace them.
4. **Release.** Lag at P6 and where the club passes the hands is its own later section, with clubhead speed.
5. **Commits.** Each stage is committed and pushed when its tests pass, so the cabin PC pulls rather than
   patching.

---

## 9. Build plan

Commits only at the end of a stage, after Mark has judged its results. Every UI stage: annotated before/after
review sheets, `--probe-qml` before it is called done, then a windowed run. Two to three app builds per stage.

| Stage | What | Done when |
|---|---|---|
| **0. Evidence** (DONE 29 Oct) | `tools/diagnostics/club_delivery_extract.py` and the per-swing CSV, committed with this doc. | §2 reproduces from the CSV |
| **1. The shoulder plane** | Publish `shoulderPlane` (a scalar at P1) from the fusion stage beside the hand plane: the plane through the ball line and the trail shoulder at address, from the DTL pose and ball. Catalogue descriptor (chart only). Fusion stage version bump; cabin sweep (pose and ball reused). | The library's values match the CSV's `shoulderLine_P1` to 0.5°; `shaft_plane_curve_test` gains the case |
| **2. The presenter (C++)** | `ChartsDeliveryPresenter` beside the Posture and Wrist presenters, with `session_delivery.{h,cpp}`: the plane story (§5.1), the per-golfer fits and their rule (§4.4, §6.1), the impact lines (§4.5), the shot-shape classification (§4.6), the session band and "varies most" (§4.7), the head (§4.3) and the trust lines (§4.9). Nothing computed in QML. | Unit tests on fixtures built from the CSV reproduce §2.2's slopes and r, the three golfers' fit verdicts (Mark and Jess shown, Nick refused), §2.4's two sentences, and a no-monitor and a no-DTL fixture |
| **3. The section (QML) and the plot lines** | This shot, Session, the Replay glance; the address line, shoulder plane, slot and hands band on the trace figure; the scatter and the grid. | Review sheets on Mark (5 Oct, with monitor), Nick and Jess (20 Oct), Analyse/Replay, light and dark, approved; `qml_ui` probes for the section's objectNames |
| **4. Print** | The Club delivery page in the session report. | Review sheet of the page for each golfer |
| **5. Release** (its own section, §8.4) | Lag at P6, the club passing the hands, clubhead peak timing, against attack and low point. | Its own review |
| **6. The picture strip** | Address, top, P6 down-the-line frames with the address and shoulder planes drawn (the DTL coach-line sets exist). | Review sheet |

Stages 1–3 are the chart Mark asked for. 4 is print. 5 and 6 follow.

**Stage 6 as built (29 Oct, uncommitted).** `Gui/charts/delivery_picture.{h,cpp}`, the Posture strip's twin: pure C++ over the
swing document, on the presenter's `thisShot.picture`, drawn by `PpChDeliveryPicture.qml` (each frame a `PpChPostureFrame`,
which gained a filled `poly` mark for the slot) after the plane story, in Analyse and in the Replay glance (as Posture's strip
is). The frames are the down-the-line stills at P1, P4 and P6 by the report stills' rule, the crop the Posture picture's
widened to take in the ball. The address line runs from the DTL ball through the measured club sample nearest address (the
nearest MEASUREMENT within 60 ms: the replay's rule refuses a held frame that happens to sit nearest, which left swing 7 with
no line); the shoulder plane through the trail shoulder the `shoulderPlane` metric was read through (`shoulderPlaneFromHold`
on the same raw pose frames, so the drawn line's view-plane angle is the metric's); both to the frame's edge, the slot a wash
between them, frozen on all three frames and drawn whatever the reader's Coach lines toggles (they are the section's reading,
the plot's own hairlines). Each frame's note is the plane story's at that position ("plane 65° · inside the slot"). No ball:
the shaft line alone, said; no shoulder plane on the document: said, "re-analyse"; no down-the-line camera: nothing. Not
printed: the report's Club delivery page is 90 % (Mark 5 Oct) and 97 % (Jess 20 Oct) full and must stay one page.

---

## 10. Implementation plan

Read with §9. File paths are under `src/` unless shown otherwise. The section copies the Posture section's
seams exactly (presenter instantiated in `PpChartsPanel.qml`, visibility rule in `PpChartsBody.qml`, Loader
slots in the Shot and Session views, folds persisted through `_restoreFolds`, print through
`session_report_model.cpp`), so every step below names its Posture twin.

### Stage 1: the shoulder plane (producer)

Q1 decided: the trail shoulder (§8.1).

1. `Club/shaft_plane_curve.h`: `shoulderPlaneInclDeg(cam, sx, sy, bx, by)`, the hand plane's maths on a shoulder
   point, with its σ from the pose's placement.
2. `Analysis/analysis_pipeline.cpp`, the swing plane block of ShaftFusionStage: the trail shoulder from
   `ctx.detail->poseDtl` (handedness-aware: the right shoulder for a right-handed golfer) as the median over the
   address hold (before P1 − 100 ms, the ball finder's window), with the DTL ball; publish `shoulderPlane` as one
   phase sample at Address. Log it on the swing plane curve's line.
3. `Metrics/metric_catalogue_manifest.cpp`: descriptor `shoulderPlane` (PointInTime, Club delivery, chart only,
   `usedBy chart:review`); `metric_providers.cpp`, `Club/shaft_track_assembly.cpp` (covers); counts in
   `metric_catalogue_test.cpp`.
4. `Analysis/analysis_versions.h`: fusion stage 8 → 9.
5. `Analysis/tests/shaft_plane_curve_test.cpp`: a synthetic shoulder on a known plane reads it.
6. Guides: the metric catalogue row and the shaft tracker bullet.
7. Mac check on the three scratch swings against the CSV's `shoulderLine_P1`; then commit, push, cabin sweep (pose,
   ball reused; nothing grades it, so no regrade).

### Stage 2: the presenter (C++)

1. `Gui/charts/session_delivery.{h,cpp}` (pure C++, the `session_posture.h` form):
   - `DeliveryInput deliveryInputFromShot(const ShotSeries&)`: the swing plane curve and hand plane at P1…P7
     (sampled at the shot's `phases`, as `positionUs` does), the steepening, the shoulder plane, `shaftLie` at P1/P7,
     `impactShaftLean`, the `lm.*` values, the club and its nominal loft.
   - `planeStory(in)` → the §5.1 sentence parts; `slotWords(in)`.
   - `deliveryFit(inputs, x, y)` → {n, slope, intercept, r, sd, holds} with the §6.1 rule; the presenter fits
     path ~ P6 plane, path ~ steepening, attack ~ P6 plane and keeps the stronger path fit.
   - `impactLines(in)` (§4.5), `shotShape(in)` (§4.6), `sessionBand(inputs)` and `variesMost(inputs)` (§4.7),
     `deliveryHead(...)` (§4.3), `deliveryTrust(...)` (§4.9).
   - The §6.1 thresholds as named constants at the top, each with its reason.
2. `Gui/charts/charts_delivery_presenter.{h,cpp}`: `ChartsDeliveryPresenter`, the `ChartsPosturePresenter` shape:
   inputs `model`, `pickedSwingDir`, `pickedDetail`, `register`, `playheadUs`, `scope`; outputs `available`,
   `busy`, `choices` (with `onShowKeys` = the plane and `lm.` delivery keys), `thisShot`, `session`, `standfirst`,
   and `plotLayers` (per row key: the address and shoulder `hline`s, the slot `band`, the hands-carry `span`s,
   with their labels). One coalesced job on the single-thread pool; `setSnapshot`/`setSynchronous`/`waitForIdle`
   test seams; `deliverySessionReportPayload(snap, …)` for print.
3. `cmake/PinPointQmlModule.cmake`: register both sources.
4. Tests, `Gui/tests/charts_delivery_tests.cmake` (included OPTIONAL from `Gui/tests/CMakeLists.txt` beside the
   posture one), sources from `charts_wrist_sources.cmake`:
   - `session_delivery_test`: fixtures cut from the CSV (Mark 5 Oct, 28 monitor shots; Jess and Nick 20 Oct)
     under `Gui/tests/data/delivery/`: §2.2's slopes and r to 0.01, Mark and Jess shown, Nick refused; §2.4's two
     plane stories word for word; a no-monitor shot (no fit, the §4.3 rule 3 head); a no-DTL shot (grid only);
     the lie → face line on a 7-iron and a driver.
   - `charts_delivery_presenter_test`: an in-memory session, `thisShot`/`session` payload shapes, `plotLayers`
     values, the scope switch.

### Stage 3: the section and the plot lines (QML)

1. `Gui/charts/PpChartsPanel.qml`: instantiate `ChartsDeliveryPresenter` beside the posture one (same bindings);
   hand it to the body as `deliveryP`; add `delivery`, `deliverySession` to `_restoreFolds`.
2. `Gui/charts/PpChartsBody.qml`: `deliveryOnShow`, `deliveryShotShown`, `deliverySessionShown` on the posture rule;
   payload aliases; `busy`.
3. `Gui/charts/PpChDeliveryShotSection.qml` (fold key `delivery`, objectNames `chDeliverySection`,
   `chDeliveryHead`, `chDeliveryStory`, `chDeliveryFlight`, `chDeliveryImpact`, `chDeliveryShape`); Replay glance =
   head, story, shape mark. `PpChDeliverySession.qml` (fold `deliverySession`). The scatter reuses
   `PpChScatter.qml`; a new `PpChShotShapeGrid.qml` for §4.6.
4. Slots: `PpChartsShotView.qml` after `chPostureShotSlot` (`chDeliveryShotSlot`); `PpChartsSessionView.qml` after
   the posture session slot.
5. `Gui/charts/PpChTraceFigure.qml` `layersFor(r)`: append the presenter's `plotLayers[r.key]` (the plot already
   paints `hline`, `band` and `span`; no painter change), plus a small label overlay for "address", "shoulders"
   and "hands carry the plane here".
6. Tests: `Gui/tests/qml/tst_charts_panel.qml` cases for the section's objectNames, fold persistence and the Replay
   glance; `qml_ui_test.cpp` `feed()` gains a delivery branch; `tests/qml/charts/ChartsHarness.qml` instantiates
   the presenter; a probe `Gui/tests/probes/charts_delivery.qml`. Single tst files only, never the whole
   `qml_ui` run.
7. Review sheets (before/after, red boxes): Mark 5 Oct swing 7 and swing 26, Nick and Jess 20 Oct, Analyse and
   Replay, light and dark. Then a windowed run.

### Stage 4: print

`Gui/report/report_delivery.h` (`deliveryReportBlock`, as `report_posture.h`); `session_report_model.cpp`
`readChartsPages()` and the `map["delivery"]` block; `Gui/report/PpDeliveryPage.qml` placed after the Posture and
Wrist pages in `PpSessionReport.qml`; `report_model_test.cpp` and `tst_session_report.qml` cases with a
`qml/data/session_report/delivery.json` fixture.

### Order and cost

Stage 1 is independent and small; it can land while stage 2 is written. Stages 2 → 3 → 4 run in order. Each
stage: at most three app builds, the affected tests once, results shown to Mark before its commit.

---

## Appendix A: reproduce

```
tools/diagnostics/club_delivery_extract.py extract /mnt/swingdata docs/research/data/club_delivery/club_delivery_swings_2026-10-29.csv
tools/diagnostics/club_delivery_extract.py medians docs/research/data/club_delivery/club_delivery_swings_2026-10-29.csv   # §2.1
tools/diagnostics/club_delivery_extract.py fits    docs/research/data/club_delivery/club_delivery_swings_2026-10-29.csv   # §2.2
```

262 swings, extracted 29 Oct 2026 from the library after the ball-finder and hand-plane sweep (4827699c,
a412aed3). The curve values come from each swing's `swing_phasegrid.json` (the app's own sampler); the monitor
values and the shoulder plane from `swing.ppsw`. The slot counts of §2.3 and the swing pair of §2.4 are read
from the same CSV.
