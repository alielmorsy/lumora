#include "Runtime/Paint.h"

#include "Node/ControlState.h"
#include "Node/KindDescriptor.h"
#include "Node/Kind.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/ChildOrder.h"
#include "Widgets/Widget.h"

#include <lumora/Foundation/Rect.h>
#include <lumora/Paint/Canvas.h>

#include <vector>

// The emit pass: walks the tree once per frame, pre-order (a node draws its visual, then its children), via Widget::Paint; control-flow hosts and
// Component are transparent. Text goes through the injected TextMeasurer (a cache hit after measure); with no measurer, text draws nothing.

namespace lumora
{
    namespace
    {
        /// Visibility + clip flags, read kind-agnostically from whichever payload carries a ViewState; null for kinds without one.
        [[nodiscard]] const ViewState* ViewOf(NodeArena& arena, const LiveNode& n)
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

        /// Compositing opacity (1.0 for kinds with no StyleState), folded into a paint-space layer so the whole subtree fades together.
        [[nodiscard]] float OpacityOf(NodeArena& arena, const LiveNode& n)
        {
            switch (n.kind)
            {
            case LiveKind::Box:
            case LiveKind::Row:
            case LiveKind::Column:
            case LiveKind::Stack:
            case LiveKind::Cell:
                return arena.PayloadOf<ContainerPayload>(n.id).style.opacity;
            case LiveKind::Grid:
                return arena.PayloadOf<GridPayload>(n.id).style.opacity;
            case LiveKind::Scroll:
                return arena.PayloadOf<ScrollPayload>(n.id).style.opacity;
            case LiveKind::VirtualList:
                return arena.PayloadOf<VirtualListState>(n.id).style.opacity;
            case LiveKind::TextSelectionScope:
                return arena.PayloadOf<TextSelectionScopePayload>(n.id).style.opacity;
            default:
                return 1.f;
            }
        }

        /// Corner radius (0 for kinds with no StyleState), so a clipping node clips to the same rounded box it paints.
        [[nodiscard]] float RadiusOf(NodeArena& arena, const LiveNode& n)
        {
            switch (n.kind)
            {
            case LiveKind::Box:
            case LiveKind::Row:
            case LiveKind::Column:
            case LiveKind::Stack:
            case LiveKind::Cell:
                return arena.PayloadOf<ContainerPayload>(n.id).style.radius;
            case LiveKind::Grid:
                return arena.PayloadOf<GridPayload>(n.id).style.radius;
            case LiveKind::Scroll:
                return arena.PayloadOf<ScrollPayload>(n.id).style.radius;
            case LiveKind::VirtualList:
                return arena.PayloadOf<VirtualListState>(n.id).style.radius;
            case LiveKind::TextSelectionScope:
                return arena.PayloadOf<TextSelectionScopePayload>(n.id).style.radius;
            default:
                return 0.f;
            }
        }

        /// The clip box a node pushes: its bare solved rect, in the space of the active layer. The backend runs it through the live layer
        /// matrix, so adding `layerOffset` would double-count enclosing translates and slide the scissor off the content.
        [[nodiscard]] Rect ClipBox(const LiveNode& n)
        {
            return n.solved;
        }

        /// Whether a child may be pruned when its box misses the active clip window. Out-of-flow or transformed children draw elsewhere than
        /// their solved rect, so they always emit; otherwise window and child share every enclosing transform (solved-space vs solved-space).
        [[nodiscard]] bool Cullable(NodeArena& arena, const LiveNode& n)
        {
            if (n.style.position != Position::Flow)
            {
                return false;
            }
            return detail::IsIdentity(detail::NodeTransform(arena, n));
        }

        /// Retained-recording state threaded through the walk; null on the PaintReuse::Off path (nothing remembered).
        struct Reuse
        {
            DisplayList*       cur     = nullptr;
            const DisplayList* prev    = nullptr;
            bool               subtree = false;
        };

        void EmitChildren(PaintContext& ctx, const LiveNode& parent, Canvas& canvas, const Rect* cull,
                          Reuse* reuse, std::uint32_t prevBase, std::uint32_t prevBytes,
                          std::uint32_t curBase);

