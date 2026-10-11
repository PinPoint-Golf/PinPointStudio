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
// The output is PDF/A-1b (archival: sRGB output intent, XMP metadata with the title, author and
// creator "PinPoint Studio"). PDF/A forbids transparency, so a page image with alpha is flattened
// onto white first. Images are embedded LOSSLESSLY by default — a report page is text and rules,
// and JPEG rings round both — at their own pixel size; `dpi` sets the writer's coordinate
// resolution, not a resample, so a page grabbed at 300 dpi stays 300 dpi in the file.
class PdfDocumentWriter : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageCountChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(QString path READ path NOTIFY activeChanged)
    // Lossless (Flate) image embedding instead of JPEG. Read at each addPage().
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
    Q_INVOKABLE bool addPage(const QVariant &image);

    // Finishes the document and moves it into place. Fails, writing nothing, if no page was added.
    Q_INVOKABLE bool end();

    // Abandons the open document; the destination is left as it was. A no-op when none is open.
    Q_INVOKABLE void abort();

    // The C++ face of addPage().
    bool addImage(const QImage &image);

    int pageCount() const { return m_pageCount; }
    QString errorString() const { return m_error; }
    bool isActive() const { return m_file != nullptr; }
    QString path() const { return m_path; }
    bool lossless() const { return m_lossless; }
    void setLossless(bool on);

signals:
    void pageCountChanged();
    void errorStringChanged();
    void activeChanged();
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

    QString m_path;
    QString m_error;
    int     m_pageCount = 0;
    bool    m_lossless = true;
};
