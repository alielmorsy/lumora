#pragma once

#include <lumora/Api.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lumora::platform
{
    // One file-type filter row, e.g. {"Text", "*.txt"}.
    struct FileDialogFilter
    {
        std::string label;
        std::string pattern;
    };

    // Native open-file dialog. Returns the chosen path, or nullopt if the user cancelled.
    [[nodiscard]] LUMORA_API std::optional<std::filesystem::path>
    OpenFileDialog(std::string_view title, const std::vector<FileDialogFilter>& filters = {});

    // Native save dialog. `defaultExtension` (no dot) is appended to a bare name; `startDir` is used when it exists. nullopt if cancelled.
    [[nodiscard]] LUMORA_API std::optional<std::filesystem::path>
    SaveFileDialog(std::string_view title, const std::vector<FileDialogFilter>& filters = {},
                   std::string_view suggestedName = {}, std::string_view defaultExtension = {},
                   const std::filesystem::path& startDir = {});

    // Native pick-folder dialog. Returns the chosen folder, or nullopt if the user cancelled.
    [[nodiscard]] LUMORA_API std::optional<std::filesystem::path> PickFolderDialog(std::string_view title);
}
