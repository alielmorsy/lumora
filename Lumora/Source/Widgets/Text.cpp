#include <lumora/Widgets/Text.h>

#include "Layout/Measure.h"
#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"
#include "Style/StyleResolver.h"
#include "Widgets/TextEmit.h"
#include "Widgets/Widget.h"

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Paint/Canvas.h>
#include <lumora/Text/TextMeasurer.h>

#include <string>
#include <utility>
#include <vector>

namespace lumora
{
    namespace
    {
        /// Text carries a positional content argument beyond its cfg group, so its blueprint config is this pair.
        struct TextData
        {
            Str content;
            TextCfg cfg;
        };

        class TextWidget final : public TypedWidget<TextData>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Text"; }
            [[nodiscard]] bool ContentSized() const noexcept override { return true; }
            [[nodiscard]] bool ReflowsWidth() const noexcept override { return true; }

            void Build(AppCtx& ctx, TextData& data, NodeId id, Owner&) override
            {
                const std::uint32_t slot = ctx.arena.Pool<TextPayload>(LiveKind::Text).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Text, slot};

                BindProp(ctx.arena, id, data.content,
                         [](NodeArena& a, NodeId n, const std::string& v)
                         {
                             TextPayload& p = a.PayloadOf<TextPayload>(n);
                             p.text = v;
                             ++p.contentEpoch;
                         },
                         Dirty::Layout);

                // Selection handle + identity are fixed for the node's life: a leaf changing selection would be a different leaf.
                TextPayload& selectable = ctx.arena.PayloadOf<TextPayload>(id);
                selectable.selection = data.cfg.selection;
                selectable.selectionKey = data.cfg.selectionKey;

                if (ctx.style)
                {
                    // Sole writer: the resolver owns size/color/font/flow and layout.
                    BindInteractProps<TextPayload>(ctx, id, data.cfg.on, &TextPayload::on);
                    style::BindClass(ctx, id, data.cfg,
                        [](NodeArena& a, NodeId n, const style::ComputedStyle& cs, const TextCfg& c)
                        {
                            style::ApplyText(a, n, cs, c);
                        });
                }
                else
                {
                    TextPayload& payload = ctx.arena.PayloadOf<TextPayload>(id);
                    payload.font = data.cfg.font;
                    payload.flow = data.cfg.flow;
                    BindProp(ctx.arena, id, data.cfg.size,
                             [](NodeArena& a, NodeId n, const float& v)
                             {
                                 TextPayload& p = a.PayloadOf<TextPayload>(n);
                                 p.size = v;
                                 ++p.contentEpoch;
                             },
                             Dirty::Layout);
                    BindProp(ctx.arena, id, data.cfg.color,
                             [](NodeArena& a, NodeId n, const Color& v) { a.PayloadOf<TextPayload>(n).color = v; },
                             Dirty::Style);
                    BindLayoutProps(ctx.arena, id, data.cfg.layout);
                    BindInteractProps<TextPayload>(ctx, id, data.cfg.on, &TextPayload::on);
                }
            }

            Vec2 Measure(const MeasureContext& ctx, NodeId id, AvailableSpace width, AvailableSpace) override
            {
                if (!ctx.text || !ctx.arena)
                {
                    return {};
                }
                TextPayload& payload = ctx.arena->PayloadOf<TextPayload>(id);
                if (payload.measuredValid && payload.measuredEpoch == payload.contentEpoch
                    && payload.measuredWidth == width)
                {
                    return payload.measuredSize; // content + constraint unchanged: skip the shaping call
                }
                const Vec2 size = ctx.MeasureText(payload.text, payload.font, payload.size, payload.flow, width);
                payload.measuredValid = true;
                payload.measuredEpoch = payload.contentEpoch;
                payload.measuredWidth = width;
                payload.measuredSize = size;
                return size;
            }

            void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                const LiveNode& n = ctx.arena->Resolve(id);
                TextPayload& t = ctx.arena->PayloadOf<TextPayload>(id);
                // Wrap into the solved box only when the wrap-aware re-measure sized this node from a definite width
                // (n.wrapInBox); a Wrap node under a shrink-to-fit parent stays single-line. Wrap off: clipped at the box edge.
                const bool wrap = t.flow.wrap;
                const bool wrapToBox = wrap && n.wrapInBox;
                if (!wrap)
                {
                    canvas.PushClip(n.solved);
                }
                PaintSelection(ctx, n, t, canvas);
                EmitText(canvas, ctx, n.solved, n.style.padding, t.text, t.font, t.size, t.flow, t.color, &t,
                         Align::Start, Align::Start, wrapToBox);
                if (!wrap)
                {
                    canvas.PopClip();
                }
            }

            void OnEvent(EventContext& ctx, Event& ev) override
            {
                DispatchInteract(ctx, ev, ctx.Payload<TextPayload>().on);
            }

            /// Fill the scope's selection that lands in this leaf, behind the glyphs; the gesture belongs to TextSelectionScope.
            static void PaintSelection(PaintContext& ctx, const LiveNode& n, const TextPayload& t,
                                       Canvas& canvas)
            {
                if (!t.selection || !ctx.fonts || t.text.empty())
                {
                    return;
                }
                const auto range = t.selection->RangeIn(t.selectionKey, t.text.size());
                if (!range)
                {
                    return;
                }
                const TextRunGeometry run = TextRunGeometryOf(n, t);
                std::vector<Rect> rects;
                ctx.fonts->SelectionRects(t.text, t.font, t.size, t.flow, run.width, range->first,
                                          range->second, rects);
                for (const Rect& sr : rects)
                {
                    canvas.DrawRect({.bounds = {run.pen.x + sr.x, run.pen.y + sr.y, sr.width, sr.height},
                                     .fill = t.selection->color});
                }
            }
        };

        void EnsureKind()
        {
            static const bool registered = []
            {
                KindRegistry::Register(LiveKind::Text, MakeRef<TextWidget>());
                return true;
            }();
            (void)registered;
        }
    } // namespace

    Elem Text(Str content, TextCfg cfg)
    {
        EnsureKind();
        return Elem(detail::MakeBlueprint(LiveKind::Text, TextData{std::move(content), std::move(cfg)}));
    }
} // namespace lumora
