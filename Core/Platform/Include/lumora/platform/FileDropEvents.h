#pragma once

#include "InputEvents.h"

#include <string>
#include <vector>

namespace lumora::platform
{
    /// Files dropped from the OS shell, published on Window::FileDrops(). `position` is in client-area pixels (top-left
    /// origin), like InputEvent::position.
    struct FileDropEvent
    {
        std::vector<std::string> paths;       // absolute filesystem paths, UTF-8
        InputVec2                position{};
    };
}
