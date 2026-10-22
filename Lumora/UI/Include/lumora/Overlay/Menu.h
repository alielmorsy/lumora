#pragma once

#include <lumora/Elem.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Overlay/Popover.h>
#include <lumora/Overlay/Positioner.h>
#include <lumora/Reactive/Computed.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Anchor.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/ContextMenu.h>
#include <lumora/Widgets/ControlFlow.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/Misc.h>
#include <lumora/Widgets/Text.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace lumora
{
    /// Surface look of a dropdown / fly-out panel; shared by the menubar and the nested-menu renderer.
    struct MenuPanelStyle
    {
        float width      = 220.f;
        Color fill       = Color::Rgba(30, 32, 39, 252);
        Color border     = Color::Rgba(60, 63, 72, 255);
        Color hover      = Color::Rgba(60, 110, 200, 90);
        Color text       = Color::Rgba(228, 230, 236, 255);
        Color dim        = Color::Rgba(150, 153, 162, 255);
        float itemHeight = 28.f;
        float fontSize   = 13.f;
    };

    /// One dropdown level: a column of rows over `items`. A leaf runs `onClick` then `closeAll` (collapsing the open chain);
    /// a row with a `submenu` opens a fly-out. Each level owns its open-submenu signal. Returned as the body of a Popover,
    /// which supplies the panel surface.
    struct MenuPanel
    {
        std::vector<ContextMenuItem> items;
        MenuPanelStyle               style{};
        std::function<void()>        closeAll{};

        Signal<int>         openSub{-1};
        std::vector<Anchor> subAnchors;

        Elem Render()
        {
            subAnchors = std::vector<Anchor>(items.size());

            std::vector<Elem> rows;
            rows.reserve(items.size());
            for (int i = 0; i < static_cast<int>(items.size()); ++i)
            {
                const ContextMenuItem& it = items[i];
                if (it.separator)
                {
                    rows.push_back(Divider({.color = style.border, .margin = Spacing::Symmetric(4.f, 6.f)}));
                    continue;
                }

                const bool        enabled  = it.enabled;
                const bool        hasSub   = !it.submenu.empty();
                const Color       labelCol = enabled ? style.text : style.dim;
                const std::string right    = hasSub ? std::string("\xE2\x80\xBA") : it.shortcut; // U+203A

                BoxCfg row;
                row.layout = {.width = Expand, .height = Fixed(style.itemHeight),
                              .padding = Spacing::Symmetric(0.f, 10.f), .cross = Stretch};
                row.style  = {.hoverFill = enabled ? style.hover : Color::Transparent, .radius = 5.f};

                Elem flyout{};
                if (enabled && hasSub)
                {
                    row.on.ref     = subAnchors[i];
                    row.on.onEnter = [this, i] { openSub = i; };
                    row.on.onClick = [this, i] { openSub = i; };

                    const Anchor a = subAnchors[i];
                    flyout = Show(Computed<bool>([this, i] { return openSub() == i; }),
                        [this, i, a]
                        {
                            PopoverCfg pc;
                            pc.anchor    = a;
                            pc.side      = Side::Right;
                            pc.align     = Align3::Start;
                            pc.offset    = 2.f;
                            pc.layer     = Layer::Menu;
                            pc.layout    = {.padding = Spacing::All(5.f)};
                            pc.style     = {.fill = style.fill, .radius = 9.f,
                                            .border = {.width = 1.f, .color = style.border}};
                            pc.onDismiss = [this, i](DismissReason) { if (openSub.Peek() == i) openSub = -1; };
                            return Popover(std::move(pc), Elem{MenuPanel{items[i].submenu, style, closeAll}});
                        });
                }
                else if (enabled)
                {
                    row.on.onEnter = [this] { openSub = -1; };
                    const std::function<void()> action = it.onClick;
                    const std::function<void()> close  = closeAll;
                    row.on.onClick = [action, close] { if (action) action(); if (close) close(); };
                }

                rows.push_back(Box(std::move(row),
                    Row(RowCfg{.layout = {.width = Expand, .height = Expand, .cross = Center, .gap = 16.f}},
                        Text(it.label, {.size = style.fontSize, .color = labelCol}),
                        Spacer(),
                        Text(right, {.size = hasSub ? style.fontSize + 2.f : style.fontSize - 1.f,
                                     .color = style.dim})),
                    std::move(flyout)));
            }

            return Column(ColumnCfg{.layout = {.width = Fixed(style.width), .cross = Stretch, .gap = 1.f}},
                          std::move(rows));
        }
    };

    struct MenuItem
    {
        std::string           label;
        std::string           shortcut;
        bool                  enabled = true;
        bool                  separator = false;
        std::function<void()> onSelect{};
    };

    /// Anchored, light-dismiss dropdown menu; gate with Show. `onClose` fires after a selection or any dismissal so the caller can flip its signal.
    struct MenuCfg
    {
        Anchor                anchor;
        Side                  side  = Side::Bottom;
        Align3                align = Align3::Start;
        std::vector<MenuItem> items;
        std::function<void()> onClose{};
        float                 width = 210.f;
        Color                 fill = Color::Rgba(30, 32, 39, 252);
        Color                 border = Color::Rgba(60, 63, 72, 255);
        Color                 hover = Color::Rgba(60, 110, 200, 90);
        Color                 text = Color::Rgba(228, 230, 236, 255);
        Color                 dim = Color::Rgba(150, 153, 162, 255);
    };

    [[nodiscard]] inline Elem Menu(MenuCfg cfg)
    {
        const auto close = cfg.onClose;

        std::vector<Elem> rows;
        rows.reserve(cfg.items.size());
        for (const MenuItem& item : cfg.items)
        {
            if (item.separator)
            {
                rows.push_back(Box(BoxCfg{
                    .layout = {.width = Expand, .height = Fixed(1.f), .margin = Spacing::Symmetric(4.f, 6.f)},
                    .style  = {.fill = cfg.border}}));
                continue;
            }
            const Color labelCol = item.enabled ? cfg.text : cfg.dim;
            BoxCfg row;
            row.layout = {.width = Expand, .padding = Spacing::Symmetric(6.f, 10.f)};
            row.style  = {.hoverFill = cfg.hover, .radius = 5.f};
            if (item.enabled)
            {
                row.on.onClick = [onSelect = item.onSelect, close] {
                    if (onSelect) onSelect();
                    if (close) close();
                };
            }
            rows.push_back(Box(std::move(row),
                Row(RowCfg{.layout = {.width = Expand, .cross = Center, .gap = 16.f}},
                    Text(item.label, {.size = 13.f, .color = labelCol}),
                    Spacer(),
                    Text(item.shortcut, {.size = 12.f, .color = cfg.dim}))));
        }

        PopoverCfg pc;
        pc.anchor    = cfg.anchor;
        pc.side      = cfg.side;
        pc.align     = cfg.align;
        pc.offset    = 4.f;
        pc.layer     = Layer::Menu;
        pc.layout    = {.padding = Spacing::All(5.f)};
        pc.style     = {.fill = cfg.fill, .radius = 9.f, .border = {.width = 1.f, .color = cfg.border}};
        pc.onDismiss = [close](DismissReason) { if (close) close(); };
        return Popover(std::move(pc),
            Column(ColumnCfg{.layout = {.width = Fixed(cfg.width), .cross = Stretch, .gap = 1.f}},
                   std::move(rows)));
    }
}
