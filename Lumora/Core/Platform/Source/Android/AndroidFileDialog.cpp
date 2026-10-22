#include <lumora/platform/FileDialog.h>

namespace lumora::platform
{
    std::optional<std::filesystem::path>
    OpenFileDialog(std::string_view /*title*/,
                   const std::vector<FileDialogFilter>& /*filters*/)
    {
        return std::nullopt;
    }

    std::optional<std::filesystem::path>
    SaveFileDialog(std::string_view /*title*/,
                   const std::vector<FileDialogFilter>& /*filters*/,
                   std::string_view /*suggestedName*/,
                   std::string_view /*defaultExtension*/,
                   const std::filesystem::path& /*startDir*/)
    {
        return std::nullopt;
    }

    std::optional<std::filesystem::path>
    PickFolderDialog(std::string_view /*title*/)
    {
        return std::nullopt;
    }
}
