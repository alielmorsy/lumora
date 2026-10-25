#pragma once

#include <lumora/Foundation/Rect.h>
#include <lumora/Host/HostTree.h>

#include <cstdint>

namespace lumora {

/// Test-only reads of a HostTree's live state.
struct HostTreeTestAccess {
    [[nodiscard]] static std::uint32_t LiveNodes(const HostTree& tree);
    /// Solved rect of a built handle; empty when unbuilt or freed.
    [[nodiscard]] static Rect Solved(const HostTree& tree, HostHandle node);
};

}
