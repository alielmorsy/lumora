#include <lumora/Graph/Model.h>

#include <algorithm>
#include <utility>

namespace lumora::nodegraph
{
    namespace
    {
        [[nodiscard]] bool SlotLive(const std::vector<std::uint32_t>& gens, std::uint32_t slot) noexcept
        {
            return slot < gens.size() && (gens[slot] & 1u) != 0u;
        }
    } // namespace

    GraphNodeId Model::Add(NodeTypeId type, Vec2 position)
    {
        Node node;
        node.type = type;
        node.position = position;
        return AddRaw(std::move(node));
    }

    GraphNodeId Model::AddRaw(Node node)
    {
        std::uint32_t slot;
        if (!m_freeNodes.empty())
        {
            slot = m_freeNodes.back();
            m_freeNodes.pop_back();
            m_nodes[slot] = std::move(node);
        }
        else
        {
            slot = static_cast<std::uint32_t>(m_nodes.size());
            m_nodes.push_back(std::move(node));
            m_nodeGen.push_back(0u);
        }
        ++m_nodeGen[slot];
        ++m_nodeCount;
        return GraphNodeId{slot, m_nodeGen[slot]};
    }

    bool Model::Remove(GraphNodeId id)
    {
        if (!Alive(id))
        {
            return false;
        }
        for (std::uint32_t i = 0; i < LinkSlotCount(); ++i)
        {
            if (!SlotLive(m_linkGen, i))
            {
                continue;
            }
            const Link& link = m_links[i];
            if (link.from.node == id || link.to.node == id)
            {
                Disconnect(LinkId{i, m_linkGen[i]});
            }
        }
        m_nodes[id.index] = Node{};
        ++m_nodeGen[id.index];
        m_freeNodes.push_back(id.index);
        --m_nodeCount;
        return true;
    }

    bool Model::Alive(GraphNodeId id) const
    {
        return SlotLive(m_nodeGen, id.index) && m_nodeGen[id.index] == id.gen;
    }

    Node* Model::Get(GraphNodeId id) { return Alive(id) ? &m_nodes[id.index] : nullptr; }

    const Node* Model::Get(GraphNodeId id) const { return Alive(id) ? &m_nodes[id.index] : nullptr; }

    std::uint32_t Model::SlotCount() const { return static_cast<std::uint32_t>(m_nodes.size()); }

    GraphNodeId Model::At(std::uint32_t slot) const
    {
        return SlotLive(m_nodeGen, slot) ? GraphNodeId{slot, m_nodeGen[slot]} : GraphNodeId{};
    }

    LinkId Model::Connect(PinRef from, PinRef to)
    {
        if (from.dir != PinDir::Output || to.dir != PinDir::Input)
        {
            return {};
        }
        if (!Alive(from.node) || !Alive(to.node) || from.node == to.node)
        {
            return {};
        }
        for (std::uint32_t i = 0; i < LinkSlotCount(); ++i)
        {
            if (SlotLive(m_linkGen, i) && m_links[i].from == from && m_links[i].to == to)
            {
                return {};
            }
        }

        std::uint32_t slot;
        if (!m_freeLinks.empty())
        {
            slot = m_freeLinks.back();
            m_freeLinks.pop_back();
            m_links[slot] = Link{from, to};
        }
        else
        {
            slot = static_cast<std::uint32_t>(m_links.size());
            m_links.push_back(Link{from, to});
            m_linkGen.push_back(0u);
        }
        ++m_linkGen[slot];
        ++m_linkCount;
        return LinkId{slot, m_linkGen[slot]};
    }

    bool Model::Disconnect(LinkId id)
    {
        if (!Alive(id))
        {
            return false;
        }
        m_links[id.index] = Link{};
        ++m_linkGen[id.index];
        m_freeLinks.push_back(id.index);
        --m_linkCount;
        return true;
    }

    bool Model::Alive(LinkId id) const
    {
        return SlotLive(m_linkGen, id.index) && m_linkGen[id.index] == id.gen;
    }

    const Link* Model::Get(LinkId id) const { return Alive(id) ? &m_links[id.index] : nullptr; }

    std::uint32_t Model::LinkSlotCount() const { return static_cast<std::uint32_t>(m_links.size()); }

    LinkId Model::LinkAt(std::uint32_t slot) const
    {
        return SlotLive(m_linkGen, slot) ? LinkId{slot, m_linkGen[slot]} : LinkId{};
    }

