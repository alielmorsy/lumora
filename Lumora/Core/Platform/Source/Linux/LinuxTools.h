#pragma once

#include "lumora/os/Environment.h"

#include <filesystem>
#include <optional>
#include <string_view>

namespace lumora::platform
{
    /// First of `names` found on PATH, for the helper programs (zenity, kdialog, fc-match) the desktop shell pieces shell out to.
    [[nodiscard]] inline std::optional<std::filesystem::path> FindTool(std::initializer_list<std::string_view> names)
    {
        const std::string path = os::GetEnv("PATH").value_or("/usr/local/bin:/usr/bin:/bin");
        for (const std::string_view name : names)
        {
            if (auto found = os::WhichInPath(name, path)) return found;
        }
        return std::nullopt;
    }
}
