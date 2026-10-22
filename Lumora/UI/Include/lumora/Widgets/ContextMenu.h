#pragma once

#include <functional>
#include <string>
#include <vector>

namespace lumora
{
    /// One context-menu row. A separator ignores other fields; a disabled item is dimmed and never invokes. A non-empty `submenu`
    /// makes a parent row (its own `onClick` is ignored). `onClick` runs deferred (next Tick, never mid-dispatch), so capture
    /// only stable handles.
    struct ContextMenuItem
    {
        std::string label{};
        std::string shortcut{};
        bool enabled = true;
        bool separator = false;
        std::function<void()> onClick{};
        std::vector<ContextMenuItem> submenu{};
    };
}
