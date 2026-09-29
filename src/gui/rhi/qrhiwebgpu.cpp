// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qrhiwebgpu_p.h"
#include <qmath.h>
#include <QLoggingCategory>

#ifdef Q_OS_WASM
#include <emscripten.h>
#endif

#if defined(Q_OS_MACOS)
#include <QtGui/qpa/qplatformwindow_p.h>
Q_DECLARE_OPAQUE_POINTER(CALayer *)
#endif

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

QT_BEGIN_NAMESPACE

Q_LOGGING_CATEGORY(lcWebGPU, "qt.rhi.webgpu")

/*!
    \class QRhiWebGPUInitParams
    \inmodule QtGuiPrivate
    \inheaderfile rhi/qrhi.h
    \since 6.8
    \brief WebGPU specific initialization parameters.

    \note This is a RHI API with limited compatibility guarantees, see \l QRhi
    for details.

    A WebGPU QRhi needs no special parameters for initialization on Emscripten.
    On native platforms using Dawn, device creation happens automatically.

    \badcode
        QRhiWebGPUInitParams params;
        rhi = QRhi::create(QRhi::WebGPU, &params);
    \endcode

    The WebGPU backend provides a portable graphics API that works both
    in browsers via WebAssembly and on native platforms via Dawn.
 */

/*!
    \class QRhiWebGPUNativeHandles
    \inmodule QtGuiPrivate
    \inheaderfile rhi/qrhi.h
    \since 6.8
    \brief Holds the WebGPU device and queue used by the QRhi.

    \note This is a RHI API with limited compatibility guarantees, see \l QRhi
    for details.
 */

// QWebGPUBuffer

QWebGPUBuffer::QWebGPUBuffer(QRhiImplementation *rhi, Type type, UsageFlags usage, quint32 size)
    : QRhiBuffer(rhi, type, usage, size)
{
}

QWebGPUBuffer::~QWebGPUBuffer()
{
    destroy();
}

void QWebGPUBuffer::destroy()
{
    if (!buffer)
        return;

    qCDebug(lcWebGPU, "QWebGPUBuffer::destroy() buffer=%p size=%u", buffer, m_size);
    wgpuBufferRelease(buffer);
    buffer = nullptr;

    delete[] stagingData;
    stagingData = nullptr;

    QRHI_RES_RHI(QRhiWebGPU);
    if (rhiD)
        rhiD->unregisterResource(this);
}

bool QWebGPUBuffer::create()
{
    if (buffer)
        destroy();

    QRHI_RES_RHI(QRhiWebGPU);
    const quint32 roundedSize = rhiD->q->ubufAligned(m_size);

    qCDebug(lcWebGPU, "QWebGPUBuffer::create() type=%d usage=0x%x size=%u roundedSize=%u",
            int(m_type), int(m_usage), m_size, roundedSize);

    WGPUBufferUsage usage = WGPUBufferUsage_CopyDst;
    if (m_usage.testFlag(QRhiBuffer::VertexBuffer))
        usage |= WGPUBufferUsage_Vertex;
    if (m_usage.testFlag(QRhiBuffer::IndexBuffer))
        usage |= WGPUBufferUsage_Index;
    if (m_usage.testFlag(QRhiBuffer::UniformBuffer))
        usage |= WGPUBufferUsage_Uniform;
    if (m_usage.testFlag(QRhiBuffer::StorageBuffer))
        usage |= WGPUBufferUsage_Storage;

    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = roundedSize;
    desc.usage = usage;
    desc.mappedAtCreation = false;

    buffer = wgpuDeviceCreateBuffer(rhiD->device, &desc);
    if (!buffer) {
        qWarning("Failed to create WebGPU buffer");
        return false;
    }

    qCDebug(lcWebGPU, "QWebGPUBuffer::create() created buffer=%p", buffer);

    if (m_type == Dynamic) {
        stagingData = new char[roundedSize];
        memset(stagingData, 0, roundedSize);
    }

    generation += 1;
    rhiD->registerResource(this);
    return true;
}

QRhiBuffer::NativeBuffer QWebGPUBuffer::nativeBuffer()
{
    return { { &buffer }, 1 };
}

char *QWebGPUBuffer::beginFullDynamicBufferUpdateForCurrentFrame()
{
    return stagingData;
}

void QWebGPUBuffer::endFullDynamicBufferUpdateForCurrentFrame()
{
    if (!stagingData || !buffer)
        return;

    QRHI_RES_RHI(QRhiWebGPU);
    const quint32 roundedSize = rhiD->q->ubufAligned(m_size);
    wgpuQueueWriteBuffer(rhiD->queue, buffer, 0, stagingData, roundedSize);
}

// QWebGPURenderBuffer

QWebGPURenderBuffer::QWebGPURenderBuffer(QRhiImplementation *rhi, Type type, const QSize &pixelSize,
                                         int sampleCount, QRhiRenderBuffer::Flags flags,
                                         QRhiTexture::Format backingFormatHint)
    : QRhiRenderBuffer(rhi, type, pixelSize, sampleCount, flags, backingFormatHint)
{
}

QWebGPURenderBuffer::~QWebGPURenderBuffer()
{
    destroy();
}

void QWebGPURenderBuffer::destroy()
{
    if (!texture)
        return;

    qCDebug(lcWebGPU, "QWebGPURenderBuffer::destroy() texture=%p type=%d size=%dx%d",
            texture, int(m_type), m_pixelSize.width(), m_pixelSize.height());

    if (textureView) {
        wgpuTextureViewRelease(textureView);
        textureView = nullptr;
    }
    wgpuTextureRelease(texture);
    texture = nullptr;

    QRHI_RES_RHI(QRhiWebGPU);
    if (rhiD)
        rhiD->unregisterResource(this);
}

bool QWebGPURenderBuffer::create()
{
    if (texture)
        destroy();

    QRHI_RES_RHI(QRhiWebGPU);

    samples = rhiD->effectiveSampleCount(m_sampleCount);

    qCDebug(lcWebGPU, "QWebGPURenderBuffer::create() type=%d size=%dx%d sampleCount=%d flags=0x%x",
            int(m_type), m_pixelSize.width(), m_pixelSize.height(), m_sampleCount, int(m_flags));

    // UsedWithSwapChainOnly render buffers are created/resized by the
    // swapchain's createOrResize(), not here. Just register and return.
    if (m_flags.testFlag(UsedWithSwapChainOnly)) {
        qCDebug(lcWebGPU, "  UsedWithSwapChainOnly - deferring creation");
        generation += 1;
        rhiD->registerResource(this);
        return true;
    }

    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size.width = uint32_t(m_pixelSize.width());
    desc.size.height = uint32_t(m_pixelSize.height());
    desc.size.depthOrArrayLayers = 1;
    desc.mipLevelCount = 1;
    desc.sampleCount = uint32_t(samples);
    desc.dimension = WGPUTextureDimension_2D;
    desc.usage = WGPUTextureUsage_RenderAttachment;

    switch (m_type) {
    case Color:
        desc.format = QRhiWebGPU::toWgpuTextureFormat(backingFormat(), {});
        break;
    case DepthStencil:
        desc.format = WGPUTextureFormat_Depth24PlusStencil8;
        break;
    }

    texture = wgpuDeviceCreateTexture(rhiD->device, &desc);
    if (!texture) {
        qWarning("Failed to create WebGPU render buffer texture");
        return false;
    }

    textureView = wgpuTextureCreateView(texture, nullptr);

    qCDebug(lcWebGPU, "QWebGPURenderBuffer::create() created texture=%p textureView=%p format=%d",
            texture, textureView, desc.format);

    generation += 1;
    rhiD->registerResource(this);
    return true;
}

QRhiTexture::Format QWebGPURenderBuffer::backingFormat() const
{
    if (m_backingFormatHint != QRhiTexture::UnknownFormat)
        return m_backingFormatHint;
    return m_type == Color ? QRhiTexture::RGBA8 : QRhiTexture::UnknownFormat;
}

// QWebGPUTexture

QWebGPUTexture::QWebGPUTexture(QRhiImplementation *rhi, Format format, const QSize &pixelSize, int depth,
                               int arraySize, int sampleCount, Flags flags)
    : QRhiTexture(rhi, format, pixelSize, depth, arraySize, sampleCount, flags)
{
    qCDebug(lcWebGPU, "QWebGPUTexture() this=%p format=%d size=%dx%d depth=%d arraySize=%d sampleCount=%d flags=0x%x",
            this, int(format), pixelSize.width(), pixelSize.height(), depth, arraySize, sampleCount, int(flags));
}

QWebGPUTexture::~QWebGPUTexture()
{
    qWarning("~QWebGPUTexture() this=%p texture=%p textureView=%p size=%dx%d format=%d flags=0x%x",
             this, texture, textureView,
             m_pixelSize.width(), m_pixelSize.height(), int(m_format), int(m_flags));
    destroy();
}

void QWebGPUTexture::destroy()
{
    if (!texture) {
        qCDebug(lcWebGPU, "QWebGPUTexture::destroy() this=%p (no-op: texture already null, textureView=%p)", this, textureView);
        return;
    }

    qCDebug(lcWebGPU, "QWebGPUTexture::destroy() this=%p texture=%p textureView=%p format=%d size=%dx%d imported=%d",
            this, texture, textureView, int(m_format), m_pixelSize.width(), m_pixelSize.height(), imported);

    if (textureView) {
        wgpuTextureViewRelease(textureView);
        textureView = nullptr;
    }
    if (!imported)
        wgpuTextureRelease(texture);
    texture = nullptr;
    imported = false;

    QRHI_RES_RHI(QRhiWebGPU);
    if (rhiD)
        rhiD->unregisterResource(this);
}

bool QWebGPUTexture::prepareCreate(QSize *adjustedSize)
{
    QRHI_RES_RHI(QRhiWebGPU);
    const QSize size = m_pixelSize.isEmpty() ? QSize(1, 1) : m_pixelSize;

    if (adjustedSize)
        *adjustedSize = size;

    wgpuFormat = QRhiWebGPU::toWgpuTextureFormat(m_format, m_flags);
    if (wgpuFormat == WGPUTextureFormat_Undefined) {
        qWarning("Unsupported texture format %d", int(m_format));
        return false;
    }

    const bool hasMipMaps = m_flags.testFlag(MipMapped);
    mipLevelCount = hasMipMaps ? rhiD->q->mipLevelsForSize(size) : 1;

    // WebGPU requires multisampled textures to have exactly 1 mip level
    // and only 2D textures can be multisampled (not 3D, not 1D)
    const bool is3D = m_flags.testFlag(ThreeDimensional);
    if (mipLevelCount > 1 || is3D)
        samples = 1;
    else
        samples = rhiD->effectiveSampleCount(m_sampleCount);

    return true;
}

bool QWebGPUTexture::create()
{
    if (texture)
        destroy();

    QSize size;
    if (!prepareCreate(&size))
        return false;

    qCDebug(lcWebGPU, "QWebGPUTexture::create() this=%p format=%d size=%dx%d depth=%d arraySize=%d sampleCount=%d flags=0x%x",
            this, int(m_format), size.width(), size.height(), m_depth, m_arraySize, m_sampleCount, int(m_flags));

    QRHI_RES_RHI(QRhiWebGPU);

    const bool is3D = m_flags.testFlag(ThreeDimensional);
    const bool isCube = m_flags.testFlag(CubeMap);

    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.dimension = is3D ? WGPUTextureDimension_3D : WGPUTextureDimension_2D;
    desc.size.width = uint32_t(size.width());
    desc.size.height = uint32_t(size.height());
    // Cubemaps always have 6 faces (array layers)
    if (is3D)
        desc.size.depthOrArrayLayers = uint32_t(m_depth);
    else if (isCube)
        desc.size.depthOrArrayLayers = 6;
    else
        desc.size.depthOrArrayLayers = qMax(1, m_arraySize);
    desc.mipLevelCount = uint32_t(mipLevelCount);
    desc.sampleCount = uint32_t(samples);
    desc.format = wgpuFormat;
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    if (m_flags.testFlag(RenderTarget))
        desc.usage |= WGPUTextureUsage_RenderAttachment;
    if (m_flags.testFlag(UsedWithGenerateMips))
        desc.usage |= WGPUTextureUsage_CopySrc;
    if (m_flags.testFlag(UsedAsTransferSource))
        desc.usage |= WGPUTextureUsage_CopySrc;
    if (m_flags.testFlag(UsedWithLoadStore))
        desc.usage |= WGPUTextureUsage_StorageBinding;

    texture = wgpuDeviceCreateTexture(rhiD->device, &desc);
    if (!texture) {
        qWarning("Failed to create WebGPU texture");
        return false;
    }

    qCDebug(lcWebGPU, "QWebGPUTexture::create() this=%p texture=%p wgpuFormat=%d size=%dx%d mipLevels=%d samples=%d flags=0x%x",
            this, texture, wgpuFormat, m_pixelSize.width(), m_pixelSize.height(), mipLevelCount, samples, int(m_flags));

    const bool isArray = m_arraySize > 0 && !isCube;

    if (isCube || isArray || is3D) {
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.format = wgpuFormat;
        viewDesc.mipLevelCount = uint32_t(mipLevelCount);
        viewDesc.baseArrayLayer = 0;
        if (isCube) {
            viewDesc.dimension = WGPUTextureViewDimension_Cube;
            viewDesc.arrayLayerCount = 6;
        } else if (isArray) {
            viewDesc.dimension = WGPUTextureViewDimension_2DArray;
            viewDesc.arrayLayerCount = uint32_t(m_arraySize);
        } else {
            viewDesc.dimension = WGPUTextureViewDimension_3D;
            viewDesc.arrayLayerCount = 1;
        }
        textureView = wgpuTextureCreateView(texture, &viewDesc);
    } else {
        textureView = wgpuTextureCreateView(texture, nullptr);
    }

    if (!textureView) {
        qWarning("QWebGPUTexture::create(): wgpuTextureCreateView failed for format=%d wgpuFormat=%d size=%dx%d flags=0x%x isCube=%d isArray=%d is3D=%d",
                 int(m_format), int(wgpuFormat), size.width(), size.height(), int(m_flags),
                 isCube, isArray, is3D);
    }

    generation += 1;
    rhiD->registerResource(this);
    return true;
}

bool QWebGPUTexture::createFrom(NativeTexture src)
{
    if (texture)
        destroy();

    if (!src.object)
        return false;

    texture = reinterpret_cast<WGPUTexture>(src.object);
    imported = true;

    const bool isCube = m_flags.testFlag(CubeMap);
    const bool isArray = m_arraySize > 0 && !isCube;
    const bool is3D = m_flags.testFlag(ThreeDimensional);

    if (isCube || isArray || is3D) {
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED;
        viewDesc.baseArrayLayer = 0;
        if (isCube) {
            viewDesc.dimension = WGPUTextureViewDimension_Cube;
            viewDesc.arrayLayerCount = 6;
        } else if (isArray) {
            viewDesc.dimension = WGPUTextureViewDimension_2DArray;
            viewDesc.arrayLayerCount = uint32_t(m_arraySize);
        } else {
            viewDesc.dimension = WGPUTextureViewDimension_3D;
            viewDesc.arrayLayerCount = 1;
        }
        textureView = wgpuTextureCreateView(texture, &viewDesc);
    } else {
        textureView = wgpuTextureCreateView(texture, nullptr);
    }

    QRHI_RES_RHI(QRhiWebGPU);
    generation += 1;
    rhiD->registerResource(this, false);
    return true;
}

QRhiTexture::NativeTexture QWebGPUTexture::nativeTexture()
{
    return { quint64(texture), 0 };
}

// QWebGPUSampler

QWebGPUSampler::QWebGPUSampler(QRhiImplementation *rhi, Filter magFilter, Filter minFilter, Filter mipmapMode,
                               AddressMode u, AddressMode v, AddressMode w)
    : QRhiSampler(rhi, magFilter, minFilter, mipmapMode, u, v, w)
{
}

QWebGPUSampler::~QWebGPUSampler()
{
    destroy();
}

void QWebGPUSampler::destroy()
{
    if (!sampler)
        return;

    qCDebug(lcWebGPU, "QWebGPUSampler::destroy() sampler=%p", sampler);

    wgpuSamplerRelease(sampler);
    sampler = nullptr;

    QRHI_RES_RHI(QRhiWebGPU);
    if (rhiD)
        rhiD->unregisterResource(this);
}

static WGPUFilterMode toWgpuFilter(QRhiSampler::Filter f)
{
    switch (f) {
    case QRhiSampler::Nearest:
        return WGPUFilterMode_Nearest;
    case QRhiSampler::Linear:
        return WGPUFilterMode_Linear;
    default:
        return WGPUFilterMode_Nearest;
    }
}

static WGPUMipmapFilterMode toWgpuMipmapMode(QRhiSampler::Filter f)
{
    switch (f) {
    case QRhiSampler::None:
    case QRhiSampler::Nearest:
        return WGPUMipmapFilterMode_Nearest;
    case QRhiSampler::Linear:
        return WGPUMipmapFilterMode_Linear;
    default:
        return WGPUMipmapFilterMode_Nearest;
    }
}

static WGPUAddressMode toWgpuAddressMode(QRhiSampler::AddressMode m)
{
    switch (m) {
    case QRhiSampler::Repeat:
        return WGPUAddressMode_Repeat;
    case QRhiSampler::ClampToEdge:
        return WGPUAddressMode_ClampToEdge;
    case QRhiSampler::Mirror:
        return WGPUAddressMode_MirrorRepeat;
    default:
        return WGPUAddressMode_ClampToEdge;
    }
}

static WGPUCompareFunction toWgpuTextureCompareOp(QRhiSampler::CompareOp op)
{
    switch (op) {
    case QRhiSampler::Never:
        return WGPUCompareFunction_Never;
    case QRhiSampler::Less:
        return WGPUCompareFunction_Less;
    case QRhiSampler::Equal:
        return WGPUCompareFunction_Equal;
    case QRhiSampler::LessOrEqual:
        return WGPUCompareFunction_LessEqual;
    case QRhiSampler::Greater:
        return WGPUCompareFunction_Greater;
    case QRhiSampler::NotEqual:
        return WGPUCompareFunction_NotEqual;
    case QRhiSampler::GreaterOrEqual:
        return WGPUCompareFunction_GreaterEqual;
    case QRhiSampler::Always:
        return WGPUCompareFunction_Always;
    default:
        return WGPUCompareFunction_Never;
    }
}

static bool formatHasStencil(WGPUTextureFormat format)
{
    switch (format) {
    case WGPUTextureFormat_Stencil8:
    case WGPUTextureFormat_Depth24PlusStencil8:
    case WGPUTextureFormat_Depth32FloatStencil8:
        return true;
    default:
        return false;
    }
}

bool QWebGPUSampler::create()
{
    if (sampler)
        destroy();

    qCDebug(lcWebGPU, "QWebGPUSampler::create() magFilter=%d minFilter=%d mipmapMode=%d addressU=%d addressV=%d addressW=%d",
            int(m_magFilter), int(m_minFilter), int(m_mipmapMode), int(m_addressU), int(m_addressV), int(m_addressW));

    QRHI_RES_RHI(QRhiWebGPU);

    WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    desc.magFilter = toWgpuFilter(m_magFilter);
    desc.minFilter = toWgpuFilter(m_minFilter);
    desc.mipmapFilter = toWgpuMipmapMode(m_mipmapMode);
    desc.addressModeU = toWgpuAddressMode(m_addressU);
    desc.addressModeV = toWgpuAddressMode(m_addressV);
    desc.addressModeW = toWgpuAddressMode(m_addressW);
    desc.lodMinClamp = 0.0f;
    desc.lodMaxClamp = 1000.0f;
    desc.maxAnisotropy = 1;

    if (m_compareOp != Never)
        desc.compare = toWgpuTextureCompareOp(m_compareOp);

    sampler = wgpuDeviceCreateSampler(rhiD->device, &desc);
    if (!sampler) {
        qWarning("Failed to create WebGPU sampler");
        return false;
    }

    qCDebug(lcWebGPU, "QWebGPUSampler::create() created sampler=%p", sampler);

    generation += 1;
    rhiD->registerResource(this);
    return true;
}

// QWebGPURenderPassDescriptor

QWebGPURenderPassDescriptor::QWebGPURenderPassDescriptor(QRhiImplementation *rhi)
    : QRhiRenderPassDescriptor(rhi)
{
}

QWebGPURenderPassDescriptor::~QWebGPURenderPassDescriptor()
{
    destroy();
}

