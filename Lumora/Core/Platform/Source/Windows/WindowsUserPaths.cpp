#include <lumora/platform/UserPaths.h>

#include <cstdlib>
#include <string>
#include <system_error>

namespace lumora::platform
{
    std::filesystem::path GetUserConfigDir(std::string_view app)
    {
        std::filesystem::path base;
        if (const wchar_t* appdata = _wgetenv(L"APPDATA"); appdata && *appdata)
            base = std::filesystem::path(appdata);
        else
            base = std::filesystem::temp_directory_path();

        std::filesystem::path dir = base / std::filesystem::path(std::string(app));
        std::error_code       ec;
        std::filesystem::create_directories(dir, ec);
        return dir;
    }
}
