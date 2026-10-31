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

// COLUMNS FITTED TO THE WIDEST WORD — the session report's measured tables (Your numbers, the launch monitor appendix's
// shot and summary tables; §9.1 rule 5): each column as wide as the widest thing in it (a value, a unit, the widest WORD
// of its head: heads wrap onto two lines rather than widen the table), and the spare width shared between the gaps,
// never less than `minGap`. The table measures with its own FontMetrics (widest(), widestWord()) in `measure`, a
// function giving each column's width; this calls it for `cols` and gives the gap, the total and each column's x.
// `measure` is called again whenever `width` changes, as well as when what it reads changes, including a FontMetrics'
// font when it measures through widest()/widestWord() (they read its height; a bare advanceWidth() call does not), so
// a font that settles after the data does is re-measured.
// `measure` is defined on this object, so it names the table's own properties through the table's id.
//   PpFitColumns { id: fit; width: t.width; minGap: 6; measure: function () { return [Math.ceil(fit.widest(figFm, …)), …] } }
//   …   x: fit.xOf(c)
QtObject {
    id: fc
    property var measure: null                          // (width) → each column's width, px (the table's Math.ceil of its widest)
    property real width: 0
    property real minGap: 6
    // measured at the width: passed so this binding reads it (the tables' measures do not need it)
    readonly property var cols: fc.measure ? (fc.measure(fc.width) || []) : []
    readonly property int n: cols.length
    readonly property real used: cols.reduce(function (a, b) { return a + b }, 0)
    readonly property real gap: n > 1 ? Math.max(minGap, (width - used) / (n - 1)) : 0
    readonly property real total: used + gap * Math.max(0, n - 1)
    function xOf(c) { let x = 0; for (let i = 0; i < c; ++i) x += cols[i] + gap; return x }
    // the widest of `texts` in the FontMetrics `fm` (0 for none). advanceWidth() is a call, no binding dependency; reading
    // fm.height (it notifies when the font changes) makes the binding that measures re-run when the font settles —
    // without it a table measured before its print font arrived kept the default font's widths. A bare `fm.height`
    // line would be dropped by the compiler, so the value is used.
    function widest(fm, texts) {
        let m = 0
        if (!(fm.height > 0)) return m
        for (const t of texts) m = Math.max(m, fm.advanceWidth(String(t)))
        return m
    }
    // the widest WORD of `text` (a head that wraps)
    function widestWord(fm, text) { return widest(fm, String(text || "").split(" ")) }
}
