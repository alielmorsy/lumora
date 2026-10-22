#include <lumora/Widgets/Input.h>

#include "Input/Focus.h"
#include "Input/Interaction.h"
#include "Layout/Measure.h"
#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/ContextMenuHost.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"
#include "Style/StyleResolver.h"
#include "Widgets/TextEmit.h"
#include "Widgets/Widget.h"

#include <lumora/Clipboard.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Paint/Canvas.h>
#include <lumora/Reactive/Bind.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Text/TextMeasurer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lumora {

namespace {

constexpr float kBlinkPeriod   = 0.53f;
constexpr float kDoubleClickSec = 0.4f;

class InputWidget final : public TypedWidget<InputCfg> {
    [[nodiscard]] const char* Name() const noexcept override { return "Input"; }

    void Build(AppCtx& ctx, InputCfg& cfg, NodeId id, Owner&) override {
        const std::uint32_t slot = ctx.arena.Pool<InputPayload>(LiveKind::Input).Alloc();
        ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Input, slot};
        InputPayload& payload = ctx.arena.PayloadOf<InputPayload>(id);
        payload.bind        = cfg.value;
        payload.onChange    = cfg.onChange;
        payload.onSubmit    = cfg.onSubmit;
        payload.onCancel    = cfg.onCancel;
        payload.onBlur      = cfg.onBlur;
        payload.contextMenu = cfg.contextMenu;
        payload.multiLine   = cfg.multiLine;
        // Stored as authored; `disabled` gates it at query time (see Checkbox).
        payload.focusPolicy = cfg.focusPolicy;
        payload.tabIndex    = cfg.tabIndex;
        payload.selectOnFocus = cfg.selectOnFocus;
        BindProp(ctx.arena, id, cfg.disabled,
                 [](NodeArena& a, NodeId n, const bool& v) { a.PayloadOf<InputPayload>(n).disabled = v; },
                 Dirty::Style);
        // Static-only read, for the reason given in Checkbox::Build.
        const bool disabledAtMount = cfg.disabled.IsStatic() && cfg.disabled.Get();
        if (cfg.autofocus && !disabledAtMount) {
            ctx.pendingAutofocus = id;
        }

        const Bind<std::string> bound = cfg.value;
        BindProp(ctx.arena, id, Reactive<std::string>([bound] { return bound.Get(); }),
                 [](NodeArena& a, NodeId n, const std::string& v) { a.PayloadOf<InputPayload>(n).value = v; },
                 Dirty::Layout);
        BindProp(ctx.arena, id, cfg.placeholder,
                 [](NodeArena& a, NodeId n, const std::string& v) { a.PayloadOf<InputPayload>(n).placeholder = v; },
                 Dirty::Layout);
        if (ctx.style)
        {
            // Sole writer: the resolver owns fill/color/radius/border and layout.
            style::BindClass(ctx, id, cfg,
                             [](NodeArena& a, NodeId n, const style::ComputedStyle& cs, const InputCfg& c)
                             { style::ApplyInput(a, n, cs, c); });
        }
        else
        {
            BindProp(ctx.arena, id, cfg.color,
                     [](NodeArena& a, NodeId n, const Color& v) { a.PayloadOf<InputPayload>(n).color = v; },
                     Dirty::Repaint);
            BindLayoutProps(ctx.arena, id, cfg.layout);
            BindStyleProps<InputPayload>(ctx.arena, id, cfg.style, &InputPayload::style);
        }

        // Caret blink keyed by NodeId (the widget is stateless); owner-scoped, dropped on dispose.
        NodeArena* arena = &ctx.arena;
        OnFrame([arena, id](float dt) {
            LiveNode* node = arena->TryResolve(id);
            if (!node || !node->payload.Valid()) {
                return;
            }
            InputPayload& p = arena->PayloadOf<InputPayload>(id);
            p.clock += dt;
            if (!p.focused) {
                return;
            }
            p.blinkClock += dt;
            if (p.blinkClock >= kBlinkPeriod) {
                p.blinkClock -= kBlinkPeriod;
                p.caretVisible = !p.caretVisible;
                node->MarkDirty(Dirty::Repaint);
            }
        });
    }

    [[nodiscard]] bool ContentSized() const noexcept override { return true; }
    [[nodiscard]] bool ReflowsWidth() const noexcept override { return true; }

