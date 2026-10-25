#include "JsHost.h"

#include <SampleHost.h>

#include <lumora/Host/HostTree.h>
#include <lumora/MountedApp.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/Text.h>
#include <lumora/script/JsApp.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace lumora::jshost {

namespace {

namespace fs = std::filesystem;

constexpr double kPollMs = 250.0;

struct HostState {
    std::string bundle;
    std::string dumpPath;
    int lastFrame = -1; ///< from --headless-frames / --exit-after; -1 = until the window closes
    int frame     = 0;
    double nowMs  = 0.0;
    double nextPollMs = 0.0;
    std::optional<fs::file_time_type> stamp;
    std::unique_ptr<script::JsApp> js;

    void ReportError() const { std::fprintf(stderr, "%s\n", js->LastError().c_str()); }

    /// Loads the bundle if its timestamp moved. A missing file keeps the old app; a failed read retries next poll.
    void Poll()
    {
        std::error_code ec;
        const fs::file_time_type t = fs::last_write_time(bundle, ec);
        if (ec || (stamp && *stamp == t))
            return;
        std::ifstream file(bundle, std::ios::binary);
        if (!file)
            return;
        const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (file.bad())
            return;
        stamp = t;
        if (!js->Load(source, bundle))
            ReportError();
    }

    void Frame(MountedApp& app, float dt)
    {
        ++frame;
        if (!bundle.empty()) {
            if (!js)
                js = std::make_unique<script::JsApp>(app);
            if (nowMs >= nextPollMs) {
                nextPollMs = nowMs + kPollMs;
                Poll();
            }
            const bool wasLoaded = js->Loaded();
            js->Tick(nowMs);
            if (wasLoaded && !js->Loaded())
                ReportError();
        }
        nowMs += dt * 1000.0;

        // The last frame we know of: the JsApp must go before the MountedApp, and the dump needs the app alive.
        if (frame == lastFrame) {
            if (!dumpPath.empty()) {
                std::ofstream out(dumpPath, std::ios::binary);
                for (const std::string& line : app.Host().TextContents())
                    out << line << '\n';
            }
            js.reset();
        }
    }
};

} // namespace

int RunJsHost(int argc, char** argv)
{
    auto state = std::make_shared<HostState>();
    std::string title = "Lumora";
    int headlessFrames = -1;
    int exitAfter      = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        const bool hasValue = i + 1 < argc;
        if (a == "--bundle" && hasValue)
            state->bundle = argv[++i];
        else if (a == "--title" && hasValue)
            title = argv[++i];
        else if (a == "--dump-text" && hasValue)
            state->dumpPath = argv[++i];
        else if (a == "--headless-frames" && hasValue)
            headlessFrames = std::atoi(argv[++i]);
        else if (a == "--exit-after" && hasValue)
            exitAfter = std::atoi(argv[++i]);
    }
    state->lastFrame = headlessFrames >= 0 ? headlessFrames : exitAfter;

    const bool hasBundle = !state->bundle.empty();
    const int rc = samples::RunSample(
        {.title   = title,
         .root    = [hasBundle] {
             const BoxCfg cfg{.className = "lumora-root", .inlineCss = "width: 100%; height: 100%; flex-direction: column"};
             return hasBundle ? Box(cfg) : Box(cfg, Text("LumoraJsHost: pass --bundle <file>"));
         },
         .onFrame = [state](MountedApp& app, float dt) { state->Frame(app, dt); }},
        argc, argv);

    // ponytail: the window closed without a known last frame, so the MountedApp is already gone and the JsApp
    // cannot be destroyed safely; the process exits next. A SampleDesc exit hook would let it unload properly.
    (void)state->js.release();
    return rc;
}

} // namespace lumora::jshost
