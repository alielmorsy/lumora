#include "Gallery.h"

#include <string>
#include <vector>

namespace gallery {

namespace {

Elem SplitterCard(Theme theme)
{
    Signal<float> leftPx{220.f};

    return Card("Splitter", "Drag the handle; the left pane width is a signal.", {
        Row({.layout = {.width = Expand, .height = Fixed(160.f)}, .className = "split"},
            Column({.layout = {.width = [leftPx] { return Fixed(leftPx()); }, .height = Expand},
                    .className = "pane"},
                Label("Left pane", "pane-title"),
                Label(Format("{:.0f} px wide", leftPx), "muted")),
            Splitter({
                .minPanePx = 120.f,
                .color     = [theme] { return theme.dark() ? Color::Rgba(58, 64, 74, 255) : Color::Rgba(221, 225, 232, 255); },
                .onDrag    = [leftPx](float prev, float) { leftPx = prev; },
                .className = "splitter",
            }),
            Column({.layout = {.width = Expand, .height = Expand}, .className = "pane"},
                Label("Right pane", "pane-title"),
                Label("Takes the remaining space.", "muted"))),
    });
}

Elem AccordionCard()
{
    Signal<bool> advancedOpen{false};

    return Card("Accordion", "Collapsed bodies are not built until opened.", {
        Accordion({.title = "General", .defaultOpen = true, .className = "accordion"}, [] {
            return Column({.className = "acc-body"},
                Label("Name, description and tags live here."),
                Checkbox({.checked = Signal<bool>{true}, .label = "Visible in search", .className = "checkbox"}));
        }),
        Accordion({.title = Format("Advanced ({})", Computed<std::string>([advancedOpen] {
                       return advancedOpen() ? std::string("open") : std::string("closed");
                   })),
                   .open = Bind<bool>(advancedOpen),
                   .className = "accordion"},
                  [] {
                      return Column({.className = "acc-body"},
                          NumberField({.value = Signal<double>{0.5}, .min = 0.0, .max = 1.0, .decimals = 2,
                                       .label = "Threshold", .className = "number"}));
                  }),
        Foldout({.title = "Notes", .defaultOpen = false, .className = "accordion"},
                {Column({.className = "acc-body"}, Label("A Foldout wraps already-built children."))}),
    });
}

Elem GridCard()
{
    const auto tile = [](const char* name, const char* cls) {
        return Column({.className = std::string("tile ") + cls}, Label(name, "tile-label"));
    };

    return Card("Grid", "Fixed sidebar track plus two fr columns, with a spanning cell.", {
        Grid({.columns = {Px(120.f), 1_fr, 2_fr}, .gap = 8.f, .autoRows = Px(64.f),
              .layout = {.width = Expand}, .className = "grid"},
            Cell({.col = 0, .row = 0, .rowSpan = 2, .className = "cell"}, tile("Sidebar", "tile-a")),
            Cell({.className = "cell"}, tile("1fr", "tile-b")),
            Cell({.className = "cell"}, tile("2fr", "tile-c")),
            Cell({.colSpan = 2, .className = "cell"}, tile("Spans two columns", "tile-d"))),
    });
}

Elem BreadcrumbCard(Theme theme)
{
    Signal<std::vector<std::string>> path{std::vector<std::string>{"Home", "Projects", "Lumora", "Samples"}};
    static const char* folders[] = {"Assets", "Textures", "Characters", "Hero", "Variants", "Night"};

    return Card("Breadcrumb", "Click a crumb to go up; collapses past five.", {
        Breadcrumb({.path = [path] { return path(); },
                    .onSelect = [path](int i) {
                        path.Update([i](std::vector<std::string>& v) { v.resize(static_cast<std::size_t>(i) + 1); });
                    },
                    .separator = ">",
                    .color = [theme] { return theme.dark() ? Color::Rgba(198, 202, 210, 255) : Color::Rgba(102, 112, 133, 255); },
                    .activeColor = [theme] { return theme.dark() ? Color::Rgba(236, 238, 242, 255) : Color::Rgba(27, 31, 39, 255); },
                    .dimColor = [theme] { return theme.dark() ? Color::Rgba(126, 132, 142, 255) : Color::Rgba(150, 153, 162, 255); },
                    .className = "crumbs"}),
        Row({.className = "row"},
            Button({.label = "Open folder",
                    .onClick = [path] {
                        path.Update([](std::vector<std::string>& v) { v.push_back(folders[v.size() % 6]); });
                    },
                    .variant = ButtonVariant::Outline, .className = "btn btn-outline"}),
            Button({.label = "Root", .onClick = [path] { path = std::vector<std::string>{"Home"}; },
                    .variant = ButtonVariant::Ghost, .className = "btn btn-ghost"}),
            Spacer(),
            Label(Format("depth {}", Computed<int>([path] { return static_cast<int>(path().size()); })), "muted")),
    });
}

}

Elem LayoutSection(Theme theme)
{
    return Column({.className = "section"},
        SectionHeader("Layout", "Splitter, Accordion, Grid and Breadcrumb."),
        SplitterCard(theme),
        AccordionCard(),
        GridCard(),
        BreadcrumbCard(theme));
}

}
