#pragma once

#include "Common.h"
#include "Flags.h"
#include "Formats.h"
#include "Handles.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace lumora::rhi {

// ---- Device ----

enum class DeviceType : uint8_t { Other, Integrated, Discrete, Virtual, Cpu };

struct AdapterInfo {
    std::string name;
    DeviceType  type          = DeviceType::Other;
    uint32_t    apiVersion    = 0;
    uint32_t    index         = 0;
    bool        pushDescriptor   = false; // VK_KHR_push_descriptor
    bool        dynamicRendering = false; // core 1.3 or VK_KHR_dynamic_rendering
};

struct RHIInitDescription {
    int      adapterIndex     = -1; // -1 = auto: prefer discrete
    bool     enableValidation = false;
    bool     enableDebugLabels = false;
    uint32_t framesInFlight   = 2;
};

// ---- Resources ----

struct BufferDescription {
    uint64_t         size       = 0;
    BufferUsage      usage      = BufferUsage::None;
    MemoryPattern    memory     = MemoryPattern::GpuOnly;
    std::string_view debugName  = {};
    bool             preferHost = false; // CpuToGpu*: avoid device-local host-visible memory
};

struct TextureDescription {
    TextureType      type        = TextureType::e2D;
    Extent3D         extent      = {1, 1, 1};
    Format           format      = Format::R8G8B8A8_Unorm;
    TextureUsage     usage       = TextureUsage::Sampled;
    uint32_t         mipLevels   = 1;
    uint32_t         arrayLayers = 1;
    SampleCount      samples     = SampleCount::e1;
    MemoryPattern    memory      = MemoryPattern::GpuOnly;
    std::string_view debugName   = {};
};

struct SamplerDescription {
    Filter           magFilter        = Filter::Linear;
    Filter           minFilter        = Filter::Linear;
    MipmapMode       mipmapMode       = MipmapMode::Linear;
    AddressMode      addressU         = AddressMode::Repeat;
    AddressMode      addressV         = AddressMode::Repeat;
    AddressMode      addressW         = AddressMode::Repeat;
    float            mipLodBias       = 0.f;
    bool             anisotropyEnable = false;
    float            maxAnisotropy    = 1.f;
    float            minLod           = 0.f;
    float            maxLod           = 1000.f;
    BorderColor      borderColor      = BorderColor::OpaqueBlack;
    std::string_view debugName        = {};
};

// ---- Uploads ----

struct TextureUploadRegion {
    uint32_t mipLevel   = 0;
    uint32_t baseLayer  = 0;
    uint32_t layerCount = 1;
    Offset3D offset     = {};
    Extent3D extent     = {0, 0, 1}; // zero width or height = full mip
};

// Exactly one of dstBuffer / dstTexture is valid.
struct UploadRequest {
    BufferHandle        dstBuffer{};
    TextureHandle       dstTexture{};
    uint64_t            dstOffset = 0;
    const void*         data      = nullptr;
    size_t              size      = 0;
    TextureUploadRegion textureRegion{};
};

// ---- Descriptors ----

struct DescriptorBinding {
    uint32_t         binding = 0;
    DescriptorType   type    = DescriptorType::UniformBuffer;
    uint32_t         count   = 1;
    ShaderStageFlags stages  = ShaderStageFlags::All;
};

// pushDescriptor: the layout is only ever fed through CommandBuffer::PushDescriptors. On devices without
// VK_KHR_push_descriptor the backend falls back to per-frame descriptor sets transparently.
struct DescriptorLayoutDescription {
    std::span<const DescriptorBinding> bindings;
    bool                               pushDescriptor = false;
    std::string_view                   debugName      = {};
};

struct BufferBinding {
    BufferHandle buffer;
    uint64_t     offset = 0;
    uint64_t     range  = ~0ull; // whole buffer
};

struct ImageBinding {
    TextureHandle     texture;
    TextureViewHandle view; // overrides the texture's default view when valid
    ImageLayout       layout = ImageLayout::ShaderReadOnly;
    SamplerHandle     sampler;
};

struct DescriptorWrite {
    uint32_t       binding      = 0;
    uint32_t       arrayElement = 0;
    DescriptorType type         = DescriptorType::UniformBuffer;
    BufferBinding  bufferInfo{};
    ImageBinding   imageInfo{};
    SamplerHandle  samplerOnly{};
};

// ---- Pipelines ----

struct ShaderStageDesc {
    ShaderStageFlags           stage = ShaderStageFlags::None;
    std::span<const std::byte> code; // SPIR-V on Vulkan, UTF-8 WGSL on WebGPU
    std::string_view           entryPoint = "main";
    std::string_view           debugName  = {};
};

struct VertexAttribute {
    uint32_t location = 0;
    uint32_t binding  = 0;
    Format   format   = Format::Undefined;
    uint32_t offset   = 0;
};