        void EmitNode(PaintContext& ctx, NodeId id, Canvas& canvas, const Rect* cull, Reuse* reuse,
                      std::uint32_t prevBase, std::uint32_t curBase);

        /// The node's own visual plus its children, unconditionally. `prevBase`/`prevBytes` are the node's range in the PREVIOUS stream and
        /// `curBase` its start in the current one; children resolve parent-relative spans against all three.
        void EmitBody(PaintContext& ctx, NodeId id, Canvas& canvas, const Rect* cull, Reuse* reuse,
                      std::uint32_t prevBase, std::uint32_t prevBytes, std::uint32_t curBase)
        {
            const LiveNode& n = ctx.arena->Resolve(id);

            if (IsTransparent(n.kind))
            {
                // host: descend, no visual of its own - forward the parent's cull window
                EmitChildren(ctx, n, canvas, cull, reuse, prevBase, prevBytes, curBase);
                return;
            }

            const ViewState* view = ViewOf(*ctx.arena, n);
            if (view && !view->visible)
            {
                if (ctx.counters)
                {
                    ++ctx.counters->emitHidden;
                }
                return;
            }

            // Compositing layer: a transform and/or opacity < 1 fold the node and its subtree into one layer (one PushLayer).
            // It opens before the node's own Paint because the transform moves the element itself; the solved rect is untouched.
            const TransformState* xf = detail::TransformOf(*ctx.arena, n);
            const svg::Affine xm = detail::NodeTransform(*ctx.arena, n);
            const bool hasXform = !detail::IsIdentity(xm);
            const float op = OpacityOf(*ctx.arena, n);
            const bool layer = hasXform || op < 1.f;
            if (layer)
            {
                if (ctx.counters)
                {
                    ++ctx.counters->emitLayers;
                }
                if (hasXform)
                {
                    const float m[9] = {xm.a, xm.b, 0.f, xm.c, xm.d, 0.f, xm.e, xm.f, 1.f};
                    canvas.PushLayer(op, m);
                }
                else
                {
                    canvas.PushLayer(op, nullptr);
                }
            }

            const Vec2 savedOffset = ctx.layerOffset;
            if (hasXform && xf)
            {
                ctx.layerOffset.x += xf->TotalX();
                ctx.layerOffset.y += xf->TotalY();
            }

            if (Widget* widget = KindRegistry::For(n.kind))
            {
                widget->Paint(ctx, id, canvas);
            }

            // Scroll: clip to the viewport, then translate the content by -offset. The clip stays under the parent transform; the layer shifts only the children.
            if (n.kind == LiveKind::Scroll)
            {
                ScrollPayload& sp = ctx.arena->PayloadOf<ScrollPayload>(id);
                const float xform[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, -sp.offsetX, -sp.offsetY, 1.f};

                // A moved offset re-bakes every clip scissor below, so the content cannot be republished from the previous stream this frame.
                // Only the cached path cares; the reuse flag is restored for the following siblings.
                const bool offsetMoved = !sp.paintedOffsetValid || sp.paintedOffsetX != sp.offsetX ||
                                         sp.paintedOffsetY != sp.offsetY;
                sp.paintedOffsetX     = sp.offsetX;
                sp.paintedOffsetY     = sp.offsetY;
                sp.paintedOffsetValid = true;
                const bool savedReuse = reuse != nullptr && reuse->subtree;
                if (offsetMoved && reuse != nullptr)
                {
                    reuse->subtree = false;
                }
                if (ctx.counters)
                {
                    ++ctx.counters->emitClips;
                    ++ctx.counters->emitLayers;
                }
                canvas.PushClip(ClipBox(n), RadiusOf(*ctx.arena, n));
                canvas.PushLayer(1.f, xform);
                ctx.layerOffset.x -= sp.offsetX;
                ctx.layerOffset.y -= sp.offsetY;
                // Children sit at un-scrolled solved rects and the layer shifts them by -offset, so a child is visible iff its box meets the viewport pushed by +offset.
                const Rect window{n.solved.x + sp.offsetX, n.solved.y + sp.offsetY, n.solved.width, n.solved.height};
                EmitChildren(ctx, n, canvas, &window, reuse, prevBase, prevBytes, curBase);
                ctx.layerOffset.x += sp.offsetX;
                ctx.layerOffset.y += sp.offsetY;
                if (reuse != nullptr)
                {
                    reuse->subtree = savedReuse;
                }
                canvas.PopLayer();
                canvas.PopClip();
                if (Widget* w = KindRegistry::For(n.kind))
                {
                    w->PaintOverlay(ctx, id, canvas); // scrollbar: viewport space, on top of the content
                }
            }
            else
            {
                const bool clip = view && view->clip;
                Rect shifted;
                Rect clipWindow;
                const Rect* childCull = cull; // non-clip node: forward the inherited window

                // A node's transform moves its subtree in paint space while children keep untransformed solved rects, so the window travels
                // through the inverse (solved-space vs solved-space), or a child the layer pulls into view is pruned.
                svg::Affine inv;
                if (hasXform && childCull != nullptr && xm.Invert(inv))
                {
                    shifted   = detail::MapRect(inv, *childCull);
                    childCull = &shifted;
                }
                if (clip)
                {
                    if (ctx.counters)
                    {
                        ++ctx.counters->emitClips;
                    }
                    canvas.PushClip(ClipBox(n), RadiusOf(*ctx.arena, n));
                    // A clipping node tightens the window to its own box, intersected with the inherited one so an outer scroll viewport still bounds the subtree.
                    // Uses `solved`, not `ClipBox`: the clip travels with the layer, so in the children's space it is the un-translated rect.
                    clipWindow = childCull ? childCull->Intersection(n.solved) : n.solved;
                    childCull = &clipWindow;
                }
                EmitChildren(ctx, n, canvas, childCull, reuse, prevBase, prevBytes, curBase);
                if (clip)
                {
                    canvas.PopClip();
                }
                // Chrome on top of this node's children in its own space (a VirtualList's scrollbar); inert for kinds that do not override it.
                if (Widget* w = KindRegistry::For(n.kind))
                {
                    w->PaintOverlay(ctx, id, canvas);
                }
            }

            if (layer)
            {
                canvas.PopLayer();
            }
            ctx.layerOffset = savedOffset;
        }

