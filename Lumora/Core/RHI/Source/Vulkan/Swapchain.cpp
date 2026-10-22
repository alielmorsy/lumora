#include <lumora/rhi/RHI.h>

#include "Translate.h"
#include "VulkanRHI.h"

#include <algorithm>
#include <vector>

LUMORA_LOG_CATEGORY("RHI");

namespace lumora::rhi {

namespace {

#define LOG_INVALID_HANDLE_ONCE(type, h)                                             \
    do {                                                                             \
        static bool s_logged = false;                                                \
        if (!s_logged) {                                                             \
            s_logged = true;                                                         \
            LOG_WARN("Invalid " type " handle: ({}, {})", (h).index, (h).generation); \
        }                                                                            \
    } while (false)

VkSurfaceKHR CreateSurface(VkInstance instance, const NativeWindowHandles& handles)
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
#if defined(_WIN32)
    if (handles.kind == NativeWindowKind::Win32) {
        VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        sci.hwnd      = static_cast<HWND>(handles.window);
        sci.hinstance = handles.instance ? static_cast<HINSTANCE>(handles.instance) : GetModuleHandle(nullptr);
        LUMORA_VK_CHECK(vkCreateWin32SurfaceKHR(instance, &sci, nullptr, &surface));
        return surface;
    }
#elif defined(__ANDROID__)
    if (handles.kind == NativeWindowKind::Android) {
        VkAndroidSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
        sci.window = static_cast<ANativeWindow*>(handles.window);
        LUMORA_VK_CHECK(vkCreateAndroidSurfaceKHR(instance, &sci, nullptr, &surface));
        return surface;
    }
#endif
    LOG_ERROR("Unsupported native window kind or platform");
    return VK_NULL_HANDLE;
}

VkSurfaceFormatKHR PickSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available, Format preferred)
{
    if (available.empty())
        return {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

    if (available.size() == 1 && available[0].format == VK_FORMAT_UNDEFINED) {
        VkFormat f = (preferred != Format::Undefined) ? detail::ToVk(preferred) : VK_FORMAT_B8G8R8A8_UNORM;
        return {f, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }

    const auto match = [&](VkFormat vkFmt) -> const VkSurfaceFormatKHR* {
        for (const auto& f : available) {
            if (f.format == vkFmt)
                return &f;
        }
        return nullptr;
    };

    if (preferred != Format::Undefined) {
        if (const auto* p = match(detail::ToVk(preferred)))
            return *p;
    }
    if (const auto* p = match(VK_FORMAT_B8G8R8A8_UNORM))
        return *p;
    if (const auto* p = match(VK_FORMAT_R8G8B8A8_UNORM))
        return *p;

    for (const auto& f : available) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM ||
            f.format == VK_FORMAT_A8B8G8R8_UNORM_PACK32)
            return f;
    }
    return available[0];
}

VkPresentModeKHR PickPresentMode(const std::vector<VkPresentModeKHR>& available, PresentMode preferred)
{
    const VkPresentModeKHR want = detail::ToVk(preferred);
    for (VkPresentModeKHR m : available) {
        if (m == want)
            return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

Extent2D ClampExtent(const VkSurfaceCapabilitiesKHR& caps, Extent2D requested)
{
    if (caps.currentExtent.width != UINT32_MAX)
        return {caps.currentExtent.width, caps.currentExtent.height};
    Extent2D e = requested;
    e.width    = std::clamp(e.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    e.height   = std::clamp(e.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return e;
}

VkSurfaceCapabilitiesKHR SurfaceCaps(detail::NativeRHI& n, VkSurfaceKHR surface)
{
    VkSurfaceCapabilitiesKHR caps{};
    LUMORA_VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(n.physical, surface, &caps));
    return caps;
}

/// `extent` must be non-zero (callers check ClampExtent first).
void BuildSwapchain(detail::NativeRHI& n, detail::Swapchain& sc, VkSwapchainKHR oldSwapchain,
                    const VkSurfaceCapabilitiesKHR& caps, Extent2D extent)
{
    uint32_t formatCount = 0;
    LUMORA_VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(n.physical, sc.surface, &formatCount, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    LUMORA_VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(n.physical, sc.surface, &formatCount, formats.data()));

    const VkSurfaceFormatKHR surfaceFormat = PickSurfaceFormat(formats, sc.desc.preferredFormat);

    uint32_t presentModeCount = 0;
    LUMORA_VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(n.physical, sc.surface, &presentModeCount, nullptr));
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    LUMORA_VK_CHECK(
        vkGetPhysicalDeviceSurfacePresentModesKHR(n.physical, sc.surface, &presentModeCount, presentModes.data()));

    const VkPresentModeKHR presentMode = PickPresentMode(presentModes, sc.desc.presentMode);

    uint32_t imageCount = std::max(sc.desc.imageCount, caps.minImageCount);
    if (caps.maxImageCount > 0)
        imageCount = std::min(imageCount, caps.maxImageCount);

    VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
        composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
        composite = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface          = sc.surface;
    sci.minImageCount    = imageCount;
    sci.imageFormat      = surfaceFormat.format;
    sci.imageColorSpace  = surfaceFormat.colorSpace;
    sci.imageExtent      = {extent.width, extent.height};
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform     = caps.currentTransform;
    sci.compositeAlpha   = composite;
    sci.presentMode      = presentMode;
    sci.clipped          = VK_TRUE;
    sci.oldSwapchain     = oldSwapchain;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vkCreateSwapchainKHR(n.device, &sci, nullptr, &swapchain));

    sc.swapchain = swapchain;
    sc.vkFormat  = surfaceFormat.format;
    sc.format    = detail::FromVk(surfaceFormat.format);
    sc.extent    = extent;

    uint32_t count = 0;
    LUMORA_VK_CHECK(vkGetSwapchainImagesKHR(n.device, sc.swapchain, &count, nullptr));
    std::vector<VkImage> vkImages(count);
    LUMORA_VK_CHECK(vkGetSwapchainImagesKHR(n.device, sc.swapchain, &count, vkImages.data()));

    sc.images.clear();
    sc.images.reserve(count);
    for (VkImage img : vkImages) {
        TextureHandle th = n.AdoptImage(img, surfaceFormat.format, extent, TextureUsage::ColorAttachment);
        sc.images.push_back(th);
    }

    sc.renderDone.resize(count, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < count; ++i) {
        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        LUMORA_VK_CHECK(vkCreateSemaphore(n.device, &semInfo, nullptr, &sc.renderDone[i]));
    }
    sc.imageIndex = 0;
}

} // namespace

