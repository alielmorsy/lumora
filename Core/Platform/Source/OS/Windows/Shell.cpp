#include <lumora/os/Shell.h>

#include <lumora/log/Log.h>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

LUMORA_LOG_CATEGORY("OSCore")

namespace lumora::os
{
    bool LaunchDetached(const std::filesystem::path& exe, std::span<const std::filesystem::path> args)
    {
        std::error_code ec;
        if (exe.empty() || !std::filesystem::exists(exe, ec) || ec)
        {
            LOG_WARN("Cannot launch '{}': file does not exist", exe.string());
            return false;
        }

        std::wstring parameters;
        for (const std::filesystem::path& arg : args)
        {
            std::filesystem::path preferred = arg;
            std::wstring          text      = preferred.make_preferred().native();
            while (text.size() > 3 && text.back() == L'\\') text.pop_back();

            if (!parameters.empty()) parameters += L' ';
            parameters += L'"' + text + L'"';
        }

        const std::wstring directory = exe.parent_path().native();
        const HINSTANCE    result    = ::ShellExecuteW(nullptr, L"open", exe.c_str(),
                                                        parameters.empty() ? nullptr : parameters.c_str(),
                                                        directory.empty() ? nullptr : directory.c_str(), SW_SHOWNORMAL);

        const auto code = reinterpret_cast<INT_PTR>(result);
        if (code <= 32)
        {
            LOG_WARN("ShellExecuteW failed to launch '{}': return code {}", exe.string(), code);
            return false;
        }
        return true;
    }

    bool RevealInFileManager(const std::filesystem::path& path)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::exists(path, ec) || ec)
        {
            LOG_WARN("Cannot reveal '{}': path does not exist", path.string());
            return false;
        }

        // Explorer expects native backslashes.
        std::filesystem::path preferred = path;
        preferred.make_preferred();
        const std::wstring nativePath = preferred.native();

        HINSTANCE result = nullptr;
        if (std::filesystem::is_directory(preferred, ec) && !ec)
        {
            result = ::ShellExecuteW(nullptr, L"open", nativePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        else
        {
            const std::wstring args = L"/select,\"" + nativePath + L"\"";
            result = ::ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
        }

        const auto resInt = reinterpret_cast<INT_PTR>(result);
        if (resInt <= 32)
        {
            LOG_WARN("ShellExecuteW failed to reveal '{}': return code {}", path.string(), resInt);
            return false;
        }

        return true;
    }
}
