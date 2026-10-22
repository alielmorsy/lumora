#include <lumora/os/Environment.h>

#include <climits>
#include <cstdlib>
#include <sstream>
#include <unistd.h>

namespace lumora::os
{
    std::optional<std::string> GetEnv(std::string_view name)
    {
        const std::string nameStr(name);
        const char* val = ::getenv(nameStr.c_str());
        if (!val) return std::nullopt;
        return std::string(val);
    }

    std::optional<std::filesystem::path> WhichInPath(std::string_view exe, std::string_view pathValue)
    {
        std::stringstream ss{std::string(pathValue)};
        std::string dir;
        while (std::getline(ss, dir, ':'))
        {
            if (dir.empty()) continue;
            std::filesystem::path cand = std::filesystem::path(dir) / exe;
            std::error_code ec;
            if (std::filesystem::exists(cand, ec)) return cand;
        }
        return std::nullopt;
    }

    std::filesystem::path ExecutablePath()
    {
#if defined(__linux__) || defined(__ANDROID__)
        char buf[PATH_MAX];
        const ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0)
        {
            buf[len] = '\0';
            return std::filesystem::path(buf);
        }
#endif
        return {};
    }

    std::filesystem::path ExecutableDir()
    {
        return ExecutablePath().parent_path();
    }
}
