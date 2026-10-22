#include <lumora/Widgets/Painter.h>

#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"
#include "Widgets/Widget.h"

#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/Paint2D.h>

#include <utility>

namespace lumora
{
    namespace
    {
        class PainterWidget final : public TypedWidget<PainterCfg>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Painter"; }

            void Build(AppCtx& ctx, PainterCfg& cfg, NodeId id, Owner&) override
            {
                ctx.hasImmediatePainters = true; // its callback may read the live cursor: see AppCtx
                const std::uint32_t slot = ctx.arena.Pool<PainterPayload>(LiveKind::Painter).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Painter, slot};
                ctx.arena.PayloadOf<PainterPayload>(id).onPaint = std::move(cfg.onPaint);

                LayoutProps layout{};
                layout.width = cfg.width;
                layout.height = cfg.height;
                layout.padding = cfg.padding;
                BindLayoutProps(ctx.arena, id, layout);
                BindInteractProps<PainterPayload>(ctx, id, cfg.on, &PainterPayload::on);
                BindProp<std::uint64_t>(ctx.arena, id, cfg.repaint,
                                        [](NodeArena&, NodeId, const std::uint64_t&) {},
                                        Dirty::Repaint);
            }

            void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                const PainterPayload& p = ctx.arena->PayloadOf<PainterPayload>(id);
                if (!p.onPaint)
                {
                    return;
                }
                const LiveNode& n = ctx.arena->Resolve(id);
                const Spacing& pad = n.style.padding;
                const Rect content{n.solved.x + pad.left, n.solved.y + pad.top,
                                   n.solved.width - pad.left - pad.right,
                                   n.solved.height - pad.top - pad.bottom};
                canvas.PushClip(content);
                Paint2D surface(ctx, canvas, content, ctx.pointerPos - ctx.layerOffset, ctx.layerOffset);
                p.onPaint(surface);
                surface.Unwind();
                canvas.PopClip();
            }

            [[nodiscard]] HitTestBehavior HitTest(const EventContext& ctx, Vec2) const override
            {
                const InteractState& on = ctx.Payload<PainterPayload>().on;
                if (on.hitThrough)
                {
                    return HitTestBehavior::Ignore;
                }
                const bool interactive = on.onClick || on.onPress || on.onRelease || on.onMove
                                      || on.onEnter || on.onLeave || on.onScroll || on.contextMenu;
                return interactive ? HitTestBehavior::Opaque : HitTestBehavior::Ignore;
            }

            void OnEvent(EventContext& ctx, Event& ev) override
            {
                DispatchInteract(ctx, ev, ctx.Payload<PainterPayload>().on);
            }
        };

        void EnsureKind()
        {
            static const bool registered = []
            {
                KindRegistry::Register(LiveKind::Painter, MakeRef<PainterWidget>());
                return true;
            }();
            (void)registered;
        }
    } // namespace

    Elem Painter(PainterCfg cfg)
    {
        EnsureKind();
        return Elem(detail::MakeBlueprint(LiveKind::Painter, std::move(cfg)));
    }
} // namespace lumora