void QWebGPURenderPassDescriptor::destroy()
{
    QRHI_RES_RHI(QRhiWebGPU);
    if (rhiD)
        rhiD->unregisterResource(this);
}

bool QWebGPURenderPassDescriptor::isCompatible(const QRhiRenderPassDescriptor *other) const
{
    const QWebGPURenderPassDescriptor *o = QRHI_RES(const QWebGPURenderPassDescriptor, other);
    if (colorAttachmentCount != o->colorAttachmentCount)
        return false;
    if (hasDepthStencil != o->hasDepthStencil)
        return false;
    for (int i = 0; i < colorAttachmentCount; ++i) {
        if (colorFormat[i] != o->colorFormat[i])
            return false;
    }
    if (hasDepthStencil && dsFormat != o->dsFormat)
        return false;
    return true;
}

void QWebGPURenderPassDescriptor::updateSerializedFormat()
{
    serializedFormatData.clear();
    serializedFormatData.append(colorAttachmentCount);
    serializedFormatData.append(hasDepthStencil ? 1 : 0);
    for (int i = 0; i < colorAttachmentCount; ++i)
        serializedFormatData.append(colorFormat[i]);
    if (hasDepthStencil)
        serializedFormatData.append(dsFormat);
}

QRhiRenderPassDescriptor *QWebGPURenderPassDescriptor::newCompatibleRenderPassDescriptor() const
{
    QWebGPURenderPassDescriptor *rpD = new QWebGPURenderPassDescriptor(m_rhi);
    rpD->colorAttachmentCount = colorAttachmentCount;
    rpD->hasDepthStencil = hasDepthStencil;
    for (int i = 0; i < colorAttachmentCount; ++i)
        rpD->colorFormat[i] = colorFormat[i];
    rpD->dsFormat = dsFormat;
    rpD->updateSerializedFormat();

    QRHI_RES_RHI(QRhiWebGPU);
    rhiD->registerResource(rpD);
    return rpD;
}

QVector<quint32> QWebGPURenderPassDescriptor::serializedFormat() const
{
    return serializedFormatData;
}

// QWebGPUSwapChainRenderTarget

QWebGPUSwapChainRenderTarget::QWebGPUSwapChainRenderTarget(QRhiImplementation *rhi, QRhiSwapChain *swapchain)
    : QRhiSwapChainRenderTarget(rhi, swapchain),
      d(rhi)
{
}

QWebGPUSwapChainRenderTarget::~QWebGPUSwapChainRenderTarget()
{
    destroy();
}

void QWebGPUSwapChainRenderTarget::destroy()
{
}

QSize QWebGPUSwapChainRenderTarget::pixelSize() const
{
    return d.pixelSize;
}

float QWebGPUSwapChainRenderTarget::devicePixelRatio() const
{
    return d.dpr;
}

int QWebGPUSwapChainRenderTarget::sampleCount() const
{
    return QRHI_RES(const QWebGPUSwapChain, swapChain())->samples;
}

// QWebGPUTextureRenderTarget

QWebGPUTextureRenderTarget::QWebGPUTextureRenderTarget(QRhiImplementation *rhi,
                                                       const QRhiTextureRenderTargetDescription &desc,
                                                       Flags flags)
    : QRhiTextureRenderTarget(rhi, desc, flags),
      d(rhi)
{
    for (int i = 0; i < QWebGPURenderPassDescriptor::MAX_COLOR_ATTACHMENTS; ++i) {
        colorAttachmentViews[i] = 0;
        resolveAttachmentViews[i] = 0;
    }
}

QWebGPUTextureRenderTarget::~QWebGPUTextureRenderTarget()
{
    destroy();
}

void QWebGPUTextureRenderTarget::destroy()
{
    for (int i = 0; i < QWebGPURenderPassDescriptor::MAX_COLOR_ATTACHMENTS; ++i) {
        if (ownedColorAttachmentViews[i]) {
            wgpuTextureViewRelease(colorAttachmentViews[i]);
            colorAttachmentViews[i] = nullptr;
            ownedColorAttachmentViews[i] = false;
        }
        colorAttachmentDepthSlice[i] = WGPU_DEPTH_SLICE_UNDEFINED;
    }
    QRHI_RES_RHI(QRhiWebGPU);
    if (rhiD)
        rhiD->unregisterResource(this);
}

QSize QWebGPUTextureRenderTarget::pixelSize() const
{
    return d.pixelSize;
}

float QWebGPUTextureRenderTarget::devicePixelRatio() const
{
    return d.dpr;
}

int QWebGPUTextureRenderTarget::sampleCount() const
{
    // Derive sample count from the first color attachment (texture or renderbuffer).
    for (auto it = m_desc.cbeginColorAttachments(), end = m_desc.cendColorAttachments(); it != end; ++it) {
        if (it->renderBuffer())
            return QRHI_RES(QWebGPURenderBuffer, it->renderBuffer())->samples;
        if (it->texture())
            return QRHI_RES(QWebGPUTexture, it->texture())->samples;
    }
    return 1;
}

QRhiRenderPassDescriptor *QWebGPUTextureRenderTarget::newCompatibleRenderPassDescriptor()
{
    QWebGPURenderPassDescriptor *rpD = new QWebGPURenderPassDescriptor(m_rhi);
    rpD->colorAttachmentCount = 0;
    rpD->hasDepthStencil = m_desc.depthStencilBuffer() || m_desc.depthTexture();

    for (auto it = m_desc.cbeginColorAttachments(), itEnd = m_desc.cendColorAttachments(); it != itEnd; ++it) {
        WGPUTextureFormat fmt;
        if (it->texture()) {
            QWebGPUTexture *texD = QRHI_RES(QWebGPUTexture, it->texture());
            fmt = texD->wgpuFormat;
        } else if (it->renderBuffer()) {
            QWebGPURenderBuffer *rbD = QRHI_RES(QWebGPURenderBuffer, it->renderBuffer());
            fmt = QRhiWebGPU::toWgpuTextureFormat(rbD->backingFormat(), {});
        } else {
            continue;
        }
        rpD->colorFormat[rpD->colorAttachmentCount++] = fmt;
    }

    if (rpD->hasDepthStencil) {
        if (m_desc.depthTexture()) {
            QWebGPUTexture *depthTexD = QRHI_RES(QWebGPUTexture, m_desc.depthTexture());
            rpD->dsFormat = depthTexD->wgpuFormat;
        } else {
            rpD->dsFormat = WGPUTextureFormat_Depth24PlusStencil8;
        }
    }

    rpD->updateSerializedFormat();

    QRHI_RES_RHI(QRhiWebGPU);
    rhiD->registerResource(rpD);
    return rpD;
}

bool QWebGPUTextureRenderTarget::create()
{
    destroy();

    QRHI_RES_RHI(QRhiWebGPU);

    d.rp = QRHI_RES(QWebGPURenderPassDescriptor, m_renderPassDesc);

    const bool hasColorAttachments = m_desc.cbeginColorAttachments() != m_desc.cendColorAttachments();
    Q_ASSERT(hasColorAttachments || m_desc.depthTexture());

    if (hasColorAttachments) {
        const int count = int(m_desc.cendColorAttachments() - m_desc.cbeginColorAttachments());
        if (count > QWebGPURenderPassDescriptor::MAX_COLOR_ATTACHMENTS) {
            qWarning("Too many color attachments (%d, max is %d)", count, QWebGPURenderPassDescriptor::MAX_COLOR_ATTACHMENTS);
            return false;
        }

        int attIndex = 0;
        for (auto it = m_desc.cbeginColorAttachments(), itEnd = m_desc.cendColorAttachments(); it != itEnd; ++it, ++attIndex) {
            if (it->renderBuffer()) {
                // MSAA color renderbuffer — use its textureView directly
                QWebGPURenderBuffer *rbD = QRHI_RES(QWebGPURenderBuffer, it->renderBuffer());
                colorAttachmentViews[attIndex] = rbD->textureView;
                ownedColorAttachmentViews[attIndex] = false;
                if (it->resolveTexture()) {
                    QWebGPUTexture *resolveTexD = QRHI_RES(QWebGPUTexture, it->resolveTexture());
                    resolveAttachmentViews[attIndex] = resolveTexD->textureView;
                }
                if (attIndex == 0)
                    d.pixelSize = rbD->pixelSize();
            } else {
                QWebGPUTexture *texD = QRHI_RES(QWebGPUTexture, it->texture());
                const int layer = it->layer();
                const int level = it->level();
                const bool is3D = texD->flags().testFlag(QRhiTexture::ThreeDimensional);
                const bool isArrayOrCube = texD->flags().testFlag(QRhiTexture::CubeMap)
                                        || texD->arraySize() > 0;
                colorAttachmentDepthSlice[attIndex] = WGPU_DEPTH_SLICE_UNDEFINED;
                if (is3D) {
                    // For 3D textures, use a 3D view with mipLevelCount=1 and set
                    // depthSlice in the render pass attachment to select the z-layer.
                    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                    viewDesc.dimension = WGPUTextureViewDimension_3D;
                    viewDesc.baseMipLevel = uint32_t(level);
                    viewDesc.mipLevelCount = 1;
                    viewDesc.format = texD->wgpuFormat;
                    colorAttachmentViews[attIndex] = wgpuTextureCreateView(texD->texture, &viewDesc);
                    ownedColorAttachmentViews[attIndex] = true;
                    colorAttachmentDepthSlice[attIndex] = uint32_t(layer);
                } else if (layer != 0 || level != 0 || isArrayOrCube) {
                    // WebGPU requires render target attachments to have arrayLayerCount == 1.
                    // Create a per-layer/level view for this attachment.
                    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                    viewDesc.dimension = WGPUTextureViewDimension_2D;
                    viewDesc.baseArrayLayer = uint32_t(layer);
                    viewDesc.arrayLayerCount = 1;
                    viewDesc.baseMipLevel = uint32_t(level);
                    viewDesc.mipLevelCount = 1;
                    viewDesc.format = texD->wgpuFormat;
                    colorAttachmentViews[attIndex] = wgpuTextureCreateView(texD->texture, &viewDesc);
                    ownedColorAttachmentViews[attIndex] = true;
                } else {
                    colorAttachmentViews[attIndex] = texD->textureView;
                    ownedColorAttachmentViews[attIndex] = false;
                }
                if (it->resolveTexture()) {
                    QWebGPUTexture *resolveTexD = QRHI_RES(QWebGPUTexture, it->resolveTexture());
                    resolveAttachmentViews[attIndex] = resolveTexD->textureView;
                }
                if (attIndex == 0)
                    d.pixelSize = texD->pixelSize();
            }
        }
    }

    if (m_desc.depthTexture()) {
        QWebGPUTexture *depthTexD = QRHI_RES(QWebGPUTexture, m_desc.depthTexture());
        dsAttachmentView = depthTexD->textureView;
        if (d.pixelSize.isEmpty())
            d.pixelSize = depthTexD->pixelSize();
    } else if (m_desc.depthStencilBuffer()) {
        QWebGPURenderBuffer *rbD = QRHI_RES(QWebGPURenderBuffer, m_desc.depthStencilBuffer());
        dsAttachmentView = rbD->textureView;
    }

    rhiD->registerResource(this);
    return true;
}

// QWebGPUShaderResourceBindings

QWebGPUShaderResourceBindings::QWebGPUShaderResourceBindings(QRhiImplementation *rhi)
    : QRhiShaderResourceBindings(rhi)
{
}

QWebGPUShaderResourceBindings::~QWebGPUShaderResourceBindings()
{
    destroy();
}

void QWebGPUShaderResourceBindings::destroy()
{
    if (bindGroup) {
        wgpuBindGroupRelease(bindGroup);
        bindGroup = nullptr;
    }
    if (bindGroupLayout) {
        wgpuBindGroupLayoutRelease(bindGroupLayout);
        bindGroupLayout = nullptr;
    }

    QRHI_RES_RHI(QRhiWebGPU);
    if (rhiD)
        rhiD->unregisterResource(this);
}

bool QWebGPUShaderResourceBindings::create()
{
    if (bindGroupLayout)
        destroy();

    qCDebug(lcWebGPU, "QWebGPUShaderResourceBindings::create() bindingCount=%d", int(m_bindings.size()));

    sortedBindings.clear();
    std::copy(m_bindings.cbegin(), m_bindings.cend(), std::back_inserter(sortedBindings));
    std::sort(sortedBindings.begin(), sortedBindings.end(),
              [](const QRhiShaderResourceBinding &a, const QRhiShaderResourceBinding &b)
    {
        return QRhiImplementation::shaderResourceBindingData(a)->binding
             < QRhiImplementation::shaderResourceBindingData(b)->binding;
    });

    if (!sortedBindings.isEmpty())
        maxBinding = QRhiImplementation::shaderResourceBindingData(sortedBindings.last())->binding;

    // If we already have a binding map (from a previous pipeline creation), use it.
    // Otherwise, create layout+group with 1:1 mapping (will be recreated by pipeline if needed).
    if (!nativeBindingMap.isEmpty())
        return createWithBindingMap(nativeBindingMap);

    // Check if there are SampledTextures that require split bindings (texture + sampler).
    // If so, skip the fallback and defer to pipeline creation which has the proper binding map.
    for (const QRhiShaderResourceBinding &binding : sortedBindings) {
        const auto *b = QRhiImplementation::shaderResourceBindingData(binding);
        if (b->type == QRhiShaderResourceBinding::SampledTexture) {
            // Defer bind group creation to pipeline creation which has the binding map
            QRHI_RES_RHI(QRhiWebGPU);
            generation += 1;
            rhiD->registerResource(this);
            return true;
        }
    }

    // Fallback: 1:1 mapping (works when there are no SampledTextures)
    return createWithBindingMap(QShader::NativeResourceBindingMap());
}

static std::pair<int, int> mapBinding(int binding, const QShader::NativeResourceBindingMap &map)
{
    if (map.isEmpty())
        return { binding, binding };
    auto it = map.constFind(binding);
    if (it != map.cend())
        return *it;
    return { -1, -1 };
}

static WGPUShaderStage toWgpuShaderStage(QRhiShaderResourceBinding::StageFlags stages)
{
    WGPUShaderStage flags = WGPUShaderStage_None;
    if (stages.testFlag(QRhiShaderResourceBinding::VertexStage))
        flags |= WGPUShaderStage_Vertex;
    if (stages.testFlag(QRhiShaderResourceBinding::FragmentStage))
        flags |= WGPUShaderStage_Fragment;
    if (stages.testFlag(QRhiShaderResourceBinding::ComputeStage))
        flags |= WGPUShaderStage_Compute;
    return flags;
}

