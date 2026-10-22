#include <lumora/Widgets/Controls.h>

#include "Input/Focus.h"
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
#include <lumora/Reactive/Bind.h>
#include <lumora/Reactive/Reactive.h>

#include <algorithm>
#include <string>
#include <utility>

#include "Layout/Measure.h"

namespace lumora
{
    namespace
    {
        inline constexpr float kCheckboxBox = 18.f;
        inline constexpr float kCheckboxGap = 8.f;

        struct CheckboxLocalPayload : public CheckboxPayload
        {
            Color checkColor = Color::Blue;
            Color borderColor = Color::Gray;
            Color labelColor = Color::Transparent;
        };

        class CheckboxWidget final : public TypedWidget<CheckboxCfg>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Checkbox"; }
            [[nodiscard]] bool        ContentSized() const noexcept override { return true; }

            void Build(AppCtx& ctx, CheckboxCfg& cfg, NodeId id, Owner&) override
            {
                const std::uint32_t slot = ctx.arena.Pool<CheckboxLocalPayload>(LiveKind::Checkbox).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Checkbox, slot};
                CheckboxLocalPayload& payload = ctx.arena.PayloadOf<CheckboxLocalPayload>(id);
                payload.bind = cfg.checked;
                payload.onChange = cfg.onChange;
                payload.focusPolicy = cfg.focusPolicy;
                payload.tabIndex = cfg.tabIndex;
                BindProp(ctx.arena, id, cfg.disabled,
                         [](NodeArena& a, NodeId n, const bool& v)
                         { a.PayloadOf<CheckboxLocalPayload>(n).disabled = v; },
                         Dirty::Style);
                const bool disabledAtMount = cfg.disabled.IsStatic() && cfg.disabled.Get();
                if (cfg.autofocus && !disabledAtMount)
                {
                    ctx.pendingAutofocus = id;
                }

                const Bind<bool> bound = cfg.checked;
                BindProp(ctx.arena, id, Reactive<bool>([bound] { return bound.Get(); }),
                         [](NodeArena& a, NodeId n, const bool& v) { a.PayloadOf<CheckboxLocalPayload>(n).value = v; },
                         Dirty::Style);
                BindProp(ctx.arena, id, cfg.label,
                         [](NodeArena& a, NodeId n, const std::string& v)
                         {
                             a.PayloadOf<CheckboxLocalPayload>(n).label = v;
                         },
                         Dirty::Layout);
                if (ctx.style)
                {
                    style::BindClass(ctx, id, cfg,
                        [](NodeArena& a, NodeId n, const style::ComputedStyle& cs, const CheckboxCfg& c)
                        {
                            style::ApplyCheckbox(a, n, cs, c);
                            CheckboxLocalPayload& p = a.PayloadOf<CheckboxLocalPayload>(n);
                            if (const style::CssValue* v = cs.Get("color"_sid))
                                p.labelColor = style::ToColor(*v, c.labelColor.Get());
                            else
                                p.labelColor = c.labelColor.Get();

                            if (const style::CssValue* v = cs.Get("background-color"_sid))
                                p.checkColor = style::ToColor(*v, c.checkColor.Get());
                            else
                                p.checkColor = c.checkColor.Get();

                            if (const style::CssValue* v = cs.Get("border-color"_sid))
                                p.borderColor = style::ToColor(*v, c.borderColor.Get());
                            else
                                p.borderColor = c.borderColor.Get();

                            a.Resolve(n).MarkDirty(Dirty::All);
                        });
                    style::StyleEngine* eng = ctx.style;
                    Effect([eng, id, bound] { eng->SetPseudo(id, style::Pseudo::Checked, bound.Get()); });
                }
                else
                {
                    BindLayoutProps(ctx.arena, id, cfg.layout);
                    BindProp(ctx.arena, id, cfg.checkColor,
                             [](NodeArena& a, NodeId n, const Color& v)
                             { a.PayloadOf<CheckboxLocalPayload>(n).checkColor = v; },
                             Dirty::Repaint);
                    BindProp(ctx.arena, id, cfg.borderColor,
                             [](NodeArena& a, NodeId n, const Color& v)
                             { a.PayloadOf<CheckboxLocalPayload>(n).borderColor = v; },
                             Dirty::Repaint);
                    BindProp(ctx.arena, id, cfg.labelColor,
                             [](NodeArena& a, NodeId n, const Color& v)
                             { a.PayloadOf<CheckboxLocalPayload>(n).labelColor = v; },
                             Dirty::Repaint);
                }
            }

            Vec2 Measure(const MeasureContext& ctx, NodeId id, AvailableSpace, AvailableSpace) override
            {
                if (!ctx.arena)
                {
                    return {};
                }
                const CheckboxLocalPayload& cb  = ctx.arena->PayloadOf<CheckboxLocalPayload>(id);
                const Spacing&              pad = ctx.arena->Resolve(id).style.padding;
                const Vec2                  text = (ctx.text && !cb.label.empty())
                    ? ctx.MeasureText(cb.label, FontProps{}, kControlFontSize,
                                      TextFlowProps{.maxLines = 1, .wrap = false},
                                      AvailableSpace::MaxContent())
                    : Vec2{cb.label.empty() ? 0.f : static_cast<float>(cb.label.size()) * 7.f, kCheckboxBox};
                return {text.x + kCheckboxBox + kCheckboxGap + pad.Horizontal(),
                        std::max(text.y, kCheckboxBox) + pad.Vertical()};
            }

