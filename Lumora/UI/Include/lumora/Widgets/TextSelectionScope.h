#pragma once

#include <lumora/Api.h>
#include <lumora/Ref.h>
#include <lumora/Detail/Node.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Children.h>
#include <lumora/Widgets/Props.h>
#include <lumora/Widgets/TextSelection.h>

#include <functional>
#include <string>
#include <utility>

namespace lumora
{
    /// Turns the selectable Text leaves beneath it into one continuous selection; it owns the gesture and focus, leaves only
    /// opt in (`TextCfg::selection` + `selectionKey`) and paint their share. A drag across leaves is captured by the pressed
    /// node, and leaf focus dies when a virtualized row scrolls out, so the gesture cannot live on the leaves.
    struct TextSelectionScopeCfg
    {
        /// The shared selection this scope drives. Required - a scope without one is inert.
        TextSelectionRef selection{};

        /// Text of the range [from, to] for Copy. Required whenever leaves can be unmounted (virtualized lists), else copy stops at
        /// the scrolled window's edge. Unset falls back to mounted leaves joined by `separator` in key order.
        std::function<std::string(TextPos from, TextPos to)> textRange{};

        /// Extent Ctrl+A selects; unset falls back to mounted leaves (wrong under virtualization, like `textRange`).
        std::function<std::pair<TextPos, TextPos>()> fullRange{};

        /// Joins consecutive leaves in the fallback (mounted-leaves) copy path.
        std::string separator = "\n";

        LayoutProps           layout{};
        StyleProps            style{};
        ViewProps             view{};
        TransformProps        transform{};
        PositionProps         position{};
        bool                  anchor = false;
        InteractProps         on{};
        Str                   className{};
    };

    namespace detail
    {
        LUMORA_API Ref<Node> BuildTextSelectionScope(TextSelectionScopeCfg cfg);
    }

    template <typename... Kids>
    Elem TextSelectionScope(TextSelectionScopeCfg cfg = {}, Kids&&... kids)
    {
        Ref<detail::Node> node = detail::BuildTextSelectionScope(std::move(cfg));
        detail::AppendKids(node, std::forward<Kids>(kids)...);
        return Elem(std::move(node));
    }
}
