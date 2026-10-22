#pragma once

#include "Svg/SvgScene.h"

#include <string_view>

namespace lumora::svg
{
    /// Parse SVG document text into a resolved scene. Returns an invalid scene (`valid == false`) when
    /// the text has no usable <svg> root.
    SvgScene ParseSvg(std::string_view svgText);
} // namespace lumora::svg
