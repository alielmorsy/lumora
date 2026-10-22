#pragma once

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Input/Event.h>
#include <lumora/Widgets/Props.h>

#include "Input/Interaction.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"

#include <algorithm>
#include <cstdint>

namespace lumora::scrollcore
{
    // Scrolling behaviour shared by scroll-like kinds (Scroll, VirtualList): track/thumb geometry, wheel, thumb drag.

    constexpr float kBarThickness = 10.f; ///< bar/thumb width (also the thumb hit width)
    constexpr float kBarMargin    = 2.f;  ///< inset from the viewport edges
    constexpr float kMinThumbLen  = 30.f; ///< floor so a long page keeps a grabbable thumb
    constexpr float kThumbRadius  = kBarThickness * 0.5f;
    constexpr float kWheelStep    = 48.f;
    constexpr Color kThumbColor{165, 165, 175, 255};

    /// Thumb alpha ramps with interaction: faint at rest, brighter as the pointer nears it.
    [[nodiscard]] constexpr std::uint8_t ThumbAlpha(bool dragging, bool thumbHover, bool hovered) noexcept
    {
        return dragging ? 180 : thumbHover ? 140 : hovered ? 95 : 45;
    }

    struct Extent
    {
        float viewW = 0.f, viewH = 0.f;
        float contentW = 0.f, contentH = 0.f;
        float maxX = 0.f, maxY = 0.f;
        bool overflowX = false, overflowY = false;
    };

    [[nodiscard]] inline Extent MakeExtent(const Rect& viewport, float contentW, float contentH)
    {
        Extent e;
        e.viewW     = viewport.width;
        e.viewH     = viewport.height;
        e.contentW  = contentW;
        e.contentH  = contentH;
        e.maxX      = std::max(0.f, e.contentW - e.viewW);
        e.maxY      = std::max(0.f, e.contentH - e.viewH);
        e.overflowX = e.maxX > 0.5f;
        e.overflowY = e.maxY > 0.5f;
        return e;
    }

    /// Accumulates one child's far edges, descending through transparent kinds (`Show`, `Each`, `Memo`,
    /// `Component`, `Portal`): they own no solver node, so their `solved` rect is zero.
    inline void ReachOf(NodeArena& arena, const LiveNode& n, NodeId id, float& right, float& bottom)
    {
        const LiveNode& child = arena.Resolve(id);
        if (IsTransparent(child.kind))
        {
            for (NodeId c = child.firstChild; c.Valid(); c = arena.Resolve(c).nextSibling)
                ReachOf(arena, n, c, right, bottom);
            return;
        }
        right  = std::max(right, child.solved.Right() - n.solved.x);
        bottom = std::max(bottom, child.solved.Bottom() - n.solved.y);
    }

    /// Content reach as the children's bounding box from the viewport's top-left. The offset is paint-only,
    /// so solved rects are un-scrolled and far edges are the extent directly.
    [[nodiscard]] inline Extent MeasureChildren(NodeArena& arena, const LiveNode& n)
    {
        float right = 0.f, bottom = 0.f;
        for (NodeId c = n.firstChild; c.Valid(); c = arena.Resolve(c).nextSibling)
            ReachOf(arena, n, c, right, bottom);
        return MakeExtent(n.solved, right, bottom);
    }

    struct AxisBar
    {
        Rect  track{};      ///< full track region along the trailing edge (press hit area)
        Rect  thumb{};      ///< draggable thumb within the track
        float travel = 0.f; ///< px the thumb can travel along the track (track - thumb length)
    };

    /// Track + thumb in viewport space. The track shortens by the other bar's thickness when both show.
    [[nodiscard]] inline AxisBar VBar(const Rect& v, const Extent& e, float offsetY)
    {
        const float trackLen = std::max(0.f, v.height - 2.f * kBarMargin - (e.overflowX ? kBarThickness : 0.f));
        const float ratio    = e.contentH > 0.f ? e.viewH / e.contentH : 1.f;
        const float thumbLen = std::clamp(trackLen * ratio, std::min(kMinThumbLen, trackLen), trackLen);
        const float travel   = std::max(0.f, trackLen - thumbLen);
        const float t        = e.maxY > 0.f ? std::clamp(offsetY / e.maxY, 0.f, 1.f) : 0.f;
        const float x        = v.Right() - kBarThickness - kBarMargin;
        const float top      = v.y + kBarMargin;
        return {Rect{x, top, kBarThickness, trackLen}, Rect{x, top + travel * t, kBarThickness, thumbLen}, travel};
    }

