# Tendencies flow diagram (Sankey) — design & implementation

Status: design, ready to implement
Owner: Mark
Target: Tendencies page (Home → Tendencies), plus printed reports
Visual reference: "Tendencies visual concepts" canvas, board C ("Where your drives leak")

## 1. Purpose

The Tendencies page classifies every shot on three outcome families:

| Family    | Values (current vocabulary)                                   | "Good" value |
|-----------|---------------------------------------------------------------|--------------|
| Strike    | Solid, Thin, Fat, Heel, Toe (+ Shank if present)              | Solid        |
| Direction | Straight, Push, Slice, Push-slice, Pull, Hook, Pull-hook (+ any others in the outcome vocabulary) | Straight |
| Flight    | High, Mid, Low                                                | High (see §11) |

The flow diagram answers one question at a glance:

> How often do I get solid **and** straight **and** high, and where do the other shots leak out?

Shots flow left to right: **Strike → Direction → Flight**. The ideal path (Solid → Straight → High) is a green ribbon. Every other ribbon is coloured by the **first family at which the shot left the ideal path**. This makes knock-on patterns visible, for example "thin → low", or "heel → slice → mid".

The diagram must follow the Tendencies page filters: a specific club, a shot type, all clubs, a date range or sessions. It must render identically in the app and in printed or PDF reports.

## 2. Before you start (Claude Code)

1. Locate the existing Tendencies page, its filter state and its data source: the auto-classified shot-outcome history. Read them before writing anything.
2. Take the outcome vocabulary for each family from the existing shot-outcome classification and diagnostics model. **Do not hard-code value lists.** The tables above are illustrative.
3. Match the existing conventions:
   - `Pp*` QML components and `Theme.qml` tokens.
   - The abstract base class and factory pattern where a renderer abstraction is needed.
   - Producers and the `MetricCatalogue` where the codebase routes derived data that way.
4. Qt Graphs has no Sankey. Draw it with `QtQuick.Shapes` in the app, and with `QPainter` for print. Do not use Qt WebEngine or any web view (mobile constraint).
5. If what you find contradicts this doc (names, where filters live, how shots are classified), follow the code and note the deviation in your summary.

Names below (`TendencyFlowModel`, `PpTendencyFlow`, etc.) are proposals. Rename them to fit the existing structure.

## 3. Behaviour

### 3.1 Inputs
- The **filtered shot set** produced by the Tendencies page's current filters. The diagram never applies its own filtering.
- Each shot carries `strike`, `direction` and `flight` values, or null where that family could not be classified.

### 3.2 Unclassified shots
- A shot missing **any** family is excluded from the flow.
- The excluded count is shown under the diagram, e.g. "12 shots without flight data not shown".
- PinPoint never guesses an outcome. This is consistent with the principle that PinPoint never second-guesses the golfer's intent.

### 3.3 Minimum sample
- Below `kMinShots` (default 10, a named constant), show an empty state instead of the diagram: "Hit at least 10 classified shots with this club to see your tendencies."
- Above it, always draw. Small samples are honest; do not hide them. The shot count in the header carries the caveat.

### 3.4 Filter changes
- Recompute on any filter change.
- Cross-fade the old and new diagram (`--pp-duration-normal`, 220 ms; 0 under reduced motion).
- Never animate ribbon geometry, in line with the design-system motion rule.

### 3.5 Header copy (computed)
- Micro label: `TENDENCIES · <filter summary> · <n> shots` (e.g. `TENDENCIES · DRIVER · 80 SHOTS`, `ALL CLUBS · LAST 30 DAYS`).
- Headline: "<p>% of your <club noun> were solid, straight and high". Use "shots" for all clubs and "drives" for driver; take club nouns from the existing club formatting, not a new table.
- Subtitle (static): "Read left to right: strike, then direction, then flight. Green is the path you want. Every other colour shows where a shot first left it."
- Do not add causal language ("because") here. Cause is the diagnostics layer's job.

### 3.6 Legend
Four entries. Each shows a swatch, a name and "n · p%":

