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