    [[nodiscard]] inline AxisBar HBar(const Rect& v, const Extent& e, float offsetX)
    {
        const float trackLen = std::max(0.f, v.width - 2.f * kBarMargin - (e.overflowY ? kBarThickness : 0.f));
        const float ratio    = e.contentW > 0.f ? e.viewW / e.contentW : 1.f;
        const float thumbLen = std::clamp(trackLen * ratio, std::min(kMinThumbLen, trackLen), trackLen);
        const float travel   = std::max(0.f, trackLen - thumbLen);
        const float t        = e.maxX > 0.f ? std::clamp(offsetX / e.maxX, 0.f, 1.f) : 0.f;
        const float y        = v.Bottom() - kBarThickness - kBarMargin;
        const float left     = v.x + kBarMargin;
        return {Rect{left, y, trackLen, kBarThickness}, Rect{left + travel * t, y, thumbLen, kBarThickness}, travel};
    }

    /// The per-node scroll state, addressed by reference so each kind keeps its own payload layout.
    struct Gesture
    {
        ScrollAxis axis = ScrollAxis::Vertical;
        float* offsetX  = nullptr;
        float* offsetY  = nullptr;
        bool*  draggingX = nullptr;
        bool*  draggingY = nullptr;
        float* grabOffset = nullptr;
        Vec2*  grabCursor = nullptr;
        bool*  hovered = nullptr;
        bool*  hoverX  = nullptr;
        bool*  hoverY  = nullptr;

        /// Paint-space scroll needs only a repaint; a virtualized kind materializes different rows at the new
        /// offset, so it needs the solve too.
        bool relayoutOnScroll = false;
    };

    [[nodiscard]] inline bool ShowV(const Gesture& g, const Extent& e) noexcept
    {
        return e.overflowY && (g.axis == ScrollAxis::Vertical || g.axis == ScrollAxis::Both);
    }

    [[nodiscard]] inline bool ShowH(const Gesture& g, const Extent& e) noexcept
    {
        return e.overflowX && (g.axis == ScrollAxis::Horizontal || g.axis == ScrollAxis::Both);
    }

    /// Re-clamp against the current extent: content can shrink with no input event; runs before paint.
    inline void Clamp(const Gesture& g, const Extent& e)
    {
        *g.offsetX = std::clamp(*g.offsetX, 0.f, e.maxX);
        *g.offsetY = std::clamp(*g.offsetY, 0.f, e.maxY);
    }

