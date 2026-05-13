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

#include <QMutex>
#include <QQuickRhiItem>
#include <QtQml/qqml.h>
#include "raw_video_frame.h"

class BayerVideoItemRenderer;

// QQuickRhiItem that displays raw Bayer frames via a GPU bilinear demosaic shader.
//
// Usage from C++:
//   controller->setBayerItem(bayerView);   // called once from QML
//   controller calls updateFrame() on the main thread for each display-rate frame
//
// Usage from QML:
//   BayerVideoItem { id: bayerView; anchors.fill: parent }
//   Component.onCompleted: controller.setBayerItem(bayerView)

class BayerVideoItem : public QQuickRhiItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit BayerVideoItem(QQuickItem *parent = nullptr);

    // Called from the main thread (drainRawFrame) to push a new camera frame.
    void updateFrame(const RawVideoFrame &frame);

protected:
    QQuickRhiItemRenderer *createRenderer() override;

private:
    friend class BayerVideoItemRenderer;

    // Written on the main thread, read in synchronize() (render thread, GUI locked).
    QMutex       m_frameMutex;
    RawVideoFrame m_pendingFrame;
    bool          m_pendingDirty = false;
};