SwapchainHandle RHISwapchain::CreateSwapchain(const SwapchainDescription& desc)
{
    if (!_n)
        return {};

    detail::NativeRHI& n = *_n;
    VkSurfaceKHR surface = CreateSurface(n.instance, desc.nativeWindow);
    if (!surface)
        return {};

    VkBool32 supported = VK_FALSE;
    LUMORA_VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(n.physical, n.queueFamily, surface, &supported));
    if (!supported) {
        LOG_ERROR("Queue family {} does not support presentation on surface", n.queueFamily);
        vkDestroySurfaceKHR(n.instance, surface, nullptr);
        return {};
    }

    detail::Swapchain sc{};
    sc.surface = surface;
    sc.desc    = desc;

    const VkSurfaceCapabilitiesKHR caps   = SurfaceCaps(n, surface);
    const Extent2D                 extent = ClampExtent(caps, desc.extent);
    if (extent.width == 0 || extent.height == 0) {
        LOG_ERROR("Failed to create initial swapchain (zero surface extent)");
        vkDestroySurfaceKHR(n.instance, surface, nullptr);
        return {};
    }
    BuildSwapchain(n, sc, VK_NULL_HANDLE, caps, extent);

    return n.swapchains.Add(std::move(sc));
}

void RHISwapchain::RecreateSwapchain(SwapchainHandle scHandle, Extent2D extent)
{
    if (!_n)
        return;
    detail::Swapchain* sc = _n->swapchains.Get(scHandle);
    if (!sc) {
        LOG_INVALID_HANDLE_ONCE("swapchain", scHandle);
        return;
    }

    if (extent.width == 0 || extent.height == 0)
        return;

    // A minimized or transitioning window reports a zero surface extent. Bail out before tearing anything down: the old
    // swapchain stays valid (acquire keeps returning OUT_OF_DATE) and the caller retries next frame. Destroying it here
    // left a dangling handle that the next vkAcquireNextImageKHR hung or crashed on.
    const VkSurfaceCapabilitiesKHR caps          = SurfaceCaps(*_n, sc->surface);
    const Extent2D                 surfaceExtent = ClampExtent(caps, extent);
    if (surfaceExtent.width == 0 || surfaceExtent.height == 0)
        return;

    vkDeviceWaitIdle(_n->device);

    for (TextureHandle img : sc->images) {
        if (detail::Texture* t = _n->textures.Get(img)) {
            _n->DestroyTextureNow(*t);
            _n->textures.Remove(img);
        }
    }
    sc->images.clear();

    for (VkSemaphore s : sc->renderDone) {
        if (s)
            vkDestroySemaphore(_n->device, s, nullptr);
    }
    sc->renderDone.clear();

    VkSwapchainKHR old = sc->swapchain;
    sc->desc.extent    = extent;

    BuildSwapchain(*_n, *sc, old, caps, surfaceExtent);

    if (old)
        vkDestroySwapchainKHR(_n->device, old, nullptr);
}

void RHISwapchain::DestroySwapchain(SwapchainHandle scHandle)
{
    if (!_n)
        return;
    detail::Swapchain* sc = _n->swapchains.Get(scHandle);
    if (!sc) {
        LOG_INVALID_HANDLE_ONCE("swapchain", scHandle);
        return;
    }

    vkDeviceWaitIdle(_n->device);

    for (TextureHandle img : sc->images) {
        if (detail::Texture* t = _n->textures.Get(img)) {
            _n->DestroyTextureNow(*t);
            _n->textures.Remove(img);
        }
    }
    sc->images.clear();

    for (VkSemaphore s : sc->renderDone) {
        if (s)
            vkDestroySemaphore(_n->device, s, nullptr);
    }
    sc->renderDone.clear();

    if (sc->swapchain) {
        vkDestroySwapchainKHR(_n->device, sc->swapchain, nullptr);
        sc->swapchain = VK_NULL_HANDLE;
    }
    if (sc->surface) {
        vkDestroySurfaceKHR(_n->instance, sc->surface, nullptr);
        sc->surface = VK_NULL_HANDLE;
    }

    _n->swapchains.Remove(scHandle);
}

Extent2D RHISwapchain::GetSwapchainExtent(SwapchainHandle scHandle) const
{
    if (!_n)
        return {};
    detail::Swapchain* sc = _n->swapchains.Get(scHandle);
    return sc ? sc->extent : Extent2D{};
}

Format RHISwapchain::GetSwapchainFormat(SwapchainHandle scHandle) const
{
    if (!_n)
        return Format::Undefined;
    detail::Swapchain* sc = _n->swapchains.Get(scHandle);
    return sc ? sc->format : Format::Undefined;
}

} // namespace lumora::rhi
