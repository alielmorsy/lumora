#include "Input/FocusTraversal.h"

#include "Input/Dispatch.h"
#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Node/View.h"
#include "Runtime/ChildOrder.h"
#include "Runtime/OverlayRoot.h"
#include "Widgets/Widget.h"

#include <lumora/Foundation/Rect.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace lumora
{
    namespace
    {
        struct RingEntry
        {
            NodeRef       ref;
            int           tabIndex;
            std::uint32_t seq; ///< tree-order rank, the stable tiebreak within a tabIndex group
        };

        /// The node's Tab participation via its Widget; transparent/unregistered kinds are never tab stops.
        [[nodiscard]] Widget::FocusPolicyResult QueryPolicy(const EventContext& base, NodeId id)
        {
            const LiveNode& n = base.arena->Resolve(id);
            if (n.kind == LiveKind::Component)
            {
                return {0, false};
            }
            Widget* widget = KindRegistry::For(n.kind);
            if (!widget)
            {
                return {0, false};
            }
            EventContext ctx = base;
            ctx.node = id;
            return widget->TabPolicy(ctx);
        }

        void Walk(const EventContext& base, NodeId id, std::vector<RingEntry>& acc, std::uint32_t& seq)
        {
            if (!id.Valid())
            {
                return;
            }
            const LiveNode& n = base.arena->Resolve(id);
            if (IsTransparent(n.kind)) // host: no solver node, descend without emitting
            {
                for (NodeId c = n.firstChild; c.Valid(); c = base.arena->Resolve(c).nextSibling)
                {
                    Walk(base, c, acc, seq);
                }
                return;
            }
            if (!Visible(*base.arena, n)) // an invisible subtree is unreachable, like in paint
            {
                return;
            }
            const Widget::FocusPolicyResult pol = QueryPolicy(base, id);
            if (pol.isTabStop && pol.tabIndex >= 0)
            {
                acc.push_back({NodeRef{*base.arena, id}, pol.tabIndex, seq++});
            }
            for (NodeId c = n.firstChild; c.Valid(); c = base.arena->Resolve(c).nextSibling)
            {
                Walk(base, c, acc, seq);
            }
        }

        /// The Tab ring's subtree: the topmost focus-trapping overlay's content, else the innermost focusScope ancestor of focus, else `root`.
        /// Traversal never escapes a focusScope.
        [[nodiscard]] NodeId ResolveScope(const EventContext& base, InteractionState& s, NodeId root)
        {
            NodeId scopeLimit = root;
            if (s.overlays)
            {
                const NodeId trap = s.overlays->TopmostTrappingContent();
                if (trap.Valid() && base.arena->TryResolve(trap))
                {
                    scopeLimit = trap;
                }
            }

            const NodeId cur = s.focused.Resolve(*base.arena);
            if (cur.Valid())
            {
                NodeId inner{};
                for (NodeId a = cur; a.Valid();)
                {
                    if (a == scopeLimit)
                    {
                        return inner.Valid() ? inner : scopeLimit;
                    }
                    if (!inner.Valid() && detail::IsFocusScope(*base.arena, a))
                    {
                        inner = a;
                    }
                    const LiveNode* n = base.arena->TryResolve(a);
                    if (!n) break;
                    a = n->parent;
                }
            }
            return scopeLimit;
        }

        /// Brings `target` into view in its nearest ancestor Scroll by the minimal offset move. Scroll offset is paint-only,
        /// so on-screen position is `rect - viewport - offset`.
        void ScrollIntoView(NodeArena& arena, NodeId target)
        {
            if (!target.Valid())
            {
                return;
            }
            NodeId scrollId{};
            for (NodeId a = arena.Resolve(target).parent; a.Valid(); a = arena.Resolve(a).parent)
            {
                if (arena.Resolve(a).kind == LiveKind::Scroll)
                {
                    scrollId = a;
                    break;
                }
            }
            if (!scrollId.Valid())
            {
                return;
            }

            LiveNode&       sc  = arena.Resolve(scrollId);
            const LiveNode& tgt = arena.Resolve(target);
            ScrollPayload&  p   = arena.PayloadOf<ScrollPayload>(scrollId);

            // Content extent for clamping (mirrors Scroll's ComputeExtent: far edges of the children).
            float right = 0.f, bottom = 0.f;
            for (NodeId c = sc.firstChild; c.Valid(); c = arena.Resolve(c).nextSibling)
            {
                const LiveNode& ch = arena.Resolve(c);
                right  = std::max(right, ch.solved.Right() - sc.solved.x);
                bottom = std::max(bottom, ch.solved.Bottom() - sc.solved.y);
            }
            const float maxX = std::max(0.f, right - sc.solved.width);
            const float maxY = std::max(0.f, bottom - sc.solved.height);

            const float relTop    = tgt.solved.y - sc.solved.y;
            const float relBottom = tgt.solved.Bottom() - sc.solved.y;
            const float relLeft   = tgt.solved.x - sc.solved.x;
            const float relRight  = tgt.solved.Right() - sc.solved.x;

            float ny = p.offsetY;
            if (relTop < ny)
            {
                ny = relTop;
            }
            else if (relBottom > ny + sc.solved.height)
            {
                ny = relBottom - sc.solved.height;
            }
            float nx = p.offsetX;
            if (relLeft < nx)
            {
                nx = relLeft;
            }
            else if (relRight > nx + sc.solved.width)
            {
                nx = relRight - sc.solved.width;
            }

            nx = std::clamp(nx, 0.f, maxX);
            ny = std::clamp(ny, 0.f, maxY);
            if (nx != p.offsetX || ny != p.offsetY)
            {
                p.offsetX = nx;
                p.offsetY = ny;
                sc.MarkDirty(Dirty::Repaint);
            }
        }

        struct ScoreResult
        {
            bool inHalfPlane = false;
            float score = 0.f;
        };

        [[nodiscard]] ScoreResult ScoreDirectional(const Rect& src, const Rect& cand, Direction dir)
        {
            const Vec2 srcCenter{src.x + src.width * 0.5f, src.y + src.height * 0.5f};
            const Vec2 candCenter{cand.x + cand.width * 0.5f, cand.y + cand.height * 0.5f};

            float primary = 0.f;
            float ortho = 0.f;
            bool orthoOverlap = false;

            switch (dir)
            {
                case Direction::Up:
                {
                    if (candCenter.y >= srcCenter.y - 0.001f) return {false, 0.f};
                    primary = (src.y >= cand.Bottom()) ? (src.y - cand.Bottom()) : (srcCenter.y - candCenter.y);
                    orthoOverlap = (std::max(src.x, cand.x) < std::min(src.Right(), cand.Right()));
                    if (orthoOverlap)
                    {
                        ortho = std::abs(candCenter.x - srcCenter.x);
                    }
                    else
                    {
                        const float edgeDist = (cand.x >= src.Right()) ? (cand.x - src.Right()) : (src.x - cand.Right());
                        ortho = edgeDist + std::abs(candCenter.x - srcCenter.x) * 0.5f;
                    }
                    break;
                }
                case Direction::Down:
                {
                    if (candCenter.y <= srcCenter.y + 0.001f) return {false, 0.f};
                    primary = (cand.y >= src.Bottom()) ? (cand.y - src.Bottom()) : (candCenter.y - srcCenter.y);
                    orthoOverlap = (std::max(src.x, cand.x) < std::min(src.Right(), cand.Right()));
                    if (orthoOverlap)
                    {
                        ortho = std::abs(candCenter.x - srcCenter.x);
                    }
                    else
                    {
                        const float edgeDist = (cand.x >= src.Right()) ? (cand.x - src.Right()) : (src.x - cand.Right());
                        ortho = edgeDist + std::abs(candCenter.x - srcCenter.x) * 0.5f;
                    }
                    break;
                }
                case Direction::Left:
                {
                    if (candCenter.x >= srcCenter.x - 0.001f) return {false, 0.f};
                    primary = (src.x >= cand.Right()) ? (src.x - cand.Right()) : (srcCenter.x - candCenter.x);
                    orthoOverlap = (std::max(src.y, cand.y) < std::min(src.Bottom(), cand.Bottom()));
                    if (orthoOverlap)
                    {
                        ortho = std::abs(candCenter.y - srcCenter.y);
                    }
                    else
                    {
                        const float edgeDist = (cand.y >= src.Bottom()) ? (cand.y - src.Bottom()) : (src.y - cand.Bottom());
                        ortho = edgeDist + std::abs(candCenter.y - srcCenter.y) * 0.5f;
                    }
                    break;
                }
                case Direction::Right:
                {
                    if (candCenter.x <= srcCenter.x + 0.001f) return {false, 0.f};
                    primary = (cand.x >= src.Right()) ? (cand.x - src.Right()) : (candCenter.x - srcCenter.x);
                    orthoOverlap = (std::max(src.y, cand.y) < std::min(src.Bottom(), cand.Bottom()));
                    if (orthoOverlap)
                    {
                        ortho = std::abs(candCenter.y - srcCenter.y);
                    }
                    else
                    {
                        const float edgeDist = (cand.y >= src.Bottom()) ? (cand.y - src.Bottom()) : (src.y - cand.Bottom());
                        ortho = edgeDist + std::abs(candCenter.y - srcCenter.y) * 0.5f;
                    }
                    break;
                }
            }

            float score = primary + 3.f * ortho;
            if (orthoOverlap)
            {
                score *= 0.5f;
            }
            return {true, score};
        }
    } // namespace

    void FocusTraversal::CollectRing(const EventContext& base, NodeId scope, std::vector<NodeRef>& out)
    {
        std::vector<RingEntry> acc;
        std::uint32_t          seq = 0;
        Walk(base, scope, acc, seq);

        std::stable_sort(acc.begin(), acc.end(), [](const RingEntry& a, const RingEntry& b)
        {
            const bool aPos = a.tabIndex > 0;
            const bool bPos = b.tabIndex > 0;
            if (aPos != bPos)
            {
                return aPos; // positive tabIndex group comes before the defaults
            }
            if (aPos && a.tabIndex != b.tabIndex)
            {
                return a.tabIndex < b.tabIndex;
            }
            return a.seq < b.seq;
        });

        out.clear();
        out.reserve(acc.size());
        for (const RingEntry& e : acc)
        {
            out.push_back(e.ref);
        }
    }

    void FocusTraversal::Move(const EventContext& base, InteractionState& s, NodeId scopeRoot, FocusDir dir)
    {
        const NodeId         scope = ResolveScope(base, s, scopeRoot);
        std::vector<NodeRef> ring;
        CollectRing(base, scope, ring);
        if (ring.empty())
        {
            return;
        }

        const NodeId cur = s.focused.Resolve(*base.arena);
        std::size_t  idx = ring.size(); // sentinel: focus not found in this ring
        if (cur.Valid())
        {
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                if (ring[i].Resolve(*base.arena) == cur)
                {
                    idx = i;
                    break;
                }
            }
        }

        std::size_t next;
        if (idx == ring.size())
        {
            next = dir == FocusDir::Forward ? 0 : ring.size() - 1;
        }
        else
        {
            const std::size_t count = ring.size();
            next = dir == FocusDir::Forward ? (idx + 1) % count : (idx + count - 1) % count;
        }

        RequestFocus(*base.arena, s, ring[next], /*viaKeyboard=*/true);
        ScrollIntoView(*base.arena, ring[next].Resolve(*base.arena));
    }

    bool FocusTraversal::MoveDirectional(const EventContext& base, InteractionState& s, NodeId scopeRoot,
                                        Direction dir)
    {
        const NodeId scope = ResolveScope(base, s, scopeRoot);
        std::vector<NodeRef> ring;
        CollectRing(base, scope, ring);
        if (ring.empty())
        {
            return false;
        }

        const NodeId cur = s.focused.Resolve(*base.arena);
        if (!cur.Valid())
        {
            // Nothing focused: restore scope memory or focus first stop in ring
            NodeId target = detail::GetScopeLastFocused(*base.arena, scope);
            if (!target.Valid() || !base.arena->TryResolve(target))
            {
                target = ring.front().Resolve(*base.arena);
            }
            if (target.Valid())
            {
                RequestFocus(*base.arena, s, NodeRef{*base.arena, target}, /*viaKeyboard=*/true);
                ScrollIntoView(*base.arena, target);
                return true;
            }
            return false;
        }

        const InteractState* on = detail::InteractOf(*base.arena, base.arena->Resolve(cur));
        if (on != nullptr)
        {
            std::optional<Anchor> overrideAnchor;
            switch (dir)
            {
                case Direction::Up:    overrideAnchor = on->nav.up; break;
                case Direction::Down:  overrideAnchor = on->nav.down; break;
                case Direction::Left:  overrideAnchor = on->nav.left; break;
                case Direction::Right: overrideAnchor = on->nav.right; break;
            }
            if (overrideAnchor.has_value() && overrideAnchor->State())
            {
                const NodeId targetId = overrideAnchor->State()->id;
                if (targetId.Valid() && base.arena->TryResolve(targetId) &&
                    base.arena->GenerationOf(targetId) == overrideAnchor->State()->gen)
                {
                    RequestFocus(*base.arena, s, NodeRef{*base.arena, targetId}, /*viaKeyboard=*/true);
                    ScrollIntoView(*base.arena, targetId);
                    return true;
                }
            }
        }

        const Rect srcRect = base.arena->Resolve(cur).solved;
        NodeRef bestRef{};
        float bestScore = std::numeric_limits<float>::infinity();

        for (const NodeRef& candidateRef : ring)
        {
            const NodeId candId = candidateRef.Resolve(*base.arena);
            if (!candId.Valid() || candId == cur)
            {
                continue;
            }
            const Rect candRect = base.arena->Resolve(candId).solved;
            const auto [inHalfPlane, score] = ScoreDirectional(srcRect, candRect, dir);
            if (inHalfPlane && score < bestScore)
            {
                bestScore = score;
                bestRef = candidateRef;
            }
        }

        if (bestRef.id.Valid())
        {
            const NodeId bestId = bestRef.Resolve(*base.arena);
            RequestFocus(*base.arena, s, bestRef, /*viaKeyboard=*/true);
            ScrollIntoView(*base.arena, bestId);
            return true;
        }

        return false;
    }
} // namespace lumora
