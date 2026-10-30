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

// PpPdfPrinter — prints a paged register to one PDF.
//
// `printToPdf(target, path[, landscape])` steps target.printPage from 0 to target.printPageCount - 1,
// grabs each page at `dpi` and hands it to a PdfDocumentWriter; `finished` reports the outcome.
// The target is any Item laid out as an A4 sheet at 96 dpi logical (794×1123, or 1123×794 for
// landscape) that exposes `printPage` (writable) and `printPageCount` — the contract of the
// Session diagnostics print register. It must be in a window, because grabToImage renders
// through the window's scene graph; it need not be on screen.
//
// grabToImage is asynchronous, so the run is a chain: set the page, let it settle for
// `settleMs` (the pager lays a page out over a frame or two), grab, add, next. The target's
// printPage is put back as it was at the end, whatever the outcome. No dialogs: the caller
// chooses the path and reads the result from `finished`.
//
// `encoding` is the writer's: "lossless" (the default, every page Flate RGB) or "auto" (a palette
// page, photographs as JPEG — a third of the size; see pdf_document_writer.h). For "auto" a target
// that draws photographs says so per page with `readonly property bool printPageHasPhoto`, read
// once the page has settled; a target without it is taken to have none.
//
// A target may also set its own resolution page by page with `readonly property int printPageDpi`
// (0 or absent = `dpi`): the session report grabs its client pages at 300 dpi and its technical
// appendix at 200 (half the bytes; the appendix is read on screen). The sheet is A4 either way —
// the writer fits every page image to the whole sheet whatever its pixel size.
//
// `grabber` (optional) draws the target without the app window: an object whose grab(pixelSize)
// returns the page as an image, synchronously — the session report's OffscreenSheet, which keeps a
// print going while the app is covered or minimised (grabToImage waits for the window's next frame,
// and a window out of sight draws none). Null: grabToImage, as before.
//
// Not wired to any control yet — print is the last thing on the plan.
QtObject {
    id: root

    // 300 dpi: a 794-px-wide sheet is grabbed 2481 px wide, A4's width at 300 dpi.
    property int dpi: 300
    property int settleMs: 150
    property string title: ""
    property string author: ""
    property string subject: ""
    property string encoding: "lossless"
    property var grabber: null
    readonly property bool busy: _target !== null

    signal pageWritten(int index, int count)
    signal finished(bool ok, string path, int pageCount, string error)

    // Starts a run; false (with `finished` emitted) if it cannot start. `landscape` defaults to
    // the target's own shape.
    function printToPdf(target, path, landscape) {
        if (busy) {
            finished(false, path, 0, "A PDF is already being printed to " + _path + ".")
            return false
        }
        if (!target || target.printPageCount === undefined || target.printPage === undefined) {
            finished(false, path, 0, "The item to print has no printPage / printPageCount.")
            return false
        }
        if (!(target.width > 0 && target.height > 0)) {
            finished(false, path, 0, "The item to print has no size.")
            return false
        }
        const count = Math.max(0, target.printPageCount | 0)
        if (count < 1) {
            finished(false, path, 0, "The item to print has no pages.")
            return false
        }
        const wide = (landscape === undefined) ? target.width > target.height : !!landscape
        _writer.encoding = encoding
        _writer.subject = subject
        if (!_writer.begin(path, wide, dpi, title, author)) {
            finished(false, path, 0, _writer.errorString)
            return false
        }
        _target = target
        _path = path
        _count = count
        _page = 0
        _restorePage = target.printPage
        _step()
        return true
    }

    // Abandons a run in progress; the destination file is left as it was. `finished` reports it.
    function cancel() {
        if (busy)
            _fail("Printing was cancelled.")
    }

    // ── the run ───────────────────────────────────────────────────────────────────────────
    property Item _target: null
    property string _path: ""
    property int _count: 0
    property int _page: 0
    property int _restorePage: 0

    readonly property PdfDocumentWriter _writer: PdfDocumentWriter {}
    readonly property Timer _settle: Timer {
        repeat: false
        interval: Math.max(0, root.settleMs)
        onTriggered: root._grab()
    }

    function _step() {
        if (_page >= _count) {
            const ok = _writer.end()
            _finish(ok, ok ? _writer.pageCount : 0, ok ? "" : _writer.errorString)
            return
        }
        _target.printPage = _page
        _settle.restart()
    }

    function _grab() {
        if (!_target)
            return
        const target = _target
        const page = _page
        // Read now, while this page is the one laid out.
        const pageDpi = (target.printPageDpi !== undefined && target.printPageDpi > 0) ? target.printPageDpi : dpi
        const scale = pageDpi / 96
        const photo = target.printPageHasPhoto !== undefined && !!target.printPageHasPhoto
        const requested = Qt.size(Math.round(target.width * scale), Math.round(target.height * scale))
        if (grabber) {
            const image = grabber.grab(requested)
            if (!_writer.addPage(image, photo, requested)) {
                _fail("Page " + (page + 1) + " could not be drawn: "
                      + (grabber.errorString || _writer.errorString))
                return
            }
            pageWritten(page, _count)
            _page = page + 1
            _step()
            return
        }
        const started = target.grabToImage(function (result) {
            // A cancel (or a new run) between the request and the picture: drop it.
            if (root._target !== target || root._page !== page)
                return
            if (!root._writer.addPage(result.image, photo, requested)) {
                root._fail(root._writer.errorString)
                return
            }
            root.pageWritten(page, root._count)
            root._page = page + 1
            root._step()
        }, requested)
        if (!started)
            _fail("Page " + (page + 1) + " could not be grabbed: the item must be visible in a window.")
    }

    function _fail(message) {
        _writer.abort()
        _finish(false, 0, message)
    }

    function _finish(ok, pages, error) {
        const path = _path
        if (_target)
            _target.printPage = _restorePage
        _target = null
        _path = ""
        _count = 0
        _page = 0
        finished(ok, path, pages, error)
    }
}
