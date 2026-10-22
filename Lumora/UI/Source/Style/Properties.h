#pragma once

#include <string_view>

namespace lumora::style
{
    /// Every property name the resolver consults; the loader warns about declared properties not listed here.
    /// Hand-maintained: adding a property to the resolver means adding it here.
    [[nodiscard]] constexpr bool IsConsultedProperty(std::string_view name) noexcept
    {
        constexpr std::string_view kConsulted[] = {
            // sizing
            "width", "height", "min-width", "min-height", "max-width", "max-height", "aspect-ratio",
            // spacing
            "padding", "padding-top", "padding-right", "padding-bottom", "padding-left",
            "margin", "margin-top", "margin-right", "margin-bottom", "margin-left",
            "gap", "row-gap", "column-gap",
            // flex + display
            "display", "flex", "flex-direction", "flex-wrap", "flex-grow", "flex-shrink", "flex-basis",
            "align-items", "align-self", "align-content", "justify-content", "order",
            // grid
            "grid-template-columns", "grid-template-rows", "grid-auto-rows", "grid-column", "grid-row",
            // positioning
            "position", "inset", "top", "right", "bottom", "left", "z-index",
            // paint
            "background", "background-color", "color", "opacity", "visibility", "overflow",
            "border", "border-width", "border-color", "border-radius", "border-style",
            "border-top", "border-right", "border-bottom", "border-left",
            "border-top-width", "border-right-width", "border-bottom-width", "border-left-width",
            "border-top-color", "border-right-color", "border-bottom-color", "border-left-color",
            "border-top-style", "border-right-style", "border-bottom-style", "border-left-style",
            // text
            "font-family", "font-size", "font-style", "font-weight", "line-height", "text-align",
            "text-decoration", "text-overflow", "white-space", "max-lines", "-webkit-line-clamp",
            // motion
            "transform", "transform-origin", "transition", "animation",
        };
        for (const std::string_view consulted : kConsulted)
        {
            if (consulted == name)
            {
                return true;
            }
        }
        return false;
    }
} // namespace lumora::style
