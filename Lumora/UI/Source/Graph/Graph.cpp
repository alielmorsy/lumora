#include "Graph/State.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace lumora::nodegraph
{
    namespace
    {
        /// Open a transaction only when the caller has not already opened one, so an interactive
        /// gesture stays a single undo step while a one-shot call is still atomic.
        struct Scope
        {
            State& state;
            bool   owned = false;

            Scope(State& s, std::string_view label) : state(s)
            {
                if (!s.history.Open())
                {
                    s.history.Begin(label);
                    owned = true;
                }
            }

            ~Scope()
            {
                if (owned)
                {
                    state.history.Commit();
                }
            }
        };

        [[nodiscard]] std::vector<GraphNodeId> NodesInside(State& s, const Rect& rect)
        {
            s.EnsureLayout();
            std::vector<GraphNodeId> inside;
            for (std::uint32_t slot = 0; slot < s.model.SlotCount(); ++slot)
            {
                const GraphNodeId id = s.model.At(slot);
                const NodeGeom*   geom = s.layout.Of(id);
                if (geom != nullptr && rect.Contains(geom->rect.Center()))
                {
                    inside.push_back(id);
                }
            }
            return inside;
        }

        /// Apply `change` to a frame as one recorded edit. False when the frame is gone.
        template <typename Change>
        bool MutateFrame(State& s, FrameId id, std::string_view label, Change&& change)
        {
            Frame* frame = s.model.GetFrame(id);
            if (frame == nullptr)
            {
                return false;
            }
            Scope       scope(s, label);
            const Frame before = *frame;
            change(*frame);
            s.history.Record(
                Edit{.kind = Edit::Kind::MutateFrame, .frameBefore = before, .frameAfter = *frame});
            s.Touch();
            return true;
        }

        [[nodiscard]] bool Holds(const Frame& frame, GraphNodeId id)
        {
            return std::ranges::find(frame.members, id) != frame.members.end();
        }

        [[nodiscard]] bool Encloses(const Rect& outer, const Rect& inner) noexcept
        {
            return inner.x >= outer.x && inner.y >= outer.y && inner.Right() <= outer.Right()
                && inner.Bottom() <= outer.Bottom();
        }

        [[nodiscard]] std::vector<FrameId> FrameIds(const State& s)
        {
            std::vector<FrameId> ids;
            for (const Frame& frame : s.model.Frames())
            {
                ids.push_back(frame.id);
            }
            return ids;
        }

        void Leave(State& s, FrameId frame, GraphNodeId id)
        {
            MutateFrame(s, frame, "Leave frame", [id](Frame& f) { std::erase(f.members, id); });
        }

        void Enter(State& s, FrameId frame, GraphNodeId id)
        {
            MutateFrame(s, frame, "Join frame", [id](Frame& f) { f.members.push_back(id); });
        }

        /// `base` grown to enclose `frame`'s live members plus padding (and the header on top). Layout
        /// must be current.
        [[nodiscard]] Rect GrowAround(const State& s, const Rect& base, const Frame& frame)
        {
            const Rect members = s.layout.BoundsOf(frame.members);
            if (members.width <= 0.f || members.height <= 0.f)
            {
                return base;
            }
            const float pad = s.theme.FramePadding;
            const float left = std::min(base.x, members.x - pad);
            const float top = std::min(base.y, members.y - pad - s.theme.FrameHeaderHeight);
            const float right = std::max(base.Right(), members.Right() + pad);
            const float bottom = std::max(base.Bottom(), members.Bottom() + pad);
            return Rect{left, top, right - left, bottom - top};
        }
    } // namespace

    void CaptureFrames(State& s, std::span<const GraphNodeId> nodes)
    {
        s.followFrames.clear();
        for (const Frame& frame : s.model.Frames())
        {
            if (std::ranges::any_of(nodes, [&frame](GraphNodeId id) { return Holds(frame, id); }))
            {
                s.followFrames.emplace_back(frame.id, frame.rect);
            }
        }
    }

    void FollowFrames(State& s, bool alt)
    {
        s.EnsureLayout();
        for (const auto& [id, base] : s.followFrames)
        {
            const Frame* frame = s.model.GetFrame(id);
            if (frame == nullptr)
            {
                continue;
            }
            const Rect target = alt ? base : GrowAround(s, base, *frame);
            if (!(target == frame->rect))
            {
                MutateFrame(s, id, "Grow frame", [&target](Frame& f) { f.rect = target; });
            }
        }
    }

    void JoinFrameAt(State& s, GraphNodeId node, Vec2 canvasPoint)
    {
        const std::span<const Frame> frames = s.model.Frames();
        if (std::ranges::any_of(frames, [node](const Frame& f) { return Holds(f, node); }))
        {
            return;
        }
        for (auto it = frames.rbegin(); it != frames.rend(); ++it)
        {
            if (it->rect.Contains(canvasPoint))
            {
                Enter(s, it->id, node);
                return;
            }
        }
    }

    void SettleFrames(State& s, std::span<const GraphNodeId> dragged, bool alt)
    {
        s.EnsureLayout();
        if (alt)
        {
            for (const GraphNodeId id : dragged)
            {
                const NodeGeom* geom = s.layout.Of(id);
                if (geom == nullptr)
                {
                    continue;
                }
                const Vec2 centre = geom->rect.Center();
                for (const FrameId frameId : FrameIds(s))
                {
                    const Frame* frame = s.model.GetFrame(frameId);
                    if (Holds(*frame, id) && !frame->rect.Contains(centre))
                    {
                        Leave(s, frameId, id);
                    }
                }
                const std::span<const Frame> frames = s.model.Frames();
                if (std::ranges::any_of(frames, [id](const Frame& f) { return Holds(f, id); }))
                {
                    continue;
                }
                for (auto it = frames.rbegin(); it != frames.rend(); ++it)
                {
                    if (Encloses(it->rect, geom->rect))
                    {
                        Enter(s, it->id, id);
                        break;
                    }
                }
            }
        }

        for (const FrameId frameId : FrameIds(s))
        {
            const Frame* frame = s.model.GetFrame(frameId);
            if (std::ranges::none_of(dragged, [frame](GraphNodeId id) { return Holds(*frame, id); }))
            {
                continue;
            }
            const Rect grown = GrowAround(s, frame->rect, *frame);
            if (!(grown == frame->rect))
            {
                MutateFrame(s, frameId, "Grow frame", [&grown](Frame& f) { f.rect = grown; });
            }
        }
    }

    Controller::Controller() : m_state(MakeRef<State>()) {}
    Controller::~Controller() = default;
    Controller::Controller(const Controller&) = default;
    Controller::Controller(Controller&&) noexcept = default;
    Controller& Controller::operator=(const Controller&) = default;
    Controller& Controller::operator=(Controller&&) noexcept = default;

    Signal<std::uint64_t>& Controller::Revision() { return m_state->revision; }
    Registry&    Controller::Types() { return m_state->registry; }
    Theme&       Controller::Visuals() { return m_state->theme; }
    LinkPolicy&  Controller::Policy() { return m_state->policy; }
    Camera&      Controller::View() { return m_state->camera; }
    const Model& Controller::Data() const { return m_state->model; }

    NodeTypeId Controller::Type(std::string_view id, NodeTypeDesc desc)
    {
        const NodeTypeId type = m_state->registry.Register(id, std::move(desc));
        m_state->Invalidate();
        return type;
    }

    GraphNodeId Controller::AddNode(NodeTypeId type, Vec2 canvasPos)
    {
        State& s = *m_state;
        Scope  scope(s, "Add node");

        const GraphNodeId id = s.model.Add(type, canvasPos);
        s.history.Record(Edit{.kind = Edit::Kind::AddNode, .node = *s.model.Get(id), .nodeId = id});
        s.Invalidate();
        if (!s.model.Frames().empty())
        {
            s.EnsureLayout();
            if (const NodeGeom* geom = s.layout.Of(id))
            {
                JoinFrameAt(s, id, geom->rect.Center());
            }
        }
        if (onNodeAdded)
        {
            onNodeAdded(id);
        }
        if (onModelChanged)
        {
            onModelChanged();
        }
        return id;
    }

    void Controller::RemoveNode(GraphNodeId id)
    {
        State& s = *m_state;
        if (!s.model.Alive(id))
        {
            return;
        }
        Scope scope(s, "Delete node");

        // Model::Remove drops incident links silently, so record them first or undo loses them.
        std::vector<LinkId> incident;
        s.model.LinksOf(id, incident);
        for (const LinkId link : incident)
        {
            const Link& edge = *s.model.Get(link);
            s.history.Record(Edit{.kind = Edit::Kind::Disconnect,
                                  .from = edge.from,
                                  .to = edge.to,
                                  .linkId = link});
        }

        s.history.Record(Edit{.kind = Edit::Kind::RemoveNode, .node = *s.model.Get(id), .nodeId = id});
        s.model.Remove(id);
        s.PruneSelection();
        s.Invalidate();
        // Fire link hooks only after the model reflects the removal, so a host calling Data() sees the link gone.
        for (const LinkId link : incident)
        {
            if (onLinkRemoved)
            {
                onLinkRemoved(link);
            }
        }
        if (onNodeRemoved)
        {
            onNodeRemoved(id);
        }
        if (onModelChanged)
        {
            onModelChanged();
        }
    }

    void Controller::RemoveSelection()
    {
        State& s = *m_state;
        if (s.selection.empty() && !s.selectedLink.Valid())
        {
            return;
        }
        Scope scope(s, "Delete");

        const std::vector<GraphNodeId> doomed = s.selection;
        for (const GraphNodeId id : doomed)
        {
            RemoveNode(id);
        }
        if (s.selectedLink.Valid())
        {
            Disconnect(s.selectedLink);
        }
    }

    LinkId Controller::Connect(PinRef from, PinRef to)
    {
        State& s = *m_state;
        if (!s.model.CanConnect(s.registry, s.policy, from, to) || !s.PinShown(from) || !s.PinShown(to))
        {
            return {};
        }
        Scope scope(s, "Connect");

        const PinDesc* inPin = s.registry.Pin(s.model.Get(to.node)->type, to.slot);
        if (inPin != nullptr && !inPin->multi)
        {
            if (const LinkId occupant = s.model.LinkOnInput(to); occupant.Valid())
            {
                const Link& edge = *s.model.Get(occupant);
                s.history.Record(Edit{.kind = Edit::Kind::Disconnect,
                                      .from = edge.from,
                                      .to = edge.to,
                                      .linkId = occupant});
                s.model.Disconnect(occupant);
                // The evicted link may have been selected; a dead handle must never stay in the selection.
                s.PruneSelection();
                if (onLinkRemoved)
                {
                    onLinkRemoved(occupant);
                }
            }
        }

        const LinkId id = s.model.Connect(from, to);
        if (!id.Valid())
        {
            return {};
        }
        s.history.Record(Edit{.kind = Edit::Kind::Connect, .from = from, .to = to, .linkId = id});
        s.Touch();
        if (onLinkAdded)
        {
            onLinkAdded(id);
        }
        if (onModelChanged)
        {
            onModelChanged();
        }
        return id;
    }

    void Controller::Disconnect(LinkId id)
    {
        State& s = *m_state;
        if (!s.model.Alive(id))
        {
            return;
        }
        Scope scope(s, "Disconnect");

        const Link& edge = *s.model.Get(id);
        s.history.Record(
            Edit{.kind = Edit::Kind::Disconnect, .from = edge.from, .to = edge.to, .linkId = id});
        s.model.Disconnect(id);
        s.PruneSelection();
        s.Touch();
        if (onLinkRemoved)
        {
            onLinkRemoved(id);
        }
        if (onModelChanged)
        {
            onModelChanged();
        }
    }

    void Controller::MoveNodes(std::span<const GraphNodeId> ids, Vec2 canvasDelta)
    {
        State& s = *m_state;
        Scope  scope(s, "Move");

        for (const GraphNodeId id : ids)
        {
            Node* node = s.model.Get(id);
            if (node == nullptr)
            {
                continue;
            }
            const Node before = *node;
            node->position += canvasDelta;
            if (s.theme.SnapSize > 0.f)
            {
                const float grid = s.theme.SnapSize;
                node->position = Vec2{std::round(node->position.x / grid) * grid,
                                      std::round(node->position.y / grid) * grid};
            }
            s.history.Record(Edit{.kind = Edit::Kind::Mutate,
                                  .nodeId = id,
                                  .before = before,
                                  .after = *node});
        }
        s.Invalidate();
    }

    void Controller::SetNodeWidth(GraphNodeId id, float width)
    {
        State& s = *m_state;
        Node*  node = s.model.Get(id);
        if (node == nullptr)
        {
            return;
        }
        const NodeTypeDesc* desc = s.registry.Find(node->type);
        const float         floor = desc != nullptr ? DefaultWidth(*desc, s.theme) : 80.f;

        Scope      scope(s, "Resize node");
        const Node before = *node;
        node->width = std::max(width, floor);
        s.history.Record(
            Edit{.kind = Edit::Kind::Mutate, .nodeId = id, .before = before, .after = *node});
        s.Invalidate();
    }

    void Controller::SetNodeSize(GraphNodeId id, Vec2 size)
    {
        State& s = *m_state;
        Node*  node = s.model.Get(id);
        if (node == nullptr)
        {
            return;
        }
        const NodeTypeDesc* desc = s.registry.Find(node->type);
        const float         floor = desc != nullptr ? DefaultWidth(*desc, s.theme) : 80.f;

        Scope      scope(s, "Resize node");
        const Node before = *node;
        node->width = size.x > 0.f ? std::max(size.x, floor) : 0.f;
        s.Invalidate();
        s.EnsureLayout();
        if (const NodeGeom* geom = s.layout.Of(id); geom == nullptr || !geom->collapsed)
        {
            const float content = geom != nullptr ? geom->contentHeight : 0.f;
            node->height = size.y > content ? size.y : 0.f;
        }
        s.history.Record(
            Edit{.kind = Edit::Kind::Mutate, .nodeId = id, .before = before, .after = *node});
        s.Invalidate();
    }

    void Controller::SetCollapsed(GraphNodeId id, bool collapsed)
    {
        State& s = *m_state;
        Node*  node = s.model.Get(id);
        if (node == nullptr || node->collapsed == collapsed)
        {
            return;
        }
        if (const NodeTypeDesc* desc = s.registry.Find(node->type); desc != nullptr && desc->compact)
        {
            return;
        }
        Scope      scope(s, collapsed ? "Collapse" : "Expand");
        const Node before = *node;
        node->collapsed = collapsed;
        s.history.Record(
            Edit{.kind = Edit::Kind::Mutate, .nodeId = id, .before = before, .after = *node});
        s.Invalidate();
    }

    void Controller::SetGroupOpen(GraphNodeId id, int group, bool open)
    {
        State& s = *m_state;
        Node*  node = s.model.Get(id);
        if (node == nullptr || group < 0 || group >= 32)
        {
            return;
        }
        const std::uint32_t bit = 1u << group;
        if (((node->openGroups & bit) != 0u) == open)
        {
            return;
        }
        Scope      scope(s, open ? "Expand group" : "Collapse group");
        const Node before = *node;
        node->openGroups = open ? (node->openGroups | bit) : (node->openGroups & ~bit);
        s.history.Record(
            Edit{.kind = Edit::Kind::Mutate, .nodeId = id, .before = before, .after = *node});
        s.Invalidate();
    }

    void Controller::SetGroupHeight(GraphNodeId id, int group, float height)
    {
        State& s = *m_state;
        Node*  node = s.model.Get(id);
        if (node == nullptr || group < 0 || group >= 4)
        {
            return;
        }
        Scope      scope(s, "Resize group");
        const Node before = *node;
        node->groupHeight[group] = std::max(height, 0.f);
        s.history.Record(
            Edit{.kind = Edit::Kind::Mutate, .nodeId = id, .before = before, .after = *node});
        s.Invalidate();
    }

    void Controller::SetUserData(GraphNodeId id, std::uint64_t userData)
    {
        State& s    = *m_state;
        Node*  node = s.model.Get(id);
        if (node == nullptr || node->userData == userData)
        {
            return;
        }
        Scope      scope(s, "Set node data");
        const Node before = *node;
        node->userData = userData;
        s.history.Record(
            Edit{.kind = Edit::Kind::Mutate, .nodeId = id, .before = before, .after = *node});
        s.Touch();
    }

    void Controller::SetNodeTitle(GraphNodeId id, std::string_view title)
    {
        State& s    = *m_state;
        Node*  node = s.model.Get(id);
        if (node == nullptr || node->title == title)
        {
            return;
        }
        Scope      scope(s, "Rename node");
        const Node before = *node;
        node->title = std::string(title);
        s.history.Record(
            Edit{.kind = Edit::Kind::Mutate, .nodeId = id, .before = before, .after = *node});
        s.Touch();
        if (onModelChanged)
        {
            onModelChanged();
        }
    }

    std::vector<GraphNodeId> Controller::Duplicate(std::span<const GraphNodeId> ids, Vec2 offset)
    {
        State& s = *m_state;
        Scope  scope(s, "Duplicate");

        std::unordered_map<std::uint64_t, GraphNodeId> mapping;
        std::vector<GraphNodeId>                       copies;
        copies.reserve(ids.size());

        const auto key = [](GraphNodeId id)
        { return (static_cast<std::uint64_t>(id.index) << 32) | id.gen; };

        for (const GraphNodeId id : ids)
        {
            const Node* source = s.model.Get(id);
            if (source == nullptr)
            {
                continue;
            }
            Node copy = *source;
            copy.position += offset;
            const GraphNodeId fresh = s.model.AddRaw(copy);
            s.history.Record(
                Edit{.kind = Edit::Kind::AddNode, .node = copy, .nodeId = fresh});
            mapping[key(id)] = fresh;
            copies.push_back(fresh);
            if (onNodeAdded)
            {
                onNodeAdded(fresh);
            }
        }

        for (std::uint32_t slot = 0; slot < s.model.LinkSlotCount(); ++slot)
        {
            const LinkId link = s.model.LinkAt(slot);
            if (!link.Valid())
            {
                continue;
            }
            const Link edge = *s.model.Get(link);
            const auto fromIt = mapping.find(key(edge.from.node));
            const auto toIt = mapping.find(key(edge.to.node));
            if (fromIt == mapping.end() || toIt == mapping.end())
            {
                continue;
            }
            PinRef from = edge.from;
            PinRef to = edge.to;
            from.node = fromIt->second;
            to.node = toIt->second;
            const LinkId fresh = s.model.Connect(from, to);
            if (fresh.Valid())
            {
                s.history.Record(
                    Edit{.kind = Edit::Kind::Connect, .from = from, .to = to, .linkId = fresh});
                if (onLinkAdded)
                {
                    onLinkAdded(fresh);
                }
            }
        }

        s.Invalidate();
        if (!s.model.Frames().empty())
        {
            s.EnsureLayout();
            for (const GraphNodeId copy : copies)
            {
                if (const NodeGeom* geom = s.layout.Of(copy))
                {
                    JoinFrameAt(s, copy, geom->rect.Center());
                }
            }
        }
        if (onModelChanged)
        {
            onModelChanged();
        }
        return copies;
    }

    FrameId Controller::AddFrame(std::string_view title, Color color)
    {
        State& s = *m_state;
        s.EnsureLayout();
        const Rect bounds = s.layout.BoundsOf(s.selection);
        if (bounds.width <= 0.f || bounds.height <= 0.f)
        {
            return {};
        }
        const float pad = s.theme.FramePadding;
        const float header = s.theme.FrameHeaderHeight;
        return AddFrame(Rect{bounds.x - pad, bounds.y - pad - header, bounds.width + pad * 2.f,
                             bounds.height + pad * 2.f + header},
                        title, color);
    }

    FrameId Controller::AddFrame(const Rect& canvasRect, std::string_view title, Color color)
    {
        State& s = *m_state;
        Scope  scope(s, "Add frame");

        const FrameId id = s.model.AddFrame(Frame{.rect = canvasRect,
                                                  .title = std::string(title),
                                                  .color = color,
                                                  .members = NodesInside(s, canvasRect)});
        s.history.Record(Edit{.kind = Edit::Kind::AddFrame, .frameAfter = *s.model.GetFrame(id)});
        s.Touch();
        if (onModelChanged)
        {
            onModelChanged();
        }
        return id;
    }

    void Controller::RemoveFrame(FrameId id)
    {
        State&       s = *m_state;
        const Frame* frame = s.model.GetFrame(id);
        if (frame == nullptr)
        {
            return;
        }
        Scope scope(s, "Delete frame");
        s.history.Record(Edit{.kind = Edit::Kind::RemoveFrame, .frameBefore = *frame});
        s.model.RemoveFrame(id);
        if (s.activeFrame == id)
        {
            s.activeFrame = FrameId{};
        }
        s.Touch();
        if (onModelChanged)
        {
            onModelChanged();
        }
    }

    void Controller::SetFrameTitle(FrameId id, std::string_view title)
    {
        if (MutateFrame(*m_state, id, "Rename frame",
                        [title](Frame& frame) { frame.title = std::string(title); })
            && onModelChanged)
        {
            onModelChanged();
        }
    }

    void Controller::SetFrameColor(FrameId id, Color color)
    {
        if (MutateFrame(*m_state, id, "Frame colour", [color](Frame& frame) { frame.color = color; })
            && onModelChanged)
        {
            onModelChanged();
        }
    }

    void Controller::SetFrameRect(FrameId id, const Rect& canvasRect)
    {
        MutateFrame(*m_state, id, "Resize frame",
                    [&canvasRect](Frame& frame) { frame.rect = canvasRect; });
    }

    void Controller::MoveFrame(FrameId id, Vec2 canvasDelta)
    {
        State&       s = *m_state;
        const Frame* frame = s.model.GetFrame(id);
        if (frame == nullptr)
        {
            return;
        }
        Scope                     scope(s, "Move frame");
        const std::vector<GraphNodeId> members = frame->members;
        MutateFrame(s, id, "Move frame", [canvasDelta](Frame& f)
                    { f.rect = Rect{f.rect.x + canvasDelta.x, f.rect.y + canvasDelta.y, f.rect.width,
                                    f.rect.height}; });
        MoveNodes(members, canvasDelta);
    }

    void Controller::RefreshFrameMembers(FrameId id)
    {
        State&       s = *m_state;
        const Frame* frame = s.model.GetFrame(id);
        if (frame == nullptr)
        {
            return;
        }
        std::vector<GraphNodeId> inside = NodesInside(s, frame->rect);
        if (inside == frame->members)
        {
            return;
        }
        MutateFrame(s, id, "Frame members",
                    [&inside](Frame& f) { f.members = std::move(inside); });
    }

    void Controller::RemoveFromFrame(GraphNodeId id)
    {
        State& s = *m_state;
        const std::vector<FrameId> holders = [&s, id]
        {
            std::vector<FrameId> out;
            for (const Frame& frame : s.model.Frames())
            {
                if (Holds(frame, id))
                {
                    out.push_back(frame.id);
                }
            }
            return out;
        }();
        if (holders.empty())
        {
            return;
        }
        Scope scope(s, "Remove from frame");
        for (const FrameId frame : holders)
        {
            Leave(s, frame, id);
        }
        if (onModelChanged)
        {
            onModelChanged();
        }
    }

    std::span<const Frame> Controller::Frames() const { return m_state->model.Frames(); }

    std::span<const GraphNodeId> Controller::Selection() const { return m_state->selection; }

    void Controller::Select(std::span<const GraphNodeId> ids, bool additive)
    {
        State& s = *m_state;
        if (!additive)
        {
            s.selection.clear();
        }
        for (const GraphNodeId id : ids)
        {
            if (!s.model.Alive(id))
            {
                continue;
            }
            if (std::find(s.selection.begin(), s.selection.end(), id) == s.selection.end())
            {
                s.selection.push_back(id);
            }
        }
        s.selectedLink = LinkId{};
        s.Touch();
        if (onSelectionChanged)
        {
            onSelectionChanged();
        }
    }

    void Controller::ToggleSelected(GraphNodeId id)
    {
        State& s = *m_state;
        const auto it = std::find(s.selection.begin(), s.selection.end(), id);
        if (it != s.selection.end())
        {
            s.selection.erase(it);
        }
        else if (s.model.Alive(id))
        {
            s.selection.push_back(id);
        }
        s.Touch();
        if (onSelectionChanged)
        {
            onSelectionChanged();
        }
    }

    void Controller::ClearSelection()
    {
        State& s = *m_state;
        if (s.selection.empty() && !s.selectedLink.Valid())
        {
            return;
        }
        s.selection.clear();
        s.selectedLink = LinkId{};
        s.Touch();
        if (onSelectionChanged)
        {
            onSelectionChanged();
        }
    }

    bool Controller::IsSelected(GraphNodeId id) const
    {
        const State& s = *m_state;
        return std::find(s.selection.begin(), s.selection.end(), id) != s.selection.end();
    }

    void Controller::SelectLink(LinkId id)
    {
        State& s = *m_state;
        s.selection.clear();
        s.selectedLink = s.model.Alive(id) ? id : LinkId{};
        s.Touch();
        if (onSelectionChanged)
        {
            onSelectionChanged();
        }
    }

    LinkId Controller::SelectedLink() const { return m_state->selectedLink; }

    void Controller::SetViewport(const Rect& layerRect) { m_state->viewport = layerRect; }

    const Rect& Controller::Viewport() const { return m_state->viewport; }

    Rect Controller::CanvasToSurface(const Rect& canvasRect) const
    {
        const State& s = *m_state;
        const Rect   layer = s.camera.ToLayer(canvasRect, s.viewport);
        return Rect{layer.x - s.surfaceToLayer.x, layer.y - s.surfaceToLayer.y, layer.width, layer.height};
    }

    Rect Controller::CellRect(GraphNodeId node, std::uint16_t cellIndex) const
    {
        State& s = *m_state;
        s.EnsureLayout();
        const NodeGeom* geom = s.layout.Of(node);
        if (geom == nullptr)
        {
            return {};
        }
        const auto it = std::ranges::find(geom->cells, static_cast<int>(cellIndex), &CellGeom::bodyIndex);
        return it != geom->cells.end() ? it->rect : Rect{};
    }

    void Controller::FitAll()
    {
        State& s = *m_state;
        s.EnsureLayout();
        s.camera.Fit(s.layout.Bounds(), s.viewport);
        s.Touch();
    }

    void Controller::FitSelection()
    {
        State& s = *m_state;
        s.EnsureLayout();
        s.camera.Fit(s.layout.BoundsOf(s.selection), s.viewport);
        s.Touch();
    }

    void Controller::Repaint() { m_state->Invalidate(); }

    void Controller::Begin(std::string_view label) { m_state->history.Begin(label); }
    void Controller::Commit() { m_state->history.Commit(); }

    void Controller::Abort()
    {
        State& s = *m_state;
        s.history.Abort(s.model);
        s.PruneSelection();
        s.Invalidate();
    }

    void Controller::Undo()
    {
        State& s = *m_state;
        if (!s.history.Undo(s.model))
        {
            return;
        }
        s.PruneSelection();
        s.Invalidate();
        if (onModelChanged)
        {
            onModelChanged();
        }
    }

    void Controller::Redo()
    {
        State& s = *m_state;
        if (!s.history.Redo(s.model))
        {
            return;
        }
        s.PruneSelection();
        s.Invalidate();
        if (onModelChanged)
        {
            onModelChanged();
        }
    }

    bool Controller::CanUndo() const { return m_state->history.CanUndo(); }
    bool Controller::CanRedo() const { return m_state->history.CanRedo(); }
} // namespace lumora::nodegraph
