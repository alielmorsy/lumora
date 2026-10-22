#pragma once

#include <cstdint>

namespace lumora::rhi {

enum class Format : uint32_t {
    Undefined = 0,
    R8_Unorm,
    R8G8_Unorm,
    R8G8B8A8_Unorm,
    R8G8B8A8_Srgb,
    B8G8R8A8_Unorm,
    B8G8R8A8_Srgb,
    R16_Sfloat,
    R16G16B16A16_Sfloat,
    R32_Uint,
    R32_Sfloat,
    R32G32_Sfloat,
    R32G32B32_Sfloat,
    R32G32B32A32_Sfloat,
    D32_Sfloat,
    D24_Unorm_S8_Uint,
};

enum class TextureType : uint8_t { e2D, e2DArray, eCube };
enum class SampleCount : uint8_t { e1 = 1, e2 = 2, e4 = 4, e8 = 8 };

enum class ImageLayout : uint8_t {
    Undefined,
    General,
    ColorAttachment,
    DepthStencilAttachment,
    ShaderReadOnly,
    TransferSrc,
    TransferDst,
    PresentSrc,
};

enum class Filter : uint8_t { Nearest, Linear };
enum class MipmapMode : uint8_t { Nearest, Linear };
enum class AddressMode : uint8_t { Repeat, MirroredRepeat, ClampToEdge, ClampToBorder };
enum class BorderColor : uint8_t { TransparentBlack, OpaqueBlack, OpaqueWhite };
enum class CompareOp : uint8_t { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always };

enum class DescriptorType : uint8_t {
    Sampler,
    CombinedImageSampler,
    SampledImage,
    StorageImage,
    UniformBuffer,
    StorageBuffer,
};

enum class LoadOp : uint8_t { Load, Clear, DontCare };
enum class StoreOp : uint8_t { Store, DontCare };
enum class IndexType : uint8_t { Uint16, Uint32 };

// CpuToGpuMapped and GpuToCpu stay persistently mapped.
enum class MemoryPattern : uint8_t { GpuOnly, CpuToGpu, CpuToGpuMapped, GpuToCpu };

enum class PipelineType : uint8_t { Graphics };
enum class PrimitiveTopology : uint8_t { PointList, LineList, LineStrip, TriangleList, TriangleStrip };
enum class PolygonMode : uint8_t { Fill, Line };
enum class CullMode : uint8_t { None, Front, Back };
enum class FrontFace : uint8_t { CounterClockwise, Clockwise };
enum class BlendFactor : uint8_t {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
};
enum class BlendOp : uint8_t { Add, Subtract, ReverseSubtract, Min, Max };
enum class PresentMode : uint8_t { Immediate, Mailbox, Fifo, FifoRelaxed };

} // namespace lumora::rhi
