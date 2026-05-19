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

#include "bayer_video_item.h"

#include <QFile>
#include <QMutexLocker>
#include <rhi/qrhi.h>
#include "pp_debug.h"

// ---------------------------------------------------------------------------
// UBO layout — must match bayer_demosaic.vert / .frag exactly (std140).
//
// Offsets:
//   0  : mat4 corrMatrix (16 floats = 64 bytes)
//   64 : float pattern   (4 bytes)
//   68 : float texW      (4 bytes)
//   72 : float texH      (4 bytes)
//   76 : float opacity   (4 bytes)
//   total: 80 bytes
// ---------------------------------------------------------------------------
struct BayerUBO {
    float corrMatrix[16];
    float pattern;   // float avoids int/float mixing in HLSL constant buffers
    float texW;
    float texH;
    float opacity;
};
static_assert(sizeof(BayerUBO) == 80, "BayerUBO size mismatch");

// Fullscreen quad — (x, y, z, w, u, v), OpenGL Y-up convention.
// corrMatrix in the vertex shader applies rhi->clipSpaceCorrMatrix() to
// handle Y-axis differences between Vulkan/Metal and OpenGL/D3D11.
static const float kQuadVerts[] = {
    -1.f,  1.f, 0.f, 1.f,   0.f, 0.f,  // top-left
     1.f,  1.f, 0.f, 1.f,   1.f, 0.f,  // top-right
    -1.f, -1.f, 0.f, 1.f,   0.f, 1.f,  // bottom-left
     1.f, -1.f, 0.f, 1.f,   1.f, 1.f,  // bottom-right
};
static const quint16 kQuadIdx[] = {0, 2, 1, 1, 2, 3};

static QShader loadShader(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        ppError() << "[BayerVideoItem] cannot open shader:" << path;
        return {};
    }
    QShader s = QShader::fromSerialized(f.readAll());
    if (!s.isValid())
        ppWarn() << "[BayerVideoItem] invalid shader:" << path;
    return s;
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------
class BayerVideoItemRenderer : public QQuickRhiItemRenderer
{
public:
    ~BayerVideoItemRenderer() override;
    void initialize(QRhiCommandBuffer *cb) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *cb) override;

private:
    void buildPipeline();
    void rebuildTexture(QRhiResourceUpdateBatch *batch);

    QRhi                      *m_rhi     = nullptr;  // tracked to detect device loss
    QRhiTexture              *m_tex      = nullptr;
    QRhiTexture              *m_oldTex   = nullptr;  // deferred one-frame deletion
    QRhiSampler              *m_sampler  = nullptr;
    QRhiBuffer               *m_ubuf     = nullptr;
    QRhiBuffer               *m_vbuf     = nullptr;
    QRhiBuffer               *m_ibuf     = nullptr;
    QRhiShaderResourceBindings *m_srb    = nullptr;
    QRhiGraphicsPipeline     *m_pipeline = nullptr;
    bool                      m_initialized = false;

    // Synchronized from the item in synchronize().
    RawVideoFrame m_frame;
    bool          m_frameDirty = false;

    int m_texW = 0;
    int m_texH = 0;
};

BayerVideoItemRenderer::~BayerVideoItemRenderer()
{
    delete m_pipeline;
    delete m_srb;
    delete m_ubuf;
    delete m_sampler;
    delete m_oldTex;
    delete m_tex;
    delete m_ibuf;
    delete m_vbuf;
}

void BayerVideoItemRenderer::buildPipeline()
{
    delete m_pipeline;
    m_pipeline = rhi()->newGraphicsPipeline();
    m_pipeline->setShaderStages({
        { QRhiShaderStage::Vertex,   loadShader(":/shaders/src/Shaders/bayer_demosaic.vert.qsb") },
        { QRhiShaderStage::Fragment, loadShader(":/shaders/src/Shaders/bayer_demosaic.frag.qsb") },
    });

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ QRhiVertexInputBinding(6 * sizeof(float)) });
    inputLayout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float4, 0),
        QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float2, 4 * sizeof(float)),
    });
    m_pipeline->setVertexInputLayout(inputLayout);
    m_pipeline->setShaderResourceBindings(m_srb);
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    // clipSpaceCorrMatrix() flips Y on D3D11/Vulkan/Metal, reversing the winding
    // of our CCW fullscreen quad to CW.  Disable culling so the back-face culler
    // doesn't silently drop both triangles (which would output only black).
    m_pipeline->setCullMode(QRhiGraphicsPipeline::None);
    m_pipeline->setDepthTest(false);
    m_pipeline->setDepthWrite(false);

    if (!m_pipeline->create())
        ppWarn() << "[BayerVideoItem] pipeline create failed";
}

