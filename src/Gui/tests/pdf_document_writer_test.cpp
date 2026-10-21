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
// The "auto" encoding (the session report's) is the writer's own PDF/A-1b serializer, so it is
// read back harder: the page images are pulled out of the file and decoded here — a flat-colour
// page must come back as an Indexed Flate image whose pixels match what was drawn (exactly when
// it has 256 colours or fewer, within a few levels when anti-aliased text makes more), a page
// passed as a photograph must carry its photograph as DCT, the subject must be in the Info
// dictionary and the XMP, every xref offset must land on its object, and a text-heavy A4 page at
// 300 dpi must stay under a size bound set from the real report pages (build/report-build/encoding/).
//
// PpPdfPrinter, the QML half, is then run for real against a three-page stand-in register in an
// offscreen window: it must step every page through grabToImage's callbacks, finish with a
// three-page PDF and put the register's printPage back — and, set to "auto", pass a register's
// printPageHasPhoto through to the writer page by page.
//
// Silent and windowless: the platform is forced offscreen before the application exists.
// There is no Qt Pdf module here, so pages are counted by their /Type /Page objects.

#include "print/pdf_document_writer.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QRandomGenerator>
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

// ── reading an "auto" file back ──────────────────────────────────────────────────────────────

// The body of object `n` (between "n 0 obj" and "endobj"), and its stream if it has one.
struct PdfObject {
    QByteArray dict;
    QByteArray stream;
    bool found = false;
};

static PdfObject pdfObject(const QByteArray &pdf, int n)
{
    PdfObject o;
    const QByteArray head = QByteArray::number(n) + " 0 obj\n";
    qsizetype at = pdf.startsWith(head) ? 0 : pdf.indexOf("\n" + head);
    if (at < 0)
        return o;
    at = pdf.indexOf(head, at) + head.size();
    const qsizetype end = pdf.indexOf("endobj", at);
    const qsizetype st = pdf.indexOf("stream\n", at);
    o.found = true;
    if (st < 0 || st > end) {
        o.dict = pdf.mid(at, end - at);
        return o;
    }
    o.dict = pdf.mid(at, st - at);
    QRegularExpression len(QStringLiteral("/Length (\\d+)"));
    const auto m = len.match(QString::fromLatin1(o.dict));
    if (m.hasMatch())
        o.stream = pdf.mid(st + 7, m.captured(1).toLongLong());
    return o;
}

static QByteArray inflate(const QByteArray &zlib, qsizetype expected)
{
    QByteArray withLength(4, '\0');
    withLength[0] = char((expected >> 24) & 0xff);
    withLength[1] = char((expected >> 16) & 0xff);
    withLength[2] = char((expected >> 8) & 0xff);
    withLength[3] = char(expected & 0xff);
    return qUncompress(withLength + zlib);
}

// The image XObjects of an "auto" file, in file order, decoded: Indexed Flate to RGB32, DCT
// through QImage. `kind` is "indexed", "dct" or "rgb".
struct PdfImageBack {
    QString kind;
    QImage image;
    int bits = 0;
    int colours = 0;
    qsizetype bytes = 0;
};

