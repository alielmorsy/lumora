#pragma once

#include <lumora/Api.h>
#include <lumora/Ref.h>
#include <lumora/Detail/Node.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Children.h>
#include <lumora/Widgets/Props.h>

#include <utility>

namespace lumora
{
    struct ScrollCfg
    {
        ScrollAxis axis = ScrollAxis::Vertical;
        LayoutProps layout{};
        StyleProps style{};
        ViewProps view{};
        TransformProps transform{};
        InteractProps on{};
        Str                   className{};
        /// Per-node CSS declarations (`padding: 4px; color: red`); reactive. Beat stylesheet rules except `!important`.
        Str                   inlineCss{};
    };

    namespace detail
    {
        LUMORA_API Ref<Node> BuildScroll(ScrollCfg cfg);
    }

    /// Scrollable region; the wheel/drag offset is owned in the live payload and layout clips to the viewport.
    template <typename... Kids>
    Elem Scroll(ScrollCfg cfg = {}, Kids&&... kids)
    {
        Ref<detail::Node> node = detail::BuildScroll(std::move(cfg));
        detail::AppendKids(node, std::forward<Kids>(kids)...);
        return Elem(std::move(node));
    }
}
