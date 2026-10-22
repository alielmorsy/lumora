#pragma once

#include <cstdint>

namespace lumora {

/// Dense, stable per-node id assigned by the NodeArena; never reused within a mount.
struct NodeId {
    uint32_t value = 0; ///< 0 == the null id; the arena hands out ids from 1.

    [[nodiscard]] constexpr bool Valid() const noexcept { return value != 0; }
    [[nodiscard]] constexpr bool operator==(const NodeId&) const noexcept = default;
};

}