bool QWebGPUShaderResourceBindings::createWithBindingMap(const QShader::NativeResourceBindingMap &map)
{
    qCDebug(lcWebGPU, "createWithBindingMap enter this=%p oldBG=%p oldBGL=%p mapSize=%d sortedBindings=%d",
            this, (void *)bindGroup, (void *)bindGroupLayout, int(map.size()), int(sortedBindings.size()));

    // Release old objects
    if (bindGroup) {
        qCDebug(lcWebGPU, "  releasing oldBG=%p", (void *)bindGroup);
        wgpuBindGroupRelease(bindGroup);
        bindGroup = nullptr;
    }
    if (bindGroupLayout) {
        qCDebug(lcWebGPU, "  releasing oldBGL=%p", (void *)bindGroupLayout);
        wgpuBindGroupLayoutRelease(bindGroupLayout);
        bindGroupLayout = nullptr;
    }
    dynamicBindings.clear();

    QRHI_RES_RHI(QRhiWebGPU);
    nativeBindingMap = map;

    qCDebug(lcWebGPU, "  pre-scan begin");

    // Pre-scan: bail early if any binding references a null wgpu handle. Submitting
    // a null handle to wgpuDeviceCreateBindGroup trips a JS assertion in emdawnwebgpu's
    // getJsObject, which aborts the whole process. A soft abort here leaves bindGroup
    // null so setShaderResources skips SetBindGroup and the draw fails as a validation
    // error instead.
    for (const QRhiShaderResourceBinding &binding : sortedBindings) {
        const QRhiShaderResourceBinding::Data *b = QRhiImplementation::shaderResourceBindingData(binding);
        switch (b->type) {
        case QRhiShaderResourceBinding::UniformBuffer:
        {
            QWebGPUBuffer *bufD = QRHI_RES(QWebGPUBuffer, b->u.ubuf.buf);
            if (!bufD || !bufD->buffer) {
                qFatal("QRhiWebGPU: UniformBuffer binding %d has null buffer",
                         b->binding);
                return false;
            }
            break;
        }
        case QRhiShaderResourceBinding::SampledTexture:
        {
            QWebGPUTexture *texD = QRHI_RES(QWebGPUTexture, b->u.stex.texSamplers[0].tex);
            QWebGPUSampler *samplerD = QRHI_RES(QWebGPUSampler, b->u.stex.texSamplers[0].sampler);
            if (!texD || !texD->textureView) {
                qFatal("QRhiWebGPU: SampledTexture binding %d has null textureView "
                       "(texD=%p texture=%p size=%dx%d format=%d flags=0x%x)",
                       b->binding, texD,
                       texD ? (void *)texD->texture : nullptr,
                       texD ? texD->pixelSize().width() : 0,
                       texD ? texD->pixelSize().height() : 0,
                       texD ? int(texD->format()) : 0,
                       texD ? int(texD->flags()) : 0);
                return false;
            }
            if (!samplerD || !samplerD->sampler) {
                qFatal("QRhiWebGPU: SampledTexture binding %d has null sampler",
                         b->binding);
                return false;
            }
            break;
        }
        case QRhiShaderResourceBinding::Sampler:
        {
            QWebGPUSampler *samplerD = QRHI_RES(QWebGPUSampler, b->u.stex.texSamplers[0].sampler);
            if (!samplerD || !samplerD->sampler) {
                qFatal("QRhiWebGPU: Sampler binding %d has null sampler",
                         b->binding);
                return false;
            }
            break;
        }
        case QRhiShaderResourceBinding::BufferLoad:
        case QRhiShaderResourceBinding::BufferStore:
        case QRhiShaderResourceBinding::BufferLoadStore:
        {
            QWebGPUBuffer *bufD = QRHI_RES(QWebGPUBuffer, b->u.sbuf.buf);
            if (!bufD || !bufD->buffer) {
                qFatal("QRhiWebGPU: Storage buffer binding %d has null buffer",
                         b->binding);
                return false;
            }
            break;
        }
        case QRhiShaderResourceBinding::ImageLoad:
        {
            QWebGPUTexture *texD = QRHI_RES(QWebGPUTexture, b->u.simage.tex);
            if (!texD || !texD->textureView) {
                qFatal("QRhiWebGPU: ImageLoad binding %d has null textureView (texture=%p)",
                         b->binding, texD);
                return false;
            }
            break;
        }
        case QRhiShaderResourceBinding::ImageStore:
        case QRhiShaderResourceBinding::ImageLoadStore:
        {
            QWebGPUTexture *texD = QRHI_RES(QWebGPUTexture, b->u.simage.tex);
            if (!texD || !texD->texture) {
                qFatal("QRhiWebGPU: Storage image binding %d has null texture (texture=%p)",
                         b->binding, texD);
                return false;
            }
            break;
        }
        default:
            break;
        }
    }

    qCDebug(lcWebGPU, "  pre-scan ok, entering build loop");

    // Create bind group layout entries and bind group entries simultaneously
    QVarLengthArray<WGPUBindGroupLayoutEntry, 8> layoutEntries;
    QVarLengthArray<WGPUBindGroupEntry, 8> groupEntries;

    for (const QRhiShaderResourceBinding &binding : sortedBindings) {
        const QRhiShaderResourceBinding::Data *b = QRhiImplementation::shaderResourceBindingData(binding);
        const auto nativeBinding = mapBinding(b->binding, map);

        qCDebug(lcWebGPU, "build binding=%d type=%d stage=0x%x nativeBinding=(%d,%d)",
                b->binding, int(b->type), int(b->stage), nativeBinding.first, nativeBinding.second);

        switch (b->type) {
        case QRhiShaderResourceBinding::UniformBuffer:
        {
            if (nativeBinding.first < 0)
                break;
            WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            layoutEntry.binding = uint32_t(nativeBinding.first);
            layoutEntry.visibility = toWgpuShaderStage(b->stage);
            layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
            if (b->u.ubuf.hasDynamicOffset) {
                layoutEntry.buffer.hasDynamicOffset = true;
                dynamicBindings.append(b->binding); // track in layout order for setShaderResources
            }
            layoutEntries.append(layoutEntry);

            QWebGPUBuffer *bufD = QRHI_RES(QWebGPUBuffer, b->u.ubuf.buf);
            WGPUBindGroupEntry groupEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            groupEntry.binding = uint32_t(nativeBinding.first);
            groupEntry.buffer = bufD->buffer;
            groupEntry.offset = b->u.ubuf.offset; // base offset (0 for dynamic bindings)
            // WebGPU requires uniform buffer binding sizes to be a multiple of 16 bytes
            const quint32 rawSize = b->u.ubuf.maybeSize ? b->u.ubuf.maybeSize : bufD->size();
            groupEntry.size = (rawSize + 15u) & ~15u;
            groupEntries.append(groupEntry);
        }
            break;

        case QRhiShaderResourceBinding::SampledTexture:
        {
            // In WGSL, a combined sampler2D is split into a texture_2d and a sampler
            // at separate binding indices given by the NativeResourceBindingMap.
            const int textureBinding = nativeBinding.first;
            const int samplerBinding = nativeBinding.second;

            QWebGPUTexture *texD = QRHI_RES(QWebGPUTexture, b->u.stex.texSamplers[0].tex);
            QWebGPUSampler *samplerD = QRHI_RES(QWebGPUSampler, b->u.stex.texSamplers[0].sampler);
            qCDebug(lcWebGPU, "  SampledTexture texD=%p (textureView=%p) samplerD=%p (sampler=%p)",
                    texD, texD ? texD->textureView : nullptr,
                    samplerD, samplerD ? samplerD->sampler : nullptr);

            // Determine if this is a depth texture (requires special sample type)
            const bool isDepthTexture = texD && (
                texD->wgpuFormat == WGPUTextureFormat_Depth16Unorm ||
                texD->wgpuFormat == WGPUTextureFormat_Depth24Plus ||
                texD->wgpuFormat == WGPUTextureFormat_Depth24PlusStencil8 ||
                texD->wgpuFormat == WGPUTextureFormat_Depth32Float ||
                texD->wgpuFormat == WGPUTextureFormat_Depth32FloatStencil8);

            // 32-bit float formats are UnfilterableFloat unless the float32-filterable device feature
            // was requested (and is available). With the feature, linear filtering is allowed.
            const bool isUnfilterableFloat = !rhiD->caps.hasFloat32Filterable && texD && (
                texD->wgpuFormat == WGPUTextureFormat_RGBA32Float ||
                texD->wgpuFormat == WGPUTextureFormat_R32Float);

            // Determine if this is a comparison sampler
            const bool isComparisonSampler = samplerD && samplerD->textureCompareOp() != QRhiSampler::Never;

            if (textureBinding >= 0 && texD) {
                const bool isMultisampled = texD->samples > 1;
                WGPUTextureViewDimension viewDim = WGPUTextureViewDimension_2D;
                if (texD->flags().testFlag(QRhiTexture::CubeMap))
                    viewDim = WGPUTextureViewDimension_Cube;
                else if (texD->arraySize() > 0 && !isMultisampled)
                    viewDim = WGPUTextureViewDimension_2DArray;
                else if (texD->flags().testFlag(QRhiTexture::ThreeDimensional))
                    viewDim = WGPUTextureViewDimension_3D;
                WGPUBindGroupLayoutEntry texLayoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                texLayoutEntry.binding = uint32_t(textureBinding);
                texLayoutEntry.visibility = toWgpuShaderStage(b->stage);
                // Multisampled textures cannot use filterable-float; use UnfilterableFloat.
                texLayoutEntry.texture.sampleType = isDepthTexture
                    ? WGPUTextureSampleType_Depth
                    : (isMultisampled || isUnfilterableFloat ? WGPUTextureSampleType_UnfilterableFloat : WGPUTextureSampleType_Float);
                texLayoutEntry.texture.viewDimension = viewDim;
                texLayoutEntry.texture.multisampled = isMultisampled ? WGPU_TRUE : WGPU_FALSE;
                layoutEntries.append(texLayoutEntry);

                WGPUBindGroupEntry texGroupEntry = WGPU_BIND_GROUP_ENTRY_INIT;
                texGroupEntry.binding = uint32_t(textureBinding);
                texGroupEntry.textureView = texD->textureView;
                groupEntries.append(texGroupEntry);
            }

            if (samplerBinding >= 0 && samplerD) {
                if (!samplerD->sampler) {
                    qWarning("QRhiWebGPU: Sampler binding %d has null sampler",
                             samplerBinding);
                }
                WGPUBindGroupLayoutEntry samplerLayoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                samplerLayoutEntry.binding = uint32_t(samplerBinding);
                samplerLayoutEntry.visibility = toWgpuShaderStage(b->stage);
                samplerLayoutEntry.sampler.type = isComparisonSampler
                    ? WGPUSamplerBindingType_Comparison
                    : (isUnfilterableFloat ? WGPUSamplerBindingType_NonFiltering : WGPUSamplerBindingType_Filtering);
                layoutEntries.append(samplerLayoutEntry);

                WGPUBindGroupEntry samplerGroupEntry = WGPU_BIND_GROUP_ENTRY_INIT;
                samplerGroupEntry.binding = uint32_t(samplerBinding);
                samplerGroupEntry.sampler = samplerD->sampler;
                groupEntries.append(samplerGroupEntry);
            }
        }
            break;

        case QRhiShaderResourceBinding::Sampler:
        {
            if (nativeBinding.second < 0)
                break;
            QWebGPUSampler *samplerD = QRHI_RES(QWebGPUSampler, b->u.stex.texSamplers[0].sampler);
            WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            layoutEntry.binding = uint32_t(nativeBinding.second);
            layoutEntry.visibility = toWgpuShaderStage(b->stage);
            layoutEntry.sampler.type = WGPUSamplerBindingType_Filtering;
            layoutEntries.append(layoutEntry);

            WGPUBindGroupEntry groupEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            groupEntry.binding = uint32_t(nativeBinding.second);
            groupEntry.sampler = samplerD->sampler;
            groupEntries.append(groupEntry);
        }
            break;

        case QRhiShaderResourceBinding::BufferLoad:
        {
            if (nativeBinding.first < 0)
                break;
            WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            layoutEntry.binding = uint32_t(nativeBinding.first);
            layoutEntry.visibility = toWgpuShaderStage(b->stage);
            layoutEntry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            layoutEntries.append(layoutEntry);

            QWebGPUBuffer *bufD = QRHI_RES(QWebGPUBuffer, b->u.sbuf.buf);
            WGPUBindGroupEntry groupEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            groupEntry.binding = uint32_t(nativeBinding.first);
            groupEntry.buffer = bufD->buffer;
            groupEntry.offset = b->u.sbuf.offset;
            groupEntry.size = b->u.sbuf.maybeSize ? b->u.sbuf.maybeSize : bufD->size();
            groupEntries.append(groupEntry);
        }
            break;

        case QRhiShaderResourceBinding::BufferStore:
        case QRhiShaderResourceBinding::BufferLoadStore:
        {
            if (nativeBinding.first < 0)
                break;
            WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            layoutEntry.binding = uint32_t(nativeBinding.first);
            layoutEntry.visibility = toWgpuShaderStage(b->stage);
            layoutEntry.buffer.type = WGPUBufferBindingType_Storage;
            layoutEntries.append(layoutEntry);

            QWebGPUBuffer *bufD = QRHI_RES(QWebGPUBuffer, b->u.sbuf.buf);
            WGPUBindGroupEntry groupEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            groupEntry.binding = uint32_t(nativeBinding.first);
            groupEntry.buffer = bufD->buffer;
            groupEntry.offset = b->u.sbuf.offset;
            groupEntry.size = b->u.sbuf.maybeSize ? b->u.sbuf.maybeSize : bufD->size();
            groupEntries.append(groupEntry);
        }
            break;

        case QRhiShaderResourceBinding::ImageLoad:
        {
            // In WGSL, readonly image2D maps to texture_2d<f32> (regular texture, not storage)
            if (nativeBinding.first < 0)
                break;
            QWebGPUTexture *texD = QRHI_RES(QWebGPUTexture, b->u.simage.tex);

            WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            layoutEntry.binding = uint32_t(nativeBinding.first);
            layoutEntry.visibility = toWgpuShaderStage(b->stage);
            layoutEntry.texture.sampleType = WGPUTextureSampleType_Float;
            layoutEntry.texture.viewDimension = WGPUTextureViewDimension_2D;
            layoutEntries.append(layoutEntry);

            WGPUBindGroupEntry groupEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            groupEntry.binding = uint32_t(nativeBinding.first);
            groupEntry.textureView = texD->textureView;
            groupEntries.append(groupEntry);
        }
            break;

        case QRhiShaderResourceBinding::ImageStore:
        case QRhiShaderResourceBinding::ImageLoadStore:
        {
            // In WGSL, writeonly image2D maps to texture_storage_2d<fmt, write>
            // readwrite image2D maps to texture_storage_2d<fmt, read_write>
            // WebGPU requires storage texture views to have mipLevelCount == 1,
            // so always create a per-mip-level view.
            if (nativeBinding.first < 0)
                break;
            QWebGPUTexture *texD = QRHI_RES(QWebGPUTexture, b->u.simage.tex);
            const WGPUTextureFormat fmt = QRhiWebGPU::toWgpuTextureFormat(texD->format(), texD->flags());

            WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            layoutEntry.binding = uint32_t(nativeBinding.first);
            layoutEntry.visibility = toWgpuShaderStage(b->stage);
            layoutEntry.storageTexture.access = (b->type == QRhiShaderResourceBinding::ImageLoadStore)
                ? WGPUStorageTextureAccess_ReadWrite : WGPUStorageTextureAccess_WriteOnly;
            layoutEntry.storageTexture.format = fmt;
            layoutEntry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
            layoutEntries.append(layoutEntry);

            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.format = fmt;
            viewDesc.baseMipLevel = uint32_t(b->u.simage.level);
            viewDesc.mipLevelCount = 1;
            viewDesc.baseArrayLayer = 0;
            viewDesc.arrayLayerCount = 1;
            WGPUTextureView mipView = wgpuTextureCreateView(texD->texture, &viewDesc);
            // Note: mipView is a temporary; WebGPU copies descriptor data synchronously in CreateBindGroup.
            WGPUBindGroupEntry groupEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            groupEntry.binding = uint32_t(nativeBinding.first);
            groupEntry.textureView = mipView;
            groupEntries.append(groupEntry);
            wgpuTextureViewRelease(mipView);
        }
            break;

        default:
            break;
        }
    }

    WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.entryCount = layoutEntries.size();
    layoutDesc.entries = layoutEntries.data();
    bindGroupLayout = wgpuDeviceCreateBindGroupLayout(rhiD->device, &layoutDesc);

    // Dump each entry's handle immediately before wgpuDeviceCreateBindGroup so
    // we can identify which handle is stale when the JS shim asserts.
    for (qsizetype i = 0; i < groupEntries.size(); ++i) {
        const WGPUBindGroupEntry &ge = groupEntries[i];
        qCDebug(lcWebGPU, "  entry[%d] binding=%u buffer=%p textureView=%p sampler=%p",
                int(i), ge.binding, (void *)ge.buffer, (void *)ge.textureView, (void *)ge.sampler);
    }

    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = bindGroupLayout;
    bindGroupDesc.entryCount = groupEntries.size();
    bindGroupDesc.entries = groupEntries.data();
    bindGroup = wgpuDeviceCreateBindGroup(rhiD->device, &bindGroupDesc);

    qCDebug(lcWebGPU, "QWebGPUShaderResourceBindings::createWithBindingMap() created bindGroupLayout=%p bindGroup=%p entries=%d",
            bindGroupLayout, bindGroup, int(layoutEntries.size()));

    generation += 1;
    rhiD->registerResource(this);
    return true;
}

void QWebGPUShaderResourceBindings::updateResources(UpdateFlags flags)
{
    Q_UNUSED(flags);

    qCDebug(lcWebGPU, "QWebGPUShaderResourceBindings::updateResources() flags=0x%x bindGroup=%p bindGroupLayout=%p",
            int(flags), bindGroup, bindGroupLayout);

    if (bindGroup) {
        wgpuBindGroupRelease(bindGroup);
        bindGroup = nullptr;
    }
    if (bindGroupLayout) {
        wgpuBindGroupLayoutRelease(bindGroupLayout);
        bindGroupLayout = nullptr;
    }
    // Reuse the stored binding map
    createWithBindingMap(nativeBindingMap);
}

// QWebGPUGraphicsPipeline

QWebGPUGraphicsPipeline::QWebGPUGraphicsPipeline(QRhiImplementation *rhi)
    : QRhiGraphicsPipeline(rhi)
{
}

QWebGPUGraphicsPipeline::~QWebGPUGraphicsPipeline()
{
    destroy();
}

void QWebGPUGraphicsPipeline::destroy()
{
    if (pipeline) {
        qCDebug(lcWebGPU, "QWebGPUGraphicsPipeline::destroy() pipeline=%p pipelineLayout=%p", pipeline, pipelineLayout);
        wgpuRenderPipelineRelease(pipeline);
        pipeline = nullptr;
    }
    if (pipelineLayout) {
        wgpuPipelineLayoutRelease(pipelineLayout);
        pipelineLayout = nullptr;
    }

    QRHI_RES_RHI(QRhiWebGPU);
    if (rhiD)
        rhiD->unregisterResource(this);
}

