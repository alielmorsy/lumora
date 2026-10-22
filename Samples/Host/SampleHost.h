#pragma once

#include <lumora/Elem.h>
#include <lumora/platform/Launch.h>

#include <functional>
#include <string>

namespace lumora::samples {

struct SampleDesc {
    std::string title;
    int width  = 1280;
    int height = 800;
    std::function<Elem()> root;
    std::string css;
};

// Runs the sample. Today: headless (mount, tick, layout). Later: window + RHI, same signature.
// `--headless-frames N` on the command line forces headless mode for N frames.
int RunSample(const SampleDesc& desc, int argc, char** argv);

} // namespace lumora::samples
