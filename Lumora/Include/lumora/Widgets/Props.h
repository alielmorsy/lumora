#pragma once

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/Position.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Input/DragDrop.h>
#include <lumora/Input/Event.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Widgets/Anchor.h>
#include <lumora/Widgets/ContextMenu.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lumora
{
    /// A node's outline: width participates in layout (border-box), colour is painter-only. Corner rounding is StyleProps::radius.
    struct Border
    {
        float width = 0.f;
        Color color{};
        /// CSS `border-style: dashed`; the painter derives the dash from the width.
        bool dashed = false;

        float top = -1.f;
        float right = -1.f;
        float bottom = -1.f;
        float left = -1.f;

        Color topColor{};
        Color rightColor{};
        Color bottomColor{};
        Color leftColor{};
        bool hasTopColor = false;
        bool hasRightColor = false;
        bool hasBottomColor = false;
        bool hasLeftColor = false;

        bool topDashed = false;
        bool rightDashed = false;
        bool bottomDashed = false;
        bool leftDashed = false;
        bool hasTopStyle = false;
        bool hasRightStyle = false;
        bool hasBottomStyle = false;
        bool hasLeftStyle = false;

        [[nodiscard]] bool operator==(const Border&) const = default;

        [[nodiscard]] float WidthTop() const noexcept { return top >= 0.f ? top : width; }
        [[nodiscard]] float WidthRight() const noexcept { return right >= 0.f ? right : width; }
        [[nodiscard]] float WidthBottom() const noexcept { return bottom >= 0.f ? bottom : width; }
        [[nodiscard]] float WidthLeft() const noexcept { return left >= 0.f ? left : width; }

        [[nodiscard]] Color ColorTop() const noexcept { return hasTopColor ? topColor : color; }
        [[nodiscard]] Color ColorRight() const noexcept { return hasRightColor ? rightColor : color; }
        [[nodiscard]] Color ColorBottom() const noexcept { return hasBottomColor ? bottomColor : color; }
        [[nodiscard]] Color ColorLeft() const noexcept { return hasLeftColor ? leftColor : color; }

        [[nodiscard]] bool DashedTop() const noexcept { return hasTopStyle ? topDashed : dashed; }
        [[nodiscard]] bool DashedRight() const noexcept { return hasRightStyle ? rightDashed : dashed; }
        [[nodiscard]] bool DashedBottom() const noexcept { return hasBottomStyle ? bottomDashed : dashed; }
        [[nodiscard]] bool DashedLeft() const noexcept { return hasLeftStyle ? leftDashed : dashed; }

        [[nodiscard]] bool IsUniform() const noexcept
        {
            const float wt = WidthTop();
            if (WidthRight() != wt || WidthBottom() != wt || WidthLeft() != wt) return false;
            const Color ct = ColorTop();
            if (ColorRight() != ct || ColorBottom() != ct || ColorLeft() != ct) return false;
            const bool dt = DashedTop();
            if (DashedRight() != dt || DashedBottom() != dt || DashedLeft() != dt) return false;
            return true;
        }

        [[nodiscard]] bool HasBorder() const noexcept
        {
            return WidthTop() > 0.f || WidthRight() > 0.f || WidthBottom() > 0.f || WidthLeft() > 0.f;
        }

        /// Dash length in px for the painter, or 0 for a solid stroke.
        [[nodiscard]] float DashLength() const noexcept { return dashed ? std::max(width, 1.f) * 3.f : 0.f; }
        [[nodiscard]] float DashLengthTop() const noexcept { return DashedTop() ? std::max(WidthTop(), 1.f) * 3.f : 0.f; }
        [[nodiscard]] float DashLengthRight() const noexcept { return DashedRight() ? std::max(WidthRight(), 1.f) * 3.f : 0.f; }
        [[nodiscard]] float DashLengthBottom() const noexcept { return DashedBottom() ? std::max(WidthBottom(), 1.f) * 3.f : 0.f; }
        [[nodiscard]] float DashLengthLeft() const noexcept { return DashedLeft() ? std::max(WidthLeft(), 1.f) * 3.f : 0.f; }
    };

    /// Modifier-key bitset (Input/Event.h), forward-declared so PointerInfo avoids pulling in the event header.
    enum class KeyMods : std::uint8_t;

    /// Pointer state delivered to InteractProps handlers: surface-space position plus per-event delta and button.
    struct PointerInfo
    {
        Vec2 position{};
        Vec2 delta{};
        int button = 0;
        bool pressed = false;
        KeyMods modifiers{}; ///< Ctrl/Shift/Alt/Super held when the event fired; query with Has(...).
    };

    /// Keyboard state delivered to onKeyDown: the key and held modifiers. Text insertion arrives separately via onTextInput
    /// (decoded UTF-8).
    struct KeyInfo
    {
        Key     key = Key::Unknown;
        KeyMods modifiers{};
    };

    enum class ImageFit : std::uint8_t { Fill, Contain, Cover, None };

    enum class ScrollAxis : std::uint8_t { Vertical, Horizontal, Both };

    enum class ButtonVariant : std::uint8_t { Solid, Outline, Ghost, Text };

    /// Focus participation: None never focusable; Click focusable by mouse/code but skipped by Tab; Tab is a Tab stop AND
    /// click-focusable (default for controls). `disabled` overrides all to non-focusable. Containers default to None.
    enum class FocusPolicy : std::uint8_t { None, Click, Tab };

    /// Layout concern, consumed by the layout bridge. `width`/`height`/`gap` are reactive (a Signal/lambda patches and re-solves);
    /// the rest of the flex surface is structural (copied once at mount): drive it live with a `className` toggle.
    struct LayoutProps
    {
        Reactive<SizeHint> width = Wrap;
        Reactive<SizeHint> height = Wrap;
        Spacing padding{};
        Spacing margin{};
        Align main = Align::Start;
        Align cross = Align::Auto; // resolved per kind: Row→Center, Column→Stretch, else Start
        Reactive<float> gap = 0.f;

        /// Container-level flex surface.
        std::optional<FlexAxis> direction{}; ///< main axis; unset == inherit from the widget kind (Row/Column/Stack)
        FlexWrap wrap = FlexWrap::NoWrap;    ///< CSS flex-wrap
        DisplayMode display = DisplayMode::Flex; ///< CSS display: Block routes to Masharif normal-flow
        Align alignContent = Align::Stretch; ///< CSS align-content; multi-line cross distribution (with wrap)

        /// Min/max clamps (CSS min-/max-width/height). Wrap == unconstrained.
        SizeHint minWidth = Wrap;
        SizeHint minHeight = Wrap;
        SizeHint maxWidth = Wrap;
        SizeHint maxHeight = Wrap;

        /// Flex-item surface - how this node behaves as a child of a flex parent.
        float flexWeight = 0.f;        ///< CSS flex-grow (also driven by Expanded/Grow)
        float flexShrink = 0.f;        ///< CSS flex-shrink; 0 keeps the rigid overflow-not-crush model
        SizeHint flexBasis = Wrap;     ///< CSS flex-basis (Wrap == auto: size from width/height or content)
        Align alignSelf = Align::Auto; ///< CSS align-self; Auto == inherit the container's align-items
        int order = 0;                 ///< CSS order: flex position only (paint/hit-test stay tree order; use z)
    };

    /// Visual concern, consumed by the painter.
    struct StyleProps
    {
        Reactive<Color> fill = Color::Transparent;
        Reactive<Color> hoverFill = Color::Transparent; ///< drawn in place of fill while the pointer is over the node
        Reactive<float> radius = 0.f;
        Border border{};
        Reactive<float> opacity = 1.f;
    };

    /// Node-level behaviour: visibility and clipping.
    struct ViewProps
    {
        Reactive<bool> visible = true;
        bool clip = false;
    };

    /// Compositing-axis translate: pure visual, never touches layout (the solver places the node as if x/y were 0; the painter
    /// translates the subtree and hit-test subtracts it). Use for entrances, drags and 60 fps motion. scale/rotate are reserved.
    struct TransformProps
    {
        Reactive<float> x{};
        Reactive<float> y{};
    };

    /// Out-of-flow placement. `position` and `z` are structural (set once at mount); `offset` and per-edge `inset` patch
    /// reactively. `offset` is shorthand for inset.{left,top}; use insetLeft/Top/Right/Bottom for animated or per-corner pinning.
    /// An unset (auto) edge falls back to the containing block's flex alignment.
    struct PositionProps
    {
        Position position = Position::Flow;
        Vec2 offset{};
        Inset inset{};
        int z = 0;
        Reactive<float> insetLeft{}, insetTop{}, insetRight{}, insetBottom{};
    };

    /// Directional navigation overrides: explicit targets take precedence over spatial search.
    struct NavOverrides
    {
        std::optional<Anchor> up{};
        std::optional<Anchor> down{};
        std::optional<Anchor> left{};
        std::optional<Anchor> right{};
    };

    /// Interaction concern, consumed by dispatch. Handlers are copied once; only `disabled` is reactive.
    struct InteractProps
    {
        std::function<void()> onClick{};
        /// The second click of a pair on this node inside the double-click window. Fires IN ADDITION to `onClick` (already run for
        /// both presses), so a node that opens on double-click still selects on the first. A third click starts a fresh pair.
        std::function<void()> onDoubleClick{};
        std::function<void(PointerInfo)> onPress{}, onRelease{}, onMove{};
        std::function<void(float)> onScroll{};
        std::function<void()> onEnter{}, onLeave{};
        /// Right-click hook: build the items to pop at the cursor; an empty list opens nothing. The framework owns overlay,
        /// positioning and dismissal; items may nest via ContextMenuItem::submenu.
        std::function<std::vector<ContextMenuItem>()> onContextMenu{};

        /// Drag source: what this node carries. Called on the press; return nullopt for "not draggable now". Returning a payload
        /// does not consume the press; the drag arms past the movement threshold. The ghost is one app-level builder keyed on the
        /// payload (`Lumora::SetDragPreview`), not declared here: a `std::function<Elem()>` would pull Elem.h into every widget header.
        std::function<std::optional<DragPayload>()> onDragStart{};
        /// Drop target: true for accepted payloads. Called per pointer move during a drag, so keep it cheap. The innermost accepting
        /// node wins; a refusing node does not block an accepting ancestor.
        std::function<bool(const DragPayload&)> dropAccepts{};
        /// The drop. `info.position` is the surface point the pointer was released at.
        std::function<void(const DragPayload&, PointerInfo)> onDrop{};
        /// Entered (true) / left (false) this target while armed. `:drop-active` is written either way; use this for more (an
        /// insertion caret, a preview).
        std::function<void(const DragPayload&, bool)> onDragOver{};

        /// Keyboard focus arrived/left. `viaKeyboard` is true for Tab traversal or programmatic focus, false for a click (drives the
        /// focus ring). Needs a `focusPolicy` other than None.
        std::function<void(bool)> onFocus{};
        std::function<void()>     onBlur{};
        /// A key pressed while this node (or an unconsuming descendant) holds focus. Return true to consume; an unconsumed Tab runs
        /// focus traversal.
        std::function<bool(KeyInfo)> onKeyDown{};
        /// Key release while focused; the other half of a held-key latch seen through `onKeyDown`. Return true to consume.
        std::function<bool(KeyInfo)> onKeyUp{};
        /// Decoded text typed while focused (one grapheme per call, never a shortcut); return true to consume. The type-to-edit seam.
        std::function<bool(std::string_view)> onTextInput{};

        Reactive<bool> disabled = false;
        /// Receive middle-button press/release through `onPress`/`onRelease` (`PointerInfo::button` distinguishes it). Opt-in because
        /// dispatch otherwise drops middle presses; routing them everywhere would make any `onPress` react to a button it never saw.
        /// Middle never produces `onClick` or `:active`.
        bool middleButton = false;
        /// Receive right-button press/release likewise. Opt-in for the same reason; takes precedence over `onContextMenu` (the node
        /// decides on its own release which the gesture was). The press captures the pointer, so a right-drag leaving the rect keeps routing here.
        bool rightButton = false;
        /// Pointer-transparent: opts out as a hit target (events pass through) while children are still tested.
        bool hitThrough = false;
        /// Subtree pass-through: the node AND all descendants are invisible to hit-testing, even those with handlers.
        bool hitThroughSubtree = false;
        /// Bind this node as an anchor's trigger (`.ref = myAnchor`); the positioner reads its solved rect. Null unless assigned.
        Ref<detail::AnchorState> ref{};
        /// Directional navigation overrides: explicit targets take precedence over spatial search.
        NavOverrides nav{};
        /// Keyboard focus: `focusPolicy` gates focus and Tab stops; `tabIndex` overrides order (positive first ascending, 0 = tree
        /// order, negative = skipped by Tab); `autofocus` grabs focus once at first mount. All static.
        FocusPolicy focusPolicy = FocusPolicy::None;
        int         tabIndex    = 0;
        bool        autofocus   = false;
    };

    /// Typeface selection and decoration for Text.
    struct FontProps
    {
        std::string family{};
        bool bold = false;
        bool italic = false;
        bool strike = false;
        float lineHeight = 1.f;
    };

    /// Multi-line flow controls for Text.
    struct TextFlowProps
    {
        int maxLines = 0; ///< 0 == unlimited
        bool wrap = true;
        Align align = Align::Start;
        bool ellipsis = false;
    };
}
