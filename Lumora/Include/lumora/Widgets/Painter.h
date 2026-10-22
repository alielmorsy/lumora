#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Paint/Paint2D.h>
#include <lumora/Widgets/Props.h>

#include <cstdint>
#include <functional>

namespace lumora
{
    /// Immediate-mode custom-paint leaf: `onPaint` draws onto a Paint2D facade each frame, clipped to the content rect. With no
    /// `on` handlers it is pointer-transparent (paint-only); set any handler to make it interactive.
    /// Paint2D space is y-down, so arc angles (StrokeArc/FillPie) run clockwise on screen.
    struct PainterCfg
    {
        SizeHint                      width  = Expand;
        SizeHint                      height = Expand;
        Spacing                       padding{};
        std::function<void(Paint2D&)> onPaint;
        InteractProps                 on{};

        /// Bump to force a repaint: bind a Signal the painter's state writes; each change raises Dirty::Repaint without layout or
        /// style. A static value binds nothing.
        Reactive<std::uint64_t>       repaint{};
    };

    LUMORA_API Elem Painter(PainterCfg cfg = {});
}
