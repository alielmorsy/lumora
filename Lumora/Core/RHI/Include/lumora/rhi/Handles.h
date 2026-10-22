#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace lumora::rhi {

#define LUMORA_RHI_HANDLE(NAME)                                                           \
    struct NAME {                                                                         \
        uint32_t index      = 0;                                                          \
        uint16_t generation = 0;                                                          \
        [[nodiscard]] constexpr bool IsValid() const noexcept { return generation != 0; } \
        constexpr bool operator==(const NAME&) const = default;                           \
    };

LUMORA_RHI_HANDLE(TextureHandle)
LUMORA_RHI_HANDLE(TextureViewHandle)
LUMORA_RHI_HANDLE(BufferHandle)
LUMORA_RHI_HANDLE(SamplerHandle)
LUMORA_RHI_HANDLE(PipelineHandle)
LUMORA_RHI_HANDLE(DescriptorLayoutHandle)
LUMORA_RHI_HANDLE(SwapchainHandle)

#undef LUMORA_RHI_HANDLE

} // namespace lumora::rhi

#define LUMORA_RHI_HASH(NAME)                                                \
    template <> struct std::hash<lumora::rhi::NAME> {                        \
        size_t operator()(const lumora::rhi::NAME& h) const noexcept         \
        {                                                                    \
            return (static_cast<size_t>(h.index) << 16) ^ h.generation;      \
        }                                                                    \
    };

LUMORA_RHI_HASH(TextureHandle)
LUMORA_RHI_HASH(TextureViewHandle)
LUMORA_RHI_HASH(BufferHandle)
LUMORA_RHI_HASH(SamplerHandle)
LUMORA_RHI_HASH(PipelineHandle)
LUMORA_RHI_HASH(DescriptorLayoutHandle)
LUMORA_RHI_HASH(SwapchainHandle)

#undef LUMORA_RHI_HASH
