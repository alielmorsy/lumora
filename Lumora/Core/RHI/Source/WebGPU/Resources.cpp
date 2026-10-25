#include "WebGPURHI.h"

#include <algorithm>
#include <cstring>

LUMORA_LOG_CATEGORY("RHI");

namespace lumora::rhi {

namespace {

constexpr uint64_t Align4(uint64_t v) noexcept
{
    return (v + 3) & ~3ull;
}

// writeBuffer needs 4-byte aligned offset and size; buffers are created with a size rounded up to 4.
void WriteBuffer(detail::NativeRHI& n, const detail::Buffer& b, uint64_t offset, const void* data, uint64_t size)
{
    const uint64_t begin = offset & ~3ull;
    const uint64_t end   = std::min(Align4(offset + size), Align4(b.desc.size));
    if (end <= begin)
        return;
    if (begin == offset && end == offset + size) {
        wgpuQueueWriteBuffer(n.queue, b.buffer, begin, data, size);
        return;
    }
    std::vector<std::byte> tmp(end - begin);
    if (!b.shadow.empty())
        std::memcpy(tmp.data(), b.shadow.data() + begin, tmp.size());
    std::memcpy(tmp.data() + (offset - begin), data, std::min<uint64_t>(size, tmp.size() - (offset - begin)));
    wgpuQueueWriteBuffer(n.queue, b.buffer, begin, tmp.data(), tmp.size());
}

void Upload(detail::NativeRHI& n, std::span<const UploadRequest> requests)
{
    for (const UploadRequest& req : requests) {
        if (!req.data || req.size == 0)
            continue;
        if (req.dstBuffer.IsValid()) {
            if (const detail::Buffer* b = n.buffers.Get(req.dstBuffer))
                WriteBuffer(n, *b, req.dstOffset, req.data, req.size);
            continue;
        }
        const detail::Texture* t = n.textures.Get(req.dstTexture);
        if (!t)
            continue;
        const TextureUploadRegion& reg = req.textureRegion;
        Extent3D extent = reg.extent;
        if (extent.width == 0 || extent.height == 0)
            extent = {std::max(1u, t->desc.extent.width >> reg.mipLevel),
                      std::max(1u, t->desc.extent.height >> reg.mipLevel), 1};

        WGPUTexelCopyTextureInfo dst{};
        dst.texture  = t->texture;
        dst.mipLevel = reg.mipLevel;
        dst.origin   = {static_cast<uint32_t>(reg.offset.x), static_cast<uint32_t>(reg.offset.y),
                        static_cast<uint32_t>(reg.offset.z) + reg.baseLayer};
        dst.aspect   = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow  = extent.width * detail::BytesPerPixel(t->desc.format);
        layout.rowsPerImage = extent.height;
        const WGPUExtent3D size{extent.width, extent.height,
                                std::max(1u, extent.depth) * std::max(1u, reg.layerCount)};
        wgpuQueueWriteTexture(n.queue, &dst, req.data, req.size, &layout, &size);
    }
}

} // namespace

BufferHandle RHIResources::CreateBuffer(const BufferDescription& desc)
{
    if (!_n)
        return {};
    if (desc.memory == MemoryPattern::GpuToCpu) {
        LOG_ERROR("GpuToCpu buffers are not supported on WebGPU");
        return {};
    }
    detail::NativeRHI& n = *_n;
    WGPUBufferDescriptor bd{};
    bd.label = detail::Str(desc.debugName);
    bd.usage = detail::ToWgpu(desc.usage);
    bd.size  = Align4(std::max<uint64_t>(desc.size, 4));

    detail::Buffer b{};
    b.buffer = wgpuDeviceCreateBuffer(n.device, &bd);
    if (desc.memory == MemoryPattern::CpuToGpuMapped)
        b.shadow.assign(bd.size, std::byte{0});
    b.desc           = desc;
    b.desc.debugName = {};
    return n.buffers.Add(std::move(b));
}

TextureHandle RHIResources::CreateTexture(const TextureDescription& desc)
{
    if (!_n)
        return {};
    detail::NativeRHI& n = *_n;
    const uint32_t layers = std::max(1u, desc.arrayLayers);

    WGPUTextureDescriptor td{};
    td.label         = detail::Str(desc.debugName);
    td.usage         = detail::ToWgpu(desc.usage);
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = {desc.extent.width, desc.extent.height, layers};
    td.format        = detail::ToWgpu(desc.format);
    td.mipLevelCount = std::max(1u, desc.mipLevels);
    td.sampleCount   = static_cast<uint32_t>(desc.samples);
    WGPUTexture texture = wgpuDeviceCreateTexture(n.device, &td);

    WGPUTextureViewDescriptor vd{};
    vd.label           = td.label;
    vd.format          = td.format;
    vd.dimension       = detail::ViewDimension(desc.type, layers);
    vd.mipLevelCount   = td.mipLevelCount;
    vd.arrayLayerCount = layers;
    vd.aspect          = WGPUTextureAspect_All;

    detail::Texture t{};
    t.texture        = texture;
    t.desc           = desc;
    t.desc.debugName = {};
    const TextureHandle th = n.textures.Add(std::move(t));
    n.textures.Get(th)->defaultView = n.views.Add(detail::TextureView{wgpuTextureCreateView(texture, &vd), th});
    return th;
}

SamplerHandle RHIResources::CreateSampler(const SamplerDescription& desc)
{
    if (!_n)
        return {};
    WGPUSamplerDescriptor sd{};
    sd.label         = detail::Str(desc.debugName);
    sd.addressModeU  = detail::ToWgpu(desc.addressU);
    sd.addressModeV  = detail::ToWgpu(desc.addressV);
    sd.addressModeW  = detail::ToWgpu(desc.addressW);
    sd.magFilter     = detail::ToWgpu(desc.magFilter);
    sd.minFilter     = detail::ToWgpu(desc.minFilter);
    sd.mipmapFilter  = detail::ToWgpu(desc.mipmapMode);
    sd.lodMinClamp   = desc.minLod;
    sd.lodMaxClamp   = std::max(desc.minLod, desc.maxLod);
    sd.maxAnisotropy = 1;
    // WebGPU only allows anisotropy with every filter linear.
    if (desc.anisotropyEnable && desc.magFilter == Filter::Linear && desc.minFilter == Filter::Linear &&
        desc.mipmapMode == MipmapMode::Linear)
        sd.maxAnisotropy = static_cast<uint16_t>(std::clamp(desc.maxAnisotropy, 1.f, 16.f));
    return _n->samplers.Add(detail::Sampler{wgpuDeviceCreateSampler(_n->device, &sd)});
}

TextureViewHandle RHIResources::GetDefaultView(TextureHandle texture)
{
    if (!_n)
        return {};
    if (const detail::Texture* t = _n->textures.Get(texture))
        return t->defaultView;
    return {};
}

void* RHIResources::GetMappedPointer(BufferHandle buffer) const
{
    if (!_n)
        return nullptr;
    detail::Buffer* b = _n->buffers.Get(buffer);
    return b && !b->shadow.empty() ? b->shadow.data() : nullptr;
}

// GpuToCpu readback is not supported on WebGPU yet (see CommandBuffer::CopyImageToBuffer).
void RHIResources::InvalidateMapped(BufferHandle, uint64_t, uint64_t) {}

void RHIResources::FlushMapped(BufferHandle buffer, uint64_t offset, uint64_t size)
{
    if (!_n)
        return;
    const detail::Buffer* b = _n->buffers.Get(buffer);
    if (!b || b->shadow.empty() || offset >= b->shadow.size())
        return;
    size = std::min<uint64_t>(size, b->shadow.size() - offset);
    WriteBuffer(*_n, *b, offset, b->shadow.data() + offset, size);
}

TextureDescription RHIResources::GetTextureDescription(TextureHandle texture) const
{
    if (!_n)
        return {};
    if (const detail::Texture* t = _n->textures.Get(texture))
        return t->desc;
    return {};
}

void RHIResources::Destroy(BufferHandle h)
{
    if (!_n)
        return;
    detail::NativeRHI& n = *_n;
    detail::Buffer b = n.buffers.Remove(h);
    if (!b.buffer)
        return;
    n.ClearBindGroups();
    n.Defer([buffer = b.buffer]() {
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
    });
}

void RHIResources::Destroy(TextureHandle h)
{
    if (!_n)
        return;
    detail::NativeRHI& n = *_n;
    detail::Texture t = n.textures.Remove(h);
    if (!t.texture)
        return;
    n.ClearBindGroups();
    n.Defer([&n, t]() mutable { n.DestroyTextureNow(t, true); });
}

void RHIResources::Destroy(SamplerHandle h)
{
    if (!_n)
        return;
    detail::NativeRHI& n = *_n;
    detail::Sampler s = n.samplers.Remove(h);
    if (!s.sampler)
        return;
    n.ClearBindGroups();
    n.Defer([sampler = s.sampler]() { wgpuSamplerRelease(sampler); });
}

void RHIUploads::UploadBlocking(std::span<const UploadRequest> requests)
{
    if (_n)
        Upload(*_n, requests);
}

void RHIUploads::UpdateBlocking(std::span<const UploadRequest> requests)
{
    if (_n)
        Upload(*_n, requests);
}

} // namespace lumora::rhi
