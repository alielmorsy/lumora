#pragma once

#include <lumora/Elem.h>

namespace lumora
{
    struct ContextMenuHost;

    /// Always-mounted modal context-menu overlay for `host`; renders nothing while closed.
    [[nodiscard]] Elem BuildContextMenuOverlay(ContextMenuHost* host);
} // namespace lumora
