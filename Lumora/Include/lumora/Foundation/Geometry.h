#pragma once

#include <lumora/Foundation/Rect.h>

#include <cstdint>

namespace lumora
{
    /// Alignment for both axes: Stretch on cross; Between/Around/Evenly on main; invalid combos are ignored.
    /// Auto defers to the container kind (resolved at instantiate, never seen by the solver).
    enum class Align : uint8_t { Start, Center, End, Stretch, Between, Around, Evenly, Auto };

    /// Cardinal direction for 2D spatial focus navigation and layout.
    enum class Direction : uint8_t { Up, Down, Left, Right };

    inline constexpr Align Start = Align::Start;
    inline constexpr Align Center = Align::Center;
    inline constexpr Align End = Align::End;
    inline constexpr Align Stretch = Align::Stretch;
    inline constexpr Align Between = Align::Between;
    inline constexpr Align Around = Align::Around;
    inline constexpr Align Evenly = Align::Evenly;

    struct SizeHint
    {
        enum class Kind : uint8_t { Fixed, Percent, Expand, Wrap } kind = Kind::Wrap;

        float value = 0.f; ///< px for Fixed, percent for Percent, flex weight for Expand

        [[nodiscard]] static constexpr SizeHint MakeFixed(float px) noexcept { return {Kind::Fixed, px}; }
        [[nodiscard]] static constexpr SizeHint MakePercent(float p) noexcept { return {Kind::Percent, p}; }
        [[nodiscard]] static constexpr SizeHint MakeExpand(float w = 1.f) noexcept { return {Kind::Expand, w}; }
        [[nodiscard]] static constexpr SizeHint MakeWrap() noexcept { return {Kind::Wrap, 0.f}; }

        [[nodiscard]] constexpr bool IsSet() const noexcept { return kind != Kind::Wrap || value != 0.f; }
        [[nodiscard]] constexpr bool operator==(const SizeHint&) const noexcept = default;
    };

    inline constexpr SizeHint Expand = SizeHint::MakeExpand();
    inline constexpr SizeHint Wrap = SizeHint::MakeWrap();
    [[nodiscard]] constexpr SizeHint Fixed(float px) noexcept { return SizeHint::MakeFixed(px); }
    [[nodiscard]] constexpr SizeHint Percent(float p) noexcept { return SizeHint::MakePercent(p); }
    [[nodiscard]] constexpr SizeHint Grow(float weight) noexcept { return SizeHint::MakeExpand(weight); }

    /// Child-layout mode / main axis. Resolved from the kind at mount; overridable by LayoutProps::direction / CSS flex-direction.
    enum class FlexAxis : uint8_t { Row, Column, Stack, Grid };

    /// Inner layout mode mirroring CSS `display`: Block (Masharif normal flow) or Flex. Grid and None live
    /// elsewhere (FlexAxis::Grid, visibility).
    enum class DisplayMode : uint8_t { Flex, Block };

    /// Flex-wrap; only matters when a line overflows the main axis.
    enum class FlexWrap : uint8_t { NoWrap, Wrap, WrapReverse };

    /// Implicitly constructible from shorthand:
    ///   .padding = 20            → all sides
    ///   .padding = {8, 12}       → {vertical, horizontal}
    ///   .padding = {1, 2, 3, 4}  → {top, right, bottom, left}
    struct SpacingOnly
    {
        float top = 0, right = 0, bottom = 0, left = 0;
    };

    struct Spacing
    {
        float top = 0, right = 0, bottom = 0, left = 0;

        constexpr Spacing() = default;

        constexpr Spacing(float all) noexcept // NOLINT(*-explicit-constructor)
            : top(all), right(all), bottom(all), left(all)
        {
        }

        constexpr Spacing(float vertical, float horizontal) noexcept
            : top(vertical), right(horizontal), bottom(vertical), left(horizontal)
        {
        }

        constexpr Spacing(float t, float r, float b, float l) noexcept
            : top(t), right(r), bottom(b), left(l)
        {
        }

        [[nodiscard]] static constexpr Spacing All(float v) noexcept { return Spacing(v); }
        [[nodiscard]] static constexpr Spacing Symmetric(float v, float h) noexcept { return {v, h}; }

        [[nodiscard]] static constexpr Spacing Only(SpacingOnly only) noexcept
        {
            return {only.top, only.right, only.bottom, only.left};
        }

        [[nodiscard]] static constexpr Spacing Only(float t = 0, float r = 0, float b = 0, float l = 0) noexcept
        {
            return {t, r, b, l};
        }

        [[nodiscard]] constexpr float Horizontal() const noexcept { return left + right; }
        [[nodiscard]] constexpr float Vertical() const noexcept { return top + bottom; }

        [[nodiscard]] constexpr Rect Inset(const Rect& rect) const noexcept
        {
            return {rect.x + left, rect.y + top, rect.width - Horizontal(), rect.height - Vertical()};
        }

        [[nodiscard]] constexpr Rect Outset(const Rect& rect) const noexcept
        {
            return {rect.x - left, rect.y - top, rect.width + Horizontal(), rect.height + Vertical()};
        }

        [[nodiscard]] constexpr bool operator==(const Spacing&) const noexcept = default;
    };

    /// The measure-constraint mode handed to leaf measure callbacks (Yoga/Taffy model):
    ///   Definite(px) - measure into exactly this much space (text wraps at it);
    ///   MinContent   - the smallest the content can be (longest unbreakable run);
    ///   MaxContent   - the content's natural size, unconstrained (no wrapping).
    struct AvailableSpace
    {
        enum class Kind : uint8_t { Definite, MinContent, MaxContent } kind = Kind::MaxContent;

        float value = 0.f; ///< px; meaningful only when kind == Definite

        [[nodiscard]] static constexpr AvailableSpace Definite(float px) noexcept { return {Kind::Definite, px}; }
        [[nodiscard]] static constexpr AvailableSpace MinContent() noexcept { return {Kind::MinContent, 0.f}; }
        [[nodiscard]] static constexpr AvailableSpace MaxContent() noexcept { return {Kind::MaxContent, 0.f}; }

        [[nodiscard]] constexpr bool IsDefinite() const noexcept { return kind == Kind::Definite; }
        [[nodiscard]] constexpr bool operator==(const AvailableSpace&) const noexcept = default;
    };
}
