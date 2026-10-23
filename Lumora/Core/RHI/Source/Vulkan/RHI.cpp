#define VMA_IMPLEMENTATION
// Dynamic: Android's libvulkan does not export 1.3 entry points the static path would link against.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "VulkanRHI.h"

#include "Translate.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cwchar>
#endif

LUMORA_LOG_CATEGORY("RHI");

namespace lumora::rhi {
namespace detail {

void VkFail(VkResult r, const char* expr, const char* file, int line)
{
    LOG_FATAL("Vulkan call failed ({}): {} at {}:{}", static_cast<int>(r), expr, file, line);
    std::fprintf(stderr, "Vulkan call failed (%d): %s at %s:%d\n", static_cast<int>(r), expr, file, line);
#if defined(_WIN32)
    const char* noDialog = std::getenv("LUMORA_NO_DIALOGS");
    if (!noDialog || (std::strcmp(noDialog, "1") != 0 && std::strcmp(noDialog, "true") != 0)) {
        wchar_t buf[512];
        std::swprintf(buf, sizeof(buf) / sizeof(buf[0]),
                      L"Vulkan fatal error (%d):\n%hs\nat %hs:%d",
                      static_cast<int>(r), expr, file, line);
        MessageBoxW(nullptr, buf, L"Lumora - Vulkan Error", MB_OK | MB_ICONERROR);
    }
#endif
    std::abort();
}

void NativeRHI::Name(VkObjectType type, uint64_t handle, std::string_view name) const
{
    if (!setObjectName || name.empty())
        return;
    const std::string n(name);
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType   = type;
    info.objectHandle = handle;
    info.pObjectName  = n.c_str();
    setObjectName(device, &info);
}

TextureHandle NativeRHI::AdoptImage(VkImage image, VkFormat format, Extent2D extent, TextureUsage usage)
{
    Texture t{};
    t.image       = image;
    t.format      = format;
    t.desc.extent = {extent.width, extent.height, 1};
    t.desc.format = FromVk(format);
    t.desc.usage  = usage;
    const TextureHandle h = textures.Add(t);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image            = image;
    vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vi.format           = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view    = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vkCreateImageView(device, &vi, nullptr, &view));
    textures.Get(h)->defaultView = views.Add(TextureView{view, h});
    return h;
}

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

void NativeRHI::DestroyTextureNow(Texture& t)
{
    if (TextureView* v = views.Get(t.defaultView)) {
        vkDestroyImageView(device, v->view, nullptr);
        views.Remove(t.defaultView);
    }
    if (t.alloc)
        vmaDestroyImage(allocator, t.image, t.alloc);
}

} // namespace detail

using detail::NativeRHI;

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LOG_ERROR("[vk] {}", data->pMessage);
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LOG_WARN("[vk] {}", data->pMessage);
    return VK_FALSE;
}

bool HasExtension(const std::vector<VkExtensionProperties>& exts, const char* name)
{
    return std::any_of(exts.begin(), exts.end(),
                       [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

std::vector<VkExtensionProperties> DeviceExtensions(VkPhysicalDevice pd)
{
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, exts.data());
    return exts;
}

uint32_t InstanceApiVersion()
{
    uint32_t v = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&v);
    return std::min(v, static_cast<uint32_t>(VK_API_VERSION_1_3));
}

AdapterInfo Describe(VkPhysicalDevice pd, uint32_t index, uint32_t instanceVersion)
{
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(pd, &p);
    const auto exts = DeviceExtensions(pd);

    AdapterInfo a{};
    a.name       = p.deviceName;
    a.index      = index;
    a.apiVersion = std::min(p.apiVersion, instanceVersion);
    switch (p.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: a.type = DeviceType::Integrated; break;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: a.type = DeviceType::Discrete; break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: a.type = DeviceType::Virtual; break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: a.type = DeviceType::Cpu; break;
    default: a.type = DeviceType::Other; break;
    }
    a.pushDescriptor   = HasExtension(exts, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    a.dynamicRendering = a.apiVersion >= VK_API_VERSION_1_3 ||
                         (HasExtension(exts, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) &&
                          HasExtension(exts, VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME) &&
                          HasExtension(exts, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME));
    return a;
}

std::vector<VkPhysicalDevice> PhysicalDevices(VkInstance instance)
{
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    std::vector<VkPhysicalDevice> pds(n);
    vkEnumeratePhysicalDevices(instance, &n, pds.data());
    return pds;
}

VkInstance CreateInstance(uint32_t apiVersion, bool validation, bool& debugUtils)
{
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> available(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, available.data());

    std::vector<const char*> exts{VK_KHR_SURFACE_EXTENSION_NAME};
#if defined(_WIN32)
    exts.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__ANDROID__)
    exts.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif
    debugUtils = HasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debugUtils)
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::vector<const char*> layers;
    if (validation) {
        uint32_t ln = 0;
        vkEnumerateInstanceLayerProperties(&ln, nullptr);
        std::vector<VkLayerProperties> lp(ln);
        vkEnumerateInstanceLayerProperties(&ln, lp.data());
        const char* khronos = "VK_LAYER_KHRONOS_validation";
        if (std::any_of(lp.begin(), lp.end(), [&](auto& l) { return std::strcmp(l.layerName, khronos) == 0; }))
            layers.push_back(khronos);
        else
            LOG_WARN("Validation requested but VK_LAYER_KHRONOS_validation is not installed");
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Lumora";
    app.pEngineName      = "Lumora";
    app.apiVersion       = apiVersion;

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    ci.enabledLayerCount       = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames     = layers.data();

    VkInstance instance = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vkCreateInstance(&ci, nullptr, &instance));
    return instance;
}