struct VertexBinding {
    uint32_t binding     = 0;
    uint32_t stride      = 0;
    bool     perInstance = false;
};

struct VertexLayout {
    std::span<const VertexBinding>   bindings;
    std::span<const VertexAttribute> attributes;
};

struct BlendAttachment {
    bool                blendEnable  = false;
    BlendFactor         srcColor     = BlendFactor::One;
    BlendFactor         dstColor     = BlendFactor::Zero;
    BlendOp             colorBlendOp = BlendOp::Add;
    BlendFactor         srcAlpha     = BlendFactor::One;
    BlendFactor         dstAlpha     = BlendFactor::Zero;
    BlendOp             alphaBlendOp = BlendOp::Add;
    ColorComponentFlags writeMask    = ColorComponentFlags::RGBA;
};

struct BlendState {
    std::span<const BlendAttachment> attachments;
};

struct DepthStencilState {
    bool      depthTestEnable  = true;
    bool      depthWriteEnable = true;
    CompareOp depthCompareOp   = CompareOp::GreaterOrEqual;
};

struct RasterizerState {
    PolygonMode polygonMode = PolygonMode::Fill;
    CullMode    cullMode    = CullMode::Back;
    FrontFace   frontFace   = FrontFace::CounterClockwise;
};

struct PushConstantRange {
    ShaderStageFlags stages = ShaderStageFlags::None;
    uint32_t         offset = 0;
    uint32_t         size   = 0;
};

// Viewport and scissor are always dynamic.
struct PipelineDescription {
    PipelineType                            type = PipelineType::Graphics;
    std::span<const ShaderStageDesc>        stages;
    VertexLayout                            vertex{};
    PrimitiveTopology                       topology = PrimitiveTopology::TriangleList;
    BlendState                              blend{};
    DepthStencilState                       depthStencil{};
    RasterizerState                         rasterizer{};
    std::span<const DescriptorLayoutHandle> setLayouts;
    std::span<const PushConstantRange>      pushConstants;
    std::span<const Format>                 colorFormats;
    Format                                  depthFormat = Format::Undefined;
    SampleCount                             samples     = SampleCount::e1;
    std::string_view                        debugName   = {};
};

// ---- Commands ----

struct ColorAttachment {
    TextureHandle     texture;
    TextureViewHandle view; // overrides the texture's default view when valid
    ImageLayout       layout     = ImageLayout::ColorAttachment;
    LoadOp            loadOp     = LoadOp::Load;
    StoreOp           storeOp    = StoreOp::Store;
    Color             clearColor = {};
};

struct RenderingInfo {
    Rect2D                           renderArea{};
    std::span<const ColorAttachment> colorAttachments;
};

struct TextureSubresource {
    uint32_t baseMipLevel   = 0;
    uint32_t levelCount     = 1;
    uint32_t baseArrayLayer = 0;
    uint32_t layerCount     = 1;
};

struct TextureBarrier {
    TextureHandle      texture;
    PipelineStage      srcStage  = PipelineStage::None;
    AccessMask         srcAccess = AccessMask::None;
    PipelineStage      dstStage  = PipelineStage::None;
    AccessMask         dstAccess = AccessMask::None;
    ImageLayout        oldLayout = ImageLayout::Undefined;
    ImageLayout        newLayout = ImageLayout::Undefined;
    TextureSubresource subresource{};
};

struct BufferBarrier {
    BufferHandle  buffer;
    PipelineStage srcStage  = PipelineStage::None;
    AccessMask    srcAccess = AccessMask::None;
    PipelineStage dstStage  = PipelineStage::None;
    AccessMask    dstAccess = AccessMask::None;
    uint64_t      offset    = 0;
    uint64_t      size      = ~0ull;
};

// ---- Swapchain ----

enum class NativeWindowKind : uint8_t { Win32, Android, Web, Xlib, Wayland };

// Win32: window = HWND, instance = HINSTANCE. Android: window = ANativeWindow*. Web: window = const char* canvas CSS selector.
// Xlib: window = X11 Window id (as a pointer-sized integer), instance = Display*. Wayland: window = wl_surface*, instance = wl_display*.
struct NativeWindowHandles {
    void*            window   = nullptr;
    void*            instance = nullptr;
    NativeWindowKind kind     = NativeWindowKind::Win32;
};

// preferredFormat is a hint; the surface decides (GetSwapchainFormat).
struct SwapchainDescription {
    NativeWindowHandles nativeWindow{};
    Extent2D            extent          = {0, 0};
    uint32_t            imageCount      = 3;
    Format              preferredFormat = Format::B8G8R8A8_Unorm;
    PresentMode         presentMode     = PresentMode::Fifo;
};

} // namespace lumora::rhi
