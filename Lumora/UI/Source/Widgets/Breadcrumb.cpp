#include <lumora/Widgets/Breadcrumb.h>

#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/ControlFlow.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/Text.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace lumora
{
    namespace
    {
        constexpr const char* kEllipsis = "\xE2\x80\xA6"; // U+2026

        /// One rendered element. Flat segments because `Each` keys one flat list and the
        /// separators and collapsed marker are positional too.
        struct Segment
        {
            enum class Kind : std::uint8_t { Crumb, Separator, Marker };

            Kind        kind  = Kind::Crumb;
            int         index = -1;   ///< index into the caller's path, for Crumb
            std::string label;
            bool        last = false;

            [[nodiscard]] bool operator==(const Segment&) const = default;
        };

        /// Runs inside `Each`'s reconciling scope, so the `path` read is tracked.
        [[nodiscard]] std::vector<Segment> Flatten(const std::vector<std::string>& path, int maxVisible,
                                                    const std::string& separator)
        {
            const std::size_t count = path.size();

            std::vector<std::size_t> shown;
            bool                     collapsed = false;
            if (maxVisible <= 0 || count <= static_cast<std::size_t>(std::max(maxVisible, 3)))
            {
                shown.reserve(count);
                for (std::size_t i = 0; i < count; ++i) shown.push_back(i);
            }
            else
            {
                // Never drop the last crumb: it is the part that says where you are.
                const std::size_t budget = static_cast<std::size_t>(std::max(maxVisible, 3));
                const std::size_t tail   = budget - 2; // first + marker take the other two slots
                shown.push_back(0);
                collapsed = true;
                for (std::size_t i = count - tail; i < count; ++i) shown.push_back(i);
            }

            std::vector<Segment> out;
            out.reserve(shown.size() * 2 + 2);
            for (std::size_t slot = 0; slot < shown.size(); ++slot)
            {
                const std::size_t i = shown[slot];
                if (slot > 0)
                {
                    out.push_back(Segment{Segment::Kind::Separator, static_cast<int>(i), separator, false});
                    if (slot == 1 && collapsed)
                    {
                        out.push_back(Segment{Segment::Kind::Marker, -1, {}, false});
                        out.push_back(
                            Segment{Segment::Kind::Separator, static_cast<int>(i), separator, false});
                    }
                }
                out.push_back(Segment{Segment::Kind::Crumb, static_cast<int>(i), path[i],
                                      i + 1 == count});
            }
            return out;
        }
    } // namespace

    Elem Breadcrumb(BreadcrumbCfg cfg)
    {
        LayoutProps layout = cfg.layout;
        layout.gap         = cfg.gap;
        layout.cross       = Align::Center;

        auto source = [path = cfg.path, maxVisible = cfg.maxVisible,
                       separator = cfg.separator]() mutable -> std::vector<Segment>
        {
            if (!path) return {};
            return Flatten(path(), maxVisible.Get(), separator);
        };

        // Key must include position, kind, label and `last`: a reused row's builder does not
        // re-run (see `RowKey`), so anything omitted goes stale.
        const auto key = [](const Segment& segment)
        {
            return RowKey(segment.index, static_cast<std::uint8_t>(segment.kind), segment.last,
                          segment.label);
        };

        return Row(RowCfg{.layout = std::move(layout), .className = std::move(cfg.className)},
                   Each(std::move(source), key,
                        [cfg](const Segment& segment) -> Elem
                        {
                            switch (segment.kind)
                            {
                            case Segment::Kind::Separator:
                                return Text(segment.label,
                                            {.size = cfg.fontSize, .color = cfg.dimColor});

                            case Segment::Kind::Marker:
                            {
                                Elem marker = cfg.ellipsis
                                                  ? cfg.ellipsis
                                                  : Text(kEllipsis, {.size  = cfg.fontSize,
                                                                     .color = cfg.dimColor});
                                // Wrapped only when pressable: an inert marker should not be a hit target.
                                if (!cfg.onEllipsis) return marker;
                                return Box(BoxCfg{.layout = {.cross = Align::Center},
                                                  .on     = {.onClick = cfg.onEllipsis}},
                                           std::move(marker));
                            }

                            case Segment::Kind::Crumb:
                            default:
                            {
                                const Reactive<Color> color = segment.last ? cfg.activeColor : cfg.color;
                                Elem        label = Text(segment.label,
                                                         {.size = cfg.fontSize, .color = color});
                                if (!cfg.onSelect) return label;

                                const int index = segment.index;
                                std::function<void(int)> select = cfg.onSelect;
                                return Box(BoxCfg{.layout = {.cross = Align::Center},
                                                  .on = {.onClick = [select, index] { select(index); }}},
                                           std::move(label));
                            }
                            }
                        }));
    }
} // namespace lumora