    // HTML <input> semantics: stable intrinsic width (~20 glyph advances, from `size`) independent of the
    // typed value, one-line height; fills the container only under cross-stretch (bridge skips the width pin).
    Vec2 Measure(const MeasureContext& ctx, NodeId id, AvailableSpace, AvailableSpace) override {
        if (!ctx.text || !ctx.arena) {
            return {};
        }
        const Spacing&      pad = ctx.arena->Resolve(id).style.padding;
        const TextFlowProps flow{.maxLines = 1, .wrap = false};
        constexpr int       kCols = 20;
        const float colW = ctx.MeasureText("0", FontProps{}, kControlFontSize, flow, AvailableSpace::MaxContent()).x;
        const float line = ctx.MeasureText("Mg", FontProps{}, kControlFontSize, flow, AvailableSpace::MaxContent()).y;
        return {colW * kCols + pad.Horizontal(), line + pad.Vertical()};
    }

    [[nodiscard]] bool WantsFocus(const EventContext& ctx) const override {
        const InputPayload& p = ctx.Payload<InputPayload>();
        return IsFocusable(p.focusPolicy, p.disabled);
    }

    [[nodiscard]] FocusPolicyResult TabPolicy(const EventContext& ctx) const override {
        const InputPayload& p = ctx.Payload<InputPayload>();
        return {p.tabIndex, IsTabStop(p.focusPolicy, p.tabIndex, p.disabled)};
    }

    [[nodiscard]] RingShape FocusRingShape(const PaintContext& ctx, NodeId id) const override {
        return {0.f, ctx.arena->PayloadOf<InputPayload>(id).style.radius};
    }

    void OnEvent(EventContext& ctx, Event& ev) override {
        if (ctx.phase == RoutePhase::Capture) {
            return;
        }
        auto& p = ctx.Payload<InputPayload>();
        switch (ev.Type) {
            case EventType::FocusGained:
                p.focused = true;
                if (p.selectOnFocus) {
                    p.editor.SelectAll();
                }
                ResetBlink(p);
                ctx.Node().MarkDirty(Dirty::Repaint);
                return;
            case EventType::FocusLost:
                p.focused = false;
                ctx.Node().MarkDirty(Dirty::Repaint);
                NotifyBlur(p);
                return;
            default:
                break;
        }
        if (p.disabled) {
            return;
        }
        switch (ev.Type) {
            case EventType::MouseDown:
            case EventType::MouseMove:
            case EventType::MouseUp:
                HandlePointer(ctx, p, ev);
                break;
            case EventType::KeyDown:
            case EventType::TextInput:
                HandleKey(ctx, p, ev);
                break;
            default:
                break;
        }
    }

    void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override {
        const LiveNode& n  = ctx.arena->Resolve(id);
        InputPayload&   in = ctx.arena->PayloadOf<InputPayload>(id);
        const Rect&     r  = n.solved;
        const StyleState& s = in.style;

        const Color borderColor = in.focused ? Color::Blue
                                  : (in.disabled ? s.border.color.WithAlpha(s.border.color.a / 2) : s.border.color);
        const float borderWidth = in.focused ? std::max(s.border.width, 1.f) : s.border.width;
        if (s.fill.a > 0 || borderWidth > 0.f) {
            canvas.DrawRect({.bounds       = r,
                             .fill         = s.fill,
                             .radius       = s.radius,
                             .borderColor  = borderColor,
                             .borderWidth  = borderWidth,
                             .borderRadius = s.radius});
        }

        const Rect            content = n.style.padding.Inset(r);
        const bool            empty   = in.value.empty();
        const TextFlowProps   flow{.maxLines = in.multiLine ? 0 : 1, .wrap = in.multiLine};
        const AvailableSpace  width   = in.multiLine ? AvailableSpace::Definite(content.width)
                                                     : AvailableSpace::MaxContent();

        // Keep the caret in view by scrolling horizontally (single-line only).
        float caretX = 0.f;
        if (ctx.fonts && !empty) {
            caretX = ctx.fonts->CaretAtOffset(in.value, FontProps{}, kControlFontSize, flow, width,
                                              in.editor.Caret()).x;
        }
        if (!in.multiLine) {
            if (caretX - in.scrollX > content.width) {
                in.scrollX = caretX - content.width;
            }
            if (caretX - in.scrollX < 0.f) {
                in.scrollX = caretX;
            }
            in.scrollX = std::max(0.f, in.scrollX);
        }

        canvas.PushClip(content);

        if (ctx.fonts && in.focused && in.editor.HasSelection() && !empty) {
            std::vector<Rect> rects;
            ctx.fonts->SelectionRects(in.value, FontProps{}, kControlFontSize, flow, width,
                                      in.editor.SelMin(), in.editor.SelMax(), rects);
            for (const Rect& sr : rects) {
                canvas.DrawRect({.bounds = {content.x - in.scrollX + sr.x, content.y, sr.width, content.height},
                                 .fill   = in.selectionColor});
            }
        }

        const std::string_view text = empty ? std::string_view{in.placeholder} : std::string_view{in.value};
        if (!text.empty()) {
            const Color tint = in.disabled ? in.color.WithAlpha(90)
                                           : (empty ? in.color.WithAlpha(110) : in.color);
            const Rect  textRect{r.x - in.scrollX, r.y, r.width, r.height};
            EmitText(canvas, ctx, textRect, n.style.padding, text, FontProps{}, kControlFontSize, flow, tint,
                     nullptr);
        }

        if (in.focused && in.caretVisible && !in.editor.HasSelection()) {
            const float cx = content.x - in.scrollX + caretX;
            canvas.DrawRect({.bounds = {cx, content.y, 1.f, content.height}, .fill = in.color});
        }

        canvas.PopClip();
    }

private:
    static void ResetBlink(InputPayload& p) {
        p.blinkClock   = 0.f;
        p.caretVisible = true;
    }

