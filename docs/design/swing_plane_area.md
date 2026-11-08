# The Swing plane chart and the Plane & path area

Decided with Mark, 8 Nov 2026. The Charts panel listed ten plane readings under names that overlapped, built on about
four measurements. This note says what each one is, where it now lives, and what the conditions grade.

Nothing here changed a producer. Keys and saved documents are as they were; labels, groups, cards and the four
measures' bindings changed. A session's ledger regrades when it is next opened (the content stamp covers the pack).

## The readings

| Key | Label | What it is | Where |
|---|---|---|---|
| `swingPlaneCurve` | Swing plane | Plane through the target line and the shaft, frame by frame, from the down-the-line view; hands stand in where the shaft defines no plane | Main plot and card: Address, P3, Top, P6, Impact |
| `handPlaneCurve` | Hand plane | Plane through the ball line and the hands | Second line on the plot |
| `shoulderPlane` | Shoulder plane at address | Ball to trail shoulder at address, the steep edge of the slot | Line on the plot, tile on the Swing plane card |
| `swingPlaneAngle` | Fitted plane | Best-fit 3-D plane through the two-camera shaft direction, one per half, to the ground | Card: back, down |
| `swingPlane` | Fitted plane vs address | The same two, less the address plane | Tiles on the Fitted plane card, with Δ PLANE |
| `handPathLoop` | Hand path loop | Hands' downswing path against their backswing path, % of hand rise | Over-the-top card |
| `handPlaneSteepening` | Hand plane steepening | The hand plane's downswing half less its backswing half, at the same height | Tile on the hand path loop's card |
| `transitionPlaneDelta` | Transition plane delta (experimental) | Face-on ellipse fitted to each half, then subtracted | Own card, with ⚠ |

`shaftLie` stays with shaft lean in Club delivery: it is the curve's number at P1 and P7 (medians 0.0° and −0.2° apart),
read as a lie. `shoulderPlaneAngle`, renamed Shoulder tilt, is the shoulder line's image tilt and stays in Body rotation.

`swingPlaneIotaBack/Down` are tilts against the face-on image, not the ground. They stay in the document, are not offered
on any chart (`isResearchOnlySeries`).

**Every plane Δ now reads + = steeper.** Δ PLANE carried the path's sign from 29 Sept to 8 Nov.

## What the conditions grade

The eight plane conditions moved from the two-camera fit to the curve, with their measure ids and norm values unchanged.

| Measure | Reads | Conditions |
|---|---|---|
| `m_backswingPlaneAngle` | curve at P3 | `backswing_plane_too_upright`, `backswing_plane_too_flat` |
| `m_downswingPlaneAngle` | curve at P6 | `downswing_plane_too_steep`, `downswing_plane_too_flat` |
| `m_shaftPlaneBackswing` | curve, P1→P3 | `steep_backswing_plane`, `flat_backswing_plane` |
| `m_shaftPlaneDelivery` | curve, P1→P6 | `steep_downswing_shaft`, `under_plane_stuck` |

Why: the curve reads P3 and P6 on 96 % of down-the-line swings, the fit gets both halves on 45 %, and within a session
the curve at P6 follows the launch monitor's club path at least as well (Mark r −0.78 against −0.70 for the fitted
downswing plane, 28 and 27 shots).

Read-only dry run over the library, existing norms, fired / assessable (284 swings, 204 of them Mark's):

| Condition | Fit | Curve |
|---|---|---|
| `backswing_plane_too_upright` | 9/116 | 38/179 |
| `backswing_plane_too_flat` | 11/116 | 2/179 |
| `downswing_plane_too_steep` | 17/114 | 1/182 |
| `downswing_plane_too_flat` | 1/114 | 0/182 |
| `steep_backswing_plane` | 0/110 | 9/173 |
| `flat_backswing_plane` | 0/110 | 0/173 |
| `steep_downswing_shaft` | 19/107 | 11/176 |
| `under_plane_stuck` | 0/107 | 0/176 |

## What a position reads

A position is an instant. The two plane curves are already fits (`planecurve::fitCurve`), so every surface reads them at the
position's instant, through no window: `reduceInstant`, keyed by `isFittedCurve` (`src/Metrics/fitted_curve.h`). That covers
the phase grid (so the conditions and the Club delivery section), the Charts card tiles, the headline tiles and the Readings
tab. The plot draws the curve itself, and its peak is on that line. An instant inside a bridged stretch has no reading and
shows "-".

Dry run over the library, against the ±15 ms median it replaced (184 swings with the curve): median change 0.09° at P3,
0.04° at P6, 0.11° at P1; four verdicts change; 15 swings lose their P3 reading and 5 their P6, because the position falls in
a bridged stretch. Every other reading still goes through its own window; choosing those from measured noise and timing is
separate work.

## Where it lives on Your swing

The area is Plane & path on Home's Your swing tab and Plane and path in the Consistency table, the row after Wrists and
release (condition group `planePath`, `swing_areas_regroup.md`). Swing plane is still the chart's name.

A condition belongs to the area when it is about the club's route: the takeaway, the shaft at the top, the plane and the
path. That gives 18:

| Part of the route | Conditions |
|---|---|
| Takeaway | `inside_takeaway`, `outside_takeaway` |
| Shaft at the top | `across_the_line`, `laid_off`, `overswing`, `club_short_of_parallel` |
| Plane | the eight above, plus `over_the_top` (hand path loop) and `shallowing` (transition plane delta) |
| Path | `in_to_out_path`, `out_to_in_path` |

The Consistency table reads the area over every span, P1-P2 to P7-P8.

## What to keep in mind

- **At P2, the top and most P6s the curve is the hand plane.** Within 0.5° on 100 % of swings at P2, 99 % at the top, 76 %
  at P6 and 49 % at P5; 0 % at P1, 13 % at P3, 6 % at P7. So the two P6 measures grade the hands' plane on about three
  quarters of swings. The shaft cannot define a plane there.
- **One uncalibrated camera.** The absolute angle carries a few degrees of camera placement. The norms were written for
  the fitted plane and have not been re-derived; on the curve they fire more at P3 and much less at P6.
- **No ground truth for any plane.** The launch monitor is a proxy, and it says nothing about Nick's swings (curve at P6
  r −0.05 with path).
- **Transition plane delta** correlates about 0 with the fit's change, steepening, the hand path loop and club path, and
  `shallowing`, which reads it, has not fired in 221 shots. It stays on show because readings are shown with a warning,
  not withheld.
