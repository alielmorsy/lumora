#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lumora::os
{
    [[nodiscard]] std::optional<std::vector<std::byte>> ReadAsset(std::string_view path);

    [[nodiscard]] std::optional<std::string> ReadAssetText(std::string_view path);
}