        void EmitNode(PaintContext& ctx, NodeId id, Canvas& canvas, const Rect* cull, Reuse* reuse,
                      const std::uint32_t prevBase, const std::uint32_t curBase)
        {
            if (!id.Valid())
            {
                return;
            }
            if (ctx.counters)
            {
                ++ctx.counters->emitVisited;
            }
            if (!reuse)
            {
                EmitBody(ctx, id, canvas, cull, nullptr, 0, 0, 0);
                return;
            }

            LiveNode& node = ctx.arena->Resolve(id);
            const std::uint32_t start = reuse->cur->Size();
            const PaintSpan recorded = node.paint;
            // Written before the reuse test: a republished subtree still moves in the stream, so its parent-relative offset changes even when its bytes do not.
            node.paint.first = start - curBase;

            if (reuse->subtree && reuse->prev != nullptr && recorded.valid &&
                !node.IsSubtreeDirty(Dirty::Repaint) &&
                reuse->cur->Append(*reuse->prev, prevBase, recorded.bytes))
            {
                if (ctx.counters)
                {
                    ++ctx.counters->emitReused;
                    ctx.counters->emitReusedBytes += recorded.bytes;
                }
                return;
            }

            // Cleared before the widget runs: a widget that dirties itself inside Paint (caret, VirtualList settling) must survive into the next frame.
            node.ClearDirty(Dirty::Repaint);
            node.ClearDescendantDirty(Dirty::Repaint);
            if (ctx.counters)
            {
                ++ctx.counters->emitRecorded;
            }
            EmitBody(ctx, id, canvas, cull, reuse, prevBase, recorded.valid ? recorded.bytes : 0, start);
            LiveNode& after = ctx.arena->Resolve(id);
            after.paint.bytes = reuse->cur->Size() - start;
            after.paint.valid = true;
        }