    /// Hands the text to the blur handler, last on the FocusLost path. The handler may unmount this
    /// input, so it runs off a copy and nothing touches the payload afterwards.
    static void NotifyBlur(InputPayload& p) {
        if (!p.onBlur) {
            return;
        }
        EnsureSynced(p);
        const std::function<void(std::string)> blur = p.onBlur;
        blur(p.editor.Text());
    }

    /// Adopt the payload value if changed externally; a no-op after our own commit (already mirrored).
    static void EnsureSynced(InputPayload& p) {
        if (p.editor.Text() != p.value) {
            p.editor.Reset(p.value, p.editor.Caret());
        }
    }

    static void CommitView(EventContext& ctx, InputPayload& p) {
        ResetBlink(p);
        ctx.Node().MarkDirty(Dirty::Repaint);
    }

    static void CommitText(EventContext& ctx, InputPayload& p) {
        if (p.editor.Text() != p.value) {
            if (p.bind) p.bind->Set(p.editor.Text());
            if (p.onChange) p.onChange(p.editor.Text());
            p.value = p.editor.Text();
        }
        CommitView(ctx, p);
    }

    [[nodiscard]] static std::size_t OffsetAt(const EventContext& ctx, const InputPayload& p, Vec2 pos) {
        if (!ctx.fonts) {
            return p.editor.Caret();
        }
        const LiveNode&      n       = ctx.arena->Resolve(ctx.node);
        const Rect           content = n.style.padding.Inset(n.solved);
        const TextFlowProps  flow{.maxLines = p.multiLine ? 0 : 1, .wrap = p.multiLine};
        const AvailableSpace width   = p.multiLine ? AvailableSpace::Definite(content.width)
                                                   : AvailableSpace::MaxContent();
        const Vec2           boxRel{pos.x - (content.x - p.scrollX), pos.y - content.y};
        return ctx.fonts->CaretAtPoint(p.value, FontProps{}, kControlFontSize, flow, width, boxRel).byteOffset;
    }

