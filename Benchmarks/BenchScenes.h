#pragma once

#include <lumora/Elem.h>

#include <functional>
#include <string>
#include <vector>

namespace lumora
{
    class MountedApp;
}

namespace lumora::bench
{
    /// One benchmark scene: a tree to mount, an optional stylesheet, and an optional per-frame mutation (a signal write via
    /// MountedApp::RunScoped or a synthetic event via MountedApp::Dispatch).
    /// Driven through MountedApp, the only path that seeds AppCtx::style and runs the shell and layout passes; anything
    /// cheaper measures a frame that never ships.
    struct Scene
    {
        std::string name;
        std::string note;             ///< what this scene isolates
        std::string css;              ///< empty = no stylesheet installed
        bool        text = false;     ///< true when the scene's cost is dominated by text
        std::function<Elem()>                       build;
        std::function<void(MountedApp&, int frame)>  step; ///< null = a genuinely static scene
    };

    /// The catalogue. `scale` sizes the parametric scenes (roughly the styled-node count), so one scene at 1k/10k/100k
    /// reports a scaling exponent.
    [[nodiscard]] std::vector<Scene> Catalogue(int scale);
} // namespace lumora::bench
