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

// PdfDocumentWriter and PpPdfPrinter — print is to PDF.
//
// The writer is driven with drawn QImages and the PDF it writes is read back as bytes: it exists,
// it is a PDF, it has one page object per image on an A4 MediaBox of the asked-for orientation,
// carries one image per page, embedded losslessly, and names PinPoint Studio as its creator. The
// failures that matter are reported rather than written: a bad path, addPage/end before begin,
// a document with no pages, something that is not an image — and an abandoned document leaves
// the destination as it was.
//
// PpPdfPrinter, the QML half, is then run for real against a three-page stand-in register in an
// offscreen window: it must step every page through grabToImage's callbacks, finish with a
// three-page PDF and put the register's printPage back.
//
// Silent and windowless: the platform is forced offscreen before the application exists.
// There is no Qt Pdf module here, so pages are counted by their /Type /Page objects.

#include "print/pdf_document_writer.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUrl>
#include <QtQml/qqml.h>

#include <cstdio>

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

static void checkInt(const QString &label, long long got, long long want)
{
    check(QStringLiteral("%1 (got %2, want %3)").arg(label).arg(got).arg(want), got == want);
}

template <typename Predicate>
static bool spinUntil(Predicate done, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        if (done())
            return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

// A page as a register would draw it: paper, a coloured band, a rule per line and a page mark.
static QImage drawnPage(int index, QSize size, bool alpha = false)
{
    QImage img(size, alpha ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
    img.fill(alpha ? Qt::transparent : Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    static const QColor bands[] = { QColor(0xc0, 0x30, 0x30), QColor(0x30, 0x90, 0x40),
                                    QColor(0x30, 0x40, 0xc0) };
    p.fillRect(QRect(0, 0, size.width(), size.height() / 8), bands[index % 3]);
    p.setPen(QPen(Qt::black, 2));
    for (int y = size.height() / 6; y < size.height() - 40; y += 36)
        p.drawLine(60, y, size.width() - 60, y);
    p.drawEllipse(QPoint(size.width() / 2, size.height() / 2), 40 + 20 * index, 40 + 20 * index);
    p.end();
    return img;
}

static QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static int countMatches(const QByteArray &pdf, const QString &pattern)
{
    const QString text = QString::fromLatin1(pdf);
    QRegularExpression re(pattern);
    int n = 0;
    for (auto it = re.globalMatch(text); it.hasNext(); it.next())
        ++n;
    return n;
}

// Each page object, not the /Pages tree node.
static int pageObjects(const QByteArray &pdf)
{
    return countMatches(pdf, QStringLiteral("/Type\\s*/Page(?![A-Za-z])"));
}

// Every MediaBox is A4 (595 × 842 pt, ±1), of the given orientation. Returns the count seen.
static int a4MediaBoxes(const QByteArray &pdf, bool landscape, bool *allA4)
{
    const QString text = QString::fromLatin1(pdf);
    QRegularExpression re(QStringLiteral(
        "/MediaBox\\s*\\[\\s*([-\\d.]+)\\s+([-\\d.]+)\\s+([-\\d.]+)\\s+([-\\d.]+)\\s*\\]"));
    int n = 0;
    *allA4 = true;
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        const double w = m.captured(3).toDouble() - m.captured(1).toDouble();
        const double h = m.captured(4).toDouble() - m.captured(2).toDouble();
        const double wantW = landscape ? 842 : 595, wantH = landscape ? 595 : 842;
        if (qAbs(w - wantW) > 1.0 || qAbs(h - wantH) > 1.0)
            *allA4 = false;
        ++n;
    }
    return n;
}

static bool mentions(const QByteArray &pdf, const QString &s)
{
    if (pdf.contains(s.toUtf8()))
        return true;
    QByteArray utf16;
    for (QChar c : s) {
        utf16.append(char(c.unicode() >> 8));
        utf16.append(char(c.unicode() & 0xff));
    }
    return pdf.contains(utf16);
}

static void testPortraitDocument(const QTemporaryDir &dir)
{
    std::printf("\nA three-page portrait document\n");
    const QString path = dir.filePath(QStringLiteral("portrait.pdf"));
    PdfDocumentWriter w;
    check(QStringLiteral("W1 begin() opens the document"),
          w.begin(path, false, 300, QStringLiteral("Session diagnostics"), QStringLiteral("Test Golfer")));
    check(QStringLiteral("W1 it is active"), w.isActive());
    check(QStringLiteral("W1 nothing is at the path until end()"), !QFileInfo::exists(path));
    // 2.5× a 794×1123 sheet, as the register is grabbed; the third page carries alpha.
    const QSize sheet(1985, 2808);
    check(QStringLiteral("W2 page 1 (QImage) is added"), w.addPage(QVariant(drawnPage(0, sheet))));
    check(QStringLiteral("W2 page 2 (QImage) is added"), w.addImage(drawnPage(1, sheet)));
    check(QStringLiteral("W2 page 3 (QImage with alpha) is added"),
          w.addPage(QVariant(drawnPage(2, sheet, true))));
    checkInt(QStringLiteral("W2 pageCount"), w.pageCount(), 3);
    check(QStringLiteral("W3 end() succeeds"), w.end());
    check(QStringLiteral("W3 errorString is empty: \"") + w.errorString() + QLatin1Char('"'),
          w.errorString().isEmpty());
    check(QStringLiteral("W3 it is no longer active"), !w.isActive());

    const QByteArray pdf = readAll(path);
    check(QStringLiteral("W4 the file exists"), QFileInfo::exists(path) && !pdf.isEmpty());
    check(QStringLiteral("W4 it starts with %PDF-"), pdf.startsWith("%PDF-"));
    check(QStringLiteral("W4 it ends with %%EOF"), pdf.trimmed().endsWith("%%EOF"));
    checkInt(QStringLiteral("W5 /Type /Page objects"), pageObjects(pdf), 3);
    bool allA4 = false;
    checkInt(QStringLiteral("W5 MediaBoxes"), a4MediaBoxes(pdf, false, &allA4), 3);
    check(QStringLiteral("W5 every page is A4 portrait (595×842 pt)"), allA4);
    check(QStringLiteral("W6 one image per page (≥ 3 /Subtype /Image)"),
          countMatches(pdf, QStringLiteral("/Subtype\\s*/Image")) >= 3);
    check(QStringLiteral("W6 no JPEG: the pages are embedded losslessly"), !pdf.contains("/DCTDecode"));
    check(QStringLiteral("W7 the creator is PinPoint Studio"), mentions(pdf, QStringLiteral("PinPoint Studio")));
    check(QStringLiteral("W7 the title is written"), mentions(pdf, QStringLiteral("Session diagnostics")));
    check(QStringLiteral("W7 the author is written"), mentions(pdf, QStringLiteral("Test Golfer")));
    check(QStringLiteral("W7 it declares PDF/A (pdfaid in the XMP)"), pdf.contains("pdfaid"));
}

static void testLandscapeAndInputs(const QTemporaryDir &dir)
{
    std::printf("\nLandscape, and the things QML hands addPage()\n");
    const QString path = dir.filePath(QStringLiteral("landscape.pdf"));
    const QString png = dir.filePath(QStringLiteral("page.png"));
    drawnPage(1, QSize(1123, 794)).save(png);

    PdfDocumentWriter w;
    check(QStringLiteral("L1 begin() takes a file: URL"),
          w.begin(QUrl::fromLocalFile(path).toString(), true, 150));
    check(QStringLiteral("L2 a landscape image"), w.addPage(QVariant(drawnPage(0, QSize(2808, 1985)))));
    check(QStringLiteral("L2 a PORTRAIT image on a landscape sheet (fitted, centred)"),
          w.addPage(QVariant(drawnPage(1, QSize(794, 1123)))));
    QObject grabResult;   // stands in for ItemGrabResult: the picture is its `image` property
    grabResult.setProperty("image", QVariant(drawnPage(2, QSize(1123, 794))));
    check(QStringLiteral("L3 an object with an image property (ItemGrabResult)"),
          w.addPage(QVariant::fromValue(static_cast<QObject *>(&grabResult))));
    check(QStringLiteral("L4 an image file's path"), w.addPage(QVariant(png)));
    check(QStringLiteral("L4 an image file's URL"), w.addPage(QVariant(QUrl::fromLocalFile(png))));
    check(QStringLiteral("L5 end()"), w.end());
    const QByteArray pdf = readAll(path);
    checkInt(QStringLiteral("L5 /Type /Page objects"), pageObjects(pdf), 5);
    bool allA4 = false;
    checkInt(QStringLiteral("L5 MediaBoxes"), a4MediaBoxes(pdf, true, &allA4), 5);
    check(QStringLiteral("L5 every page is A4 landscape (842×595 pt)"), allA4);
    check(QStringLiteral("L6 an empty title becomes the file's name"), mentions(pdf, QStringLiteral("landscape")));

    // A writer is reusable: the page count starts again.
    const QString again = dir.filePath(QStringLiteral("again.pdf"));
    check(QStringLiteral("L7 the same writer begins a second document"), w.begin(again, false));
    checkInt(QStringLiteral("L7 pageCount starts again"), w.pageCount(), 0);
    w.addImage(drawnPage(0, QSize(794, 1123)));
    check(QStringLiteral("L7 and ends it"), w.end());
    checkInt(QStringLiteral("L7 one page"), pageObjects(readAll(again)), 1);
}

static void testErrors(const QTemporaryDir &dir)
{
    std::printf("\nFailures are reported, not written\n");
    {
        PdfDocumentWriter w;
        check(QStringLiteral("E1 addPage() before begin() fails"), !w.addPage(QVariant(drawnPage(0, QSize(10, 10)))));
        check(QStringLiteral("E1 and says so: \"") + w.errorString() + QLatin1Char('"'),
              w.errorString().contains(QLatin1String("before begin")));
        check(QStringLiteral("E2 end() before begin() fails"), !w.end());
        check(QStringLiteral("E2 and says so"), w.errorString().contains(QLatin1String("before begin")));
    }
    {
        PdfDocumentWriter w;
        const QString bad = dir.filePath(QStringLiteral("no/such/folder/out.pdf"));
        check(QStringLiteral("E3 begin() on a path in a missing folder fails"), !w.begin(bad, false));
        check(QStringLiteral("E3 with a reason naming the file: \"") + w.errorString() + QLatin1Char('"'),
              w.errorString().contains(bad));
        check(QStringLiteral("E3 it is not active"), !w.isActive());
        check(QStringLiteral("E3 an empty path fails"), !w.begin(QString(), false));
        check(QStringLiteral("E3 a silly dpi fails"), !w.begin(dir.filePath(QStringLiteral("dpi.pdf")), false, 5));
    }
    {
        PdfDocumentWriter w;
        const QString path = dir.filePath(QStringLiteral("empty.pdf"));
        check(QStringLiteral("E4 begin()"), w.begin(path, false));
        check(QStringLiteral("E4 a second begin() while open fails"), !w.begin(path, true));
        check(QStringLiteral("E5 addPage() of a number fails"), !w.addPage(QVariant(42)));
        check(QStringLiteral("E5 and names what it got: \"") + w.errorString() + QLatin1Char('"'),
              w.errorString().contains(QLatin1String("image")));
        check(QStringLiteral("E5 addPage() of a null image fails"), !w.addPage(QVariant(QImage())));
        check(QStringLiteral("E5 addPage() of an unreadable file fails"),
              !w.addPage(QVariant(dir.filePath(QStringLiteral("missing.png")))));
        checkInt(QStringLiteral("E5 no page was counted"), w.pageCount(), 0);
        check(QStringLiteral("E6 end() with no pages fails"), !w.end());
        check(QStringLiteral("E6 and writes nothing"), !QFileInfo::exists(path));
        check(QStringLiteral("E6 the writer is closed"), !w.isActive());
    }
    {
        // An abandoned document leaves what was at the path alone.
        const QString path = dir.filePath(QStringLiteral("keep.pdf"));
        QFile f(path);
        check(QStringLiteral("E7 a previous file is in place"), f.open(QIODevice::WriteOnly));
        f.write("previous contents");
        f.close();
        {
            PdfDocumentWriter w;
            w.begin(path, false);
            w.addImage(drawnPage(0, QSize(794, 1123)));
            w.abort();
            check(QStringLiteral("E7 abort() closes the writer"), !w.isActive());
        }
        check(QStringLiteral("E7 abort() leaves the old file as it was"), readAll(path) == "previous contents");
        {
            PdfDocumentWriter w;
            w.begin(path, false);
            w.addImage(drawnPage(0, QSize(794, 1123)));
        }   // destroyed mid-document
        check(QStringLiteral("E8 destroying an open writer leaves the old file as it was"),
              readAll(path) == "previous contents");
    }
}

// The QML half, run for real: a three-page stand-in register in an offscreen window.
static void testQmlPrinter(const QTemporaryDir &dir)
{
    std::printf("\nPpPdfPrinter steps a register through grabToImage\n");
    qmlRegisterType<PdfDocumentWriter>("PinPointStudio", 1, 0, "PdfDocumentWriter");
    qmlRegisterType(QUrl::fromLocalFile(QStringLiteral(PP_PRINT_QML_DIR "/PpPdfPrinter.qml")),
                    "PinPointStudio", 1, 0, "PpPdfPrinter");

    QQuickView view;
    view.resize(800, 1130);
    QQmlComponent comp(view.engine());
    comp.setData(R"(
        import QtQuick
        import PinPointStudio
        Item {
            id: scene
            width: 794; height: 1123
            property bool done: false
            property bool ok: false
            property int pages: -1
            property string error: ""
            property string donePath: ""
            property string seen: ""
            property alias sheetPage: sheet.printPage
            Rectangle {
                id: sheet
                width: 794; height: 1123
                property int printPage: 0
                readonly property int printPageCount: 3
                color: ["#c03030", "#30a040", "#3040c0"][printPage] || "white"
                Rectangle { x: 60; y: 60 + 200 * sheet.printPage; width: 300; height: 120; color: "black" }
            }
            PpPdfPrinter {
                id: printer
                dpi: 150
                settleMs: 30
                onPageWritten: (index, count) => scene.seen += (scene.seen ? "," : "") + index + "/" + count
                onFinished: (ok, path, pages, error) => {
                    scene.ok = ok; scene.pages = pages; scene.error = error; scene.donePath = path
                    scene.done = true
                }
            }
            function run(path) { scene.done = false; return printer.printToPdf(sheet, path) }
            function busy() { return printer.busy }
        }
    )", QUrl::fromLocalFile(dir.filePath(QStringLiteral("scene.qml"))));
    QObject *root = comp.create();
    check(QStringLiteral("Q0 the scene loads: ") + comp.errorString().trimmed(), root != nullptr);
    if (!root)
        return;
    auto *item = qobject_cast<QQuickItem *>(root);
    item->setParentItem(view.contentItem());
    view.show();
    spinUntil([&] { return view.isExposed(); }, 5000);

    // A bad path fails at once, through the signal.
    QVariant started;
    QMetaObject::invokeMethod(root, "run", Q_RETURN_ARG(QVariant, started),
                              Q_ARG(QVariant, dir.filePath(QStringLiteral("no/such/dir/x.pdf"))));
    check(QStringLiteral("Q1 a bad path does not start"), !started.toBool());
    check(QStringLiteral("Q1 finished(false) carries the reason: \"") + root->property("error").toString() + QLatin1Char('"'),
          root->property("done").toBool() && !root->property("ok").toBool()
              && !root->property("error").toString().isEmpty());

    root->setProperty("sheetPage", 1);   // the register was showing page 2
    const QString path = dir.filePath(QStringLiteral("printed.pdf"));
    QMetaObject::invokeMethod(root, "run", Q_RETURN_ARG(QVariant, started), Q_ARG(QVariant, path));
    check(QStringLiteral("Q2 a run starts"), started.toBool());
    const bool done = spinUntil([&] { return root->property("done").toBool(); }, 20000);
    check(QStringLiteral("Q2 and finishes"), done);
    check(QStringLiteral("Q2 ok, error \"") + root->property("error").toString() + QLatin1Char('"'),
          root->property("ok").toBool());
    checkInt(QStringLiteral("Q2 finished reports pages"), root->property("pages").toInt(), 3);
    check(QStringLiteral("Q2 finished reports the path"), root->property("donePath").toString() == path);
    const QString seen = root->property("seen").toString();
    check(QStringLiteral("Q3 pageWritten for every page in order: ") + seen,
          seen == QLatin1String("0/3,1/3,2/3"));
    checkInt(QStringLiteral("Q4 the register's printPage is put back"), root->property("sheetPage").toInt(), 1);
    QVariant busy;
    QMetaObject::invokeMethod(root, "busy", Q_RETURN_ARG(QVariant, busy));
    check(QStringLiteral("Q4 the printer is idle"), !busy.toBool());

    const QByteArray pdf = readAll(path);
    check(QStringLiteral("Q5 the PDF exists and starts %PDF-"), pdf.startsWith("%PDF-"));
    checkInt(QStringLiteral("Q5 /Type /Page objects"), pageObjects(pdf), 3);
    bool allA4 = false;
    a4MediaBoxes(pdf, false, &allA4);
    check(QStringLiteral("Q5 portrait A4, from the register's own shape"), allA4);
    delete root;
}

int main(int argc, char **argv)
{
    // Silent and windowless whoever runs it: no window may appear, so offscreen is forced here as
    // well as set on the ctest entry.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    std::printf("pdf_document_writer_test — print is to PDF\n");
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("cannot make a temporary directory\n");
        return 1;
    }
    testPortraitDocument(dir);
    testLandscapeAndInputs(dir);
    testErrors(dir);
    testQmlPrinter(dir);
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