bool QWebGPUGraphicsPipeline::create()
{
    if (pipeline)
        destroy();

    qCDebug(lcWebGPU, "QWebGPUGraphicsPipeline::create() topology=%d cullMode=%d frontFace=%d depthTest=%d depthWrite=%d stencilTest=%d sampleCount=%d",
            int(m_topology), int(m_cullMode), int(m_frontFace), m_depthTest, m_depthWrite, m_stencilTest, m_sampleCount);

    QRHI_RES_RHI(QRhiWebGPU);

    // Get shaders first (we need the binding map before creating the pipeline layout)
    WGPUShaderModule vsModule = nullptr;
    WGPUShaderModule fsModule = nullptr;
    QByteArray vsEntry;
    QByteArray fsEntry;
    QShader::NativeResourceBindingMap bindingMap;

    for (const QRhiShaderStage &stage : m_shaderStages) {
        const QShader &shader = stage.shader();
        QShaderKey wgslKey = { QShader::WgslShader, 100, stage.shaderVariant() };
        QShaderCode wgslCode = shader.shader(wgslKey);
        if (wgslCode.shader().isEmpty()) {
            qWarning("No WGSL shader code found");
            if (vsModule) { wgpuShaderModuleRelease(vsModule); vsModule = nullptr; }
            if (fsModule) { wgpuShaderModuleRelease(fsModule); fsModule = nullptr; }
            return false;
        }

        // Merge binding maps from all stages (vertex shader usually has them all)
        QShader::NativeResourceBindingMap stageMap = shader.nativeResourceBindingMap(wgslKey);
        for (auto it = stageMap.cbegin(), end = stageMap.cend(); it != end; ++it) {
            if (!bindingMap.contains(it.key()))
                bindingMap.insert(it.key(), it.value());
        }

        WGPUShaderSourceWGSL wgslSource = WGPU_SHADER_SOURCE_WGSL_INIT;
        wgslSource.code = {wgslCode.shader().constData(), size_t(wgslCode.shader().size())};

        WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        moduleDesc.nextInChain = &wgslSource.chain;

        WGPUShaderModule module = wgpuDeviceCreateShaderModule(rhiD->device, &moduleDesc);
        if (!module) {
            qWarning("Failed to create WebGPU shader module");
            if (vsModule) { wgpuShaderModuleRelease(vsModule); vsModule = nullptr; }
            if (fsModule) { wgpuShaderModuleRelease(fsModule); fsModule = nullptr; }
            return false;
        }

        if (stage.type() == QRhiShaderStage::Vertex) {
            vsModule = module;
            vsEntry = wgslCode.entryPoint().isEmpty() ? QByteArrayLiteral("main") : wgslCode.entryPoint();
        } else if (stage.type() == QRhiShaderStage::Fragment) {
            fsModule = module;
            fsEntry = wgslCode.entryPoint().isEmpty() ? QByteArrayLiteral("main") : wgslCode.entryPoint();
        }
    }

    // Rebuild SRB with the shader's native binding map (handles texture/sampler split)
    if (m_shaderResourceBindings) {
        QWebGPUShaderResourceBindings *srbD = QRHI_RES(QWebGPUShaderResourceBindings, m_shaderResourceBindings);
        srbD->createWithBindingMap(bindingMap);
    }

    // Pipeline layout
    QVarLengthArray<WGPUBindGroupLayout, 4> bindGroupLayouts;
    if (m_shaderResourceBindings) {
        QWebGPUShaderResourceBindings *srbD = QRHI_RES(QWebGPUShaderResourceBindings, m_shaderResourceBindings);
        if (srbD->bindGroupLayout)
            bindGroupLayouts.append(srbD->bindGroupLayout);
    }

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.bindGroupLayoutCount = bindGroupLayouts.size();
    pipelineLayoutDesc.bindGroupLayouts = bindGroupLayouts.data();
    pipelineLayout = wgpuDeviceCreatePipelineLayout(rhiD->device, &pipelineLayoutDesc);

    // Build vertex buffer layouts
    // We need to collect all attributes first, then set pointers, because
    // appending to QVarLengthArray can reallocate and invalidate pointers.
    QVarLengthArray<WGPUVertexBufferLayout, 4> vertexBufferLayouts;
    QVarLengthArray<WGPUVertexAttribute, 16> vertexAttributes;
    QMap<int, QVector<QRhiVertexInputAttribute>> attrsByBuffer;

    for (auto it = m_vertexInputLayout.cbeginAttributes(), end = m_vertexInputLayout.cendAttributes(); it != end; ++it)
        attrsByBuffer[it->binding()].append(*it);

    // First pass: collect all attributes into vertexAttributes array
    qCDebug(lcWebGPU, "QWebGPUGraphicsPipeline::create collecting vertex attributes");
    for (auto it = m_vertexInputLayout.cbeginBindings(), end = m_vertexInputLayout.cendBindings(); it != end; ++it) {
        const int bindingIndex = int(it - m_vertexInputLayout.cbeginBindings());
        const auto &attrs = attrsByBuffer[bindingIndex];
        for (const QRhiVertexInputAttribute &attr : attrs) {
            WGPUVertexAttribute wgpuAttr = WGPU_VERTEX_ATTRIBUTE_INIT;
            wgpuAttr.shaderLocation = uint32_t(attr.location());
            wgpuAttr.offset = attr.offset();
            wgpuAttr.format = QRhiWebGPU::toWgpuVertexFormat(attr.format());
            qCDebug(lcWebGPU, "  attr: location=%u offset=%llu format=%d", wgpuAttr.shaderLocation, wgpuAttr.offset, wgpuAttr.format);
            vertexAttributes.append(wgpuAttr);
        }
    }

    // Second pass: build buffer layouts with stable pointers
    int attrOffset = 0;
    for (auto it = m_vertexInputLayout.cbeginBindings(), end = m_vertexInputLayout.cendBindings(); it != end; ++it) {
        WGPUVertexBufferLayout bufLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        bufLayout.arrayStride = it->stride();
        bufLayout.stepMode = it->classification() == QRhiVertexInputBinding::PerVertex
                             ? WGPUVertexStepMode_Vertex : WGPUVertexStepMode_Instance;

        const int bindingIndex = int(it - m_vertexInputLayout.cbeginBindings());
        const auto &attrs = attrsByBuffer[bindingIndex];
        bufLayout.attributeCount = attrs.size();
        bufLayout.attributes = vertexAttributes.data() + attrOffset;
        attrOffset += attrs.size();

        qCDebug(lcWebGPU, "  buffer[%d]: stride=%llu numAttrs=%zu stepMode=%d", bindingIndex, bufLayout.arrayStride, bufLayout.attributeCount, bufLayout.stepMode);
        vertexBufferLayouts.append(bufLayout);
    }

    // Color targets
    QWebGPURenderPassDescriptor *rpD = QRHI_RES(QWebGPURenderPassDescriptor, m_renderPassDesc);
    QVarLengthArray<WGPUColorTargetState, 8> colorTargets;
    QVarLengthArray<WGPUBlendState, 8> blendStates;  // Keep blend states alive

    qCDebug(lcWebGPU, "QWebGPUGraphicsPipeline::create colorAttachmentCount=%d", rpD->colorAttachmentCount);
    for (int i = 0; i < rpD->colorAttachmentCount; ++i) {
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = rpD->colorFormat[i];
        qCDebug(lcWebGPU, "  colorTarget[%d] format=%d", i, target.format);
        target.writeMask = WGPUColorWriteMask_All;

        if (!m_targetBlends.isEmpty() && i < m_targetBlends.size()) {
            const QRhiGraphicsPipeline::TargetBlend &blend = m_targetBlends[i];
            target.writeMask = WGPUColorWriteMask_None;
            if (blend.colorWrite & QRhiGraphicsPipeline::R)
                target.writeMask |= WGPUColorWriteMask_Red;
            if (blend.colorWrite & QRhiGraphicsPipeline::G)
                target.writeMask |= WGPUColorWriteMask_Green;
            if (blend.colorWrite & QRhiGraphicsPipeline::B)
                target.writeMask |= WGPUColorWriteMask_Blue;
            if (blend.colorWrite & QRhiGraphicsPipeline::A)
                target.writeMask |= WGPUColorWriteMask_Alpha;
            if (blend.enable) {
                const auto isDualSrc = [](QRhiGraphicsPipeline::BlendFactor f) {
                    return f == QRhiGraphicsPipeline::Src1Color
                        || f == QRhiGraphicsPipeline::OneMinusSrc1Color
                        || f == QRhiGraphicsPipeline::Src1Alpha
                        || f == QRhiGraphicsPipeline::OneMinusSrc1Alpha;
                };
                if (!rhiD->caps.hasDualSourceBlending
                    && (isDualSrc(blend.srcColor) || isDualSrc(blend.dstColor)
                        || isDualSrc(blend.srcAlpha) || isDualSrc(blend.dstAlpha))) {
                    qWarning("QRhiWebGPU: Src1 blend factor requested but dual-source-blending feature is not supported by the adapter");
                }
                WGPUBlendState blendState = WGPU_BLEND_STATE_INIT;
                blendState.color.srcFactor = QRhiWebGPU::toWgpuBlendFactor(blend.srcColor);
                blendState.color.dstFactor = QRhiWebGPU::toWgpuBlendFactor(blend.dstColor);
                blendState.color.operation = QRhiWebGPU::toWgpuBlendOp(blend.opColor);
                blendState.alpha.srcFactor = QRhiWebGPU::toWgpuBlendFactor(blend.srcAlpha);
                blendState.alpha.dstFactor = QRhiWebGPU::toWgpuBlendFactor(blend.dstAlpha);
                blendState.alpha.operation = QRhiWebGPU::toWgpuBlendOp(blend.opAlpha);
                blendStates.append(blendState);
                target.blend = &blendStates.last();
            }
        }

        colorTargets.append(target);
    }

    // Build render pipeline descriptor
    WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = pipelineLayout;

    // Vertex state
    pipelineDesc.vertex.module = vsModule;
    pipelineDesc.vertex.entryPoint = {vsEntry.constData(), size_t(vsEntry.size())};
    pipelineDesc.vertex.bufferCount = vertexBufferLayouts.size();
    pipelineDesc.vertex.buffers = vertexBufferLayouts.data();

    // Primitive state
    pipelineDesc.primitive.topology = QRhiWebGPU::toWgpuPrimitiveTopology(m_topology);
    pipelineDesc.primitive.frontFace = QRhiWebGPU::toWgpuFrontFace(m_frontFace);
    pipelineDesc.primitive.cullMode = QRhiWebGPU::toWgpuCullMode(m_cullMode);
    // WebGPU requires stripIndexFormat to be set when the topology is a strip
    // and the pipeline is used with indexed draws. Qt's QRhiGraphicsPipeline
    // has no index format hint at pipeline creation; default to Uint32 which
    // matches what Qt Quick's scenegraph emits for its strip geometry.
    if (m_topology == QRhiGraphicsPipeline::TriangleStrip
        || m_topology == QRhiGraphicsPipeline::LineStrip) {
        pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Uint32;
    }
    if (m_depthClamp) {
        if (rhiD->caps.hasDepthClipControl) {
            pipelineDesc.primitive.unclippedDepth = true;
        } else {
            qWarning("QRhiWebGPU: DepthClamp requested but depth-clip-control feature is not supported by the adapter; ignoring");
        }
    }

    // Fragment state
    WGPUFragmentState fragmentState = WGPU_FRAGMENT_STATE_INIT;
    if (fsModule) {
        fragmentState.module = fsModule;
        fragmentState.entryPoint = {fsEntry.constData(), size_t(fsEntry.size())};
        fragmentState.targetCount = colorTargets.size();
        fragmentState.targets = colorTargets.data();
        pipelineDesc.fragment = &fragmentState;
    }

    // Depth stencil state
    WGPUDepthStencilState depthStencilState = WGPU_DEPTH_STENCIL_STATE_INIT;
    if (rpD->hasDepthStencil) {
        depthStencilState.format = rpD->dsFormat;
        depthStencilState.depthWriteEnabled = m_depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencilState.depthCompare = m_depthTest ? QRhiWebGPU::toWgpuCompareOp(m_depthOp) : WGPUCompareFunction_Always;

        if (m_stencilTest) {
            depthStencilState.stencilFront.compare = QRhiWebGPU::toWgpuCompareOp(m_stencilFront.compareOp);
            depthStencilState.stencilFront.failOp = QRhiWebGPU::toWgpuStencilOp(m_stencilFront.failOp);
            depthStencilState.stencilFront.depthFailOp = QRhiWebGPU::toWgpuStencilOp(m_stencilFront.depthFailOp);
            depthStencilState.stencilFront.passOp = QRhiWebGPU::toWgpuStencilOp(m_stencilFront.passOp);

            depthStencilState.stencilBack.compare = QRhiWebGPU::toWgpuCompareOp(m_stencilBack.compareOp);
            depthStencilState.stencilBack.failOp = QRhiWebGPU::toWgpuStencilOp(m_stencilBack.failOp);
            depthStencilState.stencilBack.depthFailOp = QRhiWebGPU::toWgpuStencilOp(m_stencilBack.depthFailOp);
            depthStencilState.stencilBack.passOp = QRhiWebGPU::toWgpuStencilOp(m_stencilBack.passOp);

            depthStencilState.stencilReadMask = m_stencilReadMask;
            depthStencilState.stencilWriteMask = m_stencilWriteMask;
        }
        pipelineDesc.depthStencil = &depthStencilState;
    }

    // Multisample state
    pipelineDesc.multisample.count = uint32_t(m_sampleCount);
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    pipeline = wgpuDeviceCreateRenderPipeline(rhiD->device, &pipelineDesc);

    // Release shader modules after pipeline creation
    if (vsModule)
        wgpuShaderModuleRelease(vsModule);
    if (fsModule)
        wgpuShaderModuleRelease(fsModule);

    if (!pipeline) {
        qWarning("Failed to create WebGPU render pipeline");
        return false;
    }

    qCDebug(lcWebGPU, "QWebGPUGraphicsPipeline::create() created pipeline=%p pipelineLayout=%p", pipeline, pipelineLayout);

    generation += 1;
    rhiD->registerResource(this);
    return true;
}

// QWebGPUComputePipeline

QWebGPUComputePipeline::QWebGPUComputePipeline(QRhiImplementation *rhi)
    : QRhiComputePipeline(rhi)
{
}

QWebGPUComputePipeline::~QWebGPUComputePipeline()
{
    destroy();
}

void QWebGPUComputePipeline::destroy()
{
    if (pipeline) {
        qCDebug(lcWebGPU, "QWebGPUComputePipeline::destroy() pipeline=%p pipelineLayout=%p", pipeline, pipelineLayout);
        wgpuComputePipelineRelease(pipeline);
        pipeline = nullptr;
    }
    if (pipelineLayout) {
        wgpuPipelineLayoutRelease(pipelineLayout);
        pipelineLayout = nullptr;
    }

    QRHI_RES_RHI(QRhiWebGPU);
    if (rhiD)
        rhiD->unregisterResource(this);
}

bool QWebGPUComputePipeline::create()
{
    if (pipeline)
        destroy();

    qCDebug(lcWebGPU, "QWebGPUComputePipeline::create()");

    QRHI_RES_RHI(QRhiWebGPU);

    QShaderKey wgslKey = { QShader::WgslShader, 100, m_shaderStage.shaderVariant() };
    QShaderCode wgslCode = m_shaderStage.shader().shader(wgslKey);

    // Rebuild SRB with the compute shader's native binding map
    if (m_shaderResourceBindings) {
        QShader::NativeResourceBindingMap bindingMap = m_shaderStage.shader().nativeResourceBindingMap(wgslKey);
        QWebGPUShaderResourceBindings *srbD = QRHI_RES(QWebGPUShaderResourceBindings, m_shaderResourceBindings);
        srbD->createWithBindingMap(bindingMap);
    }

    QVarLengthArray<WGPUBindGroupLayout, 4> bindGroupLayouts;
    if (m_shaderResourceBindings) {
        QWebGPUShaderResourceBindings *srbD = QRHI_RES(QWebGPUShaderResourceBindings, m_shaderResourceBindings);
        if (srbD->bindGroupLayout)
            bindGroupLayouts.append(srbD->bindGroupLayout);
    }

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.bindGroupLayoutCount = bindGroupLayouts.size();
    pipelineLayoutDesc.bindGroupLayouts = bindGroupLayouts.data();
    pipelineLayout = wgpuDeviceCreatePipelineLayout(rhiD->device, &pipelineLayoutDesc);
    if (wgslCode.shader().isEmpty()) {
        qWarning("No WGSL compute shader code found");
        wgpuPipelineLayoutRelease(pipelineLayout);
        pipelineLayout = nullptr;
        return false;
    }

    WGPUShaderSourceWGSL wgslSource = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgslSource.code = {wgslCode.shader().constData(), size_t(wgslCode.shader().size())};

    WGPUShaderModuleDescriptor moduleDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    moduleDesc.nextInChain = &wgslSource.chain;

    WGPUShaderModule csModule = wgpuDeviceCreateShaderModule(rhiD->device, &moduleDesc);
    if (!csModule) {
        qWarning("Failed to create WebGPU compute shader module");
        wgpuPipelineLayoutRelease(pipelineLayout);
        pipelineLayout = nullptr;
        return false;
    }

    QByteArray entry = wgslCode.entryPoint().isEmpty() ? QByteArrayLiteral("main") : wgslCode.entryPoint();

    WGPUComputePipelineDescriptor computeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    computeDesc.layout = pipelineLayout;
    computeDesc.compute.module = csModule;
    computeDesc.compute.entryPoint = {entry.constData(), size_t(entry.size())};
    pipeline = wgpuDeviceCreateComputePipeline(rhiD->device, &computeDesc);

    wgpuShaderModuleRelease(csModule);

    if (!pipeline) {
        qWarning("Failed to create WebGPU compute pipeline");
        return false;
    }

    qCDebug(lcWebGPU, "QWebGPUComputePipeline::create() created pipeline=%p pipelineLayout=%p", pipeline, pipelineLayout);

    generation += 1;
    rhiD->registerResource(this);
    return true;
}

// QWebGPUCommandBuffer

QWebGPUCommandBuffer::QWebGPUCommandBuffer(QRhiImplementation *rhi)
    : QRhiCommandBuffer(rhi)
{
}

QWebGPUCommandBuffer::~QWebGPUCommandBuffer()
{
    destroy();
}

void QWebGPUCommandBuffer::destroy()
{
}

void QWebGPUCommandBuffer::resetState()
{
    recordingPass = NoPass;
    currentTarget = nullptr;
    resetPerPassCachedState();
}

void QWebGPUCommandBuffer::resetPerPassState()
{
    recordingPass = NoPass;
    currentTarget = nullptr;
    resetPerPassCachedState();
}

void QWebGPUCommandBuffer::resetPerPassCachedState()
{
    currentGraphicsPipeline = nullptr;
    currentComputePipeline = nullptr;
    currentPipelineGeneration = 0;
    currentGraphicsSrb = nullptr;
    currentComputeSrb = nullptr;
    currentSrbGeneration = 0;
}

// QWebGPUSwapChain

QWebGPUSwapChain::QWebGPUSwapChain(QRhiImplementation *rhi)
    : QRhiSwapChain(rhi),
      rtWrapper(rhi, this),
      cbWrapper(rhi)
{
}

QWebGPUSwapChain::~QWebGPUSwapChain()
{
    destroy();
}

void QWebGPUSwapChain::destroy()
{
    qCDebug(lcWebGPU, "QWebGPUSwapChain::destroy() surface=%p pixelSize=%dx%d", surface, pixelSize.width(), pixelSize.height());

    if (msaaTextureView) {
        wgpuTextureViewRelease(msaaTextureView);
        msaaTextureView = nullptr;
    }
    if (msaaTexture) {
        wgpuTextureRelease(msaaTexture);
        msaaTexture = nullptr;
    }

    if (currentTextureView) {
        wgpuTextureViewRelease(currentTextureView);
        currentTextureView = nullptr;
    }
    currentTexture = nullptr;

    if (surface) {
        wgpuSurfaceRelease(surface);
        surface = nullptr;
    }

    QRHI_RES_RHI(QRhiWebGPU);
    if (rhiD) {
        rhiD->swapchains.remove(this);
        rhiD->unregisterResource(this);
    }
}

QRhiCommandBuffer *QWebGPUSwapChain::currentFrameCommandBuffer()
{
    return &cbWrapper;
}

QRhiRenderTarget *QWebGPUSwapChain::currentFrameRenderTarget()
{
    return &rtWrapper;
}

QSize QWebGPUSwapChain::surfacePixelSize()
{
    if (m_window)
        return m_window->size() * m_window->devicePixelRatio();
    return QSize();
}

bool QWebGPUSwapChain::isFormatSupported(Format f)
{
    return f == SDR;
}

QRhiRenderPassDescriptor *QWebGPUSwapChain::newCompatibleRenderPassDescriptor()
{
    QWebGPURenderPassDescriptor *rpD = new QWebGPURenderPassDescriptor(m_rhi);
    rpD->colorAttachmentCount = 1;
    rpD->colorFormat[0] = surfaceFormat;
    rpD->hasDepthStencil = m_depthStencil != nullptr;
    if (rpD->hasDepthStencil)
        rpD->dsFormat = WGPUTextureFormat_Depth24PlusStencil8;
    rpD->updateSerializedFormat();

    QRHI_RES_RHI(QRhiWebGPU);
    rhiD->registerResource(rpD);
    return rpD;
}

bool QWebGPUSwapChain::createOrResize()
{
    QRHI_RES_RHI(QRhiWebGPU);

    const QSize newPixelSize = surfacePixelSize();

    qCDebug(lcWebGPU, "QWebGPUSwapChain::createOrResize() newPixelSize=%dx%d currentPixelSize=%dx%d surface=%p",
            newPixelSize.width(), newPixelSize.height(), pixelSize.width(), pixelSize.height(), surface);

    // Early out if already configured and the size hasn't changed
    if (surface && pixelSize == newPixelSize && newPixelSize.isValid()) {
        qCDebug(lcWebGPU, "  no resize needed, early return");
        return true;
    }

    if (currentTextureView) {
        wgpuTextureViewRelease(currentTextureView);
        currentTextureView = nullptr;
    }

    window = m_window;
    pixelSize = newPixelSize;
    m_currentPixelSize = pixelSize;

    if (pixelSize.isEmpty())
        return false;

    // Get or create surface
    if (!surface) {
#ifdef __EMSCRIPTEN__
        // Get the canvas selector for this window. Qt for WebAssembly creates
        // canvases dynamically and registers them in Emscripten's specialHTMLTargets
        // using the "!qtwindow<WId>" selector format.
        std::string selector = "!qtwindow" + std::to_string(window->winId());
        qCDebug(lcWebGPU, "QRhiWebGPU: surface selector='%s' winId=%lu", selector.c_str(), (unsigned long)window->winId());

        WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasSource =
            WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
        canvasSource.selector = {selector.c_str(), selector.size()};

        WGPUSurfaceDescriptor surfaceDesc = WGPU_SURFACE_DESCRIPTOR_INIT;
        surfaceDesc.nextInChain = &canvasSource.chain;
        surface = wgpuInstanceCreateSurface(rhiD->instance, &surfaceDesc);
#elif defined(Q_OS_MACOS)
        // macOS: Get CAMetalLayer via QCocoaWindow native interface (same approach as Metal RHI)
        CALayer *layer = nullptr;
        if (auto *cocoaWindow = window->nativeInterface<QNativeInterface::Private::QCocoaWindow>())
            layer = cocoaWindow->contentLayer();

        if (!layer) {
            qWarning("QRhiWebGPU: Failed to get content layer from window (ensure window uses MetalSurface type)");
            return false;
        }

        qCDebug(lcWebGPU, "QRhiWebGPU: Got CAMetalLayer %p for window %p", layer, window);

        WGPUSurfaceSourceMetalLayer metalSource = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
        metalSource.layer = layer;

        WGPUSurfaceDescriptor surfaceDesc = WGPU_SURFACE_DESCRIPTOR_INIT;
        surfaceDesc.nextInChain = &metalSource.chain;
        surface = wgpuInstanceCreateSurface(rhiD->instance, &surfaceDesc);
#elif defined(Q_OS_WIN)
        // Windows: Use HWND
        HWND hwnd = reinterpret_cast<HWND>(window->winId());
        if (!hwnd) {
            qWarning("QRhiWebGPU: Failed to get HWND from window");
            return false;
        }

        WGPUSurfaceSourceWindowsHWND hwndSource = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
        hwndSource.hinstance = GetModuleHandle(nullptr);
        hwndSource.hwnd = hwnd;

        WGPUSurfaceDescriptor surfaceDesc = WGPU_SURFACE_DESCRIPTOR_INIT;
        surfaceDesc.nextInChain = &hwndSource.chain;
        surface = wgpuInstanceCreateSurface(rhiD->instance, &surfaceDesc);
#else
        // Other platforms not yet implemented
        qWarning("Native WebGPU surface creation not yet implemented for this platform");
#endif
        if (!surface) {
            qWarning("Failed to create WebGPU surface");
            return false;
        }
    }

    // Get surface capabilities and preferred format
    WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
    wgpuSurfaceGetCapabilities(surface, rhiD->adapter, &caps);
    surfaceFormat = caps.formats[0];  // Use first (preferred) format
    qCDebug(lcWebGPU, "QRhiWebGPU::createOrResize surfaceFormat=%d", surfaceFormat);

    // Configure surface
    WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
    config.device = rhiD->device;
    config.format = surfaceFormat;
    config.width = pixelSize.width();
    config.height = pixelSize.height();
    config.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(surface, &config);

    qCDebug(lcWebGPU, "QWebGPUSwapChain::createOrResize() configured surface=%p format=%d size=%dx%d",
            surface, surfaceFormat, pixelSize.width(), pixelSize.height());

    // Create or resize the depth/stencil render buffer if one is attached
    if (m_depthStencil) {
        QWebGPURenderBuffer *rbD = QRHI_RES(QWebGPURenderBuffer, m_depthStencil);
        // Release old resources
        if (rbD->texture) {
            if (rbD->textureView) {
                wgpuTextureViewRelease(rbD->textureView);
                rbD->textureView = nullptr;
            }
            wgpuTextureRelease(rbD->texture);
            rbD->texture = nullptr;
        }
        // Create with swapchain's pixel size
        WGPUTextureDescriptor dsDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        dsDesc.size.width = uint32_t(pixelSize.width());
        dsDesc.size.height = uint32_t(pixelSize.height());
        dsDesc.size.depthOrArrayLayers = 1;
        dsDesc.mipLevelCount = 1;
        dsDesc.sampleCount = uint32_t(rhiD->effectiveSampleCount(m_sampleCount));
        dsDesc.dimension = WGPUTextureDimension_2D;
        dsDesc.usage = WGPUTextureUsage_RenderAttachment;
        dsDesc.format = WGPUTextureFormat_Depth24PlusStencil8;
        rbD->texture = wgpuDeviceCreateTexture(rhiD->device, &dsDesc);
        if (rbD->texture)
            rbD->textureView = wgpuTextureCreateView(rbD->texture, nullptr);
        rbD->generation += 1;
    }

    rtWrapper.setRenderPassDescriptor(m_renderPassDesc);
    rtWrapper.d.pixelSize = pixelSize;
    rtWrapper.d.dpr = float(window->devicePixelRatio());

    samples = rhiD->effectiveSampleCount(m_sampleCount);

    // Create MSAA color texture if samples > 1
    // WebGPU swapchain surfaces are always single-sampled, so we need a separate
    // MSAA texture to render into, which then resolves to the swapchain texture
    if (msaaTextureView) {
        wgpuTextureViewRelease(msaaTextureView);
        msaaTextureView = nullptr;
    }
    if (msaaTexture) {
        wgpuTextureRelease(msaaTexture);
        msaaTexture = nullptr;
    }
    if (samples > 1) {
        WGPUTextureDescriptor msaaDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        msaaDesc.size.width = uint32_t(pixelSize.width());
        msaaDesc.size.height = uint32_t(pixelSize.height());
        msaaDesc.size.depthOrArrayLayers = 1;
        msaaDesc.mipLevelCount = 1;
        msaaDesc.sampleCount = uint32_t(samples);
        msaaDesc.dimension = WGPUTextureDimension_2D;
        msaaDesc.usage = WGPUTextureUsage_RenderAttachment;
        msaaDesc.format = surfaceFormat;
        msaaTexture = wgpuDeviceCreateTexture(rhiD->device, &msaaDesc);
        if (msaaTexture)
            msaaTextureView = wgpuTextureCreateView(msaaTexture, nullptr);
        qCDebug(lcWebGPU, "  created MSAA color texture with sampleCount=%d", samples);
    }

    rhiD->swapchains.insert(this);
    rhiD->registerResource(this);

    return true;
}