static QVector<PdfImageBack> pdfImages(const QByteArray &pdf)
{
    QVector<PdfImageBack> out;
    QRegularExpression obj(QStringLiteral("(?:^|\\n)(\\d+) 0 obj\\n<< /Type /XObject /Subtype /Image"));
    for (auto it = obj.globalMatch(QString::fromLatin1(pdf)); it.hasNext();) {
        const PdfObject o = pdfObject(pdf, it.next().captured(1).toInt());
        const QString d = QString::fromLatin1(o.dict);
        const int w = QRegularExpression(QStringLiteral("/Width (\\d+)")).match(d).captured(1).toInt();
        const int h = QRegularExpression(QStringLiteral("/Height (\\d+)")).match(d).captured(1).toInt();
        const int bits = QRegularExpression(QStringLiteral("/BitsPerComponent (\\d+)")).match(d).captured(1).toInt();
        PdfImageBack b;
        b.bits = bits;
        b.bytes = o.stream.size();
        if (d.contains(QLatin1String("/DCTDecode"))) {
            b.kind = QStringLiteral("dct");
            b.image = QImage::fromData(o.stream, "JPEG");
        } else if (d.contains(QLatin1String("/Indexed"))) {
            b.kind = QStringLiteral("indexed");
            const auto m = QRegularExpression(QStringLiteral("/Indexed /DeviceRGB (\\d+) <([0-9A-F]*)>")).match(d);
            const QByteArray lookup = QByteArray::fromHex(m.captured(2).toLatin1());
            b.colours = m.captured(1).toInt() + 1;
            const qsizetype rowBytes = (qsizetype(w) * bits + 7) / 8;
            const QByteArray raw = inflate(o.stream, rowBytes * h);
            if (raw.size() == rowBytes * h && lookup.size() == b.colours * 3) {
                b.image = QImage(w, h, QImage::Format_RGB32);
                for (int y = 0; y < h; ++y) {
                    const auto *row = reinterpret_cast<const uchar *>(raw.constData()) + rowBytes * y;
                    auto *line = reinterpret_cast<QRgb *>(b.image.scanLine(y));
                    for (int x = 0; x < w; ++x) {
                        const int bit = x * bits;
                        const int idx = (row[bit >> 3] >> (8 - bits - (bit & 7))) & ((1 << bits) - 1);
                        const auto *c = reinterpret_cast<const uchar *>(lookup.constData()) + 3 * qMin(idx, b.colours - 1);
                        line[x] = qRgb(c[0], c[1], c[2]);
                    }
                }
            }
        } else {
            b.kind = QStringLiteral("rgb");
        }
        out.append(b);
    }
    return out;
}

// Largest channel difference between two same-sized images, and how many pixels differ by more
// than `over`.
static int maxChannelError(const QImage &a, const QImage &b, int over = 255, qint64 *countOver = nullptr)
{
    if (a.size() != b.size())
        return 999;
    const QImage x = a.convertToFormat(QImage::Format_RGB32), y = b.convertToFormat(QImage::Format_RGB32);
    int worst = 0;
    qint64 n = 0;
    for (int r = 0; r < x.height(); ++r) {
        const auto *p = reinterpret_cast<const QRgb *>(x.constScanLine(r));
        const auto *q = reinterpret_cast<const QRgb *>(y.constScanLine(r));
        for (int c = 0; c < x.width(); ++c) {
            const int e = qMax(qAbs(qRed(p[c]) - qRed(q[c])),
                               qMax(qAbs(qGreen(p[c]) - qGreen(q[c])), qAbs(qBlue(p[c]) - qBlue(q[c]))));
            worst = qMax(worst, e);
            n += e > over;
        }
    }
    if (countOver)
        *countOver = n;
    return worst;
}

// A report page at 300 dpi as the register draws one: paper, a running head, two columns of
// anti-aliased body text in ink and grey, an accent heading per section, rules and coloured dots.
static QImage textPage300()
{
    QImage img(2481, 3509, QImage::Format_RGB32);
    img.fill(QColor(0xff, 0xff, 0xff));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const QColor ink(0x22, 0x22, 0x22), grey(0x77, 0x77, 0x77), accent(0x1f, 0x6f, 0x5c), rule(0xd8, 0xd8, 0xd8);
    QFont body(QStringLiteral("Helvetica"));
    body.setPixelSize(38);
    QFont head = body;
    head.setPixelSize(64);
    p.setFont(head);
    p.setPen(ink);
    p.drawText(QPointF(200, 300), QStringLiteral("Keep the flex in your trail knee"));
    p.fillRect(QRectF(200, 340, 2081, 3), rule);
    static const char *words[] = {"Your", "trail", "leg", "straightens", "going", "back", "on", "all", "ten",
                                  "swings,", "steady", "from", "first", "swing", "to", "last.", "Typical", "bend",
                                  "lost", "by", "the", "top:", "22°,", "against", "a", "target", "of", "less", "than", "12°."};
    int wi = 0;
    for (int col = 0; col < 2; ++col) {
        double y = 460;
        for (int line = 0; line < 62; ++line, y += 48) {
            if (line % 14 == 0) {
                p.setFont(head);
                p.setPen(accent);
                p.drawText(QPointF(200 + col * 1080, y + 20), QStringLiteral("What we saw"));
                y += 70;
                p.setFont(body);
                p.setPen(ink);
                for (int d = 0; d < 10; ++d) {
                    p.setBrush(d % 3 ? QColor(0xc8, 0x6b, 0x2a) : accent);
                    p.setPen(Qt::NoPen);
                    p.drawEllipse(QPointF(230 + col * 1080 + d * 90, y), 14, 14);
                }
                p.setPen(ink);
                y += 50;
            }
            p.setPen(line % 5 == 4 ? grey : ink);
            QString text;
            while (text.size() < 44)
                text += QString::fromUtf8(words[wi++ % 30]) + QLatin1Char(' ');
            p.drawText(QPointF(200 + col * 1080, y), text.trimmed());
        }
    }
    p.end();
    return img;
}

