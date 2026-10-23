#pragma once

#include <cstdint>

namespace lumora::platform
{
    enum class WindowBackend : uint8_t
    {
        Win32,
        Xlib,
        Wayland,
        Cocoa,
        Android,
        Web,
    };

    // Opaque native handles filled by the platform window and passed to the renderer; type-erased so no OS headers leak.
    struct NativeWindowHandle
    {
        void*         window   = nullptr;  // HWND on Win32, canvas CSS selector (const char*) on Web
        void*         instance = nullptr;  // HINSTANCE on Win32
        WindowBackend backend  = WindowBackend::Win32;
    };
}
