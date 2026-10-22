#include "Runtime/OverlayRoot.h"

#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/View.h"
#include "Runtime/ChildOrder.h"

#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/Layouts.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace lumora
{
    namespace
    {
        /// True if `node` is `ancestor` or beneath it (parent links chain through transparent nodes too).
        [[nodiscard]] bool IsAncestorOrSelf(NodeArena& arena, NodeId ancestor, NodeId node) noexcept
        {
            if (!ancestor.Valid())
            {
                return false;
            }
            for (NodeId n = node; n.Valid(); n = arena.Resolve(n).parent)
            {
                if (n == ancestor)
                {
                    return true;
                }
            }
            return false;
        }

        /// True when a clipping ancestor has scrolled the trigger entirely out of view (alive does not mean visible).
        [[nodiscard]] bool ClippedAway(NodeArena& arena, NodeId id, const Rect& painted)
        {
            for (NodeId a = arena.Resolve(id).parent; a.Valid();)
            {
                LiveNode* n = arena.TryResolve(a);
                if (!n)
                {
                    break;
                }
                const ViewState* view  = ViewOf(arena, *n);
                const bool       clips = (view && view->clip) || n->kind == LiveKind::Scroll;
                if (clips && !painted.Intersects(detail::PaintedRect(arena, a, *n)))
                {
                    return true;
                }
                a = n->parent;
            }
            return false;
        }

        /// Why the anchor produced no rect: Hidden (scrolled out of view; hide and come back) vs Gone (trigger unmounted; dismiss).
        enum class AnchorMiss : std::uint8_t { None, Hidden, Gone };
/// The anchor's live rect in surface space: the trigger's *painted* rect (a Scroll offsets children only at paint time, so the solved rect is stale),
/// or a zero-size rect at the pointer for pointer anchors. Sets `miss` when there is no rect.

        [[nodiscard]] std::optional<Rect> ResolveAnchorRect(NodeArena& arena,
                                                            const Ref<detail::AnchorState>& anchor,
                                                            AnchorMiss& miss)
        {
            miss = AnchorMiss::None;
            if (!anchor)
            {
                miss = AnchorMiss::Gone;
                return std::nullopt;
            }
            if (anchor->isPointer)
            {
                return Rect{anchor->pointer.x, anchor->pointer.y, 0.f, 0.f};
            }
            if (!anchor->id.Valid() || arena.GenerationOf(anchor->id) != anchor->gen)
            {
                miss = AnchorMiss::Gone;
                return std::nullopt;
            }
            const LiveNode* node = arena.TryResolve(anchor->id);
            if (!node)
            {
                miss = AnchorMiss::Gone;
                return std::nullopt;
            }
            const Rect painted = detail::PaintedRect(arena, anchor->id, *node);
            if (ClippedAway(arena, anchor->id, painted))
            {
                miss = AnchorMiss::Hidden;
                return std::nullopt;
            }
            return painted;
        }

        [[nodiscard]] bool NearlyEq(float a, float b) noexcept { return std::abs(a - b) < 0.5f; }
    } // namespace

    const PortalEntry* OverlayRoot::Find(std::uint64_t id) const noexcept
    {
        for (const PortalEntry& e : m_entries)
        {
            if (e.id == id)
            {
                return &e;
            }
        }
        return nullptr;
    }

    void OverlayRoot::Remove(std::uint64_t id)
    {
        std::erase_if(m_entries, [id](const PortalEntry& e) { return e.id == id; });
    }

    void OverlayRoot::RequestDismiss(std::uint64_t id, DismissReason reason)
    {
        for (const auto& [pendingId, pendingReason] : m_pendingDismiss)
        {
            if (pendingId == id)
            {
                return;
            }
        }
        m_pendingDismiss.emplace_back(id, reason);
    }

    bool OverlayRoot::DismissTopmostEscapable()
    {
        const PortalEntry* top = nullptr;
        for (const PortalEntry& e : m_entries)
        {
            if (!e.options.escapeDismiss)
            {
                continue;
            }
            if (!top || e.layer > top->layer || (e.layer == top->layer && e.seq > top->seq))
            {
                top = &e;
            }
        }
        if (!top)
        {
            return false;
        }
        RequestDismiss(top->id, DismissReason::Escape);
        return true;
    }

    NodeId OverlayRoot::TopmostTrappingContent() const noexcept
    {
        const PortalEntry* top = nullptr;
        for (const PortalEntry& e : m_entries)
        {
            const bool traps = e.options.trapsFocus || e.layer == Layer::Modal || e.layer == Layer::Drawer;
            if (!traps)
            {
                continue;
            }
            if (!top || e.layer > top->layer || (e.layer == top->layer && e.seq > top->seq))
            {
                top = &e;
            }
        }
        return top ? top->content : NodeId{};
    }

    void OverlayRoot::LightDismissOutside(NodeArena& arena, NodeId hitLeaf)
    {
        for (const PortalEntry& e : m_entries)
        {
            if (IsAncestorOrSelf(arena, e.content, hitLeaf))
            {
                return;
            }
        }
        for (const PortalEntry& e : m_entries)
        {
            if (e.options.lightDismiss)
            {
                RequestDismiss(e.id, DismissReason::LightDismiss);
            }
        }
    }

    void OverlayRoot::Flush()
    {
        if (m_pendingDismiss.empty())
        {
            return;
        }
        std::vector<std::pair<std::uint64_t, DismissReason>> pending;
        pending.swap(m_pendingDismiss); // re-entrant requests queue for the next Flush
        for (const auto& [id, reason] : pending)
        {
            if (const PortalEntry* e = Find(id); e && e->options.onDismiss)
            {
                e->options.onDismiss(reason);
            }
        }
    }

    void OverlayRoot::RemovePositioner(NodeId panel)
    {
        std::erase_if(m_positioners, [panel](const PositionerEntry& e) { return e.panel == panel; });
    }

    bool OverlayRoot::RunPositioners(NodeArena& arena)
    {
        bool moved = false;
        for (PositionerEntry& e : m_positioners)
        {
            if (!e.panel.Valid() || !arena.TryResolve(e.panel))
            {
                continue;
            }
            AnchorMiss                miss{};
            const std::optional<Rect> anchorRect = ResolveAnchorRect(arena, e.anchor, miss);

            PositionerResult result;
            if (!anchorRect)
            {
                result.hidden = true;

                // A trigger that unmounted never comes back, so dismiss the surface (hiding would leave the popover registered until Escape);
                // a trigger merely scrolled out of view stays registered.
                if (miss == AnchorMiss::Gone)
                {
                    for (const PortalEntry& entry : m_entries)
                    {
                        if (entry.content == e.panel)
                        {
                            RequestDismiss(entry.id, DismissReason::Explicit);
                            break;
                        }
                    }
                }
            }
            else
            {
                const Rect& panelRect = arena.Resolve(e.panel).solved;
                PositionerInput in;
                in.anchor      = *anchorRect;
                in.surface     = m_surface;
                in.viewportPad = e.viewportPad;
                in.content     = {panelRect.width, panelRect.height};
                in.base        = e.base;
                in.offset      = e.offset;
                result = ComputePosition(in, e.middleware);

                LayoutStyle& style = arena.Resolve(e.panel).style;
                if (!NearlyEq(style.inset.left, result.rect.x) || !NearlyEq(style.inset.top, result.rect.y))
                {
                    style.inset.left = result.rect.x;
                    style.inset.top  = result.rect.y;
                    arena.Resolve(e.panel).MarkDirty(Dirty::Layout);
                    moved = true;
                }
            }
            if (e.onResult)
            {
                e.onResult(result);
            }
        }
        return moved;
    }

    Elem BuildOverlayLayer()
    {
        // hitThrough so an empty layer never eats clicks meant for content beneath; it still routes to portal content spliced into it.
        return Stack(StackCfg{.layout = {.width = Expand, .height = Expand}, .on = {.hitThrough = true}});
    }
} // namespace lumora