void BayerVideoItemRenderer::initialize(QRhiCommandBuffer *cb)
{
    QRhi *rhi = this->rhi();

    if (m_initialized && m_rhi == rhi) {
        // The render target size changed (item resized) but the RHI and format
        // are the same — only rebuild the pipeline against the new render pass
        // descriptor.  All other resources (textures, buffers, SRB) remain valid.
        buildPipeline();
        return;
    }

    // Full initialisation: first call or after a D3D device reset.
    if (m_initialized) {
        delete m_pipeline; m_pipeline = nullptr;
        delete m_srb;      m_srb      = nullptr;
        delete m_ubuf;     m_ubuf     = nullptr;
        delete m_sampler;  m_sampler  = nullptr;
        delete m_oldTex;   m_oldTex   = nullptr;
        delete m_tex;      m_tex      = nullptr;
        delete m_ibuf;     m_ibuf     = nullptr;
        delete m_vbuf;     m_vbuf     = nullptr;
        m_initialized = false;
        m_texW = m_texH = 0;
    }

    m_rhi = rhi;
    ppInfo() << "[BayerVideoItem] full init — backend:" << rhi->backendName();

    m_vbuf = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuadVerts));
    if (!m_vbuf->create()) { ppWarn() << "[BayerVideoItem] vbuf create failed"; return; }
    m_ibuf = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, sizeof(kQuadIdx));
    if (!m_ibuf->create()) { ppWarn() << "[BayerVideoItem] ibuf create failed"; return; }

    auto *u = rhi->nextResourceUpdateBatch();
    u->uploadStaticBuffer(m_vbuf, kQuadVerts);
    u->uploadStaticBuffer(m_ibuf, kQuadIdx);

    m_ubuf = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(BayerUBO));
    if (!m_ubuf->create()) { ppWarn() << "[BayerVideoItem] ubuf create failed"; return; }

    // 1×1 R8 placeholder; actual camera-resolution texture created in rebuildTexture().
    m_tex = rhi->newTexture(QRhiTexture::R8, QSize(1, 1), 1, {});
    if (!m_tex->create()) { ppWarn() << "[BayerVideoItem] tex create failed"; return; }
    const quint8 black = 0;
    u->uploadTexture(m_tex, QRhiTextureUploadEntry(0, 0,
        QRhiTextureSubresourceUploadDescription(&black, 1)));

    m_sampler = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                QRhiSampler::None,
                                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
    if (!m_sampler->create()) { ppWarn() << "[BayerVideoItem] sampler create failed"; return; }

    m_srb = rhi->newShaderResourceBindings();
    m_srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_ubuf),
        QRhiShaderResourceBinding::sampledTexture(1,
            QRhiShaderResourceBinding::FragmentStage,
            m_tex, m_sampler),
    });
    if (!m_srb->create()) { ppWarn() << "[BayerVideoItem] srb create failed"; return; }

    buildPipeline();

    m_initialized = true;
    cb->resourceUpdate(u);
}

void BayerVideoItemRenderer::rebuildTexture(QRhiResourceUpdateBatch *batch)
{
    QRhi *rhi = this->rhi();
    // Move old texture to deferred-delete slot; it's freed at the start of the
    // NEXT render() call after the GPU has finished with it.
    delete m_oldTex;
    m_oldTex = m_tex;
    m_tex = rhi->newTexture(QRhiTexture::R8, QSize(m_frame.width, m_frame.height), 1, {});
    m_tex->create();

    m_srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_ubuf),
        QRhiShaderResourceBinding::sampledTexture(1,
            QRhiShaderResourceBinding::FragmentStage,
            m_tex, m_sampler),
    });
    m_srb->create();

    m_texW = m_frame.width;
    m_texH = m_frame.height;

    QRhiTextureSubresourceUploadDescription sub(
        m_frame.data.constData(), m_frame.data.size());
    batch->uploadTexture(m_tex, QRhiTextureUploadEntry(0, 0, sub));
}

