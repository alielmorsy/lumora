#pragma once

#include <lumora/StringId.h>
#include <lumora/Style/CssValue.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lumora::style
{
    /// The five supported pseudo-classes, packed into a bitmask on a compound selector.
    enum class Pseudo : uint8_t
    {
        Hover = 1 << 0,
        Active = 1 << 1,
        Focus = 1 << 2,
        Disabled = 1 << 3,
        Checked = 1 << 4,
        /// A drag is in flight and this node is the target that accepted its payload. Spelled `:drop-active`.
        DropActive = 1 << 5,
    };

    using PseudoMask = uint8_t;

    [[nodiscard]] constexpr PseudoMask operator|(Pseudo a, Pseudo b) noexcept
    {
        return static_cast<PseudoMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    [[nodiscard]] constexpr bool HasPseudo(PseudoMask mask, Pseudo p) noexcept
    {
        return (mask & static_cast<uint8_t>(p)) != 0;
    }

    /// One compound selector: classes AND-ed with pseudo-classes, e.g. `.card.active:hover`. `*` sets `universal`.
    struct CompoundSelector
    {
        std::vector<StringId> classes;
        PseudoMask pseudo = 0;
        bool universal = false;
        bool root = false; ///< the `:root` pseudo-class - matches only the app root node

        [[nodiscard]] bool operator==(const CompoundSelector&) const = default;
    };

    enum class Combinator : uint8_t
    {
        Descendant, ///< `.a .b`
        Child,      ///< `.a > .b`
    };

    /// A full selector: compounds left to right with a combinator between each pair (combinators.size() == compounds.size() - 1).
    struct Selector
    {
        std::vector<CompoundSelector> compounds;
        std::vector<Combinator> combinators;
        uint32_t specificity = 0; ///< classes-only model: total (#classes + #pseudo) across compounds
        bool matchesRoot = false; ///< the `:root` selector

        [[nodiscard]] const CompoundSelector& Key() const noexcept { return compounds.back(); }
    };

    struct Declaration
    {
        StringId prop{};    ///< interned property name (e.g. "background-color"_sid, or "--brand"_sid)
        CssValue value{};
        bool important = false;
        bool custom = false; ///< a custom property (`--x`); StringId is lossy so this is flagged at parse

        [[nodiscard]] bool operator==(const Declaration&) const = default;
    };

    /// One style rule; several selectors are matched independently, each with its own specificity.
    struct Rule
    {
        std::vector<Selector> selectors;
        std::vector<Declaration> declarations;
        uint32_t order = 0; ///< source order across the whole sheet, for cascade tie-breaking
    };

    struct KeyframeStop
    {
        float offset = 0.f; ///< 0..1 (from/`0%` = 0, to/`100%` = 1)
        std::vector<Declaration> declarations;
    };

    struct KeyframesDef
    {
        StringId name{};
        std::vector<KeyframeStop> stops;
    };

    struct MediaFeature
    {
        enum class Type : uint8_t { MinWidth, MaxWidth, MinHeight, MaxHeight } type = Type::MinWidth;
        float value = 0.f; ///< px
    };

    /// A conjunction of features - `@media (min-width: 600px) and (max-width: 900px)`.
    struct MediaCondition
    {
        std::vector<MediaFeature> features;
    };

    struct MediaGroup
    {
        MediaCondition condition;
        std::vector<Rule> rules;
    };

    /// Parsed result of one or more `.css` sources: rules flat (nesting desugared), `@media` kept grouped, `@keyframes` indexed
    /// by name, `:root` custom properties collected.
    struct Stylesheet
    {
        std::vector<Rule> rules;
        std::vector<MediaGroup> media;
        std::unordered_map<StringId, KeyframesDef, StringIdHash> keyframes;
        std::vector<Declaration> rootVars;
        uint32_t ruleCount = 0; ///< running source-order counter, also used when merging sheets
    };
}
