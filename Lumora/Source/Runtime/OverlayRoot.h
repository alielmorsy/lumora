#pragma once

#include <lumora/Elem.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/NodeId.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Overlay/Positioner.h>
#include <lumora/Widgets/Anchor.h>
#include <lumora/Widgets/Overlay.h>

#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace lumora
{
    class NodeArena;

    /// One live portal registration (dismissal/hit-test metadata). The content subtree is spliced under `layerNode` and owned by the declaring scope, not here.
    struct PortalEntry
    {
        std::uint64_t id = 0;
        std::uint64_t seq = 0; ///< monotonic open order; highest (layer, seq) is the topmost surface
        Layer layer = Layer::Popover;
        NodeId content;        ///< spliced subtree root, under the layer node
        NodeId layerNode;
        PortalOptions options;
    };

    /// One anchored-placement registration: each frame between layout and paint the positioner sizes the panel against its anchor and writes its inset.
    struct PositionerEntry
    {
        Ref<detail::AnchorState> anchor;
        NodeId                   panel;
        Placement                base{};
        float                    offset = 0.f;
        Spacing                  viewportPad{};
        std::vector<Middleware>  middleware;
        std::function<void(const PositionerResult&)> onResult; ///< optional (arrow / hidden handling)
    };

    /// Per-app overlay registry: owns the named layer nodes (full-surface containing blocks, top siblings of the user root) and tracks open portals for dismissal.
    class OverlayRoot
    {
    public:
        OverlayRoot() = default;

        void SetLayerNode(Layer layer, NodeId node) noexcept { m_layerNodes[Index(layer)] = node; }
        [[nodiscard]] NodeId LayerNode(Layer layer) const noexcept { return m_layerNodes[Index(layer)]; }

        [[nodiscard]] std::uint64_t NextId() noexcept { return m_nextId++; }
        [[nodiscard]] std::uint64_t NextSeq() noexcept { return m_nextSeq++; }
        void Register(PortalEntry entry) { m_entries.push_back(std::move(entry)); }
        void Remove(std::uint64_t id);

        // Dismissal callbacks are deferred to Flush so an overlay never frees mid-dispatch.
        void RequestDismiss(std::uint64_t id, DismissReason reason);
        /// Request dismiss of the topmost Escape-eligible surface; true if one was queued.
        bool DismissTopmostEscapable();
        /// Content root of the topmost focus-trapping surface (modal/drawer, or a portal with PortalOptions::trapsFocus), or an invalid NodeId; scopes Tab traversal.
        [[nodiscard]] NodeId TopmostTrappingContent() const noexcept;
        /// If `hitLeaf` is outside every open overlay's content, request dismiss of all light-dismiss
        /// surfaces (a press inside any overlay keeps them all open).
        void LightDismissOutside(NodeArena& arena, NodeId hitLeaf);

        void AddPositioner(PositionerEntry entry) { m_positioners.push_back(std::move(entry)); }
        void RemovePositioner(NodeId panel);
        /// Re-derives every open anchored surface's placement from the just-solved rects and patches its inset. Returns true if any panel moved (caller re-solves once).
        bool RunPositioners(NodeArena& arena);

        /// Fire queued onDismiss callbacks at a safe point (after the graph Tick).
        void Flush();
        void SetSurface(Vec2 surface) noexcept { m_surface = surface; }
        [[nodiscard]] Vec2 Surface() const noexcept { return m_surface; }
        [[nodiscard]] bool AnyOpen() const noexcept { return !m_entries.empty(); }

    private:
        [[nodiscard]] static constexpr std::size_t Index(Layer layer) noexcept
        {
            return static_cast<std::size_t>(layer);
        }
        [[nodiscard]] const PortalEntry* Find(std::uint64_t id) const noexcept;

        std::array<NodeId, static_cast<std::size_t>(Layer::Count)> m_layerNodes{};
        std::vector<PortalEntry> m_entries; ///< open order
        std::vector<PositionerEntry> m_positioners;
        std::vector<std::pair<std::uint64_t, DismissReason>> m_pendingDismiss;
        std::uint64_t m_nextId = 1;
        std::uint64_t m_nextSeq = 1;
        Vec2 m_surface{};
    };

    /// One overlay layer: a full-surface Stack portals splice into; as a child of the root Stack it is an absolute containing block, so portal content resolves in surface coords.
    [[nodiscard]] Elem BuildOverlayLayer();
} // namespace lumora