    /// The bar/wheel half of a scroll-like OnEvent. Returns true when the core owns this step, so the
    /// caller must skip its own dispatch. `selfTransform` (node paint translate) is undone for hit tests.
    inline bool HandleEvent(EventContext& ctx, Event& ev, LiveNode& n, const Extent& e, const Gesture& g,
                            Vec2 selfTransform)
    {
        const bool showV = ShowV(g, e);
        const bool showH = ShowH(g, e);

        // An offset change costs a repaint, plus a re-solve when it changes which rows exist.
        const Dirty moved = g.relayoutOnScroll ? (Dirty::Layout | Dirty::Repaint) : Dirty::Repaint;

        Vec2 p = ev.Position;
        p.x -= selfTransform.x;
        p.y -= selfTransform.y;

        // Active drag: pointer capture routes moves here. The latch is valid only while this node holds
        // capture (DnD arming, a rebuilt node or a lost release clear it), else it scrolls on bare mouse moves.
        if (*g.draggingY || *g.draggingX)
        {
            if (ctx.state->captured.Resolve(*ctx.arena) != ctx.node)
            {
                *g.draggingX = *g.draggingY = false;
                n.MarkDirty(Dirty::Repaint);
            }
            else if (ev.Type == EventType::MouseMove)
            {
                if (*g.draggingY)
                {
                    const float travel = VBar(n.solved, e, *g.offsetY).travel;
                    if (travel > 0.f)
                    {
                        *g.offsetY = std::clamp(
                            *g.grabOffset + (ev.Position.y - g.grabCursor->y) * (e.maxY / travel), 0.f, e.maxY);
                    }
                }
                else
                {
                    const float travel = HBar(n.solved, e, *g.offsetX).travel;
                    if (travel > 0.f)
                    {
                        *g.offsetX = std::clamp(
                            *g.grabOffset + (ev.Position.x - g.grabCursor->x) * (e.maxX / travel), 0.f, e.maxX);
                    }
                }
                n.MarkDirty(moved);
                ev.Consume();
                return true;
            }
            else if (ev.Type == EventType::MouseUp)
            {
                *g.draggingX = *g.draggingY = false;
                ctx.ReleaseCapture();
                ev.Consume();
                return true;
            }
        }

        // Press claimed on the Capture pass so a child under the overlay bar cannot steal it; a track press
        // outside the thumb jumps to centre it under the cursor, then drags.
        if (ctx.phase == RoutePhase::Capture && ev.Type == EventType::MouseDown &&
            ev.Button == PointerButton::Left)
        {
            if (showV)
            {
                const AxisBar bar = VBar(n.solved, e, *g.offsetY);
                if (bar.track.Contains(p))
                {
                    if (!bar.thumb.Contains(p) && bar.travel > 0.f)
                    {
                        const float t = (p.y - bar.thumb.height * 0.5f - bar.track.y) / bar.travel;
                        *g.offsetY    = std::clamp(t * e.maxY, 0.f, e.maxY);
                    }
                    *g.draggingY  = true;
                    *g.grabOffset = *g.offsetY;
                    *g.grabCursor = ev.Position;
                    ctx.Capture();
                    n.MarkDirty(moved);
                    ev.Consume();
                    return true;
                }
            }
            if (showH)
            {
                const AxisBar bar = HBar(n.solved, e, *g.offsetX);
                if (bar.track.Contains(p))
                {
                    if (!bar.thumb.Contains(p) && bar.travel > 0.f)
                    {
                        const float t = (p.x - bar.thumb.width * 0.5f - bar.track.x) / bar.travel;
                        *g.offsetX    = std::clamp(t * e.maxX, 0.f, e.maxX);
                    }
                    *g.draggingX  = true;
                    *g.grabOffset = *g.offsetX;
                    *g.grabCursor = ev.Position;
                    ctx.Capture();
                    n.MarkDirty(moved);
                    ev.Consume();
                    return true;
                }
            }
            // Missed the bars: the press flows to children; the caller must not treat a capture-phase press as onPress.
            return true;
        }

        if (ctx.phase != RoutePhase::Capture)
        {
            if (ev.Type == EventType::MouseEnter)
            {
                if (!*g.hovered)
                {
                    *g.hovered = true;
                    n.MarkDirty(Dirty::Repaint);
                }
            }
            else if (ev.Type == EventType::MouseLeave)
            {
                if (*g.hovered || *g.hoverX || *g.hoverY)
                {
                    *g.hovered = false;
                    *g.hoverX = *g.hoverY = false;
                    n.MarkDirty(Dirty::Repaint);
                }
            }
            else if (ev.Type == EventType::MouseMove)
            {
                const bool hv = showV && VBar(n.solved, e, *g.offsetY).thumb.Contains(p);
                const bool hh = showH && HBar(n.solved, e, *g.offsetX).thumb.Contains(p);
                if (hv != *g.hoverY || hh != *g.hoverX)
                {
                    *g.hoverY = hv;
                    *g.hoverX = hh;
                    n.MarkDirty(Dirty::Repaint);
                }
            }
        }

        if (ctx.phase != RoutePhase::Capture && ev.Type == EventType::Scroll)
        {
            const bool wantH = g.axis == ScrollAxis::Horizontal ||
                               (g.axis == ScrollAxis::Both &&
                                (Has(ev.Mods, KeyMods::Shift) || (!e.overflowY && e.overflowX)));
            bool changed = false;
            if (wantH && e.maxX > 0.f)
            {
                const float delta = ev.ScrollDelta.x != 0.f ? ev.ScrollDelta.x : ev.ScrollDelta.y;
                const float next  = std::clamp(*g.offsetX - delta * kWheelStep, 0.f, e.maxX);
                changed    = next != *g.offsetX;
                *g.offsetX = next;
            }
            else
            {
                if (e.maxY > 0.f)
                {
                    const float next = std::clamp(*g.offsetY - ev.ScrollDelta.y * kWheelStep, 0.f, e.maxY);
                    changed    = next != *g.offsetY;
                    *g.offsetY = next;
                }
                // A trackpad's sideways component (or a tilt wheel) pans a two-axis view directly.
                if (g.axis == ScrollAxis::Both && ev.ScrollDelta.x != 0.f && e.maxX > 0.f)
                {
                    const float next = std::clamp(*g.offsetX - ev.ScrollDelta.x * kWheelStep, 0.f, e.maxX);
                    changed    = changed || next != *g.offsetX;
                    *g.offsetX = next;
                }
            }
            if (changed)
            {
                n.MarkDirty(moved);
                ev.Consume(); // owns the wheel only while it can move; bubbles out at the ends
                return true;
            }
        }

        return false;
    }
} // namespace lumora::scrollcore
