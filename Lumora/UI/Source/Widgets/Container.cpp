#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/Layouts.h>

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

#include <utility>

namespace lumora
{
    namespace
    {
        /// The slice of BoxCfg the style resolver re-applies on every resolve. Its effect holds this for the node's lifetime, so
        /// handlers, paint offset and content scale (bound once onto the payload) stay out of it.
        struct BoxStyleCfg
        {
            LayoutProps layout;
            StyleProps style;
            ViewProps view;
            TransformProps transform;
            PositionProps position;
            Reactive<std::string> className;
            struct
            {
                Reactive<bool> disabled;
            } on;
        };

        /// Box/Row/Column/Stack share one behaviour and config; they differ only in the flex axis (from the node kind).
        class ContainerWidget final : public TypedWidget<BoxCfg>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Container"; }

            void Build(AppCtx& ctx, BoxCfg& cfg, NodeId id, Owner&) override
            {
                const LiveKind kind = ctx.arena.Resolve(id).kind;
                const std::uint32_t slot = ctx.arena.Pool<ContainerPayload>(kind).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{kind, slot};
                ctx.arena.Resolve(id).style.axis = kind == LiveKind::Row
                                                       ? FlexAxis::Row
                                                       : kind == LiveKind::Stack
                                                       ? FlexAxis::Stack
                                                       : FlexAxis::Column;
                ctx.arena.Resolve(id).style.anchor = cfg.anchor;

                ContainerPayload& payload = ctx.arena.PayloadOf<ContainerPayload>(id);
                payload.focusScope = cfg.focusScope;
                BindPaintOffset<ContainerPayload>(ctx.arena, id, cfg.paintOffset, &ContainerPayload::transform);
                BindProp(ctx.arena, id, cfg.contentScale,
                         [](NodeArena& a, NodeId n, const float& v) { a.PayloadOf<ContainerPayload>(n).contentScale = v; },
                         Dirty::Repaint);

                if (ctx.style)
                {
                    // Sole writer: the CSS resolver owns layout, paint, view and transform; position and interaction bind classically.
                    BindPositionProps(ctx.arena, id, cfg.position);
                    BindInteractProps<ContainerPayload>(ctx, id, cfg.on, &ContainerPayload::on);
                    const FlexAxis base = ctx.arena.Resolve(id).style.axis;
                    style::StyleEngine* eng = ctx.style;
                    style::BindClass(ctx, id,
                        BoxStyleCfg{cfg.layout, cfg.style, cfg.view, cfg.transform, cfg.position, cfg.className,
                                    {cfg.on.disabled}},
                        [base, eng](NodeArena& a, NodeId n, const style::ComputedStyle& cs, const BoxStyleCfg& c)
                        {
                            style::ApplyBoxLike<ContainerPayload>(a, n, cs, c, &ContainerPayload::style,
                                                                  &ContainerPayload::view, &ContainerPayload::transform,
                                                                  base, eng->NodeState(n));
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
                if (cfg.hitThroughSubtree)
                {
                    ctx.arena.PayloadOf<ContainerPayload>(id).on.hitThroughSubtree = true;
                }
            }

            void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                const LiveNode& n = ctx.arena->Resolve(id);
                const ContainerPayload& c = ctx.arena->PayloadOf<ContainerPayload>(id);
                const StyleState& s = c.style;
                const bool hovering = c.on.hovered && s.hoverFill.a > 0;
                if (s.fill.a == 0 && !s.border.HasBorder() && !hovering)
                {
                    return;
                }
                const Color fill = hovering ? s.hoverFill : s.fill;
                if (s.border.IsUniform())
                {
                    canvas.DrawRect({
                        .bounds = n.solved,
                        .fill = fill,
                        .radius = s.radius,
                        .borderColor = s.border.ColorTop(),
                        .borderWidth = s.border.WidthTop(),
                        .borderRadius = s.radius,
                        .borderDash = s.border.DashLengthTop()
                    });
                    return;
                }

                if (fill.a > 0)
                {
                    canvas.DrawRect({
                        .bounds = n.solved,
                        .fill = fill,
                        .radius = s.radius
                    });
                }

                const Rect& r = n.solved;
                const float topW = s.border.WidthTop();
                const float rightW = s.border.WidthRight();
                const float bottomW = s.border.WidthBottom();
                const float leftW = s.border.WidthLeft();

                const Color topC = s.border.ColorTop();
                const Color rightC = s.border.ColorRight();
                const Color bottomC = s.border.ColorBottom();
                const Color leftC = s.border.ColorLeft();

                const bool hasRadius = s.radius > 0.f;
                if (hasRadius)
                {
                    canvas.PushClip(r, s.radius);
                }
                if (topW > 0.f && topC.a > 0)
                {
                    canvas.DrawRect({.bounds = Rect{r.x, r.y, r.width, topW}, .fill = topC});
                }
                if (bottomW > 0.f && bottomC.a > 0)
                {
                    canvas.DrawRect({.bounds = Rect{r.x, r.Bottom() - bottomW, r.width, bottomW}, .fill = bottomC});
                }
                if (leftW > 0.f && leftC.a > 0)
                {
                    canvas.DrawRect({.bounds = Rect{r.x, r.y, leftW, r.height}, .fill = leftC});
                }
                if (rightW > 0.f && rightC.a > 0)
                {
                    canvas.DrawRect({.bounds = Rect{r.Right() - rightW, r.y, rightW, r.height}, .fill = rightC});
                }
                if (hasRadius)
                {
                    canvas.PopClip();
                }
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

        void EnsureContainerKinds()
        {
            static const bool registered = []
            {
                Ref<Widget> widget = MakeRef<ContainerWidget>();
                KindRegistry::Register(LiveKind::Box, widget);
                KindRegistry::Register(LiveKind::Row, widget);
                KindRegistry::Register(LiveKind::Column, widget);
                KindRegistry::Register(LiveKind::Stack, widget);
                return true;
            }();
            (void)registered;
        }
    } // namespace

    namespace detail
    {
        Ref<Node> BuildBox(BoxCfg cfg)
        {
            EnsureContainerKinds();
            return MakeBlueprint(LiveKind::Box, std::move(cfg));
        }

        Ref<Node> BuildRow(RowCfg cfg)
        {
            EnsureContainerKinds();
            return MakeBlueprint(LiveKind::Row, std::move(cfg));
        }

        Ref<Node> BuildColumn(ColumnCfg cfg)
        {
            EnsureContainerKinds();
            return MakeBlueprint(LiveKind::Column, std::move(cfg));
        }

        Ref<Node> BuildStack(StackCfg cfg)
        {
            EnsureContainerKinds();
            return MakeBlueprint(LiveKind::Stack, std::move(cfg));
        }
    } // namespace detail
} // namespace lumora