| Group | Rule (first failure)                       | Name                       |
|-------|--------------------------------------------|----------------------------|
| G0    | strike good, direction good, flight good   | Solid, straight and high   |
| G3    | strike good, direction good, flight not    | On line, flight off        |
| G2    | strike good, direction not                 | Struck well, off line      |
| G1    | strike not good                            | Mis-struck                 |

Generate the names from the active "good" values (§11) so they stay correct if the flight target changes.

### 3.7 Interaction (app only)
- **Hover or press on a node**: highlight every ribbon through that node, including upstream and downstream segments of the same shots, and dim the rest to 25%. A tooltip gives the node total, plus a breakdown by group.
- **Hover on a ribbon**: highlight it and show "<from> → <to>: n shots (p%)".
- **Tap on a node (v2, optional)**: emit `nodeActivated(family, value)` so the page can filter the shot list or jump into diagnostics ("what is your bad shot?" entry point). Expose the signal in v1, but leave it unwired.
- Keyboard: nodes are focusable in column order. Enter does the same as tap; focus does the same as hover.

## 4. Layout algorithm

Put this in pure C++ (no Qt Quick dependency) so it can be unit-tested and shared by the app and print renderers.

### 4.1 Aggregate
1. Count shots per full combo `(s, d, f)`. Assign each combo its group `g` using the first-failure rule.
2. Column counts: `count[col][value]`.
3. **Fold small values**:
   - Within each column, a value with fewer than `max(2, 3% of total)` shots merges into a single "Other" node, placed last in its column.
   - The good value is never folded.
   - Only fold when two or more values qualify. Never produce an "Other" node holding a single value.
   - Folding happens before link aggregation.

### 4.2 Node order (top to bottom)
- Strike: good value first, then the vocabulary order of the remaining values.
- Direction: good value (Straight) first, then right-side misses, then left-side misses. The vocabulary must say which side each value is on. If it doesn't, add that attribute to the outcome vocabulary rather than hard-coding it here.
- Flight: High, Mid, Low (good first, then descending).
- Nodes with zero shots are omitted.

### 4.3 Geometry (in logical units; renderers scale)
Let:
- `H` = plot height
- `gap` = 12 px × `Theme.sp`
- `nMax` = the largest number of nodes in any column
- `k = (H − gap × (nMax − 1)) / total`, the pixels per shot, identical across all columns

Then:
- Each column's stack is vertically centred in `H`.
- Node height is `count × k`. Node width is 12 px.
- Column x positions are at 0, 50% and 100% of the plot width, minus the label gutters. Strike labels sit to the left, flight labels to the right, and direction labels sit on a chip right of the node.

### 4.4 Links
There are two link sets: Strike→Direction and Direction→Flight. Each is aggregated by `(from, to, g)`.

- **Outgoing order at the source node**: sort by (target node order, group order). Group order is G0, G3, G2, G1, which keeps green on top.
- **Incoming order at the target node**: sort by (source node order, group order).
- Assign each link its `y0` (source) and `y1` (target) by walking those orders with per-node cursors.
- Ribbon path, with `x0` = source right edge, `x1` = target left edge, `xm = (x0+x1)/2`, `h = n·k`:
  ```
  M x0 y0  C xm y0, xm y1, x1 y1
  L x1 y1+h  C xm y1+h, xm y0+h, x0 y0+h  Z
  ```
- Draw order: G1, G2, G3, then G0 last, so the green path sits on top.

### 4.5 Output structure (proposal)
```cpp
struct FlowNode   { int column; QString value; bool good; bool other;
                    int count; qreal y, h; };
struct FlowLink   { int fromNode, toNode; Group group; int count;
                    qreal y0, y1, h; };
struct FlowGroupTotal { Group group; int count; };
struct FlowLayout { QVector<FlowNode> nodes; QVector<FlowLink> links;
                    QVector<FlowGroupTotal> groups; int total; int excluded;
                    qreal k; };
FlowLayout layoutTendencyFlow(const QVector<ShotOutcome>&, const FlowOptions&);
```
`FlowOptions` carries: `plotHeight`, `gap`, `nodeWidth`, `foldThresholdPct`, `foldMinShots`, and the good value per family.

