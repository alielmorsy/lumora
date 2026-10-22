#pragma once

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>

#include <cstdint>

namespace lumora {

/// Opaque handle to a backend-owned texture, minted by the painter. Lumora never dereferences it.
struct ImageHandle {
    std::uint64_t id     = 0;
    float         width  = 0.f;
    float         height = 0.f;

    [[nodiscard]] constexpr bool Valid() const noexcept { return id != 0; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return Valid(); }
    [[nodiscard]] constexpr bool operator==(const ImageHandle&) const noexcept = default;
};

/// Custom-paint draw callback, declared opaquely to avoid a dependency on the custom-paint module.
class PaintDelegate;

/// A rounded rect with optional fill and stroke, passed as ONE primitive to Canvas::DrawRect: fills when `fill.a > 0`,
/// then strokes the inner ring when `borderWidth > 0`.
struct RectCmd {
    Rect  bounds{};
    Color fill         = Color::Transparent; ///< a == 0 → no fill
    float radius       = 0.f;                ///< fill corner radius
    Color borderColor  = Color::Transparent;
    float borderWidth  = 0.f;                ///< 0 → no border
    float borderRadius = 0.f;                ///< border corner radius
    /// Dash length in px for a dashed stroke (0 = solid); the gap equals the dash. The pattern runs per side, so corners show a phase break.
    float borderDash   = 0.f;
};

/// One triangle-run vertex: a surface-space position and a straight colour interpolated across the triangle. Plain data.
struct PaintVertex {
    Vec2  pos{};
    Color col = Color::White;
};

/// Shape rendered by Canvas::DrawShape.
enum class ShapeKind : std::uint8_t {
    Circle, ///< filled disc (radius); start/sweep ignored
    Ring,   ///< stroked circle / donut (radius = centerline, strokeWidth); start/sweep ignored
    Arc,    ///< round-capped ring segment (radius = centerline, strokeWidth, start + sweep)
    Pie,    ///< filled wedge (radius, start + sweep)
};

/// An analytic 2D shape, one primitive per node. `center`/`radius` are surface pixels; `strokeWidth` is the FULL thickness
/// (Ring/Arc). Angles are radians (0 = +X, clockwise on screen, y-down) for Arc/Pie; |sweep| >= 2pi degrades Arc to Ring and Pie to Circle.
struct ShapeCmd {
    Vec2      center{};
    float     radius      = 0.f;
    ShapeKind kind        = ShapeKind::Circle;
    Color     color       = Color::Transparent; ///< fill (Circle/Pie) or stroke (Ring/Arc)
    float     strokeWidth = 0.f;                 ///< full thickness for Ring/Arc
    float     startAngle  = 0.f;                 ///< radians, 0 = +X, clockwise (y-down) (Arc/Pie)
    float     sweepAngle  = 0.f;                 ///< radians, total swept angle (Arc/Pie)
};

}
