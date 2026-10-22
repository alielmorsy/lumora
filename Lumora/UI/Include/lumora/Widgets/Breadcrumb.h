#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Props.h>

#include <functional>
#include <string>
#include <vector>

namespace lumora
{
    /// A navigable trail. `path` is a function, not a vector: config is evaluated once at parent build, so a vector would be stuck
    /// at its mount-time state. Collapse is by count, not width (no solved rects at build): `maxVisible` keeps the first crumb and
    /// the last `maxVisible - 2`. The marker becomes a control when `onEllipsis` is set.
    struct BreadcrumbCfg
    {
        /// The trail, root first. Re-read whenever anything it touches changes.
        std::function<std::vector<std::string>()> path{};

        /// A crumb was pressed, by index into the list `path` returned (one callback, not a closure per crumb).
        std::function<void(int)> onSelect{};

        std::string separator = "/";

        /// 0 disables collapsing; values below 3 are treated as 3 (first + marker + last). Reactive so it can change at runtime.
        Reactive<int> maxVisible = 5;

        /// What the collapsed marker draws; empty draws a text ellipsis.
        Elem ellipsis{};

        /// Pressed the collapsed marker. Unset leaves it inert, like a separator.
        std::function<void()> onEllipsis{};

        float fontSize = 12.f;
        float gap      = 4.f;

        Reactive<Color> color       = Color::Rgba(198, 202, 210, 255);
        Reactive<Color> activeColor = Color::Rgba(236, 238, 242, 255); ///< the last crumb
        Reactive<Color> dimColor    = Color::Rgba(126, 132, 142, 255); ///< separators and the marker

        LayoutProps           layout{};
        Str                   className{};
    };

    [[nodiscard]] LUMORA_API Elem Breadcrumb(BreadcrumbCfg cfg);
}
