#pragma once

namespace lumora
{
    struct AppCtx;

    /// Recompute every live VirtualList's window and materialize needed rows. Returns true when any window moved
    /// (caller must re-solve). Must run between the first solve and the re-solve: the window depends on the
    /// solved viewport rect, and spliced rows need solving before paint.
    bool RunVirtualLists(AppCtx& ctx);
} // namespace lumora
