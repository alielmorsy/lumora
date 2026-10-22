#include "Runtime/MenuBarView.h"

#include <lumora/Overlay/Menu.h>
#include <lumora/Reactive/Computed.h>
#include <lumora/Widgets/Anchor.h>
#include <lumora/Widgets/ControlFlow.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/MenuBar.h>
#include <lumora/Widgets/Text.h>

#include <string>
#include <utility>
#include <vector>

namespace lumora
{
    namespace
    {
        constexpr float kBarHeight = 30.f;

        /// One top-level title: toggles its dropdown on click, switches to it on hover while another menu is open, and hosts the dropdown Popover.
        Elem BuildTitle(MenuBarController* c, const MenuBarMenu& menu, int index, MenuPanelStyle style)
        {
            const Anchor                 anchor;
            const int                    i = index;
            std::vector<ContextMenuItem> items = menu.items;
            std::string                  title = menu.title;

            RowCfg cell;
            cell.layout    = {.padding = Spacing::Symmetric(0.f, 11.f), .cross = Center};
            cell.style     = {.hoverFill = Color::Rgba(255, 255, 255, 22), .radius = 5.f};
            cell.on.ref     = anchor;
            cell.on.onClick = [c, i] { c->active = (c->active.Peek() == i ? -1 : i); };
            cell.on.onEnter = [c, i] { if (c->active.Peek() != -1) c->active = i; };

            Elem flyout = Show(Computed<bool>([c, i] { return c->active() == i; }),
                [c, i, anchor, items, style]
                {
                    PopoverCfg pc;
                    pc.anchor    = anchor;
                    pc.side      = Side::Bottom;
                    pc.align     = Align3::Start;
                    pc.offset    = 4.f;
                    pc.layer     = Layer::Menu;
                    pc.layout    = {.padding = Spacing::All(5.f)};
                    pc.style     = {.fill = style.fill, .radius = 9.f,
                                    .border = {.width = 1.f, .color = style.border}};
                    pc.onDismiss = [c, i](DismissReason) { if (c->active.Peek() == i) c->active = -1; };
                    return Popover(std::move(pc), Elem{MenuPanel{items, style, [c] { c->active = -1; }}});
                });

            return Row(std::move(cell),
                       Text(title, {.size = 13.f, .color = style.text}),
                       std::move(flyout));
        }

        Elem BuildBar(MenuBarController* c)
        {
            const MenuPanelStyle style;
            return Row(RowCfg{.layout = {.width = Expand, .height = Fixed(kBarHeight),
                                         .padding = Spacing::Symmetric(4.f, 6.f), .cross = Stretch, .gap = 2.f},
                              .style  = {.fill = Color::Rgba(24, 26, 32, 255)}},
                       Each(c->menus,
                            [c, style](const MenuBarMenu& m, int i) { return BuildTitle(c, m, i, style); }));
        }
    } // namespace

    Elem BuildMenuBarView(MenuBarController* controller)
    {
        return Show(Computed<bool>([controller] { return !controller->menus().empty(); }),
                    [controller] { return BuildBar(controller); });
    }
} // namespace lumora
