#pragma once

#include "CommandBuffer.h"
#include "Descriptions.h"

#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace lumora::rhi {

namespace detail {
struct NativeRHI;
}

// Destroy is deferred: the native object dies once in-flight frames that may use it have finished.
class RHIResources {
public:
    explicit RHIResources(detail::NativeRHI*& n) noexcept : _n(n) {}

    BufferHandle  CreateBuffer(const BufferDescription& desc);
    TextureHandle CreateTexture(const TextureDescription& desc);
    SamplerHandle CreateSampler(const SamplerDescription& desc);

    // Owned by the texture; never Destroy it.
    TextureViewHandle GetDefaultView(TextureHandle texture);
    // Null unless the buffer is CpuToGpuMapped or GpuToCpu.
    [[nodiscard]] void* GetMappedPointer(BufferHandle buffer) const;
    [[nodiscard]] TextureDescription GetTextureDescription(TextureHandle texture) const;

    void Destroy(BufferHandle h);
    void Destroy(TextureHandle h);
    void Destroy(SamplerHandle h);

private:
    detail::NativeRHI*& _n;
};

// Blocking uploads through a temporary staging buffer. Textures end in ImageLayout::ShaderReadOnly.
class RHIUploads {
public:
    explicit RHIUploads(detail::NativeRHI*& n) noexcept : _n(n) {}

    // Into fresh resources (texture content before is undefined).
    void UploadBlocking(std::span<const UploadRequest> requests);
    // Into live resources (texture content outside the region is preserved).
    void UpdateBlocking(std::span<const UploadRequest> requests);

private:
    detail::NativeRHI*& _n;
};

class RHIDescriptors {
public:
    explicit RHIDescriptors(detail::NativeRHI*& n) noexcept : _n(n) {}

    DescriptorLayoutHandle CreateDescriptorLayout(const DescriptorLayoutDescription& desc);
    void Destroy(DescriptorLayoutHandle h);

private:
    detail::NativeRHI*& _n;
};

class RHIPipelines {
public:
    explicit RHIPipelines(detail::NativeRHI*& n) noexcept : _n(n) {}

    PipelineHandle CreatePipeline(const PipelineDescription& desc);
    void Destroy(PipelineHandle h);

private:
    detail::NativeRHI*& _n;
};

class RHISwapchain {
public:
    explicit RHISwapchain(detail::NativeRHI*& n) noexcept : _n(n) {}

    SwapchainHandle CreateSwapchain(const SwapchainDescription& desc);
    void RecreateSwapchain(SwapchainHandle sc, Extent2D extent);
    void DestroySwapchain(SwapchainHandle sc);

    [[nodiscard]] Extent2D GetSwapchainExtent(SwapchainHandle sc) const;
    [[nodiscard]] Format   GetSwapchainFormat(SwapchainHandle sc) const;

private:
    detail::NativeRHI*& _n;
};

struct FrameContext {
    uint32_t        frameIndex = 0; // slot in [0, framesInFlight)
    CommandBuffer*  cmd        = nullptr;
    SwapchainHandle swapchain;
    TextureHandle   swapchainImage;    // invalid: nothing acquired, skip the frame (still call EndFrame)
    bool            swapchainOutOfDate = false;
};

class RHI {
public:
    RHI();
    ~RHI();

    RHI(const RHI&)            = delete;
    RHI& operator=(const RHI&) = delete;

    void Init(const RHIInitDescription& desc);
    void Shutdown();
    [[nodiscard]] bool IsInitialised() const noexcept { return _n != nullptr; }
    [[nodiscard]] const AdapterInfo& GetActiveAdapter() const;

    // Waits for the slot, acquires an image of `swapchain` (if valid) and opens its command buffer.
    FrameContext BeginFrame(SwapchainHandle swapchain = {});
    // Submits and presents. Sets ctx.swapchainOutOfDate when the swapchain needs recreating.
    void EndFrame(FrameContext& ctx);

    void ImmediateSubmit(const std::function<void(CommandBuffer&)>& fn);
    void WaitIdle() const;
    [[nodiscard]] uint32_t GetFramesInFlight() const noexcept;

    [[nodiscard]] RHIResources&   Resources() noexcept { return _resources; }
    [[nodiscard]] RHIUploads&     Uploads() noexcept { return _uploads; }
    [[nodiscard]] RHIDescriptors& Descriptors() noexcept { return _descriptors; }
    [[nodiscard]] RHIPipelines&   Pipelines() noexcept { return _pipelines; }
    [[nodiscard]] RHISwapchain&   Swapchain() noexcept { return _swapchain; }

    [[nodiscard]] detail::NativeRHI* Native() const noexcept { return _n; }

private:
    detail::NativeRHI* _n = nullptr;
    RHIResources       _resources{_n};
    RHIUploads         _uploads{_n};
    RHIDescriptors     _descriptors{_n};
    RHIPipelines       _pipelines{_n};
    RHISwapchain       _swapchain{_n};
};

[[nodiscard]] std::vector<AdapterInfo> EnumerateAdapters();

} // namespace lumora::rhi
