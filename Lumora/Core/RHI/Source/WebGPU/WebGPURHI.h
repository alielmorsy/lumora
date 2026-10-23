#pragma once

// Internal backend state shared by every WebGPU source file. Not installed.

#include <lumora/rhi/RHI.h>

#include <webgpu/webgpu.h>

#include <lumora/log/Log.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string_view>
#include <vector>

namespace lumora::rhi::detail {

// Generational slot pool. Index 0 is never handed out, so a default handle is invalid.
template <class H, class T>
struct Pool {
    struct Slot {
        T        obj{};
        uint16_t generation = 0;
        bool     live       = false;
    };
    std::vector<Slot>     slots{Slot{}};
    std::vector<uint32_t> freeList;

    H Add(T obj)
    {
        uint32_t i;
        if (!freeList.empty()) {
            i = freeList.back();
            freeList.pop_back();
        } else {
            i = static_cast<uint32_t>(slots.size());
            slots.emplace_back();
        }
        Slot& s = slots[i];
        s.obj   = std::move(obj);
        s.live  = true;
        if (++s.generation == 0)
            s.generation = 1;
        return H{i, s.generation};
    }

    T* Get(H h)
    {
        if (h.index == 0 || h.index >= slots.size())
            return nullptr;
        Slot& s = slots[h.index];
        return (s.live && s.generation == h.generation) ? &s.obj : nullptr;
    }

    T Remove(H h)
    {
        T* p = Get(h);
        if (!p)
            return T{};
        T out = std::move(*p);
        slots[h.index].obj  = T{};
        slots[h.index].live = false;
        freeList.push_back(h.index);
        return out;
    }

    template <class F>
    void ForEachLive(F&& f)
    {
        for (Slot& s : slots)
            if (s.live)
                f(s.obj);
    }
};

inline WGPUStringView Str(std::string_view s) noexcept
{
    return {s.data(), s.size()};
}

inline std::string_view Str(WGPUStringView s) noexcept
{
    if (!s.data)
        return {};
    return s.length == WGPU_STRLEN ? std::string_view(s.data) : std::string_view(s.data, s.length);
}

struct Texture {
    WGPUTexture        texture = nullptr;
    TextureDescription desc{};
    TextureViewHandle  defaultView{};
};

struct TextureView {
    WGPUTextureView view = nullptr;
    TextureHandle   texture{};
};

struct Buffer {
    WGPUBuffer             buffer = nullptr;
    std::vector<std::byte> shadow; // CpuToGpuMapped: what GetMappedPointer hands out
    BufferDescription      desc{};
};

struct Sampler {
    WGPUSampler sampler = nullptr;
};

struct DescriptorLayout {
    WGPUBindGroupLayout            layout = nullptr;
    std::vector<DescriptorBinding> bindings;
};

struct Pipeline {
    WGPURenderPipeline                  pipeline = nullptr;
    std::vector<DescriptorLayoutHandle> setLayouts;
    uint32_t                            pushSize = 0; // bound size of the push block at group setLayouts.size()
};

struct Swapchain {
    WGPUSurface       surface = nullptr;
    WGPUTextureFormat wgpuFormat = WGPUTextureFormat_Undefined;
    Format            format  = Format::Undefined;
    Extent2D          extent{};
    TextureHandle     current{}; // surface texture of the frame being recorded
};

// Push constants live in 256-byte slices of a per-slot uniform buffer, written into `shadow` while
// recording and uploaded once before submit.
struct PushRing {
    WGPUBuffer                                  buffer = nullptr;
    std::vector<std::byte>                      shadow;
    uint64_t                                    used = 0;
    std::vector<std::pair<uint32_t, WGPUBindGroup>> groups; // by bound size
};

// What CommandBuffer::_cmd points at.
struct Recorder {
    WGPUCommandEncoder        encoder = nullptr;
    WGPURenderPassEncoder     pass    = nullptr;
    Extent2D                  passExtent{};
    std::array<std::byte, 256> push{};
    bool                      pushDirty = false;
    std::vector<bool>         labels; // per open label: was it actually pushed
};

struct Frame {
    PushRing                       push;
    Recorder                       rec;
    std::unique_ptr<CommandBuffer> recorder;
};

struct NativeRHI {
    WGPUInstance        instance = nullptr;
    WGPUAdapter         adapter  = nullptr;
    WGPUDevice          device   = nullptr;
    WGPUQueue           queue    = nullptr;
    WGPUBindGroupLayout pushLayout = nullptr;
    AdapterInfo         info{};
    bool                debugLabels = false;

