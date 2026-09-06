// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QRHIWEBGPU_P_H
#define QRHIWEBGPU_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include "qrhi_p.h"
#include "qshader.h"
#include <QWindow>

// Standard WebGPU C API (webgpu.h)
// - Native builds: Dawn's webgpu.h
// - Emscripten builds: emdawnwebgpu's webgpu.h (via --use-port=emdawnwebgpu)
#include <webgpu/webgpu.h>

QT_BEGIN_NAMESPACE

struct QWebGPUBuffer : public QRhiBuffer
{
    QWebGPUBuffer(QRhiImplementation *rhi, Type type, UsageFlags usage, quint32 size);
    ~QWebGPUBuffer();
    void destroy() override;
    bool create() override;
    QRhiBuffer::NativeBuffer nativeBuffer() override;
    char *beginFullDynamicBufferUpdateForCurrentFrame() override;
    void endFullDynamicBufferUpdateForCurrentFrame() override;

    WGPUBuffer buffer = nullptr;
    char *stagingData = nullptr;
    uint generation = 0;
    int lastActiveFrameSlot = -1;
    friend class QRhiWebGPU;
};

struct QWebGPURenderBuffer : public QRhiRenderBuffer
{
    QWebGPURenderBuffer(QRhiImplementation *rhi, Type type, const QSize &pixelSize,
                        int sampleCount, QRhiRenderBuffer::Flags flags,
                        QRhiTexture::Format backingFormatHint);
    ~QWebGPURenderBuffer();
    void destroy() override;
    bool create() override;
    QRhiTexture::Format backingFormat() const override;

    WGPUTexture texture = nullptr;
    WGPUTextureView textureView = nullptr;
    int samples = 1;
    uint generation = 0;
    int lastActiveFrameSlot = -1;
    friend class QRhiWebGPU;
};

struct QWebGPUTexture : public QRhiTexture
{
    QWebGPUTexture(QRhiImplementation *rhi, Format format, const QSize &pixelSize, int depth,
                   int arraySize, int sampleCount, Flags flags);
    ~QWebGPUTexture();
    void destroy() override;
    bool create() override;
    bool createFrom(NativeTexture src) override;
    NativeTexture nativeTexture() override;

    bool prepareCreate(QSize *adjustedSize = nullptr);

    WGPUTexture texture = nullptr;
    WGPUTextureView textureView = nullptr;
    WGPUTextureFormat wgpuFormat = WGPUTextureFormat_Undefined;
    int mipLevelCount = 0;
    int samples = 1;
    bool imported = false;
    uint generation = 0;
    int lastActiveFrameSlot = -1;
    friend class QRhiWebGPU;
};

struct QWebGPUSampler : public QRhiSampler
{
    QWebGPUSampler(QRhiImplementation *rhi, Filter magFilter, Filter minFilter, Filter mipmapMode,
                   AddressMode u, AddressMode v, AddressMode w);
    ~QWebGPUSampler();
    void destroy() override;
    bool create() override;

    WGPUSampler sampler = nullptr;
    uint generation = 0;
    int lastActiveFrameSlot = -1;
    friend class QRhiWebGPU;
};

struct QWebGPURenderPassDescriptor : public QRhiRenderPassDescriptor
{
    QWebGPURenderPassDescriptor(QRhiImplementation *rhi);
    ~QWebGPURenderPassDescriptor();
    void destroy() override;
    bool isCompatible(const QRhiRenderPassDescriptor *other) const override;
    QRhiRenderPassDescriptor *newCompatibleRenderPassDescriptor() const override;
    QVector<quint32> serializedFormat() const override;

    void updateSerializedFormat();

    static const int MAX_COLOR_ATTACHMENTS = 8;
    int colorAttachmentCount = 0;
    bool hasDepthStencil = false;
    WGPUTextureFormat colorFormat[MAX_COLOR_ATTACHMENTS];
    WGPUTextureFormat dsFormat = WGPUTextureFormat_Undefined;
    QVector<quint32> serializedFormatData;
};

struct QWebGPURenderTargetData
{
    QWebGPURenderTargetData(QRhiImplementation *) { }

