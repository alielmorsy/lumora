#pragma once

#include <lumora/Foundation/NodeId.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Widgets/Props.h>

#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"

#include <concepts>
#include <optional>
#include <utility>

namespace lumora
{
    class Owner;

    namespace detail
    {
        class Node;
    }

    /// Walks `blueprint` depth-first under `owner`, allocating live headers/payloads and binding reactive props; returns the root id.
    /// Requires an ambient ReactiveGraph (GraphScope) with `owner` governing the new subtree.
    NodeId Instantiate(AppCtx& ctx, detail::Node& blueprint, NodeId parent, Owner& owner);

    void AppendChild(NodeArena& arena, NodeId parentId, NodeId childId);

    /// Binds one reactive prop onto a live field: constant props write once at mount; dynamic props get an owner-scoped effect
    /// that re-writes the field and raises `flag` (patching, never re-instantiating). `apply` must be idempotent.
    /// A re-run yielding the value last applied is dropped: a getter prop has no equality gate of its own (Signal/Computed do).
    template <typename T, typename Apply>
    void BindProp(NodeArena& arena, NodeId id, const Reactive<T>& prop, Apply apply, Dirty flag)
    {
        if (prop.IsStatic())
        {
            apply(arena, id, prop.Get());
            return;
        }
        NodeArena* arenaPtr = &arena;
        Effect([arenaPtr, id, prop, apply = std::move(apply), flag, last = std::optional<T>{}]() mutable
        {
            const T& value = prop.Get();
            if constexpr (std::equality_comparable<T>)
            {
                if (last && *last == value)
                {
                    return;
                }
                last = value;
            }
            apply(*arenaPtr, id, value);
            arenaPtr->Resolve(id).MarkDirty(flag);
        });
    }

    // Per-group bind helpers shared by every widget builder; `member` points at the matching live-state struct in the node's payload.

    /// Resolves Align::Auto by container kind (Row centres, Column stretches, else start); explicit values pass through,
    /// so the layout solver only sees concrete alignments.
    [[nodiscard]] inline Align ResolveCrossAlign(LiveKind kind, Align cross)
    {
        if (cross != Align::Auto)
        {
            return cross;
        }
        switch (kind)
        {
        case LiveKind::Row: return Align::Center;
        case LiveKind::Column: return Align::Stretch;
        case LiveKind::Cell: return Align::Stretch;
        // A row fills the viewport's cross axis so the whole row (not just the text) is a hit target.
        case LiveKind::VirtualList: return Align::Stretch;
        default: return Align::Start;
        }
    }

    /// Layout props resolve onto the header LayoutStyle (payload-independent). Only `gap` is reactive.
    inline void BindLayoutProps(NodeArena& arena, NodeId id, const LayoutProps& props)
    {
        LiveNode& node = arena.Resolve(id);
        LayoutStyle& style = node.style;
        style.padding = props.padding;
        style.margin = props.margin;
        style.main = props.main;
        if (props.direction)
        {
            style.axis = *props.direction;
        }
        // Cross default follows the effective axis: with a direction override, Row maps to Center, else Stretch.
        const LiveKind crossKind = props.direction
                                       ? (*props.direction == FlexAxis::Row ? LiveKind::Row : LiveKind::Column)
                                       : node.kind;
        style.cross = ResolveCrossAlign(crossKind, props.cross);
        style.wrap = props.wrap;
        style.display = props.display;
        style.alignContent = props.alignContent;
        style.minWidth = props.minWidth;
        style.minHeight = props.minHeight;
        style.maxWidth = props.maxWidth;
        style.maxHeight = props.maxHeight;
        style.flexWeight = props.flexWeight;
        style.flexShrink = props.flexShrink;
        style.flexBasis = props.flexBasis;
        style.alignSelf = props.alignSelf;
        style.order = props.order;
        BindProp(arena, id, props.width,
                 [](NodeArena& a, NodeId n, const SizeHint& v) { a.Resolve(n).style.width = v; },
                 Dirty::Layout);
        BindProp(arena, id, props.height,
                 [](NodeArena& a, NodeId n, const SizeHint& v) { a.Resolve(n).style.height = v; },
                 Dirty::Layout);
        BindProp(arena, id, props.gap,
                 [](NodeArena& a, NodeId n, const float& v) { a.Resolve(n).style.gap = v; },
                 Dirty::Layout);
    }

    template <typename Payload>
    void BindStyleProps(NodeArena& arena, NodeId id, const StyleProps& props, StyleState Payload::* member)
    {
        (arena.PayloadOf<Payload>(id).*member).border = props.border;
        BindProp(arena, id, props.fill,
                 [member](NodeArena& a, NodeId n, const Color& v) { (a.PayloadOf<Payload>(n).*member).fill = v; },
                 Dirty::Style);
        BindProp(arena, id, props.hoverFill,
                 [member](NodeArena& a, NodeId n, const Color& v) { (a.PayloadOf<Payload>(n).*member).hoverFill = v; },
                 Dirty::Style);
        BindProp(arena, id, props.radius,
                 [member](NodeArena& a, NodeId n, const float& v) { (a.PayloadOf<Payload>(n).*member).radius = v; },
                 Dirty::Style);
        BindProp(arena, id, props.opacity,
                 [member](NodeArena& a, NodeId n, const float& v) { (a.PayloadOf<Payload>(n).*member).opacity = v; },
                 Dirty::Style);
    }

