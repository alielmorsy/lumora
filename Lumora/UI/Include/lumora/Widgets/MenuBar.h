#pragma once

#include <lumora/Api.h>
#include <lumora/Reactive/Context.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/ContextMenu.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace lumora
{
    /// One top-level menubar entry: its title and the dropdown's items (the recursive ContextMenuItem, so fly-outs, shortcuts
    /// and separators work).
    struct MenuBarMenu
    {
        std::string                  title;
        std::vector<ContextMenuItem> items;
    };

    /// Reactive model behind the auto-mounted menubar. The runtime owns one per app; reach it via `Use(MenuBarCtx)` or
    /// `Lumora::MenuBar()`. Every change re-projects the bar, which reserves no space until the first menu is added.
    class LUMORA_API MenuBarController
    {
    public:
        /// Menus shown on the bar, left to right; the single reactive source the bar projects.
        Signal<std::vector<MenuBarMenu>> menus;
        /// Index of the open top-level menu, or -1; driven by the bar, settable to open one programmatically.
        Signal<int> active{-1};

        /// Replace the whole menu set in one shot.
        void Set(std::vector<MenuBarMenu> menus);

        /// Append a top-level menu to the right of the bar.
        void AddMenu(std::string title, std::vector<ContextMenuItem> items);

        /// Drop every top-level menu whose title matches.
        void RemoveMenu(std::string_view title);

        /// Edit the items of the menu titled `title` in place, creating it first if absent.
        void UpdateMenu(std::string_view title, const std::function<void(std::vector<ContextMenuItem>&)>& edit);

        [[nodiscard]] bool Empty() const { return menus.Peek().empty(); }
    };

    /// Context key the runtime provides at the root: `MenuBarController* bar = Use(MenuBarCtx);`.
    extern LUMORA_API Context<MenuBarController*> MenuBarCtx;
}
