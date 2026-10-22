#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Props.h>

namespace lumora
{
    struct IconCfg
    {
        Reactive<float> size = 16.f;
        Reactive<Color> color = Color::Black;
        LayoutProps layout{};
        InteractProps on{};
    };

    struct DividerCfg
    {
        Reactive<float> thickness = 1.f;
        Reactive<Color> color = Color::Gray;
        Spacing margin{};
    };

    /// Icon leaf, addressed by name and squared to `size`.
    LUMORA_API Elem Icon(Str name, IconCfg cfg = {});

    /// A thin rule between siblings.
    LUMORA_API Elem Divider(DividerCfg cfg = {});

    /// `0` makes a flexible spacer that eats remaining main-axis space; `>0` is a fixed gap.
    LUMORA_API Elem Spacer(float size = 0.f);

    /// A flex child that grows to fill the main axis, weighted by `weight`.
    LUMORA_API Elem Expanded(float weight = 1.f);
}
