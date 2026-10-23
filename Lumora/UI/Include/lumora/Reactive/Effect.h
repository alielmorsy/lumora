#pragma once

#include <lumora/Api.h>
#include <lumora/Reactive/Tracking.h>

namespace lumora
{
    /// Runs fn now, then again whenever a Signal/Computed it reads changes. Lifetime is tied to the current Owner; without
    /// one it runs once and does not persist.
    LUMORA_API void Effect(CleanupFn fn);

    /// Runs fn once now, untracked. No-op without a current Owner.
    LUMORA_API void OnMount(CleanupFn fn);

    /// Registers fn to run when the current Owner disposes (LIFO). No-op without an Owner.
    LUMORA_API void OnCleanup(CleanupFn fn);

    /// Registers a per-frame callback fired once per Tick() with the real frame delta in seconds (clamped to 0.1), so it runs at
    /// the display rate: scale motion by dt, never by call count. Tied to the current Owner; without one it never fires.
    LUMORA_API void OnFrame(FrameFn fn);
}
