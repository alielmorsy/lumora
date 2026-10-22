#pragma once

#include "Svg/SvgScene.h"

namespace lumora::svg
{
    struct XmlNode;

    /// Walk a parsed element tree into a CTM-baked SvgScene: inherited presentation attributes, shapes expanded to paths,
    /// gradients / clipPaths / <use> resolved.
    SvgScene BuildScene(const XmlNode& root);
} // namespace lumora::svg