    QWebGPURenderPassDescriptor *rp = nullptr;
    QSize pixelSize;
    float dpr = 1;
    QRhiRenderTargetAttachmentTracker::ResIdList currentResIdList;
};

struct QWebGPUSwapChainRenderTarget : public QRhiSwapChainRenderTarget
{
    QWebGPUSwapChainRenderTarget(QRhiImplementation *rhi, QRhiSwapChain *swapchain);
    ~QWebGPUSwapChainRenderTarget();
    void destroy() override;

    QSize pixelSize() const override;
    float devicePixelRatio() const override;
    int sampleCount() const override;

    QWebGPURenderTargetData d;
};

struct QWebGPUTextureRenderTarget : public QRhiTextureRenderTarget
{
    QWebGPUTextureRenderTarget(QRhiImplementation *rhi, const QRhiTextureRenderTargetDescription &desc, Flags flags);
    ~QWebGPUTextureRenderTarget();
    void destroy() override;

    QSize pixelSize() const override;
    float devicePixelRatio() const override;
    int sampleCount() const override;

    QRhiRenderPassDescriptor *newCompatibleRenderPassDescriptor() override;
    bool create() override;

    QWebGPURenderTargetData d;
    WGPUTextureView colorAttachmentViews[QWebGPURenderPassDescriptor::MAX_COLOR_ATTACHMENTS];
    WGPUTextureView resolveAttachmentViews[QWebGPURenderPassDescriptor::MAX_COLOR_ATTACHMENTS];
    WGPUTextureView dsAttachmentView = nullptr;
    // true when the view was created by us (per-layer/level) and must be released in destroy()
    bool ownedColorAttachmentViews[QWebGPURenderPassDescriptor::MAX_COLOR_ATTACHMENTS] = {};
    // For 3D texture attachments: the z-slice index (WGPU_DEPTH_SLICE_UNDEFINED for non-3D)
    uint32_t colorAttachmentDepthSlice[QWebGPURenderPassDescriptor::MAX_COLOR_ATTACHMENTS] = {};
    friend class QRhiWebGPU;
};

struct QWebGPUShaderResourceBindings : public QRhiShaderResourceBindings
{
    QWebGPUShaderResourceBindings(QRhiImplementation *rhi);
    ~QWebGPUShaderResourceBindings();
    void destroy() override;
    bool create() override;
    void updateResources(UpdateFlags flags) override;

    bool createWithBindingMap(const QShader::NativeResourceBindingMap &map);

    QVarLengthArray<QRhiShaderResourceBinding, 8> sortedBindings;
    int maxBinding = -1;

    WGPUBindGroupLayout bindGroupLayout = nullptr;
    WGPUBindGroup bindGroup = nullptr;
    QShader::NativeResourceBindingMap nativeBindingMap;
    // Sorted list of Qt binding numbers that have hasDynamicOffset=true,
    // in the order they appear in the WebGPU bind group layout.
    QVarLengthArray<int, 4> dynamicBindings;
    uint generation = 0;
    friend class QRhiWebGPU;
};

struct QWebGPUGraphicsPipeline : public QRhiGraphicsPipeline
{
    QWebGPUGraphicsPipeline(QRhiImplementation *rhi);
    ~QWebGPUGraphicsPipeline();
    void destroy() override;
    bool create() override;

    WGPURenderPipeline pipeline = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    uint generation = 0;
    int lastActiveFrameSlot = -1;
    friend class QRhiWebGPU;
};

struct QWebGPUComputePipeline : public QRhiComputePipeline
{
    QWebGPUComputePipeline(QRhiImplementation *rhi);
    ~QWebGPUComputePipeline();
    void destroy() override;
    bool create() override;

    WGPUComputePipeline pipeline = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    uint generation = 0;
    int lastActiveFrameSlot = -1;
    friend class QRhiWebGPU;
};

struct QWebGPUCommandBuffer : public QRhiCommandBuffer
{
    QWebGPUCommandBuffer(QRhiImplementation *rhi);
    ~QWebGPUCommandBuffer();
    void destroy() override;

