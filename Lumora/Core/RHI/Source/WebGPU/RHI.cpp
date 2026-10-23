#include "WebGPURHI.h"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <cstring>

LUMORA_LOG_CATEGORY("RHI");

EM_JS(int, LumoraHasNavigatorGpu, (), { return (typeof navigator !== 'undefined' && !!navigator.gpu) ? 1 : 0; });

namespace lumora::rhi {
namespace detail {

namespace {

constexpr uint32_t kPushSlice    = 256;
constexpr uint64_t kPushRingSize = 64 * 1024;

void ReleaseRing(PushRing& ring)
{
    for (auto& [size, group] : ring.groups)
        wgpuBindGroupRelease(group);
    ring.groups.clear();
    if (ring.buffer) {
        wgpuBufferDestroy(ring.buffer);
        wgpuBufferRelease(ring.buffer);
    }
    ring.buffer = nullptr;
}

} // namespace

void NativeRHI::RunDeferred(bool all)
{
    const uint64_t inFlight = frames.size();
    std::vector<Deferred> keep;
    std::vector<Deferred> run;
    for (auto& d : deferred)
        (all || d.frame + inFlight <= frameNumber ? run : keep).push_back(std::move(d));
    deferred = std::move(keep);
    for (auto& d : run)
        d.fn();
}

void NativeRHI::ClearBindGroups()
{
    for (auto& [key, group] : bindGroups)
        wgpuBindGroupRelease(group);
    bindGroups.clear();
}

TextureHandle NativeRHI::AdoptTexture(WGPUTexture texture, Format format, Extent2D extent, TextureUsage usage)
{
    Texture t{};
    t.texture     = texture;
    t.desc.extent = {extent.width, extent.height, 1};
    t.desc.format = format;
    t.desc.usage  = usage;
    const TextureHandle h = textures.Add(t);
    textures.Get(h)->defaultView = views.Add(TextureView{wgpuTextureCreateView(texture, nullptr), h});
    return h;
}

void NativeRHI::DestroyTextureNow(Texture& t, bool destroy)
{
    if (TextureView* v = views.Get(t.defaultView)) {
        wgpuTextureViewRelease(v->view);
        views.Remove(t.defaultView);
    }
    if (destroy)
        wgpuTextureDestroy(t.texture);
    wgpuTextureRelease(t.texture);
}

uint32_t NativeRHI::PushSlice(PushRing& ring, const void* data, uint32_t size)
{
    if (ring.used + kPushSlice > ring.shadow.size()) {
        // Commands recorded so far keep the old buffer alive; upload what they read, then start a bigger one.
        const uint64_t grown = std::max<uint64_t>(kPushRingSize, ring.shadow.size() * 2);
        if (ring.buffer) {
            UploadPush(ring);
            WGPUBuffer old = ring.buffer;
            Defer([old]() {
                wgpuBufferDestroy(old);
                wgpuBufferRelease(old);
            });
            ring.buffer = nullptr;
            for (auto& [s, group] : ring.groups)
                wgpuBindGroupRelease(group);
            ring.groups.clear();
        }
        WGPUBufferDescriptor bd{};
        bd.label    = Str("Lumora.PushRing");
        bd.usage    = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        bd.size     = grown;
        ring.buffer = wgpuDeviceCreateBuffer(device, &bd);
        ring.shadow.assign(grown, std::byte{0});
        ring.used = 0;
    }
    const uint32_t offset = static_cast<uint32_t>(ring.used);
    std::memcpy(ring.shadow.data() + offset, data, size);
    ring.used += kPushSlice;
    return offset;
}

WGPUBindGroup NativeRHI::PushGroup(PushRing& ring, uint32_t size)
{
    for (auto& [s, group] : ring.groups)
        if (s == size)
            return group;
    WGPUBindGroupEntry e{};
    e.binding = 0;
    e.buffer  = ring.buffer;
    e.offset  = 0;
    e.size    = size;
    WGPUBindGroupDescriptor bd{};
    bd.layout     = pushLayout;
    bd.entryCount = 1;
    bd.entries    = &e;
    WGPUBindGroup group = wgpuDeviceCreateBindGroup(device, &bd);
    ring.groups.emplace_back(size, group);
    return group;
}

void NativeRHI::UploadPush(PushRing& ring)
{
    if (ring.buffer && ring.used)
        wgpuQueueWriteBuffer(queue, ring.buffer, 0, ring.shadow.data(), ring.used);
}

} // namespace detail

using detail::NativeRHI;

namespace {

void OnUncapturedError(const WGPUDevice*, WGPUErrorType type, WGPUStringView message, void*, void*)
{
    LOG_ERROR("[webgpu] error {}: {}", static_cast<int>(type), detail::Str(message));
}

void OnDeviceLost(const WGPUDevice*, WGPUDeviceLostReason reason, WGPUStringView message, void*, void*)
{
    if (reason != WGPUDeviceLostReason_Destroyed)
        LOG_ERROR("[webgpu] device lost ({}): {}", static_cast<int>(reason), detail::Str(message));
}

void Wait(WGPUInstance instance, WGPUFuture future)
{
    WGPUFutureWaitInfo info{future, false};
    wgpuInstanceWaitAny(instance, 1, &info, UINT64_MAX);
}

} // namespace

std::vector<AdapterInfo> EnumerateAdapters()
{
    if (!LumoraHasNavigatorGpu())
        return {};
    AdapterInfo a{};
    a.name       = "WebGPU";
    a.apiVersion = (1u << 22) | (3u << 12);
    return {a};
}

RHI::RHI() = default;

RHI::~RHI()
{
    Shutdown();
}

void RHI::Init(const RHIInitDescription& desc)
{
    if (_n)
        return;
    if (!LumoraHasNavigatorGpu()) {
        LOG_FATAL("WebGPU is not available in this browser");
        return;
    }
    auto n = std::make_unique<NativeRHI>();

    const WGPUInstanceFeatureName timedWait = WGPUInstanceFeatureName_TimedWaitAny;
    WGPUInstanceDescriptor id{};
    id.requiredFeatureCount = 1;
    id.requiredFeatures     = &timedWait;
    n->instance             = wgpuCreateInstance(&id);
    if (!n->instance) {
        LOG_FATAL("wgpuCreateInstance failed (TimedWaitAny needs -sASYNCIFY)");
        return;
    }

    WGPURequestAdapterOptions ao{};
    ao.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo acb{};
    acb.mode      = WGPUCallbackMode_WaitAnyOnly;
    acb.userdata1 = n.get();
    acb.callback  = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* ud, void*) {
        if (status == WGPURequestAdapterStatus_Success)
            static_cast<NativeRHI*>(ud)->adapter = adapter;
        else
            LOG_ERROR("[webgpu] requestAdapter failed: {}", detail::Str(message));
    };
    Wait(n->instance, wgpuInstanceRequestAdapter(n->instance, &ao, acb));
    if (!n->adapter) {
        LOG_FATAL("No WebGPU adapter");
        return;
    }

