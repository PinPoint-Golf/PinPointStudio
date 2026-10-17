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
import QtQuick.Controls
import QtQuick.Shapes
import QtQuick.Window
import PinPointStudio

// THE LAUNCH MONITOR PANEL'S BODY — Foresight FSX's graphic grammar set in the typeset themes
// (docs/design/lm_panel_fsx.md; the signed-off spec is round 4c of mock A,
// build/run-me/lm-variants/qml/Panel_A_fsxpro.qml). GC Quad owners read FSX fluently: a tile with a big
// value and an AVG ± SD footer, dispersion ellipses on carry arcs, an annotated club view. Inside these
// figures FSX's conventions take precedence over §15's no-boxes and colour-only-for-out rules; themes,
// print, em size classes and fold-don't-drop still follow §15.
//
// Two quiet tabs. THIS SHOT: the glance tiles, then four EQUAL club figures (Path, Loft, Spin, Impact)
// in FSX's 2×2 (image 03: Path and Spin on top, Loft and Impact below; a row or a column runs Path, Loft,
// Spin, Impact), then the true-scale flight. The flight shrinks or strips first; the figures never fold
// before it. SESSION: a ball half (dispersion with enclosing ellipses on carry arcs, launch × spin, ball
// tiles) and a club half (impact map, path × face, club tiles), every tile with a strip of every shot.
// Directions are WORDS, never signs; a missing value is "-".
//
// THE NUMBERS COME FROM C++. This file positions and paints. Everything it draws is LmSessionModel's
// `graphics` map (g.values, g.shots, g.clubs, g.mismatch, g.device, g.leftHanded, g.flight …) or the
// board's bands, and every picture is LmClubArt's (heads, anchors, strike origin, ball, impact patch).
// No ellipse, club vote, club override or anchor table is computed here. A field the model does not
// publish yet reads "-" or is not drawn — never an error, never a stand-in number.
//
// INTERACTION (Analyse only; cast and print are static — `interactive: false`): a click on a mark in a
// Session figure or strip focuses that shot (drill-in from evidence: `shotRequested`); hovering a mark
// opens a card in words and a click pins it, holding Leave out and Club; the club key sets the Session
// scope; the mismatch note carries Undo. No shot list (the carousel navigates), no menus, no dialogs, no
// keyboard shortcuts (an app-wide accessibility design is planned).
Item {
    id: root

    // ═══ inputs (the panel binds these; a test may hand a plain map) ═══
    property var g: ({})                       // LmSessionModel.graphics
    property var bands: []                     // the board's rows: [{ band, count, tiles }]
    property var shotList: []                  // [{ shotId, ordinal, club }] as recorded — the heading's fallback only
    property string scopeText: ""              // LmSessionModel.scopeText
    property string emptyText: ""              // LmSessionModel.emptyText
    property string liveDeviceName: ""         // the live connector's name, when graphics.device is absent
    property bool reviewing: false
    property int focusedShotId: -1             // SessionMode.focusedShotId
    property bool interactive: true            // false on the cast and in print
    property string tab: "shot"                // "shot" | "session"
    property string medium: "screen"           // "screen" | "print"
    property string surface: "panel"           // "panel" | "cast"
    property string printOrientation: ""
    property int printPage: 0
    readonly property int printPageCount: medium === "print" ? printPlan.length : 1
    readonly property string register: medium === "print" ? "Print"
                                      : (surface === "cast" || SessionMode.mode !== SessionMode.analyse) ? "Replay" : "Analyse"

    // ═══ intents (the panel turns these into the model's and SessionMode's calls) ═══
    signal shotRequested(int shotId)                          // focus that shot (SessionMode)
    signal excludeRequested(int shotId, bool excluded)        // LmSessionModel.setShotExcluded
    signal clubRequested(int shotId, string club)             // LmSessionModel.setShotClub ("" = the default)
    signal undoMismatchRequested()                            // LmSessionModel.undoMismatch

    // The last This shot layout chosen (arrangement, cell size, flight placement): read by the render
    // harness for its layout sheet, never by this file's own bindings.
    property var lastShotLayout: null

    // THE ROLE TABLE is Theme's: Theme.typeset.lm (defaults in _typesetDefault.lm, a theme's overrides merged
    // over them). Sizes are design units (through ts.u()); ems are the body role. `centreMark` names a colour
    // role ("accent" | "series:N" | "ink"), resolved in roleColor().
    readonly property var lr: Theme.typeset.lm || ({})

    // ═══ interaction state (the panel's only; what persists is the model's) ═══
    property string scopeKey: ""                      // the Session scope picked in the club key ("" = the focused shot's club)
    property int chooserFor: -1                       // the shot whose inline club chooser is open
    property int hoverShot: -1
    property string hoverSrc: ""
    property point hoverPos: Qt.point(0, 0)
    property bool cardHeld: false
    readonly property bool canAct: interactive && medium === "screen" && !isCast
    readonly property real hitR: Theme.sp(lr.hitPx) / 2
    // a click (or a tap: touch has no hover) PINS the card to that shot, so Leave out and the club fix are
    // reachable; hovering other shots shows theirs, and the card falls back to the pinned one. A click on a
    // figure's empty ground, or a tab change, unpins it.
    property int pinnedId: -1
    property string pinnedSrc: ""
    property point pinnedPos: Qt.point(0, 0)
    function pinCard(id, src, pt) { pinnedId = id; pinnedSrc = src; pinnedPos = pt; setHover(id, src, pt) }
    function unpinCard() { pinnedId = -1; hoverShot = -1; hoverSrc = ""; chooserFor = -1 }
    onCurTabChanged: unpinCard()
    onFocusedShotIdChanged: chooserFor = -1
    Timer { id: hoverClear; interval: root.lr.hoverGraceMs
            onTriggered: { if (root.cardHeld) return
                           if (root.pinnedId >= 0) { root.hoverShot = root.pinnedId; root.hoverSrc = root.pinnedSrc; root.hoverPos = root.pinnedPos }
                           else { root.hoverShot = -1; root.hoverSrc = "" } } }
    function setHover(id, src, pt) { hoverClear.stop(); hoverShot = id; hoverSrc = src; hoverPos = pt }
    function leaveHover(src) { if (hoverSrc === src) hoverClear.restart() }
    function focusOn(id) { scopeKey = ""; chooserFor = -1; root.shotRequested(id) }
    function toggleExclude(id) { const s = shotById(id); root.excludeRequested(id, !(s && s.out)) }
    function setClub(id, club) { chooserFor = -1; root.clubRequested(id, String(club)) }
    // the notes' actions, by name (a note carries { label, act, ids })
    function runAction(a) {
        if (a.act === "undoMismatch") root.undoMismatchRequested()
        else if (a.act === "unlabel") { for (const id of a.ids) root.clubRequested(id, "") }
        else if (a.act === "restore") { for (const id of a.ids) root.excludeRequested(id, false) }
    }
    readonly property int hoverId: hoverShot
    readonly property string hoverFrom: hoverSrc

    readonly property QtObject ts: tsx               // the one PpTypeset, reachable from the blocks as h.ts

    // the tiles, as data: tier 1 always; tier 3 from medium up. Else: the quiet "also" line under the
    // strip (every register), and the reading index / ledger below the fold.
    readonly property var heroDefs: [
        { key: "lm.clubheadSpeed", label: "Club speed",   tier: 3 },
        { key: "lm.ballSpeed",     label: "Ball speed",   tier: 1 },
        { key: "lm.smashFactor",   label: "Smash",        tier: 3 },
        { key: "lm.launchAngle",   label: "Launch angle", tier: 1 },
        { key: "lm.spinRate",      label: "Spin rate",    tier: 1 },
        { key: "lm.carryDistance", label: "Carry",        tier: 1 },
        { key: "lm.totalDistance", label: "Total",        tier: 3 },
        { key: "lm.offline",       label: "Offline",      tier: 3 }
    ]
    // print: the blocks on each page (each block is an atom; nothing splits)
    readonly property var printPlan: printOrientation === "landscape"
        ? [["mast", "tiles", "figs"], ["sesHead", "ballHalf", "clubsum"], ["clubHalf", "notes"], ["ledger"], ["shots"]]
        : [["mast", "tiles", "club"], ["flight", "sesHead", "ballHalf"], ["clubHalf", "clubsum", "notes"], ["ledger"], ["shots"]]

    // ═══ data: the model's map, read as it is published ═══
    readonly property var vals: (g && g.values) || ({})
    // WHY A READING IS MISSING (trust stage 6 session 2): the gate that dropped each reading the headline shot
    // lacks ({ subject, gate, words, … }, LmSessionModel), and the swing its reason opens on. A tile prints the
    // words where it printed "-"; an old swing has none and keeps its "-".
    readonly property string gateSwingDir: (g && g.gateSwingDir) || ""
    function gateOf(key) { const m = g && g.gates; return (m && m[key] && m[key].words) ? m[key] : null }
    readonly property bool hasData: !!g && g.has === true
    // every shot in the session, in shot order, with its EFFECTIVE club (graphics.shots). Until the model
    // publishes graphics.shots, the recorded list stands in for the heading only (no per-shot values).
    readonly property bool hasShots: !!g && isList(g.shots)
    readonly property var shots: {
        const src = hasShots ? g.shots : (shotList || []).map(s => ({ id: s.shotId, number: s.ordinal, club: s.club, recordedClub: s.club,
                                                                         clubSource: "recorded", excluded: false, values: {}, evidence: {} }))
        const out = []
        for (const s of src) {
            const v = s.values || {}
            const club = String(s.club || ""), raw = String(s.recordedClub !== undefined ? s.recordedClub : club)
            out.push({ shotId: s.id, ordinal: s.number, club: club, clubKey: club.toLowerCase(), rawClub: raw,
                       clubSource: s.clubSource || "recorded", relabelled: club.toLowerCase() !== raw.toLowerCase(),
                       out: s.excluded === true, values: v, evidence: s.evidence || {}, gateWords: s.gateWords || {},
                       carry: fin(v["lm.carryDistance"]), offline: fin(v["lm.offline"]), total: fin(v["lm.totalDistance"]) })
        }
        out.sort((a, b) => a.ordinal - b.ordinal)
        return out
    }
    // a list the model published (a QVariantList reaches QML as a sequence, which Array.isArray does not call an array)
    function isList(x) { return x !== null && x !== undefined && typeof x === "object" && typeof x.length === "number" }
    function fin(x) { return (typeof x === "number" && isFinite(x)) ? x : NaN }
    function shotById(id) { for (const s of shots) if (s.shotId === id) return s; return null }
    function shotV(s, key) { return s && s.values ? fin(s.values[key]) : NaN }
    readonly property int focusId: focusedShotId
    readonly property var focusShot: shotById(focusId)
    readonly property int shotNo: focusShot ? focusShot.ordinal : 0
    // the SCOPE: the club picked in the key, else the focused shot's club
    readonly property string focusKey: {
        if (scopeKey !== "") for (const s of shots) if (s.clubKey === scopeKey) return s.clubKey
        return focusShot ? focusShot.clubKey : ""
    }
    // the clubs: one per effective club, from graphics.clubs (stats and ellipses), plus any club whose shots
    // are all left out (its marks still draw, hollow). Hues by bag order, so a club keeps its hue whatever
    // the scope; the scoped club first.
    readonly property var clubs: {
        const m = {}, order = [], gc = (g && isList(g.clubs)) ? g.clubs : []
        function ensure(club) {
            const k = String(club || "").toLowerCase()
            if (!m[k]) { m[k] = { key: k, name: clubName(club), club: club, head: "", n: 0, stats: null, ellipses: {}, ell: null, shots: [], inc: [] }; order.push(m[k]) }
            return m[k]
        }
        for (const c of gc) { const e = ensure(c.club); e.head = c.head || ""; e.n = c.n || 0; e.stats = c.stats || null; e.ellipses = c.ellipses || {} }
        for (const s of shots) { const e = ensure(s.club); e.shots.push(s); if (!s.out) e.inc.push(s) }
        order.sort((a, b) => bagRank(a.key) - bagRank(b.key))
        const ser = lr.clubSeries
        order.forEach((c, i) => {
            c.hue = Theme.chartSeriesColor(ser[i % ser.length])
            const d = c.ellipses ? c.ellipses.dispersion : null
            c.ell = ellOk(d) ? { mx: d.cx, my: d.cy, rx: d.a, ry: d.b, rot: (d.angleDeg || 0) * Math.PI / 180,
                                 n: d.n > 0 ? d.n : c.inc.filter(s => isFinite(s.carry) && isFinite(s.offline)).length } : null
        })
        order.sort((a, b) => (a.key === root.focusKey ? -1 : b.key === root.focusKey ? 1 : 0))
        return order
    }
    // an ellipse the model published: { cx, cy, a, b, angleDeg }; absent under three shots
    function ellOk(e) { return !!e && isFinite(e.cx) && isFinite(e.cy) && e.a > 0 && e.b >= 0 }
    readonly property var focusClub: { for (const c of clubs) if (c.key === focusKey) return c; return null }
    readonly property var scopedAll: focusClub ? focusClub.shots : []        // drawn (left-out shots hollow)
    readonly property var scopedShots: focusClub ? focusClub.inc : []        // the statistics' shots (none left out)
    readonly property color focusHue: focusClub ? focusClub.hue : ts.ink2
    readonly property int scopeN: focusClub ? focusClub.inc.length : 0
    readonly property var clubChoices: {
        const seen = {}, out = []
        for (const s of shots) for (const c of [s.rawClub, s.club]) { const k = String(c).toLowerCase(); if (k && !seen[k]) { seen[k] = 1; out.push(c) } }
        return out.sort((a, b) => bagRank(String(a).toLowerCase()) - bagRank(String(b).toLowerCase()))
    }

    // ═══ the quiet notes: the model's mismatch (with Undo), the golfer's own club fixes (with Undo), the
    // shots left out (with Restore). [{ text, actions: [{ label, act, ids }], print }] ═══
    readonly property var noteItems: {
        const out = [], mm = g ? g.mismatch : null
        if (mm && mm.count > 0) {
            const n = mm.count, sug = String(mm.suggested || ""), generic = mm.relabelled === false || /^an? /i.test(sug)
            const t = words(n) + (n === 1 ? " shot" : " shots") + " recorded as " + clubName(mm.recorded)
                    + (generic ? (n === 1 ? " reads" : " read") + " like " + sug
                               : (n === 1 ? " is" : " are") + " treated as " + clubName(sug).toLowerCase())
            const ids = Array.from(mm.shotIds || [])
            out.push({ text: t, actions: generic ? [] : [{ label: "Undo", act: "undoMismatch", ids: ids }],
                       print: t + (ids.length ? " (" + (ids.length === 1 ? "shot " : "shots ") + ordList(ids.map(id => shotById(id)).filter(s => !!s)) + ")" : "") + "." })
        }
        const groups = {}
        for (const s of shots) if (s.clubSource === "user" && s.relabelled) {
            const k = s.rawClub + "→" + s.club
            ;(groups[k] = groups[k] || { from: s.rawClub, to: s.club, list: [] }).list.push(s)
        }
        for (const k in groups) {
            const gp = groups[k], n = gp.list.length
            const t = words(n) + (n === 1 ? " shot" : " shots") + " recorded as " + clubName(gp.from) + (n === 1 ? " is" : " are")
                    + " set to " + clubName(gp.to).toLowerCase() + " (" + (n === 1 ? "shot " : "shots ") + ordList(gp.list) + ")"
            out.push({ text: t, actions: [{ label: "Undo", act: "unlabel", ids: gp.list.map(s => s.shotId) }], print: t + "." })
        }
        const ex = shots.filter(s => s.out)
        if (ex.length) out.push({ text: words(ex.length) + (ex.length === 1 ? " shot" : " shots") + " left out (" + (ex.length === 1 ? "shot " : "shots ") + ordList(ex) + "); the averages, spreads and ellipses are without " + (ex.length === 1 ? "it" : "them"),
                                  actions: [{ label: ex.length === 1 ? "Restore" : "Restore all", act: "restore", ids: ex.map(s => s.shotId) }],
                                  print: (ex.length === 1 ? "Shot " : "Shots ") + ordList(ex) + " left out of the averages, spreads and ellipses." })
        return out
    }
    function ordList(list) { return list.map(s => s.ordinal).join(", ") }
    readonly property var outKeys: { const o = []; for (const b of bands) for (const t of b.tiles) if (isOut(t.key)) o.push(t); return o }
    readonly property int gradedN: { let n = 0; for (const b of bands) for (const t of b.tiles) if (t.grade) ++n; return n }
    readonly property int readingN: { let n = 0; for (const b of bands) n += b.tiles.length; return n }

    // ═══ helpers ═══
    function clubName(c) { const s = String(c || "").toLowerCase(); return s.length ? s.charAt(0).toUpperCase() + s.slice(1) : "Unknown club" }
    // the bag's order, for hues and lists (presentation only)
    function bagRank(k) {
        if (/driver/.test(k)) return 0
        let m = k.match(/(\d+)\s*(wood|w$)/); if (m) return 10 + (+m[1])
        m = k.match(/(\d+)\s*(hybrid|h$)/); if (m) return 20 + (+m[1])
        m = k.match(/(\d+)\s*(iron|i$)/); if (m) return 30 + (+m[1])
        if (/pitch|^pw/.test(k)) return 50
        if (/wedge|^\d\d$/.test(k)) return 55
        return 90
    }
    function vOf(key) { return vals[key] || null }
    function has(key) { const v = vOf(key); return !!v && v.has === true && String(v.text) !== "—" }
    function num(key) { const v = vOf(key); return v && v.has ? v.value : NaN }
    // no explicit signs: a directional reading is its size plus a direction WORD (dirParts below)
    function signed(key, t, x) { return String(t).replace(/^-/, "−") }
    function txt(key) { if (!has(key)) return "-"; const v = vOf(key); return signed(key, String(v.text), v.value) }
    // A raw value in the reading's own precision (the board's text for that reading sets the decimals):
    // the model hands raw doubles for the per-shot and per-club figures, and this only sets them in type.
    function decimalsOf(key) {
        const v = vOf(key)
        if (v && v.text !== undefined) { const m = String(v.text).match(/\.(\d+)/); return m ? m[1].length : 0 }
        const u = v ? String(v.unit) : ""
        return u === "ratio" ? 2 : (u === "rpm" || u === "mm" || u === "ft") ? 0 : 1
    }
    function fmtKey(key, x) {
        if (!isFinite(x)) return "-"
        const d = decimalsOf(key), a = Math.abs(x).toFixed(d)
        return (x < 0 && Number(a) !== 0 ? "−" : "") + a
    }
    // ═══ DIRECTION WORDS (Mark: "words are clearer than symbols") ═══
    // LR: path, face, start, offline, spin axis, side spin → "2.3 R" / "1.3 L" (a side letter, target-relative);
    // UD: attack, lie → "0.2 DOWN" / "1.6 UP" (lie: toe up); OC: face to path → "1.2 OPEN" / "CLOSED";
    // TH: strike location → "9 mm HEEL" / "TOE"; HL: strike height → "1 mm LOW" / "HIGH". A value of exactly 0
    // has no word. The device's values are target-relative for both hands (graphics.leftHanded publishes the
    // convention): R/L never flip, OPEN/CLOSED flips for a left-handed golfer, heel/toe, up/down and
    // high/low are the club's own and never flip.
    readonly property var dirKinds: ({ "lm.launchDirection": "LR", "lm.offline": "LR", "lm.spinAxis": "LR", "lm.sideSpin": "LR",
        "lm.clubPath": "LR", "lm.faceAngle": "LR", "lm.faceToPath": "OC", "lm.attackAngle": "UD", "lm.lieAngle": "UD",
        "lm.strikeLocation": "TH", "lm.strikeHeight": "HL" })
    readonly property bool leftHanded: !!g && g.leftHanded === true
    function dirWordOf(key, neg) {
        const k = dirKinds[key]
        if (k === "LR") return neg ? "L" : "R"
        if (k === "UD") return neg ? "DOWN" : "UP"
        if (k === "OC") return (neg !== leftHanded) ? "CLOSED" : "OPEN"
        if (k === "TH") return neg ? "HEEL" : "TOE"
        if (k === "HL") return neg ? "LOW" : "HIGH"
        return ""
    }
    // "−1.3" → { num: "1.3", dir: "L" }; a plain reading keeps its text. `unit` is set between ("9 mm HEEL").
    function dirParts(key, text, unit) {
        let s = String(text === undefined || text === null ? "-" : text).replace(/^μ\s*/, "").trim()
        if (s === "" || s === "-" || s === "—") return { num: "-", dir: "" }
        if (!dirKinds[key]) return { num: s.replace(/^-/, "−").replace(/^\+/, "") + (unit || ""), dir: "" }
        const neg = /^[−-]/.test(s), n = s.replace(/^[−+-]/, "")
        return { num: n + (unit || ""), dir: parseFloat(n) === 0 ? "" : dirWordOf(key, neg) }
    }
    function dirStr(key, text, unit) { const d = dirParts(key, text, unit); return d.num + (d.dir ? " " + d.dir : "") }
    function isDir(key) { return !!dirKinds[key] }
    // a reading with its degree sign or strike millimetres, then its word: "1.3° L", "9 mm HEEL", "0.2° DOWN"
    function dirU(key, text) { const u = unitOf(key); return dirStr(key, text, u === "°" ? "°" : u === "mm" ? " mm" : "") }
    function unitOf(key) { const v = vOf(key); return v ? String(v.unit) : "" }
    function meanTxt(key) {
        const v = vOf(key); if (!v || !v.hasSpread) return "-"
        return signed(key, String(v.mean).replace(/^μ\s*/, ""), v.meanValue)
    }
    function sdTxt(key) { const v = vOf(key); return v && v.hasSpread ? String(v.sd) : "" }
    function isOut(key) { const v = vOf(key); const gr = v ? String(v.grade) : ""; return gr === "watch" || gr === "action" }
    function figParts(key) { if (!isDir(key)) return { num: figU(key), dir: "" }; const u = unitOf(key); return dirParts(key, txt(key), u === "°" ? "°" : u === "mm" ? " mm" : "") }
    function figU(key) { if (isDir(key)) return dirU(key, txt(key)); const u = unitOf(key); return (u === "°" && has(key)) ? txt(key) + "°" : txt(key) }
    function label(key) { const v = vOf(key); return v ? String(v.label).replace(/\s*\(measured\)/, "") : key }
    function measured(key) { const v = vOf(key); return v ? /\(measured\)/.test(String(v.label)) : false }
    function unitWord(u) { return u === "ratio" ? "" : u }
    function words(n) { return ts.cap1(ts.word(n)) }
    // THE DEVICE: graphics.device (from the shot data, not only the live connection); else the live name
    readonly property var device: (g && g.device) || ({})
    // (until graphics.device is published: the device the board's scope line names, "GC Quad · DRIVER · 8 shots")
    readonly property string deviceName: device.name ? String(device.name)
                                       : String(scopeText).split(" · ").length >= 3 ? String(scopeText).split(" · ")[0] : liveDeviceName
    readonly property string markerKind: device.markers === "four" || device.markers === "one" ? device.markers : ""
    readonly property string scopeLine: (deviceName ? deviceName + " · " : "") + (focusClub ? focusClub.name + " · " + scopeN + " shots" : ts.cap1(String(scopeText).toLowerCase()))
    readonly property string corridorLine: !hasData ? "" : outKeys.length === 0
        ? (gradedN > 0 ? "nothing outside its corridor" : "no corridor for this club")
        : outKeys.map(t => label(t.key)).join(", ") + (outKeys.length === 1 ? " outside its corridor" : " outside their corridors")
    readonly property string headLine: focusShot ? "Shot " + shotNo + " of " + shots.length + "  ·  " + clubName(focusShot.club)
                                                   + (focusShot.relabelled ? " (recorded " + clubName(focusShot.rawClub).toLowerCase() + ")" : "") + (focusShot.out ? "  ·  left out" : "") : ""
    // print: the club fixes and the shots left out, as plain sentences (no controls on paper)
    readonly property string printNotes: noteItems.map(n => n.print).join(" ")
    readonly property var shape: (g && g.shape) || ({})
    readonly property var strike: (g && g.strike) || ({})
    readonly property var flight: (g && g.flight) || ({})
    readonly property string shapeLine: shape.has ? shape.name + " (inferred): " + shape.evidence : ""
    // low point: the device's reading, else PinPoint's own optical estimate (labelled as the app does, "PPS est.")
    readonly property var lowPt: (g && g.lowPoint) || ({})
    readonly property string lowPointLine: lowPt.has === true
        ? "Low point " + lowPt.text + " " + (lowPt.unit || "in") + (lowPt.source === "inferred" ? " · PPS est." : "") + "."
        : "Low point: not measured."
    readonly property string focusHead: focusShot ? headOf(focusShot.club) : ""
    readonly property bool focusIron: focusHead === "iron" || focusHead === "wedge"
    readonly property string patchShort: !art ? "" : "Face: this strike, a " + patchMm(focusHead) + " mm patch" + (patchMm(focusHead) === 20 ? " (est.)" : "")
        + (markerKind ? "; dots: " + deviceName + " markers." : ".")
    readonly property string patchLine: !art ? "" : "Face: this strike as a " + patchMm(focusHead) + " mm impact patch"
        + (patchMm(focusHead) === 20 ? " (20 mm on irons and wedges is an estimate)" : "")
        + ", centre marked, read from " + (focusIron ? "Foresight's centre, midway between score lines 6 and 7" : "the middle of the score lines")
        + (markerKind ? "; dots: the " + deviceName + "'s club markers." : ".")

    // ═══ type & measures (from the roles; never from our own laid-out size) ═══
    readonly property font fHero: Qt.font({ family: ts.r.figureFamily, pixelSize: Math.round(ts.u(lr.heroSize)), weight: lr.heroWeight })
    readonly property font fTileLabel: Qt.font({ family: ts.r.labelFamily, pixelSize: Math.round(ts.u(ts.r.labelSize)), weight: Font.Normal,
                                                 capitalization: Font.AllUppercase, letterSpacing: lr.tileLabelTracking })
    readonly property real em: ts.em
    readonly property real tileGap: ts.u(lr.tileGap)
    FontMetrics { id: fmLab; font: root.fTileLabel }
    FontMetrics { id: fmHero; font: root.fHero }
    readonly property real labLH: Math.ceil(fmLab.height)
    readonly property real heroLH: Math.ceil(fmHero.height)
    readonly property real footH: Math.ceil(ts.fFigLabel.pixelSize * 1.3 + ts.u(8))
    readonly property real tileH: Math.round(ts.u(lr.tilePad) + labLH + ts.u(1) + heroLH + labLH + ts.u(5) + footH)
    readonly property real annoH: labLH + ts.u(lr.annoGap) + Math.ceil(ts.fFigL.pixelSize * 1.15)
    readonly property real stripRowH: Math.round(ts.u(lr.sesStripH + lr.stripGap + 3))
    // the direction word's face: small capitals in the label family, at the label size, tracked
    readonly property font fQual: Qt.font({ family: ts.r.labelFamily, pixelSize: Math.round(ts.u(ts.r.labelSize)), weight: Font.Medium,
                                            capitalization: Font.AllUppercase, letterSpacing: lr.qualTracking })
    readonly property color tileBorder: Qt.alpha(ts.ink, Theme.dark && !ts.isPrint ? 0.14 : 0.16)
    readonly property real tileRadius: ts.isPrint ? 0 : Theme.radius
    readonly property color annoHalo: ts.isPrint ? "#FFFFFF" : Theme.colorSurface

    // ═══ the cast's whole-surface zoom: an em estimate of the glance's height, never a measurement ═══
    readonly property bool isCast: surface === "cast"
    readonly property real castEstEm: 2.2 + 21 + 2 * (tileH / em) + 1.5 + 3
    readonly property real zoom: !isCast ? 1 : Math.max(1, Math.min(1.6, height / (castEstEm * em), width / (42 * em)))

    // ═══ the two views: This shot · Session. Replay and the cast open on This shot. ═══
    readonly property string curTab: tab === "session" ? "session" : "shot"
    readonly property bool castInteractive: isCast && interactive
    readonly property var heroTiles: heroDefs.map(d => heroTile(d))
    // the glance also names closure rate (the also line); peak height and descent are written on the flight's side view
    readonly property var glanceExtra: ["lm.closureRate"].filter(k => has(k)).map(k => ({ key: k, label: label(k) }))
    function heroTile(d) { const p = dirParts(d.key, txt(d.key))
                           return { key: d.key, tier: d.tier, label: d.label, value: p.num, dir: p.dir, unit: unitWord(unitOf(d.key)), footL: meanTxt(d.key) === "-" ? "-" : dirStr(d.key, meanTxt(d.key)), footR: sdTxt(d.key), out: isOut(d.key),
                                    gate: has(d.key) ? null : gateOf(d.key) } }
    // Session: the scoped club's statistics — graphics.clubs[].stats (mean, sd, n, without the shots left
    // out, by the effective club). Until the model publishes them, the board's own scope (vals) stands for the
    // focused shot's club, and any other club has none ("-").
    function statsFor(key) {
        const c = focusClub
        if (c && c.stats && c.stats[key]) { const s = c.stats[key]; return { mean: fin(s.mean), sd: fin(s.sd), n: s.n || 0, hasSpread: s.hasSpread !== undefined ? s.hasSpread === true : fin(s.sd) > 0 } }
        if (c && !(g && isList(g.clubs)) && focusShot && c.key === focusShot.clubKey) {
            const v = vOf(key)
            if (v && v.hasSpread) return { mean: fin(v.meanValue), sd: fin(v.sdValue), n: v.n || 0, hasSpread: true }
        }
        return null
    }
    function sesTile(key, lab) {
        const st = statsFor(key), p = dirParts(key, st ? fmtKey(key, st.mean) : "-")
        return { key: key, label: lab, value: p.num, dir: p.dir, unit: unitWord(unitOf(key)), footL: (st ? st.n : scopeN) + " shots",
                 footR: st && st.hasSpread ? "±" + fmtKey(key, st.sd) : "", out: false, strip: stripFor(key) }
    }
    // the scoped shots' extremes (range, spray): a selection from the shots' own values, not a statistic
    // a club's extremes (range, spray): graphics.clubs[].stats.<key>.min / .max, over the shots not left out. Until
    // the model publishes them, the club's shots' own values are scanned (a selection, not a statistic).
    function rangeOf(f, club) {
        const c = club === undefined ? focusClub : club
        const key = f === "carry" ? "lm.carryDistance" : f === "offline" ? "lm.offline" : f
        const s = c && c.stats ? c.stats[key] : null
        if (s && isFinite(s.min) && isFinite(s.max)) return { lo: s.min, hi: s.max }
        const xs = (c ? c.inc : []).map(x => f === "carry" || f === "offline" ? x[f] : shotV(x, key)).filter(x => isFinite(x))
        return xs.length ? { lo: Math.min.apply(null, xs), hi: Math.max.apply(null, xs) } : null
    }
    function lrWord(x) { return Math.abs(x) < 0.05 ? "0" : ts.num(Math.abs(x)) + (x < 0 ? " L" : " R") }
    // the every-shot strip: every scoped shot placed on ±3 SD of the scope's statistics, a faint tick each;
    // the focused shot the dark tick; a left-out shot a small hollow ring at its own place
    function shotVal(s, key) {
        if (key === "lm.carryDistance") return s.carry
        if (key === "lm.offline") return s.offline
        return s.shotId === focusId && has(key) ? num(key) : shotV(s, key)
    }
    function stripFor(key) {
        const st = statsFor(key)
        if (!st || !st.hasSpread || !(st.sd > 0) || !isFinite(st.mean)) return null
        const pts = []
        for (const s of scopedAll) { const v = shotVal(s, key); if (isFinite(v)) pts.push({ id: s.shotId, f: (Math.max(-3, Math.min(3, (v - st.mean) / st.sd)) + 3) / 6, out: s.out, focus: s.shotId === focusId }) }
        return pts.length ? { key: key, pts: pts } : null
    }
    // the ball half's consistency tiles: carry, its range, spray, ball speed, launch, spin
    readonly property var statTiles: {
        const c = rangeOf("carry"), o = rangeOf("offline")
        return [
            sesTile("lm.carryDistance", "Avg carry"),
            { key: "s2", label: "Carry range", value: c ? ts.num(c.hi - c.lo) : "-", unit: "yd", footL: c ? ts.num(c.lo) : "", footR: c ? ts.num(c.hi) : "", out: false, strip: stripFor("lm.carryDistance") },
            { key: "s4", label: "Spray", value: o ? ts.num(o.hi - o.lo) : "-", unit: "yd", footL: o ? lrWord(o.lo) : "", footR: o ? lrWord(o.hi) : "", out: false, strip: stripFor("lm.offline") },
            sesTile("lm.ballSpeed", "Avg ball speed"),
            sesTile("lm.launchAngle", "Avg launch"),
            sesTile("lm.spinRate", "Avg spin")
        ]
    }
    // the club half's: delivery and strike, avg ± SD; low point only when the device measured it
    readonly property var clubTiles: {
        const t = [sesTile("lm.clubheadSpeed", "Club speed"), sesTile("lm.clubPath", "Path"), sesTile("lm.faceAngle", "Face"),
                   sesTile("lm.faceToPath", "Face to path"), sesTile("lm.attackAngle", "Attack"), sesTile("lm.dynamicLoft", "Dyn. loft"),
                   sesTile("lm.smashFactor", "Smash"), sesTile("lm.lieAngle", "Lie")]
        if (statsFor("lm.lowPointAhead")) t.push(sesTile("lm.lowPointAhead", "Low point"))
        return t
    }
    readonly property string statKey: "Footers: shots · ±1 SD under an average; shortest · longest, or furthest left · right, under a range. Strips: every shot on ±3 SD, the band ±1 SD, the dark tick shot " + shotNo + "."
    readonly property string clubKeyText: "Footers: shots · ±1 SD. Strips: every shot, the dark tick shot " + shotNo + "." + (statsFor("lm.lowPointAhead") ? "" : " Low point: not measured by the device, so no spread.")
    readonly property var strikeEll: { const e = focusClub && focusClub.ellipses ? focusClub.ellipses.strike : null; return ellOk(e) ? e : null }
    readonly property string mapCaption: {
        const n = scopedShots.filter(s => isFinite(shotV(s, "lm.strikeLocation")) && isFinite(shotV(s, "lm.strikeHeight"))).length
        return "Impact: the " + n + " " + (focusClub ? focusClub.name : "") + " strikes the device located, from "
             + (focusIron ? "Foresight's centre (between score lines 6 and 7)" : "the face centre")
             + (strikeEll ? "; ellipse: all " + n : "") + "; ring: shot " + shotNo + "." + (markerKind ? " Dots: the " + deviceName + "'s club markers." : "")
             + " Path × face: one dot per shot; the diagonal is face to path 0."
    }

    // ═══ SCREEN ═══
    Item {
        id: page
        visible: root.medium !== "print"
        width: root.width / root.zoom; height: root.height / root.zoom
        scale: root.zoom; transformOrigin: Item.TopLeft

        readonly property real pw: width
        readonly property string cls: ts.sizeClass(pw)
        readonly property real gut: ts.gutter(pw)
        readonly property bool replay: root.register === "Replay"
        readonly property bool session: root.curTab === "session"
        readonly property bool showTabs: !root.isCast || root.castInteractive
        // round 4b: no shot list (the app's session view navigates); the panel has the whole width
        readonly property real mainW: pw
        // round 4b: on a SHORT window, when the eight tiles would take two rows, the tier-3 tiles (club speed, smash,
        // total, offline) fold into the also line, so the club figures keep their room (tier 1 is the glance)
        readonly property bool shortWin: height / root.em < root.lr.shortWinEm && rowsFor(root.heroTiles.length, mainW) > 1
        readonly property var tileDefs: root.heroTiles.filter(d => ts.shows(d.tier, pw) && !(shortWin && d.tier > 1))
        readonly property var foldedDefs: root.heroTiles.filter(d => !(ts.shows(d.tier, pw) && !(shortWin && d.tier > 1)))
        function rowsFor(n, w) { const fit = ts.columns(w, root.lr.tileMinEm, root.tileGap)
                                 const c = Math.max(1, fit >= n ? n : fit >= Math.ceil(n / 2) ? Math.ceil(n / 2) : Math.ceil(n / 4)); return Math.ceil(n / c) }
        readonly property real chromeH: (showTabs ? tabsTop.height : 0) + headRow.height + (notesBar.visible ? notesBar.height + gut * 0.6 : 0) + gut * 1.2
        // round 4b, This shot: the tiles first (the glance, always in view), then the club & impact figures in the height
        // left, then the flight in whatever room the club leaves (shotLayout)
        readonly property real tilesH: rowsFor(tileDefs.length, mainW) * (root.tileH + root.tileGap) + (foldedDefs.length || (replay && root.glanceExtra.length) ? root.labLH * 1.6 : 0)
        readonly property real shotFigH: height - chromeH - tilesH - gut * 1.6
        // Session: the figure row (dispersion, launch × spin | impact map, path × face) takes the height left
        // over the half's head, the club key and two rows of tiles (rule 12), inside its minimum and maximum
        readonly property real sesFigH: Math.max(root.lr.sesFigMinEm * root.em, Math.min(root.lr.sesFigMaxEm * root.em,
                                        height - chromeH - root.labLH * 4.6 - 2 * (root.tileH + root.tileGap) - gut * 3))

        Flickable {
            id: flick
            anchors.fill: parent
            contentWidth: width; contentHeight: col.height + page.gut
            clip: true
            interactive: contentHeight > height + 1
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: flick.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

            Column {
                id: col
                width: page.pw
                spacing: page.gut * 0.6

                PpTabs {
                    id: tabsTop
                    visible: page.showTabs
                    width: parent.width
                    roles: ts
                    tabs: [{ key: "shot", label: "This shot" }, { key: "session", label: "Session" }]
                    current: root.curTab
                    interactive: root.medium === "screen" && (!root.isCast || root.castInteractive)
                    onTabActivated: key => root.tab = key
                }

                // ── the head: the answer, then its context (on an auto-closing cast, the running head) ──
                Item {
                    id: headRow
                    width: parent.width
                    height: headFlow.height + ts.u(4)
                    Flow {
                        id: headFlow
                        width: parent.width - (castView.visible ? castView.width + ts.u(12) : 0)
                        spacing: ts.u(12)
                        Text { id: headT; text: !root.hasData ? "" : page.session ? root.sessionHead : root.headLine; font: ts.fBodyM; color: ts.ink; visible: text.length > 0 }
                        Text { visible: root.hasData; textFormat: Text.RichText
                               text: page.session ? ts.esc(root.sessionCtx)
                                     : ts.esc(root.scopeLine) + "  ·  " + (root.outKeys.length ? ts.toneHtml(ts.esc(root.corridorLine), ts.warn) : ts.esc(root.corridorLine))
                               font: ts.fMicro; color: ts.ink3; height: headT.height; verticalAlignment: Text.AlignVCenter
                               width: Math.min(implicitWidth, headFlow.width); wrapMode: Text.WordWrap }
                        Text { visible: !root.hasData; text: root.emptyText || "No launch monitor readings in this session"; font: ts.fBody; color: ts.ink3 }
                    }
                    Text { id: castView; visible: root.isCast && !page.showTabs && root.hasData; anchors.right: parent.right; text: page.session ? "Session" : "This shot"; font: ts.fMicro; color: ts.ink2 }
                    Rectangle { visible: root.isCast && !page.showTabs; anchors.bottom: parent.bottom; width: parent.width; height: ts.hw; color: ts.rule }
                }

                // ── round 4: the quiet notes (club fixes, mismatches, shots left out), each with its inline action ──
                NotesBar { id: notesBar; visible: root.hasData && root.noteItems.length > 0; width: parent.width; h: root }

                // ── the body: the main column (round 4b: no shot list) ──
                Item {
                    visible: root.hasData
                    width: parent.width
                    height: page.session ? sesCol.height : shotCol.height

                    // ═══ THIS SHOT ═══ (round 4b) the tiles first, so the glance is in view without scrolling; then the FSX
                    // club & impact figures, which never fold while they fit at their least size; the flight takes the rest
                    Column {
                        id: shotCol
                        visible: !page.session
                        width: page.mainW
                        spacing: page.gut * 0.8
                        TileBlock { width: parent.width; defs: page.tileDefs; folded: page.foldedDefs; showAlso: true; h: root
                                    extra: page.replay ? root.glanceExtra : [] }
                        ShotFigures { visible: !root.isCast; width: parent.width; availH: page.shotFigH; h: root }
                        // the auto-closing cast's glance: the tiles and the flight only
                        FlightFigure {
                            visible: root.isCast
                            width: parent.width; mode: "shot"; h: root
                            figH: naturalH(width) - captionH
                        }
                    }

                    // ═══ SESSION ═══ two halves, ball and club; side by side from lr.halvesEm, else stacked (ball first)
                    Column {
                        id: sesCol
                        visible: page.session
                        width: page.mainW
                        spacing: page.gut * 0.8
                        Item {
                            id: halves
                            width: parent.width
                            readonly property bool side: width / root.em >= root.lr.halvesEm
                            readonly property real hw: side ? (width - page.gut * 1.5) / 2 : width
                            height: side ? Math.max(ballH.height, clubH.height) : ballH.height + page.gut * 1.2 + clubH.height
                            BallHalf { id: ballH; width: halves.hw; figH: page.sesFigH; h: root }
                            Rectangle { visible: halves.side; x: halves.hw + page.gut * 0.75; width: ts.hw; height: parent.height; color: ts.hair }
                            ClubHalf { id: clubH; x: halves.side ? halves.hw + page.gut * 1.5 : 0; y: halves.side ? 0 : ballH.height + page.gut * 1.2
                                       width: halves.hw; figH: page.sesFigH; h: root }
                        }
                    }
                }

                // ── below the fold ──
                Item { visible: root.hasData && (!page.replay || page.session); width: 1; height: page.gut * 0.4 }
                ClubSummary { visible: root.hasData && page.session && !root.isCast; width: parent.width; h: root }
                LedgerBlock { visible: root.hasData && page.session && !page.replay; width: parent.width; h: root }
                LedgerBlock { visible: !page.replay && root.hasData && !page.session; width: parent.width; h: root; indexMode: true }
                PpNotes {
                    visible: !page.replay && root.hasData
                    width: parent.width; roles: ts; small: false
                    text: page.session ? root.sessionNotes : root.shotNotes
                }
            }
            // the hover card's layer: over the column, in its coordinates (the pickers map their points into it)
            Item {
                id: cardLayer
                width: col.width; height: col.height
                z: 50
                HoverCard { h: root }
            }
        }
    }

    readonly property Item cardLayerItem: cardLayer
    readonly property string sessionHead: focusClub ? focusClub.name + ", " + ts.word(scopeN) + " shots" : ""
    readonly property string sessionCtx: shots.length + " shots in the session  ·  " + clubs.map(c => c.name + " " + c.shots.length).join(", ")
                                         + "  ·  shot " + shotNo + " ringed"
    readonly property string shotNotes:
        "Every reading the " + (root.deviceName || "device") + " reported for shot " + shotNo + ". Tiles: the big figure is this shot; the band under it is the "
        + (focusClub ? focusClub.name : "") + " session average (left) and ±1 SD (right). " + ts.cap1(ts.word(gradedN)) + " of the " + readingN
        + " readings carry a corridor; a reading outside it is set in warn with the word, and inside looks like no corridor at all. "
        + "† measured, as the device labels it. The shot shape and the strike are PinPoint's inferred reads, not the device's. "
        + "Path and Spin draw angles ×" + lr.angleGain + "; Loft is true. " + patchLine + " Session holds every shot, the spreads, the dispersion and the impact map."
    readonly property string sessionNotes:
        "The " + scopeN + " " + (focusClub ? focusClub.name : "") + " shots in scope, against which shot " + shotNo + " is read: the ball half (where it landed, how it launched) and the club half (where it struck, how it was delivered). The strip runs ±3 SD; the shaded band is ±1 SD and the tick is shot "
        + shotNo + ". Each ellipse is the smallest that holds every shot of its club (none under three shots), drawn by PinPoint from the shots; other clubs are ghosts, dots and ellipse. "
        + "The dispersion's distance axis starts at the first arc that holds a landing; the tee is behind the break."

    // ═══ PRINT ═══
    Item {
        id: paper
        visible: root.medium === "print"
        anchors.fill: parent
        readonly property real mx: ts.p.marginX
        readonly property real textW: width - 2 * mx
        readonly property var blocks: root.printPlan[Math.min(root.printPage, root.printPlan.length - 1)] || []
        function on(b) { return blocks.indexOf(b) >= 0 }
        readonly property var readout: root.focusShot ? { shotIndex: root.shotNo - 1, shotCount: root.shots.length, club: root.clubName(root.focusShot.club) } : null
        readonly property var source: root.focusShot ? { sessionDir: root.swingDirOf(root.focusShot.shotId) } : null
        readonly property real half: (textW - ts.gutter(textW) * 1.5) / 2
        Rectangle { anchors.fill: parent; color: "#FFFFFF" }
        PpRunningHead {
            x: paper.mx; y: ts.p.headY; width: paper.textW
            roles: ts; source: paper.source; readout: paper.readout
            title: "PinPoint Studio  ·  Launch monitor"
        }
        Column {
            x: paper.mx; y: ts.p.top; width: paper.textW
            spacing: ts.u(14)
            // ── This shot ──
            Column {
                visible: paper.on("mast"); width: parent.width; spacing: ts.u(6)
                Text { text: "This shot"; font: ts.fKicker; color: ts.ink3 }
                Text { width: parent.width; wrapMode: Text.WordWrap; text: root.mastHead; font: ts.fTitle; color: ts.ink }
                Text { width: Math.min(parent.width, ts.flows.measure * ts.em * 1.25); wrapMode: Text.WordWrap; textFormat: Text.RichText
                       text: root.standfirst; font: ts.fStand; color: ts.ink2; lineHeight: 1.08 }
                Text { visible: root.printNotes.length > 0; width: parent.width; wrapMode: Text.WordWrap; text: root.printNotes; font: ts.fMicro; color: ts.ink3 }
            }
            Column {
                visible: paper.on("tiles"); width: parent.width; spacing: ts.u(5)
                FigHead { width: parent.width; h: root; n: 1; title: "Shot " + root.shotNo; sub: "under each figure: the " + (root.focusClub ? root.focusClub.name : "") + " session average · ±1 SD" }
                TileBlock { width: parent.width; defs: root.heroTiles; folded: []; h: root }
            }
            Column {
                visible: paper.on("club"); width: parent.width; spacing: ts.u(5)
                FigHead { width: parent.width; h: root; n: 2; title: "Club and impact"; sub: "Path, Spin, Loft and Impact, in FSX's quadrants" }
                ClubQuad { h: root; arrangement: "quad"; gap: ts.gutter(parent.width); cw: Math.floor((parent.width - gap) / 2); ch: Math.floor(cw / 1.62) }
                Text { width: parent.width; wrapMode: Text.WordWrap; text: "Path and Spin views ×" + root.lr.angleGain + "; Loft true. " + root.lowPointLine + " " + root.patchShort; font: ts.fMicro; color: ts.ink3 }
            }
            Column {
                visible: paper.on("flight"); width: parent.width; spacing: ts.u(5)
                FigHead { width: parent.width; h: root; n: 3; title: "Flight"; sub: "from above and from the side, one scale on both axes" }
                FlightFigure { width: parent.width; figH: naturalH(width) - captionH; mode: "shot"; h: root }
            }
            Column {
                visible: paper.on("figs"); width: parent.width; spacing: ts.u(5)
                FigHead { width: parent.width; h: root; n: 2; title: "Club and impact · flight"; sub: "Path, Spin, Loft, Impact in FSX's quadrants; the flight beside, one scale on both axes" }
                Item {
                    id: prFig
                    width: parent.width
                    readonly property real g2: ts.gutter(width)
                    readonly property real qcw: Math.floor(width * 0.27)
                    readonly property real qch: Math.floor(qcw / 1.62)
                    height: Math.max(prQ.height, prFl.height)
                    ClubQuad { id: prQ; h: root; arrangement: "quad"; gap: prFig.g2; cw: prFig.qcw; ch: prFig.qch }
                    FlightFigure { id: prFl; x: prQ.width + prFig.g2; width: prFig.width - x; figH: Math.min(naturalH(width), prQ.height) - captionH; mode: "shot"; h: root }
                }
                Text { width: parent.width; wrapMode: Text.WordWrap; text: "Path and Spin views ×" + root.lr.angleGain + "; Loft true. " + root.lowPointLine + " " + root.patchShort; font: ts.fMicro; color: ts.ink3 }
            }
            Column {
                visible: paper.on("index"); width: parent.width; spacing: ts.u(6)
                FigHead { width: parent.width; h: root; n: 1; table: true; title: "Every reading on shot " + root.shotNo; sub: root.readingN + " readings in five groups" }
                LedgerBlock { width: parent.width; h: root; bare: true; indexMode: true }
            }
            // ── Session ──
            Column {
                visible: paper.on("sesHead"); width: parent.width; spacing: ts.u(6)
                Text { text: "Session"; font: ts.fKicker; color: ts.ink3 }
                Text { width: parent.width; wrapMode: Text.WordWrap; text: root.sessionMast; font: ts.fTitle; color: ts.ink }
            }
            // round 2: the Session's two halves, as figures 4 (ball) and 5 (club)
            Column {
                visible: paper.on("ballHalf"); width: parent.width; spacing: ts.u(5)
                FigHead { width: parent.width; h: root; n: 4; title: "Ball"; sub: "dispersion, launch × spin and consistency · " + (root.focusClub ? root.focusClub.name + ", " + root.scopeN + " shots" : "") }
                BallHalf { width: parent.width; figH: ts.u(190); bare: true; h: root }
            }
            Column {
                visible: paper.on("clubHalf"); width: parent.width; spacing: ts.u(5)
                FigHead { width: parent.width; h: root; n: 5; title: "Club"; sub: "impact, path × face and consistency · " + (root.focusClub ? root.focusClub.name + ", " + root.scopeN + " shots" : "") }
                ClubHalf { width: parent.width; figH: ts.u(210); bare: true; h: root }
            }
            Row {
                visible: paper.on("halves"); width: parent.width; spacing: ts.gutter(paper.textW) * 1.5
                Column {
                    width: paper.half; spacing: ts.u(5)
                    FigHead { width: parent.width; h: root; n: 4; title: "Ball"; sub: "dispersion · launch × spin · consistency" }
                    BallHalf { width: parent.width; figH: ts.u(132); bare: true; h: root }
                }
                Column {
                    width: paper.half; spacing: ts.u(5)
                    FigHead { width: parent.width; h: root; n: 5; title: "Club"; sub: "impact · path × face · consistency" }
                    ClubHalf { width: parent.width; figH: ts.u(132); bare: true; h: root }
                }
            }
            Column {
                visible: paper.on("clubsum"); width: parent.width; spacing: ts.u(5)
                Item { width: 1; height: ts.u(4) }
                ClubSummary { width: parent.width; h: root }
            }
            Column {
                visible: paper.on("stats"); width: parent.width; spacing: ts.u(5)
                FigHead { width: parent.width; h: root; n: 4; title: "Consistency"; sub: root.focusClub ? root.focusClub.name + ", " + root.scopeN + " shots" : "" }
                TileBlock { width: parent.width; defs: root.statTiles; folded: []; h: root }
                Text { width: parent.width; wrapMode: Text.WordWrap; text: root.statKey; font: ts.fMicro; color: ts.ink3 }
            }
            Column {
                visible: paper.on("disp"); width: parent.width; spacing: ts.u(5)
                FigHead { width: parent.width; h: root; n: 5; title: "Dispersion"; sub: "every landing; each ellipse holds all its club's shots" }
                ClubKey { width: parent.width; h: root }
                FlightFigure { width: parent.width; figH: Math.min(naturalH(width), ts.u(300)); mode: "session"; h: root }
                Item { width: 1; height: ts.u(4) }
                ClubSummary { width: parent.width; h: root }
            }
            Row {
                visible: paper.on("statsDisp"); width: parent.width; spacing: ts.gutter(paper.textW) * 1.5
                Column {
                    width: paper.half; spacing: ts.u(5)
                    FigHead { width: parent.width; h: root; n: 4; title: "Dispersion"; sub: "every landing; each ellipse holds all its club's shots" }
                    ClubKey { width: parent.width; h: root }
                    FlightFigure { width: parent.width; figH: ts.u(250); mode: "session"; h: root }
                }
                Column {
                    width: paper.half; spacing: ts.u(5)
                    FigHead { width: parent.width; h: root; n: 5; title: "Consistency"; sub: root.focusClub ? root.focusClub.name + ", " + root.scopeN + " shots" : "" }
                    TileBlock { width: parent.width; defs: root.statTiles; folded: []; h: root }
                    Text { width: parent.width; wrapMode: Text.WordWrap; text: root.statKey; font: ts.fMicro; color: ts.ink3 }
                    Item { width: 1; height: ts.u(8) }
                    ClubSummary { width: parent.width; h: root }
                }
            }
            Column {
                visible: paper.on("ledger"); width: parent.width; spacing: ts.u(6)
                FigHead { width: parent.width; h: root; n: 1; table: true; title: "Every reading, against the session"; sub: root.readingN + " readings; shot " + root.shotNo + " is the tick" }
                LedgerBlock { width: parent.width; h: root; bare: true }
            }
            Column {
                visible: paper.on("shots"); width: parent.width; spacing: ts.u(6)
                FigHead { width: parent.width; h: root; n: 2; table: true; title: "The session's shots"; sub: root.shots.length + " shots, grouped by club; shot " + root.shotNo + " shaded" }
                ShotTable { width: parent.width; h: root }
            }
            PpNotes { visible: paper.on("notes"); width: parent.width; roles: ts; text: root.sessionNotes + " " + root.printNotes + " Table 2 sets each shot's readings as the device reported them (* club relabelled; out: left out); its Avg and ± SD rows are of the " + (root.focusClub ? root.focusClub.name : "") + " shots not left out." }
            PpNotes { visible: paper.on("shotnotes"); width: parent.width; roles: ts; text: root.shotNotes }
        }
        PpFolio {
            x: paper.mx; width: paper.textW
            y: paper.height - ts.u(14) - height
            roles: ts; readout: paper.readout
            footer: root.board_reviewNote
            section: paper.on("mast") ? "This shot" : paper.on("flight") ? "This shot · Session" : "Session"
            page: root.printPage + 1; pageCount: root.printPageCount
            keyKinds: []
        }
    }
    readonly property string sessionMast: !focusClub ? "" : focusClub.name + ": " + meanTxt("lm.carryDistance") + " yd average carry, "
                                          + (rangeOf("carry") ? ts.num(rangeOf("carry").hi - rangeOf("carry").lo) : "-") + " yd from shortest to longest"
    readonly property string board_reviewNote: (reviewing === true ? "Reviewed session" : "Session") + "  ·  " + scopeLine
    readonly property string mastHead: !focusShot ? "" : "Shot " + shotNo + ": " + txt("lm.carryDistance") + " yd carry"
                                       + (shape.has ? ", a " + String(shape.name).toLowerCase() : "")
    readonly property string standfirst: !focusShot ? "" :
        ts.cap1(clubName(focusShot.club)) + ", " + ts.figHtml(txt("lm.ballSpeed") + " mph") + " ball speed from " + ts.figHtml(txt("lm.clubheadSpeed") + " mph")
        + " (smash " + ts.figHtml(txt("lm.smashFactor")) + "), launched at " + ts.figHtml(txt("lm.launchAngle") + "°") + " with " + ts.figHtml(txt("lm.spinRate") + " rpm")
        + ". It started " + ts.figHtml(dirParts("lm.launchDirection", txt("lm.launchDirection"), "°").num) + (num("lm.launchDirection") >= 0 ? " right" : " left")
        + " and landed " + ts.figHtml(dirParts("lm.offline", txt("lm.offline"), " yd").num) + (num("lm.offline") >= 0 ? " right" : " left")
        + " of the line. " + ts.cap1(corridorLine) + "."

    // ═══════════════════════════════════════════════════════════════════════════════════════════════
    // BLOCKS (inline components). Each takes `h` (this panel) and lays itself out by the width it is given.
    // ═══════════════════════════════════════════════════════════════════════════════════════════════

    // a figure / table head: "Fig. 2  Flight and dispersion   sub"
    component FigHead: Column {
        id: fh
        property var h
        property int n: 1
        property bool table: false
        property string title: ""
        property string sub: ""
        spacing: h.ts.u(4)
        Rectangle { width: parent.width; height: fh.h.ts.r.figureRuleTop * (fh.h.ts.isPrint ? 0.5 : 1); color: fh.h.ts.rule }
        Flow {
            width: parent.width; spacing: fh.h.ts.u(8)
            Text { id: fhw; text: (fh.table ? "Table" : fh.h.ts.r.exhibitWord) + " " + fh.n; font: fh.h.ts.fMicro; color: fh.h.ts.ink3; height: fht.height; verticalAlignment: Text.AlignVCenter }
            Text { id: fht; text: fh.title; font: fh.h.ts.fBodyM; color: fh.h.ts.ink }
            Text { visible: fh.sub.length > 0; text: fh.sub; font: fh.h.ts.fMicro; color: fh.h.ts.ink3; height: fht.height; verticalAlignment: Text.AlignVCenter }
        }
    }

    // ONE tile: label above (small caps), the figure, the unit muted below, the AVG · ±σ band
    component Tile: Item {
        id: tl
        property var h
        property var d: ({})
        readonly property bool out: d.out === true
        // a reading a gate dropped: its words, quiet, where the figure would be (a click opens the reason)
        readonly property bool gated: !!d.gate && !!d.gate.words
        signal gateClicked(string subject)
        height: h.tileH + (d.strip ? h.stripRowH : 0)
        Rectangle {
            anchors.fill: parent
            color: tl.h.ts.isPrint ? "#FFFFFF" : Theme.colorSurface
            border.width: tl.h.ts.hw; border.color: tl.h.tileBorder
            radius: tl.h.tileRadius
        }
        Text {
            id: tLab
            y: tl.h.ts.u(tl.h.lr.tilePad); width: parent.width; horizontalAlignment: Text.AlignHCenter
            text: tl.d.label || ""; font: tl.h.fTileLabel; color: tl.h.ts.ink2
        }
        // the figure; a direction rides beside it (B, round 4): a side letter at the hero size ("2.3 R"), a word as
        // a small-caps qualifier on the figure's baseline ("0.2 DOWN")
        Row {
            id: tValRow
            visible: !tl.gated
            anchors.top: tLab.bottom; anchors.topMargin: tl.h.ts.u(1)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: tl.h.ts.u(5)
            Text { id: tVal; text: tl.d.value || "-"; font: tl.h.fHero; color: tl.out ? tl.h.ts.warn : tl.h.ts.ink }
            Text { visible: (tl.d.dir || "").length === 1; text: tl.d.dir || ""; font: tl.h.fHero; color: tl.out ? tl.h.ts.warn : tl.h.ts.ink; anchors.baseline: tVal.baseline }
            Text { visible: (tl.d.dir || "").length > 1; text: tl.d.dir || ""; font: tl.h.fQual; color: tl.out ? tl.h.ts.warn : tl.h.ts.ink2; anchors.baseline: tVal.baseline }
        }
        Row {
            anchors.top: tValRow.bottom; anchors.topMargin: 0
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: tl.h.ts.u(5)
            Text { text: tl.d.unit || ""; font: tl.h.fTileLabel; color: tl.h.ts.ink3; visible: text.length > 0 && !tl.gated }
            Text { visible: tl.out; text: "outside"; font: tl.h.fTileLabel; color: tl.h.ts.warn }
        }
        Text {
            objectName: "lmTileGate"
            visible: tl.gated
            anchors.top: tLab.bottom; anchors.topMargin: tl.h.ts.u(6)
            x: tl.h.ts.u(6); width: parent.width - 2 * x
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
            textFormat: Text.StyledText
            font: tl.h.ts.fFigLabel; color: tl.h.ts.ink3
            text: !tl.gated ? "" : (tl.h.ts.isPrint || !tl.h.canAct) ? tl.d.gate.words
                                 : "<font color='" + tl.h.ts.link + "'>" + tl.d.gate.words + " ›</font>"
            MouseArea {
                anchors.fill: parent; anchors.margins: -2
                enabled: tl.gated && !tl.h.ts.isPrint && tl.h.canAct
                cursorShape: Qt.PointingHandCursor
                onClicked: tl.gateClicked(tl.d.gate.subject)
            }
        }
        // Session (round 4, from B): every scoped shot on the tile's strip; clicking a tick focuses that shot
        StripTicks { visible: !!tl.d.strip; h: tl.h; strip: tl.d.strip || null
                     x: tl.h.ts.u(10); width: parent.width - 2 * x
                     y: foot.y - tl.h.ts.u(tl.h.lr.stripGap) - height; height: tl.h.ts.u(tl.h.lr.sesStripH) }
        Rectangle {
            id: foot
            x: tl.h.ts.hw; width: parent.width - 2 * tl.h.ts.hw
            anchors.bottom: parent.bottom; anchors.bottomMargin: tl.h.ts.hw
            height: tl.h.footH
            radius: Math.max(0, tl.h.tileRadius - 1)
            color: Qt.alpha(tl.h.ts.ink, tl.h.lr.tileFootAlpha * (tl.h.ts.isPrint ? 1.2 : 1))
            Rectangle { width: parent.width; height: Math.min(parent.height, parent.radius + 1); color: parent.color; visible: parent.radius > 0 }
            Rectangle { width: parent.width; height: tl.h.ts.hw; color: tl.h.tileBorder }
            Text { x: tl.h.ts.u(7); anchors.verticalCenter: parent.verticalCenter; text: tl.d.footL || ""; font: tl.h.ts.fFigLabel; color: tl.h.ts.ink2 }
            Text { anchors.right: parent.right; anchors.rightMargin: tl.h.ts.u(7); anchors.verticalCenter: parent.verticalCenter
                   text: tl.d.footR || ""; font: tl.h.ts.fFigLabel; color: tl.h.ts.ink2 }
        }
    }

    // the strip of equal tiles: n → n/2 → n/4 per row (never a ragged row); the folded tiles as one line
    component TileBlock: Column {
        id: tb
        property var h
        property var defs: []
        property var folded: []
        property bool showAlso: false
        property var extra: []                       // readings added to the "also" line (the glance: closure, peak, descent)
        property string inlineSubject: ""            // a gated tile's reason, open under the block (Trust panel off)
        readonly property var alsoList: folded.concat(extra)
        readonly property int n: defs.length
        readonly property int fit: h.ts.columns(width, h.lr.tileMinEm, h.tileGap)
        readonly property int cols: Math.max(1, fit >= n ? n : fit >= Math.ceil(n / 2) ? Math.ceil(n / 2) : Math.ceil(n / 4))
        readonly property real cw: (width - (cols - 1) * h.tileGap) / cols
        spacing: h.ts.u(5)
        Item {
            width: parent.width
            readonly property real rowH: tb.h.tileH + (tb.defs.some(d => !!d.strip) ? tb.h.stripRowH : 0)
            height: Math.ceil(tb.n / tb.cols) * (rowH + tb.h.tileGap) - tb.h.tileGap
            Repeater {
                model: tb.defs
                delegate: Tile {
                    required property var modelData
                    required property int index
                    h: tb.h
                    x: (index % tb.cols) * (tb.cw + tb.h.tileGap)
                    y: Math.floor(index / tb.cols) * (parent.rowH + tb.h.tileGap)
                    width: tb.cw
                    d: modelData
                    onGateClicked: (subject) => {
                        if (!TrustFocus.open(subject, tb.h.gateSwingDir))
                            tb.inlineSubject = tb.inlineSubject === subject ? "" : subject
                    }
                }
            }
        }
        Text {
            visible: tb.showAlso && tb.alsoList.length > 0
            width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.RichText
            text: tb.alsoList.map(d => (d.gate && d.gate.words) ? tb.h.ts.esc(d.label).replace(/ /g, "&nbsp;") + "&nbsp;" + tb.h.ts.esc(d.gate.words) :
                                     tb.h.ts.esc(d.label).replace(/ /g, "&nbsp;") + "&nbsp;" + tb.h.ts.figHtml(tb.h.isDir(d.key) ? tb.h.dirParts(d.key, tb.h.txt(d.key), tb.h.unitOf(d.key) === "°" ? "°" : "").num : tb.h.figU(d.key), tb.h.isOut(d.key) ? tb.h.ts.warn : tb.h.ts.ink2)
                                     + (tb.h.unitOf(d.key) === "°" ? "" : "&nbsp;" + tb.h.unitWord(tb.h.unitOf(d.key)))
                                     + (tb.h.isDir(d.key) && tb.h.dirParts(d.key, tb.h.txt(d.key)).dir ? "&nbsp;" + tb.h.ts.esc(tb.h.dirParts(d.key, tb.h.txt(d.key)).dir) : "")
                                     + (tb.h.isOut(d.key) ? " " + tb.h.ts.toneHtml("outside", tb.h.ts.warn) : "")).join("&nbsp;&nbsp; · &nbsp;&nbsp;")
            font: tb.h.ts.fMicro; color: tb.h.ts.ink3
        }
        PpTrustInlineFor {
            width: parent.width
            roles: tb.h.ts
            subject: tb.inlineSubject
            swingDir: tb.h.gateSwingDir
            onClosed: tb.inlineSubject = ""
        }
    }

    // the club key: one hue per club (FSX's stripe), the scoped club solid, the others as ghosts
    component ClubKey: Flow {
        id: ck
        property var h
        spacing: h.ts.u(16)
        Repeater {
            model: ck.h.clubs
            delegate: Item {
              id: ckw
              required property var modelData
              width: ckr.width; height: ckr.height
              // round 4: a club in the key becomes the Session scope; the others turn ghosts (dots and ellipse)
              MouseArea { objectName: "lmClubKey"; x: -ck.h.ts.u(4); width: ckr.width + ck.h.ts.u(8); y: (ckr.height - height) / 2; height: Math.max(ckr.height, Theme.sp(ck.h.lr.hitPx))
                          enabled: ck.h.canAct; cursorShape: Qt.PointingHandCursor; onClicked: ck.h.scopeKey = ckw.modelData.key }
              Row {
                id: ckr
                readonly property var modelData: ckw.modelData
                spacing: ck.h.ts.u(6)
                opacity: modelData.key === ck.h.focusKey ? 1 : 0.75
                Rectangle { width: ck.h.ts.u(3); height: kt.height; color: modelData.hue; opacity: modelData.key === ck.h.focusKey ? 1 : ck.h.lr.ghostAlpha + 0.2 }
                Text { id: kt; textFormat: Text.RichText; font: ck.h.ts.fMicro; color: ck.h.ts.ink2
                       text: "<span style=\"color:" + ck.h.ts.ink + ";\">" + ck.h.ts.esc(modelData.name) + "</span>  " + modelData.shots.length + " shots"
                             + (modelData.inc.length < modelData.shots.length ? " (" + (modelData.shots.length - modelData.inc.length) + " left out)" : "")
                             + (modelData.ell ? "  ·  ellipse: all " + modelData.ell.n : "") + (modelData.key === ck.h.focusKey ? "" : "  ·  ghost") }
              }
            }
        }
        Row {
            spacing: ck.h.ts.u(6)
            Item { width: ck.h.ts.ringD; height: kt2.height
                   Rectangle { anchors.centerIn: parent; width: ck.h.ts.ringD; height: width; radius: width / 2; color: "transparent"; border.width: 1; border.color: ck.h.ts.ink } }
            Text { id: kt2; text: "shot " + ck.h.shotNo; font: ck.h.ts.fMicro; color: ck.h.ts.ink2 }
        }
    }

    // the clubs side by side: shots, average carry, carry range, spray (FSX's per-club averages, set as a table).
    // The scoped club's average is the board's; the other clubs' figures are their shots' own (per-shot geometry).
    component ClubSummary: Column {
        id: cs
        property var h
        readonly property real c1: width * 0.28
        readonly property real cw: (width - c1) / 4
        readonly property real rowH: Math.ceil(h.ts.fFig.pixelSize * 1.7)
        spacing: 0
        Item {
            width: parent.width; height: cs.h.labLH + cs.h.ts.u(4)
            Text { text: "Club"; font: cs.h.fTileLabel; color: cs.h.ts.ink3 }
            Repeater { model: ["Shots", "Avg carry", "Range", "Spray"]
                delegate: Text { required property string modelData; required property int index
                                 x: cs.c1 + (index + 1) * cs.cw - width; text: modelData; font: cs.h.fTileLabel; color: cs.h.ts.ink3 } }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: cs.h.ts.hw; color: cs.h.ts.rule }
        }
        Repeater {
            model: cs.h.clubs
            delegate: Item {
                id: cr
                required property var modelData
                readonly property var rc: cs.h.rangeOf("carry", modelData)
                readonly property var ro: cs.h.rangeOf("offline", modelData)
                readonly property var cells: [String(modelData.inc.length),
                    cs.h.clubMeanTxt(modelData, "lm.carryDistance") + " yd",
                    rc ? cs.h.ts.num(rc.lo) + "–" + cs.h.ts.num(rc.hi) : "-",
                    ro ? cs.h.ts.num(ro.hi - ro.lo) + " yd" : "-"]
                width: cs.width; height: cs.rowH
                Rectangle { y: cs.h.ts.u(3); width: cs.h.ts.u(3); height: parent.height - cs.h.ts.u(6); color: cr.modelData.hue
                            opacity: cr.modelData.key === cs.h.focusKey ? 1 : cs.h.lr.ghostAlpha + 0.25 }
                Text { x: cs.h.ts.u(9); anchors.verticalCenter: parent.verticalCenter; text: cr.modelData.name; font: cs.h.ts.fBody; color: cs.h.ts.ink }
                Repeater { model: cr.cells
                    delegate: Text { required property string modelData; required property int index
                                     x: cs.c1 + (index + 1) * cs.cw - width; anchors.verticalCenter: parent.verticalCenter
                                     text: modelData; font: cs.h.ts.fFig; color: cs.h.ts.ink } }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: cs.h.ts.hw; color: cs.h.ts.hair }
            }
        }
    }

    // a figure annotation: label (small caps) over the value, set beside the geometry it measures
    component Anno: Column {
        id: an
        objectName: "anno"
        property var h
        property string label: ""
        property string value: "-"
        property string dir: ""                      // round 4: the direction (side letter at the figure's size, or a small-caps word)
        property bool warnOn: false
        property bool alignR: false
        property bool quiet: false
        spacing: h.ts.u(h.lr.annoGap)
        // a figure's readings carry a halo of the card's own tone: invisible on the card, legible where a head runs under them
        Text { style: Text.Outline; styleColor: an.h.annoHalo; width: an.width; horizontalAlignment: an.alignR ? Text.AlignRight : Text.AlignLeft; text: an.label; font: an.h.fTileLabel; color: an.warnOn ? an.h.ts.warn : an.h.ts.ink2 }
        Item {
            width: an.width; height: av.height
            Row {
                x: an.alignR ? parent.width - width : 0
                spacing: an.h.ts.u(4)
                Text { style: Text.Outline; styleColor: an.h.annoHalo; id: av; text: an.value; font: an.quiet ? an.h.ts.fFig : an.h.ts.fFigL; color: an.warnOn ? an.h.ts.warn : an.h.ts.ink }
                Text { style: Text.Outline; styleColor: an.h.annoHalo; visible: an.dir.length === 1; text: an.dir; font: av.font; color: av.color; anchors.baseline: av.baseline }
                Text { style: Text.Outline; styleColor: an.h.annoHalo; visible: an.dir.length > 1; text: an.dir; font: an.h.fQual; color: an.warnOn ? an.h.ts.warn : an.h.ts.ink2; anchors.baseline: av.baseline }
            }
        }
        // OUTSIDE ITS CORRIDOR: the word on its own line under the value, as the tiles set it under theirs, in warn
        // with the value. The label never grows, so a flagged reading cannot run into its neighbour.
        Text { style: Text.Outline; styleColor: an.h.annoHalo; visible: an.warnOn; width: an.width
               horizontalAlignment: an.alignR ? Text.AlignRight : Text.AlignLeft; text: "outside"; font: an.h.fTileLabel; color: an.h.ts.warn }
    }

    component ClubCell: Item {
        id: cc
        objectName: kind === "map" ? "lmMap" : "lmCell"
        property var h
        property string kind: "top"
        property string title: ""                    // round 4c: the figure's quiet title (Path, Loft, Spin, Impact)
        readonly property real titleH: title.length ? h.labLH + h.ts.u(6) : 0
        readonly property real aw: width
        readonly property real ah: Math.max(1, height - titleH)
        readonly property var annos: h.annosFor(kind, aw, ah)
        readonly property var pl: h.clubGeom(kind, aw, ah)
        readonly property bool faceish: kind === "faceQ" || kind === "map"
        readonly property var deps: [cc.h.g, cc.h.ts.ink, cc.h.ts.paper, cc.h.focusHue, cc.pl, cc.aw, cc.ah, cc.h.art, cc.h.scopedAll, cc.h.focusId, cc.h.hoverId]
        onDepsChanged: { under.requestPaint(); over.requestPaint() }
        Text { visible: cc.title.length > 0; text: cc.title; font: cc.h.ts.fSmallM; color: cc.h.ts.ink2 }
        Rectangle { visible: cc.title.length > 0; y: cc.titleH - cc.h.ts.u(3); width: parent.width; height: cc.h.ts.hw; color: cc.h.ts.hair }
        Item {
            id: area
            y: cc.titleH; width: cc.aw; height: cc.ah
            Canvas {
                id: under
                anchors.fill: parent
                onPaint: { const ctx = getContext("2d"); ctx.reset(); cc.h.paintClub(ctx, cc.kind, width, height, "under", cc.pl) }
            }
            Item {                      // the head, clipped to its cell
                anchors.fill: parent
                clip: true
                HeadImage { pl: cc.pl && cc.pl.a ? cc.pl : null }
                Repeater {
                    model: cc.pl && cc.pl.balls ? cc.pl.balls : []
                    delegate: BallImage { required property var modelData; b: modelData; h: cc.h }
                }
                FaceInk { visible: cc.faceish; h: cc.h; pl: cc.faceish && cc.pl && cc.pl.a && cc.pl.a.ff ? cc.pl : null; mode: cc.kind === "map" ? "map" : "shot" }
            }
            Canvas {
                id: over
                anchors.fill: parent
                onPaint: { const ctx = getContext("2d"); ctx.reset(); cc.h.paintClub(ctx, cc.kind, width, height, "over", cc.pl) }
            }
            Repeater {
                model: cc.annos
                delegate: Anno {
                    required property var modelData
                    h: cc.h
                    x: modelData.right ? modelData.x - width : modelData.x
                    y: Math.max(0, Math.min(modelData.y, cc.ah - implicitHeight))
                    width: modelData.w
                    label: modelData.label; value: modelData.value; dir: modelData.dir || ""; alignR: !!modelData.right
                    warnOn: !!modelData.warn; quiet: !!modelData.quiet || !!modelData.small
                }
            }
            ShotPicker { visible: cc.kind === "map"; anchors.fill: parent; h: cc.h; src: "map"; pts: cc.kind === "map" && cc.pl ? cc.h.mapPts(cc.pl) : [] }
        }
    }
    // round 4c: the four club figures, ONE size, in FSX's quadrants (image 03: Path top left, Spin top right, Loft bottom
    // left, Impact bottom right), or a row / a column in the owner's order (Path, Loft, Spin, Impact)
    // Impact's face width (px across the body, heel to toe) in a drawing area w × h: between the corner columns, under
    // the top readings (the head sits on the floor)
    // (the whole head, toe to hosel, stands between the corner columns, so it may use the full height)
    function faceQW(w, h) { const cw = lr.faceCornerEm * em
                            return Math.max(20, Math.min((w - 2 * cw - ts.u(8)) / 1.12, (h - ts.u(6)) / 0.74, w * 0.62)) }
    function faceQPxMm(cw, ch) { return faceQW(cw, ch - (labLH + ts.u(6))) / headWidthMm(focusHead, "face") }
    function figTitle(k) { return ({ top: "Path", side: "Loft", axis: "Spin", faceQ: "Impact" })[k] || "" }
    component ClubQuad: Item {
        id: cq
        property var h
        property string arrangement: "quad"          // "quad" | "row" | "col"
        property real cw: 300
        property real ch: 180
        property real gap: 12
        readonly property var order: arrangement === "quad" ? ["top", "axis", "side", "faceQ"] : ["top", "side", "axis", "faceQ"]
        readonly property int cols: arrangement === "quad" ? 2 : arrangement === "row" ? 4 : 1
        readonly property int rows: Math.ceil(4 / cols)
        width: cols * cw + (cols - 1) * gap
        height: rows * ch + (rows - 1) * gap
        Repeater {
            model: cq.order
            delegate: ClubCell {
                required property string modelData
                required property int index
                kind: modelData; title: cq.h.figTitle(modelData); h: cq.h
                x: (index % cq.cols) * (cq.cw + cq.gap); y: Math.floor(index / cq.cols) * (cq.ch + cq.gap)
                width: cq.cw; height: cq.ch
            }
        }
    }

    // ═══ ROUND 4c — THIS SHOT'S FIGURES: four EQUAL club cells, then the flight ═══
    // shotLayout(W, H): W = the panel's width, H = the height left in view under the tiles. One cell size for all four
    // (aspect lr.cellAspectMin–Max, height clubCellMin–MaxEm, width ≥ cellMinEm), the largest that fits:
    //   1. FSX's 2×2 quadrants with the true-scale flight BESIDE them (a wide, short window) when the flight gets
    //      ≥ flightBesideMinEm and the quadrants stay ≥ 90 % of their size with the flight below;
    //   2. else the 2×2 with the flight below (full width, fitted to the height left, or a single plan / side strip);
    //   3. a row of four only when a 2×2 cannot fit in view at the least size and a row can;
    //   4. nothing fits in view: the 2×2 sized by the width (scroll), or one column when the width is too narrow.
    function shotLayout(W, H) {
        const g = ts.gutter(W), amin = lr.cellAspectMin, amax = lr.cellAspectMax
        const cap = Math.ceil(ts.fMicro.pixelSize * 1.5) * (W / em < 70 ? 3 : 2) + ts.u(6)
        const cmin = lr.clubCellMinEm * em, cmax = lr.clubCellMaxEm * em, minW = lr.cellMinEm * em
        const flCap = labLH * 1.5, flMinW = lr.flightBesideMinEm * em, kMin = lr.flightMinK
        const sc = flightScale("shot")
        function fit(cols, rows, Wc, Hc) {
            const ch = Math.floor(Math.min(cmax, (Hc - cap - (rows - 1) * g) / rows, (Wc - (cols - 1) * g) / (cols * amin)))
            if (!(ch >= cmin)) return null
            const cw = Math.floor(Math.min(amax * ch, (Wc - (cols - 1) * g) / cols))
            if (cw < minW) return null
            return { cols: cols, rows: rows, ch: ch, cw: cw, gw: cols * cw + (cols - 1) * g, gh: rows * ch + (rows - 1) * g }
        }
        function flightBelow(rem) {
            const both = flightK(W, rem, "shot", "both"), natK = flightK(W, 1e6, "shot", "both")
            if (both >= natK - 1e-6) return { part: "both", k: both, w: W, inView: true }
            const side = flightK(W, rem, "shot", "side"), plan = flightK(W, rem, "shot", "plan")
            const one = side >= plan ? { part: "side", k: side } : { part: "plan", k: plan }
            const pick = both >= one.k * 0.7 ? { part: "both", k: both } : one
            if (pick.k < kMin) return { part: "both", k: natK, w: W, inView: false }
            return { part: pick.part, k: pick.k, w: Math.min(W, fPadL + (sc.maxYd - sc.lo) * pick.k + fPadR), inView: true }
        }
        let f = null, fl = null, arr = "quad"
        const below = fit(2, 2, W, H)
        const beside = fit(2, 2, W - flMinW - g, H)
        if (beside) {
            const fw = W - beside.gw - g, fk = flightK(fw, beside.gh + cap - flCap, "shot", "both")
            if (fk >= kMin && (!below || beside.ch >= 0.9 * below.ch))
                { f = beside; fl = { part: "both", k: fk, w: Math.min(fw, fPadL + (sc.maxYd - sc.lo) * fk + fPadR), inView: true, beside: true } }
        }
        if (!f && below) { f = below; fl = Object.assign(flightBelow(H - below.gh - cap - g - flCap), { beside: false }) }
        if (!f) { const r = fit(4, 1, W, H); if (r) { f = r; arr = "row"; fl = Object.assign(flightBelow(H - r.gh - cap - g - flCap), { beside: false }) } }
        if (!f) { const q = fit(2, 2, W, 1e6); if (q) { f = q; fl = Object.assign(flightBelow(0), { beside: false }) } }
        if (!f) { const c = fit(1, 4, W, 1e6); if (c) { f = c; arr = "col"; fl = Object.assign(flightBelow(0), { beside: false }) } }
        if (!f) { const ch = Math.max(40, Math.floor(Math.min(cmin, W / amin))); f = { cols: 1, rows: 4, ch: ch, cw: Math.min(W, Math.floor(amin * ch)), gw: Math.min(W, Math.floor(amin * ch)), gh: 4 * ch + 3 * g }; arr = "col"; fl = Object.assign(flightBelow(0), { beside: false }) }
        const totalW = fl.beside ? f.gw + g + fl.w : f.gw
        const gx = Math.max(0, (W - totalW) / 2)
        const flY = fl.beside ? 0 : f.gh + cap + g, flFig = flightHAt(fl.k, "shot", fl.part)
        const flX = fl.beside ? gx + f.gw + g : Math.max(0, (W - fl.w) / 2)
        const out = { arrangement: arr, cw: f.cw, ch: f.ch, gap: g, gx: gx, groupW: f.gw, groupH: f.gh, capY: f.gh + ts.u(3), folded: [],
                      cells: [{ kind: "cells", w: f.cw, h: f.ch }],
                      fl: { x: flX, y: flY, w: fl.w, figH: flFig, part: fl.part, k: fl.k, inView: fl.inView, beside: fl.beside },
                      totalH: Math.max(f.gh + cap, flY + flFig + flCap) }
        return out
    }
    component ShotFigures: Item {
        id: sfg
        property var h
        property real availH: 400
        readonly property var lay: h.shotLayout(width, Math.max(0, availH))
        height: lay.totalH
        // published for the render harness's layout sheet (nothing in this file reads it)
        onLayChanged: if (visible) sfg.h.lastShotLayout = Object.assign({ W: Math.round(width), H: Math.round(availH), em: sfg.h.em,
                                                                          impactPxMm: sfg.h.faceQPxMm(lay.cw, lay.ch) }, lay)
        ClubQuad { x: sfg.lay.gx; h: sfg.h; arrangement: sfg.lay.arrangement; cw: sfg.lay.cw; ch: sfg.lay.ch; gap: sfg.lay.gap }
        Text {
            x: sfg.lay.gx; y: sfg.lay.capY; width: sfg.lay.groupW; wrapMode: Text.WordWrap
            text: "Path and Spin views ×" + sfg.h.lr.angleGain + "; Loft true. " + sfg.h.lowPointLine + " " + sfg.h.patchShort
            font: sfg.h.ts.fMicro; color: sfg.h.ts.ink3
        }
        FlightFigure {
            x: sfg.lay.fl.x; y: sfg.lay.fl.y; width: sfg.lay.fl.w
            figH: sfg.lay.fl.figH; part: sfg.lay.fl.part; mode: "shot"; h: sfg.h
        }
    }

    // ═══ Session halves (round 2). Session is SCOPE: the aggregate of the scoped club, ball and club. ═══
    component HalfHead: Item {
        id: hh
        property var h
        property string title: ""
        property string sub: ""
        readonly property QtObject t: h ? h.ts : null
        height: t ? hht.height + t.u(7) : 0
        Text { id: hht; visible: !!hh.t; text: hh.title; font: hh.t ? hh.t.fSection : Qt.font({}); color: hh.t ? hh.t.ink : "transparent" }
        Text { visible: !!hh.t; anchors.left: hht.right; anchors.leftMargin: hh.t ? hh.t.u(10) : 0; anchors.baseline: hht.baseline; width: hh.width - x
               text: hh.sub; font: hh.t ? hh.t.fMicro : Qt.font({}); color: hh.t ? hh.t.ink3 : "transparent"; elide: Text.ElideNone; wrapMode: Text.NoWrap; clip: true }
        Rectangle { visible: !!hh.t; anchors.bottom: parent.bottom; anchors.bottomMargin: hh.t ? hh.t.u(3) : 0; width: parent.width; height: hh.t ? hh.t.hw : 0; color: hh.t ? hh.t.rule : "transparent" }
    }
    // BALL: the dispersion (+ launch × spin beside it when the half has lr.lsPairEm), then six consistency tiles
    component BallHalf: Column {
        id: bh
        property var h
        property real figH: 240
        property bool bare: false
        readonly property bool lsOn: width / h.em >= h.lr.lsPairEm
        readonly property real gap: h.ts.gutter(width)
        readonly property real lsW: lsOn ? Math.max(14 * h.em, Math.min(width * 0.36, figH * 1.05)) : 0
        spacing: h.ts.u(6)
        HalfHead { visible: !bh.bare; width: parent.width; h: bh.h; title: "Ball"; sub: "where it landed · how it launched" }
        ClubKey { width: parent.width; h: bh.h }
        Item {
            width: parent.width; height: bh.figH
            FlightFigure { width: parent.width - (bh.lsOn ? bh.lsW + bh.gap : 0); figH: bh.figH; mode: "session"; h: bh.h }
            ScatterFig { visible: bh.lsOn; x: parent.width - width; width: bh.lsW; height: bh.figH; kind: "ls"; h: bh.h }
        }
        TileBlock { width: parent.width; defs: bh.h.statTiles; folded: []; h: bh.h }
        Text { width: parent.width; wrapMode: Text.WordWrap; text: bh.h.statKey; font: bh.h.ts.fMicro; color: bh.h.ts.ink3 }
    }
    // CLUB: the impact map beside path × face (stacked below lr.cfPairEm), then the delivery tiles
    component ClubHalf: Column {
        id: chf
        property var h
        property real figH: 240
        property bool bare: false
        readonly property bool pair: width / h.em >= h.lr.cfPairEm
        readonly property real gap: h.ts.gutter(width)
        readonly property real cw: pair ? (width - gap) * h.lr.mapShare : width
        readonly property real sw: pair ? width - gap - cw : width
        spacing: h.ts.u(6)
        HalfHead { visible: !chf.bare; width: parent.width; h: chf.h; title: "Club"; sub: "where it struck · how it was delivered" }
        Item {
            width: parent.width; height: chf.pair ? chf.figH : 2 * chf.figH + chf.h.ts.u(10)
            Item {
                width: chf.cw; height: chf.figH
                Text { id: mt; text: "Impact · " + (chf.h.focusClub ? chf.h.focusClub.name.toLowerCase() : ""); font: chf.h.fTileLabel; color: chf.h.ts.ink2 }
                ClubCell { y: mt.height + chf.h.ts.u(2); width: parent.width; height: parent.height - y; kind: "map"; h: chf.h }
            }
            ScatterFig { x: chf.pair ? chf.cw + chf.gap : 0; y: chf.pair ? 0 : chf.figH + chf.h.ts.u(10); width: chf.sw; height: chf.figH; kind: "pf"; h: chf.h }
        }
        Text { width: parent.width; wrapMode: Text.WordWrap; text: chf.h.mapCaption; font: chf.h.ts.fMicro; color: chf.h.ts.ink3 }
        TileBlock { width: parent.width; defs: chf.h.clubTiles; folded: []; h: chf.h }
        Text { width: parent.width; wrapMode: Text.WordWrap; text: chf.h.clubKeyText; font: chf.h.ts.fMicro; color: chf.h.ts.ink3 }
    }
    // a scatter of the scoped shots: "pf" path × face (square, the face-to-path diagonal, quadrant words) or
    // "ls" launch × spin. Shots are the club's hue as hollow rings; this shot filled and ringed in ink.
    component ScatterFig: Item {
        id: sf
        property var h
        property string kind: "pf"
        readonly property var geo: h.scatterGeom(kind, width, height)
        Text { id: sft; text: sf.kind === "pf" ? "Path ° × face °" : "Launch ° × spin rpm"; font: sf.h.fTileLabel; color: sf.h.ts.ink2 }
        Text { anchors.left: sft.right; anchors.leftMargin: sf.h.ts.u(8); anchors.baseline: sft.baseline
               text: sf.geo.n + " shots"; font: sf.h.ts.fMicro; color: sf.h.ts.ink3 }
        Canvas {
            anchors.fill: parent
            readonly property var deps: [sf.geo, sf.h.ts.ink, sf.h.focusHue, sf.h.ts.paper, sf.h.hoverId]
            onDepsChanged: requestPaint()
            onPaint: { const ctx = getContext("2d"); ctx.reset(); sf.h.paintScatter(ctx, sf.geo) }
        }
        ShotPicker { anchors.fill: parent; h: sf.h; src: sf.kind; pts: sf.geo.pts }
        Repeater {
            model: sf.geo.labels
            delegate: Text {
                required property var modelData
                x: modelData.ax === "r" ? modelData.x - width : modelData.ax === "c" ? modelData.x - width / 2 : modelData.x
                y: modelData.y - height / 2
                text: modelData.t
                font: modelData.word ? sf.h.ts.fMicro : sf.h.ts.fFigLabel
                color: modelData.word ? sf.h.ts.ink2 : sf.h.ts.ink3
            }
        }
    }
    function scatterGeom(kind, w, hgt) {
        const top = labLH + ts.u(10), padL = 3.2 * em, padB = labLH * 1.8, padR = ts.u(10)
        const aw = Math.max(10, w - padL - padR), ah = Math.max(10, hgt - top - padB)
        const pts = [], labels = [], hand = g.leftHanded ? -1 : 1
        const kx = kind === "pf" ? "lm.clubPath" : "lm.launchAngle", ky = kind === "pf" ? "lm.faceAngle" : "lm.spinRate"
        for (const s of scopedAll) {
            const f = s.shotId === focusId, x = f ? num(kx) : shotV(s, kx), y = f ? num(ky) : shotV(s, ky)
            if (isFinite(x) && isFinite(y)) pts.push({ vx: x, vy: y, focus: f, id: s.shotId, out: s.out })
        }
        // round 4: the axes in words too ("2 L · 0 · 2 R"; path across, face up the side)
        const fmt = v => v === 0 ? "0" : Math.abs(v) + (v > 0 ? " R" : " L")
        if (kind === "pf") {
            let m = 3
            for (const p of pts) m = Math.max(m, Math.abs(p.vx), Math.abs(p.vy))
            const step = m <= 5 ? 2 : m <= 11 ? 4 : 5, R = Math.ceil((m + 1) / step) * step
            const side = Math.min(aw, ah), x0 = padL + (aw - side) / 2, y0 = top + (ah - side) / 2
            const X = v => x0 + (hand * v + R) / (2 * R) * side, Y = v => y0 + (R - v) / (2 * R) * side
            for (let v = -R + step; v < R; v += step) {
                labels.push({ t: fmt(v), x: X(hand * v), y: y0 + side + labLH * 0.7, ax: "c" })
                labels.push({ t: fmt(v), x: x0 - ts.u(5), y: Y(v), ax: "r" })
            }
            // the quadrant words: start right (face open) is push, left pull; face open to the path fades, closed draws
            const ins = ts.u(6), wd = (a, b) => hand > 0 ? a : b
            labels.push({ t: wd("fade", "draw"), x: x0 + ins, y: y0 + labLH * 0.7, ax: "l", word: true })
            labels.push({ t: wd("push", "pull"), x: x0 + side - ins, y: y0 + labLH * 0.7, ax: "r", word: true })
            labels.push({ t: wd("pull", "push"), x: x0 + ins, y: y0 + side - labLH * 0.7, ax: "l", word: true })
            labels.push({ t: wd("draw", "fade"), x: x0 + side - ins, y: y0 + side - labLH * 0.7, ax: "r", word: true })
            return { kind: kind, x0: x0, y0: y0, pw: side, ph: side, n: pts.length, labels: labels,
                     pts: pts.map(p => ({ x: X(p.vx), y: Y(p.vy), focus: p.focus, id: p.id, out: p.out })),
                     zx: X(0), zy: Y(0), diag: [X(-R), Y(-R), X(R), Y(R)], gx: [], gy: [] }
        }
        // launch × spin: nice steps; spin in 500s or 1000s
        let x0v = 1e9, x1v = -1e9, y0v = 1e9, y1v = -1e9
        for (const p of pts) { x0v = Math.min(x0v, p.vx); x1v = Math.max(x1v, p.vx); y0v = Math.min(y0v, p.vy); y1v = Math.max(y1v, p.vy) }
        if (!pts.length) { x0v = 0; x1v = 20; y0v = 0; y1v = 4000 }
        const sx = (x1v - x0v) <= 8 ? 2 : 5, sy = (y1v - y0v) <= 1800 ? 500 : 1000
        const lx = Math.floor((x0v - 0.5) / sx) * sx, hx = Math.ceil((x1v + 0.5) / sx) * sx
        const ly = Math.floor((y0v - 100) / sy) * sy, hy = Math.ceil((y1v + 100) / sy) * sy
        const X = v => padL + (v - lx) / (hx - lx) * aw, Y = v => top + (hy - v) / (hy - ly) * ah
        const gx = [], gy = []
        for (let v = lx; v <= hx + 1e-6; v += sx) { gx.push(X(v)); if (v < hx) labels.push({ t: String(v), x: X(v), y: top + ah + labLH * 0.7, ax: "c" }) }
        for (let v = ly; v <= hy + 1e-6; v += sy) { gy.push(Y(v)); if (v < hy) labels.push({ t: String(v), x: padL - ts.u(5), y: Y(v), ax: "r" }) }
        return { kind: kind, x0: padL, y0: top, pw: aw, ph: ah, n: pts.length, labels: labels,
                 pts: pts.map(p => ({ x: X(p.vx), y: Y(p.vy), focus: p.focus, id: p.id, out: p.out })), gx: gx, gy: gy }
    }
    function paintScatter(ctx, G) {
        if (!hasData) return
        const lw = ts.isPrint ? 0.75 : 1
        ctx.lineCap = "round"
        ctx.strokeStyle = ts.grid; ctx.lineWidth = lw
        for (const x of G.gx) { ctx.beginPath(); ctx.moveTo(x, G.y0); ctx.lineTo(x, G.y0 + G.ph); ctx.stroke() }
        for (const y of G.gy) { ctx.beginPath(); ctx.moveTo(G.x0, y); ctx.lineTo(G.x0 + G.pw, y); ctx.stroke() }
        ctx.strokeStyle = ts.hair; ctx.strokeRect(G.x0, G.y0, G.pw, G.ph)
        if (G.kind === "pf") {
            ctx.strokeStyle = ts.baseline; ctx.lineWidth = lw
            ctx.beginPath(); ctx.moveTo(G.zx, G.y0); ctx.lineTo(G.zx, G.y0 + G.ph); ctx.moveTo(G.x0, G.zy); ctx.lineTo(G.x0 + G.pw, G.zy); ctx.stroke()
            ctx.save(); ctx.setLineDash(lr.refDash); ctx.strokeStyle = ts.ink3; ctx.lineWidth = 1.25
            ctx.beginPath(); ctx.moveTo(G.diag[0], G.diag[1]); ctx.lineTo(G.diag[2], G.diag[3]); ctx.stroke(); ctx.restore()
        }
        const r = ts.u(lr.scatterDot) / 2
        let f = null
        for (const p of G.pts) {
            if (p.focus) { f = p; continue }
            ctx.beginPath(); ctx.arc(p.x, p.y, r, 0, 2 * Math.PI, false)
            ctx.fillStyle = p.out ? ts.paper : Qt.alpha(focusHue, 0.22); ctx.fill(); ctx.lineWidth = p.out ? 1 : 1.25; ctx.strokeStyle = p.out ? Qt.alpha(focusHue, 0.7) : focusHue; ctx.stroke()
            if (p.id === hoverId) hoverRing(ctx, p.x, p.y)
        }
        if (f) {
            dot(ctx, f.x, f.y, r, focusHue, f.out, ts.paper)
            ctx.beginPath(); ctx.arc(f.x, f.y, ts.ringD / 2 + 1, 0, 2 * Math.PI, false); ctx.lineWidth = 1.25; ctx.strokeStyle = ts.ink; ctx.stroke()
        }
    }

    // FSX's flight (images 04–05): from above with yardage arcs every 25 yd, the target line, each club's
    // enclosing landing ellipse (the scoped club solid with its landing dots, the others ghosted, dots too) and this
    // shot's tracer; from the side with one height line. The two share one distance scale.
    component FlightFigure: Item {
        id: ff
        property var h
        property real figH: 300
        property string mode: "shot"                 // "shot": tracer, plan + side · "session": every landing, plan only
        property string part: "both"                 // round 4b, "shot": "both" | "plan" (from above only) | "side" (from the side only)
        readonly property real captionH: capT.visible ? capT.height + h.ts.u(4) : 0
        readonly property var geo: h.flightGeom(width, figH, mode, part)
        function naturalW(H) { return h.flightNaturalW(H - captionH, mode, part) }
        function naturalH(W) { return h.flightNaturalH(W, mode, part) + captionH }
        height: figH + captionH
        Canvas {
            id: fc
            width: parent.width; height: ff.figH
            readonly property var deps: [ff.geo, ff.h.clubs, ff.h.ts.ink, ff.h.focusId, ff.h.hoverId, ff.h.shots, ff.h.flight, ff.mode, ff.part]
            onDepsChanged: requestPaint()
            onPaint: { const ctx = getContext("2d"); ctx.reset(); ff.h.paintFlight(ctx, ff.geo) }
        }
        Repeater {
            model: ff.geo.arcLabels
            delegate: Text {
                required property var modelData
                x: modelData.x - width / 2; y: modelData.y - height / 2
                rotation: modelData.rot
                text: modelData.text; font: ff.h.ts.fFigLabel; color: ff.h.ts.ink3
            }
        }
        Text { visible: ff.geo.ok && ff.geo.sideOn; x: ff.geo.x0 + ff.h.ts.u(2); y: ff.geo.hlY - height - 1; text: ff.geo.hlText; font: ff.h.ts.fFigLabel; color: ff.h.ts.ink3 }
        Row {
            visible: ff.geo.ok && ff.geo.sideOn && ff.h.has("lm.launchAngle")
            x: ff.geo.x0 + 3.2 * ff.h.em; y: ff.geo.ground + ff.h.ts.u(5); spacing: ff.h.ts.u(5)
            Text { text: "Launch"; font: ff.h.fTileLabel; color: ff.h.ts.ink3; anchors.baseline: lav.baseline }
            Text { id: lav; text: ff.h.figU("lm.launchAngle"); font: ff.h.ts.fFig; color: ff.h.ts.ink2 }
        }
        // round 2: peak height at the apex and descent at the landing, on the glance as on the read
        Row {
            visible: ff.geo.ok && ff.geo.sideOn && ff.h.has("lm.peakHeight")
            x: ff.geo.apexX - width / 2; y: ff.geo.apexY - height - ff.h.ts.u(3); spacing: ff.h.ts.u(5)
            Text { text: "Peak"; font: ff.h.fTileLabel; color: ff.h.ts.ink3; anchors.baseline: pkv.baseline }
            Text { id: pkv; text: ff.h.txt("lm.peakHeight") + " ft"; font: ff.h.ts.fFig; color: ff.h.isOut("lm.peakHeight") ? ff.h.ts.warn : ff.h.ts.ink2 }
        }
        Row {
            visible: ff.geo.ok && ff.geo.sideOn && ff.h.has("lm.descentAngle")
            x: ff.geo.landX - width - 3.0 * ff.h.em; y: ff.geo.ground - height - ff.h.ts.u(2); spacing: ff.h.ts.u(5)
            Text { text: "Descent"; font: ff.h.fTileLabel; color: ff.h.ts.ink3; anchors.baseline: dsv.baseline }
            Text { id: dsv; text: ff.h.figU("lm.descentAngle"); font: ff.h.ts.fFig; color: ff.h.isOut("lm.descentAngle") ? ff.h.ts.warn : ff.h.ts.ink2 }
        }
        Text { visible: ff.geo.ok && ff.geo.planOn; x: ff.geo.x0 - width - ff.h.ts.u(6); y: ff.geo.y0 - height / 2; text: ff.geo.lo > 0 ? "≈ " + ff.geo.lo : "Tee"; font: ff.h.ts.fMicro; color: ff.h.ts.ink3 }
        ShotPicker { visible: ff.mode === "session"; width: parent.width; height: ff.figH; h: ff.h; src: "disp"
                     pts: ff.mode === "session" && ff.geo.ok ? ff.h.dispPts(ff.geo) : [] }
        Text {
            id: capT
            visible: ff.mode === "shot" && ff.h.shapeLine.length > 0
            y: ff.figH + ff.h.ts.u(2)
            width: parent.width; wrapMode: Text.WordWrap
            text: ff.h.shapeLine; font: ff.h.ts.fMicro; color: ff.h.ts.ink3
        }
    }

    // every reading, grouped Club · Strike · Launch · Spin · Flight, flowed into columns counted from a
    // 36 em minimum; a row is one line from 35 em, else its mean ± SD and strip fold to line 2
    component LedgerBlock: Column {
        id: lb
        property var h
        property bool bare: false
        property bool indexMode: false               // This shot: name … value only, in index columns (20 em)
        property var keys: null                      // a subset of readings (the glance's spread), or null for all
        property int maxCols: indexMode ? 3 : 2
        property string title: ""
        readonly property real gap: h.ts.gutter(width) * 1.4
        readonly property int cols: Math.max(1, Math.min(maxCols, h.ts.columns(width, indexMode ? h.ts.flows.index : h.lr.ledgerColEm, gap)))
        readonly property var srcBands: !keys ? h.bands : h.bands.map(b => ({ band: b.band, tiles: b.tiles.filter(x => keys.indexOf(x.key) >= 0) }))
                                                                   .filter(b => b.tiles.length > 0).map(b => ({ band: b.band, count: b.tiles.length, tiles: b.tiles }))
        readonly property real cw: (width - (cols - 1) * gap) / cols
        readonly property bool full: indexMode || cw / h.em >= h.lr.ledgerFullEm
        // the bands split into columns in order, balanced by row count
        readonly property var split: {
            const bs = srcBands, out = []
            for (let i = 0; i < cols; ++i) out.push([])
            const total = bs.reduce((s, b) => s + b.tiles.length + 1.5, 0)
            let acc = 0, ci = 0
            for (const b of bs) {
                const w = b.tiles.length + 1.5
                if (ci < cols - 1 && acc > 0 && acc + w / 2 > total * (ci + 1) / cols) ++ci
                out[ci].push(b); acc += w
            }
            return out
        }
        spacing: h.ts.u(6)
        Column {
            visible: !lb.bare; width: parent.width; spacing: lb.h.ts.u(3)
            Rectangle { width: parent.width; height: lb.h.ts.r.figureRuleTop; color: lb.h.ts.rule }
            Flow { width: parent.width; spacing: lb.h.ts.u(10)
                Text { id: lbt; text: lb.title ? lb.title : lb.indexMode ? "Every reading on shot " + lb.h.shotNo : "Every reading, against the session"; font: lb.h.ts.fSection; color: lb.h.ts.ink }
                Text { text: (lb.indexMode ? lb.h.readingN + " readings" : "shot " + lb.h.shotNo + " against the " + lb.h.scopeN + " " + (lb.h.focusClub ? lb.h.focusClub.name : "") + " shots") + "  ·  " + lb.h.corridorLine
                       font: lb.h.ts.fMicro; color: lb.h.outKeys.length ? lb.h.ts.warn : lb.h.ts.ink3; height: lbt.height; verticalAlignment: Text.AlignVCenter } }
        }
        Row {
            spacing: lb.gap
            Repeater {
                model: lb.split
                delegate: Column {
                    id: lcol
                    required property var modelData
                    width: lb.cw
                    spacing: 0
                    LedgerHead { width: parent.width; h: lb.h; full: lb.full; indexOnly: lb.indexMode }
                    Repeater {
                        model: lcol.modelData
                        delegate: Column {
                            id: bandCol
                            required property var modelData
                            width: lcol.width
                            Item {
                                width: parent.width; height: bh.height + lb.h.ts.u(10)
                                Text { id: bh; anchors.bottom: parent.bottom; anchors.bottomMargin: lb.h.ts.u(3); text: bandCol.modelData.band; font: lb.h.ts.fSection; color: lb.h.ts.ink }
                                Text { anchors.left: bh.right; anchors.leftMargin: lb.h.ts.u(8); anchors.baseline: bh.baseline; text: bandCol.modelData.count; font: lb.h.ts.fFigLabel; color: lb.h.ts.ink3 }
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: lb.h.ts.hw; color: lb.h.ts.rule }
                            }
                            Repeater {
                                model: bandCol.modelData.tiles
                                delegate: LedgerRow { required property var modelData; tile: modelData; width: bandCol.width; h: lb.h; full: lb.full; indexOnly: lb.indexMode }
                            }
                        }
                    }
                }
            }
        }
    }
    component LedgerHead: Item {
        id: lh
        property var h
        property bool full: true
        property bool indexOnly: false
        readonly property var c: h.ledgerCols(width, full && !indexOnly, indexOnly)
        height: h.labLH + h.ts.u(4)
        Text { x: 0; text: "Reading"; font: lh.h.fTileLabel; color: lh.h.ts.ink3 }
        Text { x: lh.c.valR - width; text: "Shot " + lh.h.shotNo; font: lh.h.fTileLabel; color: lh.h.ts.ink3 }
        Text { visible: lh.full && !lh.indexOnly; x: lh.c.meanR - width; text: "Avg"; font: lh.h.fTileLabel; color: lh.h.ts.ink3 }
        Text { visible: lh.full && !lh.indexOnly; x: lh.c.sdR - width; text: "± SD"; font: lh.h.fTileLabel; color: lh.h.ts.ink3 }
        Text { visible: lh.full && !lh.indexOnly; x: lh.c.stripX; text: "−3  ±1 SD  +3"; font: lh.h.fTileLabel; color: lh.h.ts.ink3; width: lh.c.stripW; horizontalAlignment: Text.AlignHCenter }
    }
    component LedgerRow: Item {
        id: lrw
        property var h
        property var tile: ({})
        property bool full: true
        property bool indexOnly: false
        readonly property var c: h.ledgerCols(width, full && !indexOnly, indexOnly)
        readonly property bool out: h.isOut(tile.key)
        readonly property real lineH: Math.ceil(h.ts.fBody.pixelSize * 1.55)
        height: full || indexOnly ? lineH : lineH + Math.ceil(h.ts.fMicro.pixelSize * 1.5) + h.ts.u(2)
        Text { id: nm; x: 0; y: (lrw.lineH - height) / 2; width: lrw.c.nameW; elide: Text.ElideNone; wrapMode: Text.NoWrap
               text: lrw.h.label(lrw.tile.key) + (lrw.h.measured(lrw.tile.key) ? " †" : ""); font: lrw.h.ts.fBody; color: lrw.h.ts.ink }
        // round 4: the size in the figure column (aligned with every other row), its direction word after the unit
        readonly property var dp: h.dirParts(tile.key, h.txt(tile.key), h.unitOf(tile.key) === "°" && h.has(tile.key) ? "°" : "")
        Text { id: lv; x: lrw.c.valR - width; anchors.baseline: nm.baseline; text: lrw.dp.num
               font: lrw.h.ts.fFig; color: lrw.out ? lrw.h.ts.warn : lrw.h.ts.ink }
        Text { id: lu; x: lrw.c.unitX; anchors.baseline: nm.baseline; text: lrw.h.unitOf(lrw.tile.key) === "°" ? "" : lrw.h.unitWord(lrw.h.unitOf(lrw.tile.key)); font: lrw.h.ts.fMicro; color: lrw.h.ts.ink3 }
        Text { visible: lrw.dp.dir.length > 0; x: lrw.c.unitX + (lu.text.length ? lu.implicitWidth + lrw.h.ts.u(4) : 0); anchors.baseline: nm.baseline
               text: lrw.dp.dir; font: lrw.h.fQual; color: lrw.out ? lrw.h.ts.warn : lrw.h.ts.ink2 }
        Text { visible: lrw.out; x: lrw.c.wordX; anchors.baseline: nm.baseline; text: "outside"; font: lrw.h.ts.fMicro; color: lrw.h.ts.warn }
        // the folded or inline spread: mean, ±SD, the ±3 SD strip
        Text { visible: !lrw.indexOnly; x: lrw.full ? lrw.c.meanR - width : 0; y: lrw.full ? 0 : lrw.lineH; anchors.baseline: lrw.full ? nm.baseline : undefined
               text: lrw.full ? lrw.h.dirStr(lrw.tile.key, lrw.h.meanTxt(lrw.tile.key)) : "avg " + lrw.h.dirStr(lrw.tile.key, lrw.h.meanTxt(lrw.tile.key)) + "   " + lrw.h.sdTxt(lrw.tile.key)
               font: lrw.full ? lrw.h.ts.fFigLabel : lrw.h.ts.fMicro; color: lrw.h.ts.ink2 }
        Text { visible: lrw.full && !lrw.indexOnly; x: lrw.c.sdR - width; anchors.baseline: nm.baseline; text: lrw.h.sdTxt(lrw.tile.key); font: lrw.h.ts.fFigLabel; color: lrw.h.ts.ink3 }
        Item {
            x: lrw.c.stripX; width: lrw.c.stripW
            y: lrw.full ? (lrw.lineH - height) / 2 : lrw.lineH + (Math.ceil(lrw.h.ts.fMicro.pixelSize * 1.5) - height) / 2
            height: lrw.h.ts.stripH
            visible: lrw.tile.hasSpread === true && !lrw.indexOnly
            Rectangle { y: parent.height / 2; width: parent.width; height: lrw.h.ts.hw; color: lrw.h.ts.hair }
            Rectangle { x: parent.width / 3; width: parent.width / 3; height: parent.height; color: lrw.h.ts.wash }
            Rectangle { x: parent.width / 2; width: lrw.h.ts.hw; height: parent.height; color: lrw.h.ts.hair }
            Rectangle { visible: lrw.tile.hasLatest === true; x: Math.round(parent.width * (lrw.tile.tickPct || 50) / 100 - width / 2); width: Math.max(2, lrw.h.ts.stripBar * 0.7); height: parent.height
                        color: lrw.out ? lrw.h.ts.warn : lrw.h.ts.ink }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: lrw.h.ts.hw; color: lrw.h.ts.hair }
    }
    // the ledger's column positions in a row of width w (one line when `full`)
    function ledgerCols(w, full, indexOnly) {
        const e = em, gap = 0.9 * e
        if (indexOnly) { const unitW = 4.6 * e, wordW = outKeys.length ? 4.6 * e : 0, unitX = w - wordW - unitW, valR = unitX - gap * 0.35
                         return { nameW: valR - 4.4 * e, valR: valR, unitX: unitX, meanR: 0, sdR: 0, stripX: 0, stripW: 0, wordX: w - wordW + gap * 0.5 } }
        if (full) {
            const stripW = 7 * e, sdW = 3.4 * e, meanW = 5.4 * e, wordW = outKeys.length ? 4.6 * e : 0, unitW = 4.6 * e, valW = 4.4 * e
            const stripX = w - wordW - stripW
            const sdR = stripX - gap, meanR = sdR - sdW - gap * 0.4
            const unitX = meanR - meanW - unitW, valR = unitX - gap * 0.35
            return { nameW: valR - valW, valR: valR, unitX: unitX, meanR: meanR, sdR: sdR, stripX: stripX, stripW: stripW, wordX: w - wordW + gap * 0.5 }
        }
        const unitW = 4.6 * e, wordW = outKeys.length ? 4.6 * e : 0
        const unitX = w - wordW - unitW, valR = unitX - gap * 0.35
        return { nameW: valR - 4.4 * e, valR: valR, unitX: unitX, meanR: 0, sdR: 0, stripX: Math.max(10 * e, w - wordW - 7 * e), stripW: 7 * e, wordX: w - wordW + gap * 0.5 }
    }

    // print: the session's shots as a table (Foresight's Table), grouped by club; Avg and ± rows from the board
    readonly property var shotCols: [
        { key: "lm.carryDistance", head: "Carry", unit: "yd" }, { key: "lm.totalDistance", head: "Total", unit: "yd" },
        { key: "lm.offline", head: "Offline", unit: "yd" }, { key: "lm.ballSpeed", head: "Ball", unit: "mph" },
        { key: "lm.clubheadSpeed", head: "Club", unit: "mph" }, { key: "lm.smashFactor", head: "Smash", unit: "" },
        { key: "lm.launchAngle", head: "Launch", unit: "°" }, { key: "lm.spinRate", head: "Spin", unit: "rpm" },
        { key: "lm.spinAxis", head: "Axis", unit: "°" }, { key: "lm.peakHeight", head: "Peak", unit: "ft" }
    ]
    // a reading the shot lacks prints its gate's words (print: words only), else "-"
    function cellTxt(s, key) { const x = shotV(s, key); return isFinite(x) ? dirStr(key, fmtKey(key, x)) : ((s && s.gateWords && s.gateWords[key]) || "-") }
    component ShotTable: Column {
        id: st
        property var h
        readonly property real numW: h.em * 3
        readonly property real cellW: (width - numW) / h.shotCols.length
        readonly property real rowH: Math.ceil(h.ts.fFigLabel.pixelSize * 1.65)
        Item {
            width: parent.width; height: st.h.labLH * 2 + st.h.ts.u(4)
            Text { y: 0; text: "Shot"; font: st.h.fTileLabel; color: st.h.ts.ink3 }
            Repeater {
                model: st.h.shotCols
                delegate: Column {
                    required property var modelData
                    required property int index
                    x: st.numW + (index + 1) * st.cellW - width; width: st.cellW
                    Text { width: parent.width; horizontalAlignment: Text.AlignRight; text: modelData.head; font: st.h.fTileLabel; color: st.h.ts.ink3 }
                    Text { width: parent.width; horizontalAlignment: Text.AlignRight; text: modelData.unit; font: st.h.ts.fMicro; color: st.h.ts.ink3 }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: st.h.ts.hw; color: st.h.ts.rule }
        }
        Repeater {
            model: st.h.clubs
            delegate: Column {
                id: tg
                required property var modelData
                width: st.width
                readonly property bool scoped: modelData.key === st.h.focusKey
                Item {
                    width: parent.width; height: st.rowH * 1.35
                    Rectangle { y: st.h.ts.u(3); width: st.h.ts.u(3); height: parent.height - st.h.ts.u(6); color: tg.modelData.hue }
                    Text { x: st.h.ts.u(8); anchors.verticalCenter: parent.verticalCenter; text: tg.modelData.name + "  ·  " + tg.modelData.shots.length + " shots"; font: st.h.ts.fBodyM; color: st.h.ts.ink }
                }
                Repeater {
                    model: tg.modelData.shots
                    delegate: Item {
                        id: trw
                        required property var modelData
                        width: tg.width; height: st.rowH
                        Rectangle { anchors.fill: parent; color: trw.modelData.shotId === st.h.focusId ? st.h.ts.pickWash : "transparent" }
                        Text { x: st.h.ts.u(8); anchors.verticalCenter: parent.verticalCenter; text: "#" + trw.modelData.ordinal + (trw.modelData.out ? " out" : trw.modelData.relabelled ? "*" : ""); font: st.h.ts.fFigLabel; color: trw.modelData.out ? st.h.ts.ink3 : st.h.ts.ink2 }
                        Repeater {
                            model: st.h.shotCols
                            delegate: Text {
                                required property var modelData
                                required property int index
                                x: st.numW + (index + 1) * st.cellW - width; anchors.verticalCenter: parent.verticalCenter
                                readonly property bool gated: !isFinite(st.h.shotV(trw.modelData, modelData.key)) && !!(trw.modelData.gateWords && trw.modelData.gateWords[modelData.key])
                                width: gated ? st.cellW - st.h.ts.u(4) : implicitWidth; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight
                                text: st.h.cellTxt(trw.modelData, modelData.key); font: gated ? st.h.ts.fMicro : st.h.ts.fFigLabel
                                color: trw.modelData.out || gated ? st.h.ts.ink3 : st.h.ts.ink
                            }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: st.h.ts.hw; color: st.h.ts.hair }
                    }
                }
                Repeater {
                    model: tg.scoped ? ["Avg", "± SD"] : []
                    delegate: Item {
                        id: ar
                        required property string modelData
                        required property int index
                        width: tg.width; height: st.rowH
                        Rectangle { anchors.fill: parent; color: Qt.alpha(st.h.ts.ink, st.h.lr.tileFootAlpha) }
                        Text { x: st.h.ts.u(8); anchors.verticalCenter: parent.verticalCenter; text: ar.modelData; font: st.h.fTileLabel; color: st.h.ts.ink2 }
                        Repeater {
                            model: st.h.shotCols
                            delegate: Text {
                                required property var modelData
                                required property int index
                                x: st.numW + (index + 1) * st.cellW - width; anchors.verticalCenter: parent.verticalCenter
                                text: ar.index === 0 ? st.h.dirStr(modelData.key, st.h.meanTxt(modelData.key)) : st.h.sdTxt(modelData.key) || "-"
                                font: st.h.ts.fFigLabel; color: st.h.ts.ink2
                            }
                        }
                    }
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════
    // GEOMETRY + PAINT. QML positions and paints; every number drawn is the board's, or a shot's own.
    // ═══════════════════════════════════════════════════════════════════════════════════════════════
    // the distance window and the lateral half-width, per mode. "shot": from the tee, wide enough for this
    // shot's tracer. "session": every landing and every ellipse; the empty run from the tee collapses
    // behind a drawn break (§15.8's data-driven broken axis), so the landings get the room.
    function flightScale(mode) {
        const step = lr.arcEvery, ses = mode === "session"
        let maxT = isFinite(flight.totalYd) ? flight.totalYd : 0, minC = 1e9, L = 12
        if (!ses) L = Math.max(L, Math.abs(flight.offlineYd || 0), flight.lateralExtentYd || 0)
        for (const s of shots) {
            if (ses) { if (isFinite(s.total)) maxT = Math.max(maxT, s.total); else if (isFinite(s.carry)) maxT = Math.max(maxT, s.carry) }
            if (ses && isFinite(s.carry)) minC = Math.min(minC, s.carry)
            if (ses && isFinite(s.offline)) L = Math.max(L, Math.abs(s.offline))
        }
        if (ses) for (const c of clubs) if (c.ell) { minC = Math.min(minC, c.ell.mx - Math.max(c.ell.rx, c.ell.ry)); L = Math.max(L, Math.abs(c.ell.my) + Math.max(c.ell.rx, c.ell.ry)) }
        const maxYd = Math.max(step * 4, Math.ceil((maxT + 6) / step) * step)
        const lo = ses && minC < 1e8 ? Math.max(0, Math.floor((minC - 6) / step) * step) : 0
        return { maxYd: maxYd, lo: lo >= step * 2 ? lo : 0, L: L + (ses ? 5 : 4) }
    }
    readonly property real fPadL: ts.u(30)
    readonly property real fPadR: ts.u(8)
    readonly property real fSideFrac: 0.36
    readonly property real fSideMin: 8 * em               // the side view's least height: heights are exaggerated, one height line says by how much
    // round 3: TRUE SCALE. The plan view and the side view share one px per yard on both axes (feet ÷ 3), so the
    // drawn launch and descent are the measured angles. The fixed extras: the plan's inset, the gap between the
    // views, the side view's headroom for the peak label and its ground ticks.
    readonly property real fGap: ts.u(10)
    readonly property real fHead: labLH * 2.2
    readonly property real fFoot: labLH * 1.3 + ts.u(8)
    function flightApexYd() { return isFinite(flight.apexFt) ? flight.apexFt / 3 : 0 }
    // round 4b: the shot's flight as "both" views, or one ("plan": from above; "side": from the side); one scale k
    // (px per yd) on every axis, the largest the width and the height both allow
    function flightK(W, H, mode, part) {
        const sc = flightScale(mode), kW = (W - fPadL - fPadR) / (sc.maxYd - sc.lo), ap = flightApexYd()
        if (mode === "session") return Math.max(0.1, Math.min(kW, (H - ts.u(6)) / (2 * sc.L)))
        if (part === "plan") return Math.max(0.1, Math.min(kW, (H - ts.u(6)) / (2 * sc.L)))
        if (part === "side") return Math.max(0.1, Math.min(kW, (H - fHead - fFoot) / Math.max(1, ap)))
        return Math.max(0.1, Math.min(kW, (H - ts.u(6) - fGap - fHead - fFoot) / (2 * sc.L + ap)))
    }
    function flightHAt(k, mode, part) {
        const sc = flightScale(mode), ap = flightApexYd()
        if (mode === "session") return 2 * sc.L * k + ts.u(6)
        if (part === "plan") return 2 * sc.L * k + ts.u(6)
        if (part === "side") return fHead + ap * k + fFoot
        return 2 * sc.L * k + ts.u(6) + fGap + fHead + ap * k + fFoot
    }
    function flightNaturalW(H, mode, part) {
        const sc = flightScale(mode)
        return fPadL + (sc.maxYd - sc.lo) * flightK(1e6, H, mode, part || "both") + fPadR
    }
    function flightNaturalH(W, mode, part) {
        const sc = flightScale(mode), k = (W - fPadL - fPadR) / (sc.maxYd - sc.lo)
        const hh = flightHAt(k, mode, part || "both")
        return mode === "session" ? Math.min(hh, ts.u(300)) : hh
    }
    function flightGeom(W, H, mode, part) {
        const ses = mode === "session", sc = flightScale(mode), pt = ses ? "plan" : (part || "both")
        const planOn = pt !== "side", sideOn = !ses && pt !== "plan"
        const ok = flight.has === true && W > 40 && H > 40
        const kW = (W - fPadL - fPadR) / (sc.maxYd - sc.lo)
        // shot: one scale k (px per yd) for the plan view (across and along) and the side view (up and along),
        // the largest the width and the height both allow; any height left over stays below the figure
        const apex = isFinite(flight.apexFt) ? flight.apexFt : 0
        const k = flightK(W, H, mode, pt)
        const topH = ses ? H : planOn ? 2 * sc.L * k + ts.u(6) : 0
        const x0 = fPadL, y0 = topH / 2, teeX = x0 - sc.lo * k
        const sideTop = planOn ? topH + fGap : 0, ground = sideTop + fHead + apex / 3 * k
        const hStep = apex > 70 ? 25 : apex > 30 ? 10 : 5
        const hl = Math.max(hStep, Math.floor(apex / hStep) * hStep)
        const ky = k / 3                                                    // px per ft: the same scale as the yards
        const labels = [], hh = topH / 2 - ts.u(2)
        const every = (lr.arcEvery * k < ts.u(34)) ? lr.arcEvery * 2 : lr.arcEvery
        for (let d = every; d <= sc.maxYd; d += every) {
            if (d <= sc.lo) continue
            const r = d * k, yo = Math.min(hh * 0.80, r * 0.80)
            const x = teeX + Math.sqrt(Math.max(0, r * r - yo * yo))
            if (x + ts.u(7) > W - ts.u(8)) continue
            labels.push({ x: x + ts.u(7), y: y0 + yo, rot: Math.atan2(yo, x - teeX) * 180 / Math.PI - 90, text: d + " yd" })
        }
        const totY = isFinite(flight.totalYd) ? flight.totalYd : 0
        return { ok: ok, ses: ses, planOn: planOn, sideOn: sideOn, W: W, H: H, k: k, x0: x0, y0: y0, teeX: teeX, lo: sc.lo, topH: topH, hh: hh, ground: ground, sideTop: sideTop, ky: ky,
                 apexX: x0 + (isFinite(flight.apexAtX) ? flight.apexAtX : 0.5) * totY * k, apexY: ground - apex * ky,
                 landX: x0 + (isFinite(flight.carryFraction) ? flight.carryFraction : 1) * totY * k,
                 hlY: ground - hl * ky, hlText: hl + " ft", arcLabels: ok && planOn ? labels : [], maxYd: sc.maxYd, xEnd: x0 + (sc.maxYd - sc.lo) * k }
    }
    // the dispersion's landings, every club's, as { id, x, y } in the figure's coordinates
    function dispPts(G) {
        const out = []
        for (const c of clubs) for (const s of c.shots) if (isFinite(s.carry) && isFinite(s.offline)) out.push({ id: s.shotId, x: G.teeX + s.carry * G.k, y: G.y0 + s.offline * G.k })
        return out
    }
    function paintFlight(ctx, G) {
        if (!G.ok) return
        const k = G.k, x0 = G.x0, y0 = G.y0, tx = G.teeX, hand = 1
        const lw = ts.isPrint ? 0.75 : 1
        ctx.lineCap = "round"
        if (G.planOn) {                                     // round 4b: the plan view may be folded away
        ctx.save()
        ctx.beginPath(); ctx.rect(x0 - (G.lo > 0 ? 0 : ts.u(4)), 0, G.xEnd - x0 + ts.u(6), G.topH); ctx.clip()
        // arcs every 25 yd, centred on the tee (behind the break in Session)
        ctx.strokeStyle = ts.grid; ctx.lineWidth = lw
        for (let d = lr.arcEvery; d <= G.maxYd; d += lr.arcEvery) {
            if (d <= G.lo) continue
            const r = d * k, a = Math.asin(Math.min(1, G.hh / r))
            ctx.beginPath(); ctx.arc(tx, y0, r, -a, a, false); ctx.stroke()
        }
        // the target line, dashed
        ctx.save(); ctx.setLineDash(lr.targetDash); ctx.strokeStyle = ts.ink3; ctx.lineWidth = lw
        ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(G.xEnd, y0); ctx.stroke(); ctx.restore()
        if (G.ses) {
            // ellipses: the other clubs as ghosts first, the scoped club last and solid
            const cl = clubs.slice().reverse()
            for (const c of cl) {
                if (!c.ell) continue
                ctx.save()
                ctx.translate(tx + c.ell.mx * k, y0 + hand * c.ell.my * k); ctx.rotate(hand * c.ell.rot)
                ctx.scale(Math.max(0.5, c.ell.rx * k), Math.max(0.5, c.ell.ry * k))
                ctx.beginPath(); ctx.arc(0, 0, 1, 0, 2 * Math.PI, false); ctx.restore()
                ctx.globalAlpha = c.key === focusKey ? 1 : lr.ghostAlpha
                ctx.strokeStyle = c.hue; ctx.lineWidth = lr.ellipseW * (ts.isPrint ? 0.8 : 1); ctx.stroke()
                ctx.globalAlpha = 1
            }
            // landing dots: every club's own (round 3: no ellipse without its dots), the other clubs at the ghost
            // alpha first, then the scoped club; its hue, a paper ring; shot N ringed once in ink
            // round 4: a left-out shot is hollow; the hovered shot gets a light ring and the focused shot the ink ring,
            // whichever club they belong to
            const rings = []
            for (const c of cl) {
                if (c.key === focusKey) continue
                ctx.globalAlpha = lr.ghostAlpha
                for (const s of c.shots) {
                    if (!isFinite(s.carry) || !isFinite(s.offline)) continue
                    const x = tx + s.carry * k, y = y0 + hand * s.offline * k
                    dot(ctx, x, y, ts.u(lr.dot) / 2, c.hue, s.out, ts.paper)
                    if (s.shotId === focusId || s.shotId === hoverId) rings.push({ x: x, y: y, f: s.shotId === focusId })
                }
                ctx.globalAlpha = 1
            }
            if (focusClub) {
                for (const s of focusClub.shots) {
                    if (!isFinite(s.carry) || !isFinite(s.offline)) continue
                    const x = tx + s.carry * k, y = y0 + hand * s.offline * k
                    dot(ctx, x, y, ts.u(lr.dot) / 2, focusClub.hue, s.out, ts.paper)
                    if (s.shotId === focusId || s.shotId === hoverId) rings.push({ x: x, y: y, f: s.shotId === focusId })
                }
            }
            for (const q of rings) {
                if (!q.f) { hoverRing(ctx, q.x, q.y); continue }
                ctx.beginPath(); ctx.arc(q.x, q.y, ts.ringD / 2 + 1, 0, 2 * Math.PI, false); ctx.lineWidth = 1.25; ctx.strokeStyle = ts.ink; ctx.stroke()
            }
        } else {
            ctx.fillStyle = ts.ink2; ctx.fillRect(tx - 1.5, y0 - 1.5, 3, 3)
            // this shot's tracer, carry solid, roll dashed, the landing ringed once
            const tr = flight.track || [], tot = flight.totalYd, lat = flight.lateralExtentYd, cf = flight.carryFraction
            if (tr.length > 1 && isFinite(tot)) {
                ctx.strokeStyle = focusHue; ctx.lineWidth = lr.tracerW * (ts.isPrint ? 0.8 : 1)
                ctx.beginPath()
                for (let i = 0; i < tr.length; ++i) {
                    const X = tx + tr[i].x * tot * k, Y = y0 + hand * tr[i].y * lat * k
                    if (i === 0) ctx.moveTo(X, Y); else ctx.lineTo(X, Y)
                    if (tr[i].x >= cf - 1e-6) break
                }
                ctx.stroke()
                if (isFinite(flight.offlineYd) && isFinite(flight.carryYd)) {
                    const lx = tx + flight.carryYd * k, ly = y0 + hand * flight.offlineYd * k
                    const fx = tx + tot * k, fy = y0 + hand * (flight.finish ? flight.finish.z * lat : flight.offlineYd) * k
                    ctx.save(); ctx.setLineDash(lr.rollDash); ctx.lineWidth = lr.rollW
                    ctx.beginPath(); ctx.moveTo(lx, ly); ctx.lineTo(fx, fy); ctx.stroke(); ctx.restore()
                    ctx.beginPath(); ctx.arc(lx, ly, ts.ringD / 2, 0, 2 * Math.PI, false); ctx.lineWidth = 1.25; ctx.strokeStyle = ts.ink; ctx.stroke()
                }
            }
        }
        ctx.restore()
        if (G.lo > 0) {
            // the break: // on the target line where the empty run from the tee was collapsed
            ctx.strokeStyle = ts.ink2; ctx.lineWidth = 1.25
            for (const dx of [-3, 3]) { ctx.beginPath(); ctx.moveTo(x0 + dx - 3, y0 + 6); ctx.lineTo(x0 + dx + 3, y0 - 6); ctx.stroke() }
        }
        }
        if (G.ses || !G.sideOn) return
        // ── side view: ground with 25 yd ticks, one height line, the profile ──
        const tot = flight.totalYd, cf = flight.carryFraction
        ctx.strokeStyle = ts.baseline; ctx.lineWidth = lw
        ctx.beginPath(); ctx.moveTo(x0, G.ground); ctx.lineTo(G.xEnd, G.ground); ctx.stroke()
        ctx.strokeStyle = ts.ink3
        for (let d = 0; d <= G.maxYd; d += lr.arcEvery) { ctx.beginPath(); ctx.moveTo(x0 + d * k, G.ground); ctx.lineTo(x0 + d * k, G.ground + ts.u(4)); ctx.stroke() }
        ctx.save(); ctx.setLineDash(lr.refDash); ctx.strokeStyle = ts.grid; ctx.lineWidth = lw
        ctx.beginPath(); ctx.moveTo(x0, G.hlY); ctx.lineTo(G.xEnd, G.hlY); ctx.stroke(); ctx.restore()
        const pr = flight.profile || [], apex = flight.apexFt
        if (pr.length > 1 && isFinite(tot) && isFinite(apex)) {
            ctx.strokeStyle = focusHue; ctx.lineWidth = lr.tracerW * (ts.isPrint ? 0.8 : 1)
            ctx.beginPath()
            for (let i = 0; i < pr.length; ++i) {
                const X = x0 + pr[i].x * tot * k, Y = G.ground - pr[i].y * apex * G.ky
                if (i === 0) ctx.moveTo(X, Y); else ctx.lineTo(X, Y)
                if (pr[i].x >= cf - 1e-6) break
            }
            ctx.stroke()
            const lx = x0 + cf * tot * k
            ctx.save(); ctx.setLineDash(lr.rollDash); ctx.lineWidth = lr.rollW
            ctx.beginPath(); ctx.moveTo(lx, G.ground); ctx.lineTo(x0 + tot * k, G.ground); ctx.stroke(); ctx.restore()
            ctx.beginPath(); ctx.arc(lx, G.ground, ts.ringD / 2, 0, 2 * Math.PI, false); ctx.lineWidth = 1.25; ctx.strokeStyle = ts.ink; ctx.stroke()
            // the guide ticks: the device's MEASURED launch and descent angles at the two ends of the curve (true scale,
            // so they lie along the curve where the model agrees with the device)
            const la = num("lm.launchAngle"), da = num("lm.descentAngle"), Lg = 3.2 * em, rad = Math.PI / 180
            ctx.strokeStyle = ts.ink; ctx.lineWidth = 1
            if (isFinite(la)) { ctx.beginPath(); ctx.moveTo(x0, G.ground); ctx.lineTo(x0 + Lg * Math.cos(la * rad), G.ground - Lg * Math.sin(la * rad)); ctx.stroke() }
            if (isFinite(da)) { ctx.beginPath(); ctx.moveTo(lx, G.ground); ctx.lineTo(lx - Lg * Math.cos(da * rad), G.ground - Lg * Math.sin(da * rad)); ctx.stroke() }
        }
    }

    // ═══ club heads: LmClubArt's rendered heads, placed and turned by their anchors ═══
    // The art is vendored from ~/Projects/Resources (src/Gui/resources/lm/VENDORED.md) and served by the
    // LmClubArt singleton: headFor(club), image(head, view, look), anchors(head, view) — the anchor JSON with
    // its pixel fields rescaled to the vendored image — strikeOriginMm(head), ballImage(look), patchImage(mm).
    // Until the singleton is in the build, `art` is null and the figures draw without heads or balls' renders.
    function _artSingleton() { try { return LmClubArt } catch (e) { return null } }
    readonly property var art: _artSingleton()
    function headOf(club) { try { return art ? String(art.headFor(String(club || "")) || "") : "" } catch (e) { return "" } }
    // The anchor JSON in the short names the geometry below uses: W, H = image size; mm = mm_per_px;
    // pivot, fc = face_centre, le = leading_edge.mid, fp = face_plane, fn = face_normal, gy = ground_y,
    // sole = sole_contact, hb / bb = head / body bbox, ff = face_frame (mm on the face → px), sa = shaft_axis.
    // Image px, origin top left, y down. Memoised per head and view (the art does not change at run time).
    property var _anchorMemo: ({})
    function artA(head, view) {
        if (!art || !head) return null
        const key = head + "/" + view
        if (_anchorMemo[key] !== undefined) return _anchorMemo[key]
        let out = null
        try {
            const j = art.anchors(head, view)
            if (j && j.image && j.mm_per_px > 0) {
                const ff = j.face_frame
                out = { W: j.image.width, H: j.image.height, mm: j.mm_per_px, loft: j.loft_deg, lie: j.lie_deg,
                        pivot: j.pivot, fc: j.face_centre, le: j.leading_edge ? j.leading_edge.mid : j.face_centre,
                        fp: j.face_plane, fn: j.face_normal, gy: j.ground_y, sole: j.sole_contact,
                        hb: j.head_bbox, bb: j.body_bbox, sa: j.shaft_axis,
                        ff: ff ? { o: ff.origin, x: ff.x_px_per_mm, y: ff.y_px_per_mm } : null }
            }
        } catch (e) { out = null }
        _anchorMemo[key] = out
        return out
    }
    function artUrl(head, view, look) { try { return art ? String(art.image(head, view, look)) : "" } catch (e) { return "" } }
    // Foresight's face centre above our face_centre, up the face, in mm (7 iron +6.17, wedge −0.63, woods 0)
    function strikeOrigin(head) { try { const v = art ? art.strikeOriginMm(head) : 0; return isFinite(v) ? v : 0 } catch (e) { return 0 } }
    // a point or direction turned by `deg` (clockwise on screen), after the mirrors
    function headTurn(v, deg, mirH, mirV) {
        const a = deg * Math.PI / 180, x = mirH ? -v[0] : v[0], y = mirV ? -v[1] : v[1]
        return [x * Math.cos(a) - y * Math.sin(a), x * Math.sin(a) + y * Math.cos(a)]
    }
    // Place one render: scale kmm (item px per mm); image point `at` lands on (tx, ty); then the head turns
    // by `rot` degrees (clockwise) about image point `about`. `dev` is device px per item px (print 2,
    // the cast's zoom); it sets sourceSize. `look`: light, dark, line-black, line-white. `paper`: the ground
    // the shaft fades into. Returns the Image's placement plus map(p): any anchor (image px) → item coordinates.
    function headPlace(head, view, look, kmm, at, tx, ty, about, rot, mirH, mirV, dev, paper, keep) {
        const a = artA(head, view)
        if (!a || !(kmm > 0)) return null
        const k = kmm * a.mm
        const mu = p => [mirH ? a.W - p[0] : p[0], mirV ? a.H - p[1] : p[1]]
        const A = mu(at), R = mu(about)
        const ox = tx - A[0] * k, oy = ty - A[1] * k, rx = ox + R[0] * k, ry = oy + R[1] * k
        const c = Math.cos(rot * Math.PI / 180), s = Math.sin(rot * Math.PI / 180)
        const map = p => { const q = mu(p), x = ox + q[0] * k - rx, y = oy + q[1] * k - ry; return [rx + x * c - y * s, ry + x * s + y * c] }
        // the shaft is kept for 30 mm past the hosel (10 mm on the face, where it would run into the corner
        // readings) and fades into the paper over that length; the rest is cropped (it runs up in every
        // view). The fade covers the shaft only, from its axis.
        const keepMm = keep !== undefined ? keep : view === "face" ? 10 : 30
        const y0 = Math.max(0, a.hb[1] - keepMm / a.mm), y1 = a.hb[1]
        const cy0 = mirV ? 0 : y0, cy1 = mirV ? a.H - y0 : a.H
        const sx = y => a.sa[0][0] + (y - a.sa[0][1]) * (a.sa[1][0] - a.sa[0][0]) / ((a.sa[1][1] - a.sa[0][1]) || 1)
        const hw = 9 / a.mm, fx0 = Math.min(sx(y0), sx(y1)) - hw, fx1 = Math.max(sx(y0), sx(y1)) + hw
        const fadeH = (y1 - y0) * k
        return { a: a, k: k, kmm: kmm, map: map, rot: rot, rx: rx, ry: ry, mirH: mirH, mirV: mirV, dev: dev, model: head,
                 src: artUrl(head, view, look),
                 ssw: Math.max(1, Math.min(Math.round(a.W), Math.ceil(a.W * k * dev))),
                 iw: a.W * k, ih: a.H * k, cx: ox, cy: oy + cy0 * k, cw: a.W * k, ch: (cy1 - cy0) * k, srcY0: y0,
                 paper: paper, fadeTop: !mirV,
                 fade: { x: (mirH ? a.W - fx1 : fx0) * k, w: (fx1 - fx0) * k, y: mirV ? (cy1 - cy0) * k - fadeH : 0, h: fadeH } }
    }
    // a strike on the face (mm: +x toward the toe, +y up the face) → item coordinates, through face_frame
    function headFacePt(pl, xmm, ymm) {
        const f = pl.a.ff
        return pl.map([f.o[0] + xmm * f.x[0] + ymm * f.y[0], f.o[1] + xmm * f.x[1] + ymm * f.y[1]])
    }
    // the scoped club's strike ellipse (graphics.clubs[].ellipses.strike: face mm from the Foresight origin,
    // the major axis' angle from +H toward +V) in the shape headEllipsePts takes
    function strikeEllipseFace() {
        const e = strikeEll
        return e ? { meanX: e.cx, meanY: e.cy, majorSd: e.a, minorSd: e.b, tiltDeg: -(e.angleDeg || 0) } : null
    }
    // the impact map's marks: every scoped shot (left-out ones too) as { id, x, y, out } in the cell's coordinates
    function mapPts(pl) {
        const out = []
        if (!pl || !pl.a || !pl.a.ff) return out
        for (const s of scopedAll) {
            const H = shotVal(s, "lm.strikeLocation"), V = shotVal(s, "lm.strikeHeight")
            if (!isFinite(H) || !isFinite(V)) continue
            const p = strikeAt(pl, H, V)
            out.push({ id: s.shotId, x: p[0], y: p[1], out: s.out })
        }
        return out
    }
    // one shot's mark: filled in its hue with a paper ring, or (left out) a hollow ring in its hue
    function dot(ctx, x, y, r, hue, hollow, paperC) {
        ctx.beginPath(); ctx.arc(x, y, r, 0, 2 * Math.PI, false)
        if (hollow) { ctx.fillStyle = paperC; ctx.fill(); ctx.lineWidth = 1.5; ctx.strokeStyle = hue; ctx.stroke() }
        else { ctx.fillStyle = hue; ctx.fill(); ctx.lineWidth = lr.dotRing; ctx.strokeStyle = paperC; ctx.stroke() }
    }
    // the hovered shot, lit in every figure at once: a lighter second ring (the picked shot's ring stays)
    function hoverRing(ctx, x, y) { ctx.beginPath(); ctx.arc(x, y, ts.ringD / 2 + 1, 0, 2 * Math.PI, false); ctx.lineWidth = 1.25; ctx.strokeStyle = ts.ink3; ctx.stroke() }
    // an ellipse on the face as item-coordinate points, built in mm
    function headEllipsePts(pl, e) {
        const out = [], t = -e.tiltDeg * Math.PI / 180, c = Math.cos(t), s = Math.sin(t)
        for (let i = 0; i <= 72; ++i) {
            const u = i / 72 * 2 * Math.PI, x = Math.max(0.3, e.majorSd) * Math.cos(u), y = Math.max(0.3, e.minorSd) * Math.sin(u)
            out.push(headFacePt(pl, e.meanX + x * c - y * s, e.meanY + x * s + y * c))
        }
        return out
    }
    // the face's width heel to toe in mm (body, no hosel), from the top or the face view
    function headWidthMm(head, view) {
        const a = artA(head, view)
        if (!a) return NaN
        return view === "top" ? (a.bb[3] - a.bb[1]) * a.mm : (a.bb[2] - a.bb[0]) * a.mm
    }
    readonly property real ballR_mm: 21.335

    // ═══ the strike origin, the impact patch, the Foresight club markers, the ball ═══
    // The strike origin is LmClubArt.strikeOriginMm(head): Foresight reads an iron's strike height from
    // midway between score lines 6 and 7, which sits above our face_centre.
    // FACE MARKS, per head (mm on the face from our face_centre, +x toe; from Resources' clubs.py groove
    // geometry, see docs/design/lm_panel_fsx.md): toe / heel = one 5 mm marker in from the ends of the score
    // line nearest the centre; lo / hi = the top and bottom of the strike zone about the Foresight centre.
    // patch: the contact diameter, 25 mm for woods and 20 mm for irons and wedges (an estimate). tone: the
    // patch ink — the woods' faces are a dark insert (light ink), the irons' a light blasted steel (dark ink).
    readonly property var faceMarks: ({
        driver:  { toe: 36.1, heel: -36.1, lo: -12.5, hi: 12.5, patch: 25, tone: "light" },
        "3wood": { toe: 24.6, heel: -23.9, lo: -10.3, hi: 10.3, patch: 25, tone: "light" },
        fairway: { toe: 24.6, heel: -23.9, lo: -10.3, hi: 10.3, patch: 25, tone: "light" },
        hybrid:  { toe: 24.1, heel: -25.0, lo: -8.45, hi: 8.45, patch: 25, tone: "light" },
        iron:    { toe: 20.0, heel: -19.8, lo: -17.1, hi: 17.1, patch: 20, tone: "dark" },
        wedge:   { toe: 18.9, heel: -20.0, lo: -13.7, hi: 13.7, patch: 20, tone: "dark" }
    })
    function faceRef(head) {
        const r = Object.assign({ sc: strikeOrigin(head) }, faceMarks[head] || faceMarks.iron)
        try { if (art && typeof art.patchMm === "function") { const p = art.patchMm(head); if (p > 0) r.patch = p } } catch (e) {}
        return r
    }
    function patchMm(head) { return faceRef(head).patch }
    // the patch as path data (0.01 mm units, origin the contact centre, y down), for a Shape filled in a theme colour
    function patchData(mm) { try { return art && typeof art.patchPath === "function" ? String(art.patchPath(mm) || "") : "" } catch (e) { return "" } }
    // a marker: a circle of lr.markerDmm in 0.01 mm units (the face paths' units)
    readonly property string markerSvg: { const r = Math.round(lr.markerDmm * 50); return "M " + r + " 0 A " + r + " " + r + " 0 1 0 " + (-r) + " 0 A " + r + " " + r + " 0 1 0 " + r + " 0 Z" }
    // Foresight's marker layouts (graphics.device.markers): four dots (GC Quad, QuadMAX, Falcon, GCHawk), one
    // dot (GC3, HMT on its own)
    function markerPts(head) {
        // LmClubArt.markers(head): [{ name: toe | heel | lo | hi | one, xMm, yMm }], face_frame mm from our face_centre
        try {
            if (art && typeof art.markers === "function" && markerKind) {
                const want = markerKind === "four" ? ["toe", "heel", "lo", "hi"] : ["one"], out = []
                for (const m of Array.from(art.markers(head) || [])) if (want.indexOf(m.name) >= 0 && isFinite(m.xMm) && isFinite(m.yMm)) out.push([m.xMm, m.yMm])
                if (out.length) return out
            }
        } catch (e) {}
        // until the art publishes them: the face marks below (Foresight's fiducial guide, Resources' groove geometry)
        const r = faceRef(head)
        if (markerKind === "four") return [[r.toe, r.sc], [r.heel, r.sc], [r.toe, r.sc + r.hi], [r.toe, r.sc + r.lo]]
        if (markerKind === "one") return [[0, r.sc + r.hi]]
        return []
    }
    // theme tones: "light" and "dark" ink, from the theme in both modes
    function toneColor(t) {
        if (t === "light") return ts.isPrint ? ts.paper : Theme.dark ? Theme.colorText : Theme.colorSurface
        return ts.isPrint || !Theme.dark ? Theme.colorText : Theme.colorBg
    }
    // a role token → a theme colour: "accent", "series:N" (Theme.chartSeries), "ink"
    function roleColor(tok) {
        const t = String(tok || "")
        if (t.indexOf("series:") === 0) return Theme.chartSeriesColor(parseInt(t.slice(7)))
        if (t === "ink") return ts.ink
        return Theme.colorAccent
    }
    // a strike in mm (+x toe, +y up, from the Foresight centre) → item coordinates
    function strikeAt(pl, H, V) { return headFacePt(pl, H, V + faceRef(pl.model).sc) }
    // the affine map from a face path (0.01 mm units, y down) centred at (xmm, ymm) on the face → item px, through
    // face_frame: true mm scale, foreshortened by loft, turned with the head
    function faceMatrix(pl, xmm, ymm) {
        const P0 = headFacePt(pl, xmm, ymm), Px = headFacePt(pl, xmm + 1, ymm), Py = headFacePt(pl, xmm, ymm + 1), u = 0.01
        return [(Px[0] - P0[0]) * u, (Px[1] - P0[1]) * u, -(Py[0] - P0[0]) * u, -(Py[1] - P0[1]) * u, P0[0], P0[1]]
    }
    function facePxPerMm(pl) { const P0 = headFacePt(pl, 0, 0), Px = headFacePt(pl, 1, 0); return Math.hypot(Px[0] - P0[0], Px[1] - P0[1]) }
    function ballLook() { return ts.isPrint ? "light" : Theme.dark ? "dark" : "light" }
    function ballUrl() { try { return art ? String(art.ballImage(ballLook())) : "" } catch (e) { return "" } }
    // the ball's radius as a fraction of its render's side (ball_side.json; Resources frames it 957 of 2048 px)
    readonly property real ballRadiusFrac: {
        try {
            const j = art && typeof art.ballAnchors === "function" ? art.ballAnchors() : null
            if (j && j.image && j.image.width > 0 && j.radius_px > 0) return j.radius_px / j.image.width
        } catch (e) {}
        return 0.46729
    }

    // one render as an Item: cropped above the hosel with the shaft fading into the paper, turned about its anchor
    component HeadImage: Item {
        id: hi
        property var pl
        readonly property bool on: !!pl
        visible: on
        x: on ? pl.cx : 0; y: on ? pl.cy : 0
        width: on ? pl.cw : 1; height: on ? pl.ch : 1
        transform: Rotation { origin.x: hi.on ? hi.pl.rx - hi.pl.cx : 0; origin.y: hi.on ? hi.pl.ry - hi.pl.cy : 0; angle: hi.on ? hi.pl.rot : 0 }
        // cropped through sourceClipRect, so the turned item needs no clip of its own
        Image {
            anchors.fill: parent
            source: hi.on ? hi.pl.src : ""
            sourceSize.width: hi.on ? hi.pl.ssw : 0
            sourceClipRect: hi.on ? Qt.rect(0, hi.pl.srcY0 * hi.pl.ssw / hi.pl.a.W, hi.pl.ssw, (hi.pl.a.H - hi.pl.srcY0) * hi.pl.ssw / hi.pl.a.W) : Qt.rect(0, 0, 0, 0)
            fillMode: Image.PreserveAspectFit
            mirror: hi.on && hi.pl.mirH; mirrorVertically: hi.on && hi.pl.mirV
            smooth: true; mipmap: true
        }
        Rectangle {                 // the shaft's fade into the paper
            visible: hi.on && hi.pl.fade.h > 0
            x: hi.on ? hi.pl.fade.x : 0; y: hi.on ? hi.pl.fade.y : 0
            width: hi.on ? hi.pl.fade.w : 0; height: hi.on ? hi.pl.fade.h : 0
            gradient: Gradient {
                GradientStop { position: 0; color: hi.on && hi.pl.fadeTop ? hi.pl.paper : "transparent" }
                GradientStop { position: 1; color: hi.on && hi.pl.fadeTop ? "transparent" : (hi.on ? hi.pl.paper : "transparent") }
            }
        }
    }


    // a path on the face (0.01 mm units, y down) placed by an affine map [a, b, c, d, e, f] → item px
    component FacePath: Shape {
        id: fpth
        property var m: null
        property string path: ""
        property color fill: "transparent"
        property color stroke: "transparent"
        property real strokeLocal: -1
        visible: !!m && path.length > 0
        width: 1; height: 1
        transform: Matrix4x4 { matrix: fpth.m ? Qt.matrix4x4(fpth.m[0], fpth.m[2], 0, fpth.m[4], fpth.m[1], fpth.m[3], 0, fpth.m[5], 0, 0, 1, 0, 0, 0, 0, 1) : Qt.matrix4x4() }
        ShapePath {
            fillColor: fpth.fill; fillRule: ShapePath.OddEvenFill
            strokeColor: fpth.stroke; strokeWidth: fpth.strokeLocal
            PathSvg { path: fpth.path }
        }
    }
    // the face's ink: the device's club markers (both views), and in This shot the impact patch at the strike with
    // its centre marker. All through face_frame: true mm, foreshortened by the loft, turned with the head.
    component FaceInk: Item {
        id: fi
        property var h
        property var pl: null
        property string mode: "shot"
        anchors.fill: parent
        readonly property var ref: pl ? h.faceRef(pl.model) : null
        readonly property real hS: h.num("lm.strikeLocation")
        readonly property real vS: h.num("lm.strikeHeight")
        readonly property bool hasStrike: !!pl && !!ref && mode === "shot" && isFinite(hS) && isFinite(vS)
        readonly property var sp: hasStrike ? [hS, vS + ref.sc] : null          // the strike, mm from fc
        readonly property real kpm: pl ? h.facePxPerMm(pl) : 1
        readonly property color patchInk: ref ? h.toneColor(ref.tone) : "transparent"
        readonly property color under: ref ? h.toneColor(ref.tone === "light" ? "dark" : "light") : "transparent"
        layer.enabled: !h.ts.isPrint && !!pl
        layer.samples: 4
        layer.textureSize: Qt.size(Math.ceil(width * Math.max(1, Screen.devicePixelRatio) * h.zoom), Math.ceil(height * Math.max(1, Screen.devicePixelRatio) * h.zoom))
        Repeater {                      // the club markers: 5 mm silver retroreflective dots, near-white, subtle
            model: fi.pl ? fi.h.markerPts(fi.pl.model) : []
            delegate: FacePath {
                required property var modelData
                m: fi.h.faceMatrix(fi.pl, modelData[0], modelData[1])
                path: fi.h.markerSvg
                fill: Qt.alpha(fi.h.toneColor("light"), fi.h.lr.markerAlpha)
                stroke: Qt.alpha(fi.h.toneColor("dark"), 0.35)
                strokeLocal: 0.8 / Math.max(0.01, fi.kpm * 0.01)       // 0.8 px whatever the scale
            }
        }
        FacePath {                      // the impact patch (LmClubArt.patchPath), tinted with a theme tone that reads on this face
            m: fi.sp ? fi.h.faceMatrix(fi.pl, fi.sp[0], fi.sp[1]) : null
            path: fi.ref ? fi.h.patchData(fi.ref.patch) : ""
            fill: Qt.alpha(fi.patchInk, fi.h.lr.patchAlpha)
        }
        Rectangle {                     // its centre marker: a theme role colour, haloed in the face's own tone
            visible: !!fi.sp
            readonly property var p: fi.sp ? fi.h.headFacePt(fi.pl, fi.sp[0], fi.sp[1]) : [0, 0]
            readonly property real d: 2 * Math.round(Math.max(5, Math.min(10, fi.h.lr.centreDmm * fi.kpm)) / 2)
            x: Math.round(p[0]) - d / 2; y: Math.round(p[1]) - d / 2; width: d; height: d; radius: d / 2
            color: fi.h.roleColor(fi.h.lr.centreMark)
            border.width: 1; border.color: Qt.alpha(fi.under, 0.9)
            antialiasing: true
        }
    }
    // a ball render at radius r (item px), centred on (x, y), turned by rot degrees
    // (LmClubArt.ballImage: the plain ball, side view, framed as Resources frames it — radius 957 of 2048 px)
    component BallImage: Image {
        id: bi
        property var b: null
        property var h
        readonly property real side: b ? b.r / h.ballRadiusFrac : 1
        readonly property real dev: (h.ts.isPrint ? 2 : Math.max(1, Screen.devicePixelRatio)) * h.zoom
        readonly property string url: b ? h.ballUrl() : ""
        visible: url.length > 0
        x: b ? b.x - side / 2 : 0; y: b ? b.y - side / 2 : 0
        width: side; height: side
        rotation: b && b.rot ? b.rot : 0
        source: url
        sourceSize: Qt.size(Math.max(1, Math.min(2048, Math.ceil(side * dev))), Math.max(1, Math.min(2048, Math.ceil(side * dev))))
        smooth: true; mipmap: true
    }

    // A's placement per cell: the ball keeps the mock's size and the head is drawn to the ball's scale
    // (plan, side); on the face the head keeps the mock's face width. Prints use the light render, as the
    // mock's print set the same solid head as light paper.
    function headLookA() { return ts.isPrint ? "light" : Theme.dark ? "dark" : "light" }
    // the plan and side views' ball radius (round 4b: larger, so the heads read beside the big face)
    function ballR(w, h) { return Math.max(5, Math.min(w * lr.ballFrac, h * lr.ballFracH)) }
    // the corner readings of a figure as boxes (x0, y0, x1, y1) in its drawing area: the reading's own
    // width (label or value, the wider), not the column it may use
    function annoBoxes(kind, w, h) {
        const out = []
        for (const o of annosFor(kind, w, h)) {
            const lw = labelW(o.label), vw = valueW(o.value + (o.dir ? "  " + o.dir : ""), o.quiet || o.small)
            const ww = Math.min(o.w, Math.max(lw, vw)) + ts.u(4)
            const x0 = o.right ? o.x - ww : o.x
            const hh = annoH + (o.warn ? labLH + ts.u(lr.annoGap) : 0)
            const y0 = Math.max(0, Math.min(o.y, h - hh))
            out.push([x0, y0, x0 + ww, y0 + hh + ts.u(2)])
        }
        return out
    }
    FontMetrics { id: fmFigL; font: root.ts.fFigL }
    FontMetrics { id: fmFig; font: root.ts.fFig }
    function labelW(s) { return fmLab.advanceWidth(String(s).toUpperCase()) + String(s).length * lr.tileLabelTracking }
    function valueW(s, quiet) { return (quiet ? fmFig : fmFigL).advanceWidth(String(s)) }
    // does the head's body (its rotated body box) run into a reading, or out of the cell?
    function headHits(pl, boxes, w, h) {
        if (!pl || !pl.a || !pl.map) return false
        const b = pl.a.bb, cs = [[b[0], b[1]], [b[2], b[1]], [b[2], b[3]], [b[0], b[3]]].map(p => pl.map(p))
        const x0 = Math.min.apply(null, cs.map(c => c[0])), x1 = Math.max.apply(null, cs.map(c => c[0]))
        const y0 = Math.min.apply(null, cs.map(c => c[1])), y1 = Math.max.apply(null, cs.map(c => c[1]))
        if (x0 < 0 || y1 > h + 1) return true
        for (const r of boxes) if (x0 < r[2] && x1 > r[0] && y0 < r[3] && y1 > r[1]) return true
        // the hosel and the kept shaft stub: points along the shaft axis from its foot (the ground) to the crop, 8 mm wide
        const a = pl.a, yT = a.sa[0][1], yC = pl.srcY0
        const sx = y => a.sa[0][0] + (y - a.sa[0][1]) * (a.sa[1][0] - a.sa[0][0]) / ((a.sa[1][1] - a.sa[0][1]) || 1)
        const hw = 4 / a.mm * pl.k
        for (let i = 0; i <= 12; ++i) {
            const y = yT + (yC - yT) * i / 12, p = pl.map([sx(y), y])
            for (const r of boxes) if (p[0] + hw > r[0] && p[0] - hw < r[2] && p[1] + hw > r[1] && p[1] - hw < r[3]) return true
        }
        return false
    }
    function clubGeom(kind, w, h) {
        if (!hasData || !focusShot || !(w > 0) || !(h > 0)) return null
        const hand = g.leftHanded ? -1 : 1, G = lr.angleGain
        const model = focusHead, look = headLookA()
        // without the art (LmClubArt not in the build, or no anchors for this head) Path and Loft still draw
        // their balls and lines; the faces draw nothing
        const put = (pl, extra) => Object.assign(pl || {}, { model: model }, extra)
        if (kind === "axis") {                                          // no head: the ball's side render, turned by the axis
            const axis = num("lm.spinAxis"), R = Math.min(w * 0.14, h * 0.22)
            return { model: model, balls: [{ x: w * 0.5, y: h * 0.40, r: R, view: "side", rot: hand * (isFinite(axis) ? axis : 0) * G }] }
        }
        const dev = (ts.isPrint ? 2 : Math.max(1, Screen.devicePixelRatio)) * zoom, paper = ts.isPrint ? "#FFFFFF" : Theme.colorSurface
        const rb0 = ballR(w, h)
        // HEAD AND ANNOTATIONS NEVER SHARE GROUND. A deep head (a driver from above is ~115 mm back from the face)
        // drawn at the ball's scale runs into the corner readings, so Path and Loft step the whole drawing (ball,
        // head, lines) down in scale, about the ball, until the head's body clears every reading's box. The iron
        // and the wedge clear at full size, as signed off; the cells stay one size and the readings stay put.
        const boxes = annoBoxes(kind, w, h)
        // The shaft stub above the hosel shortens first (it fades out anyway); only then does the drawing shrink.
        // Impact may also lower the head toward the cell's floor (its hosel rises toward the top corner).
        const fit = (build, keeps, drops) => {
            let out = null
            for (let s = 1; s >= 0.5 - 1e-9; s -= 0.05)
                for (const dy of (drops || [0]))
                    for (const k of keeps) { out = build(s, k, dy); if (!headHits(out, boxes, w, h)) return out }
            return out
        }
        if (kind === "top") return fit((s, keep) => {
            const rb = rb0 * s
            const a = artA(model, "top"), face = num("lm.faceAngle")
            const rot = hand * (isFinite(face) ? face : 0) * G, mirV = hand < 0
            const bx = w * 0.46, by = h * 0.50, n = a ? headTurn(a.fn, rot, false, mirV) : [Math.cos(rot * Math.PI / 180), Math.sin(rot * Math.PI / 180)]
            const q = [bx - rb * n[0], by - rb * n[1]]                 // the face centre touches the ball
            const pl = a ? headPlace(model, "top", look, rb / ballR_mm, a.pivot, q[0], q[1], a.pivot, rot, false, mirV, dev, paper, keep) : null
            const st = num("lm.launchDirection"), sa = hand * (isFinite(st) ? st : 0) * G * Math.PI / 180, D = w * 0.36
            const b2 = { x: bx + D * Math.cos(sa), y: by + D * Math.sin(sa), r: rb, view: "front" }
            return put(pl, { rb: rb, bx: bx, by: by, q: q, n: n, b2: b2, balls: isFinite(st) ? [{ x: bx, y: by, r: rb, view: "front" }, b2] : [{ x: bx, y: by, r: rb, view: "front" }] })
        }, [30, 20, 10, 0])
        if (kind === "side") return fit((s, keep) => {
            const rb = rb0 * s
            const a = artA(model, "side"), loft = num("lm.dynamicLoft")
            const ground = h * 0.68, bx = w * 0.42, by = ground - rb, kmm = rb / ballR_mm
            let pl = null, n = [1, 0]
            if (a) {
                const rot = a.loft - (isFinite(loft) ? loft : a.loft)       // the render's loft → this shot's dynamic loft
                const k = kmm * a.mm
                n = headTurn(a.fn, rot, false, false)
                const py = ground - (a.gy - a.pivot[1]) * k                  // the render's ground on ours
                const px = bx - (rb - (by - py) * n[1]) / n[0]               // the face plane tangent to the ball
                pl = headPlace(model, "side", look, kmm, a.pivot, px, py, a.pivot, rot, false, false, dev, paper, keep)
            }
            const la = num("lm.launchAngle"), aa = -(isFinite(la) ? la : 0) * Math.PI / 180, D = w * 0.34
            const b2 = { x: bx + D * Math.cos(aa), y: by + D * Math.sin(aa), r: rb, view: "side" }
            return put(pl, { rb: rb, bx: bx, by: by, ground: ground, q: [bx - rb * n[0], by - rb * n[1]], n: n, b2: b2,
                             balls: isFinite(la) ? [{ x: bx, y: by, r: rb, view: "side" }, b2] : [{ x: bx, y: by, r: rb, view: "side" }] })
        }, [30, 20, 10, 0])
        const rb = rb0
        const a = artA(model, "face"), wmm = headWidthMm(model, "face")
        if (!a || !(wmm > 0)) return null
        if (kind === "map") {                                           // Session: the face square on, no lie, as large as fits
            const fw = Math.min(w * lr.mapFaceW, h * lr.mapFaceH), cx = w * 0.48, cy = h * 0.60
            const pl = headPlace(model, "face", look, fw / wmm, a.fc, cx, cy, a.sole, 0, hand < 0, false, dev, paper)
            return pl ? put(pl, { rb: rb, fcx: cx, fcy: cy, fw: fw }) : null
        }
        // faceQ — Impact, one size with the others, FSX's corner readings: the head (toe to hosel) between the
        // corner columns, centred in the height; toe up turns the head about the sole contact. Lie is drawn TRUE,
        // never × angleGain: a tilted head reads at a glance, so an exaggerated one alarms (Mark, 17 Oct)
        const lie = num("lm.lieAngle"), cw = lr.faceCornerEm * em
        return fit((s, keep, dy) => {
            const fw = faceQW(w, h) * s
            const cx = cw + ((w - 2 * cw) - 1.12 * fw) / 2 + 0.44 * fw, cy = (h - 0.71 * fw) / 2 + 0.52 * fw + dy * h
            const rot = hand * (isFinite(lie) ? lie : 0)
            const pl = headPlace(model, "face", look, fw / wmm, a.fc, cx, cy, a.sole, rot, hand < 0, false, dev, paper, keep)
            return pl ? put(pl, { rb: rb, fcx: cx, fcy: cy, fw: fw }) : null
        }, [10, 5, 0], [0, 0.04, 0.08, 0.12, 0.16, 0.2])
    }

    // the club-view annotations per cell, as data: label, value, where (x, y in px), width, alignment
    function annosFor(kind, w, h) {
        const aw = Math.max(7 * em, w * 0.30), o = []
        const roomy = w / em >= 22
        const A = (label, key, x, y, right, extra) => (extra && extra.quiet && !roomy) ? 0 : o.push(Object.assign({ label: label, value: figParts(key).num, dir: figParts(key).dir, x: x, y: y, w: aw, right: !!right, warn: isOut(key) }, extra || {}))
        if (kind === "top") {
            // the head's toe reaches the bottom-left corner, so that corner stays clear (round 2): Face to path
            // sits bottom centre under the ball, Face (quiet) top centre
            A("Club path", "lm.clubPath", w * 0.02, h * 0.05)
            A("Face", "lm.faceAngle", w * 0.52, h * 0.05, false, { quiet: true, w: 6 * em })
            A("Face to path", "lm.faceToPath", w * 0.44, h * 0.70, false, { w: 8 * em })
            A("Start", "lm.launchDirection", w * 0.98, h * 0.70, true)
            A("Spin axis", "lm.spinAxis", w * 0.98, h * 0.05, true)
        } else if (kind === "side") {
            A("Attack", "lm.attackAngle", w * 0.02, h * 0.05)
            A("Dyn. loft", "lm.dynamicLoft", w * 0.47, h * 0.05, false, { w: 7 * em })
            A("Spin", "lm.spinRate", w * 0.98, h * 0.05, true, { label: "Spin (rpm)" })
            A("Launch", "lm.launchAngle", w * 0.98, h * 0.72, true)
            A("Spin loft", "lm.spinLoft", w * 0.02, h * 0.72, false, { quiet: true })
        } else if (kind === "axis") {
            A("Spin axis", "lm.spinAxis", w * 0.02, h * 0.72)
            A("Total spin (rpm)", "lm.spinRate", w * 0.98, h * 0.72, true)
            A("Back spin", "lm.backSpin", w * 0.02, h * 0.05, false, { quiet: true })
            A("Side spin", "lm.sideSpin", w * 0.98, h * 0.05, true, { quiet: true })
        } else if (kind === "faceQ") {
            // round 4c: FSX's corners, the same grammar as Spin: V and H impact over, Lie and the strike under
            const cw = lr.faceCornerEm * em
            A("V impact", "lm.strikeHeight", w * 0.02, h * 0.05, false, { w: cw })
            A("H impact", "lm.strikeLocation", w * 0.98, h * 0.05, true, { w: cw })
            A("Lie", "lm.lieAngle", w * 0.02, h * 0.72, false, { w: cw })
            o.push({ label: "Strike", value: strike.has ? strike.name : "-", dir: strike.has ? "inferred" : "", x: w * 0.98, y: h * 0.72, w: cw, right: true, warn: false })
        } else if (kind === "map") {
            const ms = k => { const st = statsFor(k); return st ? dirStr(k, fmtKey(k, st.mean)) + (st.hasSpread ? "  ±" + fmtKey(k, st.sd) : "") : "-" }
            const mw = 10 * em, side = w >= 2 * mw + ts.u(12)
            o.push({ label: "H mm · avg ± SD", value: ms("lm.strikeLocation"), x: w * 0.02, y: 0, w: mw, right: false, warn: false })
            o.push({ label: "V mm · avg ± SD", value: ms("lm.strikeHeight"), x: side ? w * 0.02 + mw + ts.u(12) : w * 0.02, y: side ? 0 : annoH + ts.u(2), w: mw, right: false, warn: false })
        }
        return o
    }
    // the figure around the head, in two layers: `under` the render (references, the path and attack lines,
    // which the head hides where it passes), `over` it (start and launch, loft, lie, the balls, the strike).
    function paintClub(ctx, kind, w, h, layer, pl) {
        if (!hasData) return
        const ink = ts.ink, ink2 = ts.ink2, ink3 = ts.ink3, paper = ts.isPrint ? "#FFFFFF" : Theme.colorBg
        const lw = ts.isPrint ? 0.75 : 1, rad = Math.PI / 180, G = lr.angleGain, hand = g.leftHanded ? -1 : 1
        const rb = pl && pl.rb > 0 ? pl.rb : ballR(w, h)
        const under = layer === "under"
        ctx.lineCap = "round"; ctx.lineJoin = "round"
        function dash(x1, y1, x2, y2, col) { ctx.save(); ctx.setLineDash(lr.refDash); ctx.strokeStyle = col; ctx.lineWidth = lw; ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke(); ctx.restore() }
        function line(x1, y1, x2, y2, col, wd) { ctx.strokeStyle = col; ctx.lineWidth = wd || lw; ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2, y2); ctx.stroke() }
        function head(x, y, a, sz, col) { ctx.save(); ctx.translate(x, y); ctx.rotate(a); ctx.fillStyle = col; ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(-sz, -sz * 0.45); ctx.lineTo(-sz, sz * 0.45); ctx.closePath(); ctx.fill(); ctx.restore() }
        function ball(x, y, r) { ctx.beginPath(); ctx.arc(x, y, r, 0, 2 * Math.PI, false); ctx.fillStyle = paper; ctx.fill(); ctx.lineWidth = 1.25; ctx.strokeStyle = ink; ctx.stroke() }
        function spinArrow(x, y, r, a0, a1, cw) {   // an arc round a ball with its arrowhead at the end it turns to
            ctx.beginPath(); ctx.arc(x, y, r, a0 * rad, a1 * rad, false); ctx.lineWidth = 1.25; ctx.strokeStyle = ink2; ctx.stroke()
            const ae = (cw ? a1 : a0) * rad, ex = x + r * Math.cos(ae), ey = y + r * Math.sin(ae)
            head(ex, ey, ae + (cw ? Math.PI / 2 : -Math.PI / 2), ts.u(6), ink2)
        }
        // no ball render in the build: the balls are drawn here, paper with an ink ring
        if (!under && pl && pl.balls && !ballUrl()) for (const b of pl.balls) ball(b.x, b.y, b.r)
        if (kind === "top") {
            if (!pl) return
            const bx = pl.bx, by = pl.by, q = pl.q
            const path = num("lm.clubPath"), start = num("lm.launchDirection"), axis = num("lm.spinAxis")
            if (under) {
                dash(Math.max(w * 0.02, bx - 5 * rb), by, Math.min(w * 0.98, pl.b2.x + 2.5 * rb), by, ink3)   // round 4b: the drawing's extent
                // the club path, through the face centre where it meets the ball
                if (isFinite(path)) {
                    const a = hand * path * G * rad, L = w * 0.2, c = Math.cos(a), s = Math.sin(a), e = rb + L * 0.45
                    line(q[0] - L * c, q[1] - L * s, q[0] + e * c, q[1] + e * s, ink2, 1.25)
                    head(q[0] + e * c, q[1] + e * s, a, ts.u(6), ink2)
                }
                return
            }
            // the start line from the face centre to the second ball, and that ball's turn (the spin axis' sign).
            // The balls themselves are the ball renders (BallImage), under this layer.
            if (isFinite(start)) {
                const a = hand * start * G * rad, ex = pl.b2.x, ey = pl.b2.y
                line(q[0], q[1], ex - rb * Math.cos(a), ey - rb * Math.sin(a), focusHue, 1.5)
                if (isFinite(axis)) spinArrow(ex, ey, rb * 1.45, -160, -20, axis * hand >= 0)
            }
        } else if (kind === "side") {
            if (!pl) return
            const ground = pl.ground, bx = pl.bx, by = pl.by, q = pl.q
            const aoa = num("lm.attackAngle"), launch = num("lm.launchAngle")
            const le = pl.a ? pl.map(pl.a.le) : [q[0], ground]          // no art: the face's foot under the contact
            if (under) {
                dash(Math.max(w * 0.02, le[0] - 6 * rb), ground, Math.min(w * 0.98, pl.b2.x + 2.5 * rb), ground, ink3)
                dash(le[0], ground, le[0], Math.max(h * 0.08, ground - 6 * rb), ink3)
                // the attack angle, through the face centre
                if (isFinite(aoa)) {
                    const a = -aoa * rad, L = w * 0.24, c = Math.cos(a), s = Math.sin(a), e = rb + L * 0.35
                    line(q[0] - L * c, q[1] - L * s, q[0] + e * c, q[1] + e * s, ink2, 1.25)
                    head(q[0] + e * c, q[1] + e * s, a, ts.u(6), ink2)
                }
                return
            }
            // the loft line: the face plane, from the leading edge up, at this shot's dynamic loft
            if (pl.a) {
                const f0 = pl.map(pl.a.fp[0]), f1 = pl.map(pl.a.fp[1]), fl = Math.hypot(f1[0] - f0[0], f1[1] - f0[1]) || 1
                const ux = (f1[0] - f0[0]) / fl, uy = (f1[1] - f0[1]) / fl, Lf = Math.max(0, le[1] - (h * 0.05 + annoH + ts.u(4))) / Math.max(0.2, -uy)   // stops under the top readings
                line(le[0], le[1], le[0] + ux * Lf, le[1] + uy * Lf, ink2, 1.25)
            }
            if (isFinite(launch)) {
                const a = -launch * rad, ex = pl.b2.x, ey = pl.b2.y
                line(q[0], q[1], ex - rb * Math.cos(a), ey - rb * Math.sin(a), focusHue, 1.5)
                spinArrow(ex, ey, rb * 1.45, -160, -20, false)       // backspin: the ball's top turns back
            }
        } else if (kind === "axis") {
            if (under) return
            // the ball is its side render (BallImage, under this layer), turned by the axis × gain with its
            // parting line; the axis is drawn over it in ink, through the centre
            const cx = w * 0.5, cy = h * 0.40, R = Math.min(w * 0.14, h * 0.22)
            dash(Math.max(w * 0.22, cx - R * 3), cy, cx - R * 1.95, cy, ink3); dash(cx + R * 1.95, cy, Math.min(w * 0.78, cx + R * 3), cy, ink3)
            const axis = num("lm.spinAxis"), a = hand * (isFinite(axis) ? axis : 0) * G * rad
            line(cx - R * 1.9 * Math.cos(a), cy - R * 1.9 * Math.sin(a), cx + R * 1.9 * Math.cos(a), cy + R * 1.9 * Math.sin(a), ink2, 1.25)
            if (isFinite(axis)) {
                const ux = Math.sin(a), uy = -Math.cos(a)
                line(cx + ux * R, cy + uy * R, cx + ux * R * 1.75, cy + uy * R * 1.75, ink, 2)
                head(cx + ux * R * 1.95, cy + uy * R * 1.95, Math.atan2(uy, ux), ts.u(8), ink)
            }
        } else if (kind === "faceQ") {
            if (!pl) return
            // the reference cross runs through the FORESIGHT centre (an iron's score lines 6–7), not our fc
            const fs = strikeAt(pl, 0, 0), fw = pl.fw
            if (under) { dash(Math.max(lr.faceCornerEm * em, fs[0] - fw * 0.6), fs[1], Math.min(w - lr.faceCornerEm * em * 0.5, fs[0] + fw * 0.8), fs[1], ink3); return }
            dash(fs[0], fs[1] - fw * 0.31, fs[0], fs[1] + fw * 0.31, ink3)
            // the lie line through that centre, turned with the head (true, as the head is)
            const lie = num("lm.lieAngle")
            if (isFinite(lie)) { const a = hand * lie * rad, L = fw * 0.72
                                 // the big face: the line stops short of the annotation column
                                 const lx = lr.faceCornerEm * em
                                 const L0 = lx > -1e8 ? Math.min(L, Math.max(0, fs[0] - lx - ts.u(4)) / Math.max(0.2, Math.cos(a))) : L
                                 line(fs[0] - L0 * Math.cos(a), fs[1] - L0 * Math.sin(a), fs[0] + L * Math.cos(a), fs[1] + L * Math.sin(a), ink2, 1.25) }
            // this strike is the impact patch and its centre marker (FaceInk, a layer under this one); single shot only
        } else if (kind === "map") {
            if (!pl) return
            const fs = strikeAt(pl, 0, 0), fw = pl.fw
            if (under) { dash(w * 0.04, fs[1], w * 0.96, fs[1], ink3); return }
            dash(fs[0], fs[1] - fw * 0.30, fs[0], fs[1] + fw * 0.30, ink3)
            const sc = faceRef(pl.model).sc
            // the ellipse enclosing every scoped strike (round 3), from the Foresight centre, outline only, in the club's hue
            const se = strikeEllipseFace()
            if (se) {
                const pts = headEllipsePts(pl, Object.assign({}, se, { meanY: se.meanY + sc }))
                ctx.beginPath(); pts.forEach((p, i) => i ? ctx.lineTo(p[0], p[1]) : ctx.moveTo(p[0], p[1])); ctx.closePath()
                ctx.lineWidth = lr.ellipseW * (ts.isPrint ? 0.8 : 1); ctx.strokeStyle = focusHue; ctx.stroke()
            }
            // every scoped strike: the club's hue with a paper ring (the dispersion's dot), a left-out one hollow;
            // the hovered shot a light ring; this shot ringed once in ink
            let fp = null
            for (const q of mapPts(pl)) {
                const p = [q.x, q.y], r = ts.u(lr.dot) / 2
                dot(ctx, q.x, q.y, r, focusHue, q.out, paper)
                if (q.id === hoverId && q.id !== focusId) hoverRing(ctx, q.x, q.y)
                if (q.id === focusId) fp = p
            }
            if (fp) {
                ctx.beginPath(); ctx.arc(fp[0], fp[1], ts.ringD / 2 + 1, 0, 2 * Math.PI, false); ctx.lineWidth = 3; ctx.strokeStyle = Qt.alpha(paper, 0.85); ctx.stroke()
                ctx.lineWidth = 1.25; ctx.strokeStyle = ink; ctx.stroke()
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════════
    // ROUND 4 — INTERACTION BLOCKS. Every target is ≥ lr.hitPx (Theme.sp) for touch; print and the cast take
    // no input and show no controls (root.interactive). No menus, no dialogs, no confirmations: all inline.
    // ═══════════════════════════════════════════════════════════════════════════════════════════════

    // a figure's shots as one hit layer: the nearest shot within hitR wins (dense dots stay pickable). Hover
    // lights the shot in every figure and opens the hover card; a click (or a tap) focuses it.
    component ShotPicker: MouseArea {
        id: spk
        objectName: "lmPicker"
        property var h
        property string src: ""
        property var pts: []                       // [{ id, x, y }] in this item's coordinates
        enabled: h.canAct
        hoverEnabled: h.canAct
        property int hitId: -1
        cursorShape: hitId >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
        function nearest(x, y) {
            let best = null, bd = spk.h.hitR * spk.h.hitR
            for (const p of spk.pts) { const d = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y); if (d <= bd) { bd = d; best = p } }
            return best
        }
        function publish(p) { spk.h.setHover(p.id, spk.src, spk.mapToItem(spk.h.cardLayerItem, p.x, p.y)) }
        onPositionChanged: mouse => { const p = nearest(mouse.x, mouse.y); hitId = p ? p.id : -1; if (p) publish(p); else spk.h.leaveHover(spk.src) }
        onExited: { hitId = -1; spk.h.leaveHover(spk.src) }
        onClicked: mouse => { const p = nearest(mouse.x, mouse.y)
                              if (p) { spk.h.focusOn(p.id); spk.h.pinCard(p.id, spk.src, spk.mapToItem(spk.h.cardLayerItem, p.x, p.y)) }
                              else spk.h.unpinCard() }
    }

    // the every-shot strip under a Session tile: ±3 SD hairline, the ±1 SD band, a faint tick per shot, the
    // focused shot dark, the hovered shot mid-ink and taller, a left-out shot a small hollow ring
    component StripTicks: Item {
        id: stk
        property var h
        property var strip: null
        readonly property var pts: strip ? strip.pts : []
        Rectangle { y: Math.round(parent.height / 2); width: parent.width; height: stk.h.ts.hw; color: Qt.alpha(stk.h.ts.ink, 0.22) }
        Rectangle { x: parent.width / 3; width: parent.width / 3; height: parent.height; color: Qt.alpha(stk.h.ts.ink, stk.h.lr.stripBandAlpha) }
        Repeater {
            model: stk.pts
            delegate: Item {
                required property var modelData
                readonly property bool lit: modelData.id === stk.h.hoverId
                x: Math.round(modelData.f * stk.width); width: 1; height: stk.height
                Rectangle { visible: !modelData.out && !modelData.focus; x: lit ? -1 : 0; width: lit ? 2 : 1; y: lit ? -2 : 0; height: parent.height + (lit ? 4 : 0)
                            color: lit ? stk.h.ts.ink2 : Qt.alpha(stk.h.ts.ink, stk.h.lr.shotTickAlpha) }
                Rectangle { visible: modelData.focus && !modelData.out; x: -stk.h.lr.stripTickW / 2; y: -1; width: stk.h.lr.stripTickW; height: parent.height + 2; color: stk.h.ts.ink }
                Rectangle { visible: modelData.out; readonly property real d: 2 * Math.round(stk.h.ts.u(5) / 2)
                            x: -d / 2; y: (stk.height - d) / 2; width: d; height: d; radius: d / 2; color: "transparent"
                            border.width: 1; border.color: modelData.focus ? stk.h.ts.ink : stk.h.ts.ink3 }
            }
        }
        ShotPicker {
            h: stk.h; src: "strip"
            y: -(height - stk.height) / 2; width: parent.width; height: Math.max(stk.height, Theme.sp(stk.h.lr.hitPx))
            pts: stk.pts.map(p => ({ id: p.id, x: p.f * stk.width, y: height / 2 }))
        }
    }

    // an inline control: quiet text in a hairline frame, a ≥ 28 px target; `on` = the chosen one (pick wash)
    component Chip: Item {
        id: ch
        objectName: "lmChip"
        property var h
        property string text: ""
        property bool on: false
        signal tapped()
        implicitWidth: ct.implicitWidth + 2 * h.ts.u(8)
        implicitHeight: Theme.sp(h.lr.hitPx)
        width: implicitWidth; height: implicitHeight
        Rectangle {
            anchors.centerIn: parent; width: parent.width; height: Math.round(ct.implicitHeight + ch.h.ts.u(6))
            radius: Math.min(Theme.radius, height / 2); color: ch.on ? ch.h.ts.pickWash : cma.containsMouse ? ch.h.ts.rowHover : "transparent"
            border.width: ch.h.ts.hw; border.color: ch.h.tileBorder
        }
        Text { id: ct; anchors.centerIn: parent; text: ch.text; font: ch.h.ts.fMicro; color: ch.on ? ch.h.ts.ink : ch.h.ts.ink2 }
        MouseArea { id: cma; anchors.fill: parent; enabled: ch.h.canAct; hoverEnabled: ch.h.canAct; cursorShape: Qt.PointingHandCursor; onClicked: ch.tapped() }
    }

    // a shot's inline controls: leave out / restore, and its club with an inline chooser (opens in place)
    component ShotControls: Flow {
        id: sc
        property var h
        property var shot: null
        readonly property bool open: !!shot && sc.h.chooserFor === shot.shotId
        spacing: h.ts.u(6)
        Chip { h: sc.h; visible: !!sc.shot; text: sc.shot && sc.shot.out ? "Restore" : "Leave out"; onTapped: sc.h.toggleExclude(sc.shot.shotId) }
        Chip { h: sc.h; visible: !!sc.shot; on: sc.open
               text: !sc.shot ? "" : "Club: " + sc.h.clubName(sc.shot.club).toLowerCase() + (sc.shot.relabelled ? " (recorded " + sc.h.clubName(sc.shot.rawClub).toLowerCase() + ")" : "")
               onTapped: sc.h.chooserFor = sc.open ? -1 : sc.shot.shotId }
        Repeater {
            model: sc.open ? sc.h.clubChoices : []
            delegate: Chip { required property var modelData; h: sc.h; on: String(modelData).toLowerCase() === String(sc.shot.club).toLowerCase()
                             text: sc.h.clubName(modelData).toLowerCase(); onTapped: sc.h.setClub(sc.shot.shotId, modelData) }
        }
    }

    // the quiet notes under the head: the club mismatch and the golfer's own club fixes with Undo, shots left out
    // with Restore. Ink3, one line each; the actions are chips. Print shows the text only (static).
    component NotesBar: Column {
        id: nb
        property var h
        spacing: 0
        Repeater {
            model: nb.h.noteItems
            delegate: Flow {
                required property var modelData
                width: nb.width; spacing: nb.h.ts.u(8)
                Text { text: modelData.text; font: nb.h.ts.fMicro; color: nb.h.ts.ink3; height: Theme.sp(nb.h.lr.hitPx); verticalAlignment: Text.AlignVCenter
                       width: Math.min(implicitWidth, nb.width); wrapMode: Text.WordWrap }
                Repeater {
                    model: nb.h.canAct ? modelData.actions : []
                    delegate: Chip { required property var modelData; h: nb.h; text: modelData.label; onTapped: nb.h.runAction(modelData) }
                }
            }
        }
    }

    // THE HOVER CARD, in the house hover grammar (PpBoxReading): a paper backing, figures in the figure face at
    // the label size, the count line in labels; ink for the shot, ink2 for its readings. It stands beside the
    // shot it describes and outlives the pointer by lr.hoverGraceMs, so its inline controls can be used.
    component HoverCard: Item {
        id: hc
        objectName: "lmCard"
        property var h
        readonly property var shot: { for (const s of hc.h.shots) if (s.shotId === hc.h.hoverId) return s; return null }
        readonly property bool on: !!shot && hc.h.hoverFrom !== "" && hc.h.medium === "screen" && !hc.h.isCast
        readonly property real pad: h.ts.u(9)
        visible: on
        width: Math.min(Math.max(hl1.implicitWidth, hl2.implicitWidth, hl3.visible ? hl3.implicitWidth : 0, ctl.visible ? h.ts.u(220) : 0) + 2 * pad, h.ts.u(360))
        height: hcol.implicitHeight + 2 * pad
        // beside the point, right unless that leaves the panel; vertically centred on it, kept inside
        readonly property real px: h.hoverPos.x
        readonly property real py: h.hoverPos.y
        x: { const r = px + h.ts.u(14); return r + width <= parent.width ? r : Math.max(0, px - h.ts.u(14) - width) }
        y: Math.max(0, Math.min(parent.height - height, py - height / 2))
        HoverHandler { onHoveredChanged: { hc.h.cardHeld = hovered; if (!hovered) hc.h.leaveHover(hc.h.hoverSrc) } }
        Rectangle { anchors.fill: parent; radius: Theme.radius; color: Qt.alpha(hc.h.ts.paper, hc.h.lr.cardAlpha); border.width: hc.h.ts.hw; border.color: hc.h.tileBorder }
        Column {
            id: hcol
            x: hc.pad; y: hc.pad; width: hc.width - 2 * hc.pad
            spacing: hc.h.ts.u(3)
            Text { id: hl1; textFormat: Text.RichText; font: hc.h.ts.fFigLabel; color: hc.h.ts.ink
                   text: !hc.shot ? "" : "Shot " + hc.shot.ordinal + " · " + hc.h.ts.esc(hc.h.clubName(hc.shot.club).toLowerCase())
                         + (hc.shot.relabelled ? hc.h.ts.toneHtml(" (recorded " + hc.h.ts.esc(hc.h.clubName(hc.shot.rawClub).toLowerCase()) + ")", hc.h.ts.ink3) : "")
                         + (hc.shot.out ? hc.h.ts.toneHtml(" · left out", hc.h.ts.ink3) : "") }
            Text { id: hl2; font: hc.h.ts.fFigLabel; color: hc.h.ts.ink2; text: hc.shot ? hc.h.cardLine(hc.shot) : "" }
            Text { id: hl3; visible: text.length > 0; font: hc.h.ts.fMicro; color: hc.h.ts.ink3
                   text: hc.shot ? hc.h.evidenceLine(hc.shot) : "" }
            ShotControls { id: ctl; visible: hc.h.canAct; width: parent.width; h: hc.h; shot: hc.shot }
        }
    }
    // the card's key readings, in words: "158 yd carry · 9.8 R · path 1.3° L, face 0.1° L"
    // the model's club evidence for a shot, in words: "reads like an iron, 3 of 3 tests (loft 21°, smash 1.35, 4949 rpm)"
    function evidenceLine(s) {
        const e = s && s.evidence ? s.evidence : null, sug = e ? String(e.suggest || "") : ""
        if (!sug) return ""
        const like = /^an? /i.test(sug) ? sug : "a " + clubName(sug).toLowerCase()
        const ev = evidence(s)
        return "reads like " + like + (e.votes > 0 && e.of > 0 ? ", " + e.votes + " of " + e.of + " tests" : "") + (ev ? " (" + ev + ")" : "")
    }
    function clubMeanTxt(c, key) {
        const s = c && c.stats && c.stats[key] ? c.stats[key] : (c && c === focusClub ? statsFor(key) : null)
        return s && isFinite(s.mean) ? fmtKey(key, s.mean) : "-"
    }
    function swingDirOf(id) { for (const s of (shotList || [])) if (s.shotId === id) return s.swingDir || ""; return "" }
    function cardLine(s) {
        const v = k => { const x = shotVal(s, k); return isFinite(x) ? (Math.abs(x) < 1e-9 ? "0" : (x < 0 ? "−" : "") + ts.num(Math.abs(x))) : "-" }
        const p = []
        if (isFinite(s.carry)) p.push(ts.num(s.carry) + " yd carry")
        if (isFinite(s.offline)) p.push(dirStr("lm.offline", v("lm.offline")))
        const cp = shotV(s, "lm.clubPath"), fa = shotV(s, "lm.faceAngle")
        const club = [isFinite(cp) ? "path " + dirStr("lm.clubPath", v("lm.clubPath"), "°") : "", isFinite(fa) ? "face " + dirStr("lm.faceAngle", v("lm.faceAngle"), "°") : ""].filter(x => x)
        if (club.length) p.push(club.join(", "))
        return p.join(" · ")
    }
    function evidence(s) {
        const l = shotV(s, "lm.dynamicLoft"), sm = shotV(s, "lm.smashFactor"), sp = shotV(s, "lm.spinRate"), p = []
        if (isFinite(l)) p.push("loft " + ts.num(l) + "°"); if (isFinite(sm)) p.push("smash " + sm.toFixed(2)); if (isFinite(sp)) p.push(Math.round(sp) + " rpm")
        return p.join(", ")
    }

    // declared last (children are destroyed in creation order); never the id `roles`
    PpTypeset { id: tsx; medium: root.medium }
}
