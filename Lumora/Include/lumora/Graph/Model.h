#pragma once

#include <lumora/Api.h>
#include <lumora/Graph/Types.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace lumora::nodegraph
{
    class Model;

    /// A node instance. Everything here serializes; anything that needs a std::function lives on the
    /// type descriptor instead.
    struct Node
    {
        NodeTypeId    type{};
        /// Canvas space.
        Vec2          position{};
        /// 0 takes NodeTypeDesc::minWidth.
        float         width = 0.f;
        /// Title override; empty takes NodeTypeDesc::title.
        std::string   title;
        bool          collapsed = false;
        /// One bit per GroupCell in body order; all open by default.
        std::uint32_t openGroups = ~0u;
        /// Drag-resized heights for the first four resizable groups (0 = size from children). Capped at four to keep Node cheap to copy.
        float         groupHeight[4]{};
        /// The caller's payload key (e.g. a guid or pass index). Opaque here.
        std::uint64_t userData = 0;
        /// 0 sizes to the content; otherwise the node is max(content, height) tall. Collapsed nodes
        /// ignore it.
        float         height = 0.f;
    };

    /// A directed edge. `from` is always an Output, `to` always an Input.
    struct Link
    {
        PinRef from{};
        PinRef to{};
    };

    /// A titled rectangle behind nodes; `members` move with it and are sticky (a dragged member stays one and the frame
    /// grows around it). Leave via Alt-release outside or RemoveFromFrame; join via Alt-release inside or add with centre
    /// inside. A member handle may be dead so undo can restore the node.
    struct Frame
    {
        FrameId                  id{};
        /// Canvas space; the header strip is the top Theme::FrameHeaderHeight of it.
        Rect                     rect{};
        std::string              title;
        /// Transparent takes Theme::FrameDefault.
        Color                    color = Color::Transparent;
        std::vector<GraphNodeId> members;
    };

    /// The domain's half of link legality. The Model owns structure; this owns meaning.
    struct LinkPolicy
    {
        /// Type compatibility. Null requires identical PinTypeId.
        std::function<bool(const Model&, PinRef from, PinRef to)> canConnect;
        /// Pin and link colour by type. Null takes Theme::PinDefault / Theme::LinkDefault.
        std::function<Color(PinTypeId)> pinColor;
        /// Refuse edges that would close a loop; false for blueprint-style graphs where loops are intended.
        bool acyclic = true;
    };

    /// Nodes, links, and every structural rule. Depends on Registry for pin descriptors and on nothing
    /// else - no Lumora runtime, no paint, no camera - so it is testable headless.
    class LUMORA_API Model
    {
    public:
        GraphNodeId Add(NodeTypeId type, Vec2 position);
        /// Insert a fully-specified node, preserving every field. For Restore and paste.
        GraphNodeId AddRaw(Node node);
        /// Remove a node and every link touching it. False when the handle is already dead.
        bool Remove(GraphNodeId id);

        [[nodiscard]] bool             Alive(GraphNodeId id) const;
        [[nodiscard]] Node*            Get(GraphNodeId id);
        [[nodiscard]] const Node*      Get(GraphNodeId id) const;

        /// Slot-space iteration: walk 0..SlotCount() and skip the handles At() reports invalid.
        [[nodiscard]] std::uint32_t SlotCount() const;
        [[nodiscard]] GraphNodeId   At(std::uint32_t slot) const;

        /// Structural connect only (rejects dead endpoints, same-direction, self and duplicate links). Does NOT replace a
        /// single-connect input's occupant: the controller does, in the same transaction. Invalid id on refusal; gate on CanConnect.
        LinkId Connect(PinRef from, PinRef to);
        bool   Disconnect(LinkId id);

        [[nodiscard]] bool        Alive(LinkId id) const;
        [[nodiscard]] const Link* Get(LinkId id) const;
        [[nodiscard]] std::uint32_t LinkSlotCount() const;
        [[nodiscard]] LinkId        LinkAt(std::uint32_t slot) const;

        /// Full gate: structure, pin directions, policy (or type equality), acyclic. Does NOT fail on an occupied
        /// single-connect input (the controller replaces it).
        [[nodiscard]] bool CanConnect(const Registry& reg, const LinkPolicy& policy,
                                      PinRef from, PinRef to) const;

        /// The link currently occupying `input`, or an invalid id.
        [[nodiscard]] LinkId LinkOnInput(PinRef input) const;
        /// Every live link with either endpoint on `id`. Clears `out` first.
        void LinksOf(GraphNodeId id, std::vector<LinkId>& out) const;
        /// Is `to` reachable from `from` via outputs? Iterative and visited-marked, so cyclic graphs terminate.
        [[nodiscard]] bool Reaches(GraphNodeId from, GraphNodeId to) const;

        [[nodiscard]] std::uint32_t NodeCount() const;
        [[nodiscard]] std::uint32_t LinkCount() const;

        /// Insert a frame. Keeps `frame.id` when it is valid and unused (undo, restore), otherwise
        /// mints a fresh one. Frames stay ordered by id, which is their paint order.
        FrameId AddFrame(Frame frame);
        bool    RemoveFrame(FrameId id);
        [[nodiscard]] Frame*       GetFrame(FrameId id);
        [[nodiscard]] const Frame* GetFrame(FrameId id) const;
        [[nodiscard]] std::span<const Frame> Frames() const;
        /// Substitute `to` for `from` in every frame's members - the node handle history re-minted.
        void RemapNode(GraphNodeId from, GraphNodeId to);

    private:
        /// A slot is free when its generation is even; Add bumps it odd. That makes a default-
        /// constructed handle (gen 0) permanently invalid without a separate liveness array.
        std::vector<Node>          m_nodes;
        std::vector<std::uint32_t> m_nodeGen;
        std::vector<std::uint32_t> m_freeNodes;

        std::vector<Link>          m_links;
        std::vector<std::uint32_t> m_linkGen;
        std::vector<std::uint32_t> m_freeLinks;

        std::uint32_t m_nodeCount = 0;
        std::uint32_t m_linkCount = 0;

        std::vector<Frame> m_frames;
        std::uint32_t      m_nextFrame = 1;
    };
}
