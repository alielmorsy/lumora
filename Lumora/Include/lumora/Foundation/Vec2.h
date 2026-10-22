#pragma once

#include <cmath>

namespace lumora {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x, float y) : x(x), y(y) {}

    constexpr Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    constexpr Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    constexpr Vec2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
    constexpr Vec2 operator/(float scalar) const { return {x / scalar, y / scalar}; }

    constexpr Vec2& operator+=(const Vec2& other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    constexpr Vec2& operator-=(const Vec2& other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    [[nodiscard]] constexpr bool operator==(const Vec2&) const = default;

    [[nodiscard]] float Length() const { return std::sqrt(x * x + y * y); }
    [[nodiscard]] constexpr float LengthSquared() const { return x * x + y * y; }

    [[nodiscard]] Vec2 Normalized() const {
        const float len = Length();
        return len > 0.0f ? *this / len : Vec2{};
    }
};

}