## 5. Components

| Piece | Responsibility |
|-------|----------------|
| `TendencyFlowLayout` (C++, pure) | §4. No Qt Quick. Unit-tested. |
| `TendencyFlowModel` (C++ QObject, QML-exposed) | Watches the Tendencies filter and shot source. Runs the layout. Exposes `nodes`, `links`, `groups`, `total`, `excluded`, `idealPct` and `headline` as properties or list models. Exposes the `nodeActivated` signal. |
| `PpTendencyFlow.qml` | Draws with `Shape`, one `ShapePath` per link, `PathCubic` pairs, plus `Rectangle` nodes and `Text` labels. Handles hover, focus and tooltip. Pure presentation: no computation in QML. |
| `PpTendencyFlowLegend.qml` | The legend row (§3.6). Reusable in print. |
| `TendencyFlowPainter` (C++) | Renders a `FlowLayout` to a `QPainter` for PDF and print reports. Uses the same layout output as the app, so the two match by construction. |

Hook the painter into whatever report or print pipeline already exists (the Sessions screen has make/send report actions). If no shared pipeline exists yet, add the painter with a single `paint(QPainter*, QRectF, const FlowLayout&, const PrintStyle&)` entry point and leave report integration as a follow-up.

## 6. Visual spec (theme tokens)

| Element | Token / value |
|---------|---------------|
| Background | `colorSurface` |
| G0 ribbon | `colorGood`, alpha 0.62 |
| G3 ribbon | `gradientCool`, alpha 0.38 (see §11 — confirm token) |
| G2 ribbon | `colorAttention`, alpha 0.34 |
| G1 ribbon | `colorWarn`, alpha 0.34 (the coral; never `colorError`) |
| Ribbon edge | 0.6 px stroke in `colorSurface` (separates adjacent ribbons) |
| Nodes | `colorText`; the good node in each column `colorGood` |
| Node label | body style, 14 px; the good value in `colorGood` |
| Node sub-label | data style (mono) 11 px, `colorText2`: "n · p%" |
| Direction label chip | `colorSurface` at 86% behind the label |
| Column headers | micro style (mono, 10 px, tracked 0.8, caps): STRIKE / DIRECTION / FLIGHT |
| Headline | display style for the active aesthetic |

Further rules:
- Use only theme tokens: no hard-coded colours. The diagram must work in all aesthetics and in light and dark.
- Colour is never the only channel. Every node is labelled, and the legend gives counts.

### 6.1 Print
- Size: A4 landscape. Body text ≥ 12 pt and labels ≥ 9 pt.
- Always render in a light theme (Instrument Light), whatever theme the app is using.
- Greyscale safety, needed because reports may be printed in mono:
  - G0 is drawn solid.
  - G3 gets a fine dotted pattern.
  - G2 gets 45° hatching.
  - G1 gets crosshatch.
  - The patterns are drawn over the alpha fill, and the legend shows the same patterns.
  - This applies to `TendencyFlowPainter` only. The app relies on colour plus labels.
- Footer: filter summary, date range, the shot count and the excluded count.

## 7. Placement on the Tendencies page
- The diagram is the primary visual for the current filter selection, above any lists of individual tendencies.
- It fills the content width and scales its height with width: aspect ratio about 1.85:1, with a minimum plot height of 360 px × `Theme.sp`.
- Below 640 px width (mobile), the layout still works in three columns. Hide the node sub-labels (counts appear in the tooltip and on tap instead), and set direction labels below the chip.

## 8. Tests

**Unit tests (layout)**
- Conservation: for every direction node, the sum of incoming links equals the sum of outgoing links, which equals the node count. The sum of all node counts in a column equals `total`.
- Determinism: the same input gives identical output whatever the input order.
- Folding: values below the threshold merge into "Other". The good value is never folded. A single small value is not folded.
- Edge cases:
  - Empty input gives the empty state.
  - All shots ideal gives one node per column and only G0 links.
  - No shots ideal gives no G0 links.
  - A single shot works.
  - A shot with a null family is counted in `excluded`.