// QRhiWebGPU implementation

// Async callbacks for device initialization
static void onDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device,
                                  WGPUStringView message, void *userdata1, void *userdata2)
{
    Q_UNUSED(userdata2);
    QRhiWebGPU *rhi = static_cast<QRhiWebGPU *>(userdata1);

    if (status == WGPURequestDeviceStatus_Success) {
        rhi->device = device;
        rhi->queue = wgpuDeviceGetQueue(device);
        qCDebug(lcWebGPU, "QRhiWebGPU: Device and queue obtained");
    } else {
        qWarning("QRhiWebGPU: Device request failed: %.*s",
                 int(message.length), message.data);
    }
}

static void onAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                                   WGPUStringView message, void *userdata1, void *userdata2)
{
    Q_UNUSED(userdata2);
    QRhiWebGPU *rhi = static_cast<QRhiWebGPU *>(userdata1);

    if (status == WGPURequestAdapterStatus_Success) {
        rhi->adapter = adapter;
        qCDebug(lcWebGPU, "QRhiWebGPU: Adapter obtained");
    } else {
        qWarning("QRhiWebGPU: Adapter request failed: %.*s",
                 int(message.length), message.data);
    }
}

static void onUncapturedError(WGPUDevice const *dev, WGPUErrorType errorType,
                                                WGPUStringView message, void *userdata1, void *userdata2)
{
    Q_UNUSED(dev);
    Q_UNUSED(userdata1);
    Q_UNUSED(userdata2);
    qWarning("QRhiWebGPU VALIDATION ERROR (type %d): %.*s",
             errorType, int(message.length), message.data);
}

QRhiWebGPU::QRhiWebGPU(QRhiWebGPUInitParams *params, QRhiWebGPUNativeHandles *importDevice)
    : offscreenCommandBuffer(this)
{
    Q_UNUSED(params);

    if (importDevice) {
        importedDevice = true;
        device = static_cast<WGPUDevice>(importDevice->device);
        queue = static_cast<WGPUQueue>(importDevice->queue);
    }
}

QRhiWebGPU::~QRhiWebGPU()
{
    destroy();
}

bool QRhiWebGPU::create(QRhi::Flags flags)
{
    qCDebug(lcWebGPU, "QRhiWebGPU::create() flags=0x%x importedDevice=%d", int(flags), importedDevice);

    rhiFlags = flags;

    if (importedDevice) {
        qCDebug(lcWebGPU, "using imported device=%p queue=%p", device, queue);
        if (!queue)
            queue = wgpuDeviceGetQueue(device);
        return true;
    }

    // Create WebGPU instance with TimedWaitAny enabled (needed for
    // synchronous adapter/device requests via wgpuInstanceWaitAny)
    WGPUInstanceDescriptor instanceDesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    WGPUInstanceFeatureName timedWaitFeature = WGPUInstanceFeatureName_TimedWaitAny;
    instanceDesc.requiredFeatureCount = 1;
    instanceDesc.requiredFeatures = &timedWaitFeature;
    instance = wgpuCreateInstance(&instanceDesc);

    qCDebug(lcWebGPU, "created instance=%p", instance);
    if (!instance) {
        qWarning("QRhiWebGPU: wgpuCreateInstance returned null — bailing out of create()");
        return false;
    }

    // Request adapter and wait (suspends via JSPI on WASM)
    WGPURequestAdapterOptions adapterOpts = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;

    WGPURequestAdapterCallbackInfo adapterCallback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapterCallback.mode = WGPUCallbackMode_WaitAnyOnly;
    adapterCallback.callback = onAdapterRequestEnded;
    adapterCallback.userdata1 = this;

    WGPUFuture adapterFuture = wgpuInstanceRequestAdapter(instance, &adapterOpts, adapterCallback);
    WGPUFutureWaitInfo adapterWait = WGPU_FUTURE_WAIT_INFO_INIT;
    adapterWait.future = adapterFuture;
    qCDebug(lcWebGPU, "wgpuInstanceWaitAny for adapter start");
    WGPUWaitStatus adapterStatus = wgpuInstanceWaitAny(instance, 1, &adapterWait, UINT64_MAX);
    qCDebug(lcWebGPU, "wgpuInstanceWaitAny returned status=%d adapter=%p", adapterStatus, adapter);
    if (adapterStatus != WGPUWaitStatus_Success || !adapter) {
        qWarning("QRhiWebGPU: Failed to obtain adapter (wait status %d)", adapterStatus);
        return false;
    }

    // Query adapter limits and request them for the device, so we get the
    // best available limits (e.g. maxColorAttachmentBytesPerSample) rather
    // than the conservative WebGPU defaults.
    WGPULimits adapterLimits = WGPU_LIMITS_INIT;
    wgpuAdapterGetLimits(adapter, &adapterLimits);
    caps.limits = adapterLimits;

    // Request device and wait (suspends via JSPI on WASM)
    WGPUDeviceDescriptor deviceDesc = WGPU_DEVICE_DESCRIPTOR_INIT;
    deviceDesc.requiredLimits = &adapterLimits;
    deviceDesc.uncapturedErrorCallbackInfo.callback = onUncapturedError;

    // Query optional adapter features and request all that are available.
    // Each feature is only requested if the adapter supports it; requesting
    // an unsupported feature would cause device creation to fail.
    QVarLengthArray<WGPUFeatureName, 16> requiredFeatures;

    auto checkAndRequest = [&](WGPUFeatureName feature, bool &capFlag) {
        capFlag = wgpuAdapterHasFeature(adapter, feature);
        if (capFlag)
            requiredFeatures.append(feature);
    };

    // Float32Filterable: linear sampling of RGBA32F/R32F/RG32F textures.
    // Without this, those formats require a non-filtering sampler.
    checkAndRequest(WGPUFeatureName_Float32Filterable,      caps.hasFloat32Filterable);
    // Texture compression families — availability is platform/GPU-dependent.
    // Desktop browsers/GPUs typically support BC; mobile typically supports ETC2 and/or ASTC.
    checkAndRequest(WGPUFeatureName_TextureCompressionBC,   caps.hasTextureCompressionBC);
    checkAndRequest(WGPUFeatureName_TextureCompressionETC2, caps.hasTextureCompressionETC2);
    checkAndRequest(WGPUFeatureName_TextureCompressionASTC, caps.hasTextureCompressionASTC);
    // TimestampQuery: GPU timing — useful for profiling.
    checkAndRequest(WGPUFeatureName_TimestampQuery,         caps.hasTimestampQuery);
    // Depth32FloatStencil8: combined D32F+S8 format (QRhiTexture::D32FS8).
    checkAndRequest(WGPUFeatureName_Depth32FloatStencil8,   caps.hasDepth32FloatStencil8);
    // IndirectFirstInstance: firstInstance != 0 in drawIndirect/drawIndexedIndirect.
    checkAndRequest(WGPUFeatureName_IndirectFirstInstance,  caps.hasIndirectFirstInstance);
    // DualSourceBlending: Src1Color/OneMinusSrc1Color blend factors.
    checkAndRequest(WGPUFeatureName_DualSourceBlending,     caps.hasDualSourceBlending);
    // DepthClipControl: allow disabling depth clipping (unclippedDepth).
    checkAndRequest(WGPUFeatureName_DepthClipControl,       caps.hasDepthClipControl);
    // ShaderF16: half-precision (f16) in WGSL.
    checkAndRequest(WGPUFeatureName_ShaderF16,              caps.hasShaderF16);

    if (!requiredFeatures.isEmpty()) {
        deviceDesc.requiredFeatureCount = requiredFeatures.size();
        deviceDesc.requiredFeatures = requiredFeatures.data();
    }
    qCDebug(lcWebGPU, "QRhiWebGPU features: float32Filterable=%d compressionBC=%d compressionETC2=%d compressionASTC=%d timestamps=%d "
                      "d32fs8=%d indirectFirstInstance=%d dualSourceBlending=%d depthClipControl=%d shaderF16=%d",
            caps.hasFloat32Filterable, caps.hasTextureCompressionBC,
            caps.hasTextureCompressionETC2, caps.hasTextureCompressionASTC,
            caps.hasTimestampQuery, caps.hasDepth32FloatStencil8,
            caps.hasIndirectFirstInstance, caps.hasDualSourceBlending,
            caps.hasDepthClipControl, caps.hasShaderF16);

    WGPURequestDeviceCallbackInfo deviceCallback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    deviceCallback.mode = WGPUCallbackMode_WaitAnyOnly;
    deviceCallback.callback = onDeviceRequestEnded;
    deviceCallback.userdata1 = this;

    WGPUFuture deviceFuture = wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCallback);
    WGPUFutureWaitInfo deviceWait = WGPU_FUTURE_WAIT_INFO_INIT;
    deviceWait.future = deviceFuture;
    qCDebug(lcWebGPU, "wgpuInstanceWaitAny for device start");
    WGPUWaitStatus deviceStatus = wgpuInstanceWaitAny(instance, 1, &deviceWait, UINT64_MAX);
    qCDebug(lcWebGPU, "wgpuInstanceWaitAny returned status=%d device=%p", deviceStatus, device);
    if (deviceStatus != WGPUWaitStatus_Success || !device) {
        qWarning("QRhiWebGPU: Failed to obtain device (wait status %d)", deviceStatus);
        return false;
    }

    qCDebug(lcWebGPU, "QRhiWebGPU::create() complete: instance=%p adapter=%p device=%p queue=%p",
            instance, adapter, device, queue);

    return true;
}

void QRhiWebGPU::destroy()
{
    qCDebug(lcWebGPU, "QRhiWebGPU::destroy() importedDevice=%d device=%p adapter=%p instance=%p",
            importedDevice, device, adapter, instance);

    executeDeferredReleases(true);
    finishActiveReadbacks(true);

    if (!importedDevice) {
        if (device) {
            qCDebug(lcWebGPU, "  releasing device");
            wgpuDeviceRelease(device);
            device = nullptr;
        }
        if (adapter) {
            qCDebug(lcWebGPU, "  releasing adapter");
            wgpuAdapterRelease(adapter);
            adapter = nullptr;
        }
        if (instance) {
            qCDebug(lcWebGPU, "  releasing instance");
            wgpuInstanceRelease(instance);
            instance = nullptr;
        }
        queue = nullptr;
    }
}

QList<int> QRhiWebGPU::supportedSampleCounts() const
{
    return caps.supportedSampleCounts;
}

QList<QSize> QRhiWebGPU::supportedShadingRates(int sampleCount) const
{
    Q_UNUSED(sampleCount);
    return {};
}

int QRhiWebGPU::ubufAlignment() const
{
    return 256;
}

bool QRhiWebGPU::isYUpInFramebuffer() const
{
    return false;
}

bool QRhiWebGPU::isYUpInNDC() const
{
    return true;
}

bool QRhiWebGPU::isClipDepthZeroToOne() const
{
    return true;
}

QMatrix4x4 QRhiWebGPU::clipSpaceCorrMatrix() const
{
    return QMatrix4x4();
}

bool QRhiWebGPU::isTextureFormatSupported(QRhiTexture::Format format, QRhiTexture::Flags flags) const
{
    Q_UNUSED(flags);

    switch (format) {
    case QRhiTexture::RGBA8:
    case QRhiTexture::BGRA8:
    case QRhiTexture::R8:
    case QRhiTexture::RED_OR_ALPHA8:
    case QRhiTexture::RG8:
    case QRhiTexture::R16:
    case QRhiTexture::RG16:
    case QRhiTexture::RGBA16F:
    case QRhiTexture::RGBA32F:
    case QRhiTexture::D16:
    case QRhiTexture::D24:
    case QRhiTexture::D24S8:
    case QRhiTexture::D32F:
        return true;
    case QRhiTexture::D32FS8:
        return caps.hasDepth32FloatStencil8;
    case QRhiTexture::BC1:
    case QRhiTexture::BC2:
    case QRhiTexture::BC3:
    case QRhiTexture::BC4:
    case QRhiTexture::BC5:
    case QRhiTexture::BC6H:
    case QRhiTexture::BC7:
        return caps.hasTextureCompressionBC;
    case QRhiTexture::ETC2_RGB8:
    case QRhiTexture::ETC2_RGB8A1:
    case QRhiTexture::ETC2_RGBA8:
        return caps.hasTextureCompressionETC2;
    case QRhiTexture::ASTC_4x4:
    case QRhiTexture::ASTC_5x4:
    case QRhiTexture::ASTC_5x5:
    case QRhiTexture::ASTC_6x5:
    case QRhiTexture::ASTC_6x6:
    case QRhiTexture::ASTC_8x5:
    case QRhiTexture::ASTC_8x6:
    case QRhiTexture::ASTC_8x8:
    case QRhiTexture::ASTC_10x5:
    case QRhiTexture::ASTC_10x6:
    case QRhiTexture::ASTC_10x8:
    case QRhiTexture::ASTC_10x10:
    case QRhiTexture::ASTC_12x10:
    case QRhiTexture::ASTC_12x12:
        return caps.hasTextureCompressionASTC;
    default:
        return false;
    }
}

bool QRhiWebGPU::isFeatureSupported(QRhi::Feature feature) const
{
    switch (feature) {
    case QRhi::MultisampleTexture:
    case QRhi::MultisampleRenderBuffer:
    case QRhi::DebugMarkers:
    case QRhi::Instancing:
    case QRhi::CustomInstanceStepRate:
    case QRhi::PrimitiveRestart:
    case QRhi::NonDynamicUniformBuffers:
    case QRhi::NPOTTextureRepeat:
    case QRhi::RedOrAlpha8IsRed:
    case QRhi::ElementIndexUint:
    case QRhi::Compute:
    case QRhi::BaseVertex:
    case QRhi::BaseInstance:
    case QRhi::ThreeDimensionalTextures:
    case QRhi::TextureArrays:
        return true;
    case QRhi::Timestamps:
        return caps.hasTimestampQuery;
    case QRhi::NonFourAlignedEffectiveIndexBufferOffset:
    case QRhi::WideLines:
    case QRhi::VertexShaderPointSize:
        return false;
    default:
        return false;
    }
}

int QRhiWebGPU::resourceLimit(QRhi::ResourceLimit limit) const
{
    switch (limit) {
    case QRhi::TextureSizeMin:
        return 1;
    case QRhi::TextureSizeMax:
        return int(caps.limits.maxTextureDimension2D);
    case QRhi::MaxColorAttachments:
        return int(caps.limits.maxColorAttachments);
    case QRhi::FramesInFlight:
        return 2;
    case QRhi::MaxAsyncReadbackFrames:
        return 2;
    case QRhi::MaxThreadGroupsPerDimension:
        return int(caps.limits.maxComputeWorkgroupsPerDimension);
    case QRhi::MaxThreadsPerThreadGroup:
        return int(caps.limits.maxComputeInvocationsPerWorkgroup);
    case QRhi::MaxThreadGroupX:
        return int(caps.limits.maxComputeWorkgroupSizeX);
    case QRhi::MaxThreadGroupY:
        return int(caps.limits.maxComputeWorkgroupSizeY);
    case QRhi::MaxThreadGroupZ:
        return int(caps.limits.maxComputeWorkgroupSizeZ);
    case QRhi::TextureArraySizeMax:
        return int(caps.limits.maxTextureArrayLayers);
    case QRhi::MaxUniformBufferRange:
        return int(qMin(caps.limits.maxUniformBufferBindingSize, quint64(INT_MAX)));
    case QRhi::MaxVertexInputs:
        return int(caps.limits.maxVertexBuffers);
    case QRhi::MaxVertexOutputs:
        return int(caps.limits.maxInterStageShaderVariables);
    default:
        return 0;
    }
}

const QRhiNativeHandles *QRhiWebGPU::nativeHandles()
{
    nativeHandlesStruct.device = device;
    nativeHandlesStruct.queue = queue;
    return &nativeHandlesStruct;
}

QRhiDriverInfo QRhiWebGPU::driverInfo() const
{
    QRhiDriverInfo info;
    info.deviceName = QByteArrayLiteral("WebGPU");
    return info;
}

QRhiStats QRhiWebGPU::statistics()
{
    return {};
}

bool QRhiWebGPU::makeThreadLocalNativeContextCurrent()
{
    return false;
}

void QRhiWebGPU::setQueueSubmitParams(QRhiNativeHandles *params)
{
    Q_UNUSED(params);
}

void QRhiWebGPU::releaseCachedResources()
{
}

bool QRhiWebGPU::isDeviceLost() const
{
    return false;
}

QByteArray QRhiWebGPU::pipelineCacheData()
{
    return {};
}

void QRhiWebGPU::setPipelineCacheData(const QByteArray &data)
{
    Q_UNUSED(data);
}

QRhiSwapChain *QRhiWebGPU::createSwapChain()
{
    return new QWebGPUSwapChain(this);
}

QRhiBuffer *QRhiWebGPU::createBuffer(QRhiBuffer::Type type, QRhiBuffer::UsageFlags usage, quint32 size)
{
    return new QWebGPUBuffer(this, type, usage, size);
}

QRhiRenderBuffer *QRhiWebGPU::createRenderBuffer(QRhiRenderBuffer::Type type, const QSize &pixelSize,
                                                  int sampleCount, QRhiRenderBuffer::Flags flags,
                                                  QRhiTexture::Format backingFormatHint)
{
    return new QWebGPURenderBuffer(this, type, pixelSize, sampleCount, flags, backingFormatHint);
}

QRhiTexture *QRhiWebGPU::createTexture(QRhiTexture::Format format, const QSize &pixelSize,
                                        int depth, int arraySize, int sampleCount, QRhiTexture::Flags flags)
{
    return new QWebGPUTexture(this, format, pixelSize, depth, arraySize, sampleCount, flags);
}

QRhiSampler *QRhiWebGPU::createSampler(QRhiSampler::Filter magFilter, QRhiSampler::Filter minFilter,
                                        QRhiSampler::Filter mipmapMode,
                                        QRhiSampler::AddressMode u, QRhiSampler::AddressMode v, QRhiSampler::AddressMode w)
{
    return new QWebGPUSampler(this, magFilter, minFilter, mipmapMode, u, v, w);
}

QRhiTextureRenderTarget *QRhiWebGPU::createTextureRenderTarget(const QRhiTextureRenderTargetDescription &desc,
                                                                QRhiTextureRenderTarget::Flags flags)
{
    return new QWebGPUTextureRenderTarget(this, desc, flags);
}

QRhiShadingRateMap *QRhiWebGPU::createShadingRateMap()
{
    return nullptr;
}

QRhiGraphicsPipeline *QRhiWebGPU::createGraphicsPipeline()
{
    return new QWebGPUGraphicsPipeline(this);
}

QRhiComputePipeline *QRhiWebGPU::createComputePipeline()
{
    return new QWebGPUComputePipeline(this);
}

QRhiShaderResourceBindings *QRhiWebGPU::createShaderResourceBindings()
{
    return new QWebGPUShaderResourceBindings(this);
}

