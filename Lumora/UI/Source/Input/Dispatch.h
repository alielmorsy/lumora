#pragma once

#include <lumora/Foundation/NodeId.h>
#include <lumora/Foundation/Vec2.h>

namespace lumora
{
    struct DragPayload;
    struct Event;
    struct InteractionState;
    struct NodeRef;
    class NodeArena;

    /// Routes one event through the live tree at `root`: hit-test to one root->leaf chain, then capture, target, bubble. `state` carries
    /// hover/focus/capture across calls; `ev` is mutated (Consumed stops the route). Run under the ambient ReactiveGraph with structural edits deferred.
    void DispatchEvent(NodeArena& arena, NodeId root, InteractionState& state, Event& ev);

    /// Moves keyboard focus to `next` (empty clears), firing FocusLost/FocusGained and marking repaint. `viaKeyboard` (Tab/programmatic)
    /// is what makes the focus-visible ring show.
    void RequestFocus(NodeArena& arena, InteractionState& state, const NodeRef& next, bool viaKeyboard);

    /// Delivers an external drop (e.g. OS files) at `logicalPos` in surface space to the innermost target accepting `payload`.
    /// Returns true if a target accepted it.
    bool DispatchDropExternal(NodeArena& arena, NodeId root, InteractionState& state,
                              const DragPayload& payload, Vec2 logicalPos);
} // namespace lumora
