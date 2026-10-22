#include <lumora/os/Environment.h>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <sstream>

namespace lumora::os
{
    std::optional<std::string> GetEnv(std::string_view name)
    {
        char*       buffer = nullptr;
        std::size_t len    = 0;
        if (_dupenv_s(&buffer, &len, std::string(name).c_str()) != 0 || !buffer)
        {
            if (buffer) std::free(buffer);
            return std::nullopt;
        }
        std::string value(buffer);
        std::free(buffer);
        return value;
    }

    std::optional<std::filesystem::path> WhichInPath(std::string_view exe, std::string_view pathValue)
    {
        std::stringstream ss{std::string(pathValue)};
        std::string       dir;
        while (std::getline(ss, dir, ';'))
        {
            if (dir.empty()) continue;
            std::filesystem::path cand = std::filesystem::path(dir) / exe;
            std::error_code       ec;
            if (std::filesystem::exists(cand, ec)) return cand;
        }
        return std::nullopt;
    }

    std::filesystem::path ExecutablePath()
    {
        std::wstring buf(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
            if (n == 0) return {};
            if (n < buf.size())
            {
                buf.resize(n);
                break;
            }
            buf.resize(buf.size() * 2);
        }
        return std::filesystem::path(buf);
    }

    std::filesystem::path ExecutableDir()
    {
        return ExecutablePath().parent_path();
    }
}
