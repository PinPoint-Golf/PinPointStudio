# PpSankey

A Sankey: things flowing left to right through three columns of nodes, each flow a ribbon as thick as its count, the nodes named and counted, a legend under it, and the ribbons examined in place.

Source: `src/Gui/components/PpSankey.qml`. Added 10 Nov 2026 for the tendency flow ([tendencies_flow_design.md](../../tendencies_flow_design.md)); approved by Mark as a new component.

## When to use
Where shots (or anything counted) pass through a fixed sequence of categories and the question is where they go: the tendency flow on Home's Tendencies tab (strike → direction → flight, `HmTendencyFlow`). Not for a share of one category (use `PpBandBar`) or a value over time (a chart).

It works out no number. The layout (counts, folding, order, every node's and ribbon's place) comes from C++, for the tendency flow `src/Coaching/tendency_flow.h` (`layoutTendencyFlow`), unit-tested on its own; the words come from whoever owns them (`WorkOnsController::tendencyFlow`). Never draw a flow from Shapes in a panel.

## Consumer supplies
`nodes`, `links` and `columns`, placed in plot units for a plot `plotHeight` high (lay the flow out at the height it is drawn, so a unit is a pixel), each ribbon's `tone` and `strength`, and optionally a `legend`.

## Props
| name | type | default | meaning |
|---|---|---|---|
| nodes | var | [] | `[{ column (0, 1, 2), label, sub ("22 · 28%"), good, tone?, y, h }]`, column by column, each top to bottom. `tone` inks the bar and its label; without it a good node is `colorGood`, the rest `colorText`. |
| links | var | [] | `[{ from, to (node indices), y0, y1, h, tone, strength (fill alpha), through: [node], line }]`, in draw order (last on top). `through` is every node the ribbon's things pass through: what a node's examination lights. `line` is what the examined ribbon says on the plot. |
| columns | var | [] | Three titles, set as caps micro heads over the columns. |
| legend | var | [] | `[{ tone, strength, name, line ("38 · 51%") }]`: a swatch, its words and its count, under the plot. |
| plotHeight | real | sp(360) | The plot's height; the layout's units. |
| nodeWidth | real | sp(12) | A node bar's width. |
| labelGutter | real | sp(104) | Room left of the first column and right of the last for their labels. |
| compactWidth | real | sp(640) | Narrower than this, the node subs are left out (their counts still show when examined). |
| signal nodeActivated(int index) | | | A node was pressed, or Return / Enter on its focus. |

Read-only: `compact`, `hoveredNode`, `hoveredLink`, `examining`, `hoverLabels`.

objectNames: `sankeyPlot`, `sankeyLink`, `sankeyNode`, `sankeyNodeLabel`, `sankeyNodeSub`, `sankeyHoverLabel`, `sankeyLegend`.

## Tokens & values
Ribbons: the link's `tone` at its `strength`, a `colorSurface` edge of 0.6 px between neighbours, cubic curves from the source's right edge to the target's left edge, never re-shaped by animation. Nodes: `nodeWidth` bars in their tone, radius `sp(1)`. Labels: name in `fontBody` / `fontSzBody` in the node's tone; sub in `fontData` / `fontSzLabel`, `colorText2`. Column 0's labels sit left of their nodes (right-aligned), column 2's right, column 1's on a knock-out chip (`colorSurface` at 86 %) right of its node; a label that would overlap a neighbour steps clear, inside the plot. Column heads: PpMicro caps. Legend: a `sp(14)` × `sp(8)` swatch, the name in body 12 px, the count in data 11 px `colorText2`. Focus ring: 2 px `colorAccent`, `Theme.radius`.

## States
- **Examined node** (pointer or keyboard focus): every ribbon in its `through` lit, the rest at 25 % of their strength, the other nodes at 40 %; its own ribbons' `line`s on the plot at their middles.
- **Examined ribbon** (pointer; where ribbons overlap, the one drawn last): it alone lit, its `line` at its middle.
- Labels on the plot that would collide with one already placed (the bigger ribbon's first) are left out.
- **Data change:** the plot fades in over `Theme.durationNormal` (0 under reduced motion). Geometry never animates.
- Colour is never the only channel: every node is named and counted, the legend names each tone with its count.

## Usage
```qml
PpSankey {
    width: parent.width
    plotHeight: 360
    columns: flow.columns
    nodes: flow.nodes.map(n => Object.assign({}, n, { tone: toneOf(n.kind) }))
    links: flow.links.map(l => Object.assign({}, l, { tone: groupTone(l.group), strength: groupStrength(l.group) }))
    legend: flow.groups.map(g => ({ tone: groupTone(g.group), strength: groupStrength(g.group), name: g.name, line: g.line }))
}
```

## Not yet
Print (the doc's `TendencyFlowPainter`, with greyscale patterns) is a follow-up; the report's pages are QML today.
