#pragma once

#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>

namespace lumora
{
    /// Floating-UI/Popper-style anchored placement as pure math over solved rects + the viewport; runs after layout, before
    /// paint. No reactivity or tree mutation.

    enum class Side : std::uint8_t { Top, Bottom, Left, Right };
    enum class Align3 : std::uint8_t { Start, Center, End }; ///< named Align3 to avoid the Foundation Align clash

    struct Placement
    {
        Side   side  = Side::Bottom;
        Align3 align = Align3::Center;
    };

    struct PositionerInput
    {
        Rect      anchor{};       ///< trigger rect (zero-size at a point for pointer anchors), surface space
        Vec2      surface{};      ///< surface extent
        Spacing   viewportPad{};  ///< safe-area inset overlays keep clear of the window edge
        Vec2      content{};      ///< the surface's desired size (its solved size)
        Placement base{};         ///< requested side x align
        float     offset = 0.f;   ///< gap between anchor and surface along the side normal
    };

    struct ArrowResult
    {
        bool present = false;
        Vec2 pos{};   ///< caret tip in surface space
        Side side = Side::Top;
    };

    struct PositionerResult
    {
        Rect        rect{};        ///< resolved surface-space rect for the surface
        Placement   placement{};   ///< final placement after flip / autoPlacement
        ArrowResult arrow{};
        bool        hidden = false; ///< the anchor scrolled out of view
    };

    struct MiddlewareState
    {
        PositionerInput in;
        Rect            rect{};
        Placement       placement{};
        ArrowResult     arrow{};
        bool            hidden = false;
    };

    using Middleware = std::function<void(MiddlewareState&)>;

    namespace detail
    {
        [[nodiscard]] inline float RectRight(const Rect& r) noexcept { return r.x + r.width; }
        [[nodiscard]] inline float RectBottom(const Rect& r) noexcept { return r.y + r.height; }
        [[nodiscard]] inline float RectCx(const Rect& r) noexcept { return r.x + r.width * 0.5f; }
        [[nodiscard]] inline float RectCy(const Rect& r) noexcept { return r.y + r.height * 0.5f; }

        [[nodiscard]] inline Side Opposite(Side s) noexcept
        {
            switch (s)
            {
                case Side::Top: return Side::Bottom;
                case Side::Bottom: return Side::Top;
                case Side::Left: return Side::Right;
                case Side::Right: return Side::Left;
            }
            return Side::Bottom;
        }

        [[nodiscard]] inline bool IsVertical(Side s) noexcept { return s == Side::Top || s == Side::Bottom; }

        [[nodiscard]] inline Rect PaddedViewport(const PositionerInput& in) noexcept
        {
            return Rect{in.viewportPad.left, in.viewportPad.top,
                        in.surface.x - in.viewportPad.Horizontal(),
                        in.surface.y - in.viewportPad.Vertical()};
        }

        /// A clamp that degrades gracefully when the content is larger than the range (lo > hi → lo).
        [[nodiscard]] inline float ClampLoHi(float v, float lo, float hi) noexcept
        {
            if (hi < lo) return lo;
            return std::clamp(v, lo, hi);
        }

        /// Base placement: position `content` on `placement.side` of `anchor`, aligned along the cross
        /// axis, with `offset` clearance along the side normal.
        [[nodiscard]] inline Rect PlaceOnSide(const Rect& anchor, Vec2 content, Placement placement,
                                              float offset) noexcept
        {
            Rect r{0.f, 0.f, content.x, content.y};
            const auto alignMain = [](Align3 a, float anchorStart, float anchorSize, float size) {
                switch (a)
                {
                    case Align3::Start: return anchorStart;
                    case Align3::Center: return anchorStart + (anchorSize - size) * 0.5f;
                    case Align3::End: return anchorStart + anchorSize - size;
                }
                return anchorStart;
            };
            switch (placement.side)
            {
                case Side::Bottom:
                    r.y = RectBottom(anchor) + offset;
                    r.x = alignMain(placement.align, anchor.x, anchor.width, content.x);
                    break;
                case Side::Top:
                    r.y = anchor.y - offset - content.y;
                    r.x = alignMain(placement.align, anchor.x, anchor.width, content.x);
                    break;
                case Side::Right:
                    r.x = RectRight(anchor) + offset;
                    r.y = alignMain(placement.align, anchor.y, anchor.height, content.y);
                    break;
                case Side::Left:
                    r.x = anchor.x - offset - content.x;
                    r.y = alignMain(placement.align, anchor.y, anchor.height, content.y);
                    break;
            }
            return r;
        }

        [[nodiscard]] inline bool OverflowsOnSide(const Rect& r, const Rect& vp, Side side) noexcept
        {
            switch (side)
            {
                case Side::Bottom: return RectBottom(r) > RectBottom(vp);
                case Side::Top: return r.y < vp.y;
                case Side::Right: return RectRight(r) > RectRight(vp);
                case Side::Left: return r.x < vp.x;
            }
            return false;
        }

        [[nodiscard]] inline float RoomOnSide(const Rect& anchor, const Rect& vp, Side side) noexcept
        {
            switch (side)
            {
                case Side::Top: return anchor.y - vp.y;
                case Side::Bottom: return RectBottom(vp) - RectBottom(anchor);
                case Side::Left: return anchor.x - vp.x;
                case Side::Right: return RectRight(vp) - RectRight(anchor);
            }
            return 0.f;
        }
    }

