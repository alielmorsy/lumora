#pragma once

#include <lumora/Foundation/NodeId.h>
#include <lumora/Foundation/Rect.h>

#include "Node/Kind.h"
#include "Node/LayoutStyle.h"

#include <cstdint>

namespace lumora {

class Owner;
class NodeArena;
struct LiveNode;

/// The unit of incremental work, classified per property at bind time: Layout (re-solve), Style (restyle without re-layout), Repaint (emit pass).
enum class Dirty : std::uint8_t {
    None    = 0,
    Style   = 1u << 0,
    Layout  = 1u << 1,
    Repaint = 1u << 2,
    All     = Style | Layout | Repaint,
};

[[nodiscard]] constexpr Dirty operator|(Dirty a, Dirty b) noexcept {
    return static_cast<Dirty>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
[[nodiscard]] constexpr Dirty operator&(Dirty a, Dirty b) noexcept {
    return static_cast<Dirty>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}
constexpr Dirty& operator|=(Dirty& a, Dirty b) noexcept { return a = a | b; }

[[nodiscard]] constexpr bool HasFlag(Dirty value, Dirty flag) noexcept {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0;
}

namespace detail {
    /// Publishes `flags` raised on `node`: the ancestor chain gets `descendantDirty` bits (so "any dirt below me" is O(1)) and a Repaint also lands on
    /// the direct parent's `dirty`. Defined in NodeArena.cpp since LiveNode cannot include its arena.
    void PropagateDirty(NodeArena& arena, LiveNode& node, Dirty flags) noexcept;
} // namespace detail

/// Where a node's recorded paint commands live inside its parent's recorded range. Offsets are PARENT-RELATIVE so a clean subtree
/// is republished by copying its range verbatim, with no fixup walk over descendants.
struct PaintSpan {
    std::uint32_t first = 0; ///< byte offset from the start of the parent's recorded range
    std::uint32_t bytes = 0; ///< size of this node's own recorded range (its visual + its subtree)
    bool          valid = false; ///< false until the node has been recorded at least once
};

/// Dense, fixed-size live-tree header. The arena relocates header storage on growth, so links are NodeId, never raw pointers.
/// Per-kind state lives out-of-line via `payload`; new kinds add a payload struct + descriptor row, never a field here.
struct LiveNode {
    LiveKind      kind = LiveKind::Invalid;
    NodeId        id{};

    NodeId        parent{};
    NodeId        firstChild{};
    NodeId        lastChild{};
    NodeId        prevSibling{};
    NodeId        nextSibling{};
    std::uint32_t childCount = 0;

    Owner*        scope = nullptr; ///< reactive scope owning this node's effects + storage
    NodeArena*    arena = nullptr; ///< the arena that allocated this header, so MarkDirty can propagate
    LayoutStyle   style{};         ///< resolved layout inputs the layout bridge reads
    LayoutHandle  layout{};        ///< opaque solver-node handle, null for transparent nodes
    Rect          solved{};        ///< absolute rect, written by the layout pass
    bool          wrapInBox = false; ///< true when this node sits in a definite-width context, so paint reflows it into its box
    PayloadRef    payload{};       ///< out-of-line per-kind state, invalid for stateless leaves
    Dirty         dirty = Dirty::None;
    Dirty         descendantDirty = Dirty::None; ///< union of the flags raised anywhere below this node
    PaintSpan     paint{};         ///< last frame's recorded command range (see PaintSpan)

    /// Raises `flags` on this node and publishes them upward. A Repaint also dirties the direct parent (one level only), so a parent re-records its
    /// child list and picks up a child that appeared, vanished or reordered; the chain above gets `descendantDirty`, so a leaf write never escalates.
    void MarkDirty(Dirty flags) noexcept {
        // Layout and Style always change how the node looks, so both imply Repaint (a Text changing to a same-width string
        // raises Layout, moves no rect, and would otherwise never redraw).
        if (HasFlag(flags, Dirty::Layout) || HasFlag(flags, Dirty::Style)) {
            flags |= Dirty::Repaint;
        }
        dirty |= flags;
        if (arena) {
            detail::PropagateDirty(*arena, *this, flags);
        }
    }
    void ClearDirty(Dirty flags) noexcept {
        dirty = static_cast<Dirty>(static_cast<std::uint8_t>(dirty) & ~static_cast<std::uint8_t>(flags));
    }
    void ClearDescendantDirty(Dirty flags) noexcept {
        descendantDirty = static_cast<Dirty>(static_cast<std::uint8_t>(descendantDirty) &
                                             ~static_cast<std::uint8_t>(flags));
    }
    [[nodiscard]] bool IsDirty(Dirty flags) const noexcept { return HasFlag(dirty, flags); }
    /// True when this node, or anything beneath it, carries any of `flags`.
    [[nodiscard]] bool IsSubtreeDirty(Dirty flags) const noexcept {
        return HasFlag(dirty, flags) || HasFlag(descendantDirty, flags);
    }
};

} // namespace lumora
