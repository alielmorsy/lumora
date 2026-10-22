#include <lumora/Widgets/VirtualList.h>

#include "Layout/LayoutBridge.h"
#include "Node/ControlState.h"
#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/ControlRuntime.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"
#include "Style/StyleResolver.h"
#include "Widgets/ScrollCore.h"
#include "Widgets/VirtualListRuntime.h"
#include "Widgets/Widget.h"

#include <lumora/Paint/Canvas.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Widgets/Layouts.h>

#include <lumora/log/Log.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

LUMORA_LOG_CATEGORY (

"Lumora"
)

namespace lumora
{
    namespace
    {
        [[nodiscard]] scrollcore::Gesture GestureOf(VirtualListState& st)
        {
            return scrollcore::Gesture{
                .axis = st.axis,
                .offsetX = &st.offsetX,
                .offsetY = &st.offsetY,
                .draggingX = &st.draggingX,
                .draggingY = &st.draggingY,
                .grabOffset = &st.grabOffset,
                .grabCursor = &st.grabCursor,
                .hovered = &st.hovered,
                .hoverX = &st.hoverX,
                .hoverY = &st.hoverY,
                // A different row set exists at the new offset, so the solve has to run again.
                .relayoutOnScroll = true,
            };
        }

        [[nodiscard]] bool Horizontal(const VirtualListState& st) noexcept
        {
            return st.axis == ScrollAxis::Horizontal;
        }

        /// Horizontal reach of a `Both` list: the widest row materialized since rows could last have narrowed. Only
        /// a row-driven width (wider than the viewport's inner width) is remembered, else a narrowed viewport scrolls blank space.
        [[nodiscard]] float ContentWidth(NodeArena& arena, const LiveNode& n, VirtualListState& st)
        {
            if (st.axis != ScrollAxis::Both || !st.content.Valid())
            {
                return n.solved.width;
            }
            const Spacing& pad = n.style.padding;
            const LiveNode& content = arena.Resolve(st.content);
            if (!st.forgetWidest && content.solved.width > n.solved.width - pad.Horizontal() + 0.5f)
            {
                st.widest = std::max(st.widest, content.solved.Right() - n.solved.x + pad.right);
            }
            return std::max(n.solved.width, st.widest);
        }

        /// Reach is `count * itemExtent`, not the children's bounding box (only a window of rows exists, so the thumb
        /// would fill the track), plus the list's padding insets so the last row can scroll clear of the trailing edge.
        [[nodiscard]] scrollcore::Extent ExtentOf(NodeArena& arena, const LiveNode& n, VirtualListState& st)
        {
            const Spacing& pad = n.style.padding;
            const float span = st.config.itemExtent > 0.f
                                   ? static_cast<float>(st.count) * st.config.itemExtent
                                   : 0.f;
            return Horizontal(st)
                       ? scrollcore::MakeExtent(n.solved, span + pad.left + pad.right, n.solved.height)
                       : scrollcore::MakeExtent(n.solved, ContentWidth(arena, n, st), span + pad.top + pad.bottom);
        }

        /// Leading inset the rows sit behind; the offset must cross it before the first row leaves the viewport.
        [[nodiscard]] float LeadPad(const LiveNode& n, const VirtualListState& st) noexcept
        {
            return Horizontal(st) ? n.style.padding.left : n.style.padding.top;
        }

        struct Window
        {
            int first = 0;
            int last = -1; ///< inclusive; -1 == empty
        };

        [[nodiscard]] Window WindowFor(const LiveNode& n, const VirtualListState& st)
        {
            const float extent = st.config.itemExtent;
            if (st.count <= 0 || extent <= 0.f)
            {
                return {0, -1};
            }
            const float viewLen = Horizontal(st) ? n.solved.width : n.solved.height;
            const float offset = Horizontal(st) ? st.offsetX : st.offsetY;
            if (viewLen <= 0.f)
            {
                return {0, -1}; // not solved yet: the first pass has no viewport to window against
            }

            const int overscan = std::max(0, st.config.overscan);
            // The offset is measured against the padded reach, so subtract the leading inset first or a strip of padding
            // at the top goes un-materialized.
            const int firstVis = static_cast<int>(std::floor(std::max(0.f, offset - LeadPad(n, st)) / extent));
            // +1 for the row the viewport's own top edge cuts through, +1 for the one its bottom does.
            const int visible = static_cast<int>(std::ceil(viewLen / extent)) + 1;

            Window w;
            w.first = std::clamp(firstVis - overscan, 0, st.count - 1);
            w.last = std::clamp(firstVis + visible + overscan, 0, st.count - 1);
            return w;
        }

