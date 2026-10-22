#pragma once

#include "Svg/SvgScene.h"

#include <lumora/Foundation/Color.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lumora::svg
{
    /// Parse a leading number (ignores any trailing unit). Returns false if no number is present.
    bool ParseNumber(std::string_view s, float& out);

    /// Parse an SVG length to user units (px). Units px/pt/pc/mm/cm/in are converted; `%` resolves
    /// against `reference`; em/ex approximate against a 16px font. Unitless values pass through.
    float ParseLength(std::string_view s, float reference = 0.f, float fallback = 0.f);

    /// Parse a whitespace/comma-separated number list (points, dash arrays).
    std::vector<float> ParseNumberList(std::string_view s);

    /// Parse a <color>: #rgb / #rgba / #rrggbb / #rrggbbaa, rgb()/rgba(), `currentColor`,
    /// `transparent`, or a named colour. nullopt for `none` / unparseable.
    std::optional<Color> ParseColor(std::string_view s, Color currentColor);

    struct PaintSpec
    {
        PaintKind kind = PaintKind::None;
        Color color{};      ///< when kind == Solid
        std::string ref;    ///< gradient/element id (no '#') when kind == Gradient
    };

    /// Parse a paint value (`none`, `url(#id)`, `currentColor`, or a colour).
    PaintSpec ParsePaint(std::string_view s, Color currentColor);

    /// Parse a `transform` attribute (matrix/translate/scale/rotate/skewX/skewY), composed left→right.
    Affine ParseTransform(std::string_view s);

    /// Parse a `viewBox` ("minX minY width height"). Returns false if fewer than 4 numbers.
    bool ParseViewBox(std::string_view s, Vec2& min, Vec2& size);
} // namespace lumora::svg
