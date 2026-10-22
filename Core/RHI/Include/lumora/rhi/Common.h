#pragma once

#include <cstdint>

namespace lumora::rhi {

struct Extent2D {
    uint32_t width  = 0;
    uint32_t height = 0;
    constexpr bool operator==(const Extent2D&) const = default;
};

struct Extent3D {
    uint32_t width  = 0;
    uint32_t height = 0;
    uint32_t depth  = 1;
    constexpr bool operator==(const Extent3D&) const = default;
};

struct Offset2D {
    int32_t x = 0;
    int32_t y = 0;
    constexpr bool operator==(const Offset2D&) const = default;
};

struct Offset3D {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
    constexpr bool operator==(const Offset3D&) const = default;
};

struct Rect2D {
    Offset2D offset{};
    Extent2D extent{};
    constexpr bool operator==(const Rect2D&) const = default;
};

struct Viewport {
    float x = 0.f, y = 0.f, width = 0.f, height = 0.f;
    float minDepth = 0.f, maxDepth = 1.f;
};

struct Color {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
};

} // namespace lumora::rhi
