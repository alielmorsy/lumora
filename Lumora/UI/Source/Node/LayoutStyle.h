#pragma once

#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/Position.h>
#include <lumora/Foundation/Vec2.h>

#include <cstdint>

namespace lumora
{
    /// FlexAxis / DisplayMode / FlexWrap live in the public Foundation/Geometry.h so LayoutProps and the CSS resolver share one definition with the layout bridge.
    /// Row -> Row, every other container -> Column, Scroll follows its scroll axis; Stack and Grid children are absolute overlays positioned by the backend / grid pass.

    /// Where a node sits in its parent Grid. `col`/`row` are 0-based track indices (-1 = auto-flow); spans are >= 1, clamped to the track count.
    /// Only a Cell sets these; ignored unless the parent is a Grid.
    struct GridItemPlacement
    {
        std::int16_t col = -1;
        std::int16_t row = -1;
        std::int16_t colSpan = 1;
        std::int16_t rowSpan = 1;
    };

    /// Opaque solver-node handle resolved only by the layout backend; 0 is the null handle (transparent nodes own no solver node).
    struct LayoutHandle
    {
        std::uint32_t value = 0;

        [[nodiscard]] constexpr bool Valid() const noexcept { return value != 0; }
        [[nodiscard]] constexpr bool operator==(const LayoutHandle&) const noexcept = default;
    };

    /// Which axis an `aspect-ratio` node derives from the other. Decided by the style resolver from the *declared* sizes: the post-solve pass pins the
    /// dependent axis, so re-deriving from live sizes would see its own patch and never follow a resize.
    enum class AspectPin : std::uint8_t { None, Height, Width };

    /// The fully-resolved layout inputs for one node, the single source the layout bridge reads. Only gap and the per-edge insets are patched
    /// reactively; the paint-only compositing transform lives in the payload.
    struct LayoutStyle
    {
        SizeHint width = Wrap;
        SizeHint height = Wrap;
        SizeHint minWidth = Wrap;  ///< Wrap == no constraint (Masharif default); set by CSS min-width
        SizeHint minHeight = Wrap; ///< Wrap == no constraint; set by CSS min-height
        SizeHint maxWidth = Wrap;  ///< Wrap == no constraint; set by CSS max-width
        SizeHint maxHeight = Wrap; ///< Wrap == no constraint; set by CSS max-height
        Spacing padding{};
        Spacing margin{};
        /// Percent edges for padding/margin: a nonzero edge is a percentage (its px counterpart is ignored). Set only by the CSS resolver.
        Spacing paddingPct{};
        Spacing marginPct{};
        Align main = Align::Start;
        Align cross = Align::Start;
        float gap = 0.f;
        float gapPct = 0.f; ///< nonzero == gap is a percentage (native to Masharif); 0 keeps px `gap`
        float flexWeight = 0.f; ///< explicit grow weight (Expanded / flexible Spacer / Grow / CSS flex-grow)
        DisplayMode display = DisplayMode::Flex; ///< CSS display: Block routes to Masharif normal-flow
        FlexWrap wrap = FlexWrap::NoWrap;        ///< CSS flex-wrap
        float flexShrink = 0.f;   ///< CSS flex-shrink; default 0 keeps the rigid overflow-not-crush model
        SizeHint flexBasis = Wrap; ///< CSS flex-basis (Wrap == auto: size from width/height or content)
        Align alignSelf = Align::Auto;       ///< CSS align-self; Auto == inherit the container's align-items
        Align alignContent = Align::Stretch; ///< CSS align-content; multi-line cross distribution (with wrap)
        int order = 0; ///< CSS order: flex layout position only (paint/hit-test order stays tree order; use z)
        FlexAxis axis = FlexAxis::Column;
        Position position = Position::Flow;
        Inset inset{}; ///< per-edge auto/px insets, applied when position != Flow
        /// CSS `aspect-ratio` as width / height; 0 == none. Not a Masharif input: honoured by the post-solve pass (RunAspectRatios), which pins the dependent axis.
        float aspectRatio = 0.f;
        AspectPin aspectPin = AspectPin::None; ///< meaningful only while aspectRatio > 0
        bool anchor = false; ///< container opts in to be the containing block for absolute descendants
        int z = 0; ///< paint priority within siblings (reorders paint/hit-test, not the tree)
        GridItemPlacement placement{}; ///< this node's cell in a parent Grid (auto otherwise)
    };
} // namespace lumora