    bool Model::CanConnect(const Registry& reg, const LinkPolicy& policy, PinRef from, PinRef to) const
    {
        if (from.dir != PinDir::Output || to.dir != PinDir::Input)
        {
            return false;
        }
        if (!Alive(from.node) || !Alive(to.node) || from.node == to.node)
        {
            return false;
        }

        const PinDesc* outPin = reg.Pin(m_nodes[from.node.index].type, from.slot);
        const PinDesc* inPin  = reg.Pin(m_nodes[to.node.index].type, to.slot);
        if (outPin == nullptr || inPin == nullptr)
        {
            return false;
        }
        if (outPin->dir != PinDir::Output || inPin->dir != PinDir::Input)
        {
            return false;
        }

        if (policy.canConnect)
        {
            if (!policy.canConnect(*this, from, to))
            {
                return false;
            }
        }
        else if (!(outPin->type == inPin->type))
        {
            return false;
        }

        for (std::uint32_t i = 0; i < LinkSlotCount(); ++i)
        {
            if (SlotLive(m_linkGen, i) && m_links[i].from == from && m_links[i].to == to)
            {
                return false;
            }
        }

        if (policy.acyclic && Reaches(to.node, from.node))
        {
            return false;
        }
        return true;
    }

    LinkId Model::LinkOnInput(PinRef input) const
    {
        for (std::uint32_t i = 0; i < LinkSlotCount(); ++i)
        {
            if (SlotLive(m_linkGen, i) && m_links[i].to == input)
            {
                return LinkId{i, m_linkGen[i]};
            }
        }
        return {};
    }

    void Model::LinksOf(GraphNodeId id, std::vector<LinkId>& out) const
    {
        out.clear();
        for (std::uint32_t i = 0; i < LinkSlotCount(); ++i)
        {
            if (!SlotLive(m_linkGen, i))
            {
                continue;
            }
            if (m_links[i].from.node == id || m_links[i].to.node == id)
            {
                out.push_back(LinkId{i, m_linkGen[i]});
            }
        }
    }

    bool Model::Reaches(GraphNodeId from, GraphNodeId to) const
    {
        if (!Alive(from) || !Alive(to))
        {
            return false;
        }
        if (from == to)
        {
            return true;
        }
        std::vector<bool> seen(m_nodes.size(), false);
        std::vector<std::uint32_t> stack{from.index};
        seen[from.index] = true;

        while (!stack.empty())
        {
            const std::uint32_t current = stack.back();
            stack.pop_back();
            for (std::uint32_t i = 0; i < LinkSlotCount(); ++i)
            {
                if (!SlotLive(m_linkGen, i) || m_links[i].from.node.index != current)
                {
                    continue;
                }
                const std::uint32_t next = m_links[i].to.node.index;
                if (next == to.index)
                {
                    return true;
                }
                if (!seen[next])
                {
                    seen[next] = true;
                    stack.push_back(next);
                }
            }
        }
        return false;
    }

    std::uint32_t Model::NodeCount() const { return m_nodeCount; }

    std::uint32_t Model::LinkCount() const { return m_linkCount; }

    FrameId Model::AddFrame(Frame frame)
    {
        if (!frame.id.Valid() || GetFrame(frame.id) != nullptr)
        {
            frame.id = FrameId{m_nextFrame};
        }
        m_nextFrame = std::max(m_nextFrame, frame.id.value + 1);
        const FrameId id = frame.id;
        const auto    at = std::ranges::lower_bound(m_frames, id.value, {},
                                                    [](const Frame& f) { return f.id.value; });
        m_frames.insert(at, std::move(frame));
        return id;
    }

    bool Model::RemoveFrame(FrameId id)
    {
        return std::erase_if(m_frames, [id](const Frame& f) { return f.id == id; }) != 0;
    }

    Frame* Model::GetFrame(FrameId id)
    {
        const auto it = std::ranges::find(m_frames, id, &Frame::id);
        return id.Valid() && it != m_frames.end() ? &*it : nullptr;
    }

    const Frame* Model::GetFrame(FrameId id) const
    {
        const auto it = std::ranges::find(m_frames, id, &Frame::id);
        return id.Valid() && it != m_frames.end() ? &*it : nullptr;
    }

    std::span<const Frame> Model::Frames() const { return m_frames; }

    void Model::RemapNode(GraphNodeId from, GraphNodeId to)
    {
        for (Frame& frame : m_frames)
        {
            std::ranges::replace(frame.members, from, to);
        }
    }
} // namespace lumora::nodegraph
