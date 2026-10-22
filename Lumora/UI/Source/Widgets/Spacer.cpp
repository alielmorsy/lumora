#include <lumora/Widgets/Misc.h>

#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Widgets/Widget.h"

#include <lumora/Foundation/Geometry.h>

namespace lumora
{
    namespace
    {
        struct SpacerData
        {
            float size = 0.f;
        };

        class SpacerWidget final : public TypedWidget<SpacerData>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Spacer"; }

            void Build(AppCtx& ctx, SpacerData& data, NodeId id, Owner&) override
            {
                const std::uint32_t slot = ctx.arena.Pool<SpacerPayload>(LiveKind::Spacer).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Spacer, slot};
                SpacerPayload& payload = ctx.arena.PayloadOf<SpacerPayload>(id);
                payload.size = data.size;
                payload.flexible = data.size <= 0.f;

                LayoutStyle& style = ctx.arena.Resolve(id).style;
                if (payload.flexible)
                {
                    style.flexWeight = 1.f; // a flexible Spacer is a grow-weighted gap
                }
                else
                {
                    style.width = Fixed(payload.size);
                    style.height = Fixed(payload.size);
                }
            }
        };

        void EnsureKind()
        {
            static const bool registered = []
            {
                KindRegistry::Register(LiveKind::Spacer, MakeRef<SpacerWidget>());
                return true;
            }();
            (void)registered;
        }
    } // namespace

    Elem Spacer(float size)
    {
        EnsureKind();
        return Elem(detail::MakeBlueprint(LiveKind::Spacer, SpacerData{size}));
    }
} // namespace lumora