    template <typename Payload>
    void BindViewProps(NodeArena& arena, NodeId id, const ViewProps& props, ViewState Payload::* member)
    {
        (arena.PayloadOf<Payload>(id).*member).clip = props.clip;
        BindProp(arena, id, props.visible,
                 [member](NodeArena& a, NodeId n, const bool& v) { (a.PayloadOf<Payload>(n).*member).visible = v; },
                 Dirty::Layout);
    }

    /// Transform x/y are pure-visual: they patch the payload TransformState and raise only Repaint, never the layout solver.
    template <typename Payload>
    void BindTransformProps(NodeArena& arena, NodeId id, const TransformProps& props,
                            TransformState Payload::* member)
    {
        BindProp(arena, id, props.x,
                 [member](NodeArena& a, NodeId n, const float& v) { (a.PayloadOf<Payload>(n).*member).x = v; },
                 Dirty::Repaint);
        BindProp(arena, id, props.y,
                 [member](NodeArena& a, NodeId n, const float& v) { (a.PayloadOf<Payload>(n).*member).y = v; },
                 Dirty::Repaint);
    }

    /// Pure-visual paint offset: patches payload TransformState::paintOffset and raises only Repaint.
    template <typename Payload>
    void BindPaintOffset(NodeArena& arena, NodeId id, const Reactive<Vec2>& offset,
                         TransformState Payload::* member)
    {
        BindProp(arena, id, offset,
                 [member](NodeArena& a, NodeId n, const Vec2& v) { (a.PayloadOf<Payload>(n).*member).paintOffset = v; },
                 Dirty::Repaint);
    }

    /// Position props resolve onto the header LayoutStyle. `.offset` is shorthand for inset.{left,top} and only fills an edge the
    /// explicit inset left auto. A reactive edge binds only when the author set it (a static zero stays auto, distinct from an explicit 0px pin).
    inline void BindPositionProps(NodeArena& arena, NodeId id, const PositionProps& props)
    {
        LiveNode& node = arena.Resolve(id);
        LayoutStyle& style = node.style;
        style.position = props.position;
        style.z = props.z;
        style.inset = props.inset;
        if (Inset::IsAuto(style.inset.left) && props.offset.x != 0.f) style.inset.left = props.offset.x;
        if (Inset::IsAuto(style.inset.top) && props.offset.y != 0.f) style.inset.top = props.offset.y;

        const auto edgeSet = [](const Reactive<float>& r) { return !r.IsStatic() || r.Get() != 0.f; };
        if (edgeSet(props.insetLeft))
            BindProp(arena, id, props.insetLeft,
                     [](NodeArena& a, NodeId n, const float& v) { a.Resolve(n).style.inset.left = v; },
                     Dirty::Layout);
        if (edgeSet(props.insetTop))
            BindProp(arena, id, props.insetTop,
                     [](NodeArena& a, NodeId n, const float& v) { a.Resolve(n).style.inset.top = v; },
                     Dirty::Layout);
        if (edgeSet(props.insetRight))
            BindProp(arena, id, props.insetRight,
                     [](NodeArena& a, NodeId n, const float& v) { a.Resolve(n).style.inset.right = v; },
                     Dirty::Layout);
        if (edgeSet(props.insetBottom))
            BindProp(arena, id, props.insetBottom,
                     [](NodeArena& a, NodeId n, const float& v) { a.Resolve(n).style.inset.bottom = v; },
                     Dirty::Layout);
    }

    /// Copies the interaction surface onto the node's InteractState and registers its one-shot autofocus (autofocus is an app-level request, hence AppCtx).
    template <typename Payload>
    void BindInteractProps(AppCtx& ctx, NodeId id, const InteractProps& props, InteractState Payload::* member)
    {
        NodeArena&     arena = ctx.arena;
        InteractState& state = arena.PayloadOf<Payload>(id).*member;
        state.onClick = props.onClick;
        state.onDoubleClick = props.onDoubleClick;
        state.onPress = props.onPress;
        state.onRelease = props.onRelease;
        state.onMove = props.onMove;
        state.onScroll = props.onScroll;
        state.onEnter = props.onEnter;
        state.onLeave = props.onLeave;
        state.contextMenu = props.onContextMenu;
        state.dragStart = props.onDragStart;
        state.dropAccepts = props.dropAccepts;
        state.onDrop = props.onDrop;
        state.onDragOver = props.onDragOver;
        state.onFocus = props.onFocus;
        state.onBlur = props.onBlur;
        state.onKeyDown = props.onKeyDown;
        state.onKeyUp = props.onKeyUp;
        state.onTextInput = props.onTextInput;
        state.middleButton = props.middleButton;
        state.rightButton = props.rightButton;
        state.hitThrough = props.hitThrough;
        state.hitThroughSubtree = props.hitThroughSubtree;
        state.nav = props.nav;
        state.focusPolicy = props.focusPolicy;
        state.tabIndex = props.tabIndex;
        if (props.autofocus && props.focusPolicy != FocusPolicy::None)
        {
            ctx.pendingAutofocus = id;
            ctx.pendingAutofocusList.push_back(id);
        }
        if (props.ref)
        {
            props.ref->id        = id;
            props.ref->gen       = arena.GenerationOf(id);
            props.ref->isPointer = false;
            arena.TrackAnchor(props.ref); // mirror this node's solved rect into the anchor each frame
        }
        BindProp(arena, id, props.disabled,
                 [member](NodeArena& a, NodeId n, const bool& v) { (a.PayloadOf<Payload>(n).*member).disabled = v; },
                 Dirty::Style);
    }
} // namespace lumora
