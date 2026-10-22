#include "Gallery.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace gallery {

namespace {

constexpr int kRowCount = 10000;

struct Asset {
    std::string name;
    std::string kind;
    int         sizeKb;
};

const std::vector<Asset>& Assets()
{
    static const std::vector<Asset> assets = [] {
        static const char* kinds[] = {"Texture", "Mesh", "Material", "Audio", "Script", "Prefab"};
        static const char* words[] = {"rock", "tree", "hero", "door", "lamp", "crate", "river", "cloud", "wall", "gem"};
        std::vector<Asset> v;
        v.reserve(kRowCount);
        for (int i = 0; i < kRowCount; ++i)
            v.push_back({std::string(words[(i * 7) % 10]) + "_" + std::to_string(i), kinds[i % 6], 4 + (i * 37) % 2048});
        return v;
    }();
    return assets;
}

std::string Lower(std::string s)
{
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

Elem VirtualListCard()
{
    Signal<std::string> filter{""};
    // Indices into Assets() that match the filter, by name or kind.
    Computed<std::vector<int>> matches([filter] {
        const std::string needle = Lower(filter());
        std::vector<int> out;
        const auto& all = Assets();
        for (int i = 0; i < static_cast<int>(all.size()); ++i)
            if (needle.empty() || Lower(all[i].name).find(needle) != std::string::npos
                || Lower(all[i].kind).find(needle) != std::string::npos)
                out.push_back(i);
        return out;
    });
    Computed<int> count([matches] { return static_cast<int>(matches().size()); });

    // Row text reads the match list reactively, so filtering re-labels materialized rows without a rebuild.
    const auto field = [matches](int row, auto pick) {
        return Str([matches, row, pick] {
            const auto& m = matches();
            return row < static_cast<int>(m.size()) ? pick(Assets()[static_cast<std::size_t>(m[row])]) : std::string{};
        });
    };

    return Card("VirtualList", "10,000 rows, only the visible ones are built.", {
        Row({.className = "row"},
            Input({.value = filter, .placeholder = "Filter by name or kind (try 'mesh' or 'hero_1')",
                   .layout = {.width = Expand}, .className = "field"}),
            Badge({.label = Format("{} / {}", count, kRowCount), .variant = BadgeVariant::Info})),
        Row({.className = "list-head"},
            Label("Name", "list-col list-name"),
            Label("Kind", "list-col list-kind"),
            Label("Size", "list-col list-size")),
        VirtualList({
            .count      = count,
            .itemExtent = 28.f,
            .row        = [field](int i) {
                return Row({.className = i % 2 ? "list-row odd" : "list-row"},
                    Text(field(i, [](const Asset& a) { return a.name; }), {.className = "list-name"}),
                    Text(field(i, [](const Asset& a) { return a.kind; }), {.className = "list-kind"}),
                    Text(field(i, [](const Asset& a) { return std::to_string(a.sizeKb) + " KB"; }),
                         {.className = "list-size"}));
            },
            .layout    = {.width = Expand, .height = Fixed(320.f)},
            .className = "vlist",
        }),
        Show([count] { return count() == 0; }, [] { return Label("No rows match.", "muted"); }),
    });
}

struct Note {
    int         id;
    std::string text;
    bool operator==(const Note&) const = default;
};

Elem ScrollCard()
{
    Signal<std::vector<Note>> notes{std::vector<Note>{{1, "Mount once, react forever."}, {2, "Key rows by identity."}}};
    Signal<int> nextId{3};

    return Card("Scroll + Each", "A keyed list inside a plain Scroll.", {
        Row({.className = "row"},
            Button({.label = "Add note",
                    .onClick = [notes, nextId] {
                        const int id = nextId.Peek();
                        nextId += 1;
                        notes.Update([id](std::vector<Note>& v) { v.push_back({id, "Note #" + std::to_string(id)}); });
                    },
                    .className = "btn btn-primary"}),
            Button({.label = "Remove first",
                    .onClick = [notes] {
                        notes.Update([](std::vector<Note>& v) { if (!v.empty()) v.erase(v.begin()); });
                    },
                    .variant = ButtonVariant::Outline, .className = "btn btn-outline"})),
        Scroll({.layout = {.width = Expand, .height = Fixed(160.f)}, .className = "scroll-box"},
            Column({.className = "note-list"},
                Each(notes, &Note::id, [](const Note& n) {
                    return Row({.className = "note"}, Badge({.label = "#" + std::to_string(n.id)}), Label(n.text));
                }))),
    });
}

}

Elem ListsSection(Theme)
{
    return Column({.className = "section"},
        SectionHeader("Lists", "Virtualized rows with a live filter, and a keyed Each."),
        VirtualListCard(),
        ScrollCard());
}

}