// A page with one photograph on it: noise and gradients in a box, text round it.
static QImage photoPage(QRect *where)
{
    QImage img(1240, 1754, QImage::Format_RGB32);   // A4 at 150 dpi
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f(QStringLiteral("Helvetica"));
    f.setPixelSize(22);
    p.setFont(f);
    p.setPen(QColor(0x22, 0x22, 0x22));
    for (int i = 0; i < 20; ++i)
        p.drawText(QPointF(100, 120 + i * 30), QStringLiteral("Address, swing 5 — the typical swing of the session."));
    p.end();
    *where = QRect(620, 760, 420, 560);
    QRandomGenerator rng(42);
    for (int y = where->top(); y <= where->bottom(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = where->left(); x <= where->right(); ++x) {
            const int n = int(rng.bounded(40)) - 20;
            line[x] = qRgb(qBound(0, 30 + (x - where->left()) / 3 + n, 255),
                           qBound(0, 40 + (y - where->top()) / 4 + n, 255),
                           qBound(0, 60 + ((x + y) % 97) + n, 255));
        }
    }
    return img;
}

// Every xref entry points at "<n> 0 obj", and startxref at "xref".
static bool xrefHolds(const QByteArray &pdf)
{
    const qsizetype sx = pdf.lastIndexOf("startxref\n");
    if (sx < 0)
        return false;
    const qsizetype xref = pdf.mid(sx + 10).split('\n').value(0).toLongLong();
    if (pdf.mid(xref, 5) != "xref\n")
        return false;
    const QList<QByteArray> lines = pdf.mid(xref).split('\n');
    const int count = lines.value(1).split(' ').value(1).toInt();
    if (count < 7)
        return false;
    for (int i = 1; i < count; ++i) {
        const QByteArray e = lines.value(2 + i);
        if (e.size() != 19 || !e.endsWith(" 00000 n "))
            return false;
        if (!pdf.mid(e.left(10).toLongLong()).startsWith(QByteArray::number(i) + " 0 obj\n"))
            return false;
    }
    return true;
}

