/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

// A table row (the §2.5 visual language): a fixed-height row laid out on its card's column template.
//
//   height       Theme.rowH, or Theme.rowH2 with `secondLine`; a caller whose content wraps may set
//                its own height
//   padding      Theme.rowPadH each side (`padH`); the cells live in the box between
//   hover        a colorBg2 wash (colorBg3 on a table card, whose ground is already colorBg2), drawn
//                under the cells; `highlighted` holds it on (PpExpandRow's press area owns the
//                pointer, so it says when the row is hovered)
//   cells        the row's children, in column order: cell i takes column i + `columnOffset` of the
//                template — its x by the column's align, centred vertically (`centreCells`). A cell
//                keeps its own width; bind it to columnWidth(i) to fill or elide in its column (and
//                give a right-aligned Text horizontalAlignment: Text.AlignRight). A cell with no
//                column (no template, or more cells than columns) is left where the caller put it.
//                Cells are the direct children: lay a repeated cell out inside one Item, not with a
//                Repeater in the row.
//
// The template is the nearest PpCard { table: true } up the parent chain (`card`, or set it), so
// the rows and the card's captions share one set of column edges. `layout` may be set instead: a
// resolved template, [{ x, width, align }], x from the content box's left (PpCard.resolveColumns).
//
// A row that is the first or last in its table card rounds its hover to the card's corner;
// `roundTop` / `roundBottom` say so for a row nested deeper (PpExpandRow's header).

import QtQuick
import PinPointStudio

Item {
    id: row

    property bool secondLine:   false
    property int  padH:         Theme.sp(Theme.rowPadH)
    property int  columnOffset: 0
    property bool centreCells:  true
    property bool topRule:      false        // a hairline at the top, for a row outside a table card
    property bool hoverEnabled: true
    property bool highlighted:  false
    // How far the hover wash reaches past the row's sides: a row inside a padded card reaches the
    // card's edges with it.
    property int  hoverBleed:   0
    property color hoverColor:  row.card !== null ? Theme.colorBg3 : Theme.colorBg2

    property var  card: row._findCard(row.parent)
    property var  layout: row.card !== null ? row.card.columnLayout : []

    property bool roundTop:    row.card !== null && row.parent === row.card.bodyItem
                               && row.y < 0.5 && row.card.headerHeight === 0
    property bool roundBottom: row.card !== null && row.parent === row.card.bodyItem
                               && Math.abs(row.y + row.height - row.parent.height) < 0.5

    readonly property bool hovered: hover.hovered
    readonly property bool lit: row.hoverEnabled && (hover.hovered || row.highlighted)
    // The cells' box, for a caller to place a cell by hand.
    readonly property alias cellBox: cellHost
    default property alias cells: cellHost.data

    function _findCard(p) {
        for (let t = p; t; t = t.parent)
            if (t.isTableCard === true) return t
        return null
    }
    // A caller may hand a layout that is not an array (nothing yet); read it through this.
    function _layout()      { return row.layout || [] }
    function columnX(i)     { const L = row._layout()[i + row.columnOffset]; return L ? L.x : 0 }
    function columnWidth(i) { const L = row._layout()[i + row.columnOffset]; return L ? L.width : 0 }

    width:          parent ? parent.width : 0
    implicitHeight: Theme.sp(row.secondLine ? Theme.rowH2 : Theme.rowH)
    height:         implicitHeight

    HoverHandler { id: hover; enabled: row.hoverEnabled }

    Rectangle {     // the hover wash
        visible: row.lit
        x: -row.hoverBleed
        width: row.width + 2 * row.hoverBleed; height: row.height
        readonly property int r: row.card !== null ? Math.max(0, row.card.cornerRadius - 1) : 0
        topLeftRadius:     row.roundTop    ? r : 0
        topRightRadius:    row.roundTop    ? r : 0
        bottomLeftRadius:  row.roundBottom ? r : 0
        bottomRightRadius: row.roundBottom ? r : 0
        color: row.hoverColor
    }
    Rectangle {
        visible: row.topRule
        width: row.width; height: 1
        color: Theme.colorHairline
    }

    Item {
        id: cellHost
        x: row.padH
        width:  Math.max(0, row.width - 2 * row.padH)
        height: row.height
        onChildrenChanged: if (row._built) row._place()
    }
    onLayoutChanged:       if (row._built) row._place()
    onColumnOffsetChanged: if (row._built) row._place()
    onCentreCellsChanged:  if (row._built) row._place()

    // The cells are placed once the row is complete, never while it is being built. Placing reads
    // `layout`, and reading it while the cells are still being added forces the caller's `layout:`
    // binding to run early — in a Repeater delegate, before the delegate's context can resolve the
    // ids around it ("Cannot read property … of undefined"). A binding that fails that way has
    // captured nothing, so it never runs again and the row is left with no template at all.
    property bool _built: false
    Component.onCompleted: { row._built = true; row._place() }

    // Bind each cell that has a column to that column. The bindings read the layout, so a template
    // that changes width moves the cells without another pass.
    function _place() {
        const kids = cellHost.children
        for (let i = 0; i < kids.length; ++i) {
            const k = kids[i]
            const idx = i + row.columnOffset
            if (!row._layout()[idx]) continue
            k.x = Qt.binding(function () {
                const L = row._layout()[idx]
                if (!L) return 0
                return Math.round(L.align === "right"  ? L.x + L.width - k.width
                                : L.align === "center" ? L.x + (L.width - k.width) / 2
                                :                        L.x)
            })
            if (row.centreCells)
                k.y = Qt.binding(function () { return Math.round((cellHost.height - k.height) / 2) })
        }
    }
}
