#include "Style/Cascade.h"

#include <algorithm>
#include <array>
#include <unordered_set>

namespace lumora::style
{
    void StyleIndex::Add(const Rule& rule)
    {
        std::unordered_set<uint64_t> seen; // dedupe class buckets across this rule's selectors
        bool anyClassless = false;
        for (const Selector& sel : rule.selectors)
        {
            const CompoundSelector& key = sel.Key();
            if (key.classes.empty())
            {
                anyClassless = true;
                continue;
            }
            for (StringId cls : key.classes)
            {
                if (seen.insert(cls.Value()).second) byClass[cls].push_back(&rule);
            }
        }
        if (anyClassless) universal.push_back(&rule);
    }

    StyleIndex BuildIndex(const Stylesheet& sheet)
    {
        StyleIndex index;
        for (const Rule& r : sheet.rules) index.Add(r);
        return index;
    }

    namespace
    {
        struct Win
        {
            CssValue value;
            bool important = false;
            uint32_t spec = 0;
            uint32_t order = 0;
            uint32_t index = 0;
            bool set = false;
            bool custom = false;
        };

        // Later-or-equal order wins at equal specificity/importance, matching CSS.
        [[nodiscard]] bool Beats(const Win& cand, const Win& cur)
        {
            if (!cur.set) return true;
            if (cand.important != cur.important) return cand.important;
            if (cand.spec != cur.spec) return cand.spec > cur.spec;
            if (cand.order != cur.order) return cand.order > cur.order;
            return cand.index >= cur.index;
        }

        struct ShorthandFamily
        {
            StringId shorthand;
            std::array<StringId, 4> longhands;
        };

        // Shorthand and longhands cascade as separate keys; a longhand outranked by its shorthand must not survive
        // (`.a { margin-left: 4px } .a.b { margin: 0 }`).
        const std::array kShorthandFamilies{
            ShorthandFamily{
                "margin"_sid, {"margin-top"_sid, "margin-right"_sid, "margin-bottom"_sid, "margin-left"_sid}
            },
            ShorthandFamily{
                "padding"_sid, {"padding-top"_sid, "padding-right"_sid, "padding-bottom"_sid, "padding-left"_sid}
            },
            ShorthandFamily{"inset"_sid, {"top"_sid, "right"_sid, "bottom"_sid, "left"_sid}},
            ShorthandFamily{"flex"_sid, {"flex-grow"_sid, "flex-shrink"_sid, "flex-basis"_sid, StringId{}}},
        };

        void DropOutrankedLonghands(std::unordered_map<StringId, Win, StringIdHash>& best)
        {
            for (const ShorthandFamily& family : kShorthandFamilies)
            {
                const auto shorthand = best.find(family.shorthand);
                if (shorthand == best.end()) continue;
                for (const StringId longhand : family.longhands)
                {
                    if (longhand == StringId{}) continue;
                    const auto it = best.find(longhand);
                    if (it != best.end() && !Beats(it->second, shorthand->second)) best.erase(it);
                }
            }
        }

        void ApplyRule(const Rule& rule, NodeHandle node, const MatchOps& ops,
                       std::unordered_map<StringId, Win, StringIdHash>& best)
        {
            uint32_t matchedSpec = 0;
            bool matched = false;
            for (const Selector& sel : rule.selectors)
            {
                if (MatchesSelector(sel, node, ops))
                {
                    matched = true;
                    matchedSpec = std::max(matchedSpec, sel.specificity);
                }
            }
            if (!matched) return;

            for (uint32_t index = 0; const Declaration& decl : rule.declarations)
            {
                Win cand{decl.value, decl.important, matchedSpec, rule.order, index++, true, decl.custom};
                Win& cur = best[decl.prop];
                if (Beats(cand, cur)) cur = cand;
            }
        }
    } // namespace

    ComputedStyle Cascade(NodeHandle node, const MatchOps& ops, const StyleIndex& index,
                          const std::vector<const Rule*>& extra)
    {
        std::unordered_map<StringId, Win, StringIdHash> best;

        for (const Rule* r : index.universal) ApplyRule(*r, node, ops, best);
        for (StringId cls : ops.classes(node))
        {
            auto it = index.byClass.find(cls);
            if (it != index.byClass.end())
                for (const Rule* r : it->second) ApplyRule(*r, node, ops, best);
        }
        for (const Rule* r : extra) ApplyRule(*r, node, ops, best);
        DropOutrankedLonghands(best);

        ComputedStyle out;
        for (auto& [prop, win] : best)
        {
            if (win.custom) out.customProps.emplace(prop, std::move(win.value));
            else out.props.emplace(prop, std::move(win.value));
        }
        return out;
    }
} // namespace lumora::style
