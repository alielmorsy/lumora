#pragma once

#include <lumora/StringId.h>
#include <lumora/Foundation/Color.h>

#include <cstdint>
#include <string>
#include <vector>

namespace lumora::style
{
    /// Unit tag of a numeric CssValue. Units stay symbolic through parse and cascade; they collapse to pixels only in the
    /// computed pass, where the resolution context is known.
    enum class Unit : uint8_t
    {
        None, ///< no unit (colours, keywords, plain numbers)
        Px,
        Percent,
        Em,
        Rem,
        Vw,
        Vh,
        Fr, ///< grid flexible track fraction (grid-template-columns/rows); stays symbolic, consumed by ToTrackList
        Deg,
        Rad,
        Turn,
        S,
        Ms,
    };

    enum class ValueKind : uint8_t
    {
        Invalid,
        Length,   ///< num + a length Unit (Px/Percent/Em/Rem/Vw/Vh)
        Number,   ///< unitless number (flex-grow, opacity, line-height multiplier, z-index)
        Time,     ///< num + S/Ms
        Angle,    ///< num + Deg/Rad/Turn
        Color,    ///< resolved colour
        Keyword,  ///< interned identifier - colour names and enum keywords resolve at apply time
        VarRef,   ///< var(--name [, fallback])
        Function, ///< name( args… ) - cubic-bezier(), steps(), translateX(), rgb() kept raw, …
        List,     ///< several values; `comma` distinguishes `a, b` from `a b`
    };

    /// A single parsed CSS value (a `List` when the source had several parts).
    struct CssValue
    {
        ValueKind kind = ValueKind::Invalid;
        Unit unit = Unit::None;
        float num = 0.f;
        Color color{};
        StringId keyword{}; ///< Keyword: the ident; VarRef: the `--name`; Function: the fn name
        std::string text;   ///< the raw string for Keyword/String/Function/VarRef (StringId is lossy in
                            ///< release, so string-valued props like font-family read this)
        bool comma = false; ///< List only: comma-separated (true) vs space-separated (false)

        /// List: the child values. Function: the arguments. VarRef: the optional fallback (0 or 1).
        std::vector<CssValue> list;

        [[nodiscard]] constexpr bool Valid() const noexcept { return kind != ValueKind::Invalid; }
        [[nodiscard]] bool operator==(const CssValue&) const = default;

        [[nodiscard]] static CssValue Length(float v, Unit u) { return {ValueKind::Length, u, v}; }
        [[nodiscard]] static CssValue Px(float v) { return {ValueKind::Length, Unit::Px, v}; }
        [[nodiscard]] static CssValue Percent(float v) { return {ValueKind::Length, Unit::Percent, v}; }
        [[nodiscard]] static CssValue Number(float v) { return {ValueKind::Number, Unit::None, v}; }
        [[nodiscard]] static CssValue Time(float v, Unit u) { return {ValueKind::Time, u, v}; }
        [[nodiscard]] static CssValue Angle(float v, Unit u) { return {ValueKind::Angle, u, v}; }

        [[nodiscard]] static CssValue Rgba(Color c)
        {
            CssValue v;
            v.kind = ValueKind::Color;
            v.color = c;
            return v;
        }

        [[nodiscard]] static CssValue Word(StringId id, std::string text = {})
        {
            CssValue v;
            v.kind = ValueKind::Keyword;
            v.keyword = id;
            v.text = std::move(text);
            return v;
        }

        /// Seconds regardless of the source unit (S or Ms). 0 for non-Time values.
        [[nodiscard]] float AsSeconds() const noexcept
        {
            if (kind != ValueKind::Time) return 0.f;
            return unit == Unit::Ms ? num / 1000.f : num;
        }
    };
}
