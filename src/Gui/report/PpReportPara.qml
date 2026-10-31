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

import QtQuick

// A LABELLED PARAGRAPH of the session report (the work-on pages, About this report): the label in the margin column
// (`labelW` wide), or above the text (`stacked`), the text in the measure — the map's words, with its inline <i> / <b>.
// One pager atom; drawn only when it has words (rule 6). `bold` and `dense` pick the body face (a dense work-on sets
// its paragraphs a size down), `lead` the line spacing, `foot` the space under it (About's paragraphs keep 14 px).
// FOLIO LIGHT through `ro`.
Item {
    id: pa
    property QtObject ro: null
    property string label: ""
    property string text: ""
    property real labelW: 118
    property bool stacked: false
    property bool bold: false
    property bool dense: false
    property real lead: 1.32
    property real foot: 0
    property bool atom: true
    width: 300
    visible: text !== ""
    height: visible ? (stacked ? pl.height + 4 + pt.height : Math.max(pl.height, pt.height)) + foot : 0
    Text { id: pl; y: pa.stacked ? 0 : 2; width: pa.stacked ? pa.width : pa.labelW - 12; text: pa.label; font: pa.ro.fLabel; color: pa.ro.ink; wrapMode: Text.WordWrap }
    Text { id: pt; x: pa.stacked ? 0 : pa.labelW; y: pa.stacked ? pl.height + 4 : 0; width: pa.width - x; text: pa.text
           textFormat: Text.StyledText; font: pa.bold ? (pa.dense ? pa.ro.fBodyDM : pa.ro.fBodyM) : (pa.dense ? pa.ro.fBodyD : pa.ro.fBody); color: pa.ro.ink; wrapMode: Text.WordWrap; lineHeight: pa.lead }
}
