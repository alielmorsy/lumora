#include "Graph/State.h"

#include <cstring>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace lumora::nodegraph
{
    namespace
    {
        constexpr char          kMagic[4] = {'D', 'N', 'G', 'R'};
        /// 2 inserts the frame table between the nodes and the links; version 1 blobs restore with no
        /// frames. 3 appends Node::height to each node; older blobs restore with height 0.
        constexpr std::uint32_t kVersion = 3u;

        struct Writer
        {
            std::vector<std::byte> bytes;

            void Raw(const void* data, std::size_t size)
            {
                const auto* first = static_cast<const std::byte*>(data);
                bytes.insert(bytes.end(), first, first + size);
            }

            template <typename T>
            void Pod(const T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                Raw(&value, sizeof(T));
            }

            void Str(std::string_view text)
            {
                Pod(static_cast<std::uint32_t>(text.size()));
                Raw(text.data(), text.size());
            }
        };

        /// Bounds-checked cursor. Every read that would run past the end sets `bad`, and Restore
        /// refuses the whole blob rather than committing a partial parse.
        struct Reader
        {
            std::span<const std::byte> bytes;
            std::size_t                at = 0;
            bool                       bad = false;

            bool Raw(void* out, std::size_t size)
            {
                if (bad || at + size > bytes.size())
                {
                    bad = true;
                    return false;
                }
                std::memcpy(out, bytes.data() + at, size);
                at += size;
                return true;
            }

            template <typename T>
            [[nodiscard]] T Pod()
            {
                static_assert(std::is_trivially_copyable_v<T>);
                T value{};
                Raw(&value, sizeof(T));
                return value;
            }

            [[nodiscard]] std::string Str()
            {
                const std::uint32_t length = Pod<std::uint32_t>();
                if (bad || at + length > bytes.size())
                {
                    bad = true;
                    return {};
                }
                std::string text(reinterpret_cast<const char*>(bytes.data() + at), length);
                at += length;
                return text;
            }
        };
    } // namespace

    std::vector<std::byte> Controller::Serialize() const
    {
        const State& state = *m_state;
        Writer       writer;

        writer.Raw(kMagic, sizeof(kMagic));
        writer.Pod(kVersion);
        writer.Pod(state.camera.Pan().x);
        writer.Pod(state.camera.Pan().y);
        writer.Pod(state.camera.Zoom());

        // Type table: one entry per distinct type actually used, so the blob carries no dead names.
        std::vector<std::string>                         sources;
        std::unordered_map<std::uint64_t, std::uint32_t> typeIndex;
        std::vector<GraphNodeId>                         order;

        for (std::uint32_t slot = 0; slot < state.model.SlotCount(); ++slot)
        {
            const GraphNodeId id = state.model.At(slot);
            if (!id.Valid())
            {
                continue;
            }
            order.push_back(id);
            const NodeTypeId type = state.model.Get(id)->type;
            if (!typeIndex.contains(type.value))
            {
                typeIndex[type.value] = static_cast<std::uint32_t>(sources.size());
                sources.emplace_back(state.registry.SourceOf(type));
            }
        }

        writer.Pod(static_cast<std::uint32_t>(sources.size()));
        for (const std::string& source : sources)
        {
            writer.Str(source);
        }

        std::unordered_map<std::uint64_t, std::uint32_t> nodeIndex;
        const auto key = [](GraphNodeId id)
        { return (static_cast<std::uint64_t>(id.index) << 32) | id.gen; };

        writer.Pod(static_cast<std::uint32_t>(order.size()));
        for (std::uint32_t i = 0; i < order.size(); ++i)
        {
            const GraphNodeId id = order[i];
            const Node&       node = *state.model.Get(id);
            nodeIndex[key(id)] = i;

            writer.Pod(typeIndex.at(node.type.value));
            writer.Pod(node.position.x);
            writer.Pod(node.position.y);
            writer.Pod(node.width);
            writer.Str(node.title);
            writer.Pod(static_cast<std::uint8_t>(node.collapsed ? 1 : 0));
            writer.Pod(node.openGroups);
            for (const float height : node.groupHeight)
            {
                writer.Pod(height);
            }
            writer.Pod(node.userData);
            writer.Pod(node.height);
        }

        const std::span<const Frame> frames = state.model.Frames();
        writer.Pod(static_cast<std::uint32_t>(frames.size()));
        for (const Frame& frame : frames)
        {
            writer.Pod(frame.rect.x);
            writer.Pod(frame.rect.y);
            writer.Pod(frame.rect.width);
            writer.Pod(frame.rect.height);
            writer.Str(frame.title);
            writer.Pod(frame.color);
            std::vector<std::uint32_t> members;
            for (const GraphNodeId id : frame.members)
            {
                if (const auto it = nodeIndex.find(key(id)); it != nodeIndex.end())
                {
                    members.push_back(it->second);
                }
            }
            writer.Pod(static_cast<std::uint32_t>(members.size()));
            for (const std::uint32_t index : members)
            {
                writer.Pod(index);
            }
        }

        std::vector<const Link*> links;
        for (std::uint32_t slot = 0; slot < state.model.LinkSlotCount(); ++slot)
        {
            const LinkId id = state.model.LinkAt(slot);
            if (id.Valid())
            {
                links.push_back(state.model.Get(id));
            }
        }

        writer.Pod(static_cast<std::uint32_t>(links.size()));
        for (const Link* link : links)
        {
            writer.Pod(nodeIndex.at(key(link->from.node)));
            writer.Pod(link->from.slot);
            writer.Pod(nodeIndex.at(key(link->to.node)));
            writer.Pod(link->to.slot);
        }

        writer.Pod(static_cast<std::uint32_t>(state.selection.size()));
        for (const GraphNodeId id : state.selection)
        {
            const auto it = nodeIndex.find(key(id));
            writer.Pod(it == nodeIndex.end() ? 0xFFFFFFFFu : it->second);
        }

        return std::move(writer.bytes);
    }

    bool Controller::Restore(std::span<const std::byte> data)
    {
        State& state = *m_state;
        Reader reader{data};

        char magic[4] = {};
        if (!reader.Raw(magic, sizeof(magic)) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0)
        {
            return false;
        }
        const std::uint32_t version = reader.Pod<std::uint32_t>();
        if (reader.bad || version < 1u || version > kVersion)
        {
            return false;
        }

        const float panX = reader.Pod<float>();
        const float panY = reader.Pod<float>();
        const float zoom = reader.Pod<float>();

        const std::uint32_t typeCount = reader.Pod<std::uint32_t>();
        if (reader.bad)
        {
            return false;
        }
        std::vector<NodeTypeId> types;
        types.reserve(typeCount);
        for (std::uint32_t i = 0; i < typeCount && !reader.bad; ++i)
        {
            const std::string source = reader.Str();
            const NodeTypeId  id{source};
            // An unregistered type resolves to an invalid id, which drops its nodes below.
            types.push_back(state.registry.Find(id) != nullptr ? id : NodeTypeId{});
        }
        if (reader.bad)
        {
            return false;
        }

        const std::uint32_t nodeCount = reader.Pod<std::uint32_t>();
        if (reader.bad)
        {
            return false;
        }
        std::vector<Node> parsed;
        std::vector<bool> keep;
        parsed.reserve(nodeCount);
        keep.reserve(nodeCount);

        for (std::uint32_t i = 0; i < nodeCount && !reader.bad; ++i)
        {
            const std::uint32_t typeSlot = reader.Pod<std::uint32_t>();
            Node                node;
            node.position.x = reader.Pod<float>();
            node.position.y = reader.Pod<float>();
            node.width = reader.Pod<float>();
            node.title = reader.Str();
            node.collapsed = reader.Pod<std::uint8_t>() != 0;
            node.openGroups = reader.Pod<std::uint32_t>();
            for (float& height : node.groupHeight)
            {
                height = reader.Pod<float>();
            }
            node.userData = reader.Pod<std::uint64_t>();
            if (version >= 3u)
            {
                node.height = reader.Pod<float>();
            }

            const bool known = typeSlot < types.size() && types[typeSlot].Valid();
            node.type = known ? types[typeSlot] : NodeTypeId{};
            parsed.push_back(std::move(node));
            keep.push_back(known);
        }
        if (reader.bad)
        {
            return false;
        }

        struct ParsedFrame
        {
            Frame                      frame;
            std::vector<std::uint32_t> members;
        };
        std::vector<ParsedFrame> parsedFrames;
        if (version >= 2u)
        {
            const std::uint32_t frameCount = reader.Pod<std::uint32_t>();
            for (std::uint32_t i = 0; i < frameCount && !reader.bad; ++i)
            {
                ParsedFrame parsedFrame;
                Frame&      frame = parsedFrame.frame;
                frame.rect.x = reader.Pod<float>();
                frame.rect.y = reader.Pod<float>();
                frame.rect.width = reader.Pod<float>();
                frame.rect.height = reader.Pod<float>();
                frame.title = reader.Str();
                frame.color = reader.Pod<Color>();
                const std::uint32_t memberCount = reader.Pod<std::uint32_t>();
                for (std::uint32_t m = 0; m < memberCount && !reader.bad; ++m)
                {
                    parsedFrame.members.push_back(reader.Pod<std::uint32_t>());
                }
                parsedFrames.push_back(std::move(parsedFrame));
            }
            if (reader.bad)
            {
                return false;
            }
        }

        const std::uint32_t linkCount = reader.Pod<std::uint32_t>();
        if (reader.bad)
        {
            return false;
        }
        struct ParsedLink
        {
            std::uint32_t from = 0;
            std::uint16_t fromSlot = 0;
            std::uint32_t to = 0;
            std::uint16_t toSlot = 0;
        };
        std::vector<ParsedLink> parsedLinks;
        parsedLinks.reserve(linkCount);
        for (std::uint32_t i = 0; i < linkCount && !reader.bad; ++i)
        {
            ParsedLink link;
            link.from = reader.Pod<std::uint32_t>();
            link.fromSlot = reader.Pod<std::uint16_t>();
            link.to = reader.Pod<std::uint32_t>();
            link.toSlot = reader.Pod<std::uint16_t>();
            parsedLinks.push_back(link);
        }
        if (reader.bad)
        {
            return false;
        }

        const std::uint32_t selectionCount = reader.Pod<std::uint32_t>();
        if (reader.bad)
        {
            return false;
        }
        std::vector<std::uint32_t> parsedSelection;
        parsedSelection.reserve(selectionCount);
        for (std::uint32_t i = 0; i < selectionCount && !reader.bad; ++i)
        {
            parsedSelection.push_back(reader.Pod<std::uint32_t>());
        }
        if (reader.bad)
        {
            return false;
        }

        // Everything validated - only now touch the live graph.
        state.model = Model{};
        state.history.Clear();
        state.selection.clear();
        state.selectedLink = LinkId{};

        std::vector<GraphNodeId> handles(parsed.size(), GraphNodeId{});
        for (std::size_t i = 0; i < parsed.size(); ++i)
        {
            if (keep[i])
            {
                handles[i] = state.model.AddRaw(parsed[i]);
            }
        }

        for (const ParsedLink& link : parsedLinks)
        {
            if (link.from >= handles.size() || link.to >= handles.size())
            {
                continue;
            }
            const GraphNodeId from = handles[link.from];
            const GraphNodeId to = handles[link.to];
            if (!from.Valid() || !to.Valid())
            {
                continue;
            }
            state.model.Connect(PinRef{from, link.fromSlot, PinDir::Output},
                                PinRef{to, link.toSlot, PinDir::Input});
        }

        for (const std::uint32_t index : parsedSelection)
        {
            if (index < handles.size() && handles[index].Valid())
            {
                state.selection.push_back(handles[index]);
            }
        }

        for (ParsedFrame& parsedFrame : parsedFrames)
        {
            for (const std::uint32_t index : parsedFrame.members)
            {
                if (index < handles.size() && handles[index].Valid())
                {
                    parsedFrame.frame.members.push_back(handles[index]);
                }
            }
            state.model.AddFrame(std::move(parsedFrame.frame));
        }

        state.camera.SetPan(Vec2{panX, panY});
        state.camera.SetZoom(zoom);
        state.Invalidate();

        if (onSelectionChanged)
        {
            onSelectionChanged();
        }
        if (onModelChanged)
        {
            onModelChanged();
        }
        return true;
    }
} // namespace lumora::nodegraph
