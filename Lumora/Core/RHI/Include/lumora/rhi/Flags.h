#pragma once

#include <cstdint>
#include <type_traits>

namespace lumora::rhi {

#define LUMORA_RHI_BITMASK(E)                                                                  \
    constexpr E operator|(E a, E b) noexcept                                                   \
    {                                                                                          \
        return static_cast<E>(static_cast<std::underlying_type_t<E>>(a) |                      \
                              static_cast<std::underlying_type_t<E>>(b));                      \
    }                                                                                          \
    constexpr E operator&(E a, E b) noexcept                                                   \
    {                                                                                          \
        return static_cast<E>(static_cast<std::underlying_type_t<E>>(a) &                      \
                              static_cast<std::underlying_type_t<E>>(b));                      \
    }                                                                                          \
    constexpr E& operator|=(E& a, E b) noexcept { return a = a | b; }                          \
    constexpr bool HasFlag(E v, E f) noexcept { return (v & f) == f; }                         \
    constexpr bool AnyFlag(E v, E f) noexcept                                                  \
    {                                                                                          \
        return static_cast<std::underlying_type_t<E>>(v & f) != 0;                             \
    }

enum class PipelineStage : uint32_t {
    None            = 0,
    TopOfPipe       = 1u << 0,
    VertexInput     = 1u << 1,
    VertexShader    = 1u << 2,
    FragmentShader  = 1u << 3,
    ColorAttachment = 1u << 4,
    ComputeShader   = 1u << 5,
    Transfer        = 1u << 6,
    BottomOfPipe    = 1u << 7,
    Host            = 1u << 8,
    AllGraphics     = 1u << 9,
    AllCommands     = 1u << 10,
};
LUMORA_RHI_BITMASK(PipelineStage)

enum class AccessMask : uint32_t {
    None                 = 0,
    IndexRead            = 1u << 0,
    VertexAttributeRead  = 1u << 1,
    UniformRead          = 1u << 2,
    ShaderRead           = 1u << 3,
    ShaderWrite          = 1u << 4,
    ColorAttachmentRead  = 1u << 5,
    ColorAttachmentWrite = 1u << 6,
    TransferRead         = 1u << 7,
    TransferWrite        = 1u << 8,
    HostRead             = 1u << 9,
    HostWrite            = 1u << 10,
    MemoryRead           = 1u << 11,
    MemoryWrite          = 1u << 12,
};
LUMORA_RHI_BITMASK(AccessMask)

enum class ShaderStageFlags : uint32_t {
    None        = 0,
    Vertex      = 1u << 0,
    Fragment    = 1u << 1,
    Compute     = 1u << 2,
    AllGraphics = Vertex | Fragment,
    All         = AllGraphics | Compute,
};
LUMORA_RHI_BITMASK(ShaderStageFlags)

enum class TextureUsage : uint32_t {
    None            = 0,
    TransferSrc     = 1u << 0,
    TransferDst     = 1u << 1,
    Sampled         = 1u << 2,
    Storage         = 1u << 3,
    ColorAttachment = 1u << 4,
    DepthAttachment = 1u << 5,
};
LUMORA_RHI_BITMASK(TextureUsage)

enum class BufferUsage : uint32_t {
    None        = 0,
    TransferSrc = 1u << 0,
    TransferDst = 1u << 1,
    Uniform     = 1u << 2,
    Storage     = 1u << 3,
    Index       = 1u << 4,
    Vertex      = 1u << 5,
    Indirect    = 1u << 6,
};
LUMORA_RHI_BITMASK(BufferUsage)

enum class ColorComponentFlags : uint32_t {
    None = 0,
    R    = 1u << 0,
    G    = 1u << 1,
    B    = 1u << 2,
    A    = 1u << 3,
    RGB  = R | G | B,
    RGBA = R | G | B | A,
};
LUMORA_RHI_BITMASK(ColorComponentFlags)

#undef LUMORA_RHI_BITMASK

} // namespace lumora::rhi
