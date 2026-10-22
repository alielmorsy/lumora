#pragma once

#include <lumora/Foundation/NodeId.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Input/Event.h>
#include <lumora/Style/Stylesheet.h> // style::Pseudo
#include <lumora/Widgets/Props.h>

#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"

#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace lumora
{
    class TextMeasurer;
    class Clipboard;
    class OverlayRoot;
    class DragHost;
    struct ContextMenuHost;
    struct InteractionState;

    namespace style
    {
        class StyleEngine;
    }

    /// The double-click window in seconds; same value as `Input.cpp`'s word/line selection so both recognisers agree.
    inline constexpr float kDoubleClickSeconds = 0.4f;
    inline constexpr float kDoubleClickSlop    = 4.f;

    /// Sets a node's CSS pseudo-state signal through the app's StyleEngine (no-op if none is wired or the node is unstyled).
    void WritePseudo(InteractionState& state, NodeId node, style::Pseudo p, bool on);

    /// Pops a context menu at `anchor` through the app's host; declared here so DispatchInteract avoids including the host header.
    void OpenContextMenu(InteractionState* state, Vec2 anchor, std::vector<ContextMenuItem> items);

    /// Hit-testing mode: Opaque blocks nodes behind it (the interactive default); Translucent is reserved (acts as Opaque);
    /// Ignore opts the node out as a target but still tests its children.
    enum class HitTestBehavior : std::uint8_t { Opaque, Translucent, Ignore };

    /// Where a node sits on a capture -> target -> bubble route; built-ins act on Target/Bubble, Capture is for ancestors/Components intercepting early.
    enum class RoutePhase : std::uint8_t { Capture, Target, Bubble };

    /// A node reference that survives slot recycling: pairs the NodeId with its generation, so a reused slot resolves to null.
    struct NodeRef
    {
        NodeId        id{};
        std::uint32_t gen = 0;

        NodeRef() = default;
        NodeRef(NodeArena& arena, NodeId i) noexcept : id(i), gen(arena.GenerationOf(i)) {}

        [[nodiscard]] NodeId Resolve(NodeArena& arena) const noexcept
        {
            return (id.Valid() && arena.TryResolve(id) && arena.GenerationOf(id) == gen) ? id : NodeId{};
        }

        [[nodiscard]] bool operator==(const NodeRef& other) const noexcept
        {
            return id == other.id && gen == other.gen;
        }
    };

    using HitPath = std::vector<NodeRef>;

    /// The dispatcher's per-app working set, owned by MountedApp. Hover/focus/captured are stale-safe refs; the scratch buffers are
    /// reused so steady-state dispatch allocates nothing.
    struct InteractionState
    {
        NodeRef hovered;
        NodeRef focused;
        NodeRef captured;
        Vec2    pointerPos{};
        Vec2    pointerDelta{};
        /// Monotonic seconds, advanced once per `MountedApp::Tick`; the only clock a dispatch handler can read (containers get no OnFrame).
        float   clock = 0.f;
        bool    focusVisible = false; ///< true only while focus last moved by keyboard - drives the ring
        bool    focusTakesText = false; ///< the focused node edits text; hosts show the soft keyboard / IME while set
        style::StyleEngine* style = nullptr; ///< for writing CSS pseudo-state signals (:hover/:active/:focus)

        const TextMeasurer* fonts    = nullptr;
        Clipboard*          clip     = nullptr;
        ContextMenuHost*    menu     = nullptr;
        OverlayRoot*        overlays = nullptr;
        DragHost*           drag     = nullptr;

        HitPath hitScratch;
        HitPath keyScratch;
        HitPath hoveredPath; ///< root->leaf chain currently under the pointer, for enter/leave diffing

        void Capture(NodeId id, NodeArena& arena) noexcept { captured = NodeRef{arena, id}; }
        void ReleaseCapture() noexcept { captured = NodeRef{}; }
    };

    /// What a Widget's OnEvent/HitTest receives: node, arena, interaction state, font/clipboard seams, and the routing phase.
    struct EventContext
    {
        NodeId              node{};
        NodeArena*          arena = nullptr;
        InteractionState*   state = nullptr;
        const TextMeasurer* fonts = nullptr;
        Clipboard*          clip  = nullptr;
        RoutePhase          phase = RoutePhase::Target;
        /// The pointer mapped into this node's local space (ancestor transforms undone, Scroll offsets applied). Use it, not ev.Position,
        /// for release-inside hit tests so clicks land correctly under a transform or inside a scroll.
        Vec2                localPos{};

        template <typename T>
        [[nodiscard]] T& Payload() const { return arena->PayloadOf<T>(node); }
        [[nodiscard]] LiveNode& Node() const { return arena->Resolve(node); }

        void Capture() const noexcept { state->Capture(node, *arena); }
        void ReleaseCapture() const noexcept { state->ReleaseCapture(); }
    };

    [[nodiscard]] inline PointerInfo MakePointerInfo(const Event& ev, const InteractionState& state)
    {
        return PointerInfo{
            .position  = ev.Position,
            .delta     = state.pointerDelta,
            .button    = static_cast<int>(ev.Button),
            .pressed   = ev.Type == EventType::MouseDown,
            .modifiers = ev.Mods,
        };
    }

    /// Shared interaction behaviour for kinds with an InteractState: press-capture (a release inside the rect is a click), hover,
    /// onMove/onScroll. Runs in Target/Bubble only; a node with no matching handler leaves the event unconsumed so it bubbles.
    inline void DispatchInteract(EventContext& ctx, Event& ev, InteractState& on)
    {
        if (on.disabled)
        {
            // Clear any latched hover/active so a node disabled mid-interaction doesn't stay :hover/:active.
            if (on.hovered || on.pressed)
            {
                on.hovered = false;
                on.pressed = false;
                WritePseudo(*ctx.state, ctx.node, style::Pseudo::Hover, false);
                WritePseudo(*ctx.state, ctx.node, style::Pseudo::Active, false);
                ctx.Node().MarkDirty(Dirty::Repaint);
            }
            return;
        }
        if (ctx.phase == RoutePhase::Capture)
        {
            return;
        }
        switch (ev.Type)
        {
            case EventType::MouseEnter:
                on.hovered = true;
                WritePseudo(*ctx.state, ctx.node, style::Pseudo::Hover, true);
                if (on.onEnter) on.onEnter();
                ctx.Node().MarkDirty(Dirty::Repaint);
                break;
            case EventType::MouseLeave:
                on.hovered = false;
                on.pressed = false;
                WritePseudo(*ctx.state, ctx.node, style::Pseudo::Hover, false);
                WritePseudo(*ctx.state, ctx.node, style::Pseudo::Active, false);
                if (on.onLeave) on.onLeave();
                ctx.Node().MarkDirty(Dirty::Repaint);
                break;
            case EventType::MouseDown:
                if (ev.Button == PointerButton::Left)
                {
                    // :active tracks the press on any node, independent of whether a handler is set.
                    WritePseudo(*ctx.state, ctx.node, style::Pseudo::Active, true);
                }
                // `onRelease` latches the press too; otherwise a release-only node never sets `pressed` and the MouseUp arm (gated on it) is silently dead.
                // It is also the only shape a draggable row can use: selecting on release never changes the selection before a drag arms, since an armed drag consumes the MouseUp.
                if (ev.Button == PointerButton::Left &&
                    (on.onClick || on.onPress || on.onDoubleClick || on.onRelease))
                {
                    on.pressed = true;
                    ctx.Capture();
                    if (on.onPress) on.onPress(MakePointerInfo(ev, *ctx.state));
                    ctx.Node().MarkDirty(Dirty::Repaint);
                    ev.Consume();
                }
                else if (ev.Button == PointerButton::Middle && on.middleButton && on.onPress)
                {
                    // Opt-in only, no :active and no click recognition (a middle press is a drag gesture). Capture so moves and the release route back here even outside the rect.
                    on.middlePressed = true;
                    ctx.Capture();
                    on.onPress(MakePointerInfo(ev, *ctx.state));
                    ctx.Node().MarkDirty(Dirty::Repaint);
                    ev.Consume();
                }
                else if (ev.Button == PointerButton::Right && on.rightButton && on.onPress)
                {
                    // Same shape as middle, ahead of the context menu so an opted-in node gets the gesture and can still pop its own menu on a release that never dragged.
                    on.rightPressed = true;
                    ctx.Capture();
                    on.onPress(MakePointerInfo(ev, *ctx.state));
                    ctx.Node().MarkDirty(Dirty::Repaint);
                    ev.Consume();
                }
                else if (ev.Button == PointerButton::Right && on.contextMenu)
                {
                    OpenContextMenu(ctx.state, ev.Position, on.contextMenu());
                    ev.Consume();
                }
                break;
            case EventType::MouseUp:
                if (ev.Button == PointerButton::Middle && on.middlePressed)
                {
                    on.middlePressed = false;
                    ctx.ReleaseCapture();
                    if (on.onRelease) on.onRelease(MakePointerInfo(ev, *ctx.state));
                    ctx.Node().MarkDirty(Dirty::Repaint);
                    ev.Consume();
                }
                else if (ev.Button == PointerButton::Right && on.rightPressed)
                {
                    on.rightPressed = false;
                    ctx.ReleaseCapture();
                    if (on.onRelease) on.onRelease(MakePointerInfo(ev, *ctx.state));
                    ctx.Node().MarkDirty(Dirty::Repaint);
                    ev.Consume();
                }
                else if (ev.Button == PointerButton::Left)
                {
                    WritePseudo(*ctx.state, ctx.node, style::Pseudo::Active, false);
                    if (on.pressed)
                    {
                        on.pressed = false;
                        ctx.ReleaseCapture();
                        if (on.onRelease) on.onRelease(MakePointerInfo(ev, *ctx.state));
                        if (ctx.Node().solved.Contains(ctx.localPos))
                        {
                            if (on.onClick) on.onClick();
                            if (on.onDoubleClick)
                            {
                                const float now = ctx.state->clock;
                                const bool  dbl =
                                    (now - on.lastClickTime) < kDoubleClickSeconds &&
                                    std::abs(ev.Position.x - on.lastClickPos.x) < kDoubleClickSlop &&
                                    std::abs(ev.Position.y - on.lastClickPos.y) < kDoubleClickSlop;
                                // Consumed on a hit, so a triple click opens a fresh pair instead of firing twice.
                                on.lastClickTime = dbl ? -1.f : now;
                                on.lastClickPos  = ev.Position;
                                if (dbl) on.onDoubleClick();
                            }
                        }
                        ctx.Node().MarkDirty(Dirty::Repaint);
                        ev.Consume();
                    }
                }
                break;
            case EventType::MouseMove:
                if (on.onMove)
                {
                    on.onMove(MakePointerInfo(ev, *ctx.state));
                    ev.Consume();
                }
                break;
            case EventType::Scroll:
                if (on.onScroll)
                {
                    on.onScroll(ev.ScrollDelta.y);
                    ev.Consume();
                }
                break;
            case EventType::FocusGained:
                // focusVisible is written before the derived event is delivered, so it reads as "this move came from the keyboard / a focus request".
                if (on.onFocus) on.onFocus(ctx.state->focusVisible);
                break;
            case EventType::FocusLost:
                if (on.onBlur) on.onBlur();
                break;
            case EventType::KeyDown:
                // Unconsumed when the handler returns false: an unconsumed Tab runs focus traversal, anything else still bubbles.
                if (on.onKeyDown && on.onKeyDown({ev.KeyCode, ev.Mods}))
                {
                    ev.Consume();
                }
                else if ((ev.KeyCode == Key::Enter || ev.KeyCode == Key::Space) && on.onClick && ctx.phase == RoutePhase::Target)
                {
                    on.pressed = true;
                    WritePseudo(*ctx.state, ctx.node, style::Pseudo::Active, true);
                    ctx.Node().MarkDirty(Dirty::Repaint);
                    ev.Consume();
                }
                break;
            case EventType::KeyUp:
                if (on.onKeyUp && on.onKeyUp({ev.KeyCode, ev.Mods}))
                {
                    ev.Consume();
                }
                else if ((ev.KeyCode == Key::Enter || ev.KeyCode == Key::Space) && on.pressed && ctx.phase == RoutePhase::Target)
                {
                    on.pressed = false;
                    WritePseudo(*ctx.state, ctx.node, style::Pseudo::Active, false);
                    ctx.Node().MarkDirty(Dirty::Repaint);
                    if (on.onClick) on.onClick();
                    ev.Consume();
                }
                break;
            case EventType::TextInput:
                if (on.onTextInput && !Has(ev.Mods, KeyMods::Ctrl) && !Has(ev.Mods, KeyMods::Alt) &&
                    on.onTextInput(std::string_view{ev.Text}))
                {
                    ev.Consume();
                }
                break;
            default:
                break;
        }
    }
} // namespace lumora