    /// Gap along the side normal (additive to the base offset).
    [[nodiscard]] inline Middleware Offset(float px)
    {
        return [px](MiddlewareState& s) {
            switch (s.placement.side)
            {
                case Side::Bottom: s.rect.y += px; break;
                case Side::Top: s.rect.y -= px; break;
                case Side::Right: s.rect.x += px; break;
                case Side::Left: s.rect.x -= px; break;
            }
        };
    }

    struct FlipCfg {};
    /// If the surface overflows the padded viewport on its side, flip to the opposite side when that fits.
    [[nodiscard]] inline Middleware Flip(FlipCfg = {})
    {
        return [](MiddlewareState& s) {
            const Rect vp = detail::PaddedViewport(s.in);
            if (!detail::OverflowsOnSide(s.rect, vp, s.placement.side))
            {
                return;
            }
            const Side opp = detail::Opposite(s.placement.side);
            const Rect flipped = detail::PlaceOnSide(s.in.anchor, s.in.content, {opp, s.placement.align}, s.in.offset);
            if (!detail::OverflowsOnSide(flipped, vp, opp))
            {
                s.rect = flipped;
                s.placement.side = opp;
            }
        };
    }

    struct ShiftCfg { float padding = 0.f; };
    /// Slide along the cross axis to stay on-screen (keeping `padding` from the viewport edge).
    [[nodiscard]] inline Middleware Shift(ShiftCfg cfg = {})
    {
        return [cfg](MiddlewareState& s) {
            const Rect vp = detail::PaddedViewport(s.in);
            if (detail::IsVertical(s.placement.side))
            {
                s.rect.x = detail::ClampLoHi(s.rect.x, vp.x + cfg.padding,
                                             detail::RectRight(vp) - s.rect.width - cfg.padding);
            }
            else
            {
                s.rect.y = detail::ClampLoHi(s.rect.y, vp.y + cfg.padding,
                                             detail::RectBottom(vp) - s.rect.height - cfg.padding);
            }
        };
    }

    struct SizeCfg { bool matchAnchorWidth = false; bool clamp = true; };
    /// Match the anchor's width and/or clamp the surface to the viewport so it scrolls instead of
    /// running off-screen.
    [[nodiscard]] inline Middleware Size(SizeCfg cfg = {})
    {
        return [cfg](MiddlewareState& s) {
            if (cfg.matchAnchorWidth)
            {
                s.rect.width = s.in.anchor.width;
            }
            if (cfg.clamp)
            {
                const Rect vp = detail::PaddedViewport(s.in);
                s.rect.width = std::min(s.rect.width, vp.width);
                s.rect.height = std::min(s.rect.height, vp.height);
            }
        };
    }

    struct ArrowCfg { float padding = 4.f; };
    /// Position a caret pointing at the anchor centre, clamped within the surface edge.
    [[nodiscard]] inline Middleware Arrow(ArrowCfg cfg = {})
    {
        return [cfg](MiddlewareState& s) {
            s.arrow.present = true;
            s.arrow.side = detail::Opposite(s.placement.side); // the caret points back at the anchor
            if (detail::IsVertical(s.placement.side))
            {
                const float cx = detail::ClampLoHi(detail::RectCx(s.in.anchor), s.rect.x + cfg.padding,
                                                   detail::RectRight(s.rect) - cfg.padding);
                s.arrow.pos = {cx, s.placement.side == Side::Bottom ? s.rect.y : detail::RectBottom(s.rect)};
            }
            else
            {
                const float cy = detail::ClampLoHi(detail::RectCy(s.in.anchor), s.rect.y + cfg.padding,
                                                   detail::RectBottom(s.rect) - cfg.padding);
                s.arrow.pos = {s.placement.side == Side::Right ? s.rect.x : detail::RectRight(s.rect), cy};
            }
        };
    }

    /// Pick the side with the most room and re-place there before the rest of the pipeline runs.
    [[nodiscard]] inline Middleware AutoPlacement()
    {
        return [](MiddlewareState& s) {
            const Rect vp = detail::PaddedViewport(s.in);
            Side best = s.placement.side;
            float bestRoom = detail::RoomOnSide(s.in.anchor, vp, best);
            static constexpr Side kSides[] = {Side::Top, Side::Bottom, Side::Left, Side::Right};
            for (const Side side : kSides)
            {
                const float room = detail::RoomOnSide(s.in.anchor, vp, side);
                if (room > bestRoom)
                {
                    bestRoom = room;
                    best = side;
                }
            }
            s.placement.side = best;
            s.rect = detail::PlaceOnSide(s.in.anchor, s.in.content, s.placement, s.in.offset);
        };
    }

    /// Hide when the anchor has scrolled fully out of the surface.
    [[nodiscard]] inline Middleware Hide()
    {
        return [](MiddlewareState& s) {
            const Rect surface{0.f, 0.f, s.in.surface.x, s.in.surface.y};
            const bool intersects = s.in.anchor.x < detail::RectRight(surface)
                                 && detail::RectRight(s.in.anchor) > surface.x
                                 && s.in.anchor.y < detail::RectBottom(surface)
                                 && detail::RectBottom(s.in.anchor) > surface.y;
            if (!intersects)
            {
                s.hidden = true;
            }
        };
    }

    /// Resolve the surface's placement: base side x align, then fold each middleware in order.
    [[nodiscard]] inline PositionerResult ComputePosition(const PositionerInput& in,
                                                          std::span<const Middleware> pipeline)
    {
        MiddlewareState s;
        s.in        = in;
        s.placement = in.base;
        s.rect      = detail::PlaceOnSide(in.anchor, in.content, in.base, in.offset);
        for (const Middleware& m : pipeline)
        {
            if (m)
            {
                m(s);
            }
        }
        return PositionerResult{s.rect, s.placement, s.arrow, s.hidden};
    }
}
