#include <lumora/Widgets/MenuBar.h>

#include <utility>

namespace lumora
{
    Context<MenuBarController*> MenuBarCtx{};

    void MenuBarController::Set(std::vector<MenuBarMenu> next)
    {
        // Update, not `menus = ...`: Signal::operator= equality-gates the write and std::vector<MenuBarMenu>::operator== does not compile.
        menus.Update([&](std::vector<MenuBarMenu>& v) { v = std::move(next); });
    }

    void MenuBarController::AddMenu(std::string title, std::vector<ContextMenuItem> items)
    {
        menus.Update([&](std::vector<MenuBarMenu>& v)
        {
            v.push_back(MenuBarMenu{std::move(title), std::move(items)});
        });
    }

    void MenuBarController::RemoveMenu(std::string_view title)
    {
        menus.Update([&](std::vector<MenuBarMenu>& v)
        {
            std::erase_if(v, [&](const MenuBarMenu& m) { return m.title == title; });
        });
    }

    void MenuBarController::UpdateMenu(std::string_view title,
                                       const std::function<void(std::vector<ContextMenuItem>&)>& edit)
    {
        menus.Update([&](std::vector<MenuBarMenu>& v)
        {
            for (MenuBarMenu& m : v)
            {
                if (m.title == title)
                {
                    edit(m.items);
                    return;
                }
            }
            v.push_back(MenuBarMenu{std::string(title), {}});
            edit(v.back().items);
        });
    }
} // namespace lumora
