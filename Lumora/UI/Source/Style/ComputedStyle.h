#pragma once

#include <lumora/StringId.h>
#include <lumora/Style/CssValue.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace lumora::style
{
    /// StringId -> CssValue as a sorted vector: per-node maps hold a handful of entries, so one contiguous block beats a hash
    /// table's node-per-entry (and, on MSVC, its allocations even when empty). Inserts invalidate iterators and pointers.
    class PropMap
    {
    public:
        using value_type = std::pair<StringId, CssValue>;
        using iterator = std::vector<value_type>::iterator;
        using const_iterator = std::vector<value_type>::const_iterator;

        [[nodiscard]] iterator begin() noexcept { return m_items.begin(); }
        [[nodiscard]] iterator end() noexcept { return m_items.end(); }
        [[nodiscard]] const_iterator begin() const noexcept { return m_items.begin(); }
        [[nodiscard]] const_iterator end() const noexcept { return m_items.end(); }
        [[nodiscard]] std::size_t size() const noexcept { return m_items.size(); }
        [[nodiscard]] bool empty() const noexcept { return m_items.empty(); }
        void clear() noexcept { m_items.clear(); }

        [[nodiscard]] iterator find(StringId key)
        {
            const auto it = LowerBound(key);
            return it != m_items.end() && it->first == key ? it : m_items.end();
        }
        [[nodiscard]] const_iterator find(StringId key) const { return const_cast<PropMap*>(this)->find(key); }
        [[nodiscard]] bool contains(StringId key) const { return find(key) != end(); }

        CssValue& operator[](StringId key)
        {
            const auto it = LowerBound(key);
            if (it != m_items.end() && it->first == key)
            {
                return it->second;
            }
            return m_items.emplace(it, key, CssValue{})->second;
        }

        std::pair<iterator, bool> emplace(StringId key, CssValue value)
        {
            const auto it = LowerBound(key);
            if (it != m_items.end() && it->first == key)
            {
                return {it, false};
            }
            return {m_items.emplace(it, key, std::move(value)), true};
        }

        std::size_t erase(StringId key)
        {
            const auto it = find(key);
            if (it == m_items.end())
            {
                return 0;
            }
            m_items.erase(it);
            return 1;
        }

        [[nodiscard]] bool operator==(const PropMap&) const = default;

    private:
        [[nodiscard]] iterator LowerBound(StringId key)
        {
            return std::lower_bound(m_items.begin(), m_items.end(), key,
                                    [](const value_type& item, StringId k) { return item.first < k; });
        }

        std::vector<value_type> m_items;
    };

    /// Winning declared value per property for one node; values stay symbolic (var()/units unresolved).
    struct ComputedStyle
    {
        PropMap props;
        PropMap customProps;
        /// Properties (custom ones included) whose winning value came from an `!important` rule; inline CSS may not override them.
        std::vector<StringId> important;

        [[nodiscard]] const CssValue* Get(StringId prop) const
        {
            auto it = props.find(prop);
            return it == props.end() ? nullptr : &it->second;
        }

        [[nodiscard]] bool Has(StringId prop) const { return props.contains(prop); }

        [[nodiscard]] bool operator==(const ComputedStyle&) const = default;
    };

    using VarMap = PropMap;

    /// What a node publishes to descendants: resolved custom-property map plus inherited props (color, font-*, line-height,
    /// text-align). Equality-compared to gate the inheritEpoch bump.
    struct InheritedStyle
    {
        VarMap vars;
        PropMap props;

        [[nodiscard]] bool operator==(const InheritedStyle&) const = default;
    };
} // namespace lumora::style
