#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Bind.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Props.h>

#include <functional>

namespace lumora
{
    /// `label` and `onClick` are promoted to the top level. `onClick` shadows `on.onClick`: set either, never both (debug-asserted).
    struct ButtonCfg
    {
        Str label{};
        std::function<void()> onClick{};
        ButtonVariant variant = ButtonVariant::Solid;
        Reactive<Color> fill = Color::Transparent; // unset → variant fill
        Reactive<Color> color = Color::Transparent; // unset → variant text colour
        Reactive<float> radius = 6.f;
        LayoutProps layout{.padding = Spacing::Symmetric(10.f, 20.f)};
        InteractProps on{};
        Reactive<std::string> className{};
    };

    /// `on` is the bound boolean (two-way), not the interaction group.
    struct SwitchCfg
    {
        Bind<bool> on;
        Reactive<Color> activeColor = Color::Blue;
        std::function<void(bool)> onChange{};
        LayoutProps layout{};
        FocusPolicy focusPolicy = FocusPolicy::Tab; ///< keyboard focus; None opts out, Click skips Tab
        int tabIndex = 0;
        bool autofocus = false;
        Reactive<std::string> className{};
    };

    struct CheckboxCfg
    {
        Bind<bool> checked;
        Str label{};
        std::function<void(bool)> onChange{};
        Reactive<bool> disabled = false;
        Reactive<Color> checkColor = Color::Blue;
        Reactive<Color> borderColor = Color::Gray;
        Reactive<Color> labelColor = Color::Transparent;
        LayoutProps layout{.padding = Spacing::Symmetric(4.f, 2.f), .margin = Spacing::Symmetric(0.f, 4.f)};
        FocusPolicy focusPolicy = FocusPolicy::Tab;
        int tabIndex = 0;
        bool autofocus = false;
        Reactive<std::string> className{};
    };

    LUMORA_API Elem Button(ButtonCfg cfg = {});
    LUMORA_API Elem Switch(SwitchCfg cfg);
    LUMORA_API Elem Checkbox(CheckboxCfg cfg);
}
