#include <lumora/Widgets/Controls.h>

#include "Input/Focus.h"
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
#include <lumora/Paint/Canvas.h>

#include <cassert>
#include <string>
#include <utility>

namespace lumora {

namespace {

/// Variant chrome used when the author left fill/colour unset (alpha 0).
struct ButtonStyle {
    Color fill;
    Color text;
    Color border;
    float borderWidth;
};

[[nodiscard]] ButtonStyle ResolveButtonStyle(ButtonVariant variant) {
    const Color accent = Color::Blue;
    switch (variant) {
        case ButtonVariant::Outline:
            return {Color::Transparent, accent, accent, 1.5f};
        case ButtonVariant::Ghost:
        case ButtonVariant::Text:
            return {Color::Transparent, accent, Color::Transparent, 0.f};
        case ButtonVariant::Solid:
        default:
            return {accent, Color::White, Color::Transparent, 0.f};
    }
}

class ButtonWidget final : public TypedWidget<ButtonCfg> {
    [[nodiscard]] const char* Name() const noexcept override { return "Button"; }
    [[nodiscard]] bool        ContentSized() const noexcept override { return true; }

    void Build(AppCtx& ctx, ButtonCfg& cfg, NodeId id, Owner&) override {
        const std::uint32_t slot = ctx.arena.Pool<ButtonPayload>(LiveKind::Button).Alloc();
        ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Button, slot};
        ctx.arena.PayloadOf<ButtonPayload>(id).variant = cfg.variant;

        assert(!(cfg.onClick && cfg.on.onClick) &&
               "Button: set onClick at the top level or in .on, not both");
        InteractProps interact = cfg.on;
        if (cfg.onClick) {
            interact.onClick = cfg.onClick;
        }
        if (interact.focusPolicy == FocusPolicy::None) {
            interact.focusPolicy = FocusPolicy::Tab; // a button is a tab stop unless the author opts out
        }
        BindProp(ctx.arena, id, cfg.label,
                 [](NodeArena& a, NodeId n, const std::string& v) { a.PayloadOf<ButtonPayload>(n).label = v; },
                 Dirty::Layout);
        if (ctx.style)
        {
            // Sole writer: the resolver owns fill/color/radius and layout.
            style::BindClass(ctx, id, cfg,
                [](NodeArena& a, NodeId n, const style::ComputedStyle& cs, const ButtonCfg& c)
                {
                    style::ApplyButton(a, n, cs, c);
                });
        }
        else
        {
            BindProp(ctx.arena, id, cfg.fill,
                     [](NodeArena& a, NodeId n, const Color& v) { a.PayloadOf<ButtonPayload>(n).fill = v; },
                     Dirty::Style);
            BindProp(ctx.arena, id, cfg.color,
                     [](NodeArena& a, NodeId n, const Color& v) { a.PayloadOf<ButtonPayload>(n).color = v; },
                     Dirty::Style);
            BindProp(ctx.arena, id, cfg.radius,
                     [](NodeArena& a, NodeId n, const float& v) { a.PayloadOf<ButtonPayload>(n).radius = v; },
                     Dirty::Style);
            BindLayoutProps(ctx.arena, id, cfg.layout);
        }
        BindInteractProps<ButtonPayload>(ctx, id, interact, &ButtonPayload::on);
    }

    // The label is painted, not a child node: measure it as one line plus padding (the
    // solver does not re-add padding to an explicit size).
    Vec2 Measure(const MeasureContext& ctx, NodeId id, AvailableSpace, AvailableSpace) override {
        if (!ctx.text || !ctx.arena) {
            return {};
        }
        const ButtonPayload& b   = ctx.arena->PayloadOf<ButtonPayload>(id);
        const Spacing&       pad = ctx.arena->Resolve(id).style.padding;
        const Vec2           text = ctx.MeasureText(b.label, FontProps{}, kControlFontSize,
                                                    TextFlowProps{.maxLines = 1, .wrap = false},
                                                    AvailableSpace::MaxContent());
        return {text.x + pad.Horizontal(), text.y + pad.Vertical()};
    }

    void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override {
        const LiveNode&      n    = ctx.arena->Resolve(id);
        const ButtonPayload& b    = ctx.arena->PayloadOf<ButtonPayload>(id);
        const ButtonStyle    vs   = ResolveButtonStyle(b.variant);
        const Color          fill = b.fill.a > 0 ? b.fill : vs.fill;   // explicit cfg value wins over the variant default
        const Color          text = b.color.a > 0 ? b.color : vs.text;
        if (fill.a > 0 || vs.borderWidth > 0.f) {
            canvas.DrawRect({.bounds       = n.solved,
                             .fill         = fill,
                             .radius       = b.radius,
                             .borderColor  = vs.border,
                             .borderWidth  = vs.borderWidth,
                             .borderRadius = b.radius});
        }
        if (b.on.pressed || b.on.hovered) {
            const Color overlay = b.on.pressed ? Color::Rgba(0, 0, 0, 40) : Color::Rgba(255, 255, 255, 28);
            canvas.DrawRect({.bounds = n.solved, .fill = overlay, .radius = b.radius, .borderRadius = b.radius});
        }
        EmitText(canvas, ctx, n.solved, n.style.padding, b.label, FontProps{}, kControlFontSize,
                 TextFlowProps{.maxLines = 1, .wrap = false}, text, nullptr, Align::Center, Align::Center);
    }

    void OnEvent(EventContext& ctx, Event& ev) override {
        InteractState& on = ctx.Payload<ButtonPayload>().on;
        if (ctx.phase != RoutePhase::Capture && !on.disabled) {
            // Enter/Space: press visual on key-down, onClick on key-up (repeats are key-downs);
            // swallow the Space char.
            if (ev.Type == EventType::KeyDown && (ev.KeyCode == Key::Enter || ev.KeyCode == Key::Space)) {
                on.pressed = true;
                ctx.Node().MarkDirty(Dirty::Repaint);
                ev.Consume();
                return;
            }
            if (ev.Type == EventType::KeyUp && (ev.KeyCode == Key::Enter || ev.KeyCode == Key::Space)) {
                on.pressed = false;
                if (on.onClick) on.onClick();
                ctx.Node().MarkDirty(Dirty::Repaint);
                ev.Consume();
                return;
            }
            if (ev.Type == EventType::TextInput && ev.Text[0] == ' ' && ev.Text[1] == '\0') {
                ev.Consume();
                return;
            }
        }
        DispatchInteract(ctx, ev, on);
    }

    [[nodiscard]] bool WantsFocus(const EventContext& ctx) const override {
        const InteractState& on = ctx.Payload<ButtonPayload>().on;
        return IsFocusable(on.focusPolicy, on.disabled);
    }

    [[nodiscard]] FocusPolicyResult TabPolicy(const EventContext& ctx) const override {
        const InteractState& on = ctx.Payload<ButtonPayload>().on;
        return {on.tabIndex, IsTabStop(on.focusPolicy, on.tabIndex, on.disabled)};
    }

    [[nodiscard]] RingShape FocusRingShape(const PaintContext& ctx, NodeId id) const override {
        return {0.f, ctx.arena->PayloadOf<ButtonPayload>(id).radius};
    }
};

void EnsureKind() {
    static const bool registered = [] {
        KindRegistry::Register(LiveKind::Button, MakeRef<ButtonWidget>());
        return true;
    }();
    (void)registered;
}

} // namespace

Elem Button(ButtonCfg cfg) {
    EnsureKind();
    return Elem(detail::MakeBlueprint(LiveKind::Button, std::move(cfg)));
}

} // namespace lumora