    void HandlePointer(EventContext& ctx, InputPayload& p, Event& ev) {
        switch (ev.Type) {
            case EventType::MouseDown: {
                EnsureSynced(p);
                const std::size_t off = OffsetAt(ctx, p, ev.Position);
                if (ev.Button == PointerButton::Right) {
                    // Right-click moves the caret unless inside an existing selection, then opens the editing menu.
                    if (!(p.editor.HasSelection() && off >= p.editor.SelMin() && off <= p.editor.SelMax())) {
                        p.editor.MoveToOffset(off, false);
                        CommitView(ctx, p);
                    }
                    if (ctx.state && ctx.state->menu) {
                        ctx.state->menu->Open(ev.Position, p.contextMenu ? p.contextMenu() : DefaultMenu(ctx, p));
                    }
                    ev.Consume();
                    return;
                }
                if (ev.Button != PointerButton::Left) {
                    return;
                }
                const bool dbl = (p.clock - p.lastClickTime) < kDoubleClickSec
                                 && std::abs(ev.Position.x - p.lastClickPos.x) < 4.f
                                 && std::abs(ev.Position.y - p.lastClickPos.y) < 4.f;
                p.clickCount     = dbl ? p.clickCount + 1 : 1;
                p.lastClickTime  = p.clock;
                p.lastClickPos   = ev.Position;
                if (p.clickCount >= 3) {
                    p.editor.SelectLineAt(off);
                } else if (p.clickCount == 2) {
                    p.editor.SelectWordAt(off);
                } else {
                    p.editor.MoveToOffset(off, Has(ev.Mods, KeyMods::Shift));
                }
                p.dragging = p.clickCount == 1;
                if (p.dragging) {
                    ctx.Capture();
                }
                CommitView(ctx, p);
                ev.Consume();
                break;
            }
            case EventType::MouseMove:
                if (p.dragging) {
                    EnsureSynced(p);
                    p.editor.MoveToOffset(OffsetAt(ctx, p, ev.Position), true);
                    CommitView(ctx, p);
                    ev.Consume();
                }
                break;
            case EventType::MouseUp:
                if (p.dragging) {
                    p.dragging = false;
                    ctx.ReleaseCapture();
                    ev.Consume();
                }
                break;
            default:
                break;
        }
    }

    static void HandleKey(EventContext& ctx, InputPayload& p, Event& event) {
        EnsureSynced(p);
        const bool ctrl  = Has(event.Mods, KeyMods::Ctrl) || Has(event.Mods, KeyMods::Super);
        const bool shift = Has(event.Mods, KeyMods::Shift);

        if (event.Type == EventType::TextInput) {
            if (ctrl || Has(event.Mods, KeyMods::Alt)) {
                return; // a shortcut, not text to insert
            }
            if (p.editor.Insert(event.Text)) {
                CommitText(ctx, p);
            }
            event.Consume();
            return;
        }

        switch (event.KeyCode) {
            case Key::Backspace:
                (ctrl ? p.editor.DeleteWordBack() : p.editor.Backspace());
                CommitText(ctx, p);
                event.Consume();
                break;
            case Key::Delete:
                (ctrl ? p.editor.DeleteWordForward() : p.editor.DeleteForward());
                CommitText(ctx, p);
                event.Consume();
                break;
            case Key::Left:
                (ctrl ? p.editor.MoveWordLeft(shift) : p.editor.MoveLeft(shift));
                CommitView(ctx, p);
                event.Consume();
                break;
            case Key::Right:
                (ctrl ? p.editor.MoveWordRight(shift) : p.editor.MoveRight(shift));
                CommitView(ctx, p);
                event.Consume();
                break;
            case Key::Home:
                (ctrl ? p.editor.MoveToOffset(0, shift) : p.editor.MoveHome(shift));
                CommitView(ctx, p);
                event.Consume();
                break;
            case Key::End:
                (ctrl ? p.editor.MoveToOffset(p.editor.Text().size(), shift) : p.editor.MoveEnd(shift));
                CommitView(ctx, p);
                event.Consume();
                break;
            case Key::Enter:
                if (p.multiLine) {
                    if (p.editor.Insert("\n")) CommitText(ctx, p);
                } else if (p.onSubmit) {
                    p.onSubmit(p.editor.Text());
                }
                event.Consume();
                break;
            case Key::Escape:
                if (p.onCancel) {
                    p.onCancel();
                    event.Consume();
                }
                break;
            case Key::A:
                if (ctrl) {
                    p.editor.SelectAll();
                    CommitView(ctx, p);
                    event.Consume();
                }
                break;
            case Key::C:
                if (ctrl) {
                    Copy(ctx.clip, p);
                    event.Consume();
                }
                break;
            case Key::X:
                if (ctrl) {
                    if (Cut(ctx.clip, p)) CommitText(ctx, p);
                    event.Consume();
                }
                break;
            case Key::V:
                if (ctrl) {
                    if (Paste(ctx.clip, p)) CommitText(ctx, p);
                    event.Consume();
                }
                break;
            case Key::Z:
                if (ctrl) {
                    (shift ? p.editor.Redo() : p.editor.Undo());
                    CommitText(ctx, p);
                    event.Consume();
                }
                break;
            case Key::Y:
                if (ctrl) {
                    p.editor.Redo();
                    CommitText(ctx, p);
                    event.Consume();
                }
                break;
            default:
                // A printable key belongs to the focused field even if the Text event inserts nothing; unconsumed it
                // would bubble out and fire bare-letter shortcuts. Unhandled chords (Ctrl+S, Alt+x) still go up.
                if (!ctrl && !Has(event.Mods, KeyMods::Alt) && IsTextKey(event.KeyCode)) {
                    event.Consume();
                }
                break;
        }
    }

