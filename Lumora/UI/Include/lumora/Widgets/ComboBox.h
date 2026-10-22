#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Reactive/Bind.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Anchor.h>
#include <lumora/Widgets/Props.h>

#include <functional>
#include <string>
#include <vector>

namespace lumora
{
    /// A select/combo control. `editable == false` is a plain dropdown; `editable == true` makes the trigger a text field that
    /// filters the list. `selected` is the two-way source of truth: the option index, or -1 for none / free-typed text.
    struct ComboBoxCfg
    {
        std::vector<std::string> options{};
        Bind<int>                selected; ///< two-way selected index, -1 = none
        bool                     editable = false;
        /// Greys out and ignores input. Reactive; a plain `bool` binds as a static prop.
        Reactive<bool>           disabled = false;
        Str                      placeholder = "Select...";

        /// Fired with the new index after a pick (or -1 when editable text matches no option).
        std::function<void(int)> onChange{};
        /// Editable only: fired with the raw text on every keystroke.
        std::function<void(const std::string&)> onTextChange{};

        bool  stretch        = false; ///< trigger fills its parent's width; the dropdown matches it
        float width          = 200.f; ///< trigger/dropdown width when !stretch
        float itemHeight     = 28.f;
        float fontSize       = 14.f;
        int   maxVisibleRows = 8; ///< list scrolls once the option count passes this

        LayoutProps layout{}; ///< trigger outer margin
        StyleProps  style{
            .fill      = Color::Rgba(36, 38, 46, 255),
            .hoverFill = Color::Rgba(46, 48, 58, 255),
            .radius    = 4.f,
            .border    = {1.f, Color::Rgba(60, 63, 72, 255)},
        };
        /// CSS class list forwarded onto the trigger so its chrome can be themed from a stylesheet.
        Reactive<std::string> className{};

        // Dropdown surface palette.
        Reactive<Color> panelFill   = Color::Rgba(30, 32, 39, 252);
        Reactive<Color> panelBorder = Color::Rgba(60, 63, 72, 255);
        Reactive<Color> hover       = Color::Rgba(60, 110, 200, 90);
        Reactive<Color> textColor   = Color::Rgba(228, 230, 236, 255);
        Reactive<Color> dim         = Color::Rgba(150, 153, 162, 255);
    };

    /// Composed component (Box/Row/Text/Input + a Popover dropdown); drops into a children list like any widget.
    class LUMORA_API ComboBox
    {
    public:
        explicit ComboBox(ComboBoxCfg cfg) : m_cfg(std::move(cfg)) {}

        [[nodiscard]] Elem Render();

    private:
        ComboBoxCfg         m_cfg;
        Signal<bool>        m_open{false};
        Signal<std::string> m_text{};
        Anchor              m_anchor{};
    };
}