void BayerVideoItemRenderer::synchronize(QQuickRhiItem *item)
{
    auto *bvi = static_cast<BayerVideoItem *>(item);
    QMutexLocker lk(&bvi->m_frameMutex);
    if (bvi->m_pendingDirty) {
        m_frame      = bvi->m_pendingFrame;
        m_frameDirty = true;
        bvi->m_pendingDirty = false;
    }
}

void BayerVideoItemRenderer::render(QRhiCommandBuffer *cb)
{
    // Release the texture deferred from the previous frame — safe now that the
    // GPU has finished with that frame's commands.
    delete m_oldTex;
    m_oldTex = nullptr;

    if (!m_initialized || !m_pipeline) {
        cb->beginPass(renderTarget(), QColor(Qt::transparent), {1.0f, 0}, nullptr);
        cb->endPass();
        return;
    }

    QRhi *rhi = this->rhi();
    auto *batch = rhi->nextResourceUpdateBatch();

    if (m_frameDirty && !m_frame.isNull()) {
        if (m_frame.width != m_texW || m_frame.height != m_texH)
            rebuildTexture(batch);
        else {
            QRhiTextureSubresourceUploadDescription sub(
                m_frame.data.constData(), m_frame.data.size());
            batch->uploadTexture(m_tex, QRhiTextureUploadEntry(0, 0, sub));
        }
        m_frameDirty = false;
    }

    BayerUBO ubo{};
    const QMatrix4x4 corr = rhi->clipSpaceCorrMatrix();
    memcpy(ubo.corrMatrix, corr.constData(), sizeof(ubo.corrMatrix));
    ubo.pattern = static_cast<float>(m_frame.pattern);
    ubo.texW    = static_cast<float>(m_texW > 0 ? m_texW : 1);
    ubo.texH    = static_cast<float>(m_texH > 0 ? m_texH : 1);
    ubo.opacity = 1.0f;
    batch->updateDynamicBuffer(m_ubuf, 0, sizeof(BayerUBO), &ubo);

    const QSize outputSize = renderTarget()->pixelSize();
    cb->beginPass(renderTarget(), QColor(Qt::black), {1.0f, 0}, batch);
    cb->setGraphicsPipeline(m_pipeline);
    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
    cb->setShaderResources(m_srb);
    const QRhiCommandBuffer::VertexInput vi(m_vbuf, 0);
    cb->setVertexInput(0, 1, &vi, m_ibuf, 0, QRhiCommandBuffer::IndexUInt16);
    cb->drawIndexed(6);
    cb->endPass();
}

// ---------------------------------------------------------------------------
// BayerVideoItem
// ---------------------------------------------------------------------------

BayerVideoItem::BayerVideoItem(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
    setFlag(ItemHasContents, true);
    // Pin the render target to a stable placeholder immediately.  Without this,
    // QQuickRhiItem tracks item->size().toSize() which oscillates ±1px every
    // render tick (fractional ColumnLayout heights + integer rounding), causing
    // initialize() to be called on every frame and leaking thousands of resources.
    // The first updateFrame() call upgrades this to the actual camera resolution.
    setFixedColorBufferWidth(1);
    setFixedColorBufferHeight(1);
}

QQuickRhiItemRenderer *BayerVideoItem::createRenderer()
{
    return new BayerVideoItemRenderer;
}

void BayerVideoItem::updateFrame(const RawVideoFrame &frame)
{
    if (!frame.isNull()) {
        // Pin render target to camera resolution on first frame.  After this,
        // the buffer size is stable and initialize() is not called again due to
        // item size fluctuations (Qt 6.11 API: fixedColorBufferWidth/Height).
        if (fixedColorBufferWidth()  != frame.width ||
            fixedColorBufferHeight() != frame.height)
        {
            setFixedColorBufferWidth(frame.width);
            setFixedColorBufferHeight(frame.height);
        }
    }
    {
        QMutexLocker lk(&m_frameMutex);
        m_pendingFrame = frame;
        m_pendingDirty = true;
    }
    update();
}