        /// Bring the materialized set to exactly [first, last]. A row is reused when the same index still projects
        /// the same key (scrolling), keeping scope, signals and focus; a moved index is rebuilt (builder is index-addressed).
        void ReconcileWindow(AppCtx& ctx, NodeId id, Window w, bool rebuildAll)
        {
            NodeId content;
            std::function < Elem(int) > rowFn;
            std::function < std::uint64_t(int) > keyFn;
            {
                VirtualListState& st = ctx.arena.PayloadOf<VirtualListState>(id);
                content = st.content;
                rowFn = st.config.row;
                keyFn = st.config.key;
            }
            if (!content.Valid() || !rowFn)
            {
                return;
            }

            std::vector<VirtualRow> old;
            {
                VirtualListState& st = ctx.arena.PayloadOf<VirtualListState>(id);
                old = std::move(st.rows);
                st.rows.clear();
            }
            std::vector<bool> reused(old.size(), false);

            std::vector<VirtualRow> next;
            next.reserve(w.last >= w.first ? static_cast<std::size_t>(w.last - w.first + 1) : 0u);

            for (int index = w.first; index <= w.last; ++index)
            {
                const std::uint64_t key = keyFn ? keyFn(index) : static_cast<std::uint64_t>(index);

                std::size_t hit = old.size();
                if (!rebuildAll || keyFn)
                {
                    for (std::size_t i = 0; i < old.size(); ++i)
                    {
                        if (!reused[i] && old[i].index == index && old[i].key == key)
                        {
                            hit = i;
                            break;
                        }
                    }
                }

                if (hit != old.size())
                {
                    reused[hit] = true;
                    next.push_back(std::move(old[hit]));
                    continue;
                }

                MountedBody body = MountBody(ctx, content, [rowFn, index] { return rowFn(index); });
                next.push_back(VirtualRow{key, index, std::move(body.scope), body.root});
            }

            for (std::size_t i = 0; i < old.size(); ++i)
            {
                if (reused[i])
                {
                    continue;
                }
                if (old[i].root.Valid())
                {
                    UnlinkChild(ctx.arena, content, old[i].root);
                }
                old[i].scope.reset(); // disposes the row subtree and runs its cleanups
            }

            std::vector<NodeId> order;
            order.reserve(next.size());
            for (const VirtualRow& row : next)
            {
                if (row.root.Valid())
                {
                    order.push_back(row.root);
                }
            }
            SetChildOrder(ctx.arena, content, order);

            {
                VirtualListState& st = ctx.arena.PayloadOf<VirtualListState>(id);
                st.rows = std::move(next);
                st.first = w.first;
                st.last = w.last;
            }
            // The content column re-gathers its solver children; it is opaque, so mark it directly.
            ctx.arena.Resolve(content).MarkDirty(Dirty::Layout);
        }

        class VirtualListWidget final : public TypedWidget<VirtualListCfg>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "VirtualList"; }

            void Build(AppCtx& ctx, VirtualListCfg& cfg, NodeId id, Owner& owner) override
            {
                const std::uint32_t slot = ctx.arena.Pool<VirtualListState>(LiveKind::VirtualList).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::VirtualList, slot};

                const ScrollAxis axis = cfg.axis;
                ctx.arena.Resolve(id).style.axis = axis == ScrollAxis::Horizontal ? FlexAxis::Row : FlexAxis::Column;

                if (cfg.itemExtent <= 0.f)
                {
                    LOG_WARN("VirtualList: itemExtent must be > 0 (got {}); the list will materialize "
                             "nothing until it is set",
                             cfg.itemExtent);
                }

                {
                    VirtualListState& st = ctx.arena.PayloadOf<VirtualListState>(id);
                    st.axis = axis;
                    st.config = cfg;
                }

                if (ctx.style)
                {
                    BindInteractProps<VirtualListState>(ctx, id, ctx.arena.PayloadOf<VirtualListState>(id).config.on,
                                                        &VirtualListState::on);
                    const FlexAxis base = ctx.arena.Resolve(id).style.axis;
                    style::StyleEngine* eng = ctx.style;
                    style::BindClass(ctx, id, ctx.arena.PayloadOf<VirtualListState>(id).config,
                                     [base, eng](NodeArena& a, NodeId n, const style::ComputedStyle& cs,
                                                 const VirtualListCfg& c)
                                     {
                                         style::ApplyBoxLike<VirtualListState>(
                                             a, n, cs, c, &VirtualListState::style, &VirtualListState::view,
                                             &VirtualListState::transform, base, eng->NodeState(n));
                                         style::EnrolBoxAnimator<VirtualListState, &VirtualListState::style,
                                                                 &VirtualListState::transform>(*eng, a, n);
                                     });
                }
                else
                {
                    const VirtualListCfg& c = ctx.arena.PayloadOf<VirtualListState>(id).config;
                    BindLayoutProps(ctx.arena, id, c.layout);
                    BindStyleProps<VirtualListState>(ctx.arena, id, c.style, &VirtualListState::style);
                    BindViewProps<VirtualListState>(ctx.arena, id, c.view, &VirtualListState::view);
                    BindTransformProps<VirtualListState>(ctx.arena, id, c.transform, &VirtualListState::transform);
                    BindInteractProps<VirtualListState>(ctx, id, c.on, &VirtualListState::on);
                }

