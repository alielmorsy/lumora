#pragma once

namespace lumora::platform
{
    // Plain launch payload for the application entry. No OS headers, safe to include anywhere.
    struct LaunchArgs
    {
        int    argc = 0;
        char** argv = nullptr;
    };

    /// Application entry point, implemented once by the application; the OS entry point (EntryPoint.h) forwards here.
    int Main(const LaunchArgs& args);
}
