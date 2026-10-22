#pragma once

#include <lumora/Graph/Graph.h>

#include <span>
#include <string>
#include <string_view>

namespace lumora::nodegraph
{
    /// ScoreType over loose fields, for popup rows that are not node types.
    [[nodiscard]] int ScoreFields(std::string_view query, std::string_view title,
                                  std::string_view category, std::span<const std::string> keywords);
} // namespace lumora::nodegraph
