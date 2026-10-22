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
#include "Widgets/Widget.h"

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Paint/Canvas.h>
#include <lumora/Reactive/Bind.h>
#include <lumora/Reactive/Reactive.h>

#include <algorithm>
#include <utility>

namespace lumora {

namespace {

class SwitchWidget final : public TypedWidget<SwitchCfg> {
    [[nodiscard]] const char* Name() const noexcept override { return "Switch"; }
    [[nodiscard]] bool        ContentSized() const noexcept override { return true; }

    Vec2 Measure(const MeasureContext&, NodeId, AvailableSpace, AvailableSpace) override {
        return {40.f, 22.f};
    }

    void Build(AppCtx& ctx, SwitchCfg& cfg, NodeId id, Owner&) override {
        const std::uint32_t slot = ctx.arena.Pool<SwitchPayload>(LiveKind::Switch).Alloc();
        ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Switch, slot};
        SwitchPayload& payload = ctx.arena.PayloadOf<SwitchPayload>(id);
        payload.bind     = cfg.on;
        payload.onChange = cfg.onChange;
        payload.focusPolicy = cfg.focusPolicy;
        payload.tabIndex = cfg.tabIndex;
        if (cfg.autofocus) {
            ctx.pendingAutofocus = id;
        }

        const Bind<bool> bound = cfg.on;
        BindProp(ctx.arena, id, Reactive<bool>([bound] { return bound.Get(); }),
                 [](NodeArena& a, NodeId n, const bool& v) { a.PayloadOf<SwitchPayload>(n).value = v; },
                 Dirty::Style);
        if (ctx.style)
        {
            style::BindClass(ctx, id, cfg,
                [](NodeArena& a, NodeId n, const style::ComputedStyle& cs, const SwitchCfg& c)
                {
                    style::ApplySwitch(a, n, cs, c);
                });
            style::StyleEngine* eng = ctx.style;
            Effect([eng, id, bound] { eng->SetPseudo(id, style::Pseudo::Checked, bound.Get()); });
        }
        else
        {
            BindProp(ctx.arena, id, cfg.activeColor,
                     [](NodeArena& a, NodeId n, const Color& v) { a.PayloadOf<SwitchPayload>(n).activeColor = v; },
                     Dirty::Style);
            BindLayoutProps(ctx.arena, id, cfg.layout);
        }
    }

    void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override {
        const Rect           r  = ctx.arena->Resolve(id).solved;
        const SwitchPayload& sw = ctx.arena->PayloadOf<SwitchPayload>(id);
        const float          radius = r.height * 0.5f;
        const Color          track  = sw.value ? sw.activeColor : Color::Rgba(90, 96, 108, 255);
        canvas.DrawRect({.bounds = r, .fill = track, .radius = radius});

        const float knob  = std::max(0.f, r.height - 6.f);
        const float knobX = sw.value ? (r.x + r.width - knob - 3.f) : (r.x + 3.f);
        canvas.DrawRect({.bounds = {knobX, r.y + 3.f, knob, knob}, .fill = Color::White, .radius = knob * 0.5f});
    }

    void OnEvent(EventContext& ctx, Event& ev) override {
        SwitchPayload& sw = ctx.Payload<SwitchPayload>();
        if (ctx.phase == RoutePhase::Capture) {
            return;
        }
        // Space toggles on key-up (native edge; defeats auto-repeat). Swallow the key-down + space char.
        if (ev.Type == EventType::KeyUp && ev.KeyCode == Key::Space) {
            const bool next = !sw.value;
            sw.value = next;
            if (sw.bind) sw.bind->Set(next);
            if (sw.onChange) sw.onChange(next);
            ctx.Node().MarkDirty(Dirty::Repaint);
            ev.Consume();
            return;
        }
        if (ev.Type == EventType::KeyDown && ev.KeyCode == Key::Space) {
            ev.Consume();
            return;
        }
        if (ev.Type == EventType::TextInput && ev.Text[0] == ' ' && ev.Text[1] == '\0') {
            ev.Consume();
            return;
        }
        if (ev.Button != PointerButton::Left) {
            return;
        }
        if (ev.Type == EventType::MouseDown) {
            sw.pressed = true;
            ctx.Capture();
            ev.Consume();
        } else if (ev.Type == EventType::MouseUp && sw.pressed) {
            sw.pressed = false;
            ctx.ReleaseCapture();
            if (ctx.Node().solved.Contains(ctx.localPos)) {
                const bool next = !sw.value;
                sw.value = next; // optimistic: an accessor Bind isn't reactive, so refresh the cache here
                if (sw.bind) sw.bind->Set(next);
                if (sw.onChange) sw.onChange(next);
                ctx.Node().MarkDirty(Dirty::Repaint);
            }
            ev.Consume();
        }
    }

    [[nodiscard]] bool WantsFocus(const EventContext& ctx) const override {
        const SwitchPayload& sw = ctx.Payload<SwitchPayload>();
        return IsFocusable(sw.focusPolicy, false);
    }

    [[nodiscard]] FocusPolicyResult TabPolicy(const EventContext& ctx) const override {
        const SwitchPayload& sw = ctx.Payload<SwitchPayload>();
        return {sw.tabIndex, IsTabStop(sw.focusPolicy, sw.tabIndex, false)};
    }

    [[nodiscard]] RingShape FocusRingShape(const PaintContext& ctx, NodeId id) const override {
        return {0.f, ctx.arena->Resolve(id).solved.height * 0.5f}; // pill
    }
};

void EnsureKind() {
    static const bool registered = [] {
        KindRegistry::Register(LiveKind::Switch, MakeRef<SwitchWidget>());
        return true;
    }();
    (void)registered;
}

} // namespace

Elem Switch(SwitchCfg cfg) {
    EnsureKind();
    return Elem(detail::MakeBlueprint(LiveKind::Switch, std::move(cfg)));
}

} // namespace lumora
