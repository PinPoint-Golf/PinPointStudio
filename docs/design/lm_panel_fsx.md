# Launch monitor panel: the FSX design

Status: design signed off by Mark on 13 Oct 2026. This is round 4c of mock A. Implementation is in progress.

## What it is

The launch monitor stage panel is redrawn in Foresight FSX's graphic grammar, inside the typeset themes. GC Quad owners already read FSX fluently: a tile with a big value and an AVG ± SD footer, dispersion ellipses on carry arcs, and an annotated club view. A different grammar would read as wrong. Inside these figures, FSX's conventions take precedence over §15's no-boxes and colour-only-for-out rules. Themes, print, em size classes, and fold-don't-drop still follow §15.

The spec is the signed-off mock: `build/run-me/lm-variants/qml/Panel_A_fsxpro.qml`, with its notes in `NOTES.md` beside the scratch copy. The review sheets are in `build/run-me/lm-variants/round4/`. Where this document and the mock disagree, this document wins on the model contract and the mock wins on the look.

### The two views (quiet PpTabs)

**This shot:**
- The glance tiles sit on top.
- Below them are four club figures, all exactly the same size:
  - **Path:** the plan view, with the path and face arrows on the head.
  - **Loft:** the side view, with dynamic loft and attack.
  - **Spin:** the spin axis on a plain ball.
  - **Impact:** the face view, with the impact patch, the theme's `centreMark`, Foresight club markers (5 mm, near-white) when the device is a Foresight unit, and up–down, heel–toe and lie in words.
- Then comes the true-scale flight, in a side view and a top view.
- **Order of the figures:**
  - The 2×2 follows FSX image 03: Path and Spin on top, Loft and Impact below.
  - A row or a column uses the order Path, Loft, Spin, Impact.
- **Placement by screen shape:**
  - On wide, short screens, the 2×2 sits beside the flight.
  - On medium screens, the 2×2 sits above the flight, and the flight falls below the fold.
  - On narrow screens, the figures form one column.
  - A row of four is only a fallback.
- **Fold order:** the flight shrinks or strips first. The four figures never fold before the flight.

**Session:**
- **The ball half:** dispersion with enclosing ellipses on carry arcs, launch × spin, and ball tiles.
- **The club half:** an impact map (every strike on the face render, with its enclosing ellipse), path × face (with the face-to-path diagonal and the push, pull, draw and fade quadrants), and club tiles.
- Every tile carries a strip showing every shot.
- Clicking the club key sets the scope.

**Words, not signs:**
- L/R, UP/DOWN, OPEN/CLOSED, HEEL/TOE and LOW/HIGH.
- Missing values show "-".

### Ellipse rules

These are Mark's rules:
- An ellipse encloses every shot in its group: it is the minimum-area enclosing ellipse (Khachiyan), not an SD ellipse.
- A group of fewer than 3 shots gets no ellipse.
- No ellipse is ever drawn without its dots.
- Ellipses are drawn in outline only.
- A session with several clubs is fine; each club gets its own ellipse.

### Interaction (Analyse view only; cast and print are static)

- **Click to focus:** clicking a mark in a Session figure or strip focuses that shot. This is drill-in from evidence, such as a thin strike or a hook.
- **No shot list.** The panel does not navigate shots. The carousel and the session view do that.
- **Hover card:** hovering a mark shows a linked card in words. Clicking or tapping pins the card.
- **Pinned card actions:**
  - **Leave out:** excludes the shot.
  - **Club:** fixes a wrong club label.
  - Both actions are inline. There are no menus or dialogs.
- **Mismatch note:** for example, "Eight shots recorded as Driver are treated as 7 iron · Undo".
- **No keyboard shortcuts.** Keyboard accessibility is planned for the whole app later.

## Model contract

**Rule:** the numbers come from C++. The QML draws. The mock's own JS computations do not survive the port:
- the enclosing ellipse;
- the mismatch rule;
- the club override;
- the anchors table.

The field names below are binding. An implementer who needs to change one must say so in their report.

### Artwork: `LmClubArt` (new QML singleton, `src/Gui/launchmonitor/lm_club_art.{h,cpp}`)

The art is vendored from `~/Projects/Resources`:
- The vendoring script is `tools/lm_art/vendor.sh`.
- `src/Gui/resources/lm/VENDORED.md` records the Resources commit.
- The anchors are read from the vendored JSON at runtime. No anchor table is copied by hand.

| Member | Returns |
|---|---|
| `Q_INVOKABLE QString headFor(QString club)` | `driver` \| `3wood` \| `hybrid` \| `iron` \| `wedge`, mapped from a club label. The mapping is driver; woods; hybrids; irons 2–9 → `iron`; PW and wedges → `wedge`. |
| `Q_INVOKABLE QUrl image(QString head, QString view, QString look)` | `view` is `top` \| `side` \| `face` \| `back`. `look` is `light` \| `dark` \| `line-black` \| `line-white`. |
| `Q_INVOKABLE QVariantMap anchors(QString head, QString view)` | The anchor JSON as a map. Pixel fields are rescaled to the vendored image size. |
| `Q_INVOKABLE double strikeOriginMm(QString head)` | Foresight's face centre minus our `face_centre`, up the face, in mm: 7 iron +6.17, wedge −0.63, woods 0. The 7 iron's centre is midway between score lines 6 and 7; derive this from `clubheads/src/clubs.py` and cite it. |
| `Q_INVOKABLE QUrl ballImage(QString look)`, `Q_INVOKABLE QUrl patchImage(int mm)` | The plain ball (side view) and the impact patch (20, 25 or 30 mm, tintable SVG). The patch is 25 mm for woods and 20 mm for irons and wedges. |

