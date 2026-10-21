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

#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QPainter;
class QPdfWriter;
class QSaveFile;
namespace pdf_detail { class PdfAFile; }

// PdfDocumentWriter — print is to PDF. It turns page images into one A4 document.
//
// The print registers lay a report out as A4 pages at 96 dpi logical (794×1123 portrait,
// 1123×794 landscape) and step `printPage` through `printPageCount`. Each page already carries
// its own margins, running head and folio, so this class adds NOTHING to the page: every image
// is drawn over the whole sheet, aspect kept, centred if its shape differs from the sheet's.
// The QML half that steps a register and grabs its pages is PpPdfPrinter.qml beside this file.
//
//     begin(path, landscape, dpi, title, author)  →  addPage(image) × n  →  end()
//
// The document is written through a QSaveFile: nothing appears at `path` until end() succeeds,
// and abort(), a failed end() or destroying the writer mid-document leaves whatever was there
// before untouched. No dialogs, no menus: every failure is a false return plus `errorString`.
//
// The output is PDF/A-1b (archival: sRGB output intent, XMP metadata with the title, author,
// subject and creator "PinPoint Studio"). PDF/A forbids transparency, so a page image with alpha
// is flattened onto white first. `dpi` sets the writer's coordinate resolution, not a resample,
// so a page grabbed at 300 dpi stays 300 dpi in the file. Pages of one document may differ in
// pixel size (the report's appendix is grabbed at 200 dpi, its client pages at 300): each image is
// fitted to the whole A4 sheet, so every page stays A4 and only its image's resolution differs —
// in both encodings.
//
// How a page image is stored is `encoding`, fixed for a document when begin() opens it:
//
//   "lossless" (the default)  Qt's own QPdfWriter, exactly as before: every page Flate-compressed
//       24-bit RGB (or Qt's fixed-quality-94 JPEG when `lossless` is false). Biggest file; what
//       the Session diagnostics print has always written.
//   "auto"  this class's own PDF/A-1b serializer (QPdfEngine cannot do what follows: it expands
//       any palette image to RGB and hard-codes its JPEG quality). A page is reduced to an 8-bit
//       (or 1/2/4-bit) palette, NOT dithered, and Flate-compressed: a report page is paper, ink,
//       a handful of accent colours and the anti-aliased edges between them, so it quantises
//       with no visible loss — a page with 256 colours or fewer is stored exactly. A page passed
//       with `photo` true has its photographs found and stored as JPEG tiles over the palette
//       page, so a still keeps its tones without the text round it ringing. The real mock
//       report: 6.5 MB lossless → ~2.4 MB auto at 300 dpi (build/report-build/encoding/).
//
// `subject` (the session date, for the report) goes into the Info dictionary and the XMP in
// "auto". Qt's QPdfWriter has no subject at all, so "lossless" files carry none.
class PdfDocumentWriter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageCountChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(QString path READ path NOTIFY activeChanged)
    // "lossless" | "auto" (see above). Read by begin(); a change while a document is open applies
    // to the next one.
    Q_PROPERTY(QString encoding READ encoding WRITE setEncoding NOTIFY encodingChanged)
    // The document's subject (Info /Subject and XMP dc:description). Read by begin(); "auto" only.
    Q_PROPERTY(QString subject READ subject WRITE setSubject NOTIFY subjectChanged)
    // "lossless" encoding only: Flate RGB (true, the default) or Qt's JPEG. Read at each addPage().
    Q_PROPERTY(bool lossless READ lossless WRITE setLossless NOTIFY losslessChanged)

public:
    explicit PdfDocumentWriter(QObject *parent = nullptr);
    ~PdfDocumentWriter() override;

    // Opens a new document at `path` (a local path or a file: URL). A4, `landscape` or portrait,
    // no margins, `dpi` (72..1200) as the coordinate resolution. An empty `title` becomes the
    // file's base name. Fails if a document is already open or the file cannot be created.
    Q_INVOKABLE bool begin(const QString &path, bool landscape, int dpi = 300,
                           const QString &title = QString(), const QString &author = QString());

    // Appends one page. Accepts what QML has to hand: a QImage (ItemGrabResult.image), a QPixmap,
    // the ItemGrabResult itself (anything with an `image` property), or an image file's path/URL.
    // `photo`: the page draws a photograph (a register's printPageHasPhoto). "auto" stores the
    // photographs on it as JPEG; "lossless" ignores it.
    // `requested` is the size the caller grabbed at. On a HiDPI window (devicePixelRatio 2 on a 5K or
    // Retina screen) QQuickItem::grabToImage returns an image that many times bigger: the page either drawn
    // in its top-left `requested` corner (kept) or scaled to fill it (scaled back down). Offscreen and
    // ratio-1 grabs come back at `requested` and are untouched.
    Q_INVOKABLE bool addPage(const QVariant &image, bool photo = false, const QSize &requested = QSize());

    // How the last page was stored, for measurement and tests: {encoding: "rgb" (lossless) |
    // "palette" | "palette+jpeg" | "jpeg", bytes (image bytes on the page, "auto" only), colours,
    // exact (every colour kept), bitsPerPixel, maxError (largest channel error a palette made,
    // photographs aside), photos (JPEG tiles)}.
    Q_INVOKABLE QVariantMap lastPageInfo() const { return m_lastPage; }

    // Finishes the document and moves it into place. Fails, writing nothing, if no page was added.
    Q_INVOKABLE bool end();

    // Abandons the open document; the destination is left as it was. A no-op when none is open.
    Q_INVOKABLE void abort();

    // The C++ face of addPage().
    bool addImage(const QImage &image, bool photo = false);

    int pageCount() const { return m_pageCount; }
    QString errorString() const { return m_error; }
    bool isActive() const { return m_file != nullptr; }
    QString path() const { return m_path; }
    QString encoding() const { return m_encoding; }
    void setEncoding(const QString &encoding);
    QString subject() const { return m_subject; }
    void setSubject(const QString &subject);
    bool lossless() const { return m_lossless; }
    void setLossless(bool on);

signals:
    void pageCountChanged();
    void errorStringChanged();
    void activeChanged();
    void encodingChanged();
    void subjectChanged();
    void losslessChanged();

private:
    bool fail(const QString &message);
    void clearError();
    void release(bool commit);
    static QImage imageFrom(const QVariant &v, QString *why, int depth = 0);

    // Destroyed in reverse order: the painter before the writer before the file it writes to.
    std::unique_ptr<QSaveFile>  m_file;
    std::unique_ptr<QPdfWriter> m_writer;
    std::unique_ptr<QPainter>   m_painter;
    // "auto": this class's own serializer instead of the three above (the file stays m_file).
    std::unique_ptr<pdf_detail::PdfAFile> m_raw;

    QString m_path;
    QString m_error;
    QString m_encoding = QStringLiteral("lossless");
    QString m_subject;
    QVariantMap m_lastPage;
    int     m_pageCount = 0;
    bool    m_lossless = true;
};
