#include <lumora/platform/UserPaths.h>

#include "lumora/os/Environment.h"

#include <system_error>

namespace lumora::platform
{
    std::filesystem::path GetUserConfigDir(std::string_view app)
    {
        std::filesystem::path base;
        if (const auto xdg = os::GetEnv("XDG_CONFIG_HOME"); xdg && !xdg->empty())
            base = *xdg;
        else if (const auto home = os::GetEnv("HOME"); home && !home->empty())
            base = std::filesystem::path(*home) / ".config";
        else
            base = std::filesystem::temp_directory_path();

        std::filesystem::path dir = base / std::filesystem::path(std::string(app));
        std::error_code       ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }
}
