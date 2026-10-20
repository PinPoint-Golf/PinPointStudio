import QtQuick
import PinPointStudio

// Prints SessionReportMock.qml (fed report.json) through PpPdfPrinter -> PdfDocumentWriter.
//   --probe-register <abs qml> --probe-json <abs json> --probe-out <abs pdf> [--probe-dpi 300] [--probe-theme 12]
Item {
    id: probe
    function arg(name, dflt) { const a = Qt.application.arguments, i = a.indexOf(name); return (i >= 0 && i + 1 < a.length) ? a[i + 1] : dflt }
    function log(s) { console.warn("PROBE: " + s) }
    function after(ms, fn) { const t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', probe); t.interval = ms; t.triggered.connect(() => { t.destroy(); fn() }); t.start() }
    property var sheet: null
    PpPdfPrinter {
        id: printer
        dpi: parseInt(probe.arg("--probe-dpi", "300")); settleMs: 400; title: "Session report"; author: "PinPoint Studio"
        onPageWritten: (i, n) => probe.log("page " + (i + 1) + "/" + n)
        onFinished: (ok, path, pages, error) => { probe.log(ok ? "wrote " + path + " (" + pages + " pages)" : "FAILED " + error); probe.after(200, () => Qt.exit(ok ? 0 : 3)) }
    }
    Timer {
        interval: 2500; running: true
        onTriggered: {
            Theme.themeIndex = parseInt(probe.arg("--probe-theme", "12"))
            const xhr = new XMLHttpRequest()
            xhr.open("GET", "file://" + probe.arg("--probe-json", ""), false); xhr.send()
            const report = JSON.parse(xhr.responseText)
            const comp = Qt.createComponent("file://" + probe.arg("--probe-register", ""))
            if (comp.status !== Component.Ready) { probe.log("register error: " + comp.errorString()); Qt.exit(2); return }
            probe.sheet = Qt.createQmlObject('import QtQuick; Rectangle { z: 1000000; color: "#FFFFFF"; width: 794; height: 1123; property var reg: null; property int printPage: 0; readonly property int printPageCount: reg ? reg.printPageCount : 0; onPrintPageChanged: if (reg) reg.printPage = printPage }', probe.Window.window.contentItem)
            const reg = comp.createObject(probe.sheet, { report: report })
            if (!reg) { probe.log("create failed: " + comp.errorString()); Qt.exit(2); return }
            probe.sheet.reg = reg
            probe.log("theme " + Theme.aesthetic + " pages " + reg.printPageCount)
            printer.title = (report.meta || {}).pdfTitle || "Session report"
            probe.after(1500, () => printer.printToPdf(probe.sheet, probe.arg("--probe-out", "/tmp/x.pdf"), false))
        }
    }
}
