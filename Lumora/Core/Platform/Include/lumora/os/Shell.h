#pragma once


#include <filesystem>
#include <span>

namespace lumora::os
{
    /// Starts `exe` as an independent process without waiting. Each argument is passed as one quoted token. Returns false if
    /// `exe` is missing or the OS refused to start it.
    [[nodiscard]] bool LaunchDetached(const std::filesystem::path& exe, std::span<const std::filesystem::path> args);

    /// Opens the file manager with `path` revealed (a directory is opened; a file's parent opens with the file selected).
    /// Non-blocking; false if `path` is missing or launch failed.
    [[nodiscard]] bool RevealInFileManager(const std::filesystem::path& path);
}