        void EmitChildren(PaintContext& ctx, const LiveNode& parent, Canvas& canvas, const Rect* cull,
                          Reuse* reuse, const std::uint32_t prevBase, const std::uint32_t prevBytes,
                          const std::uint32_t curBase)
        {
            // Prune a child whose box falls wholly outside the active clip window, skipping its whole subtree (the win on a long scrolled list).
            // Transparent children have no box; they re-forward the window to their real children in EmitNode.
            const auto visit = [&](NodeId c)
            {
                LiveNode& child = ctx.arena->Resolve(c);
                if (cull)
                {
                    if (!IsTransparent(child.kind) && Cullable(*ctx.arena, child) && !child.solved.Intersects(*cull))
                    {
                        if (ctx.counters)
                        {
                            ++ctx.counters->emitCulled;
                        }
                        // A culled child contributes no bytes, so its recorded range is stale: invalidate it, or scrolling it back would republish a sibling's bytes.
                        child.paint.valid = false;
                        return;
                    }
                }
                std::uint32_t childPrev = 0;
                if (reuse)
                {
                    // The child's range must lie wholly inside the range its parent occupied last frame; it does not when the parent's range shrank
                    // (hidden/culled ancestor, removed sibling). Invalidate instead: one re-record beats drawing another node's geometry.
                    const PaintSpan& span = child.paint;
                    if (span.valid &&
                        static_cast<std::uint64_t>(span.first) + span.bytes <= prevBytes)
                    {
                        childPrev = prevBase + span.first;
                    }
                    else
                    {
                        child.paint.valid = false;
                    }
                }
                EmitNode(ctx, c, canvas, cull, reuse, childPrev, curBase);
            };

            std::vector<NodeId> ordered; // empty (no alloc) on the common no-z fast path
            if (detail::ChildrenByZ(*ctx.arena, parent, ordered))
            {
                if (ctx.counters)
                {
                    ++ctx.counters->emitZSorts;
                }
                for (NodeId c : ordered)
                {
                    visit(c);
                }
                return;
            }
            for (NodeId c = parent.firstChild; c.Valid(); c = ctx.arena->Resolve(c).nextSibling)
            {
                visit(c);
            }
        }
    } // namespace

    void Paint(AppCtx& ctx, NodeId root, Canvas& canvas)
    {
        if (!root.Valid())
        {
            return;
        }
        PaintContext pctx{
            .arena = &ctx.arena,
            .fonts = ctx.measurer,
            .pointerPos = ctx.pointerPos,
            .contentScale = ctx.contentScale,
            .images = ctx.images,
            .counters = ctx.counters
        };
        detail::ScopedAccumMs paintTiming(ctx.counters ? &ctx.counters->paintMs : nullptr);
        EmitNode(pctx, root, canvas, nullptr, nullptr, 0, 0);
    }

    void Record(AppCtx& ctx, NodeId root, PaintCache& cache, const bool subtreeReuse)
    {
        if (!root.Valid())
        {
            return;
        }
        // Retire this frame's stream to `previous` and build into the older buffer; both keep their capacity for the app's lifetime.
        cache.previous.Swap(cache.current);
        cache.current.Clear();

        PaintContext& pctx = cache.scratch;
        pctx.arena        = &ctx.arena;
        pctx.fonts        = ctx.measurer;
        pctx.pointerPos   = ctx.pointerPos;
        pctx.layerOffset  = Vec2{};
        pctx.contentScale = ctx.contentScale;
        pctx.images       = ctx.images;
        pctx.counters     = ctx.counters;

        DisplayListCanvas recorder(cache.current);
        Reuse reuse{&cache.current, cache.primed ? &cache.previous : nullptr, subtreeReuse};
        {
            detail::ScopedAccumMs paintTiming(ctx.counters ? &ctx.counters->paintMs : nullptr);
            EmitNode(pctx, root, recorder, nullptr, &reuse, 0, 0);
        }
        cache.primed = true;
        if (ctx.counters)
        {
            ctx.counters->paintRecordedBytes = cache.current.Size();
        }
    }
} // namespace lumora
