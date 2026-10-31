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
.pragma library

// Home's shared words and row limits (.pragma library, imported relatively by HmFocus and
// HmSwingSummary): the ONE place a golfer's phrase is sentence-cased and the pip rows are capped.

// At most this many pips in a row: the latest judged sessions, oldest first, so a long history
// keeps the row its width.
var maxPips = 8

// A phrase in sentence case: its first letter capitalised, the rest as written.
function capitalised(s) {
    s = String(s)
    return s.length ? s.charAt(0).toUpperCase() + s.slice(1) : s
}
