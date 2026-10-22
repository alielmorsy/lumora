#pragma once

#include <lumora/StringId.h>

#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lumora
{
    /// Payload kind for files dropped from the operating system file manager.
    inline constexpr StringId DragKindOsFiles{"os.files"};

    /// Splits newline-delimited paths carried in an "os.files" DragPayload, skipping empty entries.
    [[nodiscard]] inline std::vector<std::string_view> SplitOsFiles(std::string_view value)
    {
        std::vector<std::string_view> paths;
        for (const auto part : value | std::views::split('\n'))
        {
            if (!part.empty()) paths.emplace_back(part.begin(), part.end());
        }
        return paths;
    }

    /// The inverse of `SplitOsFiles`: joins paths into an "os.files" DragPayload value.
    [[nodiscard]] inline std::string JoinOsFiles(std::span<const std::string> paths)
    {
        std::string joined;
        for (const std::string& path : paths)
        {
            if (path.empty()) continue;
            if (!joined.empty()) joined += '\n';
            joined += path;
        }
        return joined;
    }

    /// What a drag carries. `kind` is what a drop target filters on ("asset", "entity"); `value` is the identity as
    /// text, not a pointer, because the payload is read on a later frame than the press.
    struct DragPayload
    {
        StringId    kind{};
        std::string value{};

        [[nodiscard]] bool Valid() const noexcept { return kind.Value() != 0; }
        [[nodiscard]] bool Is(StringId k) const noexcept { return kind == k; }
    };
}
