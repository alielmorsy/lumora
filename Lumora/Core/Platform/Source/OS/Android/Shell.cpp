#include <lumora/os/Shell.h>
#include <lumora/log/Log.h>

LUMORA_LOG_CATEGORY("OSCore")

namespace lumora::os
{
    bool LaunchDetached(const std::filesystem::path& /*exe*/,
                        std::span<const std::filesystem::path> /*args*/)
    {
        LOG_WARN("Shell::LaunchDetached is not supported on Android");
        return false;
    }

    bool RevealInFileManager(const std::filesystem::path& /*path*/)
    {
        LOG_WARN("Shell::RevealInFileManager is not supported on Android");
        return false;
    }
}
