#include "SampleHost.h"

#include <lumora/MountedApp.h>
#include <lumora/Text/FontSystem.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if LUMORA_SAMPLE_GPU
#include <EmbeddedShaders.h>
#include <lumora/log/Log.h>
#include <lumora/log/Spdlog.h>
#include <lumora/platform/SystemMetrics.h>
#include <lumora/platform/Window.h>
#include <lumora/rhi/Lumora.h>
#include <lumora/rhi/RHI.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <chrono>
#include <cstddef>
#include <fstream>
#include <thread>
#include <vector>

LUMORA_LOG_CATEGORY("SampleHost");
#endif

namespace lumora::samples {

namespace {

int RunHeadless(const SampleDesc& desc, int frames)
{
    FontSystem fonts;
    MountedApp app(desc.root(), &fonts);
    if (!desc.css.empty())
        app.SetStylesheetSource(desc.css);
    for (int f = 0; f < frames; ++f) {
        app.Tick(1.f / 60.f);
        app.Layout(static_cast<float>(desc.width), static_cast<float>(desc.height));
    }
    std::printf("%s: %d headless frames ok\n", desc.title.c_str(), frames);
    return 0;
}

#if LUMORA_SAMPLE_GPU
std::vector<std::byte> ReadFile(const char* path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return {};
    std::vector<std::byte> bytes(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

int RunWindowed(const SampleDesc& desc, bool validation, int exitAfter)
{
    log::InstallSpdlog();

    const bool simulateNoVulkan = []() {
        const char* s = std::getenv("LUMORA_SIMULATE_NO_VULKAN");
        return s && (std::strcmp(s, "1") == 0 || std::strcmp(s, "true") == 0);
    }();

    bool hasVulkan11 = false;
    if (!simulateNoVulkan) {
        for (const auto& a : rhi::EnumerateAdapters()) {
            if (a.apiVersion >= ((1u << 22) | (1u << 12))) {
                hasVulkan11 = true;
                break;
            }
        }
    }

    if (!hasVulkan11) {
        LOG_FATAL("This app needs a GPU with Vulkan 1.1 drivers");
        std::fprintf(stderr, "This app needs a GPU with Vulkan 1.1 drivers\n");
#if defined(_WIN32)
        const char* noDialog = std::getenv("LUMORA_NO_DIALOGS");
        if (!noDialog || (std::strcmp(noDialog, "1") != 0 && std::strcmp(noDialog, "true") != 0)) {
            MessageBoxW(nullptr, L"This app needs a GPU with Vulkan 1.1 drivers", L"Lumora", MB_OK | MB_ICONERROR);
        }
#endif
        return 1;
    }

    auto window = platform::Window::Create(
        {.title = desc.title, .width = static_cast<uint32_t>(desc.width), .height = static_cast<uint32_t>(desc.height)});
    const auto native = window->GetNativeHandle();

    rhi::RHI device;
    device.Init({.enableValidation = validation, .enableDebugLabels = validation});

    rhi::SwapchainDescription sc{};
    sc.nativeWindow = {native.window, native.instance,
                       native.backend == platform::WindowBackend::Android ? rhi::NativeWindowKind::Android : rhi::NativeWindowKind::Win32};
    sc.extent       = {window->GetWidth(), window->GetHeight()};
    const rhi::SwapchainHandle swapchain = device.Swapchain().CreateSwapchain(sc);

    auto ui = MakeLumora({
        .rhi         = &device,
        .spirvLoader = [](std::string_view name) {
            auto span = lumora::EmbeddedSpirv(name);
            return std::vector<std::byte>(span.begin(), span.end());
        },
        .colorFormat = device.Swapchain().GetSwapchainFormat(swapchain),
    });
    ui->SetContentScale(window->GetDpiScale());

    const platform::DefaultFontFiles fontFiles = platform::GetDefaultUiFont();
    auto regular = ReadFile(fontFiles.regular.string().c_str());
    auto bold    = ReadFile(fontFiles.bold.string().c_str());
    if (!regular.empty()) {
        ui->Fonts().RegisterFont(regular, {.family = "Sans"});
        ui->Fonts().SetDefaultFamily("Sans");
        if (!bold.empty())
            ui->Fonts().RegisterFont(bold, {.family = "Sans", .bold = true});
    } else {
        LOG_WARN("No system font found at {}", fontFiles.regular.string());
    }

    auto mounted = ui->Mount(desc.root);
    if (!desc.css.empty())
        ui->SetStylesheetSource(desc.css);

    const auto inputSub = window->InputEvents().Subscribe([&](const platform::InputEvent& e) { ui->Dispatch(e); });
    const auto winSub   = window->Events().Subscribe([&](const platform::WindowEvent& e) {
        if (e.type == platform::WindowEventType::DpiChanged)
            ui->SetContentScale(e.dpiScale);
    });

    auto last = std::chrono::steady_clock::now();
    for (int frame = 0; !window->ShouldClose() && frame != exitAfter; ++frame) {
        window->PumpEvents();
        const auto now = std::chrono::steady_clock::now();
        ui->Update(std::chrono::duration<float>(now - last).count());
        if (mounted)
            window->SetTextInputActive(mounted->WantsTextInput());
        last = now;

        rhi::FrameContext ctx = device.BeginFrame(swapchain);
        if (ctx.swapchainImage.IsValid()) {
            const rhi::Extent2D extent = device.Swapchain().GetSwapchainExtent(swapchain);
            ui->Layout(static_cast<float>(extent.width), static_cast<float>(extent.height));
            rhi::CommandBuffer& cmd = *ctx.cmd;
            cmd.TransitionTexture(ctx.swapchainImage, rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment);
            ui->Paint(cmd, ctx.frameIndex, device.Resources().GetDefaultView(ctx.swapchainImage), extent,
                      rhi::Color{0.f, 0.f, 0.f, 1.f});
            cmd.TransitionTexture(ctx.swapchainImage, rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::PresentSrc);
        }
        device.EndFrame(ctx);
        if (ctx.swapchainOutOfDate)
            device.Swapchain().RecreateSwapchain(swapchain, {window->GetWidth(), window->GetHeight()});
        // Nothing presented (minimized / zero-sized surface) means no vsync wait; throttle instead of spinning.
        if (!ctx.swapchainImage.IsValid())
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    device.WaitIdle();
    ui = nullptr;
    device.Swapchain().DestroySwapchain(swapchain);
    device.Shutdown();
    return 0;
}
#endif

} // namespace

int RunSample(const SampleDesc& desc, int argc, char** argv)
{
    int  headlessFrames = -1;
    int  exitAfter      = -1;
    bool validation     = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless-frames") == 0 && i + 1 < argc)
            headlessFrames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--exit-after") == 0 && i + 1 < argc)
            exitAfter = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--validation") == 0)
            validation = true;
    }
#if LUMORA_SAMPLE_GPU
    if (headlessFrames < 0)
        return RunWindowed(desc, validation, exitAfter);
#else
    (void)validation;
    (void)exitAfter;
    if (headlessFrames < 0)
        headlessFrames = 3;
#endif
    return RunHeadless(desc, headlessFrames);
}

} // namespace lumora::samples
