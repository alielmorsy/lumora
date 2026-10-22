#pragma once

#include "Descriptions.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace lumora::rhi {

namespace detail {
struct NativeRHI;
}

struct BufferImageCopy {
    uint64_t bufferOffset      = 0;
    uint32_t bufferRowLength   = 0;
    uint32_t bufferImageHeight = 0;
    uint32_t mipLevel          = 0;
    uint32_t baseArrayLayer    = 0;
    uint32_t layerCount        = 1;
    Offset3D imageOffset       = {};
    Extent3D imageExtent       = {1, 1, 1};
};

// Non-owning recorder over a backend command buffer. Valid between RHI::BeginFrame and EndFrame
// (or inside an ImmediateSubmit callback).
class CommandBuffer {
public:
    CommandBuffer(detail::NativeRHI* rhi, void* nativeCmd, uint32_t frameSlot) noexcept
        : _rhi(rhi), _cmd(nativeCmd), _frameSlot(frameSlot)
    {
    }

    void Barrier(std::span<const TextureBarrier> textures, std::span<const BufferBarrier> buffers = {});
    // Whole-texture layout change with conservative stage/access masks.
    void TransitionTexture(TextureHandle texture, ImageLayout oldLayout, ImageLayout newLayout);

    void BeginRendering(const RenderingInfo& info);
    void EndRendering();

    void SetViewport(const Viewport& vp);
    void SetScissor(const Rect2D& rect);

    void BindPipeline(PipelineHandle pipeline);
    void BindVertexBuffer(uint32_t binding, BufferHandle buffer, uint64_t offset);
    void BindIndexBuffer(BufferHandle buffer, uint64_t offset, IndexType type);

    // Both need a bound pipeline: its layout is used.
    void PushConstants(ShaderStageFlags stages, uint32_t offset, std::span<const std::byte> data);
    void PushDescriptors(uint32_t set, std::span<const DescriptorWrite> writes);

    void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex = 0, uint32_t firstInstance = 0);
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0);

    void CopyBufferToImage(BufferHandle src, TextureHandle dst, ImageLayout dstLayout,
                           std::span<const BufferImageCopy> regions);

    void BeginDebugLabel(std::string_view name, Color color);
    void EndDebugLabel();

    [[nodiscard]] void* Native() const noexcept { return _cmd; }
    [[nodiscard]] uint32_t FrameSlot() const noexcept { return _frameSlot; }

private:
    detail::NativeRHI* _rhi = nullptr;
    void*              _cmd = nullptr;
    uint32_t           _frameSlot = 0;
    PipelineHandle     _bound{};
    // Render-pass fallback (no dynamic rendering): the pass opened by BeginRendering.
    bool               _inLegacyPass = false;
};

} // namespace lumora::rhi
