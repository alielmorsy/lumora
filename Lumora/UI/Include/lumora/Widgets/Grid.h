#pragma once

#include <lumora/Api.h>
#include <lumora/Ref.h>
#include <lumora/Detail/Node.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Children.h>
#include <lumora/Widgets/Props.h>

#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace lumora
{
    /// One track. Px is an absolute size; Fr takes a share of the space left after px tracks and gaps (`{Px(120), 1_fr, 2_fr}`
    /// = 120px sidebar then 1:2). Fr needs a definite extent: under a Wrap axis an fr track collapses to 0.
    struct TrackSize
    {
        enum class Kind : std::uint8_t { Px, Fr } kind = Kind::Fr;
        float value = 1.f;

        [[nodiscard]] static constexpr TrackSize MakePx(float px) noexcept { return {Kind::Px, px}; }
        [[nodiscard]] static constexpr TrackSize MakeFr(float fr) noexcept { return {Kind::Fr, fr}; }

        [[nodiscard]] constexpr bool operator==(const TrackSize&) const noexcept = default;
    };

    [[nodiscard]] constexpr TrackSize Px(float px) noexcept { return TrackSize::MakePx(px); }
    [[nodiscard]] constexpr TrackSize Fr(float fr) noexcept { return TrackSize::MakeFr(fr); }

    inline namespace literals
    {
        [[nodiscard]] constexpr TrackSize operator""_fr(unsigned long long v) noexcept
        {
            return TrackSize::MakeFr(static_cast<float>(v));
        }
        [[nodiscard]] constexpr TrackSize operator""_fr(long double v) noexcept
        {
            return TrackSize::MakeFr(static_cast<float>(v));
        }
    }

    /// Track template for one axis: a brace list of TrackSize, or a count `N` meaning N equal `1_fr` tracks (`.columns = 3`).
    struct TrackList
    {
        std::vector<TrackSize> tracks;

        TrackList() = default;

        TrackList(int count) // NOLINT(*-explicit-constructor): the `.columns = 3` shorthand is intentional
            : tracks(count > 0 ? static_cast<std::size_t>(count) : 0, TrackSize::MakeFr(1.f))
        {
        }

        TrackList(std::initializer_list<TrackSize> list) // NOLINT(*-explicit-constructor)
            : tracks(list)
        {
        }

        [[nodiscard]] std::size_t Count() const noexcept { return tracks.size(); }
        [[nodiscard]] bool Empty() const noexcept { return tracks.empty(); }
    };

    /// Grid container config; shared groups behave as on Box. `gap` sets both axes, `colGap`/`rowGap` override one when >= 0.
    /// Rows beyond `rows` are sized by `autoRows` (default an equal fr share of a definite height; give px for a Wrap-height gallery).
    struct GridCfg
    {
        TrackList columns{};
        TrackList rows{};
        float gap = 0.f;
        float colGap = -1.f;
        float rowGap = -1.f;
        TrackSize autoRows = TrackSize::MakeFr(1.f);

        LayoutProps layout{};
        StyleProps style{};
        ViewProps view{};
        TransformProps transform{};
        PositionProps position{};
        bool anchor = false;
        InteractProps on{};
        Str                   className{};
    };

    /// A grid item with explicit placement; `col`/`row` are 0-based, -1 = auto-flow. A bare grid child auto-flows as 1x1, so
    /// Cell is only needed for placement or spanning.
    struct CellCfg
    {
        int col = -1;
        int row = -1;
        int colSpan = 1;
        int rowSpan = 1;

        LayoutProps layout{};
        StyleProps style{};
        ViewProps view{};
        TransformProps transform{};
        PositionProps position{};
        bool anchor = false;
        InteractProps on{};
        Str                   className{};
    };

    namespace detail
    {
        LUMORA_API Ref<Node> BuildGrid(GridCfg cfg);
        LUMORA_API Ref<Node> BuildCell(CellCfg cfg);
    }

    /// CSS-Grid-style container: tracks sized in Lumora, each cell laid out by the flex solver inside its rect.
    template <typename... Kids>
    Elem Grid(GridCfg cfg = {}, Kids&&... kids)
    {
        Ref<detail::Node> node = detail::BuildGrid(std::move(cfg));
        detail::AppendKids(node, std::forward<Kids>(kids)...);
        return Elem(std::move(node));
    }

    /// A placed grid item (see CellCfg). Outside a Grid it behaves like a plain Box.
    template <typename... Kids>
    Elem Cell(CellCfg cfg = {}, Kids&&... kids)
    {
        Ref<detail::Node> node = detail::BuildCell(std::move(cfg));
        detail::AppendKids(node, std::forward<Kids>(kids)...);
        return Elem(std::move(node));
    }
}
