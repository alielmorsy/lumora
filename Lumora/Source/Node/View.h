#pragma once

namespace lumora
{
    class NodeArena;
    struct LiveNode;
    struct ViewState;

    /// The node's visibility + clip flags, read kind-agnostically from whichever payload carries a ViewState; null for kinds without one.
    /// Shared by the paint walk, debug overlay and focus traversal so all agree on "visible".
    [[nodiscard]] const ViewState* ViewOf(NodeArena& arena, const LiveNode& n);

    /// True when the node is shown (no ViewState, or it says visible); invisible subtrees are skipped by paint and focus traversal.
    [[nodiscard]] bool Visible(NodeArena& arena, const LiveNode& n);
} // namespace lumora
