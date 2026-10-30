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

#include "offscreen_sheet.h"

#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QtMath>
#include <rhi/qrhi.h>

#include "pp_debug.h"

OffscreenSheet::OffscreenSheet(QObject *parent) : QObject(parent)
{
    // Polish requests arrive in bursts (a pager re-flows, every Column in it asks); run them once the burst
    // is over, as a shown window's next frame would.
    m_polish.setSingleShot(true);
    m_polish.setInterval(0);
    connect(&m_polish, &QTimer::timeout, this, [this] {
        if (m_control)
            m_control->polishItems();
    });
}

OffscreenSheet::~OffscreenSheet()
{
    release();
}

bool OffscreenSheet::adopt(QQuickItem *item)
{
    if (m_window || m_busy)
        return fail(tr("The sheet already holds an item."));
    if (!item || !(item->width() > 0 && item->height() > 0))
        return fail(tr("There is no sheet to adopt, or it has no size."));
    m_busy = std::make_unique<BusyActivity>("Printing a session report");
    m_error.clear();

    m_control = std::make_unique<QQuickRenderControl>();
    m_window = std::make_unique<QQuickWindow>(m_control.get());
    m_window->setColor(Qt::white);
    const QSize logical(qCeil(item->width()), qCeil(item->height()));
    m_window->setGeometry(QRect(QPoint(0, 0), logical));
    m_window->contentItem()->setSize(logical);
    if (!m_control->initialize()) {
        teardown();
        return fail(tr("Qt Quick has no RHI here, so the sheet cannot be drawn in a window of its own."));
    }
    if (!makeTarget(logical, 1.0)) {
        teardown();
        return false;
    }
    connect(m_control.get(), &QQuickRenderControl::sceneChanged, &m_polish, qOverload<>(&QTimer::start));
    connect(m_control.get(), &QQuickRenderControl::renderRequested, &m_polish, qOverload<>(&QTimer::start));

    m_item = item;
    item->setParentItem(m_window->contentItem());
    item->setPosition(QPointF(0, 0));
    m_polish.start();
    ppInfo() << "[OffscreenSheet] adopted a" << logical.width() << "x" << logical.height() << "sheet on"
             << m_control->rhi()->backendName();
    emit activeChanged();
    return true;
}

QImage OffscreenSheet::grab(const QSize &pixelSize)
{
    if (!m_window || !m_item) {
        fail(tr("There is no sheet to draw."));
        return {};
    }
    if (pixelSize.isEmpty()) {
        fail(tr("A sheet cannot be drawn at no size."));
        return {};
    }
    const qreal dpr = qreal(pixelSize.width()) / m_window->width();
    if (!makeTarget(pixelSize, dpr))
        return {};

    QRhi *rhi = m_control->rhi();
    m_polish.stop();
    m_control->polishItems();
    m_control->beginFrame();
    m_control->sync();
    m_control->render();
    QRhiReadbackResult readback;
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    batch->readBackTexture(m_texture.get(), &readback);
    m_control->commandBuffer()->resourceUpdate(batch);
    m_control->endFrame();      // an offscreen frame: waits for the GPU, so the readback is complete

    if (readback.data.isEmpty()) {
        fail(tr("The sheet was drawn but could not be read back."));
        return {};
    }
    QImage image(reinterpret_cast<const uchar *>(readback.data.constData()), readback.pixelSize.width(),
                 readback.pixelSize.height(), QImage::Format_RGBA8888_Premultiplied);
    return rhi->isYUpInFramebuffer() ? image.flipped(Qt::Vertical) : image.copy();
}

void OffscreenSheet::release()
{
    const bool was = m_window != nullptr;
    if (m_item && m_window && m_item->window() == m_window.get())
        m_item->setParentItem(nullptr);
    m_item = nullptr;
    teardown();
    m_busy.reset();
    if (was)
        emit activeChanged();
}

// The colour texture the window draws into, with the depth-stencil Qt Quick clips through. Kept while the
// size holds (every page of a run is grabbed at one or two sizes).
bool OffscreenSheet::makeTarget(const QSize &pixelSize, qreal dpr)
{
    if (m_target && m_targetSize == pixelSize)
        return true;
    QRhi *rhi = m_control->rhi();
    m_window->setRenderTarget(QQuickRenderTarget());
    dropTarget();

    m_texture.reset(rhi->newTexture(QRhiTexture::RGBA8, pixelSize, 1,
                                    QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!m_texture->create())
        return fail(tr("A %1 × %2 page could not be drawn: no texture.").arg(pixelSize.width()).arg(pixelSize.height()));
    m_depth.reset(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, pixelSize, 1));
    if (!m_depth->create())
        return fail(tr("A %1 × %2 page could not be drawn: no depth buffer.").arg(pixelSize.width()).arg(pixelSize.height()));
    QRhiTextureRenderTargetDescription desc { QRhiColorAttachment(m_texture.get()) };
    desc.setDepthStencilBuffer(m_depth.get());
    m_target.reset(rhi->newTextureRenderTarget(desc));
    m_pass.reset(m_target->newCompatibleRenderPassDescriptor());
    m_target->setRenderPassDescriptor(m_pass.get());
    if (!m_target->create())
        return fail(tr("A %1 × %2 page could not be drawn: no render target.").arg(pixelSize.width()).arg(pixelSize.height()));

    QQuickRenderTarget target = QQuickRenderTarget::fromRhiRenderTarget(m_target.get());
    target.setDevicePixelRatio(dpr);
    m_window->setRenderTarget(target);
    m_targetSize = pixelSize;
    return true;
}

void OffscreenSheet::dropTarget()
{
    m_target.reset();
    m_pass.reset();
    m_depth.reset();
    m_texture.reset();
    m_targetSize = QSize();
}

// The RHI resources belong to the render control's QRhi, so they go first; then the window, whose scene
// graph is released through the control; then the control.
void OffscreenSheet::teardown()
{
    m_polish.stop();
    if (m_window)
        m_window->setRenderTarget(QQuickRenderTarget());
    dropTarget();
    m_window.reset();
    m_control.reset();
}

bool OffscreenSheet::fail(const QString &why)
{
    m_error = why;
    ppWarn() << "[OffscreenSheet]" << why;
    emit activeChanged();
    return false;
}
