#include "Graph/Search.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace lumora::nodegraph
{
    namespace
    {
        constexpr int kWordStartBonus = 8;
        constexpr int kRunBonus = 4;
        constexpr int kLeadingBonus = 6;

        [[nodiscard]] char Lower(char c) noexcept
        {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        [[nodiscard]] bool WordStart(std::string_view text, std::size_t i) noexcept
        {
            if (i == 0)
            {
                return true;
            }
            const auto prev = static_cast<unsigned char>(text[i - 1]);
            const auto cur = static_cast<unsigned char>(text[i]);
            return std::isalnum(prev) == 0 || (std::islower(prev) != 0 && std::isupper(cur) != 0);
        }

        [[nodiscard]] std::uint16_t ProbePins(const Model& scratch, const Registry& reg,
                                              const LinkPolicy& policy, PinRef dragged, GraphNodeId probe,
                                              const NodeTypeDesc& desc)
        {
            for (std::uint16_t slot = 0; slot < desc.pins.size(); ++slot)
            {
                const PinDesc& pin = desc.pins[slot];
                if (pin.dir == dragged.dir)
                {
                    continue;
                }
                const PinRef other{probe, slot, pin.dir};
                const bool   legal = dragged.dir == PinDir::Output
                                       ? scratch.CanConnect(reg, policy, dragged, other)
                                       : scratch.CanConnect(reg, policy, other, dragged);
                if (legal)
                {
                    return slot;
                }
            }
            return NoPin;
        }
    } // namespace

    int FuzzyScore(std::string_view query, std::string_view text)
    {
        std::string needle;
        for (const char c : query)
        {
            if (c != ' ')
            {
                needle.push_back(Lower(c));
            }
        }
        if (needle.empty())
        {
            return 0;
        }

        int         score = 0;
        std::size_t matched = 0;
        std::size_t last = std::string_view::npos;
        for (std::size_t i = 0; i < text.size() && matched < needle.size(); ++i)
        {
            if (Lower(text[i]) != needle[matched])
            {
                continue;
            }
            score += 1;
            if (WordStart(text, i))
            {
                score += kWordStartBonus;
            }
            if (last != std::string_view::npos && i == last + 1)
            {
                score += kRunBonus;
            }
            if (i == 0)
            {
                score += kLeadingBonus;
            }
            last = i;
            ++matched;
        }
        if (matched < needle.size())
        {
            return -1;
        }
        return std::max(score - static_cast<int>(text.size()) / 8, 0);
    }

    int ScoreFields(std::string_view query, std::string_view title, std::string_view category,
                    std::span<const std::string> keywords)
    {
        int best = -1;
        if (const int s = FuzzyScore(query, title); s >= 0)
        {
            best = s * 2;
        }
        for (const std::string& keyword : keywords)
        {
            if (const int s = FuzzyScore(query, keyword); s >= 0)
            {
                best = std::max(best, s);
            }
        }
        if (const int s = FuzzyScore(query, category); s >= 0)
        {
            best = std::max(best, s / 2);
        }
        return best;
    }

    int ScoreType(std::string_view query, const NodeTypeDesc& desc)
    {
        return ScoreFields(query, desc.title, desc.category, desc.keywords);
    }

    std::vector<NodeTypeId> RankTypes(const Registry& reg, std::span<const NodeTypeId> types,
                                      std::string_view query)
    {
        std::vector<std::pair<int, NodeTypeId>> scored;
        for (const NodeTypeId type : types)
        {
            const NodeTypeDesc* desc = reg.Find(type);
            if (desc == nullptr)
            {
                continue;
            }
            if (const int score = ScoreType(query, *desc); score >= 0)
            {
                scored.emplace_back(score, type);
            }
        }
        std::ranges::stable_sort(scored, std::greater{}, [](const auto& entry) { return entry.first; });

        std::vector<NodeTypeId> ranked;
        ranked.reserve(scored.size());
        for (const auto& [score, type] : scored)
        {
            ranked.push_back(type);
        }
        return ranked;
    }

    std::uint16_t FirstCompatiblePin(const Model& model, const Registry& reg, const LinkPolicy& policy,
                                     PinRef dragged, NodeTypeId type)
    {
        const NodeTypeDesc* desc = reg.Find(type);
        if (desc == nullptr || !model.Alive(dragged.node))
        {
            return NoPin;
        }
        // A scratch copy with a probe node of the candidate type, so the policy sees a real node.
        Model             scratch = model;
        const GraphNodeId probe = scratch.Add(type, Vec2{});
        return ProbePins(scratch, reg, policy, dragged, probe, *desc);
    }

    std::vector<NodeTypeId> CompatibleTypes(const Model& model, const Registry& reg,
                                            const LinkPolicy& policy, PinRef dragged,
                                            std::span<const NodeTypeId> types)
    {
        std::vector<NodeTypeId> out;
        if (!model.Alive(dragged.node))
        {
            return out;
        }
        Model             scratch = model;
        const GraphNodeId probe = scratch.Add(NodeTypeId{}, Vec2{});
        for (const NodeTypeId type : types)
        {
            const NodeTypeDesc* desc = reg.Find(type);
            if (desc == nullptr)
            {
                continue;
            }
            scratch.Get(probe)->type = type;
            if (ProbePins(scratch, reg, policy, dragged, probe, *desc) != NoPin)
            {
                out.push_back(type);
            }
        }
        return out;
    }
} // namespace lumora::nodegraph
