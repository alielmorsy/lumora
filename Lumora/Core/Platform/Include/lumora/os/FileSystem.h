#pragma once


#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace lumora::os
{
    [[nodiscard]] std::optional<std::string> ReadFileText(const std::filesystem::path& path);

    bool WriteFileText(const std::filesystem::path& path, std::string_view content);

    // Replaces `dst` with `tmp` only when their contents differ (so an unchanged output produces no
    // downstream rebuild); otherwise removes `tmp`. Returns false on failure.
    bool WriteFileIfDifferent(const std::filesystem::path& tmp, const std::filesystem::path& dst);
}
