#pragma once

#include <cstdint>

namespace lumora
{
    /// Live-tree node kinds: built-ins plus a registration range for custom kinds. The value indexes the KindDescriptor table that drives all dispatch.
    enum class LiveKind : std::uint16_t
    {
        Invalid = 0,
        Component, ///< erased class component (built-in)

        Box, Row, Column, Stack, Scroll,
        Grid, Cell, ///< Grid = CSS-grid containing block; Cell = a placed/spanning grid item
        Text, Image, Icon, Divider, Spacer, Expanded,
        Button, Switch, Checkbox, Input,

        // Immediate-mode custom-paint leaf (user onPaint over a Paint2D facade).
        Painter,

        // Static-SVG vector leaf: parses an SVG file and replays it through Paint2D / Canvas.
        Svg,

        // Draggable divider between two flex siblings; commits new flex weights on release.
        Splitter,

        // Scroll viewport that instantiates only the rows its window can show. Opaque (it owns a solver
        // node and clips), but it splices a runtime-managed row set like a control-flow host does.
        VirtualList,

        // Control-flow hosts: transparent, own no solver node, splice their subtree under the nearest opaque ancestor.
        Show, Each, Memo,

        // Portal: transparent like the control-flow hosts, but splices its subtree under a runtime overlay layer (paint/hit-test hoisted, ownership kept at the declaration site).
        Portal,

        // Container owning a cross-leaf text selection: drives the drag/keyboard gesture for every selectable Text leaf beneath it.
        TextSelectionScope,

        BuiltinCount,

        CustomBase = 256, ///< first id handed to runtime-registered custom kinds
    };

    /// Transparent kinds (Component, Show/Each/Memo) own no solver node; the layout bridge hoists them so descendants attach to the nearest opaque ancestor.
    [[nodiscard]] constexpr bool IsTransparent(LiveKind kind) noexcept
    {
        return kind == LiveKind::Component
            || kind == LiveKind::Show
            || kind == LiveKind::Each
            || kind == LiveKind::Memo
            || kind == LiveKind::Portal;
    }

    /// Selects a node's payload pool (by owning kind) and its slot; stateless leaf kinds carry an invalid ref.
    struct PayloadRef
    {
        LiveKind kind = LiveKind::Invalid;
        std::uint32_t index = 0;

        [[nodiscard]] constexpr bool Valid() const noexcept { return kind != LiveKind::Invalid; }
    };
} // namespace lumora
