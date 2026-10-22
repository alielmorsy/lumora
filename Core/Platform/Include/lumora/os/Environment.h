#pragma once


#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace lumora::os
{
    [[nodiscard]] std::optional<std::string> GetEnv(std::string_view name);

    // Searches a PATH-style value (entries separated by the platform separator) for `exe`, returning
    // the first entry under which it exists.
    [[nodiscard]] std::optional<std::filesystem::path> WhichInPath(std::string_view exe, std::string_view pathValue);

    [[nodiscard]] std::filesystem::path ExecutablePath();
    [[nodiscard]] std::filesystem::path ExecutableDir();
}
