#include "SampleHost.h"

#include <lumora/Debug/FrameCounters.h>
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
#include <lumora/platform/Thread.h>
#include <lumora/platform/Window.h>
#include <lumora/rhi/Lumora.h>
#include <lumora/rhi/RHI.h>
#include <lumora/rhi/RenderThread.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <lumora/rhi/CommandBuffer.h>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#if defined(__ANDROID__)
#include <android/log.h>
#include <sys/system_properties.h>
#define PERF_PRINT(...) __android_log_print(ANDROID_LOG_INFO, "LumoraPerf", __VA_ARGS__)
#else
#define PERF_PRINT(...) (std::printf(__VA_ARGS__), std::printf("\n"), std::fflush(stdout))
#endif

#include <atomic>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <mutex>
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
        if (desc.onFrame)
            desc.onFrame(app, 1.f / 60.f);
        app.Tick(1.f / 60.f);
        app.Layout(static_cast<float>(desc.width), static_cast<float>(desc.height));
    }
    std::printf("%s: %d headless frames ok\n", desc.title.c_str(), frames);
    return 0;
}

#if LUMORA_SAMPLE_GPU
#if !defined(__EMSCRIPTEN__)
static bool IsPerfEnabled()
{
#if defined(__ANDROID__)
    char val[PROP_VALUE_MAX]{};
    __system_property_get("debug.lumora.perf", val);
    return std::strcmp(val, "1") == 0;
#else
    const char* s = std::getenv("LUMORA_PERF");
    return s && (std::strcmp(s, "1") == 0 || std::strcmp(s, "true") == 0);
#endif
}
#endif

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

rhi::NativeWindowHandles ToRhiHandles(const platform::NativeWindowHandle& h)
{
    rhi::NativeWindowKind kind = rhi::NativeWindowKind::Win32;
    if (h.backend == platform::WindowBackend::Android)
        kind = rhi::NativeWindowKind::Android;
    else if (h.backend == platform::WindowBackend::Web)
        kind = rhi::NativeWindowKind::Web;
    else if (h.backend == platform::WindowBackend::Xlib)
        kind = rhi::NativeWindowKind::Xlib;
    else if (h.backend == platform::WindowBackend::Wayland)
        kind = rhi::NativeWindowKind::Wayland;
    return {.window = h.window, .instance = h.instance, .kind = kind};
}

#if defined(__EMSCRIPTEN__)
struct WebApp
{
    Ref<platform::Window> window;
    rhi::RHI device;
    rhi::SwapchainHandle swapchain;
    Ref<Lumora> ui;
    MountedApp* mounted = nullptr;
    FramePacket packet;
    bool packetPending = false;
    Subscription inputSub;
    Subscription winSub;
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    int frameCount = 0;
    int exitAfter = -1;
    std::function<void(MountedApp&, float)> onFrame;
};

// One browser frame: the same sequence RenderThread runs, inline on the main thread.
void WebFrame(void* arg)
{
    WebApp& app = *static_cast<WebApp*>(arg);
    app.window->PumpEvents();

    const rhi::Extent2D canvas{app.window->GetWidth(), app.window->GetHeight()};
    if (canvas.width == 0 || canvas.height == 0)
        return;

    if (!app.packetPending) {
        rhi::Extent2D extent = app.device.Swapchain().GetSwapchainExtent(app.swapchain);
        if (extent.width != canvas.width || extent.height != canvas.height) {
            app.device.Swapchain().RecreateSwapchain(app.swapchain, canvas);
            extent = canvas;
        }

        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - app.last).count();
        app.last = now;

        if (app.onFrame && app.mounted)
            app.onFrame(*app.mounted, dt);
        app.ui->Update(dt);
        if (app.mounted)
            app.window->SetTextInputActive(app.mounted->WantsTextInput());
        app.ui->Layout(static_cast<float>(extent.width), static_cast<float>(extent.height));
        app.ui->RecordFrame(app.packet, extent);
        app.packetPending = true;
    }

    rhi::FrameContext ctx = app.device.BeginFrame(app.swapchain);
    if (ctx.swapchainImage.IsValid()) {
        rhi::CommandBuffer& cmd = *ctx.cmd;
        cmd.TransitionTexture(ctx.swapchainImage, rhi::ImageLayout::Undefined, rhi::ImageLayout::ColorAttachment);
        app.ui->RenderFrame(app.packet, cmd, ctx.frameIndex, app.device.Resources().GetDefaultView(ctx.swapchainImage),
                            app.device.Swapchain().GetSwapchainExtent(app.swapchain), rhi::Color{0.f, 0.f, 0.f, 1.f});
        cmd.TransitionTexture(ctx.swapchainImage, rhi::ImageLayout::ColorAttachment, rhi::ImageLayout::PresentSrc);
        app.packetPending = false;
    }
    app.device.EndFrame(ctx);

    if (ctx.swapchainOutOfDate)
        app.device.Swapchain().RecreateSwapchain(app.swapchain, canvas);

    if (app.exitAfter > 0 && ++app.frameCount >= app.exitAfter)
        emscripten_cancel_main_loop();
}

