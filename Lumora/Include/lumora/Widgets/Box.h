#pragma once

#include <lumora/Api.h>
#include <lumora/Ref.h>
#include <lumora/Detail/Node.h>
#include <lumora/Elem.h>
#include <lumora/Widgets/Children.h>
#include <lumora/Widgets/Props.h>

#include <utility>

namespace lumora
{
    /// The canonical container config, one group per concern; Row/Column/Stack reuse it and differ only in how children are arranged.
    struct BoxCfg
    {
        LayoutProps layout{};
        StyleProps style{};
        ViewProps view{};
        TransformProps transform{};
        PositionProps position{};
        bool anchor = false; ///< establish a positioning context for absolute descendants
        InteractProps on{};
        /// Space-separated CSS class list; reactive, so toggling re-resolves style. Ignored when no StyleEngine is wired.
        Reactive<std::string> className{};
        /// Pure-visual translation at PAINT and HIT-TEST time without dirtying layout.
        /// Changing it marks repaint only, preserving retained subtree replay.
        Reactive<Vec2> paintOffset{};
        /// Subtree focus boundary: Tab and directional traversal never leave this scope.
        /// Focus memory remembers and restores the last focused node on re-entry.
        bool focusScope = false;
        /// Subtree scale container: lays out in logical size and paints scaled.
        Reactive<float> contentScale = 1.f;
        /// Subtree pass-through: makes the node AND all descendants invisible to hit-testing.
        bool hitThroughSubtree = false;
    };

    namespace detail
    {
        LUMORA_API Ref<Node> BuildBox(BoxCfg cfg);
    }

    /// Generic container. Children are variadic - each may be an Elem, a class component, a
    /// Children bundle, or a range of Elem.
    template <typename... Kids>
    Elem Box(BoxCfg cfg = {}, Kids&&... kids)
    {
        Ref<detail::Node> node = detail::BuildBox(std::move(cfg));
        detail::AppendKids(node, std::forward<Kids>(kids)...);
        return Elem(std::move(node));
    }
}
