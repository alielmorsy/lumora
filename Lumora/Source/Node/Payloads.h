#pragma once

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Paint/DrawTypes.h>
#include <lumora/Reactive/Bind.h>
#include <lumora/Text/Glyphs.h>
#include <lumora/Widgets/Grid.h>
#include <lumora/Widgets/Props.h>
#include <lumora/Widgets/SvgTypes.h>

#include <lumora/Widgets/TextSelection.h>

#include "Svg/SvgScene.h"
#include "Widgets/TextEditor.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lumora
{
    struct Event;

    // Per-kind out-of-line state, dense in PayloadPool<T>. Static fields are copied at bind, reactive ones patched by owner-scoped effects (BindXProps).
    // Layout/position state lives on the header LayoutStyle; paint/interaction state, including the paint-only transform, lives here.

    /// Pure-visual transform (translate, then scale/rotate about the origin), folded into a paint-space layer by the painter and
    /// inverted by hit-test; never affects the solved rect.
    struct TransformState
    {
        float x = 0.f;
        float y = 0.f;
        Vec2 paintOffset{};
        float scaleX = 1.f;
        float scaleY = 1.f;
        float rotate = 0.f;  ///< radians, clockwise (y down)
        float originX = 0.5f; ///< transform-origin as a fraction of the solved box
        float originY = 0.5f;

        [[nodiscard]] bool IsIdentity() const noexcept
        {
            return x == 0.f && y == 0.f && paintOffset.x == 0.f && paintOffset.y == 0.f && scaleX == 1.f &&
                   scaleY == 1.f && rotate == 0.f;
        }

        [[nodiscard]] bool IsTranslateOnly() const noexcept { return scaleX == 1.f && scaleY == 1.f && rotate == 0.f; }

        [[nodiscard]] float TotalX() const noexcept { return x + paintOffset.x; }
        [[nodiscard]] float TotalY() const noexcept { return y + paintOffset.y; }
    };

    struct StyleState
    {
        Color fill = Color::Transparent;
        Color hoverFill = Color::Transparent;
        float radius = 0.f;
        Border border{};
        float opacity = 1.f;
    };

    struct ViewState
    {
        bool visible = true;
        bool clip = false;
    };

    struct InteractState
    {
        std::function<void()> onClick;
        std::function<void()> onDoubleClick;
        std::function<void(PointerInfo)> onPress, onRelease, onMove;
        std::function<void(float)> onScroll;
        std::function<void()> onEnter, onLeave;
        std::function<std::vector<ContextMenuItem>()> contextMenu;

        // Drag & drop, copied at bind. A node is a drag source iff dragStart is set and a drop target iff dropAccepts is set; both may be set.
        std::function<std::optional<DragPayload>()>          dragStart;
        std::function<bool(const DragPayload&)>              dropAccepts;
        std::function<void(const DragPayload&, PointerInfo)> onDrop;
        std::function<void(const DragPayload&, bool)>        onDragOver;

        // Keyboard, copied at bind. onFocus/onBlur fire off the dispatcher-derived FocusGained/FocusLost; onKeyDown/onTextInput are reached
        // only while this node (or a descendant that left the key unconsumed) holds focus.
        std::function<void(bool)>             onFocus;
        std::function<void()>                 onBlur;
        std::function<bool(KeyInfo)>          onKeyDown;
        std::function<bool(KeyInfo)>          onKeyUp;
        std::function<bool(std::string_view)> onTextInput;

        bool disabled = false;
        bool middleButton = false; ///< opt in to middle-button press/release (see InteractProps)
        bool rightButton = false;  ///< opt in to right-button press/release, ahead of the context menu
        bool hitThrough = false; ///< opt out as a hit target (events pass through), still test children
        bool hitThroughSubtree = false; ///< opt out node and all descendants from hit-testing

        // Keyboard focus (static): policy gates click + Tab focus, tabIndex overrides Tab order. Default None so containers never grab focus.
        FocusPolicy focusPolicy = FocusPolicy::None;
        int         tabIndex    = 0;
        NavOverrides nav{};

        // Transient dispatch state: pressed = held primary button (press-capture, so a release inside the rect is a click); hovered drives
        // pointer-over visuals; dropActive is the latched :drop-active state, kept here so a target left mid-drag is cleared from one place.
        bool pressed = false;
        /// Held middle button, tracked apart from `pressed` so a middle press then left release is not mistaken for a click.
        bool middlePressed = false;
        /// Held right button, for the same reason.
        bool rightPressed = false;
        bool hovered = false;
        bool dropActive = false;

        // Double-click recognition is per node, so a click on a sibling cannot see this timestamp (no lastClickNode compare needed). Same thresholds as InputPayload.
        float lastClickTime = -1.f;
        Vec2  lastClickPos{};
    };

    // Reached by the dispatcher for a transparent Component node's OnEvent: the erased instance and event thunk, copied from the blueprint at instantiation.
    struct ComponentPayload
    {
        void* instance = nullptr;
        void (*event)(void*, Event&) = nullptr;
    };

    struct ContainerPayload
    {
        StyleState style;
        ViewState view;
        TransformState transform;
        InteractState on;
        bool focusScope = false;
        NodeId lastFocusedId{};
        std::uint32_t lastFocusedGen = 0;
        float contentScale = 1.f;
    };

    struct GridPayload
    {
        std::vector<TrackSize> columns; ///< column track template
        std::vector<TrackSize> rows;    ///< explicit row track template (rows beyond it use autoRows)
        float colGap = 0.f;
        float rowGap = 0.f;
        TrackSize autoRows = TrackSize::MakeFr(1.f); ///< size for implicit rows past the template

        // What the placement pass last sized against, so an unchanged grid converges with no extra solve, while a new assigned size or changed child set forces a re-place.
        float lastContentW = -1.f;
        float lastContentH = -1.f;
        std::uint64_t lastItemsHash = 0;

        StyleState style;
        ViewState view;
        TransformState transform;
        InteractState on;
    };

    struct ScrollPayload
    {
        ScrollAxis axis = ScrollAxis::Vertical;
        float offsetX = 0.f;
        float offsetY = 0.f;

        bool draggingX = false; ///< horizontal thumb captured
        bool draggingY = false; ///< vertical thumb captured
        float grabOffset = 0.f; ///< axis offset (px) recorded at MouseDown
        Vec2 grabCursor{};      ///< pointer pos (surface space) recorded at MouseDown
        bool hovered = false;   ///< pointer anywhere in the viewport
        bool hoverX = false;    ///< pointer over the horizontal thumb (highlight)
        bool hoverY = false;    ///< pointer over the vertical thumb (highlight)

        /// The offset the content below was last RECORDED at. Clip scissors are surface space, so a clipper in the content bakes it into its recorded rect;
        /// the emit pass compares it to decide whether the content may be republished from cache.
        float paintedOffsetX = 0.f;
        float paintedOffsetY = 0.f;
        bool  paintedOffsetValid = false;

        StyleState style;
        ViewState view;
        TransformState transform;
        InteractState on;
    };

    struct TextPayload
    {
        std::string text;
        float size = 14.f;
        Color color = Color::Black;
        FontProps font;
        TextFlowProps flow;
        InteractState on;

        // Read-only selection shared with the enclosing TextSelectionScope (which owns the gesture); null for most text, so paint short-circuits.
        Ref<TextSelection> selection;
        std::uint64_t selectionKey = 0;

        // Measure memoisation (see MeasureText): `contentEpoch` bumps when a measurable input (text/size) changes; `font` is set once at instantiate.
        // A node re-laid-out for another reason (visibility, position, reorder) hits the cache and skips shaping.
        std::uint32_t contentEpoch = 0;
        std::uint32_t measuredEpoch = 0;
        bool measuredValid = false;
        AvailableSpace measuredWidth{};
        Vec2 measuredSize{};

        // Paint-geometry memoisation (see EmitText): pen-relative glyph quads keyed by the same (contentEpoch, width) signature as the measure memo;
        // rebuilt only when text/size or wrap width change.
        bool glyphsValid = false;
        std::uint32_t glyphsEpoch = 0;
        std::uint64_t glyphsGen = 0; // measurer Generation() the cache was built against (DPI re-raster)
        AvailableSpace glyphsWidth{};
        std::vector<CachedGlyph> coverageGlyphs;
        std::vector<CachedGlyph> colorGlyphs;
    };

    struct ImagePayload
    {
        std::string src;          // bound from cfg.src
        ImageHandle handle{};     // bound from cfg.handle (direct, borrowed - never released here)
        ImageHandle acquired{};   // last handle Acquire()d for `heldSrc` (provider-owned)
        ImageHandle resolved{};   // what Paint draws: handle if Valid() else acquired
        std::string heldSrc;      // the key this node holds an outstanding Acquire ref on ("" = none)
        ImageFit fit = ImageFit::Contain;
        float radius = 0.f;
        float opacity = 1.f;
        ViewState view;
        InteractState on;
    };

    struct IconPayload
    {
        std::string name;
        float size = 16.f;
        Color color = Color::Black;
        InteractState on;
    };

    struct PainterPayload
    {
        std::function<void(class Paint2D&)> onPaint;
        InteractState on;
    };

    struct SvgPayload
    {
        svg::SvgScene scene;   ///< parsed at build / on reactive src change; invalid until first bind
        svg::SceneCache cache; ///< device-scale-keyed build product, rebuilt by the renderer in Paint
        SvgAlign align = SvgAlign::XMidYMid;
        SvgMeetOrSlice meetOrSlice = SvgMeetOrSlice::Meet;
        float opacity = 1.f;
        ViewState view;
        InteractState on;
    };

    struct DividerPayload
    {
        float thickness = 1.f;
        Color color = Color::Gray;
        Spacing margin{};
    };

    struct SpacerPayload
    {
        float size = 0.f;
        bool flexible = false;
    };

    struct ExpandedPayload
    {
        float weight = 1.f;
    };

    struct ButtonPayload
    {
        std::string label;
        ButtonVariant variant = ButtonVariant::Solid;
        Color fill = Color::Transparent; // unset (a == 0): resolve from variant at paint
        Color color = Color::Transparent; // unset (a == 0): resolve from variant at paint
        float radius = 6.f;
        InteractState on;
    };

    struct SwitchPayload
    {
        bool value = false;
        Color activeColor = Color::Blue;
        std::optional<Bind<bool>> bind;
        std::function<void(bool)> onChange;
        bool pressed = false;
        FocusPolicy focusPolicy = FocusPolicy::None;
        int         tabIndex    = 0;
    };

    struct CheckboxPayload
    {
        bool value = false;
        std::string label;
        std::optional<Bind<bool>> bind;
        std::function<void(bool)> onChange;
        bool pressed = false;
        bool disabled = false;
        FocusPolicy focusPolicy = FocusPolicy::None;
        int         tabIndex    = 0;
    };

    struct InputPayload
    {
        std::string value;
        std::string placeholder;
        std::optional<Bind<std::string>> bind;
        std::function<void(std::string)> onChange;
        std::function<void(std::string)> onSubmit;
        std::function<void()>            onCancel;
        std::function<void(std::string)> onBlur;
        std::function<std::vector<ContextMenuItem>()> contextMenu;
        StyleState style;
        Color      color = Color::Black; // text + caret colour

        TextEditor editor;
        FocusPolicy focusPolicy = FocusPolicy::None;
        int        tabIndex = 0;
        bool       focused = false;
        bool       selectOnFocus = false;
        bool       caretVisible = true;
        bool       multiLine = false;
        bool       disabled = false;
        bool       dragging = false;
        float      blinkClock = 0.f;
        float      clock = 0.f;       // monotonic seconds (from OnFrame) for click timing
        float      scrollX = 0.f;     // horizontal scroll so a long value keeps the caret in view
        float      lastClickTime = -1.f;
        Vec2       lastClickPos{};
        int        clickCount = 0;
        Color      selectionColor = Color::Rgba(54, 110, 200, 130);
    };

    struct TextSelectionScopePayload
    {
        Ref<TextSelection> selection;
        std::function<std::string(TextPos, TextPos)>  textRange;
        std::function<std::pair<TextPos, TextPos>()>  fullRange;
        std::string separator = "\n";

        // Gesture state: `dragging` is the captured drag; the three click fields recognise word/line expansion with the thresholds Input and the dispatcher use.
        bool  dragging = false;
        float lastClickTime = -1.f;
        Vec2  lastClickPos{};
        int   clickCount = 0;

        StyleState style;
        ViewState view;
        TransformState transform;
        InteractState on;
    };
} // namespace lumora
