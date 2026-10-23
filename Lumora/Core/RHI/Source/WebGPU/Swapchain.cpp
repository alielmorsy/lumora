#include "WebGPURHI.h"

LUMORA_LOG_CATEGORY("RHI");

namespace lumora::rhi {

namespace {

void Configure(detail::NativeRHI& n, detail::Swapchain& sc, Extent2D extent)
{
    WGPUSurfaceConfiguration cfg{};
    cfg.device      = n.device;
    cfg.format      = sc.wgpuFormat;
    cfg.usage       = WGPUTextureUsage_RenderAttachment;
    cfg.width       = extent.width;
    cfg.height      = extent.height;
    cfg.alphaMode   = WGPUCompositeAlphaMode_Opaque;
    cfg.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(sc.surface, &cfg);
    sc.extent = extent;
}

} // namespace

SwapchainHandle RHISwapchain::CreateSwapchain(const SwapchainDescription& desc)
{
    if (!_n)
        return {};
    detail::NativeRHI& n = *_n;
    if (desc.nativeWindow.kind != NativeWindowKind::Web || !desc.nativeWindow.window) {
        LOG_ERROR("WebGPU swapchain needs a NativeWindowKind::Web canvas selector");
        return {};
    }
    if (desc.extent.width == 0 || desc.extent.height == 0) {
        LOG_ERROR("Failed to create initial swapchain (zero surface extent)");
        return {};
    }

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas{};
    canvas.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvas.selector    = detail::Str(static_cast<const char*>(desc.nativeWindow.window));
    WGPUSurfaceDescriptor sd{};
    sd.nextInChain = &canvas.chain;

    detail::Swapchain sc{};
    sc.surface = wgpuInstanceCreateSurface(n.instance, &sd);
    if (!sc.surface) {
        LOG_ERROR("wgpuInstanceCreateSurface failed for '{}'", static_cast<const char*>(desc.nativeWindow.window));
        return {};
    }

    WGPUSurfaceCapabilities caps{};
    wgpuSurfaceGetCapabilities(sc.surface, n.adapter, &caps);
    sc.wgpuFormat = caps.formatCount ? caps.formats[0] : WGPUTextureFormat_BGRA8Unorm;
    for (size_t i = 0; i < caps.formatCount; ++i)
        if (detail::FromWgpu(caps.formats[i]) == desc.preferredFormat)
            sc.wgpuFormat = caps.formats[i];
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    sc.format = detail::FromWgpu(sc.wgpuFormat);

    Configure(n, sc, desc.extent);
    return n.swapchains.Add(sc);
}

void RHISwapchain::RecreateSwapchain(SwapchainHandle scHandle, Extent2D extent)
{
    if (!_n || extent.width == 0 || extent.height == 0)
        return;
    if (detail::Swapchain* sc = _n->swapchains.Get(scHandle))
        Configure(*_n, *sc, extent);
}

void RHISwapchain::DestroySwapchain(SwapchainHandle scHandle)
{
    if (!_n)
        return;
    detail::Swapchain sc = _n->swapchains.Remove(scHandle);
    if (!sc.surface)
        return;
    if (detail::Texture* t = _n->textures.Get(sc.current)) {
        _n->DestroyTextureNow(*t, false);
        _n->textures.Remove(sc.current);
    }
    wgpuSurfaceUnconfigure(sc.surface);
    wgpuSurfaceRelease(sc.surface);
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