            void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                const LiveNode& n = ctx.arena->Resolve(id);
                const CheckboxLocalPayload& cb = ctx.arena->PayloadOf<CheckboxLocalPayload>(id);
                const Rect& r = n.solved;
                const float box = std::min(r.height, kCheckboxBox);
                const Color accent = cb.disabled ? cb.checkColor.WithAlpha(110) : cb.checkColor;
                const Color empty  = cb.disabled ? cb.borderColor.WithAlpha(110) : cb.borderColor;
                canvas.DrawRect({
                    .bounds = {r.x, r.y + ((r.height - box) * 0.5f), box, box},
                    .fill = cb.value ? accent : Color::Transparent,
                    .radius = 3.f,
                    .borderColor = cb.value ? accent : empty,
                    .borderWidth = cb.value ? 0.f : 1.5f,
                    .borderRadius = 3.f
                });

                if (cb.value)
                {
                    const float pad = 4.f;
                    canvas.DrawRect({
                        .bounds = {r.x + pad, r.y + ((r.height - box) * 0.5f) + pad, box - (pad * 2.f), box - (pad * 2.f)},
                        .fill = Color::White,
                        .radius = 2.f
                    });
                }

                const Spacing labelPad{0.f, 0.f, 0.f, box + kCheckboxGap};
                Color labelCol = cb.labelColor.a != 0 ? cb.labelColor : Color::Black;
                if (cb.disabled) labelCol = labelCol.WithAlpha(110);
                EmitText(canvas, ctx, r, labelPad, cb.label, FontProps{}, kControlFontSize,
                         TextFlowProps{.maxLines = 1, .wrap = false}, labelCol, nullptr);
            }

            void OnEvent(EventContext& ctx, Event& ev) override
            {
                CheckboxLocalPayload& cb = ctx.Payload<CheckboxLocalPayload>();
                if (ctx.phase == RoutePhase::Capture || cb.disabled)
                {
                    return;
                }
                if (ev.Type == EventType::KeyUp && ev.KeyCode == Key::Space)
                {
                    const bool next = !cb.value;
                    cb.value = next;
                    if (cb.bind) cb.bind->Set(next);
                    if (cb.onChange) cb.onChange(next);
                    ctx.Node().MarkDirty(Dirty::Repaint);
                    ev.Consume();
                    return;
                }
                if (ev.Type == EventType::KeyDown && ev.KeyCode == Key::Space)
                {
                    ev.Consume();
                    return;
                }
                if (ev.Type == EventType::TextInput && ev.Text[0] == ' ' && ev.Text[1] == '\0')
                {
                    ev.Consume();
                    return;
                }
                if (ev.Button != PointerButton::Left)
                {
                    return;
                }
                if (ev.Type == EventType::MouseDown)
                {
                    cb.pressed = true;
                    ctx.Capture();
                    ev.Consume();
                }
                else if (ev.Type == EventType::MouseUp && cb.pressed)
                {
                    cb.pressed = false;
                    ctx.ReleaseCapture();
                    if (ctx.Node().solved.Contains(ctx.localPos))
                    {
                        const bool next = !cb.value;
                        cb.value = next;
                        if (cb.bind) cb.bind->Set(next);
                        if (cb.onChange) cb.onChange(next);
                        ctx.Node().MarkDirty(Dirty::Repaint);
                    }
                    ev.Consume();
                }
            }

            [[nodiscard]] bool WantsFocus(const EventContext& ctx) const override
            {
                const CheckboxLocalPayload& cb = ctx.Payload<CheckboxLocalPayload>();
                return IsFocusable(cb.focusPolicy, cb.disabled);
            }

            [[nodiscard]] FocusPolicyResult TabPolicy(const EventContext& ctx) const override
            {
                const CheckboxLocalPayload& cb = ctx.Payload<CheckboxLocalPayload>();
                return {cb.tabIndex, IsTabStop(cb.focusPolicy, cb.tabIndex, cb.disabled)};
            }

            [[nodiscard]] RingShape FocusRingShape(const PaintContext&, NodeId) const override
            {
                return {0.f, 3.f}; // ring the whole control row, matching the checkbox box corner
            }
        };

        void EnsureKind()
        {
            static const bool registered = []
            {
                KindRegistry::Register(LiveKind::Checkbox, MakeRef<CheckboxWidget>());
                return true;
            }();
            (void)registered;
        }
    } // namespace

    Elem Checkbox(CheckboxCfg cfg)
    {
        EnsureKind();
        return Elem(detail::MakeBlueprint(LiveKind::Checkbox, std::move(cfg)));
    }
} // namespace lumora