int PickAdapter(const std::vector<AdapterInfo>& adapters, int requested)
{
    if (requested >= 0 && requested < static_cast<int>(adapters.size()))
        return requested;
    int best = -1;
    for (size_t i = 0; i < adapters.size(); ++i) {
        if (adapters[i].apiVersion < VK_API_VERSION_1_1)
            continue;
        if (best < 0 || (adapters[i].type == DeviceType::Discrete && adapters[best].type != DeviceType::Discrete))
            best = static_cast<int>(i);
    }
    return best;
}

void CreateDevice(NativeRHI& n)
{
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(n.physical, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(n.physical, &qn, qfs.data());
    n.queueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < qn; ++i)
        if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            n.queueFamily = i;
            break;
        }
    if (n.queueFamily == UINT32_MAX)
        detail::VkFail(VK_ERROR_INITIALIZATION_FAILED, "no graphics queue", __FILE__, __LINE__);

    const bool core13 = n.adapter.apiVersion >= VK_API_VERSION_1_3;
    std::vector<const char*> exts{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (n.adapter.pushDescriptor)
        exts.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    if (n.adapter.dynamicRendering && !core13) {
        exts.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        exts.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
        exts.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
    }

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dyn{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
    dyn.dynamicRendering = VK_TRUE;

    const float priority = 1.f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = n.queueFamily;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;

    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.pNext                   = n.adapter.dynamicRendering ? &dyn : nullptr;
    ci.queueCreateInfoCount    = 1;
    ci.pQueueCreateInfos       = &qci;
    ci.enabledExtensionCount   = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    LUMORA_VK_CHECK(vkCreateDevice(n.physical, &ci, nullptr, &n.device));
    vkGetDeviceQueue(n.device, n.queueFamily, 0, &n.queue);

    if (n.adapter.dynamicRendering) {
        n.cmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(
            vkGetDeviceProcAddr(n.device, core13 ? "vkCmdBeginRendering" : "vkCmdBeginRenderingKHR"));
        n.cmdEndRendering = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(
            vkGetDeviceProcAddr(n.device, core13 ? "vkCmdEndRendering" : "vkCmdEndRenderingKHR"));
    }
    if (n.adapter.pushDescriptor)
        n.cmdPushDescriptorSet = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(
            vkGetDeviceProcAddr(n.device, "vkCmdPushDescriptorSetKHR"));
}

void CreateFrames(NativeRHI& n, uint32_t count)
{
    n.frames.resize(std::max(1u, count));
    for (uint32_t i = 0; i < n.frames.size(); ++i) {
        detail::Frame& f = n.frames[i];
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = n.queueFamily;
        LUMORA_VK_CHECK(vkCreateCommandPool(n.device, &pci, nullptr, &f.pool));

        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool        = f.pool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        LUMORA_VK_CHECK(vkAllocateCommandBuffers(n.device, &ai, &f.cmd));

        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        LUMORA_VK_CHECK(vkCreateFence(n.device, &fci, nullptr, &f.fence));

        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        LUMORA_VK_CHECK(vkCreateSemaphore(n.device, &sci, nullptr, &f.acquired));

        if (!n.adapter.pushDescriptor) {
            // ponytail: fixed-size pool per frame; grow or chain pools if UI ever exceeds it.
            const VkDescriptorPoolSize sizes[] = {
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024},
                {VK_DESCRIPTOR_TYPE_SAMPLER, 1024},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256},
            };
            VkDescriptorPoolCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            dci.maxSets       = 4096;
            dci.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
            dci.pPoolSizes    = sizes;
            LUMORA_VK_CHECK(vkCreateDescriptorPool(n.device, &dci, nullptr, &f.descPool));
        }
        f.recorder = std::make_unique<CommandBuffer>(&n, f.cmd, i);
    }

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pci.queueFamilyIndex = n.queueFamily;
    LUMORA_VK_CHECK(vkCreateCommandPool(n.device, &pci, nullptr, &n.immediatePool));
}

} // namespace