- Geometry: no node overlaps within a column, and every node lies within `H`.
- Group assignment: the first-failure rule, tested with the good values swapped (e.g. flight good = Mid).

**Golden test**
- Use the 80-shot sample dataset in the appendix.
- Assert the group totals: G0 22, G3 6, G2 23, G1 29. Assert `idealPct` = 28.

**Visual check**
- Render `TendencyFlowPainter` to PNG for the sample and attach it to the PR. Do this in Instrument Light and one dark theme.

## 9. Acceptance criteria
1. Changing the club, shot-type or date filter on the Tendencies page updates the diagram, headline and legend.
2. The "All clubs" selection works.
3. The ideal path is green and on top. Every non-ideal shot appears in exactly one of G1, G2 or G3.
4. Unclassified shots are excluded and counted on screen.
5. The app rendering and the PDF rendering come from the same `FlowLayout`.
6. No hard-coded colours, and correct rendering in every aesthetic, light and dark.
7. Reduced motion disables the cross-fade.
8. Layout unit tests and the golden test pass.

## 10. Phasing
1. `TendencyFlowLayout` and its tests.
2. `TendencyFlowModel` bound to the Tendencies filters, plus `PpTendencyFlow` and the legend. Static first, then hover and focus.
3. `TendencyFlowPainter` and the print pattern fills. Integrate with the report pipeline if one exists.
4. (v2) `nodeActivated` drill-down into the shot list and diagnostics.

## 11. Open questions (decide before or during step 1)
1. **Flight target.** Is "High" always the good flight, or should it be per club or shot type (e.g. Mid for a punch or a partial wedge)? Recommendation: take the good value per family from the club/shot-type context, defaulting to High. Make it a `FlowOptions` field from day one.
2. **Token for G3.** The design system reserves `gradientCool` for "a relation (neither praise nor fault)". Confirm it's acceptable for "on line, flight off", or add a dedicated chart token.
3. **Direction sides.** Does the outcome vocabulary already carry left/right side for direction values? If not, add it there.
4. **Shank and other rare outcomes.** Confirm they are part of the strike vocabulary. Folding (§4.1) keeps them from cluttering the diagram.

## Appendix — sample dataset (golden test, driver, 80 shots)

| Strike | Direction | Flight | n |
|---|---|---|---|
| Solid | Straight | High | 22 |
| Solid | Straight | Mid | 6 |
| Solid | Slice | High | 8 |
| Solid | Slice | Mid | 4 |
| Solid | Push | High | 3 |
| Solid | Push | Mid | 2 |
| Solid | Pull | Mid | 2 |
| Solid | Push-slice | High | 3 |
| Solid | Pull-hook | Low | 1 |
| Thin | Straight | Low | 4 |
| Thin | Slice | Low | 5 |
| Thin | Slice | Mid | 2 |
| Thin | Pull | Low | 3 |
| Thin | Pull-hook | Low | 2 |
| Heel | Slice | Mid | 3 |
| Heel | Push-slice | Mid | 3 |
| Heel | Straight | Mid | 1 |
| Toe | Hook | Mid | 2 |
| Toe | Pull-hook | Low | 1 |
| Fat | Straight | Low | 1 |
| Fat | Pull | Low | 1 |
| Fat | Pull-hook | Low | 1 |

Expected totals:
- Strike: Solid 51, Thin 16, Heel 7, Toe 3, Fat 3.
- Flight: High 36, Mid 25, Low 19.
- Groups: G0 22, G3 6, G2 23, G1 29.
- With fold threshold `max(2, 3% of 80 = 2.4)`, values with fewer than 2.4 shots fold. In strike, Toe (3) and Fat (3) do not fold. In direction, Hook (2) is the only qualifier, so no fold occurs (single-value rule).

## As built (10 November 2026)

