#pragma once

#include <cstdint>

namespace lumora::platform
{
    enum class WindowEventType : uint8_t
    {
        Resize,
        Close,
        FocusGained,
        FocusLost,
        DpiChanged,
    };

    struct WindowEvent
    {
        WindowEventType type{};
        uint32_t        width    = 0;    // valid for Resize
        uint32_t        height   = 0;    // valid for Resize
        float           dpiScale = 1.0f; // valid for DpiChanged
    };
}
