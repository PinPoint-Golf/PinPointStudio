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
import PinPointStudio

// THE SESSION REPORT'S PRINTER (docs/design/session_report_design.md §8.1; CONTRACTS §5). Instantiated
// once in Main.qml. It answers sessionReport.printRequested(report, path): a white A4 sheet (794 × 1123,
// 96 dpi logical) is made in the window, PpSessionReport is laid on it with the map, the technical
// appendix is fed from the report model's private diagnostics model (sessionReport.printModel — the same
// four reads diag_print_pdf.qml's feed() takes off the panel's body), and PpPdfPrinter prints it: client
// pages at 300 dpi, appendix pages at 200 (the sheet relays the register's printPageDpi), photographs as
// JPEG and everything else as a palette ("auto"). Then printFinished, and the sheet is destroyed.
//
// THE SHEET IS NEVER SEEN, AND NEVER WAITS FOR THE APP WINDOW. It is made here and moved at once into an
// OffscreenSheet — a window of its own that is never shown, laid out and drawn on demand — and the printer
// draws each page through it. A sheet left in the app window would be laid out and grabbed only when that
// window draws, and a window out of sight draws nothing (covered, minimised or on another Space on a Mac;
// minimised elsewhere): the print stalled until the app was looked at again. Where Qt Quick has no RHI
// (the offscreen test platform) the sheet stays here instead, outside the window's left edge and under
// everything, and is grabbed with grabToImage as before. Either way the OffscreenSheet keeps App Nap off
// until the print is done. Nothing here touches Theme — the register draws in Folio light from its own
// palette, whatever the app's theme is.
//
// LAYOUT BEFORE PRINT. The pager lays a long galley out over a few frames, and printToPdf reads
// printPageCount once, at the start; so the print waits until the page count has held still for a few
// ticks (or a ceiling passes) before it begins.
Item {
    id: host

    property Item sheet: null
    property string path: ""
    property int _stableTicks: 0
    property int _lastCount: -1
    property int _ticks: 0

    function _model() { return (typeof sessionReport !== "undefined") ? sessionReport : null }

    Connections {
        target: host._model()
        function onPrintRequested(report, path) { host.start(report, path) }
    }

    OffscreenSheet { id: offscreen }

    PpPdfPrinter {
        id: printer
        dpi: 300
        settleMs: 350                    // the pager settles a page over a frame or two; images load synchronously
        encoding: "auto"
        onPageWritten: (index, count) => { const m = host._model(); if (m) m.printProgress(index + 1, count) }
        onFinished: (ok, path, pages, error) => host.done(ok, path, pages, error)
    }

    Component {
        id: sheetComp
        Rectangle {
            id: sh
            color: "#FFFFFF"
            width: 794; height: 1123
            x: -width - 400                  // off the window's left edge
            y: 0
            z: -1000000                      // and under everything, should a window ever be that wide
            property int printPage: 0
            readonly property int printPageCount: reg.printPageCount
            readonly property bool printPageHasPhoto: !!reg.printPageHasPhoto
            // 200 dpi on the appendix's pages, the printer's own 300 elsewhere (0 = the printer's dpi)
            readonly property int printPageDpi: reg.printPageDpi
            readonly property alias register: reg
            onPrintPageChanged: reg.printPage = printPage
            PpSessionReport {
                id: reg
                anchors.fill: parent
                medium: "print"
            }
        }
    }

    // The appendix's body: the four reads PpDiagPrint takes, off the live diagnostics model.
    function _appendixBody(report) {
        const m = host._model()
        const src = m ? m.printModel : null
        if (!src || !report || report.appendix !== true) return null
        const shot = m.printShotId
        return {
            source: src,
            readout: shot >= 0 ? src.shotReadout(shot) : null,
            cards: src.cards || [],
            header: src.headerInfo || null,
            detail: src.detail || null
        }
    }

    function start(report, path) {
        const m = host._model()
        if (printer.busy || host.sheet) {
            if (m) m.printFinished(false, path, 0, "A report is already being printed.")
            return
        }
        const sh = sheetComp.createObject(host)
        if (!sh) {
            if (m) m.printFinished(false, path, 0, "The report sheet could not be made: " + sheetComp.errorString())
            return
        }
        host.sheet = sh
        host.path = path
        if (offscreen.adopt(sh)) {
            sh.x = 0                     // its own window: drawn from the origin, not off the left edge
            printer.grabber = offscreen
        } else {
            printer.grabber = null
            if (typeof appLog !== "undefined")
                appLog.info("ReportPrintHost", "printing in the app window: " + offscreen.errorString)
        }
        sh.register.appendixBody = host._appendixBody(report)
        sh.register.report = report
        const meta = report.meta || ({}), brand = report.brand || ({})
        printer.title = meta.pdfTitle || "Session report"
        printer.subject = meta.date ? "Session of " + meta.date : ""
        printer.author = brand.coach || brand.studio || ""
        host._stableTicks = 0
        host._lastCount = -1
        host._ticks = 0
        host._phase = 0
        settle.start()
    }

    // Waits for the page count to hold still (four ticks running), at most ~12 s a phase, then prints.
    // TWO PHASES when there is an appendix: the client galley lays out while page 1 is showing, but the
    // appendix (PpDiagPrint) paginates only once it is visible — so the sheet is turned to the first
    // appendix page and the count is let settle again before the print reads it.
    property int _phase: 0
    Timer {
        id: settle
        interval: 250; repeat: true
        onTriggered: {
            if (!host.sheet) { stop(); return }
            const n = host.sheet.printPageCount
            host._ticks++
            if (n > 0 && n === host._lastCount) host._stableTicks++
            else host._stableTicks = 0
            host._lastCount = n
            if (host._stableTicks < 4 && host._ticks < 48) return
            const reg = host.sheet.register
            // To the app log (ppInfo via the appLog bridge), not the console.
            if (typeof appLog !== "undefined")
                appLog.info("ReportPrintHost", "phase " + host._phase + ": " + n + " pages (" + reg.clientPageCount
                            + " client), appendix " + (reg.appendixBody ? "fed" : "none"))
            if (host._phase === 0 && reg.appendixBody && reg.clientPageCount > 0) {
                host._phase = 1
                host._ticks = 0; host._stableTicks = 0; host._lastCount = -1
                host.sheet.printPage = reg.clientPageCount       // the appendix's first page
                return
            }
            stop()
            if (n < 1) { host.done(false, host.path, 0, "The report laid out no pages."); return }
            const m = host._model()
            if (m) m.printProgress(0, n)
            host.sheet.printPage = 0
            printer.printToPdf(host.sheet, host.path, false)
        }
    }

    function done(ok, path, pages, error) {
        const m = host._model()
        const sh = host.sheet
        host.sheet = null
        printer.grabber = null
        offscreen.release()              // before the sheet goes: the window gives it back first
        if (sh) sh.destroy()
        if (m) m.printFinished(ok, path, pages, error)
    }
}
