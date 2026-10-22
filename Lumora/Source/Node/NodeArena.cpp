#include "Node/NodeArena.h"

#include "Runtime/ChildOrder.h"

namespace lumora {

namespace detail {

void PropagateDirty(NodeArena& arena, LiveNode& node, const Dirty flags) noexcept {
    if (flags == Dirty::None) {
        return;
    }
    LiveNode* parent = arena.TryResolve(node.parent);
    if (!parent) {
        return;
    }
    // One-level widening for Repaint: the parent re-records its child list (see LiveNode::MarkDirty).
    if (HasFlag(flags, Dirty::Repaint)) {
        parent->dirty |= Dirty::Repaint;
    }
    // Then the ancestors' "something below me moved" summary; bailing once the bits are present keeps a burst of writes in one subtree O(1) amortised.
    for (LiveNode* up = parent; up != nullptr; up = arena.TryResolve(up->parent)) {
        const Dirty missing = static_cast<Dirty>(static_cast<std::uint8_t>(flags) &
                                                 ~static_cast<std::uint8_t>(up->descendantDirty));
        if (missing == Dirty::None) {
            break;
        }
        up->descendantDirty |= missing;
    }
}

} // namespace detail

LiveNode& NodeArena::Alloc(const LiveKind kind) {
    std::uint32_t index;
    if (!m_free.empty()) {
        index = m_free.back();
        m_free.pop_back();
    } else {
        index = static_cast<std::uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }

    Slot& slot = m_slots[index];
    slot.node = LiveNode{};
    slot.node.kind = kind;
    slot.node.id = NodeId{index + 1};
    slot.node.arena = this;
    slot.live = true;
    ++m_liveCount;
    return slot.node;
}

void NodeArena::Free(NodeId id) {
    if (!id.Valid()) {
        return;
    }
    const std::uint32_t index = id.value - 1;
    assert(index < m_slots.size());
    Slot& slot = m_slots[index];
    if (!slot.live) {
        return;
    }
    FreePayload(slot.node.payload);
    slot.node = LiveNode{};
    slot.live = false;
    ++slot.generation;
    --m_liveCount;
    m_free.push_back(index);
}

LiveNode& NodeArena::Resolve(NodeId id) {
    assert(id.Valid() && id.value - 1 < m_slots.size() && m_slots[id.value - 1].live);
    return m_slots[id.value - 1].node;
}

const LiveNode& NodeArena::Resolve(NodeId id) const {
    assert(id.Valid() && id.value - 1 < m_slots.size() && m_slots[id.value - 1].live);
    return m_slots[id.value - 1].node;
}

LiveNode* NodeArena::TryResolve(NodeId id) noexcept {
    if (!id.Valid() || id.value - 1 >= m_slots.size() || !m_slots[id.value - 1].live) {
        return nullptr;
    }
    return &m_slots[id.value - 1].node;
}

std::uint32_t NodeArena::GenerationOf(NodeId id) const noexcept {
    if (!id.Valid() || id.value - 1 >= m_slots.size()) {
        return 0;
    }
    return m_slots[id.value - 1].generation;
}

void NodeArena::TrackAnchor(const Ref<detail::AnchorState>& anchor) {
    if (!anchor) {
        return;
    }
    for (const Ref<detail::AnchorState>& existing : m_measuredAnchors) {
        if (existing.Get() == anchor.Get()) {
            return; // already tracked (the node re-bound the same anchor on re-instantiation)
        }
    }
    m_measuredAnchors.push_back(anchor);
}

void NodeArena::TrackAspect(NodeId id) {
    if (!id.Valid()) {
        return;
    }
    const std::uint32_t gen = GenerationOf(id);
    for (const std::pair<NodeId, std::uint32_t>& existing : m_aspectNodes) {
        if (existing.first == id && existing.second == gen) {
            return; // already tracked; a re-resolve of the same node must not duplicate the entry
        }
    }
    m_aspectNodes.emplace_back(id, gen);
}

void NodeArena::RefreshAnchorRects() {
    // Compact first, publish second: `anchor->rect = ...` is a reactive write whose effects can mount a surface that TrackAnchors into the vector erase_if is compacting.
    std::erase_if(m_measuredAnchors, [this](const Ref<detail::AnchorState>& anchor) {
        // unbound or trigger gone - stop mirroring (a re-mount re-tracks it)
        return !anchor || !anchor->id.Valid() || GenerationOf(anchor->id) != anchor->gen;
    });
    // Indexed against a size captured up front: a publish may append an entry that is already current (mirrored next frame anyway).
    const std::size_t tracked = m_measuredAnchors.size();
    for (std::size_t i = 0; i < tracked; ++i) {
        const Ref<detail::AnchorState> anchor = m_measuredAnchors[i];
        if (const LiveNode* node = TryResolve(anchor->id)) {
            // The painted rect, not the scroll-0 solved rect, so a trigger in a scrolled container reports where it is drawn. Equality-gated: notifies only on a real change.
            anchor->rect = detail::PaintedRect(*this, anchor->id, *node);
        }
    }
}

void NodeArena::FreePayload(PayloadRef ref) {
    if (!ref.Valid()) {
        return;
    }
    const auto raw = static_cast<std::uint16_t>(ref.kind);
    if (raw < static_cast<std::uint16_t>(LiveKind::BuiltinCount)) {
        if (const std::unique_ptr<IPayloadPool>& pool = m_builtinPools[raw]) {
            pool->Free(ref.index);
        }
        return;
    }
    const auto it = m_customPools.find(ref.kind);
    if (it != m_customPools.end() && it->second) {
        it->second->Free(ref.index);
    }
}

} // namespace lumora
