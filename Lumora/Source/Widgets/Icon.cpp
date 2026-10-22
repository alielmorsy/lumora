#include <lumora/Widgets/Misc.h>

#include "Layout/Measure.h"
#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Widgets/Widget.h"

#include <lumora/Foundation/Color.h>

#include <string>
#include <utility>

namespace lumora {

namespace {

/// Icon carries a positional name argument beyond its cfg group.
struct IconData {
    Str     name;
    IconCfg cfg;
};

class IconWidget final : public TypedWidget<IconData> {
    [[nodiscard]] const char* Name() const noexcept override { return "Icon"; }
    [[nodiscard]] bool        ContentSized() const noexcept override { return true; }

    void Build(AppCtx& ctx, IconData& data, NodeId id, Owner&) override {
        const std::uint32_t slot = ctx.arena.Pool<IconPayload>(LiveKind::Icon).Alloc();
        ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Icon, slot};

        BindProp(ctx.arena, id, data.name,
                 [](NodeArena& a, NodeId n, const std::string& v) { a.PayloadOf<IconPayload>(n).name = v; },
                 Dirty::Layout);
        BindProp(ctx.arena, id, data.cfg.size,
                 [](NodeArena& a, NodeId n, const float& v) { a.PayloadOf<IconPayload>(n).size = v; },
                 Dirty::Layout);
        BindProp(ctx.arena, id, data.cfg.color,
                 [](NodeArena& a, NodeId n, const Color& v) { a.PayloadOf<IconPayload>(n).color = v; },
                 Dirty::Style);
        BindLayoutProps(ctx.arena, id, data.cfg.layout);
        BindInteractProps<IconPayload>(ctx, id, data.cfg.on, &IconPayload::on);
    }

    Vec2 Measure(const MeasureContext& ctx, NodeId id, AvailableSpace, AvailableSpace) override {
        if (!ctx.arena) {
            return {};
        }
        const float side = ctx.arena->PayloadOf<IconPayload>(id).size;
        return {side, side};
    }

    void OnEvent(EventContext& ctx, Event& ev) override {
        DispatchInteract(ctx, ev, ctx.Payload<IconPayload>().on);
    }
};

void EnsureKind() {
    static const bool registered = [] {
        KindRegistry::Register(LiveKind::Icon, MakeRef<IconWidget>());
        return true;
    }();
    (void)registered;
}

} // namespace

Elem Icon(Str name, IconCfg cfg) {
    EnsureKind();
    return Elem(detail::MakeBlueprint(LiveKind::Icon, IconData{std::move(name), std::move(cfg)}));
}

} // namespace lumora
