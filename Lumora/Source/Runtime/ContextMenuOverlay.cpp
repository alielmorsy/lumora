#include "Runtime/ContextMenuOverlay.h"

#include "Runtime/ContextMenuHost.h"

#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/ControlFlow.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/Misc.h>
#include <lumora/Widgets/Text.h>

#include <string>
#include <utility>
#include <vector>

namespace lumora
{
    namespace
    {
        Elem BuildPanel(ContextMenuHost* host, const MenuLevel& level, int depth)
        {
            const MenuStyle& st = host->style;

            std::vector<Elem> rows;
            rows.reserve(level.items.size());
            for (int i = 0; i < static_cast<int>(level.items.size()); ++i)
            {
                const ContextMenuItem& it = level.items[i];
                if (it.separator)
                {
                    rows.push_back(Divider({.color = st.border, .margin = Spacing::Symmetric(4.f, 6.f)}));
                    continue;
                }

                const bool enabled = it.enabled;
                const bool hasSub = !it.submenu.empty();

                InteractProps on;
                if (enabled)
                {
                    // Hovering opens a submenu (if any) and highlights via hoverFill.
                    on.onEnter = [host, depth, i] { host->HoverItem(depth, i); };
                    if (hasSub)
                    {
                        on.onClick = [host, depth, i] { host->HoverItem(depth, i); };
                    }
                    else
                    {
                        std::function<void()> action = it.onClick;
                        on.onClick = [host, action] { host->Select(action); };
                    }
                }

                const std::string label = it.label;
                const std::string right = hasSub ? std::string("\xE2\x80\xBA") : it.shortcut;
                const float rightSize = hasSub ? 24.f : st.fontSize - 1.f;
                const Color labelCol = enabled ? st.text : st.textDim;
                const Color dimCol = st.textDim;

                rows.push_back(Box(
                    BoxCfg{
                        .layout = {
                            .width = Expand,
                            .height = Fixed(st.itemHeight),
                            .padding = Spacing::Symmetric(0.f, st.itemPadH),
                            .cross = Stretch
                        },
                        .style = {
                            .hoverFill = enabled ? st.hover : Color::Transparent,
                            .radius = 5.f
                        },
                        .on = std::move(on)
                    },
                    Row(RowCfg{.layout = {.width = Expand, .height = Expand, .cross = Center}},
                        Text(label, {.size = st.fontSize, .color = labelCol}),
                        Spacer(),
                        Text(right, {.size = rightSize, .color = dimCol}))));
            }

            return Box(
                BoxCfg{
                    .layout = {
                        .width = Fixed(level.width),
                        .padding = Spacing::Symmetric(st.panelPad, st.panelPad),
                        .cross = Stretch
                    },
                    .style = {
                        .fill = st.fill,
                        .radius = st.radius,
                        .border = {.width = 1.f, .color = st.border}
                    },
                    .position = {
                        .position = Position::Absolute,
                        .inset = {.left = level.anchor.x, .top = level.anchor.y}
                    }
                },
                Column(ColumnCfg{.layout = {.width = Expand, .cross = Stretch, .gap = 1.f}},
                       std::move(rows)));
        }

        Elem BuildMenuBody(ContextMenuHost* host)
        {
            Elem backdrop = Box(BoxCfg{
                .layout = {.width = Expand, .height = Expand},
                .on = {.onClick = [host] { host->RequestClose(); }}
            });

            Elem panels = Each(host->levels, &MenuLevel::id,
                               [host](const MenuLevel& level, int depth)
                               {
                                   return BuildPanel(host, level, depth);
                               });

            return Stack(StackCfg{.layout = {.width = Expand, .height = Expand}},
                         std::move(backdrop), std::move(panels));
        }
    } // namespace

    Elem BuildContextMenuOverlay(ContextMenuHost* host)
    {
        auto hasMenu = Reactive<bool>([host] { return !host->levels().empty(); });
        return Show(std::move(hasMenu), [host] { return BuildMenuBody(host); });
    }
} // namespace lumora
