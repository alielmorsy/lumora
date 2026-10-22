#include <lumora/Widgets/Misc.h>

#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"
#include "Widgets/Widget.h"

#include <lumora/Foundation/Color.h>
#include <lumora/Paint/Canvas.h>

#include <utility>

namespace lumora
{
    namespace
    {
        class DividerWidget final : public TypedWidget<DividerCfg>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Divider"; }

            void Build(AppCtx& ctx, DividerCfg& cfg, NodeId id, Owner&) override
            {
                const std::uint32_t slot = ctx.arena.Pool<DividerPayload>(LiveKind::Divider).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Divider, slot};
                ctx.arena.PayloadOf<DividerPayload>(id).margin = cfg.margin;
                ctx.arena.Resolve(id).style.margin = cfg.margin;

                BindProp(ctx.arena, id, cfg.thickness,
                         [](NodeArena& a, NodeId n, const float& v) { a.PayloadOf<DividerPayload>(n).thickness = v; },
                         Dirty::Layout);
                BindProp(ctx.arena, id, cfg.color,
                         [](NodeArena& a, NodeId n, const Color& v) { a.PayloadOf<DividerPayload>(n).color = v; },
                         Dirty::Style);
            }

            void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                const LiveNode& n = ctx.arena->Resolve(id);
                const DividerPayload& d = ctx.arena->PayloadOf<DividerPayload>(id);
                if (d.color.a > 0)
                {
                    canvas.DrawRect({.bounds = n.solved, .fill = d.color});
                }
            }
        };

        void EnsureKind()
        {
            static const bool registered = []
            {
                KindRegistry::Register(LiveKind::Divider, MakeRef<DividerWidget>());
                return true;
            }();
            (void)registered;
        }
    } // namespace

    Elem Divider(DividerCfg cfg)
    {
        EnsureKind();
        return {detail::MakeBlueprint(LiveKind::Divider, std::move(cfg))};
    }
} // namespace lumora
