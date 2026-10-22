#pragma once

namespace lumora
{
    struct AppCtx;

    /// Pin the dependent axis of every node carrying a CSS `aspect-ratio`; returns true when a pinned size changed (re-solve needed).
    /// Runs between the first solve and the re-solve, since the driving axis (`Expand`/percent) has no px extent earlier. Ratio applies to the border box.
    bool RunAspectRatios(AppCtx& ctx);
} // namespace lumora
