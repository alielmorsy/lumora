#pragma once

#include <cstdint>
#include <limits>

namespace lumora
{
    /// Out-of-flow placement. Absolute pins to the nearest anchor ancestor's content box (else root/viewport); Fixed pins
    /// to the surface. Orthogonal to transform/Portal compositing.
    enum class Position : std::uint8_t { Flow, Absolute, Fixed };

    /// Per-edge inset against the containing block's content box. A NaN edge means "auto" (falls back to the flex
    /// static position on that axis). The .offset shorthand collapses into {left, top}.
    struct Inset
    {
        float left = std::numeric_limits<float>::quiet_NaN();
        float top = std::numeric_limits<float>::quiet_NaN();
        float right = std::numeric_limits<float>::quiet_NaN();
        float bottom = std::numeric_limits<float>::quiet_NaN();

        /// constexpr NaN test (std::isnan is not): a value is auto iff it != itself.
        [[nodiscard]] static constexpr bool IsAuto(float v) noexcept { return v != v; }
    };
}
