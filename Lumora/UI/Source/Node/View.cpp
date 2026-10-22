#include "Node/View.h"

#include "Node/ControlState.h"
#include "Node/Kind.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"

namespace lumora
{
    const ViewState* ViewOf(NodeArena& arena, const LiveNode& n)
    {
        switch (n.kind)
        {
            case LiveKind::Box:
            case LiveKind::Row:
            case LiveKind::Column:
            case LiveKind::Stack:
            case LiveKind::Cell:
                return &arena.PayloadOf<ContainerPayload>(n.id).view;
            case LiveKind::Grid:
                return &arena.PayloadOf<GridPayload>(n.id).view;
            case LiveKind::Scroll:
                return &arena.PayloadOf<ScrollPayload>(n.id).view;
            case LiveKind::VirtualList:
                return &arena.PayloadOf<VirtualListState>(n.id).view;
            case LiveKind::Image:
                return &arena.PayloadOf<ImagePayload>(n.id).view;
            case LiveKind::TextSelectionScope:
                return &arena.PayloadOf<TextSelectionScopePayload>(n.id).view;
            default:
                return nullptr;
        }
    }

    bool Visible(NodeArena& arena, const LiveNode& n)
    {
        const ViewState* v = ViewOf(arena, n);
        return !v || v->visible;
    }
} // namespace lumora
