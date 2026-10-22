#pragma once

#include <lumora/Style/Stylesheet.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace lumora::style
{
    /// Opaque per-node handle for the matcher. 0 is the null handle (no parent). Callers map it to
    /// whatever identity they use (NodeId.value at runtime, a vector index in tests).
    using NodeHandle = uint32_t;

    /// Tree access injected as callbacks so the matcher is testable in isolation and the resolver can make `classes`/`pseudo`
    /// read signals (subscribing the effect to exactly what was tested).
    struct MatchOps
    {
        std::function<NodeHandle(NodeHandle)> parent;                    ///< parent handle, 0 if root
        std::function<const std::vector<StringId>&(NodeHandle)> classes;
        std::function<PseudoMask(NodeHandle)> pseudo;
        std::function<bool(NodeHandle)> isRoot;
    };

    /// Does one compound (classes AND pseudo AND :root) hold for this node?
    [[nodiscard]] bool MatchesCompound(const CompoundSelector& c, NodeHandle n, const MatchOps& ops);

    /// Right-to-left match of the full complex selector, backtracking on descendant combinators.
    [[nodiscard]] bool MatchesSelector(const Selector& sel, NodeHandle n, const MatchOps& ops);
} // namespace lumora::style