QRhi::FrameOpResult QRhiWebGPU::beginFrame(QRhiSwapChain *swapChain, QRhi::BeginFrameFlags flags)
{
    Q_UNUSED(flags);

    QWebGPUSwapChain *swapChainD = QRHI_RES(QWebGPUSwapChain, swapChain);
    currentSwapChain = swapChainD;

    qCDebug(lcWebGPU, "QRhiWebGPU::beginFrame() swapchain=%p surface=%p frameCount=%u",
            swapChainD, swapChainD->surface, swapChainD->frameCount);

    // Check for valid surface (may be null on native if surface creation not implemented)
    if (!swapChainD->surface) {
        qWarning("QRhiWebGPU::beginFrame: No valid surface (native surface creation may not be implemented)");
        currentSwapChain = nullptr;
        return QRhi::FrameOpError;
    }

    // Get current texture from surface
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(swapChainD->surface, &surfaceTexture);

    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        qCDebug(lcWebGPU, "  surface texture status=%d (not optimal/suboptimal), returning FrameOpSwapChainOutOfDate",
                surfaceTexture.status);
        currentSwapChain = nullptr;
        return QRhi::FrameOpSwapChainOutOfDate;
    }

    swapChainD->currentTexture = surfaceTexture.texture;
    swapChainD->currentTextureView = wgpuTextureCreateView(swapChainD->currentTexture, nullptr);

    qCDebug(lcWebGPU, "  acquired surfaceTexture=%p textureView=%p status=%d",
            swapChainD->currentTexture, swapChainD->currentTextureView, surfaceTexture.status);

    swapChainD->cbWrapper.resetState();
    executeDeferredReleases();

    return QRhi::FrameOpSuccess;
}

QRhi::FrameOpResult QRhiWebGPU::endFrame(QRhiSwapChain *swapChain, QRhi::EndFrameFlags flags)
{
    Q_UNUSED(flags);

    QWebGPUSwapChain *swapChainD = QRHI_RES(QWebGPUSwapChain, swapChain);

    qCDebug(lcWebGPU, "QRhiWebGPU::endFrame() swapchain=%p frameCount=%u", swapChainD, swapChainD->frameCount);

    // Process swapchain readbacks before releasing the texture.
    // Swapchain readbacks are identified by sourceTexture being set.
    bool hasSwapchainReadbacks = false;
    for (const TextureReadback &rb : std::as_const(activeTextureReadbacks)) {
        if (rb.sourceTexture) {
            hasSwapchainReadbacks = true;
            break;
        }
    }
    if (hasSwapchainReadbacks) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        for (const TextureReadback &readback : std::as_const(activeTextureReadbacks)) {
            if (!readback.sourceTexture)
                continue;
            quint32 bpl = 0;
            textureFormatInfo(readback.format, readback.pixelSize, &bpl, nullptr, nullptr);
            quint32 alignedBpl = (bpl + 255) & ~255;
            WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            src.texture = readback.sourceTexture;
            WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
            dst.buffer = readback.stagingBuffer;
            dst.layout.bytesPerRow = alignedBpl;
            dst.layout.rowsPerImage = readback.pixelSize.height();
            WGPUExtent3D copySize = {};
            copySize.width = readback.pixelSize.width();
            copySize.height = readback.pixelSize.height();
            copySize.depthOrArrayLayers = 1;
            wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &copySize);
            qCDebug(lcWebGPU, "  swapchain readback copy: texture=%p -> buffer=%p size=%dx%d",
                    readback.sourceTexture, readback.stagingBuffer,
                    readback.pixelSize.width(), readback.pixelSize.height());
        }
        WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(queue, 1, &cmdBuf);
        wgpuCommandBufferRelease(cmdBuf);
        wgpuCommandEncoderRelease(encoder);
        // Wait for GPU completion and deliver results before releasing the texture
        finishActiveReadbacks(true);
    }

    if (swapChainD->currentTextureView) {
        wgpuTextureViewRelease(swapChainD->currentTextureView);
        swapChainD->currentTextureView = nullptr;
    }
    if (swapChainD->currentTexture) {
        wgpuTextureRelease(swapChainD->currentTexture);
        swapChainD->currentTexture = nullptr;
    }

#ifndef __EMSCRIPTEN__
    // On native Dawn, we need to explicitly present
    // On Emscripten, present is implicit when the frame ends
    qCDebug(lcWebGPU, "  presenting surface (native)");
    wgpuSurfacePresent(swapChainD->surface);
#endif

    swapChainD->frameCount += 1;
    currentSwapChain = nullptr;

    return QRhi::FrameOpSuccess;
}

QRhi::FrameOpResult QRhiWebGPU::beginOffscreenFrame(QRhiCommandBuffer **cb, QRhi::BeginFrameFlags flags)
{
    Q_UNUSED(flags);
    qCDebug(lcWebGPU, "QRhiWebGPU::beginOffscreenFrame()");
    offscreenCommandBuffer.resetState();
    *cb = &offscreenCommandBuffer;
    return QRhi::FrameOpSuccess;
}

QRhi::FrameOpResult QRhiWebGPU::endOffscreenFrame(QRhi::EndFrameFlags flags)
{
    Q_UNUSED(flags);
    qCDebug(lcWebGPU, "QRhiWebGPU::endOffscreenFrame() pendingReadbacks=%d", int(activeTextureReadbacks.size()));

    // Process pending texture readbacks
    if (!activeTextureReadbacks.isEmpty()) {
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

        for (const TextureReadback &readback : std::as_const(activeTextureReadbacks)) {
            WGPUTexture sourceTexture = readback.sourceTexture;
            if (!sourceTexture) {
                QWebGPUTexture *texD = QRHI_RES(QWebGPUTexture, readback.desc.texture());
                if (!texD || !texD->texture)
                    continue;
                sourceTexture = texD->texture;
            }

            quint32 bpl = 0;
            textureFormatInfo(readback.format, readback.pixelSize, &bpl, nullptr, nullptr);
            quint32 alignedBpl = (bpl + 255) & ~255;

            WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            src.texture = sourceTexture;
            src.mipLevel = readback.desc.level();
            const QRect rbRect = readback.desc.rect();
            src.origin.x = rbRect.isValid() ? uint32_t(rbRect.x()) : 0;
            src.origin.y = rbRect.isValid() ? uint32_t(rbRect.y()) : 0;
            src.origin.z = uint32_t(readback.desc.layer()); // z for 3D, array layer index for 2D arrays/cubemaps

            WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
            dst.buffer = readback.stagingBuffer;
            dst.layout.offset = 0;
            dst.layout.bytesPerRow = alignedBpl;
            dst.layout.rowsPerImage = readback.pixelSize.height();

            WGPUExtent3D copySize = {};
            copySize.width = readback.pixelSize.width();
            copySize.height = readback.pixelSize.height();
            copySize.depthOrArrayLayers = 1;

            wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &copySize);

            qCDebug(lcWebGPU, "  readback copy: texture=%p -> buffer=%p size=%dx%d bpl=%u alignedBpl=%u",
                    sourceTexture, readback.stagingBuffer,
                    readback.pixelSize.width(), readback.pixelSize.height(), bpl, alignedBpl);
        }

        WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuQueueSubmit(queue, 1, &cmdBuf);
        wgpuCommandBufferRelease(cmdBuf);
        wgpuCommandEncoderRelease(encoder);

        // Wait for GPU work to complete and process readbacks
        finishActiveReadbacks(true);
    }

    return QRhi::FrameOpSuccess;
}

QRhi::FrameOpResult QRhiWebGPU::finish()
{
    return QRhi::FrameOpSuccess;
}

void QRhiWebGPU::resourceUpdate(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *resourceUpdates)
{
    enqueueResourceUpdates(cb, resourceUpdates);
}

void QRhiWebGPU::enqueueResourceUpdates(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *resourceUpdates)
{
    Q_UNUSED(cb);

    QRhiResourceUpdateBatchPrivate *ud = QRhiResourceUpdateBatchPrivate::get(resourceUpdates);

    qCDebug(lcWebGPU, "QRhiWebGPU::enqueueResourceUpdates() bufferOps=%d textureOps=%d",
            ud->activeBufferOpCount, ud->activeTextureOpCount);

    for (int opIdx = 0; opIdx < ud->activeBufferOpCount; ++opIdx) {
        const QRhiResourceUpdateBatchPrivate::BufferOp &u(ud->bufferOps[opIdx]);
        if (u.type == QRhiResourceUpdateBatchPrivate::BufferOp::DynamicUpdate
            || u.type == QRhiResourceUpdateBatchPrivate::BufferOp::StaticUpload) {
            QWebGPUBuffer *bufD = QRHI_RES(QWebGPUBuffer, u.buf);
            qCDebug(lcWebGPU, "  buffer upload: buffer=%p offset=%d size=%d type=%d",
                    bufD->buffer, u.offset, int(u.data.size()), u.type);
            wgpuQueueWriteBuffer(queue, bufD->buffer, u.offset, u.data.constData(), u.data.size());
        }
    }

    for (int opIdx = 0; opIdx < ud->activeTextureOpCount; ++opIdx) {
        const QRhiResourceUpdateBatchPrivate::TextureOp &u(ud->textureOps[opIdx]);
        if (u.type == QRhiResourceUpdateBatchPrivate::TextureOp::Upload) {
            QWebGPUTexture *texD = QRHI_RES(QWebGPUTexture, u.dst);
            qCDebug(lcWebGPU, "  texture upload: texture=%p format=%d", texD->texture, texD->wgpuFormat);
            for (int layer = 0, maxLayer = u.subresDesc.size(); layer < maxLayer; ++layer) {
                for (int level = 0; level < QRhi::MAX_MIP_LEVELS; ++level) {
                    for (const QRhiTextureSubresourceUploadDescription &subresDesc : std::as_const(u.subresDesc[layer][level])) {
                        QByteArray data;
                        QSize size;

                        if (!subresDesc.image().isNull()) {
                            QImage img = subresDesc.image().convertToFormat(QImage::Format_RGBA8888);
                            // Apply source cropping if requested
                            const QPoint srcTopLeft = subresDesc.sourceTopLeft();
                            const QSize srcSize = subresDesc.sourceSize();
                            if (!srcTopLeft.isNull() || srcSize.isValid()) {
                                const QSize effectiveSrcSize = srcSize.isValid() ? srcSize : img.size();
                                img = img.copy(srcTopLeft.x(), srcTopLeft.y(),
                                               effectiveSrcSize.width(), effectiveSrcSize.height());
                            }
                            size = img.size();
                            // WebGPU requires bytesPerRow to be a multiple of 256.
                            // QImage rows may not be 256-aligned; re-pack with padding if needed.
                            quint32 srcBpl = 0;
                            textureFormatInfo(texD->m_format, size, &srcBpl, nullptr, nullptr);
                            const quint32 alignedSrcBpl = (srcBpl + 255) & ~255u;
                            if (alignedSrcBpl == quint32(img.bytesPerLine())) {
                                data = QByteArray(reinterpret_cast<const char *>(img.constBits()), img.sizeInBytes());
                            } else {
                                data.resize(qsizetype(alignedSrcBpl) * size.height());
                                for (int row = 0; row < size.height(); ++row) {
                                    memcpy(data.data() + qsizetype(alignedSrcBpl) * row,
                                           img.constScanLine(row), srcBpl);
                                }
                            }
                        } else if (!subresDesc.data().isEmpty()) {
                            size = subresDesc.sourceSize();
                            // If sourceSize not set, use texture dimensions at this mip level
                            if (!size.isValid())
                                size = q->sizeForMipLevel(level, texD->m_pixelSize);
                            // Raw data: caller provides tightly-packed rows (stride = bpl).
                            // WebGPU requires bytesPerRow to be a multiple of 256, so
                            // re-pack with padding if needed (unless caller supplies dataStride).
                            quint32 srcBpl = 0;
                            textureFormatInfo(texD->m_format, size, &srcBpl, nullptr, nullptr);
                            const quint32 alignedSrcBpl = (srcBpl + 255) & ~255u;
                            if (subresDesc.dataStride() > 0 || alignedSrcBpl == srcBpl) {
                                data = subresDesc.data();
                            } else {
                                data.resize(qsizetype(alignedSrcBpl) * size.height());
                                const char *src = subresDesc.data().constData();
                                for (int row = 0; row < size.height(); ++row) {
                                    memcpy(data.data() + qsizetype(alignedSrcBpl) * row,
                                           src + qsizetype(srcBpl) * row, srcBpl);
                                }
                            }
                        }

                        if (!data.isEmpty()) {
                            WGPUTexelCopyTextureInfo dest = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                            dest.texture = texD->texture;
                            dest.mipLevel = uint32_t(level);
                            dest.origin.x = uint32_t(subresDesc.destinationTopLeft().x());
                            dest.origin.y = uint32_t(subresDesc.destinationTopLeft().y());
                            dest.origin.z = uint32_t(layer);

                            // Use dataStride if provided, otherwise derive from the texture format.
                            // Must be a multiple of 256 per WebGPU spec.
                            quint32 bpl = 0;
                            textureFormatInfo(texD->m_format, size, &bpl, nullptr, nullptr);
                            const quint32 alignedBpl = (bpl + 255) & ~255u;
                            const quint32 bytesPerRow = subresDesc.dataStride() > 0
                                ? quint32(subresDesc.dataStride())
                                : alignedBpl;

                            WGPUTexelCopyBufferLayout dataLayout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
                            dataLayout.bytesPerRow = bytesPerRow;
                            dataLayout.rowsPerImage = uint32_t(size.height());

                            WGPUExtent3D writeSize = {};
                            writeSize.width = uint32_t(size.width());
                            writeSize.height = uint32_t(size.height());
                            writeSize.depthOrArrayLayers = 1;

                            wgpuQueueWriteTexture(queue, &dest, data.constData(),
                                                  data.size(), &dataLayout, &writeSize);
                        }
                    }
                }
            }
        } else if (u.type == QRhiResourceUpdateBatchPrivate::TextureOp::Copy) {
            Q_ASSERT(u.src && u.dst);
            QWebGPUTexture *srcD = QRHI_RES(QWebGPUTexture, u.src);
            QWebGPUTexture *dstD = QRHI_RES(QWebGPUTexture, u.dst);
            const QPoint sp = u.desc.sourceTopLeft();
            const QPoint dp = u.desc.destinationTopLeft();
            const QSize mipSize = q->sizeForMipLevel(u.desc.sourceLevel(), srcD->m_pixelSize);
            const QSize copySize = u.desc.pixelSize().isEmpty() ? mipSize : u.desc.pixelSize();

            WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            src.texture = srcD->texture;
            src.mipLevel = uint32_t(u.desc.sourceLevel());
            src.origin.x = uint32_t(sp.x());
            src.origin.y = uint32_t(sp.y());
            src.origin.z = uint32_t(u.desc.sourceLayer()); // z for 3D, array layer index for 2D arrays/cubemaps
            src.aspect = WGPUTextureAspect_All;

            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            dst.texture = dstD->texture;
            dst.mipLevel = uint32_t(u.desc.destinationLevel());
            dst.origin.x = uint32_t(dp.x());
            dst.origin.y = uint32_t(dp.y());
            dst.origin.z = uint32_t(u.desc.destinationLayer()); // z for 3D, array layer index for 2D arrays/cubemaps
            dst.aspect = WGPUTextureAspect_All;

            WGPUExtent3D ext = {};
            ext.width = uint32_t(copySize.width());
            ext.height = uint32_t(copySize.height());
            ext.depthOrArrayLayers = 1;

            WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(device, nullptr);
            wgpuCommandEncoderCopyTextureToTexture(enc, &src, &dst, &ext);
            WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(enc, nullptr);
            wgpuQueueSubmit(queue, 1, &cmdBuf);
            wgpuCommandBufferRelease(cmdBuf);
            wgpuCommandEncoderRelease(enc);

        } else if (u.type == QRhiResourceUpdateBatchPrivate::TextureOp::Read) {
            TextureReadback readback;
            readback.desc = u.rb;
            readback.result = u.result;

            QWebGPUTexture *texD = QRHI_RES(QWebGPUTexture, u.rb.texture());
            if (texD) {
                if (texD->samples > 1) {
                    qWarning("Multisample texture cannot be read back");
                    continue;
                }
                QRect rect;
                if (u.rb.rect().isValid())
                    rect = u.rb.rect();
                else
                    rect = QRect(QPoint(0, 0), q->sizeForMipLevel(u.rb.level(), texD->m_pixelSize));

                readback.pixelSize = rect.size();
                readback.format = texD->m_format;

                quint32 bpl = 0;
                textureFormatInfo(readback.format, readback.pixelSize, &bpl, &readback.bufferSize, nullptr);

                // WebGPU requires bytesPerRow to be a multiple of 256
                quint32 alignedBpl = (bpl + 255) & ~255;
                readback.bufferSize = alignedBpl * readback.pixelSize.height();

                WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
                bufDesc.size = readback.bufferSize;
                bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
                readback.stagingBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);

                qCDebug(lcWebGPU, "  texture readback: texture=%p size=%dx%d bufferSize=%u stagingBuffer=%p",
                        texD->texture, readback.pixelSize.width(), readback.pixelSize.height(),
                        readback.bufferSize, readback.stagingBuffer);

                activeTextureReadbacks.append(readback);
            } else {
                // Swapchain readback: null texture means "read from the current swapchain frame"
                if (!currentSwapChain) {
                    qWarning("QRhiWebGPU: swapchain readback requested but no active swapchain");
                    continue;
                }
                QWebGPUSwapChain *swapChainD = currentSwapChain;
                readback.pixelSize = swapChainD->pixelSize;
                readback.format = (swapChainD->surfaceFormat == WGPUTextureFormat_BGRA8Unorm
                                   || swapChainD->surfaceFormat == WGPUTextureFormat_BGRA8UnormSrgb)
                                  ? QRhiTexture::BGRA8 : QRhiTexture::RGBA8;

                quint32 bpl = 0;
                textureFormatInfo(readback.format, readback.pixelSize, &bpl, &readback.bufferSize, nullptr);
                quint32 alignedBpl = (bpl + 255) & ~255;
                readback.bufferSize = alignedBpl * readback.pixelSize.height();

                WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
                bufDesc.size = readback.bufferSize;
                bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
                readback.stagingBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);
                readback.sourceTexture = swapChainD->currentTexture;

                qCDebug(lcWebGPU, "  swapchain readback: texture=%p size=%dx%d bufferSize=%u stagingBuffer=%p",
                        readback.sourceTexture, readback.pixelSize.width(), readback.pixelSize.height(),
                        readback.bufferSize, readback.stagingBuffer);

                activeTextureReadbacks.append(readback);
            }
        }
    }

    ud->free();
}