**Image resolution:** the Impact face must reach ≥3 px/mm in physical pixels at Mark's full screen on a 2× display. Downscale in the vendoring script to the smallest size that meets that. Budget: the vendored art totals ≤15 MB.

### `LmSessionModel` additions

| Member | Shape |
|---|---|
| `graphics.shots` | One entry per shot in the session, in shot order: `{ id, number, club, recordedClub, clubSource: "recorded"\|"evidence"\|"user", excluded, values: { <lm key>: double }, evidence: { suggest, votes, of } }`. The `values` are the raw doubles, so scatter points no longer stack on rounded display strings. `club` is the effective club. |
| `graphics.clubs` | One entry per effective club among the included shots: `{ club, head, n, stats: { <lm key>: { mean, sd, n } }, ellipses: { dispersion, launchSpin, strike, pathFace } }`. Each ellipse is `{ cx, cy, a, b, angleDeg }` in that figure's units, or absent when n < 3. `strike` is in face mm, measured from the Foresight origin. |
| `graphics.mismatch` | `{ count, recorded, suggested, shotIds }`, or empty. This drives the note. |
| `graphics.device` | `{ name, maker, markers: "four"\|"one"\|"" }`. It comes from the shot data's device, not only the live connection. Use `four` for GC Quad, QuadMAX, Falcon and GCHawk, `one` for GC3 and HMT-only, and empty otherwise. |
| `graphics.leftHanded` | This publishes the convention. Values stay in the device's convention, and the panel mirrors the art. |
| `Q_INVOKABLE void setShotExcluded(int shotId, bool excluded)` | This one is persisted. |
| `Q_INVOKABLE void setShotClub(int shotId, QString club)` | `""` restores the default (the evidence or the recorded club). Choosing the recorded club explicitly declines the evidence. This one is persisted. |
| `Q_INVOKABLE void undoMismatch()` | Marks every evidence-relabelled shot in the note as declined. This one is persisted. |

**Rebuild rules:**
- The board's means, SDs, grades, corridors, scope text, tiles and ellipses are all computed without the excluded shots.
- All of them use the effective club. Today they scope by the recorded club, which is why fixture shot 8 read "Heel" against the Driver scope.

**Club evidence:** the evidence rule lives in `src/Analysis/lm_club_evidence.h`. It is pure, with no Qt GUI, and has its own test.
- **The three votes:**
  - dynamic loft: ≤ 17° says wood, ≥ 20° says iron;
  - smash: ≥ 1.42 says wood, ≤ 1.40 says iron;
  - spin: ≤ 3500 says wood, ≥ 4000 says iron.
- **Decision:** two votes decide.
- **Suggestions:**
  - The suggested club is the session's most common club of the other family.
  - When the session has no club of the other family, the suggestion is a generic one ("an iron", "a wood"), and the shot is flagged but not relabelled.

**Enclosing ellipse:** the enclosing ellipse lives in `src/Analysis/lm_enclosing_ellipse.h`. It is Khachiyan's algorithm, made exact by expanding until every point is enclosed, and it has its own test.

**Persistence:**
- `excluded` and `clubOverride` are optional per-shot fields written to the shot's saved record through the existing save path.
- Older readers ignore them, and older files load without them (see the `swing.json` compatibility rules).
- They are user annotations, not analysis output, so no stage version is bumped.

### Panel (QML)

- **Files:** `PpLaunchMonitorPanel.qml` and `PpLmGraphicsBody.qml` carry the design.
- **Old components:** `PpLmCard`, `PpLmDiagram`, `PpLmRead`, `PpLmRule` and `PpLmSpread` are kept only if something outside the panel still uses them. Check `ScreenWrist`, `PpSessionDiagnosticsPanel` and `ScreenSessionMode`.
- **Interaction API:** the body gets `property bool interactive` and `signal shotRequested(int shotId)`. The panel wires `shotRequested` to SessionMode's focus.
- **Cast and print** set `interactive: false`.
- **Theme:** colours come from theme roles only, including `centreMark`.

### Addendum, 13 Oct (round 2 of the implementation)

| Member | Shape |
|---|---|
| `graphics.clubs[].stats.<key>.min`, `.max` | The per-club extremes over included shots. The Session tiles' range and spray read these instead of scanning `graphics.shots` in QML. |
| `LmClubArt.markers(head)` → list | Foresight club-marker positions for that head, as `[{ name: "toe"\|"heel"\|"lo"\|"hi"\|"one", xMm, yMm }]` in `face_frame` mm, measured from our `face_centre`. This is the four-dot layout. `one` is the single GC3/HMT dot. The values come from the table now in `PpLmGraphicsBody.qml`, citing Foresight's fiducial guide. |
| `Theme.typeset.lm` | The panel's colour roles (`centreMark`, `clubSeries` and the rest of `_lmRoles`) live in each theme's typeset block, with defaults in `_typesetDefault`. They no longer live in the body. |

**Evidence rule decision (Mark, 13 Oct):** the rule judges each shot on its own numbers. A recorded club's group is never moved as a whole.

**The old Tiles/Graphics switch is retired.** The FSX design is the whole panel, and `appSettings.lmPanelMode` (`ui/lmPanelMode`) is removed.

## Done means

- This shot and Session match the round-4c sheets at Mark's window sizes (2180×1320 and his wide-short full screen, fontScale 1.35) and at 1680×1050, in Folio and Vector, on screen and in print.
- Excluding a shot and relabelling a club survive a reload of the session.
- The new Analysis headers have passing tests, through ctest.
- Mark has looked at the renders and the try-out before the full `qml_ui` suite runs.
- Nothing is committed until Mark approves. The flight-model fix in `lm_flight_path.h` is still uncommitted and joins the same commit set.