Phases 1 and 2 (§10) are built: the layout and its tests, and the flow beside the rows on the Tendencies tab. Decided with Mark, or followed from the code, where this doc left a choice or the code disagreed:

- **The good path (§11.1)** is each family's values that are neither a fault nor one to watch, read from the shot-factor value table (`shotFactorGoodValues`): Solid; Straight, Fade or Draw (shapes, never a miss); Mid or High (Low is amber). The ideal is named from them: "Solid, on line, mid or high". The doc's "High only, Straight only" is a `FlowOptions` choice the golden test makes, and its totals hold (22 / 6 / 23 / 29, 28 %).
- **Colours (§6, §11.2)** follow the rows' scheme: ideal green; mis-struck red; struck well but off line red at a lighter strength; on line but flight off amber (the watch tone). Each node and its label wears its value's tone: good green, Low amber, a fault red, a fold of several in ink.
- **Direction sides (§11.3)** were not in the vocabulary; `ShotFactorValueInfo::side` now says "right" or "left". **Shank (§11.4)** is in the strike vocabulary and folds like any small value.
- **Where it sits (§7):** beside the rows, at 36 % of the tab's width, from `sp(1040)`; narrower, it stacks above the table. The Usual distance column is a fixed `sp(104)` so the distance is never cut short beside it.
- **Filters (§3.1):** the Tendencies tab has the Club picker only, groups only; the flow follows the same group as the rows (All clubs or one group) and pools full and part swings. There is no shot-type or date filter on that tab to follow.
- **Data:** each Tendencies row now keeps its shots as one value per family, the value its bars count each shot under (`TendencyItem::flow`, tendency rule 17); `WorkOnsController::tendencyFlow(group, plotHeight, gap)` pools the group's rows and runs `layoutTendencyFlow` (`src/Coaching/tendency_flow.h`) at the drawn size. That is the doc's `TendencyFlowModel`, as one call on the controller the tab already reads.
- **Headline** is in the heading style, not display: the design system allows one display headline a screen, and Home's is the hero.
- **Interaction (§3.7):** no tooltip, and nothing laid over the plot. Examining a node or ribbon lights its shots' ribbons and the node labels already there say how many of the examined shots pass through each node and what share ("2 · 67%"); nodes none reach dim. The first build put readouts on the ribbons, and Mark found they covered the plot (10 Nov 2026). `nodeActivated` is exposed and unwired.
- **Motion (§3.4):** the new flow fades in; the old one is not kept to cross-fade from.
- **The component** is `PpSankey` (src/Gui/components; its page is docs/design/system/components/PpSankey.md): generic, drawing whatever placed nodes and toned ribbons it is given. `HmTendencyFlow` is the Tendencies card around it.
- **Print (phase 3), built the same day:** the report's Your tendencies page draws the flow under the table, for every club on the page as of the report's session, through the same PpSankey with the page's print roles, not a separate QPainter painter. The report's pages are QML and saved as a raster PDF, so this keeps print and screen one drawing (§9.5 holds: both read the one map, `tendencyFlowMap`, src/Coaching/tendency_flow_map.h). Departures: the page is the report's A4 portrait, not landscape; the theme is Folio light, as every report page, not Instrument Light; the patterns are vector strokes inside each ribbon (ideal plain, flight off dotted, off line hatched, mis-struck cross-hatched; the legend the same), because a texture fill draws nothing in the offscreen grab the report is made from. The flow is laid out once at 300 units and scaled to the page. The footer is the page's (golfer, session, page); the flow's own lines say the clubs, the shot count and how many were not shown.
- **Column order and colour (Mark, 11 November 2026):** strike → flight → direction was tried and put back: the doc's strike → direction → flight stays. A tangled direction column is a finding in itself ("I am inconsistent … the confusing chart is an indicator too"). What stayed from the trial: the colour never follows the columns. A shot's group is the first family off the good path in a fixed precedence, strike, then direction, then flight ("the coloring is important — a slice needs to be red"), and both orders are `FlowOptions` fields (`columns`, `precedence`), with a test that a solid low slice stays red if the columns ever move.
