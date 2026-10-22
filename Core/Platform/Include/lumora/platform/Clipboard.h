#pragma once

#include <lumora/Api.h>

#include <string>
#include <string_view>

namespace lumora::platform
{
    // System clipboard, text only, UTF-8 in and out. Failure (clipboard busy or holding no text) is reported as false/empty
    // rather than an exception.

    LUMORA_API bool SetClipboardText(std::string_view text);

    [[nodiscard]] LUMORA_API std::string GetClipboardText();
}
