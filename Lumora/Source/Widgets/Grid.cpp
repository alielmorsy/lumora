#include <lumora/Widgets/Grid.h>

#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"
#include "Style/StyleResolver.h"
#include "Widgets/Widget.h"

#include <lumora/Paint/Canvas.h>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace lumora
{
    namespace
    {
        /// Container-style fill/border for either payload carrying StyleState (Grid background, Cell box).
        void PaintBox(const Rect& bounds, const StyleState& s, bool hovered, Canvas& canvas)
        {
            const bool showHover = hovered && s.hoverFill.a > 0;
            if (s.fill.a == 0 && s.border.width <= 0.f && !showHover)
            {
                return;
            }
            canvas.DrawRect({
                .bounds = bounds,
                .fill = showHover ? s.hoverFill : s.fill,
                .radius = s.radius,
                .borderColor = s.border.color,
                .borderWidth = s.border.width,
                .borderRadius = s.radius,
                .borderDash = s.border.DashLength()
            });
        }

        /// The grid container: a relative containing block (FlexAxis::Grid); the layout bridge's
        /// placement pass turns its children into absolute cells.
        class GridWidget final : public TypedWidget<GridCfg>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Grid"; }

            void Build(AppCtx& ctx, GridCfg& cfg, NodeId id, Owner&) override
            {
                const std::uint32_t slot = ctx.arena.Pool<GridPayload>(LiveKind::Grid).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Grid, slot};
                ctx.arena.Resolve(id).style.axis = FlexAxis::Grid;
                ctx.arena.Resolve(id).style.anchor = cfg.anchor;

                GridPayload& gp = ctx.arena.PayloadOf<GridPayload>(id);
                // Copy, not move: BindClass captures cfg by value and ApplyGrid falls back to cfg.columns/rows
                // when no CSS grid-template matches.
                gp.columns = cfg.columns.tracks;
                gp.rows = cfg.rows.tracks;
                gp.colGap = cfg.colGap >= 0.f ? cfg.colGap : cfg.gap;
                gp.rowGap = cfg.rowGap >= 0.f ? cfg.rowGap : cfg.gap;
                gp.autoRows = cfg.autoRows;

                if (ctx.style)
                {
                    BindPositionProps(ctx.arena, id, cfg.position);
                    BindInteractProps<GridPayload>(ctx, id, cfg.on, &GridPayload::on);
                    style::StyleEngine* eng = ctx.style;
                    style::BindClass(ctx, id, cfg,
                        [eng](NodeArena& a, NodeId n, const style::ComputedStyle& cs, const GridCfg& c)
                        {
                            style::ApplyBoxLike<GridPayload>(a, n, cs, c, &GridPayload::style, &GridPayload::view,
                                                             &GridPayload::transform, FlexAxis::Grid,
                                                             eng->NodeState(n));
                            style::ApplyGrid(a, n, cs, c);
                            style::EnrolBoxAnimator<GridPayload, &GridPayload::style,
                                                    &GridPayload::transform>(*eng, a, n);
                        });
                }
                else
                {
                    BindLayoutProps(ctx.arena, id, cfg.layout);
                    BindStyleProps<GridPayload>(ctx.arena, id, cfg.style, &GridPayload::style);
                    BindViewProps<GridPayload>(ctx.arena, id, cfg.view, &GridPayload::view);
                    BindTransformProps<GridPayload>(ctx.arena, id, cfg.transform, &GridPayload::transform);
                    BindPositionProps(ctx.arena, id, cfg.position);
                    BindInteractProps<GridPayload>(ctx, id, cfg.on, &GridPayload::on);
                }
            }

            void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                const LiveNode& n = ctx.arena->Resolve(id);
                const GridPayload& g = ctx.arena->PayloadOf<GridPayload>(id);
                PaintBox(n.solved, g.style, g.on.hovered, canvas);
            }

            [[nodiscard]] HitTestBehavior HitTest(const EventContext& ctx, Vec2) const override
            {
                return ctx.Payload<GridPayload>().on.hitThrough ? HitTestBehavior::Ignore
                                                                : HitTestBehavior::Opaque;
            }

            void OnEvent(EventContext& ctx, Event& ev) override
            {
                DispatchInteract(ctx, ev, ctx.Payload<GridPayload>().on);
            }
        };

        /// A placed grid item: a Box with explicit col/row/span placement, ignored outside a Grid.
        /// Shares ContainerPayload with Box/Row/Column/Stack.
        class CellWidget final : public TypedWidget<CellCfg>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Cell"; }

            void Build(AppCtx& ctx, CellCfg& cfg, NodeId id, Owner&) override
            {
                const std::uint32_t slot = ctx.arena.Pool<ContainerPayload>(LiveKind::Cell).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Cell, slot};
                ctx.arena.Resolve(id).style.axis = FlexAxis::Column;
                ctx.arena.Resolve(id).style.anchor = cfg.anchor;
                ctx.arena.Resolve(id).style.placement = GridItemPlacement{
                    static_cast<std::int16_t>(cfg.col),
                    static_cast<std::int16_t>(cfg.row),
                    static_cast<std::int16_t>(std::max(1, cfg.colSpan)),
                    static_cast<std::int16_t>(std::max(1, cfg.rowSpan)),
                };

                if (ctx.style)
                {
                    BindPositionProps(ctx.arena, id, cfg.position);
                    BindInteractProps<ContainerPayload>(ctx, id, cfg.on, &ContainerPayload::on);
                    style::StyleEngine* eng = ctx.style;
                    style::BindClass(ctx, id, cfg,
                        [eng](NodeArena& a, NodeId n, const style::ComputedStyle& cs, const CellCfg& c)
                        {
                            style::ApplyBoxLike<ContainerPayload>(a, n, cs, c, &ContainerPayload::style,
                                                                  &ContainerPayload::view, &ContainerPayload::transform,
                                                                  FlexAxis::Column, eng->NodeState(n));
                            style::EnrolBoxAnimator<ContainerPayload, &ContainerPayload::style,
                                                    &ContainerPayload::transform>(*eng, a, n);
                        });
                }
                else
                {
                    BindLayoutProps(ctx.arena, id, cfg.layout);
                    BindStyleProps<ContainerPayload>(ctx.arena, id, cfg.style, &ContainerPayload::style);
                    BindViewProps<ContainerPayload>(ctx.arena, id, cfg.view, &ContainerPayload::view);
                    BindTransformProps<ContainerPayload>(ctx.arena, id, cfg.transform, &ContainerPayload::transform);
                    BindPositionProps(ctx.arena, id, cfg.position);
                    BindInteractProps<ContainerPayload>(ctx, id, cfg.on, &ContainerPayload::on);
                }
            }

            void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                const LiveNode& n = ctx.arena->Resolve(id);
                const ContainerPayload& c = ctx.arena->PayloadOf<ContainerPayload>(id);
                PaintBox(n.solved, c.style, c.on.hovered, canvas);
            }

            [[nodiscard]] HitTestBehavior HitTest(const EventContext& ctx, Vec2) const override
            {
                return ctx.Payload<ContainerPayload>().on.hitThrough ? HitTestBehavior::Ignore
                                                                     : HitTestBehavior::Opaque;
            }

            void OnEvent(EventContext& ctx, Event& ev) override
            {
                DispatchInteract(ctx, ev, ctx.Payload<ContainerPayload>().on);
            }
        };

        void EnsureGridKinds()
        {
            static const bool registered = []
            {
                KindRegistry::Register(LiveKind::Grid, MakeRef<GridWidget>());
                KindRegistry::Register(LiveKind::Cell, MakeRef<CellWidget>());
                return true;
            }();
            (void)registered;
        }
    } // namespace

    namespace detail
    {
        Ref<Node> BuildGrid(GridCfg cfg)
        {
            EnsureGridKinds();
            return MakeBlueprint(LiveKind::Grid, std::move(cfg));
        }

        Ref<Node> BuildCell(CellCfg cfg)
        {
            EnsureGridKinds();
            return MakeBlueprint(LiveKind::Cell, std::move(cfg));
        }
    } // namespace detail
} // namespace lumora