    WGPUAdapterInfo ai{};
    if (wgpuAdapterGetInfo(n->adapter, &ai) == WGPUStatus_Success) {
        n->info.name = std::string(detail::Str(ai.description));
        if (n->info.name.empty())
            n->info.name = std::string(detail::Str(ai.vendor)) + " " + std::string(detail::Str(ai.architecture));
        switch (ai.adapterType) {
        case WGPUAdapterType_DiscreteGPU: n->info.type = DeviceType::Discrete; break;
        case WGPUAdapterType_IntegratedGPU: n->info.type = DeviceType::Integrated; break;
        case WGPUAdapterType_CPU: n->info.type = DeviceType::Cpu; break;
        default: break;
        }
        wgpuAdapterInfoFreeMembers(ai);
    }
    n->info.apiVersion = (1u << 22) | (3u << 12);

    WGPUDeviceDescriptor dd{};
    dd.label                                 = detail::Str("Lumora");
    dd.deviceLostCallbackInfo.mode           = WGPUCallbackMode_AllowSpontaneous;
    dd.deviceLostCallbackInfo.callback       = OnDeviceLost;
    dd.uncapturedErrorCallbackInfo.callback  = OnUncapturedError;
    WGPURequestDeviceCallbackInfo dcb{};
    dcb.mode      = WGPUCallbackMode_WaitAnyOnly;
    dcb.userdata1 = n.get();
    dcb.callback  = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* ud, void*) {
        if (status == WGPURequestDeviceStatus_Success)
            static_cast<NativeRHI*>(ud)->device = device;
        else
            LOG_ERROR("[webgpu] requestDevice failed: {}", detail::Str(message));
    };
    Wait(n->instance, wgpuAdapterRequestDevice(n->adapter, &dd, dcb));
    if (!n->device) {
        LOG_FATAL("No WebGPU device");
        return;
    }
    n->queue       = wgpuDeviceGetQueue(n->device);
    n->debugLabels = desc.enableDebugLabels;
    LOG_INFO("Adapter: {} (WebGPU)", n->info.name);

    WGPUBindGroupLayoutEntry pe{};
    pe.binding                 = 0;
    pe.visibility              = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    pe.buffer.type             = WGPUBufferBindingType_Uniform;
    pe.buffer.hasDynamicOffset = true;
    WGPUBindGroupLayoutDescriptor pld{};
    pld.label      = detail::Str("Lumora.PushLayout");
    pld.entryCount = 1;
    pld.entries    = &pe;
    n->pushLayout  = wgpuDeviceCreateBindGroupLayout(n->device, &pld);

    n->frames.resize(std::max(1u, desc.framesInFlight));
    for (uint32_t i = 0; i < n->frames.size(); ++i)
        n->frames[i].recorder = std::make_unique<CommandBuffer>(n.get(), &n->frames[i].rec, i);
    _n = n.release();
}