                // Rows live in an inner Column so the sub-row scroll remainder rides its TransformState (a paint-space
                // translate painter and hit-test already handle) instead of a second Scroll special case.
                {
                    MountedBody body = MountBody(
                        ctx, id,
                        [axis]
                        {
                            // Both axes hug: main = materialized rows' extent, cross = imposed by the list's Align::Stretch.
                            if (axis == ScrollAxis::Horizontal)
                            {
                                return Row(RowCfg{.layout = {.cross = Align::Stretch}});
                            }
                            // Both opts out of that stretch: the column hugs its widest row (floored at the viewport) and stretches rows to match.
                            if (axis == ScrollAxis::Both)
                            {
                                return Column(ColumnCfg{
                                    .layout = {
                                        .cross = Align::Stretch,
                                        .minWidth = Percent(100.f),
                                        .alignSelf = Align::Start
                                    }
                                });
                            }
                            return Column(ColumnCfg{.layout = {.cross = Align::Stretch}});
                        });
                    if (body.root.Valid())
                    {
                        AppendChild(ctx.arena, id, body.root);
                    }
                    VirtualListState& st = ctx.arena.PayloadOf<VirtualListState>(id);
                    st.contentScope = std::move(body.scope);
                    st.content = body.root;
                }

                // Count and version only dirty the node; rows are materialized in RunVirtualLists, which knows the solved viewport.
                AppCtx* ctxPtr = &ctx;
                Effect([ctxPtr, id]
                {
                    const VirtualListState& read = ctxPtr->arena.PayloadOf<VirtualListState>(id);
                    const int count = read.config.count.Get();
                    const std::uint64_t version = read.config.version.Get();
                    const std::uint32_t gen = ctxPtr->arena.GenerationOf(id);
                    ctxPtr->graph.ApplyStructuralEdit([ctxPtr, id, gen, count, version]
                    {
                        if (ctxPtr->arena.GenerationOf(id) != gen)
                        {
                            return;
                        }
                        VirtualListState& st = ctxPtr->arena.PayloadOf<VirtualListState>(id);
                        if (st.count == count && st.version == version)
                        {
                            return;
                        }
                        st.rebuild = st.rebuild || st.version != version;
                        if (std::max(0, count) < st.count || st.version != version)
                        {
                            st.widest = 0.f;
                            st.forgetWidest = true;
                        }
                        st.count = std::max(0, count);
                        st.version = version;
                        ctxPtr->arena.Resolve(id).MarkDirty(Dirty::Layout | Dirty::Repaint);
                    });
                });

                ctx.virtualLists.push_back(AppCtx::TrackedNode{id, ctx.arena.GenerationOf(id)});

