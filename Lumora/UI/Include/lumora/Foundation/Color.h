#pragma once

#include <cstdint>

namespace lumora
{
    struct Color
    {
        uint8_t r = 0, g = 0, b = 0, a = 255;

        constexpr Color() = default;

        constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
            : r(r), g(g), b(b), a(a)
        {
        }

        [[nodiscard]] static constexpr Color Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) noexcept
        {
            return {r, g, b, a};
        }

        /// Packed 32-bit ARGB hex value (e.g. `0xFF14151A`).
        [[nodiscard]] static constexpr Color Hex(uint32_t argb) noexcept
        {
            return {
                static_cast<uint8_t>(argb >> 16),
                static_cast<uint8_t>(argb >> 8),
                static_cast<uint8_t>(argb >> 0),
                static_cast<uint8_t>(argb >> 24)
            };
        }

        [[nodiscard]] constexpr Color WithAlpha(uint8_t alpha) const noexcept { return {r, g, b, alpha}; }

        /// Blend toward white. `t=0` unchanged, `t=1` white.
        [[nodiscard]] constexpr Color Lighten(float t) const noexcept
        {
            return {Lerp(r, 255, t), Lerp(g, 255, t), Lerp(b, 255, t), a};
        }

        /// Blend toward black. `t=0` unchanged, `t=1` black.
        [[nodiscard]] constexpr Color Darken(float t) const noexcept
        {
            return {Lerp(r, 0, t), Lerp(g, 0, t), Lerp(b, 0, t), a};
        }

        [[nodiscard]] constexpr bool operator==(const Color&) const = default;

        static const Color Transparent;
        static const Color Black;
        static const Color White;
        static const Color Gray;
        static const Color DarkGray;
        static const Color Red;
        static const Color Green;
        static const Color Blue;

    private:
        [[nodiscard]] static constexpr uint8_t Lerp(uint8_t from, int to, float t) noexcept
        {
            float v = static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) * t;
            if (v < 0.f) v = 0.f;
            if (v > 255.f) v = 255.f;
            return static_cast<uint8_t>(v + 0.5f);
        }
    };

    /// `0x14151A_rgb` → opaque RGB (alpha = 255)
    [[nodiscard]] constexpr Color operator""_rgb(unsigned long long v) noexcept
    {
        return {static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v), 255};
    }

    /// `0x14151A80_rgba` → RGBA (low byte = alpha)
    [[nodiscard]] constexpr Color operator""_rgba(unsigned long long v) noexcept
    {
        return {
            static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8),
            static_cast<uint8_t>(v)
        };
    }

    inline const Color Color::Transparent = {0, 0, 0, 0};
    inline const Color Color::Black = {0, 0, 0, 255};
    inline const Color Color::White = {255, 255, 255, 255};
    inline const Color Color::Gray = {128, 128, 128, 255};
    inline const Color Color::DarkGray = {64, 64, 64, 255};
    inline const Color Color::Red = {220, 53, 69, 255};
    inline const Color Color::Green = {40, 167, 69, 255};
    inline const Color Color::Blue = {0, 123, 255, 255};
}