void QRhiWebGPU::beginPass(QRhiCommandBuffer *cb, QRhiRenderTarget *rt,
                           const QColor &colorClearValue,
                           const QRhiDepthStencilClearValue &depthStencilClearValue,
                           QRhiResourceUpdateBatch *resourceUpdates,
                           QRhiCommandBuffer::BeginPassFlags flags)
{
    Q_UNUSED(flags);

    qCDebug(lcWebGPU, "QRhiWebGPU::beginPass() rt=%p size=%dx%d clearColor=(%f,%f,%f,%f) clearDepth=%f clearStencil=%u",
            rt, rt->pixelSize().width(), rt->pixelSize().height(),
            colorClearValue.redF(), colorClearValue.greenF(), colorClearValue.blueF(), colorClearValue.alphaF(),
            depthStencilClearValue.depthClearValue(), depthStencilClearValue.stencilClearValue());

    if (resourceUpdates)
        enqueueResourceUpdates(cb, resourceUpdates);

    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    cbD->currentTarget = rt;

    cbD->commandEncoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

    WGPURenderPassColorAttachment colorAttachments[QWebGPURenderPassDescriptor::MAX_COLOR_ATTACHMENTS];
    int colorAttachmentCount = 0;

    QWebGPURenderPassDescriptor *rpD = nullptr;

    if (rt->resourceType() == QRhiResource::SwapChainRenderTarget) {
        QWebGPUSwapChain *swapChainD = currentSwapChain;

        WGPURenderPassColorAttachment att = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        att.loadOp = WGPULoadOp_Clear;
        att.clearValue.r = colorClearValue.redF();
        att.clearValue.g = colorClearValue.greenF();
        att.clearValue.b = colorClearValue.blueF();
        att.clearValue.a = colorClearValue.alphaF();

        if (swapChainD->samples > 1 && swapChainD->msaaTextureView) {
            // Render to MSAA texture, resolve to swapchain texture
            att.view = swapChainD->msaaTextureView;
            att.resolveTarget = swapChainD->currentTextureView;
            att.storeOp = WGPUStoreOp_Discard; // MSAA texture is discarded after resolve
        } else {
            // No MSAA, render directly to swapchain texture
            att.view = swapChainD->currentTextureView;
            att.storeOp = WGPUStoreOp_Store;
        }

        colorAttachments[0] = att;
        colorAttachmentCount = 1;
        rpD = QRHI_RES(QWebGPURenderPassDescriptor, swapChainD->rtWrapper.renderPassDescriptor());
    } else {
        QWebGPUTextureRenderTarget *rtD = QRHI_RES(QWebGPUTextureRenderTarget, rt);
        rpD = QRHI_RES(QWebGPURenderPassDescriptor, rtD->renderPassDescriptor());

        for (int i = 0; i < rpD->colorAttachmentCount; ++i) {
            WGPURenderPassColorAttachment att = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            att.view = rtD->colorAttachmentViews[i];
            att.depthSlice = rtD->colorAttachmentDepthSlice[i];
            att.loadOp = WGPULoadOp_Clear;
            att.storeOp = WGPUStoreOp_Store;
            att.clearValue.r = colorClearValue.redF();
            att.clearValue.g = colorClearValue.greenF();
            att.clearValue.b = colorClearValue.blueF();
            att.clearValue.a = colorClearValue.alphaF();
            if (rtD->resolveAttachmentViews[i]) {
                att.resolveTarget = rtD->resolveAttachmentViews[i];
                att.storeOp = WGPUStoreOp_Discard; // MSAA texture is discarded after resolve
            }
            colorAttachments[i] = att;
        }
        colorAttachmentCount = rpD->colorAttachmentCount;
    }

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = colorAttachmentCount;
    passDesc.colorAttachments = colorAttachments;

    WGPURenderPassDepthStencilAttachment depthStencilAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    if (rpD && rpD->hasDepthStencil) {
        const bool hasStencil = formatHasStencil(rpD->dsFormat);
        if (rt->resourceType() == QRhiResource::SwapChainRenderTarget) {
            QWebGPUSwapChain *swapChainD = currentSwapChain;
            if (swapChainD->depthStencil()) {
                QWebGPURenderBuffer *rbD = QRHI_RES(QWebGPURenderBuffer, swapChainD->depthStencil());
                if (rbD->textureView) {
                    depthStencilAttachment.view = rbD->textureView;
                    depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
                    depthStencilAttachment.depthStoreOp = WGPUStoreOp_Store;
                    depthStencilAttachment.depthClearValue = depthStencilClearValue.depthClearValue();
                    if (hasStencil) {
                        depthStencilAttachment.stencilLoadOp = WGPULoadOp_Clear;
                        depthStencilAttachment.stencilStoreOp = WGPUStoreOp_Store;
                        depthStencilAttachment.stencilClearValue = depthStencilClearValue.stencilClearValue();
                    }
                    passDesc.depthStencilAttachment = &depthStencilAttachment;
                }
            }
        } else if (rt->resourceType() == QRhiResource::TextureRenderTarget) {
            QWebGPUTextureRenderTarget *rtD = QRHI_RES(QWebGPUTextureRenderTarget, rt);
            if (rtD->dsAttachmentView) {
                depthStencilAttachment.view = rtD->dsAttachmentView;
                depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
                depthStencilAttachment.depthStoreOp = WGPUStoreOp_Store;
                depthStencilAttachment.depthClearValue = depthStencilClearValue.depthClearValue();
                if (hasStencil) {
                    depthStencilAttachment.stencilLoadOp = WGPULoadOp_Clear;
                    depthStencilAttachment.stencilStoreOp = WGPUStoreOp_Store;
                    depthStencilAttachment.stencilClearValue = depthStencilClearValue.stencilClearValue();
                }
                passDesc.depthStencilAttachment = &depthStencilAttachment;
            }
        }
    }

    cbD->renderPassEncoder = wgpuCommandEncoderBeginRenderPass(cbD->commandEncoder, &passDesc);
    cbD->recordingPass = QWebGPUCommandBuffer::RenderPass;

    qCDebug(lcWebGPU, "  created commandEncoder=%p renderPassEncoder=%p colorAttachments=%d hasDepthStencil=%d",
            cbD->commandEncoder, cbD->renderPassEncoder, colorAttachmentCount, rpD ? rpD->hasDepthStencil : 0);
}

void QRhiWebGPU::endPass(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *resourceUpdates)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);

    qCDebug(lcWebGPU, "QRhiWebGPU::endPass() renderPassEncoder=%p commandEncoder=%p",
            cbD->renderPassEncoder, cbD->commandEncoder);

    wgpuRenderPassEncoderEnd(cbD->renderPassEncoder);
    cbD->renderPassEncoder = 0;

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(cbD->commandEncoder, nullptr);
    cbD->commandEncoder = nullptr;

    qCDebug(lcWebGPU, "  submitting commandBuffer=%p to queue=%p", commandBuffer, queue);
    wgpuQueueSubmit(queue, 1, &commandBuffer);
    wgpuCommandBufferRelease(commandBuffer);

    cbD->resetPerPassState();

    if (resourceUpdates)
        enqueueResourceUpdates(cb, resourceUpdates);
}

void QRhiWebGPU::setGraphicsPipeline(QRhiCommandBuffer *cb, QRhiGraphicsPipeline *ps)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    QWebGPUGraphicsPipeline *psD = QRHI_RES(QWebGPUGraphicsPipeline, ps);

    qCDebug(lcWebGPU, "QRhiWebGPU::setGraphicsPipeline encoder=%p pipeline=%p", cbD->renderPassEncoder, psD->pipeline);
    if (cbD->currentGraphicsPipeline != psD || cbD->currentPipelineGeneration != psD->generation) {
        wgpuRenderPassEncoderSetPipeline(cbD->renderPassEncoder, psD->pipeline);
        cbD->currentGraphicsPipeline = psD;
        cbD->currentPipelineGeneration = psD->generation;
    }
}

void QRhiWebGPU::setShaderResources(QRhiCommandBuffer *cb, QRhiShaderResourceBindings *srb,
                                     int dynamicOffsetCount,
                                     const QRhiCommandBuffer::DynamicOffset *dynamicOffsets)
{

    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);

    // If srb is null, use the one from the current pipeline
    if (!srb) {
        if (cbD->currentGraphicsPipeline)
            srb = cbD->currentGraphicsPipeline->m_shaderResourceBindings;
        else if (cbD->currentComputePipeline)
            srb = cbD->currentComputePipeline->m_shaderResourceBindings;
    }

    QWebGPUShaderResourceBindings *srbD = QRHI_RES(QWebGPUShaderResourceBindings, srb);

    // Layout-compatible SRBs (not the one passed to the pipeline at creation time) may not
    // have a bind group yet. Lazily create it using the pipeline's native binding map, which
    // was stored on the pipeline's reference SRB during pipeline creation.
    if (!srbD->bindGroup) {
        QShader::NativeResourceBindingMap map;
        if (cbD->currentGraphicsPipeline && cbD->currentGraphicsPipeline->m_shaderResourceBindings) {
            auto *pipelineSrb = QRHI_RES(QWebGPUShaderResourceBindings,
                                          cbD->currentGraphicsPipeline->m_shaderResourceBindings);
            map = pipelineSrb->nativeBindingMap;
        } else if (cbD->currentComputePipeline && cbD->currentComputePipeline->m_shaderResourceBindings) {
            auto *pipelineSrb = QRHI_RES(QWebGPUShaderResourceBindings,
                                          cbD->currentComputePipeline->m_shaderResourceBindings);
            map = pipelineSrb->nativeBindingMap;
        }
        if (!map.isEmpty())
            srbD->createWithBindingMap(map);
    }

    qCDebug(lcWebGPU, "QRhiWebGPU::setShaderResources() srb=%p bindGroup=%p dynamicOffsetCount=%d",
            srbD, srbD ? srbD->bindGroup : nullptr, dynamicOffsetCount);

    if (srbD->bindGroup) {
        // Build WebGPU-ordered dynamic offset array from Qt's (binding, offset) pairs.
        // WebGPU expects offsets in the order dynamic bindings appear in the bind group layout.
        QVarLengthArray<uint32_t, 4> wgpuDynOffsets;
        if (dynamicOffsetCount > 0 && !srbD->dynamicBindings.isEmpty()) {
            wgpuDynOffsets.resize(srbD->dynamicBindings.size(), 0);
            for (int i = 0; i < dynamicOffsetCount; ++i) {
                const int qtBinding = dynamicOffsets[i].first;
                for (int j = 0; j < srbD->dynamicBindings.size(); ++j) {
                    if (srbD->dynamicBindings[j] == qtBinding) {
                        wgpuDynOffsets[j] = uint32_t(dynamicOffsets[i].second);
                        break;
                    }
                }
            }
        }
        const uint32_t dynCount = uint32_t(wgpuDynOffsets.size());
        const uint32_t *dynData = dynCount > 0 ? wgpuDynOffsets.data() : nullptr;
        if (cbD->recordingPass == QWebGPUCommandBuffer::ComputePass)
            wgpuComputePassEncoderSetBindGroup(cbD->computePassEncoder, 0, srbD->bindGroup, dynCount, dynData);
        else
            wgpuRenderPassEncoderSetBindGroup(cbD->renderPassEncoder, 0, srbD->bindGroup, dynCount, dynData);
    }

    if (cbD->recordingPass == QWebGPUCommandBuffer::ComputePass) {
        cbD->currentComputeSrb = srbD;
    } else {
        cbD->currentGraphicsSrb = srbD;
    }
    cbD->currentSrbGeneration = srbD->generation;
}

void QRhiWebGPU::setVertexInput(QRhiCommandBuffer *cb,
                                 int startBinding, int bindingCount, const QRhiCommandBuffer::VertexInput *bindings,
                                 QRhiBuffer *indexBuf, quint32 indexOffset, QRhiCommandBuffer::IndexFormat indexFormat)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);

    qCDebug(lcWebGPU, "QRhiWebGPU::setVertexInput startBinding=%d bindingCount=%d", startBinding, bindingCount);
    for (int i = 0; i < bindingCount; ++i) {
        QWebGPUBuffer *bufD = QRHI_RES(QWebGPUBuffer, bindings[i].first);
        qCDebug(lcWebGPU, "  vertex buffer[%d]: buffer=%p offset=%u size=%u", i, bufD->buffer, bindings[i].second, bufD->size());
        wgpuRenderPassEncoderSetVertexBuffer(cbD->renderPassEncoder,
                                                    uint32_t(startBinding + i),
                                                    bufD->buffer,
                                                    bindings[i].second,
                                                    bufD->size() - bindings[i].second);
    }

    if (indexBuf) {
        QWebGPUBuffer *ibufD = QRHI_RES(QWebGPUBuffer, indexBuf);
        WGPUIndexFormat wgpuFormat = indexFormat == QRhiCommandBuffer::IndexUInt16
                                     ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
        wgpuRenderPassEncoderSetIndexBuffer(cbD->renderPassEncoder, ibufD->buffer,
                                            wgpuFormat, indexOffset, ibufD->size() - indexOffset);
    }
}

void QRhiWebGPU::setViewport(QRhiCommandBuffer *cb, const QRhiViewport &viewport)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    QSize outputSize = cbD->currentTarget->pixelSize();

    // QRhiViewport uses -1 for width/height to mean "use render target size"
    // Substitute those values before passing to the helper function
    std::array<float, 4> r = viewport.viewport();
    if (r[2] < 0)
        r[2] = float(outputSize.width());
    if (r[3] < 0)
        r[3] = float(outputSize.height());

    // Convert from bottom-left origin to top-left origin
    float x, y, w, h;
    if (!qrhi_toTopLeftRenderTargetRect<UnBounded>(outputSize, r, &x, &y, &w, &h))
        return;

    qCDebug(lcWebGPU, "QRhiWebGPU::setViewport x=%f y=%f w=%f h=%f minDepth=%f maxDepth=%f (outputSize=%dx%d)",
            x, y, w, h, viewport.minDepth(), viewport.maxDepth(), outputSize.width(), outputSize.height());
    wgpuRenderPassEncoderSetViewport(cbD->renderPassEncoder, x, y, w, h,
                                           viewport.minDepth(), viewport.maxDepth());
}

void QRhiWebGPU::setScissor(QRhiCommandBuffer *cb, const QRhiScissor &scissor)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    QSize outputSize = cbD->currentTarget->pixelSize();

    // Convert from bottom-left origin to top-left origin, and clamp to output bounds
    int x, y, w, h;
    if (!qrhi_toTopLeftRenderTargetRect<Bounded>(outputSize, scissor.scissor(), &x, &y, &w, &h))
        return;

    wgpuRenderPassEncoderSetScissorRect(cbD->renderPassEncoder, uint32_t(x), uint32_t(y),
                                               uint32_t(w), uint32_t(h));
}

void QRhiWebGPU::setBlendConstants(QRhiCommandBuffer *cb, const QColor &c)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    WGPUColor color = {c.redF(), c.greenF(), c.blueF(), c.alphaF()};
    wgpuRenderPassEncoderSetBlendConstant(cbD->renderPassEncoder, &color);
}

void QRhiWebGPU::setStencilRef(QRhiCommandBuffer *cb, quint32 refValue)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    wgpuRenderPassEncoderSetStencilReference(cbD->renderPassEncoder, refValue);
}

void QRhiWebGPU::setShadingRate(QRhiCommandBuffer *cb, const QSize &coarsePixelSize)
{
    Q_UNUSED(cb);
    Q_UNUSED(coarsePixelSize);
}

void QRhiWebGPU::draw(QRhiCommandBuffer *cb, quint32 vertexCount,
                       quint32 instanceCount, quint32 firstVertex, quint32 firstInstance)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    qCDebug(lcWebGPU, "QRhiWebGPU::draw vertexCount=%u instanceCount=%u firstVertex=%u firstInstance=%u encoder=%p pipeline=%p srb=%p",
            vertexCount, instanceCount, firstVertex, firstInstance,
            cbD->renderPassEncoder, cbD->currentGraphicsPipeline ? cbD->currentGraphicsPipeline->pipeline : nullptr,
            cbD->currentGraphicsSrb ? cbD->currentGraphicsSrb->bindGroup : nullptr);
    wgpuRenderPassEncoderDraw(cbD->renderPassEncoder, vertexCount, instanceCount, firstVertex, firstInstance);
}

void QRhiWebGPU::drawIndexed(QRhiCommandBuffer *cb, quint32 indexCount,
                              quint32 instanceCount, quint32 firstIndex,
                              qint32 vertexOffset, quint32 firstInstance)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    qCDebug(lcWebGPU, "QRhiWebGPU::drawIndexed() indexCount=%u instanceCount=%u firstIndex=%u vertexOffset=%d firstInstance=%u",
            indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    wgpuRenderPassEncoderDrawIndexed(cbD->renderPassEncoder, indexCount, instanceCount,
                                           firstIndex, vertexOffset, firstInstance);
}

void QRhiWebGPU::drawIndirect(QRhiCommandBuffer *cb, QRhiBuffer *indirectBuffer,
                               quint32 offset, quint32 drawCount, quint32 stride)
{
    Q_UNUSED(cb);
    Q_UNUSED(indirectBuffer);
    Q_UNUSED(offset);
    Q_UNUSED(drawCount);
    Q_UNUSED(stride);
    qWarning("QRhiWebGPU::drawIndirect: not implemented");
}

void QRhiWebGPU::drawIndexedIndirect(QRhiCommandBuffer *cb, QRhiBuffer *indirectBuffer,
                                      quint32 offset, quint32 drawCount, quint32 stride)
{
    Q_UNUSED(cb);
    Q_UNUSED(indirectBuffer);
    Q_UNUSED(offset);
    Q_UNUSED(drawCount);
    Q_UNUSED(stride);
    qWarning("QRhiWebGPU::drawIndexedIndirect: not implemented");
}

void QRhiWebGPU::drawIndirectCount(QRhiCommandBuffer *cb,
                                    QRhiBuffer *indirectBuffer, quint32 indirectBufferOffset,
                                    QRhiBuffer *countBuffer, quint32 countBufferOffset,
                                    quint32 maxDrawCount, quint32 stride)
{
    Q_UNUSED(cb);
    Q_UNUSED(indirectBuffer);
    Q_UNUSED(indirectBufferOffset);
    Q_UNUSED(countBuffer);
    Q_UNUSED(countBufferOffset);
    Q_UNUSED(maxDrawCount);
    Q_UNUSED(stride);
    qWarning("QRhiWebGPU::drawIndirectCount: not implemented");
}

void QRhiWebGPU::drawIndexedIndirectCount(QRhiCommandBuffer *cb,
                                           QRhiBuffer *indirectBuffer, quint32 indirectBufferOffset,
                                           QRhiBuffer *countBuffer, quint32 countBufferOffset,
                                           quint32 maxDrawCount, quint32 stride)
{
    Q_UNUSED(cb);
    Q_UNUSED(indirectBuffer);
    Q_UNUSED(indirectBufferOffset);
    Q_UNUSED(countBuffer);
    Q_UNUSED(countBufferOffset);
    Q_UNUSED(maxDrawCount);
    Q_UNUSED(stride);
    qWarning("QRhiWebGPU::drawIndexedIndirectCount: not implemented");
}

void QRhiWebGPU::dispatchIndirect(QRhiCommandBuffer *cb, QRhiBuffer *indirectBuffer,
                                   quint32 offset)
{
    Q_UNUSED(cb);
    Q_UNUSED(indirectBuffer);
    Q_UNUSED(offset);
    qWarning("QRhiWebGPU::dispatchIndirect: not implemented");
}

void QRhiWebGPU::debugMarkBegin(QRhiCommandBuffer *cb, const QByteArray &name)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    if (cbD->renderPassEncoder)
        wgpuRenderPassEncoderPushDebugGroup(cbD->renderPassEncoder, {name.constData(), size_t(name.size())});
}

void QRhiWebGPU::debugMarkEnd(QRhiCommandBuffer *cb)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    if (cbD->renderPassEncoder)
        wgpuRenderPassEncoderPopDebugGroup(cbD->renderPassEncoder);
}

void QRhiWebGPU::debugMarkMsg(QRhiCommandBuffer *cb, const QByteArray &msg)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    if (cbD->renderPassEncoder)
        wgpuRenderPassEncoderInsertDebugMarker(cbD->renderPassEncoder, {msg.constData(), size_t(msg.size())});
}

void QRhiWebGPU::beginComputePass(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *resourceUpdates,
                                   QRhiCommandBuffer::BeginPassFlags flags)
{
    Q_UNUSED(flags);

    qCDebug(lcWebGPU, "QRhiWebGPU::beginComputePass()");

    if (resourceUpdates)
        enqueueResourceUpdates(cb, resourceUpdates);

    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    cbD->commandEncoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
    cbD->computePassEncoder = wgpuCommandEncoderBeginComputePass(cbD->commandEncoder, nullptr);
    cbD->recordingPass = QWebGPUCommandBuffer::ComputePass;

    qCDebug(lcWebGPU, "  created commandEncoder=%p computePassEncoder=%p", cbD->commandEncoder, cbD->computePassEncoder);
}

void QRhiWebGPU::endComputePass(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *resourceUpdates)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);

    qCDebug(lcWebGPU, "QRhiWebGPU::endComputePass() computePassEncoder=%p commandEncoder=%p",
            cbD->computePassEncoder, cbD->commandEncoder);

    wgpuComputePassEncoderEnd(cbD->computePassEncoder);
    cbD->computePassEncoder = 0;

    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(cbD->commandEncoder, nullptr);
    cbD->commandEncoder = nullptr;

    qCDebug(lcWebGPU, "  submitting commandBuffer=%p to queue=%p", commandBuffer, queue);
    wgpuQueueSubmit(queue, 1, &commandBuffer);
    wgpuCommandBufferRelease(commandBuffer);

    cbD->resetPerPassState();

    if (resourceUpdates)
        enqueueResourceUpdates(cb, resourceUpdates);
}

void QRhiWebGPU::setComputePipeline(QRhiCommandBuffer *cb, QRhiComputePipeline *ps)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    QWebGPUComputePipeline *psD = QRHI_RES(QWebGPUComputePipeline, ps);

    qCDebug(lcWebGPU, "QRhiWebGPU::setComputePipeline() encoder=%p pipeline=%p", cbD->computePassEncoder, psD->pipeline);
    wgpuComputePassEncoderSetPipeline(cbD->computePassEncoder, psD->pipeline);
    cbD->currentComputePipeline = psD;
    cbD->currentPipelineGeneration = psD->generation;
}

