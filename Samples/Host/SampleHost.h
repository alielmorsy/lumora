#pragma once

#include <lumora/Elem.h>
#include <lumora/platform/Launch.h>

#include <functional>
#include <string>

namespace lumora {
class MountedApp;
}

namespace lumora::samples {

struct SampleDesc {
    std::string title;
    int width  = 1280;
    int height = 800;
    std::function<Elem()> root;
    std::string css;
    /// Once per frame on the thread that ticks the app, after input dispatch and before Update/Tick. Null = no-op.
    std::function<void(MountedApp&, float dt)> onFrame;
};

// Runs the sample. Today: headless (mount, tick, layout). Later: window + RHI, same signature.
// `--headless-frames N` on the command line forces headless mode for N frames.
int RunSample(const SampleDesc& desc, int argc, char** argv);

} // namespace lumora::samples
