#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Props.h>

#include <cstdint>
#include <string>

namespace lumora
{
    /// Severity ramp, named by meaning so a stylesheet can recolour it without changing call sites.
    enum class BadgeVariant : std::uint8_t { Neutral, Info, Success, Warn, Danger };

    /// A small status label (cook state, tab count, "modified" marker). Presentational only, no interaction. Position it
    /// yourself; it measures to its text.
    struct BadgeCfg
    {
        Str          label{};
        BadgeVariant variant = BadgeVariant::Neutral;

        /// Unset (alpha 0) resolves from `variant`. Set to override one badge without a new variant.
        Reactive<Color> fill  = Color::Transparent;
        Reactive<Color> color = Color::Transparent;

        float fontSize = 10.f;
        float height   = 15.f;
        float radius   = 3.f;

        LayoutProps           layout{};
        Reactive<std::string> className{};
    };

    [[nodiscard]] LUMORA_API Elem Badge(BadgeCfg cfg = {});
}