    static void Copy(Clipboard* clip, const InputPayload& p) {
        if (clip && p.editor.HasSelection()) {
            clip->SetText(p.editor.SelectedText());
        }
    }

    static bool Cut(Clipboard* clip, InputPayload& p) {
        if (!p.editor.HasSelection()) {
            return false;
        }
        if (clip) {
            clip->SetText(p.editor.SelectedText());
        }
        return p.editor.DeleteSelection();
    }

    static bool Paste(Clipboard* clip, InputPayload& p) {
        if (!clip) {
            return false;
        }
        std::string text = clip->GetText();
        if (text.empty()) {
            return false;
        }
        if (!p.multiLine) {
            std::replace_if(text.begin(), text.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
        }
        return p.editor.Insert(text);
    }

    enum class InputMenuOp { Cut, Copy, Paste, SelectAll };

    // Run a menu op against the input resolved from `ref`. Runs deferred without an EventContext, so it
    // captures only stable handles; a no-op if the input was unmounted.
    static void ApplyInputMenuOp(NodeArena& arena, Clipboard* clip, NodeRef ref, InputMenuOp op) {
        const NodeId id = ref.Resolve(arena);
        if (!id.Valid()) {
            return;
        }
        LiveNode* node = arena.TryResolve(id);
        if (!node || !node->payload.Valid()) {
            return;
        }
        InputPayload& p = arena.PayloadOf<InputPayload>(id);
        if (p.disabled) {
            return;
        }
        EnsureSynced(p);
        switch (op) {
            case InputMenuOp::Cut:       Cut(clip, p);            break;
            case InputMenuOp::Copy:      Copy(clip, p);           break;
            case InputMenuOp::Paste:     Paste(clip, p);          break;
            case InputMenuOp::SelectAll: p.editor.SelectAll();    break;
        }
        if (p.editor.Text() != p.value) {
            if (p.bind)     p.bind->Set(p.editor.Text());
            if (p.onChange) p.onChange(p.editor.Text());
            p.value = p.editor.Text();
        }
        p.blinkClock   = 0.f;
        p.caretVisible = true;
        node->MarkDirty(Dirty::Repaint);
    }

    // The built-in editing menu: Cut/Copy/Paste/Select All, enabled by the current selection + clipboard.
    static std::vector<ContextMenuItem> DefaultMenu(const EventContext& ctx, const InputPayload& p) {
        NodeArena*    arena = ctx.arena;
        Clipboard*    clip  = ctx.clip;
        const NodeRef self{*ctx.arena, ctx.node};
        auto          act = [arena, clip, self](InputMenuOp op) {
            return [arena, clip, self, op] { ApplyInputMenuOp(*arena, clip, self, op); };
        };
        const bool hasSel   = p.editor.HasSelection();
        const bool canPaste = clip && !clip->GetText().empty();
        return {
            {.label = "Cut",        .shortcut = "Ctrl+X", .enabled = hasSel,   .onClick = act(InputMenuOp::Cut)},
            {.label = "Copy",       .shortcut = "Ctrl+C", .enabled = hasSel,   .onClick = act(InputMenuOp::Copy)},
            {.label = "Paste",      .shortcut = "Ctrl+V", .enabled = canPaste, .onClick = act(InputMenuOp::Paste)},
            {.separator = true},
            {.label = "Select All", .shortcut = "Ctrl+A", .onClick = act(InputMenuOp::SelectAll)},
        };
    }
};

void EnsureKind() {
    static const bool registered = [] {
        KindRegistry::Register(LiveKind::Input, MakeRef<InputWidget>());
        return true;
    }();
    (void)registered;
}

} // namespace

Elem Input(InputCfg cfg) {
    EnsureKind();
    return {detail::MakeBlueprint(LiveKind::Input, std::move(cfg))};
}

} // namespace lumora
