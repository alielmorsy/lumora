#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Props.h>

#include <cstdint>
#include <functional>
#include <string>

namespace lumora
{
    /// Materializes only the rows the viewport can show (Scroll + Each would build a solver node per item). The window comes
    /// from the solved viewport and scroll offset after the first solve; layout re-runs only when it moves. Rows are addressed by index.
    /// Requires a uniform `itemExtent`; a non-positive one lays rows out plainly and warns.
    /// `ScrollAxis::Both` also scrolls horizontally: rows stretch to the widest materialized row or the viewport, so they must not
    /// size themselves by percentage width.
    struct VirtualListCfg
    {
        Reactive<int> count; ///< total logical rows
        float itemExtent = 0.f; ///< uniform row size along the scroll axis, in px
        int overscan = 3; ///< rows materialized beyond each edge
        ScrollAxis axis = ScrollAxis::Vertical;

        /// Bump to say the data behind the indices changed: every materialized row rebuilds. Needed only when a row's content changes
        /// under a stable index; leave unset if rows read values reactively.
        Reactive<std::uint64_t> version{};

        /// Builds row `index`. Runs under the row's own reactive scope, so a row may own local state.
        std::function<Elem(int index)> row;

        /// Row identity for reconciliation. Unset means index is identity: fine for lists that never shift,
        /// wrong where an insert would renumber each row's state. A hierarchy passes the entity id.
        std::function<std::uint64_t(int index)> key;

        LayoutProps layout{};
        StyleProps style{};
        /// Clips by default: it is a viewport, and the row below the fold is only partly built.
        ViewProps view{.visible = true, .clip = true};
        TransformProps transform{};
        InteractProps on{};

        Str                   className{};
    };

    LUMORA_API Elem VirtualList(VirtualListCfg cfg);
}
