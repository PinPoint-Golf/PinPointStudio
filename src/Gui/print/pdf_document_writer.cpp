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

#include "pdf_document_writer.h"

#include <QFileInfo>
#include <QJSValue>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPixmap>
#include <QSaveFile>
#include <QUrl>

namespace {

// QML hands paths over as strings that are sometimes "file:///…" URLs.
QString localPath(const QString &pathOrUrl)
{
    if (pathOrUrl.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
        return QUrl(pathOrUrl).toLocalFile();
    return pathOrUrl;
}

constexpr int kMinDpi = 72;
constexpr int kMaxDpi = 1200;

} // namespace

PdfDocumentWriter::PdfDocumentWriter(QObject *parent)
    : QObject(parent)
{
}

PdfDocumentWriter::~PdfDocumentWriter()
{
    // A document still open here was never finished: discard it rather than leave half a PDF.
    if (m_file)
        release(false);
}

void PdfDocumentWriter::setLossless(bool on)
{
    if (m_lossless == on)
        return;
    m_lossless = on;
    emit losslessChanged();
}

bool PdfDocumentWriter::fail(const QString &message)
{
    if (m_error != message) {
        m_error = message;
        emit errorStringChanged();
    }
    return false;
}

void PdfDocumentWriter::clearError()
{
    if (!m_error.isEmpty()) {
        m_error.clear();
        emit errorStringChanged();
    }
}

// Ends the painter (which writes the PDF trailer), drops the writer, then either moves the file
// into place or discards it. Returns through m_error: a failed commit is reported by end().
void PdfDocumentWriter::release(bool commit)
{
    if (m_painter && m_painter->isActive())
        m_painter->end();
    m_painter.reset();
    m_writer.reset();
    if (m_file) {
        if (commit) {
            if (!m_file->commit())
                fail(tr("Could not save %1: %2").arg(m_path, m_file->errorString()));
        } else {
            m_file->cancelWriting();
        }
        m_file.reset();
    }
    emit activeChanged();
}

bool PdfDocumentWriter::begin(const QString &path, bool landscape, int dpi,
                              const QString &title, const QString &author)
{
    if (m_file)
        return fail(tr("A PDF is already being written to %1; end or abort it first.").arg(m_path));

    const QString file = localPath(path);
    if (file.isEmpty())
        return fail(tr("No file name was given for the PDF."));
    if (dpi < kMinDpi || dpi > kMaxDpi)
        return fail(tr("%1 dpi is outside %2–%3.").arg(dpi).arg(kMinDpi).arg(kMaxDpi));

    auto out = std::make_unique<QSaveFile>(file);
    if (!out->open(QIODevice::WriteOnly))
        return fail(tr("Could not create %1: %2").arg(file, out->errorString()));

    auto writer = std::make_unique<QPdfWriter>(out.get());
    writer->setPdfVersion(QPagedPaintDevice::PdfVersion_A1b);
    writer->setResolution(dpi);
    // The page images carry their own margins, so the sheet has none.
    const QPageLayout layout(QPageSize(QPageSize::A4),
                             landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                             QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter, QMarginsF(0, 0, 0, 0));
    if (!writer->setPageLayout(layout)) {
        out->cancelWriting();
        return fail(tr("Could not set an A4 page layout."));
    }
    writer->setTitle(title.isEmpty() ? QFileInfo(file).completeBaseName() : title);
    writer->setCreator(QStringLiteral("PinPoint Studio"));
    if (!author.isEmpty())
        writer->setAuthor(author);

    m_file = std::move(out);
    m_writer = std::move(writer);
    m_path = file;
    clearError();
    if (m_pageCount != 0) {
        m_pageCount = 0;
        emit pageCountChanged();
    }
    emit activeChanged();
    return true;
}

QImage PdfDocumentWriter::imageFrom(const QVariant &v, QString *why, int depth)
{
    const QMetaType type = v.metaType();
    switch (type.id()) {
    case QMetaType::QImage:
        return v.value<QImage>();
    case QMetaType::QPixmap:
        return v.value<QPixmap>().toImage();
    case QMetaType::QUrl:
    case QMetaType::QString: {
        const QString file = type.id() == QMetaType::QUrl ? v.toUrl().toLocalFile()
                                                         : localPath(v.toString());
        QImage img(file);
        if (img.isNull())
            *why = tr("Could not read an image from %1.").arg(file);
        return img;
    }
    default:
        break;
    }
    if (type == QMetaType::fromType<QJSValue>() && depth == 0)
        return imageFrom(v.value<QJSValue>().toVariant(), why, depth + 1);
    // An ItemGrabResult, or anything else that carries its picture as `image`.
    if ((type.flags() & QMetaType::PointerToQObject) && depth == 0) {
        const QObject *obj = *static_cast<QObject *const *>(v.constData());
        if (obj) {
            const QVariant image = obj->property("image");
            if (image.isValid())
                return imageFrom(image, why, depth + 1);
        }
    }
    *why = tr("addPage() needs an image; it was given %1.")
               .arg(type.isValid() ? QString::fromLatin1(type.name()) : QStringLiteral("nothing"));
    return {};
}

bool PdfDocumentWriter::addPage(const QVariant &image)
{
    if (!m_file)
        return fail(tr("addPage() was called before begin()."));
    QString why;
    const QImage img = imageFrom(image, &why);
    if (img.isNull())
        return fail(why.isEmpty() ? tr("addPage() was given an empty image.") : why);
    return addImage(img);
}

bool PdfDocumentWriter::addImage(const QImage &image)
{
    if (!m_file)
        return fail(tr("addPage() was called before begin()."));
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
        return fail(tr("addPage() was given an empty image."));

    // PDF/A has no transparency: lay the page on white paper.
    QImage page = image;
    if (page.hasAlphaChannel()) {
        QImage flat(page.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, page);
        p.end();
        page = flat;
    }

    if (!m_painter) {
        // The painter opens the first page; it starts on the first image, so a document with no
        // pages is never begun and end() can refuse it without leaving a blank sheet behind.
        m_painter = std::make_unique<QPainter>();
        if (!m_painter->begin(m_writer.get())) {
            m_painter.reset();
            return fail(tr("Could not start writing %1.").arg(m_path));
        }
    } else if (!m_writer->newPage()) {
        return fail(tr("Could not add page %1 to %2.").arg(m_pageCount + 1).arg(m_path));
    }

    m_painter->setRenderHint(QPainter::LosslessImageRendering, m_lossless);
    m_painter->setRenderHint(QPainter::SmoothPixmapTransform, true);

    // The painter's coordinates are the sheet in device pixels at the writer's resolution.
    const QSizeF sheet(m_writer->width(), m_writer->height());
    QSizeF fitted = QSizeF(page.size());
    fitted.scale(sheet, Qt::KeepAspectRatio);
    const QRectF target(QPointF((sheet.width() - fitted.width()) / 2.0,
                                (sheet.height() - fitted.height()) / 2.0),
                        fitted);
    m_painter->drawImage(target, page);

    ++m_pageCount;
    emit pageCountChanged();
    return true;
}

bool PdfDocumentWriter::end()
{
    if (!m_file)
        return fail(tr("end() was called before begin()."));
    if (m_pageCount == 0) {
        const QString path = m_path;
        release(false);
        return fail(tr("No pages were added, so %1 was not written.").arg(path));
    }
    clearError();
    release(true);
    return m_error.isEmpty();
}

void PdfDocumentWriter::abort()
{
    if (m_file)
        release(false);
}