    std::vector<Frame> frames;
    uint32_t           frameSlot   = 0;
    uint64_t           frameNumber = 0;

    struct Deferred {
        uint64_t              frame;
        std::function<void()> fn;
    };
    std::vector<Deferred> deferred;

    // PushDescriptors bind groups keyed by layout + written resources. Dropped whenever a resource dies.
    std::map<std::vector<uint64_t>, WGPUBindGroup> bindGroups;

    Pool<TextureHandle, Texture>                   textures;
    Pool<TextureViewHandle, TextureView>           views;
    Pool<BufferHandle, Buffer>                     buffers;
    Pool<SamplerHandle, Sampler>                   samplers;
    Pool<DescriptorLayoutHandle, DescriptorLayout> descriptorLayouts;
    Pool<PipelineHandle, Pipeline>                 pipelines;
    Pool<SwapchainHandle, Swapchain>               swapchains;

    void Defer(std::function<void()> fn) { deferred.push_back({frameNumber, std::move(fn)}); }
    void RunDeferred(bool all);
    void ClearBindGroups();

    TextureHandle AdoptTexture(WGPUTexture texture, Format format, Extent2D extent, TextureUsage usage);
    void DestroyTextureNow(Texture& t, bool destroy);

    // Copies `size` bytes into a fresh slice and returns its offset.
    uint32_t PushSlice(PushRing& ring, const void* data, uint32_t size);
    WGPUBindGroup PushGroup(PushRing& ring, uint32_t size);
    void UploadPush(PushRing& ring);
};

// ---- Translation ----

inline WGPUTextureFormat ToWgpu(Format f) noexcept
{
    switch (f) {
    case Format::R8_Unorm: return WGPUTextureFormat_R8Unorm;
    case Format::R8G8_Unorm: return WGPUTextureFormat_RG8Unorm;
    case Format::R8G8B8A8_Unorm: return WGPUTextureFormat_RGBA8Unorm;
    case Format::R8G8B8A8_Srgb: return WGPUTextureFormat_RGBA8UnormSrgb;
    case Format::B8G8R8A8_Unorm: return WGPUTextureFormat_BGRA8Unorm;
    case Format::B8G8R8A8_Srgb: return WGPUTextureFormat_BGRA8UnormSrgb;
    case Format::R16_Sfloat: return WGPUTextureFormat_R16Float;
    case Format::R16G16B16A16_Sfloat: return WGPUTextureFormat_RGBA16Float;
    case Format::R32_Uint: return WGPUTextureFormat_R32Uint;
    case Format::R32_Sfloat: return WGPUTextureFormat_R32Float;
    case Format::R32G32_Sfloat: return WGPUTextureFormat_RG32Float;
    case Format::R32G32B32A32_Sfloat: return WGPUTextureFormat_RGBA32Float;
    case Format::D32_Sfloat: return WGPUTextureFormat_Depth32Float;
    case Format::D24_Unorm_S8_Uint: return WGPUTextureFormat_Depth24PlusStencil8;
    default: return WGPUTextureFormat_Undefined;
    }
}

inline Format FromWgpu(WGPUTextureFormat f) noexcept
{
    switch (f) {
    case WGPUTextureFormat_RGBA8Unorm: return Format::R8G8B8A8_Unorm;
    case WGPUTextureFormat_RGBA8UnormSrgb: return Format::R8G8B8A8_Srgb;
    case WGPUTextureFormat_BGRA8Unorm: return Format::B8G8R8A8_Unorm;
    case WGPUTextureFormat_BGRA8UnormSrgb: return Format::B8G8R8A8_Srgb;
    case WGPUTextureFormat_RGBA16Float: return Format::R16G16B16A16_Sfloat;
    default: return Format::Undefined;
    }
}

inline uint32_t BytesPerPixel(Format f) noexcept
{
    switch (f) {
    case Format::R8_Unorm: return 1;
    case Format::R8G8_Unorm:
    case Format::R16_Sfloat: return 2;
    case Format::R16G16B16A16_Sfloat:
    case Format::R32G32_Sfloat: return 8;
    case Format::R32G32B32_Sfloat: return 12;
    case Format::R32G32B32A32_Sfloat: return 16;
    default: return 4;
    }
}

inline WGPUVertexFormat ToWgpuVertex(Format f) noexcept
{
    switch (f) {
    case Format::R8G8_Unorm: return WGPUVertexFormat_Unorm8x2;
    case Format::R8G8B8A8_Unorm: return WGPUVertexFormat_Unorm8x4;
    case Format::R32_Uint: return WGPUVertexFormat_Uint32;
    case Format::R32_Sfloat: return WGPUVertexFormat_Float32;
    case Format::R32G32_Sfloat: return WGPUVertexFormat_Float32x2;
    case Format::R32G32B32_Sfloat: return WGPUVertexFormat_Float32x3;
    case Format::R32G32B32A32_Sfloat: return WGPUVertexFormat_Float32x4;
    case Format::R16G16B16A16_Sfloat: return WGPUVertexFormat_Float16x4;
    default: return WGPUVertexFormat_Float32x4;
    }
}

inline WGPUBufferUsage ToWgpu(BufferUsage u) noexcept
{
    WGPUBufferUsage r = WGPUBufferUsage_CopyDst;
    if (AnyFlag(u, BufferUsage::TransferSrc)) r |= WGPUBufferUsage_CopySrc;
    if (AnyFlag(u, BufferUsage::Uniform)) r |= WGPUBufferUsage_Uniform;
    if (AnyFlag(u, BufferUsage::Storage)) r |= WGPUBufferUsage_Storage;
    if (AnyFlag(u, BufferUsage::Index)) r |= WGPUBufferUsage_Index;
    if (AnyFlag(u, BufferUsage::Vertex)) r |= WGPUBufferUsage_Vertex;
    if (AnyFlag(u, BufferUsage::Indirect)) r |= WGPUBufferUsage_Indirect;
    return r;
}

inline WGPUTextureUsage ToWgpu(TextureUsage u) noexcept
{
    WGPUTextureUsage r = WGPUTextureUsage_None;
    if (AnyFlag(u, TextureUsage::TransferSrc)) r |= WGPUTextureUsage_CopySrc;
    if (AnyFlag(u, TextureUsage::TransferDst)) r |= WGPUTextureUsage_CopyDst;
    if (AnyFlag(u, TextureUsage::Sampled)) r |= WGPUTextureUsage_TextureBinding;
    if (AnyFlag(u, TextureUsage::Storage)) r |= WGPUTextureUsage_StorageBinding;
    if (AnyFlag(u, TextureUsage::ColorAttachment | TextureUsage::DepthAttachment))
        r |= WGPUTextureUsage_RenderAttachment;
    return r;
}

inline WGPUShaderStage ToWgpu(ShaderStageFlags s) noexcept
{
    WGPUShaderStage r = WGPUShaderStage_None;
    if (AnyFlag(s, ShaderStageFlags::Vertex)) r |= WGPUShaderStage_Vertex;
    if (AnyFlag(s, ShaderStageFlags::Fragment)) r |= WGPUShaderStage_Fragment;
    if (AnyFlag(s, ShaderStageFlags::Compute)) r |= WGPUShaderStage_Compute;
    return r;
}

inline WGPUFilterMode ToWgpu(Filter f) noexcept
{
    return f == Filter::Nearest ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
}

inline WGPUMipmapFilterMode ToWgpu(MipmapMode m) noexcept
{
    return m == MipmapMode::Nearest ? WGPUMipmapFilterMode_Nearest : WGPUMipmapFilterMode_Linear;
}

// No border colour sampling in WebGPU: ClampToBorder clamps to edge.
inline WGPUAddressMode ToWgpu(AddressMode m) noexcept
{
    switch (m) {
    case AddressMode::Repeat: return WGPUAddressMode_Repeat;
    case AddressMode::MirroredRepeat: return WGPUAddressMode_MirrorRepeat;
    default: return WGPUAddressMode_ClampToEdge;
    }
}

inline WGPUCompareFunction ToWgpu(CompareOp op) noexcept
{
    switch (op) {
    case CompareOp::Never: return WGPUCompareFunction_Never;
    case CompareOp::Less: return WGPUCompareFunction_Less;
    case CompareOp::Equal: return WGPUCompareFunction_Equal;
    case CompareOp::LessOrEqual: return WGPUCompareFunction_LessEqual;
    case CompareOp::Greater: return WGPUCompareFunction_Greater;
    case CompareOp::NotEqual: return WGPUCompareFunction_NotEqual;
    case CompareOp::GreaterOrEqual: return WGPUCompareFunction_GreaterEqual;
    default: return WGPUCompareFunction_Always;
    }
}

inline WGPUBlendFactor ToWgpu(BlendFactor f) noexcept
{
    switch (f) {
    case BlendFactor::Zero: return WGPUBlendFactor_Zero;
    case BlendFactor::One: return WGPUBlendFactor_One;
    case BlendFactor::SrcColor: return WGPUBlendFactor_Src;
    case BlendFactor::OneMinusSrcColor: return WGPUBlendFactor_OneMinusSrc;
    case BlendFactor::DstColor: return WGPUBlendFactor_Dst;
    case BlendFactor::OneMinusDstColor: return WGPUBlendFactor_OneMinusDst;
    case BlendFactor::SrcAlpha: return WGPUBlendFactor_SrcAlpha;
    case BlendFactor::OneMinusSrcAlpha: return WGPUBlendFactor_OneMinusSrcAlpha;
    case BlendFactor::DstAlpha: return WGPUBlendFactor_DstAlpha;
    case BlendFactor::OneMinusDstAlpha: return WGPUBlendFactor_OneMinusDstAlpha;
    }
    return WGPUBlendFactor_One;
}

inline WGPUBlendOperation ToWgpu(BlendOp op) noexcept
{
    switch (op) {
    case BlendOp::Add: return WGPUBlendOperation_Add;
    case BlendOp::Subtract: return WGPUBlendOperation_Subtract;
    case BlendOp::ReverseSubtract: return WGPUBlendOperation_ReverseSubtract;
    case BlendOp::Min: return WGPUBlendOperation_Min;
    case BlendOp::Max: return WGPUBlendOperation_Max;
    }
    return WGPUBlendOperation_Add;
}

inline WGPUColorWriteMask ToWgpu(ColorComponentFlags c) noexcept
{
    WGPUColorWriteMask r = WGPUColorWriteMask_None;
    if (AnyFlag(c, ColorComponentFlags::R)) r |= WGPUColorWriteMask_Red;
    if (AnyFlag(c, ColorComponentFlags::G)) r |= WGPUColorWriteMask_Green;
    if (AnyFlag(c, ColorComponentFlags::B)) r |= WGPUColorWriteMask_Blue;
    if (AnyFlag(c, ColorComponentFlags::A)) r |= WGPUColorWriteMask_Alpha;
    return r;
}

inline WGPUPrimitiveTopology ToWgpu(PrimitiveTopology t) noexcept
{
    switch (t) {
    case PrimitiveTopology::PointList: return WGPUPrimitiveTopology_PointList;
    case PrimitiveTopology::LineList: return WGPUPrimitiveTopology_LineList;
    case PrimitiveTopology::LineStrip: return WGPUPrimitiveTopology_LineStrip;
    case PrimitiveTopology::TriangleStrip: return WGPUPrimitiveTopology_TriangleStrip;
    default: return WGPUPrimitiveTopology_TriangleList;
    }
}

inline WGPUCullMode ToWgpu(CullMode c) noexcept
{
    switch (c) {
    case CullMode::Front: return WGPUCullMode_Front;
    case CullMode::Back: return WGPUCullMode_Back;
    default: return WGPUCullMode_None;
    }
}

inline WGPUTextureViewDimension ViewDimension(TextureType t, uint32_t layers) noexcept
{
    switch (t) {
    case TextureType::eCube: return WGPUTextureViewDimension_Cube;
    case TextureType::e2DArray: return WGPUTextureViewDimension_2DArray;
    default: return layers > 1 ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
    }
}

} // namespace lumora::rhi::detail