std::vector<AdapterInfo> EnumerateAdapters()
{
    bool debugUtils = false;
    const uint32_t version = InstanceApiVersion();
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = version;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS)
        return {};
    (void)debugUtils;

    std::vector<AdapterInfo> out;
    const auto pds = PhysicalDevices(instance);
    for (uint32_t i = 0; i < pds.size(); ++i)
        out.push_back(Describe(pds[i], i, version));
    vkDestroyInstance(instance, nullptr);
    return out;
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
    auto n = std::make_unique<NativeRHI>();

    const uint32_t version = InstanceApiVersion();
    bool debugUtils        = false;
    n->instance            = CreateInstance(version, desc.enableValidation, debugUtils);

    if (debugUtils) {
        n->cmdBeginLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(n->instance, "vkCmdBeginDebugUtilsLabelEXT"));
        n->cmdEndLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(n->instance, "vkCmdEndDebugUtilsLabelEXT"));
        n->setObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(n->instance, "vkSetDebugUtilsObjectNameEXT"));
        n->debugLabels = desc.enableDebugLabels;
        if (desc.enableValidation) {
            auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(n->instance, "vkCreateDebugUtilsMessengerEXT"));
            VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mi.pfnUserCallback = DebugCallback;
            if (create)
                create(n->instance, &mi, nullptr, &n->messenger);
        }
    }

    const auto pds = PhysicalDevices(n->instance);
    std::vector<AdapterInfo> adapters;
    for (uint32_t i = 0; i < pds.size(); ++i)
        adapters.push_back(Describe(pds[i], i, version));
    const int pick = PickAdapter(adapters, desc.adapterIndex);
    if (pick < 0)
        detail::VkFail(VK_ERROR_INCOMPATIBLE_DRIVER, "no Vulkan 1.1 adapter", __FILE__, __LINE__);
    n->physical = pds[pick];
    n->adapter  = adapters[pick];
    LOG_INFO("Adapter: {} (Vulkan {}.{}, push descriptors: {}, dynamic rendering: {})", n->adapter.name,
             VK_API_VERSION_MAJOR(n->adapter.apiVersion), VK_API_VERSION_MINOR(n->adapter.apiVersion),
             n->adapter.pushDescriptor, n->adapter.dynamicRendering);

    CreateDevice(*n);

    VmaVulkanFunctions vkFns{};
    vkFns.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vkFns.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo ai{};
    ai.pVulkanFunctions = &vkFns;
    ai.physicalDevice   = n->physical;
    ai.device           = n->device;
    ai.instance         = n->instance;
    ai.vulkanApiVersion = n->adapter.apiVersion;
    LUMORA_VK_CHECK(vmaCreateAllocator(&ai, &n->allocator));

    CreateFrames(*n, desc.framesInFlight);
    _n = n.release();
}

void RHI::Shutdown()
{
    if (!_n)
        return;
    NativeRHI& n = *_n;
    vkDeviceWaitIdle(n.device);

    n.RunDeferred(true);

    n.swapchains.ForEachLive([&](detail::Swapchain& sc) {
        for (TextureHandle img : sc.images)
            if (detail::Texture* t = n.textures.Get(img))
                n.DestroyTextureNow(*t);
        for (VkSemaphore s : sc.renderDone)
            vkDestroySemaphore(n.device, s, nullptr);
        vkDestroySwapchainKHR(n.device, sc.swapchain, nullptr);
        vkDestroySurfaceKHR(n.instance, sc.surface, nullptr);
    });
    n.pipelines.ForEachLive([&](detail::Pipeline& p) {
        vkDestroyPipeline(n.device, p.pipeline, nullptr);
        vkDestroyPipelineLayout(n.device, p.layout, nullptr);
    });
    n.descriptorLayouts.ForEachLive(
        [&](detail::DescriptorLayout& l) { vkDestroyDescriptorSetLayout(n.device, l.layout, nullptr); });
    n.samplers.ForEachLive([&](detail::Sampler& s) { vkDestroySampler(n.device, s.sampler, nullptr); });
    n.textures.ForEachLive([&](detail::Texture& t) { n.DestroyTextureNow(t); });
    n.buffers.ForEachLive([&](detail::Buffer& b) { vmaDestroyBuffer(n.allocator, b.buffer, b.alloc); });
    for (auto& rp : n.renderPasses)
        vkDestroyRenderPass(n.device, rp.pass, nullptr);

    for (auto& f : n.frames) {
        if (f.descPool)
            vkDestroyDescriptorPool(n.device, f.descPool, nullptr);
        vkDestroySemaphore(n.device, f.acquired, nullptr);
        vkDestroyFence(n.device, f.fence, nullptr);
        vkDestroyCommandPool(n.device, f.pool, nullptr);
    }
    vkDestroyCommandPool(n.device, n.immediatePool, nullptr);
    vmaDestroyAllocator(n.allocator);
    vkDestroyDevice(n.device, nullptr);
    if (n.messenger) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(n.instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy)
            destroy(n.instance, n.messenger, nullptr);
    }
    vkDestroyInstance(n.instance, nullptr);
    delete _n;
    _n = nullptr;
}

