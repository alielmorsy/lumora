#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Props.h>

#include <functional>
#include <string>

namespace lumora
{
    /// A compact labelled token (tag, filter, multi-select item) composed from Row/Box/Text; takes `className` like any
    /// container. The leading dot encodes one binary property at a glance (filled vs hollow).
    struct ChipCfg
    {
        Str label{};

        /// Transparent (the default) draws no dot. Any other colour draws one, filled or hollow.
        Color dotColor = Color::Transparent;
        bool  dotFilled = true;

        /// Set to show a trailing remove affordance; the chip's own onClick still fires for the body.
        std::function<void()> onRemove{};
        /// What that affordance draws; empty falls back to a multiplication sign.
        Elem removeIcon{};
        std::function<void()> onClick{};

        Reactive<bool> disabled = false;

        float fontSize = 11.f;
        float height   = 20.f;

        LayoutProps layout{};
        StyleProps  style{
            .fill      = Color::Rgba(44, 47, 56, 255),
            .hoverFill = Color::Transparent,
            .radius    = 10.f,
            .border    = {1.f, Color::Rgba(64, 68, 78, 255)},
        };
        Color textColor = Color::Rgba(214, 218, 226, 255);
        Reactive<std::string> className{};
    };

    [[nodiscard]] LUMORA_API Elem Chip(ChipCfg cfg = {});
}
