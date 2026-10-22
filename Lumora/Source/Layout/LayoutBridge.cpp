#include "Layout/LayoutBridge.h"

#include "Runtime/AppCtx.h"

namespace lumora {

void Layout(AppCtx& ctx, NodeId root, float surfaceW, float surfaceH) {
    if (ctx.layout) {
        ctx.layout->Run(ctx.arena, root, surfaceW, surfaceH, ctx.measurer, ctx.images, ctx.counters);
    }
}

void MarkStructureDirty(NodeArena& arena, NodeId controlNode) {
    NodeId ancestor = arena.Resolve(controlNode).parent;
    while (ancestor.Valid() && IsTransparent(arena.Resolve(ancestor).kind)) {
        ancestor = arena.Resolve(ancestor).parent;
    }
    if (ancestor.Valid()) {
        arena.Resolve(ancestor).MarkDirty(Dirty::Layout | Dirty::Repaint);
    }
    // The control node is transparent (no solver node) but owns a recorded byte range whose child list just changed; without
    // this its span would be reused verbatim and the splice would never reach the screen.
    arena.Resolve(controlNode).MarkDirty(Dirty::Repaint);
}

} // namespace lumora
