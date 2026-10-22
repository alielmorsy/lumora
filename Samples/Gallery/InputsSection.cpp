#include "Gallery.h"

#include <cctype>
#include <string>
#include <vector>

namespace gallery {

namespace {

Elem TextFieldsCard()
{
    Signal<std::string> name{""};
    Signal<std::string> submitted{""};
    Computed<std::string> greeting([=] {
        return name().empty() ? std::string("Type your name above.") : "Hello, " + name() + "!";
    });

    return Card("Input", "Single-line text bound to a signal; Enter submits.", {
        Input({.value = name, .placeholder = "Your name", .onSubmit = [submitted](std::string s) { submitted = s; },
               .className = "field"}),
        Label(greeting),
        Show([submitted] { return !submitted().empty(); },
             [submitted] { return Label(Format("Last submitted: {}", submitted), "muted"); }),
    });
}

Elem NumbersCard()
{
    Signal<double> volume{40.0};
    Signal<double> scale{1.0};

    return Card("NumberField & Slider", "Drag the label to scrub, click the value to type.", {
        NumberField({.value = volume, .min = 0.0, .max = 100.0, .decimals = 0, .label = "Volume",
                     .className = "number"}),
        Slider({.value = volume, .min = 0.0, .max = 100.0, .step = 1.0, .layout = {.width = Expand},
                .className = "slider"}),
        NumberField({.value = scale, .min = 0.1, .max = 4.0, .step = 0.01, .decimals = 2, .label = "Scale",
                     .className = "number"}),
        Label(Format("volume {:.0f}%  scale x{:.2f}", volume, scale), "muted"),
    });
}

Elem ComboCard(Theme theme)
{
    static const std::vector<std::string> fruits{"Apple", "Banana", "Cherry", "Mango", "Orange", "Peach", "Pear"};
    Signal<int> fruit{-1};
    Signal<int> size{1};
    Computed<std::string> summary([=] {
        static const char* sizes[] = {"Small", "Medium", "Large"};
        const int f = fruit();
        return f < 0 ? std::string("Nothing picked yet.")
                     : std::string(sizes[size()]) + " " + fruits[static_cast<std::size_t>(f)] + " smoothie";
    });

    const auto makeCombo = [theme](ComboBoxCfg cfg) {
        cfg.style = {
            .fill = [theme] { return theme.dark() ? Color::Rgba(36, 38, 46, 255) : Color::Rgba(255, 255, 255, 255); },
            .hoverFill = [theme] { return theme.dark() ? Color::Rgba(46, 48, 58, 255) : Color::Rgba(233, 236, 242, 255); },
            .radius = 4.f,
            .border = {1.f, theme.dark() ? Color::Rgba(60, 63, 72, 255) : Color::Rgba(221, 225, 232, 255)},
        };
        cfg.panelFill = [theme] { return theme.dark() ? Color::Rgba(30, 32, 39, 252) : Color::Rgba(255, 255, 255, 252); };
        cfg.panelBorder = [theme] { return theme.dark() ? Color::Rgba(60, 63, 72, 255) : Color::Rgba(221, 225, 232, 255); };
        cfg.textColor = [theme] { return theme.dark() ? Color::Rgba(228, 230, 236, 255) : Color::Rgba(27, 31, 39, 255); };
        cfg.dim = [theme] { return theme.dark() ? Color::Rgba(150, 153, 162, 255) : Color::Rgba(102, 112, 133, 255); };
        return ComboBox(std::move(cfg));
    };

    return Card("ComboBox", "Plain dropdown and an editable, filtering one.", {
        Row({.className = "row"},
            makeCombo({.options = fruits, .selected = fruit, .editable = true, .placeholder = "Search fruit..."}),
            makeCombo({.options = {"Small", "Medium", "Large"}, .selected = size, .width = 140.f})),
        Label(summary),
    });
}

Elem EditorCard()
{
    constexpr int kLimit = 280;
    Signal<std::string> notes{"Lumora text editor.\nMulti-line, with undo, selection and clipboard."};
    Computed<int> chars([=] { return static_cast<int>(notes().size()); });
    Computed<int> words([=] {
        int count = 0;
        bool inWord = false;
        for (const char c : notes()) {
            const bool space = std::isspace(static_cast<unsigned char>(c)) != 0;
            if (!space && !inWord) ++count;
            inWord = !space;
        }
        return count;
    });
    Computed<bool> over([=] { return chars() > kLimit; });

    return Card("Text editor", "Multi-line Input with a live character count.", {
        Input({.value = notes, .multiLine = true, .layout = {.width = Expand, .height = Fixed(140.f)},
               .className = "field editor"}),
        Row({.className = "row"},
            Label(Format("{} / {} characters", chars, kLimit),
                  "muted"),
            Label(Format("{} words", words), "muted"),
            Spacer(),
            Show(over,
                 [] { return Badge({.label = "Too long", .variant = BadgeVariant::Danger}); },
                 [] { return Badge({.label = "OK", .variant = BadgeVariant::Success}); }),
            Button({.label = "Clear", .onClick = [notes] { notes = ""; }, .variant = ButtonVariant::Ghost,
                    .className = "btn btn-ghost"})),
    });
}

}

Elem InputsSection(Theme theme)
{
    return Column({.className = "section"},
        SectionHeader("Inputs", "Text, numbers, choices and a multi-line editor."),
        TextFieldsCard(),
        NumbersCard(),
        ComboCard(theme),
        EditorCard());
}

}
