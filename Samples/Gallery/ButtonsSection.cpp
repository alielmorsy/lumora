#include "Gallery.h"

#include <algorithm>
#include <vector>

namespace gallery {

namespace {

Elem ButtonsCard()
{
    Signal<int> clicks{0};
    return Card("Buttons", "Four variants, one click counter.", {
        Row({.className = "row"},
            Button({.label = "Primary", .onClick = [clicks] { clicks += 1; }, .className = "btn btn-primary"}),
            Button({.label = "Outline", .onClick = [clicks] { clicks += 1; }, .variant = ButtonVariant::Outline,
                    .className = "btn btn-outline"}),
            Button({.label = "Ghost", .onClick = [clicks] { clicks += 1; }, .variant = ButtonVariant::Ghost,
                    .className = "btn btn-ghost"}),
            Button({.label = "Reset", .onClick = [clicks] { clicks = 0; }, .variant = ButtonVariant::Text,
                    .className = "btn btn-text"})),
        Label(Format("Clicked {} times", clicks), "muted"),
    });
}

Elem TogglesCard()
{
    Signal<bool> wifi{true};
    Signal<bool> sync{false};
    Signal<bool> beta{false};
    Signal<bool> agree{true};

    const auto toggleRow = [](Str name, Signal<bool> on) {
        return Row({.className = "toggle-row"},
            Label(std::move(name)),
            Spacer(),
            Show(on,
                 [] { return Badge({.label = "On", .variant = BadgeVariant::Success}); },
                 [] { return Badge({.label = "Off", .variant = BadgeVariant::Neutral}); }),
            Switch({.on = on, .className = "switch"}));
    };

    Computed<int> enabled([=] { return int(wifi()) + int(sync()) + int(beta()); });

    return Card("Switches & checkboxes", "Two-way bound signals; badges follow live.", {
        toggleRow("Wi-Fi", wifi),
        toggleRow("Cloud sync", sync),
        toggleRow("Beta features", beta),
        Row({.className = "row"},
            Checkbox({.checked = agree, .label = "I agree to the terms", .className = "checkbox"}),
            Checkbox({.checked = beta, .label = "Mirror beta", .disabled = [agree] { return !agree(); }, .className = "checkbox"})),
        Label(Format("{} of 3 switches enabled", enabled), "muted"),
    });
}

struct Tag {
    int         id;
    std::string name;
    bool operator==(const Tag&) const = default;
};

Elem ChipsCard()
{
    const std::vector<Tag> seed{{1, "design"}, {2, "ui"}, {3, "c++20"}, {4, "reactive"}, {5, "css"}};
    Signal<std::vector<Tag>> tags{seed};
    Signal<int> nextId{6};

    return Card("Chips & badges", "Removable chips over a keyed Each.", {
        Row({.className = "chip-row"},
            Each(tags, &Tag::id, [tags](const Tag& t) {
                const int id = t.id;
                return Chip({
                    .label    = t.name,
                    .dotColor = Color::Rgba(76, 141, 255, 255),
                    .onRemove = [tags, id] {
                        tags.Update([id](std::vector<Tag>& v) {
                            std::erase_if(v, [id](const Tag& x) { return x.id == id; });
                        });
                    },
                    .className = "chip",
                });
            })),
        Row({.className = "row"},
            Button({.label = "Add tag",
                    .onClick = [tags, nextId] {
                        const int id = nextId.Peek();
                        nextId += 1;
                        tags.Update([id](std::vector<Tag>& v) { v.push_back({id, "tag-" + std::to_string(id)}); });
                    },
                    .variant = ButtonVariant::Outline, .className = "btn btn-outline"}),
            Button({.label = "Reset", .onClick = [tags, seed] { tags = seed; }, .variant = ButtonVariant::Ghost,
                    .className = "btn btn-ghost"}),
            Spacer(),
            Badge({.label = Format("{} tags", Computed<int>([tags] { return int(tags().size()); })),
                   .variant = BadgeVariant::Info})),
        Row({.className = "row"},
            Badge({.label = "Neutral"}),
            Badge({.label = "Info", .variant = BadgeVariant::Info}),
            Badge({.label = "Success", .variant = BadgeVariant::Success}),
            Badge({.label = "Warn", .variant = BadgeVariant::Warn}),
            Badge({.label = "Danger", .variant = BadgeVariant::Danger})),
    });
}

}

Elem ButtonsSection(Theme)
{
    return Column({.className = "section"},
        SectionHeader("Buttons & toggles", "Buttons, switches, checkboxes, chips and badges."),
        ButtonsCard(),
        TogglesCard(),
        ChipsCard());
}

}
