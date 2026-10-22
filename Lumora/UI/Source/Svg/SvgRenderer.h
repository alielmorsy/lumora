#pragma once

#include "Svg/SvgScene.h"

#include <lumora/Foundation/Rect.h>
#include <lumora/Widgets/SvgTypes.h>

namespace lumora
{
    class Paint2D;
    class Canvas;
} // namespace lumora

namespace lumora::svg
{
    /// Replay a scene into the laid-out `box` (logical px), fitting the viewBox per `align`/`meetOrSlice`. `cache` is rebuilt
    /// when the resolved scale changes; `contentScale` is the OS DPI scale, used only to pick flatten/feather density.
    void RenderScene(Paint2D& surface, Canvas& canvas, SvgScene& scene, SceneCache& cache, const Rect& box,
                     SvgAlign align, SvgMeetOrSlice meetOrSlice, float widgetOpacity, float contentScale);
} // namespace lumora::svg