void RHI::Shutdown()
{
    if (!_n)
        return;
    NativeRHI& n = *_n;
    n.RunDeferred(true);
    n.ClearBindGroups();

    n.swapchains.ForEachLive([&](detail::Swapchain& sc) {
        if (detail::Texture* t = n.textures.Get(sc.current))
            n.DestroyTextureNow(*t, false);
        n.textures.Remove(sc.current);
        wgpuSurfaceUnconfigure(sc.surface);
        wgpuSurfaceRelease(sc.surface);
    });
    n.pipelines.ForEachLive([](detail::Pipeline& p) { wgpuRenderPipelineRelease(p.pipeline); });
    n.descriptorLayouts.ForEachLive([](detail::DescriptorLayout& l) { wgpuBindGroupLayoutRelease(l.layout); });
    n.samplers.ForEachLive([](detail::Sampler& s) { wgpuSamplerRelease(s.sampler); });
    n.textures.ForEachLive([&](detail::Texture& t) { n.DestroyTextureNow(t, true); });
    n.buffers.ForEachLive([](detail::Buffer& b) {
        wgpuBufferDestroy(b.buffer);
        wgpuBufferRelease(b.buffer);
    });
    for (auto& f : n.frames)
        detail::ReleaseRing(f.push);
    wgpuBindGroupLayoutRelease(n.pushLayout);
    wgpuQueueRelease(n.queue);
    wgpuDeviceDestroy(n.device);
    wgpuDeviceRelease(n.device);
    wgpuAdapterRelease(n.adapter);
    wgpuInstanceRelease(n.instance);
    delete _n;
    _n = nullptr;
}

const AdapterInfo& RHI::GetActiveAdapter() const
{
    return _n->info;
}

uint32_t RHI::GetFramesInFlight() const noexcept
{
    return _n ? static_cast<uint32_t>(_n->frames.size()) : 0;
}

// Queue work is ordered and destruction is deferred, so there is nothing to wait for on the CPU.
void RHI::WaitIdle() const {}

FrameContext RHI::BeginFrame(SwapchainHandle swapchain)
{
    NativeRHI& n     = *_n;
    detail::Frame& f = n.frames[n.frameSlot];
    ++n.frameNumber;
    n.RunDeferred(false);
    f.push.used = 0;

    FrameContext ctx{};
    ctx.frameIndex = n.frameSlot;
    ctx.swapchain  = swapchain;
    ctx.cmd        = f.recorder.get();

    if (detail::Swapchain* sc = n.swapchains.Get(swapchain)) {
        WGPUSurfaceTexture st{};
        wgpuSurfaceGetCurrentTexture(sc->surface, &st);
        if (st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
            st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            const Extent2D size{wgpuTextureGetWidth(st.texture), wgpuTextureGetHeight(st.texture)};
            sc->current = n.AdoptTexture(st.texture, sc->format, size, TextureUsage::ColorAttachment);
            ctx.swapchainImage     = sc->current;
            ctx.swapchainOutOfDate = st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
                                     !(size == sc->extent);
        } else {
            if (st.texture)
                wgpuTextureRelease(st.texture);
            ctx.swapchainOutOfDate = st.status != WGPUSurfaceGetCurrentTextureStatus_Timeout;
        }
    }

    f.rec           = {};
    f.rec.encoder   = wgpuDeviceCreateCommandEncoder(n.device, nullptr);
    return ctx;
}

void RHI::EndFrame(FrameContext& ctx)
{
    NativeRHI& n     = *_n;
    detail::Frame& f = n.frames[ctx.frameIndex];

    n.UploadPush(f.push);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(f.rec.encoder, nullptr);
    wgpuQueueSubmit(n.queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(f.rec.encoder);
    f.rec.encoder = nullptr;

    // The browser presents the surface texture once control returns to it.
    if (detail::Swapchain* sc = n.swapchains.Get(ctx.swapchain); sc && sc->current.IsValid()) {
        if (detail::Texture* t = n.textures.Get(sc->current))
            n.DestroyTextureNow(*t, false);
        n.textures.Remove(sc->current);
        sc->current = {};
    }
    n.frameSlot = (n.frameSlot + 1) % static_cast<uint32_t>(n.frames.size());
}

void RHI::ImmediateSubmit(const std::function<void(CommandBuffer&)>& fn)
{
    NativeRHI& n = *_n;
    detail::Recorder rec{};
    rec.encoder = wgpuDeviceCreateCommandEncoder(n.device, nullptr);
    CommandBuffer recorder(&n, &rec, n.frameSlot);
    fn(recorder);
    n.UploadPush(n.frames[n.frameSlot].push);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(rec.encoder, nullptr);
    wgpuQueueSubmit(n.queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(rec.encoder);
}

} // namespace lumora::rhi
