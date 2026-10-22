#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Props.h>
#include <lumora/Widgets/SvgTypes.h>

namespace lumora
{
    /// Static-SVG leaf. Parses `src` (a file path) at build and on reactive change, bakes transforms CPU-side and replays through
    /// Paint2D scaled per `align`/`meetOrSlice`. Unsized takes the root width/height, else the viewBox.
    /// Renders shapes, paths, solid + gradient fills, strokes, clip paths, groups and <use>/<defs>; <text>, filters, patterns and masks are not drawn.
    struct SvgCfg
    {
        Str             src{};                          ///< file path resolved through the runtime AssetLoader
        SvgAlign        align       = SvgAlign::XMidYMid;
        SvgMeetOrSlice  meetOrSlice = SvgMeetOrSlice::Meet;
        Reactive<float> opacity     = 1.f;              ///< whole-widget opacity, multiplies the document opacity
        LayoutProps     layout{};
        ViewProps       view{};
        InteractProps   on{};
    };

    LUMORA_API Elem Svg(SvgCfg cfg = {});
}
