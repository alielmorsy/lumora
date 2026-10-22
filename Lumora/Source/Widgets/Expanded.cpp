#include <lumora/Widgets/Misc.h>

#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Widgets/Widget.h"

namespace lumora {

namespace {

struct ExpandedData {
    float weight = 1.f;
};

class ExpandedWidget final : public TypedWidget<ExpandedData> {
    [[nodiscard]] const char* Name() const noexcept override { return "Expanded"; }

    void Build(AppCtx& ctx, ExpandedData& data, NodeId id, Owner&) override {
        const std::uint32_t slot = ctx.arena.Pool<ExpandedPayload>(LiveKind::Expanded).Alloc();
        ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Expanded, slot};
        ctx.arena.PayloadOf<ExpandedPayload>(id).weight = data.weight;
        ctx.arena.Resolve(id).style.flexWeight = data.weight;
    }
};

void EnsureKind() {
    static const bool registered = [] {
        KindRegistry::Register(LiveKind::Expanded, MakeRef<ExpandedWidget>());
        return true;
    }();
    (void)registered;
}

} // namespace

Elem Expanded(float weight) {
    EnsureKind();
    return Elem(detail::MakeBlueprint(LiveKind::Expanded, ExpandedData{weight}));
}

} // namespace lumora