void QRhiWebGPU::dispatch(QRhiCommandBuffer *cb, int x, int y, int z)
{
    QWebGPUCommandBuffer *cbD = QRHI_RES(QWebGPUCommandBuffer, cb);
    qCDebug(lcWebGPU, "QRhiWebGPU::dispatch() x=%d y=%d z=%d", x, y, z);
    wgpuComputePassEncoderDispatchWorkgroups(cbD->computePassEncoder, uint32_t(x), uint32_t(y), uint32_t(z));
}

const QRhiNativeHandles *QRhiWebGPU::nativeHandles(QRhiCommandBuffer *cb)
{
    Q_UNUSED(cb);
    return nullptr;
}

void QRhiWebGPU::beginExternal(QRhiCommandBuffer *cb)
{
    Q_UNUSED(cb);
}

void QRhiWebGPU::endExternal(QRhiCommandBuffer *cb)
{
    Q_UNUSED(cb);
}

double QRhiWebGPU::lastCompletedGpuTime(QRhiCommandBuffer *cb)
{
    Q_UNUSED(cb);
    return 0;
}

void QRhiWebGPU::executeDeferredReleases(bool forced)
{
    Q_UNUSED(forced);
}

void QRhiWebGPU::finishActiveReadbacks(bool forced)
{
    if (activeTextureReadbacks.isEmpty())
        return;

    qCDebug(lcWebGPU, "QRhiWebGPU::finishActiveReadbacks() count=%d forced=%d",
            int(activeTextureReadbacks.size()), forced);

    struct MapContext {
        bool complete = false;
        WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error; // Default to error; will be set by callback
    };

    QVarLengthArray<std::function<void()>, 4> completedCallbacks;

    for (int i = activeTextureReadbacks.size() - 1; i >= 0; --i) {
        TextureReadback &readback = activeTextureReadbacks[i];

        if (!readback.stagingBuffer)
            continue;

        MapContext ctx;

        WGPUBufferMapCallbackInfo callbackInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callbackInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView message, void *userdata1, void *userdata2) {
            Q_UNUSED(message);
            Q_UNUSED(userdata2);
            MapContext *ctx = static_cast<MapContext *>(userdata1);
            ctx->status = status;
            ctx->complete = true;
        };
        callbackInfo.userdata1 = &ctx;

#ifndef Q_OS_WASM
        // On native Dawn, use wgpuInstanceWaitAny for a proper blocking wait.
        // WGPUCallbackMode_WaitAnyOnly fires the callback from within wgpuInstanceWaitAny.
        callbackInfo.mode = WGPUCallbackMode_WaitAnyOnly;
        WGPUFuture future = wgpuBufferMapAsync(readback.stagingBuffer,
                                               WGPUMapMode_Read,
                                               0,
                                               readback.bufferSize,
                                               callbackInfo);
        WGPUFutureWaitInfo waitInfo = WGPU_FUTURE_WAIT_INFO_INIT;
        waitInfo.future = future;
        wgpuInstanceWaitAny(instance, 1, &waitInfo, UINT64_MAX);
#else
        // On WASM, poll with emscripten_sleep to yield to the browser event loop.
        callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        wgpuBufferMapAsync(readback.stagingBuffer,
                           WGPUMapMode_Read,
                           0,
                           readback.bufferSize,
                           callbackInfo);
        int pollCount = 0;
        const int maxPollCount = 1000;
        while (!ctx.complete && pollCount < maxPollCount) {
            wgpuInstanceProcessEvents(instance);
            emscripten_sleep(1);
            ++pollCount;
        }
        qCDebug(lcWebGPU, "  readback[%d]: mapping complete=%d status=%d pollCount=%d",
                i, ctx.complete, int(ctx.status), pollCount);
#endif

        if (ctx.complete && ctx.status == WGPUMapAsyncStatus_Success) {
            const void *mappedData = wgpuBufferGetConstMappedRange(readback.stagingBuffer, 0, readback.bufferSize);
            if (mappedData) {
                readback.result->format = readback.format;
                readback.result->pixelSize = readback.pixelSize;

                // Calculate actual bytes per line (may be aligned)
                quint32 bpl = 0;
                textureFormatInfo(readback.format, readback.pixelSize, &bpl, nullptr, nullptr);
                quint32 alignedBpl = (bpl + 255) & ~255;

                // If alignment caused padding, we need to copy row by row
                if (alignedBpl != bpl) {
                    readback.result->data.resize(bpl * readback.pixelSize.height());
                    const char *src = static_cast<const char *>(mappedData);
                    char *dst = readback.result->data.data();
                    for (int y = 0; y < readback.pixelSize.height(); ++y) {
                        memcpy(dst + y * bpl, src + y * alignedBpl, bpl);
                    }
                } else {
                    readback.result->data.resize(readback.bufferSize);
                    memcpy(readback.result->data.data(), mappedData, readback.bufferSize);
                }

                qCDebug(lcWebGPU, "  readback[%d]: copied %lld bytes (bpl=%u alignedBpl=%u)",
                        i, (long long)readback.result->data.size(), bpl, alignedBpl);

                if (readback.result->completed)
                    completedCallbacks.append(readback.result->completed);
            }

            wgpuBufferUnmap(readback.stagingBuffer);
        } else {
            qWarning("Texture readback failed: complete=%d status=%d", ctx.complete, int(ctx.status));
        }

        wgpuBufferRelease(readback.stagingBuffer);
        activeTextureReadbacks.remove(i);
    }

    // Call completion callbacks after all readbacks are processed
    for (const auto &callback : completedCallbacks) {
        callback();
    }
}

// Static helper functions

WGPUTextureFormat QRhiWebGPU::toWgpuTextureFormat(QRhiTexture::Format format, QRhiTexture::Flags flags)
{
    const bool srgb = flags.testFlag(QRhiTexture::sRGB);

    switch (format) {
    case QRhiTexture::RGBA8:
        return srgb ? WGPUTextureFormat_RGBA8UnormSrgb : WGPUTextureFormat_RGBA8Unorm;
    case QRhiTexture::BGRA8:
        return srgb ? WGPUTextureFormat_BGRA8UnormSrgb : WGPUTextureFormat_BGRA8Unorm;
    case QRhiTexture::R8:
    case QRhiTexture::RED_OR_ALPHA8:
        return WGPUTextureFormat_R8Unorm;
    case QRhiTexture::RG8:
        return WGPUTextureFormat_RG8Unorm;
    case QRhiTexture::R16:
        return WGPUTextureFormat_R16Uint;
    case QRhiTexture::RG16:
        return WGPUTextureFormat_RG16Uint;
    case QRhiTexture::RGBA16F:
        return WGPUTextureFormat_RGBA16Float;
    case QRhiTexture::RGBA32F:
        return WGPUTextureFormat_RGBA32Float;
    case QRhiTexture::R16F:
        return WGPUTextureFormat_R16Float;
    case QRhiTexture::R32F:
        return WGPUTextureFormat_R32Float;
    case QRhiTexture::D16:
        return WGPUTextureFormat_Depth16Unorm;
    case QRhiTexture::D24:
    case QRhiTexture::D24S8:
        return WGPUTextureFormat_Depth24PlusStencil8;
    case QRhiTexture::D32F:
        return WGPUTextureFormat_Depth32Float;
    case QRhiTexture::D32FS8:
        return WGPUTextureFormat_Depth32FloatStencil8;
    case QRhiTexture::BC1:
        return srgb ? WGPUTextureFormat_BC1RGBAUnormSrgb : WGPUTextureFormat_BC1RGBAUnorm;
    case QRhiTexture::BC2:
        return srgb ? WGPUTextureFormat_BC2RGBAUnormSrgb : WGPUTextureFormat_BC2RGBAUnorm;
    case QRhiTexture::BC3:
        return srgb ? WGPUTextureFormat_BC3RGBAUnormSrgb : WGPUTextureFormat_BC3RGBAUnorm;
    case QRhiTexture::BC4:
        return WGPUTextureFormat_BC4RUnorm;
    case QRhiTexture::BC5:
        return WGPUTextureFormat_BC5RGUnorm;
    case QRhiTexture::BC6H:
        return WGPUTextureFormat_BC6HRGBUfloat;
    case QRhiTexture::BC7:
        return srgb ? WGPUTextureFormat_BC7RGBAUnormSrgb : WGPUTextureFormat_BC7RGBAUnorm;
    case QRhiTexture::ETC2_RGB8:
        return srgb ? WGPUTextureFormat_ETC2RGB8UnormSrgb : WGPUTextureFormat_ETC2RGB8Unorm;
    case QRhiTexture::ETC2_RGB8A1:
        return srgb ? WGPUTextureFormat_ETC2RGB8A1UnormSrgb : WGPUTextureFormat_ETC2RGB8A1Unorm;
    case QRhiTexture::ETC2_RGBA8:
        return srgb ? WGPUTextureFormat_ETC2RGBA8UnormSrgb : WGPUTextureFormat_ETC2RGBA8Unorm;
    case QRhiTexture::ASTC_4x4:
        return srgb ? WGPUTextureFormat_ASTC4x4UnormSrgb : WGPUTextureFormat_ASTC4x4Unorm;
    case QRhiTexture::ASTC_5x4:
        return srgb ? WGPUTextureFormat_ASTC5x4UnormSrgb : WGPUTextureFormat_ASTC5x4Unorm;
    case QRhiTexture::ASTC_5x5:
        return srgb ? WGPUTextureFormat_ASTC5x5UnormSrgb : WGPUTextureFormat_ASTC5x5Unorm;
    case QRhiTexture::ASTC_6x5:
        return srgb ? WGPUTextureFormat_ASTC6x5UnormSrgb : WGPUTextureFormat_ASTC6x5Unorm;
    case QRhiTexture::ASTC_6x6:
        return srgb ? WGPUTextureFormat_ASTC6x6UnormSrgb : WGPUTextureFormat_ASTC6x6Unorm;
    case QRhiTexture::ASTC_8x5:
        return srgb ? WGPUTextureFormat_ASTC8x5UnormSrgb : WGPUTextureFormat_ASTC8x5Unorm;
    case QRhiTexture::ASTC_8x6:
        return srgb ? WGPUTextureFormat_ASTC8x6UnormSrgb : WGPUTextureFormat_ASTC8x6Unorm;
    case QRhiTexture::ASTC_8x8:
        return srgb ? WGPUTextureFormat_ASTC8x8UnormSrgb : WGPUTextureFormat_ASTC8x8Unorm;
    case QRhiTexture::ASTC_10x5:
        return srgb ? WGPUTextureFormat_ASTC10x5UnormSrgb : WGPUTextureFormat_ASTC10x5Unorm;
    case QRhiTexture::ASTC_10x6:
        return srgb ? WGPUTextureFormat_ASTC10x6UnormSrgb : WGPUTextureFormat_ASTC10x6Unorm;
    case QRhiTexture::ASTC_10x8:
        return srgb ? WGPUTextureFormat_ASTC10x8UnormSrgb : WGPUTextureFormat_ASTC10x8Unorm;
    case QRhiTexture::ASTC_10x10:
        return srgb ? WGPUTextureFormat_ASTC10x10UnormSrgb : WGPUTextureFormat_ASTC10x10Unorm;
    case QRhiTexture::ASTC_12x10:
        return srgb ? WGPUTextureFormat_ASTC12x10UnormSrgb : WGPUTextureFormat_ASTC12x10Unorm;
    case QRhiTexture::ASTC_12x12:
        return srgb ? WGPUTextureFormat_ASTC12x12UnormSrgb : WGPUTextureFormat_ASTC12x12Unorm;
    default:
        return WGPUTextureFormat_Undefined;
    }
}

WGPUVertexFormat QRhiWebGPU::toWgpuVertexFormat(QRhiVertexInputAttribute::Format format)
{
    switch (format) {
    case QRhiVertexInputAttribute::Float4:
        return WGPUVertexFormat_Float32x4;
    case QRhiVertexInputAttribute::Float3:
        return WGPUVertexFormat_Float32x3;
    case QRhiVertexInputAttribute::Float2:
        return WGPUVertexFormat_Float32x2;
    case QRhiVertexInputAttribute::Float:
        return WGPUVertexFormat_Float32;
    case QRhiVertexInputAttribute::UNormByte4:
        return WGPUVertexFormat_Unorm8x4;
    case QRhiVertexInputAttribute::UNormByte2:
        return WGPUVertexFormat_Unorm8x2;
    case QRhiVertexInputAttribute::UInt4:
        return WGPUVertexFormat_Uint32x4;
    case QRhiVertexInputAttribute::UInt3:
        return WGPUVertexFormat_Uint32x3;
    case QRhiVertexInputAttribute::UInt2:
        return WGPUVertexFormat_Uint32x2;
    case QRhiVertexInputAttribute::UInt:
        return WGPUVertexFormat_Uint32;
    case QRhiVertexInputAttribute::SInt4:
        return WGPUVertexFormat_Sint32x4;
    case QRhiVertexInputAttribute::SInt3:
        return WGPUVertexFormat_Sint32x3;
    case QRhiVertexInputAttribute::SInt2:
        return WGPUVertexFormat_Sint32x2;
    case QRhiVertexInputAttribute::SInt:
        return WGPUVertexFormat_Sint32;
    case QRhiVertexInputAttribute::Half4:
        return WGPUVertexFormat_Float16x4;
    case QRhiVertexInputAttribute::Half2:
        return WGPUVertexFormat_Float16x2;
    default:
        return WGPUVertexFormat_Float32x4;
    }
}

WGPUPrimitiveTopology QRhiWebGPU::toWgpuPrimitiveTopology(QRhiGraphicsPipeline::Topology t)
{
    switch (t) {
    case QRhiGraphicsPipeline::Triangles:
        return WGPUPrimitiveTopology_TriangleList;
    case QRhiGraphicsPipeline::TriangleStrip:
        return WGPUPrimitiveTopology_TriangleStrip;
    case QRhiGraphicsPipeline::Lines:
        return WGPUPrimitiveTopology_LineList;
    case QRhiGraphicsPipeline::LineStrip:
        return WGPUPrimitiveTopology_LineStrip;
    case QRhiGraphicsPipeline::Points:
        return WGPUPrimitiveTopology_PointList;
    default:
        return WGPUPrimitiveTopology_TriangleList;
    }
}

WGPUCullMode QRhiWebGPU::toWgpuCullMode(QRhiGraphicsPipeline::CullMode c)
{
    switch (c) {
    case QRhiGraphicsPipeline::None:
        return WGPUCullMode_None;
    case QRhiGraphicsPipeline::Front:
        return WGPUCullMode_Front;
    case QRhiGraphicsPipeline::Back:
        return WGPUCullMode_Back;
    default:
        return WGPUCullMode_None;
    }
}

WGPUFrontFace QRhiWebGPU::toWgpuFrontFace(QRhiGraphicsPipeline::FrontFace f)
{
    switch (f) {
    case QRhiGraphicsPipeline::CCW:
        return WGPUFrontFace_CCW;
    case QRhiGraphicsPipeline::CW:
        return WGPUFrontFace_CW;
    default:
        return WGPUFrontFace_CCW;
    }
}

WGPUBlendFactor QRhiWebGPU::toWgpuBlendFactor(QRhiGraphicsPipeline::BlendFactor f)
{
    switch (f) {
    case QRhiGraphicsPipeline::Zero:
        return WGPUBlendFactor_Zero;
    case QRhiGraphicsPipeline::One:
        return WGPUBlendFactor_One;
    case QRhiGraphicsPipeline::SrcColor:
        return WGPUBlendFactor_Src;
    case QRhiGraphicsPipeline::OneMinusSrcColor:
        return WGPUBlendFactor_OneMinusSrc;
    case QRhiGraphicsPipeline::DstColor:
        return WGPUBlendFactor_Dst;
    case QRhiGraphicsPipeline::OneMinusDstColor:
        return WGPUBlendFactor_OneMinusDst;
    case QRhiGraphicsPipeline::SrcAlpha:
        return WGPUBlendFactor_SrcAlpha;
    case QRhiGraphicsPipeline::OneMinusSrcAlpha:
        return WGPUBlendFactor_OneMinusSrcAlpha;
    case QRhiGraphicsPipeline::DstAlpha:
        return WGPUBlendFactor_DstAlpha;
    case QRhiGraphicsPipeline::OneMinusDstAlpha:
        return WGPUBlendFactor_OneMinusDstAlpha;
    case QRhiGraphicsPipeline::ConstantColor:
    case QRhiGraphicsPipeline::ConstantAlpha:
        return WGPUBlendFactor_Constant;
    case QRhiGraphicsPipeline::OneMinusConstantColor:
    case QRhiGraphicsPipeline::OneMinusConstantAlpha:
        return WGPUBlendFactor_OneMinusConstant;
    case QRhiGraphicsPipeline::SrcAlphaSaturate:
        return WGPUBlendFactor_SrcAlphaSaturated;
    case QRhiGraphicsPipeline::Src1Color:
        return WGPUBlendFactor_Src1;
    case QRhiGraphicsPipeline::OneMinusSrc1Color:
        return WGPUBlendFactor_OneMinusSrc1;
    case QRhiGraphicsPipeline::Src1Alpha:
        return WGPUBlendFactor_Src1Alpha;
    case QRhiGraphicsPipeline::OneMinusSrc1Alpha:
        return WGPUBlendFactor_OneMinusSrc1Alpha;
    default:
        return WGPUBlendFactor_One;
    }
}

WGPUBlendOperation QRhiWebGPU::toWgpuBlendOp(QRhiGraphicsPipeline::BlendOp op)
{
    switch (op) {
    case QRhiGraphicsPipeline::Add:
        return WGPUBlendOperation_Add;
    case QRhiGraphicsPipeline::Subtract:
        return WGPUBlendOperation_Subtract;
    case QRhiGraphicsPipeline::ReverseSubtract:
        return WGPUBlendOperation_ReverseSubtract;
    case QRhiGraphicsPipeline::Min:
        return WGPUBlendOperation_Min;
    case QRhiGraphicsPipeline::Max:
        return WGPUBlendOperation_Max;
    default:
        return WGPUBlendOperation_Add;
    }
}

WGPUCompareFunction QRhiWebGPU::toWgpuCompareOp(QRhiGraphicsPipeline::CompareOp op)
{
    switch (op) {
    case QRhiGraphicsPipeline::Never:
        return WGPUCompareFunction_Never;
    case QRhiGraphicsPipeline::Less:
        return WGPUCompareFunction_Less;
    case QRhiGraphicsPipeline::Equal:
        return WGPUCompareFunction_Equal;
    case QRhiGraphicsPipeline::LessOrEqual:
        return WGPUCompareFunction_LessEqual;
    case QRhiGraphicsPipeline::Greater:
        return WGPUCompareFunction_Greater;
    case QRhiGraphicsPipeline::NotEqual:
        return WGPUCompareFunction_NotEqual;
    case QRhiGraphicsPipeline::GreaterOrEqual:
        return WGPUCompareFunction_GreaterEqual;
    case QRhiGraphicsPipeline::Always:
        return WGPUCompareFunction_Always;
    default:
        return WGPUCompareFunction_Always;
    }
}

WGPUStencilOperation QRhiWebGPU::toWgpuStencilOp(QRhiGraphicsPipeline::StencilOp op)
{
    switch (op) {
    case QRhiGraphicsPipeline::StencilZero:
        return WGPUStencilOperation_Zero;
    case QRhiGraphicsPipeline::Keep:
        return WGPUStencilOperation_Keep;
    case QRhiGraphicsPipeline::Replace:
        return WGPUStencilOperation_Replace;
    case QRhiGraphicsPipeline::IncrementAndClamp:
        return WGPUStencilOperation_IncrementClamp;
    case QRhiGraphicsPipeline::DecrementAndClamp:
        return WGPUStencilOperation_DecrementClamp;
    case QRhiGraphicsPipeline::Invert:
        return WGPUStencilOperation_Invert;
    case QRhiGraphicsPipeline::IncrementAndWrap:
        return WGPUStencilOperation_IncrementWrap;
    case QRhiGraphicsPipeline::DecrementAndWrap:
        return WGPUStencilOperation_DecrementWrap;
    default:
        return WGPUStencilOperation_Keep;
    }
}

QT_END_NAMESPACE
