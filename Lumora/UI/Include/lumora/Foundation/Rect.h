#pragma once

#include <lumora/Foundation/Vec2.h>

#include <algorithm>

namespace lumora {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float x, float y, float width, float height)
        : x(x), y(y), width(width), height(height) {}
    constexpr Rect(Vec2 position, Vec2 size)
        : x(position.x), y(position.y), width(size.x), height(size.y) {}

    [[nodiscard]] constexpr Vec2 Position() const { return {x, y}; }
    [[nodiscard]] constexpr Vec2 Size() const { return {width, height}; }
    [[nodiscard]] constexpr Vec2 Center() const { return {x + width / 2.0f, y + height / 2.0f}; }

    [[nodiscard]] constexpr float Left() const { return x; }
    [[nodiscard]] constexpr float Right() const { return x + width; }
    [[nodiscard]] constexpr float Top() const { return y; }
    [[nodiscard]] constexpr float Bottom() const { return y + height; }

    [[nodiscard]] constexpr bool Contains(const Vec2& point) const {
        return point.x >= Left() && point.x <= Right() &&
               point.y >= Top() && point.y <= Bottom();
    }

    [[nodiscard]] constexpr bool Intersects(const Rect& other) const {
        return Left() <= other.Right() && Right() >= other.Left() &&
               Top() <= other.Bottom() && Bottom() >= other.Top();
    }

    [[nodiscard]] constexpr Rect Intersection(const Rect& other) const {
        if (!Intersects(other)) {
            return {};
        }
        const float ix = std::max(Left(), other.Left());
        const float iy = std::max(Top(), other.Top());
        const float irx = std::min(Right(), other.Right());
        const float iry = std::min(Bottom(), other.Bottom());
        return {ix, iy, irx - ix, iry - iy};
    }

    constexpr Rect operator+(const Vec2& offset) const { return {x + offset.x, y + offset.y, width, height}; }
    constexpr Rect operator-(const Vec2& offset) const { return {x - offset.x, y - offset.y, width, height}; }

    [[nodiscard]] constexpr bool operator==(const Rect&) const = default;
};

}
