#pragma once

#include "Style/ComputedStyle.h"
#include "Style/Matcher.h"

#include <lumora/Style/Stylesheet.h>

#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lumora::style
{
    /// Rules bucketed by the class ids of their rightmost compound; class-less rules (`*`, `:root`, pseudo-only) go in a
    /// universal bucket. A rule in several buckets is harmless (re-applying a declaration is idempotent).
    struct StyleIndex
    {
        std::unordered_map<StringId, std::vector<const Rule*>, StringIdHash> byClass;
        std::vector<const Rule*> universal;

        void Add(const Rule& rule);
    };

    /// Index a sheet's top-level rules; passing `@media` groups are supplied separately via `extra`.
    [[nodiscard]] StyleIndex BuildIndex(const Stylesheet& sheet);

    /// Winner per property is by (!important, specificity, source order); `extra` adds rules of passing `@media` groups.
    [[nodiscard]] ComputedStyle Cascade(NodeHandle node, const MatchOps& ops, const StyleIndex& index,
                                        const std::vector<const Rule*>& extra = {});

    /// Declarations of an inline `style` string (`a: b; c: d`); malformed ones are skipped, never throws.
    [[nodiscard]] std::vector<Declaration> ParseInlineDeclarations(std::string_view css);

    /// Write inline declarations over a cascaded style: they beat every rule value except an `!important` one. A shorthand
    /// replaces the rule's longhands of its family, and an `!important` shorthand shields its longhands.
    void ApplyDeclarations(ComputedStyle& cs, std::span<const Declaration> decls);
} // namespace lumora::style
