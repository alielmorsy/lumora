#pragma once

// Internal backend state shared by every Vulkan source file. Not installed.

#include <lumora/rhi/RHI.h>

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(__linux__)
#define VK_USE_PLATFORM_XLIB_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#endif
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <lumora/log/Log.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#define LUMORA_VK_CHECK(expr)                                                       \
    do {                                                                            \
        const VkResult _r = (expr);                                                 \
        if (_r != VK_SUCCESS)                                                       \
            ::lumora::rhi::detail::VkFail(_r, #expr, __FILE__, __LINE__);           \
    } while (false)

namespace lumora::rhi::detail {

[[noreturn]] void VkFail(VkResult r, const char* expr, const char* file, int line);

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

    // Returns the object so the caller can schedule its native destruction.
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

struct Texture {
    VkImage            image = VK_NULL_HANDLE;
    VmaAllocation      alloc = VK_NULL_HANDLE; // null for swapchain images
    VkFormat           format = VK_FORMAT_UNDEFINED;
    TextureDescription desc{};
    TextureViewHandle  defaultView{};
};

struct TextureView {
    VkImageView   view = VK_NULL_HANDLE;
    TextureHandle texture{};
};

struct Buffer {
    VkBuffer          buffer = VK_NULL_HANDLE;
    VmaAllocation     alloc  = VK_NULL_HANDLE;
    void*             mapped = nullptr;
    BufferDescription desc{};
};

struct Sampler {
    VkSampler sampler = VK_NULL_HANDLE;
};

struct DescriptorLayout {
    VkDescriptorSetLayout          layout = VK_NULL_HANDLE;
    bool                           push   = false; // created with the push flag (native push path)
    std::vector<DescriptorBinding> bindings;
};

struct Pipeline {
    VkPipeline                          pipeline = VK_NULL_HANDLE;
    VkPipelineLayout                    layout   = VK_NULL_HANDLE;
    std::vector<DescriptorLayoutHandle> setLayouts;
};

struct Swapchain {
    VkSurfaceKHR               surface   = VK_NULL_HANDLE;
    VkSwapchainKHR             swapchain = VK_NULL_HANDLE;
    VkFormat                   vkFormat  = VK_FORMAT_UNDEFINED;
    Format                     format    = Format::Undefined;
    Extent2D                   extent{};
    SwapchainDescription       desc{};
    std::vector<TextureHandle> images;
    std::vector<VkSemaphore>   renderDone; // one per image
    uint32_t                   imageIndex = 0;
};

struct Frame {
    VkCommandPool                      pool      = VK_NULL_HANDLE;
    VkCommandBuffer                    cmd       = VK_NULL_HANDLE;
    VkFence                            fence     = VK_NULL_HANDLE;
    VkSemaphore                        acquired  = VK_NULL_HANDLE;
    VkDescriptorPool                   descPool  = VK_NULL_HANDLE; // push-descriptor fallback, reset per frame
    std::unique_ptr<CommandBuffer>     recorder;
};

struct NativeRHI {
    VkInstance               instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger      = VK_NULL_HANDLE;
    VkPhysicalDevice         physical       = VK_NULL_HANDLE;
    VkDevice                 device         = VK_NULL_HANDLE;
    VkQueue                  queue          = VK_NULL_HANDLE;
    uint32_t                 queueFamily    = 0;
    VmaAllocator             allocator      = VK_NULL_HANDLE;
    VkCommandPool            immediatePool  = VK_NULL_HANDLE;
    AdapterInfo              adapter{};
    bool                     debugLabels    = false;

    // Extension entry points (null when unsupported). Core 1.3 functions are stored here too.
    PFN_vkCmdBeginRenderingKHR       cmdBeginRendering   = nullptr;
    PFN_vkCmdEndRenderingKHR         cmdEndRendering     = nullptr;
    PFN_vkCmdPushDescriptorSetKHR    cmdPushDescriptorSet = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT cmdBeginLabel       = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT   cmdEndLabel         = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT setObjectName       = nullptr;

    std::vector<Frame> frames;
    uint32_t           frameSlot   = 0; // slot of the frame being recorded / next to begin
    uint64_t           frameNumber = 0; // frames begun so far

    struct Deferred {
        uint64_t              frame; // last frame begun when queued
        std::function<void()> fn;
    };
    std::vector<Deferred> deferred;

    Pool<TextureHandle, Texture>                   textures;
    Pool<TextureViewHandle, TextureView>           views;
    Pool<BufferHandle, Buffer>                     buffers;
    Pool<SamplerHandle, Sampler>                   samplers;
    Pool<DescriptorLayoutHandle, DescriptorLayout> descriptorLayouts;
    Pool<PipelineHandle, Pipeline>                 pipelines;
    Pool<SwapchainHandle, Swapchain>               swapchains;

    // Run `fn` once every frame in flight now has finished (immediately at Shutdown).
    void Defer(std::function<void()> fn) { deferred.push_back({frameNumber, std::move(fn)}); }
    // Runs queued work whose frames have all retired; everything when `all`.
    void RunDeferred(bool all);

    void Name(VkObjectType type, uint64_t handle, std::string_view name) const;

    // Render-pass fallback when adapter.dynamicRendering is false. Pipelines are built against the
    // LoadOp::Load pass of their color format (render-pass compatibility ignores load ops).
    VkRenderPass GetRenderPass(VkFormat format, LoadOp load);
    struct RenderPassKey {
        VkFormat     format;
        LoadOp       load;
        VkRenderPass pass;
    };
    std::vector<RenderPassKey> renderPasses;

    // Wraps a swapchain or foreign VkImage as a texture (no allocation, not destroyed by Destroy).
    TextureHandle AdoptImage(VkImage image, VkFormat format, Extent2D extent, TextureUsage usage);
    void DestroyTextureNow(Texture& t);
};

} // namespace lumora::rhi::detail
