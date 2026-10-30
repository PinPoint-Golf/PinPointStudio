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
#include <QPointer>
#include <QQuickItem>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>

#include "busy_activity.h"

class QQuickRenderControl;
class QQuickWindow;
class QRhiRenderBuffer;
class QRhiRenderPassDescriptor;
class QRhiTexture;
class QRhiTextureRenderTarget;

// OffscreenSheet — a window of its own for a sheet that is printed, drawn whether or not the app is seen.
//
// WHY. QQuickItem::grabToImage renders through the item's window, and a window only renders when it is
// exposed: on macOS a window that is covered, minimised or on another Space stops drawing (Windows and
// Linux stop when it is minimised), and the Qt Quick polish that lays a page out stops with it. A report
// printed from a sheet in the app window therefore stalled whenever the app was out of sight, and went on
// when it came back. The sheet is moved here instead: a QQuickWindow that is never shown, driven by a
// QQuickRenderControl, so this class decides when it lays out and when it draws.
//
//     adopt(item)  →  (layout runs on its own)  →  grab(pixelSize) × n  →  release()
//
// adopt() reparents the item (visually only; its QObject parent and ownership are untouched) into the
// window's content at (0, 0), the window sized to it. Between grabs every polish the sheet asks for is run
// at once, so pagers, Columns and Repeaters settle exactly as they would in a shown window. grab() polishes,
// synchronises and renders one frame into a texture of `pixelSize` (device pixel ratio = pixelSize / item
// size, so text and vectors are drawn at that resolution, not scaled up) and reads it back, synchronously.
// release() hands the item back with no visual parent and tears the window down; call it before the item
// is destroyed.
//
// No RHI, no window: where Qt Quick runs on the software adaptation (the offscreen test platform), adopt()
// fails, `errorString` says why, and the caller keeps the sheet in its own window and grabs it the old way.
//
// APP NAP. A Mac app out of sight is also napped: its timers are coalesced, and the print's settle timers
// stretch from 250 ms to many seconds. adopt() holds a user-initiated activity (BusyActivity) until
// release() — whether or not the window could be made, since the fallback needs it as much.
class OffscreenSheet : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool active READ isActive NOTIFY activeChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY activeChanged)

public:
    explicit OffscreenSheet(QObject *parent = nullptr);
    ~OffscreenSheet() override;

    // Moves `item` into this sheet's own window. False if the window cannot be made (see above): the item
    // is left where it was.
    Q_INVOKABLE bool adopt(QQuickItem *item);

    // Draws the adopted item into a `pixelSize` image. A null image on failure (`errorString` says why).
    Q_INVOKABLE QImage grab(const QSize &pixelSize);

    // Gives the item back (no visual parent) and destroys the window. A no-op when nothing is adopted.
    Q_INVOKABLE void release();

    bool isActive() const { return m_window != nullptr; }
    QString errorString() const { return m_error; }

signals:
    void activeChanged();

private:
    bool makeTarget(const QSize &pixelSize, qreal dpr);
    void dropTarget();
    void teardown();
    bool fail(const QString &why);

    std::unique_ptr<BusyActivity> m_busy;
    std::unique_ptr<QQuickRenderControl> m_control;
    std::unique_ptr<QQuickWindow> m_window;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiRenderBuffer> m_depth;
    std::unique_ptr<QRhiTextureRenderTarget> m_target;
    std::unique_ptr<QRhiRenderPassDescriptor> m_pass;
    QSize m_targetSize;
    QPointer<QQuickItem> m_item;
    QTimer m_polish;
    QString m_error;
};
