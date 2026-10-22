#pragma once

#include <lumora/Api.h>

#include <filesystem>
#include <string_view>

namespace lumora::platform
{
    // OS per-user config directory for an application, created if missing (Windows: %APPDATA%/<app>). Lives outside any single project.
    [[nodiscard]] LUMORA_API std::filesystem::path GetUserConfigDir(std::string_view app = "Lumora");
}
