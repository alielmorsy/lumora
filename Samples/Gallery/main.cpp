#include "Gallery.h"

#include <SampleHost.h>

#include <algorithm>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gallery {

namespace {

struct NavEntry {
    const char* title;
    const char* hint;
    Elem (*build)(const Theme&);
};

const NavEntry kSections[] = {
    {"Buttons & toggles", "Button, Switch, Checkbox, Chip, Badge", [](const Theme& t) { return ButtonsSection(t); }},
    {"Inputs", "Input, NumberField, ComboBox, editor", [](const Theme& t) { return InputsSection(t); }},
    {"Lists", "Scroll, VirtualList, filter", [](const Theme& t) { return ListsSection(t); }},
    {"Layout", "Splitter, Accordion, Grid, Breadcrumb", [](const Theme& t) { return LayoutSection(t); }},
    {"Theming", "Light/dark and accents", [](const Theme& t) { return ThemingSection(t); }},
};

}

constexpr int kSectionCount = static_cast<int>(std::size(kSections));

// App shell: sidebar navigation, a theme toggle, and one mounted section at a time.
class GalleryApp {
public:
    explicit GalleryApp(int section = 0, bool dark = true) : m_theme{Signal<bool>(dark), Signal<int>(0)}, m_section(section) {}

    Elem Render()
    {
        const Theme theme = m_theme;
        const Signal<int> current = m_section;

        Elem nav = Column({.className = "nav"},
            Each(std::span(kSections), [current](const NavEntry&, int i) {
                return Button({.label = kSections[i].title,
                               .onClick = [current, i] { current = i; },
                               .variant = ButtonVariant::Ghost,
                               .className = [current, i] { return std::string(current() == i ? "nav-item active" : "nav-item"); }});
            }));

        Elem sidebar = Column({.className = "sidebar"},
            Column({.className = "brand"},
                Label("Lumora", "brand-title"),
                Label("Widget gallery", "brand-sub")),
            std::move(nav),
            Spacer(),
            Row({.className = "theme-toggle"},
                Label(Format("{}", Computed<std::string>([theme] {
                    return theme.dark() ? std::string("Dark mode") : std::string("Light mode");
                })), "muted"),
                Spacer(),
                Switch({.on = theme.dark, .className = "switch"})));

        // Show keeps only the active section mounted; the others dispose their scopes.
        std::vector<Elem> pages;
        for (int i = 0; i < kSectionCount; ++i)
            pages.push_back(Show([current, i] { return current() == i; }, [theme, i] { return kSections[i].build(theme); }));

        Elem main = Column({.className = "main"},
            Row({.className = "topbar"},
                Label(Str([current] { return std::string(kSections[current()].title); }), "topbar-title"),
                Spacer(),
                Label(Str([current] { return std::string(kSections[current()].hint); }), "topbar-hint")),
            Scroll({.layout = {.width = Expand, .height = Expand}, .className = "content"},
                Column({.className = "content-inner"}, pages)));

        return Row({.layout = {.width = Expand, .height = Expand},
                    .className = [theme] {
                        return std::string("app ") + (theme.dark() ? "dark" : "light") + " accent-"
                             + kAccents[theme.accent()];
                    }},
            std::move(sidebar),
            std::move(main));
    }

private:
    Theme       m_theme{};
    Signal<int> m_section;
};

}

int main(int argc, char** argv)
{
    // `--section N` opens on section N (lets the headless smoke run mount every page).
    int section = 0;
    bool dark = true;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--section" && i + 1 < argc)
            section = std::clamp(std::atoi(argv[i + 1]), 0, gallery::kSectionCount - 1);
        if (std::string_view(argv[i]) == "--light")
            dark = false;
    }

    return lumora::samples::RunSample({.title  = "Lumora Gallery",
                                       .width  = 1280,
                                       .height = 820,
                                       .root   = [section, dark] { return lumora::Elem(gallery::GalleryApp{section, dark}); },
                                       .css    = gallery::kStylesheet},
                                      argc, argv);
}
