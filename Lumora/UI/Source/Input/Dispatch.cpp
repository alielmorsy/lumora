#include "Input/Dispatch.h"

#include "Input/FocusTraversal.h"
#include "Input/Interaction.h"
#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/ControlState.h"
#include "Node/Payloads.h"
#include "Node/View.h"
#include "Runtime/ChildOrder.h"
#include "Runtime/DragHost.h"
#include "Runtime/OverlayRoot.h"
#include "Style/StyleEngine.h"
#include "Widgets/Widget.h"

#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Input/DragDrop.h>
#include <lumora/Input/Event.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace lumora
{
    namespace
    {
        /// Whether a non-Scroll node clips its subtree (a point outside its rect can't hit a descendant).
        [[nodiscard]] bool NodeClips(NodeArena& arena, const LiveNode& n)
        {
            switch (n.kind)
            {
                case LiveKind::Box:
                case LiveKind::Row:
                case LiveKind::Column:
                case LiveKind::Stack:
                case LiveKind::Cell:
                    return arena.PayloadOf<ContainerPayload>(n.id).view.clip;
                case LiveKind::Grid:
                    return arena.PayloadOf<GridPayload>(n.id).view.clip;
                // A viewport whose overscan rows lie outside it: paint clips them, so the hit-test must too or they swallow clicks meant for the sibling above.
                case LiveKind::VirtualList:
                    return arena.PayloadOf<VirtualListState>(n.id).view.clip;
                case LiveKind::Image:
                    return arena.PayloadOf<ImagePayload>(n.id).view.clip;
                case LiveKind::TextSelectionScope:
                    return arena.PayloadOf<TextSelectionScopePayload>(n.id).view.clip;
                default:
                    return false;
            }
        }

        [[nodiscard]] bool IsPointerEvent(EventType t) noexcept
        {
            return t == EventType::MouseDown || t == EventType::MouseUp || t == EventType::MouseMove ||
                   t == EventType::Scroll;
        }

        /// Maps a surface-space point into `target`'s local space by undoing ancestor transforms and applying ancestor Scroll offsets
        /// (same accumulation as HitChain), since the captured route never re-runs the hit-test.
        [[nodiscard]] Vec2 LocalPoint(NodeArena& arena, NodeId target, Vec2 surface)
        {
            std::vector<NodeId> chain;
            for (NodeId id = target; id.Valid();)
            {
                chain.push_back(id);
                const LiveNode* n = arena.TryResolve(id);
                if (!n)
                {
                    break;
                }
                id = n->parent;
            }

            Vec2 p = surface;
            for (std::size_t i = chain.size(); i-- > 0;) // root -> target
            {
                const LiveNode& n = arena.Resolve(chain[i]);
                if (svg::Affine inv; detail::NodeTransform(arena, n).Invert(inv))
                {
                    p = inv.Apply(p);
                }
                if (chain[i] != target && n.kind == LiveKind::Scroll) // offset applies to the child step
                {
                    const ScrollPayload& sp = arena.PayloadOf<ScrollPayload>(n.id);
                    p.x += sp.offsetX;
                    p.y += sp.offsetY;
                }
            }
            return p;
        }

        /// Delivers one event to a node in the given phase: Components via their stored thunk, other kinds via their Widget. A stale ref is skipped.
        void Invoke(const EventContext& base, const NodeRef& ref, RoutePhase phase, Event& ev)
        {
            ev.Phase = static_cast<EventPhase>(phase); // surface the pass to Component::OnEvent
            const NodeId id = ref.Resolve(*base.arena);
            if (!id.Valid())
            {
                return;
            }
            const LiveNode& n = base.arena->Resolve(id);
            if (n.kind == LiveKind::Component)
            {
                const ComponentPayload& cp = base.arena->PayloadOf<ComponentPayload>(id);
                if (cp.event)
                {
                    cp.event(cp.instance, ev);
                }
                return;
            }
            if (Widget* widget = KindRegistry::For(n.kind))
            {
                EventContext ctx = base;
                ctx.node     = id;
                ctx.phase    = phase;
                ctx.localPos = IsPointerEvent(ev.Type) ? LocalPoint(*base.arena, id, ev.Position) : ev.Position;
                widget->OnEvent(ctx, ev);
            }
        }

        /// capture (root->leaf) then target + bubble (leaf->root). `path` is ordered root->leaf.
        void Route(const EventContext& base, const HitPath& path, Event& ev)
        {
            for (std::size_t i = 0; i < path.size() && !ev.Consumed; ++i)
            {
                Invoke(base, path[i], RoutePhase::Capture, ev);
            }
            for (std::size_t i = path.size(); i-- > 0 && !ev.Consumed;)
            {
                Invoke(base, path[i], i + 1 == path.size() ? RoutePhase::Target : RoutePhase::Bubble, ev);
            }
        }

        /// Hit-tests a subtree, appending the interactive ancestry of the topmost target (leaf first). Children are walked in reverse paint
        /// order so the topmost wins. Returns true if this subtree owns the target.
        bool HitChain(NodeArena& arena, NodeId id, Vec2 point, HitPath& path, const EventContext& probe,
                      NodeId skip = NodeId{})
        {
            // `skip` prunes one subtree outright (the drag machine excludes the Drag layer: the ghost is always topmost, and `hitThrough`
            // would still test its children).
            if (!id.Valid() || id == skip)
            {
                return false;
            }
            const LiveNode& n = arena.Resolve(id);
            if (const InteractState* on = detail::InteractOf(arena, n); on && on->hitThroughSubtree)
            {
                return false;
            }
            // Hidden subtrees are not hit targets (like CSS visibility: hidden).
            if (const ViewState* view = ViewOf(arena, n); view && !view->visible)
            {
                return false;
            }

            if (IsTransparent(n.kind))
            {
                const auto tryChild = [&](NodeId c) -> bool
                {
                    if (HitChain(arena, c, point, path, probe, skip))
                    {
                        if (n.kind == LiveKind::Component)
                        {
                            path.push_back(NodeRef{arena, id});
                        }
                        return true;
                    }
                    return false;
                };
                std::vector<NodeId> ordered; // empty (no alloc) on the common no-z fast path
                if (detail::ChildrenByZ(arena, n, ordered))
                {
                    for (std::size_t i = ordered.size(); i-- > 0;)
                    {
                        if (tryChild(ordered[i])) return true;
                    }
                }
                else
                {
                    for (NodeId c = n.lastChild; c.Valid(); c = arena.Resolve(c).prevSibling)
                    {
                        if (tryChild(c)) return true;
                    }
                }
                return false;
            }

            // A pure-visual transform moves the node and its subtree, so map the query point into the node's pre-transform local space
            // (inverse of the paint layer); `solved` is the untransformed rect.
            Vec2 local = point;
            if (svg::Affine inv; detail::NodeTransform(arena, n).Invert(inv))
            {
                local = inv.Apply(local);
            }

            const bool inside = n.solved.Contains(local);
            const bool clips  = n.kind == LiveKind::Scroll || NodeClips(arena, n);
            if (clips && !inside)
            {
                return false;
            }

            Vec2 childPoint = local;
            if (n.kind == LiveKind::Scroll)
            {
                const ScrollPayload& sp = arena.PayloadOf<ScrollPayload>(id);
                childPoint.x += sp.offsetX;
                childPoint.y += sp.offsetY;
            }

            const std::size_t mark = path.size();
            const auto tryChild = [&](NodeId c) -> bool
            {
                if (HitChain(arena, c, childPoint, path, probe, skip))
                {
                    path.push_back(NodeRef{arena, id});
                    return true;
                }
                return false;
            };
            std::vector<NodeId> ordered; // empty (no alloc) on the common no-z fast path
            if (detail::ChildrenByZ(arena, n, ordered))
            {
                for (std::size_t i = ordered.size(); i-- > 0;)
                {
                    if (tryChild(ordered[i])) return true;
                }
            }
            else
            {
                for (NodeId c = n.lastChild; c.Valid(); c = arena.Resolve(c).prevSibling)
                {
                    if (tryChild(c)) return true;
                }
            }

            if (inside)
            {
                if (Widget* widget = KindRegistry::For(n.kind))
                {
                    EventContext ctx = probe;
                    ctx.node = id;
                    const HitTestBehavior beh = widget->HitTest(ctx, local);
                    if (beh != HitTestBehavior::Ignore)
                    {
                        path.push_back(NodeRef{arena, id});
                        return true;
                    }
                }
                else
                {
                    path.push_back(NodeRef{arena, id});
                    return true;
                }
            }

            path.resize(mark);
            return false;
        }

        /// Builds the root->leaf chain for a known node (captured or focused target) from parent links, keeping opaque nodes and Component ancestors.
        void BuildRootToLeaf(NodeArena& arena, NodeId leaf, HitPath& path)
        {
            path.clear();
            for (NodeId id = leaf; id.Valid();)
            {
                LiveNode* n = arena.TryResolve(id);
                if (!n)
                {
                    break;
                }
                if (!IsTransparent(n->kind) || n->kind == LiveKind::Component)
                {
                    path.push_back(NodeRef{arena, id});
                }
                id = n->parent;
            }
            std::reverse(path.begin(), path.end());
        }

        /// Deliver a synthesised enter/leave/focus event to one node (Target phase).
        void DeliverDerived(const EventContext& base, const NodeRef& ref, EventType type)
        {
            if (!ref.Resolve(*base.arena).Valid())
            {
                return;
            }
            Event ev{};
            ev.Type     = type;
            ev.Position = base.state->pointerPos;
            Invoke(base, ref, RoutePhase::Target, ev);
        }

        /// Delivers enter/leave along the whole hover chain (not just the deepest node, so a container with children still hovers),
        /// diffed against the previous chain. `path` is root->leaf.
        void UpdateHover(const EventContext& base, InteractionState& s, const HitPath& path)
        {
            const NodeRef leaf = path.empty() ? NodeRef{} : path.back();
            if (s.hovered == leaf)
            {
                return; // same leaf -> same chain, nothing entered or left
            }

            const auto contains = [](const HitPath& p, const NodeRef& ref)
            {
                for (const NodeRef& r : p)
                {
                    if (r == ref)
                    {
                        return true;
                    }
                }
                return false;
            };

            // Leave nodes that dropped out of the chain (deepest first), then enter new ones (shallowest first); shared ancestors are untouched.
            for (std::size_t i = s.hoveredPath.size(); i-- > 0;)
            {
                if (!contains(path, s.hoveredPath[i]))
                {
                    DeliverDerived(base, s.hoveredPath[i], EventType::MouseLeave);
                }
            }
            for (const NodeRef& ref : path)
            {
                if (!contains(s.hoveredPath, ref))
                {
                    DeliverDerived(base, ref, EventType::MouseEnter);
                }
            }

            s.hoveredPath = path;
            s.hovered     = leaf;
        }

        [[nodiscard]] NodeRef DeepestFocusable(const EventContext& base, const HitPath& path)
        {
            for (std::size_t i = path.size(); i-- > 0;)
            {
                const NodeId id = path[i].Resolve(*base.arena);
                if (!id.Valid())
                {
                    continue;
                }
                const LiveNode& n = base.arena->Resolve(id);
                if (n.kind == LiveKind::Component)
                {
                    continue;
                }
                if (Widget* widget = KindRegistry::For(n.kind))
                {
                    EventContext ctx = base;
                    ctx.node = id;
                    if (widget->WantsFocus(ctx))
                    {
                        return path[i];
                    }
                }
            }
            return {};
        }

        void SetFocus(const EventContext& base, InteractionState& s, const NodeRef& next, bool viaKeyboard)
        {
            NodeRef target = next;
            if (const NodeId targetId = target.Resolve(*base.arena); targetId.Valid())
            {
                if (detail::IsFocusScope(*base.arena, targetId))
                {
                    InteractState* on = detail::InteractOf(*base.arena, base.arena->Resolve(targetId));
                    if (!on || !IsFocusable(on->focusPolicy, on->disabled))
                    {
                        NodeId remembered = detail::GetScopeLastFocused(*base.arena, targetId);
                        if (remembered.Valid())
                        {
                            target = NodeRef{*base.arena, remembered};
                        }
                        else
                        {
                            std::vector<NodeRef> ring;
                            FocusTraversal::CollectRing(base, targetId, ring);
                            if (!ring.empty())
                            {
                                target = ring.front();
                            }
                        }
                    }
                }
            }

            if (s.focused == target)
            {
                s.focusVisible = viaKeyboard; // a re-focus (Tab onto the only/same stop) still shows the ring
                return;
            }
            if (const NodeId old = s.focused.Resolve(*base.arena); old.Valid())
            {
                base.arena->Resolve(old).MarkDirty(Dirty::Repaint);
                WritePseudo(s, old, style::Pseudo::Focus, false);
            }
            DeliverDerived(base, s.focused, EventType::FocusLost);
            s.focused      = target;
            s.focusVisible = viaKeyboard;
            if (const NodeId now = s.focused.Resolve(*base.arena); now.Valid())
            {
                base.arena->Resolve(now).MarkDirty(Dirty::Repaint);
                WritePseudo(s, now, style::Pseudo::Focus, true);
                detail::RecordScopeFocus(*base.arena, now);
            }
            DeliverDerived(base, s.focused, EventType::FocusGained);
        }

        /// The innermost node in a root->leaf chain that accepts `payload`; a node that refuses does not block the ones behind it.
        [[nodiscard]] NodeRef FindDropTarget(NodeArena& arena, const HitPath& path, const DragPayload& payload)
        {
            for (std::size_t i = path.size(); i-- > 0;)
            {
                const NodeId id = path[i].Resolve(arena);
                if (!id.Valid())
                {
                    continue;
                }
                InteractState* on = detail::InteractOf(arena, arena.Resolve(id));
                if (on && !on->disabled && on->dropAccepts && on->dropAccepts(payload))
                {
                    return path[i];
                }
            }
            return {};
        }

        struct FoundSource
        {
            NodeRef     ref{};
            DragPayload payload{};
        };

        /// The deepest drag source in a chain that hands over a payload. `onDragStart` is asked on the press, not when the drag arms,
        /// so the payload is what the user saw.
        [[nodiscard]] FoundSource FindDragSource(NodeArena& arena, const HitPath& path)
        {
            for (std::size_t i = path.size(); i-- > 0;)
            {
                const NodeId id = path[i].Resolve(arena);
                if (!id.Valid())
                {
                    continue;
                }
                InteractState* on = detail::InteractOf(arena, arena.Resolve(id));
                if (!on || on->disabled || !on->dragStart)
                {
                    continue;
                }
                if (std::optional<DragPayload> p = on->dragStart(); p.has_value() && p->Valid())
                {
                    return FoundSource{path[i], std::move(*p)};
                }
            }
            return {};
        }

        /// Undoes the press the source latched before the drag took over: the release is consumed by the drag, so nothing else clears
        /// the source's pressed state or its pointer capture.
        void ReleaseSourcePress(NodeArena& arena, InteractionState& s, const NodeRef& source)
        {
            // Only the source's own capture: the slot is global, and releasing it blind steals it from whoever holds the gesture (e.g. an ancestor scrollbar mid-drag).
            if (s.captured == source)
            {
                s.ReleaseCapture();
            }
            const NodeId id = source.Resolve(arena);
            if (!id.Valid())
            {
                return;
            }
            if (InteractState* on = detail::InteractOf(arena, arena.Resolve(id)))
            {
                on->pressed = false;
            }
            WritePseudo(s, id, style::Pseudo::Active, false);
            arena.Resolve(id).MarkDirty(Dirty::Repaint);
        }

        /// One pointer event while a drag is armed. Returns true when the drag owned it (every pointer event: an armed drag is modal).
        bool DispatchArmedDrag(const EventContext& base, InteractionState& s, NodeId root, Event& ev)
        {
            DragHost&    drag  = *s.drag;
            NodeArena&   arena = *base.arena;
            const NodeId ghost = s.overlays ? s.overlays->LayerNode(Layer::Drag) : NodeId{};

            switch (ev.Type)
            {
                case EventType::MouseMove:
                    drag.SetPointer(ev.Position);
                    s.hitScratch.clear();
                    HitChain(arena, root, ev.Position, s.hitScratch, base, ghost);
                    std::reverse(s.hitScratch.begin(), s.hitScratch.end());
                    drag.SetTarget(arena, s, FindDropTarget(arena, s.hitScratch, drag.Payload()));
                    ev.Consume();
                    return true;
                case EventType::MouseUp:
                    drag.Drop(arena, s, MakePointerInfo(ev, s));
                    ev.Consume();
                    return true;
                case EventType::MouseDown:
                case EventType::Scroll:
                    // A second button or wheel notch mid-drag is not a gesture this understands; cancelling cannot leave a half-applied drop behind.
                    drag.Clear(arena, s);
                    ev.Consume();
                    return true;
                default:
                    return false;
            }
        }

        void DispatchPointer(const EventContext& base, InteractionState& s, NodeId root, Event& ev)
        {
            s.pointerDelta = ev.Position - s.pointerPos;
            s.pointerPos   = ev.Position;

            if (s.drag)
            {
                // A pending drag arms here, not on the press, so a press on a draggable row stays a click until the pointer travels.
                if (s.drag->Pending())
                {
                    if (ev.Type == EventType::MouseMove && s.drag->PassedThreshold(ev.Position))
                    {
                        const NodeRef source = s.drag->Source();
                        s.drag->Arm();
                        ReleaseSourcePress(*base.arena, s, source);
                    }
                    else if (ev.Type == EventType::MouseUp)
                    {
                        // Never armed: this was a click. Clear and fall through so it routes normally.
                        s.drag->Clear(*base.arena, s);
                    }
                }
                if (s.drag->Armed() && DispatchArmedDrag(base, s, root, ev))
                {
                    return;
                }
            }

            if (const NodeId captured = s.captured.Resolve(*base.arena); captured.Valid())
            {
                BuildRootToLeaf(*base.arena, captured, s.hitScratch);
                Route(base, s.hitScratch, ev);
                return;
            }

            s.hitScratch.clear();
            HitChain(*base.arena, root, ev.Position, s.hitScratch, base);
            std::reverse(s.hitScratch.begin(), s.hitScratch.end());

            if (ev.Type == EventType::MouseMove)
            {
                UpdateHover(base, s, s.hitScratch);
            }
            if (ev.Type == EventType::MouseDown)
            {
                SetFocus(base, s, DeepestFocusable(base, s.hitScratch), /*viaKeyboard=*/false);
                if (s.overlays)
                {
                    // A press that did not land inside any open overlay light-dismisses the opted-in ones.
                    const NodeId leaf = s.hitScratch.empty() ? NodeId{} : s.hitScratch.back().Resolve(*base.arena);
                    s.overlays->LightDismissOutside(*base.arena, leaf);
                }
            }

            Route(base, s.hitScratch, ev);

            // A press may also begin a drag, after routing, so the source's own onPress/onClick still runs. Consumption is not consulted:
            // the node that consumes its press is also the draggable one.
            if (ev.Type == EventType::MouseDown && ev.Button == PointerButton::Left && s.drag &&
                s.drag->CurrentPhase() == DragHost::Phase::Idle)
            {
                if (FoundSource found = FindDragSource(*base.arena, s.hitScratch); found.ref.id.Valid())
                {
                    // Unless someone else took the pointer on this press: an armed drag is modal and would starve that node of the moves and release
                    // its gesture needs (e.g. an overlay scrollbar over draggable tiles).
                    const NodeRef holder = s.captured;
                    if (!holder.Resolve(*base.arena).Valid() || holder == found.ref)
                    {
                        s.drag->Press(found.ref, std::move(found.payload), ev.Position);
                    }
                }
            }
        }

        void DispatchKey(const EventContext& base, InteractionState& s, NodeId root, Event& ev)
        {
            // The focused widget gets first refusal (it may consume Tab); an unconsumed Tab runs focus navigation.
            const bool isTab = ev.Type == EventType::KeyDown && ev.KeyCode == Key::Tab;
            if (const NodeId focus = s.focused.Resolve(*base.arena); focus.Valid())
            {
                BuildRootToLeaf(*base.arena, focus, s.keyScratch);
                Route(base, s.keyScratch, ev);
            }
            if (isTab && !ev.Consumed)
            {
                const FocusDir dir = Has(ev.Mods, KeyMods::Shift) ? FocusDir::Backward : FocusDir::Forward;
                FocusTraversal::Move(base, s, root, dir);
                ev.Consume();
            }
        }
    } // namespace

    void DispatchEvent(NodeArena& arena, NodeId root, InteractionState& state, Event& ev)
    {
        if (!root.Valid())
        {
            return;
        }

        EventContext base{};
        base.arena = &arena;
        base.state = &state;
        base.fonts = state.fonts;
        base.clip  = state.clip;

        switch (ev.Type)
        {
            case EventType::MouseDown:
            case EventType::MouseUp:
            case EventType::MouseMove:
            case EventType::Scroll:
                DispatchPointer(base, state, root, ev);
                break;
            case EventType::KeyDown:
            case EventType::KeyUp:
            case EventType::TextInput:
                DispatchKey(base, state, root, ev);
                break;
            default:
                break;
        }
    }

    void WritePseudo(InteractionState& state, NodeId node, style::Pseudo p, bool on)
    {
        if (state.style) state.style->SetPseudo(node, p, on);
    }

    void RequestFocus(NodeArena& arena, InteractionState& state, const NodeRef& next, bool viaKeyboard)
    {
        EventContext base{};
        base.arena = &arena;
        base.state = &state;
        base.fonts = state.fonts;
        base.clip  = state.clip;
        SetFocus(base, state, next, viaKeyboard);
    }

    bool DispatchDropExternal(NodeArena& arena, NodeId root, InteractionState& state,
                              const DragPayload& payload, Vec2 logicalPos)
    {
        if (!root.Valid())
        {
            return false;
        }

        EventContext base{};
        base.arena = &arena;
        base.state = &state;
        base.fonts = state.fonts;
        base.clip  = state.clip;

        state.hitScratch.clear();
        const NodeId ghost = state.overlays ? state.overlays->LayerNode(Layer::Drag) : NodeId{};
        HitChain(arena, root, logicalPos, state.hitScratch, base, ghost);
        std::reverse(state.hitScratch.begin(), state.hitScratch.end());

        const NodeRef target = FindDropTarget(arena, state.hitScratch, payload);
        const NodeId  targetId = target.Resolve(arena);
        if (!targetId.Valid())
        {
            return false;
        }

        const InteractState* on = detail::InteractOf(arena, arena.Resolve(targetId));
        const std::function<void(const DragPayload&, PointerInfo)> drop = on ? on->onDrop : nullptr;
        if (!drop)
        {
            return false;
        }

        drop(payload, PointerInfo{
                          .position  = logicalPos,
                          .delta     = {},
                          .button    = static_cast<int>(PointerButton::Left),
                          .pressed   = false,
                          .modifiers = KeyMods::None,
                      });
        return true;
    }
} // namespace lumora
