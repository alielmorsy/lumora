#pragma once

#include <lumora/Elem.h>

namespace lumora
{
    class MenuBarController;

    /// Builds the auto-mounted menubar strip; it collapses to nothing while the controller holds no menus.
    [[nodiscard]] Elem BuildMenuBarView(MenuBarController* controller);
} // namespace lumora
