#include <lumora/rhi/CommandBuffer.h>

#include "WebGPURHI.h"

#include <algorithm>
#include <cstring>

LUMORA_LOG_CATEGORY("RHI");

namespace lumora::rhi {

namespace {

#define LOG_ONCE(...)                       \
    do {                                    \
        static bool s_logged = false;       \
        if (!s_logged) {                    \
            s_logged = true;                \
            LOG_WARN(__VA_ARGS__);          \
        }                                   \
    } while (false)

detail::Recorder* Rec(void* cmd)
{
    return static_cast<detail::Recorder*>(cmd);
}

WGPURenderPassEncoder Pass(void* cmd)
{
    detail::Recorder* r = Rec(cmd);
    if (!r || !r->pass) {
        LOG_ONCE("WebGPU: draw state set outside BeginRendering/EndRendering");
        return nullptr;
    }
    return r->pass;
}

const detail::TextureView* ResolveView(detail::NativeRHI& rhi, TextureHandle texture, TextureViewHandle view)
{
    if (view.IsValid())
        return rhi.views.Get(view);
    const detail::Texture* t = rhi.textures.Get(texture);
    return t ? rhi.views.Get(t->defaultView) : nullptr;
}

uint64_t Key(uint32_t index, uint16_t generation)
{
    return (static_cast<uint64_t>(index) << 16) | generation;
}

// Uploads the current push block into a new ring slice and binds it at the group after the sets.
void FlushPush(detail::NativeRHI& rhi, detail::Recorder& r, PipelineHandle bound, uint32_t frameSlot)
{
    if (!r.pushDirty || !r.pass)
        return;
    r.pushDirty = false;
    const detail::Pipeline* p = rhi.pipelines.Get(bound);
    if (!p || !p->pushSize)
        return;
    detail::PushRing& ring = rhi.frames[frameSlot].push;
    const uint32_t offset  = rhi.PushSlice(ring, r.push.data(), p->pushSize);
    wgpuRenderPassEncoderSetBindGroup(r.pass, static_cast<uint32_t>(p->setLayouts.size()), rhi.PushGroup(ring, p->pushSize),
                                      1, &offset);
}

} // namespace

void CommandBuffer::Barrier(std::span<const TextureBarrier>, std::span<const BufferBarrier>) {}

void CommandBuffer::TransitionTexture(TextureHandle, ImageLayout, ImageLayout) {}

void CommandBuffer::BeginRendering(const RenderingInfo& info)
{
    detail::Recorder* r = Rec(_cmd);
    if (!_rhi || !r || r->pass)
        return;

    std::vector<WGPURenderPassColorAttachment> atts;
    for (const ColorAttachment& att : info.colorAttachments) {
        const detail::TextureView* tv = ResolveView(*_rhi, att.texture, att.view);
        if (!tv) {
            LOG_ONCE("Invalid color attachment ({}, {})", att.view.index, att.view.generation);
            continue;
        }
        if (const detail::Texture* t = _rhi->textures.Get(tv->texture))
            r->passExtent = {t->desc.extent.width, t->desc.extent.height};
        WGPURenderPassColorAttachment a{};
        a.view       = tv->view;
        a.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        a.loadOp     = att.loadOp == LoadOp::Load ? WGPULoadOp_Load : WGPULoadOp_Clear;
        a.storeOp    = att.storeOp == StoreOp::Store ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
        a.clearValue = {att.clearColor.r, att.clearColor.g, att.clearColor.b, att.clearColor.a};
        atts.push_back(a);
    }

    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = atts.size();
    rp.colorAttachments     = atts.data();
    r->pass                 = wgpuCommandEncoderBeginRenderPass(r->encoder, &rp);
    r->pushDirty            = true;
}

void CommandBuffer::EndRendering()
{
    detail::Recorder* r = Rec(_cmd);
    if (!r || !r->pass)
        return;
    wgpuRenderPassEncoderEnd(r->pass);
    wgpuRenderPassEncoderRelease(r->pass);
    r->pass = nullptr;
}

void CommandBuffer::SetViewport(const Viewport& vp)
{
    if (WGPURenderPassEncoder pass = Pass(_cmd))
        wgpuRenderPassEncoderSetViewport(pass, vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth);
}

// WebGPU rejects scissors outside the attachment, so clamp to it.
void CommandBuffer::SetScissor(const Rect2D& rect)
{
    WGPURenderPassEncoder pass = Pass(_cmd);
    if (!pass)
        return;
    const Extent2D e  = Rec(_cmd)->passExtent;
    const int64_t  x0 = std::clamp<int64_t>(rect.offset.x, 0, e.width);
    const int64_t  y0 = std::clamp<int64_t>(rect.offset.y, 0, e.height);
    const int64_t  x1 = std::clamp<int64_t>(int64_t(rect.offset.x) + rect.extent.width, x0, e.width);
    const int64_t  y1 = std::clamp<int64_t>(int64_t(rect.offset.y) + rect.extent.height, y0, e.height);
    wgpuRenderPassEncoderSetScissorRect(pass, uint32_t(x0), uint32_t(y0), uint32_t(x1 - x0), uint32_t(y1 - y0));
}

void CommandBuffer::BindPipeline(PipelineHandle pipeline)
{
    WGPURenderPassEncoder pass = Pass(_cmd);
    if (!_rhi || !pass)
        return;
    detail::Pipeline* p = _rhi->pipelines.Get(pipeline);
    if (!p) {
        LOG_ONCE("Invalid pipeline handle: ({}, {})", pipeline.index, pipeline.generation);
        return;
    }
    _bound = pipeline;
    wgpuRenderPassEncoderSetPipeline(pass, p->pipeline);
    detail::Recorder* r = Rec(_cmd);
    r->push.fill(std::byte{0});
    r->pushDirty = true;
}

void CommandBuffer::BindVertexBuffer(uint32_t binding, BufferHandle buffer, uint64_t offset)
{
    WGPURenderPassEncoder pass = Pass(_cmd);
    if (!_rhi || !pass)
        return;
    if (detail::Buffer* b = _rhi->buffers.Get(buffer))
        wgpuRenderPassEncoderSetVertexBuffer(pass, binding, b->buffer, offset, WGPU_WHOLE_SIZE);
}

void CommandBuffer::BindIndexBuffer(BufferHandle buffer, uint64_t offset, IndexType type)
{
    WGPURenderPassEncoder pass = Pass(_cmd);
    if (!_rhi || !pass)
        return;
    if (detail::Buffer* b = _rhi->buffers.Get(buffer))
        wgpuRenderPassEncoderSetIndexBuffer(pass, b->buffer,
                                            type == IndexType::Uint16 ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32,
                                            offset, WGPU_WHOLE_SIZE);
}

void CommandBuffer::PushConstants(ShaderStageFlags, uint32_t offset, std::span<const std::byte> data)
{
    detail::Recorder* r = Rec(_cmd);
    if (!r || data.empty() || offset >= r->push.size())
        return;
    std::memcpy(r->push.data() + offset, data.data(), std::min(data.size(), r->push.size() - offset));
    r->pushDirty = true;
}

void CommandBuffer::PushDescriptors(uint32_t set, std::span<const DescriptorWrite> writes)
{
    WGPURenderPassEncoder pass = Pass(_cmd);
    if (!_rhi || !pass || writes.empty())
        return;
    detail::Pipeline* p = _rhi->pipelines.Get(_bound);
    if (!p || set >= p->setLayouts.size()) {
        LOG_ONCE("PushDescriptors: no bound pipeline or set {} out of range", set);
        return;
    }
    const DescriptorLayoutHandle lh = p->setLayouts[set];
    detail::DescriptorLayout* layout = _rhi->descriptorLayouts.Get(lh);
    if (!layout)
        return;

    std::vector<uint64_t> key{Key(lh.index, lh.generation)};
    for (const DescriptorWrite& w : writes) {
        TextureViewHandle vh = w.imageInfo.view;
        if (!vh.IsValid())
            if (const detail::Texture* t = _rhi->textures.Get(w.imageInfo.texture))
                vh = t->defaultView;
        const SamplerHandle sh = w.samplerOnly.IsValid() ? w.samplerOnly : w.imageInfo.sampler;
        key.insert(key.end(), {uint64_t(w.binding) << 8 | uint64_t(w.type), Key(vh.index, vh.generation),
                               Key(sh.index, sh.generation),
                               Key(w.bufferInfo.buffer.index, w.bufferInfo.buffer.generation), w.bufferInfo.offset,
                               w.bufferInfo.range});
    }

    WGPUBindGroup& group = _rhi->bindGroups[key];
    if (!group) {
        std::vector<WGPUBindGroupEntry> entries;
        for (size_t i = 0; i < writes.size(); ++i) {
            const DescriptorWrite& w = writes[i];
            const uint64_t*        k = key.data() + 1 + i * 6;
            WGPUBindGroupEntry     e{};
            e.binding = w.binding;
            const auto view = [&]() {
                const detail::TextureView* tv =
                    _rhi->views.Get(TextureViewHandle{uint32_t(k[1] >> 16), uint16_t(k[1] & 0xFFFF)});
                return tv ? tv->view : nullptr;
            };
            const auto sampler = [&]() {
                const detail::Sampler* s =
                    _rhi->samplers.Get(SamplerHandle{uint32_t(k[2] >> 16), uint16_t(k[2] & 0xFFFF)});
                return s ? s->sampler : nullptr;
            };
            switch (w.type) {
            case DescriptorType::CombinedImageSampler:
                e.binding     = 2 * w.binding;
                e.textureView = view();
                entries.push_back(e);
                e             = {};
                e.binding     = 2 * w.binding + 1;
                e.sampler     = sampler();
                break;
            case DescriptorType::Sampler: e.sampler = sampler(); break;
            case DescriptorType::SampledImage:
            case DescriptorType::StorageImage: e.textureView = view(); break;
            case DescriptorType::UniformBuffer:
            case DescriptorType::StorageBuffer:
                if (const detail::Buffer* b = _rhi->buffers.Get(w.bufferInfo.buffer)) {
                    e.buffer = b->buffer;
                    e.offset = w.bufferInfo.offset;
                    e.size   = w.bufferInfo.range == ~0ull ? WGPU_WHOLE_SIZE : w.bufferInfo.range;
                }
                break;
            }
            entries.push_back(e);
        }
        WGPUBindGroupDescriptor bd{};
        bd.layout     = layout->layout;
        bd.entryCount = entries.size();
        bd.entries    = entries.data();
        group         = wgpuDeviceCreateBindGroup(_rhi->device, &bd);
    }
    wgpuRenderPassEncoderSetBindGroup(pass, set, group, 0, nullptr);

    // ponytail: wholesale clear bounds the cache; per-resource eviction if UIs churn past it.
    if (_rhi->bindGroups.size() > 4096)
        _rhi->ClearBindGroups();
}

void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
    WGPURenderPassEncoder pass = Pass(_cmd);
    if (!_rhi || !pass)
        return;
    FlushPush(*_rhi, *Rec(_cmd), _bound, _frameSlot);
    wgpuRenderPassEncoderDraw(pass, vertexCount, instanceCount, firstVertex, firstInstance);
}

void CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset,
                                uint32_t firstInstance)
{
    WGPURenderPassEncoder pass = Pass(_cmd);
    if (!_rhi || !pass)
        return;
    FlushPush(*_rhi, *Rec(_cmd), _bound, _frameSlot);
    wgpuRenderPassEncoderDrawIndexed(pass, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

// bufferRowLength * bytes per pixel must be a multiple of 256 here (a WebGPU rule).
void CommandBuffer::CopyBufferToImage(BufferHandle src, TextureHandle dst, ImageLayout,
                                      std::span<const BufferImageCopy> regions)
{
    detail::Recorder* r = Rec(_cmd);
    if (!_rhi || !r || r->pass)
        return;
    const detail::Buffer*  b = _rhi->buffers.Get(src);
    const detail::Texture* t = _rhi->textures.Get(dst);
    if (!b || !t)
        return;
    for (const BufferImageCopy& c : regions) {
        WGPUTexelCopyBufferInfo from{};
        from.buffer              = b->buffer;
        from.layout.offset       = c.bufferOffset;
        from.layout.bytesPerRow  = (c.bufferRowLength ? c.bufferRowLength : c.imageExtent.width) *
                                  detail::BytesPerPixel(t->desc.format);
        from.layout.rowsPerImage = c.bufferImageHeight ? c.bufferImageHeight : c.imageExtent.height;
        WGPUTexelCopyTextureInfo to{};
        to.texture  = t->texture;
        to.mipLevel = c.mipLevel;
        to.origin   = {uint32_t(c.imageOffset.x), uint32_t(c.imageOffset.y), uint32_t(c.imageOffset.z) + c.baseArrayLayer};
        to.aspect   = WGPUTextureAspect_All;
        const WGPUExtent3D size{c.imageExtent.width, c.imageExtent.height,
                                std::max(1u, c.imageExtent.depth) * std::max(1u, c.layerCount)};
        wgpuCommandEncoderCopyBufferToTexture(r->encoder, &from, &to, &size);
    }
}

// ponytail: GpuToCpu buffers (the only valid readback target) are not supported on WebGPU yet, so this is a stub.
// Upgrade: wgpuCommandEncoderCopyTextureToBuffer with 256-byte aligned bytesPerRow and MapAsync readback.
void CommandBuffer::CopyImageToBuffer(TextureHandle, ImageLayout, BufferHandle, std::span<const BufferImageCopy>)
{
    LOG_ERROR("CopyImageToBuffer is not supported on WebGPU");
}

// Labels only go on the command encoder outside passes, so groups never straddle a pass boundary.
void CommandBuffer::BeginDebugLabel(std::string_view name, Color)
{
    detail::Recorder* r = Rec(_cmd);
    if (!_rhi || !r || !_rhi->debugLabels)
        return;
    const bool push = !r->pass;
    if (push)
        wgpuCommandEncoderPushDebugGroup(r->encoder, detail::Str(name));
    r->labels.push_back(push);
}

void CommandBuffer::EndDebugLabel()
{
    detail::Recorder* r = Rec(_cmd);
    if (!_rhi || !r || r->labels.empty())
        return;
    const bool pushed = r->labels.back();
    r->labels.pop_back();
    if (pushed && !r->pass)
        wgpuCommandEncoderPopDebugGroup(r->encoder);
}

} // namespace lumora::rhi
