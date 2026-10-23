#include <lumora/os/CrashHandler.h>
#include <lumora/os/DirectoryWatcher.h>
#include <lumora/os/Environment.h>
#include <lumora/os/Shell.h>
#include <lumora/log/Log.h>

#include <emscripten.h>

#include <cstdlib>
#include <sstream>

LUMORA_LOG_CATEGORY("OSCore")

namespace lumora::os
{
    namespace
    {
        EM_JS(void, web_open_url, (const char* ptr, int len), {
            window.open(new TextDecoder().decode(HEAPU8.slice(ptr, ptr + len)), "_blank", "noopener");
        });
    }

    std::optional<std::string> GetEnv(std::string_view name)
    {
        const char* val = ::getenv(std::string(name).c_str());
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
        return {};
    }

    std::filesystem::path ExecutableDir()
    {
        return "/";
    }

    void InstallCrashHandler(CrashHandlerOptions /*options*/)
    {
    }

    std::filesystem::path WriteCrashReport(std::string_view reason)
    {
        LOG_ERROR("CrashReport: {}", reason);
        return {};
    }

    // There are no processes to launch in a browser; a URL opens in a new tab instead.
    bool LaunchDetached(const std::filesystem::path& exe, std::span<const std::filesystem::path> /*args*/)
    {
        const std::string url = exe.string();
        if (url.find("://") == std::string::npos) return false;
        web_open_url(url.data(), static_cast<int>(url.size()));
        return true;
    }

    bool RevealInFileManager(const std::filesystem::path& /*path*/)
    {
        return false;
    }

    struct DirectoryWatcher::Impl
    {
    };

    DirectoryWatcher::DirectoryWatcher() = default;
    DirectoryWatcher::~DirectoryWatcher() = default;

    std::uint32_t DirectoryWatcher::AddRoot(const std::filesystem::path& /*root*/) { return kInvalidRoot; }
    void DirectoryWatcher::RemoveRoot(std::uint32_t /*root*/) {}
    std::vector<FileEvent> DirectoryWatcher::Drain() { return {}; }
    bool DirectoryWatcher::ConsumeOverflow(std::uint32_t /*root*/) { return false; }
    void DirectoryWatcher::SetQuietWindow(std::chrono::milliseconds /*window*/) {}
    void DirectoryWatcher::SetIgnoredComponents(std::vector<std::string> /*components*/) {}
}
