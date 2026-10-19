# Diagnostic Model editor

*Directory:* `src/Gui/diagnosticmodel/`  
*19 components · 9,488 lines of QML*

Settings > Reference > Diagnostic Model: a three-pane editor (type rail, table, relationship inspector) for the diagnostics model pack, with graph, corridor plot, ring menu and edit/unsaved tooling.

**C++ in this area:** `ModelBrowser`  (see [cpp-seam.md](../cpp-seam.md))

| Component | Lines | Root | Used by | Uses | Notes |
|---|---:|---|---:|---:|---|
| [DiagnosticModel](#diagnosticmodel) | 1994 | `Item` | 1 | 19 |  |
| [ModelBarButton](#modelbarbutton) | 105 | `Rectangle` | 1 | 1 |  |
| [ModelCausePicker](#modelcausepicker) | 343 | `Popup` | 2 | 2 | 1 local comp. |
| [ModelChipRow](#modelchiprow) | 94 | `ColumnLayout` | 1 | 1 |  |
| [ModelConfirm](#modelconfirm) | 159 | `Popup` | 1 | 1 |  |
| [ModelCorridorPlot](#modelcorridorplot) | 522 | `Item` | 1 | 2 | 2 local comp. |
| [ModelEdits](#modeledits) | 179 | `Item` | 1 | 1 |  |
| [ModelEnumField](#modelenumfield) | 148 | `Rectangle` | 1 | 1 |  |
| [ModelGraph](#modelgraph) | 2169 | `Item` | 1 | 3 | 3 local comp. |
| [ModelInspector](#modelinspector) | 1078 | `Item` | 1 | 5 |  |
| [ModelMint](#modelmint) | 212 | `Popup` | 1 | 3 |  |
| [ModelPicker](#modelpicker) | 170 | `Popup` | 1 | 2 |  |
| [ModelPolicyPicker](#modelpolicypicker) | 119 | `Popup` | 1 | 1 |  |
| [ModelTable](#modeltable) | 744 | `Item` | 1 | 1 |  |
| [ModelTools](#modeltools) | 237 | `Popup` | 1 | 1 | 2 local comp. |
| [ModelTrail](#modeltrail) | 114 | `Item` | 1 | 1 |  |
| [ModelTypeRail](#modeltyperail) | 355 | `Item` | 1 | 1 |  |
| [ModelUnsaved](#modelunsaved) | 121 | `Popup` | 1 | 2 |  |
| [RingMenu](#ringmenu) | 625 | `Popup` | 1 | 0 | 2 local comp. |

### DiagnosticModel
`src/Gui/diagnosticmodel/DiagnosticModel.qml` · 1994 lines · root `Item`

> Settings → Reference → Diagnostic Model. One shell, three panes: type rail → table → relationship
> inspector, with a trail of the chain you walked.
> It REPLACED an earlier Diagnostics panel, which is now deleted. That one read the pack as saved
> and could not show an unsaved edit, which is why this exists rather than growing out of it.
> The organising rule: EVERY element of the model is editable in the fewest possible clicks. No
> modals, no edit mode, no Edit button. A click selects a row; a click on a row that is already
> selected edits the cell under it — see ModelTable.qml for why that differs from the brief, which
> asked for both in one click. Everything that changes the library goes through ModelBrowser, which
> holds an unsaved working copy, so one Save writes the lot and ⌘Z steps back through all of it.

- **Functions:** `select(type, id, pushTrail)`, `selectFresh(type, id)`, `navigateTo(type, id)`, `openSubject(subject)`, `toggleFacet(key, value)`, `doCommit(id, field, value)`, `doSave()`, `doUndo()`, `doRedo()`, `doRevert()` (+10)
- **Uses:** `ModelBarButton`, `ModelCausePicker`, `ModelConfirm`, `ModelGraph`, `ModelInspector`, `ModelMint`, `ModelPicker`, `ModelPolicyPicker`, `ModelTable`, `ModelTools`, `ModelTrail`, `ModelTypeRail`, `ModelUnsaved`, `PpButton`, `PpDisplayText`, `PpPressable` (+3)
- **Used by (1):** `ScreenSettings`

### ModelBarButton
`src/Gui/diagnosticmodel/ModelBarButton.qml` · 105 lines · root `Rectangle`

> One control on the panel's two bars: a glyph, a word, or both, drawn quiet until hovered.
> PpButton is the app's button and stays the app's button — but it is a filled 34px control sized for
> a page's primary action, and the bar carries ten of them. Ten PpButtons in a row is the wall of
> equal-weight chrome ADDENDUM-02 exists to undo, so the bar's own items are drawn at bar weight and
> `Save` stays a PpButton precisely because it is the one that should not be.
> `implicitWidth` is always stated, because a RowLayout under pressure shrinks anything that does not
> state one to nothing — the lesson the Table/Graph control taught, generalised.

- **Properties:** `string glyph`, `string label`, `string hint`, `string tooltip`, `bool active`, `color tone`, `color fill`
- **Signals:** `clicked()`
- **Uses:** `PpPressable`
- **Used by (1):** `DiagnosticModel`

### ModelCausePicker
`src/Gui/diagnosticmodel/ModelCausePicker.qml` · 343 lines · root `Popup`

> "Which cause?" — asked with ONE text field that does both jobs: it narrows the conditions that
> already exist, and it names the one that does not.
> ── Why both halves are here rather than in two controls ────────────────────────────────────────
> The graph draws a NEIGHBOURHOOD, and a neighbourhood is drawn because its members are connected
> to the focus. So nearly every node on the canvas is already linked to it, and "already linked" is
> a refusal — which leaves a link drag with almost nothing legal to land on, and makes dragging to
> an on-screen node the RARE case rather than the common one. The cause an author actually wants is
> usually off the canvas entirely.
> A create-only popover at the end of that drag therefore answered the wrong question: it offered
> the one thing you can always do (make another condition) and withheld the one you usually want
> (name an existing one). The library filling with near-duplicates is the predictable consequence,
> and it is the failure mode that cannot be corrected after the fact because nobody merges them.
> So the list comes first and creation is the fallback, which is also the order of the two answers'
> likelihood. The candidates are PRE-FILTERED to legal ones in C++ — no self, no existing edge, no
> cycle — so an illegal claim cannot be constructed here, only refused before it is offered.

- **Properties:** `string title`, `var candidateSource`, `bool allowCreate`, `bool upstream`, `bool hasGhost`, `real ghostX`, `real ghostY`
- **Signals:** `picked(string id)`, `created(string objType, string name)`, `rejected()`
- **Functions:** `openAt(canvasX, canvasY, layoutX, layoutY)`
- **Local components:** `Chip`
- **Uses:** `PpPressable`, `PpTextField`
- **Used by (2):** `DiagnosticModel`, `ModelGraph`

### ModelChipRow
`src/Gui/diagnosticmodel/ModelChipRow.qml` · 94 lines · root `ColumnLayout`

> One labelled row of chips. The options come from C++ already gated by the validity table, so an
> empty row means "choose the one above first" rather than "nothing is possible" — and it says so,
> because a row of nothing with no explanation is indistinguishable from a rendering fault.

- **Properties:** `string label`, `var options`, `string chosen`
- **Signals:** `picked(string value)`
- **Uses:** `PpPressable`
- **Used by (1):** `ModelMint`

### ModelConfirm
`src/Gui/diagnosticmodel/ModelConfirm.qml` · 159 lines · root `Popup`

> The house confirmation prompt, as the session-active close interception in Main.qml draws it:
> modal, centred, dimmed, Esc cancels, display-font title, right-aligned outlined primary followed by
> a neutral Cancel. Written once here because this panel raises two of them and two hand-drawn copies
> of one standard drift apart.
> `tone` is the ONE thing the two callers differ on, and the difference is meant. Theme.colorAttention
> is defined as a call-to-action frame — "draws the eye to a row/control that needs the user to act" —
> and the close prompt wears it because it interrupts something live, not because it destroys
> anything. A prompt that discards saved work wears the error family instead, which is how the app
> already draws a destructive action: removing something is a write to the user's pack, and is
> styled as one. Two different events must not read alike.

- **Properties:** `string title`, `string body`, `string confirmText`, `string cancelText`, `string tone`
- **Signals:** `confirmed()`, `cancelled()`
- **Uses:** `PpPressable`
- **Used by (1):** `DiagnosticModel`

### ModelCorridorPlot
`src/Gui/diagnosticmodel/ModelCorridorPlot.qml` · 522 lines · root `Item` · 1 hard-coded colour(s)

> The corridor, drawn: the distribution the norm CLAIMS, over the readings the library actually
> holds.
> Every coordinate comes from ModelBrowser.corridorPlot() — which is corridor_plot.h — so this file
> draws and drags and decides nothing. The curve is split-normal because the tolerances are
> asymmetric by design; the bands are the bands that grade, monitor-band precedence included.
> ── Why the data is drawn the way it is ────────────────────────────────────────────────────────
> The old editor put a histogram behind the corridor. A histogram's bar edges are an artefact of
> the binning rather than of the swings, and at any useful bin count it dominates the picture —
> which is backwards, because the subject here is the CORRIDOR and the readings are evidence about
> it.
> So there is ONE curve — the one being set — and the readings appear on and beneath it:
> * dots sitting on the curve, each at its own value, lifted to the density the norm gives it
> * a rug of hairlines on the baseline: every reading, unbinned and uninterpreted
> Where the dots gather along the curve is the answer. A corridor centred where the swings are
> collects them around the peak; one centred elsewhere strands them along the floor of a tail.

- **Properties:** `var plot`, `bool editable`, `var plotSource`
- **Signals:** `handleCommitted(string handle, real value)`, `fieldCommitted(string field, string text)`, `scanRequested()`
- **Local components:** `Handle`, `Field`
- **Uses:** `PpPressable`, `PpTextField`
- **Used by (1):** `ModelInspector`

### ModelEdits
`src/Gui/diagnosticmodel/ModelEdits.qml` · 179 lines · root `Item`

> The Edits history. Every mutation in the session, newest at the bottom, and clicking any row
> winds the whole model to that point — which is what makes the stack navigable rather than a
> counter with two buttons.
> Undone entries stay in the list, greyed. A history that deleted what you stepped back past would
> make redo undiscoverable, and "nothing in this panel may be unrecoverable" includes the
> recoverability of the recovery.

- **Properties:** `var edits`, `bool sessionScoped`
- **Signals:** `windTo(int index)`, `closeRequested()`
- **Uses:** `PpPressable`
- **Used by (1):** `ModelUnsaved`

### ModelEnumField
`src/Gui/diagnosticmodel/ModelEnumField.qml` · 148 lines · root `Rectangle`

> A one-of-many field: the value, in a box that looks like every other field, opened by clicking it.
> It replaces a PpSegmentedControl, which is the right control for two or three choices on a wide
> bar and the wrong one in a 480px inspector: Evidence has SIX tiers, and six segments in that width
> rendered as a row of overlapping, unreadable pills.
> The rule this restores is worth stating, because the segmented control broke it: every field on a
> details pane looks the same and is changed the same way — you click it — whatever kind it holds.
> A reader should not have to learn a different gesture per field type, and a control that changes
> shape with the number of options is one that renders differently on every object.
> One click opens, one click picks. That is the same two-click budget the table's enum cells keep.

- **Properties:** `var options`, `var value`, `readonly bool opened`, `readonly string currentLabel`
- **Signals:** `picked(var value)`
- **Uses:** `PpPressable`
- **Used by (1):** `ModelInspector`

### ModelGraph
`src/Gui/diagnosticmodel/ModelGraph.qml` · 2169 lines · root `Item` · 1 hard-coded colour(s)

> The graph pane. It SWAPS the middle pane and keeps the rail and the inspector, so switching views
> keeps your place — selection is shared with the table.
> EVERY coordinate comes from ModelBrowser.graph(), which is dag_layout.h. QML positions nothing:
> rank assignment, ordering, routing and overlap are the only things about this surface that can be
> tested, and a layout computed inside delegate bindings is a layout nothing can assert. What this
> file does is draw, hit-test and drag.
> ── Editing: the gesture is the permission ──────────────────────────────────────────────────────
> There is NO editing mode and no editing toggle. A structural edit begins with a deliberate
> press-and-hold on the thing you mean, which opens the ring (RingMenu.qml). A mode would make
> permission a place you are standing rather than something you did: it has to be found, remembered
> and turned off, it is invisible at the moment of the accident it exists to prevent, and it forces
> affordance chrome onto every node for ever to advertise a state. 350 ms of pressure is a stronger
> consent signal than a switch flipped twenty minutes ago, and it cannot be left on.
> The price is that structural editing has no resting affordance here at all. It is paid three ways,
> and all three are load-bearing rather than nice: right-click opens the SAME ring (every desktop
> author tries it); the first hold of a session holds its labels a beat longer; and the inspector
> …

- **Properties:** `var layoutData`, `string focusId`, `string selectedEdgeId`, `bool editable`, `var selectedNodeIds`, `var selectedEdgeIds`, `var refusalsProbe`, `var ringValuesProbe`, `var causeCandidatesProbe`, `int totalConditions`, `int scope`, `bool includeMeasures`, `bool includeReferences`, `bool includeScreened` … (+2 more)
- **Signals:** `nodeActivated(string nodeType, string id)`, `edgeActivated(string rowId)`, `linkRequested(string fromId, string toId)`, `repointRequested(string edgeId, string end, string newId)`, `createLinkedRequested(string objType, string name, string otherId, string end)`, `selectionRequested(var nodeIds, var edgeIds)`, `verbInvoked(string verb, var arg)`, `scopeRequested(int scope)`, `switchToggled(string which)`, `expandToggled(string id)`, `collapseAllRequested()`
- **Functions:** `armRepoint(edgeId, end)`
- **Local components:** `Opener`, `Switch`, `Step`
- **Uses:** `ModelCausePicker`, `PpPressable`, `RingMenu`
- **Used by (1):** `DiagnosticModel`
- **Exercised by tests/tools:** `tst_graph_measures`, `tst_graph_opener`, `tst_graph_pan`, `tst_graph_references`, `tst_graph_scope`

### ModelInspector
`src/Gui/diagnosticmodel/ModelInspector.qml` · 1078 lines · root `Item`

> The inspector — a RELATIONSHIP HUB, not a property sheet.
> Its job is that every related object is one click away. That is the whole complaint the panel
> exists to answer: a metric, the measure that reads it, the corridor that judges it and the
> characteristic that fires are one chain, and following it used to mean leaving the page. Every row
> in here is navigable, and navigating pushes onto the trail rather than replacing where you were.
> It is always live: there is no read-only state and no Edit button, because selecting a row IS
> opening its editor.

- **Properties:** `var detail`, `var corridorPlotSource`, `bool editable`, `point actionOrigin`
- **Signals:** `navigate(string type, string id)`, `addCauseRequested()`, `addMeasureRequested()`, `addCorridorRequested()`, `addRowRequested(string action)`, `removeRowRequested(string type, string id)`, `rowActionRequested(string action, string id)`, `claimStrengthChanged(string linkId, string strength)`, `collapseRequested()`, `fieldCommitted(string field, var value)`, `duplicateRequested()`, `removeRequested()`, `bindingCycled(string contextId, bool applicable, bool material, bool clear)`, `corridorHandleCommitted(string handle, real value)`, `corridorFieldCommitted(string field, string text)`, `corridorScanRequested()`
- **Uses:** `ModelCorridorPlot`, `ModelEnumField`, `PpMetricSwatches`, `PpPressable`, `PpTextField`
- **Used by (1):** `DiagnosticModel`
- **Exercised by tests/tools:** `tst_inspector_citation`

### ModelMint
`src/Gui/diagnosticmodel/ModelMint.qml` · 212 lines · root `Popup`

> Minting a measure from facets.
> A measure is a SERIES (what · quantity · relative to what) sampled by a REDUCER (at which phase),
> and it is built by tapping chips rather than typing a name — the name is generated from the
> facets, deterministically, so two authors describing one measure cannot produce two of them.
> Every chip row is gated by the validity table in C++, so the picker never offers something the
> loader will then reject. And the near-duplicate check fires HERE, at creation, because after the
> fact nobody merges them — that is the whole defence against a library filling with almost
> identical measures inside a month.

- **Properties:** `var browser`
- **Signals:** `minted(string id)`
- **Uses:** `ModelChipRow`, `PpButton`, `PpTextField`
- **Used by (1):** `DiagnosticModel`

### ModelPicker
`src/Gui/diagnosticmodel/ModelPicker.qml` · 170 lines · root `Popup`

> Type-ahead for every link. Click, type three characters, Enter — never a picker dialog and never
> a scroll through 140 rows.
> The candidate list is PRE-FILTERED to legal targets in C++ (acyclicity, axis and
> corroborates-shadowing rules), so an illegal edit cannot be constructed. A control that offers a
> choice it will then reject is a worse control than one that does not offer it, and it also teaches
> the author that the rules are arbitrary.

- **Properties:** `var candidateSource`, `string title`
- **Signals:** `picked(string id)`
- **Uses:** `PpPressable`, `PpTextField`
- **Used by (1):** `DiagnosticModel`

### ModelPolicyPicker
`src/Gui/diagnosticmodel/ModelPolicyPicker.qml` · 119 lines · root `Popup`

> How the library is GRADED, hung off the bar's live readout rather than buried in a drawer.
> Promoted out of ModelTools because it is not an artefact and not a view — it is the state every
> corridor on screen is drawn under, and the answer to "why is this corridor red" should not be
> behind a button called Tools. It is still not an edit to the library, so it still does not touch
> the undo stack: the readout writes straight to the one global AppSettings.

- **Properties:** `var browser`, `string gradePolicy`
- **Signals:** `picked(string name)`
- **Uses:** `PpPressable`
- **Used by (1):** `DiagnosticModel`

### ModelTable
`src/Gui/diagnosticmodel/ModelTable.qml` · 744 lines · root `Item`

> The table. ONE delegate for every content type: the shell does not change, only the columns do,
> and both the columns and the cells inside them are handed over by ModelBrowser. Nothing here
> knows what a Measure is.
> Editing lives here rather than in a sheet: there is no edit mode, no Edit button and no modal. A
> cell whose value is scalar edits in place — text inline, enums as a one-click list — and the list
> stays on screen through every edit.
> The one departure from the brief is WHEN an editor opens. The brief says one click should select
> the row and open the editor together; in use that meant clicking a name to read the inspector
> dropped a caret into it, so navigating paid for editing. So: a click selects, and a click on a
> row that is ALREADY selected edits. See the handler at the foot of the cell delegate for the
> full reasoning and what it costs.

- **Properties:** `var columns`, `var rows`, `string selectedId`, `var selection`, `string sortKey`, `bool descending`, `bool editable`, `readonly int rowHeight`, `readonly bool editing`
- **Signals:** `activated(string id)`, `commit(string id, string field, var value)`, `duplicateRequested(string id)`, `removeRequested(string id)`, `sortRequested(string key)`, `graphRequested(string id)`
- **Functions:** `indexOfId(id)`, `selectRow(index)`, `beginEdit(rowId, col)`, `endEdit()`, `editNext(rowId, col, step)`
- **Uses:** `PpPressable`
- **Used by (1):** `DiagnosticModel`

### ModelTools
`src/Gui/diagnosticmodel/ModelTools.qml` · 237 lines · root `Popup`

> The `⋯` drawer: the things that are not content and are not the draft — two saved views, two
> exports, the norm-set inventory, and the way back to the shipped model.
> This was `Tools`, and Tools was a grab-bag whose own header comment described "the things that are
> not content" as if that were one thing. It is two. The grade policy left for a live readout on the
> bar (ModelPolicyPicker.qml) because it is state you need to SEE, not an action you go and find;
> what stayed is a junk drawer, which is the right shape for the rest of it.
> Nothing in here is on the undo stack except the reset, which is a command like every other write.

- **Properties:** `var browser`, `int revision`, `bool hasLocalContent`, `int changedCount`, `int yoursCount`
- **Signals:** `exportRoadmapRequested()`, `exportReferencesRequested()`, `roadmapRequested()`, `glossaryRequested()`, `resetRequested()`
- **Local components:** `Heading`, `Action`
- **Uses:** `PpPressable`
- **Used by (1):** `DiagnosticModel`

### ModelTrail
`src/Gui/diagnosticmodel/ModelTrail.qml` · 114 lines · root `Item`

> The trail: the chain you actually walked to get here. Its terminal item is ALWAYS the current
> selection, which is what makes it a trail rather than a history — a metric, the measure that
> reads it, the corridor that judges it, the characteristic that fires, read left to right.
> Clicking a step goes back to it and truncates everything after, so the trail never carries a
> future you have left.
> It is reset, not extended, whenever the author picks a row out of the table — see selectFresh() in
> DiagnosticModel.qml. Only a followed RELATIONSHIP is a step; choosing where to stand is the start of
> a different chain, and a breadcrumb that kept the old one would be describing a route nobody took.
> It is also the middle pane's HEADING (ADDENDUM-02, A2). It used to be a pill on the global toolbar
> labelled TRAIL, one band above a separate row that named the type — a breadcrumb and a pane title
> saying overlapping things in two places. Drawn as a heading, the terminal item IS the title, and
> with nothing walked yet it degrades to `fallbackLabel`, which is exactly what that second row said.
> So the pill, the eyebrow and the band all go, and nothing is lost.

- **Properties:** `var trail`, `string fallbackLabel`
- **Signals:** `stepPicked(string type, string id)`
- **Uses:** `PpPressable`
- **Used by (1):** `DiagnosticModel`

### ModelTypeRail
`src/Gui/diagnosticmodel/ModelTypeRail.qml` · 355 lines · root `Item`

> The type rail: every content type with its live count, and the selected type's facets beneath.
> Counts come from the façade, which derives them by asking the row list — never a stored figure.

- **Properties:** `var types`, `var facets`, `var activeFacets`, `string selectedType`, `int totalObjects`, `bool facetsFolded`
- **Signals:** `typePicked(string key)`, `facetToggled(string key, string value)`, `facetsCleared()`, `facetsFoldToggled()`, `collapseRequested()`
- **Uses:** `PpPressable`
- **Used by (1):** `DiagnosticModel`
- **Exercised by tests/tools:** `tst_facet_fold`

### ModelUnsaved
`src/Gui/diagnosticmodel/ModelUnsaved.qml` · 121 lines · root `Popup`

> What Save would write, hung under the `n unsaved ▾` readout that counts it.
> The history used to take the inspector's slot behind a button called `Edits`, three controls away
> from the Save it describes, while `revert` was a 12px word in the status bar nowhere near either.
> One cluster now: the count, the history it counts, and the discard that throws it away.
> ModelEdits comes across whole rather than being re-drawn here — including the sentence stating that
> the history is session-scoped, which is the fact this popover most needs and least wants a second
> wording of.

- **Properties:** `var edits`, `bool sessionScoped`
- **Signals:** `windTo(int index)`, `revertAll()`
- **Uses:** `ModelEdits`, `PpPressable`
- **Used by (1):** `DiagnosticModel`

### RingMenu
`src/Gui/diagnosticmodel/RingMenu.qml` · 625 lines · root `Popup` · 1 hard-coded colour(s)

> The press-and-hold ring. ONE component for every selection type — a node, a link, a multi-select
> and bare canvas differ only by the array passed in, never by a subclass, because the whole promise
> of the shape is that the silhouette is identical every time it opens.
> ── The eight slots are fixed by MEANING ────────────────────────────────────────────────────────
> Clockwise from north the ring reads look · make · set · unmake, and a direction means the same
> thing whatever is selected. `model` is therefore EXACTLY EIGHT entries, index 0 = N and rising
> clockwise, with `null` for a slot this selection does not fill. A gap is a hairline, never a
> greyed-out button and never a live spoke moved up to close it: muscle memory has to survive a
> change of selection, so the only variable between one opening and the next is how many wedges are
> lit. `enabled: false` is treated as a gap for the same reason — there is no disabled state here.
> Each entry: { icon, label, hint, kind, enabled, danger, values[] }
> kind    "action"  commits on release
> "drag"    commits by handing off to a drag with the button still down (§5)
> "value"   carries `▸` and opens the collar
> hint    the spoken label — what the hub says while this spoke is hot, and the ONLY place the
> ring explains itself. It is why the wedge itself can stay icon-plus-short-label.
> …

- **Properties:** `var model`, `string hubTitle`, `string hubMeta`, `bool latched`, `bool firstOfSession`, `real centreX`, `real centreY`
- **Signals:** `committed(int slot, var entry, string value)`, `cancelled()`
- **Functions:** `openAt(px, py, entries, title, meta, isLatched)`, `track(px, py)`, `release()`
- **Local components:** `Wedge`, `Cell`
- **Used by (1):** `ModelGraph`
- **Exercised by tests/tools:** `tst_ring_collar`
