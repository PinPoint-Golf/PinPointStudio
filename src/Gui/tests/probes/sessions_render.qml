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
import PinPointStudio

// Whole-window renders of the rail's Sessions screen (docs/design/sessions_screen_redesign.md).
// Run offscreen on a COPY of the settings pointed at a SCRATCH library (never the live one):
//   PinPointStudio --probe-qml <this file> --probe-out <dir> [--probe-phase before|after]
// before: <out>/before-sessions.png
// after:  <out>/after-sessions.png      the table, nothing selected
//         <out>/after-menu.png          row 2 hovered (Review shown) and its ⋯ menu open    (02_… PNG)
//         <out>/after-selection.png     rows 4, 6 and 7 selected, row 6 hovered, the bar     (01_… PNG)
//         <out>/after-confirm.png       row 4's ⋯ → Delete…: the confirm, NOT confirmed
//         <out>/after-bulk.png          rows 5, 6 and 7 emailed in bulk, caught mid-run (dry-run mail:
//                                       run.sh sets PINPOINT_MAIL_DRYRUN=1; the password is in memory)
// and logs each session's last send after the bulk email ("PROBE: sent …"), and the cells' fit at this
// text scale ("PROBE: fit …"). The bulk email MAKES real reports — on the scratch library only.
// The after states reach the rows by objectName: sessionsRow_<i> (i = shown order), each with
// previewHover, and the list's toggleRow(dir) / openRowMenu(i).
Item {
    id: probe
    function arg(name, dflt) {
        const a = Qt.application.arguments, i = a.indexOf(name)
        return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt
    }
    readonly property string outDir: arg("--probe-out", "/tmp")
    readonly property string phase: arg("--probe-phase", "before")
    function log(s) { console.warn("PROBE: " + s) }
    function after(ms, fn) {
        const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe)
        t.interval = ms; t.triggered.connect(() => { t.destroy(); fn() }); t.start()
    }
    function find(item, pred, out) {
        if (!item) return
        if (pred(item)) out.push(item)
        const k = item.children || []
        for (let i = 0; i < k.length; ++i) find(k[i], pred, out)
    }
    function first(root, pred) { const o = []; find(root, pred, o); return o.length ? o[0] : null }
    function named(n) { return first(Overlay.overlay.parent, o => o.objectName === n) }

    property var frame: null
    function grabWindow(path, then) {
        const root = Overlay.overlay.parent
        if (!probe.frame)
            probe.frame = Qt.createQmlObject('import QtQuick; import PinPointStudio; Rectangle { x: -100000; color: Theme.colorBg; property alias src: ses; ShaderEffectSource { id: ses; anchors.fill: parent; live: false; recursive: true; hideSource: false } }', probe)
        probe.frame.width = root.width; probe.frame.height = root.height
        probe.frame.src.sourceItem = root
        probe.frame.src.scheduleUpdate()
        after(250, () => {
            probe.frame.grabToImage(function (img) { img.saveToFile(path); probe.log("saved " + path); then() })
        })
    }
    function waitScan(then, tries) {
        if (sessionReportList.scanning && tries > 0) { after(250, () => waitScan(then, tries - 1)); return }
        after(1500, then)            // let the report states arrive (12 rows per turn)
    }
    function row(i) { return named("sessionsRow_" + i) }
    // Popups are not in the item tree: the list's confirm is found among its data.
    function confirmOf(list) {
        for (let i = 0; i < list.data.length; ++i)
            if (list.data[i] && list.data[i].objectName === "sessionsBulkConfirm") return list.data[i]
        return null
    }

    // Does each cell's content fit its column? (Date beside its chip; the device tags; Work-on.)
    function fit() {
        for (let i = 0; i < sessionReportList.shownCount; ++i) {
            const r = row(i); if (!r) continue
            const d = first(r, o => o.objectName === "sessionsDate"), chip = first(r, o => o.objectName === "sessionsReview")
            const tags = first(r, o => o.tokens !== undefined && o.labels !== undefined)
            const w = first(r, o => o.objectName === "sessionsWorkOn")
            log("fit row" + i + " cols=" + [0, 1, 2, 3, 4, 5, 6].map(k => r.columnWidth(k)).join(",")
                + " date+chip=" + Math.ceil(d.implicitWidth + Theme.gap(8) + chip.width) + "/" + r.columnWidth(0)
                + " tags=" + Math.ceil(tags.implicitWidth) + "/" + r.columnWidth(3)
                + " workOn=" + Math.ceil(w.implicitWidth) + "/" + r.columnWidth(4) + (w.truncated ? " (elided)" : ""))
        }
    }

    // Row 4's ⋯ → Delete…: the confirm, grabbed and cancelled.
    function confirmStep(list) {
        list.clearSelection()
        list.openRowMenu(4)
        after(300, () => {
            const del = named("menuDelete")
            log("menuDelete active=" + (del ? del.active : "?"))
            if (del) del.triggered()
            after(500, () => grabWindow(outDir + "/after-confirm.png", () => {
                const c = confirmOf(list)
                log("confirm title=\"" + (c ? c.title : "") + "\" body=\"" + (c ? c.body : "") + "\"")
                if (c) c.close()
                after(300, () => bulkStep(list))
            }))
        })
    }

    // Rows 5, 6 and 7 emailed in bulk: one confirm, then each made if need be and sent (dry run).
    function bulkStep(list) {
        const set = reportMailer.setPassword("probe-dry-run")
        log("sendConfigured=" + reportMailer.sendConfigured + " dryRun=" + reportMailer.dryRun + " " + JSON.stringify(set))
        const dirs = [5, 6, 7].map(i => row(i)).filter(r => r).map(r => r.dir)
        for (const d of dirs) list.toggleRow(d)
        const t0 = Date.now()
        list.askBulk("send", list.selection)
        const c = confirmOf(list)
        log("bulk confirm title=\"" + (c ? c.title : "") + "\" body=\"" + (c ? c.body : "") + "\"")
        if (!c || !c.visible) { log("bulk confirm did not open"); Qt.quit(); return }
        c.confirmed(); c.close()
        list.jobs.finished.connect((verb, okCount, failCount) => {
            log("bulk finished verb=" + verb + " ok=" + okCount + " failed=" + failCount
                + " status=" + JSON.stringify(list.jobs.status))
            for (const d of dirs) {
                const st = sessionReport.state(d)
                const last = st.lastSend || {}
                log("sent " + d.split("/").pop() + " state=" + st.state + " lastSend.method=" + (last.method || "-")
                    + " fresh=" + ((last.ms || 0) >= t0))
            }
            after(1500, () => Qt.quit())          // a mid-run grab may still be saving
        })
        after(2500, () => {
            if (!list.jobs.running) return
            grabWindow(outDir + "/after-bulk.png", () => {
                const w = named("sessionsBulkProgressWords")
                log("bulk mid-run: " + (w ? w.text : "?"))
            })
        })
        after(240000, () => { log("bulk TIMED OUT status=" + JSON.stringify(list.jobs.status)); Qt.quit() })
    }

    Component.onCompleted: {
        navController.navigate(AppScreen.Sessions)
        after(800, () => waitScan(run, 80))
    }
    function run() {
        log("rows=" + sessionReportList.count + " shown=" + sessionReportList.shownCount)
        if (phase === "before") {
            grabWindow(outDir + "/before-sessions.png", () => Qt.quit())
            return
        }
        const list = named("sessionReportList")
        grabWindow(outDir + "/after-sessions.png", () => {
            const r2 = row(2)
            if (r2) r2.previewHover = true
            if (list && list.openRowMenu) list.openRowMenu(2)
            after(400, () => grabWindow(outDir + "/after-menu.png", () => {
                if (list && list.closeRowMenu) list.closeRowMenu()
                if (r2) r2.previewHover = false
                for (const i of [4, 6, 7]) { const r = row(i); if (r && list) list.toggleRow(r.dir) }
                const r6 = row(6)
                if (r6) r6.previewHover = true
                after(400, () => grabWindow(outDir + "/after-selection.png", () => {
                    log("selection=" + JSON.stringify(list ? list.selection : []))
                    if (r6) r6.previewHover = false
                    fit()
                    confirmStep(list)
                }))
            }))
        })
    }
}