const AdapterInfo& RHI::GetActiveAdapter() const
{
    return _n->adapter;
}

uint32_t RHI::GetFramesInFlight() const noexcept
{
    return _n ? static_cast<uint32_t>(_n->frames.size()) : 0;
}

void RHI::WaitIdle() const
{
    if (_n)
        vkDeviceWaitIdle(_n->device);
}

FrameContext RHI::BeginFrame(SwapchainHandle swapchain)
{
    NativeRHI& n     = *_n;
    detail::Frame& f = n.frames[n.frameSlot];
    LUMORA_VK_CHECK(vkWaitForFences(n.device, 1, &f.fence, VK_TRUE, UINT64_MAX));
    ++n.frameNumber;
    n.RunDeferred(false);
    if (f.descPool)
        vkResetDescriptorPool(n.device, f.descPool, 0);

    FrameContext ctx{};
    ctx.frameIndex = n.frameSlot;
    ctx.swapchain  = swapchain;
    ctx.cmd        = f.recorder.get();

    if (detail::Swapchain* sc = n.swapchains.Get(swapchain)) {
        constexpr uint64_t kAcquireTimeoutNs = 100'000'000ULL;
        const VkResult r = vkAcquireNextImageKHR(n.device, sc->swapchain, kAcquireTimeoutNs, f.acquired, VK_NULL_HANDLE,
                                                 &sc->imageIndex);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            ctx.swapchainOutOfDate = true;
        } else if (r == VK_ERROR_SURFACE_LOST_KHR) {
            ctx.swapchainOutOfDate = true;
        } else if (r == VK_TIMEOUT || r == VK_NOT_READY) {
            // No image this frame.
        } else {
            if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
                LUMORA_VK_CHECK(r);
            ctx.swapchainImage = sc->images[sc->imageIndex];
        }
    }

    LUMORA_VK_CHECK(vkResetCommandPool(n.device, f.pool, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    LUMORA_VK_CHECK(vkBeginCommandBuffer(f.cmd, &bi));
    return ctx;
}

void RHI::EndFrame(FrameContext& ctx)
{
    NativeRHI& n     = *_n;
    detail::Frame& f = n.frames[ctx.frameIndex];
    LUMORA_VK_CHECK(vkEndCommandBuffer(f.cmd));

    detail::Swapchain* sc = ctx.swapchainImage.IsValid() ? n.swapchains.Get(ctx.swapchain) : nullptr;
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &f.cmd;
    if (sc) {
        si.waitSemaphoreCount   = 1;
        si.pWaitSemaphores      = &f.acquired;
        si.pWaitDstStageMask    = &waitStage;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores    = &sc->renderDone[sc->imageIndex];
    }
    LUMORA_VK_CHECK(vkResetFences(n.device, 1, &f.fence));
    LUMORA_VK_CHECK(vkQueueSubmit(n.queue, 1, &si, f.fence));

    if (sc) {
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores    = &sc->renderDone[sc->imageIndex];
        pi.swapchainCount     = 1;
        pi.pSwapchains        = &sc->swapchain;
        pi.pImageIndices      = &sc->imageIndex;
        const VkResult r      = vkQueuePresentKHR(n.queue, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || r == VK_ERROR_SURFACE_LOST_KHR)
            ctx.swapchainOutOfDate = true;
        else
            LUMORA_VK_CHECK(r);
    }
    n.frameSlot = (n.frameSlot + 1) % static_cast<uint32_t>(n.frames.size());
}

void RHI::ImmediateSubmit(const std::function<void(CommandBuffer&)>& fn)
{
    NativeRHI& n = *_n;
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool        = n.immediatePool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd   = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vkAllocateCommandBuffers(n.device, &ai, &cmd));

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    LUMORA_VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    CommandBuffer recorder(&n, cmd, n.frameSlot);
    fn(recorder);
    LUMORA_VK_CHECK(vkEndCommandBuffer(cmd));

    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vkCreateFence(n.device, &fci, nullptr, &fence));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    LUMORA_VK_CHECK(vkQueueSubmit(n.queue, 1, &si, fence));
    LUMORA_VK_CHECK(vkWaitForFences(n.device, 1, &fence, VK_TRUE, UINT64_MAX));
    vkDestroyFence(n.device, fence, nullptr);
    vkFreeCommandBuffers(n.device, n.immediatePool, 1, &cmd);
}

} // namespace lumora::rhi
