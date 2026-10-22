#pragma once

#include "Node/ControlState.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"

#include <lumora/Foundation/NodeId.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace lumora::detail
{
    // Shared node-view helpers so the paint and hit-test walks read transform and child order identically.
    /// The node's pure-visual transform (paint-space translate), or null for kinds with no TransformState.
    [[nodiscard]] inline const TransformState* TransformOf(NodeArena& arena, const LiveNode& n)
    {
        switch (n.kind)
        {
            case LiveKind::Box:
            case LiveKind::Row:
            case LiveKind::Column:
            case LiveKind::Stack:
            case LiveKind::Cell:
                return &arena.PayloadOf<ContainerPayload>(n.id).transform;
            case LiveKind::Grid:
                return &arena.PayloadOf<GridPayload>(n.id).transform;
            case LiveKind::Scroll:
                return &arena.PayloadOf<ScrollPayload>(n.id).transform;
            case LiveKind::VirtualList:
                return &arena.PayloadOf<VirtualListState>(n.id).transform;
            case LiveKind::TextSelectionScope:
                return &arena.PayloadOf<TextSelectionScopePayload>(n.id).transform;
            default:
                return nullptr;
        }
    }

    /// Interaction state read kind-agnostically from whichever payload carries one; null for kinds without it.
    [[nodiscard]] inline InteractState* InteractOf(NodeArena& arena, const LiveNode& n)
    {
        switch (n.kind)
        {
            case LiveKind::Box:
            case LiveKind::Row:
            case LiveKind::Column:
            case LiveKind::Stack:
            case LiveKind::Cell:
                return &arena.PayloadOf<ContainerPayload>(n.id).on;
            case LiveKind::Grid:
                return &arena.PayloadOf<GridPayload>(n.id).on;
            case LiveKind::Scroll:
                return &arena.PayloadOf<ScrollPayload>(n.id).on;
            case LiveKind::VirtualList:
                return &arena.PayloadOf<VirtualListState>(n.id).on;
            case LiveKind::Text:
                return &arena.PayloadOf<TextPayload>(n.id).on;
            case LiveKind::Image:
                return &arena.PayloadOf<ImagePayload>(n.id).on;
            case LiveKind::Icon:
                return &arena.PayloadOf<IconPayload>(n.id).on;
            case LiveKind::Painter:
                return &arena.PayloadOf<PainterPayload>(n.id).on;
            case LiveKind::Svg:
                return &arena.PayloadOf<SvgPayload>(n.id).on;
            case LiveKind::Button:
                return &arena.PayloadOf<ButtonPayload>(n.id).on;
            case LiveKind::TextSelectionScope:
                return &arena.PayloadOf<TextSelectionScopePayload>(n.id).on;
            default:
                return nullptr;
        }
    }

    [[nodiscard]] inline bool IsContainer(LiveKind kind) noexcept
    {
        switch (kind)
        {
            case LiveKind::Box:
            case LiveKind::Row:
            case LiveKind::Column:
            case LiveKind::Stack:
            case LiveKind::Cell:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] inline bool IsFocusScope(NodeArena& arena, NodeId id)
    {
        if (!id.Valid()) return false;
        const LiveNode* n = arena.TryResolve(id);
        if (!n) return false;
        if (IsContainer(n->kind))
        {
            return arena.PayloadOf<ContainerPayload>(id).focusScope;
        }
        return false;
    }

    [[nodiscard]] inline float ContentScaleOf(NodeArena& arena, const LiveNode& n)
    {
        if (IsContainer(n.kind))
        {
            return arena.PayloadOf<ContainerPayload>(n.id).contentScale;
        }
        return 1.f;
    }

    /// The node's paint layer matrix: contentScale about the box top-left, then CSS scale/rotate about transform-origin, then translate.
    [[nodiscard]] inline svg::Affine NodeTransform(NodeArena& arena, const LiveNode& n)
    {
        svg::Affine m;
        const Rect& r = n.solved;
        if (const float cs = ContentScaleOf(arena, n); cs > 0.f && cs != 1.f)
        {
            m = {cs, 0.f, 0.f, cs, r.x * (1.f - cs), r.y * (1.f - cs)};
        }
        if (const TransformState* xf = TransformOf(arena, n))
        {
            if (!xf->IsTranslateOnly())
            {
                const float ox = r.x + r.width * xf->originX;
                const float oy = r.y + r.height * xf->originY;
                const float c = std::cos(xf->rotate), s = std::sin(xf->rotate);
                svg::Affine rs{c * xf->scaleX, s * xf->scaleX, -s * xf->scaleY, c * xf->scaleY, 0.f, 0.f};
                rs.e = ox - (rs.a * ox + rs.c * oy);
                rs.f = oy - (rs.b * ox + rs.d * oy);
                m = rs.Compose(m);
            }
            m.e += xf->TotalX();
            m.f += xf->TotalY();
        }
        return m;
    }

    [[nodiscard]] inline bool IsIdentity(const svg::Affine& m) noexcept
    {
        return m.a == 1.f && m.b == 0.f && m.c == 0.f && m.d == 1.f && m.e == 0.f && m.f == 0.f;
    }

    /// Axis-aligned bounds of `r` mapped through `m`.
    [[nodiscard]] inline Rect MapRect(const svg::Affine& m, const Rect& r)
    {
        const Vec2 p[4] = {m.Apply({r.x, r.y}), m.Apply({r.x + r.width, r.y}), m.Apply({r.x, r.y + r.height}),
                           m.Apply({r.x + r.width, r.y + r.height})};
        float x0 = p[0].x, y0 = p[0].y, x1 = p[0].x, y1 = p[0].y;
        for (const Vec2& q : p)
        {
            x0 = std::min(x0, q.x);
            y0 = std::min(y0, q.y);
            x1 = std::max(x1, q.x);
            y1 = std::max(y1, q.y);
        }
        return Rect{x0, y0, x1 - x0, y1 - y0};
    }

    [[nodiscard]] inline NodeId FindEnclosingScope(NodeArena& arena, NodeId node)
    {
        for (NodeId p = node; p.Valid(); p = arena.Resolve(p).parent)
        {
            if (IsFocusScope(arena, p))
            {
                return p;
            }
        }
        return NodeId{};
    }

    [[nodiscard]] inline bool IsInScope(NodeArena& arena, NodeId node, NodeId scope)
    {
        if (!scope.Valid()) return true;
        for (NodeId p = node; p.Valid(); p = arena.Resolve(p).parent)
        {
            if (p == scope) return true;
        }
        return false;
    }

    inline void RecordScopeFocus(NodeArena& arena, NodeId focusedId)
    {
        if (!focusedId.Valid()) return;
        for (NodeId p = arena.Resolve(focusedId).parent; p.Valid(); p = arena.Resolve(p).parent)
        {
            const LiveNode& pn = arena.Resolve(p);
            if (IsContainer(pn.kind))
            {
                auto& payload = arena.PayloadOf<ContainerPayload>(p);
                if (payload.focusScope)
                {
                    payload.lastFocusedId = focusedId;
                    payload.lastFocusedGen = arena.GenerationOf(focusedId);
                }
            }
        }
    }

    /// Empty if the scope's last-focused node is unbound or stale.
    [[nodiscard]] inline NodeId GetScopeLastFocused(NodeArena& arena, NodeId scopeId)
    {
        if (!scopeId.Valid()) return NodeId{};
        const LiveNode* n = arena.TryResolve(scopeId);
        if (!n || !IsContainer(n->kind)) return NodeId{};
        const auto& payload = arena.PayloadOf<ContainerPayload>(scopeId);
        if (payload.lastFocusedId.Valid() && arena.TryResolve(payload.lastFocusedId) &&
            arena.GenerationOf(payload.lastFocusedId) == payload.lastFocusedGen)
        {
            return payload.lastFocusedId;
        }
        return NodeId{};
    }

    /// Surface-space translate the emit walk applies to `target`: ancestor transforms add, *ancestor* Scroll
    /// offsets subtract. Anything reading an on-screen box outside the emit walk (focus ring, anchor rects) must include it.
    [[nodiscard]] inline Vec2 PaintOffset(NodeArena& arena, NodeId target)
    {
        Vec2 off{};
        for (NodeId id = target; id.Valid();)
        {
            LiveNode* n = arena.TryResolve(id);
            if (!n)
            {
                break;
            }
            if (const TransformState* xf = TransformOf(arena, *n); xf && !xf->IsIdentity())
            {
                off.x += xf->TotalX();
                off.y += xf->TotalY();
            }
            if (id != target && n->kind == LiveKind::Scroll)
            {
                const ScrollPayload& sp = arena.PayloadOf<ScrollPayload>(id);
                off.x -= sp.offsetX;
                off.y -= sp.offsetY;
            }
            id = n->parent;
        }
        return off;
    }

    /// `target`'s solved rect moved into surface space by PaintOffset - where it is actually drawn.
    [[nodiscard]] inline Rect PaintedRect(NodeArena& arena, NodeId target, const LiveNode& n)
    {
        const Vec2 off = PaintOffset(arena, target);
        return Rect{n.solved.x + off.x, n.solved.y + off.y, n.solved.width, n.solved.height};
    }

    /// Children of `parent` in paint order (ascending z, stable). Returns false, leaving `out` untouched, when no child sets z.
    [[nodiscard]] inline bool ChildrenByZ(NodeArena& arena, const LiveNode& parent, std::vector<NodeId>& out)
    {
        bool anyZ = false;
        for (NodeId c = parent.firstChild; c.Valid(); c = arena.Resolve(c).nextSibling)
        {
            if (arena.Resolve(c).style.z != 0)
            {
                anyZ = true;
                break;
            }
        }
        if (!anyZ)
        {
            return false;
        }
        out.clear();
        for (NodeId c = parent.firstChild; c.Valid(); c = arena.Resolve(c).nextSibling)
        {
            out.push_back(c);
        }
        std::stable_sort(out.begin(), out.end(),
                         [&arena](NodeId a, NodeId b) { return arena.Resolve(a).style.z < arena.Resolve(b).style.z; });
        return true;
    }
} // namespace lumora::detail
