#include <lumora/Graph/Types.h>

namespace lumora::nodegraph
{
    namespace
    {
        [[nodiscard]] std::uint64_t HashSource(std::string_view s) noexcept
        {
            const std::uint64_t h = std::hash<std::string_view>{}(s);
            return h == 0 ? 1 : h;
        }
    } // namespace

    NodeTypeId::NodeTypeId(std::string_view source) : value(HashSource(source)) {}

    PinTypeId::PinTypeId(std::string_view source) : value(HashSource(source)) {}

    NodeTypeId Registry::Register(std::string_view id, NodeTypeDesc desc)
    {
        const NodeTypeId key{id};
        const bool fresh = !m_types.contains(key.value);
        m_types[key.value] = std::move(desc);
        m_sources[key.value] = std::string(id);
        if (fresh)
        {
            m_order.push_back(key);
        }
        return key;
    }

    const NodeTypeDesc* Registry::Find(NodeTypeId id) const
    {
        const auto it = m_types.find(id.value);
        return it == m_types.end() ? nullptr : &it->second;
    }

    std::string_view Registry::SourceOf(NodeTypeId id) const
    {
        const auto it = m_sources.find(id.value);
        return it == m_sources.end() ? std::string_view{} : std::string_view{it->second};
    }

    std::span<const NodeTypeId> Registry::All() const { return m_order; }

    const PinDesc* Registry::Pin(NodeTypeId type, std::uint16_t slot) const
    {
        const NodeTypeDesc* desc = Find(type);
        if (desc == nullptr || slot >= desc->pins.size())
        {
            return nullptr;
        }
        return &desc->pins[slot];
    }
} // namespace lumora::nodegraph
