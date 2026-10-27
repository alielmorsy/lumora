#include <lumora/platform/Clipboard.h>

#include "LinuxWindow.h"

namespace lumora::platform
{
    bool SetClipboardText(std::string_view text)
    {
        DisplayBackend* backend = GetBackend();
        return backend && backend->SetClipboard(text);
    }

    std::string GetClipboardText()
    {
        DisplayBackend* backend = GetBackend();
        return backend ? backend->GetClipboard() : std::string{};
    }
}