    WGPUCommandEncoder commandEncoder = nullptr;
    WGPURenderPassEncoder renderPassEncoder = nullptr;
    WGPUComputePassEncoder computePassEncoder = nullptr;

    enum PassType {
        NoPass,
        RenderPass,
        ComputePass
    };

    PassType recordingPass = NoPass;
    QRhiRenderTarget *currentTarget = nullptr;

    QWebGPUGraphicsPipeline *currentGraphicsPipeline = nullptr;
    QWebGPUComputePipeline *currentComputePipeline = nullptr;
    uint currentPipelineGeneration = 0;
    QWebGPUShaderResourceBindings *currentGraphicsSrb = nullptr;
    QWebGPUShaderResourceBindings *currentComputeSrb = nullptr;
    uint currentSrbGeneration = 0;

    void resetState();
    void resetPerPassState();
    void resetPerPassCachedState();
};

struct QWebGPUSwapChain : public QRhiSwapChain
{
    QWebGPUSwapChain(QRhiImplementation *rhi);
    ~QWebGPUSwapChain();
    void destroy() override;

    QRhiCommandBuffer *currentFrameCommandBuffer() override;
    QRhiRenderTarget *currentFrameRenderTarget() override;
    QSize surfacePixelSize() override;
    bool isFormatSupported(Format f) override;

    QRhiRenderPassDescriptor *newCompatibleRenderPassDescriptor() override;
    bool createOrResize() override;

    QWindow *window = nullptr;
    QSize pixelSize;
    int currentFrameSlot = 0;
    int frameCount = 0;
    int samples = 1;
    QWebGPUSwapChainRenderTarget rtWrapper;
    QWebGPUCommandBuffer cbWrapper;

    // WebGPU surface
    WGPUSurface surface = nullptr;
    WGPUTexture currentTexture = nullptr;
    WGPUTextureView currentTextureView = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;

    // MSAA color texture (when samples > 1)
    WGPUTexture msaaTexture = nullptr;
    WGPUTextureView msaaTextureView = nullptr;
};

class QRhiWebGPU : public QRhiImplementation
{
public:
    QRhiWebGPU(QRhiWebGPUInitParams *params, QRhiWebGPUNativeHandles *importDevice = nullptr);
    ~QRhiWebGPU();

    bool create(QRhi::Flags flags) override;
    void destroy() override;

    QRhiGraphicsPipeline *createGraphicsPipeline() override;
    QRhiComputePipeline *createComputePipeline() override;
    QRhiShaderResourceBindings *createShaderResourceBindings() override;
    QRhiBuffer *createBuffer(QRhiBuffer::Type type,
                             QRhiBuffer::UsageFlags usage,
                             quint32 size) override;
    QRhiRenderBuffer *createRenderBuffer(QRhiRenderBuffer::Type type,
                                         const QSize &pixelSize,
                                         int sampleCount,
                                         QRhiRenderBuffer::Flags flags,
                                         QRhiTexture::Format backingFormatHint) override;
    QRhiTexture *createTexture(QRhiTexture::Format format,
                               const QSize &pixelSize,
                               int depth,
                               int arraySize,
                               int sampleCount,
                               QRhiTexture::Flags flags) override;
    QRhiSampler *createSampler(QRhiSampler::Filter magFilter,
                               QRhiSampler::Filter minFilter,
                               QRhiSampler::Filter mipmapMode,
                               QRhiSampler::AddressMode u,
                               QRhiSampler::AddressMode v,
                               QRhiSampler::AddressMode w) override;

    QRhiTextureRenderTarget *createTextureRenderTarget(const QRhiTextureRenderTargetDescription &desc,
                                                       QRhiTextureRenderTarget::Flags flags) override;

    QRhiShadingRateMap *createShadingRateMap() override;

    QRhiSwapChain *createSwapChain() override;
    QRhi::FrameOpResult beginFrame(QRhiSwapChain *swapChain, QRhi::BeginFrameFlags flags) override;
    QRhi::FrameOpResult endFrame(QRhiSwapChain *swapChain, QRhi::EndFrameFlags flags) override;
    QRhi::FrameOpResult beginOffscreenFrame(QRhiCommandBuffer **cb, QRhi::BeginFrameFlags flags) override;
    QRhi::FrameOpResult endOffscreenFrame(QRhi::EndFrameFlags flags) override;
    QRhi::FrameOpResult finish() override;

