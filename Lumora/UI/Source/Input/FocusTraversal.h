#pragma once

#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/NodeId.h>

#include "Input/Interaction.h"

#include <cstdint>
#include <vector>

namespace lumora
{
    class NodeArena;

    enum class FocusDir : std::uint8_t { Forward, Backward };

    namespace FocusTraversal
    {
        /// Collects the ordered Tab ring for `scope`: pre-order DFS through transparent hosts, skipping invisible subtrees and non-stops.
        /// Positive tabIndex first (ascending, ties by tree order), then tabIndex 0 in tree order; negative is excluded.
        void CollectRing(const EventContext& base, NodeId scope, std::vector<NodeRef>& out);

        /// Moves focus one step in `dir` within the active scope (topmost focus-trapping overlay, else `scopeRoot`), wrapping at the ends;
        /// focuses the first/last stop when focus is null, stale or outside the scope. Scrolls the landing node into view.
        void Move(const EventContext& base, InteractionState& s, NodeId scopeRoot, FocusDir dir);

        /// Moves focus directionally within the active scope: explicit NavOverrides on the focused node first, else the best ring candidate in the
        /// half-plane of `dir` by primary_dist + 3 * ortho_dist (halved if overlapping on the orthogonal axis). Returns true if focus moved.
        bool MoveDirectional(const EventContext& base, InteractionState& s, NodeId scopeRoot, Direction dir);
    } // namespace FocusTraversal
} // namespace lumora
