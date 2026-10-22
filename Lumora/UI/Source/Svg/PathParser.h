#pragma once

#include "Svg/SvgScene.h"

#include <string_view>

namespace lumora::svg
{
    /// Parse SVG path data into subpaths of lines + cubics in the element's local space (the builder bakes the CTM later).
    /// Quadratics and arcs become cubics; malformed input yields what parsed cleanly before the error.
    RawPath ParsePathData(std::string_view d);
} // namespace lumora::svg
