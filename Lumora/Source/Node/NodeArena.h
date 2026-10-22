#pragma once

#include <lumora/Foundation/NodeId.h>
#include <lumora/Widgets/Anchor.h>

#include "Node/LiveNode.h"
#include "Node/PayloadPool.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lumora {

/// Owns all live-tree header storage for one app plus the per-kind payload pools. Storage relocates on growth, so refer to nodes by NodeId
/// and resolve through Resolve(); a per-slot generation counter catches stale ids in debug.
class NodeArena {
public:
    [[nodiscard]] LiveNode& Alloc(LiveKind kind);
    void Free(NodeId id);

    [[nodiscard]] LiveNode&       Resolve(NodeId id);
    [[nodiscard]] const LiveNode& Resolve(NodeId id) const;
    [[nodiscard]] LiveNode*       TryResolve(NodeId id) noexcept;

    /// The slot generation backing `id`, bumped on every Free so a recycled NodeId differs from one captured earlier. Returns 0 for an out-of-range id.
    [[nodiscard]] std::uint32_t GenerationOf(NodeId id) const noexcept;

    [[nodiscard]] std::uint32_t LiveCount() const noexcept { return m_liveCount; }

    /// Typed access to the payload pool for `kind`, created on first use (a kind always maps to one payload type, so the downcast is safe).
    /// Built-in kinds index a flat array (no hashing on the hot path); custom kinds use a map.
    template<typename T>
    [[nodiscard]] PayloadPool<T>& Pool(LiveKind kind) {
        std::unique_ptr<IPayloadPool>& slot = PoolSlot(kind);
        if (!slot) {
            slot = std::make_unique<PayloadPool<T>>();
        }
        return static_cast<PayloadPool<T>&>(*slot);
    }

    /// Resolve a node and return its payload as T (debug-asserts the node actually has one).
    template<typename T>
    [[nodiscard]] T& PayloadOf(NodeId id) {
        LiveNode& node = Resolve(id);
        assert(node.payload.Valid() && "node has no payload");
        return Pool<T>(node.payload.kind).Get(node.payload.index);
    }

    /// Type-erased free of a node's out-of-line payload, if any.
    void FreePayload(PayloadRef ref);

    /// Register an anchor (bound to a node via `.ref`) whose solved rect should be mirrored into its
    /// reactive cell each frame, so Anchor::Width()/Bounds() read live. Deduped by identity.
    void TrackAnchor(const Ref<detail::AnchorState>& anchor);

    /// Mirror every tracked anchor's solved rect into its reactive cell and drop anchors whose trigger
    /// has unmounted (a re-mount re-tracks). Call once per frame after the final solve.
    void RefreshAnchorRects();

    /// Registers a node whose LayoutStyle carries an `aspect-ratio` so the post-solve pass iterates a handful of nodes, not the tree. Lives here, not on
    /// AppCtx, because the style resolver (sole writer) is handed the arena only. Deduped; stale entries are pruned by the pass.
    void TrackAspect(NodeId id);
    [[nodiscard]] std::vector<std::pair<NodeId, std::uint32_t>>& AspectNodes() noexcept { return m_aspectNodes; }

private:
    struct Slot {
        LiveNode      node;
        std::uint32_t generation = 0;
        bool          live = false;
    };

    /// The pool storage slot for `kind`, creating the (empty) map entry for custom kinds.
    [[nodiscard]] std::unique_ptr<IPayloadPool>& PoolSlot(LiveKind kind) {
        const auto raw = static_cast<std::uint16_t>(kind);
        if (raw < static_cast<std::uint16_t>(LiveKind::BuiltinCount)) {
            return m_builtinPools[raw];
        }
        return m_customPools[kind];
    }

    static constexpr std::size_t kBuiltinPoolCount = static_cast<std::size_t>(LiveKind::BuiltinCount);

    std::vector<Slot>          m_slots;
    std::vector<std::uint32_t> m_free;
    std::uint32_t              m_liveCount = 0;

    std::array<std::unique_ptr<IPayloadPool>, kBuiltinPoolCount>  m_builtinPools;
    std::unordered_map<LiveKind, std::unique_ptr<IPayloadPool>>   m_customPools;

    std::vector<Ref<detail::AnchorState>> m_measuredAnchors;
    std::vector<std::pair<NodeId, std::uint32_t>> m_aspectNodes; ///< (node, generation at registration)
};

} // namespace lumora