static void testAutoEncoding(const QTemporaryDir &dir)
{
    std::printf("\nencoding \"auto\": palette pages, photographs as JPEG, PDF/A-1b by hand\n");
    {
        PdfDocumentWriter w;
        check(QStringLiteral("A0 \"lossless\" is still the default"), w.encoding() == QLatin1String("lossless"));
        w.setEncoding(QStringLiteral("webp"));
        check(QStringLiteral("A0 an unknown encoding is refused at begin(): \"") + w.errorString() + QLatin1Char('"'),
              !w.begin(dir.filePath(QStringLiteral("bad-encoding.pdf")), false)
                  && w.errorString().contains(QLatin1String("webp")));
    }

    const QString path = dir.filePath(QStringLiteral("auto.pdf"));
    PdfDocumentWriter w;
    w.setEncoding(QStringLiteral("auto"));
    w.setSubject(QStringLiteral("Session of Tuesday 20 October 2026"));
    check(QStringLiteral("A1 begin()"), w.begin(path, false, 300, QStringLiteral("Session report"), QStringLiteral("Alex Morgan")));

    // Page 1: flat colour, no anti-aliasing — fewer than 256 colours, so stored exactly.
    QImage flat(1240, 1754, QImage::Format_RGB32);
    flat.fill(Qt::white);
    {
        QPainter p(&flat);
        static const QColor fills[] = {QColor(0x1f, 0x6f, 0x5c), QColor(0xc8, 0x6b, 0x2a), QColor(0x22, 0x22, 0x22),
                                       QColor(0xd8, 0xd8, 0xd8), QColor(0xf4, 0xf1, 0xea)};
        for (int i = 0; i < 40; ++i)
            p.fillRect(QRect(60 + (i % 8) * 140, 80 + (i / 8) * 300, 120, 40 + i * 5), fills[i % 5]);
    }
    check(QStringLiteral("A2 a flat page is added"), w.addPage(QVariant(flat)));
    const QVariantMap flatInfo = w.lastPageInfo();
    check(QStringLiteral("A2 it is a palette page, every colour kept: ")
              + QString::fromUtf8(QJsonDocument::fromVariant(flatInfo).toJson(QJsonDocument::Compact)),
          flatInfo.value(QStringLiteral("encoding")).toString() == QLatin1String("palette")
              && flatInfo.value(QStringLiteral("exact")).toBool());
    checkInt(QStringLiteral("A2 six colours pack at 4 bits a pixel"), flatInfo.value(QStringLiteral("bitsPerPixel")).toInt(), 4);

    // Page 2: anti-aliased text at 300 dpi — thousands of colours, quantised.
    const QImage text = textPage300();
    check(QStringLiteral("A3 a text-heavy 300 dpi page is added"), w.addPage(QVariant(text)));
    const QVariantMap textInfo = w.lastPageInfo();
    std::printf("      text page: %s\n", QJsonDocument::fromVariant(textInfo).toJson(QJsonDocument::Compact).constData());

    // Page 3: a photograph among text, flagged.
    QRect where;
    const QImage photo = photoPage(&where);
    check(QStringLiteral("A4 a photo page is added with photo = true"), w.addPage(QVariant(photo), true));
    const QVariantMap photoInfo = w.lastPageInfo();
    check(QStringLiteral("A4 it is a palette page with one JPEG photograph: ")
              + QString::fromUtf8(QJsonDocument::fromVariant(photoInfo).toJson(QJsonDocument::Compact)),
          photoInfo.value(QStringLiteral("encoding")).toString() == QLatin1String("palette+jpeg")
              && photoInfo.value(QStringLiteral("photos")).toInt() == 1);
    check(QStringLiteral("A5 end()"), w.end());

    const QByteArray pdf = readAll(path);
    const QVector<PdfImageBack> images = pdfImages(pdf);
    checkInt(QStringLiteral("A6 image XObjects (flat, text, photo page + its photograph)"), images.size(), 4);
    if (images.size() == 4) {
        check(QStringLiteral("A6 the flat page is Indexed Flate"), images[0].kind == QLatin1String("indexed"));
        checkInt(QStringLiteral("A6 and comes back pixel for pixel (max channel error)"), maxChannelError(images[0].image, flat), 0);
        check(QStringLiteral("A7 the text page is Indexed Flate, 8 bits"),
              images[1].kind == QLatin1String("indexed") && images[1].bits == 8);
        qint64 off = 0;
        const int textErr = maxChannelError(images[1].image, text, 8, &off);
        check(QStringLiteral("A7 its pixels match within a tight tolerance (max %1, %2 px off by more than 8)").arg(textErr).arg(off),
              textErr <= 24 && off * 10000 < qint64(text.width()) * text.height());
        // The size bound. The real report's pages are 145–250 KB at 300 dpi in "auto" (260–480 KB
        // as Qt's RGB Flate); this synthetic page is denser than any of them — two full columns —
        // and measured 366 KB on the Mac (640 KB lossless). 512 KB leaves room for another
        // platform's font without letting a palette that stopped working (≥ 600 KB) through.
        std::printf("      text page image: %lld bytes\n", (long long)images[1].bytes);
        check(QStringLiteral("A8 a text-heavy A4 page at 300 dpi is under 512 KB (%1 KB)").arg(images[1].bytes / 1024),
              images[1].bytes < 512 * 1024);
        check(QStringLiteral("A9 the photo page's paper is Indexed"), images[2].kind == QLatin1String("indexed"));
        check(QStringLiteral("A9 its photograph is DCT"), images[3].kind == QLatin1String("dct") && !images[3].image.isNull());
        check(QStringLiteral("A9 the JPEG tile covers the photograph (%1×%2 for %3×%4)")
                  .arg(images[3].image.width()).arg(images[3].image.height()).arg(where.width()).arg(where.height()),
              qAbs(images[3].image.width() - where.width()) <= 40 && qAbs(images[3].image.height() - where.height()) <= 40);
        // The page round the photograph is still exact-ish: text, not JPEG.
        QImage outside = photo.copy(QRect(0, 0, photo.width(), where.top() - 40));
        check(QStringLiteral("A9 the text above it is palette, within 24 levels"),
              maxChannelError(images[2].image.copy(outside.rect()), outside) <= 24);
    }
    check(QStringLiteral("A10 the subject is in the Info dictionary"), pdf.contains("/Subject <FEFF"));
    QByteArray utf16;
    for (QChar c : QStringLiteral("Session of Tuesday 20 October 2026")) {
        utf16.append(char(c.unicode() >> 8));
        utf16.append(char(c.unicode() & 0xff));
    }
    check(QStringLiteral("A10 …as the subject's own text"), pdf.contains("/Subject <FEFF" + utf16.toHex().toUpper() + '>'));
    check(QStringLiteral("A10 and in the XMP (dc:description)"),
          pdf.contains("<dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">Session of Tuesday 20 October 2026</rdf:li>"));
    check(QStringLiteral("A11 PDF/A-1b: version 1.4 with a binary comment"), pdf.startsWith("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n"));
    check(QStringLiteral("A11 PDF/A-1b: pdfaid part 1, conformance B"),
          pdf.contains("<pdfaid:part>1</pdfaid:part>") && pdf.contains("<pdfaid:conformance>B</pdfaid:conformance>"));
    check(QStringLiteral("A11 PDF/A-1b: an sRGB output intent with its ICC profile"),
          pdf.contains("/S /GTS_PDFA1 /DestOutputProfile 3 0 R") && pdfObject(pdf, 3).stream.size() > 1000);
    check(QStringLiteral("A11 PDF/A-1b: the XMP stream is not filtered"), !pdfObject(pdf, 2).dict.contains("/Filter"));
    check(QStringLiteral("A11 PDF/A-1b: no transparency, no interpolation"),
          !pdf.contains("/SMask") && !pdf.contains("/Interpolate") && !pdf.contains("/ca ") && !pdf.contains("/CA "));
    check(QStringLiteral("A11 PDF/A-1b: a trailer /ID"), QRegularExpression(QStringLiteral("/ID \\[<[0-9A-F]{32}> <[0-9A-F]{32}>\\]")).match(QString::fromLatin1(pdf)).hasMatch());
    check(QStringLiteral("A11 every xref offset lands on its object"), xrefHolds(pdf));
    checkInt(QStringLiteral("A12 /Type /Page objects"), pageObjects(pdf), 3);
    bool allA4 = false;
    checkInt(QStringLiteral("A12 MediaBoxes"), a4MediaBoxes(pdf, false, &allA4), 3);
    check(QStringLiteral("A12 every page is A4 portrait"), allA4);
    check(QStringLiteral("A12 title, author and creator are written"),
          mentions(pdf, QStringLiteral("Session report")) && mentions(pdf, QStringLiteral("Alex Morgan"))
              && mentions(pdf, QStringLiteral("PinPoint Studio")));

    {
        // The same text page through "lossless": the bound above is the gain.
        PdfDocumentWriter l;
        const QString lpath = dir.filePath(QStringLiteral("lossless-text.pdf"));
        l.begin(lpath, false, 300);
        l.addImage(text);
        l.end();
        const qint64 lossless = QFileInfo(lpath).size(), mine = images.size() > 1 ? images[1].bytes : 0;
        std::printf("      the same page lossless: %lld bytes (auto %lld)\n", (long long)lossless, (long long)mine);
        check(QStringLiteral("A13 \"lossless\" still writes no subject and no palette"),
              !readAll(lpath).contains("/Subject") && !readAll(lpath).contains("/Indexed"));
        check(QStringLiteral("A13 \"auto\" is smaller than \"lossless\" for the same page"), mine > 0 && mine < lossless);
    }
}

// The /Width × /Height of every image XObject, in file order.
static QVector<QSize> imageSizes(const QByteArray &pdf)
{
    QVector<QSize> out;
    QRegularExpression re(QStringLiteral("/Subtype\\s*/Image[^>]*?/Width\\s+(\\d+)\\s*/Height\\s+(\\d+)"));
    for (auto it = re.globalMatch(QString::fromLatin1(pdf)); it.hasNext();) {
        const auto m = it.next();
        out.append(QSize(m.captured(1).toInt(), m.captured(2).toInt()));
    }
    return out;
}

// A 300 dpi page and a 200 dpi page in one document: both sheets A4, each image kept at the
// size it was given — in both encodings.
static void testMixedResolution(const QTemporaryDir &dir)
{
    std::printf("\nMixed resolution: a 300 dpi page and a 200 dpi page, both A4\n");
    for (const QString &enc : {QStringLiteral("lossless"), QStringLiteral("auto")}) {
        const QString path = dir.filePath(QStringLiteral("mixed-%1.pdf").arg(enc));
        PdfDocumentWriter w;
        w.setEncoding(enc);
        w.begin(path, false, 300);
        const QSize at300(2481, 3509), at200(1654, 2339);
        check(QStringLiteral("M1 %1: a 300 dpi page").arg(enc), w.addImage(drawnPage(0, at300)));
        check(QStringLiteral("M1 %1: a 200 dpi page").arg(enc), w.addImage(drawnPage(1, at200)));
        check(QStringLiteral("M1 %1: end()").arg(enc), w.end());
        const QByteArray pdf = readAll(path);
        bool allA4 = false;
        checkInt(QStringLiteral("M2 %1: MediaBoxes").arg(enc), a4MediaBoxes(pdf, false, &allA4), 2);
        check(QStringLiteral("M2 %1: both pages are A4 portrait").arg(enc), allA4);
        const QVector<QSize> sizes = imageSizes(pdf);
        check(QStringLiteral("M3 %1: the images keep their sizes (%2)").arg(enc,
                  sizes.size() == 2 ? QStringLiteral("%1×%2, %3×%4").arg(sizes[0].width()).arg(sizes[0].height())
                                          .arg(sizes[1].width()).arg(sizes[1].height())
                                    : QStringLiteral("%1 images").arg(sizes.size())),
              sizes == QVector<QSize>({at300, at200}));
        if (enc == QLatin1String("auto")) {
            // Each fills the sheet: the content stream scales both to A4's 595.28 × 841.89 pt
            // (within a point: 3509 px rows are a hair taller than A4 at 300 dpi, so fitted).
            QRegularExpression cm(QStringLiteral("q ([\\d.]+) 0 0 ([\\d.]+) [\\d.]+ [\\d.]+ cm"));
            int full = 0;
            for (auto it = cm.globalMatch(QString::fromLatin1(pdf)); it.hasNext();) {
                const auto m = it.next();
                full += qAbs(m.captured(1).toDouble() - 595.28) < 1.0 && qAbs(m.captured(2).toDouble() - 841.89) < 1.0;
            }
            checkInt(QStringLiteral("M4 auto: both images are drawn over the whole sheet"), full, 2);
        }
    }
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
            property url photoUrl
            property alias sheetPage: sheet.printPage
            property alias encoding: printer.encoding
            Rectangle {
                id: sheet
                width: 794; height: 1123
                property int printPage: 0
                readonly property int printPageCount: 3
                color: ["#c03030", "#30a040", "#3040c0"][printPage] || "white"
                // Page 2 carries a photograph, and says so.
                readonly property bool printPageHasPhoto: printPage === 1
                // Page 3 asks for its own resolution: 100 dpi, against the printer's 150.
                readonly property int printPageDpi: printPage === 2 ? 100 : 0
                Rectangle { x: 60; y: 60 + 200 * sheet.printPage; width: 300; height: 120; color: "black" }
                Image { x: 300; y: 500; width: 420; height: 560; source: scene.photoUrl; visible: sheet.printPage === 1 }
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

    // "auto": the register's printPageHasPhoto reaches the writer page by page.
    QRect where;
    const QString photoPng = dir.filePath(QStringLiteral("photo.png"));
    photoPage(&where).copy(where).save(photoPng);
    root->setProperty("photoUrl", QUrl::fromLocalFile(photoPng));
    root->setProperty("encoding", QStringLiteral("auto"));
    root->setProperty("seen", QString());
    const QString autoPath = dir.filePath(QStringLiteral("printed-auto.pdf"));
    QMetaObject::invokeMethod(root, "run", Q_RETURN_ARG(QVariant, started), Q_ARG(QVariant, autoPath));
    check(QStringLiteral("Q6 an \"auto\" run finishes ok"),
          started.toBool() && spinUntil([&] { return root->property("done").toBool(); }, 20000)
              && root->property("ok").toBool());
    const QByteArray autoPdf = readAll(autoPath);
    const QVector<PdfImageBack> autoImages = pdfImages(autoPdf);
    checkInt(QStringLiteral("Q6 three pages"), pageObjects(autoPdf), 3);
    int dct = 0, indexed = 0;
    for (const PdfImageBack &b : autoImages) {
        dct += b.kind == QLatin1String("dct");
        indexed += b.kind == QLatin1String("indexed");
    }
    checkInt(QStringLiteral("Q6 every page is a palette page"), indexed, 3);
    checkInt(QStringLiteral("Q6 and only page 2, which says it has a photograph, carries a JPEG"), dct, 1);
    if (autoImages.size() == 4) {
        // Order: page 1, page 2 + its photograph, page 3.
        check(QStringLiteral("Q7 printPageDpi: page 1 at the printer's 150 dpi (%1 px wide)").arg(autoImages[0].image.width()),
              autoImages[0].image.width() == qRound(794 * 150 / 96.0));
        check(QStringLiteral("Q7 printPageDpi: page 3 at its own 100 dpi (%1 px wide)").arg(autoImages[3].image.width()),
              autoImages[3].image.width() == qRound(794 * 100 / 96.0));
    } else {
        check(QStringLiteral("Q7 four images in the auto print"), false);
    }
    bool autoA4 = false;
    a4MediaBoxes(autoPdf, false, &autoA4);
    check(QStringLiteral("Q7 every page is still A4"), autoA4);
    delete root;
}

// A HiDPI grab (a 5K or Retina window, devicePixelRatio 2) comes back twice the size asked for, and Qt draws
// the page either in the top-left corner with blank paper around it or scaled to fill. Both must land on the
// sheet as the page at the requested size (21 Oct 2026: tiny text in the top-left of every page).
static void testHiDpiGrab(const QTemporaryDir &dir)
{
    std::printf("\nHiDPI grabs: a double-size image comes back to the size asked for\n");
    const QSize asked(1240, 1754);
    const QImage page = drawnPage(0, asked);
    QImage corner(asked * 2, QImage::Format_RGB32);
    corner.fill(Qt::white);
    { QPainter p(&corner); p.drawImage(0, 0, page); }
    const QImage filled = page.scaled(asked * 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    for (const QString &enc : {QStringLiteral("lossless"), QStringLiteral("auto")}) {
        const QString path = dir.filePath(QStringLiteral("hidpi-%1.pdf").arg(enc));
        PdfDocumentWriter w;
        w.setEncoding(enc);
        w.begin(path, false, 150);
        check(QStringLiteral("H1 %1: the page in the corner").arg(enc), w.addPage(QVariant::fromValue(corner), false, asked));
        check(QStringLiteral("H1 %1: the page scaled to fill").arg(enc), w.addPage(QVariant::fromValue(filled), false, asked));
        check(QStringLiteral("H1 %1: a ratio-1 grab").arg(enc), w.addPage(QVariant::fromValue(page), false, asked));
        check(QStringLiteral("H1 %1: end()").arg(enc), w.end());
        const QVector<QSize> sizes = imageSizes(readAll(path));
        check(QStringLiteral("H2 %1: all three pages are stored at the size asked for").arg(enc),
              sizes == QVector<QSize>({asked, asked, asked}));
    }
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
    testAutoEncoding(dir);
    testMixedResolution(dir);
    testHiDpiGrab(dir);
    testQmlPrinter(dir);
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
