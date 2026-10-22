#include <lumora/Dock/Dock.h>

#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"
#include "Style/StyleResolver.h"
#include "Widgets/Widget.h"

#include <lumora/Foundation/Geometry.h>
#include <lumora/Input/Event.h>
#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/DrawTypes.h>

#include <algorithm>
#include <functional>
#include <utility>

namespace lumora
{
    namespace
    {
        // Local to this TU: only the splitter uses this pool, so the payload stays out of shared Payloads.h.
        struct SplitterPayload
        {
            DockAxis axis = DockAxis::Horizontal;
            float    thickness = 6.f;
            float    minPanePx = 60.f;
            Color    color = Color::Gray;

            std::function<void(float, float)> onDrag;
            std::function<void(float, float)> onCommit;

            bool  hovered = false;
            bool  dragging = false;
            float start = 0.f;   ///< cursor coord along the axis at press
            float prevPx0 = 0.f; ///< prev pane extent (px) at press
            float nextPx0 = 0.f; ///< next pane extent (px) at press
        };

        [[nodiscard]] float AxisCoord(DockAxis axis, Vec2 p) noexcept
        {
            return axis == DockAxis::Horizontal ? p.x : p.y;
        }
        [[nodiscard]] float Extent(DockAxis axis, const Rect& r) noexcept
        {
            return axis == DockAxis::Horizontal ? r.width : r.height;
        }

        /// The prospective (prevPx, nextPx) sizes for the two panes given the live cursor coord,
        /// clamped so neither pane drops below the minimum.
        [[nodiscard]] std::pair<float, float> SplitPx(const SplitterPayload& p, float cursorAxis) noexcept
        {
            const float total = p.prevPx0 + p.nextPx0;
            const float delta = cursorAxis - p.start;
            const float hi = std::max(p.minPanePx, total - p.minPanePx);
            const float newPrev = std::clamp(p.prevPx0 + delta, p.minPanePx, hi);
            return {newPrev, total - newPrev};
        }

        class SplitterWidget final : public TypedWidget<SplitterCfg>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Splitter"; }

            void Build(AppCtx& ctx, SplitterCfg& cfg, NodeId id, Owner&) override
            {
                const std::uint32_t slot = ctx.arena.Pool<SplitterPayload>(LiveKind::Splitter).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Splitter, slot};

                SplitterPayload& p = ctx.arena.PayloadOf<SplitterPayload>(id);
                p.axis = cfg.axis;
                p.thickness = cfg.thickness;
                p.minPanePx = cfg.minPanePx;
                p.onDrag = std::move(cfg.onDrag);
                p.onCommit = std::move(cfg.onCommit);

                LiveNode& node = ctx.arena.Resolve(id);
                if (cfg.axis == DockAxis::Horizontal)
                {
                    node.style.width = Fixed(cfg.thickness);
                    node.style.height = Expand;
                }
                else
                {
                    node.style.width = Expand;
                    node.style.height = Fixed(cfg.thickness);
                }

                if (ctx.style)
                {
                    style::BindClass(ctx, id, cfg,
                        [](NodeArena& a, NodeId n, const style::ComputedStyle& cs, const SplitterCfg& c)
                        {
                            SplitterPayload& sp = a.PayloadOf<SplitterPayload>(n);
                            if (const style::CssValue* v = cs.Get("background-color"_sid))
                                sp.color = style::ToColor(*v, c.color.Get());
                            else if (const style::CssValue* v2 = cs.Get("color"_sid))
                                sp.color = style::ToColor(*v2, c.color.Get());
                            else
                                sp.color = c.color.Get();
                            a.Resolve(n).MarkDirty(Dirty::Repaint);
                        });
                }
                else
                {
                    BindProp(ctx.arena, id, cfg.color,
                             [](NodeArena& a, NodeId n, const Color& v) { a.PayloadOf<SplitterPayload>(n).color = v; },
                             Dirty::Repaint);
                }
            }

            void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                const LiveNode& n = ctx.arena->Resolve(id);
                const SplitterPayload& p = ctx.arena->PayloadOf<SplitterPayload>(id);
                const Color fill = (p.hovered || p.dragging) ? p.color.Lighten(0.4f) : p.color;
                canvas.DrawRect({.bounds = n.solved, .fill = fill});
            }

            void OnEvent(EventContext& ctx, Event& ev) override
            {
                SplitterPayload& p = ctx.Payload<SplitterPayload>();
                switch (ev.Type)
                {
                    case EventType::MouseEnter:
                        p.hovered = true;
                        ctx.Node().MarkDirty(Dirty::Repaint);
                        break;
                    case EventType::MouseLeave:
                        p.hovered = false;
                        ctx.Node().MarkDirty(Dirty::Repaint);
                        break;
                    case EventType::MouseDown:
                        if (ev.Button == PointerButton::Left)
                        {
                            const LiveNode& self = ctx.Node();
                            const LiveNode* prev = ctx.arena->TryResolve(self.prevSibling);
                            const LiveNode* next = ctx.arena->TryResolve(self.nextSibling);
                            if (prev && next)
                            {
                                p.dragging = true;
                                p.start = AxisCoord(p.axis, ev.Position);
                                p.prevPx0 = Extent(p.axis, prev->solved);
                                p.nextPx0 = Extent(p.axis, next->solved);
                                ctx.Capture();
                                ctx.Node().MarkDirty(Dirty::Repaint);
                                ev.Consume();
                            }
                        }
                        break;
                    case EventType::MouseUp:
                        if (ev.Button == PointerButton::Left && p.dragging)
                        {
                            p.dragging = false;
                            ctx.ReleaseCapture();
                            if (p.onCommit)
                            {
                                const auto [aPx, bPx] = SplitPx(p, AxisCoord(p.axis, ev.Position));
                                p.onCommit(aPx, bPx);
                            }
                            ctx.Node().MarkDirty(Dirty::Repaint);
                            ev.Consume();
                        }
                        break;
                    case EventType::MouseMove:
                        if (p.dragging)
                        {
                            if (p.onDrag)
                            {
                                const auto [aPx, bPx] = SplitPx(p, AxisCoord(p.axis, ev.Position));
                                p.onDrag(aPx, bPx);
                            }
                            ev.Consume();
                        }
                        break;
                    default:
                        break;
                }
            }
        };

        void EnsureKind()
        {
            static const bool registered = []
            {
                KindRegistry::Register(LiveKind::Splitter, MakeRef<SplitterWidget>());
                return true;
            }();
            (void)registered;
        }
    } // namespace

    Elem Splitter(SplitterCfg cfg)
    {
        EnsureKind();
        return Elem(detail::MakeBlueprint(LiveKind::Splitter, std::move(cfg)));
    }
} // namespace lumora