// Returns after scheduling the frame loop; the runtime stays alive (-sEXIT_RUNTIME=0) and owns `app`.
int RunWindowed(const SampleDesc& desc, bool validation, int exitAfter)
{
    log::InstallSpdlog();

    if (rhi::EnumerateAdapters().empty()) {
        LOG_FATAL("This app needs a browser with WebGPU");
        std::fprintf(stderr, "This app needs a browser with WebGPU\n");
        return 1;
    }

    auto* app = new WebApp();
    app->exitAfter = exitAfter;
    app->onFrame   = desc.onFrame;
    app->window = platform::Window::Create(
        {.title = desc.title, .width = static_cast<uint32_t>(desc.width), .height = static_cast<uint32_t>(desc.height)});
    app->device.Init({.enableValidation = validation, .enableDebugLabels = validation});

    rhi::SwapchainDescription sc{};
    sc.nativeWindow = ToRhiHandles(app->window->GetNativeHandle());
    sc.extent       = {app->window->GetWidth(), app->window->GetHeight()};
    app->swapchain  = app->device.Swapchain().CreateSwapchain(sc);

    app->ui = MakeLumora({
        .rhi         = &app->device,
        .spirvLoader = [](std::string_view name) {
            auto span = lumora::EmbeddedSpirv(name);
            return std::vector<std::byte>(span.begin(), span.end());
        },
        .colorFormat = app->device.Swapchain().GetSwapchainFormat(app->swapchain),
    });
    app->ui->SetContentScale(app->window->GetDpiScale());

    const platform::DefaultFontFiles fontFiles = platform::GetDefaultUiFont();
    auto regular = ReadFile(fontFiles.regular.string().c_str());
    auto bold    = ReadFile(fontFiles.bold.string().c_str());
    if (!regular.empty()) {
        app->ui->Fonts().RegisterFont(regular, {.family = "Sans"});
        app->ui->Fonts().SetDefaultFamily("Sans");
        if (!bold.empty())
            app->ui->Fonts().RegisterFont(bold, {.family = "Sans", .bold = true});
    } else {
        LOG_WARN("No font found at {}", fontFiles.regular.string());
    }

    app->mounted = app->ui->Mount(desc.root);
    if (!desc.css.empty())
        app->ui->SetStylesheetSource(desc.css);

    app->inputSub = app->window->InputEvents().Subscribe([app](const platform::InputEvent& e) { app->ui->Dispatch(e); });
    app->winSub   = app->window->Events().Subscribe([app](const platform::WindowEvent& e) {
        if (e.type == platform::WindowEventType::DpiChanged)
            app->ui->SetContentScale(e.dpiScale);
    });

    emscripten_set_main_loop_arg(WebFrame, app, 0, false);
    return 0;
}
#else
struct EventInbox
{
    std::mutex mutex;
    std::vector<platform::InputEvent> inputEvents;
    std::vector<platform::WindowEvent> windowEvents;
};

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
    sc.nativeWindow = ToRhiHandles(native);
    sc.extent       = {window->GetWidth(), window->GetHeight()};
    const rhi::SwapchainHandle swapchain = device.Swapchain().CreateSwapchain(sc);

    const float initialDpi = window->GetDpiScale();
    const uint32_t initWidth = window->GetWidth();
    const uint32_t initHeight = window->GetHeight();
    std::atomic<uint32_t> windowWidth = initWidth;
    std::atomic<uint32_t> windowHeight = initHeight;

    EventInbox inbox;
    std::atomic<bool> quit = false;
    RenderThread* activeRenderThread = nullptr;
    std::mutex rtMutex;

    const auto inputSub = window->InputEvents().Subscribe([&](const platform::InputEvent& e) {
        std::lock_guard<std::mutex> lock(inbox.mutex);
        inbox.inputEvents.push_back(e);
    });

    const auto winSub = window->Events().Subscribe([&](const platform::WindowEvent& e) {
        if (e.type == platform::WindowEventType::Resize) {
            windowWidth.store(e.width);
            windowHeight.store(e.height);
        }
        if (e.type == platform::WindowEventType::Close) {
            quit = true;
        } else if (e.type == platform::WindowEventType::SurfaceDestroyed) {
            std::lock_guard<std::mutex> lk(rtMutex);
            if (activeRenderThread) {
                activeRenderThread->LoseSurface();
            }
        } else if (e.type == platform::WindowEventType::SurfaceCreated) {
            std::lock_guard<std::mutex> lk(rtMutex);
            if (activeRenderThread) {
                activeRenderThread->SetSurface(ToRhiHandles(window->GetNativeHandle()));
            }
        } else {
            std::lock_guard<std::mutex> lock(inbox.mutex);
            inbox.windowEvents.push_back(e);
        }
    });

    platform::Thread uiThread;
    uiThread.Start(
        "LumoraUI",
        [&] {
            auto ui = MakeLumora({
                .rhi         = &device,
                .spirvLoader = [](std::string_view name) {
                    auto span = lumora::EmbeddedSpirv(name);
                    return std::vector<std::byte>(span.begin(), span.end());
                },
                .colorFormat = device.Swapchain().GetSwapchainFormat(swapchain),
            });
            ui->SetContentScale(initialDpi);

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

            RenderThread renderThread(RenderThreadDesc{
                .device = device,
                .ui = *ui,
                .swapchain = swapchain,
                .getWindowExtent = [&] { return rhi::Extent2D{windowWidth.load(), windowHeight.load()}; },
            });
            {
                std::lock_guard<std::mutex> lk(rtMutex);
                activeRenderThread = &renderThread;
            }
            renderThread.Start();

            const bool perf = IsPerfEnabled();
            using Clock = std::chrono::steady_clock;
            auto ms = [](Clock::time_point a, Clock::time_point b) {
                return std::chrono::duration<double, std::milli>(b - a).count();
            };

            double tUiWait = 0.0, tUiWork = 0.0;
            std::uint64_t cEmitVisited = 0, cEmitRecorded = 0, cReplayed = 0, cEffects = 0, cFrameCb = 0,
                          cCascade = 0, cTextMeasure = 0, cGlyph = 0, cSolve = 0, cRefreshDirty = 0,
                          cLayoutRuns = 0, cIdleBails = 0, cReused = 0, cLayers = 0, cBytes = 0;
            int perfFrameCount = 0;
            constexpr int kPerfWindow = 300;

            auto last = Clock::now();
            int frameCount = 0;

            while (!quit.load()) {
                const auto tWaitStart = Clock::now();
                rhi::Extent2D extent{};
                if (!renderThread.WaitFrameRequest(extent)) {
                    break;
                }
                const auto tWorkStart = Clock::now();

                std::vector<platform::InputEvent> curInput;
                std::vector<platform::WindowEvent> curWin;
                {
                    std::lock_guard<std::mutex> lock(inbox.mutex);
                    curInput.swap(inbox.inputEvents);
                    curWin.swap(inbox.windowEvents);
                }
                for (const auto& in : curInput) {
                    ui->Dispatch(in);
                }
                for (const auto& w : curWin) {
                    if (w.type == platform::WindowEventType::DpiChanged) {
                        ui->SetContentScale(w.dpiScale);
                    }
                }

                const auto now = Clock::now();
                const float dt = std::chrono::duration<float>(now - last).count();
                last = now;

                if (desc.onFrame && mounted) {
                    desc.onFrame(*mounted, dt);
                }
                ui->Update(dt);
                if (mounted) {
                    window->SetTextInputActive(mounted->WantsTextInput());
                }
                ui->Layout(static_cast<float>(extent.width), static_cast<float>(extent.height));

                FramePacket& packet = renderThread.Packet();
                ui->RecordFrame(packet, extent);
                renderThread.Submit();

                const auto tWorkEnd = Clock::now();

                if (perf) {
                    tUiWait += ms(tWaitStart, tWorkStart);
                    tUiWork += ms(tWorkStart, tWorkEnd);
                    if (mounted) {
                        const FrameCounters& c = mounted->Counters();
                        cEmitVisited += c.emitVisited;
                        cEmitRecorded += c.emitRecorded;
                        cReplayed += c.paintReplayed;
                        cEffects += c.effectRuns;
                        cFrameCb += c.frameCallbacksInvoked;
                        cCascade += c.cascadeCalls;
                        cTextMeasure += c.textMeasureCalls;
                        cGlyph += c.glyphCacheCalls;
                        cSolve += c.solveCalls;
                        cRefreshDirty += c.refreshDirty;
                        cLayoutRuns += c.layoutRuns;
                        cIdleBails += c.layoutIdleBails;
                        cReused += c.emitReused;
                        cLayers += c.emitLayers;
                        cBytes += c.paintRecordedBytes;
                    }
                    if (++perfFrameCount == kPerfWindow) {
                        const RenderStats rStats = renderThread.TakeRenderStats();
                        const double n = kPerfWindow;
                        const double rendN = rStats.frameCount > 0 ? static_cast<double>(rStats.frameCount) : n;
                        PERF_PRINT("PERF ms/frame: ui-wait %.3f ui-work %.3f render-acquire %.3f render-wait %.3f render-frame %.3f present %.3f",
                                   tUiWait / n, tUiWork / n,
                                   rStats.acquireMs / rendN, rStats.waitPacketMs / rendN,
                                   rStats.renderFrameMs / rendN, rStats.presentMs / rendN);
                        PERF_PRINT("PERF per-frame: emitVisited %.0f emitRecorded %.0f emitReused %.0f layers %.0f replayed %.0f bytes %.0f | effects %.0f onFrame %.0f cascades %.0f | textMeasure %.0f glyphCache %.0f | layoutRuns %.1f idleBails %.1f refreshDirty %.0f solves %.1f",
                                   static_cast<double>(cEmitVisited) / n, static_cast<double>(cEmitRecorded) / n,
                                   static_cast<double>(cReused) / n, static_cast<double>(cLayers) / n,
                                   static_cast<double>(cReplayed) / n, static_cast<double>(cBytes) / n,
                                   static_cast<double>(cEffects) / n, static_cast<double>(cFrameCb) / n,
                                   static_cast<double>(cCascade) / n, static_cast<double>(cTextMeasure) / n,
                                   static_cast<double>(cGlyph) / n, static_cast<double>(cLayoutRuns) / n,
                                   static_cast<double>(cIdleBails) / n, static_cast<double>(cRefreshDirty) / n,
                                   static_cast<double>(cSolve) / n);
                        tUiWait = tUiWork = 0.0;
                        cEmitVisited = cEmitRecorded = cReplayed = cEffects = cFrameCb = cCascade = cTextMeasure = cGlyph = cSolve = cRefreshDirty = cLayoutRuns =
                            cIdleBails = cReused = cLayers = cBytes = 0;
                        perfFrameCount = 0;
                    }
                }

                ++frameCount;
                if (exitAfter > 0 && frameCount >= exitAfter) {
                    quit = true;
                    window->Wake();
                    break;
                }
            }

            renderThread.RequestStop();
            {
                std::lock_guard<std::mutex> lk(rtMutex);
                activeRenderThread = nullptr;
            }
            renderThread.Stop();
            ui = nullptr;
            quit = true;
            window->Wake();
        },
        platform::ThreadPriority::High);

    while (!quit.load() && !window->ShouldClose()) {
        window->WaitEvents();
        window->PumpEvents();
    }

    quit = true;
    {
        std::lock_guard<std::mutex> lk(rtMutex);
        if (activeRenderThread) {
            activeRenderThread->RequestStop();
        }
    }
    uiThread.Join();
    device.WaitIdle();
    device.Shutdown();
    return 0;
}
#endif // __EMSCRIPTEN__
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
