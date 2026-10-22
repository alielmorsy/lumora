#include "Gallery.h"

#include <string>

namespace gallery {

Elem ThemingSection(Theme theme)
{
    Computed<std::string> modeName([theme] { return theme.dark() ? std::string("Dark") : std::string("Light"); });

    const auto swatch = [theme](int i) {
        return Button({.label = kAccents[i],
                       .onClick = [theme, i] { theme.accent = i; },
                       .className = [theme, i] {
                           return std::string("swatch swatch-") + kAccents[i] + (theme.accent() == i ? " selected" : "");
                       }});
    };

    return Column({.className = "section"},
        SectionHeader("Theming", "One signal swaps the root class; CSS variables do the rest."),
        Card("Mode", "Light and dark palettes are two class rules over the same variables.", {
            Row({.className = "toggle-row"},
                Label(Format("{} mode", modeName)),
                Spacer(),
                Switch({.on = theme.dark, .className = "switch"})),
            Row({.className = "row"},
                Button({.label = "Light", .onClick = [theme] { theme.dark = false; }, .variant = ButtonVariant::Outline,
                        .className = "btn btn-outline"}),
                Button({.label = "Dark", .onClick = [theme] { theme.dark = true; }, .variant = ButtonVariant::Outline,
                        .className = "btn btn-outline"})),
        }),
        Card("Accent", "Pick an accent; buttons, switches and highlights follow.", {
            Row({.className = "row"}, swatch(0), swatch(1), swatch(2), swatch(3)),
        }),
        Card("Preview", "Live sample of the current palette.", {
            Column({.className = "preview"},
                Label("Surface", "preview-title"),
                Label("Body text uses --fg, captions use --muted.", "label"),
                Label("Captions read quieter.", "muted"),
                Row({.className = "row"},
                    Button({.label = "Primary", .className = "btn btn-primary"}),
                    Button({.label = "Outline", .variant = ButtonVariant::Outline, .className = "btn btn-outline"}),
                    Switch({.on = Signal<bool>{true}, .className = "switch"}))),
        }));
}

}
