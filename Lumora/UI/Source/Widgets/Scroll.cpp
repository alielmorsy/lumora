#include <lumora/Widgets/Scroll.h>

#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"
#include "Style/StyleResolver.h"
#include "Widgets/ScrollCore.h"
#include "Widgets/Widget.h"

#include <lumora/Paint/Canvas.h>

#include <algorithm>
#include <utility>

namespace lumora
{
    namespace
    {
        /// Offsets, drag latches and hover flags for this node's gestures; shared with VirtualList via ScrollCore.
        [[nodiscard]] scrollcore::Gesture GestureOf(ScrollPayload& sc)
        {
            return scrollcore::Gesture{
                .axis       = sc.axis,
                .offsetX    = &sc.offsetX,
                .offsetY    = &sc.offsetY,
                .draggingX  = &sc.draggingX,
                .draggingY  = &sc.draggingY,
                .grabOffset = &sc.grabOffset,
                .grabCursor = &sc.grabCursor,
                .hovered    = &sc.hovered,
                .hoverX     = &sc.hoverX,
                .hoverY     = &sc.hoverY,
            };
        }

        class ScrollWidget final : public TypedWidget<ScrollCfg>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Scroll"; }

            void Build(AppCtx& ctx, ScrollCfg& cfg, NodeId id, Owner&) override
            {
                const std::uint32_t slot = ctx.arena.Pool<ScrollPayload>(LiveKind::Scroll).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Scroll, slot};
                ctx.arena.PayloadOf<ScrollPayload>(id).axis = cfg.axis;
                ctx.arena.Resolve(id).style.axis =
                    cfg.axis == ScrollAxis::Horizontal ? FlexAxis::Row : FlexAxis::Column;

                if (ctx.style)
                {
                    BindInteractProps<ScrollPayload>(ctx, id, cfg.on, &ScrollPayload::on);
                    const FlexAxis base = ctx.arena.Resolve(id).style.axis;
                    style::StyleEngine* eng = ctx.style;
                    style::BindClass(ctx, id, cfg,
                        [base, eng](NodeArena& a, NodeId n, const style::ComputedStyle& cs, const ScrollCfg& c)
                        {
                            style::ApplyBoxLike<ScrollPayload>(a, n, cs, c, &ScrollPayload::style,
                                                               &ScrollPayload::view, &ScrollPayload::transform, base,
                                                               eng->NodeState(n));
                            style::EnrolBoxAnimator<ScrollPayload, &ScrollPayload::style,
                                                    &ScrollPayload::transform>(*eng, a, n);
                        });
                }
                else
                {
                    BindLayoutProps(ctx.arena, id, cfg.layout);
                    BindStyleProps<ScrollPayload>(ctx.arena, id, cfg.style, &ScrollPayload::style);
                    BindViewProps<ScrollPayload>(ctx.arena, id, cfg.view, &ScrollPayload::view);
                    BindInteractProps<ScrollPayload>(ctx, id, cfg.on, &ScrollPayload::on);
                }
            }

            void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                const LiveNode& n = ctx.arena->Resolve(id);
                ScrollPayload& sc = ctx.arena->PayloadOf<ScrollPayload>(id);

                // Re-clamp every frame: content can shrink with no input event, leaving a stale offset. Runs before Paint.cpp reads it.
                const scrollcore::Extent e = scrollcore::MeasureChildren(*ctx.arena, n);
                scrollcore::Clamp(GestureOf(sc), e);

                const StyleState& s = sc.style;
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
                    .borderRadius = s.radius,
                    .borderDash = s.border.DashLength()
                });
            }

            void PaintOverlay(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                const LiveNode& n = ctx.arena->Resolve(id);
                ScrollPayload& sc = ctx.arena->PayloadOf<ScrollPayload>(id);
                const scrollcore::Extent e = scrollcore::MeasureChildren(*ctx.arena, n);
                const scrollcore::Gesture g = GestureOf(sc);

                if (scrollcore::ShowV(g, e))
                {
                    canvas.DrawRect({
                        .bounds = scrollcore::VBar(n.solved, e, sc.offsetY).thumb,
                        .fill = scrollcore::kThumbColor.WithAlpha(
                            scrollcore::ThumbAlpha(sc.draggingY, sc.hoverY, sc.hovered)),
                        .radius = scrollcore::kThumbRadius
                    });
                }
                if (scrollcore::ShowH(g, e))
                {
                    canvas.DrawRect({
                        .bounds = scrollcore::HBar(n.solved, e, sc.offsetX).thumb,
                        .fill = scrollcore::kThumbColor.WithAlpha(
                            scrollcore::ThumbAlpha(sc.draggingX, sc.hoverX, sc.hovered)),
                        .radius = scrollcore::kThumbRadius
                    });
                }
            }

            void OnEvent(EventContext& ctx, Event& ev) override
            {
                ScrollPayload& sc = ctx.Payload<ScrollPayload>();
                LiveNode& n = ctx.Node();
                const scrollcore::Extent e = scrollcore::MeasureChildren(*ctx.arena, n);

                if (scrollcore::HandleEvent(ctx, ev, n, e, GestureOf(sc),
                                            Vec2{sc.transform.x, sc.transform.y}))
                {
                    return;
                }
                DispatchInteract(ctx, ev, sc.on);
            }
        };

        void EnsureKind()
        {
            static const bool registered = []
            {
                KindRegistry::Register(LiveKind::Scroll, MakeRef<ScrollWidget>());
                return true;
            }();
            (void)registered;
        }
    } // namespace

    namespace detail
    {
        Ref<Node> BuildScroll(ScrollCfg cfg)
        {
            EnsureKind();
            return MakeBlueprint(LiveKind::Scroll, std::move(cfg));
        }
    } // namespace detail
} // namespace lumora
