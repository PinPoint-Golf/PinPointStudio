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
import QtTest
import PinPointStudio
import "diag"
import "data/diag_session.js" as Real

// The session diagnostics panel's body: nothing escapes the panel (test_20, data-driven).
// One of five files split from tst_session_diagnostics.qml along its section headers (#254). The harness (the
// body, the fakes, the spies and the fixtures) is diag/SessionDiagnosticsProbe.qml, whose header is the suite's
// map; the helpers and the init() every test starts from are diag/SessionDiagnosticsCase.qml.
SessionDiagnosticsProbe {
    id: probe

    SessionDiagnosticsCase {
        name: "SessionDiagnosticsChrome"

        // ═══════════════════════════ nothing escapes the panel ═══════════════════════════

        // KEPT, rewritten: no region overhangs the panel, and nothing inside a view hangs out of it
        // sideways (below the fold is allowed: that is what the view's scroll is for) — at a full, a
        // narrow and a wide stage, in both registers, on every view, for hand-written and real states.
        function inside(item, top, label) {
            const tl = item.mapToItem(top, 0, 0), br = item.mapToItem(top, item.width, item.height)
            verify(tl.x >= -1 && tl.y >= -1, label + " starts inside")
            verify(br.x <= top.width + 1, label + " ends inside sideways (" + br.x + " > " + top.width + ")")
            return br
        }
        function noOverhang(label) {
            const regions = ["sdRegister", "sdTabBar", "sdCastHead", "sdShotCardsFlick", "sdCardsFlick", "sdWatchingFlick", "sdDetailFlick"]
            for (const n of regions) {
                const items = shownAll(body, n)
                for (let j = 0; j < items.length; ++j) {
                    if (items[j].width <= 0) continue
                    const br = inside(items[j], body, label + ": " + n)
                    verify(br.y <= body.height + 1, label + ": " + n + " fits the panel's height")
                }
            }
            const content = ["sdSessionRow", "sdFiredRow", "sdDotRow", "sdBarcodeRow", "sdSparkRow", "sdSmallMultiple",
                             "sdExpectationCard", "sdShotRuler", "sdShotStrip", "sdCauseGraph", "sdPathChains",
                             "sdFigureRow", "sdFeatureHead", "sdKeyFigures", "sdQuestions"]
            for (const n of content) {
                const items = shownAll(body, n)
                for (let j = 0; j < items.length; ++j) {
                    const f = flickOf(items[j])
                    if (!f || items[j].width <= 0) continue
                    const tl = items[j].mapToItem(f.contentItem, 0, 0)
                    const br = items[j].mapToItem(f, items[j].width, 0)
                    verify(tl.x >= -1 && tl.y >= -1, label + ": " + n + "[" + j + "] starts inside its view")
                    verify(br.x <= f.width + 1, label + ": " + n + "[" + j + "] ends inside its view sideways (" + br.x + " > " + f.width + ")")
                }
            }
        }
        // Data-driven (#254): one row a state, rather than every register × state × size (126 sweeps, 230 s of
        // the studio's Debug run). Each row sweeps all three sizes and, at each, all three views; the register
        // alternates with (state + size), so every state is seen in both registers and every register at every
        // size. So every register, state, size and view is covered, and every pair of state and size, register
        // and state, and register and size. The two detail rows (Analyse only) keep all three sizes.
        readonly property var overhangSizes: [[1168, 560], [396, 560], [1522, 700]]
        function overhangState(s) {
            switch (s) {
            case 0: return { source: coldSource(probe.expectationRows), readout: null }
            case 1: return { source: formingSource(false), readout: null }
            case 2: return { source: closingSource(), readout: null }
            case 3: return { source: reviewSource(), readout: reviewReadout() }
            case 4: return Real.reviewed()
            case 5: return Real.long23()
            case 6: return Real.liveNoPick()
            }
        }
        function test_20_nothingOverhangsTheChrome_data() {
            const rows = []
            for (let s = 0; s < 7; ++s)
                rows.push({ tag: "state " + s, state: s })
            rows.push({ tag: "detail, hand-written", detail: 0 })
            rows.push({ tag: "detail, the real session", detail: 1 })
            return rows
        }
        function test_20_nothingOverhangsTheChrome(row) {
            if (row.detail === undefined) {
                useRegister("Replay")
                applyState(overhangState(row.state))
                for (let z = 0; z < overhangSizes.length; ++z) {
                    const r = (row.state + z) % 2 === 0 ? "Replay" : "Analyse"
                    const sz = overhangSizes[z]
                    useRegister(r)
                    probe.width = sz[0]; probe.height = sz[1]
                    for (const k of ["shot", "session", "watching"]) {
                        showTab(k)
                        noOverhang(r + " state " + row.state + " " + k + " at " + sz.join("x"))
                    }
                }
            } else {
                useRegister("Analyse")
                applyState(row.detail === 0 ? { source: detailSource(), readout: null } : Real.reviewed(true))
                for (const sz of overhangSizes) {
                    probe.width = sz[0]; probe.height = sz[1]
                    laidOut()
                    noOverhang("detail at " + sz.join("x"))
                }
            }
        }
    }
}
