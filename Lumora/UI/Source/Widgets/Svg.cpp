#include <lumora/Widgets/Svg.h>

#include "Layout/Measure.h"
#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"
#include "Svg/SvgDocument.h"
#include "Svg/SvgRenderer.h"
#include "Widgets/Widget.h"

#include <lumora/AssetLoader.h>
#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/Paint2D.h>

#include <string>
#include <utility>

namespace lumora
{
    namespace
    {
        class SvgWidget final : public TypedWidget<SvgCfg>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Svg"; }
            [[nodiscard]] bool ContentSized() const noexcept override { return true; }
            [[nodiscard]] bool ReflowsWidth() const noexcept override { return false; }

            void Build(AppCtx& ctx, SvgCfg& cfg, NodeId id, Owner&) override
            {
                const std::uint32_t slot = ctx.arena.Pool<SvgPayload>(LiveKind::Svg).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Svg, slot};
                auto& payload = ctx.arena.PayloadOf<SvgPayload>(id);
                payload.align = cfg.align;
                payload.meetOrSlice = cfg.meetOrSlice;

                AssetLoader loader = ctx.assets ? ctx.assets : &DefaultFileLoad;

                BindProp(ctx.arena, id, cfg.src,
                         [loader](NodeArena& a, NodeId n, const std::string& path)
                         {
                             SvgPayload& p = a.PayloadOf<SvgPayload>(n);
                             std::string bytes;
                             if (!path.empty() && loader(path, bytes))
                             {
                                 p.scene = svg::ParseSvg(bytes);
                             }
                             else
                             {
                                 p.scene = svg::SvgScene{};
                             }
                             p.cache = svg::SceneCache{};
                         },
                         Dirty::Layout);

                BindProp(ctx.arena, id, cfg.opacity,
                         [](NodeArena& a, NodeId n, const float& v) { a.PayloadOf<SvgPayload>(n).opacity = v; },
                         Dirty::Repaint);

                BindLayoutProps(ctx.arena, id, cfg.layout);
                BindViewProps<SvgPayload>(ctx.arena, id, cfg.view, &SvgPayload::view);
                BindInteractProps<SvgPayload>(ctx, id, cfg.on, &SvgPayload::on);
            }

            Vec2 Measure(const MeasureContext& ctx, NodeId id, AvailableSpace, AvailableSpace) override
            {
                if (!ctx.arena)
                {
                    return {};
                }
                const SvgPayload& p = ctx.arena->PayloadOf<SvgPayload>(id);
                if (!p.scene.valid)
                {
                    return {};
                }
                return p.scene.intrinsic;
            }

            void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                SvgPayload& p = ctx.arena->PayloadOf<SvgPayload>(id);
                if (!p.scene.valid)
                {
                    return;
                }
                const LiveNode& n = ctx.arena->Resolve(id);
                const Spacing& pad = n.style.padding;
                const Rect content{n.solved.x + pad.left, n.solved.y + pad.top,
                                   n.solved.width - pad.left - pad.right,
                                   n.solved.height - pad.top - pad.bottom};
                canvas.PushClip(content);
                Paint2D surface(ctx, canvas, content, ctx.pointerPos);
                svg::RenderScene(surface, canvas, p.scene, p.cache, content, p.align, p.meetOrSlice, p.opacity,
                                 ctx.contentScale);
                surface.Unwind();
                canvas.PopClip();
            }

            [[nodiscard]] HitTestBehavior HitTest(const EventContext& ctx, Vec2) const override
            {
                const InteractState& on = ctx.Payload<SvgPayload>().on;
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
                DispatchInteract(ctx, ev, ctx.Payload<SvgPayload>().on);
            }
        };

        void EnsureKind()
        {
            static const bool registered = []
            {
                KindRegistry::Register(LiveKind::Svg, MakeRef<SvgWidget>());
                return true;
            }();
            (void)registered;
        }
    } // namespace

    Elem Svg(SvgCfg cfg)
    {
        EnsureKind();
        return Elem(detail::MakeBlueprint(LiveKind::Svg, std::move(cfg)));
    }
} // namespace lumora
