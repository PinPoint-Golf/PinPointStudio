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

// HOW EVERY FIGURE QML SETS IS SET — the QML twin of Core/figure_format.h, function for function, so a figure QML
// sets reads exactly as the C++ sets it (gui-catalogue C15–C17). PpTypeset's figure helpers are these (a typeset
// block calls roles.figure(…)); a file with no PpTypeset to hand imports this directly (HmAreaRow). C++ fig() is
// figure() here: PpDiagRoles.fig() is something else (the figure OF a reading the model published).
//   · missing is "-", never 0; the minus is U+2212; a value that rounds to zero is 0, never "−0"
//   · ties round away from zero (std::round)
//   · a figure and its unit: a no-break space, except that °, °/s, ″ and :1 close up; words after a % stay
//     ("13% hand rise"); "ratio" prints the bare number

function missing(v) { return v === undefined || v === null || v === "" || !isFinite(v) }

// decimalsFor: whole units, except a tempo ratio and inches (1) and seconds (2)
function decimalsFor(unit) { var u = String(unit || ""); return u === ":1" || u === "in" ? 1 : u === "s" ? 2 : 0 }

// fig: rounded at `dec` decimals (negative: to tens, hundreds…); `trimZeros` drops zeros after the point
function figure(v, dec, trimZeros) {
    if (missing(v)) return "-"
    var d = dec || 0, p = Math.pow(10, d), n = Number(v)
    var x = (n < 0 ? -1 : 1) * Math.round(Math.abs(n) * p) / p
    if (Math.abs(x) < 0.5 / p) x = 0
    var body = Math.abs(x).toFixed(Math.max(0, d))
    if (trimZeros && body.indexOf(".") >= 0) body = body.replace(/0+$/, "").replace(/\.$/, "")
    return (x < 0 ? "−" : "") + body
}

// figSigned: "+3", "−3", "0" (rounds to zero: no direction), "-" when missing
function figureSigned(v, dec) {
    var f = figure(v, dec)
    if (missing(v)) return f
    return (Math.round(Number(v) * Math.pow(10, dec || 0)) > 0 ? "+" : "") + f
}

// withUnit — THE ONE RULE FOR A FIGURE AND ITS UNIT. `f` may be a phrase ending in a figure; "-" and an empty
// unit pass through.
function withUnit(f, unit) {
    var s = String(f === undefined || f === null ? "" : f), u = String(unit || "")
    if (s === "-" || !u) return s
    if (u.charAt(0) === "°" || u.charAt(0) === "″" || u === ":1") return s + u
    if (u.charAt(0) === "%") { var rest = u.slice(1).trim(); return s + "%" + (rest ? " " + rest : "") }
    if (u === "ratio") return s
    return s + "\u00a0" + u
}

// figUnit: a figure in its unit's display precision, joined by withUnit ("82 mph", "13°", "2.6:1")
function figureUnit(v, unit) { return withUnit(figure(v, decimalsFor(unit)), unit) }

// shortUnit: every percent of a body dimension is "%" (the reading's own name carries the denominator)
function shortUnit(unit) { var u = String(unit || ""); return u.charAt(0) === "%" ? "%" : u }

// "a, b and c" (charts_words.h list)
function list(xs) { xs = xs || []; return xs.length <= 1 ? xs.join("") : xs.slice(0, -1).join(", ") + " and " + xs[xs.length - 1] }