    void resourceUpdate(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *resourceUpdates) override;

    void beginPass(QRhiCommandBuffer *cb,
                   QRhiRenderTarget *rt,
                   const QColor &colorClearValue,
                   const QRhiDepthStencilClearValue &depthStencilClearValue,
                   QRhiResourceUpdateBatch *resourceUpdates,
                   QRhiCommandBuffer::BeginPassFlags flags) override;
    void endPass(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *resourceUpdates) override;

    void setGraphicsPipeline(QRhiCommandBuffer *cb,
                             QRhiGraphicsPipeline *ps) override;

    void setShaderResources(QRhiCommandBuffer *cb,
                            QRhiShaderResourceBindings *srb,
                            int dynamicOffsetCount,
                            const QRhiCommandBuffer::DynamicOffset *dynamicOffsets) override;

    void setVertexInput(QRhiCommandBuffer *cb,
                        int startBinding, int bindingCount, const QRhiCommandBuffer::VertexInput *bindings,
                        QRhiBuffer *indexBuf, quint32 indexOffset,
                        QRhiCommandBuffer::IndexFormat indexFormat) override;

    void setViewport(QRhiCommandBuffer *cb, const QRhiViewport &viewport) override;
    void setScissor(QRhiCommandBuffer *cb, const QRhiScissor &scissor) override;
    void setBlendConstants(QRhiCommandBuffer *cb, const QColor &c) override;
    void setStencilRef(QRhiCommandBuffer *cb, quint32 refValue) override;
    void setShadingRate(QRhiCommandBuffer *cb, const QSize &coarsePixelSize) override;

    void draw(QRhiCommandBuffer *cb, quint32 vertexCount,
              quint32 instanceCount, quint32 firstVertex, quint32 firstInstance) override;

    void drawIndexed(QRhiCommandBuffer *cb, quint32 indexCount,
                     quint32 instanceCount, quint32 firstIndex,
                     qint32 vertexOffset, quint32 firstInstance) override;

    void drawIndirect(QRhiCommandBuffer *cb, QRhiBuffer *indirectBuffer,
                      quint32 offset, quint32 drawCount, quint32 stride) override;
    void drawIndexedIndirect(QRhiCommandBuffer *cb, QRhiBuffer *indirectBuffer,
                             quint32 offset, quint32 drawCount, quint32 stride) override;
    void drawIndirectCount(QRhiCommandBuffer *cb,
                           QRhiBuffer *indirectBuffer, quint32 indirectBufferOffset,
                           QRhiBuffer *countBuffer, quint32 countBufferOffset,
                           quint32 maxDrawCount, quint32 stride) override;
    void drawIndexedIndirectCount(QRhiCommandBuffer *cb,
                                  QRhiBuffer *indirectBuffer, quint32 indirectBufferOffset,
                                  QRhiBuffer *countBuffer, quint32 countBufferOffset,
                                  quint32 maxDrawCount, quint32 stride) override;

    void debugMarkBegin(QRhiCommandBuffer *cb, const QByteArray &name) override;
    void debugMarkEnd(QRhiCommandBuffer *cb) override;
    void debugMarkMsg(QRhiCommandBuffer *cb, const QByteArray &msg) override;

    void beginComputePass(QRhiCommandBuffer *cb,
                          QRhiResourceUpdateBatch *resourceUpdates,
                          QRhiCommandBuffer::BeginPassFlags flags) override;
    void endComputePass(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *resourceUpdates) override;
    void setComputePipeline(QRhiCommandBuffer *cb, QRhiComputePipeline *ps) override;
    void dispatch(QRhiCommandBuffer *cb, int x, int y, int z) override;
    void dispatchIndirect(QRhiCommandBuffer *cb, QRhiBuffer *indirectBuffer,
                          quint32 offset) override;

