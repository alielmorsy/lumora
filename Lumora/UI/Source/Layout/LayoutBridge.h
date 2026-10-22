#pragma once

#include <lumora/Foundation/NodeId.h>

#include "Layout/GridSolve.h"
#include "Layout/LayoutBackend.h"
#include "Layout/Measure.h"
#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/TreeDump.h"
#include "Widgets/Widget.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace lumora
{
    struct AppCtx;
    class ImageProvider;

    /// Backend-agnostic layout orchestration: dirty-gated re-style, child reconciliation, surface-pin gate, idle early-out and
    /// absolute-rect readback. Speaks only LayoutBackend + LayoutStyle + LayoutHandle; instantiated once with MasharifBackend.
    template <LayoutBackend B>
    class LayoutDriver
    {
    public:
        explicit LayoutDriver(B& backend) noexcept : m_backend(backend)
        {
        }

        /// One incremental pass: restyle dirty nodes, re-solve, write each node's absolute rect.
        /// A frame with no dirty node and an unchanged surface touches the solver not at all.
        void Run(NodeArena& arena, NodeId root, float surfaceW, float surfaceH, const MeasureContext& measure);

    private:
        /// Frame counter block taken from the MeasureContext at the top of Run, so passes without it (ReadBack, PlaceGrids) can record what they walked.
        FrameCounters* m_counters = nullptr;

        void SolvePass(LayoutHandle root, float surfaceW, float surfaceH);
        bool Refresh(NodeArena& arena, NodeId id, const MeasureContext& measure);
        bool Remeasure(NodeArena& arena, NodeId id, bool parentDefinite, const MeasureContext& measure);
        bool PlaceGrids(NodeArena& arena, NodeId id);
        bool PlaceOneGrid(NodeArena& arena, NodeId gridId);
        void ReadBack(NodeArena& arena, NodeId id);
        void GatherChildren(NodeArena& arena, NodeId id, std::vector<LayoutHandle>& out) const;
        void GatherChildNodes(NodeArena& arena, NodeId id, std::vector<NodeId>& out) const;
        [[nodiscard]] static NodeId EffectiveParent(NodeArena& arena, const LiveNode& node);
        [[nodiscard]] static NodeId OpaqueRoot(NodeArena& arena, NodeId root);

        B& m_backend;
        float m_appliedW = -1.f;
        float m_appliedH = -1.f;
    };

    template <LayoutBackend B>
    void LayoutDriver<B>::Run(NodeArena& arena, NodeId root, float surfaceW, float surfaceH,
                              const MeasureContext& measure)
    {
        m_counters = measure.counters;
        if (m_counters)
        {
            ++m_counters->layoutRuns;
        }
        if (!root.Valid())
        {
            return;
        }

        // The mounted root is usually transparent (a Component or control-flow host owns no solver node): descend to the first
        // opaque descendant to pin to the surface, while Refresh and ReadBack still start from the original root.
        const NodeId opaqueRoot = OpaqueRoot(arena, root);
        if (!opaqueRoot.Valid())
        {
            return; // nothing opaque to lay out yet
        }
        LiveNode& rootNode = arena.Resolve(opaqueRoot);

        const bool rootRestyled = rootNode.IsDirty(Dirty::Layout); // ApplyStyle drops the surface pin
        const bool anyWork = Refresh(arena, root, measure);

        const bool surfaceChanged = surfaceW != m_appliedW || surfaceH != m_appliedH;
        if (!anyWork && !surfaceChanged)
        {
            if (m_counters)
            {
                ++m_counters->layoutIdleBails;
            }
            return;  // idle frame
        }

        if (surfaceChanged || rootRestyled)
        {
            m_backend.PinSurface(rootNode.layout, surfaceW, surfaceH);
            m_appliedW = surfaceW;
            m_appliedH = surfaceH;
        }

        SolvePass(rootNode.layout, surfaceW, surfaceH);
        ReadBack(arena, root);

        // Grid placement: tracks are computed in Lumora from each grid's solved content box and children re-pinned as absolute cells, then re-solved.
        // Iterated top-down to a fixpoint (PlaceGrids returns false when no box changed); the cap is only a backstop.
        for (int pass = 0; pass < 8 && PlaceGrids(arena, root); ++pass)
        {
            SolvePass(rootNode.layout, surfaceW, surfaceH);
            ReadBack(arena, root);
        }

        // Wrap-aware re-measure: the first solve measured content at MaxContent single-line, so wrappable nodes (Text) can overflow their
        // parent's allotment. Constrain them to it and re-measure (wrap on gives a taller box, off keeps one clipped line), then solve again.
        // Runs after grid placement so Text in a cell wraps to the cell width; one pass, exact for a single level of constraint.
        if (Remeasure(arena, root, true, measure)) // the surface above the root is a definite width
        {
            SolvePass(rootNode.layout, surfaceW, surfaceH);
            ReadBack(arena, root);
        }

#if !defined(NDEBUG) && 0
        if (anyWork)
        {
            LogLiveTree(arena, root);
        }
#endif
    }

    template <LayoutBackend B>
    void LayoutDriver<B>::SolvePass(LayoutHandle root, float surfaceW, float surfaceH)
    {
        if (m_counters)
        {
            ++m_counters->solveCalls;
        }
        detail::ScopedAccumMs solveTiming(m_counters ? &m_counters->measureMs : nullptr);
        m_backend.Solve(root, surfaceW, surfaceH);
    }

    template <LayoutBackend B>
    bool LayoutDriver<B>::Refresh(NodeArena& arena, NodeId id, const MeasureContext& measure)
    {
        if (m_counters)
        {
            ++m_counters->refreshVisited;
        }
        if (IsTransparent(arena.Resolve(id).kind))
        {
            bool transparentWork = false;
            for (NodeId c = arena.Resolve(id).firstChild; c.Valid(); c = arena.Resolve(c).nextSibling)
            {
                transparentWork |= Refresh(arena, c, measure);
            }
            return transparentWork;
        }

        bool work = false;
        if (arena.Resolve(id).IsDirty(Dirty::Layout))
        {
            if (m_counters)
            {
                ++m_counters->refreshDirty;
                ++m_counters->applyStyleCalls;
                ++m_counters->setChildrenCalls;
            }
            const LiveNode& node = arena.Resolve(id);
            const NodeId parentId = EffectiveParent(arena, node);
            const SolverInput input{
                node.style,
                parentId.Valid() ? arena.Resolve(parentId).style.axis : FlexAxis::Column,
                parentId.Valid(),
            };
            {
                detail::ScopedAccumMs styleTiming(m_counters ? &m_counters->styleMs : nullptr);
                m_backend.ApplyStyle(node.layout, input);
            }

            if (Widget* widget = KindRegistry::For(node.kind); widget && widget->ContentSized())
            {
                Vec2 size;
                {
                    detail::ScopedAccumMs measureTiming(m_counters ? &m_counters->measureMs : nullptr);
                    size = widget->Measure(measure, id, AvailableSpace::MaxContent(), AvailableSpace::MaxContent());
                }
                // Pin a Wrap axis to its content size except the axis the parent stretches (its cross axis under cross-align Stretch),
                // which pinning to Px would defeat. Cross axis is height for a Row, width for a Column.
                const bool parentRow = input.hasParent && input.parentAxis == FlexAxis::Row;
                const Align parentCross = parentId.Valid() ? arena.Resolve(parentId).style.cross : Align::Start;
                const bool stretchCross = parentCross == Align::Stretch;
                const bool pinWidth = node.style.width.kind == SizeHint::Kind::Wrap && !(stretchCross && !parentRow);
                const bool pinHeight = node.style.height.kind == SizeHint::Kind::Wrap && !(stretchCross && parentRow);
                m_backend.PinContentSize(node.layout, size, pinWidth, pinHeight);
            }

            std::vector<LayoutHandle> children;
            GatherChildren(arena, id, children);
            {
                detail::ScopedAccumMs styleTiming(m_counters ? &m_counters->styleMs : nullptr);
                m_backend.SetChildren(node.layout, children);
            }
            work = true;
        }

        for (NodeId c = arena.Resolve(id).firstChild; c.Valid(); c = arena.Resolve(c).nextSibling)
        {
            work |= Refresh(arena, c, measure);
        }
        return work;
    }

    template <LayoutBackend B>
    bool LayoutDriver<B>::Remeasure(NodeArena& arena, NodeId id, bool parentDefinite, const MeasureContext& measure)
    {
        if (m_counters)
        {
            ++m_counters->remeasureVisited;
        }
        bool changed = false;
        LiveNode& node = arena.Resolve(id);

        // Definiteness propagates top-down: a definite width is imposed by an ancestor, else shrink-to-fit. Text wraps only in a definite box,
        // or a heading in a collapsing shrink-to-fit Column would break word-by-word. Transparent hosts own no box.
        bool thisDefinite = parentDefinite;
        if (!IsTransparent(node.kind))
        {
            const NodeId parentId = EffectiveParent(arena, node);
            const bool hasParent = parentId.Valid();
            const bool parentRow = hasParent && arena.Resolve(parentId).style.axis == FlexAxis::Row;
            const bool stretchCross = hasParent && arena.Resolve(parentId).style.cross == Align::Stretch;
            const bool crossStretchedWidth = stretchCross && !parentRow; // parent stretches this node's width

            switch (node.style.width.kind)
            {
            case SizeHint::Kind::Fixed:
            case SizeHint::Kind::Percent: thisDefinite = true; break;          // self-definite
            case SizeHint::Kind::Expand:  thisDefinite = parentDefinite; break; // fills the parent
            case SizeHint::Kind::Wrap:    thisDefinite = crossStretchedWidth && parentDefinite; break;
            }

            Widget* widget = KindRegistry::For(node.kind);
            if (hasParent && widget && widget->ContentSized() && widget->ReflowsWidth())
            {
                node.wrapInBox = thisDefinite; // paint reflows into the box only when it's a real allotment

                const LiveNode& parent = arena.Resolve(parentId);
                // A Wrap axis is content-pinned unless the parent cross-stretches it; its solved rect then carries the overflowing natural width,
                // so the real allotment is the parent's inner box minus this node's placed x. A stretched/explicit width is already the allotment.
                const bool widthPinned = node.style.width.kind == SizeHint::Kind::Wrap && !crossStretchedWidth;
                const bool heightPinned = node.style.height.kind == SizeHint::Kind::Wrap && !(stretchCross && parentRow);

                if (thisDefinite && (widthPinned || heightPinned))
                {
                    const float padH = node.style.padding.Horizontal();
                    const float padV = node.style.padding.Vertical();
                    float availContent;
                    if (widthPinned)
                    {
                        const float parentInnerRight =
                            parent.solved.x + parent.solved.width - parent.style.padding.right;
                        availContent = parentInnerRight - node.solved.x - node.style.margin.right - padH;
                    }
                    else
                    {
                        availContent = node.solved.width - padH;
                    }
                    availContent = std::max(0.f, availContent);

                    // Content size for the allotted width: hug the natural single line when it fits, else reflow (wrap on: taller box; wrap off:
                    // single line clamped so the run clips at paint). `natural` is the cached first-pass MaxContent measure.
                    detail::ScopedAccumMs measureTiming(m_counters ? &m_counters->measureMs : nullptr);
                    const Vec2 natural =
                        widget->Measure(measure, id, AvailableSpace::MaxContent(), AvailableSpace::MaxContent());
                    constexpr float kSlack = 0.5f;
                    Vec2 target = natural;
                    if (natural.x > availContent + kSlack)
                    {
                        const Vec2 reflowed = widget->Measure(measure, id, AvailableSpace::Definite(availContent),
                                                              AvailableSpace::MaxContent());
                        target = {availContent, reflowed.y};
                    }

                    // Re-pin only the axis that changed (content-size compare): restores natural width when the surface grows back and skips a needless solve.
                    const bool widthChanged = widthPinned && std::abs(target.x - (node.solved.width - padH)) > kSlack;
                    const bool heightChanged = heightPinned && std::abs(target.y - (node.solved.height - padV)) > kSlack;
                    if (widthChanged || heightChanged)
                    {
                        if (m_counters)
                        {
                            ++m_counters->remeasureRepins;
                        }
                        m_backend.PinContentSize(node.layout, target, widthPinned, heightPinned);
                        changed = true;
                    }
                }
            }
        }

        for (NodeId c = arena.Resolve(id).firstChild; c.Valid(); c = arena.Resolve(c).nextSibling)
        {
            changed |= Remeasure(arena, c, thisDefinite, measure);
        }
        return changed;
    }

    template <LayoutBackend B>
    void LayoutDriver<B>::GatherChildren(NodeArena& arena, NodeId id, std::vector<LayoutHandle>& out) const
    {
        for (NodeId c = arena.Resolve(id).firstChild; c.Valid();)
        {
            const LiveNode& child = arena.Resolve(c);
            if (IsTransparent(child.kind))
            {
                GatherChildren(arena, c, out); // hoist transparent hosts to their real descendants
            }
            else
            {
                out.push_back(child.layout);
            }
            c = child.nextSibling;
        }
    }

    template <LayoutBackend B>
    void LayoutDriver<B>::GatherChildNodes(NodeArena& arena, NodeId id, std::vector<NodeId>& out) const
    {
        for (NodeId c = arena.Resolve(id).firstChild; c.Valid();)
        {
            const LiveNode& child = arena.Resolve(c);
            if (IsTransparent(child.kind))
            {
                GatherChildNodes(arena, c, out); // hoist transparent hosts to their real descendants
            }
            else
            {
                out.push_back(child.id);
            }
            c = child.nextSibling;
        }
    }

    template <LayoutBackend B>
    bool LayoutDriver<B>::PlaceGrids(NodeArena& arena, NodeId id)
    {
        if (m_counters)
        {
            ++m_counters->gridsVisited;
        }
        bool work = false;
        if (arena.Resolve(id).kind == LiveKind::Grid)
        {
            const bool placed = PlaceOneGrid(arena, id); // parent-first: a child grid is placed after its cell is sized
            if (placed && m_counters)
            {
                ++m_counters->gridsPlaced;
            }
            work |= placed;
        }
        for (NodeId c = arena.Resolve(id).firstChild; c.Valid(); c = arena.Resolve(c).nextSibling)
        {
            work |= PlaceGrids(arena, c);
        }
        return work;
    }

    template <LayoutBackend B>
    bool LayoutDriver<B>::PlaceOneGrid(NodeArena& arena, NodeId gridId)
    {
        std::vector<NodeId> items;
        GatherChildNodes(arena, gridId, items);
        if (items.empty())
        {
            return false;
        }


        std::uint64_t itemsHash = 1469598103934665603ull;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            itemsHash = (itemsHash ^ (static_cast<std::uint64_t>(items[i].value) * (i + 1))) * 1099511628211ull;
        }

        GridPayload& gp = arena.PayloadOf<GridPayload>(gridId);
        const LiveNode& grid = arena.Resolve(gridId);
        const Spacing pad = grid.style.padding;

        // Grid content box: Fixed axes are exact without solving, Expand/Percent are read off the solved rect, and a Wrap axis has
        // no definite extent (fr tracks collapse; the grid is pinned to the track total below).
        const bool definiteW = grid.style.width.kind != SizeHint::Kind::Wrap;
        const bool definiteH = grid.style.height.kind != SizeHint::Kind::Wrap;
        const float boxW =
            grid.style.width.kind == SizeHint::Kind::Fixed ? grid.style.width.value : grid.solved.width;
        const float boxH =
            grid.style.height.kind == SizeHint::Kind::Fixed ? grid.style.height.value : grid.solved.height;
        const float contentW = std::max(0.f, boxW - pad.Horizontal());
        const float contentH = std::max(0.f, boxH - pad.Vertical());

        // Already placed against this box and child set: stop the fixpoint loop (a steady grid does no solver work). But if a cell's
        // style re-resolved (:hover, className toggle), its resolver overwrote our absolute placement with its Flow/Wrap origin, so
        // re-place whenever any cell lost its absolute placement or it would drop out of the grid.
        constexpr float kEps = 0.5f;
        bool placementIntact = true;
        for (const NodeId it : items)
        {
            if (arena.Resolve(it).style.position != Position::Absolute)
            {
                placementIntact = false;
                break;
            }
        }
        if (placementIntact && itemsHash == gp.lastItemsHash && std::abs(contentW - gp.lastContentW) < kEps &&
            std::abs(contentH - gp.lastContentH) < kEps)
        {
            return false;
        }
        gp.lastContentW = contentW;
        gp.lastContentH = contentH;
        gp.lastItemsHash = itemsHash;

        const int colCount = std::max<int>(1, static_cast<int>(gp.columns.size()));

        std::vector<detail::GridItemReq> reqs(items.size());
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const GridItemPlacement& pl = arena.Resolve(items[i]).style.placement;
            reqs[i] = {pl.col, pl.row, pl.colSpan, pl.rowSpan};
        }

        std::vector<detail::GridPlacement> placed;
        int rowCount = 0;
        detail::PlaceGridItems(colCount, static_cast<int>(gp.rows.size()), reqs, placed, rowCount);

        const std::vector<float> colSizes = detail::ResolveTrackSizes(gp.columns, contentW, gp.colGap, definiteW);

        std::vector<TrackSize> rowTracks(static_cast<std::size_t>(rowCount));
        for (int r = 0; r < rowCount; ++r)
        {
            rowTracks[static_cast<std::size_t>(r)] = r < static_cast<int>(gp.rows.size()) ? gp.rows[r] : gp.autoRows;
        }
        const std::vector<float> rowSizes = detail::ResolveTrackSizes(rowTracks, contentH, gp.rowGap, definiteH);

        const std::vector<float> colOff = detail::TrackOffsets(colSizes, gp.colGap);
        const std::vector<float> rowOff = detail::TrackOffsets(rowSizes, gp.rowGap);

        constexpr float kAuto = std::numeric_limits<float>::quiet_NaN();
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const detail::GridPlacement& p = placed[i];
            LiveNode& child = arena.Resolve(items[i]);
            LayoutStyle& s = child.style;
            // Absolute, content-box-relative (the solver adds the grid's padding origin). Left and top are explicit, since an auto edge
            // falls into the flex static-position pass and centres; right/bottom stay auto (width/height are fixed).
            s.position = Position::Absolute;
            s.inset.left = p.col < static_cast<int>(colOff.size()) ? colOff[static_cast<std::size_t>(p.col)] : 0.f;
            s.inset.top = p.row < static_cast<int>(rowOff.size()) ? rowOff[static_cast<std::size_t>(p.row)] : 0.f;
            s.inset.right = kAuto;
            s.inset.bottom = kAuto;
            s.width = SizeHint::MakeFixed(detail::SpanExtent(colSizes, p.col, p.colSpan, gp.colGap));
            s.height = SizeHint::MakeFixed(detail::SpanExtent(rowSizes, p.row, p.rowSpan, gp.rowGap));
            const SolverInput in{s, FlexAxis::Grid, true};
            m_backend.ApplyStyle(child.layout, in);
        }

        // A Wrap axis hugs the track total: children are out of flow, so the flex solver would otherwise size the grid to its padding.
        if (!definiteW || !definiteH)
        {
            const float totalW = detail::SpanExtent(colSizes, 0, static_cast<int>(colSizes.size()), gp.colGap);
            const float totalH = detail::SpanExtent(rowSizes, 0, static_cast<int>(rowSizes.size()), gp.rowGap);
            m_backend.PinContentSize(grid.layout, Vec2{totalW + pad.Horizontal(), totalH + pad.Vertical()},
                                     !definiteW, !definiteH);
        }

        return true;
    }

    template <LayoutBackend B>
    NodeId LayoutDriver<B>::EffectiveParent(NodeArena& arena, const LiveNode& node)
    {
        NodeId p = node.parent;
        while (p.Valid() && IsTransparent(arena.Resolve(p).kind))
        {
            p = arena.Resolve(p).parent;
        }
        return p;
    }

    template <LayoutBackend B>
    NodeId LayoutDriver<B>::OpaqueRoot(NodeArena& arena, NodeId root)
    {
        NodeId id = root;
        while (id.Valid() && IsTransparent(arena.Resolve(id).kind))
        {
            id = arena.Resolve(id).firstChild;
        }
        return id;
    }

    template <LayoutBackend B>
    void LayoutDriver<B>::ReadBack(NodeArena& arena, NodeId id)
    {
        if (m_counters)
        {
            ++m_counters->readBackVisited;
        }
        LiveNode& node = arena.Resolve(id);
        if (!IsTransparent(node.kind))
        {
            const Rect solved = m_backend.ReadRect(node.layout);
            // A moved or resized box needs repaint; equality-gated so a re-solve that lands every rect where it was does not dirty the tree.
            if (solved.x != node.solved.x || solved.y != node.solved.y ||
                solved.width != node.solved.width || solved.height != node.solved.height)
            {
                node.solved = solved;
                node.MarkDirty(Dirty::Repaint);
            }
            node.ClearDirty(Dirty::Layout);
        }
        for (NodeId c = node.firstChild; c.Valid(); c = arena.Resolve(c).nextSibling)
        {
            ReadBack(arena, c);
        }
    }

    /// The Masharif-backed layout tree for one app: owns the solver nodes and drives the per-frame pass. Masharif-free here
    /// (pimpl); defined in MasharifBackend.cpp.
    class LayoutTree
    {
    public:
        LayoutTree();
        ~LayoutTree();
        LayoutTree(const LayoutTree&) = delete;
        LayoutTree& operator=(const LayoutTree&) = delete;

        /// Create the solver node for a freshly instantiated live node.
        [[nodiscard]] LayoutHandle Attach();
        /// Release a solver node when its live node is disposed.
        void Detach(LayoutHandle handle) noexcept;
        /// One incremental layout pass over the live tree rooted at `root`. `counters` is optional -
        /// null outside a MountedApp frame (bare harnesses, headless tools).
        void Run(NodeArena& arena, NodeId root, float surfaceW, float surfaceH,
                 const TextMeasurer* measurer, ImageProvider* images, FrameCounters* counters = nullptr);

        /// Per-node strategy-run count, the incremental-behaviour probe (cheap observability, not a hot path).
        [[nodiscard]] std::uint32_t SolveCount(LayoutHandle handle) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

    /// Per-frame entry: map the live tree onto the solver, solve, write absolute rects. No-op when no LayoutTree is attached.
    void Layout(AppCtx& ctx, NodeId root, float surfaceW, float surfaceH);

    /// Flag a control-flow splice (Show/Each/Memo add, remove, reorder) for re-layout: control nodes own no solver node, so mark the
    /// nearest opaque ancestor Dirty::Layout (re-runs SetChildren). No-op when the subtree has none.
    void MarkStructureDirty(NodeArena& arena, NodeId controlNode);
} // namespace lumora
