#pragma once

#include <lumora/Api.h>
#include <lumora/Ref.h>
#include <lumora/Detail/Node.h>
#include <lumora/Elem.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/Children.h>

#include <utility>

namespace lumora
{
    // Flex containers share BoxCfg; the kind alone picks the arrangement (Row along the main axis, Column down it, Stack z-layers).
    using RowCfg = BoxCfg;
    using ColumnCfg = BoxCfg;
    using StackCfg = BoxCfg;

    // A Container is a Box driven entirely by its flex surface (`layout.direction` etc. or a CSS `className`); same node as Box.
    using ContainerCfg = BoxCfg;

    namespace detail
    {
        LUMORA_API Ref<Node> BuildRow(RowCfg cfg);
        LUMORA_API Ref<Node> BuildColumn(ColumnCfg cfg);
        LUMORA_API Ref<Node> BuildStack(StackCfg cfg);
    }

    template <typename... Kids>
    Elem Row(RowCfg cfg = {}, Kids&&... kids)
    {
        Ref<detail::Node> node = detail::BuildRow(std::move(cfg));
        detail::AppendKids(node, std::forward<Kids>(kids)...);
        return Elem(std::move(node));
    }

    template <typename... Kids>
    Elem Column(ColumnCfg cfg = {}, Kids&&... kids)
    {
        Ref<detail::Node> node = detail::BuildColumn(std::move(cfg));
        detail::AppendKids(node, std::forward<Kids>(kids)...);
        return Elem(std::move(node));
    }

    template <typename... Kids>
    Elem Stack(StackCfg cfg = {}, Kids&&... kids)
    {
        Ref<detail::Node> node = detail::BuildStack(std::move(cfg));
        detail::AppendKids(node, std::forward<Kids>(kids)...);
        return Elem(std::move(node));
    }

    template <typename... Kids>
    Elem Container(ContainerCfg cfg = {}, Kids&&... kids)
    {
        Ref<detail::Node> node = detail::BuildBox(std::move(cfg));
        detail::AppendKids(node, std::forward<Kids>(kids)...);
        return {std::move(node)};
    }
}
