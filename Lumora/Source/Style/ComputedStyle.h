#pragma once

#include <lumora/StringId.h>
#include <lumora/Style/CssValue.h>

#include <unordered_map>

namespace lumora::style
{
    /// Winning declared value per property for one node; values stay symbolic (var()/units unresolved).
    struct ComputedStyle
    {
        std::unordered_map<StringId, CssValue, StringIdHash> props;
        std::unordered_map<StringId, CssValue, StringIdHash> customProps;

        [[nodiscard]] const CssValue* Get(StringId prop) const
        {
            auto it = props.find(prop);
            return it == props.end() ? nullptr : &it->second;
        }

        [[nodiscard]] bool Has(StringId prop) const { return props.contains(prop); }

        [[nodiscard]] bool operator==(const ComputedStyle&) const = default;
    };

    using VarMap = std::unordered_map<StringId, CssValue, StringIdHash>;

    /// What a node publishes to descendants: resolved custom-property map plus inherited props (color, font-*, line-height,
    /// text-align). Equality-compared to gate the inheritEpoch bump.
    struct InheritedStyle
    {
        VarMap vars;
        std::unordered_map<StringId, CssValue, StringIdHash> props;

        [[nodiscard]] bool operator==(const InheritedStyle&) const = default;
    };
} // namespace lumora::style