                // Disposal order matters: rows and the content column live under scopes this payload owns, so drop them
                // while it still resolves.
                NodeArena* arena = &ctx.arena;
                owner.AddCleanup([arena, id]
                {
                    if (LiveNode* node = arena->TryResolve(id); node && node->payload.Valid())
                    {
                        VirtualListState& st = arena->PayloadOf<VirtualListState>(id);
                        for (VirtualRow& row : st.rows)
                        {
                            row.scope.reset();
                        }
                        st.rows.clear();
                        st.contentScope.reset();
                    }
                });
            }

            void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                const LiveNode& n = ctx.arena->Resolve(id);
                VirtualListState& st = ctx.arena->PayloadOf<VirtualListState>(id);
                scrollcore::Clamp(GestureOf(st), ExtentOf(*ctx.arena, n, st));

                const StyleState& s = st.style;
                if (s.fill.a == 0 && s.border.width <= 0.f)
                {
                    return;
                }
                canvas.DrawRect({
                    .bounds = n.solved,
                    .fill = s.fill,
                    .radius = s.radius,
                    .borderColor = s.border.color,
                    .borderWidth = s.border.width,
                    .borderRadius = s.radius
                });
            }

            void PaintOverlay(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                const LiveNode& n = ctx.arena->Resolve(id);
                VirtualListState& st = ctx.arena->PayloadOf<VirtualListState>(id);
                const scrollcore::Extent e = ExtentOf(*ctx.arena, n, st);
                const scrollcore::Gesture g = GestureOf(st);

                if (scrollcore::ShowV(g, e))
                {
                    canvas.DrawRect({
                        .bounds = scrollcore::VBar(n.solved, e, st.offsetY).thumb,
                        .fill = scrollcore::kThumbColor.WithAlpha(
                            scrollcore::ThumbAlpha(st.draggingY, st.hoverY, st.hovered)),
                        .radius = scrollcore::kThumbRadius
                    });
                }
                if (scrollcore::ShowH(g, e))
                {
                    canvas.DrawRect({
                        .bounds = scrollcore::HBar(n.solved, e, st.offsetX).thumb,
                        .fill = scrollcore::kThumbColor.WithAlpha(
                            scrollcore::ThumbAlpha(st.draggingX, st.hoverX, st.hovered)),
                        .radius = scrollcore::kThumbRadius
                    });
                }
            }

            void OnEvent(EventContext& ctx, Event& ev) override
            {
                VirtualListState& st = ctx.Payload<VirtualListState>();
                LiveNode& n = ctx.Node();

                if (scrollcore::HandleEvent(ctx, ev, n, ExtentOf(*ctx.arena, n, st), GestureOf(st),
                                            Vec2{st.transform.x, st.transform.y}))
                {
                    return;
                }
                DispatchInteract(ctx, ev, st.on);
            }
        };

        void EnsureKind()
        {
            static const bool registered = []
            {
                KindRegistry::Register(LiveKind::VirtualList, MakeRef<VirtualListWidget>());
                return true;
            }();
            (void)registered;
        }
    } // namespace

    bool RunVirtualLists(AppCtx& ctx)
    {
        if (ctx.virtualLists.empty())
        {
            return false;
        }

        bool changed = false;
        std::vector<AppCtx::TrackedNode> live;
        live.reserve(ctx.virtualLists.size());

        for (const AppCtx::TrackedNode& tracked : ctx.virtualLists)
        {
            LiveNode* node = ctx.arena.TryResolve(tracked.id);
            if (!node || ctx.arena.GenerationOf(tracked.id) != tracked.gen || !node->payload.Valid())
            {
                continue; // unmounted since it registered; a re-mount re-registers
            }
            live.push_back(tracked);

            const NodeId id = tracked.id;
            Window window;
            bool rebuild = false;
            NodeId content;
            Vec2 shift{};
            {
                VirtualListState& st = ctx.arena.PayloadOf<VirtualListState>(id);
                scrollcore::Clamp(GestureOf(st), ExtentOf(ctx.arena, *node, st));

                window = WindowFor(*node, st);
                rebuild = st.rebuild;
                content = st.content;

                const bool horizontal = Horizontal(st);
                const float offset = horizontal ? st.offsetX : st.offsetY;
                const float remainder = st.config.itemExtent > 0.f
                                            ? offset - static_cast<float>(window.first) * st.config.itemExtent
                                            : 0.f;
                // Main axis rides only the sub-row remainder; Both's cross axis is not virtualized, so it takes the whole
                // horizontal offset.
                shift = horizontal
                            ? Vec2{-remainder, 0.f}
                            : Vec2{st.axis == ScrollAxis::Both ? -st.offsetX : 0.f, -remainder};
                // The clamp ran without the stale rows; whatever is mounted after this pass is current, so extent reads may learn again.
                st.forgetWidest = false;

                if (!rebuild && window.first == st.first && window.last == st.last)
                {
                    // Window unchanged: the offset moved within the leading row, which is only a translate.
                    st.rebuild = false;
                    if (content.Valid())
                    {
                        TransformState& xf = ctx.arena.PayloadOf<ContainerPayload>(content).transform;
                        if (xf.x != shift.x || xf.y != shift.y)
                        {
                            xf.x = shift.x;
                            xf.y = shift.y;
                            ctx.arena.Resolve(content).MarkDirty(Dirty::Repaint);
                            ctx.arena.Resolve(id).MarkDirty(Dirty::Repaint);
                        }
                    }
                    continue;
                }
                st.rebuild = false;
            }

            ReconcileWindow(ctx, id, window, rebuild);

            if (content.Valid())
            {
                TransformState& xf = ctx.arena.PayloadOf<ContainerPayload>(content).transform;
                xf.x = shift.x;
                xf.y = shift.y;
            }
            ctx.arena.Resolve(id).MarkDirty(Dirty::Repaint);
            changed = true;
        }

        ctx.virtualLists = std::move(live);
        return changed;
    }

    Elem VirtualList(VirtualListCfg cfg)
    {
        EnsureKind();
        return Elem(detail::MakeBlueprint(LiveKind::VirtualList, std::move(cfg)));
    }
} // namespace lumora
