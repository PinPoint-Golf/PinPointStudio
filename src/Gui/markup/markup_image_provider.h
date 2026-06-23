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

// MarkupImageProvider — the QML image source for the Markup Lab's exact-frame
// view. MarkupController decodes one MP4 frame on demand (cv::VideoCapture) and
// pushes it here; QML pulls it via `image://markup/<token>`. A bumped token in
// the URL busts the QML pipeline cache so the new frame is re-requested. The
// QImage is implicitly shared and copied under a mutex (requestImage may run off
// the GUI thread), so producer/consumer never tear the same buffer.

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

class MarkupImageProvider : public QQuickImageProvider
{
public:
    MarkupImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    void setImage(const QImage &img)
    {
        QMutexLocker lock(&m_mutex);
        m_image = img;
    }

    QImage requestImage(const QString & /*id*/, QSize *size, const QSize & /*requested*/) override
    {
        QMutexLocker lock(&m_mutex);
        if (size) *size = m_image.size();
        return m_image;
    }

private:
    QMutex m_mutex;
    QImage m_image;
};
