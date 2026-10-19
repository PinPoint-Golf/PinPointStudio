# Shots

*Directory:* `src/Gui/shot/`  
*9 components · 2,809 lines of QML*

The shot carousel/card/filter/action bar, export options, swing edit panel, shot pip row, the data-warning format singleton, and the wall-cast session-diagnostics window.

**C++ in this area:** `CurrentSwing`, `DiskReplaySource`, `PpcpClipFiler`, `ReanalysisController`, `ReplaySource`, `ShotController`, `ShotFilterProxyModel`, `ShotListModel`, `ShotProcessor`, `ShotReplayController`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [DataWarningFormat](#datawarningformat) | 92 | `QtObject` | 2 | 0 | singleton |
| [PpExportOptionsSheet](#ppexportoptionssheet) | 197 | `Popup` | 1 | 5 | 1 local comp. |
| [PpSessionDiagnosticsWindow](#ppsessiondiagnosticswindow) | 172 | `Window` | 1 | 2 |  |
| [PpShotActionBar](#ppshotactionbar) | 431 | `Item` | 1 | 7 |  |
| [PpShotCard](#ppshotcard) | 387 | `Rectangle` | 1 | 8 |  |
| [PpShotCarousel](#ppshotcarousel) | 725 | `Item` | 2 | 12 |  |
| [PpShotFilter](#ppshotfilter) | 450 | `Item` | 1 | 6 |  |
| [PpShotPipRow](#ppshotpiprow) | 120 | `Item` | 1 | 0 |  |
| [PpSwingEditPanel](#ppswingeditpanel) | 235 | `Item` | 1 | 12 |  |

### DataWarningFormat
`src/Gui/shot/DataWarningFormat.qml` · 92 lines · root `QtObject` · **singleton** · no direct `Theme.*` refs

> The words for a shot's data-integrity warning, from its facts (ShotListModel's
> dataWarningDetail: swing_doc.h dataWarningDetailFrom plus the shot processor's
> clubRefused / dtlTopOutOfView). ONE wording for every place the ⚠ is explained —
> the film-strip card's tooltip and the swing panel's warning block — so the two
> can never tell a reader different things about the same shot.

- **Functions:** `reasons(detail)`, `subject(detail)`, `text(detail)`
- **Used by (2):** `PpShotCard`, `PpSwingEditPanel`

### PpExportOptionsSheet
`src/Gui/shot/PpExportOptionsSheet.qml` · 197 lines · root `Popup`

> Bulk-export options popover — opened from the shot carousel's ⋯ menu. Lets the
> user pick which cameras' video to include and whether to bundle the JSON data,
> then emits confirmed() with the selection. The host (PpShotCarousel) calls
> swingExporter.exportShots() and shows the completion toast. thumb.jpg is always
> included; raw frames never are — so the only choices are cameras + JSON.

- **Properties:** `var cameras`, `var swingDirs`, `int shotCount`, `bool includeJson`
- **Signals:** `confirmed(var selectedVideoFiles, bool includeJson)`, `cancelled()`
- **Local components:** `CheckRow`
- **Uses:** `PpButton`, `PpCardNote`, `PpMicro`, `PpPopoverCard`, `PpPressable`
- **Used by (1):** `PpShotCarousel`

### PpSessionDiagnosticsWindow
`src/Gui/shot/PpSessionDiagnosticsWindow.qml` · 172 lines · root `Window`

> PpSessionDiagnosticsWindow — the wall-cast surface for the SESSION DIAGNOSTICS panel.
> A top-level window placed on the secondary display, hosting PpSessionDiagnosticsPanel:
> what keeps happening this session and what the model thinks is causing it.
> The target screen, the kiosk map-then-fullscreen dance, the mirror transform, the pin,
> the dwell, the windowed geometry and the viewing-distance scale are all user-kept
> behaviours around the content. No `sessionType`: the diagnostics panel gates on
> available data and devices, never on the session's type, so it has nothing to do
> with one.
> TWO MODELS, ON PURPOSE FOR NOW. PpSessionDiagnosticsPanel instantiates its own
> SessionDiagnosticsModel and claims SessionMode.sessionDiagnostics on completion (last wins).
> With an in-app panel AND this cast up there are therefore two models ingesting the same
> shots independently — wasteful, and correct: both are wired to the same ShotProcessor
> signal, both are idempotent per shot id, and both reduce the same rows to the same surface.
> The seam the carousel pips read is simply whichever came up last. Sharing one model between
> the two is a lifetime question (who owns it when the stage panel is torn down and the cast
> is not) that is not worth answering before the wasted CPU is measurable.

- **Properties:** `bool mirror`, `bool kiosk`, `var targetScreen`, `bool autoClose`, `readonly bool interactive`, `bool pinned`, `readonly bool dwellHeld`, `real contentScale`
- **Uses:** `PpSessionDiagnosticsPanel`, `View3DKeepAlive`
- **Used by (1):** `Main`

### PpShotActionBar
`src/Gui/shot/PpShotActionBar.qml` · 431 lines · root `Item`

> PpShotActionBar — the scope-aware action header of the shot carousel. ONE
> surface for the two action scopes that survived the PpShotPanel removal:
> · FOCUSED SHOT (the swing on the stage)  — Export · Re-analyse · Move to trash
> · FILTERED SET ("N of M shown")          — folded in as the "all N shown ▾"
> menu, or the whole bar in Capture
> Three states fall out of (summary.valid, filterActive):
> 1. focus + filter — identity left; this-shot actions; "all N shown ▾" control
> 2. focus, no filter — identity left; this-shot actions only
> 3. no focus, filter — set descriptor left; the actions act on the SET
> Re-analyse appears both as the focused-shot button (reanalyseShot) and, in the
> "all shown" menu, as a set-scope action (reanalyseShown) for bulk corpus re-runs.
> Host-injected: the bar owns no model lookups. Pure bindings — handlers only
> forward a signal or open/close the menu; no computation lives here.

- **Properties:** `var summary`, `bool filterActive`, `int visibleCount`, `int sourceCount`, `string filterLabel`
- **Signals:** `exportShot()`, `reanalyseShot()`, `trashShot()`, `editRequested()`, `exportShown()`, `reanalyseShown()`, `trashShown()`
- **Uses:** `ClubFormat`, `PpButton`, `PpMicro`, `PpPopoverCard`, `PpPressable`, `PpQualityPill`, `PpStarRating`
- **Used by (1):** `PpShotCarousel`

### PpShotCard
`src/Gui/shot/PpShotCard.qml` · 387 lines · root `Rectangle` · 3 hard-coded colour(s)

> One shot card in the carousel film-strip. Video still when recorded (or a
> neutral placeholder until real thumbnails arrive), wrist-angle trace
> fallback for IMU-only shots. Overlays: ordinal chip, quality pill, and a
> read-only star row on a bottom scrim. The delegate's required properties
> are auto-filled from the ShotFilterProxyModel roles; the carousel also
> reads them back to feed the review panel, so all roles are declared even
> where this card doesn't render them.
> ...and, when a session diagnostics panel is up, one more: a PIP ROW — one pip per tracked
> condition for this swing, plus the count that fired (design 13a, brief §6.1). It is what
> makes the carousel the way INTO the panel post-session rather than a strip of thumbnails
> beside it: the row is that swing's whole diagnostic read at a glance, so a reader picks the
> shot they want to open instead of stepping through them. It costs a null check when no
> panel is up — see the Loader below.

- **Properties:** `required int shotId`, `required int ordinal`, `required string timestampLabel`, `required string club`, `required bool hasVideo`, `required url thumbnailSource`, `required var tracePoints`, `required int score`, `required int rating`, `required string note`, `required var metrics`, `required string swingDir`, `required bool dataWarning`, `required var dataWarningDetail` … (+6 more)
- **Signals:** `tapped()`, `rated(int newValue)`
- **Uses:** `DataWarningFormat`, `PpMicro`, `PpQualityPill`, `PpShotPipRow`, `PpStarRating`, `PpTrace`, `SessionMode`, `TrustFocus`
- **Used by (1):** `PpShotCarousel`
- **Exercised by tests/tools:** `tst_shot_card`

### PpShotCarousel
`src/Gui/shot/PpShotCarousel.qml` · 725 lines · root `Item`

> Session-shot carousel — the film-strip rail docked at the bottom of the
> mode screens. Reusable: each screen supplies its own metricKeys (the panel's
> summary metrics) and traceLabel; the shot data itself is the session-global
> `shotModel` context property, filtered per-screen through this carousel's
> own ShotFilterProxyModel. Rating/note/trash go straight to the shotModel
> invokables; replay/export/face-on are the host screen's to wire.
> The rail is collapsible: the chevron at the right of the top band folds the card
> strip away and keeps the band (session/filter chips, action bar, transport), so the
> controls survive while the stage above gets the estate back. That choice is
> remembered per screen (Wrist separately from GRF) — the host supplies its
> sessionType; see _collapseKey.

- **Properties:** `var metricKeys`, `string traceLabel`, `Component transport`, `bool transportActive`, `int sessionType`, `bool expanded`, `int selectedShotId`, `Item selectedCard`, `readonly bool reviewing`, `readonly var activeModel`, `readonly real drawerWidth`
- **Uses:** `PpChip`, `PpExportOptionsSheet`, `PpMicro`, `PpPopoverCard`, `PpPressable`, `PpSessionDrawer`, `PpShotActionBar`, `PpShotCard`, `PpShotFilter`, `PpSwingEditPanel`, `PpToast`, `SessionMode`
- **Used by (2):** `ScreenSessionMode`, `ScreenWrist`

### PpShotFilter
`src/Gui/shot/PpShotFilter.qml` · 450 lines · root `Item` · 2 hard-coded colour(s)

> Filter popover content — traffic-light quality bands (same colours as the
> card score pills), an exact star-rating picker (only shots rated exactly N;
> tap the same star again to clear), and a has-video toggle. Pure bindings to
> the carousel's ShotFilterProxyModel Q_PROPERTYs; the filtering itself lives
> in C++.
> ...and, below the filters, a SHOT PICKER that appears only when the host's film
> strip is folded away (showShots). With the cards up they ARE the selector and this
> menu is just the filter, which is all it has ever been; collapsed, there is no other
> way to reach a swing, and re-opening the strip to pick one defeats the fold. The
> picker is a contact sheet of ordinals tinted by the same quality ramp as the card
> pills — one glance finds the good ones, one click puts a swing on the stage, one
> click on the chip already on the stage takes it off again. It lists the rows of the
> model the filters drive, so a filtered-out swing is absent here exactly as it is
> absent from the strip: filtering is honoured by construction, not by a second rule.
> Ordinals alone would not identify a swing, so the readout line under the grid names
> whatever the cursor is over (falling back to the swing on the stage).
> …

- **Properties:** `required var proxy`, `bool showShots`, `var shots`, `int focusedShotId`, `var focusedSummary`
- **Signals:** `shotToggled(int shotId, string swingDir)`
- **Functions:** `positionAtFocused()`
- **Uses:** `PpBadge`, `PpCardNote`, `PpLink`, `PpMicro`, `PpPressable`, `PpStarRating`
- **Used by (1):** `PpShotCarousel`
- **Exercised by tests/tools:** `tst_shot_picker`

### PpShotPipRow
`src/Gui/shot/PpShotPipRow.qml` · 120 lines · root `Item`

> A shot card's diagnostic read, at carousel size: one pip per tracked condition, plus the
> count of the ones that fired (design 13a, brief §6.1).
> SAME COLOUR RULE AS THE TICK RUN, AND THE SAME THIRD STATE. Fired fills in the error token,
> clean in the good one, and a condition this capture could not answer is drawn SHORTER AND
> OUTLINED rather than left out — the carousel is where a golfer decides which swing to open,
> and a row that silently dropped the unmeasured conditions would make two different shots
> look like the same read. The panel's PpTickRun makes this argument at the other end of the
> session; this is the same fact at 3 px.
> NOT PpPips. The coaching-card pips are sp(6) dots at sp(4) apart, which is a row of eight
> sessions; a swing's read is one pip per tracked condition, seventy-odd on a real ledger, in
> a card Theme.sp(139) wide. So the row keeps the tick run's own marks (a filled bar for a
> firing, a shorter outlined one for a condition the capture could not answer) and shares out
> the width instead, and the fired COUNT in words is the signal colour never has to carry.
> EXTRACTED FROM PpShotCard SO IT CAN BE PRESSED. The card requires a dozen model roles and a
> proxy row to exist at all; the pip row requires a list of states. Keeping it separate is
> what lets the offscreen QML test assert the third state is drawn rather than eyeball it,
> …

- **Properties:** `var pips`, `int firedCount`, `readonly int count`, `readonly color countColor`
- **Used by (1):** `PpShotCard`
- **Exercised by tests/tools:** `tst_session_diagnostics`

### PpSwingEditPanel
`src/Gui/shot/PpSwingEditPanel.qml` · 235 lines · root `Item`

> Swing-edit popover content — the focused shot's editable metadata: club,
> rating and note. Presentational (bindings only, no model lookups): the host
> (PpShotCarousel) injects `summary` (ShotListModel::shotSummary) and the
> `clubOptions` list, and wires the edit signals back to the active model's
> setClub / setRating / setNote. Replaces the club + rating + note fields of
> the retired PpShotPanel; video and metrics now live on the Review stage.

- **Properties:** `var summary`, `var clubOptions`
- **Signals:** `clubChosen(string club)`, `rated(int value)`, `noteChanged(string text)`, `closeRequested()`
- **Uses:** `ClubFormat`, `DataWarningFormat`, `PpBadge`, `PpComboBox`, `PpInset`, `PpLink`, `PpMicro`, `PpStarRating`, `PpTextField`, `PpTrustInlineFor`, `PpTypeset`, `TrustFocus`
- **Used by (1):** `PpShotCarousel`
- **Exercised by tests/tools:** `tst_shot_card`