    const QRhiNativeHandles *nativeHandles(QRhiCommandBuffer *cb) override;
    void beginExternal(QRhiCommandBuffer *cb) override;
    void endExternal(QRhiCommandBuffer *cb) override;
    double lastCompletedGpuTime(QRhiCommandBuffer *cb) override;

    QList<int> supportedSampleCounts() const override;
    QList<QSize> supportedShadingRates(int sampleCount) const override;
    int ubufAlignment() const override;
    bool isYUpInFramebuffer() const override;
    bool isYUpInNDC() const override;
    bool isClipDepthZeroToOne() const override;
    QMatrix4x4 clipSpaceCorrMatrix() const override;
    bool isTextureFormatSupported(QRhiTexture::Format format, QRhiTexture::Flags flags) const override;
    bool isFeatureSupported(QRhi::Feature feature) const override;
    int resourceLimit(QRhi::ResourceLimit limit) const override;
    const QRhiNativeHandles *nativeHandles() override;
    QRhiDriverInfo driverInfo() const override;
    QRhiStats statistics() override;
    bool makeThreadLocalNativeContextCurrent() override;
    void setQueueSubmitParams(QRhiNativeHandles *params) override;
    void releaseCachedResources() override;
    bool isDeviceLost() const override;

    QByteArray pipelineCacheData() override;
    void setPipelineCacheData(const QByteArray &data) override;

    // Internal helpers
    void executeDeferredReleases(bool forced = false);
    void finishActiveReadbacks(bool forced = false);
    void enqueueResourceUpdates(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *resourceUpdates);

    static WGPUTextureFormat toWgpuTextureFormat(QRhiTexture::Format format, QRhiTexture::Flags flags);
    static WGPUVertexFormat toWgpuVertexFormat(QRhiVertexInputAttribute::Format format);
    static WGPUPrimitiveTopology toWgpuPrimitiveTopology(QRhiGraphicsPipeline::Topology t);
    static WGPUCullMode toWgpuCullMode(QRhiGraphicsPipeline::CullMode c);
    static WGPUFrontFace toWgpuFrontFace(QRhiGraphicsPipeline::FrontFace f);
    static WGPUBlendFactor toWgpuBlendFactor(QRhiGraphicsPipeline::BlendFactor f);
    static WGPUBlendOperation toWgpuBlendOp(QRhiGraphicsPipeline::BlendOp op);
    static WGPUCompareFunction toWgpuCompareOp(QRhiGraphicsPipeline::CompareOp op);
    static WGPUStencilOperation toWgpuStencilOp(QRhiGraphicsPipeline::StencilOp op);

    QRhi::Flags rhiFlags;
    bool importedDevice = false;
    QRhiWebGPUNativeHandles nativeHandlesStruct;
    QRhiDriverInfo driverInfoStruct;

    // WebGPU core objects
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;

    QWebGPUSwapChain *currentSwapChain = nullptr;
    QSet<QWebGPUSwapChain *> swapchains;
    QWebGPUCommandBuffer offscreenCommandBuffer;

    struct {
        WGPULimits limits = WGPU_LIMITS_INIT; // populated from adapter after device creation
        QList<int> supportedSampleCounts = { 1, 4 };
        bool hasFloat32Filterable = false;
        bool hasTextureCompressionBC = false;
        bool hasTextureCompressionETC2 = false;
        bool hasTextureCompressionASTC = false;
        bool hasTimestampQuery = false;
        bool hasDepth32FloatStencil8 = false;
        bool hasIndirectFirstInstance = false;
        bool hasDualSourceBlending = false;
        bool hasDepthClipControl = false;
        bool hasShaderF16 = false;
    } caps;

    struct TextureReadback {
        QRhiReadbackDescription desc;
        QRhiReadbackResult *result;
        WGPUBuffer stagingBuffer = nullptr;
        quint32 bufferSize = 0;
        QSize pixelSize;
        QRhiTexture::Format format;
        // Non-null for swapchain readbacks (desc.texture() == nullptr case)
        WGPUTexture sourceTexture = nullptr;
    };
    QVarLengthArray<TextureReadback, 2> activeTextureReadbacks;
};

QT_END_NAMESPACE

#endif
